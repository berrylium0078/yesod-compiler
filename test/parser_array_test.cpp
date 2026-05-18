#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

constexpr const char* kFunctionArrayParamSource =
    "void f1(int n, int a[]) {"
    "while (n > 0) {"
    "n = n - 1;"
    "a[n] = 0;"
    "}"
    "}"
    "void f2(int n, int a[][10]) {"
    "while (n > 0) {"
    "n = n - 1;"
    "f1(10, a[n]);"
    "}"
    "}"
    "void f3(int n, int a[][10][10]) {"
    "while (n > 0) {"
    "n = n - 1;"
    "f2(10, a[n]);"
    "}"
    "}"
    "int main() {"
    "int a[10][10][10];"
    "f3(10, a);"
    "}";

const LVal& requireLValExp(const Handle<Exp>& exp_nn)
{
    return requireLVal(exp_nn);
}

const InitVal::List& requireInitList(const Handle<InitVal>& initVal_nn)
{
    const InitVal::List* list_nn = nullptr;
    std::visit(
        [&](const auto& initAlt) {
            using AltType = std::decay_t<decltype(initAlt)>;
            if constexpr (std::is_same_v<AltType, InitVal::List>) {
                list_nn = &initAlt;
            }
        },
        initVal_nn->m_kind);
    require(list_nn != nullptr, "expected brace initializer list");
    return *list_nn;
}

void testArrayDeclaratorsAndLValuesParse()
{
    const auto output = parseRoot(
        "int load(int a[], int b[][3]){int arr[2] = {1, 2}; arr[1] = b[0][1]; return a[0];}");
    const auto funcDef_nn = firstFuncDef(output.m_root);

    require(funcDef_nn->m_funcFParams.size() == 2,
        "array function should preserve both formal parameters");
    require(funcDef_nn->m_funcFParams[0]->m_isArray,
        "first parameter should record the unsized array marker");
    require(funcDef_nn->m_funcFParams[0]->m_trailingDimensions.empty(),
        "first parameter should have no trailing dimensions");
    require(funcDef_nn->m_funcFParams[1]->m_isArray,
        "second parameter should record the unsized array marker");
    require(funcDef_nn->m_funcFParams[1]->m_trailingDimensions.size() == 1,
        "second parameter should preserve its trailing constant dimension");

    const auto& blockItems = funcDef_nn->m_block_nn->m_blockItems;
    const auto varDecl_nn = extractVarDecl(extractDeclNode(blockItems[0]));
    require(varDecl_nn->m_varDefs[0]->m_dimensions.size() == 1,
        "local array declaration should preserve one dimension expression");
    const auto& initList = requireInitList(varDecl_nn->m_varDefs[0]->m_initVal_nn);
    require(initList.m_values.size() == 2,
        "brace initializer should preserve both initializer elements");

    const auto assignStmt_nn = extractAssignStmt(extractStmtNode(blockItems[1]));
    require(requireLValExp(assignStmt_nn->m_lVal_nn).m_indices.size() == 1,
        "assignment lhs should preserve its subscript");
    require(requireLValExp(assignStmt_nn->m_exp_nn).m_indices.size() == 2,
        "rhs array read should preserve both subscripts");

    const auto returnStmt_nn = extractReturnStmt(extractStmtNode(blockItems[2]));
    require(requireLValExp(returnStmt_nn->m_exp_nn).m_indices.size() == 1,
        "return expression should preserve array subscripts");
}

void testArrayRecoveryDiagnostic()
{
    const auto output = parseSource("int main(){int arr[2; return 0;}");
    require(!output.success(), "missing array bracket should fail parsing");
    require(firstDiagnostic(output).m_kind == DiagnosticKind::missingArrayRBracket,
        "missing array bracket should report the dedicated recovery label");
    require(output.m_root != nullptr,
        "array declarator recovery should still produce a compilation unit");
}

void testNestedFunctionArrayParametersParse()
{
    const auto output = parseRoot(kFunctionArrayParamSource);
    require(output.m_root->m_topLevelItems.size() == 4,
        "function-array-parameter sample should preserve all helper functions and main");

    std::vector<Handle<FuncDef>> funcs;
    for (const auto topLevelItem_nn : output.m_root->m_topLevelItems) {
        Handle<FuncDef> funcDef_nn;
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType, Handle<FuncDef>>) {
                    funcDef_nn = topLevelAlt;
                }
            },
            topLevelItem_nn->m_topLevelItem);
        if (funcDef_nn) {
            funcs.push_back(funcDef_nn);
        }
    }

    require(funcs.size() == 4,
        "function-array-parameter sample should parse four function definitions");
    require(funcs[0]->m_funcFParams[1]->m_isArray,
        "f1 should preserve its unsized array parameter marker");
    require(funcs[0]->m_funcFParams[1]->m_trailingDimensions.empty(),
        "f1 should preserve zero trailing array dimensions");
    require(funcs[1]->m_funcFParams[1]->m_isArray,
        "f2 should preserve its unsized array parameter marker");
    require(funcs[1]->m_funcFParams[1]->m_trailingDimensions.size() == 1,
        "f2 should preserve one trailing array dimension");
    require(funcs[2]->m_funcFParams[1]->m_isArray,
        "f3 should preserve its unsized array parameter marker");
    require(funcs[2]->m_funcFParams[1]->m_trailingDimensions.size() == 2,
        "f3 should preserve two trailing array dimensions");

    const auto mainVarDecl_nn
        = extractVarDecl(extractDeclNode(funcs[3]->m_block_nn->m_blockItems[0]));
    require(mainVarDecl_nn->m_varDefs[0]->m_dimensions.size() == 3,
        "main should preserve all three array declarator dimensions");
}

} // namespace

int main()
{
    testArrayDeclaratorsAndLValuesParse();
    testArrayRecoveryDiagnostic();
    testNestedFunctionArrayParametersParse();
    return 0;
}