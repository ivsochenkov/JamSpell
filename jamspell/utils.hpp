#pragma once

#include <string>
#include <vector>
// #include <unordered_set>
#include <locale>

#ifdef USE_BOOST_CONVERT
    #include <boost/locale/encoding_utf.hpp>
#else
    #include <codecvt>
#endif

#include <contrib/handypack/handypack.hpp>

namespace NJamSpell {

struct TWord {
    TWord() = default;
    TWord(const wchar_t* ptr, size_t len)
        : Ptr(ptr)
        , Len(len)
    {
    }
    TWord(const std::wstring& w)
        : Ptr(&w[0])
        , Len(w.size())
    {
    }
    bool operator ==(const TWord& other) const {
        return (Ptr == other.Ptr && Len == other.Len);
    }
    const wchar_t* Ptr = nullptr;
    size_t Len = 0;
};

struct TScoredWord {
    TWord Word;
    double Score = 0;
};

struct TWordHashPtr {
public:
  std::size_t operator()(const TWord& x) const {
      return (size_t)x.Ptr;
  }
};

using TWords = std::vector<TWord>;
using TScoredWords = std::vector<TScoredWord>;
using TSentences = std::vector<TWords>;

class TTokenizer {
public:

    using alphabet_type = std::vector<wchar_t>;

    TTokenizer();
    bool LoadAlphabet(const std::string& alphabetFile);
    TSentences Process(const std::wstring& originalText) const;
    void Clear();

    alphabet_type const & GetAlphabet() const {return Alphabet;}

    char FindInAlphabet (wchar_t const ch) const;

    std::string ToAlphabet(TWord const & s) const;
    std::wstring FromAlphabet(std::string const & s) const;

    HANDYPACK(Alphabet)
private:
    alphabet_type Alphabet;
    std::locale Locale;
};

std::string LoadFile(const std::string& fileName);
void SaveFile(const std::string& fileName, const std::string& data);

struct utf8_to_wide_t
{
    std::wstring operator () (std::string const & text) 
    {
#ifdef USE_BOOST_CONVERT
        using boost::locale::conv::utf_to_utf;
        return utf_to_utf<wchar_t>(text.c_str(), text.c_str() + text.size());
#else
        return m_cnv.from_bytes(text);
#endif        
    }

private:
#ifdef USE_BOOST_CONVERT
#else
    using cnv_type = std::wstring_convert<std::codecvt_utf8_utf16<
        wchar_t, 0x10ffff, std::little_endian>
    >;
    cnv_type    m_cnv;
#endif
};

std::wstring UTF8ToWide(const std::string& text);
std::string WideToUTF8(const std::wstring& text);

uint64_t GetCurrentTimeMs();
void ToLower(std::wstring& text);
wchar_t MakeUpperIfRequired(wchar_t orig, wchar_t sample);
uint16_t CityHash16(const std::string& str);
uint16_t CityHash16(const char* str, size_t size);

} // NJamSpell
