// Stress test for polynomial operations with n = 10^5.
// Tests memory safety (AddressSanitizer) first, then runtime (O2) if ASAN passes.
// Disabled by default — enable with: ctest -R poly_stress -V
//
// Usage (manual):
//   build/test/poly_stress_test

#include <cstdint>
#include <cstdlib>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr int32_t POLY_N = 100000;
constexpr int32_t MINT_MOD = 998244353;
constexpr int32_t TIMEOUT_ASAN = 180; // 3 minutes for ASAN build+run
constexpr int32_t TIMEOUT_O2 = 120;   // 2 minutes for O2 build+run

// Test targets
struct PolyTestCase {
    const char* name;
    const char* sourceFile; // relative to testsuit-collection/poly/
};

constexpr std::array<PolyTestCase, 3> TEST_CASES = { {
    { "poly_inv", "poly_inv.c" },
    { "poly_exp_newton", "poly_exp_newton.c" },
    { "poly_exp_dc", "poly_exp_dc.c" },
} };

// ── Helpers ─────────────────────────────────────────────────────────────

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "poly_stress_test failure: " << message << std::endl;
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
    require(
        input.good(), "failed to open file for reading: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void writeTextFile(const fs::path& path, const std::string& contents)
{
    std::ofstream output(path, std::ios::binary);
    require(
        output.good(), "failed to open file for writing: " + path.string());
    output << contents;
    require(output.good(), "failed to write file: " + path.string());
}

struct TempFile {
    explicit TempFile(const std::string& suffix)
    {
        std::string pattern = "/tmp/poly_stress_XXXXXX" + suffix;
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const int fd = ::mkstemps(
            buffer.data(), static_cast<int>(suffix.size()));
        require(fd != -1, "failed to create temporary file under /tmp");
        ::close(fd);
        m_path = buffer.data();
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    ~TempFile()
    {
        if (!m_path.empty()) {
            std::error_code ec;
            fs::remove(m_path, ec);
        }
    }

    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

// ── LLVM IR helpers (make generated IR self-contained with I/O) ─────

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

std::string makeLlvmRunnable(const std::string& llvmText)
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

// ─── Input generation ──────────────────────────────────────────────────

std::string generateInput(int32_t n)
{
    std::ostringstream oss;
    oss << n << "\n";

    // Deterministic LCG for reproducibility.
    uint64_t state = 123456789;
    for (int32_t i = 0; i < n; ++i) {
        if (i > 0) {
            oss << " ";
        }
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        oss << static_cast<int32_t>(state % MINT_MOD);
    }
    oss << "\n";
    return oss.str();
}

// ─── Compiler path discovery ───────────────────────────────────────────

std::string compilerPath()
{
    std::vector<char> buffer(4096, '\0');
    const ssize_t len
        = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    require(len != -1, "failed to resolve test executable path");

    const fs::path testPath(
        std::string(buffer.data(), static_cast<size_t>(len)));
    const auto localCompiler = testPath.parent_path() / "compiler";
    if (fs::exists(localCompiler)) {
        return localCompiler.string();
    }
    const auto parentCompiler
        = testPath.parent_path().parent_path() / "compiler";
    require(fs::exists(parentCompiler),
        "expected compiler binary next to poly_stress_test or in parent "
        "build directory at "
            + parentCompiler.string());
    return parentCompiler.string();
}

// ─── Command execution ─────────────────────────────────────────────────

void runCheckedCommand(
    const std::string& command, const std::string& purpose)
{
    const int status = std::system(command.c_str());
    require(status != -1, "failed to start command for " + purpose);
    require(WIFEXITED(status),
        "command did not exit normally for " + purpose + ": " + command);
    require(WEXITSTATUS(status) == 0,
        purpose + " failed with exit code "
            + std::to_string(WEXITSTATUS(status)) + ": " + command);
}

// Returns exit code without failing on non-zero.
int runCommandGetStatus(
    const std::string& command, const std::string& purpose)
{
    const int status = std::system(command.c_str());
    require(status != -1, "failed to start command for " + purpose);
    require(WIFEXITED(status),
        "command did not exit normally for " + purpose + ": " + command);
    return WEXITSTATUS(status);
}

// ─── Source directory ──────────────────────────────────────────────────

#ifndef YESOD_TEST_SOURCE_DIR
#define YESOD_TEST_SOURCE_DIR "."
#endif

const fs::path polySourceDir()
{
    return fs::path(YESOD_TEST_SOURCE_DIR) / "testsuit-collection" / "poly";
}

// ─── Test runner ───────────────────────────────────────────────────────

struct StressResult {
    bool asanPassed = false;
    bool o2Passed = false;
    int64_t asanBuildMs = 0;
    int64_t o2BuildMs = 0;
    int64_t asanRunMs = 0;
    int64_t o2RunMs = 0;
};

StressResult runStressTest(
    const PolyTestCase& testCase, const std::string& compiler)
{
    StressResult result;

    const fs::path sourcePath = polySourceDir() / testCase.sourceFile;
    require(fs::exists(sourcePath),
        "source file not found: " + sourcePath.string());

    std::cerr << "\n=== " << testCase.name << " (n = " << POLY_N << ") ===\n";

    // Generate input
    const std::string inputData = generateInput(POLY_N);

    TempFile inputFile(".in");
    TempFile rawLlvmFile(".ll");
    TempFile runnableLlvmFile(".ll");
    TempFile asanBinary(".out");
    TempFile o2Binary(".out");
    TempFile outputFile(".txt");
    const std::string inputRedirect
        = " < " + quoteShell(inputFile.path().string());

    writeTextFile(inputFile.path(), inputData);

    // ── Phase 1: Compile SysY → LLVM IR ────────────────────────────
    {
        const auto t0 = std::chrono::steady_clock::now();
        const std::string purpose1
            = std::string("compiling ") + testCase.name + " to LLVM IR";
        runCheckedCommand(
            "timeout " + std::to_string(TIMEOUT_ASAN) + "s "
                + quoteShell(compiler) + " -llvm "
                + quoteShell(sourcePath.string()) + " -o "
                + quoteShell(rawLlvmFile.path().string()),
            purpose1);

        // Make the LLVM IR self-contained by replacing SysY I/O
        // declarations with inline C implementations.
        writeTextFile(runnableLlvmFile.path(),
            makeLlvmRunnable(readTextFile(rawLlvmFile.path())));

        const auto t1 = std::chrono::steady_clock::now();
        result.asanBuildMs
            = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0)
                  .count();
    }

    // ── Phase 2: Build with AddressSanitizer & run ──────────────────
    {
        const auto t0 = std::chrono::steady_clock::now();
        const std::string purpose2 = std::string("compiling ") + testCase.name
            + " LLVM IR with AddressSanitizer";
        runCheckedCommand(
            "timeout " + std::to_string(TIMEOUT_ASAN) + "s clang "
            "-fsanitize=address -g "
            + quoteShell(runnableLlvmFile.path().string()) + " -o "
            + quoteShell(asanBinary.path().string()),
            purpose2);
        const auto t1 = std::chrono::steady_clock::now();
        result.asanBuildMs
            += std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0)
                   .count();

        const auto t2 = std::chrono::steady_clock::now();
        const std::string purpose3
            = std::string("running ") + testCase.name + " under AddressSanitizer";
        const int asanExit
            = runCommandGetStatus("ASAN_OPTIONS=detect_leaks=1 "
                                  "HOME=/tmp timeout "
                    + std::to_string(TIMEOUT_ASAN) + "s "
                    + quoteShell(asanBinary.path().string()) + inputRedirect
                    + " > " + quoteShell(outputFile.path().string()) + " 2>&1",
                purpose3);
        const auto t3 = std::chrono::steady_clock::now();
        result.asanRunMs
            = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2)
                  .count();

        if (asanExit != 0) {
            const std::string asanOutput = readTextFile(outputFile.path());
            std::cerr << "ASAN FAILED (exit " << asanExit << "):\n"
                      << asanOutput << "\n";
            result.asanPassed = false;
            return result;
        }
        result.asanPassed = true;
        std::cerr << "  ASAN: passed (" << result.asanRunMs << " ms run)\n";
    }

    // ── Phase 3: Build with -O2 & benchmark runtime ─────────────────
    {
        const auto t0 = std::chrono::steady_clock::now();
        const std::string purpose4
            = std::string("compiling ") + testCase.name + " LLVM IR with -O2";
        runCheckedCommand(
            "timeout " + std::to_string(TIMEOUT_O2) + "s clang -O2 "
            + quoteShell(runnableLlvmFile.path().string()) + " -o "
            + quoteShell(o2Binary.path().string()),
            purpose4);
        const auto t1 = std::chrono::steady_clock::now();
        result.o2BuildMs
            = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0)
                  .count();

        const auto t2 = std::chrono::steady_clock::now();
        const std::string purpose5
            = std::string("running ") + testCase.name + " -O2 benchmark";
        const int o2Exit = runCommandGetStatus(
            "HOME=/tmp timeout " + std::to_string(TIMEOUT_O2) + "s "
                + quoteShell(o2Binary.path().string()) + inputRedirect
                + " > /dev/null 2>&1",
            purpose5);
        const auto t3 = std::chrono::steady_clock::now();
        result.o2RunMs
            = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2)
                  .count();

        if (o2Exit != 0) {
            std::cerr << "O2 FAILED (exit " << o2Exit << ")\n";
            result.o2Passed = false;
            return result;
        }
        result.o2Passed = true;
        std::cerr << "  O2:   passed (" << result.o2RunMs << " ms run)\n";
    }

    return result;
}

} // namespace

int main()
{
    const std::string compiler = compilerPath();
    std::cerr << "compiler: " << compiler << "\n";
    std::cerr << "test source dir: "
              << fs::absolute(polySourceDir()).string() << "\n";

    bool allPassed = true;

    for (const auto& testCase : TEST_CASES) {
        const auto result = runStressTest(testCase, compiler);

        std::cerr << "  [build] ASAN: " << result.asanBuildMs
                  << " ms, O2: " << result.o2BuildMs << " ms\n";
        if (result.asanPassed) {
            std::cerr << "  [run]   ASAN: " << result.asanRunMs << " ms";
            if (result.o2Passed) {
                std::cerr << ", O2: " << result.o2RunMs << " ms";
            }
            std::cerr << "\n";
        }

        if (!result.asanPassed) {
            std::cerr << "  >> MEMORY ISSUE DETECTED (AddressSanitizer)\n";
            allPassed = false;
        } else if (!result.o2Passed) {
            std::cerr << "  >> O2 execution failed (non-zero exit)\n";
            allPassed = false;
        } else {
            std::cerr << "  >> OK\n";
        }
    }

    if (allPassed) {
        std::cerr << "\nAll stress tests passed.\n";
        return 0;
    }
    std::cerr << "\nSome stress tests failed.\n";
    return 1;
}
