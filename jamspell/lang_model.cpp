#include <iostream>
#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>
#include <ostream>
#include <cstring>
#include <algorithm>
#include <string_view>
#include <filesystem>

#include "lang_model.hpp"

#include <contrib/cityhash/city.h>

#ifndef ssize_t
#define ssize_t int
#endif

namespace NJamSpell {

class MemStream: public std::basic_streambuf<char> {
public:
    MemStream(char* buff, long maxSize)
        : Buff(buff)
        , MaxSize(maxSize)
        , Pos(0)
    {
    }

	std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (n <= 0) {
            return n;
        }
        long toCopy = std::min<long>(n, MaxSize - Pos);
        memcpy(Buff + Pos, s, toCopy);
        Pos += toCopy;
        return n;
    }
    void Reset() {
        Pos = 0;
    }
    long Size() const {
        return Pos;
    }
private:
    char* Buff;
    long MaxSize;
    long Pos;
};

struct TLangModel::TGramLoader 
{
    TLangModel                              &LM;
    std::unordered_map<TGram1Key, TCount>   grams1;
    std::unordered_map<TGram2Key, TCount, TGram2KeyHash> grams2;
    std::unordered_map<TGram3Key, TCount, TGram3KeyHash> grams3;

    std::size_t trainText_size, sentences_size, max_grams1_sz, max_grams2_sz, max_grams3_sz;

    explicit TGramLoader(TLangModel & lm)
    : LM(lm)
    , trainText_size{0}, sentences_size{0}
    , max_grams1_sz{10000000}, max_grams2_sz{20000000}, max_grams3_sz{40000000}
    {}

    bool Load(const std::string& fName)
    {
        std::cerr << "[info] generating N-grams... " << std::endl;
        std::size_t const file_sz = std::filesystem::file_size(fName);
        std::ifstream in(fName, std::ios::binary);
        if(!in)
        {
            return false;
        }

        std::size_t lcnt{0}, bytes_cnt{0};
        uint64_t lastTime = GetCurrentTimeMs();
        std::string l;        
        utf8_to_wide_t utf8_to_wide{};
        while (!in.eof())
        {            
            std::getline(in, l); 
            if(!l.empty())
            {            
                bytes_cnt += (1 + l.size());
                std::wstring trainText (utf8_to_wide(l));                
                trainText_size += trainText.size();
                TSentences const & sentences = LM.Tokenizer.Tokenize(trainText);
                ToLower(trainText);
                TIdSentences const & sentenceIds = LM.ConvertToIds(sentences);                

                FillGramms(sentenceIds);

                if( (++lcnt) % 10000u == 0)
                {
                    uint64_t const currTime = GetCurrentTimeMs();
                    if (currTime - lastTime > 4000) 
                    {
                        std::cerr << "[info] processed " << (100.0 * float(bytes_cnt) / float(file_sz)) << "%" << std::endl;
                        lastTime = currTime;
                    }
                }                
            }        
        }
        Reduce();   
        return lcnt;  
    }

private:

    void FillGramms(TIdSentences const & sentenceIds)
    {        
        for (TWordIds const& words : sentenceIds) 
        {
            for (auto const & w: words) 
            {
                grams1[w] += 1u;
                LM.TotalWords += 1u;
            }
            if (words.size() > 2)
            {
                for (auto it {words.begin()}, nxt{it + 1}, e{words.end()}
                    ; nxt != e; grams2[TGram2Key {*it++, *nxt++}] += 1u
                ){}
            }
            if(words.size() > 3)
            {
                for (auto it {words.begin()}, nxt1{it + 1}, nxt2{nxt1 + 1}, e{words.end()}
                    ; nxt2 != e; grams3[TGram3Key {*it++, *nxt1++, *nxt2++}] += 1u
                ){}
            }        
        }
        
        ReduceIfNeeded();
    }

    template <typename TMap> void Reduce (char const * nGrNam
        , TMap & grams
        , std::size_t & mx_sz
        , std::size_t const threshold = 2u
        , float gf = 1.02
    )
    {
        std::cerr << "--[info] started reduce of " << nGrNam 
        << " size = " << grams.size() << " with threshold="<< threshold << std::endl;

        if (gf < 1.0 || gf > 2.0)
            gf = 1.02;

        for (auto it {grams.begin()}, e {grams.end()}
            ; it != e
            ; ( it -> second > threshold) ? ++it : (it->first, it = grams.erase(it))
        ) {} 
        mx_sz *= gf;

        std::cerr << "--[info] finished reduce of " << nGrNam 
            << " size = " << grams.size() << " new threshold_size="<< mx_sz << std::endl;
    }

    template <typename TMap> void ReduceIfNeeded (char const * nGrNam
        , TMap & grams
        , std::size_t & mx_sz
        , std::size_t const threshold = 2u
        , float const gf = 1.02
    )
    {
        if(grams.size() >= mx_sz)
        {
            Reduce(nGrNam, grams, mx_sz, threshold, gf);
        }
    }

    void ReduceIfNeeded()
    {
        ReduceIfNeeded("one-word-grams", grams1, max_grams1_sz, 6);
        ReduceIfNeeded("two-word-grams", grams2, max_grams2_sz, 3);
        ReduceIfNeeded("three-word-grams", grams3, max_grams3_sz, 2);
    }

    void Reduce()
    {
        Reduce("one-word-grams", grams1, max_grams1_sz, 6);
        Reduce("two-word-grams", grams2, max_grams2_sz, 3);
        Reduce("three-word-grams", grams3, max_grams3_sz, 2);
    }

    template <typename TKey> void Cleanup(TKey const & k) {}
    void Cleanup(TWordId const & k) 
    {
        //LM.WordToId.erase(k);        
    }

};

template<typename T>
std::string DumpKey(const T& key) {
    std::stringbuf buf;
    std::ostream out(&buf);
    NHandyPack::Dump(out, key);
    return buf.str();
}

template<typename T>
void PrepareNgramKeys(const T& grams, std::vector<std::string>& keys) {
    for (auto&& it: grams) {
        keys.push_back(DumpKey(it.first));
    }
}

static const uint32_t MAX_REAL_NUM = 268435456;
static const uint32_t MAX_AVAILABLE_NUM = 65536;

inline uint16_t PackInt32(uint32_t num) {
    double r = double(num) / double(MAX_REAL_NUM);
    assert(r >= 0.0 && r <= 1.0);
    r = pow(r, 0.2);
    r *= MAX_AVAILABLE_NUM;
    return uint16_t(r);
}

inline uint32_t UnpackInt32(uint16_t num) {
    double r = double(num) / double(MAX_AVAILABLE_NUM);
    r = pow(r, 5.0);
    r *= MAX_REAL_NUM;
    return uint32_t(ceil(r));
}

template<typename T>
void InitializeBuckets(const T& grams, TPerfectHash& ph, std::vector<std::pair<uint16_t, uint16_t>>& buckets) {
    for (auto&& it: grams) {
        std::string key = DumpKey(it.first);
        //std::string_view const key (mk_string_view(it.first));
        uint32_t bucket = ph.Hash(key);
        if (bucket >= buckets.size()) {
            std::cerr << bucket << " " << buckets.size() << "\n";
        }
        assert(bucket < buckets.size());
        std::pair<uint16_t, uint16_t> data;
        data.first = CityHash16(key);
        data.second = PackInt32(it.second);
        buckets[bucket] = data;
    }
}

bool TLangModel::Train(const std::string& fileName, const std::string& alphabetFile) {

    std::cerr << "[info] loading text" << std::endl;
    uint64_t trainStarTime = GetCurrentTimeMs();
    if (!Tokenizer.LoadAlphabet(alphabetFile)) {
        std::cerr << "[error] failed to load alphabet" << std::endl;
        return false;
    }
    
    TGramLoader gldr{*this};
    if(!gldr.Load(fileName))
    {
        std::cerr << "[error] failed to load grams" << std::endl;
        return false;
    }

    VocabSize = gldr.grams1.size();
    if(!VocabSize)
    {
        std::cerr << "[error] got empty vocabulary: not enough frequent grams" << std::endl;
        return false;
    }

    std::cerr << "[info] generating keys" << std::endl;

    {
        std::vector<std::string> keys;
        std::size_t const ttlGrams {gldr.grams1.size() + gldr.grams2.size() + gldr.grams3.size()};
        keys.reserve(ttlGrams);

        std::cerr << "[info] ngrams1: " << gldr.grams1.size() << "\n";
        std::cerr << "[info] ngrams2: " << gldr.grams2.size() << "\n";
        std::cerr << "[info] ngrams3: " << gldr.grams3.size() << "\n";
        std::cerr << "[info] total: " << ttlGrams << "\n";

        PrepareNgramKeys(gldr.grams1, keys);
        PrepareNgramKeys(gldr.grams2, keys);
        PrepareNgramKeys(gldr.grams3, keys);

        std::cerr << "[info] generating perf hash" << std::endl;

        PerfectHash.Init(keys);
    }

    std::cerr << "[info] finished, buckets: " << PerfectHash.BucketsNumber() << "\n";

    Buckets.resize(PerfectHash.BucketsNumber());
    InitializeBuckets(gldr.grams1, PerfectHash, Buckets);
    InitializeBuckets(gldr.grams2, PerfectHash, Buckets);
    InitializeBuckets(gldr.grams3, PerfectHash, Buckets);

    std::cerr << "[info] buckets filled" << std::endl;

    std::stringbuf checkSumBuf;
    std::ostream checkSumOut(&checkSumBuf);
    NHandyPack::Dump(checkSumOut, trainStarTime, gldr.grams1.size(), gldr.grams2.size(),
                    gldr.grams3.size(), Buckets.size(), gldr.trainText_size, gldr.sentences_size);
    std::string checkSumStr = checkSumBuf.str();
    CheckSum = CityHash64(&checkSumStr[0], checkSumStr.size());
    return true;
}

double TLangModel::Score(const TWords& words) const {
    TWordIds sentence;
    for (auto&& w: words) {
        sentence.push_back(GetWordIdNoCreate(w));
    }
    if (sentence.empty()) {
        return std::numeric_limits<double>::min();
    }

    sentence.push_back(TWordId::Unknown);
    sentence.push_back(TWordId::Unknown);

    double result = 0;
    for (size_t i = 0; i < sentence.size() - 2; ++i) {
        result += log(GetGram1Prob(sentence[i]));
        result += log(GetGram2Prob(sentence[i], sentence[i + 1]));
        result += log(GetGram3Prob(sentence[i], sentence[i + 1], sentence[i + 2]));
    }
    return result;
}

double TLangModel::Score(std::wstring str ) const 
{
    TSentences sentences = Tokenizer.Tokenize(str);
    ToLower(str);
    TWords words;
    for (auto&& s: sentences) {
        for (auto&& w: s) {
            words.emplace_back(std::move(w));
        }
    }
    return Score(words);
}

bool TLangModel::Dump(const std::string& modelFileName) const {
    std::ofstream out(modelFileName, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    NHandyPack::Dump(out, LANG_MODEL_MAGIC_BYTE);
    NHandyPack::Dump(out, LANG_MODEL_VERSION);
    Dump(out);
    NHandyPack::Dump(out, LANG_MODEL_MAGIC_BYTE);
    return true;
}

bool TLangModel::Load(const std::string& modelFileName) {
    std::ifstream in(modelFileName, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    uint16_t version = 0;
    uint64_t magicByte = 0;
    NHandyPack::Load(in, magicByte);
    if (magicByte != LANG_MODEL_MAGIC_BYTE) {
        return false;
    }
    NHandyPack::Load(in, version);
    if (version != LANG_MODEL_VERSION) {
        return false;
    }
    Load(in);
    magicByte = 0;
    NHandyPack::Load(in, magicByte);
    if (magicByte != LANG_MODEL_MAGIC_BYTE) {
        Clear();
        return false;
    }
    /*
    IdToWord.clear();
    IdToWord.resize(WordToId.size() + 1, nullptr);
    for (auto&& it: WordToId) {
        IdToWord[to_underlying(it.second)] = &it.first;
    }
    */
    return true;
}

void TLangModel::Clear() {
    K = LANG_MODEL_DEFAULT_K;
    WordToId.clear();
    LastWordID = 0;
    TotalWords = 0;
    Tokenizer.Clear();
}

const TRobinHash& TLangModel::GetWordToId() {
    return WordToId;
}



TIdSentences TLangModel::ConvertToIds(const TSentences& sentences) {
    TIdSentences newSentences;
    for (size_t i = 0; i < sentences.size(); ++i) {
        const TWords& words = sentences[i];
        TWordIds wordIds;
        for (size_t j = 0; j < words.size(); ++j) {
            const TWord& word = words[j];
            wordIds.push_back(GetWordId(word));
        }
        newSentences.emplace_back(std::move(wordIds));
    }
    return newSentences;
}

TWordId TLangModel::GetWordId(const TWord& word) {
    assert(word.Ptr && word.Len);
    assert(word.Len < 10000);
    std::wstring w(word.Ptr, word.Len);
    //std::string w = Tokenizer.RecodeWord(word);
    auto it = WordToId.find(w);
    if (it != WordToId.end()) {
        return it->second;
    }
    TWordId wordId {LastWordID};
    ++LastWordID;
    //it = WordToId.insert(std::make_pair(w, wordId)).first;
    WordToId.emplace(std::move(w), wordId);
    //IdToWord.push_back(&(it->first));
    return wordId;
}

TWordId TLangModel::GetWordIdNoCreate(const TWord& word) const {
    std::wstring w(word.Ptr, word.Len);
    auto it = WordToId.find(w);
    if (it != WordToId.end()) {
        return it->second;
    }
    return TWordId::Unknown;
}

/*
TWord TLangModel::GetWordById(TWordId wid) const {
    if (to_underlying( wid ) >= IdToWord.size()) {
        return TWord();
    }
    return TWord(*IdToWord[to_underlying(wid)]);
}
*/

TCount TLangModel::GetWordCount(TWordId wid) const {
    return GetGram1HashCount(wid);
}

uint64_t TLangModel::GetCheckSum() const {
    return CheckSum;
}

TWord TLangModel::GetWord(const std::wstring& word) const {
    auto it = WordToId.find(word);
    if (it != WordToId.end()) {
        return TWord(&it->first[0], it->first.size());
    }
    return TWord();
}

TSentences TLangModel::Tokenize(const std::wstring& text) const {
    return Tokenizer.Tokenize(text);
}

double TLangModel::GetGram1Prob(TWordId word) const {
    double countsGram1 = GetGram1HashCount(word);
    countsGram1 += K;
    return countsGram1 / (TotalWords + VocabSize);
}

double TLangModel::GetGram2Prob(TWordId word1, TWordId word2) const {
    double countsGram1 = GetGram1HashCount(word1);
    double countsGram2 = GetGram2HashCount(word1, word2);
    if (countsGram2 > countsGram1) { // (hash collision)
        countsGram2 = 0;
    }
    countsGram1 += TotalWords;
    countsGram2 += K;
    return countsGram2 / countsGram1;
}

double TLangModel::GetGram3Prob(TWordId word1, TWordId word2, TWordId word3) const {
    double countsGram2 = GetGram2HashCount(word1, word2);
    double countsGram3 = GetGram3HashCount(word1, word2, word3);
    if (countsGram3 > countsGram2) { // hash collision
        countsGram3 = 0;
    }
    countsGram2 += TotalWords;
    countsGram3 += K;
    return countsGram3 / countsGram2;
}

template<typename T>
TCount GetGramHashCount(T key,
                        const TPerfectHash& ph,
                        const std::vector<std::pair<uint16_t, uint16_t>>& buckets)
{
    constexpr int TMP_BUF_SIZE = 128;
    char tmpBuff[TMP_BUF_SIZE];
    MemStream tmpBuffStream(tmpBuff, TMP_BUF_SIZE - 1);
    std::ostream out(&tmpBuffStream);

    tmpBuffStream.Reset();

    NHandyPack::Dump(out, key);

    uint32_t bucket = ph.Hash(tmpBuff, tmpBuffStream.Size());

    assert(bucket < ph.BucketsNumber());
    const std::pair<uint16_t, uint16_t>& data = buckets[bucket];

    TCount res = TCount();
    if (data.first == CityHash16(tmpBuff, tmpBuffStream.Size())) {
        res = UnpackInt32(data.second);
    }
    return res;
}

TCount TLangModel::GetGram1HashCount(TWordId word) const {
    if (word == TWordId::Unknown) {
        return TCount();
    }
    TGram1Key key = word;
    return GetGramHashCount(key, PerfectHash, Buckets);
}

TCount TLangModel::GetGram2HashCount(TWordId word1, TWordId word2) const {
    if (word1 == TWordId::Unknown || word2 == TWordId::Unknown) {
        return TCount();
    }
    TGram2Key key({word1, word2});
    return GetGramHashCount(key, PerfectHash, Buckets);
}

TCount TLangModel::GetGram3HashCount(TWordId word1, TWordId word2, TWordId word3) const {
    if (word1 == TWordId::Unknown || word2 == TWordId::Unknown || word3 == TWordId::Unknown) {
        return TCount();
    }
    TGram3Key key(word1, word2, word3);
    return GetGramHashCount(key, PerfectHash, Buckets);
}

} // NJamSpell
