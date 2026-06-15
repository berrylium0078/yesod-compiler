#include "koopa/koopa_interpreter.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "koopa/ast_to_koopa.h"

namespace {

namespace fs = std::filesystem;
namespace frontend = yesod::frontend;
namespace koopa = yesod::koopa;
namespace interpreter = yesod::test_support::koopa::interpreter;

#ifndef YESOD_TEST_SOURCE_DIR
#define YESOD_TEST_SOURCE_DIR "."
#endif

struct TestCase {
    fs::path sourcePath;
    fs::path inputPath;
    fs::path outputPath;
};

struct ExpectedOutput {
    std::string stdoutText;
    int32_t returnCode = 0;
};

[[nodiscard]] std::string readTextFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

[[nodiscard]] ExpectedOutput parseExpectedOutput(const std::string& text)
{
    std::string normalized = text;
    if (!normalized.empty() && normalized.back() == '\n') {
        normalized.pop_back();
    }
    if (!normalized.empty() && normalized.back() == '\r') {
        normalized.pop_back();
    }

    const size_t lineBreak = normalized.rfind('\n');
    std::string returnLine;
    std::string stdoutText;
    if (lineBreak == std::string::npos) {
        returnLine = normalized;
    } else {
        stdoutText = normalized.substr(0, lineBreak + 1);
        returnLine = normalized.substr(lineBreak + 1);
        if (!returnLine.empty() && returnLine.back() == '\r') {
            returnLine.pop_back();
        }
    }

    return ExpectedOutput {
        .stdoutText = stdoutText,
        .returnCode = static_cast<int32_t>(std::stoi(returnLine)),
    };
}

[[nodiscard]] std::vector<TestCase> collectTestCases(
    const fs::path& directory, const std::string& filter)
{
    std::vector<TestCase> testCases;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".c") {
            continue;
        }
        const std::string pathText = entry.path().string();
        if (!filter.empty() && pathText.find(filter) == std::string::npos) {
            continue;
        }
        fs::path outputPath = entry.path();
        outputPath.replace_extension(".out");
        if (!fs::exists(outputPath)) {
            std::cerr << "missing .out for " << entry.path() << '\n';
            continue;
        }
        fs::path inputPath = entry.path();
        inputPath.replace_extension(".in");
        testCases.push_back(TestCase {
            .sourcePath = entry.path(),
            .inputPath = fs::exists(inputPath) ? inputPath : fs::path { },
            .outputPath = outputPath,
        });
    }
    std::sort(testCases.begin(), testCases.end(),
        [](const TestCase& lhs, const TestCase& rhs) -> bool {
            return lhs.sourcePath < rhs.sourcePath;
        });
    return testCases;
}

[[nodiscard]] std::unique_ptr<yesod::koopa::ir::Program> generateProgram(
    const std::string& source)
{
    frontend::Parser parser(
        frontend::prependBuiltinFunctionDeclarations(source));
    auto parseOutput = parser.parse();
    if (!parseOutput.success()) {
        throw std::runtime_error("parse failed");
    }

    frontend::SemanticAnalyzer semanticAnalyzer;
    auto semanticOutput = semanticAnalyzer.analyze(
        std::move(parseOutput.m_ast), parseOutput.m_root.ref());
    if (!semanticOutput.success()) {
        throw std::runtime_error("semantic analysis failed");
    }

    koopa::Generator generator;
    return generator.generateIr(
        semanticOutput.m_ast, semanticOutput.m_root, semanticOutput.m_info);
}

[[nodiscard]] int32_t normalizeReturnCode(int32_t value)
{
    return value & 0xff;
}

[[nodiscard]] bool outputMatches(
    const std::string& actual, const std::string& expected)
{
    return actual == expected || actual + '\n' == expected;
}

[[nodiscard]] bool runTestCase(const TestCase& testCase)
{
    try {
        const std::string source = readTextFile(testCase.sourcePath);
        const std::string inputText = testCase.inputPath.empty()
            ? std::string { }
            : readTextFile(testCase.inputPath);
        const ExpectedOutput expected
            = parseExpectedOutput(readTextFile(testCase.outputPath));

        auto program = generateProgram(source);
        std::stringstream input(inputText);
        std::stringstream output;
        std::stop_source stopSource;
        const auto result = interpreter::execute(
            *program, input, output, stopSource.get_token());

        const int32_t actualReturn = normalizeReturnCode(result.returnValue);
        const int32_t expectedReturn = normalizeReturnCode(expected.returnCode);
        const std::string actualStdout = output.str();
        if (result.status != interpreter::ExecuteStatus::normal
            || !outputMatches(actualStdout, expected.stdoutText)
            || actualReturn != expectedReturn) {
            std::cerr << "failed: " << testCase.sourcePath << '\n'
                      << "  status: " << interpreter::toString(result.status)
                      << '\n'
                      << "  message: " << result.message << '\n'
                      << "  expected return: " << expectedReturn << '\n'
                      << "  actual return: " << actualReturn << '\n'
                      << "  expected stdout size: "
                      << expected.stdoutText.size() << '\n'
                      << "  actual stdout size: " << actualStdout.size()
                      << '\n';
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        std::cerr << "failed: " << testCase.sourcePath << '\n'
                  << "  exception: " << exception.what() << '\n';
        return false;
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::string filter = argc >= 2 ? argv[1] : std::string { };
    const fs::path testDirectory
        = fs::path(YESOD_TEST_SOURCE_DIR) / "testsuit" / "lvX";
    const auto testCases = collectTestCases(testDirectory, filter);
    if (testCases.empty()) {
        std::cerr << "no test cases found";
        if (!filter.empty()) {
            std::cerr << " for filter: " << filter;
        }
        std::cerr << '\n';
        return 1;
    }

    size_t passed = 0;
    for (const auto& testCase : testCases) {
        if (runTestCase(testCase)) {
            ++passed;
        }
    }

    std::cerr << "passed " << passed << " / " << testCases.size()
              << " koopa extra tests\n";
    return passed == testCases.size() ? 0 : 1;
}
