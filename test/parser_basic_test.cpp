#include "parser_test_support.h"

using namespace yesod::test_support::parser;

class ParserBasicTest : public ParserTestBase {
public:
    void testMinimalFunction()
    {
        parseRoot("int main(){return 42;}");
        const auto root_nn = root();
        const auto funcDef_nn = firstFuncDef();
        const auto returnStmt_nn = extractReturnStmt(
            funcDef_nn(ast()).body(ast()).items.front());

        require(root_nn(ast()).sourcePos.m_offset == 0,
            "root start offset should be the first token");
        require(
            funcDef_nn(ast()).m_funcType == FuncTypeKeyword::intKeyword,
            "function type should use enum keyword");
        require(
            funcDef_nn(ast()).identifier(ast()).name == "main",
            "identifier payload should only store text");
        require(
            funcDef_nn(ast()).body(ast()).items.size()
                == 1,
            "block should contain the single documented statement");
        require(returnStmt_nn(ast()).sourcePos.m_offset == 11,
            "statement start offset should point to the return keyword");
        require(
            returnStmt_nn(ast()).exp(ast()).sourcePos.m_offset
                == 18,
            "expression start offset should match source byte offset");
        require(evaluateExp(returnStmt_nn(ast()).exp.ref()) == 42,
            "number literal should parse decimal values");
    }

    void testWhitespace()
    {
        const std::string source
            = "\n \tint spaced_name ( )\n{\n  return\t7 ;\n}\n";
        parseRoot(source);
        const auto root_nn = root();
        const auto funcDef_nn = firstFuncDef();
        const auto returnStmt_nn = extractReturnStmt(
            funcDef_nn(ast()).body(ast()).items.front());

        require(root_nn(ast()).sourcePos.m_offset == 3,
            "leading whitespace should be skipped before the first token");
        require(funcDef_nn(ast()).identifier(ast()).name
                == "spaced_name",
            "identifier should survive trivia skipping");
        require(returnStmt_nn(ast()).sourcePos.m_offset == 27,
            "statement start offset should point to the return keyword");
        require(evaluateExp(returnStmt_nn(ast()).exp.ref()) == 7,
            "number should parse after trivia");
    }

    void testComments()
    {
        const std::string source
            = "/* leading block */ int /* name gap */ main // line comment "
              "before "
              "parens\n"
              "( /* empty params */ ) /* before block */ { // before return\n"
              "  return /* number */ 42; /* trailing */\n"
              "}\n";

        parseRoot(source);
        const auto funcDef_nn = firstFuncDef();
        const auto returnStmt_nn = extractReturnStmt(
            funcDef_nn(ast()).body(ast()).items.front());

        require(
            funcDef_nn(ast()).identifier(ast()).name == "main",
            "comments should be skipped instead of entering the AST");
        require(evaluateExp(returnStmt_nn(ast()).exp.ref()) == 42,
            "number should parse across comments");
    }

    void testDecimalLiteral()
    {
        parseRoot("int d(){return 42;}");
        require(evaluateExp(
                    extractReturnStmt(firstFuncDef()(ast())
                                .body(ast())
                                .items.front())(ast())
                        .exp.ref())
                == 42,
            "decimal literal should parse");
    }
    void testOctalLiteral()
    {
        parseRoot("int o(){return 053;}");
        require(evaluateExp(
                    extractReturnStmt(firstFuncDef()(ast())
                                .body(ast())
                                .items.front())(ast())
                        .exp.ref())
                == 43,
            "octal literal should parse");
    }
    void testHexadecimalLiteral()
    {
        parseRoot("int h(){return 0X2c;}");
        require(evaluateExp(
                    extractReturnStmt(firstFuncDef()(ast())
                                .body(ast())
                                .items.front())(ast())
                        .exp.ref())
                == 44,
            "hexadecimal literal should parse");
    }

    void testEmptyBlock()
    {
        parseRoot("int main(){/*I am empty*/}");
        require(firstFuncDef()(ast())
                    .body(ast())
                    .items.empty(),
            "block grammar should allow zero block items");
    }

    void testHexOrderedChoiceWinsBeforeOctal()
    {
        parseRoot("int hex(){return 0x2a;}");
        const auto funcDef_nn = firstFuncDef();
        const auto returnStmt_nn = extractReturnStmt(
            funcDef_nn(ast()).body(ast()).items.front());
        require(evaluateExp(returnStmt_nn(ast()).exp.ref()) == 42,
            "0x2a must parse as a hexadecimal literal, not octal zero plus "
            "trailing input");
    }

    void testMalformedInputsFailWithFocusedDiagnostics()
    {
        parseSource("int main(){return 1}");
        require(!success(),
            "missing semicolon should produce a recovery diagnostic");
        require(root() != nullptr,
            "missing semicolon should recover to the block boundary and still "
            "build the root");
        require(
            firstDiagnostic().kind == DiagnosticKind::missingSemicolon,
            "missing semicolon should report the PEG recovery label");
        require(firstDiagnostic().m_offset == 19,
            "missing semicolon should diagnose at the statement boundary "
            "before "
            "the closing brace");

        parseSource("int main(){return 0x;}");
        require(!success(), "malformed hexadecimal literal should fail");
        require(firstDiagnostic().kind
                == DiagnosticKind::malformedReturnValue,
            "malformed hexadecimal literal should report the committed "
            "malformed-return diagnostic");

        parseSource("int main(){return 1;");
        require(!success(),
            "missing closing brace should produce a recovery diagnostic");
        require(root() != nullptr,
            "missing closing brace should recover at EOF and still build the "
            "root");
        require(firstDiagnostic().kind == DiagnosticKind::missingRBrace,
            "missing closing brace should report the PEG recovery label");

        parseSource("int main( {return 1;}");
        require(!success(),
            "missing ')' should produce a recovery diagnostic");
        require(root() != nullptr,
            "missing ')' should recover before the block and still build the "
            "root");
        require(
            firstDiagnostic().kind == DiagnosticKind::missingFuncRParen,
            "missing ')' should report the PEG recovery label");

        parseSource("int main(){nope}");
        require(!success(), "malformed statement head should fail");
        require(
            firstDiagnostic().kind == DiagnosticKind::missingSemicolon,
            "bare identifier statement should now report the "
            "expression-statement "
            "semicolon label");

        parseRoot("int main(){return ;}");
        const auto emptyReturnStmt
            = extractReturnStmt(firstFuncDef()(ast())
                    .body(ast())
                    .items.front());
        require(emptyReturnStmt(ast()).exp == nullptr,
            "empty return should now parse as an optional-expression return");

        parseSource("int main(){return 1;} trailing");
        require(!success(), "trailing input should fail");
        require(firstDiagnostic().kind == DiagnosticKind::trailingInput,
            "trailing input should produce a trailing-input diagnostic");
    }

    void testOutOfRangeIntegerFails()
    {
        parseSource("int main(){return 2147483648;}");
        require(!success(), "out-of-range integer should fail");
        require(
            firstDiagnostic().kind == DiagnosticKind::integerOutOfRange,
            "out-of-range integer should report range overflow");
    }
};

int main()
{
    ParserBasicTest test;
    test.testMinimalFunction();
    test.testWhitespace();
    test.testComments();
    test.testDecimalLiteral();
    test.testOctalLiteral();
    test.testHexadecimalLiteral();
    test.testEmptyBlock();
    test.testHexOrderedChoiceWinsBeforeOctal();
    test.testMalformedInputsFailWithFocusedDiagnostics();
    test.testOutOfRangeIntegerFails();
    return 0;
}