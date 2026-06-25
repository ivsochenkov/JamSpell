#pragma once

#include "tokenizer.hpp"
#include "perfect_hash.hpp"
#include "utils.hpp"

#include <array>
#include <vector>
#include <utility>
#include <string>
#include <limits>
#include <cmath>
#include <algorithm>
#include <type_traits>

#include <contrib/handypack/handypack.hpp>
#include <contrib/tsl/robin_map.h>

#include <contrib/tsl/htrie_map.h>


namespace NJamSpell 
{


constexpr uint64_t LANG_MODEL_MAGIC_BYTE = 8559322735408079686L;
constexpr uint16_t LANG_MODEL_VERSION = 1;
constexpr double LANG_MODEL_DEFAULT_K = 0.05;




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
        std::size_t             max_grams_sz        = 100000000;
        std::array<unsigned, 3> ngram_thresholds    = {7, 5, 3};
                        
        //float                   growth_factor       = 1.003;

        static train_options_t make_default();

        static train_options_t ReadFromEnv();
    };

    using alphabet_type = TTokenizer::alphabet_type;

    bool Train(const std::string& fileName
        , const std::string& alphabetFile
        , train_options_t const & tr_opt = train_options_t::ReadFromEnv()
    );

    template <typename TWIt>
    double Score(TWIt beg, TWIt const & e) const;

    template <typename TWIt>
    double Score(boost::iterator_range<TWIt> const &r) const
    {return Score(r.begin(), r.end());}

    double Score(text_tokens_t & words) const;
    double Score(std::wstring const & str) const;

    //str_t GetWord( str_view_t const & word) const;

    wdata_t GetWordInfo(str_view_t const & word) const;

    alphabet_type const & GetAlphabet() const { return Tokenizer.GetAlphabet();}
    TTokenizer const & GetTokenizer() const {return Tokenizer;}

    bool Dump(const std::string& modelFileName) const;
    bool Load(const std::string& modelFileName);
    void Clear();

    std::size_t dict_size() const {return WordToId.size();}
    std::size_t avg_word_length(std::size_t max_probes = 10000u) const;

    TWordId UpdateWordId(str_view_t const & word);
    TWordId GetWordId(str_view_t const & word) const;

    template <typename TWords>
    void InitWords(text_tokens_t const & orig_txt_tok, TWords & wrds) const;

    uint64_t GetCheckSum() const {return CheckSum;}

    using dict_const_iterator = TWord2IdMap::const_iterator;

    dict_const_iterator dict_begin() const {return WordToId.begin();}
    dict_const_iterator dict_end() const {return WordToId.end();}


    HANDYPACK(WordToId, LastWordID, TotalWords, VocabSize,
              PerfectHash, Buckets, Tokenizer, CheckSum)
private:

    long double CalcGram1Prob(wdata_t const & winf) const
    {
        return ((long double )(winf.cnt) + K) / (TotalWords + VocabSize);
    }
    
    long double CalcGram2Prob(wdata_t const & winf1
        , wdata_t const & winf2
    ) const;

    long double Calc1StepGram2Prob(wdata_t const & winf1
        , wdata_t const & winf3
    ) const;

    long double CalcGram3Prob(wdata_t const & winf1
        , wdata_t const & winf2
        , wdata_t const & winf3
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
    std::underlying_type<TWordId>::type LastWordID = to_underlying(TWordId::Any) + 1u, 
                                        TotalWords = 0,
                                        VocabSize = 0;

    TTokenizer                  Tokenizer;
    buckets_type                Buckets;
    TPerfectHash                PerfectHash;
    uint64_t                    CheckSum;
};

////////////////////////////////////////////////////////////////////////////////

template <typename TWIt>
double TLangModel::Score(TWIt beg, TWIt const & e) const
{
    double result = 0.0;
    static wdata_t const unkn_wi {TWordId::Unknown};
    TWIt next1 {beg}, next2 {beg};
    std::advance(next1, 1);
    std::advance(next2, 2);
    do 
    {
        result += std::log2(CalcGram1Prob(*beg));
        wdata_t const & rN1 = (next1 < e) ? *next1: unkn_wi;
        result += std::log2(CalcGram2Prob(*beg, rN1 ));
        wdata_t const & rN2 = (next2 < e) ? *next2: unkn_wi;
        result += std::log2(CalcGram3Prob(*beg, rN1, rN2));
        result += std::log2(Calc1StepGram2Prob(*beg, rN2));
  
        ++next1;
        ++next2;
    }
    while (++beg != e);

    return result;
}

template <typename TWords>
void TLangModel::InitWords(text_tokens_t const & orig_txt_tok, TWords & wrds) const
{   
    wrds.resize(orig_txt_tok.size());
    auto wit = wrds.begin();
    for (wstr_view_t const & orig_token : orig_txt_tok)
    {
        if((!orig_token.empty()) && !TTokenizer::isSentEnd(orig_token))
        {
            str_t al_str = ToAlphabet(Tokenizer.GetAlphabet(), orig_token);
            if(WellFormedInAlphabet(al_str))
            {                
                wit -> str = std::move(al_str);
                wit -> reset (GetWordInfo(wit -> str));
            }
        }
        ++wit;
    }
    wrds.resize(std::distance(wrds.begin(), wit));
}

////////////////////////////////////////////////////////////////////////////////

} // NJamSpell
