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

//using TWordId = uint32_t;
enum class TWordId : uint32_t 
{
    Unknown = std::numeric_limits<std::underlying_type<TWordId>::type>::max()
};

inline constexpr std::underlying_type<TWordId>::type to_underlying(TWordId w)
{return static_cast<std::underlying_type<TWordId>::type>(w);}

using TCount = uint32_t;

using TWordIds = std::vector<TWordId>;
using TIdSentences = std::vector<TWordIds>;

struct wstr_hash_t
{
    std::size_t operator () (std::wstring_view const & w) const
    {
        static std::hash<std::wstring_view > hasher{};
        return hasher(w); 
    }
};

using wstr_to_id_map_t = tsl::robin_map<std::wstring, TWordId
        , wstr_hash_t
        , std::equal_to<void> //wstr_equal_t
        , std::allocator<std::pair<std::wstring, TWordId> >
        , true // store hash
        , tsl::rh::mod_growth_policy<std::ratio<11, 10> >
    >;

class TRobinSerializer: public NHandyPack::TUnorderedMapSerializer<
    wstr_to_id_map_t, std::wstring, TWordId
> 
{};

class TRobinHash: public wstr_to_id_map_t 
{
public:
    virtual void Dump(std::ostream& out) const {
        TRobinSerializer::Dump(out, *this);
    }
    virtual void Load(std::istream& in) {
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

    double Score(const TWords& words) const;
    double Score(std::wstring str) const;

    TWord GetWord(const std::wstring_view& word) const;

    alphabet_type const & GetAlphabet() const { return Tokenizer.GetAlphabet();}
    TSentences Tokenize(const std::wstring& text) const;

    bool Dump(const std::string& modelFileName) const;
    bool Load(const std::string& modelFileName);
    void Clear();

    const TRobinHash& GetWordToId();

    TWordId GetWordId(const TWord& word);
    TWordId GetWordIdNoCreate(const TWord& word) const;
    //TWord GetWordById(TWordId wid) const;
    TCount GetWordCount(TWordId wid) const;

    uint64_t GetCheckSum() const;

    HANDYPACK(WordToId, LastWordID, TotalWords, VocabSize,
              PerfectHash, Buckets, Tokenizer, CheckSum)
private:

    TIdSentences ConvertToIds(const TSentences& sentences);

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
