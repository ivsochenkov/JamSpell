#include "tokenizer.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cassert>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <locale>

namespace NJamSpell 
{

struct TTokenizer::join_hyphen_pred_t
{
    explicit join_hyphen_pred_t (TTokenizer const & tknzr): m_tknzr(tknzr) {}

    template <typename TTokIt>
    bool operator () (TTokIt const & a, TTokIt const & b, TTokIt const & c) const
    {
        return m_tknzr.isGoodWordToken(*c) 
            && m_tknzr.isNotSpaceDelimited(a, b) 
            && m_tknzr.isNotSpaceDelimited(b, c)
            && (b -> size() == 1) && m_tknzr.isHyphen(b -> front()) ;
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

void TTokenizer::FilterAndJoin(text_tokens_t & tokens) const
{
    FilterJoin(join_pred_t{*this}, tokens);
}

void TTokenizer::FilterAndJoinHyphen(text_tokens_t & tokens) const
{
    FilterJoin(join_hyphen_pred_t{*this}, tokens);
}

TTokenizer::TTokenizer()
    : Locale(GetLocale () )
{
}

bool TTokenizer::LoadAlphabet(const std::string& alphabetFile) 
{
    return Alphabet.LoadFromFile(alphabetFile);
}

text_tokens_t TTokenizer::Parse(const std::wstring_view& txt
    , sep_type const & sep 
) const
{
    tokenizer_type const & tok(Tokenize(txt, sep));
    text_tokens_t ret;
    ret.reserve(1u + txt.size() / avg_word_len);
    ret.assign(tok.begin(), tok.end());
    return ret;
}

void TTokenizer::Clear() 
{
    Alphabet.Clear();
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
