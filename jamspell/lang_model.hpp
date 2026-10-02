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

#include <contrib/tsl/array-hash/array_map.h>
#include <contrib/tsl/htrie_map.h>


namespace NJamSpell 
{


constexpr uint64_t LANG_MODEL_MAGIC_BYTE = 8559322735408079686L;
constexpr uint16_t LANG_MODEL_VERSION = 2;
constexpr double LANG_MODEL_DEFAULT_K = 0.01;

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
        static_cast<str_to_id_map_t &>(*this) 
            = str_to_id_map_t::deserialize(ds, true);
    }
};

/////////////////////////////////////////////////////////////////////////////////

class CDict
{
    using dict_impl_type = tsl::array_map<char, dict_info_t
        , tsl::ah::str_hash<char>
        , tsl::ah::str_equal<char>
        , false, std::uint8_t
        , std::uint32_t
        , tsl::ah::power_of_two_growth_policy<2>
        >;

public:

    explicit CDict(TAlphabet const & alphbt)
    : m_dict{}, m_alphabet{alphbt}
    {}

    std::size_t Load(std::string const & dictFnam);

    dict_info_t Get(str_view_t const & wrd) const
    {
        auto const & i = m_dict.find(wrd);
        return i == m_dict.end() ? dict_info_t::diNone : i.value();
    }

    template <typename TVisitor>
    void VisitAll(TVisitor && f) const
    { VisitImpl(m_dict, std::forward<TVisitor>(f));}

    template <typename TVisitor>
    void VisitAll(TVisitor && f)
    { VisitImpl(m_dict, std::forward<TVisitor>(f));}

private:

    template <typename TDict, typename TVisitor>
    static void VisitImpl(TDict & dict, TVisitor && f)
    { 
        for (auto i = dict.begin(), e = dict.end(); i != e; ++i)
        {
            std::invoke(std::forward<TVisitor>(f), i);
        }
    }

    dict_impl_type  m_dict;
    TAlphabet       m_alphabet;

};

/////////////////////////////////////////////////////////////////////////////////

class TLangModel 
{
    struct TGramLoader ;
    class TGramKey;

    using buckets_type = std::vector<std::pair<::std::uint16_t, ::std::uint16_t>> ;

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

    bool Train(const std::string& datasetFIle
        , const std::string& dictFile
        , const std::string& alphabetFile
        , train_options_t const & tr_opt = train_options_t::ReadFromEnv()
    );

    bool TokenIsBad(str_view_t const & alStr, token_stat_t const & ts) const
    {
        return (!ts.is_title_case)
            && (
                    (alStr.size() >= 3 && ts.consonant_cnt == alStr.size())
                ||  (alStr.size() > 3 && ts.vowel_cnt == alStr.size())
                ||  ts.max_consonant_in_row > 7
                ||  ts.max_vovel_in_row > 5
               );
    }

    template <typename TWIt>
    float Score(TWIt beg, TWIt const & e) const;

    template <typename TWIt>
    float Score(boost::iterator_range<TWIt> const &r) const
    {return Score(r.begin(), r.end());}

    float Score(text_tokens_t & words) const;
    float Score(std::wstring const & str) const;

    //str_t GetWord( str_view_t const & word) const;

    wdata_t GetWordInfo(str_view_t const & word) const;

    word_t LongestPrefixSearch(str_view_t const & word) const;

    alphabet_type const & GetAlphabet() const { return Tokenizer.GetAlphabet();}
    TTokenizer const & GetTokenizer() const {return Tokenizer;}

    bool Dump(const std::string& modelFileName) const;
    bool Load(const std::string& modelFileName);
    void Clear();

    std::size_t dict_size() const {return WordToId.size();}
    std::size_t avg_word_length(std::size_t max_probes = 10000u) const;
    std::size_t total_word_occs() const {return TotalWords;}

    
    TWordId UpdateWordIdIfPresent(str_view_t const & word, cnt_t const c = 1u)
    {
        assert(!word.empty());
        auto i = WordToId.find(word);
        return (i != WordToId.end()) ? (i.value().cnt += c, i.value().id) 
            : TWordId::Unknown;
    }   

    TWordId UpdateWordId(str_view_t const & word, cnt_t const c = 1u)
    {
        assert(!word.empty());
        auto insR = WordToId.emplace(word, wdata_t{TWordId (LastWordID), 0u} );
        insR.first.value().cnt += c;
        LastWordID += insR.second;
        return insR.first.value().id;
    }

    TWordId UpdateWordId(str_view_t const & word
        , bool if_present
        , cnt_t const c = 1u
    )
    {
        return (if_present) ? UpdateWordIdIfPresent(word, c) 
            : UpdateWordId (word, c);
    }

    TWordId GetWordId(str_view_t const & word) const
    {
        auto it = WordToId.find(word);
        return (it != WordToId.end()) ? it.value().id : TWordId::Unknown;
    }

    template <typename TTokens, typename TWords>
    void InitWords(TTokens & orig_txt_tok, TWords & wrds) const;

    template <typename TCntxt>
    void InitContext(TCntxt & cntxt) const;

    void Text2Words(wstr_view_t const & txt, words_t & wrds) const;
    void Text2Words(wstr_view_t const & txt, context_t & cntxt) const;

    uint64_t GetCheckSum() const {return CheckSum;}

    using dict_const_iterator = TWord2IdMap::const_iterator;

    dict_const_iterator dict_begin() const {return WordToId.begin();}
    dict_const_iterator dict_end() const {return WordToId.end();}


    HANDYPACK(WordToId, LastWordID, TotalWords, VocabSize,
              PerfectHash, Buckets, Tokenizer, CheckSum)
private:

    bool InitWordFromToken(token_info_t & tinf, word_t & w) const;

    double CalcGram1Prob(wdata_t const & winf) const
    {
        //return (winf.cnt + K) / (TotalWords + VocabSize); // JS_CNT_FIX
        return (static_cast<double>(winf.cnt) + K) / (TotalWords); 
    }
    
    double CalcGram2Prob(wdata_t const & winf1
        , wdata_t const & winf2
    ) const;

    double Calc1StepGram2Prob(wdata_t const & winf1
        , wdata_t const & winf3
    ) const;

    double CalcGram3Prob(wdata_t const & winf1
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
float TLangModel::Score(TWIt beg, TWIt const & e) const
{
    double result = 0.0;
    static wdata_t const unkn_wi {TWordId::Unknown};
    TWIt next1 = Advance2Next(beg, e); //std::advance(next1, 1); 
    TWIt next2 = Advance2Next(next1, e);

    do 
    {
        wdata_t const & wd = WordData(*beg);
        result += std::log2(CalcGram1Prob(wd));
        wdata_t const & rN1 = (next1 < e) ? WordData(*next1): unkn_wi;
        result += std::log2(CalcGram2Prob(wd, rN1 ));
        wdata_t const & rN2 = (next2 < e) ? WordData(*next2): unkn_wi;
        result += std::log2(CalcGram3Prob(wd, rN1, rN2));
        result += std::log2(Calc1StepGram2Prob(wd, rN2));
  
        beg = next1;
        next1 = next2;
        next2 = Advance2Next(next2, e);
    }
    while (beg != e);

    return result;
}

template <typename TTokens, typename TWords>
void TLangModel::InitWords(TTokens & orig_txt_tok, TWords & wrds) const
{   
    wrds.resize(orig_txt_tok.size());
    auto wit = wrds.begin();
    for (auto & orig_token : orig_txt_tok)
    {
        wit += InitWordFromToken(orig_token, Word(*wit));
    }
    wrds.resize(std::distance(wrds.begin(), wit));
}

template <typename TCntxt>
void TLangModel::InitContext(TCntxt & cntxt) const
{   
    auto wit = cntxt.begin();
    for (cntxt_word_t & cw : cntxt)
    {
        SetKind(*wit, cand_kind_t(InitWordFromToken(cw.token, Word(*wit))));
        ++wit;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // NJamSpell
