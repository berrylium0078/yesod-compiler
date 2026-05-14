#ifndef _YESOD_TEST_SEMANTIC_TEST_SUPPORT_H_
#define _YESOD_TEST_SEMANTIC_TEST_SUPPORT_H_

#include <cstdlib>

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "frontend/ast.h"
#include "frontend/parser.h"
#include "frontend/semantic.h"

namespace yesod::test_support::semantic {

using yesod::frontend::ParseOutput;
using yesod::frontend::Parser;
using yesod::frontend::SemanticAnalyzer;
using yesod::frontend::SemanticDiagnostic;
using yesod::frontend::SemanticDiagnosticKind;
using yesod::frontend::SemanticExpValueKind;
using yesod::frontend::SemanticOutput;
namespace ast = yesod::frontend;

static_assert(std::is_same_v<decltype(std::declval<yesod::frontend::SemanticSymbol>().m_name),
    std::string>);
static_assert(std::is_same_v<decltype(std::declval<ast::Number>().m_value),
    int32_t>);
static_assert(std::variant_size_v<ast::Stmt> == 8);

[[noreturn]] inline void fail(const std::string& message)
{
    std::cerr << "semantic_test failure: " << message << std::endl;
    std::exit(1);
}

inline void require(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

inline ParseOutput parseSource(const std::string& source)
{
    Parser parser(source);
    return parser.parse();
}

inline SemanticOutput analyzeSource(const std::string& source)
{
    const auto parseOutput = parseSource(source);
    if (!parseOutput.success()) {
        fail("expected parse success before semantic analysis");
    }

    SemanticAnalyzer analyzer;
    return analyzer.analyze(parseOutput.m_root);
}

inline std::shared_ptr<ast::CompUnit> analyzeRoot(const std::string& source)
{
    const auto output = analyzeSource(source);
    if (!output.success()) {
        std::string message = "expected semantic success";
        if (!output.m_diagnostics.empty()) {
            message += ": ";
            message += output.m_diagnostics.front().m_message;
        }
        fail(message);
    }
    return output.m_root;
}

inline const SemanticDiagnostic& firstDiagnostic(const SemanticOutput& output)
{
    require(!output.m_diagnostics.empty(), "expected at least one semantic diagnostic");
    return output.m_diagnostics.front();
}

inline const yesod::frontend::SemanticSymbol& requireSymbol(
    const SemanticOutput& output, const ast::AstNode& node)
{
    const auto* symbol = output.m_info.findSymbolByNodeId(node.m_id);
    require(symbol != nullptr, "expected symbol binding for node");
    return *symbol;
}

inline int32_t requireConstantValue(
    const SemanticOutput& output, const ast::AstNode& node)
{
    const auto constantValue = output.m_info.findConstantValue(node.m_id);
    require(constantValue.has_value(), "expected constant value for node");
    return *constantValue;
}

inline SemanticExpValueKind requireExpValueKind(
    const SemanticOutput& output, const ast::AstNode& node)
{
    const auto valueKind = output.m_info.findExpValueKind(node.m_id);
    require(valueKind.has_value(), "expected expression kind for node");
    return *valueKind;
}

inline int32_t requireLoopId(
    const SemanticOutput& output, const ast::AstNode& node)
{
    const auto loopId = output.m_info.findLoopId(node.m_id);
    require(loopId.has_value(), "expected loop binding for node");
    return *loopId;
}

} // namespace yesod::test_support::semantic

#endif
