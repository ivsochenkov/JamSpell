#include <algorithm>
#include <fstream>

#include "spell_corrector.hpp"
#include "utils.hpp"

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


bool TSpellCorrector::WordIsKnown( str_view_t const & word) const 
{
    return !LangModel.GetWordInfo(word).unknown();
}

words_seq_t TSpellCorrector::GetCandidates(word_seq_range_t const & orig_sent
    , size_t const position
) const
{
    if (position >= orig_sent.size()) // TODO move to the higher level ?
    {
        return words_seq_t{};
    }

    word_info_t & orig_word = orig_sent[position];
    words_seq_t cands = ProcessCandidates(!orig_word.unknown(), orig_sent, position);
    
    word_info_t const oword = orig_word;    // make a copy!
    words_seq_t sw_cands;
    {
        str_t switched_word = PuntoSwitcher(orig_word.str);
        if(!switched_word.empty())
        {
            ReInitWord(orig_word, std::move(switched_word));
            sw_cands = ProcessCandidates(!oword.unknown(), orig_sent, position);
        }        
    }
    orig_word = std::move(oword);  // restore from copy!
    Merge(cands, sw_cands);
    return cands;
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

std::vector<std::pair<std::wstring,double> > 
TSpellCorrector::GetCandidatesWithScores(
    const std::vector<std::wstring>& sentence,
    size_t const position
) const 
{
    text_tokens_t txt_tokens(sentence.begin(), sentence.end());
    words_seq_t in_sent = LangModel.InitWords(txt_tokens);
    words_seq_t const & scoredCandidates = GetCandidates(in_sent, position);

    std::vector<std::pair<std::wstring,double> > results (scoredCandidates.size());
    auto it = results.begin();
    for (auto s: scoredCandidates) 
    {
        *it++ = std::make_pair(
                FromAlphabet(LangModel.GetAlphabet(), s.str)
            ,   s.weight
        );
    }
    return results;
}

std::vector<std::wstring> 
TSpellCorrector::GetCandidates(const std::vector<std::wstring>& sentence
    , size_t const position
) const 
{
    text_tokens_t txt_tokens(sentence.begin(), sentence.end());
    words_seq_t in_sent = LangModel.InitWords(txt_tokens);

    words_seq_t const & scoredCandidates = GetCandidates(in_sent, position);
    std::vector<std::wstring> results(scoredCandidates.size());
    auto it = results.begin();
    for (auto&& c: scoredCandidates) 
    {
        *it++ = FromAlphabet(LangModel.GetAlphabet(), c.str);
    }
    return results;
}

std::wstring TSpellCorrector::FixFragment(const std::wstring& text) const 
{
    std::wstring_view const orig_txt(text);
    text_tokens_t orig_txt_tokens = LangModel.GetTokenizer().Parse(text);
    LangModel.GetTokenizer().FilterAndJoin(orig_txt_tokens);
    words_seq_t txt_words = LangModel.InitWords(orig_txt_tokens);
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

        word_seq_range_t curr_sent(MapSentence(txt_words, orig_txt_tokens, orig_sent));
        std::size_t j = 0;
        for ( auto al_word_it = curr_sent.begin()
            ; al_word_it != curr_sent.end()
            ; ++j, ++al_word_it
        ) 
        {
            if (al_word_it -> str.empty())
            {
                continue;
            }

            word_info_t & curr_word = *al_word_it;
            words_seq_t const &candidates = GetCandidates(curr_sent, j);
            bool kept_orig = true;
            if (!candidates.empty()) 
            {
                word_info_t const & top_w = candidates.front();
                if(curr_word.id != top_w.id)
                {
                    curr_word.str = top_w.str;
                    curr_word.id = top_w.id;
                    curr_word.weight = LangModel.GetWordInfo(curr_word.str).weight;
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

const TLangModel& TSpellCorrector::GetLangModel() const {
    return LangModel;
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

void TSpellCorrector::ReInitWord(word_info_t & word, str_t && new_swtch_word) const
{
    word = LangModel.GetWordInfo(new_swtch_word);
    word.str = std::move( new_swtch_word );
}

void TSpellCorrector::FormEditsCandidates(word_info_t const & word
    , TCandMgr & candidates
) const
{
    Edits2(word.str, candidates);  // MAY BE last_level = false
    if (candidates.empty()) 
    {
        Edits(word.str, candidates);
    } 
}

void TSpellCorrector::Edits(str_view_t const& word, TCandMgr & candidates) const 
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
            LookupAndAppend2Candidates(false, w, candidates);
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

void TSpellCorrector::Edits2(str_view_t const & word
    , TCandMgr & candidates
) const 
{
    JS_TRACE_MSG(std::cerr << "[debug] Edits (1-letters) candidates for word: \'" 
        << wide_to_utf8_t{}(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), word)
        ) << "\'\n" 
    );

    str_view_t const & w(word);

    str_t s;
    s.reserve(w.size() + 1u);    // one size fits all!
    for (size_t i = 0; i < w.size() + 1; ++i) 
    {
        // delete
        if (i < w.size()) 
        {
            (s = w.substr(0, i)) += w.substr(i+1);
            LookupAndAppend2Candidates(true, s, candidates);
            // if (!lastLevel) 
            // {
            //     Edits2(s, candidates, false, true);
            // }
        }

        // transpose
        if (i + 1 < w.size()) 
        {
            ((s = w.substr(0, i)) += w[i + 1]) += w[i];
            if (i + 2 < w.size()) 
            {
                s += w.substr(i+2);
            }
            LookupAndAppend2Candidates(true, s, candidates);
            // if (!lastLevel) 
            // {
            //     Edits2(s, candidates, false, true);
            // }
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
                LookupAndAppend2Candidates(true, s, candidates);
                // if (!lastLevel) 
                // {
                //     Edits2(s, candidates, false, true);
                // }
            }
        }

        // inserts
        {
            for (auto&& ch: LangModel.GetAlphabet()) 
            {
                ((s = w.substr(0, i)) += ch) += w.substr(i);
                LookupAndAppend2Candidates(true, s, candidates);
                // if (!lastLevel) 
                // {
                //     Edits2(s, candidates, false, true);
                // }
            }
        }
    }

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
        LookupAndAppend2Candidates(false, s, result);
    }
}

void TSpellCorrector::Inserts(str_view_t const & w, TCandMgr& result, str_t & buf) const 
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

void TSpellCorrector::Inserts2(str_view_t const & w, TCandMgr& result) const 
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
    if (magicByte != SPELL_CHECKER_CACHE_MAGIC_BYTE) {
        return false;
    }
    Deletes1 = std::move(deletes1);
    Deletes2 = std::move(deletes2);
    return true;
}

bool TSpellCorrector::SaveCache(const std::string& cacheFile) {
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

bool TSpellCorrector::LookupAndAppend2Candidates(bool const first_level
    , str_view_t const & w, TCandMgr & candidates
) const
{
    JS_TRACE_MSG(std::cerr << "[debug] synthesized candidate: \'" 
        << wide_to_utf8_t{}(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), w)) 
        << "\'\n" 
    );
    word_info_t c = LangModel.GetWordInfo(w);
    if (c) 
    {
        c.first_level = first_level;
        c.str = w;
        candidates.insert(std::move(c));
        return true;
    }
    return false;
}

TSpellCorrector::sent_range TSpellCorrector::GetSentenceRange(
      word_seq_range_t const & sentence
    , std::size_t const pos
) const
{
    word_info_t const * pbeg = &(sentence.front()), * pend = pbeg;
    std::advance(pbeg, std::max(long (pos) - 2l, 0l));
    std::advance(pend, std::min(pos + 3ul , sentence.size()));
    return sent_range{pbeg, pend};
}

void TSpellCorrector::Score(bool const orig_word_is_known
    , word_seq_range_t const & orig_sent
    , std::size_t const pos
    , words_seq_t & candidates
) const
{
    sent_range cand_sent = GetSentenceRange(orig_sent, pos);
    word_info_t & curr_word = orig_sent[pos]; // ~ cand_sent.begin() + 2
    word_info_t const orig_word = curr_word; // make a copy before it is too late! =)
    //bool const orig_word_is_known = (orig_word.id != TWordId::Unknown);

    for (word_info_t & cand: candidates) 
    {
        curr_word = cand;
        double const sc = LangModel.Score(cand_sent.begin(), cand_sent.end());
        cand.weight = (orig_word_is_known) ?
                ((cand.first_level) ? (sc - OrigWordIsKnownPenalty) : (sc * SecondLvlPenFactor)) 
            :   (sc - OrigWordIsUnknownPenalty);

        JS_TRACE_MSG(std::cerr << "[debug] Scored candidate: \'" 
            << wide_to_utf8_t{}(
                FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), cand.str)
            ) << "\' weight = " << cand.weight << "\n"
        );
    }

    curr_word = orig_word; // restore original state
    // Finally, we score the orig sentence as it is for now!

    candidates.emplace_back(orig_word.id, orig_word.str 
        , LangModel.Score(cand_sent.begin(), cand_sent.end())
    ); 
    JS_TRACE_MSG(std::cerr << "[debug] Scored orig: \'" 
        << wide_to_utf8_t{}(
            FromAlphabet(GetLangModel().GetTokenizer().GetAlphabet(), orig_word.str)
        ) << "\' weight = " << candidates.back().weight << "\n"
    );

    std::sort(candidates.begin(), candidates.end()
        , word_info_greater_t{}
    );
}

void TSpellCorrector::Merge(words_seq_t & a, words_seq_t & b) const
{
    if(b.empty())
    {
        return;
    }

    if(a.empty())
    {
        a = std::move(b);
        return;
    }
    
    words_seq_t res(a.size() + b.size());
    std::merge(a.begin(), a.end(), b.begin(), b.end(), res.begin(), word_info_greater_t{});
    
    a = std::move(res); 
}

words_seq_t TSpellCorrector::ProcessCandidates(bool const orig_word_is_known
    , word_seq_range_t const & orig_sent
    , size_t const position
) const
{
    words_seq_t cands;
    TCandMgr scoredCandidates (cands, MaxCandidatesToCheck);   
    FormEditsCandidates(orig_sent[position], scoredCandidates);
    Score(orig_word_is_known, orig_sent, position, cands);
    return cands;
}


} // NJamSpell
