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
        void assign(iterator const b, iterator const e) 
        { 
            static_cast<wstr_view_t &> (*this) 
                = wstr_view_t(b, std::distance(b, e)); 
        }

        token_info_t make_info (wstr_view_t const & txt) const
        {
            return token_info_t{txt
                , static_cast<token_info_t::ofs_type>(std::distance(txt.begin(), this -> begin()))
                , static_cast<token_info_t::len_type>(this -> size())
            };
        }
    };

public:

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

    static inline bool isSoftSentEnd(wchar_t const ch)
    { 
        return ch == L'.' || ch == 0x2026; // "..."
    }

    static inline bool isHardSentEnd(wchar_t const ch)
    { return ch == L'!' || ch == L'?'; }

    bool isSentBreak(text_tokens_const_iterator_t const curr_tok_it
        , text_tokens_const_iterator_t const e
    ) const
    {
        wchar_t const curr_wch{curr_tok_it -> front()};
        text_tokens_const_iterator_t next {curr_tok_it}; ++next;

        return isHardSentEnd(curr_wch) 
            || (isSoftSentEnd(curr_wch)
                &&  (    next == e 
                    || (isCapitalLetter(next -> front()) 
                            && ( iSpaceDelimited(curr_tok_it, next) ) )
                    )
                );
    }
    
    bool iSpaceDelimited(text_tokens_const_iterator_t const curr_tok_it
        , text_tokens_const_iterator_t const next
    ) const 
    { return curr_tok_it -> end_ofs() < next -> ofs();}  

    bool isNotSpaceDelimited(text_tokens_const_iterator_t const curr_tok_it
        , text_tokens_const_iterator_t const  next
    ) const 
    { return curr_tok_it -> end_ofs() == next -> ofs();}  

    bool isCapitalLetter(wchar_t const wch)const 
    {return std::isupper(wch, Locale);}

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

    static constexpr std::size_t get_approx_word_cnt(std::size_t const txt_sz) 
    { return 1u + txt_sz / avg_word_len;}

    TTokenizer();

    bool LoadAlphabet(const std::string& alphabetFile);

    text_tokens_t Parse(wstr_view_t const & txt
        , sep_type const & sep = sep_type{}
    ) const;

    
    void Filter4Spell(text_tokens_t & tokens) const;

    void Filter4Train(text_tokens_t & tokens) const;
    
    static bool isSentEnd(token_info_t const & t)
    { 
        return (t.size() == 1 )
            && (isSoftSentEnd(t.front()) || isHardSentEnd(t.front())
            );
    }

    bool isPunct (wchar_t const ch) const
    {
        return std::ispunct(ch, Locale);
    }

    static bool isA(wchar_t const ch, wchar_t const * chrs);    

    bool isWordToken (token_info_t const & token) const
    {
        return (!token.empty()) && (token.size() < max_word_length) 
            && (!isPunct(token.front()));
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
    std::locale Locale;


};

////////////////////////////////////////////////////////////////////////////////

text_tokens_const_iterator_t GetNextSentEnd(text_tokens_const_iterator_t b
    , text_tokens_const_iterator_t const & e
);

inline text_tokens_const_iterator_range_t
GetNextSent(text_tokens_const_iterator_t b
    , text_tokens_const_iterator_t const & e
)
{
    return boost::make_iterator_range(b, GetNextSentEnd(b, e ));
}

inline candidates_range_t
MapSentence (candidates_t & contxt
    , text_tokens_t const & orig_txt_tokens
    , text_tokens_const_iterator_range_t const & curr_sent
)
{
    auto wbeg = contxt.begin(), wend = wbeg;
    std::advance(wbeg, std::distance(orig_txt_tokens.begin(), curr_sent.begin()));
    std::advance(wend, std::distance(orig_txt_tokens.begin(), curr_sent.end()));
    return candidates_range_t{wbeg, wend};
}


} // NJamSpell
