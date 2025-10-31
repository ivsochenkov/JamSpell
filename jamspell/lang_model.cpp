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
#include <array>
#include <functional>

#include "lang_model.hpp"

#include <contrib/cityhash/city.h>
#include <contrib/tsl/robin_set.h>
#include <contrib/tsl/robin_map.h>

#ifndef ssize_t
#define ssize_t int
#endif

namespace NJamSpell 
{

TLangModel::train_options_t TLangModel::train_options_t::make_default()
{
    return train_options_t{};
}

class TGramKey 
{
    using storage_type = std::array<TWordId, 3>;

public:

    enum class ngram_type : unsigned char 
    {            
            nUndef = 0    
        ,   nGram1
        ,   nGram2
        ,   nGram3
    };

    explicit TGramKey (TWordId const & n1 = TWordId::Unknown
        , TWordId const & n2 = TWordId::Unknown
        , TWordId const & n3 = TWordId::Unknown
    ) 
    : m_elems{n1, n2, n3} 
    {}

    bool operator < (TGramKey const & rhs) const 
    { return m_elems < rhs.m_elems; }

    bool operator == (TGramKey const & rhs) const 
    { return m_elems == rhs.m_elems; }

    std::string_view bytes() const 
    {
        return std::string_view(reinterpret_cast<const char*>(m_elems.data())
            , m_elems.size()*sizeof(m_elems[0])
        );
    }

    inline ngram_type kind() const
    {
        ngram_type nt {ngram_type::nUndef};
        for(auto i = m_elems.begin(); i != m_elems.end(); inc(nt, (*i++ != TWordId::Unknown)))
        {}
        return nt;
    }

    using const_iterator = storage_type::const_iterator;

    const_iterator begin () const {return m_elems.begin();}
    const_iterator end () const { return begin() + to_underlying(kind()); }

    static std::underlying_type<ngram_type>::type to_underlying(ngram_type nt)
    {
        return static_cast<std::underlying_type<TGramKey::ngram_type>::type> (nt);
    }

private:

    static void inc(TGramKey::ngram_type & nt, bool v) 
    {
        nt = TGramKey::ngram_type( to_underlying(nt) + v );
    }

    storage_type    m_elems;

};

inline std::underlying_type<TGramKey::ngram_type>::type to_underlying(TGramKey::ngram_type nt)
{
    return TGramKey::to_underlying(nt);
}

struct TGramKeyHash
{
    std::size_t operator()(const TGramKey& x) const 
    {
        return std::hash<std::string_view>{}(x.bytes());
    }
};

struct TLangModel::TGramLoader 
{
    using grams_type = tsl::robin_map<TGramKey, TCount
        , TGramKeyHash
        , std::equal_to<TGramKey>
        , std::allocator<std::pair<TGramKey, TCount> >
        , true // store hash
        , tsl::rh::mod_growth_policy<std::ratio<11, 10> >
    >;

    using word_id_set_type = tsl::robin_set<TWordId>;

    TLangModel                  &   LM;
    train_options_t                 m_tr_opt;
    grams_type                      m_grams;    

    std::size_t                     trainText_size
                                ,   sentences_size
                                ;

    unsigned get_threshold(TGramKey::ngram_type ngr_kind) const 
    { 
        assert (ngr_kind != TGramKey::ngram_type::nUndef);
        return m_tr_opt.ngram_thresholds[ to_underlying(ngr_kind) - 1 ];
    }

    explicit TGramLoader(TLangModel & lm, train_options_t const & tr_opt)
    : LM(lm)
    , m_tr_opt{tr_opt}
    , trainText_size{0}, sentences_size{0}
    {
        LM.WordToId.reserve (m_tr_opt.max_grams_sz / 3);
        m_grams.reserve(m_tr_opt.max_grams_sz * m_tr_opt.growth_factor);
    }

    grams_type & grams() {return m_grams;}

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
                        std::cerr << "[info] processed " 
                            << (100.0 * float(bytes_cnt) / float(file_sz)) 
                            << "%" << std::endl;
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
                m_grams[TGramKey(w)] += 1u;
                LM.TotalWords += 1u;
            }
            if (words.size() >= 2)
            {
                auto it {words.begin()}, nxt{it + 1}, e{words.end()};
                do 
                {
                    m_grams[TGramKey {*it++, *nxt++}] += 1u;
                }
                while (nxt != e);                
            }
            if(words.size() >= 3)
            {
                auto it {words.begin()}, nxt1{it + 1}, nxt2{nxt1 + 1}, e{words.end()};
                do 
                {
                    m_grams[TGramKey {*it++, *nxt1++, *nxt2++}] += 1u;
                } 
                while (nxt2 != e);
            }        
        }
        
        ReduceIfNeeded();
    }

    void Reduce ()
    {
        std::cerr << "--[info] started reduce of grams: size = " << m_grams.size() 
            << " with threshold="<< m_tr_opt.max_grams_sz << std::endl;

        for (auto it {m_grams.begin()}, e {m_grams.end()}
            ; it != e
            ; 
        ) 
        {
            TGramKey::ngram_type const nt = it -> first.kind();
            if (it -> second >= get_threshold(nt))
            {
                ++it;
                //stat.inc_left(nt);
            }
            else
            {
                it = m_grams.erase(it);
                //stat.inc_del(nt);
            }
        } 

        CleanupVocabulary();

        m_tr_opt.max_grams_sz *= m_tr_opt.growth_factor;

        std::cerr << "--[info] finished reduce of grams: size = " << m_grams.size() 
            << " new max_grams_sz = "<< m_tr_opt.max_grams_sz << std::endl;

    }

    void ReduceIfNeeded ()
    {
        if(m_grams.size() >= m_tr_opt.max_grams_sz)
        {
            Reduce();
        }
    }

    word_id_set_type PopulateWordIds ()
    {
        word_id_set_type wordIds;
        wordIds.reserve(LM.WordToId.size());
        for(auto const & g: m_grams)
        {
            for (TWordId const & wid: g.first)
            {
                wordIds.insert(wid);
            }
        }
        return wordIds;
    }

    void CleanupVocabulary() 
    {
        std::cerr << "++[info] cleanup vocabulary... size = " 
            << LM.WordToId.size() << std::endl;

        word_id_set_type const & wordIds = PopulateWordIds();
        for (auto it = LM.WordToId.begin(), e = LM.WordToId.end()
            ; it != e
            ; (wordIds.find(it -> second) != wordIds.end() )
                ? ++it : it = LM.WordToId.erase(it)
        ){}     

        std::cerr << "++[info] done cleanup vocabulary... size = " 
            << LM.WordToId.size() << std::endl;
    }

};

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
void InitializeBuckets(const T& grams
    , TPerfectHash& ph
    , std::vector<std::pair<uint16_t, uint16_t>>& buckets
) 
{
    for (auto&& it: grams) 
    {
        //std::string key = DumpKey(it.first);
        std::uint32_t const bucket = ph.Hash(it.first.bytes());
        if (bucket >= buckets.size()) 
        {
            std::cerr << bucket << " " << buckets.size() << "\n";
        }
        assert(bucket < buckets.size());
        std::pair<uint16_t, uint16_t> data;
        data.first = CityHash16(it.first.bytes());
        data.second = PackInt32(it.second);
        buckets[bucket] = data;
    }
}

bool TLangModel::Train(const std::string& fileName
    , const std::string& alphabetFile
    , train_options_t const & tr_opt
) 
{

    std::cerr << "[info] loading text" << std::endl;
    uint64_t trainStarTime = GetCurrentTimeMs();
    if (!Tokenizer.LoadAlphabet(alphabetFile)) 
    {
        std::cerr << "[error] failed to load alphabet" << std::endl;
        return false;
    }
    
    TGramLoader gldr{*this, tr_opt};
    if(!gldr.Load(fileName))
    {
        std::cerr << "[error] failed to load grams" << std::endl;
        return false;
    }

    //VocabSize = gldr.grams().size();
    VocabSize = WordToId.size();
    if(!VocabSize)
    {
        std::cerr << "[error] got empty vocabulary: not enough frequent grams" << std::endl;
        return false;
    }

    std::cerr << "[info] generating perf hash" << std::endl;
    struct extract_as_bytes_t
    {
        auto operator () (TGramLoader::grams_type::value_type const & gvt) const
        {return gvt.first.bytes();}
    };
    static const extract_as_bytes_t extract_as_bytes{};
    PerfectHash.Init(boost::make_transform_iterator(gldr.grams().begin(), extract_as_bytes)
        , boost::make_transform_iterator(gldr.grams().end(), extract_as_bytes)
    );
    
    std::cerr << "[info] finished, buckets: " << PerfectHash.BucketsNumber() << "\n";

    Buckets.resize(PerfectHash.BucketsNumber());
    InitializeBuckets(gldr.grams(), PerfectHash, Buckets);
    
    std::cerr << "[info] buckets filled" << std::endl;

    std::stringbuf checkSumBuf;
    std::ostream checkSumOut(&checkSumBuf);
    NHandyPack::Dump(checkSumOut, trainStarTime, gldr.grams().size()
        , Buckets.size(), gldr.trainText_size, gldr.sentences_size
    );
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

TWord TLangModel::GetWord(const std::wstring_view& word) const 
{
    auto it = WordToId.find(word);
    if (it != WordToId.end()) {
        return TWord(it->first.data(), it->first.size());
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

TCount GetGramHashCount(TGramKey const & key
    , const TPerfectHash& ph
    , const std::vector<std::pair<uint16_t, uint16_t>>& buckets
)
{
    uint32_t const bucket = ph.Hash(key.bytes());

    assert(bucket < ph.BucketsNumber());
    const std::pair<uint16_t, uint16_t>& data = buckets[bucket];

    TCount res = TCount{};
    if (data.first == CityHash16(key.bytes())) 
    {
        res = UnpackInt32(data.second);
    }
    return res;
}

TCount TLangModel::GetGram1HashCount(TWordId word) const 
{
    if (word == TWordId::Unknown) 
    {
        return TCount();
    }
    return GetGramHashCount(TGramKey{word}, PerfectHash, Buckets);
}

TCount TLangModel::GetGram2HashCount(TWordId word1, TWordId word2) const 
{
    if (word1 == TWordId::Unknown || word2 == TWordId::Unknown) 
    {
        return TCount();
    }
    return GetGramHashCount(TGramKey (word1, word2), PerfectHash, Buckets);
}

TCount TLangModel::GetGram3HashCount(TWordId word1, TWordId word2, TWordId word3) const 
{
    if (word1 == TWordId::Unknown || word2 == TWordId::Unknown || word3 == TWordId::Unknown) 
    {
        return TCount();
    }
    return GetGramHashCount(TGramKey (word1, word2, word3), PerfectHash, Buckets);
}

} // NJamSpell
