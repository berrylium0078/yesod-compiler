#include "semantic_test_support.h"

#include <type_traits>

using namespace yesod::test_support::semantic;

namespace {

std::shared_ptr<ast::StmtNode> requireStmtNode(
    const std::shared_ptr<ast::BlockItemNode>& blockItem_nn)
{
    std::shared_ptr<ast::StmtNode> stmtNode_nn;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                stmtNode_nn = blockItemAlt;
            }
        },
        blockItem_nn->m_blockItem);
    require(stmtNode_nn != nullptr, "expected statement block item");
    return stmtNode_nn;
}

std::shared_ptr<ast::WhileStmt> requireWhileStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::WhileStmt> whileStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::WhileStmt>>) {
                whileStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(whileStmt_nn != nullptr, "expected while statement");
    return whileStmt_nn;
}

std::shared_ptr<ast::IfStmt> requireIfStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::IfStmt> ifStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::IfStmt>>) {
                ifStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(ifStmt_nn != nullptr, "expected if statement");
    return ifStmt_nn;
}

std::shared_ptr<ast::BreakStmt> requireBreakStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::BreakStmt> breakStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::BreakStmt>>) {
                breakStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(breakStmt_nn != nullptr, "expected break statement");
    return breakStmt_nn;
}

std::shared_ptr<ast::ContinueStmt> requireContinueStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::ContinueStmt> continueStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ast::ContinueStmt>>) {
                continueStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(continueStmt_nn != nullptr, "expected continue statement");
    return continueStmt_nn;
}

std::shared_ptr<ast::Block> requireBlockStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::Block> block_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::Block>>) {
                block_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(block_nn != nullptr, "expected block statement");
    return block_nn;
}

void testLoopControlStatementsBindToInnermostWhile()
{
    const auto root_nn = analyzeRoot(
        "int main(){while (1) {while (2) {continue;} break;} return 0;}");
    const auto outerWhile_nn = requireWhileStmt(
        requireStmtNode(root_nn->m_funcDef_nn->m_block_nn->m_blockItems[0]));
    const auto outerBody_nn = requireBlockStmt(outerWhile_nn->m_bodyStmt_nn);
    const auto innerWhile_nn = requireWhileStmt(requireStmtNode(
        outerBody_nn->m_blockItems[0]));
    const auto innerBody_nn = requireBlockStmt(innerWhile_nn->m_bodyStmt_nn);
    const auto continueStmt_nn = requireContinueStmt(requireStmtNode(
        innerBody_nn->m_blockItems[0]));
    const auto breakStmt_nn = requireBreakStmt(requireStmtNode(
        outerBody_nn->m_blockItems[1]));

    require(outerWhile_nn->m_loopTarget_nn != nullptr,
        "outer while should allocate a loop target");
    require(innerWhile_nn->m_loopTarget_nn != nullptr,
        "inner while should allocate a loop target");
    require(continueStmt_nn->m_loopTarget_nn == innerWhile_nn->m_loopTarget_nn,
        "continue should bind to the innermost containing while");
    require(breakStmt_nn->m_loopTarget_nn == outerWhile_nn->m_loopTarget_nn,
        "break should bind to the innermost containing while");
    require(continueStmt_nn->m_loopTarget_nn != breakStmt_nn->m_loopTarget_nn,
        "nested loop-control statements should preserve distinct loop targets");
}

void testNestedInnerLoopBreakAndContinueBothBindInnermostWhile()
{
    const auto root_nn = analyzeRoot(
        "int main(){while (1) {while (2) {break; continue;}} return 0;}");
    const auto outerWhile_nn = requireWhileStmt(
        requireStmtNode(root_nn->m_funcDef_nn->m_block_nn->m_blockItems[0]));
    const auto outerBody_nn = requireBlockStmt(outerWhile_nn->m_bodyStmt_nn);
    const auto innerWhile_nn = requireWhileStmt(requireStmtNode(
        outerBody_nn->m_blockItems[0]));
    const auto innerBody_nn = requireBlockStmt(innerWhile_nn->m_bodyStmt_nn);
    const auto breakStmt_nn = requireBreakStmt(requireStmtNode(
        innerBody_nn->m_blockItems[0]));
    const auto continueStmt_nn = requireContinueStmt(requireStmtNode(
        innerBody_nn->m_blockItems[1]));

    require(breakStmt_nn->m_loopTarget_nn == innerWhile_nn->m_loopTarget_nn,
        "break inside the inner loop should bind to the innermost while");
    require(
        continueStmt_nn->m_loopTarget_nn == innerWhile_nn->m_loopTarget_nn,
        "continue inside the inner loop should bind to the innermost while");
    require(breakStmt_nn->m_loopTarget_nn != outerWhile_nn->m_loopTarget_nn,
        "inner-loop break should not bind to the outer while");
}

void testLoopControlInsideWhileIfBindsContainingWhile()
{
    const auto root_nn = analyzeRoot(
        "int main(){while (1) if (2) break; else continue; return 0;}");
    const auto whileStmt_nn = requireWhileStmt(
        requireStmtNode(root_nn->m_funcDef_nn->m_block_nn->m_blockItems[0]));
    const auto ifStmt_nn = requireIfStmt(whileStmt_nn->m_bodyStmt_nn);
    const auto breakStmt_nn = requireBreakStmt(ifStmt_nn->m_thenStmt_nn);
    const auto continueStmt_nn = requireContinueStmt(ifStmt_nn->m_elseStmt_nn);

    require(breakStmt_nn->m_loopTarget_nn == whileStmt_nn->m_loopTarget_nn,
        "break inside while-if should bind to the containing while");
    require(
        continueStmt_nn->m_loopTarget_nn == whileStmt_nn->m_loopTarget_nn,
        "continue inside while-if should bind to the containing while");
}

void testBreakOutsideWhileReportsSemanticError()
{
    const auto output = analyzeSource("int main(){break; return 0;}");
    require(!output.success(),
        "break outside while should report a semantic error");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::breakOutsideWhile,
        "break outside while should use the dedicated semantic diagnostic");
}

void testContinueOutsideWhileReportsSemanticError()
{
    const auto output = analyzeSource("int main(){continue; return 0;}");
    require(!output.success(),
        "continue outside while should report a semantic error");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::continueOutsideWhile,
        "continue outside while should use the dedicated semantic diagnostic");
}

} // namespace

int main()
{
    testLoopControlStatementsBindToInnermostWhile();
    testNestedInnerLoopBreakAndContinueBothBindInnermostWhile();
    testLoopControlInsideWhileIfBindsContainingWhile();
    testBreakOutsideWhileReportsSemanticError();
    testContinueOutsideWhileReportsSemanticError();
    return 0;
}