#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testExpressionAndEmptyStatementsParse()
{
    ParserTestBase test;
    test.parseRoot("int main(){1 + 2; ; return 0;}");
    const auto funcDef_nn = test.firstFuncDef();
    const auto& blockItems = funcDef_nn(test.ast()).body(test.ast()).items;

    require(blockItems.size() == 3,
        "block should contain expression statement, empty statement, and "
        "return");

    const auto expStmt_nn = test.extractExpStmt(blockItems[0]);
    require(expStmt_nn(test.ast()).exp != nullptr,
        "non-empty expression statement should preserve its expression");
    require(test.evaluateExp(expStmt_nn(test.ast()).exp.ref()) == 3,
        "expression statement should reuse the expression grammar");

    const auto emptyStmt_nn = test.extractExpStmt(blockItems[1]);
    require(emptyStmt_nn(test.ast()).exp == nullptr,
        "empty statement should not fabricate an expression node");
}

void testNestedBlockStatementsParse()
{
    ParserTestBase test;
    test.parseRoot("int main(){{int inner = 1; return inner;} return 0;}");
    const auto funcDef_nn = test.firstFuncDef();
    const auto& blockItems = funcDef_nn(test.ast()).body(test.ast()).items;

    require(blockItems.size() == 2,
        "outer block should contain nested block statement and trailing "
        "return");

    const auto nestedBlock_nn = test.extractBlockStmt(blockItems[0]);
    require(nestedBlock_nn(test.ast()).items.size() == 2,
        "nested block statement should preserve its own scoped items");
    require(test.extractVarDecl(test.extractDeclNode(
                nestedBlock_nn(test.ast()).items[0]))(test.ast())
                .varDef[0](test.ast())
                .identifier(test.ast())
                .name
            == "inner",
        "nested block declaration should preserve identifier payload");
    const auto returnStmt_nn
        = test.extractReturnStmt(nestedBlock_nn(test.ast()).items[1]);
    require(test.requireLVal(returnStmt_nn(test.ast()).exp.ref())
                .identifier(test.ast())
                .name
            == "inner",
        "nested block return should preserve its lvalue identifier");
}

void testExpressionStatementRecovery()
{
    ParserTestBase test;
    test.parseSource("int main(){1 + 2 return 0;}");
    require(
        !test.success(), "missing expression-statement semicolon should fail");
    require(isDiagnostic<MissingSemicolonDiagnostic>(test.firstDiagnostic()),
        "missing expression-statement semicolon should report the shared "
        "semicolon label");
    require(test.root() != nullptr,
        "missing expression-statement semicolon should still recover to a "
        "root");

    test.parseSource("int main(){@ return 0;}");
    require(!test.success(), "invalid block item should fail");
    require(isDiagnostic<MalformedBlockItemDiagnostic>(test.firstDiagnostic()),
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