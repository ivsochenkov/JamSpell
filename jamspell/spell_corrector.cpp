#include "spell_corrector.hpp"
#include "utils.hpp"

#include <algorithm>
#include <fstream>

namespace NJamSpell 
{

using del1_vec_t = std::vector<str_t>;

static del1_vec_t GetDeletes1(str_view_t const& w) 
{
    del1_vec_t results;
    if(w.size() < 2)        // one-letter candidates? hmmmm
        return results;

    results.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) 
    {
        str_t nw;
        nw.reserve(w.size());
        nw.append(w.substr(0, i)).append (w.substr(i+1));
        results.emplace_back(std::move(nw));
        // if (!nw.empty()) {
        //     results.push_back(std::move(nw));
        // }
    }
    return results;
}

using del2_vec_t = std::vector<del1_vec_t>;

static del2_vec_t GetDeletes2(str_view_t const & w) 
{
    del2_vec_t results;
    if(w.size() < 3) // one letter candiates... Hmmm?
        return results;

    results.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) 
    {
        str_t nw;
        nw.reserve(w.size());
        (nw += w.substr(0, i)) += w.substr( i+1 );
        //if (!nw.empty()) 
        {
            del1_vec_t currResults = GetDeletes1(nw);
            currResults.emplace_back(std::move(nw));
            results.emplace_back(std::move(currResults));
        }
    }
    return results;
}



void TCandMgr::reset_heap_impl ()
{
    static orig_word_greater_by_cnt_t const wheap_order{};
    std::push_heap(m_impl.begin(), m_impl.end(), wheap_order);
    std::pop_heap(m_impl.begin(), m_impl.end(), wheap_order);
}

TSpellCorrector::opt_t TSpellCorrector::opt_t::ReadFromEnv()
{
    opt_t opt{};
    
    setValFromEnv(opt.OrigWordIsKnownPenalty, "SPLL_ORIG_WORD_IS_KNOWN_PEN");
    setValFromEnv(opt.OrigWordIsUnknownPenalty, "SPLL_ORIG_WORD_IS_UNKNOWN_PEN");
    setValFromEnv(opt.SecondLvlPenFactor, "SPLL_SECOND_LVL_PEN_FACTOR");
    setValFromEnv(opt.SecondLvlPenalty, "SPLL_SECOND_LVL_PEN");
    setValFromEnv(opt.SwitchedWordPenalty, "SPLL_SWITCHED_WORD_PEN");
    setValFromEnv(opt.SwitchedWordIsKnownPenalty, "SPLL_SWITCHED_WORD_IS_KNOWN_PEN");

    setValFromEnv(opt.MaxCandidatesToCheck, "SPLL_MAX_CAND");
    
    return opt;
}

TSpellCorrector::TSpellCorrector (opt_t const & opt)
: m_opt{opt}
{
}


bool TSpellCorrector::LoadLangModel(const std::string& modelFile) 
{
    if (!LangModel.Load(modelFile)) 
    {
        return false;
    }
    std::string const & cacheFile = modelFile + ".spell";
    if (!LoadCache(cacheFile)) 
    {
        PrepareCache();
        SaveCache(cacheFile);
    }
    return true;
}

bool TSpellCorrector::TrainLangModel(const std::string& textFile
    , const std::string & dictFile
    , const std::string& alphabetFile
    , const std::string& modelFile
    , TLangModel::train_options_t const & tr_opt
) 
{
    if (!LangModel.Train(textFile, dictFile, alphabetFile, tr_opt)) 
    {
        return false;
    }

    std::cerr << "[info] preparing cache... " << std::endl;
    PrepareCache();
    
    std::cerr << "[info] saving model... " << std::endl;
    if (!LangModel.Dump(modelFile)) 
    {
        return false;
    }

    std::string cacheFile = modelFile + ".spell";
    std::cerr << "[info] saving cache... " << std::endl;
    if (!SaveCache(cacheFile)) 
    {
        return false;
    }
    return true;
}



candidates_t TSpellCorrector::GetCandidates(context_range_t const & context
    , ::std::size_t const position
) const
{
    BOOST_ASSERT_MSG (position < context.size(), "position is out of range!");

    candidates_t candidates;
    cntxt_word_t & orig_word = context[position];

    if(!orig_word.good())
    {
        return candidates;
    }

    attributes_t    attrs;
    attrs.orig_is_known = !orig_word.unknown();
    attrs.prev_was_switched = PrevWordWasSwitched(context, position);
    concat_inf_t cncat_inf = CntNonSpacedNeighbours(context, position);
    attrs.non_spaced_cnt = cncat_inf.left + cncat_inf.right;

    orig_word.score = ScoreOrig(attrs, context, position);
    JS_TRACE_MSG(std::cerr << "[debug] Scored orig: \'" 
        << w_to_u8(FromAlphabet(GetAlphabet(), orig_word.str)) 
        << "\' id = " << static_cast<std::uint32_t> (orig_word.id) 
        << " count = " << orig_word.cnt << " score = " << orig_word.score << "\n"
    );    
    
    TCandMgr cndMgr(candidates, m_opt.MaxCandidatesToCheck);

    // PITIPIWPIW WIW WIW!
    cndMgr.set_kind (ckFirstLvl);
    ::std::size_t const e2_cnt_added = Edits2(orig_word.str, cndMgr);

    if ((!attrs.orig_is_known) || IsInfreq(orig_word) 
        || attrs.non_spaced_cnt
        || attrs.prev_was_switched
        // LISPanyk would be happy
    )
    {
        CheckSwitchedCands(context, position, cndMgr, attrs, cncat_inf);   
    }

    if((!attrs.sw_orig_is_known) 
        && (!e2_cnt_added || CandidatesAreInfreq(cndMgr) )
    )
    {
        cndMgr.set_kind (ckSecondLvl);
        Edits(orig_word.str, cndMgr);        
    }

    Score(attrs, context, position, candidates);
    std::sort(candidates.begin(), candidates.end()
        , [] (cand_word_t const & lhs, cand_word_t const & rhs)
            {return lhs.score > rhs.score;}
    );

    JS_TRACE_MSG(std::cerr << "[debug] Best Candidate: \'" 
        << (candidates.empty() ? std::string{} 
        : w_to_u8(FromAlphabet(GetAlphabet(), candidates.front().str))
        ) << "\' id = " 
        << (candidates.empty() ? 0u : static_cast<std::uint32_t> (candidates.front().id))
        << " count = " << (candidates.empty() ? 0u : candidates.front().cnt)
        << " score = " << (candidates.empty() ? 0.0 : candidates.front().score) 
        << "\n"
    );

    return candidates;
}


/*
std::vector<std::pair<std::wstring,double> > 
TSpellCorrector::GetCandidatesWithScores(
      const std::vector<std::wstring>& sentence
    , size_t const position
    , bool const include_orig
) const 
{
    text_tokens_t txt_tokens(sentence.begin(), sentence.end());
    candidates_t in_sent;
    InitCtxt(txt_tokens, in_sent);

    in_sent = LangModel.InitWords(txt_tokens);
    auto const & scoredCandidates = GetCandidates(in_sent, position);

    std::vector<std::pair<std::wstring,double> > results (scoredCandidates.size());
    auto it = results.begin();
    for (auto s: scoredCandidates) 
    {
        *it++ = std::make_pair(
                FromAlphabet(LangModel.GetAlphabet(), s.str)
            ,   s.score
        );
    }
    return results;
}

std::vector<std::wstring> 
TSpellCorrector::GetCandidates(const std::vector<std::wstring>& sentence
    , size_t const position
    , bool const include_orig
) const 
{
    text_tokens_t txt_tokens(sentence.begin(), sentence.end());
    words_t in_sent = LangModel.InitWords(txt_tokens);

    auto const & scoredCandidates = GetCandidates(in_sent, position, include_orig, include_orig);
    std::vector<std::wstring> results(scoredCandidates.size());
    auto it = results.begin();
    for (auto&& c: scoredCandidates) 
    {
        *it++ = FromAlphabet(LangModel.GetAlphabet(), c.str);
    }
    return results;
}
*/

void TSpellCorrector::FixContext(context_t & cntxt) const
{
    for (auto orig_it = cntxt.begin(), e = cntxt.end()
        ; orig_it < e 
        ; ++orig_it // see the last line marked with !!!. We omit sent end token
                    // and proceed to next sentence begin
    )
    {
        context_range_t curr_sent_ctxt = GetNextSent(orig_it, e);

        std::size_t pos = 0;
        for ( auto al_word_it = curr_sent_ctxt.begin()
            ; al_word_it != curr_sent_ctxt.end()
            ; ++pos, ++al_word_it
        ) 
        {
            if (al_word_it -> is_none() || !al_word_it -> is_word())
            {
                continue;
            }

            cntxt_word_t & curr_word = *al_word_it;
            candidates_t candidates {GetCandidates(curr_sent_ctxt, pos)};
            if (!candidates.empty()) 
            {
                cand_word_t & top_w = candidates.front();
                if(curr_word.score < top_w.score && top_w.id != curr_word.id)
                {
                    // Note: ManageDroppedTokens advances al_word_it!
                    pos += ManageDroppedTokens(top_w, al_word_it);  
                    curr_word.assign(std::move(top_w));                    
                }
            }
        }
        orig_it = curr_sent_ctxt.end();  // !!!
    }
}

context_t TSpellCorrector::FixContext(std::wstring const & text) const 
{
    wstr_view_t const orig_txt(text);
    context_t cntxt;
    LangModel.Text2Words(orig_txt, cntxt);
    FixContext(cntxt);
    return cntxt;
}


std::wstring TSpellCorrector::FixFragment(std::wstring const & text) const 
{
    JS_TRACE_MSG(std::cerr << "[debug] Fixing fragment: \'" 
        << w_to_u8(text) << "\'\n"
    );

    wstr_view_t const orig_txt(text);
    std::wstring result;
    result.reserve(orig_txt.size() * 1.1 + 16u); // 640 Kb should be enough for all!

    context_t const cntxt = FixContext(text);
    
    size_t origPos = 0;
    for(cntxt_word_t const & cw: cntxt)
    {
        if (cw.omitted())
        {
            continue;
        }

        size_t const currOrigPos = cw.token.ofs();
        result += orig_txt.substr(origPos, currOrigPos - origPos);
        origPos = currOrigPos;
            
        AppendWithCase(result, cw.token.str(), cw.str); 
        origPos += cw.token.size();
    }

    result += orig_txt.substr(origPos, text.size() - origPos);
    JS_TRACE_MSG(std::cerr << "[debug] fixed result: \'" 
        << w_to_u8(result) << "\'\n"
    );

    return result;
}

context_range_t TSpellCorrector::GetNextSent(context_t::iterator const & b
    , context_t::iterator const & e
)
{
    context_t::iterator i = b;
    for(; (i != e) && (! TTokenizer::isSentEnd( i -> token )) 
        ; ++i 
    )
    {}
    return context_range_t{b, i};
}

concat_inf_t TSpellCorrector::CntNonSpacedNeighbours(
      context_range_t const & context
    , ::std::size_t const position
) const
{
    concat_inf_t i;
    for(auto lpos = context.begin() + position, pos = lpos--
        ; (pos != context.begin()) 
            && (!lpos -> is_none()) 
            && (!lpos -> is_word()) 
            && (!areSpaced(lpos -> token, pos -> token))
        ; pos = lpos--, ++i.left
    ){}     

    for(auto rpos = context.begin() + position, pos = rpos++
        ; rpos != context.end() && (!rpos -> is_none()) 
            && !areSpaced(pos -> token, rpos -> token)
        ; pos = rpos++, ++i.right
    ){}

    return i;
}

bool TSpellCorrector::PrevWordWasSwitched(context_range_t const & context
    , ::std::size_t const position
) const
{
    for(auto it = context.begin() + position; it-- != context.begin() ; )
    {
        if((!it->is_none()) && it -> is_word())
        {
            return it -> was_switched();
        }
    }
    return false;
}


std::size_t TSpellCorrector::ManageDroppedTokens(cand_word_t const & top_w 
    , context_t::iterator & al_word_it
) const
{
    cntxt_word_t & curr_word = *al_word_it;
    auto lpos_it = al_word_it - top_w.drop.left;
    token_info_t const & ltoken = lpos_it -> token;    
    for(; lpos_it != al_word_it; ++lpos_it)
    {
        lpos_it -> kind = ckNone;
    }
    
    token_info_t::len_type l = 0u; 
    for(::std::uint32_t i = 0; i < top_w.drop.right; ++i)
    {        
        (++al_word_it) -> kind = ckNone;        
        l += al_word_it -> token.size();
    }

    curr_word.token.reset(ltoken.ofs()
        , curr_word.token.ofs() - ltoken.ofs() + curr_word.token.size() + l 
    );
    return top_w.drop.right;
}

::std::size_t TSpellCorrector::CheckSwitchedCands (context_range_t const & context
    , ::std::size_t const position
    , TCandMgr & cmgr
    , attributes_t & attrs
    , concat_inf_t const & ci
) const
{    
    str_t s;
    str_view_t const sw =
    (ci.left) ? (
        MakeLeftSwCandsStrPrefix(context, position, ci, s),
        s += context[position].str
    ) : context[position].str;

    ::std::size_t i = 0u, added_cnt = 0u;
    do
    {
        added_cnt += FormGreedySwitchedCandsRight(context
            , position, sw.substr(i), cmgr, attrs, ci
        );                
    }
    while(++i <= ci.left);
    return added_cnt;
}

::std::size_t TSpellCorrector::FormGreedySwitchedCandsRight (
      context_range_t const & context
    , ::std::size_t const position
    , str_view_t const & sw_cand_str
    , TCandMgr & cmgr
    , attributes_t & attrs
    , concat_inf_t const & ci
) const
{       
    str_t s;
    str_view_t const sw = (ci.right) ?    
        (MakeRightSwCandsStr(context, position, ci, (s = sw_cand_str)), s)
        : sw_cand_str;
    
    if( (s = PuntoSwitcher(sw)).empty() )
    {
        return 0u;
    }
    
    std::size_t added_cnt = 0u;
    word_t w = LangModel.LongestPrefixSearch(s);
    if(!w.unknown())
    {
        //check, if the longest prefix is at least long as context[position].str
        if(w.str.size() >= sw_cand_str.size())
        {
            concat_inf_t const nci {ci.left, CalcRPos(ci.right, context, position
                , w.str.size() + context[position - ci.left].token.ofs()
            )};

            if(     nci.right == ci.right 
                ||  context[position + nci.right + 1].is_punct()
            )
            {
                attrs.sw_orig_is_known = true;
                cmgr.set_kind (ckOrigSw);            
                return Push2Candidates(std::move(w.str), w, cmgr, nci);
            }
            else 
            {
                //attrs.sw_orig_is_known = false;
                cmgr.set_kind (ckFirstLvlSw);            
                added_cnt += Push2Candidates(std::move(w.str), w, cmgr, nci);
            }
        }
        else 
        {
            // TODO: deal with it!
            // check if can be split on two parts?
            // return on success!
        }
    }    
    
    //attrs.sw_orig_is_known = false;
    cmgr.set_kind (ckFirstLvlSw);
    added_cnt += Edits2(s, cmgr, ci);  // be greedy!
    return added_cnt;
}

 void TSpellCorrector::MakeLeftSwCandsStrPrefix(
      context_range_t const & context
    , ::std::size_t const position
    , concat_inf_t const & ci
    , str_t & s
) const
{
    BOOST_ASSERT_MSG(ci.left
        , "Called MakeLeftSwCandsStrPrefix on empty left concatenation context"
    );
    std::size_t cnt = ci.left;
    auto i = context.begin() + position - ci.left;
    do
    {
        s += (i++) -> str;
    } 
    while (--cnt);
}

void TSpellCorrector::MakeRightSwCandsStr(
      context_range_t const & context
    , ::std::size_t const position
    , concat_inf_t const & ci
    , str_t & s
) const
{
    BOOST_ASSERT_MSG(ci.right
        , "Called MakeRightSwCandsStr on empty right concatenation context"
    );

    ::std::size_t cnt = ci.right;
    auto it = context.begin() + position + 1u;
    do
    {
        s += (it++) -> str;
    }
    while ( --cnt);
}

::std::uint32_t TSpellCorrector:: CalcRPos(std::size_t cnt
    , context_range_t const & context
    , ::std::size_t const position
    , std::size_t const rpos_ofs
)
{
    ::std::size_t i = 1u;
    for(; cnt--; ++i)
    {
        if(context[position + i].token.end_ofs() > rpos_ofs)
        {
            break;
        }
    }
    return i - 1;
}


void TSpellCorrector::AppendWithCase(std::wstring & result
    , wstr_view_t const & origWord
    , str_view_t const & newWord
) const
{
    std::size_t const pos {result.size()};
    result.resize(pos + newWord.size());

    for (std::size_t k = 0; k < newWord.size(); ++k) 
    {
        wchar_t const origChar = (k < origWord.size()) ? origWord[k] : origWord.back() ;
        result[pos + k] = MakeUpperIfRequired(GetAlphabet().Ch2Wch(newWord[k])
            , origChar
        );
    }
}

str_t TSpellCorrector::PuntoSwitcher(str_view_t const &w) const
{ 
    str_t s = FribbulusXax(LangModel.GetAlphabet(), w);
    JS_TRACE_MSG(std::cerr << "[debug] Switched kb-layout for word: \'" 
        << w_to_u8(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), w)
        ) << "\' to \'" 
        << w_to_u8(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), s)
        ) << "\'\n"
    );
    if(s == w)
    {
        s = str_t{};
    }
    return s;
}

void TSpellCorrector::Edits(str_view_t const& word
    , TCandMgr & candidates
    , concat_inf_t const & di
) const 
{
    JS_TRACE_MSG(std::cerr << "[debug] Edits (2-letters) candidates for word: \'" 
        << w_to_u8(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), word)
        ) << "\'\n" 
    );

    del2_vec_t cands = GetDeletes2(word);
    cands.emplace_back(1u, str_t{word});

    str_t buf;
    for (auto&& w1: cands) 
    {
        for (auto&& w: w1) 
        {
            LookupAndAppend2Candidates(w, candidates, di);
            if (Deletes1->Contains(w)) 
            {
                Inserts(w, candidates, buf, di);
            }
            if (Deletes2->Contains(w)) 
            {
                Inserts2(w, candidates, di);
            }
        }
    }
}

std::size_t TSpellCorrector::Edits2(str_view_t const & word
    , TCandMgr & candidates
    , concat_inf_t const & di
) const 
{
    JS_TRACE_MSG(std::cerr << "[debug] Edits candidates for word: \'" 
        << w_to_u8(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), word)
        ) << "\'\n" 
    );

    ::std::size_t cnt_added {0u};
    ::std::size_t const wsz = word.size();

    str_t s;
    s.reserve(wsz + 1u);    // one size fits all!
    for (size_t i = 0; i <= wsz; ++i) // !
    {
        // delete
        if (wsz > 2u && i < wsz) 
        {
            (s = word.substr(0u, i)) += word.substr( i + 1u);
            cnt_added += LookupAndAppend2Candidates(s, candidates, di);
        }

        // transpose
        if (i + 1u < wsz) 
        {
            ((s = word.substr(0, i)) += word[i + 1]) += word[i];
            if (i + 2u < wsz) 
            {
                s += word.substr( i + 2u);
            }
            cnt_added += LookupAndAppend2Candidates(s, candidates, di);
        }

        // replace
        if (i < wsz) 
        {
            TAlphabet::subs_type const & sbt = GetAlphabet().GetSubstitutes(word[i]);
            JS_TRACE_MSG(std::cerr << "[debug] substitutes for letter \'"
                << w_to_u8(std::wstring(1u, LangModel.GetAlphabet().Ch2Wch(word[i]))) 
                << "\': " << w_to_u8(
                    FromAlphabet(GetAlphabet(), str_view_t(sbt.data(), sbt.size()) )) 
                << "\'\n" 
            );
            s = word;
            for (auto&& ch: sbt) 
            {
                //((s = word.substr(0, i)) += ch) += word.substr( i + 1u);
                s[i] = ch;
                cnt_added += LookupAndAppend2Candidates(s, candidates, di);
            }
        }

        // inserts
        {
            ((s = word.substr(0, i)) += ' ') += word.substr(i);
            for (auto&& ch: LangModel.GetAlphabet()) 
            {
                //((s = word.substr(0, i)) += ch) += word.substr(i);
                s[i] = ch;
                cnt_added += LookupAndAppend2Candidates(s, candidates, di);
            }
        }
    }

    return cnt_added;

}

void TSpellCorrector::InsertsImpl(str_view_t const & w
    , std::size_t const i
    , TCandMgr& result
    , str_t & s
    , concat_inf_t const & di
) const 
{
    for (char const ch: LangModel.GetAlphabet()) 
    {       
        ((s = w.substr(0, i)) += ch) += w.substr(i);
        LookupAndAppend2Candidates(s, result, di);
    }
}

void TSpellCorrector::Inserts(str_view_t const & w
    , TCandMgr& result
    , str_t & buf
    , concat_inf_t const & di
) const 
{
    std::size_t const sz (w.size() + 1) ;
    for (size_t i = 0; i < sz; ++i) 
    {
        InsertsImpl(w, i, result, buf, di);
    }
}

void TSpellCorrector::Inserts2Impl(str_view_t const & w
    , std::size_t const i
    , TCandMgr& result
    , str_t & s
    , str_t & buf
    , concat_inf_t const & di
) const 
{
    for (char const ch: LangModel.GetAlphabet()) 
    {        
        ((s = w.substr(0, i)) += ch) += w.substr(i);
        if (Deletes1->Contains(s)) 
        {
            Inserts(s, result, buf, di);
        }
    }
}

void TSpellCorrector::Inserts2(str_view_t const & w
    , TCandMgr& result
    , concat_inf_t const & di
) const 
{
    std::size_t const sz (w.size() + 1u);
    str_t s, buf;
    s.reserve(sz);
    buf.reserve (sz + 1u);
    for(std::size_t i = 0; i < sz; ++i)
    {
        Inserts2Impl(w, i, result, s, buf, di);
    } 
}

void TSpellCorrector::PrepareCache() 
{
    size_t const avgWordLen = LangModel.avg_word_length()
        ,  avgWordLenMinusOne = std::max(size_t(1), avgWordLen - 1);

    uint64_t deletes1size = LangModel.dict_size() * avgWordLen;
    uint64_t deletes2size = LangModel.dict_size() * avgWordLen * avgWordLenMinusOne;
    deletes1size = std::max(uint64_t(1000), deletes1size);
    deletes1size = std::max(uint64_t(1000), deletes1size);

    double falsePositiveProb = 0.001;
    Deletes1.reset(new TBloomFilter(deletes1size, falsePositiveProb));
    Deletes2.reset(new TBloomFilter(deletes2size, falsePositiveProb));

    uint64_t deletes1real = 0;
    uint64_t deletes2real = 0;

    std::string sbuf;
    for (TLangModel::dict_const_iterator it(LangModel.dict_begin()), e(LangModel.dict_end())
        ; it != e
        ; ++it
    ) 
    {
        it.key(sbuf);
        auto deletes = GetDeletes2(sbuf);
        for (auto&& w1: deletes) 
        {
            Deletes1->Insert(w1.back());
            deletes1real += 1;
            for (std::size_t i = 0; i < w1.size() - 1; ++i) 
            {
                Deletes2->Insert(w1[i]);
                deletes2real += 1;
            }
        }
    }
}

constexpr uint64_t SPELL_CHECKER_CACHE_MAGIC_BYTE = 3811558393781437494L;
constexpr uint16_t SPELL_CHECKER_CACHE_VERSION = 1;

bool TSpellCorrector::LoadCache(const std::string& cacheFile) {
    std::ifstream in(cacheFile, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    uint16_t version = 0;
    uint64_t magicByte = 0;
    NHandyPack::Load(in, magicByte);
    if (magicByte != SPELL_CHECKER_CACHE_MAGIC_BYTE) {
        return false;
    }
    NHandyPack::Load(in, version);
    if (version != SPELL_CHECKER_CACHE_VERSION) {
        return false;
    }
    uint64_t checkSum = 0;
    NHandyPack::Load(in, checkSum);
    if (checkSum != LangModel.GetCheckSum()) {
        return false;
    }
    std::unique_ptr<TBloomFilter> deletes1(new TBloomFilter());
    std::unique_ptr<TBloomFilter> deletes2(new TBloomFilter());
    deletes1->Load(in);
    deletes2->Load(in);
    magicByte = 0;
    NHandyPack::Load(in, magicByte);
    if (magicByte != SPELL_CHECKER_CACHE_MAGIC_BYTE) 
    {
        return false;
    }
    Deletes1 = std::move(deletes1);
    Deletes2 = std::move(deletes2);
    return true;
}

bool TSpellCorrector::SaveCache(const std::string& cacheFile) 
{
    std::ofstream out(cacheFile, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    if (!Deletes1 || !Deletes2) {
        return false;
    }
    NHandyPack::Dump(out, SPELL_CHECKER_CACHE_MAGIC_BYTE);
    NHandyPack::Dump(out, SPELL_CHECKER_CACHE_VERSION);
    NHandyPack::Dump(out, LangModel.GetCheckSum());
    Deletes1->Dump(out);
    Deletes2->Dump(out);
    NHandyPack::Dump(out, SPELL_CHECKER_CACHE_MAGIC_BYTE);
    return true;
}

bool TSpellCorrector::Push2Candidates (str_t && w
    , wdata_t const & wd
    , TCandMgr & cmgr
    , concat_inf_t const & di
) const
{
    JS_TRACE_MSG(std::cerr << "[debug] synthesized candidate: \'" 
        << w_to_u8(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), w)) 
        << "\' id = " << static_cast<uint32_t>(wd.id) 
        << " count = " << wd.cnt << "\n"
    );

    return cmgr.insert(wd, std::move(w), di);
}

bool TSpellCorrector::Append2Candidates (str_view_t const & w
    , wdata_t const & wd
    , TCandMgr & cmgr
    , concat_inf_t const & di
) const
{
    JS_TRACE_MSG(std::cerr << "[debug] synthesized candidate: \'" 
        << w_to_u8(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), w)) 
        << "\' id = " << static_cast<uint32_t>(wd.id) 
        << " count = " << wd.cnt << "\n"
    );

    if (! wd.unknown() ) 
    {
        return cmgr.insert(wd, w, di);
    }
    return false;
}

context_crange_t TSpellCorrector::GetSentenceRange(
      context_crange_t const & sentence
    , std::size_t const pos
) const
{
    auto beg_it = sentence.begin(), end_it = beg_it;
    std::advance(beg_it, std::max(long (pos) - 2l, 0l));
    std::advance(end_it, std::min(pos + 3ul , sentence.size()));
    return context_crange_t{beg_it, end_it};
}

float TSpellCorrector::ScoreOrig(attributes_t const & attrs
    , context_range_t const & orig_sent
    , std::size_t const pos
) const
{
    float sc = LangModel.Score(GetSentenceRange(orig_sent, pos));
    return sc;
}

void TSpellCorrector::Score(attributes_t const & attrs
    , context_range_t const & context
    , std::size_t const pos
    , candidates_t & candidates
) const
{
    auto const & cand_sent = GetSentenceRange(context, pos);
    cntxt_word_t & oword = context[pos];
    wdata_t const odat = oword.wdata(); // store copy!
    
    for (cand_word_t & cnd: candidates) 
    {
        oword.reset(cnd.wdata());

        cnd.score = ScoreCandidate(attrs
            , LangModel.Score(cand_sent.begin(), cand_sent.end())
            , cnd.kind
        );

        JS_TRACE_MSG(std::cerr << "[debug] Scored candidate: \'" 
            << w_to_u8(
                FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), cnd.str)
            ) << "\' score = " << cnd.score << "\n"
        );
    }

    oword.reset(odat); // restore original state
}

float TSpellCorrector::ScoreCandidate (attributes_t const & attrs
    , float sc
    , cand_kind_t const ck
) const
{
    switch (ck)
    {
        case ckOrigSw:
        {
            sc -= (attrs.orig_is_known) ? m_opt.OrigWordIsKnownPenalty 
                : m_opt.OrigWordIsUnknownPenalty;
            sc -= (!attrs.prev_was_switched) * m_opt.SwitchedWordPenalty;
            break;
        }
        case ckFirstLvl:
        {
            sc -= (attrs.orig_is_known) ? m_opt.OrigWordIsKnownPenalty
                : m_opt.OrigWordIsUnknownPenalty;
            break;
        }               
        case ckSecondLvl:
        {
            sc = (attrs.orig_is_known) ? 
                (sc * m_opt.SecondLvlPenFactor) 
            :   (sc - m_opt.OrigWordIsUnknownPenalty - m_opt.SecondLvlPenalty);
            break;
        }

        case ckFirstLvlSw:
        {
            sc -= (attrs.orig_is_known) ? m_opt.OrigWordIsKnownPenalty 
                : m_opt.OrigWordIsUnknownPenalty;
            sc -= ((!attrs.prev_was_switched) * m_opt.SwitchedWordPenalty 
                    + attrs.sw_orig_is_known * m_opt.SwitchedWordIsKnownPenalty
                );
            break;
        }

        case ckSecondLvlSw:
        {
            sc = (attrs.orig_is_known) ? 
                (sc * m_opt.SecondLvlPenFactor) 
            :   (sc - m_opt.OrigWordIsUnknownPenalty - m_opt.SecondLvlPenalty);
            sc -= ((!attrs.prev_was_switched) * m_opt.SwitchedWordPenalty 
                    + attrs.sw_orig_is_known * m_opt.SwitchedWordIsKnownPenalty
                );
            break;
        }
        default:
        {
            break;
        }
    }
    return sc; 
}


} // NJamSpell
