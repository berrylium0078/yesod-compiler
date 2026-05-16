#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testMinimalFunctionParse()
{
    const auto root_nn = parseRoot("int main(){return 42;}");
    const auto returnStmt_nn = extractReturnStmt(
        root_nn->m_funcDef_nn->m_block_nn->m_blockItems.front());

    require(root_nn->m_sourcePos.m_offset == 0,
        "root start offset should be the first token");
    require(root_nn->m_funcDef_nn->m_funcType == FuncTypeKeyword::intKeyword,
        "function type should use enum keyword");
    require(root_nn->m_funcDef_nn->m_identifier_nn->m_name == "main",
        "identifier payload should only store text");
    require(root_nn->m_funcDef_nn->m_block_nn->m_blockItems.size() == 1,
        "block should contain the single documented statement");
    require(returnStmt_nn->m_sourcePos.m_offset == 11,
        "statement start offset should point to the return keyword");
    require(returnStmt_nn->m_exp_nn->m_sourcePos.m_offset == 18,
        "expression start offset should match source byte offset");
    require(evaluateExp(*returnStmt_nn->m_exp_nn) == 42,
        "number literal should parse decimal values");
}

void testWhitespaceIsSkippedBetweenTokens()
{
    const std::string source
        = "\n \tint spaced_name ( )\n{\n  return\t7 ;\n}\n";
    const auto root_nn = parseRoot(source);
    const auto returnStmt_nn = extractReturnStmt(
        root_nn->m_funcDef_nn->m_block_nn->m_blockItems.front());

    require(root_nn->m_sourcePos.m_offset == 3,
        "leading whitespace should be skipped before the first token");
    require(root_nn->m_funcDef_nn->m_identifier_nn->m_name == "spaced_name",
        "identifier should survive trivia skipping");
    require(returnStmt_nn->m_sourcePos.m_offset == 27,
        "statement start offset should point to the return keyword");
    require(evaluateExp(*returnStmt_nn->m_exp_nn) == 7,
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

    const auto root_nn = parseRoot(source);
    const auto returnStmt_nn = extractReturnStmt(
        root_nn->m_funcDef_nn->m_block_nn->m_blockItems.front());

    require(root_nn->m_funcDef_nn->m_identifier_nn->m_name == "main",
        "comments should be skipped instead of entering the AST");
    require(evaluateExp(*returnStmt_nn->m_exp_nn) == 42,
        "number should parse across comments");
}

void testIntegerLiteralForms()
{
    const auto decimalRoot_nn = parseRoot("int d(){return 42;}");
    const auto octalRoot_nn = parseRoot("int o(){return 052;}");
    const auto hexadecimalRoot_nn = parseRoot("int h(){return 0X2a;}");

    require(evaluateExp(*extractReturnStmt(
                decimalRoot_nn->m_funcDef_nn->m_block_nn->m_blockItems.front())
                             ->m_exp_nn)
            == 42,
        "decimal literal should parse");
    require(evaluateExp(*extractReturnStmt(
                octalRoot_nn->m_funcDef_nn->m_block_nn->m_blockItems.front())
                             ->m_exp_nn)
            == 42,
        "octal literal should parse");
    require(
        evaluateExp(*extractReturnStmt(
            hexadecimalRoot_nn->m_funcDef_nn->m_block_nn->m_blockItems.front())
                         ->m_exp_nn)
            == 42,
        "hexadecimal literal should parse");
}

void testEmptyBlockParses()
{
    const auto root_nn = parseRoot("int main(){/*I am empty*/}");
    require(root_nn->m_funcDef_nn->m_block_nn->m_blockItems.empty(),
        "block grammar should allow zero block items");
}

void testHexOrderedChoiceWinsBeforeOctal()
{
    const auto root_nn = parseRoot("int hex(){return 0x2a;}");
    const auto returnStmt_nn = extractReturnStmt(
        root_nn->m_funcDef_nn->m_block_nn->m_blockItems.front());
    require(evaluateExp(*returnStmt_nn->m_exp_nn) == 42,
        "0x2a must parse as a hexadecimal literal, not octal zero plus "
        "trailing input");
}

void testMalformedInputsFailWithFocusedDiagnostics()
{
    const auto missingSemicolon = parseSource("int main(){return 1}");
    require(!missingSemicolon.success(),
        "missing semicolon should produce a recovery diagnostic");
    require(missingSemicolon.m_root != nullptr,
        "missing semicolon should recover to the block boundary and still "
        "build the root");
    require(firstDiagnostic(missingSemicolon).m_kind
            == DiagnosticKind::missingSemicolon,
        "missing semicolon should report the PEG recovery label");
    require(firstDiagnostic(missingSemicolon).m_offset == 19,
        "missing semicolon should diagnose at the statement boundary before "
        "the closing brace");

    const auto malformedHex = parseSource("int main(){return 0x;}");
    require(
        !malformedHex.success(), "malformed hexadecimal literal should fail");
    require(firstDiagnostic(malformedHex).m_kind
            == DiagnosticKind::malformedReturnValue,
        "malformed hexadecimal literal should report the committed "
        "malformed-return diagnostic");

    const auto missingBrace = parseSource("int main(){return 1;");
    require(!missingBrace.success(),
        "missing closing brace should produce a recovery diagnostic");
    require(missingBrace.m_root != nullptr,
        "missing closing brace should recover at EOF and still build the root");
    require(
        firstDiagnostic(missingBrace).m_kind == DiagnosticKind::missingRBrace,
        "missing closing brace should report the PEG recovery label");

    const auto missingFuncRParen = parseSource("int main( {return 1;}");
    require(!missingFuncRParen.success(),
        "missing ')' should produce a recovery diagnostic");
    require(missingFuncRParen.m_root != nullptr,
        "missing ')' should recover before the block and still build the root");
    require(firstDiagnostic(missingFuncRParen).m_kind
            == DiagnosticKind::missingFuncRParen,
        "missing ')' should report the PEG recovery label");

    const auto malformedStmtHead = parseSource("int main(){nope}");
    require(
        !malformedStmtHead.success(), "malformed statement head should fail");
    require(firstDiagnostic(malformedStmtHead).m_kind
            == DiagnosticKind::missingSemicolon,
        "bare identifier statement should now report the expression-statement "
        "semicolon label");

    const auto malformedReturnValue = parseSource("int main(){return ;}");
    require(
        !malformedReturnValue.success(), "missing return value should fail");
    require(firstDiagnostic(malformedReturnValue).m_kind
            == DiagnosticKind::malformedReturnValue,
        "missing return value should report the PEG recovery label");

    const auto trailingInput = parseSource("int main(){return 1;} trailing");
    require(!trailingInput.success(), "trailing input should fail");
    require(
        firstDiagnostic(trailingInput).m_kind == DiagnosticKind::trailingInput,
        "trailing input should produce a trailing-input diagnostic");
}

void testOutOfRangeIntegerFails()
{
    const auto output = parseSource("int main(){return 2147483648;}");
    require(!output.success(), "out-of-range integer should fail");
    require(firstDiagnostic(output).m_kind == DiagnosticKind::integerOutOfRange,
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