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
        m_impl.reserve(maxCandCnt);
    }

    void set_kind (cand_kind_t const ck) { m_cand_kind = ck;}

    /*
    cand_word_t & push (wdata_t const & wd, str_t && s, cand_kind_t const k)
    {return m_impl.emplace_back(wd, std::move(s), k);}
    */

    template <typename TStr>
    bool insert (wdata_t const & wd, TStr && s, concat_inf_t const & drop)
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

public:

    struct opt_t
    {
        double      OrigWordIsKnownPenalty      = 18.0   // 20   
                ,   OrigWordIsUnknownPenalty    = 5.0  // 5    
                ,   SecondLvlPenFactor          = 20.0       // 50
                ,   SecondLvlPenalty            = 10.0       
                ,   SwitchedWordPenalty         = 3.0
                ,   SwitchedWordIsKnownPenalty  = 15.0
                ,   BadTokenPenalty             = 25.0
                ,   ShortFragmentPenalty        = 60.0

//                ,   LowProbPenalty              = 20.0

                ; 
            ;

        ::std::size_t         MaxCandidatesToCheck = 64
                            , InFreqWordCntThreshold = 300 // experimental!
        ;

        static opt_t ReadFromEnv();
    };

    using scored_candidates_t = std::vector<candidates_t>;

    explicit TSpellCorrector (opt_t const & opt = opt_t::ReadFromEnv());

    bool LoadLangModel(const std::string& modelFile);
    
    bool TrainLangModel(const std::string& textFile
        , const std::string & dictFile
        , const std::string& alphabetFile
        , const std::string& modelFile
        , TLangModel::train_options_t const & tr_opt 
            = TLangModel::train_options_t::ReadFromEnv()
    );

#ifdef SPLL_DEEPFIX_EXPERIMENTAL
    void DeepFix(context_t & cntxt) const;
    context_t DeepFix(std::wstring const & text) const ;
#endif // #ifdef SPLL_DEEPFIX_EXPERIMENTAL

    void Fix(context_t & cntxt) const;
    context_t Fix(std::wstring const & text) const;

    std::wstring FixFragment(const std::wstring& text) const;
    
    NJamSpell::TLangModel const & GetLangModel() const {return LangModel;}

    std::string ToU8 (str_view_t const & s) const
    {
        return w_to_u8(ToU32(s));
    }
    std::wstring ToU32 (str_view_t const & s) const
    {
        return FromAlphabet(GetAlphabet(), s);
    }

private:

    using permutation_t = std::vector<std::uint8_t>;

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

    static bool IsShortFragment(cntxt_word_t const &cw ) noexcept
    {
        return cw.concat.right > 1u;        
    }

    TAlphabet const & GetAlphabet() const noexcept
    { return GetLangModel().GetTokenizer().GetAlphabet(); }

    context_range_t GetNextSent(context_t::iterator const & b
        , context_t::iterator const & e
    ) const;

    static void TrimCandidates(cntxt_word_t & curr_word
        , std::size_t const cntxt_sz
    );

    static void AddOrig2Candidates(cntxt_word_t & curr_word);

    void FormCandidates(context_range_t const & context
        , ::std::size_t const position
    ) const;

    ::std::size_t CheckExtendedTokenStr(context_range_t const & cntxt
        , ::std::size_t const pos
        , TCandMgr & cmgr
    ) const;

    void ProcessCandidates(context_range_t const & context
        , ::std::size_t const position
    ) const
    { 
        FormCandidates(context, position);
        Score(context, position);
    }

    ::std::size_t CntNonSpacedNeighbours(context_range_t const & context
        , ::std::size_t const position
        , concat_inf_t & i
    ) const;

    bool PrevWordWasSwitched(context_range_t const & context
        , ::std::size_t const position
    ) const;

    std::size_t  ManageDroppedTokens(context_t::iterator & al_word_it) const;

    ::std::size_t CheckSwitchedCands (context_range_t const & context
        , ::std::size_t const position
        , TCandMgr & cmgr
    ) const;

    ::std::size_t FormGreedySwitchedCandsRight (
        context_range_t const & context
        , ::std::size_t const position
        , str_view_t const & sw_cand_str
        , TCandMgr & cmgr       
    ) const;

    void MakeLeftCandsStrPrefix(context_range_t const & context
        , ::std::size_t const position
        , str_t & s
    ) const;

    void MakeRightCandsStr(context_range_t const & context
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
        , concat_inf_t const & di = concat_inf_t{}
    ) const;

    std::size_t Edits2(str_view_t const & word
        , TCandMgr & candidates
        , concat_inf_t const & di = concat_inf_t{}
    ) const;

    void InsertsImpl(str_view_t const& w
        , std::size_t const i
        , TCandMgr& result
        , str_t & buf
        , concat_inf_t const & di
    ) const;
    void Inserts(str_view_t const & w
        , TCandMgr& result
        , str_t & s
        , concat_inf_t const & di
    ) const;

    void Inserts2Impl(str_view_t const & w
        , std::size_t const i
        , TCandMgr& result
        , str_t & s
        , str_t & buf
        , concat_inf_t const & di
    ) const;
    void Inserts2(str_view_t const & w
        , TCandMgr& result
        , concat_inf_t const & di
    ) const;


    void PrepareCache();
    bool LoadCache(const std::string& cacheFile);
    bool SaveCache(const std::string& cacheFile);

    bool Push2Candidates (str_t && w
        , wdata_t const & wd
        , TCandMgr & cmgr
        , concat_inf_t const & di //= concat_inf_t{}
    ) const;

    bool Append2Candidates (str_view_t const & w
        , wdata_t const & wd
        , TCandMgr & cmgr
        , concat_inf_t const & di //= concat_inf_t{}
    ) const;

    bool LookupAndAppend2Candidates(str_view_t const & w
        , TCandMgr & cmgr
        , concat_inf_t const & di //= concat_inf_t{}
    ) const
    {return Append2Candidates(w, LangModel.GetWordInfo(w), cmgr, di); }

    context_crange_t GetSentenceRange( context_crange_t const & sentence
        , std::size_t const pos
    ) const;

#ifdef SPLL_DEEPFIX_EXPERIMENTAL
    static void ApplyP(context_range_t const & context, permutation_t const & p);

    float PScore(context_range_t const & context, permutation_t const & p) const;
#endif // #ifdef SPLL_DEEPFIX_EXPERIMENTAL

    void Score(context_range_t const & context, std::size_t const pos) const;

#ifdef SPLL_DEEPFIX_EXPERIMENTAL
    void DeepScore(context_range_t const & context) const;
#endif // #ifdef SPLL_DEEPFIX_EXPERIMENTAL

    void ScoreCandidate (cntxt_word_t const & ctx_word, cand_word_t & cnd) const;

    float ScoreOrig(context_range_t const & cntxt
        , ::std::size_t const pos
    ) const;

    float ReScore (word_t const & w, float sc, token_stat_t const ts) const
    {
        /*
        float const gp = LangModel.CalcGram1Prob(w)
            , ep = LangModel.ExpectedProb(w.str.size());

        sc -= (gp < (ep / 2.0) ) ? m_opt.LowProbPenalty : 0.0;
        */
        sc -= LangModel.TokenIsBad(w.str, ts)  ?  m_opt.BadTokenPenalty : 0.0;
        return sc;
    }

    TLangModel                      LangModel;
    std::unique_ptr<TBloomFilter>   Deletes1;
    std::unique_ptr<TBloomFilter>   Deletes2;

    opt_t const                     m_opt;

};


} // NJamSpell
