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

/// @brief /////////////////////////////////////////////////////////////////////

struct token_info_t
{
    using ofs_type = ::std::uint32_t;
    using len_type = ::std::uint32_t;

    explicit token_info_t(wstr_view_t const & txt, ofs_type const ofs = -1, len_type const l = 0u)
    : m_pTxt(txt.data()), m_ofs{ofs}, m_len{l}
    {}

    token_info_t () {};

    void reset (ofs_type const ofs, len_type const l) 
    {
        m_ofs = ofs;
        m_len = l;
    }

    void assign(token_info_t const & rhs)
    {
        reset(rhs.m_ofs, rhs.m_len);
    }

    wstr_view_t str() const
    {
        return wstr_view_t {data(), m_len};
    }

    constexpr wstr_view_t::const_pointer data() const 
    {
        return m_pTxt + m_ofs;
    }

    constexpr bool empty () const {return !m_len;}

    constexpr wstr_view_t::value_type front () const 
    {
        BOOST_ASSERT_MSG(m_pTxt, "Text must not be a nullptr!");
        return m_pTxt[m_ofs];
    }

    constexpr ofs_type ofs () const {return m_ofs;}
    constexpr ofs_type end_ofs () const {return m_ofs + m_len;}
    constexpr len_type size () const {return m_len;}
    
private:

    wstr_view_t::value_type const       * m_pTxt   = nullptr;
    ofs_type                            m_ofs    = -1;
    len_type                            m_len    = 0u;
};

//using text_tokens_t    = std::vector<wstr_view_t>;
using text_tokens_t    = std::vector<token_info_t>;
using text_tokens_iterator_t = text_tokens_t::iterator;
using text_tokens_const_iterator_t = text_tokens_t::const_iterator;

using text_tokens_const_iterator_range_t 
    = boost::iterator_range<text_tokens_const_iterator_t>;

#if defined(DEBUG) || defined(JS_TRACE)
std::string Tokens2Str (text_tokens_t const & tokens);
#endif

inline bool areSpaced(token_info_t const & lhs, token_info_t const & rhs)
{ return lhs.end_ofs() < rhs.ofs();}

inline bool areNotSpaced(token_info_t const & lhs, token_info_t const & rhs)
{ return lhs.end_ofs() == rhs.ofs();}

////////////////////////////////////////////////////////////////////////////////

struct wdata_t
{
    TWordId     id;
    TCount      cnt;

    explicit wdata_t(word_id_t const i = word_id_t::Unknown, cnt_t const c = 0)
    :id{i}, cnt {c}
    {}

    bool unknown() const noexcept { return id == word_id_t::Unknown;}

    //explicit operator bool () const noexcept {return !unknown();}

    bool is_punct() const noexcept { return id == word_id_t::Any;}

    bool is_word() const noexcept { return !is_punct();}

    HANDYPACK(id, cnt)
};

struct word_t: public wdata_t
{
    using str_type =        str_t;

    str_type                str;                    

    template <typename TStr>
    word_t (TWordId const i, TStr && s, cnt_t const c = 0 )
    : wdata_t{i, c}, str{std::forward<TStr>(s)}
    {}

    explicit word_t (word_id_t const i = word_id_t::Unknown, cnt_t const c = 0.0)
    :  wdata_t{i, c}, str{}
    {}

    template <typename TStr>
    word_t (wdata_t const & wd, TStr && s)
    : wdata_t{wd}, str{std::forward<TStr>(s)}
    {}

    //explicit word_t (str_type const & s )
    //: str{s}, cnt{0}, id{word_id_t::Unknown}
    //{}

    void reset(wdata_t const & w)
    {
        static_cast<wdata_t &>(*this) = w;
    }

    wdata_t const & wdata() const {return *this;}

    bool good() const {return is_word() && !str.empty();}

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

template <typename TWIt>
TWIt Advance2Next(TWIt beg, TWIt const & e)
{
    while(++beg < e && !(beg -> is_word()))
    {}    
    return beg;
}

////////////////////////////////////////////////////////////////////////////////

struct cand_word_t: public word_t
{
    float           score;  
    cand_kind_t     kind    = ckNone;

    cand_word_t () = default;
    
    template <typename TStr>
    cand_word_t (word_id_t const i, TStr && s, cnt_t const c, cand_kind_t const ck)
    : word_t{i, std::forward<TStr>(s), c}, kind{ck} 
    {}

    /*
    cand_word_t (wdata_t const & wd
        , str_t && s
        , cand_kind_t const k
    ): word_t{wd, std::move(s)}, kind{k} 
    {}

    */

    bool is_orig() const noexcept { return ckOrig == kind;}
    bool is_none() const noexcept { return ckNone == kind;}

};

using candidates_t = std::vector<cand_word_t>;
using candidates_range_t = boost::iterator_range<candidates_t::iterator>;
using candidates_crange_t = boost::iterator_range<candidates_t::const_iterator>;

////////////////////////////////////////////////////////////////////////////////

struct cntxt_word_t: public cand_word_t
{
    cntxt_word_t() = default;

    explicit cntxt_word_t(token_info_t const & tinf)
    : cand_word_t{}, token{tinf} 
    {}

    void assign (cand_word_t && cnd)
    {
        static_cast<cand_word_t &>(*this) = std::move(cnd);
    }

    token_info_t        token;
};

using context_t = std::vector<cntxt_word_t>;
using context_range_t = boost::iterator_range<context_t::iterator>;
using context_crange_t = boost::iterator_range<context_t::const_iterator>;

////////////////////////////////////////////////////////////////////////////////

template <typename TWrds>
void ReserveWords(TWrds & wrds, wstr_view_t const & txt)
{
    static constexpr ::std::size_t avg_word_len = 3;
    wrds.reserve(8u + txt.size() / avg_word_len);
}

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
