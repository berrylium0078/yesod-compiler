#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserBinaryTest : ParserTestBase {
    void testBinaryTriviaIsSkippedAtParserBoundaries()
    {
        const std::string source
            = "\n\tint binary ( )\n{\n"
              "  return /* lhs */ 1 + /* rhs */ 2 * ( 3 + 4 ) ;\n"
              "}\n";
        parseRoot(source);
        const auto funcDef_nn = firstFuncDef();
        const auto returnStmt_nn = extractReturnStmt(
            funcDef_nn(ast()).m_block_nn(ast()).m_blockItems.front());

        require(root()(ast()).m_sourcePos.m_offset == 2,
            "leading trivia should be skipped before the first token");
        require(evaluateExp(returnStmt_nn(ast()).m_exp_nn) == 15,
            "binary expression should parse across trivia boundaries");
        require(requireBinaryExp(returnStmt_nn(ast()).m_exp_nn).m_op
                == BinaryOpKeyword::plus,
            "binary root should preserve precedence after grouped parsing");
    }

    void testPrecedenceAndAssociativity()
    {
        parseRoot("int p(){return 1 + 2 * 3 == 7 || 0;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == 1,
            "multiplicative precedence should bind tighter than additive, then "
            "equality, then logical or");

        parseRoot("int a(){return 8 - 3 - 2;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == 3,
            "additive expressions should associate left to right");

        parseRoot("int m(){return 20 / 5 / 2;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == 2,
            "multiplicative expressions should associate left to right");

        parseRoot("int r(){return 1 < 2 == 1;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == 1,
            "relational expressions should bind tighter than equality");

        parseRoot("int l(){return 0 || 2 && 0 || 5;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == 1,
            "logical and should bind tighter than logical or");
    }

    void testOrderedChoiceSensitiveOperators()
    {
        parseRoot("int rel(){return 1 <= 2 < 3;}");
        const auto funcDef_nn = firstFuncDef();
        const auto returnStmt_nn = extractReturnStmt(
            funcDef_nn(ast()).m_block_nn(ast()).m_blockItems.front());
        const auto& rootBinaryExp = requireBinaryExp(
            returnStmt_nn(ast()).m_exp_nn);
        const auto& lhsBinaryExp = requireBinaryExp(rootBinaryExp.m_lhs_nn);

        require(rootBinaryExp.m_op == BinaryOpKeyword::less,
            "< should remain available after <=");
        require(lhsBinaryExp.m_op == BinaryOpKeyword::lessEqual,
            "<= must be parsed before <");
        require(evaluateExp(returnStmt_nn(ast()).m_exp_nn) == 1,
            "relational chains should stay left-associated under the generic "
            "binary tree");
    }
};

} // namespace

int main()
{
    ParserBinaryTest test;
    test.testBinaryTriviaIsSkippedAtParserBoundaries();
    test.testPrecedenceAndAssociativity();
    test.testOrderedChoiceSensitiveOperators();
    return 0;
}