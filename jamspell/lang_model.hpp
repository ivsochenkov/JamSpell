#pragma once

#include "tokenizer.hpp"
#include "perfect_hash.hpp"
#include "utils.hpp"

#include <array>
#include <vector>
#include <utility>
#include <string>
#include <limits>

#include <contrib/handypack/handypack.hpp>
#include <contrib/tsl/robin_map.h>


namespace NJamSpell {


constexpr uint64_t LANG_MODEL_MAGIC_BYTE = 8559322735408079685L;
constexpr uint16_t LANG_MODEL_VERSION = 9;
constexpr double LANG_MODEL_DEFAULT_K = 0.05;

struct wstr_hash_t
{
    std::size_t operator () (std::wstring_view const & w) const
    {
        static std::hash<std::wstring_view > hasher{};
        return hasher(w); 
    }
};

struct wdata_t
{
    TWordId     id;
    TCount      count;

    explicit wdata_t(TWordId const i = TWordId::Unknown, TCount const c = 0)
    :id{i}, count {c}
    {}

    HANDYPACK(id, count)
};


using wstr_to_id_map_t = tsl::robin_map<std::wstring, wdata_t
        , wstr_hash_t
        , std::equal_to<void> //wstr_equal_t
        , std::allocator<std::pair<std::wstring, TWordId> >
        , true // store hash
        , tsl::rh::prime_growth_policy
    >;

class TRobinSerializer: public NHandyPack::TUnorderedMapSerializer<
    wstr_to_id_map_t, std::wstring, wdata_t
> 
{};

class TRobinHash: public wstr_to_id_map_t 
{
public:
    void Dump(std::ostream& out) const {
        TRobinSerializer::Dump(out, *this);
    }
    void Load(std::istream& in) {
        TRobinSerializer::Load(in, *this);
    }
};


class TLangModel 
{
    struct TGramLoader ;

    struct train_options_t
    {
        std::size_t             max_grams_sz        = 70000000;
        std::array<unsigned, 4> ngram_thresholds    = {8, 4, 2};
                        
        float                   growth_factor       = 1.02;

        static train_options_t make_default();
    };

public:

    using alphabet_type = TTokenizer::alphabet_type;

    bool Train(const std::string& fileName
        , const std::string& alphabetFile
        , train_options_t const & tr_opt = train_options_t::make_default()
    );

    double Score(word_info_t const * beg, word_info_t const * e) const;

    double Score(const TWords& words) const;
    double Score(std::wstring str) const;

    TWord GetWord(const std::wstring_view& word) const;

    word_info_t GetWordInfo(const std::wstring_view& word) const;

    alphabet_type const & GetAlphabet() const { return Tokenizer.GetAlphabet();}
    TTokenizer const & GetTokenizer() const {return Tokenizer;}

    

    bool Dump(const std::string& modelFileName) const;
    bool Load(const std::string& modelFileName);
    void Clear();

    const TRobinHash& GetWordToId();

    TWordId MakeWordId(const TWord& word);
    TWordId GetWordId(const TWord& word) const;
    //TWord GetWordById(TWordId wid) const;
    TCount GetWordCount(TWordId wid) const;

    uint64_t GetCheckSum() const;

    HANDYPACK(WordToId, LastWordID, TotalWords, VocabSize,
              PerfectHash, Buckets, Tokenizer, CheckSum)
private:

    TIdSentences ConvertToIds(const TSentences& sentences);

    long double CalcGram1Prob(word_info_t const & winf) const
    {
        return ((long double )(winf.weight) + K) / (TotalWords + VocabSize);
    }
    
    long double CalcGram2Prob(word_info_t const & winf1
        , word_info_t const & winf2
    ) const;

    long double CalcGram3Prob(word_info_t const & winf1
        , word_info_t const & winf2
        , word_info_t const & winf3
    ) const;

    double GetGram1Prob(TWordId word) const;
    double GetGram2Prob(TWordId word1, TWordId word2) const;
    double GetGram3Prob(TWordId word1, TWordId word2, TWordId word3) const;

    TCount GetGram1HashCount(TWordId word) const;
    TCount GetGram2HashCount(TWordId word1, TWordId word2) const;
    TCount GetGram3HashCount(TWordId word1, TWordId word2, TWordId word3) const;

private:
    //const TWordId UnknownWordId = std::numeric_limits<TWordId>::max();
    double K = LANG_MODEL_DEFAULT_K;
    TRobinHash WordToId;
    //std::vector<const std::wstring*> IdToWord;
    std::underlying_type<TWordId>::type LastWordID = 0, 
                                        TotalWords = 0,
                                        VocabSize = 0;

    TTokenizer Tokenizer;
    std::vector<std::pair<uint16_t, uint16_t>> Buckets;
    TPerfectHash PerfectHash;
    uint64_t CheckSum;
};


} // NJamSpell
