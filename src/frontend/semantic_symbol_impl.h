#ifndef _YESOD_FRONTEND_SYMBOL_IMPL_H_
#define _YESOD_FRONTEND_SYMBOL_IMPL_H_

#include <memory>
#include <optional>
#include <unordered_set>

#include "frontend/semantic_symbol.h"

namespace yesod::frontend::detail {

class SemanticSymbolResolverImpl : private AstVisitor,
                                   public SymbolResolutionResult {
public:
    explicit SemanticSymbolResolverImpl(const AST& ast);
    virtual ~SemanticSymbolResolverImpl() = default;
    void analyze(Ref<CompUnit> compUnit);

protected:
    void visitCompUnit(Ref<CompUnit> compUnit) override;
    void visitFuncDef(Ref<FuncDef> funcDef) override;
    void visitBlock(Ref<Block> block) override;
    void visitConstDecl(Ref<ConstDecl> constDecl) override;
    void visitVarDecl(Ref<VarDecl> varDecl) override;
    void visitCallExp(const Exp& exp, const Exp::Call& call) override;
    void visitLValExp(const Exp& exp, const Exp::LVal& lVal) override;

private:
    using Scope = std::unordered_map<std::string, Ref<Identifier>>;

    void declareFuncDef(Ref<FuncDef> funcDef);
    void pushScope();
    void popScope();
    [[nodiscard]] bool defineSymbol(
        const std::string& name, Ref<Identifier> identifier);
    [[nodiscard]] std::optional<Ref<Identifier>> lookupSymbol(
        const std::string& name) const;
    [[nodiscard]] int32_t resolveIdentifier(Ref<Identifier> identifier);
    void bindSymbolId(Ref<Identifier> identifier, int32_t symbolId);
    [[nodiscard]] int32_t makeFreshSymbolId();

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
};

} // namespace yesod::frontend::detail

#endif // _YESOD_FRONTEND_SYMBOL_IMPL_H_