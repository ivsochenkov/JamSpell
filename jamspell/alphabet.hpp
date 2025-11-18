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

    static constexpr letter_type const UniversalCh = 0, SepCh = L'#';

    using subs_type = std::vector<char>;

    enum class pos_t : unsigned char 
    { 
        Undefined = 0
        , Any = std::numeric_limits<std::underlying_type<pos_t>::type>::max() 
    };

    inline std::underlying_type<pos_t>::type raw_pos (pos_t const p) const
    { return static_cast<std::underlying_type<pos_t>::type> (p);}

    static bool isIgnorable (letter_type const chr) ;

private:

    struct switch_t
    {
        char switched, real;

        explicit switch_t(char const s = 0, char const r = 0)
        : switched{s}, real{r}
        {}

        HANDYPACK(switched, real);

        bool operator < (switch_t const & rhs) const
        { return switched < rhs.switched; }

        bool operator == (switch_t const & rhs) const
        { return switched == rhs.switched; }
    };

    struct subs_info_t
    {
        subs_type       subs;
        letter_type     letter;

        explicit subs_info_t (letter_type const wc = UniversalCh)
        : subs{}, letter(wc) 
        {}

        HANDYPACK(subs, letter)

    };

    using subs_map_type = std::vector<subs_info_t>;

    using strings_type = std::list<std::wstring>;

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

    HANDYPACK(m_letters, m_switches, m_subst, m_all)

    void Clear() ;

    void AddLetter(letter_type const ch, pos_t & idx);

    bool LoadFromFile (std::string const & fPath);

    char Wch2Ch (letter_type const ch) const 
    {   return static_cast<char>(GetPos(ch));  }

    letter_type Ch2Wch (char const ch) const 
    {return m_subst[std::size_t(ch)].letter; }

    subs_type const & GetSubstitutes (char const ch) const 
    { return m_subst[std::size_t(ch)].subs;}

    bool Contains(letter_type const ch) const 
    {return GetPos(ch) != pos_t::Undefined; }

    letter_type GetSwitched(char const ch) const;

private:

    using impl_type = std::vector<letter_info_t>;
    using punto_switch_type = std::vector<switch_t>;

    struct trnsfrm_t
    {
        wchar_t operator () (letter_info_t const & i) const
        { return i.m_letter;}
    };

    void LoadLines(strings_type const & lines );

    pos_t GetPos (letter_type const ch) const;

    void LoadPunto (letter_type const chr, std::wstring_view const & switched_letters);
    void LoadSubst (subs_type & sbst, std::wstring_view const & lttrs);

public: 

    using const_iterator = subs_type::const_iterator;
    using iterator = const_iterator;

    const_iterator begin() const {return m_all.begin();}
    const_iterator end() const {return m_all.end();}

private:

    impl_type           m_letters;
    punto_switch_type   m_switches;
    subs_map_type       m_subst;
    subs_type           m_all;

};

bool WellFormedInAlphabet(std::string_view const & src);

void ToAlphabet(TAlphabet const & alphabet
    , std::wstring_view const & src
    , str_t & res
);
str_t ToAlphabet(TAlphabet const & alphabet, wstr_view_t const & src);

std::wstring FromAlphabet(TAlphabet const & alphabet, str_view_t const & src) ;

// aka Punto Switcher
str_t FribbulusXax(TAlphabet const & alphabet, str_view_t const & src);


} // NJamSpell
