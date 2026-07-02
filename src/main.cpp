#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "backend/llvm.h"
#include "backend/llvm_poly_runtime.h"
#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "koopa/ast_to_koopa.h"
#include "koopa/ir.h"

namespace {
using yesod::koopa::Generator;
namespace koopa_ir = yesod::koopa::ir;

constexpr std::string_view kMintRuntimeSource = R"(typedef unsigned int u32;
typedef unsigned long long u64;
typedef int i32;

static const u32 MOD = 998244353u;
static const u32 R = 301989884u;
static const u32 R2 = 932051910u;
static const u32 NEG_INV = 998244351u;

static u32 normalize_signed(i32 x) {
    i32 remainder = x % (i32)MOD;
    return (u32)(remainder < 0 ? remainder + (i32)MOD : remainder);
}

static u32 mont_reduce(u64 x) {
    u32 q = (u32)x * NEG_INV;
    u64 t = (x + (u64)q * MOD) >> 32;
    if (t >= MOD) {
        t -= MOD;
    }
    return (u32)t;
}

static u32 mont_mul(u32 a, u32 b) {
    return mont_reduce((u64)a * (u64)b);
}

static u32 mint_pow(u32 base, u32 exp) {
    u32 result = R;
    while (exp != 0u) {
        if ((exp & 1u) != 0u) {
            result = mont_mul(result, base);
        }
        base = mont_mul(base, base);
        exp >>= 1u;
    }
    return result;
}

static u32 mint_inv(u32 value) {
    return mint_pow(value, MOD - 2u);
}

int __yesod_mint_from_int(int x) {
    return (int)mont_mul(normalize_signed(x), R2);
}

int __yesod_mint_to_int(int x) {
    return (int)mont_reduce((u32)x);
}

int __yesod_mint_add(int a, int b) {
    u32 sum = (u32)a + (u32)b;
    if (sum >= MOD) {
        sum -= MOD;
    }
    return (int)sum;
}

int __yesod_mint_sub(int a, int b) {
    return (int)((u32)a >= (u32)b ? (u32)a - (u32)b : (u32)a + MOD - (u32)b);
}

int __yesod_mint_mul(int a, int b) {
    return (int)mont_mul((u32)a, (u32)b);
}

int __yesod_mint_div(int a, int b) {
    return (int)mont_mul((u32)a, mint_inv((u32)b));
}
 )";

constexpr std::string_view kMintHelperNames[] = {
    "@__yesod_mint_add",
    "@__yesod_mint_sub",
    "@__yesod_mint_mul",
    "@__yesod_mint_div",
    "@__yesod_mint_from_int",
    "@__yesod_mint_to_int",
};

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

bool writeTextFile(const std::string& path, const std::string& contents)
{
    std::ofstream outputStream(path);
    if (!outputStream) {
        return false;
    }

    outputStream << contents;
    return outputStream.good();
}

struct TempFile {
    explicit TempFile(const std::string& suffix)
    {
        const char* tmpDir = std::getenv("TMPDIR");
        if (!tmpDir || tmpDir[0] == '\0') {
            tmpDir = "/tmp";
        }
        std::string pattern
            = std::string(tmpDir) + "/yesod_compiler_XXXXXX" + suffix;
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');

        const int fd
            = ::mkstemps(buffer.data(), static_cast<int>(suffix.size()));
        if (fd == -1) {
            throw std::runtime_error(
                "failed to create temporary file in " + std::string(tmpDir));
        }
        ::close(fd);
        m_path = buffer.data();
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    ~TempFile()
    {
        if (!m_path.empty()) {
            std::remove(m_path.c_str());
        }
    }

    [[nodiscard]] const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

bool runCommand(const std::string& command)
{
    const int status = std::system(command.c_str());
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool programUsesMintRuntime(const koopa_ir::Program& program)
{
    for (const auto& item : program.items) {
        bool usesHelper = false;
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                if constexpr (std::same_as<Item, koopa_ir::FunctionDecl>
                    || std::same_as<Item, koopa_ir::FunctionDef>) {
                    const auto& function = program[itemRef];
                    for (const auto helperName : kMintHelperNames) {
                        if (function.name.spelling == helperName) {
                            usesHelper = true;
                            break;
                        }
                    }
                }
            },
            item);
        if (usesHelper) {
            return true;
        }
    }
    return false;
}

std::string extractLlvmFunctionDefinitions(const std::string& llvmModule)
{
    std::istringstream input(llvmModule);
    std::ostringstream output;
    std::string line;
    bool inFunction = false;
    int braceDepth = 0;
    std::vector<std::string> trailingRecords;

    while (std::getline(input, line)) {
        if (!inFunction) {
            if (line.rfind("attributes #", 0) == 0
                || line.rfind("!llvm.", 0) == 0
                || (!line.empty() && line.front() == '!')) {
                trailingRecords.push_back(line);
            }
            if (line.rfind("define ", 0) != 0
                && line.rfind("define dso_local ", 0) != 0) {
                continue;
            }
            inFunction = true;
        }

        output << line << '\n';
        braceDepth
            += static_cast<int>(std::count(line.begin(), line.end(), '{'));
        braceDepth
            -= static_cast<int>(std::count(line.begin(), line.end(), '}'));
        if (inFunction && braceDepth == 0) {
            output << '\n';
            inFunction = false;
        }
    }

    if (!trailingRecords.empty()) {
        for (const auto& record : trailingRecords) {
            output << record << '\n';
        }
    }

    return output.str();
}

std::string extractLlvmPreludeRecords(const std::string& llvmModule)
{
    std::istringstream input(llvmModule);
    std::ostringstream output;
    std::string line;

    while (std::getline(input, line)) {
        if (line.rfind("; ModuleID =", 0) == 0
            || line.rfind("source_filename =", 0) == 0
            || line.rfind("target datalayout =", 0) == 0
            || line.rfind("target triple =", 0) == 0) {
            continue;
        }
        output << line << '\n';
    }

    return output.str();
}

std::string insertLlvmPrelude(
    const std::string& llvmModule, const std::string& prelude)
{
    const auto insertPos = llvmModule.find("\n\n");
    if (insertPos == std::string::npos) {
        return prelude + "\n" + llvmModule;
    }
    return llvmModule.substr(0, insertPos + 2) + prelude
        + llvmModule.substr(insertPos + 2);
}

std::string stripMintHelperDeclarations(const std::string& llvmModule)
{
    std::istringstream input(llvmModule);
    std::ostringstream output;
    std::string line;

    while (std::getline(input, line)) {
        bool isMintHelperDeclaration = false;
        if (line.rfind("declare ", 0) == 0) {
            for (const auto helperName : kMintHelperNames) {
                if (line.find(helperName) != std::string::npos) {
                    isMintHelperDeclaration = true;
                    break;
                }
            }
        }
        if (!isMintHelperDeclaration) {
            output << line << '\n';
        }
    }

    return output.str();
}

bool buildMintRuntimeLlvmPrelude(std::string& prelude)
{
    TempFile runtimeSource(".c");
    TempFile runtimeLlvm(".ll");
    if (!writeTextFile(runtimeSource.path(), std::string(kMintRuntimeSource))) {
        return false;
    }
    if (!runCommand("clang -S -emit-llvm -O2 -target "
                    "x86_64-pc-linux-gnu "
            + runtimeSource.path() + " -o " + runtimeLlvm.path())) {
        return false;
    }
    prelude = extractLlvmFunctionDefinitions(readTextFile(runtimeLlvm.path()));
    return !prelude.empty();
}

bool buildPolyRuntimeLlvmPrelude(std::string& prelude)
{
    TempFile runtimeSource(".c");
    TempFile runtimeLlvm(".ll");
    if (!writeTextFile(runtimeSource.path(),
            std::string(yesod::backend::LLVM_POLY_RUNTIME_SOURCE))) {
        return false;
    }
    if (!runCommand("clang -S -emit-llvm -O2 -target "
                    "x86_64-pc-linux-gnu "
            + runtimeSource.path() + " -o " + runtimeLlvm.path())) {
        return false;
    }
    prelude = extractLlvmPreludeRecords(readTextFile(runtimeLlvm.path()));
    return !prelude.empty();
}

bool typeUsesPoly(const koopa_ir::Type& type, const koopa_ir::Program& program)
{
    return std::visit(
        [&](const auto& node) -> bool {
            using Node = std::remove_cvref_t<decltype(node)>;
            if constexpr (std::same_as<Node, koopa_ir::PolyType>) {
                return true;
            } else if constexpr (std::same_as<Node,
                                     yesod::Ref<koopa_ir::ArrayType>>) {
                return typeUsesPoly(program[node].elementType, program);
            } else if constexpr (std::same_as<Node,
                                     yesod::Ref<koopa_ir::PointerType>>) {
                return typeUsesPoly(program[node].pointeeType, program);
            } else if constexpr (std::same_as<Node,
                                     yesod::Ref<koopa_ir::FunctionType>>) {
                const auto& functionType = program[node];
                if (functionType.returnType.has_value()
                    && typeUsesPoly(*functionType.returnType, program)) {
                    return true;
                }
                for (const auto& paramType : functionType.paramTypes) {
                    if (typeUsesPoly(paramType, program)) {
                        return true;
                    }
                }
                return false;
            } else {
                return false;
            }
        },
        type);
}

bool programUsesPolyRuntime(const koopa_ir::Program& program)
{
    for (const auto& item : program.items) {
        bool usesPoly = false;
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                if constexpr (std::same_as<Item, koopa_ir::GlobalMemoryDef>) {
                    usesPoly
                        = typeUsesPoly(program[itemRef].allocType, program);
                } else if constexpr (std::same_as<Item,
                                         koopa_ir::FunctionDecl>) {
                    const auto& function = program[itemRef];
                    usesPoly = function.returnType.has_value()
                        && typeUsesPoly(*function.returnType, program);
                    for (const auto& paramType : function.paramTypes) {
                        usesPoly = usesPoly || typeUsesPoly(paramType, program);
                    }
                } else if constexpr (std::same_as<Item,
                                         koopa_ir::FunctionDef>) {
                    const auto& function = program[itemRef];
                    usesPoly = function.returnType.has_value()
                        && typeUsesPoly(*function.returnType, program);
                    for (const auto& paramRef : function.params) {
                        usesPoly = usesPoly
                            || typeUsesPoly(program[paramRef].type, program);
                    }
                    for (const auto& blockRef : function.blocks) {
                        const auto& block = program[blockRef];
                        for (const auto& paramRef : block.params) {
                            usesPoly = usesPoly
                                || typeUsesPoly(
                                    program[paramRef].type, program);
                        }
                    }
                }
            },
            item);
        if (usesPoly) {
            return true;
        }
    }
    return false;
}

struct DiagInfo {
    std::size_t m_offset;
    std::string m_message;
    std::string kind; // "parse" or "semantic"
    yesod::frontend::DiagnosticSeverity severity
        = yesod::frontend::DiagnosticSeverity::error;
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
    const std::string& source, std::vector<DiagInfo>& diags)
{
    if (diags.empty())
        return;

    std::sort(
        diags.begin(), diags.end(), [](const DiagInfo& a, const DiagInfo& b) {
            return a.m_offset < b.m_offset;
        });

    const auto lineStarts = buildLineStarts(source);

    for (const auto& d : diags) {
        // map offset -> line/col
        const std::size_t offset = d.m_offset;
        auto it
            = std::upper_bound(lineStarts.begin(), lineStarts.end(), offset);
        std::size_t line = 0;
        if (it == lineStarts.begin()) {
            line = 0;
        } else {
            line = static_cast<std::size_t>(
                std::distance(lineStarts.begin(), it) - 1);
        }
        const std::size_t col = offset - lineStarts[line] + 1;

        std::cerr << d.kind << ' '
                  << (d.severity == yesod::frontend::DiagnosticSeverity::warning
                             ? "warning"
                             : "error")
                  << " at " << inputPath << ":" << (line + 1) << ":" << col
                  << " (offset " << offset << "): " << d.m_message << std::endl;

        // print the source line and a caret
        const std::size_t lineBegin = lineStarts[line];
        const std::size_t lineEnd = (line + 1 < lineStarts.size())
            ? (lineStarts[line + 1] - 1)
            : source.size();
        const std::string lineText
            = source.substr(lineBegin, lineEnd - lineBegin);
        std::cerr << lineText << std::endl;

        std::string caret;
        // naive caret alignment; tabs are counted as single chars here
        const std::size_t caretPos = (col > 0) ? (col - 1) : 0;
        caret.assign(caretPos, ' ');
        caret += '^';
        std::cerr << caret << std::endl;
    }
}

bool writeKoopaProgramToFile(
    const koopa_ir::Program& program, const std::string& path)
{
    koopa_ir::validate(program);
    return writeTextFile(path, koopa_ir::serializeToKoopa(program));
}

bool writeLlvmProgramToFile(
    const koopa_ir::Program& program, const std::string& path,
    bool minify = false)
{
    std::ostringstream output;
    yesod::backend::LlvmGenerator generator;
    generator.setMinify(minify);
    generator.generate(program, output);
    auto llvmModule = output.str();

    if (programUsesMintRuntime(program)) {
        std::string runtimePrelude;
        if (!buildMintRuntimeLlvmPrelude(runtimePrelude)) {
            return false;
        }
        llvmModule = insertLlvmPrelude(
            stripMintHelperDeclarations(llvmModule), runtimePrelude);
    }
    if (programUsesPolyRuntime(program)) {
        std::string runtimePrelude;
        if (!buildPolyRuntimeLlvmPrelude(runtimePrelude)) {
            return false;
        }
        llvmModule = insertLlvmPrelude(llvmModule, runtimePrelude);
    }
    return writeTextFile(path, llvmModule);
}

// ─── C export helpers ────────────────────────────────────────────────

constexpr std::string_view kSysyBuiltinSource = R"(#include <stdio.h>
#include <stdlib.h>

int getint() {
    int x;
    if (scanf("%d", &x) != 1) return 0;
    return x;
}

int getch() {
    int c = getchar();
    return c == EOF ? -1 : c;
}

int getarray(int a[]) {
    int n;
    scanf("%d", &n);
    for (int i = 0; i < n; ++i) {
        scanf("%d", &a[i]);
    }
    return n;
}

void putint(int x) {
    printf("%d", x);
}

void putch(int x) {
    putchar(x);
}

void putarray(int n, int a[]) {
    for (int i = 0; i < n; ++i) {
        if (i != 0) putchar(' ');
        printf("%d", a[i]);
    }
}

void starttime() {}
void stoptime() {}
)";

bool exportCProgram(const koopa_ir::Program& program,
    const std::string& outputPath)
{
    // Step 1: Generate minified LLVM IR to a temp file
    TempFile llvmFile(".ll");
    if (!writeLlvmProgramToFile(program, llvmFile.path(), true)) {
        std::cerr << "failed to generate minified LLVM IR" << std::endl;
        return false;
    }

    // Step 2: Decompile to C using llvm-cbe
    TempFile cFile(".cbe.c");
    const std::string decompileCmd = "llvm-cbe " + llvmFile.path() + " -o "
        + cFile.path();
    if (!runCommand(decompileCmd)) {
        std::cerr << "llvm-cbe failed" << std::endl;
        return false;
    }

    // Step 3: Read the decompiled C code and strip conflicting declarations
    std::string cCode = readTextFile(cFile.path());
    {
        // llvm-cbe may redeclare our builtin functions with different types.
        // Strip those redeclarations and the `typedef bool` (conflicts with C23).
        std::istringstream input(cCode);
        std::ostringstream cleaned;
        std::string line;
        while (std::getline(input, line)) {
            // Strip the problematic `typedef unsigned char bool;`
            if (line.find("typedef unsigned char bool") != std::string::npos) {
                continue;
            }
            // Skip the sysy builtin redeclarations from llvm-cbe
            if (line.rfind("void putint(") == 0
                || line.rfind("void putch(") == 0
                || line.rfind("uint32_t getint") == 0
                || line.rfind("uint32_t getch") == 0
                || line.rfind("uint32_t getarray") == 0
                || line.rfind("void putarray(") == 0
                || line.rfind("void starttime(") == 0
                || line.rfind("void stoptime(") == 0) {
                continue;
            }
            cleaned << line << '\n';
        }
        cCode = cleaned.str();
        // Remove the now-empty `#ifndef __cplusplus` ... `#endif` block
        // left behind after stripping the bool typedef
        {
            const std::string marker = "#ifndef __cplusplus\n#endif";
            for (auto pos = cCode.find(marker); pos != std::string::npos;
                 pos = cCode.find(marker)) {
                cCode.erase(pos, marker.size());
            }
        }
    }

    // Step 4: Build the output with runtime headers prepended
    std::ostringstream output;

    // SysY builtin functions
    output << kSysyBuiltinSource << "\n\n";

    // Mint runtime (if needed)
    if (programUsesMintRuntime(program)) {
        output << "// ── Mint runtime ─────────────────────────────────\n";
        output << kMintRuntimeSource << "\n\n";
    }

    // Poly runtime (if needed)
    if (programUsesPolyRuntime(program)) {
        output << "// ── Poly runtime ─────────────────────────────────\n";
        output << yesod::backend::LLVM_POLY_RUNTIME_SOURCE << "\n\n";
    }

    // The decompiled C code
    output << cCode;

    return writeTextFile(outputPath, output.str());
}

} // namespace

int main(int argc, const char* argv[])
{
    if (argc != 5 || std::string(argv[3]) != "-o") {
        std::cerr << "Usage: " << argv[0] << " <mode> <input> -o <output>"
                  << std::endl
                  << "Modes: -koopa, -llvm, -llvm-mini, -c" << std::endl;
        return 1;
    }

    const std::string mode = argv[1];
    const std::string inputPath = argv[2];
    const std::string outputPath = argv[4];

    if (mode != "-koopa" && mode != "-llvm" && mode != "-llvm-mini"
        && mode != "-c") {
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
                diags.push_back(DiagInfo { static_cast<std::size_t>(d->offset),
                    d->message, std::string("parse"), d->severity });
            }
            printDiagnosticsAggregate(inputPath, source, diags);
            return 1;
        }

        yesod::frontend::SemanticAnalyzer semanticAnalyzer;
        auto semanticOutput = semanticAnalyzer.analyze(
            std::move(parseOutput.m_ast), parseOutput.m_root.ref());
        if (!semanticOutput.success()) {
            std::vector<DiagInfo> diags;
            // include any parse diagnostics (if present) and semantic
            // diagnostics
            diags.reserve(parseOutput.m_diagnostics.size()
                + semanticOutput.m_diagnostics.size());
            for (const auto& d : parseOutput.m_diagnostics) {
                diags.push_back(DiagInfo { static_cast<std::size_t>(d->offset),
                    d->message, std::string("parse"), d->severity });
            }
            for (const auto& d : semanticOutput.m_diagnostics) {
                diags.push_back(DiagInfo { static_cast<std::size_t>(d->offset),
                    d->message, std::string("semantic"), d->severity });
            }
            printDiagnosticsAggregate(inputPath, source, diags);
            return 1;
        }

        if (!semanticOutput.m_diagnostics.empty()) {
            std::vector<DiagInfo> diags;
            diags.reserve(semanticOutput.m_diagnostics.size());
            for (const auto& d : semanticOutput.m_diagnostics) {
                diags.push_back(DiagInfo { static_cast<std::size_t>(d->offset),
                    d->message, std::string("semantic"), d->severity });
            }
            printDiagnosticsAggregate(inputPath, source, diags);
        }

        if (mode == "-koopa") {
            Generator generator;
            auto program = generator.generateIr(semanticOutput.m_ast,
                semanticOutput.m_root, semanticOutput.m_info);
            if (!writeKoopaProgramToFile(*program, outputPath)) {
                std::cerr << "failed to generate koopa IR" << std::endl;
                return 1;
            }
        } else if (mode == "-llvm-mini") {
            Generator generator;
            auto program = generator.generateIr(semanticOutput.m_ast,
                semanticOutput.m_root, semanticOutput.m_info);
            if (!writeLlvmProgramToFile(*program, outputPath, true)) {
                std::cerr << "failed to generate minified LLVM IR"
                          << std::endl;
                return 1;
            }
        } else if (mode == "-c") {
            Generator generator;
            auto program = generator.generateIr(semanticOutput.m_ast,
                semanticOutput.m_root, semanticOutput.m_info);
            if (!exportCProgram(*program, outputPath)) {
                std::cerr << "failed to export C program" << std::endl;
                return 1;
            }
        } else {
            Generator generator;
            auto program = generator.generateIr(semanticOutput.m_ast,
                semanticOutput.m_root, semanticOutput.m_info);
            if (!writeLlvmProgramToFile(*program, outputPath, false)) {
                std::cerr << "failed to generate LLVM IR" << std::endl;
                return 1;
            }
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << std::endl;
        return 1;
    }

    return 0;
}
