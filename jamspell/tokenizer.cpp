#include "tokenizer.hpp"

#include <chrono>
#include <cassert>
#include <iostream>
#include <algorithm>
#include <locale>

namespace NJamSpell 
{

inline bool TTokenizer::good4join (text_tokens_const_iterator_t const & a
    , text_tokens_const_iterator_t const & b
    , text_tokens_const_iterator_t const & c
) const
{
    return isGoodWordToken(*c) 
        && isNotSpaceDelimited(a, b) 
        && isNotSpaceDelimited(b, c)
        && (b -> size() == 1) && isA(b -> front(), L"-\'");
}

inline bool TTokenizer::good4join(text_tokens_const_iterator_t const& a
    , text_tokens_const_iterator_t const & b
) const
{
    return isNotSpaceDelimited(a, b) 
        && !((a -> size() == 1) && isA(a -> front(), L"([{")) 
        && !((b -> size() == 1) && isA(b -> front(), L".,;:!?)")); 
    ; 
}

void TTokenizer::Filter4Spell(text_tokens_t & tokens) const
{
    if(tokens.empty())
    {
        return;
    }

#if 0
    text_tokens_t::iterator tgt_it = tokens.begin();
    for (text_tokens_t::iterator nxt_it = tgt_it, e = tokens.end()
        ; ++nxt_it != e
        ;
    )
    {
        if(good4join (tgt_it, nxt_it))
        {
            tgt_it -> reset (tgt_it -> pos() 
                , nxt_it -> pos() + nxt_it -> size() - tgt_it -> pos()
            );
        }
        else
        {
            (++tgt_it) -> assign (*nxt_it);
        }
    }

    text_tokens_t::iterator const e = ++tgt_it; // N!B!
    for (text_tokens_t::iterator i = tgt_it = tokens.begin()
        ; i != e
        ; ++i
    )
    {
        std::size_t const tsz = i -> size();
        if( tsz < max_word_length && 
            (tsz > 1u  || (tsz > 0u && isSentBreak(i, e))) 
        )
        {
            (tgt_it++) -> assign (*i);
        }
    }

    tokens.resize(std::distance(tokens.begin(), tgt_it));
#else   // #if 0

#endif  // #if 0

}

void TTokenizer::Filter4Train(text_tokens_t & tokens) const
{
    if(tokens.empty())
    {
        return;
    }

    bool prev_tok_is_good = false;
    text_tokens_t::iterator tgt_it = tokens.begin(), i = tgt_it, e = tokens.end();
    do
    {
        if( isGoodWordToken(*i))
        {
            (tgt_it++) -> assign (*i);
            prev_tok_is_good = true;
        }
        else if((!i -> empty()) && isSentBreak(i, e))
        {
            (tgt_it++) -> assign (*i);
            prev_tok_is_good = false;
        }
        else if (prev_tok_is_good)
        {
            // try to join!
            text_tokens_t::iterator nxt_it = i;
            if(++nxt_it != e)
            {
                if(good4join(--tgt_it, i, nxt_it))
                {
                    tgt_it -> reset (tgt_it -> ofs() 
                        , nxt_it -> ofs() + nxt_it -> size() - tgt_it -> ofs()
                        // , std::distance(tgt_it -> data(), nxt_it -> data() + nxt_it -> size())
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
    while ( ++i != e);

    tokens.resize(std::distance(tokens.begin(), tgt_it));
}

TTokenizer::tokenizer_type 
TTokenizer::Tokenize(wstr_view_t const & txt, sep_type const & sep) const 
{ 
    return tokenizer_type(txt.begin(), txt.end(), sep);
}

TTokenizer::TTokenizer()
    : Locale(GetLocale () )
{
}

bool TTokenizer::LoadAlphabet(const std::string& alphabetFile) 
{
    return Alphabet.LoadFromFile(alphabetFile);
}

text_tokens_t TTokenizer::Parse(wstr_view_t const & txt
    , sep_type const & sep 
) const
{
    tokenizer_type const & tokens(Tokenize(txt, sep));
    text_tokens_t ret;
    ReserveWords(ret, txt);
    Parse(txt, ret, sep);
    return ret;
}

bool TTokenizer::isA(wchar_t const ch, wchar_t const * chrs)
{
    do 
    {
        if(*chrs == ch)
            return true;
    }
    while (*chrs++ != 0);
    return false;    
}

void TTokenizer::Clear() 
{
    Alphabet.Clear();
}

void TTokenizer::FilterHyphen(std::wstring & txt)
{
    std::wstring::iterator tgt = txt.begin();
    bool last_is_alnum = false;
    for(std::wstring::const_iterator i = txt.begin(), e = txt.end(); i != e; )
    {
        if( last_is_alnum && *i == L'-')
        {
            std::wstring::const_iterator n = i + 1u;
            for( ; n != e && (*n == L'\n' || *n == L'\r'); ++n )
            {}

            if(n != (i + 1u))
            {
                i = n;
                last_is_alnum = false;
                continue;
            }
        }
        last_is_alnum = std::isalnum(*tgt++ = *i++, GetLocale());
    }
    txt.resize(std::distance(txt.begin(), tgt));
}

/*
text_tokens_const_iterator_t GetNextSentEnd(text_tokens_const_iterator_t b
    , text_tokens_const_iterator_t const & e
)
{
    for(; (b != e) && (! TTokenizer::isSentEnd( *b )) ; ++b )
    {}
    return b;
}
*/

} // NJamSpell
