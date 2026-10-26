#pragma once

#include "utils.hpp"
#include "alphabet.hpp"

#include <boost/tokenizer.hpp>

#include <string>
#include <vector>
#include <locale>


namespace NJamSpell 
{

class TTokenizer
{
    class token_t: public wstr_view_t
    {
    public:
        void assign(iterator const b, iterator const e) noexcept
        { 
            static_cast<wstr_view_t &> (*this) 
                = wstr_view_t(b, std::distance(b, e)); 
        }

        token_info_t make_info (wstr_view_t const & txt) const noexcept
        {
            return token_info_t{txt
                , static_cast<token_info_t::ofs_type>(std::distance(txt.begin(), this -> begin()))
                , static_cast<token_info_t::len_type>(this -> size())
            };
        }
    };

public:

    static constexpr wchar_t const * SPACES = L" \t\n\r\f\v";

    using sep_type = boost::char_separator<wchar_t>;

    using token_type = token_t;

    using tokenizer_type = boost::tokenizer<sep_type
        , token_type::const_iterator
        , token_type
    >;

    using token_iterator_type = tokenizer_type::iterator;

    static constexpr std::size_t max_word_length = 64;
/* 
   ХОЗЯЙКЕ НА ЗАМЕТКУ:
   Самым длинным словом в русском языке является химический термин 
   «тетрагидропиранилциклопентилтетрагидропиридопиридиновые» (55 букв)

   Самое длинное слово в английском языке из тех, что записаны в словарях, — это
   pneumonoultramicroscopicsilicovolcanoconiosis (45 букв), 
   а абсолютным рекордсменом является химическое название белка титина,
   состоящее из 189 819 букв.
*/

    

private:

    bool areSpaceDelimited(text_tokens_const_iterator_t const curr_tok_it
        , text_tokens_const_iterator_t const next
    ) const noexcept
    { return areSpaced(*curr_tok_it, *next);}  

    bool areNotSpaceDelimited(text_tokens_const_iterator_t const curr_tok_it
        , text_tokens_const_iterator_t const  next
    ) const noexcept
    { return areNotSpaced(*curr_tok_it, *next);}  

    

    inline bool good4join (text_tokens_const_iterator_t const & a
        , text_tokens_const_iterator_t const & b
        , text_tokens_const_iterator_t const & c
    ) const;

    inline bool good4join(text_tokens_const_iterator_t const& a
        , text_tokens_const_iterator_t const & b
    ) const;

    tokenizer_type Tokenize(wstr_view_t const & txt
        , sep_type const & sep = sep_type{}
    ) const;

public:

    using alphabet_type = TAlphabet;

    static constexpr std::size_t        avg_sent_len_bytes = 27
                                    ,   avg_sent_len_words = 10
                                    ,   avg_word_len = 4
                                    ;

    static constexpr std::size_t get_approx_word_cnt(std::size_t const txt_sz) noexcept
    { return 1u + txt_sz / avg_word_len;}

    bool LoadAlphabet(const std::string& alphabetFile);

    text_tokens_t Parse(wstr_view_t const & txt
        , sep_type const & sep = sep_type{}
    ) const;

    template <typename TTokens>
    void Parse(wstr_view_t const & txt
        , TTokens & rtoks
        , sep_type const & sep = sep_type{}
    ) const;

    void Filter4Train(text_tokens_t & tokens) const;

    static bool isSoftSentEnd(wchar_t const ch) noexcept
    { 
        return ch == L'.' || ch == 0x2026; // "..."
    }

    static bool isHardSentEnd(wchar_t const ch) noexcept
    { return ch == L'!' || ch == L'?'; }

    template <typename TTokenIt>
    bool isSentBreak(TTokenIt const curr_tok_it, TTokenIt const e) const 
    {
        wchar_t const curr_wch{GetToken(*curr_tok_it).front()};
        TTokenIt next {curr_tok_it}; ++next;

        return isHardSentEnd(curr_wch) 
            || (isSoftSentEnd(curr_wch)
                &&  (    next == e 
                    || (Alphabet.isUpper(GetToken(*next).front()) 
                        && ( areSpaced(GetToken(*curr_tok_it), GetToken(*next))) 
                       )
                    )
                ); // LISPanyk is happy! =)
    }
    
    static bool isSentEnd(token_info_t const & t)
    { 
        return (t.size() == 1 )
            && (isSoftSentEnd(t.front()) || isHardSentEnd(t.front())
            );
    }

    bool isWordToken (token_info_t const & token) const
    {
        return (!token.empty()) && (token.size() < max_word_length) 
            && (!Alphabet.isPunct(token.front()));
    }

    bool isGoodWordToken (token_info_t const & token) const 
    {
        return isWordToken(token);
    }
    
    void Clear();

    alphabet_type const & GetAlphabet() const {return Alphabet;}

    static void FilterHyphen(std::wstring & txt);

    HANDYPACK(Alphabet)

private:

    alphabet_type Alphabet;

};

////////////////////////////////////////////////////////////////////////////////

template <typename TTokens>
void TTokenizer::Parse(wstr_view_t const & txt
    , TTokens & rtoks
    , sep_type const & sep
) const
{    
    tokenizer_type const & tokens(Tokenize(txt, sep)); 
    for (token_type const & token: tokens)
    {
        rtoks.emplace_back(token.make_info(txt));
    }
}


} // NJamSpell
