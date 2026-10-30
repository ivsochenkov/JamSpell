#pragma once

#include "lang_model.hpp"
#include "bloom_filter.hpp"

#include <contrib/tsl/robin_set.h>

#include <boost/range/iterator_range.hpp>

namespace NJamSpell 
{

class TCandMgr 
{
    using impl_type = candidates_t;

    struct wheap_order_t
    {
        bool is_greater (cand_word_t const & lhs
            , wdata_t const & rhs
            , cand_kind_t const ck
        ) const noexcept
        {   
            // max-heap!
            return lhs.cnt != rhs.cnt ? lhs.cnt > rhs.cnt : lhs.kind < ck ; 
        }

        bool operator () (cand_word_t const & lhs
            , cand_word_t const & rhs
        ) const noexcept
        {   
            // max-heap!
            return is_greater (lhs, rhs, rhs.kind); 
        }
    }; 

    /*
    using word_set_t = tsl::robin_set<word_id_t
        , ::std::hash<word_id_t>
        , ::std::equal_to<word_id_t>
        , ::std::allocator<word_id_t>
        , false // do not store hash
        , tsl::rh::prime_growth_policy
        >;
    */

public:

    using result_type = impl_type;

    explicit TCandMgr (candidates_t & cands, std::size_t const maxCandCnt = 64u);

    template <typename TStr>
    bool insert (cand_kind_t const ck
        , wdata_t const & wd
        , TStr && s
        , concat_inf_t const & ci
    )
    {
        static wheap_order_t const cmp;
/*
        if(!m_word_set.insert(wd.id).second)
        {
            return false;
        }
*/
        if (m_best_cnt < wd.cnt )
        {
            m_best_cnt = wd.cnt;
        }

        if((m_impl.size() >= m_max_sz) )
        {
            cand_word_t & hback = m_impl.back();
            if(cmp.is_greater(hback, wd, ck))
            {
                return false;
            }
    
            hback = cand_word_t{wd.id, std::forward<TStr>(s), wd.cnt, ck, ci};
            reset_heap_impl();
            return true;
        }        
        m_impl.emplace_back(wd.id, std::forward<TStr>(s), wd.cnt, ck, ci);
        reset_heap_impl();
        return true;
    }

    bool empty() const noexcept {return m_impl.empty();}
    cnt_t best_cnt() const noexcept {return m_best_cnt;}


private:

    void reset_heap_impl ();

    impl_type        &  m_impl;
 //   word_set_t          m_word_set;
    std::size_t const   m_max_sz;
    cnt_t               m_best_cnt  = 0;

};

////////////////////////////////////////////////////////////////////////////////

class TSpellCorrector 
{

public:

    struct opt_t
    {
        double      OrigWordIsKnownPenalty      = 18.0      
                ,   OrigWordIsUnknownPenalty    = 5.0      
                ,   SecondLvlPenFactor          = 20.0       
                ,   SecondLvlPenalty            = 30.0       
                ,   SwitchedWordPenalty         = 3.0
                ,   SwitchedWordIsKnownPenalty  = 15.0
                ,   BadTokenPenalty             = 50.0
                ,   LengthMismatchPenaltyFactor = 2.0   // experimental

//                ,   LowProbPenalty              = 20.0

                ; 
            ;

        ::std::size_t         MaxCandidatesToCheck = 64u
                            , InFreqWordCntThreshold = 200u
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

    struct fix_attrs_t
    {
        ::std::uint32_t     sw_cnt = 0;
    };

    TTokenizer const & GetTokenizer() const noexcept 
    {return GetLangModel().GetTokenizer();}

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
    { return GetTokenizer().GetAlphabet(); }

    concat_inf_t PeelWord(token_info_t & tinf, ::std::uint8_t & has_int_pnkt) const;

    void InitContextWordFromToken(cntxt_word_t & w) const;

    void Text2Context(wstr_view_t const & txt, context_t & cntxt) const;

    context_t::iterator FixSentence(context_range_t const & curr_sent_ctxt ) const;

    static sent_end_kind_t endsWithSentEnd(wstr_view_t const & tok_str
        , ::std::size_t cright
        , ::std::size_t & closing_pnct_cnt
    );

    bool StartsWithTitleCase(context_t::iterator nxt
        , context_t::iterator const & e
    ) const noexcept;

    bool isSentEnd(context_t::iterator const & b
        , context_t::iterator const & e
    ) const noexcept;

    context_range_t GetNextSent(context_t::iterator const & b
        , context_t::iterator const & e
    ) const;

    static void TrimCandidates(cntxt_word_t & curr_word
        , std::size_t const cntxt_sz
    );

    static void AddOrig2Candidates(cntxt_word_t & curr_word);

    bool FormCandidates(context_range_t const & context
        , ::std::size_t const position
        , fix_attrs_t & fix_attrs
    ) const;

    bool ProcessCandidates(context_range_t const & cntxt
        , ::std::size_t const pos
        , fix_attrs_t & fix_attrs
    ) const;

    ::std::size_t CheckSwitchedCands (cntxt_word_t & cw 
        , TCandMgr & cmgr
    ) const;

    ::std::size_t CheckSwitchedCandsRight (cntxt_word_t & cw 
        , TCandMgr & cmgr
        , ::std::size_t const l
    ) const;

    void AppendWithCase(std::wstring & result
        , wstr_view_t const & origWord
        , str_view_t const & newWord
    ) const;

    str_t PuntoSwitcher(str_view_t const &w) const;

    void Edits(cand_kind_t const ck
        , str_view_t const & word
        , TCandMgr & candidates
        , concat_inf_t const & di
    ) const;

    std::size_t Edits2(cand_kind_t const ck
        , str_view_t const & word
        , TCandMgr & candidates
        , concat_inf_t const & di
    ) const;


    void InsertsImpl(cand_kind_t const ck
        , str_view_t const& w
        , std::size_t const i
        , TCandMgr& result
        , str_t & buf
        , concat_inf_t const & di
    ) const;

    void Inserts(cand_kind_t const ck
        , str_view_t const & w
        , TCandMgr& result
        , str_t & s
        , concat_inf_t const & di
    ) const;


    void Inserts2Impl(cand_kind_t const ck
        , str_view_t const & w
        , std::size_t const i
        , TCandMgr& result
        , str_t & s
        , str_t & buf
        , concat_inf_t const & di
    ) const;

    void Inserts2(cand_kind_t const ck
        , str_view_t const & w
        , TCandMgr& result
        , concat_inf_t const & di
    ) const;


    void PrepareCache();
    bool LoadCache(const std::string& cacheFile);
    bool SaveCache(const std::string& cacheFile);

    bool Push2Candidates (cand_kind_t const ck
        , str_t && w
        , wdata_t const & wd
        , TCandMgr & cmgr
        , concat_inf_t const & di 
    ) const;

    bool Append2Candidates (cand_kind_t const ck
        , str_view_t const & w
        , wdata_t const & wd
        , TCandMgr & cmgr
        , concat_inf_t const & di 
    ) const;

    bool LookupAndAppend2Candidates(cand_kind_t const ck
        , str_view_t const & w
        , TCandMgr & cmgr
        , concat_inf_t const & di 
    ) const
    {return Append2Candidates(ck, w, LangModel.GetWordInfo(w), cmgr, di); }

    context_crange_t GetSentenceRange( context_crange_t const & sentence
        , std::size_t const pos
    ) const;

    void Score(fix_attrs_t const & fixa
        , context_range_t const & context
        , std::size_t const pos
    ) const;


    void ScoreCandidate (fix_attrs_t const & fixa
        , cntxt_word_t const & ctx_word
        , cand_word_t & cnd
    ) const;

    void ScoreOrig(fix_attrs_t const & fixa
        , context_range_t const & cntxt
        , ::std::size_t const pos
    ) const;

    void ReScore (cand_word_t & w, token_stat_t const ts) const noexcept
    {
        w.score -= m_opt.BadTokenPenalty * (
            (!ts.title_case_cnt) &&
                (w.concat.left > 1u || w.concat.right > 2u || ts.int_punct_cnt)
            || LangModel.TokenIsBad(w.str, ts) 
            
        );
    }

    TLangModel                      LangModel;
    std::unique_ptr<TBloomFilter>   Deletes1;
    std::unique_ptr<TBloomFilter>   Deletes2;

    opt_t const                     m_opt;

};


} // NJamSpell
