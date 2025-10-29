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


void TAlphabet::Clear() 
{
    m_letters.clear();
    m_substitites.clear();
}

bool TAlphabet::LoadFromFile (std::string const & fPath)
{
    std::ifstream in(fPath, std::ios::binary);
    if(!in)
    {
        return false;
    }
    
    std::size_t lcnt{0};    
    utf8_to_wide_t utf8_to_wide{};
    while (!in.eof())
    {  
        std::string l;        
        std::getline(in, l); 

        if(!l.empty())
        {   
            std::wstring letter_content (utf8_to_wide(l));
            ToLower(letter_content);

            wchar_t const chr = letter_content[0];
            if (chr == 0 || chr == 10 || chr == 13) 
            {
                continue;
            }

            auto & sbst = m_substitites.emplace_back();
            m_letters.emplace_back(chr, pos_t(m_substitites.size()));
            ++lcnt;  

            if (m_substitites.size() >= static_cast<std::size_t> (pos_t::Undefined)) 
            {
                throw std::runtime_error("alphabet size greater than 254 is not supported!\n"); 
            }

            if(letter_content.size() < 3)
                continue;

            std::size_t punto_end_pos;
            if(letter_content[1] != L'#' 
                || ((punto_end_pos = letter_content.find(L'#', 2)) == std::wstring::npos)
            )
            {
                throw std::runtime_error("bad alpahbet format: line #" + std::to_string(lcnt));
            }
            //LoadPunto(std::wstring_view(letter_content).substr(2, punto_end_pos ));

            LoadSubst(sbst, std::wstring_view(letter_content).substr(punto_end_pos + 1));
        }
        
    }
    if (m_letters.empty())
    {
        return false;
    }
   
    std::sort(m_letters.begin(), m_letters.end());
    return true;
}


TAlphabet::letters_type const & TAlphabet::GetSubstitutes (wchar_t const ch) const
{
    static const letters_type empty_sbst;
    auto const it = std::lower_bound(m_letters.begin(), m_letters.end(), letter_info_t {ch} );
    return (it != m_letters.end()) && (it -> m_letter == ch)  ? 
            m_substitites[ it -> raw_pos() ]
        : empty_sbst ;
}

TAlphabet::pos_t TAlphabet::GetPos (wchar_t const ch) const
{
    auto const it = std::lower_bound(m_letters.begin(), m_letters.end(), letter_info_t {ch} );
    return (it != m_letters.end()) && (it -> m_letter == ch)  ? 
            pos_t (std::distance(m_letters.begin(), it))
        :   pos_t::Undefined;
}

void TAlphabet::LoadSubst (letters_type & sbst, std::wstring_view lttrs)
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

} // NJamSpell
