#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <algorithm>
#include <vector>

#include "backend/riscv.h"
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

struct DiagInfo {
    std::size_t m_offset;
    std::string m_message;
    std::string kind; // "parse" or "semantic"
};

static std::vector<std::size_t> buildLineStarts(const std::string& src)
{
    std::vector<std::size_t> starts;
    starts.reserve(128);
    starts.emplace_back(0);
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '\n') {
            starts.emplace_back(i + 1);
        }
    }
    return starts;
}

static void printDiagnosticsAggregate(const std::string& inputPath,
                                      const std::string& source,
                                      std::vector<DiagInfo>& diags)
{
    if (diags.empty()) return;

    std::sort(diags.begin(), diags.end(),
              [](const DiagInfo& a, const DiagInfo& b) {
                  return a.m_offset < b.m_offset;
              });

    const auto lineStarts = buildLineStarts(source);

    for (const auto& d : diags) {
        // map offset -> line/col
        const std::size_t offset = d.m_offset;
        auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), offset);
        std::size_t line = 0;
        if (it == lineStarts.begin()) {
            line = 0;
        } else {
            line = static_cast<std::size_t>(std::distance(lineStarts.begin(), it) - 1);
        }
        const std::size_t col = offset - lineStarts[line] + 1;

        std::cerr << d.kind << " error at " << inputPath << ":"
                  << (line + 1) << ":" << col << " (offset " << offset << "): "
                  << d.m_message << std::endl;

        // print the source line and a caret
        const std::size_t lineBegin = lineStarts[line];
        const std::size_t lineEnd = (line + 1 < lineStarts.size()) ? (lineStarts[line + 1] - 1)
                                                                       : source.size();
        const std::string lineText = source.substr(lineBegin, lineEnd - lineBegin);
        std::cerr << lineText << std::endl;

        std::string caret;
        // naive caret alignment; tabs are counted as single chars here
        const std::size_t caretPos = (col > 0) ? (col - 1) : 0;
        caret.assign(caretPos, ' ');
        caret += '^';
        std::cerr << caret << std::endl;
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

bool writeRiscvProgramToFile(const Program& program, const std::string& path)
{
    std::ofstream outputStream(path);
    if (!outputStream) {
        return false;
    }

    yesod::backend::RiscvGenerator generator;
    generator.generate(program, outputStream);
    return outputStream.good();
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

    if (mode != "-koopa" && mode != "-riscv") {
        std::cerr << "unsupported mode: " << mode << std::endl;
        return 1;
    }

    try {
        const std::string source = readTextFile(inputPath);
        yesod::frontend::Parser parser(
            yesod::frontend::prependBuiltinFunctionDeclarations(source));
        auto parseOutput = parser.parse();
        if (!parseOutput.success()) {
            std::vector<DiagInfo> diags;
            diags.reserve(parseOutput.m_diagnostics.size());
            for (const auto& d : parseOutput.m_diagnostics) {
                diags.push_back(DiagInfo{static_cast<std::size_t>(d.m_offset), d.m_message, std::string("parse")});
            }
            printDiagnosticsAggregate(inputPath, source, diags);
            return 1;
        }

        yesod::frontend::SemanticAnalyzer semanticAnalyzer;
        auto semanticOutput = semanticAnalyzer.analyze(
            std::move(parseOutput.m_ast), parseOutput.m_root);
        if (!semanticOutput.success()) {
            std::vector<DiagInfo> diags;
            // include any parse diagnostics (if present) and semantic diagnostics
            diags.reserve(parseOutput.m_diagnostics.size() + semanticOutput.m_diagnostics.size());
            for (const auto& d : parseOutput.m_diagnostics) {
                diags.push_back(DiagInfo{static_cast<std::size_t>(d.m_offset), d.m_message, std::string("parse")});
            }
            for (const auto& d : semanticOutput.m_diagnostics) {
                diags.push_back(DiagInfo{static_cast<std::size_t>(d.m_offset), d.m_message, std::string("semantic")});
            }
            printDiagnosticsAggregate(inputPath, source, diags);
            return 1;
        }

        Generator generator;
        std::unique_ptr<Program> program(
            generator.generate(semanticOutput.m_ast, semanticOutput.m_root,
                semanticOutput.m_info));
        if (mode == "-koopa") {
            if (!writeKoopaProgramToFile(*program, outputPath)) {
                std::cerr << "failed to generate koopa IR" << std::endl;
                return 1;
            }
        } else {
            if (!writeRiscvProgramToFile(*program, outputPath)) {
                std::cerr << "failed to generate RISC-V assembly" << std::endl;
                return 1;
            }
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << std::endl;
        return 1;
    }

    return 0;
}