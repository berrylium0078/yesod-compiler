#include "parser_test_support.h"

using namespace yesod::test_support::parser;

struct ParserUnaryTest : ParserTestBase {
    void testPlus()
    {
        parseRoot("int plus(){return +42;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == 42,
            "unary plus should parse");
    }
    void testMinus()
    {
        parseRoot("int minus(){return -42;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == -42,
            "unary minus should parse");
    }
    void testBang()
    {
        parseRoot("int bang(){return !0;}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == 1,
            "logical not should parse");
    }
    void testNested()
    {
        parseRoot("int nested(){return -(+42);}");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == -42,
            "nested unary expressions should parse recursively");
    }
    void testTriviaAndParenthesizedPrimary()
    {
        const std::string source
            = "\n\tint unary( )\n{\n"
              "  return /* unary */ - ( /* number */ 052 ) ;\n"
              "}\n";
        parseRoot(source);
        const auto funcDef_nn = firstFuncDef();
        const auto returnStmt_nn = extractReturnStmt(
            funcDef_nn(ast()).m_block_nn(ast()).m_blockItems.front());

        require(evaluateExp(returnStmt_nn(ast()).m_exp_nn) == -42,
            "unary expression should parse across comments and whitespace");
        require(requireUnaryExp(returnStmt_nn(ast()).m_exp_nn).m_op
                == UnaryOpKeyword::minus,
            "unary root should preserve its operator after grouped parsing");
    }
    void testRecoveryDiagnostics()
    {
        parseSource("int main(){return -;}");
        require(!success(), "missing unary operand should fail");
        require(
            firstDiagnostic().m_kind == DiagnosticKind::malformedReturnValue,
            "missing unary operand should report the return-value diagnostic");

        parseSource("int main(){return (;}");
        require(!success(),
            "malformed parenthesized primary expression should fail");
        require(firstDiagnostic().m_kind == DiagnosticKind::malformedPrimaryExp,
            "malformed parenthesized expression should report the "
            "primary-expression diagnostic");

        parseSource("int main(){return (1;}");
        require(!success(),
            "missing ')' in primary expression should produce a recovery "
            "diagnostic");
        require(root(),
            "missing ')' in primary expression should recover to the statement "
            "boundary and still build the root");
        require(
            firstDiagnostic().m_kind == DiagnosticKind::missingPrimaryRParen,
            "missing ')' in primary expression should report the "
            "primary-expression delimiter diagnostic");
        require(
            evaluateExp(extractReturnStmt(
                firstFuncDef()(ast()).m_block_nn(ast()).m_blockItems.front())(
                ast())
                    .m_exp_nn)
                == 1,
            "missing ')' recovery should preserve the inner expression value");
    }
};

int main()
{
    ParserUnaryTest test;
    try {
        test.testPlus();
        test.testMinus();
        test.testBang();
        test.testNested();
        test.testTriviaAndParenthesizedPrimary();
        test.testRecoveryDiagnostics();
    } catch (std::exception& e) {
        fail(e.what());
    }
    return 0;
}