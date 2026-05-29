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
    std::cerr << "backend_riscv_test failure: " << message << std::endl;
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
        std::string pattern = "/tmp/backend_riscv_test_XXXXXX" + suffix;
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

    TempFile(TempFile&& other) noexcept
        : m_path(std::move(other.m_path))
    {
        other.m_path.clear();
    }

    TempFile& operator=(TempFile&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        cleanup();
        m_path = std::move(other.m_path);
        other.m_path.clear();
        return *this;
    }

    ~TempFile() { cleanup(); }

    [[nodiscard]] const std::string& path() const { return m_path; }

private:
    void cleanup()
    {
        if (!m_path.empty()) {
            std::error_code errorCode;
            std::filesystem::remove(m_path, errorCode);
        }
    }

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
    const auto compiler = testPath.parent_path() / "compiler";
    require(std::filesystem::exists(compiler),
        "expected compiler binary next to backend_riscv_test at "
            + compiler.string());
    return compiler.string();
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

void expectProgramOutput(const std::string& source, const std::string& input,
    const std::string& expectedOutput)
{
    const std::string compiledCompilerPath = compilerPath();
    const std::string libraryPath = riscvLibraryPath();

    TempFile sourceFile(".sy");
    TempFile assemblyFile(".S");
    TempFile objectFile(".o");
    TempFile executableFile(".elf");
    TempFile inputFile(".txt");
    TempFile outputFile(".txt");

    writeTextFile(sourceFile.path(), source);
    writeTextFile(inputFile.path(), input);

    runCheckedCommand("timeout 10s " + quoteShell(compiledCompilerPath)
            + " -riscv " + quoteShell(sourceFile.path()) + " -o "
            + quoteShell(assemblyFile.path()),
        "compiling SysY source to RISC-V assembly");

    runCheckedCommand("clang " + quoteShell(assemblyFile.path()) + " -c -o "
            + quoteShell(objectFile.path())
            + " -target riscv32-unknown-linux-elf -march=rv32im -mabi=ilp32",
        "assembling generated RISC-V code");

    runCheckedCommand("ld.lld " + quoteShell(objectFile.path()) + " -L"
            + quoteShell(libraryPath) + " -lsysy -o "
            + quoteShell(executableFile.path()),
        "linking generated RISC-V executable");

    runCheckedCommand("timeout 10s qemu-riscv32-static "
            + quoteShell(executableFile.path()) + " < "
            + quoteShell(inputFile.path()) + " > "
            + quoteShell(outputFile.path()),
        "executing generated RISC-V executable under qemu");

    const std::string actualOutput = readTextFile(outputFile.path());
    require(actualOutput == expectedOutput,
        "unexpected program output\nexpected: " + expectedOutput
            + "\nactual: " + actualOutput);
}

void testLiteralOutput()
{
    expectProgramOutput(
        "int main(){putint(42); putch(10); return 0;}", "", "42\n");
}

void testGlobalLoadStoreAndBranching()
{
    expectProgramOutput("int global = 1;"
                        "int main(){"
                        "  if(global){"
                        "    global = global + 4;"
                        "  } else {"
                        "    global = 99;"
                        "  }"
                        "  putint(global);"
                        "  putch(10);"
                        "  return 0;"
                        "}",
        "", "5\n");
}

void testCallWithMoreThanEightArguments()
{
    expectProgramOutput("int sum10(int a0,int a1,int a2,int a3,int a4,int "
                        "a5,int a6,int a7,int a8,int a9){"
                        "  return a0+a1+a2+a3+a4+a5+a6+a7+a8+a9;"
                        "}"
                        "int main(){"
                        "  putint(sum10(1,2,3,4,5,6,7,8,9,10));"
                        "  putch(10);"
                        "  return 0;"
                        "}",
        "", "55\n");
}

void testInputRedirectionAndArrayAccess()
{
    expectProgramOutput("int main(){"
                        "  int values[8];"
                        "  int length = getarray(values);"
                        "  int index = 0;"
                        "  int sum = 0;"
                        "  while(index < length){"
                        "    sum = sum + values[index];"
                        "    index = index + 1;"
                        "  }"
                        "  putint(sum);"
                        "  putch(10);"
                        "  return 0;"
                        "}",
        "4 9 8 7 6\n", "30\n");
}

void testMintRuntimeHelpersLinkAndExecute()
{
    expectProgramOutput(
        "int id(int x){return x;} int main(){mint a = mint(id(6)); mint b = "
        "mint(id(7)); putint(int(a * b)); putch(10); return 0;}",
        "", "42\n");
}

void testBitwiseAndShiftOperatorsExecute()
{
    expectProgramOutput(
        "int main(){"
        "  int a = 6;"
        "  int b = 2;"
        "  putint((~a ^ ((a << b) & 31)) | (32 >> b));"
        "  putch(10);"
        "  return 0;"
        "}",
        "", "-15\n");
}

} // namespace

int main()
{
    testLiteralOutput();
    testGlobalLoadStoreAndBranching();
    testCallWithMoreThanEightArguments();
    testInputRedirectionAndArrayAccess();
    testMintRuntimeHelpersLinkAndExecute();
    testBitwiseAndShiftOperatorsExecute();
    return 0;
}