#include <iostream>

#include <jamspell/lang_model.hpp>
#include <jamspell/spell_corrector.hpp>

#include <filesystem>
#include <fstream>

using namespace NJamSpell;

void PrintUsage(const char** argv) {
    std::cerr << "Usage: " << argv[0] << " mode args" << std::endl;
    std::cerr << "    train alphabet.txt dataset.txt resultModel.bin  - train model" << std::endl;
    std::cerr << "    score model.bin - input sentences and get score" << std::endl;
    std::cerr << "    correct model.bin - input sentences and get corrected one" << std::endl;
    std::cerr << "    fix model.bin input.txt output.txt - automatically fix txt file" << std::endl;
}

int Train(const std::string& alphabetFile,
          const std::string& datasetFile,
          const std::string& resultModelFile)
{
    TLangModel model;
    model.Train(datasetFile, alphabetFile);
    model.Dump(resultModelFile);
    return 0;
}

int Score(const std::string& modelFile) {
    TLangModel model;
    std::cerr << "[info] loading model" << std::endl;
    if (!model.Load(modelFile)) {
        std::cerr << "[error] failed to load model" << std::endl;
        return 42;
    }
    std::cerr << "[info] loaded" << std::endl;
    std::cerr << ">> ";
    utf8_to_wide_t utf8_to_wide;
    for (std::string line; std::getline(std::cin, line);) {
        std::wstring wtext = utf8_to_wide(line);
        std::cerr << model.Score(wtext) << "\n";
        std::cerr << ">> ";
    }
    return 0;
}

int Fix(const std::string& modelFile,
        const std::string& inputFile,
        const std::string& outFile)
{
    TSpellCorrector corrector;
    std::cerr << "[info] loading model" << std::endl;
    if (!corrector.LoadLangModel(modelFile)) 
    {
        std::cerr << "[error] failed to load model" << std::endl;
        return 42;
    }
    std::cerr << "[info] loaded" << std::endl;
    
    std::size_t const file_sz = std::filesystem::file_size(inputFile);
    std::ifstream in(inputFile, std::ios::binary);
    if(!in)
    {
        return 84;
    }

    std::ofstream out(outFile, std::ios::binary);
    if(!out)
    {
        return 96;
    }

    std::size_t lcnt{0}, bytes_cnt{0};
    std::string l;        
    utf8_to_wide_t utf8_to_wide;
    wide_to_utf8_t wide_to_utf8;
    uint64_t const startTime = GetCurrentTimeMs();
    uint64_t lastTime{0};

    while (!in.eof())
    {            
        std::getline(in, l); 
        ++lcnt;
        if(!l.empty())
        {            
            bytes_cnt += (1 + l.size());
            std::wstring const & text = utf8_to_wide(l);
            std::wstring const & result = corrector.FixFragment(text);
            out << wide_to_utf8(result) << '\n' << std::flush;
            
            if (lcnt % 1000 == 0)
            {
                uint64_t currTime = GetCurrentTimeMs();
                if( currTime - lastTime > 5000)
                {
                    std::cerr << "[info] procesed " << (100.0 * bytes_cnt / file_sz)  
                        << "%\n";
                    lastTime = currTime;
                }
            }
        }        
    }
    uint64_t const finishTime = GetCurrentTimeMs();
    std::cerr << "[info] process time: " << finishTime - startTime << "ms" << std::endl;
    return 0;
}

int Correct(const std::string& modelFile) {
    TSpellCorrector corrector;
    std::cerr << "[info] loading model" << std::endl;
    if (!corrector.LoadLangModel(modelFile)) {
        std::cerr << "[error] failed to load model" << std::endl;
        return 42;
    }
    std::cerr << "[info] loaded" << std::endl;
    std::cerr << ">> ";
    utf8_to_wide_t utf8_to_wide;
    wide_to_utf8_t wide_to_utf8;
    for (std::string line; std::getline(std::cin, line);) 
    {
        std::wstring wtext = utf8_to_wide(line);
        std::wstring result = corrector.FixFragment(wtext);
        std::cerr << wide_to_utf8(result) << "\n";
        std::cerr << ">> ";
    }
    return 0;
}

int main(int argc, const char** argv) {
    if (argc < 2) {
        PrintUsage(argv);
        return 42;
    }
    std::string mode = argv[1];
    if (mode == "train") {
        if (argc < 5) {
            PrintUsage(argv);
            return 42;
        }
        std::string alphabetFile = argv[2];
        std::string datasetFile = argv[3];
        std::string resultModelFile = argv[4];
        return Train(alphabetFile, datasetFile, resultModelFile);
    } else if (mode == "score") {
        if (argc < 3) {
            PrintUsage(argv);
            return 42;
        }
        std::string modelFile = argv[2];
        return Score(modelFile);
    } else if (mode == "correct") {
        if (argc < 3) {
            PrintUsage(argv);
            return 42;
        }
        std::string modelFile = argv[2];
        return Correct(modelFile);
    } else if (mode == "fix") {
        if (argc < 5) {
            PrintUsage(argv);
            return 42;
        }
        std::string modelFile = argv[2];
        std::string inFile = argv[3];
        std::string outFile = argv[4];
        return Fix(modelFile, inFile, outFile);
    }

    PrintUsage(argv);
    return 42;
}
