#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

void testConstAndVarDeclarationsParse()
{
    const auto root_nn = parseRoot(
        "int main(){const int answer = 42; int value, other = 7; return 0;}");
    const auto funcDef_nn = firstFuncDef(root_nn.m_root);
    const auto& blockItems = funcDef_nn->m_block_nn->m_blockItems;

    require(blockItems.size() == 3,
        "block should contain const decl, var decl, and return statement");

    const auto constDecl_nn = extractConstDecl(extractDeclNode(blockItems[0]));
    require(constDecl_nn->m_bType == BTypeKeyword::intKeyword,
        "const declaration should store its keyword enum");
    require(constDecl_nn->m_constDefs.size() == 1,
        "const declaration should keep its declarator list");
    require(constDecl_nn->m_constDefs[0]->m_identifier_nn->m_name == "answer",
        "identifier payload should only store the source text");
    require(
        evaluateExp(*constDecl_nn->m_constDefs[0]->m_constInitVal_nn->m_exp_nn)
            == 42,
        "const initializer should reuse the expression grammar");

    const auto varDecl_nn = extractVarDecl(extractDeclNode(blockItems[1]));
    require(varDecl_nn->m_bType == BTypeKeyword::intKeyword,
        "var declaration should store its keyword enum");
    require(varDecl_nn->m_varDefs.size() == 2,
        "var declaration should preserve comma-separated declarators");
    require(varDecl_nn->m_varDefs[0]->m_identifier_nn->m_name == "value",
        "first var declarator should preserve its identifier text");
    require(varDecl_nn->m_varDefs[0]->m_initVal_nn == nullptr,
        "uninitialized var declarators should not fabricate an init node");
    require(varDecl_nn->m_varDefs[1]->m_identifier_nn->m_name == "other",
        "second var declarator should preserve its identifier text");
    require(varDecl_nn->m_varDefs[1]->m_initVal_nn != nullptr,
        "initialized var declarators should preserve an init node");
    require(evaluateExp(*varDecl_nn->m_varDefs[1]->m_initVal_nn->m_exp_nn) == 7,
        "var initializer should preserve the parsed expression tree");
}

void testDeclarationRecoveryDiagnostics()
{
    const auto malformedDecl
        = parseSource("int main(){const int = 1; return 0;}");
    require(!malformedDecl.success(), "malformed declarator should fail");
    require(firstDiagnostic(malformedDecl).m_kind
            == DiagnosticKind::malformedDeclItem,
        "malformed declarator should report the declaration-item label");
    require(malformedDecl.m_root != nullptr,
        "malformed declaration should still recover to a root");

    const auto missingDeclSemicolon
        = parseSource("int main(){int value return 1;}");
    require(!missingDeclSemicolon.success(),
        "missing declaration semicolon should fail");
    require(firstDiagnostic(missingDeclSemicolon).m_kind
            == DiagnosticKind::missingDeclSemicolon,
        "missing declaration semicolon should report the declaration label");
    require(missingDeclSemicolon.m_root != nullptr,
        "missing declaration semicolon should still recover to a root");
}

} // namespace

int main()
{
    testConstAndVarDeclarationsParse();
    testDeclarationRecoveryDiagnostics();
    return 0;
}
