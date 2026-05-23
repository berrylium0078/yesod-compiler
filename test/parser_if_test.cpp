#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserIfTest : ParserTestBase {
    void testIfElseParses()
    {
        parseRoot("int main(){if (1) return 1; else return 0;}");
        const auto funcDef_nn = firstFuncDef();
        const auto& blockItems = funcDef_nn(ast()).body(ast()).items;

        require(blockItems.size() == 1,
            "top-level block should contain one if statement");

        const auto ifStmt_nn = extractIfStmt(blockItems[0]);
        require(evaluateExp(ifStmt_nn(ast()).condition) == 1,
            "if condition should reuse expression parsing");
        require(
            extractReturnStmt(ifStmt_nn(ast()).thenBody)(ast()).m_exp_nn.ref()
                != nullptr,
            "if then-branch should preserve its statement payload");
        require(evaluateExp(extractReturnStmt(ifStmt_nn(ast()).elseBody)(ast())
                        .m_exp_nn.ref())
                == 0,
            "else branch should preserve its return expression");
    }

    void testDanglingElseBindsInnermostIf()
    {
        parseRoot("int main(){if (1) if (2) return 3; else return 4;}");
        const auto funcDef_nn = firstFuncDef();
        const auto outerIf_nn
            = extractIfStmt(funcDef_nn(ast()).body(ast()).items[0]);
        const auto innerIf_nn = extractIfStmt(outerIf_nn(ast()).thenBody);
        require(evaluateExp(extractReturnStmt(innerIf_nn(ast()).elseBody)(ast())
                        .m_exp_nn.ref())
                == 4,
            "inner else branch should preserve its expression payload");
    }

    void testIfRecovery()
    {
        parseSource("int main(){if (1 return 1; return 0;}");
        require(!success(),
            "missing if-condition ')' should report recovery diagnostics");
        require(firstDiagnostic().kind == DiagnosticKind::missingIfRParen,
            "missing if-condition ')' should use the dedicated diagnostic");
        require(root() != nullptr,
            "missing if-condition ')' should recover to a root");

        parseSource("int main(){if (@) return 1;}");
        require(!success(),
            "malformed if condition should report recovery diagnostics");
        require(firstDiagnostic().kind == DiagnosticKind::malformedIfCond,
            "malformed if condition should use the dedicated diagnostic");
    }
};

} // namespace

int main()
{
    ParserIfTest test;
    test.testIfElseParses();
    test.testDanglingElseBindsInnermostIf();
    test.testIfRecovery();
    return 0;
}
