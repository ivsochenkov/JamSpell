#include "utils.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cassert>
#include <iostream>
#include <cassert>
#include <algorithm>

#include <contrib/cityhash/city.h>

namespace NJamSpell 
{

std::string LoadFile(const std::string& fileName) {
    std::ifstream in(fileName, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void SaveFile(const std::string& fileName, const std::string& data) {
    std::ofstream out(fileName, std::ios::binary);
    out << data;
}

/*
std::wstring UTF8ToWide(const std::string& text) {
#ifdef USE_BOOST_CONVERT
    using boost::locale::conv::utf_to_utf;
    return utf_to_utf<wchar_t>(text.c_str(), text.c_str() + text.size());
#else
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t, 0x10ffff, std::little_endian>> converter;
    return converter.from_bytes(text);
#endif
}

std::string WideToUTF8(const std::wstring& text) {
#ifdef USE_BOOST_CONVERT
    using boost::locale::conv::utf_to_utf;
    return utf_to_utf<char>(text.c_str(), text.c_str() + text.size());
#else
    using convert_type = std::codecvt_utf8<wchar_t, 0x10ffff, std::little_endian>;
    std::wstring_convert<convert_type, wchar_t> converter;
    return converter.to_bytes(text);
#endif
}
*/

uint64_t GetCurrentTimeMs() {
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return ms.count();
}

static const std::ctype<wchar_t>& GWctype = std::use_facet<std::ctype<wchar_t>>(GetLocale () );

void ToLower(std::wstring& text) 
{
    if(!text.empty())
        GWctype.tolower(text.data(), text.data() + text.size());
}

wchar_t MakeLower(wchar_t orig)
{
    return GWctype.tolower(orig);
}

wchar_t MakeUpper(wchar_t orig)
{
    return GWctype.toupper(orig);
}

uint16_t CityHash16(const std::string& str) {
    uint32_t hash = CityHash32(&str[0], str.size());
    return hash % std::numeric_limits<uint16_t>::max();
}

uint16_t CityHash16(const char* str, size_t size) {
    uint32_t hash = CityHash32(str, size);
    return hash % std::numeric_limits<uint16_t>::max();
}

} // NJamSpell
