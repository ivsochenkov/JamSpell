#pragma once

#include "lang_model.hpp"
#include "bloom_filter.hpp"

#include <contrib/tsl/robin_map.h>

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
    : m_impl{cands}, m_max_sz{maxCandCnt}, m_best_cnt {0u}, m_cand_kind{ckFirstLvl}
    {
        m_impl.reserve(maxCandCnt + 2u);
    }

    void set_kind (cand_kind_t const ck) { m_cand_kind = ck;}

    /*
    cand_word_t & push (wdata_t const & wd, str_t && s, cand_kind_t const k)
    {return m_impl.emplace_back(wd, std::move(s), k);}
    */

    template <typename TStr>
    bool insert (wdata_t const & wd, TStr && s, drop_info_t const & drop)
    {
        if (m_best_cnt < wd.cnt )
        {
            m_best_cnt = wd.cnt;
        }

        if((m_impl.size() >= m_max_sz) )
        {
            cand_word_t & hback = m_impl.back();
            if(wd.cnt <= hback.cnt)
            {
                return false;
            }
            hback = cand_word_t{wd.id, std::forward<TStr>(s), wd.cnt
                , m_cand_kind, drop
            };
            reset_heap_impl();
            return true;
        }        
        m_impl.emplace_back(wd.id, std::forward<TStr>(s), wd.cnt
            , m_cand_kind, drop
        );
        reset_heap_impl();
        return true;
    }

    bool empty() const noexcept {return m_impl.empty();}
    cnt_t best_cnt() const noexcept {return m_best_cnt;}


private:

    void reset_heap_impl ();

    impl_type        &  m_impl;
    std::size_t const   m_max_sz;
    cnt_t               m_best_cnt  = 0;

    cand_kind_t         m_cand_kind;
    

};

////////////////////////////////////////////////////////////////////////////////

class TSpellCorrector 
{
private:

    struct attributes_t
    {
        bool        orig_is_known       = false
                ,   sw_orig_is_known    = false
                ,   prev_was_switched   = false
                ;

    };

public:

    struct opt_t
    {
        double      OrigWordIsKnownPenalty      = 30.0   // 20   
                ,   OrigWordIsUnknownPenalty    = 5.0  // 5    
                ,   SecondLvlPenFactor          = 50.0       // 50
                ,   SecondLvlPenalty            = 3.0       
                ,   SwitchedWordPenalty         = 3.0
                ,   SwitchedWordIsKnownPenalty  = 5.0
                ; 
            ;

        ::std::size_t         MaxCandidatesToCheck = 64
                            , InFreqWordCntThreshold = 30 // experimental!
        ;

        static opt_t ReadFromEnv();
    };


    explicit TSpellCorrector (opt_t const & opt = opt_t::ReadFromEnv());

    bool LoadLangModel(const std::string& modelFile);
    
    bool TrainLangModel(const std::string& textFile
        , const std::string& alphabetFile
        , const std::string& modelFile
    );

    candidates_t GetCandidates(context_range_t const & context
        , ::std::size_t const position
    ) const;

    context_t FixContext(std::wstring const & text) const;

    std::wstring FixFragment(const std::wstring& text) const;
    
    NJamSpell::TLangModel const & GetLangModel() const {return LangModel;}

    static context_range_t GetNextSent(context_t::iterator const & b
        , context_t::iterator const & e
    );

private:

    bool IsInfreq(word_t const & ow) const noexcept
    {
        //return (double(ow.cnt) / LangModel.total_word_occs() )
        //    < m_opt.InFreqWordThreshold;
        return ow.cnt < m_opt.InFreqWordCntThreshold;
    }

    bool CandidatesAreInfreq(TCandMgr const & cmgr) const noexcept 
    {
        // return (double(cmgr.best_cnt()) / LangModel.total_word_occs() )
        //    < m_opt.InFreqWordThreshold;
        return cmgr.best_cnt() < m_opt.InFreqWordCntThreshold;
    }

    TAlphabet const & GetAlphabet() const noexcept
    { return GetLangModel().GetTokenizer().GetAlphabet(); }

    bool HasNonSpacedNeighbours(context_range_t const & context
        , ::std::size_t const position
    ) const;

    bool PrevWordWasSwitched(context_range_t const & context
        , ::std::size_t const position
    ) const;

    void ManageDroppedTokens(cand_word_t const & top_w
        , context_t::iterator & al_word_it
    ) const;

    ::std::size_t CheckSwitchedCands (context_range_t const & context
        , ::std::size_t const position
        , TCandMgr & cmgr
        , attributes_t & attrs
    ) const;

    ::std::size_t FormGreedySwitchedCandsRight (
        context_range_t const & context
        , ::std::size_t const position
        , str_view_t const & sw_cand_str
        , ::std::size_t const lpos
        , TCandMgr & cmgr       
        , attributes_t & attrs 
    ) const;

    ::std::size_t MakeLeftSwCandsStrPrefix(context_range_t const & context
        , ::std::size_t const position
        , str_t & s
    ) const;

    ::std::size_t MakeRightSwCandsStr(context_range_t const & context
        , ::std::size_t const position
        , str_t & s
    ) const;

    static ::std::uint32_t CalcRPos(std::size_t cnt
        , context_range_t const & context
        , ::std::size_t const position
        , std::size_t const rpos_ofs
    );

    void AppendWithCase(std::wstring & result
        , wstr_view_t const & origWord
        , str_view_t const & newWord
    ) const;

    str_t PuntoSwitcher(str_view_t const &w) const;

    void Edits(str_view_t const & word
        , TCandMgr & candidates
        , drop_info_t const & di = drop_info_t{}
    ) const;

    std::size_t Edits2(str_view_t const & word
        , TCandMgr & candidates
        , drop_info_t const & di = drop_info_t{}
    ) const;

    void InsertsImpl(str_view_t const& w
        , std::size_t const i
        , TCandMgr& result
        , str_t & buf
        , drop_info_t const & di
    ) const;
    void Inserts(str_view_t const & w
        , TCandMgr& result
        , str_t & s
        , drop_info_t const & di
    ) const;

    void Inserts2Impl(str_view_t const & w
        , std::size_t const i
        , TCandMgr& result
        , str_t & s
        , str_t & buf
        , drop_info_t const & di
    ) const;
    void Inserts2(str_view_t const & w
        , TCandMgr& result
        , drop_info_t const & di
    ) const;


    void PrepareCache();
    bool LoadCache(const std::string& cacheFile);
    bool SaveCache(const std::string& cacheFile);

    bool Push2Candidates (str_t && w
        , wdata_t const & wd
        , TCandMgr & cmgr
        , drop_info_t const & di //= drop_info_t{}
    ) const;

    bool Append2Candidates (str_view_t const & w
        , wdata_t const & wd
        , TCandMgr & cmgr
        , drop_info_t const & di //= drop_info_t{}
    ) const;

    bool LookupAndAppend2Candidates(str_view_t const & w
        , TCandMgr & cmgr
        , drop_info_t const & di //= drop_info_t{}
    ) const
    {return Append2Candidates(w, LangModel.GetWordInfo(w), cmgr, di); }

    context_crange_t GetSentenceRange( context_crange_t const & sentence
        , std::size_t const pos
    ) const;

    double ScoreOrig(context_range_t const & orig_sent
        , std::size_t const pos
    ) const;

    void Score(attributes_t const & attrs
        , context_range_t const & context
        , std::size_t const pos
        , candidates_t & candidates
    ) const;

    double ScoreCandidate (attributes_t const & attrs
        , double sc
        , cand_kind_t const ck
    ) const;

    TLangModel                      LangModel;
    std::unique_ptr<TBloomFilter>   Deletes1;
    std::unique_ptr<TBloomFilter>   Deletes2;

    opt_t const                     m_opt;

};


} // NJamSpell
