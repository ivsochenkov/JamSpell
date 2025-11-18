#pragma once

#include "tokenizer.hpp"
#include "perfect_hash.hpp"
#include "utils.hpp"

#include <array>
#include <vector>
#include <utility>
#include <string>
#include <limits>
#include <type_traits>

#include <contrib/handypack/handypack.hpp>
#include <contrib/tsl/robin_map.h>

#include <contrib/tsl/htrie_map.h>


namespace NJamSpell 
{


constexpr uint64_t LANG_MODEL_MAGIC_BYTE = 8559322735408079685L;
constexpr uint16_t LANG_MODEL_VERSION = 9;
constexpr double LANG_MODEL_DEFAULT_K = 0.05;

/*
struct wstr_hash_t
{
    std::size_t operator () (std::wstring_view const & w) const
    {
        static std::hash<std::wstring_view > hasher{};
        return hasher(w); 
    }
};
*/

struct wdata_t
{
    TWordId     id;
    TCount      count;

    explicit wdata_t(TWordId const i = TWordId::Unknown, TCount const c = 0)
    :id{i}, count {c}
    {}

    HANDYPACK(id, count)
};

/*

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
*/


using str_to_id_map_t = tsl::htrie_map<char, wdata_t
    , tsl::ah::str_hash<char>
    , std::uint8_t
>;

class TWord2IdMap : public str_to_id_map_t
{
    struct serializer_t
    {
        explicit serializer_t (std::ostream& out)
        : m_out(out) {}

        template <typename U
            , typename std::enable_if<std::is_arithmetic<U>::value 
                || std::is_same<U, wdata_t>::value>::type* = nullptr
        >
        void operator()(const U& value)
        { NHandyPack::Dump(m_out, value);}

        void operator()(const char* value, std::size_t value_size)
        { m_out.write(value, value_size); }

        private:
            std::ostream& m_out;
    };

   struct deserializer_t
    {
        explicit deserializer_t (std::istream& in)
        : m_in(in) {}

        template <typename U
            , typename std::enable_if<std::is_arithmetic<U>::value 
            || std::is_same<U, wdata_t>::value >::type* = nullptr
        >
        U operator()()
        { 
            U value;
            NHandyPack::Load(m_in, value);
            return value;
        }

        void operator()(char* value, std::size_t value_size)
        { m_in.read(value, value_size); }

        private:

            std::istream& m_in;
    };

public:
    void Dump(std::ostream& out) const 
    {
        serializer_t s(out);
        this->serialize(s);
    }
    void Load(std::istream& in) 
    {
        deserializer_t ds(in);
        static_cast<str_to_id_map_t &>(*this) = str_to_id_map_t::deserialize(ds, true);
    }
};

class TLangModel 
{
    struct TGramLoader ;
    class TGramKey;

    using buckets_type = std::vector<std::pair<uint16_t, uint16_t>> ;

public:

    struct train_options_t
    {
        std::size_t             max_grams_sz        = 70000000;
        std::array<unsigned, 4> ngram_thresholds    = {10, 6, 4};
                        
        float                   growth_factor       = 1.015;

        static train_options_t make_default();
    };

    using alphabet_type = TTokenizer::alphabet_type;

    bool Train(const std::string& fileName
        , const std::string& alphabetFile
        , train_options_t const & tr_opt = train_options_t::make_default()
    );

    double Score(word_info_t const * beg, word_info_t const * e) const;

    double Score(text_tokens_t & words) const;
    double Score(std::wstring const & str) const;

    str_t GetWord( str_view_t const & word) const;

    word_info_t GetWordInfo(str_view_t const & word) const;

    alphabet_type const & GetAlphabet() const { return Tokenizer.GetAlphabet();}
    TTokenizer const & GetTokenizer() const {return Tokenizer;}

    bool Dump(const std::string& modelFileName) const;
    bool Load(const std::string& modelFileName);
    void Clear();

    std::size_t dict_size() const {return WordToId.size();}
    std::size_t avg_word_length(std::size_t max_probes = 10000u) const;

    TWordId UpdateWordId(str_view_t const & word);
    TWordId GetWordId(str_view_t const & word) const;

    words_seq_t InitWords(text_tokens_t & orig_txt_tok) const;

    uint64_t GetCheckSum() const {return CheckSum;}

    using dict_const_iterator = TWord2IdMap::const_iterator;

    dict_const_iterator dict_begin() const {return WordToId.begin();}
    dict_const_iterator dict_end() const {return WordToId.end();}


    HANDYPACK(WordToId, LastWordID, TotalWords, VocabSize,
              PerfectHash, Buckets, Tokenizer, CheckSum)
private:

    long double CalcGram1Prob(word_info_t const & winf) const
    {
        return ((long double )(winf.weight) + K) / (TotalWords + VocabSize);
    }
    
    long double CalcGram2Prob(word_info_t const & winf1
        , word_info_t const & winf2
    ) const;

    long double Calc1StepGram2Prob(word_info_t const & winf1
        , word_info_t const & winf3
    ) const;

    long double CalcGram3Prob(word_info_t const & winf1
        , word_info_t const & winf2
        , word_info_t const & winf3
    ) const;
    
    TCount GetGramHashCount(TGramKey const & key
        , TPerfectHash const & ph
        , buckets_type const & buckets
    ) const;

    
    //const TWordId UnknownWordId = std::numeric_limits<TWordId>::max();
    double K = LANG_MODEL_DEFAULT_K;
    //TRobinHash WordToId;
    TWord2IdMap                         WordToId;
    //std::vector<const std::wstring*> IdToWord;
    std::underlying_type<TWordId>::type LastWordID = to_underlying(TWordId::Any) + 1, 
                                        TotalWords = 0,
                                        VocabSize = 0;

    TTokenizer                  Tokenizer;
    buckets_type                Buckets;
    TPerfectHash                PerfectHash;
    uint64_t                    CheckSum;
};


} // NJamSpell
