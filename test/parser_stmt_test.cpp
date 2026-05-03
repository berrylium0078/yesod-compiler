#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testExpressionAndEmptyStatementsParse()
{
    const auto root_nn = parseRoot("int main(){1 + 2; ; return 0;}");
    const auto& blockItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;

    require(blockItems.size() == 3,
        "block should contain expression statement, empty statement, and return");

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
        "outer block should contain nested block statement and trailing return");

    const auto nestedBlock_nn = extractBlockStmt(blockItems[0]);
    require(nestedBlock_nn->m_blockItems.size() == 2,
        "nested block statement should preserve its own scoped items");
    require(extractVarDecl(extractDeclNode(nestedBlock_nn->m_blockItems[0]))
                ->m_varDefs[0]
                ->m_identifier_nn->m_name
            == "inner",
        "nested block declaration should preserve identifier payload");
    const auto returnStmt_nn = extractReturnStmt(nestedBlock_nn->m_blockItems[1]);
    const auto& lOrExp = requireLOrExp(returnStmt_nn->m_exp_nn->m_lOrExp_nn);
    const auto& lAndExp = requireLAndExp(lOrExp.m_head_nn);
    const auto& eqExp = requireEqExp(lAndExp.m_head_nn);
    const auto& relExp = requireRelExp(eqExp.m_head_nn);
    const auto& addExp = requireAddExp(relExp.m_head_nn);
    const auto& mulExp = requireMulExp(addExp.m_head_nn);
    const auto& unaryExp = requireUnaryExp(mulExp.m_head_nn);

    std::shared_ptr<PrimaryExp> primaryExp_nn;
    std::visit(
        [&](const auto& unaryAlt) {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<PrimaryExp>>) {
                primaryExp_nn = unaryAlt;
            }
        },
        unaryExp.m_kind);
    require(primaryExp_nn != nullptr,
        "nested block return should preserve its primary-expression shape");

    std::visit(
        [&](const auto& primaryAlt) {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<LVal>>) {
                require(primaryAlt->m_identifier_nn->m_name == "inner",
                    "nested block return should preserve its lvalue identifier");
            } else {
                fail("nested block return should be lvalue-backed");
            }
        },
        primaryExp_nn->m_kind);
}

void testExpressionStatementRecovery()
{
    const auto missingSemicolon = parseSource("int main(){1 + 2 return 0;}");
    require(!missingSemicolon.success(),
        "missing expression-statement semicolon should fail");
    require(firstDiagnostic(missingSemicolon).m_kind
            == DiagnosticKind::missingSemicolon,
        "missing expression-statement semicolon should report the shared semicolon label");
    require(missingSemicolon.m_root != nullptr,
        "missing expression-statement semicolon should still recover to a root");

    const auto malformedBlock = parseSource("int main(){@ return 0;}");
    require(!malformedBlock.success(),
        "invalid block item should fail");
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