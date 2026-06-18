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

uint64_t GetCurrentTimeMs() {
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return ms.count();
}

static const std::ctype<wchar_t>& GWctype = std::use_facet<std::ctype<wchar_t>>(GetLocale () );

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
