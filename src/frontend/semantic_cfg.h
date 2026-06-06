#ifndef _YESOD_FRONTEND_LOOP_H_
#define _YESOD_FRONTEND_LOOP_H_

#include "frontend/ast.h"
#include "frontend/diagnostic.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace yesod::frontend {

struct SemanticExpInfo;
YESOD_DECLARE_DIAGNOSTIC(BreakOutsideWhileDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(ContinueOutsideWhileDiagnostic)

struct SemanticValue {
    using Kind = std::variant<int32_t, Ref<Exp>>;

    Kind kind;
};

using SemanticBlockItem = std::variant<Decl, Ref<AssignStmt>, Ref<ExpStmt>>;

struct SemanticJumpTerminator {
    Ref<struct SemanticBasicBlock> target;
};

struct SemanticBranchTerminator {
    Ref<Exp> condition;
    Ref<struct SemanticBasicBlock> trueTarget;
    Ref<struct SemanticBasicBlock> falseTarget;
};

struct SemanticReturnTerminator {
    std::optional<SemanticValue> value;
};

using SemanticBlockTerminator = std::variant<SemanticJumpTerminator,
    SemanticBranchTerminator, SemanticReturnTerminator>;

struct SemanticBasicBlock {
    std::string nameHint;
    std::vector<SemanticBlockItem> items;
    std::optional<SemanticBlockTerminator> terminator;
};

struct SemanticFunctionControlFlow {
    Ref<FuncDef> funcDef;
    Ref<SemanticBasicBlock> entryBlock;
    Ref<SemanticBasicBlock> endBlock;
    std::vector<Ref<SemanticBasicBlock>> blocks;
};

using SemanticControlFlowArena
    = Arena<SemanticBasicBlock, SemanticFunctionControlFlow>;

class SemanticCFG {
public:
    explicit SemanticCFG(const AST& ast);
    virtual ~SemanticCFG() = default;
    [[nodiscard]] const std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>>&
    loopByBreakStmt() const;
    [[nodiscard]] const std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>>&
    loopByContinueStmt() const;
    [[nodiscard]] const SemanticFunctionControlFlow* findControlFlow(
        Ref<FuncDef> funcDef) const;
    [[nodiscard]] const SemanticControlFlowArena& controlFlowArena() const;
    [[nodiscard]] const std::vector<std::unique_ptr<Diagnostic>>&
    diagnostics() const;

    /**
     * Pre-SSA CFG simplification pass.
     * - Replaces conditional branches with constant conditions by unconditional
     * jumps.
     * - Merges a block with its unique successor when the successor has only
     * that predecessor. Must be called after constant propagation (type
     * analysis) and before SSA construction.
     */
    void simplify(const std::unordered_map<Ref<Exp>, SemanticExpInfo>& expInfo);

protected:
    const AST& ast;
    std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>> m_loopByBreakStmt;
    std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>> m_loopByContinueStmt;
    SemanticControlFlowArena m_controlFlowArena;
    std::unordered_map<Ref<FuncDef>, Ref<SemanticFunctionControlFlow>>
        m_controlFlowByFuncDef;
    std::vector<std::unique_ptr<Diagnostic>> m_diagnostics;
};

namespace detail {
    class SemanticCFGBuilderImpl;
}

class SemanticCFGBuilder {
    friend class SemanticAnalyzer;

public:
    explicit SemanticCFGBuilder(const AST& ast);
    ~SemanticCFGBuilder();

    void analyze(Ref<CompUnit> compUnit);
    void simplify(const std::unordered_map<Ref<Exp>, SemanticExpInfo>& expInfo);
    [[nodiscard]] const SemanticCFG* operator->() const;

private:
    std::unique_ptr<detail::SemanticCFGBuilderImpl> m_impl;
};

}

#endif // _YESOD_FRONTEND_LOOP_H_