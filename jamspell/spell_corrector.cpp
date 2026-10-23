#include "spell_corrector.hpp"
#include "utils.hpp"

#include <algorithm>
#include <numeric>
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
    setValFromEnv(opt.InFreqWordCntThreshold, "SPLL_INFREQ_THRESHOLD");
    
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

void TSpellCorrector::Fix(context_t & cntxt) const
{
    for (auto orig_it = cntxt.begin(), e = cntxt.end()
        ; orig_it != e 
        ; orig_it = FixSentence(GetNextSent(orig_it, e))
    )
    {}
}

context_t TSpellCorrector::Fix(std::wstring const & text) const 
{
    context_t cntxt;
    Text2Context(text, cntxt);
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

concat_inf_t TSpellCorrector::PeelWord(token_info_t & tinf) const
{
    concat_inf_t cinf;
    wstr_view_t ts = tinf.str();
    for (auto i = ts.begin()
        ; (i != ts.end()) && GetAlphabet().isPunct(*i) 
        ; ++i, ++cinf.left 
    )
    {}

    for (auto i = ts.rbegin()
        ; (i != ts.rend()) && GetAlphabet().isPunct(*i) 
        ; ++i, ++cinf.right 
    )
    {}

    return cinf;    
}

void TSpellCorrector::InitContextWordFromToken(cntxt_word_t & w) const
{
    if(w.token.size() < MAX_WORD_LENGTH * 2u   // concat of two longest words!!!
        && ToAlphabet(GetAlphabet(), w.token.str(), w.orig_word.str, w.token.stat())
    )
    {
        w.orig_word.kind = cand_kind_t::ckOrig;

        w.concat = PeelWord(w.token);
        if(w.concat.left == w.token.size())
        {
            w.concat.right = 0u;
            return ;
        }

        w.orig_word.reset (GetLangModel().GetWordInfo(w.get_peeled()));

        return;
    }
    // handle long or ill-formed words
    w.orig_word.str = str_t{};
    w.orig_word.id = word_id_t::Any;
    w.orig_word.kind = cand_kind_t::ckNone;
}

void TSpellCorrector::Text2Context(wstr_view_t const & txt, context_t & cntxt) const
{
    ReserveWords(cntxt, txt);
    GetTokenizer().Parse(txt, cntxt, TTokenizer::sep_type(TTokenizer::SPACES));
    for (cntxt_word_t & cw : cntxt)
    {
        InitContextWordFromToken(cw);
    }
}

context_t::iterator 
TSpellCorrector::FixSentence(context_range_t const & curr_sent_ctxt ) const
{
    using pos_vec_t = ::std::vector<::std::uint32_t>;

    pos_vec_t positions(curr_sent_ctxt.size());
    ::std::iota(positions.begin(), positions.end(), 0u);
    ::std::sort(positions.begin(), positions.end()
        , [&curr_sent_ctxt] (::std::uint32_t const lhs, ::std::uint32_t const rhs) -> bool 
        {return curr_sent_ctxt[lhs].token.size() > curr_sent_ctxt[rhs].token.size();}
    );

    fix_attrs_t fix_attrs;
    for ( auto pos_it = positions.begin(); pos_it != positions.end(); ++pos_it) 
    {
        cand_word_t const & orig = curr_sent_ctxt[*pos_it].orig_word;
        if (orig.is_none() || !orig.is_word())
        {
            continue;
        }

        ProcessCandidates(curr_sent_ctxt, *pos_it, fix_attrs);        
    }        
    return curr_sent_ctxt.end();
}


bool TSpellCorrector::isSentEnd(context_t::iterator b
    , context_t::iterator const & e
) const noexcept
{
    wchar_t const tb = b -> token.back();
    ++b;

    return (b == e)
    || TTokenizer::isHardSentEnd(tb) 
    || (TTokenizer::isSoftSentEnd(tb)
        &&  (GetAlphabet().isUpper(b -> token.front()) )
       ); // LISPanyk is happy! =)
}

context_range_t TSpellCorrector::GetNextSent(context_t::iterator const & b
    , context_t::iterator const & e
) const
{
    for(context_t::iterator i = b;true ;)
    {
        if(isSentEnd( i++, e ))
        {
            return context_range_t{b, i};
        }
    }
    return context_range_t{b, e};
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
    , fix_attrs_t & fix_attrs
) const
{
    BOOST_ASSERT_MSG (position < context.size(), "position is out of range!");

    cntxt_word_t & cw = context[position];
    cand_word_t & ow = cw.orig_word;
    if(!ow.good())
    {
        return;
    }
    
    cw.attrs.orig_is_known = !ow.unknown();    
    ScoreOrig(fix_attrs, context, position);
    str_view_t const ow_str = cw.get_peeled();

    JS_TRACE_MSG(std::cerr << "[debug] Scored orig: \'" 
        << w_to_u8(FromAlphabet(GetAlphabet(), ow_str)) 
        << "\' id = " << static_cast<std::uint32_t> (ow.id) 
        << " count = " << ow.cnt << " score = " <<ow.score << "\n"
    );    
    
    // PITIPITIPIW WIW WIW
    TCandMgr cndMgr(cw.candidates, m_opt.MaxCandidatesToCheck);
    
    if(cw.concat.left == ow.str.size() || (cw.concat.size() > 3u)) 
    {
        // word consists of punctuations only
        CheckSwitchedCands(cw, cndMgr);  
    }
    else
    {
        
        if(ow_str.size() > 1u)
        {
            Edits2(ckFirstLvl, ow_str, cndMgr, cw.concat);
        }
        
        if (    fix_attrs.sw_cnt 
            ||  !cw.attrs.orig_is_known
            ||  CandidatesAreInfreq(cndMgr)
            ||  LangModel.TokenIsBad(ow_str, cw.token.stat())
        )
        {
            CheckSwitchedCands(cw, cndMgr);   
        }
        
        if(CandidatesAreInfreq(cndMgr))
        {
            Edits(ckSecondLvl, ow_str, cndMgr, cw.concat);
        }
        
    }
}

bool TSpellCorrector::ProcessCandidates(context_range_t const & cntxt
    , ::std::size_t const pos
    , fix_attrs_t & fix_attrs
) const
{ 
    FormCandidates(cntxt, pos, fix_attrs);
    Score(fix_attrs, cntxt, pos);
    bool const was_reset = cntxt[pos].reset_best_cand();
    fix_attrs.sw_cnt += cntxt[pos].get_best_cand().was_switched();
    return was_reset;
}

::std::size_t 
TSpellCorrector::CheckSwitchedCands (cntxt_word_t & cw 
    , TCandMgr & cmgr
) const
{
    ::std::size_t l = 0u, added_cnt = 0u;
    do
    {
        added_cnt += CheckSwitchedCandsRight(cw, cmgr, l);        
    }
    while (++l <= cw.concat.left && (!added_cnt || CandidatesAreInfreq(cmgr)));

    return added_cnt;
}

::std::size_t TSpellCorrector::CheckSwitchedCandsRight (cntxt_word_t & cw 
    , TCandMgr & cmgr
    , ::std::size_t const l
) const
{
    str_view_t ostr {cw.orig_word.str};
    str_t const s = PuntoSwitcher(ostr.substr(l));     
    if( s.empty() )
    {
        return 0u;
    }
    
    std::size_t added_cnt = 0u;
    word_t w = LangModel.LongestPrefixSearch(s);
    if(!w.unknown())
    {
        //check, if the longest prefix is at least long as context[position].str
        if( w.str.size() >=  (ostr.size() - l - cw.concat.right) )
        {
            concat_inf_t const nci (l, ostr.size() - l - w.str.size());
            cw.attrs.sw_orig_is_known = true;
            added_cnt += Push2Candidates(ckOrigSw, std::move(w.str), w, cmgr, nci);
            if(!IsInfreq(w))
            {
                return added_cnt;
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

    str_view_t cand_str = s;
    concat_inf_t ci (l, cw.concat.right);
    ::std::size_t cnt = 0u;
    do
    {
        added_cnt += Edits2(ckFirstLvlSw, cand_str, cmgr, ci);
        if((added_cnt && !CandidatesAreInfreq(cmgr)) || !(ci.right--))
        {
            break;
        }
        cand_str.remove_suffix(1u);
    }
    while ( ++cnt <= 3u);

    return added_cnt;
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
        << w_to_u8(FromAlphabet(GetAlphabet(), w)) << "\' to \'" 
        << w_to_u8(FromAlphabet(GetAlphabet(), s)) << "\'\n"
    );
    if(s == w)
    {
        s = str_t{};
    }
    return s;
}

void TSpellCorrector::Edits(cand_kind_t const ck
    , str_view_t const& word
    , TCandMgr & candidates
    , concat_inf_t const & di
) const 
{
    JS_TRACE_MSG(std::cerr << "[debug] Edits (2-letters) candidates for word: \'" 
        << w_to_u8(FromAlphabet(GetAlphabet(), word)) << "\'\n" 
    );

    del2_vec_t cands = GetDeletes2(word);
    cands.emplace_back(1u, str_t{word});

    str_t buf;
    for (auto&& w1: cands) 
    {
        for (auto&& w: w1) 
        {
            LookupAndAppend2Candidates(ck, w, candidates, di);
            if (Deletes1->Contains(w)) 
            {
                Inserts(ck, w, candidates, buf, di);
            }
            if (Deletes2->Contains(w)) 
            {
                Inserts2(ck, w, candidates, di);
            }
        }
    }
}

std::size_t TSpellCorrector::Edits2(cand_kind_t const ck
    , str_view_t const & word
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
            cnt_added += LookupAndAppend2Candidates(ck, s, candidates, di);
        }

        // transpose
        if (i + 1u < wsz) 
        {
            ((s = word.substr(0, i)) += word[i + 1]) += word[i];
            if (i + 2u < wsz) 
            {
                s += word.substr( i + 2u);
            }
            cnt_added += LookupAndAppend2Candidates(ck, s, candidates, di);
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
                cnt_added += LookupAndAppend2Candidates(ck, s, candidates, di);
            }
        }

        // inserts
        {
            ((s = word.substr(0, i)) += ' ') += word.substr(i);
            for (auto&& ch: LangModel.GetAlphabet()) 
            {
                //((s = word.substr(0, i)) += ch) += word.substr(i);
                s[i] = ch;
                cnt_added += LookupAndAppend2Candidates(ck, s, candidates, di);
            }
        }
    }

    return cnt_added;

}

void TSpellCorrector::InsertsImpl(cand_kind_t const ck
    , str_view_t const & w
    , std::size_t const i
    , TCandMgr& result
    , str_t & s
    , concat_inf_t const & di
) const 
{
    for (char const ch: LangModel.GetAlphabet()) 
    {       
        ((s = w.substr(0, i)) += ch) += w.substr(i);
        LookupAndAppend2Candidates(ck, s, result, di);
    }
}

void TSpellCorrector::Inserts(cand_kind_t const ck
    , str_view_t const & w
    , TCandMgr& result
    , str_t & buf
    , concat_inf_t const & di
) const 
{
    std::size_t const sz (w.size() + 1) ;
    for (size_t i = 0; i < sz; ++i) 
    {
        InsertsImpl(ck, w, i, result, buf, di);
    }
}

void TSpellCorrector::Inserts2Impl(cand_kind_t const ck
    , str_view_t const & w
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
            Inserts(ck, s, result, buf, di);
        }
    }
}

void TSpellCorrector::Inserts2(cand_kind_t const ck
    , str_view_t const & w
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
        Inserts2Impl(ck, w, i, result, s, buf, di);
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

bool TSpellCorrector::Push2Candidates (cand_kind_t const ck
    , str_t && w
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

    return cmgr.insert(ck, wd, std::move(w), di);
}

bool TSpellCorrector::Append2Candidates (cand_kind_t const ck
    , str_view_t const & w
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
        return cmgr.insert(ck, wd, w, di);
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

void TSpellCorrector::Score(fix_attrs_t const & fixa
    , context_range_t const & context
    , std::size_t const pos
) const
{
    auto const & cand_sent = GetSentenceRange(context, pos);
    cntxt_word_t & cword = context[pos];
    for (::std::size_t i = 0; i < cword.candidates.size(); ++i)
    {
        cand_word_t & cnd = cword.set_best_cand(i);
        JS_TRACE_MSG(std::cerr << "[debug] Scored candidate: \'" 
            << w_to_u8(FromAlphabet(GetAlphabet(), cnd.str))
        );
        cnd.score = LangModel.Score(cand_sent.begin(), cand_sent.end());
        ScoreCandidate(fixa, cword, cnd);

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

void 
TSpellCorrector
::ScoreCandidate (fix_attrs_t const & fixa
    , cntxt_word_t const & ctx_word
    , cand_word_t & cnd
) const
{
    switch (cnd.kind)
    {
        case ckOrigSw:
        {
            cnd.score -= (ctx_word.attrs.orig_is_known) ? 
                    m_opt.OrigWordIsKnownPenalty 
                :   m_opt.OrigWordIsUnknownPenalty;
            //break;
        }

        case ckOrig:
        {
            cnd.score -= (!fixa.sw_cnt) * m_opt.SwitchedWordPenalty;
            break;
        }

        case ckFirstLvl:
        {
            cnd.score -= (ctx_word.attrs.orig_is_known) ? 
                    m_opt.OrigWordIsKnownPenalty
                :   m_opt.OrigWordIsUnknownPenalty;
            break;
        }    
        
        case ckSecondLvl:
        {
            cnd.score = (ctx_word.attrs.orig_is_known) ? 
                (cnd.score * m_opt.SecondLvlPenFactor) 
            :   (cnd.score - m_opt.OrigWordIsUnknownPenalty - m_opt.SecondLvlPenalty);
            break;
        }

        case ckFirstLvlSw:
        {
            cnd.score -= (ctx_word.attrs.orig_is_known) ? 
                    m_opt.OrigWordIsKnownPenalty 
                :   m_opt.OrigWordIsUnknownPenalty;
            cnd.score -= ((!fixa.sw_cnt) * m_opt.SwitchedWordPenalty 
                    + ctx_word.attrs.sw_orig_is_known * m_opt.SwitchedWordIsKnownPenalty
                );
            break;
        }

        case ckSecondLvlSw:
        {
            cnd.score = (ctx_word.attrs.orig_is_known) ? 
                (cnd.score * m_opt.SecondLvlPenFactor) 
            :   (cnd.score - m_opt.OrigWordIsUnknownPenalty - m_opt.SecondLvlPenalty);
            cnd.score -= ((!fixa.sw_cnt) * m_opt.SwitchedWordPenalty 
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
    ReScore(cnd, ts);
    cnd.score -= std::abs(long(cnd.str.size()) - ctx_word.token.size()) 
        * m_opt.LengthMismatchPenaltyFactor;
}

void TSpellCorrector::ScoreOrig(fix_attrs_t const & fixa
    , context_range_t const & cntxt
    , ::std::size_t const pos
) const
{
    cntxt_word_t & cw = cntxt[pos];
    float sc = LangModel.Score(GetSentenceRange(cntxt, pos)); 
    ReScore(cw.orig_word, cw.token.stat());
    cw.orig_word.score -= (!fixa.sw_cnt) * m_opt.SwitchedWordPenalty;
    cw.orig_word.score -= cw.concat.left * m_opt.LengthMismatchPenaltyFactor;
}


} // NJamSpell
