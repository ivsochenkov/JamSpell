#pragma once

#include <boost/locale/encoding_utf.hpp>

#include <contrib/handypack/handypack.hpp>

#include <boost/range/iterator_range.hpp>

#include <boost/container/string.hpp>

#include <string>
#include <vector>
#include <string_view>
#include <locale>
#include <cstdint>
#include <charconv>
#include <limits>
#include <type_traits>
#include <functional>
#include <system_error>

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

using word_id_t = TWordId;

using TCount = uint32_t;
using cnt_t = TCount;

using TWordIds = std::vector<TWordId>;
using TIdSentences = std::vector<TWordIds>;

static constexpr const unsigned MAX_WORD_LENGTH = 64;

using wstr_view_t = std::wstring_view;
using str_view_t = std::string_view;
//using str_t = std::string;
using str_t = boost::container::string;

struct wdata_t
{
    TWordId     id;
    TCount      cnt;

    explicit wdata_t(word_id_t const i = word_id_t::Unknown, cnt_t const c = 0)
    :id{i}, cnt {c}
    {}

    bool unknown() const noexcept { return id == word_id_t::Unknown;}

    explicit operator bool () const noexcept {return !unknown();}

    HANDYPACK(id, cnt)
};

struct word_t: public wdata_t
{
    using str_type =        str_t;

    str_type                str;                    

    word_t (TWordId const i
        , str_view_t const & s
        , cnt_t const c = 0
    )
    : wdata_t{i, c}, str{s}
    {}

    explicit word_t (word_id_t const i = word_id_t::Unknown, cnt_t const c = 0.0)
    :  wdata_t{i, c}, str{}
    {}

    word_t (wdata_t const & wd, str_t && s)
    : wdata_t{wd}, str{std::move(s)}
    {}

    //explicit word_t (str_type const & s )
    //: str{s}, cnt{0}, id{word_id_t::Unknown}
    //{}

    void reset(wdata_t const & w)
    {
        static_cast<wdata_t &>(*this) = w;
    }

    wdata_t const & wdata() const {return *this;}

};

using words_t = std::vector<word_t>;    // orig_word_t
using words_crange_t = boost::iterator_range<words_t::const_iterator>;

struct orig_word_greater_by_cnt_t
{
    bool operator () (word_t const & w1, word_t const & w2) const
    { return w1.cnt > w2.cnt;}
};

enum cand_kind_t : unsigned char
{
        ckNone          = 0u
    ,   ckOrig                  
    ,   ckOrigSw                
    ,   ckFirstLvl          
    ,   ckSecondLvl         
    ,   ckFirstLvlSw            
    ,   ckSecondLvlSw           
};

inline cand_kind_t NextLevel(cand_kind_t const ck)
{
    return cand_kind_t(ck + 1u);
}

struct cand_word_t: public word_t
{
    float           score;  
    cand_kind_t     kind    = ckNone;

    cand_word_t (): word_t{} {}
    
    cand_word_t (word_id_t const i
        , str_view_t const & s
        , cnt_t const c
        , cand_kind_t const ck
    )
    : word_t{i, s, c}, kind{ck} {}

    cand_word_t (wdata_t const & wd
        , str_t && s
        , cand_kind_t const k
    ): word_t{wd, std::move(s)}, kind{k} 
    {}

};

using candidates_t = std::vector<cand_word_t>;
using candidates_range_t = boost::iterator_range<candidates_t::iterator>;
using candidates_crange_t = boost::iterator_range<candidates_t::const_iterator>;

std::string LoadFile(const std::string& fileName);
void SaveFile(const std::string& fileName, const std::string& data);

uint64_t GetCurrentTimeMs();

inline std::wstring u8_to_w(str_view_t const & text) 
{
    using boost::locale::conv::utf_to_utf;
    return utf_to_utf<wchar_t>(text.data(), text.data() + text.size());
}

inline std::string w_to_u8 (const wstr_view_t & text)
{
    using boost::locale::conv::utf_to_utf;
    return utf_to_utf<char>(text.data(), text.data() + text.size());
}


inline std::locale GetLocale () 
{
    static const std::locale GLocale = std::locale("ru_RU.UTF-8");
    return GLocale;
}

inline std::ctype<wchar_t> const & GetWCtype()
{
    static std::ctype<wchar_t> const & GWctype 
        = std::use_facet<std::ctype<wchar_t> >(GetLocale () );
    return GWctype;
}

inline wchar_t MakeLower(wchar_t orig)
{
    return GetWCtype().tolower(orig);
}

inline wchar_t MakeUpper(wchar_t orig)
{
    return GetWCtype().toupper(orig);
}

inline wchar_t MakeUpperIfRequired(wchar_t const orig, wchar_t const sample) 
{
    return GetWCtype().is(std::ctype_base::upper, sample) ? MakeUpper(orig) : orig;
    // return (MakeUpper(sample) == sample) ? MakeUpper(orig) : orig;
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

str_view_t readEnvVal (char const * const env_var_nam);

template <typename T>
inline 
std::enable_if_t<std::is_integral<T>::value || std::is_floating_point<T>::value, T> 
getEnvVal (char const * const env_var_nam, T const deflt = T{})
{
    T t;
    auto const & env_val = readEnvVal(env_var_nam);
    auto r = ::std::from_chars(env_val.data(), env_val.data() + env_val.size(), t);
    return (r.ec == std::errc())? t : deflt;
}

template <typename T>
inline void 
setValFromEnv (T & val, char const * const env_var_nam)
{
    val = getEnvVal(env_var_nam, val);
}

} // NJamSpell
