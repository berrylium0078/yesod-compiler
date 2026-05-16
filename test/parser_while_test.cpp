#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testWhileLoopParsesWithLoopControlStatements()
{
    const auto root_nn
        = parseRoot("int main(){while (1) {continue; break;} return 0;}");
    const auto& blockItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;

    require(blockItems.size() == 2,
        "top-level block should contain one while statement and one return");

    const auto whileStmt_nn = extractWhileStmt(blockItems[0]);
    require(evaluateExp(*whileStmt_nn->m_condExp_nn) == 1,
        "while condition should reuse expression parsing");

    const auto bodyBlock_nn = extractBlockStmt(whileStmt_nn->m_bodyStmt_nn);
    require(bodyBlock_nn->m_blockItems.size() == 2,
        "while body block should preserve both loop-control statements");
    require(extractContinueStmt(extractStmtNode(bodyBlock_nn->m_blockItems[0]))
            != nullptr,
        "while body should preserve continue statements");
    require(extractBreakStmt(extractStmtNode(bodyBlock_nn->m_blockItems[1]))
            != nullptr,
        "while body should preserve break statements");
}

void testNestedWhileLoopParsesInnerLoopControlStatements()
{
    const auto root_nn = parseRoot(
        "int main(){while (1) {while (2) {break; continue;}} return 0;}");
    const auto outerWhile_nn
        = extractWhileStmt(root_nn->m_funcDef_nn->m_block_nn->m_blockItems[0]);
    const auto outerBody_nn = extractBlockStmt(outerWhile_nn->m_bodyStmt_nn);
    const auto innerWhile_nn = extractWhileStmt(outerBody_nn->m_blockItems[0]);
    const auto innerBody_nn = extractBlockStmt(innerWhile_nn->m_bodyStmt_nn);

    require(innerBody_nn->m_blockItems.size() == 2,
        "inner while body should preserve both break and continue statements");
    require(extractBreakStmt(extractStmtNode(innerBody_nn->m_blockItems[0]))
            != nullptr,
        "nested inner loop should preserve break statements");
    require(extractContinueStmt(extractStmtNode(innerBody_nn->m_blockItems[1]))
            != nullptr,
        "nested inner loop should preserve continue statements");
}

void testLoopControlInsideWhileIfParses()
{
    const auto root_nn = parseRoot(
        "int main(){while (1) if (2) break; else continue; return 0;}");
    const auto whileStmt_nn
        = extractWhileStmt(root_nn->m_funcDef_nn->m_block_nn->m_blockItems[0]);
    const auto ifStmt_nn = extractIfStmt(whileStmt_nn->m_bodyStmt_nn);

    require(evaluateExp(*ifStmt_nn->m_condExp_nn) == 2,
        "while-if body should preserve the nested if condition");
    require(extractBreakStmt(ifStmt_nn->m_thenStmt_nn) != nullptr,
        "while-if then branch should preserve break statements");
    require(extractContinueStmt(ifStmt_nn->m_elseStmt_nn) != nullptr,
        "while-if else branch should preserve continue statements");
}

void testWhileStatementRecovery()
{
    const auto missingRParen
        = parseSource("int main(){while (1 return 1; return 0;}");
    require(!missingRParen.success(),
        "missing while-condition ')' should report recovery diagnostics");
    require(firstDiagnostic(missingRParen).m_kind
            == DiagnosticKind::missingWhileRParen,
        "missing while-condition ')' should use the dedicated diagnostic");
    require(missingRParen.m_root != nullptr,
        "missing while-condition ')' should recover to a root");

    const auto malformedCond = parseSource("int main(){while (@) return 1;}");
    require(!malformedCond.success(),
        "malformed while condition should report recovery diagnostics");
    require(firstDiagnostic(malformedCond).m_kind
            == DiagnosticKind::malformedWhileCond,
        "malformed while condition should use the dedicated diagnostic");
}

void testBreakAndContinueRecoveryMakeForwardProgress()
{
    const auto missingBreakSemicolon
        = parseSource("int main(){while (1) {break return 0;} return 1;}");
    require(!missingBreakSemicolon.success(),
        "missing break semicolon should report recovery diagnostics");
    require(firstDiagnostic(missingBreakSemicolon).m_kind
            == DiagnosticKind::missingBreakSemicolon,
        "missing break semicolon should use the dedicated diagnostic");
    require(missingBreakSemicolon.m_root != nullptr,
        "missing break semicolon should still recover to a root");
    require(missingBreakSemicolon.m_root->m_funcDef_nn->m_block_nn->m_blockItems
                .size()
            == 2,
        "missing break semicolon recovery should continue to the following "
        "statement");

    const auto missingContinueSemicolon
        = parseSource("int main(){while (1) {continue return 0;} return 1;}");
    require(!missingContinueSemicolon.success(),
        "missing continue semicolon should report recovery diagnostics");
    require(firstDiagnostic(missingContinueSemicolon).m_kind
            == DiagnosticKind::missingContinueSemicolon,
        "missing continue semicolon should use the dedicated diagnostic");
    require(missingContinueSemicolon.m_root != nullptr,
        "missing continue semicolon should still recover to a root");
}

} // namespace

int main()
{
    testWhileLoopParsesWithLoopControlStatements();
    testNestedWhileLoopParsesInnerLoopControlStatements();
    testLoopControlInsideWhileIfParses();
    testWhileStatementRecovery();
    testBreakAndContinueRecoveryMakeForwardProgress();
    return 0;
}