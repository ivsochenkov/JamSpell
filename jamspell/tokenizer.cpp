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

inline void TTokenizer::AddSentenceIfNeeded(TSentences & sentences, TWords & currSent )
{
    if(!currSent.empty())
    {
        sentences.emplace_back();
        sentences.back().swap(currSent);
        currSent.reserve(avg_sent_len_words);
    }
}



inline bool TTokenizer::isSentBreak(token_iterator_type curr_tok_it
    , token_iterator_type const & e
) const
{
    wchar_t const curr_wch{(*curr_tok_it)[0]};
    token_iterator_type next {curr_tok_it}; ++next;

    return isHardSentBreak(curr_wch) 
    || (isSoftSentEnd(curr_wch)
        && (    next == e 
            || (isCapitalLetter((*next)[0]) && ( iSpaceDelimited(curr_tok_it, next) ) )
        )
    );
}

void TTokenizer::AddTokens(TWords & sent
    , token_type::const_iterator s, token_type::const_iterator e
) const
{
    token_type::const_iterator prev(s);
    for(; s != e; ++s)
    {
        if(!Alphabet.Contains(*s))
        {
            if (prev != s)
            {
                sent.emplace_back(prev, std::distance(prev, s));
                prev = s;
            }
            ++prev;
        }
    }
    if (prev != e)
    {
        sent.emplace_back(prev, std::distance(prev, e));
    }
}

TTokenizer::TTokenizer()
    : Locale(std::locale::classic())
{
}

bool TTokenizer::LoadAlphabet(const std::string& alphabetFile) 
{
    return Alphabet.LoadFromFile(alphabetFile);
}


TSentences TTokenizer::Tokenize(const std::wstring_view& originalText) const 
{
    TSentences sentences;
    if (originalText.empty()) 
    {
        return sentences;
    }
    
    sentences.reserve(3 + originalText.size() / avg_sent_len_bytes);
    TWords currSentence;
    currSentence.reserve(avg_sent_len_words);

    sep_type sep; 
    tokenizer_impl_type tok(originalText.begin(), originalText.end(), sep);

    bool prev_punct{false};
    for(tokenizer_impl_type::iterator tok_iter= tok.begin()
            , e = tok.end()
        ; tok_iter != e
        ; ++tok_iter
    )
    {
        token_type const curr_tok_str (*tok_iter);
        if(curr_tok_str.size() == 1)
        {
            wchar_t const curr_ch{curr_tok_str[0]};
            if(std::ispunct(curr_ch, Locale))
            {  
                if( isSentBreak(tok_iter, e))
                {
                    AddSentenceIfNeeded(sentences, currSentence);
                    continue;
                }
                prev_punct = true;
                continue;
            }          
        }
        AddTokens(currSentence, curr_tok_str.begin(), curr_tok_str.end());              
        prev_punct = false;
    }

    AddSentenceIfNeeded(sentences, currSentence);

    return sentences;
}

void TTokenizer::Clear() 
{
    Alphabet.Clear();
}



} // NJamSpell
