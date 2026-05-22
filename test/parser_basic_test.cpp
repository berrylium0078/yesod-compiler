#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testMinimalFunctionParse()
{
    ParserTestBase test;
    test.parseRoot("int main(){return 42;}");
    const auto root_nn = test.root();
    const auto funcDef_nn = test.firstFuncDef();
    const auto returnStmt_nn = test.extractReturnStmt(
        funcDef_nn(test.ast()).m_block_nn(test.ast()).m_blockItems.front());

    require(root_nn(test.ast()).m_sourcePos.m_offset == 0,
        "root start offset should be the first token");
    require(funcDef_nn(test.ast()).m_funcType == FuncTypeKeyword::intKeyword,
        "function type should use enum keyword");
    require(funcDef_nn(test.ast()).m_identifier_nn(test.ast()).m_name == "main",
        "identifier payload should only store text");
    require(funcDef_nn(test.ast()).m_block_nn(test.ast()).m_blockItems.size() == 1,
        "block should contain the single documented statement");
    require(returnStmt_nn(test.ast()).m_sourcePos.m_offset == 11,
        "statement start offset should point to the return keyword");
    require(returnStmt_nn(test.ast()).m_exp_nn(test.ast()).m_sourcePos.m_offset == 18,
        "expression start offset should match source byte offset");
    require(test.evaluateExp(returnStmt_nn(test.ast()).m_exp_nn) == 42,
        "number literal should parse decimal values");
}

void testWhitespaceIsSkippedBetweenTokens()
{
    const std::string source
        = "\n \tint spaced_name ( )\n{\n  return\t7 ;\n}\n";
    ParserTestBase test;
    test.parseRoot(source);
    const auto root_nn = test.root();
    const auto funcDef_nn = test.firstFuncDef();
    const auto returnStmt_nn = test.extractReturnStmt(
        funcDef_nn(test.ast()).m_block_nn(test.ast()).m_blockItems.front());

    require(root_nn(test.ast()).m_sourcePos.m_offset == 3,
        "leading whitespace should be skipped before the first token");
    require(funcDef_nn(test.ast()).m_identifier_nn(test.ast()).m_name == "spaced_name",
        "identifier should survive trivia skipping");
    require(returnStmt_nn(test.ast()).m_sourcePos.m_offset == 27,
        "statement start offset should point to the return keyword");
    require(test.evaluateExp(returnStmt_nn(test.ast()).m_exp_nn) == 7,
        "number should parse after trivia");
}

void testCommentsAreSkippedBetweenTokens()
{
    const std::string source
        = "/* leading block */ int /* name gap */ main // line comment before "
          "parens\n"
          "( /* empty params */ ) /* before block */ { // before return\n"
          "  return /* number */ 42; /* trailing */\n"
          "}\n";

    ParserTestBase test;
    test.parseRoot(source);
    const auto root_nn = test.root();
    const auto funcDef_nn = test.firstFuncDef();
    const auto returnStmt_nn = test.extractReturnStmt(
        funcDef_nn(test.ast()).m_block_nn(test.ast()).m_blockItems.front());

    require(funcDef_nn(test.ast()).m_identifier_nn(test.ast()).m_name == "main",
        "comments should be skipped instead of entering the AST");
    require(test.evaluateExp(returnStmt_nn(test.ast()).m_exp_nn) == 42,
        "number should parse across comments");
}

void testIntegerLiteralForms()
{
    ParserTestBase test;
    test.parseRoot("int d(){return 42;}");
    const auto decimalRoot_nn = test.root();
    test.parseRoot("int o(){return 052;}");
    const auto octalRoot_nn = test.root();
    test.parseRoot("int h(){return 0X2a;}");
    const auto hexadecimalRoot_nn = test.root();

    require(test.evaluateExp(test.extractReturnStmt(
                test.firstFuncDef()(test.ast()).m_block_nn(test.ast()).m_blockItems.front())
                             (test.ast()).m_exp_nn)
            == 42,
        "decimal literal should parse");
    require(test.evaluateExp(test.extractReturnStmt(
                test.firstFuncDef()(test.ast()).m_block_nn(test.ast()).m_blockItems.front())
                             (test.ast()).m_exp_nn)
            == 42,
        "octal literal should parse");
    require(
        test.evaluateExp(test.extractReturnStmt(
            test.firstFuncDef()(test.ast()).m_block_nn(test.ast()).m_blockItems.front())
                         (test.ast()).m_exp_nn)
            == 42,
        "hexadecimal literal should parse");
}

void testEmptyBlockParses()
{
    ParserTestBase test;
    test.parseRoot("int main(){/*I am empty*/}");
    const auto root_nn = test.root();
    require(test.firstFuncDef()(test.ast()).m_block_nn(test.ast()).m_blockItems.empty(),
        "block grammar should allow zero block items");
}

void testHexOrderedChoiceWinsBeforeOctal()
{
    ParserTestBase test;
    test.parseRoot("int hex(){return 0x2a;}");
    const auto root_nn = test.root();
    const auto funcDef_nn = test.firstFuncDef();
    const auto returnStmt_nn = test.extractReturnStmt(
        funcDef_nn(test.ast()).m_block_nn(test.ast()).m_blockItems.front());
    require(test.evaluateExp(returnStmt_nn(test.ast()).m_exp_nn) == 42,
        "0x2a must parse as a hexadecimal literal, not octal zero plus "
        "trailing input");
}

void testMalformedInputsFailWithFocusedDiagnostics()
{
    ParserTestBase test;
    test.parseSource("int main(){return 1}");
    require(!test.success(),
        "missing semicolon should produce a recovery diagnostic");
    require(test.root() != nullptr,
        "missing semicolon should recover to the block boundary and still "
        "build the root");
    require(test.firstDiagnostic().m_kind
            == DiagnosticKind::missingSemicolon,
        "missing semicolon should report the PEG recovery label");
    require(test.firstDiagnostic().m_offset == 19,
        "missing semicolon should diagnose at the statement boundary before "
        "the closing brace");

    test.parseSource("int main(){return 0x;}");
    require(!test.success(), "malformed hexadecimal literal should fail");
    require(test.firstDiagnostic().m_kind
            == DiagnosticKind::malformedReturnValue,
        "malformed hexadecimal literal should report the committed "
        "malformed-return diagnostic");

    test.parseSource("int main(){return 1;");
    require(!test.success(),
        "missing closing brace should produce a recovery diagnostic");
    require(test.root() != nullptr,
        "missing closing brace should recover at EOF and still build the root");
    require(
        test.firstDiagnostic().m_kind == DiagnosticKind::missingRBrace,
        "missing closing brace should report the PEG recovery label");

    test.parseSource("int main( {return 1;}");
    require(!test.success(),
        "missing ')' should produce a recovery diagnostic");
    require(test.root() != nullptr,
        "missing ')' should recover before the block and still build the root");
    require(test.firstDiagnostic().m_kind
            == DiagnosticKind::missingFuncRParen,
        "missing ')' should report the PEG recovery label");

    test.parseSource("int main(){nope}");
    require(!test.success(), "malformed statement head should fail");
    require(test.firstDiagnostic().m_kind
            == DiagnosticKind::missingSemicolon,
        "bare identifier statement should now report the expression-statement "
        "semicolon label");

    test.parseRoot("int main(){return ;}");
    const auto emptyReturnStmt = test.extractReturnStmt(
        test.firstFuncDef()(test.ast()).m_block_nn(test.ast()).m_blockItems.front());
    require(emptyReturnStmt(test.ast()).m_exp_nn == nullptr,
        "empty return should now parse as an optional-expression return");

    test.parseSource("int main(){return 1;} trailing");
    require(!test.success(), "trailing input should fail");
    require(
        test.firstDiagnostic().m_kind == DiagnosticKind::trailingInput,
        "trailing input should produce a trailing-input diagnostic");
}

void testOutOfRangeIntegerFails()
{
    ParserTestBase test;
    test.parseSource("int main(){return 2147483648;}");
    require(!test.success(), "out-of-range integer should fail");
    require(test.firstDiagnostic().m_kind == DiagnosticKind::integerOutOfRange,
        "out-of-range integer should report range overflow");
}

} // namespace

int main()
{
    testMinimalFunctionParse();
    testWhitespaceIsSkippedBetweenTokens();
    testCommentsAreSkippedBetweenTokens();
    testIntegerLiteralForms();
    testEmptyBlockParses();
    testHexOrderedChoiceWinsBeforeOctal();
    testMalformedInputsFailWithFocusedDiagnostics();
    testOutOfRangeIntegerFails();
    return 0;
}