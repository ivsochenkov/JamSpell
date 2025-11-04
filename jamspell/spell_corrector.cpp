#include <algorithm>
#include <fstream>

#include "spell_corrector.hpp"
#include "utils.hpp"

namespace NJamSpell {

inline std::size_t getOffset(wchar_t const * pos, std::wstring_view const & src)
{
    return std::distance(&src[0], pos);
}

inline std::wstring_view getOrigWord(std::wstring const &text, std::size_t pos, std::size_t len)
{
    return std::wstring_view(text).substr(pos, len);
}

static std::vector<std::wstring> GetDeletes1(const std::wstring_view& w) 
{
    std::vector<std::wstring> results;
    if(w.size() < 2)        // one-letter candidates? hmmmm
        return results;

    results.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) 
    {
        std::wstring nw;
        nw.reserve(w.size());
        nw.append(w.substr(0, i)).append (w.substr(i+1));
        results.push_back(std::move(nw));
        // if (!nw.empty()) {
        //     results.push_back(std::move(nw));
        // }
    }
    return results;
}

static std::vector<std::vector<std::wstring>> GetDeletes2(const std::wstring_view & w) 
{
    std::vector<std::vector<std::wstring> > results;
    if(w.size() < 3) // one letter candiates... Hmmm?
        return results;

    results.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) 
    {
        std::wstring nw;
        nw.reserve(w.size());
        (nw += w.substr(0, i)) += w.substr( i+1 );
        //if (!nw.empty()) 
        {
            std::vector<std::wstring> currResults = GetDeletes1(nw);
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


bool TSpellCorrector::WordIsKnown(const std::wstring_view& word) const 
{
    return !LangModel.GetWord(word).empty();
}

words_seq_t TSpellCorrector::GetCandidates(words_seq_t & orig_sent
    , size_t const position
) const
{
    if (position >= orig_sent.size()) // TODO move to the higher level ?
    {
        return words_seq_t{};
    }

    words_seq_t cands;
    TCandMgr scoredCandidates (cands, MaxCandidatesToCheck, 2048); // TODO remove magic!

    word_info_t const & orig_word = orig_sent[position];
    std::wstring_view const orig_word_str{orig_word.str};
    bool firstLevel = true;
    FormEditsCandidates(orig_word, scoredCandidates, firstLevel);
    
    /*
    TWords sw_candidates; 
    {
        std::wstring switched_word = PuntoSwitcher(w);
        if(!switched_word.empty())
        {
            sw_candidates = FormEditsCandidates(switched_word, swFirstLevel); // Todo
            sw_candidates.push_back(std::move(switched_word));
        }
    }
    */


    // TODO: swtich punto here    
    scoredCandidates.prepare_to_score();
    Score(orig_sent, position, firstLevel, cands);
    return cands;
}

TWords TSpellCorrector::GetCandidatesRaw(const TWords& sentence
    , size_t const position
) const 
{
    words_seq_t in_sent (MakeSentence(sentence));    
    words_seq_t const & scoredCandidates = GetCandidates(in_sent, position);
    TWords candidates(scoredCandidates.size());
    TWords::iterator i = candidates.begin();
    for (auto s: scoredCandidates) 
    {
        *i++ = s.str;
    }
    return candidates;
}

void TSpellCorrector::AppendWithCase(std::wstring & result
    , std::wstring_view const & origWord
    , std::wstring_view const &newWord
) const
{
    std::size_t const pos {result.size()};
    result.append(newWord);

    for (size_t k = 0; k < newWord.size(); ++k) 
    {
        wchar_t const origChar = (k < origWord.size()) ? origWord[k] : origWord.back() ;
        wchar_t & newChar = result[pos + k];
        newChar = MakeUpperIfRequired(newChar, origChar);
    }
}

void TSpellCorrector::FilterCandidatesByFrequency(
      std::unordered_set<TWord, TWordHashPtr>& uniqueCandidates
    , TWord origWord
) const 
{
    if (uniqueCandidates.size() <= MaxCandidatesToCheck) {
        return;
    }

    using TCountCand = std::pair<TCount, TWord>;
    std::vector<TCountCand> candidateCounts;
    for (auto&& c: uniqueCandidates) {
        TCount cnt = LangModel.GetWordCount(LangModel.GetWordId(c));
        candidateCounts.push_back(std::make_pair(cnt, c));
    }
    uniqueCandidates.clear();
    std::stable_sort(candidateCounts.begin(), candidateCounts.end()
        , [](const TCountCand& a, const TCountCand& b) {
            return a.first > b.first;
    });

    for (size_t i = 0; i < MaxCandidatesToCheck; ++ i) {
        uniqueCandidates.insert(candidateCounts[i].second);
    }
    uniqueCandidates.insert(origWord);
}

std::vector<std::pair<std::wstring,double> > 
TSpellCorrector::GetCandidatesWithScores(
    const std::vector<std::wstring>& sentence,
    size_t const position
) const 
{

    TWords const words(sentence.begin(), sentence.end());
    words_seq_t in_sent = MakeSentence(words);
    words_seq_t const & scoredCandidates = GetCandidates(in_sent, position);

    std::vector<std::pair<std::wstring,double> > results (scoredCandidates.size());
    auto it = results.begin();
    for (auto s: scoredCandidates) 
    {
        *it++ = std::make_pair(std::wstring(s.str), s.weight);
    }
    return results;
}

std::vector<std::wstring> 
TSpellCorrector::GetCandidates(const std::vector<std::wstring>& sentence
    , size_t const position
) const 
{
    TWords words(sentence.begin(), sentence.end());

    TWords const & candidates = GetCandidatesRaw(words, position);
    std::vector<std::wstring> results(candidates.size());
    auto it = results.begin();
    for (auto&& c: candidates) 
    {
        (it++) -> assign (c.Ptr, c.Len);
    }
    return results;
}

std::wstring TSpellCorrector::FixFragment(const std::wstring& text) const 
{
    std::wstring_view const orig_txt(text);
    std::wstring in_txt = text;
    sentences_t sentences;
    LangModel.GetTokenizer().Tokenize(in_txt, sentences);
    sentences_t orig_sentences = sentences;
    
    ToLower(in_txt);
    InitSentWords(sentences);

    std::wstring result;
    result.reserve(text.size() * 1.1 + 7);

    size_t origPos = 0;
    auto orig_sent_it = orig_sentences.begin();
    for (words_seq_t & curr_sent : sentences)
    {
        auto orig_word_it = (orig_sent_it++)->begin();
        for (size_t j = 0; j < curr_sent.size(); ++j, ++orig_word_it) 
        {
            word_info_t & curr_word = curr_sent[j];
            //word_info_t const src_word = curr_word; // store copy of orig word!
            words_seq_t const &candidates = GetCandidates(curr_sent, j);
            if (!candidates.empty()) 
            {
                word_info_t const & top_w = candidates.front();
                curr_word.str = top_w.str;
                curr_word.id = top_w.id;
            }
            size_t const currOrigPos 
                = getOffset(orig_word_it->str.data(), in_txt); // tokenized lowered text
            result += orig_txt.substr(origPos, currOrigPos - origPos);
            origPos = currOrigPos;
            
            std::wstring_view const & newWord(curr_word.str);
            std::size_t const adv_sz = orig_word_it->str.size();
            std::wstring_view const & origWord(getOrigWord(text, currOrigPos, adv_sz));

            if (newWord != origWord) 
            {
                AppendWithCase(result, origWord, newWord);           
            } 
            else 
            {
                result += origWord;
            }
            origPos += adv_sz;
        }
    }
    result += orig_txt.substr(origPos, text.size() - origPos);
    return result;
}

std::wstring TSpellCorrector::FixFragmentNormalized(const std::wstring& text) const 
{
    std::wstring in_text = text;
    TSentences sentences = LangModel.GetTokenizer().Tokenize(in_text);
    ToLower(in_text);    
    std::wstring result;
    for (size_t i = 0; i < sentences.size(); ++i) 
    {
        TWords words = sentences[i];
        for (size_t i = 0; i < words.size(); ++i) 
        {
            TWords candidates = GetCandidatesRaw(words, i);
            if (candidates.size() > 0) 
            {
                words[i] = candidates[0];
            }
            result += std::wstring(words[i].Ptr, words[i].Len) + L" ";
        }
        if (words.size() > 0) {
            result.resize(result.size() - 1);
            result += L". ";
        }
    }
    if (!result.empty()) {
        result.resize(result.size() - 1);
    }
    return result;
}

void TSpellCorrector::SetPenalty(double knownWordsPenalty, double unknownWordsPenalty) {
    KnownWordsPenalty = knownWordsPenalty;
    UnknownWordsPenalty = unknownWordsPenalty;
}

void TSpellCorrector::SetMaxCandidatesToCheck(size_t maxCandidatesToCheck) {
    MaxCandidatesToCheck = maxCandidatesToCheck;
}

const TLangModel& TSpellCorrector::GetLangModel() const {
    return LangModel;
}

template<typename T>
inline void AddVec(T& target, T&& source) 
{
    target.insert(target.end(), source.begin(), source.end());
}

std::wstring TSpellCorrector::PuntoSwitcher(TWord const &w) const
{
    std::wstring_view const ww(w.Ptr, w.Len); 
    std::wstring s = FribbulusXax(LangModel.GetAlphabet(), ww);
    if(s == ww)
    {
        s = std::wstring{};
    }
    return s;
}

void TSpellCorrector::FormEditsCandidates(word_info_t const & word
    , TCandMgr & candidates
    , bool & firstLevel
) const
{
    Edits2(word.str, candidates, true);  // MAY BE last_level = false
    firstLevel = true;
    if (candidates.empty()) 
    {
        Edits(word.str, candidates);
        firstLevel = false;
    } 
}

void TSpellCorrector::Edits(wstr_view_type const& word, TCandMgr & candidates) const 
{
    wide_to_utf8_t wide_to_utf8;

    std::vector<std::vector<std::wstring>> cands = GetDeletes2(word);
    cands.emplace_back(1, std::wstring{word});

    for (auto&& w1: cands) 
    {
        for (auto&& w: w1) 
        {
            Add2CandidatesIfSuitable(w, candidates);
            std::string const & s = wide_to_utf8(w);
            if (Deletes1->Contains(s)) 
            {
                Inserts(w, candidates);
            }
            if (Deletes2->Contains(s)) 
            {
                Inserts2(w, candidates);
            }
        }
    }
}

void TSpellCorrector::Edits2(const wstr_view_type & word
    , TCandMgr & candidates
    ,  bool lastLevel
) const 
{
    std::wstring_view const & w(word);

    //candidates.reserve(word_cand_list_size_default);

    for (size_t i = 0; i < w.size() + 1; ++i) 
    {
        // delete
        if (i < w.size()) 
        {
            std::wstring s;
            s.reserve(w.size()); 
            (s += w.substr(0, i)) += w.substr(i+1);
            Add2CandidatesIfSuitable(s, candidates);
            if (!lastLevel) 
            {
                Edits2(s, candidates);
            }
        }

        // transpose
        if (i + 1 < w.size()) 
        {
            std::wstring s;
            s.reserve(w.size());
            ((s += w.substr(0, i)) += w[i + 1]) += w[i];
            if (i + 2 < w.size()) 
            {
                s += w.substr(i+2);
            }
            Add2CandidatesIfSuitable(s, candidates);
            if (!lastLevel) 
            {
                Edits2(s, candidates);
            }
        }

        // replace
        if (i < w.size()) 
        {
            TAlphabet::letters_type const & sbt = LangModel.GetAlphabet().GetSubstitutes(w[i]);
            for (auto&& ch: sbt) 
            {
                std::wstring s;
                s.reserve(w.size());
                ((s += w.substr(0, i)) += ch) += w.substr(i+1);
                Add2CandidatesIfSuitable(s, candidates);
                if (!lastLevel) 
                {
                    Edits2(s, candidates);
                }
            }
        }

        // inserts
        {
            for (auto&& ch: LangModel.GetAlphabet()) 
            {
                std::wstring s;
                s.reserve(w.size() + 1u);
                ((s += w.substr(0, i)) += ch) += w.substr(i);
                Add2CandidatesIfSuitable(s, candidates);
                if (!lastLevel) 
                {
                    Edits2(s, candidates);
                }
            }
        }
    }

}

void TSpellCorrector::Inserts(const std::wstring_view& w, TCandMgr& result) const 
{
    std::size_t const sz (w.size() + 1) ;
    for (size_t i = 0; i < sz; ++i) 
    {
        for (auto&& ch: LangModel.GetAlphabet()) 
        {
            std::wstring s;
            s.reserve(sz);
            ((s.append(w.substr(0, i))) += ch) += w.substr(i);

            Add2CandidatesIfSuitable(s, result);
        }
    }
}

void TSpellCorrector::Inserts2(const std::wstring_view& w, TCandMgr& result) const 
{
    wide_to_utf8_t wide_to_utf8;
    std::size_t const sz (w.size() + 1u);
    for (std::size_t i = 0; i < sz; ++i) 
    {
        for (auto&& ch: LangModel.GetAlphabet()) 
        {
            std::wstring s;
            s.reserve(sz);
            (s.append(w.substr(0, i)) += ch) += w.substr(i);
            if (Deletes1->Contains(wide_to_utf8(s))) 
            {
                Inserts(s, result);
            }
        }
    }
}

void TSpellCorrector::PrepareCache() 
{
    auto&& wordToId = LangModel.GetWordToId();
    size_t n = 0;
    size_t s = 0;
    for (auto&& it: wordToId) {
        n += 1;
        s += it.first.size();
        if (n > 10000) {
            break;
        }
    }
    size_t avgWordLen = std::max(int(double(s) / n) + 1, 1);
    size_t avgWordLenMinusOne = std::max(size_t(1), avgWordLen - 1);

    uint64_t deletes1size = wordToId.size() * avgWordLen;
    uint64_t deletes2size = wordToId.size() * avgWordLen * avgWordLenMinusOne;
    deletes1size = std::max(uint64_t(1000), deletes1size);
    deletes1size = std::max(uint64_t(1000), deletes1size);

    double falsePositiveProb = 0.001;
    Deletes1.reset(new TBloomFilter(deletes1size, falsePositiveProb));
    Deletes2.reset(new TBloomFilter(deletes2size, falsePositiveProb));

    uint64_t deletes1real = 0;
    uint64_t deletes2real = 0;

    wide_to_utf8_t wide_to_utf8;

    for (auto&& it: wordToId) {
        auto deletes = GetDeletes2(it.first);
        for (auto&& w1: deletes) {
            Deletes1->Insert(wide_to_utf8(w1.back()));
            deletes1real += 1;
            for (size_t i = 0; i < w1.size() - 1; ++i) {
                Deletes2->Insert(wide_to_utf8(w1[i]));
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

bool TSpellCorrector::Add2CandidatesIfSuitable(
    std::wstring_view const & s, TCandMgr & candidates
) const
{
    JS_TRACE_MSG(std::cerr << "[debug] synthesized candidate: \'" 
        << wide_to_utf8_t{}(std::wstring{s}) <<"\'\n" 
    );
    if(candidates.probe(s))
    {
        return LookupAndAppend2Candidates(s, candidates);
    }
    return false;
}

bool TSpellCorrector::LookupAndAppend2Candidates(
    std::wstring_view const & w, TCandMgr & candidates
) const
{
    auto const & c = LangModel.GetWordInfo(w);
    if (c) 
    {
        candidates.insert(c);
        return true;
    }
    return false;
}

inline void AssignWordInfo(word_info_t & tgt, word_info_t const & src)
{
    if(!src.empty())
    {
        tgt = src;
    }
    else     // TOD fixme!
    {
        tgt.weight = 0.0;
    }
}

words_seq_t TSpellCorrector::MakeSentence(TWords const & sentence) const
{
    words_seq_t words(sentence.size());
    words_seq_t::iterator i = words.begin();
    for (TWord const & w : sentence)
    {
        AssignWordInfo(*i++, LangModel.GetWordInfo(wstr_view_type(w.Ptr, w.Len)));
    }

    return words;
}

void TSpellCorrector::InitSentWords(sentences_t & sentences) const
{
    for (words_seq_t & curr_sent : sentences)
    {
        for (word_info_t & w : curr_sent) 
        {
            AssignWordInfo(w, LangModel.GetWordInfo(w.str));
        }
    }
}

TSpellCorrector::sent_range TSpellCorrector::GetSentenceRange(
      words_seq_t & sentence
    , std::size_t const pos
) const
{
    word_info_t * pbeg = sentence.data(), * pend = pbeg;
    std::advance(pbeg, std::max(long (pos) - 2l, 0l));
    std::advance(pend, std::min(pos + 3ul , sentence.size()));
    return sent_range{pbeg, pend};
}

void TSpellCorrector::Score(words_seq_t & in_sentence
        , std::size_t const pos
        , bool const firstLevel
        , words_seq_t & candidates
    ) const
{
    sent_range cand_sent = GetSentenceRange(in_sentence, pos);
    word_info_t & curr_word = in_sentence[pos]; // ~ cand_sent.begin() + 2
    word_info_t const orig_word = curr_word; // make a copy before it is too late! =)
    bool const orig_word_is_known = (orig_word.id != TWordId::Unknown);

    for (word_info_t & cand: candidates) 
    {
        curr_word = cand;
        double const sc = LangModel.Score(cand_sent.begin(), cand_sent.end());
        cand.weight = (orig_word_is_known) ?
                ((firstLevel) ? (sc - KnownWordsPenalty) : (sc * 50.0)) 
            :   (sc - UnknownWordsPenalty);
    }

    curr_word = orig_word; // restore original state
    // Finally, we score the orig sentence as it is for now!
    candidates.emplace_back(orig_word.id, orig_word.str 
        , LangModel.Score(cand_sent.begin(), cand_sent.end())
    ); // may be we shall check if orig_word_is_known before scoring?

    std::sort(candidates.begin(), candidates.end()
        , [](word_info_t const & w1, word_info_t const & w2) 
            { return w1.weight > w2.weight;}
    );
}


} // NJamSpell
