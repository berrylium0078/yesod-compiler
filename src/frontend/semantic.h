#ifndef _YESOD_FRONTEND_SEMANTIC_H_
#define _YESOD_FRONTEND_SEMANTIC_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontend/ast.h"

namespace yesod::frontend {

enum class SemanticDiagnosticKind {
    useBeforeDefinition,
    doubleDefinition,
    nonConstantConstInitializer,
    assignToConst,
    breakOutsideWhile,
    continueOutsideWhile,
};

struct SemanticDiagnostic {
    SemanticDiagnosticKind m_kind;
    int32_t m_offset;
    std::string m_message;
};

enum class SemanticExpValueKind {
    arithmetic,
    boolean,
};

struct SemanticSymbol {
    int32_t m_id;
    std::string m_name;
    bool m_isConst;
    bool m_hasConstantValue;
    int32_t m_constantValue;
};

struct SemanticInfo {
    std::unordered_map<int32_t, SemanticSymbol> m_symbolsById;
    std::unordered_map<AstNodeId, int32_t> m_symbolIdByNodeId;
    std::unordered_map<AstNodeId, SemanticExpValueKind> m_exprKindByNodeId;
    std::unordered_map<AstNodeId, int32_t> m_constantValueByNodeId;
    std::unordered_map<AstNodeId, int32_t> m_loopIdByNodeId;

    [[nodiscard]] const SemanticSymbol* findSymbolById(int32_t symbolId) const
    {
        const auto symbolIt = m_symbolsById.find(symbolId);
        if (symbolIt == m_symbolsById.end()) {
            return nullptr;
        }
        return &symbolIt->second;
    }

    [[nodiscard]] const SemanticSymbol* findSymbolByNodeId(
        AstNodeId nodeId) const
    {
        const auto bindingIt = m_symbolIdByNodeId.find(nodeId);
        if (bindingIt == m_symbolIdByNodeId.end()) {
            return nullptr;
        }
        return findSymbolById(bindingIt->second);
    }

    [[nodiscard]] std::optional<SemanticExpValueKind> findExpValueKind(
        AstNodeId nodeId) const
    {
        const auto kindIt = m_exprKindByNodeId.find(nodeId);
        if (kindIt == m_exprKindByNodeId.end()) {
            return std::nullopt;
        }
        return kindIt->second;
    }

    [[nodiscard]] std::optional<int32_t> findConstantValue(
        AstNodeId nodeId) const
    {
        const auto constantIt = m_constantValueByNodeId.find(nodeId);
        if (constantIt == m_constantValueByNodeId.end()) {
            return std::nullopt;
        }
        return constantIt->second;
    }

    [[nodiscard]] std::optional<int32_t> findLoopId(AstNodeId nodeId) const
    {
        const auto loopIt = m_loopIdByNodeId.find(nodeId);
        if (loopIt == m_loopIdByNodeId.end()) {
            return std::nullopt;
        }
        return loopIt->second;
    }
};

struct SemanticOutput {
    std::shared_ptr<CompUnit> m_root;
    SemanticInfo m_info;
    std::vector<SemanticDiagnostic> m_diagnostics;

    [[nodiscard]] bool success() const
    {
        return m_root != nullptr && m_diagnostics.empty();
    }
};

class SemanticAnalyzer {
  public:
    [[nodiscard]] SemanticOutput analyze(
        const std::shared_ptr<CompUnit>& compUnit_nn);

  private:
    struct AnalyzedExp {
        SemanticExpValueKind m_valueKind = SemanticExpValueKind::arithmetic;
        bool m_isConstant = false;
        int32_t m_constantValue = 0;
    };

    void analyzeCompUnit(const CompUnit& compUnit);
    void analyzeFuncDef(const FuncDef& funcDef);
    void analyzeBlock(const Block& block);
    void analyzeBlockItemNode(const BlockItemNode& blockItemNode);
    void analyzeDeclNode(const DeclNode& declNode);
    void analyzeConstDecl(const ConstDecl& constDecl);
    void analyzeVarDecl(const VarDecl& varDecl);
    void analyzeStmtNode(const StmtNode& stmtNode);
    void analyzeIfStmt(const IfStmt& ifStmt);
    void analyzeWhileStmt(const WhileStmt& whileStmt);
    void analyzeBreakStmt(const BreakStmt& breakStmt);
    void analyzeContinueStmt(const ContinueStmt& continueStmt);
    void analyzeAssignStmt(const AssignStmt& assignStmt);
    void analyzeExpStmt(const ExpStmt& expStmt);
    void analyzeReturnStmt(const ReturnStmt& returnStmt);
    [[nodiscard]] AnalyzedExp analyzeExp(const Exp& exp);
    [[nodiscard]] AnalyzedExp analyzeCondExp(const Exp& exp);
    [[nodiscard]] AnalyzedExp analyzeLOrExp(const LOrExp& lOrExp);
    [[nodiscard]] AnalyzedExp analyzeLAndExp(const LAndExp& lAndExp);
    [[nodiscard]] AnalyzedExp analyzeEqExp(const EqExp& eqExp);
    [[nodiscard]] AnalyzedExp analyzeRelExp(const RelExp& relExp);
    [[nodiscard]] AnalyzedExp analyzeAddExp(const AddExp& addExp);
    [[nodiscard]] AnalyzedExp analyzeMulExp(const MulExp& mulExp);
    [[nodiscard]] AnalyzedExp analyzeUnaryExp(const UnaryExp& unaryExp);
    [[nodiscard]] AnalyzedExp analyzePrimaryExp(const PrimaryExp& primaryExp);
    [[nodiscard]] std::optional<int32_t> lookupSymbol(
        const std::string& name) const;
    [[nodiscard]] int32_t resolveSymbol(const Identifier& identifier);
    [[nodiscard]] int32_t makePlaceholderSymbol(const Identifier& identifier);
    [[nodiscard]] int32_t makeSymbol(const Identifier& identifier,
        bool isConst, bool hasConstantValue, int32_t constantValue);
    [[nodiscard]] AnalyzedExp normalizeToArithmetic(
        const AstNode& node, AnalyzedExp analyzedExp);
    [[nodiscard]] AnalyzedExp normalizeToBoolean(
        const AstNode& node, AnalyzedExp analyzedExp);
    void bindSymbol(const AstNode& node, int32_t symbolId);
    void bindLoop(const AstNode& node, int32_t loopId);
    void recordExpFacts(const AstNode& node, const AnalyzedExp& analyzedExp);
    void pushScope();
    void popScope();
    [[nodiscard]] bool defineSymbol(const std::string& name, int32_t symbolId);
    [[nodiscard]] std::optional<int32_t> currentLoopId() const;
    void recordDiagnostic(SemanticDiagnosticKind kind, int32_t offset,
        std::string message);

    using Scope = std::unordered_map<std::string, int32_t>;

    std::shared_ptr<CompUnit> m_root_nn;
    SemanticInfo m_info;
    std::vector<Scope> m_scopeStack;
    std::vector<int32_t> m_loopIdStack;
    std::vector<SemanticDiagnostic> m_diagnostics;
    int32_t m_nextSymbolId = 0;
    int32_t m_nextLoopId = 0;
};

} // namespace yesod::frontend

#endif
