#ifndef _YESOD_FRONTEND_LOOP_H_
#define _YESOD_FRONTEND_LOOP_H_

#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include "frontend/ast.h"
#include "frontend/diagnostic.h"

namespace yesod::frontend {

YESOD_DECLARE_DIAGNOSTIC(BreakOutsideWhileDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(ContinueOutsideWhileDiagnostic)

class SemanticLoopBindingResult {
public:
    explicit SemanticLoopBindingResult(const AST& ast);
    virtual ~SemanticLoopBindingResult() = default;
    [[nodiscard]] const std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>>&
    loopByBreakStmt() const;
    [[nodiscard]] const std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>>&
    loopByContinueStmt() const;
    [[nodiscard]] const std::vector<std::unique_ptr<Diagnostic>>&
    diagnostics() const;

protected:
    const AST& ast;
    std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>> m_loopByBreakStmt;
    std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>> m_loopByContinueStmt;
    std::vector<std::unique_ptr<Diagnostic>> m_diagnostics;
};

namespace detail {
    class SemanticLoopBinderImpl;
}

class SemanticLoopBinder {
    friend class SemanticAnalyzer;
public:
    explicit SemanticLoopBinder(const AST& ast);
    ~SemanticLoopBinder();

    void analyze(Ref<CompUnit> compUnit);
    [[nodiscard]] const SemanticLoopBindingResult* operator->() const;

private:
    std::unique_ptr<detail::SemanticLoopBinderImpl> m_impl;
};

}

#endif // _YESOD_FRONTEND_LOOP_H_