#ifndef _YESOD_FRONTEND_SEMANTIC_H_
#define _YESOD_FRONTEND_SEMANTIC_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontend/ast.h"
#include "frontend/diagnostic.h"
#include "frontend/semantic_cfg.h"
#include "frontend/semantic_ssa.h"
#include "frontend/semantic_symbol.h"
#include "frontend/semantic_type.h"
#include "utils.h"

namespace yesod::frontend {

class SemanticInfo {
    friend class SemanticAnalyzer;

public:
    std::optional<int32_t> findSymbolId(Ref<Identifier> identifier) const;
    const SemanticSymbol* findSymbolById(int32_t symbolId) const;
    const SemanticSymbol* findSymbol(Ref<Identifier> identifier) const;
    std::optional<ExpType> findExpValueKind(Ref<Exp> node) const;
    std::optional<int32_t> findConstantValue(Ref<Exp> node) const;
    std::optional<SemanticType> findExpType(Ref<Exp> node) const;
    std::optional<Ref<WhileStmt>> findLoop(Ref<BreakStmt> node) const;
    std::optional<Ref<WhileStmt>> findLoop(Ref<ContinueStmt> node) const;
    const SemanticFunctionControlFlow* findControlFlow(Ref<FuncDef> node) const;
    const SemanticFunctionSSA* findSSA(Ref<FuncDef> node) const;
    const SemanticControlFlowArena& controlFlowArena() const;
    std::optional<SemanticSsaAlias> findAlias(Ref<Identifier> identifier) const;

    const std::unordered_map<Ref<Identifier>, SemanticSsaAlias>&
    aliasByIdentifier() const;

    const std::unordered_map<Ref<Identifier>, int32_t>&
    symbolIdByIdentifier() const;
    const std::unordered_map<int32_t, SemanticSymbol>& symbolById() const;

private:
    const SemanticCFG *m_bindingResult;
    const SemanticSSA *m_ssaResult;
    const SemanticTypeAnalysisResult *m_typeResult;
    const SymbolResolutionResult *m_symbolResult;

    std::unique_ptr<SemanticSymbolResolver> m_symbolResolver;
    std::unique_ptr<SemanticTypeAnalyzer> m_typeAnalyzer;
    std::unique_ptr<SemanticCFGBuilder> m_loopBinder;
    std::unique_ptr<SemanticSSAAnalyzer> m_ssaAnalyzer;
};

struct SemanticOutput {
    AST m_ast;
    Ptr<CompUnit> m_root;
    SemanticInfo m_info;
    std::vector<std::unique_ptr<Diagnostic>> m_diagnostics;

    bool success() const;

    Ref<CompUnit> root();
};

class SemanticAnalyzer {
public:
    SemanticOutput analyze(const AST& ast, Ref<CompUnit> compUnit);
};

} // namespace yesod::frontend

#endif
