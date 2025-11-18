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
