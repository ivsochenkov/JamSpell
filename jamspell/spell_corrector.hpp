#pragma once

#include <memory>

#include "lang_model.hpp"
#include "bloom_filter.hpp"

namespace NJamSpell 
{


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
    
    TScoredWords GetCandidatesRawWithScores(const TWords& sentence
        , size_t const position
    ) const;

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
    
    void AppendWithCase(std::wstring & result
        , std::wstring_view const & origWord
        , std::wstring_view const &newWord
    ) const;

    void FilterCandidatesByFrequency(
            std::unordered_set<TWord, TWordHashPtr>& uniqueCandidates
            , TWord origWord
    ) const;

    std::wstring PuntoSwitcher(TWord const &w) const;

    NJamSpell::TWords FormEditsCandidates(const NJamSpell::TWord& word
        , bool & firstLevel
    ) const;

    NJamSpell::TWords Edits(const NJamSpell::TWord& word) const;
    NJamSpell::TWords Edits2(const NJamSpell::TWord& word, bool lastLevel = true) const;
    void Inserts(const std::wstring& w, NJamSpell::TWords& result) const;
    void Inserts2(const std::wstring& w, NJamSpell::TWords& result) const;
    void PrepareCache();
    bool LoadCache(const std::string& cacheFile);
    bool SaveCache(const std::string& cacheFile);
private:

    TLangModel LangModel;
    std::unique_ptr<TBloomFilter> Deletes1;
    std::unique_ptr<TBloomFilter> Deletes2;
    double KnownWordsPenalty = 20.0;
    double UnknownWordsPenalty = 5.0;
    size_t MaxCandidatesToCheck = 14;

};


} // NJamSpell
