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
        (nw += w.substr(0, i)) += w.substr( i + 1u );
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
    setValFromEnv(opt.BadTokenPenalty, "SPLL_BAD_TOKEN_PENALTY");

    //setValFromEnv(opt.LowProbPenalty, "SPLL_LOW_PROB_PENALTY");
    
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

#ifdef SPLL_DEEPFIX_EXPERIMENTAL
void TSpellCorrector::DeepFix(context_t & cntxt) const
{
    for (auto orig_it = cntxt.begin(), e = cntxt.end()
        ; orig_it < e 
        ; ++orig_it // see the last line marked with !!!. We omit sent end token
                    // and proceed to next sentence begin
    )
    {
        context_range_t const & curr_sent_ctxt = GetNextSent(orig_it, e);
        if(curr_sent_ctxt.empty())
        {
            continue;
        }
        ::std::size_t pos = 0, rfact = 3u;
        auto al_word_it = curr_sent_ctxt.begin();
        do
        {
            cand_word_t const & orig = al_word_it -> orig_word;
            if (orig.is_none() || !orig.is_word())
            {
                //AddOrig2Candidates(*al_word_it);
                continue;
            }

            ProcessCandidates(curr_sent_ctxt, pos);
            TrimCandidates(*al_word_it, rfact);
            AddOrig2Candidates(*al_word_it);
        }
        while (++pos, (++al_word_it != curr_sent_ctxt.end()));
        orig_it = curr_sent_ctxt.end();  // !!!
        DeepScore(curr_sent_ctxt);
    }    
}

context_t TSpellCorrector::DeepFix(std::wstring const & text) const 
{
    context_t cntxt;
    LangModel.Text2Words(text, cntxt);
    DeepFix(cntxt);
    return cntxt;
}

#endif // #ifdef SPLL_DEEPFIX_EXPERIMENTAL

void TSpellCorrector::Fix(context_t & cntxt) const
{
    for (auto orig_it = cntxt.begin(), e = cntxt.end()
        ; orig_it < e 
        ; ++orig_it // see the last line marked with !!!. We omit sent end token
                    // and proceed to next sentence begin
    )
    {
        context_range_t const & curr_sent_ctxt = GetNextSent(orig_it, e);
        std::size_t pos = 0;
        for ( auto al_word_it = curr_sent_ctxt.begin()
            ; al_word_it != curr_sent_ctxt.end()
            ; ++pos, ++al_word_it
        ) 
        {
            cand_word_t const & orig = al_word_it -> orig_word;
            if (orig.is_none() || !orig.is_word())
            {
                continue;
            }

            ProcessCandidates(curr_sent_ctxt, pos);
            if (al_word_it -> reset_best_cand()) 
            {
                // Note: ManageDroppedTokens advances al_word_it!
                pos += ManageDroppedTokens(al_word_it);
            }
        }
        orig_it = curr_sent_ctxt.end();  // !!!
    }
}

context_t TSpellCorrector::Fix(std::wstring const & text) const 
{
    context_t cntxt;
    LangModel.Text2Words(text, cntxt);
    Fix(cntxt);
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

    context_t const & cntxt = Fix(text);
    //context_t const & cntxt = DeepFix(text);
    
    size_t origPos = 0;
    for(cntxt_word_t const & cw: cntxt)
    {
        cand_word_t const & orig = cw.orig_word;
        if (orig.omitted() || !cw.changed())
        {
            continue;
        }

        size_t const currOrigPos = cw.token.ofs();
        result += orig_txt.substr(origPos, currOrigPos - origPos);
        origPos = currOrigPos;
            
        AppendWithCase(result, cw.token.str(), cw.get_best_cand().str); 
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
) const
{
    context_t::iterator i = b;
    for(; i != e && (! GetLangModel().GetTokenizer().isSentBreak( i, e )) ; ++i)
    {}
    return context_range_t{b, i};
}

void TSpellCorrector::TrimCandidates(cntxt_word_t & curr_word
    , std::size_t const rfact
)
{    
    if(curr_word.candidates.size() > rfact)
    {
        curr_word.candidates.resize(rfact);
        //curr_word.candidates.resize(curr_word.candidates.size() / rfact + 1u);
    }    
}

void TSpellCorrector::AddOrig2Candidates(cntxt_word_t & curr_word)
{
    curr_word.candidates.emplace_back(curr_word.orig_word);
}

void TSpellCorrector::FormCandidates(context_range_t const & context
    , ::std::size_t const position
) const
{
    BOOST_ASSERT_MSG (position < context.size(), "position is out of range!");

    cntxt_word_t & curr_word = context[position];
    cand_word_t & orig_word = curr_word.orig_word;
    if(!orig_word.good())
    {
        return;
    }
    
    curr_word.attrs.prev_was_switched = PrevWordWasSwitched(context, position);
    CntNonSpacedNeighbours(context, position, curr_word.concat);
    curr_word.orig_word.score = ScoreOrig(context, position);

    JS_TRACE_MSG(std::cerr << "[debug] Scored orig: \'" 
        << w_to_u8(FromAlphabet(GetAlphabet(), orig_word.str)) 
        << "\' id = " << static_cast<std::uint32_t> (orig_word.id) 
        << " count = " << orig_word.cnt << " score = " <<orig_word.score << "\n"
    );    
    
    // PITIPIWPIW WIW WIW!
    TCandMgr cndMgr(curr_word.candidates, m_opt.MaxCandidatesToCheck);
    
    ::std::size_t e2_cnt_added = 0u;
    if(IsShortFragment(curr_word))
    {
        if (curr_word.orig_word.str.size() > (1u + curr_word.attrs.prev_was_switched))
        {
            cndMgr.set_kind (ckFirstLvlFrgmt);
            e2_cnt_added += Edits2(orig_word.str, cndMgr);
        }
        // we ignore punct to the left side of token!
        e2_cnt_added += CheckExtendedTokenStr(context, position, cndMgr);        
    }
    else
    {
        if (curr_word.orig_word.str.size() > 1)
        {
            cndMgr.set_kind (ckFirstLvl);
            e2_cnt_added += Edits2(orig_word.str, cndMgr);
        }
    }

    if (    orig_word.unknown() 
        || IsInfreq(orig_word) 
        || curr_word.has_non_spaced()
        || curr_word.attrs.prev_was_switched
    )
    {
        CheckSwitchedCands(context, position, cndMgr);   
    }

    if(     orig_word.unknown()
        &&  (!e2_cnt_added || CandidatesAreInfreq(cndMgr) )
    )
    {
        cndMgr.set_kind (
            curr_word.concat.right > 1u  ? ckSecondLvlFrgmt : ckSecondLvl
        );
        Edits(orig_word.str, cndMgr);
    }

}

::std::size_t 
TSpellCorrector::CheckExtendedTokenStr(context_range_t const & cntxt
    , ::std::size_t const pos
    , TCandMgr & cmgr
) const
{
    cntxt_word_t & cw = cntxt[pos];
    BOOST_ASSERT_MSG(cw.concat.right > 1u, "Bad extended token!");
    str_t s {cw.orig_word.str};
    MakeRightCandsStr(cntxt, pos, s);
  
    ::std::size_t cnt_added = 0u;
    cmgr.set_kind (ckFirstLvl);
    cnt_added += Edits2(s, cmgr
        , concat_inf_t(0u
            , cw.concat.right - cntxt[pos + cw.concat.right].orig_word.is_punct()
        )
    );

    if((!cnt_added) || CandidatesAreInfreq(cmgr))
    {
        cmgr.set_kind (ckSecondLvl);
        Edits(s, cmgr);
    }

    return cnt_added;
}

 ::std::size_t TSpellCorrector::CntNonSpacedNeighbours(
      context_range_t const & context
    , ::std::size_t const position
    , concat_inf_t & i
) const
{
     ::std::size_t punct_cnt = 0u;
    for(auto lpos = context.begin() + position, pos = lpos--
        ; (pos != context.begin()) 
            && (!lpos -> orig_word.is_none()) 
            && (!lpos -> orig_word.is_word()) 
            && (!areSpaced(lpos -> token, pos -> token))
        ; pos = lpos--, ++i.left, ++punct_cnt
    ){}     

    bool ends_with_pnct = false;
    for(auto rpos = context.begin() + position, pos = rpos++
        ; rpos != context.end() && (!rpos -> orig_word.is_none()) 
            && !areSpaced(pos -> token, rpos -> token)
        ; pos = rpos++, ++i.right
    )
    {
        ends_with_pnct = rpos -> orig_word.is_punct();
        punct_cnt += ends_with_pnct;
    }

    return punct_cnt -= ends_with_pnct;
}

bool TSpellCorrector::PrevWordWasSwitched(context_range_t const & context
    , ::std::size_t const position
) const
{
    for(auto it = context.begin() + position; it-- != context.begin() ; )
    {
        if((!it->orig_word.is_none()) && it -> orig_word.is_word())
        {
            return it -> get_best_cand().was_switched();
        }
    }
    return false;
}


std::size_t 
TSpellCorrector::ManageDroppedTokens(context_t::iterator & al_word_it) const
{
    cntxt_word_t & curr_word = *al_word_it;
    concat_inf_t const cinf = curr_word.get_best_cand().concat;
    auto lpos_it = al_word_it - cinf.left;
    token_info_t const & ltoken = lpos_it -> token;    
    for(; lpos_it != al_word_it; ++lpos_it)
    {
        lpos_it -> orig_word.kind = ckNone;
    }
    
    token_info_t::len_type l = 0u; 
    for(::std::uint32_t i = 0; i < cinf.right; ++i)
    {        
        (++al_word_it) -> orig_word.kind = ckNone;
        l += al_word_it -> token.size();
    }

    curr_word.token.reset(ltoken.ofs()
        , curr_word.token.ofs() - ltoken.ofs() + curr_word.token.size() + l 
    );
    return cinf.right;
}

::std::size_t 
TSpellCorrector::CheckSwitchedCands (context_range_t const & context
    , ::std::size_t const position
    , TCandMgr & cmgr
) const
{
    cntxt_word_t & cw = context[position];
    str_t s;
    str_view_t const sw =
    (cw.concat.left) ? (
        MakeLeftCandsStrPrefix(context, position, s), s += cw.orig_word.str
    ) : cw.orig_word.str;

    ::std::size_t i = 0u, added_cnt = 0u;
    do
    {
        added_cnt += FormGreedySwitchedCandsRight(context
            , position, sw.substr(i), cmgr
        );                
    }
    while(++i <= cw.concat.left);
    return added_cnt;
}

::std::size_t TSpellCorrector::FormGreedySwitchedCandsRight (
      context_range_t const & context
    , ::std::size_t const position
    , str_view_t const & sw_cand_str
    , TCandMgr & cmgr
) const
{       
    cntxt_word_t & cw = context[position];
    str_t s;
    str_view_t const sw = (cw.concat.right) ?    
        (MakeRightCandsStr(context, position, (s = sw_cand_str)), s)
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
            concat_inf_t const nci (cw.concat.left
                , CalcRPos(cw.concat.right, context, position
                    , w.str.size() + context[position - cw.concat.left].token.ofs()
                  )
            );

            if(     nci.right == cw.concat.right 
                ||  (( nci.right == cw.concat.right - 1)
                    && context[position + cw.concat.right].orig_word.is_punct()
                )
            )
            {
                cw.attrs.sw_orig_is_known = true;
                cmgr.set_kind (ckOrigSw);            
                added_cnt += Push2Candidates(std::move(w.str), w, cmgr, nci);
                if(!IsInfreq(w))
                {
                    return added_cnt;
                }
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
    added_cnt += Edits2(s, cmgr, cw.concat);  // be greedy!
    return added_cnt;
}

 void TSpellCorrector::MakeLeftCandsStrPrefix(
      context_range_t const & context
    , ::std::size_t const position
    , str_t & s
) const
{
    std::size_t cnt = context[position].concat.left;
    BOOST_ASSERT_MSG(cnt
        , "Called MakeLeftSwCandsStrPrefix on empty left concatenation context"
    );
    auto i = context.begin() + position - cnt;
    do
    {
        s += (i++) -> orig_word.str;
    } 
    while (--cnt);
}

void TSpellCorrector::MakeRightCandsStr(
      context_range_t const & context
    , ::std::size_t const position
    , str_t & s
) const
{
    ::std::size_t cnt = context[position].concat.right;
    BOOST_ASSERT_MSG(cnt
        , "Called MakeRightSwCandsStr on empty right concatenation context"
    );
    auto it = context.begin() + position + 1u;
    do
    {
        s += (it++) -> orig_word.str;
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

#ifdef SPLL_DEEPFIX_EXPERIMENTAL
void TSpellCorrector:: ApplyP(context_range_t const & context
    , permutation_t const & p
)
{
    std::size_t i = 0u;
    for(cntxt_word_t & ctxt_w : context)
    {
        if(!ctxt_w.candidates.empty())
        {
            ctxt_w.set_best_cand(p[i++]);
        }
    }
}

float TSpellCorrector:: PScore(context_range_t const & context
    , permutation_t const & p
) const
{
    ApplyP(context, p);
    return LangModel.Score(context.begin(), context.end());
}
#endif // #ifdef SPLL_DEEPFIX_EXPERIMENTAL

void TSpellCorrector::Score(context_range_t const & context
    , std::size_t const pos
) const
{
    auto const & cand_sent = GetSentenceRange(context, pos);
    cntxt_word_t & cword = context[pos];
    for (::std::size_t i = 0; i < cword.candidates.size(); ++i)
    {
        cand_word_t & cnd = cword.set_best_cand(i);
        JS_TRACE_MSG(std::cerr << "[debug] Scored candidate: \'" 
            << w_to_u8(
                FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), cnd.str)
            )
        );
        cnd.score = LangModel.Score(cand_sent.begin(), cand_sent.end());
        ScoreCandidate(cword, cnd);

        JS_TRACE_MSG(std::cerr << "\' score = " << cnd.score << "\n");
    }

    SortCandidates(cword);
    JS_TRACE_MSG(bool empt_cnd = cword.candidates.empty());
    JS_TRACE_MSG(std::cerr << "[debug] Best Candidate: \'" 
        << ( empt_cnd ? std::string{} 
        : w_to_u8(FromAlphabet(GetAlphabet(), cword.candidates.front().str))
        ) << "\' id = " 
        << (empt_cnd ? 0u : static_cast<std::uint32_t> (cword.candidates.front().id))
        << " count = " << (empt_cnd ? 0u : cword.candidates.front().cnt)
        << " score = " << (empt_cnd ? 0.0 : cword.candidates.front().score) 
        << ::std::endl
    );
}

#ifdef SPLL_DEEPFIX_EXPERIMENTAL
void TSpellCorrector:: DeepScore(context_range_t const & context) const
{
    float max_score = std::numeric_limits<float>::lowest();
    permutation_t p(context.size(), 0u), best_p;
    while (true) 
    {
        float const s = PScore(context, p);
        if(s > max_score)
        {
            best_p = p;
            max_score = s;
        }

        ::std::size_t i = 0;
        for (; i < p.size() 
            && (context[i].candidates.empty() 
                || (p[i] == context[i].candidates.size() - 1)
            )
            ; p[i++] = 0u
        ){}

        if (i == p.size()) 
        {
            break;
        }
        ++p[i];
    }
    ApplyP(context, best_p);
}
#endif //#ifdef SPLL_DEEPFIX_EXPERIMENTAL


void 
TSpellCorrector
::ScoreCandidate (cntxt_word_t const & ctx_word, cand_word_t & cnd) const
{
    switch (cnd.kind)
    {
        case ckOrigSw:
        {
            cnd.score -= (!ctx_word.orig_word.unknown()) ? 
                    m_opt.OrigWordIsKnownPenalty 
                :   m_opt.OrigWordIsUnknownPenalty;
            cnd.score -= (!ctx_word.attrs.prev_was_switched) * m_opt.SwitchedWordPenalty;
            break;
        }

        case ckFirstLvlFrgmt:
        {
            cnd.score -= m_opt.ShortFragmentPenalty;
            // no break needed!
        }
        case ckFirstLvl:
        {
            cnd.score -= (!ctx_word.orig_word.unknown()) ? 
                    m_opt.OrigWordIsKnownPenalty
                :   m_opt.OrigWordIsUnknownPenalty;
            break;
        }    
        
        case ckSecondLvlFrgmt:
        {
            cnd.score -= m_opt.ShortFragmentPenalty;
            // no break needed!
        }
        case ckSecondLvl:
        {
            cnd.score = (!ctx_word.orig_word.unknown()) ? 
                (cnd.score * m_opt.SecondLvlPenFactor) 
            :   (cnd.score - m_opt.OrigWordIsUnknownPenalty - m_opt.SecondLvlPenalty);
            break;
        }

        case ckFirstLvlSw:
        {
            cnd.score -= (!ctx_word.orig_word.unknown()) ? 
                    m_opt.OrigWordIsKnownPenalty 
                :   m_opt.OrigWordIsUnknownPenalty;
            cnd.score -= ((!ctx_word.attrs.prev_was_switched) * m_opt.SwitchedWordPenalty 
                    + ctx_word.attrs.sw_orig_is_known * m_opt.SwitchedWordIsKnownPenalty
                );
            break;
        }

        case ckSecondLvlSw:
        {
            cnd.score = (!ctx_word.orig_word.unknown()) ? 
                (cnd.score * m_opt.SecondLvlPenFactor) 
            :   (cnd.score - m_opt.OrigWordIsUnknownPenalty - m_opt.SecondLvlPenalty);
            cnd.score -= ((!ctx_word.attrs.prev_was_switched) * m_opt.SwitchedWordPenalty 
                    + ctx_word.attrs.sw_orig_is_known * m_opt.SwitchedWordIsKnownPenalty
                );
            break;
        }        

        default:
        {
            break;
        }
    }

    token_stat_t ts {GetAlphabet().CalcTokenStat(cnd.str)};
    ts.is_title_case = ctx_word.token.stat().is_title_case;
    cnd.score = ReScore(cnd, cnd.score, ts);

}

float TSpellCorrector::ScoreOrig(context_range_t const & cntxt
        , ::std::size_t const pos
    ) const
{
    cntxt_word_t const & cw = cntxt[pos];
    float sc = LangModel.Score(GetSentenceRange(cntxt, pos)); 
    sc = ReScore(cw.orig_word, sc, cw.token.stat());
    sc -= m_opt.ShortFragmentPenalty * IsShortFragment(cw);
    return sc;
}


} // NJamSpell
