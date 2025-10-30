#pragma once

#include "utils.hpp"

#include <boost/iterator/transform_iterator.hpp>

#include <contrib/handypack/handypack.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <locale>
#include <limits>

namespace NJamSpell 
{

class TAlphabet
{    
public:

    using letters_type = std::vector<wchar_t>;
    using substitutes_type = std::vector<letters_type>;

    enum class pos_t : unsigned char 
    { 
        Undefined = std::numeric_limits<std::underlying_type<pos_t>::type>::max() 
    };

    inline std::underlying_type<pos_t>::type raw_pos (pos_t const p) const
    { return static_cast<std::underlying_type<pos_t>::type> (p);}

private:

    struct letter_info_t
    {
        letter_info_t () = default;

        explicit letter_info_t (wchar_t const l, pos_t const p = pos_t::Undefined)
        : m_letter{l}, m_pos{p} 
        {} 

        bool operator < (TAlphabet::letter_info_t const & rhs) const
        { return m_letter < rhs.m_letter; }

        wchar_t             m_letter = 0;        
        pos_t               m_pos = pos_t::Undefined;

        HANDYPACK(m_letter, m_pos);
    };

public:

    HANDYPACK(m_letters, m_substitites)

    void Clear() ;

    bool LoadFromFile (std::string const & fPath);

    char Wch2Ch (wchar_t const ch) const 
    {return static_cast<char>(GetPos(ch)) + 1 ; }

    wchar_t Ch2Wch (char const ch) const 
    {return m_letters[raw_pos(pos_t(ch - 1))].m_letter; }

    letters_type const & GetSubstitutes (wchar_t const ch) const ;

    bool Contains(wchar_t const ch) const {return GetPos(ch) != pos_t::Undefined;}

private:

    void LoadSubst (letters_type & sbst, std::wstring_view lttrs);

    pos_t GetPos (wchar_t const ch) const;

    using impl_type = std::vector<letter_info_t>;

    struct trnsfrm_t
    {
        wchar_t operator () (letter_info_t const & i) const
        { return i.m_letter;}
    };

public: 
    using const_iterator = boost::transform_iterator <trnsfrm_t
        , impl_type::const_iterator
        , wchar_t, wchar_t
        >;

    using iterator = const_iterator;

    const_iterator begin() const {return const_iterator(m_letters.begin(), trnsfrm_t{});}
    const_iterator end() const {return const_iterator(m_letters.end(), trnsfrm_t{});}

private:

    impl_type           m_letters;
    substitutes_type    m_substitites;

};

std::string ToAlphabet(TAlphabet const & alphabet, std::wstring_view const & src) ;
std::wstring FromAlphabet(TAlphabet const & alphabet, std::string_view const & src) ;


} // NJamSpell
