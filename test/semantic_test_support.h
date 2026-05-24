#ifndef _YESOD_TEST_SEMANTIC_TEST_SUPPORT_H_
#define _YESOD_TEST_SEMANTIC_TEST_SUPPORT_H_

#include <string>
#include <type_traits>

#include "frontend/ast.h"
#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "test_support.h"

namespace yesod::test_support::semantic {

using yesod::frontend::ExpType;
using yesod::frontend::ParseOutput;
using yesod::frontend::Parser;
using yesod::frontend::Ptr;
using yesod::frontend::Ref;
using yesod::frontend::SemanticAnalyzer;
using yesod::frontend::SemanticDiagnostic;
using yesod::frontend::SemanticDiagnosticKind;
using yesod::frontend::SemanticOutput;
namespace ast = yesod::frontend;

using yesod::test_support::fail;
using yesod::test_support::OutputAstBase;
using yesod::test_support::require;
using yesod::test_support::TestBase;

static_assert(std::is_same_v<
    decltype(std::declval<yesod::frontend::SemanticSymbol>().name),
    std::string>);
static_assert(std::is_same_v<decltype(std::declval<Number>().value), int32_t>);
static_assert(std::variant_size_v<ast::Stmt> == 8);

struct SemanticTestBase : OutputAstBase<SemanticOutput>, TestBase {
    template <class Self> auto&& ast(this Self& self)
    {
        return self.m_output.m_ast;
    }

    [[nodiscard]] const SemanticDiagnostic& firstDiagnostic()
    {
        require(!m_output.m_diagnostics.empty(),
            "expected at least one semantic diagnostic");
        return m_output.m_diagnostics.front();
    }
};

inline ParseOutput parseSource(const std::string& source)
{
    Parser parser(source);
    auto output = parser.parse();
    bindCurrentAst(output.m_ast);
    return output;
}

inline SemanticOutput analyzeSource(const std::string& source)
{
    auto parseOutput = parseSource(source);
    if (!parseOutput.success()) {
        fail("expected parse success before semantic analysis");
    }

    auto ast = std::move(parseOutput.m_ast);
    const auto root = parseOutput.m_root;

    if (!root) {
        fail("expected non-null root node from parser output");
    }
    SemanticAnalyzer analyzer;
    auto output = analyzer.analyze(std::move(ast), root.ref());
    bindCurrentAst(output.m_ast);
    return output;
}

inline SemanticOutput analyzeRoot(const std::string& source)
{
    auto output = analyzeSource(source);
    if (!output.success()) {
        std::string message = "expected semantic success";
        if (!output.m_diagnostics.empty()) {
            message += ": ";
            message += output.m_diagnostics.front().m_message;
        }
        fail(message);
    }
    return output;
}

inline const yesod::frontend::SemanticSymbol& requireSymbol(
    const SemanticOutput& output, const Ptr<ast::Identifier>& node)
{
    const auto* symbol = output.m_info.findSymbol(node.ref());
    require(symbol != nullptr, "expected symbol binding for node");
    return *symbol;
}
inline const yesod::frontend::SemanticSymbol& requireSymbol(
    const SemanticOutput& output, const Ptr<ast::ConstDef>& node)
{
    const auto* symbol = output.m_info.findSymbol(node(output.m_ast).identifier);
    require(symbol != nullptr, "expected symbol binding for node");
    return *symbol;
}
inline const yesod::frontend::SemanticSymbol& requireSymbol(
    const SemanticOutput& output, const Ptr<ast::VarDef>& node)
{
    const auto* symbol = output.m_info.findSymbol(node(output.m_ast).identifier);
    require(symbol != nullptr, "expected symbol binding for node");
    return *symbol;
}
inline const yesod::frontend::SemanticSymbol& requireSymbol(
    const SemanticOutput& output, const Ptr<ast::Exp>& node)
{
    const auto& exp = node(output.m_ast);
    const auto &ident = MATCH(exp.kind)
        WITH([](const LVal& lVal) { return lVal.identifier; },
            [](const auto&) -> Ref<Identifier> {
                require(false, "expected lvalue expression for symbol lookup");
                std::unreachable();
            });
    return *output.m_info.findSymbol(ident);
}

template <typename T>
inline int32_t requireConstantValue(
    const SemanticOutput& output, Ref<T> node)
{
    const auto constantValue = output.m_info.findConstantValue(node);
    require(constantValue.has_value(), "expected constant value for node");
    return *constantValue;
}

template <typename T>
inline ExpType requireExpValueKind(
    const SemanticOutput& output, Ref<T> node)
{
    const auto valueKind = output.m_info.findExpValueKind(node);
    require(valueKind.has_value(), "expected expression kind for node");
    return *valueKind;
}

template <typename T>
inline Ptr<ast::WhileStmt> requireLoop(
    const SemanticOutput& output, Ref<T> node)
{
    const auto loop = output.m_info.findLoop(node);
    require(loop.has_value(), "expected loop binding for node");
    return *loop;
}

} // namespace yesod::test_support::semantic

#endif