#include "poly/poly_test_driver.h"

#include <stdexcept>

#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "poly/poly_interpreter.h"
#include "poly/poly_prelude.h"

namespace yesod::test_support::poly {

yesod::test_support::interpreter_runner::ExecuteResult executeSource(
    const std::string& source, std::istream& is, std::ostream& os,
    std::stop_token stopToken)
{
    frontend::Parser parser(frontend::prependBuiltinFunctionDeclarations(
        prependPolyTestPreludeIfMissing(source)));
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

    return interpreter::execute(semanticOutput.m_ast,
        semanticOutput.m_root.ref(), semanticOutput.m_info, is, os, stopToken);
}

} // namespace yesod::test_support::poly
