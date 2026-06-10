#pragma once

#include <boost/locale/encoding_utf.hpp>

#include <contrib/handypack/handypack.hpp>

#include <boost/range/iterator_range.hpp>

#include <string>
#include <vector>
#include <string_view>
#include <locale>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <functional>

#if defined(DEBUG) || defined(JS_TRACE)
#define JS_TRACE_MSG(arg) arg
#else
#define JS_TRACE_MSG(arg) 
#endif

namespace NJamSpell 
{

enum class TWordId : uint32_t 
{
      Any     = std::numeric_limits<std::underlying_type<TWordId>::type>::min()
    , Unknown = std::numeric_limits<std::underlying_type<TWordId>::type>::max()
};

inline constexpr std::underlying_type<TWordId>::type to_underlying(TWordId w)
{return static_cast<std::underlying_type<TWordId>::type>(w);}

using TCount = uint32_t;

using TWordIds = std::vector<TWordId>;
using TIdSentences = std::vector<TWordIds>;

static constexpr const unsigned MAX_WORD_LENGTH = 64;

/*

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

*/

using wstr_view_t = std::wstring_view;
using str_view_t = std::string_view;
using str_t = std::string;

struct word_info_t
{
    using str_type =        str_t;

    str_type                str;            
    float                   weight;         
    TWordId                 id;             
    bool                    first_level = true;

    explicit word_info_t (TWordId const i
        , str_type const & s
        , float const w = 0.0
    )
    : str{s}, weight{w}, id{i}
    {}

    explicit word_info_t (TWordId const i  = TWordId::Unknown, float const w = 0.0)
    : str{}, weight{w}, id{i}
    {}

    explicit word_info_t (str_type const & s )
    : str{s}, weight{0.0}, id{TWordId::Unknown}
    {}

    explicit operator bool () const {return !unknown();}

    bool unknown() const {return TWordId::Unknown == id;}

};

using words_seq_t = std::vector<word_info_t>;
using word_seq_range_t = boost::iterator_range<words_seq_t::iterator>;

struct word_info_greater_t
{
    bool operator () (word_info_t const & w1, word_info_t const & w2) const
    { return w1.weight > w2.weight;}
};

/*
words_seq_t::iterator GetNextSentenceEnd(words_seq_t::iterator b
    , words_seq_t::iterator const & e
);


inline bool isSentEnd(word_info_t const & winf)
{
    return winf.str.empty();
}
*/

std::string LoadFile(const std::string& fileName);
void SaveFile(const std::string& fileName, const std::string& data);

struct utf8_to_wide_t
{
    std::wstring operator () (std::string const & text) 
    {
        using boost::locale::conv::utf_to_utf;
        return utf_to_utf<wchar_t>(text.c_str(), text.c_str() + text.size());
    }

private:
};

struct wide_to_utf8_t
{
    std::string operator () (const std::wstring& text)
    {
        using boost::locale::conv::utf_to_utf;
        return utf_to_utf<char>(text.c_str(), text.c_str() + text.size());
    }
};

inline std::locale GetLocale () 
{
    static const std::locale GLocale = std::locale("ru_RU.UTF-8");
    return GLocale;
}

//std::wstring UTF8ToWide(const std::string& text);
//std::string WideToUTF8(const std::wstring& text);

uint64_t GetCurrentTimeMs();
//void ToLower(std::wstring& text);
wchar_t MakeLower(wchar_t orig);
wchar_t MakeUpper(wchar_t orig);

inline wchar_t MakeUpperIfRequired(wchar_t orig, wchar_t sample) 
{
    return (MakeUpper(sample) == sample) ? MakeUpper(orig) : orig;
}

inline str_view_t Remap(str_view_t const & atxt
    , wstr_view_t const & orig_txt
    , wstr_view_t const & orig_word
)
{
    return str_view_t(
            atxt.data() + std::distance(orig_txt.data(), orig_word.data())
        ,   orig_word.size()
    );
}

uint16_t CityHash16(const std::string& str);
uint16_t CityHash16(const char* str, size_t size);
inline uint16_t CityHash16(std::string_view const & sw) 
{return CityHash16(sw.data(), sw.size());}

} // NJamSpell
