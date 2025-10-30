#pragma once

#include <contrib/phf/phf.h>

#include <boost/iterator/transform_iterator.hpp>

#include <ostream>
#include <string_view>

namespace NJamSpell {

class TPerfectHash {
public:
    TPerfectHash();
    TPerfectHash(const TPerfectHash& other) = delete;
    ~TPerfectHash();
    void Dump(std::ostream& out) const;
    void Load(std::istream& in);
    
    template <typename TIt>
    bool Init(TIt const & beg, TIt const & e);

    template <typename TRange>
    bool Init(TRange const &r)
    {return Init(r.begin(), r.end());}

    void Clear();
    uint32_t Hash(const std::string& value) const;
    uint32_t Hash(const char* value, size_t size) const;
    uint32_t Hash(std::string_view const & sw) const {return Hash(sw.data(), sw.size());}
    uint32_t BucketsNumber() const;
private:

    struct make_phf_string_t
    {
        template <typename T>
        phf_string_t operator () (T && s) const 
        {return phf_string_t{&s[0], s.size()};}
    };

    void* Phf; // sort of forward declaration
};

template <typename TIt>
bool TPerfectHash::Init(TIt const & beg, TIt const & e) 
{
    static const make_phf_string_t make_phf_string{};
    std::vector<phf_string_t> keysForPhf;
    keysForPhf.assign(boost::make_transform_iterator(beg, make_phf_string)
        , boost::make_transform_iterator(e, make_phf_string) 
    );
    
    phf* tempPhf = new phf();
    phf_error_t res = PHF::init<phf_string_t, false>(tempPhf, &keysForPhf[0], keysForPhf.size(), 4, 80, 42);
    if (res != 0) {
        PHF::destroy(tempPhf);
        delete tempPhf;
        return false;
    }
    Clear();
    Phf = tempPhf;
    return true;
}



} // NJamSpell
