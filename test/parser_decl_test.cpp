#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserDeclTest : ParserTestBase {
    Ref<Exp> requireScalarConstInitExpHandle(
        const Ptr<ConstInitVal>& initVal_nn)
    {
        return MATCH(initVal_nn(ast()).kind)
            WITH([](const Ref<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) -> Ref<Exp> {
                    fail("expected scalar const initializer expression");
                    std::unreachable();
                });
    }

    Ref<Exp> requireScalarInitExpHandle(const Ptr<InitVal>& initVal_nn)
    {
        return MATCH(initVal_nn(ast()).kind)
            WITH([](const Ref<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) -> Ref<Exp> {
                    fail("expected scalar initializer expression");
                    std::unreachable();
                });
    }

    void testConstAndVarDeclarationsParse()
    {
        parseRoot("int main(){const int answer = 42; int value, other = 7; "
                  "return 0;}");
        const auto funcDef_nn = firstFuncDef();
        const auto& blockItems = funcDef_nn(ast()).body(ast()).items;

        require(blockItems.size() == 3,
            "block should contain const decl, var decl, and return statement");

        const auto constDecl_nn
            = extractConstDecl(extractDeclNode(blockItems[0]));
        require(constDecl_nn(ast()).bType == BTypeKeyword::intKeyword,
            "const declaration should store its keyword enum");
        require(constDecl_nn(ast()).constDef.size() == 1,
            "const declaration should keep its declarator list");
        require(constDecl_nn(ast()).constDef[0](ast()).identifier(ast()).name
                == "answer",
            "identifier payload should only store the source text");
        require(evaluateExp(requireScalarConstInitExpHandle(
                    constDecl_nn(ast()).constDef[0](ast()).constInitVal))
                == 42,
            "const initializer should reuse the expression grammar");

        const auto varDecl_nn = extractVarDecl(extractDeclNode(blockItems[1]));
        require(varDecl_nn(ast()).bType == BTypeKeyword::intKeyword,
            "var declaration should store its keyword enum");
        require(varDecl_nn(ast()).varDef.size() == 2,
            "var declaration should preserve comma-separated declarators");
        require(varDecl_nn(ast()).varDef[0](ast()).identifier(ast()).name
                == "value",
            "first var declarator should preserve its identifier text");
        require(varDecl_nn(ast()).varDef[0](ast()).initVal == nullptr,
            "uninitialized var declarators should not fabricate an init node");
        require(varDecl_nn(ast()).varDef[1](ast()).identifier(ast()).name
                == "other",
            "second var declarator should preserve its identifier text");
        require(varDecl_nn(ast()).varDef[1](ast()).initVal != nullptr,
            "initialized var declarators should preserve an init node");
        require(evaluateExp(requireScalarInitExpHandle(
                    varDecl_nn(ast()).varDef[1](ast()).initVal))
                == 7,
            "var initializer should preserve the parsed expression tree");
    }

    void testDeclarationRecoveryDiagnostics()
    {
        parseSource("int main(){const int = 1; return 0;}");
        require(!success(), "malformed declarator should fail");
        require(isDiagnostic<MalformedDeclItemDiagnostic>(firstDiagnostic()),
            "malformed declarator should report the declaration-item label");
        require(root() != nullptr,
            "malformed declaration should still recover to a root");

        parseSource("int main(){int value return 1;}");
        require(!success(), "missing declaration semicolon should fail");
        require(isDiagnostic<MissingDeclSemicolonDiagnostic>(firstDiagnostic()),
            "missing declaration semicolon should report the declaration "
            "label");
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
