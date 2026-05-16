#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testExpressionAndEmptyStatementsParse()
{
    const auto root_nn = parseRoot("int main(){1 + 2; ; return 0;}");
    const auto& blockItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;

    require(blockItems.size() == 3,
        "block should contain expression statement, empty statement, and "
        "return");

    const auto expStmt_nn = extractExpStmt(blockItems[0]);
    require(expStmt_nn->m_exp_nn != nullptr,
        "non-empty expression statement should preserve its expression");
    require(evaluateExp(*expStmt_nn->m_exp_nn) == 3,
        "expression statement should reuse the expression grammar");

    const auto emptyStmt_nn = extractExpStmt(blockItems[1]);
    require(emptyStmt_nn->m_exp_nn == nullptr,
        "empty statement should not fabricate an expression node");
}

void testNestedBlockStatementsParse()
{
    const auto root_nn
        = parseRoot("int main(){{int inner = 1; return inner;} return 0;}");
    const auto& blockItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;

    require(blockItems.size() == 2,
        "outer block should contain nested block statement and trailing "
        "return");

    const auto nestedBlock_nn = extractBlockStmt(blockItems[0]);
    require(nestedBlock_nn->m_blockItems.size() == 2,
        "nested block statement should preserve its own scoped items");
    require(extractVarDecl(extractDeclNode(nestedBlock_nn->m_blockItems[0]))
                ->m_varDefs[0]
                ->m_identifier_nn->m_name
            == "inner",
        "nested block declaration should preserve identifier payload");
    const auto returnStmt_nn
        = extractReturnStmt(nestedBlock_nn->m_blockItems[1]);
    require(
        requireLVal(returnStmt_nn->m_exp_nn).m_identifier_nn->m_name == "inner",
        "nested block return should preserve its lvalue identifier");
}

void testExpressionStatementRecovery()
{
    const auto missingSemicolon = parseSource("int main(){1 + 2 return 0;}");
    require(!missingSemicolon.success(),
        "missing expression-statement semicolon should fail");
    require(firstDiagnostic(missingSemicolon).m_kind
            == DiagnosticKind::missingSemicolon,
        "missing expression-statement semicolon should report the shared "
        "semicolon label");
    require(missingSemicolon.m_root != nullptr,
        "missing expression-statement semicolon should still recover to a "
        "root");

    const auto malformedBlock = parseSource("int main(){@ return 0;}");
    require(!malformedBlock.success(), "invalid block item should fail");
    require(firstDiagnostic(malformedBlock).m_kind
            == DiagnosticKind::malformedBlockItem,
        "invalid block item should report the block-item recovery label");
}

} // namespace

int main()
{
    testExpressionAndEmptyStatementsParse();
    testNestedBlockStatementsParse();
    testExpressionStatementRecovery();
    return 0;
}