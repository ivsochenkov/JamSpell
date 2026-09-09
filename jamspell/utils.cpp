#include "utils.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cassert>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <contrib/cityhash/city.h>

namespace NJamSpell 
{

#if defined(DEBUG) || defined(JS_TRACE)
std::string Tokens2Str (text_tokens_t const & tokens)
{
    std::string s;
    s.reserve(64u + 10*tokens.size());

    for(auto const & t : tokens )
    {
        s += "{str:\"";
        s += w_to_u8(t.str());
        s += "\"}";
    }
    
    return s;
}
#endif

uint64_t GetCurrentTimeMs() {
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return ms.count();
}

uint16_t CityHash16(const std::string& str) {
    uint32_t hash = CityHash32(&str[0], str.size());
    return hash % std::numeric_limits<uint16_t>::max();
}

uint16_t CityHash16(const char* str, size_t size) {
    uint32_t hash = CityHash32(str, size);
    return hash % std::numeric_limits<uint16_t>::max();
}

str_view_t readEnvVal (char const * const env_var_nam)
{    
    const char* env_p = std::getenv(env_var_nam);
    return (env_p != nullptr) ? str_view_t(env_p, ::std::strlen(env_p)) : str_view_t{};
}

} // NJamSpell
