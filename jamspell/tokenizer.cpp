#include "tokenizer.hpp"

#include <chrono>
#include <cassert>
#include <iostream>
#include <algorithm>
#include <locale>

namespace NJamSpell 
{

#if defined(DEBUG) || defined(JS_TRACE)
std::string Tokens2Str (text_tokens_t const & tokens)
{
    std::string s;
    s.reserve(64u + 10*tokens.size());

    for(auto const & t : tokens )
    {
        s += "{str:\"";
        s += w_to_u8(t.str());
        s += "\"}";
    }
    
    return s;
}
#endif

struct TTokenizer::join_4_train_pred_t
{
    explicit join_4_train_pred_t (TTokenizer const & tknzr): m_tknzr(tknzr) {}

    template <typename TTokIt>
    bool operator () (TTokIt const & a, TTokIt const & b, TTokIt const & c) const
    {
        return m_tknzr.isGoodWordToken(*c) 
            && m_tknzr.isNotSpaceDelimited(a, b) 
            && m_tknzr.isNotSpaceDelimited(b, c)
            && (b -> size() == 1) && m_tknzr.isA(b -> front(), L"-\'");
    }
    private:
        TTokenizer const &  m_tknzr;
};

struct TTokenizer::join_pred_t
{
    explicit join_pred_t (TTokenizer const & tknzr): m_tknzr(tknzr) {}

    template <typename TTokIt>
    bool operator () (TTokIt const& a, TTokIt const & b, TTokIt const & c) const
    {
        return m_tknzr.isGoodWordToken(*c) 
            && m_tknzr.isNotSpaceDelimited(a, b) 
            && m_tknzr.isNotSpaceDelimited(b, c)
            && (b -> size() == 1) && m_tknzr.isPunct(b -> front()); 
    }
    private:
        TTokenizer const &  m_tknzr;
};

void TTokenizer::Filter4Spell(text_tokens_t & tokens) const
{
    FilterJoin(join_pred_t{*this}, tokens);
}

void TTokenizer::Filter4Train(text_tokens_t & tokens) const
{
    FilterJoin(join_4_train_pred_t{*this}, tokens);
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
    ret.reserve(1u + txt.size() / avg_word_len);
    //ret.assign(tok.begin(), tok.end());
    for (token_type const & token: tokens)
    {
        ret.emplace_back(token.make_info(txt));
    }
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


text_tokens_const_iterator_t GetNextSentEnd(text_tokens_const_iterator_t b
    , text_tokens_const_iterator_t const & e
)
{
    for(; (b != e) && (! TTokenizer::isSentEnd( *b )) ; ++b )
    {}
    return b;
}

} // NJamSpell
