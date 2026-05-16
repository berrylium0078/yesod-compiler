#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testBinaryTriviaIsSkippedAtParserBoundaries()
{
    const std::string source
        = "\n\tint binary ( )\n{\n"
          "  return /* lhs */ 1 + /* rhs */ 2 * ( 3 + 4 ) ;\n"
          "}\n";
    const auto root_nn = parseRoot(source);
    const auto returnStmt_nn = extractReturnStmt(
        root_nn->m_funcDef_nn->m_block_nn->m_blockItems.front());

    require(root_nn->m_sourcePos.m_offset == 2,
        "leading trivia should be skipped before the first token");
    require(evaluateExp(*returnStmt_nn->m_exp_nn) == 15,
        "binary expression should parse across trivia boundaries");
    require(
        requireBinaryExp(returnStmt_nn->m_exp_nn).m_op == BinaryOpKeyword::plus,
        "binary root should preserve precedence after grouped parsing");
}

void testPrecedenceAndAssociativity()
{
    require(evaluateExp(*extractReturnStmt(
                parseRoot("int p(){return 1 + 2 * 3 == 7 || 0;}")
                    ->m_funcDef_nn->m_block_nn->m_blockItems.front())
                             ->m_exp_nn)
            == 1,
        "multiplicative precedence should bind tighter than additive, then "
        "equality, then logical or");
    require(evaluateExp(*extractReturnStmt(
                parseRoot("int a(){return 8 - 3 - 2;}")
                    ->m_funcDef_nn->m_block_nn->m_blockItems.front())
                             ->m_exp_nn)
            == 3,
        "additive expressions should associate left to right");
    require(evaluateExp(*extractReturnStmt(
                parseRoot("int m(){return 20 / 5 / 2;}")
                    ->m_funcDef_nn->m_block_nn->m_blockItems.front())
                             ->m_exp_nn)
            == 2,
        "multiplicative expressions should associate left to right");
    require(evaluateExp(*extractReturnStmt(
                parseRoot("int r(){return 1 < 2 == 1;}")
                    ->m_funcDef_nn->m_block_nn->m_blockItems.front())
                             ->m_exp_nn)
            == 1,
        "relational expressions should bind tighter than equality");
    require(evaluateExp(*extractReturnStmt(
                parseRoot("int l(){return 0 || 2 && 0 || 5;}")
                    ->m_funcDef_nn->m_block_nn->m_blockItems.front())
                             ->m_exp_nn)
            == 1,
        "logical and should bind tighter than logical or");
}

void testOrderedChoiceSensitiveOperators()
{
    const auto relRoot_nn = parseRoot("int rel(){return 1 <= 2 < 3;}");
    const auto returnStmt_nn = extractReturnStmt(
        relRoot_nn->m_funcDef_nn->m_block_nn->m_blockItems.front());
    const auto& rootBinaryExp = requireBinaryExp(*returnStmt_nn->m_exp_nn);
    const auto& lhsBinaryExp = requireBinaryExp(rootBinaryExp.m_lhs_nn);

    require(rootBinaryExp.m_op == BinaryOpKeyword::less,
        "< should remain available after <=");
    require(lhsBinaryExp.m_op == BinaryOpKeyword::lessEqual,
        "<= must be parsed before <");
    require(evaluateExp(*returnStmt_nn->m_exp_nn) == 1,
        "relational chains should stay left-associated under the generic "
        "binary tree");
}

} // namespace

int main()
{
    testBinaryTriviaIsSkippedAtParserBoundaries();
    testPrecedenceAndAssociativity();
    testOrderedChoiceSensitiveOperators();
    return 0;
}