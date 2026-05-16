#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

Handle<DeclNode> requireTopLevelDecl(
    const Handle<TopLevelItemNode>& topLevelItemNode_nn)
{
    Handle<DeclNode> declNode_nn;
    std::visit(
        [&](const auto& topLevelAlt) {
            using AltType = std::decay_t<decltype(topLevelAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<DeclNode>>) {
                declNode_nn = topLevelAlt;
            }
        },
        topLevelItemNode_nn->m_topLevelItem);
    require(declNode_nn != nullptr, "expected top-level declaration");
    return declNode_nn;
}

Handle<FuncDef> requireTopLevelFunc(
    const Handle<TopLevelItemNode>& topLevelItemNode_nn)
{
    Handle<FuncDef> funcDef_nn;
    std::visit(
        [&](const auto& topLevelAlt) {
            using AltType = std::decay_t<decltype(topLevelAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<FuncDef>>) {
                funcDef_nn = topLevelAlt;
            }
        },
        topLevelItemNode_nn->m_topLevelItem);
    require(funcDef_nn != nullptr, "expected top-level function definition");
    return funcDef_nn;
}

const Exp::Call& requireCallExp(const Handle<Exp>& exp_nn)
{
    const auto* callExp = std::get_if<Exp::Call>(&requireExp(exp_nn).m_kind);
    require(callExp != nullptr, "expected call expression root");
    return *callExp;
}

void testGlobalsFunctionsAndCallsParse()
{
    const auto root_nn = parseRoot(
        "const int seed = 4; int counter = 2; int add(int lhs, int rhs)"
        "{return lhs + rhs;} void noop(){return;} int main(){return "
        "add(seed, counter);}");

    require(root_nn->m_topLevelItems.size() == 5,
        "compilation unit should preserve globals and multiple functions in order");

    const auto constDecl_nn
        = extractConstDecl(requireTopLevelDecl(root_nn->m_topLevelItems[0]));
    require(constDecl_nn->m_constDefs[0]->m_identifier_nn->m_name == "seed",
        "global const declaration should preserve its identifier text");
    require(evaluateExp(*constDecl_nn->m_constDefs[0]->m_constInitVal_nn->m_exp_nn)
            == 4,
        "global const initializer should reuse expression parsing");

    const auto varDecl_nn
        = extractVarDecl(requireTopLevelDecl(root_nn->m_topLevelItems[1]));
    require(varDecl_nn->m_varDefs[0]->m_identifier_nn->m_name == "counter",
        "global var declaration should preserve its identifier text");
    require(evaluateExp(*varDecl_nn->m_varDefs[0]->m_initVal_nn->m_exp_nn) == 2,
        "global var initializer should preserve its expression tree");

    const auto addFunc_nn = requireTopLevelFunc(root_nn->m_topLevelItems[2]);
    require(addFunc_nn->m_funcType == FuncTypeKeyword::intKeyword,
        "int helper should preserve its function type");
    require(addFunc_nn->m_funcFParams.size() == 2,
        "helper should preserve both formal parameters");
    require(addFunc_nn->m_funcFParams[0]->m_identifier_nn->m_name == "lhs",
        "first formal parameter should preserve its identifier text");
    require(addFunc_nn->m_funcFParams[1]->m_identifier_nn->m_name == "rhs",
        "second formal parameter should preserve its identifier text");

    const auto noopFunc_nn = requireTopLevelFunc(root_nn->m_topLevelItems[3]);
    require(noopFunc_nn->m_funcType == FuncTypeKeyword::voidKeyword,
        "void helper should preserve the void function type");
    const auto noopReturn_nn
        = extractReturnStmt(noopFunc_nn->m_block_nn->m_blockItems.front());
    require(noopReturn_nn->m_exp_nn == nullptr,
        "void helper return should parse as a valueless return statement");

    const auto mainFunc_nn = requireTopLevelFunc(root_nn->m_topLevelItems[4]);
    const auto returnStmt_nn
        = extractReturnStmt(mainFunc_nn->m_block_nn->m_blockItems.front());
    const auto& callExp = requireCallExp(returnStmt_nn->m_exp_nn);
    require(callExp.m_func_nn->m_name == "add",
        "call expression should preserve the callee identifier");
    require(callExp.m_params.size() == 2,
        "call expression should preserve both actual arguments");
    require(requireLVal(callExp.m_params[0]).m_identifier_nn->m_name == "seed",
        "first call argument should preserve the referenced global const name");
    require(requireLVal(callExp.m_params[1]).m_identifier_nn->m_name == "counter",
        "second call argument should preserve the referenced global var name");
}

} // namespace

int main()
{
    testGlobalsFunctionsAndCallsParse();
    return 0;
}
