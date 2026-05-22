#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserFunctionTest : ParserTestBase {
    Ptr<Exp> requireScalarConstInitExpHandle(
        const Ptr<ConstInitVal>& initVal_nn)
    {
        const auto exp_nn = MATCH(initVal_nn(ast()).m_kind)
            WITH([](const Ptr<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) { return Ptr<Exp> { }; }, );
        require(
            exp_nn != nullptr, "expected scalar const initializer expression");
        return exp_nn;
    }

    Ptr<Exp> requireScalarInitExpHandle(const Ptr<InitVal>& initVal_nn)
    {
        const auto exp_nn = MATCH(initVal_nn(ast()).m_kind)
            WITH([](const Ptr<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) { return Ptr<Exp> { }; }, );
        require(exp_nn != nullptr, "expected scalar initializer expression");
        return exp_nn;
    }

    Ptr<DeclNode> requireTopLevelDeclNode(
        const Ptr<TopLevelItemNode>& topLevelItemNode_nn)
    {
        const auto declNode_nn
            = MATCH(topLevelItemNode_nn(ast()).m_topLevelItem) WITH(
                [](const Ptr<DeclNode>& declNode_nn) { return declNode_nn; },
                [](const auto&) { return Ptr<DeclNode> { }; }, );
        require(declNode_nn != nullptr, "expected top-level declaration");
        return declNode_nn;
    }

    Ptr<FuncDef> requireTopLevelFuncDef(
        const Ptr<TopLevelItemNode>& topLevelItemNode_nn)
    {
        const auto funcDef_nn = MATCH(topLevelItemNode_nn(ast()).m_topLevelItem)
            WITH([](const Ptr<FuncDef>& funcDef_nn) { return funcDef_nn; },
                [](const auto&) { return Ptr<FuncDef> { }; }, );
        require(
            funcDef_nn != nullptr, "expected top-level function definition");
        return funcDef_nn;
    }

    const Exp::Call& requireCallExpHandle(const Ptr<Exp>& exp_nn)
    {
        const auto callExp = MATCH(exp_nn(ast()).m_kind)
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

        require(root()(ast()).m_topLevelItems.size() == 5,
            "compilation unit should preserve globals and multiple functions "
            "in order");

        const auto constDecl_nn = extractConstDecl(
            requireTopLevelDeclNode(root()(ast()).m_topLevelItems[0]));
        require(constDecl_nn(ast())
                    .m_constDefs[0](ast())
                    .m_identifier_nn(ast())
                    .m_name
                == "seed",
            "global const declaration should preserve its identifier text");
        require(
            evaluateExp(requireScalarConstInitExpHandle(
                constDecl_nn(ast()).m_constDefs[0](ast()).m_constInitVal_nn))
                == 4,
            "global const initializer should reuse expression parsing");

        const auto varDecl_nn = extractVarDecl(
            requireTopLevelDeclNode(root()(ast()).m_topLevelItems[1]));
        require(
            varDecl_nn(ast()).m_varDefs[0](ast()).m_identifier_nn(ast()).m_name
                == "counter",
            "global var declaration should preserve its identifier text");
        require(evaluateExp(requireScalarInitExpHandle(
                    varDecl_nn(ast()).m_varDefs[0](ast()).m_initVal_nn))
                == 2,
            "global var initializer should preserve its expression tree");

        const auto addFunc_nn
            = requireTopLevelFuncDef(root()(ast()).m_topLevelItems[2]);
        require(addFunc_nn(ast()).m_funcType == FuncTypeKeyword::intKeyword,
            "int helper should preserve its function type");
        require(addFunc_nn(ast()).m_funcFParams.size() == 2,
            "helper should preserve both formal parameters");
        require(addFunc_nn(ast())
                    .m_funcFParams[0](ast())
                    .m_identifier_nn(ast())
                    .m_name
                == "lhs",
            "first formal parameter should preserve its identifier text");
        require(addFunc_nn(ast())
                    .m_funcFParams[1](ast())
                    .m_identifier_nn(ast())
                    .m_name
                == "rhs",
            "second formal parameter should preserve its identifier text");

        const auto noopFunc_nn
            = requireTopLevelFuncDef(root()(ast()).m_topLevelItems[3]);
        require(noopFunc_nn(ast()).m_funcType == FuncTypeKeyword::voidKeyword,
            "void helper should preserve the void function type");
        const auto noopReturn_nn = extractReturnStmt(
            noopFunc_nn(ast()).m_block_nn(ast()).m_blockItems.front());
        require(noopReturn_nn(ast()).m_exp_nn == nullptr,
            "void helper return should parse as a valueless return statement");

        const auto mainFunc_nn
            = requireTopLevelFuncDef(root()(ast()).m_topLevelItems[4]);
        const auto returnStmt_nn = extractReturnStmt(
            mainFunc_nn(ast()).m_block_nn(ast()).m_blockItems.front());
        const auto& callExp
            = requireCallExpHandle(returnStmt_nn(ast()).m_exp_nn);
        require(callExp.m_func_nn(ast()).m_name == "add",
            "call expression should preserve the callee identifier");
        require(callExp.m_params.size() == 2,
            "call expression should preserve both actual arguments");
        require(requireLVal(callExp.m_params[0]).m_identifier_nn(ast()).m_name
                == "seed",
            "first call argument should preserve the referenced global const "
            "name");
        require(requireLVal(callExp.m_params[1]).m_identifier_nn(ast()).m_name
                == "counter",
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
