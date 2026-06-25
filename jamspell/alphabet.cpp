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
    
    utf8_to_wide_t utf8_to_wide{};

    strings_type    lines;

    m_subst.emplace_back(char(0));             // zero char
    m_letters.emplace_back( char(0), pos_t(0));

    //m_switches.emplace_back(0, 0); // no need to do this;
    while (!in.eof())
    {  
        std::string l;        
        std::getline(in, l); 

        if(l.empty())
            continue;

        std::wstring lcnverted (utf8_to_wide(l));
        wchar_t const chr = lcnverted[0];
        if (isIgnorable(chr)) 
            continue;

        lines.push_back(std::move(lcnverted));

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
            || ((punto_end_pos = lcontent.find(SepCh, 2)) == std::wstring::npos)
        )
        {
            throw std::runtime_error("bad alpahbet format: ill-formed line \'" 
                + wide_to_utf8_t{}(*lit) + '\''
            );
        }

        LoadPunto(lcontent[0], lcontent.substr(2u, punto_end_pos - 2u ));
        LoadSubst(subst_it -> subs, lcontent.substr(punto_end_pos + 1u));
    }

    std::sort(m_switches.begin(), m_switches.end());
    std::sort(m_all.begin(), m_all.end());
    m_all.resize(std::distance(m_all.begin(), std::unique(m_all.begin(), m_all.end())));
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

void TAlphabet::LoadSubst (subs_type & sbst, std::wstring_view const & lttrs)
{
    sbst.reserve(lttrs.size());
    for (letter_type wc : lttrs)
    {
        char const c = Wch2Ch(wc);
        if(c)
        {
            sbst.push_back(c);
        }
    }
    std::sort(sbst.begin(), sbst.end());
    sbst.resize(std::distance(sbst.begin(), std::unique(sbst.begin(), sbst.end())));
    m_all.insert(m_all.end(), sbst.begin(), sbst.end());
}

bool WellFormedInAlphabet(std::string_view const & src)
{
    if (src.size() >= MAX_WORD_LENGTH)
        return false;
        
    for(char c : src)
    {
        if(!c)
            return false;
    }
    return true;
}

void ToAlphabet(TAlphabet const & alphabet
    , std::wstring_view const & src
    , str_t & res
)
{
    res.resize(src.size(), '\0');
    auto tgt = res.begin();
    for(auto it = src.begin(), e = src.end()
        ; it != e
        ; *(tgt++) = alphabet.Wch2Ch(*it++)
    ){}
}

str_t ToAlphabet(TAlphabet const & alphabet, std::wstring_view const & src)
{
    str_t s;
    ToAlphabet(alphabet, src, s);
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
