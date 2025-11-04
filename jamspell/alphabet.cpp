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

void TAlphabet::Reserve() 
{
    m_letters.reserve(max_size);
    m_subst.reserve(max_size);
    m_switches.reserve(max_size);
}

void TAlphabet::Clear() 
{
    m_letters.clear();
    m_subst.clear();
}

bool TAlphabet::LoadFromFile (std::string const & fPath)
{
    std::ifstream in(fPath, std::ios::binary);
    if(!in)
    {
        return false;
    }

    Reserve();
    
    std::size_t lcnt{0};    
    utf8_to_wide_t utf8_to_wide{};
    while (!in.eof())
    {  
        std::string l;        
        std::getline(in, l); 
        ++lcnt;

        if(l.empty())
            continue;

        std::wstring lcnverted (utf8_to_wide(l));
        ToLower(lcnverted);

        wchar_t const chr = lcnverted[0];
        if (chr == 0 || chr == 10 || chr == 13) 
            continue;
          
        std::wstring_view const letter_content (lcnverted);

        pos_t const idx {static_cast<pos_t> (m_subst.size())};
        m_subst.emplace_back();
        auto & sbst = m_subst.back();
        if (idx >= pos_t::Undefined) 
            throw std::runtime_error("alphabet size greater than 254 is not supported!\n"); 
        
        m_letters.emplace_back(chr, idx);
        wchar_t const upChr = MakeUpper(chr);
        if(chr != upChr)
        {
            m_upper.emplace_back(upChr, idx);
            // No need to load Punto for upper chars!
        }

        if(letter_content.size() < 3)
            continue;

        std::size_t punto_end_pos;
        if(letter_content[1] != L'#' 
            || ((punto_end_pos = letter_content.find(L'#', 2)) == std::wstring::npos)
        )
            throw std::runtime_error("bad alpahbet format: line #" + std::to_string(lcnt));
        
        LoadPunto(chr, letter_content.substr(2u, punto_end_pos ));
        LoadSubst(sbst, letter_content.substr(punto_end_pos + 1u));
    }
    if (m_letters.empty())
    {
        return false;
    }
   
    std::sort(m_letters.begin(), m_letters.end());
    std::sort(m_upper.begin(), m_upper.end());
    std::sort(m_switches.begin(), m_switches.end());
    return true;
}


TAlphabet::letters_type const & TAlphabet::GetSubstitutes (wchar_t const ch) const
{
    static const letters_type empty_sbst;
    auto const it = std::lower_bound(m_letters.begin(), m_letters.end(), letter_info_t {ch} );
    return (it != m_letters.end()) && (it -> m_letter == ch)  ? 
            m_subst[ raw_pos( it -> m_pos) ]
        : empty_sbst ;
}

TAlphabet::letter_type TAlphabet::GetSwitched(letter_type const wch) const
{
    auto i = std::lower_bound(m_switches.begin(), m_switches.end(), switch_t{wch, 0});
    return ( i != m_switches.end() &&  i -> switched == wch) ? i -> real : wch;
}

TAlphabet::pos_t TAlphabet::GetPos (impl_type const & lttrs, wchar_t const ch)
{
    auto const it = std::lower_bound(lttrs.begin(), lttrs.end(), letter_info_t {ch} );
    return (it != lttrs.end()) && (it -> m_letter == ch)  ? 
            it -> m_pos
        :   pos_t::Undefined;
}

void TAlphabet::LoadPunto(letter_type const chr, std::wstring_view const & switched_letters)
{
    for (letter_type const l : switched_letters)
    {
        m_switches.emplace_back(l, chr);
    }
}

void TAlphabet::LoadSubst (letters_type & sbst, std::wstring_view const & lttrs)
{
    sbst.assign(lttrs.begin(), lttrs.end());
}


std::string ToAlphabet(TAlphabet const & alphabet, std::wstring_view const & src)
{
    std::string s(src.size(), '\0');
    auto tgt = s.begin();
    for(auto it = src.begin(), e = src.end()
        ; it != e
        ; *(tgt++) = alphabet.Wch2Ch(*it++)
    ){}
    return s;
}

std::wstring FromAlphabet(TAlphabet const & alphabet, std::string_view const & src) 
{
    std::wstring s(src.size(), static_cast<wchar_t> (0) );
    auto tgt = s.begin();
    for (char const c : src)
    {
        *tgt++ = alphabet.Ch2Wch(c);
    }
    return s;
}

std::wstring FribbulusXax(TAlphabet const & alphabet, std::wstring_view const & src)
{
    std::wstring s(src);
    for (wchar_t & c : s)
    {
        c = alphabet.GetSwitched(c);
    }
    return s;
}

} // NJamSpell
