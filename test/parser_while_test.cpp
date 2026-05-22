#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserWhileTest : ParserTestBase {
    void testWhileLoopParsesWithLoopControlStatements()
    {
        parseRoot("int main(){while (1) {continue; break;} return 0;}");
        const auto funcDef_nn = firstFuncDef();
        const auto& blockItems = funcDef_nn(ast()).m_block_nn(ast()).m_blockItems;

        require(blockItems.size() == 2,
            "top-level block should contain one while statement and one return");

        const auto whileStmt_nn = extractWhileStmt(blockItems[0]);
        require(evaluateExp(whileStmt_nn(ast()).m_condExp_nn) == 1,
            "while condition should reuse expression parsing");

        const auto bodyBlock_nn = extractBlockStmt(whileStmt_nn(ast()).m_bodyStmt_nn);
        require(bodyBlock_nn(ast()).m_blockItems.size() == 2,
            "while body block should preserve both loop-control statements");
        require(extractContinueStmt(
                    extractStmtNode(bodyBlock_nn(ast()).m_blockItems[0]))
                != nullptr,
            "while body should preserve continue statements");
        require(extractBreakStmt(
                    extractStmtNode(bodyBlock_nn(ast()).m_blockItems[1]))
                != nullptr,
            "while body should preserve break statements");
    }

    void testNestedWhileLoopParsesInnerLoopControlStatements()
    {
        parseRoot(
            "int main(){while (1) {while (2) {break; continue;}} return 0;}");
        const auto funcDef_nn = firstFuncDef();
        const auto outerWhile_nn = extractWhileStmt(
            funcDef_nn(ast()).m_block_nn(ast()).m_blockItems[0]);
        const auto outerBody_nn = extractBlockStmt(outerWhile_nn(ast()).m_bodyStmt_nn);
        const auto innerWhile_nn = extractWhileStmt(outerBody_nn(ast()).m_blockItems[0]);
        const auto innerBody_nn = extractBlockStmt(innerWhile_nn(ast()).m_bodyStmt_nn);

        require(innerBody_nn(ast()).m_blockItems.size() == 2,
            "inner while body should preserve both break and continue statements");
        require(extractBreakStmt(
                    extractStmtNode(innerBody_nn(ast()).m_blockItems[0]))
                != nullptr,
            "nested inner loop should preserve break statements");
        require(extractContinueStmt(
                    extractStmtNode(innerBody_nn(ast()).m_blockItems[1]))
                != nullptr,
            "nested inner loop should preserve continue statements");
    }

    void testLoopControlInsideWhileIfParses()
    {
        parseRoot(
            "int main(){while (1) if (2) break; else continue; return 0;}");
        const auto funcDef_nn = firstFuncDef();
        const auto whileStmt_nn = extractWhileStmt(
            funcDef_nn(ast()).m_block_nn(ast()).m_blockItems[0]);
        const auto ifStmt_nn = extractIfStmt(whileStmt_nn(ast()).m_bodyStmt_nn);

        require(evaluateExp(ifStmt_nn(ast()).m_condExp_nn) == 2,
            "while-if body should preserve the nested if condition");
        require(extractBreakStmt(ifStmt_nn(ast()).m_thenStmt_nn) != nullptr,
            "while-if then branch should preserve break statements");
        require(extractContinueStmt(ifStmt_nn(ast()).m_elseStmt_nn) != nullptr,
            "while-if else branch should preserve continue statements");
    }

    void testWhileStatementRecovery()
    {
        parseSource("int main(){while (1 return 1; return 0;}");
        require(!success(),
            "missing while-condition ')' should report recovery diagnostics");
        require(firstDiagnostic().m_kind == DiagnosticKind::missingWhileRParen,
            "missing while-condition ')' should use the dedicated diagnostic");
        require(root() != nullptr,
            "missing while-condition ')' should recover to a root");

        parseSource("int main(){while (@) return 1;}");
        require(!success(),
            "malformed while condition should report recovery diagnostics");
        require(firstDiagnostic().m_kind
                == DiagnosticKind::malformedWhileCond,
            "malformed while condition should use the dedicated diagnostic");
    }

    void testBreakAndContinueRecoveryMakeForwardProgress()
    {
        parseSource("int main(){while (1) {break return 0;} return 1;}");
        require(!success(),
            "missing break semicolon should report recovery diagnostics");
        require(firstDiagnostic().m_kind
                == DiagnosticKind::missingBreakSemicolon,
            "missing break semicolon should use the dedicated diagnostic");
        require(root() != nullptr,
            "missing break semicolon should still recover to a root");
        require(firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.size()
                == 2,
            "missing break semicolon recovery should continue to the following "
            "statement");

        parseSource(
            "int main(){while (1) {continue return 0;} return 1;}");
        require(!success(),
            "missing continue semicolon should report recovery diagnostics");
        require(firstDiagnostic().m_kind
                == DiagnosticKind::missingContinueSemicolon,
            "missing continue semicolon should use the dedicated diagnostic");
        require(root() != nullptr,
            "missing continue semicolon should still recover to a root");
    }
};

} // namespace

int main()
{
    ParserWhileTest test;
    test.testWhileLoopParsesWithLoopControlStatements();
    test.testNestedWhileLoopParsesInnerLoopControlStatements();
    test.testLoopControlInsideWhileIfParses();
    test.testWhileStatementRecovery();
    test.testBreakAndContinueRecoveryMakeForwardProgress();
    return 0;
}