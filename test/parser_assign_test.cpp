#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testAssignmentAndLValueParsing()
{
    const std::string source
        = "int main(){int value; value = 1 + 2; return value;}";
    ParserTestBase test;
    test.parseRoot(source);
    const auto funcDef_nn = test.firstFuncDef();
    const auto& blockItems = funcDef_nn(test.ast()).m_block_nn(test.ast()).m_blockItems;

    require(blockItems.size() == 3,
        "block should contain a declaration, assignment, and return");

    const auto assignStmt_nn = test.extractAssignStmt(blockItems[1]);
    require(test.requireLVal(assignStmt_nn(test.ast()).m_lVal_nn).m_identifier_nn(test.ast()).m_name
            == "value",
        "assignment lvalue should preserve identifier text only");
    require(assignStmt_nn(test.ast()).m_lVal_nn(test.ast()).m_sourcePos.m_offset == 22,
        "assignment lvalue should store its starting byte offset");
    require(test.evaluateExp(assignStmt_nn(test.ast()).m_exp_nn) == 3,
        "assignment rhs should preserve the expression tree");

    const auto returnStmt_nn = test.extractReturnStmt(blockItems[2]);
    require(
        test.requireLVal(returnStmt_nn(test.ast()).m_exp_nn).m_identifier_nn(test.ast()).m_name == "value",
        "return value should preserve its lvalue identifier text");
}

void testAssignmentRecoveryDiagnostics()
{
    ParserTestBase test;
    test.parseSource("int main(){int value; value = ; return 1;}");
    require(!test.success(), "missing assignment rhs should fail");
    require(test.firstDiagnostic().m_kind
            == DiagnosticKind::malformedAssignValue,
        "missing assignment rhs should report the assignment-value label");
    require(test.root() != nullptr,
        "missing assignment rhs should still recover to a root");

    test.parseSource("int main(){int value; value = 1 return 0;}");
    require(!test.success(),
        "missing assignment semicolon should fail");
    require(test.firstDiagnostic().m_kind
            == DiagnosticKind::missingAssignSemicolon,
        "missing assignment semicolon should report the assignment delimiter "
        "label");
    require(test.root() != nullptr,
        "missing assignment semicolon should still recover to a root");
}

} // namespace

int main()
{
    testAssignmentAndLValueParsing();
    testAssignmentRecoveryDiagnostics();
    return 0;
}
