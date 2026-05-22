#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserIfTest : ParserTestBase {
    void testIfElseParses()
    {
        parseRoot("int main(){if (1) return 1; else return 0;}");
        const auto funcDef_nn = firstFuncDef();
        const auto& blockItems = funcDef_nn(ast()).m_block_nn(ast()).m_blockItems;

        require(blockItems.size() == 1,
            "top-level block should contain one if statement");

        const auto ifStmt_nn = extractIfStmt(blockItems[0]);
        require(evaluateExp(ifStmt_nn(ast()).m_condExp_nn) == 1,
            "if condition should reuse expression parsing");
        require(extractReturnStmt(ifStmt_nn(ast()).m_thenStmt_nn)(ast()).m_exp_nn
                != nullptr,
            "if then-branch should preserve its statement payload");
        require(ifStmt_nn(ast()).m_elseStmt_nn != nullptr,
            "if-else should preserve the else branch");
        require(evaluateExp(extractReturnStmt(ifStmt_nn(ast()).m_elseStmt_nn)(
                ast())
                               .m_exp_nn)
                == 0,
            "else branch should preserve its return expression");
    }

    void testDanglingElseBindsInnermostIf()
    {
        parseRoot("int main(){if (1) if (2) return 3; else return 4;}");
        const auto funcDef_nn = firstFuncDef();
        const auto outerIf_nn = extractIfStmt(
            funcDef_nn(ast()).m_block_nn(ast()).m_blockItems[0]);

        require(outerIf_nn(ast()).m_elseStmt_nn == nullptr,
            "dangling else should not attach to the outer if");

        const auto innerIf_nn = extractIfStmt(outerIf_nn(ast()).m_thenStmt_nn);
        require(innerIf_nn(ast()).m_elseStmt_nn != nullptr,
            "dangling else should attach to the inner if");
        require(evaluateExp(extractReturnStmt(innerIf_nn(ast()).m_elseStmt_nn)(
                ast())
                               .m_exp_nn)
                == 4,
            "inner else branch should preserve its expression payload");
    }

    void testIfRecovery()
    {
        parseSource("int main(){if (1 return 1; return 0;}");
        require(!success(),
            "missing if-condition ')' should report recovery diagnostics");
        require(firstDiagnostic().m_kind == DiagnosticKind::missingIfRParen,
            "missing if-condition ')' should use the dedicated diagnostic");
        require(root() != nullptr,
            "missing if-condition ')' should recover to a root");

        parseSource("int main(){if (@) return 1;}");
        require(!success(),
            "malformed if condition should report recovery diagnostics");
        require(firstDiagnostic().m_kind == DiagnosticKind::malformedIfCond,
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
