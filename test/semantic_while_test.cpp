#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

struct SemanticWhileTest : SemanticTestBase {
    void testLoopControlStatementsBindToInnermostWhile()
    {
        m_output = analyzeSource(
            "int main(){while (1) {while (2) {continue;} break;} return 0;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto outerWhile_nn
            = extractWhileStmt(funcDef_nn(ast()).body(ast()).items[0]);
        const auto outerBody_nn = extractBlockStmt(outerWhile_nn(ast()).body);
        const auto innerWhile_nn
            = extractWhileStmt(outerBody_nn(ast()).items[0]);
        const auto innerBody_nn = extractBlockStmt(innerWhile_nn(ast()).body);
        const auto continueStmt_nn = extractContinueStmt(
            extractStmtNode(innerBody_nn(ast()).items[0]));
        const auto breakStmt_nn
            = extractBreakStmt(extractStmtNode(outerBody_nn(ast()).items[1]));

        const auto outerLoop = outerWhile_nn;
        const auto innerLoop = innerWhile_nn;
        require(requireLoop(m_output, continueStmt_nn) == innerLoop,
            "continue should bind to the innermost containing while");
        require(requireLoop(m_output, breakStmt_nn) == outerLoop,
            "break should bind to the innermost containing while");
        require(innerLoop != outerLoop,
            "nested while statements should keep distinct loop identities");
    }

    void testNestedInnerLoopBreakAndContinueBothBindInnermostWhile()
    {
        m_output = analyzeSource(
            "int main(){while (1) {while (2) {break; continue;}} return 0;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto outerWhile_nn
            = extractWhileStmt(funcDef_nn(ast()).body(ast()).items[0]);
        const auto outerBody_nn = extractBlockStmt(outerWhile_nn(ast()).body);
        const auto innerWhile_nn
            = extractWhileStmt(outerBody_nn(ast()).items[0]);
        const auto innerBody_nn = extractBlockStmt(innerWhile_nn(ast()).body);
        const auto breakStmt_nn
            = extractBreakStmt(extractStmtNode(innerBody_nn(ast()).items[0]));
        const auto continueStmt_nn = extractContinueStmt(
            extractStmtNode(innerBody_nn(ast()).items[1]));

        const auto innerLoop = innerWhile_nn;
        require(requireLoop(m_output, breakStmt_nn) == innerLoop,
            "break inside the inner loop should bind to the innermost while");
        require(requireLoop(m_output, continueStmt_nn) == innerLoop,
            "continue inside the inner loop should bind to the innermost "
            "while");
        require(innerLoop != outerWhile_nn,
            "inner-loop control flow should not bind to the outer while");
    }

    void testLoopControlInsideWhileIfBindsContainingWhile()
    {
        m_output = analyzeSource(
            "int main(){while (1) if (2) break; else continue; return 0;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto whileStmt_nn
            = extractWhileStmt(funcDef_nn(ast()).body(ast()).items[0]);
        const auto ifStmt_nn = extractIfStmt(whileStmt_nn(ast()).body);
        const auto breakStmt_nn = extractBreakStmt(ifStmt_nn(ast()).thenBody);
        const auto continueStmt_nn
            = extractContinueStmt(ifStmt_nn(ast()).elseBody);

        const auto whileLoop = whileStmt_nn;
        require(requireLoop(m_output, breakStmt_nn) == whileLoop,
            "break inside while-if should bind to the containing while");
        require(requireLoop(m_output, continueStmt_nn) == whileLoop,
            "continue inside while-if should bind to the containing while");
    }

    void testBreakOutsideWhileReportsSemanticError()
    {
        m_output = analyzeSource("int main(){break; return 0;}");
        require(
            !success(), "break outside while should report a semantic error");
        require(isDiagnostic<BreakOutsideWhileDiagnostic>(firstDiagnostic()),
            "break outside while should use the dedicated semantic diagnostic");
    }

    void testContinueOutsideWhileReportsSemanticError()
    {
        m_output = analyzeSource("int main(){continue; return 0;}");
        require(!success(),
            "continue outside while should report a semantic error");
        require(isDiagnostic<ContinueOutsideWhileDiagnostic>(firstDiagnostic()),
            "continue outside while should use the dedicated semantic "
            "diagnostic");
    }
};

} // namespace

int main()
{
    SemanticWhileTest test;
    test.testLoopControlStatementsBindToInnermostWhile();
    test.testNestedInnerLoopBreakAndContinueBothBindInnermostWhile();
    test.testLoopControlInsideWhileIfBindsContainingWhile();
    test.testBreakOutsideWhileReportsSemanticError();
    test.testContinueOutsideWhileReportsSemanticError();
    return 0;
}
