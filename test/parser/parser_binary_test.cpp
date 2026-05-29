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
            funcDef_nn(ast()).body(ast()).items.front());

        require(root()(ast()).sourcePos.m_offset == 2,
            "leading trivia should be skipped before the first token");
        require(evaluateExp(returnStmt_nn(ast()).exp.ref()) == 15,
            "binary expression should parse across trivia boundaries");
        require(requireBinaryExp(returnStmt_nn(ast()).exp.ref()).op
                == BinaryOpKeyword::plus,
            "binary root should preserve precedence after grouped parsing");
    }

    void testPrecedenceAndAssociativity()
    {
        parseRoot("int p(){return 1 + 2 * 3 == 7 || 0;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).body(ast()).items.front())(
                ast())
                    .exp.ref())
                == 1,
            "multiplicative precedence should bind tighter than additive, then "
            "equality, then logical or");

        parseRoot("int a(){return 8 - 3 - 2;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).body(ast()).items.front())(
                ast())
                    .exp.ref())
                == 3,
            "additive expressions should associate left to right");

        parseRoot("int m(){return 20 / 5 / 2;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).body(ast()).items.front())(
                ast())
                    .exp.ref())
                == 2,
            "multiplicative expressions should associate left to right");

        parseRoot("int r(){return 1 < 2 == 1;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).body(ast()).items.front())(
                ast())
                    .exp.ref())
                == 1,
            "relational expressions should bind tighter than equality");

        parseRoot("int l(){return 0 || 2 && 0 || 5;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).body(ast()).items.front())(
                ast())
                    .exp.ref())
                == 1,
            "logical and should bind tighter than logical or");

        parseRoot("int b(){return 1 | 2 ^ 7 & 3 == 3 && 0;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).body(ast()).items.front())(
                ast())
                    .exp.ref())
                == 0,
            "bitwise and should bind tighter than xor, then or, all tighter than logical and");

        parseRoot("int s(){return 1 + 2 << 3 < 25;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).body(ast()).items.front())(
                ast())
                    .exp.ref())
                == 1,
            "shift expressions should bind tighter than relational and looser than additive");
    }

    void testOrderedChoiceSensitiveOperators()
    {
        parseRoot("int rel(){return 1 <= 2 < 3;}");
        const auto funcDef_nn = firstFuncDef();
        const auto returnStmt_nn = extractReturnStmt(
            funcDef_nn(ast()).body(ast()).items.front());
        const auto& rootBinaryExp = requireBinaryExp(
            returnStmt_nn(ast()).exp.ref());
        const auto& lhsBinaryExp = requireBinaryExp(rootBinaryExp.lhs);

        require(rootBinaryExp.op == BinaryOpKeyword::less,
            "< should remain available after <=");
        require(lhsBinaryExp.op == BinaryOpKeyword::lessEqual,
            "<= must be parsed before <");
        require(evaluateExp(returnStmt_nn(ast()).exp.ref()) == 1,
            "relational chains should stay left-associated under the generic "
            "binary tree");

        parseRoot("int sh(){return 1 << 2 < 5;}");
        const auto shiftReturnStmt_nn = extractReturnStmt(
            firstFuncDef()(ast()).body(ast()).items.front());
        const auto& shiftRootBinaryExp = requireBinaryExp(
            shiftReturnStmt_nn(ast()).exp.ref());
        const auto& shiftLhsBinaryExp = requireBinaryExp(shiftRootBinaryExp.lhs);

        require(shiftRootBinaryExp.op == BinaryOpKeyword::less,
            "< should still parse after << consumes the longer token");
        require(shiftLhsBinaryExp.op == BinaryOpKeyword::shl,
            "<< must be parsed before <");

        parseRoot("int bit(){return 8 >> 1 | 1;}");
        const auto bitReturnStmt_nn = extractReturnStmt(
            firstFuncDef()(ast()).body(ast()).items.front());
        const auto& bitRootBinaryExp = requireBinaryExp(
            bitReturnStmt_nn(ast()).exp.ref());
        const auto& bitLhsBinaryExp = requireBinaryExp(bitRootBinaryExp.lhs);

        require(bitRootBinaryExp.op == BinaryOpKeyword::bitOr,
            "bitwise or should remain available after >>");
        require(bitLhsBinaryExp.op == BinaryOpKeyword::sar,
            ">> should parse as arithmetic right shift");
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