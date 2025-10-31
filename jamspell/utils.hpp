#pragma once

#include <string>
#include <vector>
#include <string_view>
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

    TWord(const std::wstring_view& w)
        : Ptr(&w[0])
        , Len(w.size())
    {
    }

    bool operator ==(const TWord& other) const {
        return (Ptr == other.Ptr && Len == other.Len);
    }

    explicit operator bool () const {return Ptr != nullptr;}

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

struct wide_to_utf8_t
{
    std::string operator () (const std::wstring& text)
    {
#ifdef USE_BOOST_CONVERT
        using boost::locale::conv::utf_to_utf;
        return utf_to_utf<char>(text.c_str(), text.c_str() + text.size());
#else
        
    return m_cnv.to_bytes(text);
#endif        
    }

private:
#ifdef USE_BOOST_CONVERT
#else
    using cnv_type = std::wstring_convert<
        std::codecvt_utf8<wchar_t, 0x10ffff, std::little_endian>
        , wchar_t
    >;
    cnv_type    m_cnv;
#endif
};

//std::wstring UTF8ToWide(const std::string& text);
//std::string WideToUTF8(const std::wstring& text);

uint64_t GetCurrentTimeMs();
void ToLower(std::wstring& text);
wchar_t MakeLower(wchar_t orig);
wchar_t MakeUpper(wchar_t orig);

inline wchar_t MakeUpperIfRequired(wchar_t orig, wchar_t sample) 
{
    return (MakeUpper(sample) == sample) ? MakeUpper(orig) : orig;
}

uint16_t CityHash16(const std::string& str);
uint16_t CityHash16(const char* str, size_t size);
inline uint16_t CityHash16(std::string_view const & sw) 
{return CityHash16(sw.data(), sw.size());}

} // NJamSpell
