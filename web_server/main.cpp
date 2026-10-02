#include "jamspell/spell_corrector.hpp"
#include "contrib/httplib/httplib.h"
#include "contrib/nlohmann/json.hpp"
#include <cwctype>

std::string GetCandidates(const NJamSpell::TSpellCorrector& corrector
    , std::string const & text
)
{
    using namespace NJamSpell;

    std::wstring input = u8_to_w(text);
    //corrector.GetLangModel().GetTokenizer().FilterHyphen(input);
    
    wstr_view_t const orig_txt(input);
    context_t cntxt = corrector.Fix(input);

    nlohmann::json results;
    results["results"] = nlohmann::json::array();
    size_t origPos = 0;
    for (auto cit = cntxt.begin(), e = cntxt.end()
        ; cit < e 
        ; ++cit 
    )
    {
        if(cit -> orig_word.omitted() || cit -> candidates.empty())
        {
            continue;
        }

        cntxt_word_t & curr_word = *cit;
        nlohmann::json currentResult;
        token_info_t const & orig_token = curr_word.token;
        currentResult["pos_from"] = orig_token.ofs();
        currentResult["len"] = orig_token.size();
        currentResult["score"] = curr_word.orig_word.score;
        auto & cnds = (currentResult["candidates"] = nlohmann::json::array());
        for (cand_word_t const & cand_w : curr_word.candidates) 
        {
            nlohmann::json cnddt;
            std::size_t const pos_from = (cit - cand_w.concat.left) -> token.ofs();
            cnddt["pos_from"] = pos_from;
            auto const rtok = (cit + cand_w.concat.right) -> token;
            cnddt["len"] = rtok.ofs() + rtok.size() - pos_from;
            cnddt["score"] = cand_w.score;
            cnddt["str"] = corrector.ToU8(cand_w.str);
            cnds.emplace_back(cnddt);
        }
    }
    return results.dump(4);
}

std::string FixText(const NJamSpell::TSpellCorrector& corrector,
                    const std::string& text)
{
    using namespace NJamSpell;
    return w_to_u8(corrector.FixFragment(u8_to_w(text)));
}

int main(int argc, const char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " model.bin localhost 8080\n";
        return 42;
    }

    std::string modelFile = argv[1];
    std::string hostname = argv[2];
    int port = std::stoi(argv[3]);

    NJamSpell::TSpellCorrector corrector;
    std::cerr << "[info] loading model" << std::endl;
    if (!corrector.LoadLangModel(modelFile)) 
    {
        std::cerr << "[error] failed to load model" << std::endl;
        return 42;
    }

    static char const * const sContentType = "text/plain; charset=utf-8";
    httplib::Server srv;
    srv.Get("/fix", [&corrector](const httplib::Request& req, httplib::Response& resp) {
        resp.set_content(FixText(corrector, req.get_param_value("text")) + "\n", sContentType);
    });

    srv.Post("/fix", [&corrector](const httplib::Request& req, httplib::Response& resp) {
        resp.set_content(FixText(corrector, req.body) + "\n", sContentType);
    });

    srv.Get("/candidates", [&corrector](const httplib::Request& req, httplib::Response& resp) {
        resp.set_content(GetCandidates(corrector, req.get_param_value("text")) + "\n", sContentType);
    });

    srv.Post("/candidates", [&corrector](const httplib::Request& req, httplib::Response& resp) {
        resp.set_content(GetCandidates(corrector, req.body) + "\n", sContentType);
    });

    std::cerr << "[info] starting web server at " << hostname << ":" << port << std::endl;
    srv.listen(hostname.c_str(), port);
    return 0;
}
