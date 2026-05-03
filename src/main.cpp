#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "koopa/ast_to_koopa.h"
#include "koopa/mykoopa.h"

namespace {
using namespace yesod::koopa;

std::string readTextFile(const std::string& path)
{
    std::ifstream inputStream(path);
    if (!inputStream) {
        throw std::runtime_error("failed to open input file: " + path);
    }

    std::ostringstream buffer;
    buffer << inputStream.rdbuf();
    return buffer.str();
}

void printDiagnostics(const yesod::frontend::ParseOutput& parseOutput)
{
    for (const auto& diagnostic : parseOutput.m_diagnostics) {
        std::cerr << "parse error at offset " << diagnostic.m_offset << ": "
                  << diagnostic.m_message << std::endl;
    }
}

void printDiagnostics(const yesod::frontend::SemanticOutput& semanticOutput)
{
    for (const auto& diagnostic : semanticOutput.m_diagnostics) {
        std::cerr << "semantic error at offset " << diagnostic.m_offset << ": "
                  << diagnostic.m_message << std::endl;
    }
}

bool writeKoopaProgramToFile(const Program& program, const std::string& path)
{
    auto rawProgram = Program::dumpRaw(&program);
    koopa_program_t koopaProgram = nullptr;
    if (koopa_generate_raw_to_koopa(&rawProgram, &koopaProgram)
        != KOOPA_EC_SUCCESS) {
        return false;
    }

    const auto dumpResult = koopa_dump_to_file(koopaProgram, path.c_str());
    koopa_delete_program(koopaProgram);
    return dumpResult == KOOPA_EC_SUCCESS;
}

} // namespace

int main(int argc, const char* argv[])
{
    if (argc != 5 || std::string(argv[3]) != "-o") {
        std::cerr << "Usage: " << argv[0] << " <mode> <input> -o <output>"
                  << std::endl;
        return 1;
    }

    const std::string mode = argv[1];
    const std::string inputPath = argv[2];
    const std::string outputPath = argv[4];

    if (mode != "-koopa") {
        std::cerr << "unsupported mode: " << mode << std::endl;
        return 1;
    }

    try {
        const std::string source = readTextFile(inputPath);
        yesod::frontend::Parser parser(source);
        const auto parseOutput = parser.parse();
        if (!parseOutput.success()) {
            printDiagnostics(parseOutput);
            return 1;
        }

        yesod::frontend::SemanticAnalyzer semanticAnalyzer;
        const auto semanticOutput
            = semanticAnalyzer.analyze(*parseOutput.m_root);
        if (!semanticOutput.success()) {
            printDiagnostics(semanticOutput);
            return 1;
        }

        Generator generator;
        std::unique_ptr<Program> program(
            generator.generate(*semanticOutput.m_root));
        if (!writeKoopaProgramToFile(*program, outputPath)) {
            std::cerr << "failed to generate koopa IR" << std::endl;
            return 1;
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << std::endl;
        return 1;
    }

    return 0;
}