#pragma once

#include "lang_model.hpp"
#include "bloom_filter.hpp"

#include <contrib/tsl/robin_map.h>
#include <contrib/tsl/robin_set.h>

#include <boost/range/iterator_range.hpp>

namespace NJamSpell 
{

class TCandMgr 
{
    using str_hash_type = std::hash<wstr_view_t>;

    using id_set_type = tsl::robin_set<std::uint64_t>;

    using impl_type = words_seq_t;

    struct wheap_order_t
    {
        bool operator () (word_info_t const & lhs, word_info_t const & rhs) const 
        { return lhs.weight > rhs.weight; }  // max-heap!
    }; 

public:

    using result_type = impl_type;

    explicit TCandMgr (result_type & cands, std::size_t const maxCandCnt = 64)
    : m_impl{cands}, m_max_cnt{maxCandCnt}
    {
        m_impl.reserve(maxCandCnt + 1);     // for orig_word at later stages... 
    }

    bool insert (word_info_t const & inf)
    {
        if((m_impl.size() >= m_max_cnt) && (inf.weight <= m_impl.back().weight))
        {
            return false;
        }        
        insert_impl(inf);
        return true;
    }

    bool empty() const {return m_impl.empty();}

private:

    void insert_impl (word_info_t const & inf)
    {        
        m_impl.push_back(inf);
        static wheap_order_t const wheap_order{};
        std::push_heap(m_impl.begin(), m_impl.end(), wheap_order);
        std::pop_heap(m_impl.begin(), m_impl.end(), wheap_order);
    }

    

    impl_type         & m_impl;
    std::wstring_view   m_orig;
    std::size_t const   m_max_cnt;
    

};

////////////////////////////////////////////////////////////////////////////////

class TSpellCorrector 
{

public:

    static constexpr std::size_t word_cand_list_size_default = 11;

    bool LoadLangModel(const std::string& modelFile);
    
    bool TrainLangModel(const std::string& textFile
        , const std::string& alphabetFile
        , const std::string& modelFile
    );

    bool WordIsKnown( str_view_t const & word) const; 

    words_seq_t GetCandidates(word_seq_range_t const & orig_sent
        , size_t const position
    ) const;

    //TWords GetCandidatesRaw(const TWords& sentence, size_t const position) const;

    std::vector<std::wstring> GetCandidates(const std::vector<std::wstring>& sentence
        , size_t const position
    ) const;

    std::vector<std::pair<std::wstring,double> > GetCandidatesWithScores(
        const std::vector<std::wstring>& sentence
        , size_t const position
    ) const;
    
    std::wstring FixFragment(const std::wstring& text) const;
    //std::wstring FixFragmentNormalized(const std::wstring& text) const;
    
    void SetPenalty(double knownWordsPenalty, double unknownWordsPenalty);
    void SetMaxCandidatesToCheck(size_t maxCandidatesToCheck);
    
    const NJamSpell::TLangModel& GetLangModel() const;

private:

    using sent_range = boost::iterator_range<word_info_t const*>;
    
    void AppendWithCase(std::wstring & result
        , std::wstring_view const & origWord
        , str_view_t const & newWord
    ) const;

    str_t PuntoSwitcher(str_view_t const &w) const;
    void ReInitWord(word_info_t & word, str_t && new_swtch_word) const;

    void FormEditsCandidates(word_info_t const & word
        , TCandMgr & candidates
        , bool const firstLevel
    ) const;

    void Edits(str_view_t const & word, TCandMgr & candidates) const;
    void Edits2(str_view_t const & word
        , TCandMgr & candidates
        , bool const firstLevel
        , bool const lastLevel = true
    ) const;

    void InsertsImpl(str_view_t const& w
        , std::size_t const i
        , TCandMgr& result
        , str_t & buf
    ) const;
    void Inserts(str_view_t const & w, TCandMgr& result, str_t & s) const;

    void Inserts2Impl(str_view_t const & w
        , std::size_t const i
        , TCandMgr& result
        , str_t & s
        , str_t & buf
    ) const;
    void Inserts2(str_view_t const & w, TCandMgr& result) const;


    void PrepareCache();
    bool LoadCache(const std::string& cacheFile);
    bool SaveCache(const std::string& cacheFile);

    bool LookupAndAppend2Candidates(bool const first_level
        , str_view_t const & w
        , TCandMgr & candidates
    ) const;

    sent_range GetSentenceRange( word_seq_range_t const & sentence
        , std::size_t const pos
    ) const;

    void Score(bool const force_orig_word_score
        , word_seq_range_t const & orig_sent
        , std::size_t const sz
        , words_seq_t & candidates
    ) const;

    words_seq_t Merge(words_seq_t const & a, words_seq_t const & b) const;

    words_seq_t ProcessCandidates(bool const force_orig_word_score
        , word_seq_range_t const & orig_sent
        , size_t const position
        , bool const firstLevel
    ) const;

private:

    TLangModel                      LangModel;
    std::unique_ptr<TBloomFilter>   Deletes1;
    std::unique_ptr<TBloomFilter>   Deletes2;

    double      KnownWordsPenalty = 15.0        // 20
            ,   UnknownWordsPenalty = 5.0       // 5
            ,   SecondLvlPenFactor = 35.0       // 50
            ;

    size_t      MaxCandidatesToCheck = 128;

};


} // NJamSpell
