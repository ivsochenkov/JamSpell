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
        results.push_back(std::move(nw));
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
            currResults.push_back(std::move(nw));
            results.push_back(std::move(currResults));
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
    , const std::string& alphabetFile
    , const std::string& modelFile
) 
{
    if (!LangModel.Train(textFile, alphabetFile)) {
        return false;
    }
    PrepareCache();
    if (!LangModel.Dump(modelFile)) {
        return false;
    }
    std::string cacheFile = modelFile + ".spell";
    if (!SaveCache(cacheFile)) {
        return false;
    }
    return true;
}


// bool TSpellCorrector::WordIsKnown( str_view_t const & word) const 
// {
//     return !LangModel.GetWordInfo(word).unknown();
// }

candidates_t TSpellCorrector::GetCandidates(candidates_range_t const & context
    , size_t const position
) const
{
    BOOST_ASSERT_MSG (position < orig_sent.size(), "position is out of range!");

    cand_word_t & orig_word = context[position];
    orig_word.score = ScoreOrig(context, position);
    JS_TRACE_MSG(std::cerr << "[debug] Scored orig: \'" 
        << wide_to_utf8_t{}(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), orig_word.str)
        ) << "\' weight = " << orig_word.score << "\n"
    );

    candidates_t candidates;
    TCandMgr cndMgr(candidates, MaxCandidatesToCheck);
   
    FormEditsCandidates(ckFirstLvl, orig_word.str, cndMgr);
    
    bool sw_orig_is_known = false;
    // TODO: make punto candidtes optional!
    {
        str_t sw_word_str = PuntoSwitcher(orig_word.str);
        if(!sw_word_str.empty())
        {
            FormEditsCandidates(ckFirstLvlSw, sw_word_str, cndMgr);
            // add original switched word finally
            sw_orig_is_known = ! (
                cndMgr.push(LangModel.GetWordInfo(sw_word_str)
                    , std::move(sw_word_str), ckOrigSw
                ).unknown()
            );
            // swc.score = ScoreOrigSwitched(swc, context, position);
        }        
    }
  
    Score(context, position, candidates, sw_orig_is_known);
    std::sort(candidates.begin(), candidates.end()
        , [] (cand_word_t const & lhs, cand_word_t const & rhs) 
            {return lhs.score > rhs.score;}
    );
    return candidates;
}

candidates_t TSpellCorrector::InitContext(text_tokens_t const & orig_txt_tok) const
{
    candidates_t ctx;
    LangModel.InitWords(orig_txt_tok, ctx);

    for (auto & c : ctx)
    {
        c.kind = ckOrig;
    }
    return ctx;
}




void TSpellCorrector::AppendWithCase(std::wstring & result
    , std::wstring_view const & origWord
    , str_view_t const & newWord
) const
{
    std::size_t const pos {result.size()};
    result.resize(pos + newWord.size());

    for (std::size_t k = 0; k < newWord.size(); ++k) 
    {
        wchar_t const origChar = (k < origWord.size()) ? origWord[k] : origWord.back() ;
        result[pos + k] = MakeUpperIfRequired(
                LangModel.GetAlphabet().Ch2Wch(newWord[k])
            , origChar
        );
    }
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

std::wstring TSpellCorrector::FixFragment(const std::wstring& text) const 
{
    JS_TRACE_MSG(std::cerr << "[debug] will fix fragment: \'" 
        << wide_to_utf8_t{}(text) << "\'\n"
    );

    std::wstring_view const orig_txt(text);
    text_tokens_t orig_txt_tokens = LangModel.GetTokenizer().Parse(text);
    LangModel.GetTokenizer().FilterAndJoin(orig_txt_tokens);
    candidates_t txt_words {InitContext(orig_txt_tokens)};
    assert(txt_words.size() == orig_txt_tokens.size());

    std::wstring result;
    result.reserve(text.size() * 1.1 + 7); // 640 Kb should be enough for all!

    size_t origPos = 0;
    for (auto orig_it = orig_txt_tokens.cbegin(), e = orig_txt_tokens.cend()
        ; orig_it < e 
        ; ++orig_it // see the last line marked with !!!. We omit sent end token
                    // and proceed to next sentence begin
    )
    {
        auto orig_sent = GetNextSent(orig_it, e);

        candidates_range_t curr_sent_ctxt {
            MapSentence(txt_words, orig_txt_tokens, orig_sent)
        };
        std::size_t j = 0;
        for ( auto al_word_it = curr_sent_ctxt.begin()
            ; al_word_it != curr_sent_ctxt.end()
            ; ++j, ++al_word_it
        ) 
        {
            if (al_word_it -> str.empty())
            {
                continue;
            }

            cand_word_t & curr_word = *al_word_it;
            candidates_t candidates {GetCandidates(curr_sent_ctxt, j)};
            bool kept_orig = true;
            if (!candidates.empty()) 
            {
                cand_word_t & top_w = candidates.front();
                if(curr_word.score < top_w.score && top_w.id != curr_word.id)
                {
                    //curr_word.str = top_w.str;
                    //curr_word.id = top_w.id;
                    //curr_word.score = top_w.score;
                    //curr_word.weight = LangModel.GetWordInfo(curr_word.str).weight;
                    curr_word = std::move(top_w);
                    kept_orig = false;
                }
            }

            if(kept_orig)
            {
                continue;
            }

            size_t const currOrigPos = getOffset(orig_sent[j].data(), orig_txt);
            result += orig_txt.substr(origPos, currOrigPos - origPos);
            origPos = currOrigPos;
            
            std::size_t const adv_sz = orig_sent[j].size();
            std::wstring_view const & origWord(getOrigWord(text, currOrigPos, adv_sz));
            AppendWithCase(result, origWord, curr_word.str); 
            origPos += adv_sz;
        }
        orig_it = orig_sent.end();  // !!!
    }
    result += orig_txt.substr(origPos, text.size() - origPos);
    JS_TRACE_MSG(std::cerr << "[debug] fixed result: \'" 
        << wide_to_utf8_t{}(result) << "\'\n"
    );
    return result;
}

void TSpellCorrector::SetPenalty(double knownWordsPenalty, double unknownWordsPenalty) 
{
    OrigWordIsKnownPenalty = knownWordsPenalty;
    OrigWordIsUnknownPenalty = unknownWordsPenalty;
}

void TSpellCorrector::SetMaxCandidatesToCheck(size_t maxCandidatesToCheck) 
{
    MaxCandidatesToCheck = maxCandidatesToCheck;
}


str_t TSpellCorrector::PuntoSwitcher(str_view_t const &w) const
{ 
    str_t s = FribbulusXax(LangModel.GetAlphabet(), w);
    JS_TRACE_MSG(std::cerr << "[debug] Switched kb-layout for word: \'" 
        << wide_to_utf8_t{}(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), w)
        ) << "\' to \'" 
        << wide_to_utf8_t{}(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), s)
        ) << "\'\n"
    );
    if(s == w)
    {
        s = str_t{};
    }
    return s;
}

void TSpellCorrector::FormEditsCandidates(cand_kind_t const ck
    , str_view_t const & s
    , TCandMgr & result
) const
{
    result.set_kind (ck);
    std::size_t const cnt_addd {Edits2(s, result)};
    if (!cnt_addd) //if (result.empty()) 
    {
        result.set_kind (NextLevel(ck));
        Edits(s, result);
    }
}

void TSpellCorrector::Edits(str_view_t const& word
    , TCandMgr & candidates
) const 
{
    JS_TRACE_MSG(std::cerr << "[debug] Edits (2-letters) candidates for word: \'" 
        << wide_to_utf8_t{}(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), word)
        ) << "\'\n" 
    );

    del2_vec_t cands = GetDeletes2(word);
    cands.emplace_back(1, str_t{word});

    str_t buf;
    for (auto&& w1: cands) 
    {
        for (auto&& w: w1) 
        {
            LookupAndAppend2Candidates(w, candidates);
            if (Deletes1->Contains(w)) 
            {
                Inserts(w, candidates, buf);
            }
            if (Deletes2->Contains(w)) 
            {
                Inserts2(w, candidates);
            }
        }
    }
}

std::size_t TSpellCorrector::Edits2(str_view_t const & word
    , TCandMgr & candidates
) const 
{
    JS_TRACE_MSG(std::cerr << "[debug] Edits (1-letters) candidates for word: \'" 
        << wide_to_utf8_t{}(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), word)
        ) << "\'\n" 
    );

    std::size_t cnt_added {0u};

    str_view_t const & w(word);

    str_t s;
    s.reserve(w.size() + 1u);    // one size fits all!
    for (size_t i = 0; i < w.size() + 1; ++i) 
    {
        // delete
        if (i < w.size()) 
        {
            (s = w.substr(0, i)) += w.substr(i+1);
            cnt_added += LookupAndAppend2Candidates(s, candidates);
        }

        // transpose
        if (i + 1 < w.size()) 
        {
            ((s = w.substr(0, i)) += w[i + 1]) += w[i];
            if (i + 2 < w.size()) 
            {
                s += w.substr(i+2);
            }
            cnt_added += LookupAndAppend2Candidates(s, candidates);
        }

        // replace
        if (i < w.size()) 
        {
            TAlphabet::subs_type const & sbt = LangModel.GetAlphabet().GetSubstitutes(w[i]);
            JS_TRACE_MSG(std::cerr << "[debug] substitutes for letter \'"
                << wide_to_utf8_t{}(std::wstring(1, LangModel.GetAlphabet().Ch2Wch(w[i]))) 
                << "\': " << wide_to_utf8_t{}(
                    FromAlphabet(LangModel.GetAlphabet(), str_view_t(sbt.data(), sbt.size()) )) 
                << "\'\n" 
            );
            for (auto&& ch: sbt) 
            {
                ((s = w.substr(0, i)) += ch) += w.substr(i+1);
                cnt_added += LookupAndAppend2Candidates(s, candidates);
            }
        }

        // inserts
        {
            for (auto&& ch: LangModel.GetAlphabet()) 
            {
                ((s = w.substr(0, i)) += ch) += w.substr(i);
                cnt_added += LookupAndAppend2Candidates(s, candidates);
            }
        }
    }

    return cnt_added;

}

void TSpellCorrector::InsertsImpl(str_view_t const & w
    , std::size_t const i
    , TCandMgr& result
    , str_t & s
) const 
{
    for (char const ch: LangModel.GetAlphabet()) 
    {       
        ((s = w.substr(0, i)) += ch) += w.substr(i);
        LookupAndAppend2Candidates(s, result);
    }
}

void TSpellCorrector::Inserts(str_view_t const & w
    , TCandMgr& result
    , str_t & buf
) const 
{
    std::size_t const sz (w.size() + 1) ;
    for (size_t i = 0; i < sz; ++i) 
    {
        InsertsImpl(w, i, result, buf);
    }
}

void TSpellCorrector::Inserts2Impl(str_view_t const & w
    , std::size_t const i
    , TCandMgr& result
    , str_t & s
    , str_t & buf
) const 
{
    for (char const ch: LangModel.GetAlphabet()) 
    {        
        ((s = w.substr(0, i)) += ch) += w.substr(i);
        if (Deletes1->Contains(s)) 
        {
            Inserts(s, result, buf);
        }
    }
}

void TSpellCorrector::Inserts2(str_view_t const & w
    , TCandMgr& result
) const 
{
    std::size_t const sz (w.size() + 1u);
    str_t s, buf;
    s.reserve(sz);
    buf.reserve (sz + 1u);
    for(std::size_t i = 0; i < sz; ++i)
    {
        Inserts2Impl(w, i, result, s, buf);
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

bool TSpellCorrector::LookupAndAppend2Candidates(str_view_t const & w
    , TCandMgr & candidates
) const
{
    wdata_t const wd = LangModel.GetWordInfo(w);

    JS_TRACE_MSG(std::cerr << "[debug] synthesized candidate: \'" 
        << wide_to_utf8_t{}(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), w)) 
        << "\' id = " << static_cast<uint32_t>(wd.id) 
        << " count = " << wd.cnt << "\n"
    );

    if (! wd.unknown() ) 
    {
        return candidates.insert(wd, w);
        //return true;
    }
    return false;
}

candidates_crange_t TSpellCorrector::GetSentenceRange(
      candidates_range_t const & sentence
    , std::size_t const pos
) const
{
    auto beg_it = sentence.begin(), end_it = beg_it;
    std::advance(beg_it, std::max(long (pos) - 2l, 0l));
    std::advance(end_it, std::min(pos + 3ul , sentence.size()));
    return candidates_crange_t{beg_it, end_it};
}

float TSpellCorrector::ScoreOrig(candidates_range_t const & orig_sent
    , std::size_t const pos
) const
{
    return LangModel.Score(GetSentenceRange(orig_sent, pos));
}

void TSpellCorrector::Score(candidates_range_t const & context
    , std::size_t const pos
    , candidates_t & candidates
    , bool const sw_orig_is_known
) const
{
    auto const & cand_sent = GetSentenceRange(context, pos);
    cand_word_t & oword = context[pos];
    wdata_t const odat = oword.wdata(); // store copy!
    
    bool const orig_is_known = !odat.unknown();

    for (cand_word_t & cnd: candidates) 
    {
        oword.reset(cnd.wdata());

        cnd.score = ScoreCandidate(LangModel.Score(cand_sent.begin(), cand_sent.end())
           , cnd.kind, orig_is_known, sw_orig_is_known
        );
        /*
        (orig_is_known) ?
                ((cand.first_level) ? 
                        (sc - OrigWordIsKnownPenalty) 
                    :   (sc * SecondLvlPenFactor)) 
            :   (sc - OrigWordIsUnknownPenalty);
        */

        JS_TRACE_MSG(std::cerr << "[debug] Scored candidate: \'" 
            << wide_to_utf8_t{}(
                FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), cnd.str)
            ) << "\' weight = " << cnd.score << "\n"
        );
    }

    oword.reset(odat); // restore original state
}

double TSpellCorrector::ScoreCandidate (double sc
    , cand_kind_t const ck
    , bool const orig_is_known
    , bool const sw_orig_is_known 
) const
{
    switch (ck)
    {
        case ckOrigSw:
        {
            sc = (orig_is_known) ? 
                (sc - OrigWordIsKnownPenalty) 
            :   (sc - OrigWordIsUnknownPenalty);
            sc -= SwitchedWordPenalty;
            break;
        }
        case ckFirstLvl:
        {
            sc = (orig_is_known) ? 
                (sc - OrigWordIsKnownPenalty) 
            :   (sc - OrigWordIsUnknownPenalty);
            break;
        }               
        case ckSecondLvl:
        {
            sc = (orig_is_known) ? 
                (sc * SecondLvlPenFactor) 
            :   (sc - OrigWordIsUnknownPenalty - SecondLvlPenalty);
            break;
        }

        case ckFirstLvlSw:
        {
            sc = (orig_is_known) ? 
                (sc - OrigWordIsKnownPenalty) 
            :   (sc - OrigWordIsUnknownPenalty);
            sc -= (SwitchedWordPenalty + sw_orig_is_known * SwitchedWordIsKnownPenalty);
            break;
        }

        case ckSecondLvlSw:
        {
            sc = (orig_is_known) ? 
                (sc * SecondLvlPenFactor) 
            :   (sc - OrigWordIsUnknownPenalty - SecondLvlPenalty);
            sc -= (SwitchedWordPenalty + sw_orig_is_known * SwitchedWordIsKnownPenalty);
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
