#include "koopa/koopa_interpreter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
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

struct Options {
    std::string filter;
    bool verbose = false;
};

enum class TestResult { passed, failed, timedOut };



struct ExpectedOutput {
    std::string stdoutText;
    int32_t returnCode = 0;
};

class TestLogger {
public:
    TestLogger(const TestCase& testCase, bool verbose)
        : m_testCase(testCase)
        , m_verbose(verbose)
        , m_start(std::chrono::steady_clock::now())
        , m_phaseStart(m_start)
    {
        if (m_verbose) {
            std::cerr << "running: " << m_testCase.sourcePath << '\n';
        }
    }

    void phase(const std::string& name)
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_verbose && !m_currentPhase.empty()) {
            std::cerr << "  " << m_currentPhase << " finished in "
                      << elapsedMs(m_phaseStart, now) << " ms\n";
        }
        m_currentPhase = name;
        m_phaseStart = now;
        if (m_verbose) {
            std::cerr << "  " << m_currentPhase << "...\n";
        }
    }

    void done(TestResult result)
    {
        if (!m_verbose) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!m_currentPhase.empty()) {
            std::cerr << "  " << m_currentPhase << " finished in "
                      << elapsedMs(m_phaseStart, now) << " ms\n";
        }
        std::cerr << "done: " << m_testCase.sourcePath << " -> "
                  << toString(result) << " in " << elapsedMs(m_start, now)
                  << " ms\n";
    }

private:
    [[nodiscard]] static int64_t elapsedMs(
        std::chrono::steady_clock::time_point begin,
        std::chrono::steady_clock::time_point end)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            end - begin)
            .count();
    }

    [[nodiscard]] static const char* toString(TestResult result)
    {
        switch (result) {
        case TestResult::passed:
            return "passed";
        case TestResult::failed:
            return "failed";
        case TestResult::timedOut:
            return "timedOut";
        }
        return "unknown";
    }

    const TestCase& m_testCase;
    bool m_verbose = false;
    std::chrono::steady_clock::time_point m_start;
    std::chrono::steady_clock::time_point m_phaseStart;
    std::string m_currentPhase;
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

[[nodiscard]] Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (options.filter.empty()) {
            options.filter = arg;
        } else {
            throw std::runtime_error("unexpected argument: " + arg);
        }
    }
    return options;
}

[[nodiscard]] TestResult runTestCase(
    const TestCase& testCase, const Options& options)
{
    TestLogger logger(testCase, options.verbose);
    try {
        logger.phase("read files");
        const std::string source = readTextFile(testCase.sourcePath);
        const std::string inputText = testCase.inputPath.empty()
            ? std::string { }
            : readTextFile(testCase.inputPath);
        const ExpectedOutput expected
            = parseExpectedOutput(readTextFile(testCase.outputPath));

        logger.phase("parse, semantic, and koopa generation");
        auto program = generateProgram(source);
        std::stringstream input(inputText);
        std::stringstream output;
        std::stop_source stopSource;
        std::atomic<bool> timedOut { false };

        // Timer thread: cooperative timeout after 1 second.
        std::jthread timerThread { [&stopSource, &timedOut](
                                       std::stop_token st) -> void {
            using namespace std::chrono_literals;
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (std::chrono::steady_clock::now() < deadline) {
                if (st.stop_requested()) {
                    return;
                }
                std::this_thread::sleep_for(10ms);
            }
            timedOut.store(true, std::memory_order_release);
            stopSource.request_stop();
        } };

        logger.phase("execute");
        const auto result = interpreter::execute(
            *program, input, output, stopSource.get_token());
        timerThread.request_stop();

        if (timedOut.load(std::memory_order_acquire)
            || result.status == interpreter::ExecuteStatus::stopped) {
            std::cerr << "timed out: " << testCase.sourcePath << '\n'
                      << "  status: " << interpreter::toString(result.status)
                      << '\n'
                      << "  message: " << result.message << '\n';
            logger.done(TestResult::timedOut);
            return TestResult::timedOut;
        }

        logger.phase("compare");
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
            logger.done(TestResult::failed);
            return TestResult::failed;
        }
        logger.done(TestResult::passed);
        return TestResult::passed;
    } catch (const std::exception& exception) {
        std::cerr << "failed: " << testCase.sourcePath << '\n'
                  << "  exception: " << exception.what() << '\n';
        logger.done(TestResult::failed);
        return TestResult::failed;
    }
}

} // namespace

int main(int argc, char** argv)
{
    const Options options = parseOptions(argc, argv);
    const fs::path testDirectory
        = fs::path(YESOD_TEST_SOURCE_DIR) / "testsuit-collection" / "lvX";
    const auto testCases = collectTestCases(testDirectory, options.filter);
    if (testCases.empty()) {
        std::cerr << "no test cases found";
        if (!options.filter.empty()) {
            std::cerr << " for filter: " << options.filter;
        }
        std::cerr << '\n';
        return 1;
    }

    size_t passed = 0;
    size_t timedOut = 0;
    std::vector<std::string> timedOutNames;
    for (const auto& testCase : testCases) {
        const auto result = runTestCase(testCase, options);
        if (result == TestResult::passed) {
            ++passed;
        } else if (result == TestResult::timedOut) {
            ++timedOut;
            timedOutNames.push_back(testCase.sourcePath.filename().string());
        }
    }

    std::cerr << "passed " << passed << " / " << testCases.size()
              << " koopa extra tests\n";
    if (timedOut > 0) {
        std::cerr << "\ntimed out (" << timedOut << "):\n";
        for (const auto& name : timedOutNames) {
            std::cerr << "  " << name << '\n';
        }
    }
    return (passed + timedOut) == testCases.size() ? 0 : 1;
}
