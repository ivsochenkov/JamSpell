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
    class str_view_t: public std::wstring_view
    {
    public:
        void assign(iterator b, iterator e) 
        { 
            static_cast<std::wstring_view &> (*this) 
                = std::wstring_view(b, std::distance(b, e)); 
        }
    };

    using token_type = str_view_t;
    using sep_type = boost::char_separator<wchar_t>;
    using tokenizer_impl_type = boost::tokenizer<sep_type
        , token_type::const_iterator
        , token_type
    >;

    using token_iterator_type = tokenizer_impl_type::iterator;

    static inline bool isSoftSentEnd(wchar_t const ch)
    { return ch == L'.'; }

    static inline bool isHardSentBreak(wchar_t const ch)
    { return ch == L'!' || ch == L'?'; }

    template<typename TSentSeq, typename TWSeq>
    static void AddSentenceIfNeeded(TSentSeq & sentences, TWSeq & currSent );
    
    bool iSpaceDelimited(token_iterator_type const & curr_tok_it
        , token_iterator_type const & next
    ) const 
    { return curr_tok_it -> end() < next -> begin();}  

    bool isSentBreak(token_iterator_type curr_tok_it
        , token_iterator_type const & e
    ) const;

    bool isCapitalLetter(wchar_t const wch)const 
    {return std::isupper(wch, Locale);}

    template<typename TWSeq>
    void AddTokens(TWSeq & sent
        , token_type::const_iterator s
        , token_type::const_iterator e
    ) const;

    template <typename TSentSeq>
    void TokenizerImpl(const std::wstring_view& originalText
        , TSentSeq & result 
    )const;

public:

    using alphabet_type = TAlphabet;

    static constexpr std::size_t        avg_sent_len_bytes = 37
                                    ,   avg_sent_len_words = 17
                                    ;

    TTokenizer();
    bool LoadAlphabet(const std::string& alphabetFile);
    TSentences Tokenize(const std::wstring_view& originalText) const;
    void Tokenize(const std::wstring_view& originalText, sentences_t & result )const;
    void Clear();

    alphabet_type const & GetAlphabet() const {return Alphabet;}

    HANDYPACK(Alphabet)

private:

    alphabet_type Alphabet;
    std::locale Locale;


};

} // NJamSpell
