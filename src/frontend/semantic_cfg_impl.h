#ifndef _YESOD_FRONTEND_LOOP_IMPL_H_
#define _YESOD_FRONTEND_LOOP_IMPL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontend/semantic_cfg.h"

namespace yesod::frontend::detail {
class SemanticCFGBuilderImpl : public SemanticCFG {
public:
    explicit SemanticCFGBuilderImpl(const AST& ast);
    ~SemanticCFGBuilderImpl() = default;
    void analyze(Ref<CompUnit> compUnit);

private:
    struct LoopContext {
        Ref<WhileStmt> whileStmt;
        Ref<SemanticBasicBlock> condBlock;
        Ref<SemanticBasicBlock> endBlock;
    };

    class FunctionBuilder;

    [[nodiscard]] std::optional<LoopContext> currentLoop() const;
    void buildFunctionControlFlow(Ref<FuncDef> funcDef);
    void bindBreakStmt(Ref<BreakStmt> breakStmt);
    void bindContinueStmt(Ref<ContinueStmt> continueStmt);

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

    std::vector<LoopContext> m_loopStack;
    int32_t m_nextGeneratedBlockId = 1;
};

} // namespace yesod::frontend::detail

#endif // _YESOD_FRONTEND_LOOP_IMPL_H_