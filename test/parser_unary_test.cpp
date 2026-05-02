#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testUnaryOperatorsAndParentheses() {
    require(evaluateExp(*extractReturnStmt(parseRoot("int plus(){return +42;}")->m_funcDef_nn->m_block_nn->m_statements.front())->m_exp_nn) == 42, "unary plus should parse");
    require(evaluateExp(*extractReturnStmt(parseRoot("int minus(){return -42;}")->m_funcDef_nn->m_block_nn->m_statements.front())->m_exp_nn) == -42, "unary minus should parse");
    require(evaluateExp(*extractReturnStmt(parseRoot("int bang(){return !0;}")->m_funcDef_nn->m_block_nn->m_statements.front())->m_exp_nn) == 1, "logical not should parse");
    require(evaluateExp(*extractReturnStmt(parseRoot("int nested(){return -(+42);}")->m_funcDef_nn->m_block_nn->m_statements.front())->m_exp_nn) == -42, "nested unary expressions should parse recursively");
}

void testUnaryTriviaAndParenthesizedPrimary() {
    const std::string source =
        "\n\tint unary( )\n{\n"
        "  return /* unary */ - ( /* number */ 052 ) ;\n"
        "}\n";
    const auto root_nn = parseRoot(source);
    const auto returnStmt_nn = extractReturnStmt(root_nn->m_funcDef_nn->m_block_nn->m_statements.front());

    require(evaluateExp(*returnStmt_nn->m_exp_nn) == -42, "unary expression should parse across comments and whitespace");
    require(expressionContainsParenthesizedPrimary(*returnStmt_nn->m_exp_nn), "parenthesized primary expression should survive unary parsing");
}

void testUnaryRecoveryDiagnostics() {
    const auto malformedReturnValue = parseSource("int main(){return -;}");
    require(!malformedReturnValue.success(), "missing unary operand should fail");
    require(firstDiagnostic(malformedReturnValue).m_kind == DiagnosticKind::malformedReturnValue, "missing unary operand should report the return-value diagnostic");

    const auto malformedPrimaryExp = parseSource("int main(){return (;}");
    require(!malformedPrimaryExp.success(), "malformed parenthesized primary expression should fail");
    require(firstDiagnostic(malformedPrimaryExp).m_kind == DiagnosticKind::malformedPrimaryExp, "malformed parenthesized expression should report the primary-expression diagnostic");

    const auto missingPrimaryRParen = parseSource("int main(){return (1;}");
    require(!missingPrimaryRParen.success(), "missing ')' in primary expression should produce a recovery diagnostic");
    require(missingPrimaryRParen.m_root != nullptr, "missing ')' in primary expression should recover to the statement boundary and still build the root");
    require(firstDiagnostic(missingPrimaryRParen).m_kind == DiagnosticKind::missingPrimaryRParen, "missing ')' in primary expression should report the primary-expression delimiter diagnostic");
    require(evaluateExp(*extractReturnStmt(missingPrimaryRParen.m_root->m_funcDef_nn->m_block_nn->m_statements.front())->m_exp_nn) == 1, "missing ')' recovery should preserve the inner expression value");
}

}  // namespace

int main() {
    testUnaryOperatorsAndParentheses();
    testUnaryTriviaAndParenthesizedPrimary();
    testUnaryRecoveryDiagnostics();
    return 0;
}