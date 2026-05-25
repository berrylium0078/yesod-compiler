#ifndef _YESOD_FRONTEND_LOOP_IMPL_H_
#define _YESOD_FRONTEND_LOOP_IMPL_H_

#include "frontend/semantic_loop.h"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>

namespace yesod::frontend::detail {
class SemanticLoopBinderImpl : private AstVisitor,
                               public SemanticLoopBindingResult {
public:
    explicit SemanticLoopBinderImpl(const AST& ast);
    ~SemanticLoopBinderImpl() = default;
    void analyze(Ref<CompUnit> compUnit);

protected:
    void visitWhileStmt(Ref<WhileStmt> whileStmt) override;
    void visitBreakStmt(Ref<BreakStmt> breakStmt) override;
    void visitContinueStmt(Ref<ContinueStmt> continueStmt) override;

private:
    [[nodiscard]] std::optional<Ref<WhileStmt>> currentLoop() const;

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

    std::vector<Ref<WhileStmt>> m_loopStack;
};

} // namespace yesod::frontend::detail

#endif // _YESOD_FRONTEND_LOOP_IMPL_H_