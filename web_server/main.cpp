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
    corrector.GetLangModel().GetTokenizer().FilterHyphen(input);
    
    wstr_view_t const orig_txt(input);

    context_t cntxt;
    corrector.GetLangModel().Text2Words(orig_txt, cntxt);

    nlohmann::json results;
    results["results"] = nlohmann::json::array();

    size_t origPos = 0;
    for (auto orig_it = cntxt.begin(), e = cntxt.end()
        ; orig_it < e 
        ; ++orig_it // see the last line marked with !!!. We omit sent end token
                    // and proceed to next sentence begin
    )
    {
        context_range_t curr_sent_ctxt = TSpellCorrector::GetNextSent(orig_it, e);

        std::size_t j = 0;
        for ( auto al_word_it = curr_sent_ctxt.begin()
            ; al_word_it != curr_sent_ctxt.end()
            ; ++j, ++al_word_it
        ) 
        {
            if (!al_word_it -> good())
            {
                continue;
            }

            cand_word_t & curr_word = *al_word_it;
            candidates_t candidates {corrector.GetCandidates(curr_sent_ctxt, j)};
            if (!candidates.empty()) 
            {
                cand_word_t & top_w = candidates.front();
                if(curr_word.score >= top_w.score || curr_word.id == top_w.id)
                {
                    continue;
                }
            }
           
            nlohmann::json currentResult;
            
            token_info_t const & orig_token = al_word_it -> token;
            currentResult["pos_from"] = orig_token.ofs();
            currentResult["len"] = orig_token.size();
            currentResult["candidates"] = nlohmann::json::array();

            std::size_t const candidatesSize = std::min(candidates.size(), std::size_t(7));
            for (std::size_t k = 0; k < candidatesSize; ++k) 
            {
                currentResult["candidates"].emplace_back(w_to_u8(
                    FromAlphabet(corrector.GetLangModel().GetTokenizer().GetAlphabet()
                        , candidates[k].str
                    )
                ));
            }

            results["results"].push_back(currentResult);
        }
        orig_it = curr_sent_ctxt.end();  // !!!

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
