#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testIfElseParses()
{
    const auto root_nn
        = parseRoot("int main(){if (1) return 1; else return 0;}");
    const auto& blockItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;

    require(blockItems.size() == 1,
        "top-level block should contain one if statement");

    const auto ifStmt_nn = extractIfStmt(blockItems[0]);
    require(evaluateExp(*ifStmt_nn->m_condExp_nn) == 1,
        "if condition should reuse expression parsing");
    require(extractReturnStmt(ifStmt_nn->m_thenStmt_nn)->m_exp_nn != nullptr,
        "if then-branch should preserve its statement payload");
    require(ifStmt_nn->m_elseStmt_nn != nullptr,
        "if-else should preserve the else branch");
    require(evaluateExp(*extractReturnStmt(ifStmt_nn->m_elseStmt_nn)->m_exp_nn)
            == 0,
        "else branch should preserve its return expression");
}

void testDanglingElseBindsInnermostIf()
{
    const auto root_nn
        = parseRoot("int main(){if (1) if (2) return 3; else return 4;}");
    const auto outerIf_nn
        = extractIfStmt(root_nn->m_funcDef_nn->m_block_nn->m_blockItems[0]);

    require(outerIf_nn->m_elseStmt_nn == nullptr,
        "dangling else should not attach to the outer if");

    const auto innerIf_nn = extractIfStmt(outerIf_nn->m_thenStmt_nn);
    require(innerIf_nn->m_elseStmt_nn != nullptr,
        "dangling else should attach to the inner if");
    require(evaluateExp(*extractReturnStmt(innerIf_nn->m_elseStmt_nn)->m_exp_nn)
            == 4,
        "inner else branch should preserve its expression payload");
}

void testIfRecovery()
{
    const auto missingRParen
        = parseSource("int main(){if (1 return 1; return 0;}");
    require(!missingRParen.success(),
        "missing if-condition ')' should report recovery diagnostics");
    require(firstDiagnostic(missingRParen).m_kind
            == DiagnosticKind::missingIfRParen,
        "missing if-condition ')' should use the dedicated diagnostic");
    require(missingRParen.m_root != nullptr,
        "missing if-condition ')' should recover to a root");

    const auto malformedCond = parseSource("int main(){if (@) return 1;}");
    require(!malformedCond.success(),
        "malformed if condition should report recovery diagnostics");
    require(firstDiagnostic(malformedCond).m_kind
            == DiagnosticKind::malformedIfCond,
        "malformed if condition should use the dedicated diagnostic");
}

} // namespace

int main()
{
    testIfElseParses();
    testDanglingElseBindsInnermostIf();
    testIfRecovery();
    return 0;
}
