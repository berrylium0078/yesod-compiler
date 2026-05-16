#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

ast::Handle<ast::StmtNode> requireStmtNode(
    const ast::Handle<ast::BlockItemNode>& blockItem_nn)
{
    ast::Handle<ast::StmtNode> stmtNode_nn;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::StmtNode>>) {
                stmtNode_nn = blockItemAlt;
            }
        },
        blockItem_nn->m_blockItem);
    require(stmtNode_nn != nullptr, "expected statement block item");
    return stmtNode_nn;
}

ast::Handle<ast::WhileStmt> requireWhileStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::WhileStmt> whileStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              ast::Handle<ast::WhileStmt>>) {
                whileStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(whileStmt_nn != nullptr, "expected while statement");
    return whileStmt_nn;
}

ast::Handle<ast::IfStmt> requireIfStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::IfStmt> ifStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::IfStmt>>) {
                ifStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(ifStmt_nn != nullptr, "expected if statement");
    return ifStmt_nn;
}

ast::Handle<ast::BreakStmt> requireBreakStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::BreakStmt> breakStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              ast::Handle<ast::BreakStmt>>) {
                breakStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(breakStmt_nn != nullptr, "expected break statement");
    return breakStmt_nn;
}

ast::Handle<ast::ContinueStmt> requireContinueStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::ContinueStmt> continueStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              ast::Handle<ast::ContinueStmt>>) {
                continueStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(continueStmt_nn != nullptr, "expected continue statement");
    return continueStmt_nn;
}

ast::Handle<ast::Block> requireBlockStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::Block> block_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::Block>>) {
                block_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(block_nn != nullptr, "expected block statement");
    return block_nn;
}

void testLoopControlStatementsBindToInnermostWhile()
{
    const auto output = analyzeSource(
        "int main(){while (1) {while (2) {continue;} break;} return 0;}");
    require(output.success(), "expected semantic success");

    const auto outerWhile_nn = requireWhileStmt(requireStmtNode(
        output.m_root->m_funcDef_nn->m_block_nn->m_blockItems[0]));
    const auto outerBody_nn = requireBlockStmt(outerWhile_nn->m_bodyStmt_nn);
    const auto innerWhile_nn
        = requireWhileStmt(requireStmtNode(outerBody_nn->m_blockItems[0]));
    const auto innerBody_nn = requireBlockStmt(innerWhile_nn->m_bodyStmt_nn);
    const auto continueStmt_nn
        = requireContinueStmt(requireStmtNode(innerBody_nn->m_blockItems[0]));
    const auto breakStmt_nn
        = requireBreakStmt(requireStmtNode(outerBody_nn->m_blockItems[1]));

    const auto outerLoop = requireLoop(output, outerWhile_nn);
    const auto innerLoop = requireLoop(output, innerWhile_nn);
    require(requireLoop(output, continueStmt_nn) == innerLoop,
        "continue should bind to the innermost containing while");
    require(requireLoop(output, breakStmt_nn) == outerLoop,
        "break should bind to the innermost containing while");
    require(innerLoop != outerLoop,
        "nested while statements should keep distinct loop identities");
}

void testNestedInnerLoopBreakAndContinueBothBindInnermostWhile()
{
    const auto output = analyzeSource(
        "int main(){while (1) {while (2) {break; continue;}} return 0;}");
    require(output.success(), "expected semantic success");

    const auto outerWhile_nn = requireWhileStmt(requireStmtNode(
        output.m_root->m_funcDef_nn->m_block_nn->m_blockItems[0]));
    const auto outerBody_nn = requireBlockStmt(outerWhile_nn->m_bodyStmt_nn);
    const auto innerWhile_nn
        = requireWhileStmt(requireStmtNode(outerBody_nn->m_blockItems[0]));
    const auto innerBody_nn = requireBlockStmt(innerWhile_nn->m_bodyStmt_nn);
    const auto breakStmt_nn
        = requireBreakStmt(requireStmtNode(innerBody_nn->m_blockItems[0]));
    const auto continueStmt_nn
        = requireContinueStmt(requireStmtNode(innerBody_nn->m_blockItems[1]));

    const auto innerLoop = requireLoop(output, innerWhile_nn);
    require(requireLoop(output, breakStmt_nn) == innerLoop,
        "break inside the inner loop should bind to the innermost while");
    require(requireLoop(output, continueStmt_nn) == innerLoop,
        "continue inside the inner loop should bind to the innermost while");
    require(innerLoop != requireLoop(output, outerWhile_nn),
        "inner-loop control flow should not bind to the outer while");
}

void testLoopControlInsideWhileIfBindsContainingWhile()
{
    const auto output = analyzeSource(
        "int main(){while (1) if (2) break; else continue; return 0;}");
    require(output.success(), "expected semantic success");

    const auto whileStmt_nn = requireWhileStmt(requireStmtNode(
        output.m_root->m_funcDef_nn->m_block_nn->m_blockItems[0]));
    const auto ifStmt_nn = requireIfStmt(whileStmt_nn->m_bodyStmt_nn);
    const auto breakStmt_nn = requireBreakStmt(ifStmt_nn->m_thenStmt_nn);
    const auto continueStmt_nn = requireContinueStmt(ifStmt_nn->m_elseStmt_nn);

    const auto whileLoop = requireLoop(output, whileStmt_nn);
    require(requireLoop(output, breakStmt_nn) == whileLoop,
        "break inside while-if should bind to the containing while");
    require(requireLoop(output, continueStmt_nn) == whileLoop,
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
