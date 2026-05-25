#ifndef _YESOD_FRONTEND_PARSER_H_
#define _YESOD_FRONTEND_PARSER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "frontend/diagnostic.h"
#include "frontend/ast.h"

namespace yesod::frontend {

[[nodiscard]] std::string prependBuiltinFunctionDeclarations(
    const std::string& source);

YESOD_DECLARE_DIAGNOSTIC(UnexpectedTokenDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(ExpectedIdentifierDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(ExpectedIntegerDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(ExpectedKeywordDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(ExpectedSymbolDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingFuncRParenDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedBlockItemDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedDeclItemDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingDeclSemicolonDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedIfCondDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingIfRParenDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedIfThenStmtDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedElseStmtDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedWhileCondDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingWhileRParenDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedWhileBodyDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedStmtHeadDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedAssignValueDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedReturnValueDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedPrimaryExpDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingParamArrayRBracketDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedArrayBoundDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingArrayRBracketDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedConstInitializerDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingConstInitRBraceDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedInitializerDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingInitRBraceDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MalformedSubscriptDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingSubscriptRBracketDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingBreakSemicolonDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingContinueSemicolonDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingPrimaryRParenDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingAssignSemicolonDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingSemicolonDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(MissingRBraceDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(TrailingInputDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(IntegerOutOfRangeDiagnostic)

struct ParseOutput {
    AST m_ast;
    Ptr<CompUnit> m_root;
    std::vector<std::unique_ptr<Diagnostic>> m_diagnostics;

    bool success() const
    {
        return static_cast<bool>(m_root) && m_diagnostics.empty();
    }

    Ptr<CompUnit> root() { return m_root; }
};

namespace detail {
    class ParserImpl;
}

class Parser {
public:
    explicit Parser(std::string source);
    ~Parser();
    [[nodiscard]] ParseOutput parse();

private:
    detail::ParserImpl* m_impl;
};

} // namespace yesod::frontend

#endif