#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserFunctionTest : ParserTestBase {
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

    Decl requireTopLevelDeclNode(const CompUnit::Item& topLevelItem)
    {
        return MATCH(topLevelItem) WITH([](const Decl& decl) { return decl; },
            [](const auto&) -> Decl {
                fail("expected top-level declaration");
                std::unreachable();
            });
    }

    Ptr<FuncDef> requireTopLevelFuncDef(const CompUnit::Item& topLevelItem)
    {
        const auto funcDef_nn = MATCH(topLevelItem)
            WITH([](const Ref<FuncDef>& funcDef_nn) {
                return funcDef_nn.ptr();
            },
                [](const auto&) { return Ptr<FuncDef> { }; }, );
        require(
            funcDef_nn != nullptr, "expected top-level function definition");
        return funcDef_nn;
    }

    const Exp::Call& requireCallExpHandle(const Ref<Exp>& exp_nn)
    {
        const auto callExp = MATCH(exp_nn(ast()).kind)
            WITH([](const Exp::Call& callExp) { return &callExp; },
                [](const auto&) {
                    return static_cast<const Exp::Call*>(nullptr);
                }, );
        require(callExp != nullptr, "expected call expression root");
        return *callExp;
    }

    void testGlobalsFunctionsAndCallsParse()
    {
        parseRoot(
            "const int seed = 4; int counter = 2; int add(int lhs, int rhs)"
            "{return lhs + rhs;} void noop(){return;} int main(){return "
            "add(seed, counter);}");

        require(root()(ast()).topLevelItems.size() == 5,
            "compilation unit should preserve globals and multiple functions "
            "in order");

        const auto constDecl_nn = extractConstDecl(
            requireTopLevelDeclNode(root()(ast()).topLevelItems[0]));
        require(constDecl_nn(ast()).constDef[0](ast()).identifier(ast()).name
                == "seed",
            "global const declaration should preserve its identifier text");
        require(evaluateExp(requireScalarConstInitExpHandle(
                    constDecl_nn(ast()).constDef[0](ast()).constInitVal))
                == 4,
            "global const initializer should reuse expression parsing");

        const auto varDecl_nn = extractVarDecl(
            requireTopLevelDeclNode(root()(ast()).topLevelItems[1]));
        require(varDecl_nn(ast()).varDef[0](ast()).identifier(ast()).name
                == "counter",
            "global var declaration should preserve its identifier text");
        require(evaluateExp(requireScalarInitExpHandle(
                    varDecl_nn(ast()).varDef[0](ast()).initVal))
                == 2,
            "global var initializer should preserve its expression tree");

        const auto addFunc_nn
            = requireTopLevelFuncDef(root()(ast()).topLevelItems[2]);
        require(addFunc_nn(ast()).m_funcType == FuncTypeKeyword::intKeyword,
            "int helper should preserve its function type");
        require(addFunc_nn(ast()).funcFParams.size() == 2,
            "helper should preserve both formal parameters");
        require(
            addFunc_nn(ast()).funcFParams[0].identifier(ast()).name == "lhs",
            "first formal parameter should preserve its identifier text");
        require(
            addFunc_nn(ast()).funcFParams[1].identifier(ast()).name == "rhs",
            "second formal parameter should preserve its identifier text");

        const auto noopFunc_nn
            = requireTopLevelFuncDef(root()(ast()).topLevelItems[3]);
        require(noopFunc_nn(ast()).m_funcType == FuncTypeKeyword::voidKeyword,
            "void helper should preserve the void function type");
        const auto noopReturn_nn
            = extractReturnStmt(noopFunc_nn(ast()).body(ast()).items.front());
        require(noopReturn_nn(ast()).exp == nullptr,
            "void helper return should parse as a valueless return statement");

        const auto mainFunc_nn
            = requireTopLevelFuncDef(root()(ast()).topLevelItems[4]);
        const auto returnStmt_nn
            = extractReturnStmt(mainFunc_nn(ast()).body(ast()).items.front());
        const auto& callExp
            = requireCallExpHandle(returnStmt_nn(ast()).exp.ref());
        require(callExp.funcName(ast()).name == "add",
            "call expression should preserve the callee identifier");
        require(callExp.params.size() == 2,
            "call expression should preserve both actual arguments");
        require(requireLVal(callExp.params[0]).identifier(ast()).name == "seed",
            "first call argument should preserve the referenced global const "
            "name");
        require(
            requireLVal(callExp.params[1]).identifier(ast()).name == "counter",
            "second call argument should preserve the referenced global var "
            "name");
    }
};

} // namespace

int main()
{
    ParserFunctionTest test;
    test.testGlobalsFunctionsAndCallsParse();
    return 0;
}
