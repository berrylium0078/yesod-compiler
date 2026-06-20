#include "interpreter_runner.h"

#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "koopa/ast_to_koopa.h"
#include "koopa/koopa_interpreter.h"
#include "koopa/koopa_simplify_assert.h"
#include "poly/poly_prelude.h"

namespace {

namespace fs = std::filesystem;
namespace frontend = yesod::frontend;
namespace koopa = yesod::koopa;
namespace koopa_interpreter = yesod::test_support::koopa::interpreter;
namespace runner = yesod::test_support::interpreter_runner;

#ifndef YESOD_TEST_SOURCE_DIR
#define YESOD_TEST_SOURCE_DIR "."
#endif

[[nodiscard]] std::unique_ptr<yesod::koopa::ir::Program> generateProgram(
    const std::string& source)
{
    frontend::Parser parser(frontend::prependBuiltinFunctionDeclarations(
        yesod::test_support::poly::prependPolyTestPreludeIfMissing(source)));
    auto parseOutput = parser.parse();
    if (!parseOutput.success()) {
        throw std::runtime_error("parse failed");
    }

    frontend::SemanticAnalyzer semanticAnalyzer;
    auto semanticOutput
        = semanticAnalyzer.analyze(parseOutput.m_ast, parseOutput.m_root.ref());
    if (!semanticOutput.success()) {
        throw std::runtime_error("semantic analysis failed");
    }

    koopa::Generator generator;
    return generator.generateIr(
        semanticOutput.m_ast, semanticOutput.m_root, semanticOutput.m_info);
}

[[nodiscard]] runner::ExecuteStatus mapStatus(
    koopa_interpreter::ExecuteStatus status)
{
    switch (status) {
    case koopa_interpreter::ExecuteStatus::normal:
        return runner::ExecuteStatus::normal;
    case koopa_interpreter::ExecuteStatus::stopped:
        return runner::ExecuteStatus::stopped;
    case koopa_interpreter::ExecuteStatus::arrayOutOfBounds:
        return runner::ExecuteStatus::arrayOutOfBounds;
    case koopa_interpreter::ExecuteStatus::divisionByZero:
        return runner::ExecuteStatus::divisionByZero;
    case koopa_interpreter::ExecuteStatus::unsupported:
        return runner::ExecuteStatus::unsupported;
    case koopa_interpreter::ExecuteStatus::runtimeError:
        return runner::ExecuteStatus::runtimeError;
    }
    return runner::ExecuteStatus::runtimeError;
}

} // namespace

int main(int argc, char** argv)
{
    yesod::test_support::koopa::assertPolySimplificationPassApplied();

    const runner::RunnerConfig config {
        .defaultTestDirectory
        = fs::path(YESOD_TEST_SOURCE_DIR) / "testsuit-collection" / "lvX",
        .suiteName = "koopa extra",
        .defaultTimeoutMs = 2000,
    };
    return runner::runTestMain(argc, argv, config,
        [](const std::string& source, std::istream& input, std::ostream& output,
            std::stop_token stopToken) -> runner::ExecuteResult {
            auto program = generateProgram(source);
            const auto result = koopa_interpreter::execute(
                *program, input, output, stopToken);
            return runner::ExecuteResult {
                .status = mapStatus(result.status),
                .returnValue = result.returnValue,
                .message = result.message,
            };
        });
}
