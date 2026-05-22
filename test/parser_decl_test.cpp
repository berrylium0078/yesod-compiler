#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserDeclTest : ParserTestBase {
    Ptr<Exp> requireScalarConstInitExpHandle(
        const Ptr<ConstInitVal>& initVal_nn)
    {
        const auto exp_nn = MATCH(initVal_nn(ast()).m_kind)
            WITH (
                [](const Ptr<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) { return Ptr<Exp> {}; },
            );
        require(exp_nn != nullptr,
            "expected scalar const initializer expression");
        return exp_nn;
    }

    Ptr<Exp> requireScalarInitExpHandle(const Ptr<InitVal>& initVal_nn)
    {
        const auto exp_nn = MATCH(initVal_nn(ast()).m_kind)
            WITH (
                [](const Ptr<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) { return Ptr<Exp> {}; },
            );
        require(exp_nn != nullptr, "expected scalar initializer expression");
        return exp_nn;
    }

    void testConstAndVarDeclarationsParse()
    {
        parseRoot(
            "int main(){const int answer = 42; int value, other = 7; return 0;}");
        const auto funcDef_nn = firstFuncDef();
        const auto& blockItems = funcDef_nn(ast()).m_block_nn(ast()).m_blockItems;

        require(blockItems.size() == 3,
            "block should contain const decl, var decl, and return statement");

        const auto constDecl_nn = extractConstDecl(extractDeclNode(blockItems[0]));
        require(constDecl_nn(ast()).m_bType == BTypeKeyword::intKeyword,
            "const declaration should store its keyword enum");
        require(constDecl_nn(ast()).m_constDefs.size() == 1,
            "const declaration should keep its declarator list");
        require(constDecl_nn(ast()).m_constDefs[0](ast()).m_identifier_nn(ast()).m_name
                == "answer",
            "identifier payload should only store the source text");
        require(evaluateExp(requireScalarConstInitExpHandle(
                    constDecl_nn(ast()).m_constDefs[0](ast()).m_constInitVal_nn))
                == 42,
            "const initializer should reuse the expression grammar");

        const auto varDecl_nn = extractVarDecl(extractDeclNode(blockItems[1]));
        require(varDecl_nn(ast()).m_bType == BTypeKeyword::intKeyword,
            "var declaration should store its keyword enum");
        require(varDecl_nn(ast()).m_varDefs.size() == 2,
            "var declaration should preserve comma-separated declarators");
        require(varDecl_nn(ast()).m_varDefs[0](ast()).m_identifier_nn(ast()).m_name
                == "value",
            "first var declarator should preserve its identifier text");
        require(varDecl_nn(ast()).m_varDefs[0](ast()).m_initVal_nn == nullptr,
            "uninitialized var declarators should not fabricate an init node");
        require(varDecl_nn(ast()).m_varDefs[1](ast()).m_identifier_nn(ast()).m_name
                == "other",
            "second var declarator should preserve its identifier text");
        require(varDecl_nn(ast()).m_varDefs[1](ast()).m_initVal_nn != nullptr,
            "initialized var declarators should preserve an init node");
        require(evaluateExp(requireScalarInitExpHandle(
                    varDecl_nn(ast()).m_varDefs[1](ast()).m_initVal_nn))
                == 7,
            "var initializer should preserve the parsed expression tree");
    }

    void testDeclarationRecoveryDiagnostics()
    {
        parseSource("int main(){const int = 1; return 0;}");
        require(!success(), "malformed declarator should fail");
        require(firstDiagnostic().m_kind == DiagnosticKind::malformedDeclItem,
            "malformed declarator should report the declaration-item label");
        require(root() != nullptr,
            "malformed declaration should still recover to a root");

        parseSource("int main(){int value return 1;}");
        require(!success(), "missing declaration semicolon should fail");
        require(firstDiagnostic().m_kind
                == DiagnosticKind::missingDeclSemicolon,
            "missing declaration semicolon should report the declaration label");
        require(root() != nullptr,
            "missing declaration semicolon should still recover to a root");
    }
};

} // namespace

int main()
{
    ParserDeclTest test;
    test.testConstAndVarDeclarationsParse();
    test.testDeclarationRecoveryDiagnostics();
    return 0;
}
