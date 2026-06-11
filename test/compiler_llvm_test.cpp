#include <cstdlib>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

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

std::string readTextFile(const std::string& path)
{
    std::ifstream input(path);
    require(input.good(), "failed to open file for reading: " + path);
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void writeTextFile(const std::string& path, const std::string& contents)
{
    std::ofstream output(path);
    require(output.good(), "failed to open file for writing: " + path);
    output << contents;
    require(output.good(), "failed to write file: " + path);
}

struct TempFile {
    explicit TempFile(const std::string& suffix)
    {
        std::string pattern = "/tmp/compiler_llvm_test_XXXXXX" + suffix;
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');

        const int fileDescriptor
            = ::mkstemps(buffer.data(), static_cast<int>(suffix.size()));
        require(
            fileDescriptor != -1, "failed to create temporary file under /tmp");
        ::close(fileDescriptor);
        m_path = buffer.data();
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    ~TempFile()
    {
        if (!m_path.empty()) {
            std::error_code errorCode;
            std::filesystem::remove(m_path, errorCode);
        }
    }

    [[nodiscard]] const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

std::string compilerPath()
{
    std::vector<char> buffer(4096, '\0');
    const ssize_t length
        = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    require(length != -1, "failed to resolve test executable path");

    const std::filesystem::path testPath(
        std::string(buffer.data(), static_cast<size_t>(length)));
    const auto localCompiler = testPath.parent_path() / "compiler";
    if (std::filesystem::exists(localCompiler)) {
        return localCompiler.string();
    }

    const auto parentCompiler
        = testPath.parent_path().parent_path() / "compiler";
    require(std::filesystem::exists(parentCompiler),
        "expected compiler binary next to compiler_llvm_test or in parent "
        "build directory at "
            + parentCompiler.string());
    return parentCompiler.string();
}

std::string riscvLibraryPath()
{
    const char* cdeLibraryPath = std::getenv("CDE_LIBRARY_PATH");
    require(cdeLibraryPath != nullptr && cdeLibraryPath[0] != '\0',
        "CDE_LIBRARY_PATH must be set for RISC-V linking");
    return std::string(cdeLibraryPath) + "/riscv32";
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

void expectLlvmProgramOutput(
    const std::string& source, const std::string& expectedOutput)
{
    const std::string compiledCompilerPath = compilerPath();
    const std::string libraryPath = riscvLibraryPath();

    TempFile sourceFile(".sy");
    TempFile llvmFile(".ll");
    TempFile assemblyFile(".S");
    TempFile objectFile(".o");
    TempFile executableFile(".elf");
    TempFile outputFile(".txt");

    writeTextFile(sourceFile.path(), source);

    runCheckedCommand("timeout 10s " + quoteShell(compiledCompilerPath)
            + " -llvm " + quoteShell(sourceFile.path()) + " -o "
            + quoteShell(llvmFile.path()),
        "compiling SysY source to LLVM IR");

    runCheckedCommand("clang " + quoteShell(llvmFile.path()) + " -S -o "
            + quoteShell(assemblyFile.path())
            + " -target riscv32-unknown-linux-elf -march=rv32im -mabi=ilp32",
        "lowering LLVM IR to RISC-V assembly");

    runCheckedCommand("clang " + quoteShell(assemblyFile.path()) + " -c -o "
            + quoteShell(objectFile.path())
            + " -target riscv32-unknown-linux-elf -march=rv32im -mabi=ilp32",
        "assembling generated RISC-V assembly");

    runCheckedCommand("ld.lld " + quoteShell(objectFile.path()) + " -L"
            + quoteShell(libraryPath) + " -lsysy -o "
            + quoteShell(executableFile.path()),
        "linking generated RISC-V executable");

    runCheckedCommand("timeout 10s qemu-riscv32-static "
            + quoteShell(executableFile.path()) + " > "
            + quoteShell(outputFile.path()),
        "executing generated RISC-V executable under qemu");

    const std::string actualOutput = readTextFile(outputFile.path());
    require(actualOutput == expectedOutput,
        "unexpected program output\nexpected: " + expectedOutput
            + "\nactual: " + actualOutput);
}

void testLlvmModeUsesNativeMintLowering()
{
    const std::string compiledCompilerPath = compilerPath();

    TempFile sourceFile(".sy");
    TempFile llvmFile(".ll");
    writeTextFile(sourceFile.path(),
        "int id(int x){return x;} int main(){mint a = mint(id(6)); mint b = "
        "mint(id(7)); return int(a * b);}");

    runCheckedCommand("timeout 10s " + quoteShell(compiledCompilerPath)
            + " -llvm " + quoteShell(sourceFile.path()) + " -o "
            + quoteShell(llvmFile.path()),
        "compiling SysY source to LLVM IR");

    const std::string llvmText = readTextFile(llvmFile.path());
    require(llvmText.find("@__yesod_mint_") == std::string::npos,
        "-llvm output should not include old mint runtime helpers");
    require(llvmText.find(" mul i32 ") != std::string::npos,
        "-llvm output should lower native mint multiplication as an i32 mul");
    require(llvmText.find("@main") != std::string::npos,
        "-llvm output should still include the lowered main function");
}

void testLlvmMintProgramExecutesThroughRiscvToolchain()
{
    expectLlvmProgramOutput(
        "int id(int x){return x;} int main(){mint a = mint(id(6)); mint b = "
        "mint(id(7)); putint(int(a * b)); putch(10); return 0;}",
        "42\n");
}

void testLlvmBitwiseProgramExecutesThroughRiscvToolchain()
{
    expectLlvmProgramOutput("int main(){"
                            "  int a = 6;"
                            "  int b = 2;"
                            "  putint((~a ^ ((a << b) & 31)) | (32 >> b));"
                            "  putch(10);"
                            "  return 0;"
                            "}",
        "-23\n");
}

} // namespace

int main()
{
    testLlvmModeUsesNativeMintLowering();
    testLlvmMintProgramExecutesThroughRiscvToolchain();
    testLlvmBitwiseProgramExecutesThroughRiscvToolchain();
    return 0;
}