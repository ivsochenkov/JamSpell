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
    using str_hash_type = std::hash<wstr_view_type>;

    using id_set_type = tsl::robin_set<std::uint64_t>;

    using impl_type = words_seq_t;

    struct wheap_order_t
    {
        bool operator () (word_info_t const & lhs, word_info_t const & rhs) const 
        { return lhs.weight > rhs.weight; }  // max-heap!
    }; 

public:

    using result_type = impl_type;

    explicit TCandMgr (result_type & cands, std::size_t const maxCandCnt = 1024
        , std::size_t const expCandCnt = 2048
    )
    : m_impl{cands}, m_ids {}, m_max_cnt{maxCandCnt}
    {
        m_impl.reserve(maxCandCnt + 1);     // for orig_word at later stages... 
        m_ids.reserve(expCandCnt);
    }

    bool probe (wstr_view_type const & sw)  
    {
        static constexpr str_hash_type  sHasher{};
        std::size_t const strH = sHasher(sw);
        return m_ids.insert(strH).second;
    }

    bool insert (word_info_t const & inf)
    {
        if((m_max_cnt >= m_impl.size()) && (inf.weight <= m_impl.back().weight))
        {
            return false;
        }        
        insert_impl(inf);
        return true;
    }

    bool empty() const {return m_impl.empty();}

    std::size_t get_probed_cnt() const {return m_ids.size();}

    void prepare_to_score()
    {
        id_set_type{}.swap(m_ids);  // just cleanup
        //std::push_heap(m_impl.begin(), m_impl.end(), wheap_order);
    }

private:

    void insert_impl (word_info_t const & inf)
    {        
        m_impl.push_back(inf);
        static wheap_order_t const wheap_order{};
        std::push_heap(m_impl.begin(), m_impl.end(), wheap_order);
        std::pop_heap(m_impl.begin(), m_impl.end(), wheap_order);
    }

    

    impl_type         & m_impl;
    id_set_type         m_ids;
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

    bool WordIsKnown(const std::wstring_view& word) const;

    words_seq_t GetCandidates(words_seq_t & orig_sent, size_t const position) const;

    TWords GetCandidatesRaw(const TWords& sentence, size_t const position) const;

    std::vector<std::wstring> GetCandidates(const std::vector<std::wstring>& sentence
        , size_t const position
    ) const;

    std::vector<std::pair<std::wstring,double> > GetCandidatesWithScores(
        const std::vector<std::wstring>& sentence
        , size_t const position
    ) const;
    
    std::wstring FixFragment(const std::wstring& text) const;
    std::wstring FixFragmentNormalized(const std::wstring& text) const;
    
    void SetPenalty(double knownWordsPenalty, double unknownWordsPenalty);
    void SetMaxCandidatesToCheck(size_t maxCandidatesToCheck);
    
    const NJamSpell::TLangModel& GetLangModel() const;

private:

    using sent_range = boost::iterator_range<word_info_t const*>;
    
    void AppendWithCase(std::wstring & result
        , std::wstring_view const & origWord
        , std::wstring_view const &newWord
    ) const;

    void FilterCandidatesByFrequency(
            std::unordered_set<TWord, TWordHashPtr>& uniqueCandidates
            , TWord const origWord
    ) const;

    std::wstring PuntoSwitcher(TWord const &w) const;

    void FormEditsCandidates(word_info_t const & word
        , TCandMgr & candidates
        , bool & firstLevel
    ) const;

    void Edits(wstr_view_type const& word, TCandMgr & candidates) const;
    void Edits2(wstr_view_type const& word, TCandMgr & candidates, bool lastLevel = true) const;

    void Inserts(const std::wstring_view& w, TCandMgr& result) const;
    void Inserts2(const std::wstring_view& w, TCandMgr& result) const;

    void PrepareCache();
    bool LoadCache(const std::string& cacheFile);
    bool SaveCache(const std::string& cacheFile);

    bool Add2CandidatesIfSuitable(std::wstring_view const & s
        , TCandMgr & candidates
    ) const;

    bool LookupAndAppend2Candidates(std::wstring_view const & w
        , TCandMgr & candidates
    ) const;

    words_seq_t MakeSentence(TWords const & sentence) const;

    void InitSentWords(sentences_t & sentences) const;

    sent_range GetSentenceRange( words_seq_t & sentence, std::size_t const pos) const;

    void Score(words_seq_t & in_sentence
        , std::size_t const sz
        , bool const first_level
        , words_seq_t & candidates
    ) const;

private:

    TLangModel LangModel;
    std::unique_ptr<TBloomFilter> Deletes1;
    std::unique_ptr<TBloomFilter> Deletes2;
    double KnownWordsPenalty = 20.0;
    double UnknownWordsPenalty = 5.0;
    size_t MaxCandidatesToCheck = 14;

};


} // NJamSpell
