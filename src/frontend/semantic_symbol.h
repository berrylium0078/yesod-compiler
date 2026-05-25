#ifndef _YESOD_FRONTEND_SYMBOL_H_
#define _YESOD_FRONTEND_SYMBOL_H_

#include <memory>
#include <optional>
#include <unordered_set>

#include "frontend/ast.h"
#include "frontend/diagnostic.h"

namespace yesod::frontend {

YESOD_DECLARE_DIAGNOSTIC(UseBeforeDefinitionDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(DoubleDefinitionDiagnostic)

class SymbolResolutionResult {
public:
    explicit SymbolResolutionResult(const AST& ast);
    virtual ~SymbolResolutionResult() = default;
    [[nodiscard]] const std::unordered_map<Ref<Identifier>, int32_t>&
    symbolIdsByIdentifier() const;
    [[nodiscard]] const std::vector<std::unique_ptr<Diagnostic>>&
    diagnostics() const;
    [[nodiscard]] std::optional<int32_t> findSymbolId(
        Ref<Identifier> identifier) const;
    [[nodiscard]] bool hasDeclaration(int32_t symbolId) const;

protected:
    using Scope = std::unordered_map<std::string, Ref<Identifier>>;
    const AST& ast;
    std::unordered_map<Ref<Identifier>, int32_t> m_symbolIdByIdentifier;
    std::vector<Scope> m_scopeStack;
    std::vector<std::unique_ptr<Diagnostic>> m_diagnostics;
    std::unordered_set<int32_t> m_declaredSymbolIds;
    std::unordered_set<int32_t> m_functionSymbolIds;
    std::unordered_set<int32_t> m_definedFunctionSymbolIds;
    int32_t m_nextSymbolId = 0;
};

namespace detail {
    class SemanticSymbolResolverImpl;
}

class SemanticSymbolResolver {
    friend class SemanticAnalyzer;
public:
    explicit SemanticSymbolResolver(const AST& ast);
    ~SemanticSymbolResolver();

    void analyze(Ref<CompUnit> compUnit);
    const SymbolResolutionResult *operator->() const;

private:
    std::unique_ptr<detail::SemanticSymbolResolverImpl> m_impl;
};

}

#endif // _YESOD_FRONTEND_SYMBOL_H_