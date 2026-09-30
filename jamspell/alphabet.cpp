#include "alphabet.hpp"

#include <fstream>
#include <sstream>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <string_view>


namespace NJamSpell 
{

bool TAlphabet::isIgnorable (letter_type const chr) 
{
    return chr == 0 || chr == 10 || chr == 13 || chr == UniversalCh 
    || chr == L' ';
}

void TAlphabet::Reserve() 
{
    m_letters.reserve(max_size);
    m_subst.reserve(max_size);
    m_switches.reserve(max_size);
    m_all.reserve(max_size);
}

void TAlphabet::Clear() 
{
    m_letters.clear();
    m_switches.clear();
    m_subst.clear();
}


void TAlphabet::AddLetter(letter_type const ch, pos_t & idx)
{
    idx = static_cast<pos_t> (m_subst.size());
    m_subst.emplace_back(ch);
    m_letters.emplace_back(ch, idx);
}

bool TAlphabet::LoadFromFile (std::string const & fPath)
{
    std::ifstream in(fPath, std::ios::binary);
    if(!in)
    {
        return false;
    }

    Reserve();
    
    strings_type    lines;

    m_subst.emplace_back(char(0));             // zero char
    m_letters.emplace_back( char(0), pos_t(0));

    //m_switches.emplace_back(0, 0); // no need to do this;
    while (!in.eof())
    {  
        std::string l;        
        std::getline(in, l);
        RTrim(l); 

        if(l.empty())
        {
            continue;
        }

        std::wstring lcnverted (u8_to_w(l));
        wchar_t const chr = lcnverted[0];
        if (isIgnorable(chr))
        { 
            continue;
        }

        lines.emplace_back(std::move(lcnverted));

        pos_t idx;
        AddLetter(chr, idx);

        if (idx >= pos_t::Any) 
            throw std::runtime_error("alphabet size greater than 254 is not supported!\n"); 

        wchar_t const upChr = MakeUpper(chr);
        if(chr != upChr)
        {
            m_letters.emplace_back(upChr, idx);
            // No need to load Punto for upper chars!
        }
    }
    if (m_letters.empty())
    {
        return false;
    }

    m_all.reserve(lines.size() * lines.size()) ;
    LoadLines(lines);
    return true;

}


TAlphabet::letter_type TAlphabet::GetSwitched(char const ch) const
{
    auto i = std::lower_bound(m_switches.begin(), m_switches.end(), switch_t{ch, 0});
    return ( i != m_switches.end() &&  i -> switched == ch) ? i -> real : ch;
}

void TAlphabet::LoadLines(strings_type const & lines )
{
    std::sort(m_letters.begin(), m_letters.end()); 
    // we will use it to convert chars! See GetPos method!

    auto subst_it = m_subst.begin();
    ++subst_it; // the first is empty - for Undefined zero-char!
    for(auto lit = lines.begin(), e = lines.end(); lit != e; ++lit, ++subst_it)
    {
        if(lit -> size() < 3)
        {
            continue;   
        }

        wstr_view_t const lcontent = *lit;

        std::size_t punto_end_pos;
        if(lcontent[1] != SepCh
            || ((punto_end_pos = lcontent.find(SepCh, 2u)) == std::wstring::npos)
        )
        {
            throw std::runtime_error("bad alpahbet format: ill-formed line \'" 
                + w_to_u8(*lit) + '\''
            );
        }

        LoadPunto(lcontent[0], lcontent.substr(2u, punto_end_pos - 2u ));
        LoadSubst(*subst_it, lcontent.substr(punto_end_pos + 1u));
    }

    std::sort(m_switches.begin(), m_switches.end());
    FinalizeSubst(m_all);
    m_all.shrink_to_fit();
}

TAlphabet::pos_t TAlphabet::GetPos ( letter_type const ch) const
{
    auto const it = std::lower_bound(m_letters.begin(), m_letters.end()
        , letter_info_t {ch} 
    );

    return (it != m_letters.end()) && (it -> m_letter == ch)  ? 
            it -> m_pos
        :   pos_t::Undefined;
}

void TAlphabet::LoadPunto(letter_type const chr
    , std::wstring_view const & switched_letters
)
{
    char const c = Wch2Ch(chr);
    for (letter_type const l : switched_letters)
    {
        char const sc = Wch2Ch(l);
        if(c)
        {
            m_switches.emplace_back(sc, c);
        }
    }
}

void TAlphabet::LoadSubst (subs_info_t & subs_info, std::wstring_view lttrs)
{
    if(lttrs[lttrs.size() - 2u] == SepCVCh)
    {
        subs_info.kindCV = GetCVKind(lttrs.back());
        lttrs.remove_suffix(2u);
    }

    LoadSubstLetters(subs_info.subs, lttrs);
}

void TAlphabet::LoadSubstLetters(subs_type & subs, std::wstring_view const & lttrs)
{
    subs.reserve(lttrs.size());
    for (letter_type wc : lttrs)
    {
        char const c = Wch2Ch(wc);
        if(c)
        {
            subs.push_back(c);
        }
    }
    FinalizeSubst(subs);
    m_all.insert(m_all.end(), subs.begin(), subs.end());
}

void TAlphabet::FinalizeSubst(subs_type & subs)
{
    std::sort(subs.begin(), subs.end());
    subs.resize(std::distance(subs.begin(), std::unique(subs.begin(), subs.end())));
}

/*
bool WellFormedInAlphabet(str_view_t & src)
{
    for(char c : src)
    {
        if(!c)
            return false;
    }
    return true;
}

*/

bool ToAlphabet(TAlphabet const & alphabet
    , wstr_view_t const & src
    , str_t & res
)
{
    res.resize(src.size(), '\0');
    auto tgt = res.begin();
    for(wchar_t const src_ch: src)
    {
        if(TAlphabet::UniversalCh == (*tgt++ = alphabet.Wch2Ch(src_ch)))
        {
            res.clear();
            return false;
        }
    }
    return true;
}

bool ToAlphabet(TAlphabet const & alphabet
    , wstr_view_t const & src
    , str_t & res
    , token_stat_t & ts
)
{
    auto const & curr_loc = GetLocale();

    ts.is_title_case = true;

    std::uint8_t vow_in_row = 0u, cons_in_row = 0;
    res.resize(src.size(), '\0');
    auto tgt = res.begin();
    for(auto it = src.begin(), e = src.end(); it != e; ++tgt, ++it)
    {
        wchar_t const src_ch = *it;
        *tgt = alphabet.Wch2Ch(src_ch);
        if(*tgt == TAlphabet::UniversalCh)
        {
            res.clear();
            return false;
        }
        kindCV_t const cvk = alphabet.GetLetterKind(*tgt);
        bool const  is_vow = (cvk == kindCV_t::cvkVowel),
                    is_cons = (cvk == kindCV_t::cvkConsonant)
        ;

        if(!is_vow)
        {
            ts.max_vovel_in_row = std::max(ts.max_vovel_in_row, vow_in_row);
            vow_in_row = 0u;
        }
        if(!is_cons)
        {
            ts.max_consonant_in_row = std::max(ts.max_consonant_in_row, cons_in_row);
            cons_in_row = 0u;
        }

        vow_in_row += is_vow;
        cons_in_row += is_cons;

        ts.vowel_cnt += is_vow;
        ts.consonant_cnt += is_cons;

        if(!std::isdigit(src_ch, curr_loc))
        {
            ts.is_title_case &= std::isupper(src_ch, curr_loc);
        }

    }
    ts.max_consonant_in_row = std::max(ts.max_consonant_in_row, cons_in_row);
    ts.max_vovel_in_row = std::max(ts.max_vovel_in_row, vow_in_row);   
    return true; 
}

str_t ToAlphabet(TAlphabet const & alphabet
    , wstr_view_t const & src
    , token_stat_t & ts
)
{
    str_t s;
    ToAlphabet(alphabet, src, s, ts);
    return s;
}

std::wstring FromAlphabet(TAlphabet const & alphabet, str_view_t const & src) 
{
    std::wstring s(src.size(), static_cast<wchar_t> (0) );
    auto tgt = s.begin();
    for (char const c : src)
    {
        *tgt++ = alphabet.Ch2Wch(c);
    }
    return s;
}

str_t FribbulusXax(TAlphabet const & alphabet, str_view_t const & src)
{
    str_t s(src);
    for (auto & c : s)
    {
        c = alphabet.GetSwitched(c);
    }
    return s;
}

} // NJamSpell
