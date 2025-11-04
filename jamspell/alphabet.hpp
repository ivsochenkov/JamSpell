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

    static constexpr std::size_t    max_size = 254;

    using letter_type = wchar_t;

    using letters_type = std::vector<letter_type>;
    using substitutes_type = std::vector<letters_type>;

    struct switch_t
    {
        letter_type switched, real;

        explicit switch_t(letter_type const s = 0, letter_type const r = 0)
        : switched{s}, real{r}
        {}

        HANDYPACK(switched, real);

        bool operator < (switch_t const & rhs) const
        { return switched < rhs.switched; }

        bool operator == (switch_t const & rhs) const
        { return switched == rhs.switched; }
    };

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

        explicit letter_info_t (letter_type const l, pos_t const p = pos_t::Undefined)
        : m_letter{l}, m_pos{p} 
        {} 

        bool operator < (letter_info_t const & rhs) const
        { return m_letter < rhs.m_letter; }

        bool operator == (letter_info_t const & rhs) const
        { return m_letter == rhs.m_letter; }

        letter_type         m_letter = 0;        
        pos_t               m_pos = pos_t::Undefined;


        HANDYPACK(m_letter, m_pos);
    };

    void Reserve();

public:

    HANDYPACK(m_letters, m_upper, m_switches, m_subst)

    void Clear() ;

    bool LoadFromFile (std::string const & fPath);

    char Wch2Ch (letter_type const ch) const 
    {return static_cast<char>(GetPos(ch)) + 1 ; }

    letter_type Ch2Wch (char const ch) const 
    {return m_letters[raw_pos(pos_t(ch - 1))].m_letter; }

    letters_type const & GetSubstitutes (letter_type const ch) const ;

    bool Contains(letter_type const ch) const 
    {return GetPos(ch) != pos_t::Undefined; }

    letter_type GetSwitched(letter_type const wch) const;

private:

    using impl_type = std::vector<letter_info_t>;
    using punto_switch_type = std::vector<switch_t>;

    struct trnsfrm_t
    {
        wchar_t operator () (letter_info_t const & i) const
        { return i.m_letter;}
    };

    static pos_t GetPos (impl_type const & lttrs, wchar_t const ch);

    pos_t GetPos (letter_type const ch) const
    {
        pos_t const p = GetPos(m_letters, ch);
        return (p != pos_t::Undefined) ? p : GetPos(m_upper, ch);
    }

    void LoadPunto (letter_type const chr, std::wstring_view const & switched_letters);

    void LoadSubst (letters_type & sbst, std::wstring_view const & lttrs);

    

public: 
    using const_iterator = boost::transform_iterator <trnsfrm_t
        , impl_type::const_iterator
        , wchar_t, wchar_t
        >;

    using iterator = const_iterator;

    const_iterator begin() const {return const_iterator(m_letters.begin(), trnsfrm_t{});}
    const_iterator end() const {return const_iterator(m_letters.end(), trnsfrm_t{});}

private:

    impl_type           m_letters, m_upper;
    punto_switch_type   m_switches;
    substitutes_type    m_subst;

};

std::string ToAlphabet(TAlphabet const & alphabet, std::wstring_view const & src) ;
std::wstring FromAlphabet(TAlphabet const & alphabet, std::string_view const & src) ;

// aka Punto Switcher
std::wstring FribbulusXax(TAlphabet const & alphabet, std::wstring_view const & src);


} // NJamSpell
