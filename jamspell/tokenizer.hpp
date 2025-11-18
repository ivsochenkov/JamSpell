#pragma once

#include "utils.hpp"
#include "alphabet.hpp"

#include <boost/tokenizer.hpp>

#include <string>
#include <vector>
#include <locale>


namespace NJamSpell 
{

using text_tokens_t    = std::vector<std::wstring_view>;
using text_tokens_iterator_t = text_tokens_t::iterator;
using text_tokens_const_iterator_t = text_tokens_t::const_iterator;

using text_tokens_const_iterator_range_t 
    = boost::iterator_range<text_tokens_const_iterator_t>;

class TTokenizer
{
    class str_view_t: public std::wstring_view
    {
    public:
        void assign(iterator b, iterator e) 
        { 
            static_cast<std::wstring_view &> (*this) 
                = std::wstring_view(b, std::distance(b, e)); 
        }
    };

public:

    using sep_type = boost::char_separator<wchar_t>;

    using token_type = str_view_t;

    using tokenizer_type = boost::tokenizer<sep_type
        , token_type::const_iterator
        , token_type
    >;

    using token_iterator_type = tokenizer_type::iterator;

private:

    static inline bool isSoftSentEnd(wchar_t const ch)
    { 
        return ch == L'.' || ch == 0x2026; // "..."
    }

    static inline bool isHardSentBreak(wchar_t const ch)
    { return ch == L'!' || ch == L'?'; }


    template <typename TTokIt>
    bool isSentBreak(TTokIt curr_tok_it, TTokIt const & e) const
    {
        wchar_t const curr_wch{(*curr_tok_it)[0]};
        TTokIt next {curr_tok_it}; ++next;

        return isHardSentBreak(curr_wch) 
            || (isSoftSentEnd(curr_wch)
                &&  (    next == e 
                    || (isCapitalLetter((*next)[0]) && ( iSpaceDelimited(curr_tok_it, next) ) )
                    )
                );
    }
    
    template <typename TTokIt>
    bool iSpaceDelimited(TTokIt const & curr_tok_it, TTokIt const & next) const 
    { return curr_tok_it -> end() < next -> begin();}  

    template <typename TTokIt>
    bool isNotSpaceDelimited(TTokIt const & curr_tok_it, TTokIt const & next) const 
    { return curr_tok_it -> end() == next -> begin();}  

    bool isCapitalLetter(wchar_t const wch)const 
    {return std::isupper(wch, Locale);}

    struct join_hyphen_pred_t;
    struct join_pred_t;

public:

    using alphabet_type = TAlphabet;

    static constexpr std::size_t        avg_sent_len_bytes = 37
                                    ,   avg_sent_len_words = 17
                                    ,   avg_word_len = 5
                                    ;

    static constexpr std::size_t get_approx_word_cnt(std::size_t const txt_sz) 
    { return 1u + txt_sz / avg_word_len;}

    TTokenizer();

    bool LoadAlphabet(const std::string& alphabetFile);

    tokenizer_type Tokenize(const std::wstring_view& txt
        , sep_type const & sep = sep_type{}
    ) const 
    { return tokenizer_type(txt.begin(), txt.end(), sep);}

    text_tokens_t Parse(const std::wstring_view& txt
        , sep_type const & sep = sep_type{}
    ) const;

    template <typename TJoinPred>
    void FilterJoin(TJoinPred && good4Join
        , text_tokens_t & tokens
    ) const;

    void FilterAndJoin(text_tokens_t & tokens) const;

    void FilterAndJoinHyphen(text_tokens_t & tokens) const;

    static bool isSentEnd(const std::wstring_view& tok_str)
    { 
        return (tok_str.size() == 1 )
            && (isSoftSentEnd(tok_str.front()) 
                || isHardSentBreak(tok_str.front())
            );
    }

    bool isPunct (wchar_t const ch) const
    {
        return std::ispunct(ch, Locale);
    }

    static bool isHyphen(wchar_t const ch)
    {
        return ch == L'-';  // TODO - add more warians here!
    }

    bool isWordToken (wstr_view_t const & tok_str) const
    {
        return (!tok_str.empty()) 
            && (tok_str.size() < std::numeric_limits<std::uint8_t>::max()) 
        && (!isPunct(tok_str.front()));
    }

    bool isGoodWordToken (wstr_view_t const & tok_str) const 
    {
        return isWordToken(tok_str);
    }
    
    void Clear();

    alphabet_type const & GetAlphabet() const {return Alphabet;}

    HANDYPACK(Alphabet)

private:

    alphabet_type Alphabet;
    std::locale Locale;


};

////////////////////////////////////////////////////////////////////////////////

template <typename TJoinPred>
void TTokenizer::FilterJoin(TJoinPred && good4Join
    , text_tokens_t & tokens
) const
{
    bool prev_tok_is_good = false;
    text_tokens_t::iterator tgt_it = tokens.begin();
    for(text_tokens_t::iterator i = tokens.begin(), e = tokens.end() ; i != e ; ++i)
    {
        if( isGoodWordToken(*i))
        {
            *tgt_it++ = *i;
            prev_tok_is_good = true;
        }
        else if((!i->empty()) && isSentBreak(i, e))
        {
            *tgt_it++ = *i;
            prev_tok_is_good = false;
        }
        else if (prev_tok_is_good)
        {
            // try to join!
            text_tokens_t::iterator nxt_it = i;
            if(++nxt_it != e)
            {
                if(std::invoke(std::forward<TJoinPred>(good4Join), --tgt_it, i, nxt_it))
            
                {
                    *tgt_it = wstr_view_t(tgt_it -> data()
                        , std::distance(tgt_it -> data(), nxt_it -> data() + nxt_it -> size())
                    );
                    //prev_tok_is_good = true; // remains true, so don't needed!
                    i = nxt_it;
                }
                else
                {
                    prev_tok_is_good = false;  // i is not good
                }
                ++tgt_it;
            }
        }
    }

    tokens.resize(std::distance(tokens.begin(), tgt_it));   
}


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

inline word_seq_range_t MapSentence (words_seq_t & words
    , text_tokens_t const & orig_txt_tokens
    , text_tokens_const_iterator_range_t const & curr_sent
)
{
    auto wbeg = words.begin(), wend = wbeg;
    std::advance(wbeg, std::distance(orig_txt_tokens.begin(), curr_sent.begin()));
    std::advance(wend, std::distance(orig_txt_tokens.begin(), curr_sent.end()));
    return word_seq_range_t(wbeg, wend);
}

inline std::size_t getOffset(wchar_t const * pos
    , std::wstring_view const & src
)
{
    return std::distance(&src[0], pos);
}

inline std::wstring_view getOrigWord(std::wstring const &text
    , std::size_t pos, std::size_t len
)
{
    return std::wstring_view(text).substr(pos, len);
}

} // NJamSpell
