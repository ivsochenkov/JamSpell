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
    using impl_type = candidates_t;

    struct wheap_order_t
    {
        bool operator () (cand_word_t const & lhs, cand_word_t const & rhs) const 
        { return lhs.cnt > rhs.cnt; }  // max-heap!
    }; 

public:

    using result_type = impl_type;

    explicit TCandMgr (candidates_t & cands, std::size_t const maxCandCnt = 64u)
    : m_impl{cands}, m_max_cnt{maxCandCnt}, m_cand_kind{ckFirstLvl}
    {
        m_impl.reserve(maxCandCnt + 2u);
    }

    void set_kind (cand_kind_t const ck) { m_cand_kind = ck;}

    cand_word_t & push (wdata_t const & wd, str_t && s, cand_kind_t const k)
    {return m_impl.emplace_back(wd, std::move(s), k);}

    bool insert (wdata_t const & wd, str_view_t const & s)
    {
        if((m_impl.size() >= m_max_cnt) )
        {
            cand_word_t & hback = m_impl.back();
            if(wd.cnt <= hback.cnt)
            {
                return false;
            }
            hback = cand_word_t{wd.id, s, wd.cnt, m_cand_kind};
            reset_heap_impl();
            return true;
        }        
        m_impl.emplace_back(wd.id, s, wd.cnt, m_cand_kind);
        reset_heap_impl();
        return true;
    }

    bool empty() const {return m_impl.empty();}

private:

    void reset_heap_impl ();

    impl_type        &  m_impl;
    std::size_t const   m_max_cnt;
    cand_kind_t         m_cand_kind;
    

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

//    bool WordIsKnown( str_view_t const & word) const; 

    candidates_t GetCandidates(candidates_range_t const & context
        , size_t const position
    ) const;

    /*
    std::vector<std::wstring> GetCandidates(const std::vector<std::wstring>& sentence
        , size_t const position
        , bool const include_orig  = true
    ) const;

    
    std::vector<std::pair<std::wstring,double> > GetCandidatesWithScores(
          const std::vector<std::wstring>& sentence
        , size_t const position
        , bool const include_orig  = true
    ) const;
    */

    std::wstring FixFragment(const std::wstring& text) const;
    
    void SetPenalty(double knownWordsPenalty, double unknownWordsPenalty);
    void SetMaxCandidatesToCheck(size_t maxCandidatesToCheck);
    
    NJamSpell::TLangModel const & GetLangModel() const {return LangModel;}

    candidates_t InitContext(text_tokens_t const & orig_txt_tok) const;

private:

    void AppendWithCase(std::wstring & result
        , std::wstring_view const & origWord
        , str_view_t const & newWord
    ) const;

    str_t PuntoSwitcher(str_view_t const &w) const;

    void FormEditsCandidates(cand_kind_t const ck
        , str_view_t const & s
        , TCandMgr & result
    ) const;

    void Edits(str_view_t const & word
        , TCandMgr & candidates
    ) const;

    std::size_t Edits2(str_view_t const & word
        , TCandMgr & candidates
    ) const;

    void InsertsImpl(str_view_t const& w
        , std::size_t const i
        , TCandMgr& result
        , str_t & buf
    ) const;
    void Inserts(str_view_t const & w
        , TCandMgr& result
        , str_t & s
    ) const;

    void Inserts2Impl(str_view_t const & w
        , std::size_t const i
        , TCandMgr& result
        , str_t & s
        , str_t & buf
    ) const;
    void Inserts2(str_view_t const & w
        , TCandMgr& result
    ) const;


    void PrepareCache();
    bool LoadCache(const std::string& cacheFile);
    bool SaveCache(const std::string& cacheFile);

    bool LookupAndAppend2Candidates(str_view_t const & w
        , TCandMgr & candidates
    ) const;

    candidates_crange_t GetSentenceRange( candidates_range_t const & sentence
        , std::size_t const pos
    ) const;

    float ScoreOrig(candidates_range_t const & orig_sent
        , std::size_t const pos
    ) const;

    void Score(candidates_range_t const & context
        , std::size_t const pos
        , candidates_t & candidates
        , bool const sw_orig_is_known
    ) const;

    double ScoreCandidate (double sc
        , cand_kind_t const ck
        , bool const orig_is_known
        , bool const sw_orig_is_known 
    ) const;

private:

    TLangModel                      LangModel;
    std::unique_ptr<TBloomFilter>   Deletes1;
    std::unique_ptr<TBloomFilter>   Deletes2;

    double      OrigWordIsKnownPenalty      = 30.0   // 20   
            ,   OrigWordIsUnknownPenalty    = 5.0  // 5    
            ,   SecondLvlPenFactor          = 50.0       // 50
            ,   SecondLvlPenalty            = 3.0       
            ,   SwitchedWordPenalty         = 3.0
            ,   SwitchedWordIsKnownPenalty  = 5.0
            ;

    size_t      MaxCandidatesToCheck = 64;

};


} // NJamSpell
