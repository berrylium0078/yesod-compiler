#ifndef _YESOD_FRONTEND_SEMANTIC_TYPE_IMPL_H_
#define _YESOD_FRONTEND_SEMANTIC_TYPE_IMPL_H_

#include <memory>
#include <optional>
#include <unordered_set>

#include "frontend/semantic_symbol_impl.h"
#include "frontend/semantic_type.h"

namespace yesod::frontend::detail {

class SemanticTypeAnalyzerImpl : private AstVisitor,
                                 public SemanticTypeAnalysisResult {
public:
    explicit SemanticTypeAnalyzerImpl(
        const AST& ast, const SemanticSymbolResolver& symbolResolver);
    virtual ~SemanticTypeAnalyzerImpl() = default;
    void analyze(Ref<CompUnit> compUnit);

protected:
    void visitCompUnit(Ref<CompUnit> compUnit) override;
    void visitFuncDef(Ref<FuncDef> funcDef) override;
    void visitConstDecl(Ref<ConstDecl> constDecl) override;
    void visitVarDecl(Ref<VarDecl> varDecl) override;
    void visitIfStmt(Ref<IfStmt> ifStmt) override;
    void visitWhileStmt(Ref<WhileStmt> whileStmt) override;
    void visitAssignStmt(Ref<AssignStmt> assignStmt) override;
    void visitExpStmt(Ref<ExpStmt> expStmt) override;
    void visitReturnStmt(Ref<ReturnStmt> returnStmt) override;
    void visitExp(Ref<Exp> exp) override;

private:
    struct AnalyzedExp {
        SemanticType m_type = SemanticType::makeInteger();
        ExpType m_valueKind = ExpType::integer;
        bool m_isConstant = false;
        int32_t m_constantValue = 0;
    };

    void declareFuncDef(Ref<FuncDef> funcDef);
    [[nodiscard]] AnalyzedExp analyzeExp(Ref<Exp> exp);
    [[nodiscard]] AnalyzedExp analyzeBinaryExp(
        const Exp& exp, const Exp::Binary& binary);
    [[nodiscard]] AnalyzedExp analyzeUnaryExp(
        const Exp& exp, const Exp::Unary& unary);
    [[nodiscard]] AnalyzedExp analyzeCastExp(
        const Exp& exp, const Exp::Cast& cast);
    [[nodiscard]] AnalyzedExp analyzeCallExp(
        const Exp& exp, const Exp::Call& call);
    [[nodiscard]] AnalyzedExp analyzeLValExp(
        const Exp& exp, const Exp::LVal& lVal);
    [[nodiscard]] AnalyzedExp analyzeSliceExp(
        const Exp& exp, const Exp::Slice& slice);
    [[nodiscard]] AnalyzedExp analyzeSubscriptExp(
        const Exp& exp, const Exp::Subscript& subscript);
    [[nodiscard]] AnalyzedExp analyzeCondExp(Ref<Exp> exp);
    [[nodiscard]] SemanticType analyzeObjectType(BTypeKeyword bType,
        const std::vector<Ref<Exp>>& dimensions, int32_t offset,
        bool allowUnsizedFirstDimension = false);
    [[nodiscard]] AnalyzedExp analyzeConstInitVal(
        Ref<ConstInitVal> constInitVal, const SemanticType& expectedType,
        bool isOutermost, size_t& nextIndex, bool& hasRemainingWarning);
    [[nodiscard]] AnalyzedExp analyzeConstInitSequence(
        const std::vector<Ref<ConstInitVal>>& values, size_t& nextValueIndex,
        const SemanticType& expectedType, bool& hasRemainingWarning);
    [[nodiscard]] AnalyzedExp analyzeInitVal(Ref<InitVal> initVal,
        const SemanticType& expectedType, bool isGlobal, bool isOutermost,
        size_t& nextIndex, bool& hasRemainingWarning);
    [[nodiscard]] AnalyzedExp analyzeInitSequence(
        const std::vector<Ref<InitVal>>& values, size_t& nextValueIndex,
        const SemanticType& expectedType, bool isGlobal,
        bool& hasRemainingWarning);
    [[nodiscard]] bool typesMatchForCall(
        const SemanticType& paramType, const SemanticType& argType) const;
    [[nodiscard]] std::optional<int32_t> resolvedSymbolId(
        Ref<Identifier> identifier) const;
    [[nodiscard]] SemanticSymbol makeObjectSymbol(int32_t symbolId,
        Ref<Identifier> identifier, bool isConst,
        std::optional<int32_t> constantValue, const SemanticType& type) const;
    [[nodiscard]] SemanticSymbol makeFunctionSymbol(Ref<Identifier> identifier,
        int32_t symbolId, const SemanticType& returnType,
        const std::vector<SemanticType>& paramTypes) const;
    [[nodiscard]] AnalyzedExp normalizeToArithmetic(
        AnalyzedExp analyzedExp) const;
    [[nodiscard]] AnalyzedExp normalizeToBoolean(AnalyzedExp analyzedExp) const;
    void recordExpFacts(Ref<Exp> exp, const AnalyzedExp& analyzedExp);

    template <typename T>
    void recordDiagnostic(int32_t offset, std::string message,
        DiagnosticSeverity severity = DiagnosticSeverity::error)
    {
        m_diagnostics.push_back(
            makeDiagnostic<T>(offset, std::move(message), severity));
    }

    template <typename T>
    [[nodiscard]] std::unique_ptr<Diagnostic> makeDiagnostic(
        int32_t offset, std::string message, DiagnosticSeverity severity) const
    {
        return std::make_unique<T>(offset, std::move(message), severity);
    }

    const SemanticSymbolResolver& m_symbolResolver;
    std::optional<SemanticType> m_currentFuncReturnType;
    std::unordered_set<int32_t> m_definedFunctionSymbolIds;
};

} // namespace yesod::frontend::detail

#endif // _YESOD_FRONTEND_SEMANTIC_TYPE_IMPL_H_