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

class TLangModel::TGramKey 
{
    using storage_type = std::array<TWordId, 3>;

public:

    enum class ngram_type : unsigned char 
    {            
            nUndef      = 0    
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

    TWordId operator [] (unsigned i) const {return m_elems[i];}
    TWordId & operator [] (unsigned i) {return m_elems[i];}

    std::string_view bytes() const 
    {
        return std::string_view(reinterpret_cast<const char*>(m_elems.data())
            ,  to_underlying(kind()) * sizeof(m_elems[0])
        );
    }

    inline ngram_type kind() const
    {
        short nt = to_underlying(ngram_type::nUndef);
        for(auto i = m_elems.begin(); i != m_elems.end(); nt += (*i++ != TWordId::Unknown))
        {}
        return ngram_type(nt);
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


struct TLangModel::TGramLoader 
{
    
    static inline std::underlying_type<TLangModel::TGramKey::ngram_type>::type 
        to_underlying(TGramKey::ngram_type nt)
    {
        return TGramKey::to_underlying(nt);
    }

    struct TGramKeyHash
    {
        std::size_t operator()(const TLangModel::TGramKey& x) const 
        {
            return std::hash<std::string_view>{}(x.bytes());
        }
    };

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

    explicit TGramLoader(TLangModel & lm, train_options_t const & tr_opt);

    unsigned get_threshold(TGramKey::ngram_type ngr_kind) const 
    { 
        assert (ngr_kind != TGramKey::ngram_type::nUndef);
        return m_tr_opt.ngram_thresholds[ to_underlying(ngr_kind) - 1 ];
    }

    grams_type & grams() {return m_grams;}

    bool Load(const std::string& fName);

private:

    void ProcessText(std::wstring const & trainText );

    void PrintStatus(uint64_t & last_time
        , std::size_t bytes_cnt
        , std::size_t file_size
    );

    void FillGramms(wstr_view_t const & raw_txt
        , wstr_view_t const * b, wstr_view_t const * e
    );

    void Reduce ();

    void ReduceIfNeeded ()
    {
        if(m_grams.size() >= m_tr_opt.max_grams_sz)
        {
            Reduce();
        }
    }

    word_id_set_type PopulateWordIds ();

    void CleanupVocabulary();

    void ShiftLeft(TGramKey & gc)
    {
        gc[0] = gc[1];
        gc[1] = gc[2];
        //gc[2] = TWordId::Unknown; // not needed!
    }

};

TLangModel::TGramLoader::TGramLoader(TLangModel & lm, train_options_t const & tr_opt)
: LM(lm)
, m_tr_opt{tr_opt}
, trainText_size{0}, sentences_size{0}
{
    //LM.WordToId.reserve (m_tr_opt.max_grams_sz / 3);
    m_grams.reserve(m_tr_opt.max_grams_sz * m_tr_opt.growth_factor);
}

bool TLangModel::TGramLoader::Load(const std::string& fName)
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
            ProcessText(trainText);
            if(( ++lcnt) % 10000u) 
            {
                PrintStatus(lastTime, bytes_cnt, file_sz);
            }
        }        
    }
    Reduce();          
    return lcnt;  
}

void TLangModel::TGramLoader::ProcessText(std::wstring const & trainText )
{
    auto tokens = LM.Tokenizer.Parse(trainText);
    //ToLower(trainText);
    LM.Tokenizer.FilterAndJoinHyphen(tokens);
    FillGramms(trainText, tokens.data(), tokens.data() + tokens.size());
}

void TLangModel::TGramLoader::PrintStatus(uint64_t & last_time
    , std::size_t bytes_cnt
    , std::size_t file_size
)
{
    uint64_t const currTime = GetCurrentTimeMs();
    if (currTime - last_time > 5000) 
    {
        std::cerr << "[info] processed " 
            << (100.0 * float(bytes_cnt) / float(file_size)) 
            << "% ( vocab size = " << LM.WordToId.size() 
            << ", grams_size = " << m_grams.size() << ")\n";
        last_time = currTime;
    }           
}

void TLangModel::TGramLoader::FillGramms(wstr_view_t const & raw_txt
    , wstr_view_t const * b, wstr_view_t const * e
)
{ 
    //std::string const & atxt = ToAlphabet(LM.Tokenizer.GetAlphabet(), raw_txt);
    
    TGramKey gram_key;
    str_t alStr;
    for (bool it_is_all_over = (b == e); (!it_is_all_over) && b != e; ++b )
    {
        unsigned c = 0;
        for(
            ; (!(it_is_all_over = (b == e))) && !(TTokenizer::isSentEnd(*b))
            ; ++b 
        )
        {
            //str_view_t const & alStr = Remap(atxt, raw_txt, *b);
            ToAlphabet(LM.Tokenizer.GetAlphabet(), *b, alStr);
            if(!WellFormedInAlphabet(alStr))
            {
                c = 0;
                continue;
            }

            gram_key[c] = LM.UpdateWordId(alStr);
            ++LM.TotalWords;

            if (c > 0)
            {
                m_grams[TGramKey(gram_key[0], gram_key[1])] += 1u;
            }
            if(c > 1)
            {                
                m_grams[gram_key] += 1;
                m_grams[TGramKey(gram_key[0], TWordId::Any, gram_key[2])] += 1u;
                ShiftLeft(gram_key);
            }
            c += (c < 2);   // NOTE: Must be here! Not in cycle due to continue 
                            // statement above
        }
        
        if(c > 1) // note! Due to Shift left above we need to add shifted pair of ids!
        {
            m_grams[TGramKey(gram_key[0], gram_key[1])] += 1u;
        }
        
        //gram_key = TGramKey{};    // Not needed
    }
    
    ReduceIfNeeded();
}

void TLangModel::TGramLoader::Reduce ()
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

TLangModel::TGramLoader::word_id_set_type TLangModel::TGramLoader::PopulateWordIds ()
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

void TLangModel::TGramLoader::CleanupVocabulary() 
{
    std::cerr << "++[info] cleanup vocabulary... size = " 
        << LM.WordToId.size() << std::endl;

    unsigned const thres {get_threshold(TGramKey::ngram_type::nGram1)};

    word_id_set_type const & wordIds = PopulateWordIds();
    for (auto it = LM.WordToId.begin(), e = LM.WordToId.end()
        ; it != e
        ; ((it.value().count >= thres ) || 
            (wordIds.find(it.value().id) != wordIds.end()) 
            )  ? ++it : it = LM.WordToId.erase(it)
    ){}     

    std::cerr << "++[info] done cleanup vocabulary... size = " 
        << LM.WordToId.size() << std::endl;
}


static const uint32_t MAX_REAL_NUM = 268435456;
static const uint32_t MAX_AVAILABLE_NUM = 65536;

inline void AssignWordInfo(word_info_t & tgt, word_info_t && src)
{
    if(!src.unknown())
    {
        //tgt = std::move(src); // do not empty tgt.str !!!
        tgt.id = src.id;
        tgt.weight = src.weight;
    }
}

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

double TLangModel::Score(word_info_t const * beg, word_info_t const * e) const
{
    long double result = 0.0;
    static word_info_t const unkn_wi (TWordId::Unknown);
    word_info_t const * next1{ beg + 1}, * next2{ next1 + 1};
    do 
    {
        result += std::log(CalcGram1Prob(*beg));
        word_info_t const * pN1 = (next1 < e) ? next1: &unkn_wi;
        result += std::log(CalcGram2Prob(*beg, *pN1 ));
        word_info_t const * pN2 = (next2 < e) ? next2: &unkn_wi;
        result += std::log(CalcGram3Prob(*beg, *pN1, *pN2));
        result += std::log(Calc1StepGram2Prob(*beg, *pN2));
        // Unimplemented!
  
        ++next1;
        ++next2;
    }
    while (++beg != e);

    return result;
}

double TLangModel::Score(text_tokens_t & words) const 
{
    if (words.empty()) {
        return std::numeric_limits<double>::min();
    }

    words_seq_t txt_words = InitWords(words);
    assert(txt_words.size() == words.size());
   
    return Score(txt_words.data(), txt_words.data() + txt_words.size());
}

double TLangModel::Score(std::wstring const & str ) const 
{
    text_tokens_t orig_txt_tokens = GetTokenizer().Parse(str);
    GetTokenizer().FilterAndJoin(orig_txt_tokens);
    return Score(orig_txt_tokens);
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
    return true;
}

void TLangModel::Clear() 
{
    K = LANG_MODEL_DEFAULT_K;
    WordToId.clear();
    LastWordID = 0;
    TotalWords = 0;
    Tokenizer.Clear();
}

std::size_t TLangModel::avg_word_length(std::size_t max_probes) const
{
    if(!max_probes)
    {
        max_probes = dict_size();
    }

    size_t n = 0, s = 0;
    std::string sbuf;
    for (auto it = WordToId.begin(), e = WordToId.end()
        ; it != e && n < max_probes
        ; ++it, ++n
    ) 
    {
        it.key(sbuf);
        s += sbuf.size();
    }
    return std::max(unsigned(double(s) / n) + 1u, 1u);
}

/*
TWordIds TLangModel::ConvertToIds(text_tokens_t const & txt_tokens) 
{
    TWordIds word_ids(txt_tokens.size());

    TIdSentences newSentences(sentences.size());
    auto sent_it = newSentences.begin();
    //for (size_t i = 0; i < sentences.size(); ++i) 
    for(TWords const & words : sentences)
    {
        sent_it -> resize(words.size());
        auto wit = sent_it ->begin();
        //const TWords& words = sentences[i];
        //TWordIds wordIds (words.size());
        //auto wit = wordIds.begin();
        //for (size_t j = 0; j < words.size(); ++j) 
        for(TWord const word: words)
        {
            //const TWord& word = words[j];
            //wordIds.push_back(MakeWordId(word));
            *wit++ = MakeWordId(word);
        }
        //newSentences.emplace_back(std::move(wordIds));
        ++sent_it;
    }
    return newSentences;
}
*/

TWordId TLangModel::UpdateWordId(str_view_t const & word)
{
    assert(!word.empty());
    assert(word.size() < 200);
    
    auto insR = WordToId.emplace(word, wdata_t{TWordId (LastWordID), 0u} );
    insR.first.value().count += 1u;
    LastWordID += insR.second;
    return insR.first.value().id;
}

TWordId TLangModel::GetWordId(str_view_t const & word) const 
{
    auto it = WordToId.find(word);
    return (it != WordToId.end()) ? it.value().id : TWordId::Unknown;
}

/*
TWord TLangModel::GetWordById(TWordId wid) const {
    if (to_underlying( wid ) >= IdToWord.size()) {
        return TWord();
    }
    return TWord(*IdToWord[to_underlying(wid)]);
}
*/

/*
TCount TLangModel::GetWordCount(TWordId wid) const 
{
    return GetGram1HashCount(wid);
}
*/

words_seq_t TLangModel::InitWords(text_tokens_t & orig_txt_tok) const
{    
    words_seq_t words (orig_txt_tok.size(), word_info_t{});
    words_seq_t::iterator wit = words.begin();
    for (wstr_view_t const & orig_token : orig_txt_tok)
    {
        if((!orig_token.empty()) && !TTokenizer::isSentEnd(orig_token))
        {
            str_t al_str = ToAlphabet(Tokenizer.GetAlphabet(), orig_token);
            if(WellFormedInAlphabet(al_str))
            {                
                wit -> str = std::move(al_str);
                AssignWordInfo(*wit, GetWordInfo(wit -> str));
            }
        }
        ++wit;
    }
    return words;
}

str_t TLangModel::GetWord(str_view_t const & word) const 
{
    str_t s;
    auto it = WordToId.find(word);
    if (it != WordToId.end()) 
    {
        s.reserve(word.size());
        it.key(s);
        return s;
    }
    return s;
}

word_info_t TLangModel::GetWordInfo(str_view_t const & word) const 
{
    static word_info_t empty_wi;
    auto it = WordToId.find(word);
    return (it != WordToId.end()) ? 
            word_info_t(it.value().id /*, it.key()*/, it.value().count)
        : empty_wi;
}

long double TLangModel::CalcGram2Prob(word_info_t const & winf1
    , word_info_t const & winf2
) const
{
    long double countsGram1 = winf1.weight
        , countsGram2 = winf2.unknown() ? TCount{0} 
            : GetGramHashCount(TGramKey(winf1.id, winf2.id), PerfectHash, Buckets);

    if (countsGram2 > countsGram1)  // (hash collision)
    {
        countsGram2 = 0.0;
    }
    countsGram1 += TotalWords;
    countsGram2 += K;
    return countsGram2 / countsGram1;
}


long double TLangModel::Calc1StepGram2Prob(word_info_t const & winf1
    , word_info_t const & winf3
) const
{
    long double countsGram1 = winf1.weight
        , countsGram2 = winf3.unknown() ? TCount{0} 
        : GetGramHashCount(TGramKey(winf1.id, TWordId::Any, winf3.id), PerfectHash, Buckets);

    if (countsGram2 > countsGram1)  // (hash collision)
    {
        countsGram2 = 0.0;
    }
    countsGram1 += TotalWords;
    countsGram2 += K;
    return countsGram2 / countsGram1;
}

long double TLangModel::CalcGram3Prob(word_info_t const & winf1
    , word_info_t const & winf2
    , word_info_t const & winf3
) const 
{
    long double countsGram2 = winf2.unknown() ? TCount{0} 
        : GetGramHashCount(TGramKey(winf1.id, winf2.id), PerfectHash, Buckets)
    , countsGram3 = winf3.unknown() ? TCount{0}
        :   GetGramHashCount(TGramKey(winf1.id, winf2.id, winf3.id)
                , PerfectHash, Buckets
            );

    if (countsGram3 > countsGram2) 
    { // hash collision
        countsGram3 = 0;
    }
    countsGram2 += TotalWords;
    countsGram3 += K;
    return countsGram3 / countsGram2;
}

/*
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
*/

TCount TLangModel::GetGramHashCount(TGramKey const & key
    , TPerfectHash const & ph
    , buckets_type const & buckets
) const
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

} // NJamSpell
