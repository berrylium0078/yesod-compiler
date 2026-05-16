#ifndef _YESOD_TEST_SEMANTIC_TEST_SUPPORT_H_
#define _YESOD_TEST_SEMANTIC_TEST_SUPPORT_H_

#include <cstdlib>

#include <iostream>
#include <string>
#include <type_traits>

#include "frontend/ast.h"
#include "frontend/parser.h"
#include "frontend/semantic.h"

namespace yesod::test_support::semantic {

using yesod::frontend::ExpType;
using yesod::frontend::Handle;
using yesod::frontend::ParseOutput;
using yesod::frontend::Parser;
using yesod::frontend::SemanticAnalyzer;
using yesod::frontend::SemanticDiagnostic;
using yesod::frontend::SemanticDiagnosticKind;
using yesod::frontend::SemanticOutput;
namespace ast = yesod::frontend;

static_assert(std::is_same_v<
    decltype(std::declval<yesod::frontend::SemanticSymbol>().m_name),
    std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<ast::Number>().m_value), int32_t>);
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

struct ParsedOutput {
    ParsedOutput(const ParsedOutput&) = delete;
    ParsedOutput& operator=(const ParsedOutput&) = delete;
    ParsedOutput(ParsedOutput&& other)
        : m_output(std::move(other.m_output))
        , m_root(m_output.m_root)
        , m_diagnostics(m_output.m_diagnostics)
        , m_scope(std::move(other.m_scope))
    {
        m_scope.rebind(m_output.m_ast);
    }

    ParsedOutput& operator=(ParsedOutput&& other)
    {
        m_output = std::move(other.m_output);
        m_root = m_output.m_root;
        m_diagnostics = m_output.m_diagnostics;
        m_scope = std::move(other.m_scope);
        m_scope.rebind(m_output.m_ast);
        return *this;
    }

    explicit ParsedOutput(ParseOutput output)
        : m_output(std::move(output))
        , m_root(m_output.m_root)
        , m_diagnostics(m_output.m_diagnostics)
        , m_scope(m_output.m_ast.bindCurrent())
    {
    }

    [[nodiscard]] bool success() const { return m_output.success(); }

    [[nodiscard]] const ast::CompUnit* operator->() const
    {
        return m_root ? &m_output.m_ast.get(m_root) : nullptr;
    }

    ParseOutput m_output;
    ast::Handle<ast::CompUnit> m_root;
    std::vector<ast::Diagnostic> m_diagnostics;
    ast::AST::ScopedCurrent m_scope;
};

struct AnalyzedOutput {
    AnalyzedOutput(const AnalyzedOutput&) = delete;
    AnalyzedOutput& operator=(const AnalyzedOutput&) = delete;
    AnalyzedOutput(AnalyzedOutput&& other)
        : m_output(std::move(other.m_output))
        , m_root(m_output.m_root)
        , m_info(m_output.m_info)
        , m_diagnostics(m_output.m_diagnostics)
        , m_scope(std::move(other.m_scope))
    {
        m_scope.rebind(m_output.m_ast);
    }

    AnalyzedOutput& operator=(AnalyzedOutput&& other)
    {
        m_output = std::move(other.m_output);
        m_root = m_output.m_root;
        m_info = m_output.m_info;
        m_diagnostics = m_output.m_diagnostics;
        m_scope = std::move(other.m_scope);
        m_scope.rebind(m_output.m_ast);
        return *this;
    }

    explicit AnalyzedOutput(SemanticOutput output)
        : m_output(std::move(output))
        , m_root(m_output.m_root)
        , m_info(m_output.m_info)
        , m_diagnostics(m_output.m_diagnostics)
        , m_scope(m_output.m_ast.bindCurrent())
    {
    }

    [[nodiscard]] bool success() const { return m_output.success(); }

    [[nodiscard]] const ast::CompUnit* operator->() const
    {
        return m_root ? &m_output.m_ast.get(m_root) : nullptr;
    }

    SemanticOutput m_output;
    ast::Handle<ast::CompUnit> m_root;
    ast::SemanticInfo m_info;
    std::vector<SemanticDiagnostic> m_diagnostics;
    ast::AST::ScopedCurrent m_scope;
};

inline ParsedOutput parseSource(const std::string& source)
{
    Parser parser(source);
    return ParsedOutput(parser.parse());
}

inline AnalyzedOutput analyzeSource(const std::string& source)
{
    auto parseOutput = parseSource(source);
    if (!parseOutput.success()) {
        fail("expected parse success before semantic analysis");
    }

    auto ast = std::move(parseOutput.m_output.m_ast);
    const auto root = parseOutput.m_root;
    parseOutput.m_scope = {};

    SemanticAnalyzer analyzer;
    return AnalyzedOutput(analyzer.analyze(std::move(ast), root));
}

inline AnalyzedOutput analyzeRoot(const std::string& source)
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

inline const SemanticDiagnostic& firstDiagnostic(const AnalyzedOutput& output)
{
    require(!output.m_diagnostics.empty(),
        "expected at least one semantic diagnostic");
    return output.m_diagnostics.front();
}

inline ast::Handle<ast::FuncDef> firstFuncDef(
    const ast::Handle<ast::CompUnit>& compUnit_nn)
{
    require(compUnit_nn != nullptr, "expected compilation unit node");
    for (const auto topLevelItem_nn : compUnit_nn->m_topLevelItems) {
        ast::Handle<ast::FuncDef> funcDef_nn;
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType,
                                  ast::Handle<ast::FuncDef>>) {
                    funcDef_nn = topLevelAlt;
                }
            },
            topLevelItem_nn->m_topLevelItem);
        if (funcDef_nn) {
            return funcDef_nn;
        }
    }
    fail("expected at least one function definition in compilation unit");
}

template <typename T>
inline ast::Handle<ast::Identifier> requireSymbolIdentifier(ast::Handle<T> node)
{
    if constexpr (std::is_same_v<T, ast::Identifier>) {
        return node;
    } else if constexpr (std::is_same_v<T, ast::ConstDef>) {
        return node->m_identifier_nn;
    } else if constexpr (std::is_same_v<T, ast::VarDef>) {
        return node->m_identifier_nn;
    } else if constexpr (std::is_same_v<T, ast::Exp>) {
        require(std::holds_alternative<ast::LVal>(node->m_kind),
            "expected lvalue expression for symbol lookup");
        return std::get<ast::LVal>(node->m_kind).m_identifier_nn;
    } else {
        static_assert(
            std::is_same_v<T, void>, "unsupported symbol lookup node type");
    }
}

template <typename T>
inline const yesod::frontend::SemanticSymbol& requireSymbol(
    const AnalyzedOutput& output, ast::Handle<T> node)
{
    const auto* symbol
        = output.m_info.findSymbol(requireSymbolIdentifier(node));
    require(symbol != nullptr, "expected symbol binding for node");
    return *symbol;
}

template <typename T>
inline int32_t requireConstantValue(
    const AnalyzedOutput& output, ast::Handle<T> node)
{
    const auto constantValue = output.m_info.findConstantValue(node);
    require(constantValue.has_value(), "expected constant value for node");
    return *constantValue;
}

template <typename T>
inline ExpType requireExpValueKind(
    const AnalyzedOutput& output, ast::Handle<T> node)
{
    const auto valueKind = output.m_info.findExpValueKind(node);
    require(valueKind.has_value(), "expected expression kind for node");
    return *valueKind;
}

template <typename T>
inline ast::Handle<ast::WhileStmt> requireLoop(
    const AnalyzedOutput& output, ast::Handle<T> node)
{
    const auto loop = output.m_info.findLoop(node);
    require(loop.has_value(), "expected loop binding for node");
    return *loop;
}

} // namespace yesod::test_support::semantic

#endif
