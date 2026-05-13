#ifndef _YESOD_FRONTEND_SEMANTIC_H_
#define _YESOD_FRONTEND_SEMANTIC_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontend/ast.h"
#include "frontend/semantic_ast.h"

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

    struct SemanticOutput {
        std::shared_ptr<semantic::CompUnit> m_root;
        std::vector<SemanticDiagnostic> m_diagnostics;

        [[nodiscard]] bool success() const
        {
            return m_root != nullptr && m_diagnostics.empty();
        }
    };

    class SemanticAnalyzer {
      public:
        [[nodiscard]] SemanticOutput analyze(const CompUnit& compUnit);

      private:
        enum class ExpValueKind {
            arithmetic,
            boolean,
        };

        struct AnalyzedExp {
            std::shared_ptr<semantic::Exp> m_exp_nn;
            ExpValueKind m_valueKind = ExpValueKind::arithmetic;
            bool m_isConstant = false;
            int32_t m_constantValue = 0;
        };

        [[nodiscard]] std::shared_ptr<semantic::CompUnit> analyzeCompUnit(
            const CompUnit& compUnit);
        [[nodiscard]] std::shared_ptr<semantic::FuncDef> analyzeFuncDef(
            const FuncDef& funcDef);
        [[nodiscard]] std::shared_ptr<semantic::Block> analyzeBlock(
            const Block& block);
        [[nodiscard]] std::shared_ptr<semantic::BlockItemNode>
        analyzeBlockItemNode(const BlockItemNode& blockItemNode);
        [[nodiscard]] std::shared_ptr<semantic::DeclNode> analyzeDeclNode(
            const DeclNode& declNode);
        [[nodiscard]] std::shared_ptr<semantic::ConstDecl> analyzeConstDecl(
            const ConstDecl& constDecl);
        [[nodiscard]] std::shared_ptr<semantic::VarDecl> analyzeVarDecl(
            const VarDecl& varDecl);
        [[nodiscard]] std::shared_ptr<semantic::StmtNode> analyzeStmtNode(
            const StmtNode& stmtNode);
        [[nodiscard]] std::shared_ptr<semantic::IfStmt> analyzeIfStmt(
            const IfStmt& ifStmt);
        [[nodiscard]] std::shared_ptr<semantic::WhileStmt> analyzeWhileStmt(
            const WhileStmt& whileStmt);
        [[nodiscard]] std::shared_ptr<semantic::BreakStmt> analyzeBreakStmt(
            const BreakStmt& breakStmt);
        [[nodiscard]] std::shared_ptr<semantic::ContinueStmt>
        analyzeContinueStmt(const ContinueStmt& continueStmt);
        [[nodiscard]] std::shared_ptr<semantic::AssignStmt> analyzeAssignStmt(
            const AssignStmt& assignStmt);
        [[nodiscard]] std::shared_ptr<semantic::ExpStmt> analyzeExpStmt(
            const ExpStmt& expStmt);
        [[nodiscard]] std::shared_ptr<semantic::ReturnStmt> analyzeReturnStmt(
            const ReturnStmt& returnStmt);
        [[nodiscard]] AnalyzedExp analyzeExp(const Exp& exp);
        [[nodiscard]] AnalyzedExp analyzeCondExp(const Exp& exp);
        [[nodiscard]] AnalyzedExp analyzeLOrExp(const LOrExp& lOrExp);
        [[nodiscard]] AnalyzedExp analyzeLAndExp(const LAndExp& lAndExp);
        [[nodiscard]] AnalyzedExp analyzeEqExp(const EqExp& eqExp);
        [[nodiscard]] AnalyzedExp analyzeRelExp(const RelExp& relExp);
        [[nodiscard]] AnalyzedExp analyzeAddExp(const AddExp& addExp);
        [[nodiscard]] AnalyzedExp analyzeMulExp(const MulExp& mulExp);
        [[nodiscard]] AnalyzedExp analyzeUnaryExp(const UnaryExp& unaryExp);
        [[nodiscard]] AnalyzedExp analyzePrimaryExp(
            const PrimaryExp& primaryExp);
        [[nodiscard]] std::optional<std::shared_ptr<semantic::Symbol>>
        lookupSymbol(const std::string& name) const;
        [[nodiscard]] std::shared_ptr<semantic::Symbol> resolveSymbol(
            const Identifier& identifier);
        [[nodiscard]] std::shared_ptr<semantic::Symbol> makePlaceholderSymbol(
            const Identifier& identifier) const;
        [[nodiscard]] std::shared_ptr<semantic::Symbol> makeSymbol(
            const Identifier& identifier, bool isConst, bool hasConstantValue,
            int32_t constantValue) const;
        [[nodiscard]] std::shared_ptr<semantic::Exp> makeNumberExp(
            int32_t startOffset, int32_t value) const;
        [[nodiscard]] std::shared_ptr<semantic::Exp> makeLValExp(
            int32_t startOffset,
            const std::shared_ptr<semantic::Symbol>& symbol_nn) const;
        [[nodiscard]] std::shared_ptr<semantic::Exp> makeUnaryExp(
            int32_t startOffset, UnaryOpKeyword op,
            const std::shared_ptr<semantic::Exp>& operand_nn) const;
        [[nodiscard]] std::shared_ptr<semantic::Exp> makeBinaryExp(
            int32_t startOffset, semantic::BinaryExp::Op op,
            const std::shared_ptr<semantic::Exp>& lhs_nn,
            const std::shared_ptr<semantic::Exp>& rhs_nn) const;
        [[nodiscard]] std::shared_ptr<semantic::Exp> makeIntToBoolExp(
            int32_t startOffset,
            const std::shared_ptr<semantic::Exp>& operand_nn) const;
        [[nodiscard]] std::shared_ptr<semantic::Exp> makeBoolToIntExp(
            int32_t startOffset,
            const std::shared_ptr<semantic::Exp>& operand_nn) const;
        [[nodiscard]] std::shared_ptr<semantic::Exp> makeBooleanConstantExp(
            int32_t startOffset, int32_t value) const;
        [[nodiscard]] AnalyzedExp normalizeToArithmetic(
            int32_t startOffset, AnalyzedExp analyzedExp) const;
        [[nodiscard]] AnalyzedExp normalizeToBoolean(
            int32_t startOffset, AnalyzedExp analyzedExp) const;
        void pushScope();
        void popScope();
        [[nodiscard]] bool defineSymbol(
            const std::shared_ptr<semantic::Symbol>& symbol_nn);
        [[nodiscard]] std::shared_ptr<semantic::LoopTarget>
        currentLoopTarget() const;
        void recordDiagnostic(SemanticDiagnosticKind kind, int32_t offset,
            std::string message);

        using Scope = std::unordered_map<std::string, std::shared_ptr<semantic::Symbol>>;

        std::vector<Scope> m_scopeStack;
        std::vector<std::shared_ptr<semantic::LoopTarget>> m_loopTargetStack;
        std::vector<SemanticDiagnostic> m_diagnostics;
    };

} // namespace yesod::frontend

#endif
