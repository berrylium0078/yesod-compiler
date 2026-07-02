#include <cstdlib>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

#ifndef YESOD_TEST_SOURCE_DIR
#define YESOD_TEST_SOURCE_DIR "."
#endif

struct TestCase {
    fs::path sourcePath;
    fs::path inputPath;
    fs::path outputPath;
};

struct Options {
    fs::path testDirectory
        = fs::path(YESOD_TEST_SOURCE_DIR) / "testsuit-collection" / "lvX";
    std::string filter;
    bool verbose = false;
    int32_t timeoutSeconds = 30;
};

struct ExpectedOutput {
    std::string stdoutText;
    int32_t returnCode = 0;
};

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "compiler_llvm_test failure: " << message << std::endl;
    std::exit(1);
}

void require(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

std::string quoteShell(std::string_view text)
{
    std::string quoted;
    quoted.reserve(text.size() + 2);
    quoted += '\'';
    for (const char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += '\'';
    return quoted;
}

std::string readTextFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "failed to open file for reading: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void writeTextFile(const fs::path& path, const std::string& contents)
{
    std::ofstream output(path, std::ios::binary);
    require(output.good(), "failed to open file for writing: " + path.string());
    output << contents;
    require(output.good(), "failed to write file: " + path.string());
}

struct TempFile {
    explicit TempFile(const std::string& suffix)
    {
        const char* tmpDir = std::getenv("TMPDIR");
        if (!tmpDir || tmpDir[0] == '\0') {
            tmpDir = "/tmp";
        }
        std::string pattern
            = std::string(tmpDir) + "/compiler_llvm_test_XXXXXX" + suffix;
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');

        const int fileDescriptor
            = ::mkstemps(buffer.data(), static_cast<int>(suffix.size()));
        require(fileDescriptor != -1,
            "failed to create temporary file in " + std::string(tmpDir));
        ::close(fileDescriptor);
        m_path = buffer.data();
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    ~TempFile()
    {
        if (!m_path.empty()) {
            std::error_code errorCode;
            fs::remove(m_path, errorCode);
        }
    }

    [[nodiscard]] const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

std::string compilerPath()
{
    std::vector<char> buffer(4096, '\0');
    const ssize_t length
        = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    require(length != -1, "failed to resolve test executable path");

    const fs::path testPath(
        std::string(buffer.data(), static_cast<size_t>(length)));
    const auto localCompiler = testPath.parent_path() / "compiler";
    if (fs::exists(localCompiler)) {
        return localCompiler.string();
    }

    const auto parentCompiler
        = testPath.parent_path().parent_path() / "compiler";
    require(fs::exists(parentCompiler),
        "expected compiler binary next to compiler_llvm_test or in parent "
        "build directory at "
            + parentCompiler.string());
    return parentCompiler.string();
}

void runCheckedCommand(const std::string& command, const std::string& purpose)
{
    const int status = std::system(command.c_str());
    require(status != -1, "failed to start command for " + purpose);
    require(WIFEXITED(status),
        "command did not exit normally for " + purpose + ": " + command);
    require(WEXITSTATUS(status) == 0,
        purpose + " failed with exit code "
            + std::to_string(WEXITSTATUS(status)) + ": " + command);
}

int runCommandAllowingProgramReturn(
    const std::string& command, const std::string& purpose)
{
    const int status = std::system(command.c_str());
    require(status != -1, "failed to start command for " + purpose);
    require(WIFEXITED(status),
        "command did not exit normally for " + purpose + ": " + command);
    return WEXITSTATUS(status);
}

ExpectedOutput parseExpectedOutput(const std::string& text)
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

bool outputMatches(const std::string& actual, const std::string& expected)
{
    return actual == expected || actual + '\n' == expected;
}

int32_t normalizeReturnCode(int32_t value) { return value & 0xff; }

bool isSysyRuntimeDeclaration(const std::string& line)
{
    static constexpr std::string_view PREFIX = "declare ";
    if (!line.starts_with(PREFIX)) {
        return false;
    }
    return line.find("@getint(") != std::string::npos
        || line.find("@getch(") != std::string::npos
        || line.find("@getarray(") != std::string::npos
        || line.find("@putint(") != std::string::npos
        || line.find("@putch(") != std::string::npos
        || line.find("@putarray(") != std::string::npos
        || line.find("@starttime(") != std::string::npos
        || line.find("@stoptime(") != std::string::npos;
}

std::string makeLliRunnableLlvm(const std::string& llvmText)
{
    std::istringstream input(llvmText);
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        if (isSysyRuntimeDeclaration(line)) {
            continue;
        }
        output << line << '\n';
    }

    output << R"(
@.yesod.scan_int = private unnamed_addr constant [3 x i8] c"%d\00"
@.yesod.putint_fmt = private unnamed_addr constant [3 x i8] c"%d\00"
@.yesod.putch_fmt = private unnamed_addr constant [3 x i8] c"%c\00"

declare i32 @scanf(ptr, ...)
declare i32 @printf(ptr, ...)
declare i32 @getchar()

define i32 @getint() {
entry:
  %slot = alloca i32
  %fmt = getelementptr inbounds [3 x i8], ptr @.yesod.scan_int, i32 0, i32 0
  call i32 (ptr, ...) @scanf(ptr %fmt, ptr %slot)
  %value = load i32, ptr %slot
  ret i32 %value
}

define i32 @getch() {
entry:
  %value = call i32 @getchar()
  ret i32 %value
}

define i32 @getarray(ptr %array) {
entry:
  %count = call i32 @getint()
  br label %cond
cond:
  %i = phi i32 [ 0, %entry ], [ %next, %body ]
  %keep = icmp slt i32 %i, %count
  br i1 %keep, label %body, label %end
body:
  %value = call i32 @getint()
  %element = getelementptr i32, ptr %array, i32 %i
  store i32 %value, ptr %element
  %next = add i32 %i, 1
  br label %cond
end:
  ret i32 %count
}

define void @putint(i32 %x) {
entry:
  %fmt = getelementptr inbounds [3 x i8], ptr @.yesod.putint_fmt, i32 0, i32 0
  call i32 (ptr, ...) @printf(ptr %fmt, i32 %x)
  ret void
}

define void @putch(i32 %x) {
entry:
  %fmt = getelementptr inbounds [3 x i8], ptr @.yesod.putch_fmt, i32 0, i32 0
  call i32 (ptr, ...) @printf(ptr %fmt, i32 %x)
  ret void
}

define void @putarray(i32 %count, ptr %array) {
entry:
  call void @putint(i32 %count)
  call void @putch(i32 58)
  br label %cond
cond:
  %i = phi i32 [ 0, %entry ], [ %next, %body ]
  %keep = icmp slt i32 %i, %count
  br i1 %keep, label %body, label %end
body:
  call void @putch(i32 32)
  %element = getelementptr i32, ptr %array, i32 %i
  %value = load i32, ptr %element
  call void @putint(i32 %value)
  %next = add i32 %i, 1
  br label %cond
end:
  call void @putch(i32 10)
  ret void
}

define void @starttime() {
entry:
  ret void
}

define void @stoptime() {
entry:
  ret void
}
)";
    return output.str();
}

std::vector<TestCase> collectTestCases(
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

Options parseOptions(int argc, char** argv)
{
    Options options;
    bool directorySet = false;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (arg == "--timeout-seconds") {
            require(index + 1 < argc, "--timeout-seconds requires an argument");
            options.timeoutSeconds = std::stoi(argv[++index]);
        } else if (!directorySet && fs::is_directory(arg)) {
            options.testDirectory = arg;
            directorySet = true;
        } else if (options.filter.empty()) {
            options.filter = arg;
        } else {
            fail("unexpected argument: " + arg);
        }
    }
    return options;
}

bool runTestCase(const TestCase& testCase, const Options& options,
    const std::string& compiler)
{
    if (options.verbose) {
        std::cerr << "running: " << testCase.sourcePath << '\n';
    }

    TempFile rawLlvmFile(".ll");
    TempFile runnableLlvmFile(".ll");
    TempFile executableFile(".out");
    TempFile outputFile(".txt");
    const std::string inputRedirect = testCase.inputPath.empty()
        ? " < /dev/null"
        : " < " + quoteShell(testCase.inputPath.string());

    try {
        runCheckedCommand("timeout " + std::to_string(options.timeoutSeconds)
                + "s " + quoteShell(compiler) + " -llvm "
                + quoteShell(testCase.sourcePath.string()) + " -o "
                + quoteShell(rawLlvmFile.path().string()),
            "compiling SysY source to LLVM IR");

        writeTextFile(runnableLlvmFile.path(),
            makeLliRunnableLlvm(readTextFile(rawLlvmFile.path())));

        runCheckedCommand("timeout " + std::to_string(options.timeoutSeconds)
                + "s clang -fsanitize=address -g "
                + quoteShell(runnableLlvmFile.path().string()) + " -o "
                + quoteShell(executableFile.path().string()),
            "compiling LLVM IR with AddressSanitizer");

        const int actualReturn
            = runCommandAllowingProgramReturn("ASAN_OPTIONS=detect_leaks=1 "
                                              "HOME=/tmp timeout "
                    + std::to_string(options.timeoutSeconds) + "s "
                    + quoteShell(executableFile.path().string()) + inputRedirect
                    + " > " + quoteShell(outputFile.path().string()),
                "executing AddressSanitizer-built LLVM IR");

        const auto expected
            = parseExpectedOutput(readTextFile(testCase.outputPath));
        const std::string actualStdout = readTextFile(outputFile.path());
        const int32_t expectedReturn = normalizeReturnCode(expected.returnCode);
        if (!outputMatches(actualStdout, expected.stdoutText)
            || normalizeReturnCode(actualReturn) != expectedReturn) {
            std::cerr << "failed: " << testCase.sourcePath << '\n'
                      << "  expected return: " << expectedReturn << '\n'
                      << "  actual return: "
                      << normalizeReturnCode(actualReturn) << '\n'
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

void testLlvmModeUsesNativeMintLowering(const std::string& compiler)
{
    TempFile sourceFile(".sy");
    TempFile llvmFile(".ll");
    writeTextFile(sourceFile.path(),
        "int id(int x){return x;} int main(){mint a = mint(id(6)); mint b = "
        "mint(id(7)); return int(a * b);}");

    runCheckedCommand("timeout 10s " + quoteShell(compiler) + " -llvm "
            + quoteShell(sourceFile.path().string()) + " -o "
            + quoteShell(llvmFile.path().string()),
        "compiling SysY source to LLVM IR");

    const std::string llvmText = readTextFile(llvmFile.path());
    require(llvmText.find("@__yesod_mint_") == std::string::npos,
        "-llvm output should not include old mint runtime helpers");
    require(llvmText.find(" mul i32 ") != std::string::npos,
        "-llvm output should lower native mint multiplication as an i32 mul");
    require(llvmText.find("@main") != std::string::npos,
        "-llvm output should still include the lowered main function");
}

} // namespace

int main(int argc, char** argv)
{
    const Options options = parseOptions(argc, argv);
    const auto testCases
        = collectTestCases(options.testDirectory, options.filter);
    if (testCases.empty()) {
        std::cerr << "no test cases found in " << options.testDirectory;
        if (!options.filter.empty()) {
            std::cerr << " for filter: " << options.filter;
        }
        std::cerr << '\n';
        return 1;
    }

    const std::string compiler = compilerPath();
    testLlvmModeUsesNativeMintLowering(compiler);

    size_t passed = 0;
    for (const auto& testCase : testCases) {
        if (runTestCase(testCase, options, compiler)) {
            ++passed;
        }
    }
    std::cerr << "passed " << passed << " / " << testCases.size()
              << " LLVM AddressSanitizer tests\n";
    return passed == testCases.size() ? 0 : 1;
}
