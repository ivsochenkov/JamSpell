#pragma once

#include <string>
#include <vector>
#include <string_view>
#include <locale>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <functional>

#ifdef DEBUG
#define JS_TRACE_MSG(arg) arg
#else
#define JS_TRACE_MSG(arg) 
#endif

#ifdef USE_BOOST_CONVERT
    #include <boost/locale/encoding_utf.hpp>
#else
    #include <codecvt>
#endif

#include <contrib/handypack/handypack.hpp>

namespace NJamSpell 
{

    //using TWordId = uint32_t;
enum class TWordId : uint32_t 
{
    Unknown = std::numeric_limits<std::underlying_type<TWordId>::type>::max()
};

inline constexpr std::underlying_type<TWordId>::type to_underlying(TWordId w)
{return static_cast<std::underlying_type<TWordId>::type>(w);}

using TCount = uint32_t;

using TWordIds = std::vector<TWordId>;
using TIdSentences = std::vector<TWordIds>;


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

    bool empty () const { return !Len; }
    explicit operator bool () const {return Ptr != nullptr;}

    const wchar_t* Ptr = nullptr;
    size_t Len = 0;
};

struct TWordHashPtr {
public:
  std::size_t operator()(const TWord& x) const {
      return (size_t)x.Ptr;
  }
};

using TWords = std::vector<TWord>;
using TSentences = std::vector<TWords>;

struct TScoredWord 
{
    TWord Word;
    double Score = 0;

    explicit TScoredWord (TWord const & w = TWord{}, double sc = 0.0)
    : Word (w), Score {sc} 
    {}
};

using TScoredWords = std::vector<TScoredWord>;

using wstr_view_type = std::wstring_view;

struct word_info_t
{
    wstr_view_type          str;            // 16
    float                   weight;         // 4
    TWordId                 id;             // 4
    //bool                    first_level;    // 8
                                            // SIV: 32 bits! Pack to 24!

    explicit word_info_t (TWordId const & i = TWordId::Unknown
        , wstr_view_type const & wstr = wstr_view_type{}
        , float const w = 0.0
    )
    : str{wstr}, weight{w}, id{i}
    {}

    explicit word_info_t (wstr_view_type const & wstr )
    : str{wstr}, id{TWordId::Unknown}
    {}

    explicit operator bool () const {return !empty();}

    bool empty() const {return TWordId::Unknown == id;}

};

using words_seq_t = std::vector<word_info_t>;
using sentences_t = std::vector<words_seq_t>;

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

inline std::locale GetLocale () 
{
    static const std::locale GLocale = std::locale("ru_RU.UTF-8");
    return GLocale;
}

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
