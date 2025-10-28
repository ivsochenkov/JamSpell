#include <fstream>
#include <sstream>
#include <chrono>
#include <cassert>
#include <iostream>
#include <cassert>
#include <algorithm>

#include "utils.hpp"

#include <contrib/cityhash/city.h>


namespace NJamSpell {

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

TTokenizer::TTokenizer()
    : Locale(std::locale::classic())
{
}

bool TTokenizer::LoadAlphabet(const std::string& alphabetFile) 
{
    std::string data = LoadFile(alphabetFile);
    if (data.empty()) {
        return false;
    }
    std::wstring wdata = UTF8ToWide(data);
    if (wdata.empty()) {
        return false;
    }
    ToLower(wdata);
    Alphabet.reserve(wdata.size());
    for (auto chr: wdata) {
        if (chr == 0 || chr == 10 || chr == 13) 
        {
            continue;
        }
        Alphabet.push_back(chr);
    }
    if (Alphabet.empty()) 
    {
        return false;
    }
    if (Alphabet.size() >= std::numeric_limits<unsigned char>::max()) 
    {
        std::cerr << "[error] alphabet size greater than 254 is not supported!\n"; 
        return false;
    }

    std::sort(Alphabet.begin(), Alphabet.end());
    return true;
}

TSentences TTokenizer::Process(const std::wstring& originalText) const {
    if (originalText.empty()) {
        return TSentences();
    }

    TSentences sentences;

    TWords currSentence;
    TWord currWord;

    for (size_t i = 0; i < originalText.size(); ++i) {
        wchar_t const letter = std::tolower(originalText[i], Locale);
        //if (Alphabet.find(letter) != Alphabet.end()) 
        if (0 != FindInAlphabet(letter)) 
        {
            if (currWord.Ptr == nullptr) {
                currWord.Ptr = &originalText[i];
            }
            currWord.Len += 1;
        } else {
            if (currWord.Ptr != nullptr) {
                currSentence.push_back(currWord);
                currWord = TWord();
            }
        }
        if (letter == L'?' || letter == L'!' || letter == L'.') {
            if (!currSentence.empty()) {
                sentences.emplace_back();
                sentences.back().swap(currSentence);
                //currSentence.clear();
            }
        }
    }
    if (currWord.Ptr != nullptr) {
        currSentence.push_back(currWord);
    }
    if (!currSentence.empty()) {
        sentences.push_back(currSentence);
    }

    return sentences;
}

void TTokenizer::Clear() {
    Alphabet.clear();
}


char TTokenizer::FindInAlphabet (wchar_t const ch) const
{
    auto const it = std::lower_bound(Alphabet.begin(), Alphabet.end(), ch);
    return it != Alphabet.end() ? ((*it == ch) ? 
            static_cast<char> (std::distance(Alphabet.begin(), it) + 1) 
            : static_cast<char>(0)
        )
    : static_cast<char>(0);
}

std::string TTokenizer::ToAlphabet(TWord const & src) const
{
    std::string s(src.Len, '\0');
    auto tgt = s.begin();
    for(wchar_t const * pCh (src.Ptr), * const pEnd(src.Ptr + src.Len) 
        ; pCh != pEnd
        ; *(tgt++) = FindInAlphabet(*pCh++)
    ){}
    return s;
}

std::wstring TTokenizer::FromAlphabet(std::string const & src) const
{
    std::wstring s(src.size(), static_cast<wchar_t> (0) );
    auto tgt = s.begin();
    for (char const c : src)
    {
        *tgt++ = Alphabet[ c - 1 ];
    }
    return s;
}

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

uint64_t GetCurrentTimeMs() {
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return ms.count();
}

static const std::locale GLocale(std::locale::classic());
static const std::ctype<wchar_t>& GWctype = std::use_facet<std::ctype<wchar_t>>(GLocale);

void ToLower(std::wstring& text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t wch) {
        return GWctype.tolower(wch);
    });
}

wchar_t MakeUpperIfRequired(wchar_t orig, wchar_t sample) {
    if (GWctype.toupper(sample) == sample) {
        return GWctype.toupper(orig);
    }
    return orig;
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
