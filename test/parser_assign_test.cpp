#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testAssignmentAndLValueParsing()
{
    const std::string source
        = "int main(){int value; value = 1 + 2; return value;}";
    const auto root_nn = parseRoot(source);
    const auto funcDef_nn = firstFuncDef(root_nn.m_root);
    const auto& blockItems = funcDef_nn->m_block_nn->m_blockItems;

    require(blockItems.size() == 3,
        "block should contain a declaration, assignment, and return");

    const auto assignStmt_nn = extractAssignStmt(blockItems[1]);
    require(requireLVal(assignStmt_nn->m_lVal_nn).m_identifier_nn->m_name
            == "value",
        "assignment lvalue should preserve identifier text only");
    require(assignStmt_nn->m_lVal_nn->m_sourcePos.m_offset == 22,
        "assignment lvalue should store its starting byte offset");
    require(evaluateExp(*assignStmt_nn->m_exp_nn) == 3,
        "assignment rhs should preserve the expression tree");

    const auto returnStmt_nn = extractReturnStmt(blockItems[2]);
    require(
        requireLVal(returnStmt_nn->m_exp_nn).m_identifier_nn->m_name == "value",
        "return value should preserve its lvalue identifier text");
}

void testAssignmentRecoveryDiagnostics()
{
    const auto malformedAssign
        = parseSource("int main(){int value; value = ; return 1;}");
    require(!malformedAssign.success(), "missing assignment rhs should fail");
    require(firstDiagnostic(malformedAssign).m_kind
            == DiagnosticKind::malformedAssignValue,
        "missing assignment rhs should report the assignment-value label");
    require(malformedAssign.m_root != nullptr,
        "missing assignment rhs should still recover to a root");

    const auto missingAssignSemicolon
        = parseSource("int main(){int value; value = 1 return 0;}");
    require(!missingAssignSemicolon.success(),
        "missing assignment semicolon should fail");
    require(firstDiagnostic(missingAssignSemicolon).m_kind
            == DiagnosticKind::missingAssignSemicolon,
        "missing assignment semicolon should report the assignment delimiter "
        "label");
    require(missingAssignSemicolon.m_root != nullptr,
        "missing assignment semicolon should still recover to a root");
}

} // namespace

int main()
{
    testAssignmentAndLValueParsing();
    testAssignmentRecoveryDiagnostics();
    return 0;
}
