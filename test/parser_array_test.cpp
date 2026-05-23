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

struct ParserArrayTest : ParserTestBase {
    const InitVal::List& requireInitListHandle(const Ptr<InitVal>& initVal_nn)
    {
        const auto* list = MATCH (initVal_nn(ast()).kind)
            WITH (
                [](const InitVal::List& list) { return &list; },
                [](const auto&) { return static_cast<const InitVal::List*>(nullptr); },
            );
        require(list != nullptr, "expected brace initializer list");
        return *list;
    }

    void testArrayDeclaratorsAndLValuesParse()
    {
        parseRoot(
            "int load(int a[], int b[][3]){int arr[2] = {1, 2}; arr[1] = b[0][1]; return a[0];}");
        const auto funcDef_nn = firstFuncDef();

        require(funcDef_nn(ast()).funcFParams.size() == 2,
            "array function should preserve both formal parameters");
        require(funcDef_nn(ast()).funcFParams[0].m_trailingDimensions.empty(),
            "first parameter should have no trailing dimensions");
        require(funcDef_nn(ast()).funcFParams[1].m_trailingDimensions.size()
                == 1,
            "second parameter should preserve its trailing constant dimension");

        const auto& blockItems = funcDef_nn(ast()).body(ast()).items;
        const auto varDecl_nn = extractVarDecl(extractDeclNode(blockItems[0]));
        require(varDecl_nn(ast()).varDef[0](ast()).shape.size() == 1,
            "local array declaration should preserve one dimension expression");
        const auto& initList = requireInitListHandle(
            varDecl_nn(ast()).varDef[0](ast()).initVal);
        require(initList.size() == 2,
            "brace initializer should preserve both initializer elements");

        const auto assignStmt_nn = extractAssignStmt(extractStmtNode(blockItems[1]));
        require(requireLVal(assignStmt_nn(ast()).lval).indices.size() == 1,
            "assignment lhs should preserve its subscript");
        require(requireLVal(assignStmt_nn(ast()).exp).indices.size() == 2,
            "rhs array read should preserve both subscripts");

        const auto returnStmt_nn = extractReturnStmt(extractStmtNode(blockItems[2]));
        require(requireLVal(returnStmt_nn(ast()).exp.ref()).indices.size() == 1,
            "return expression should preserve array subscripts");
    }

    void testArrayRecoveryDiagnostic()
    {
        parseSource("int main(){int arr[2; return 0;}");
        require(!success(), "missing array bracket should fail parsing");
        require(firstDiagnostic().kind == DiagnosticKind::missingArrayRBracket,
            "missing array bracket should report the dedicated recovery label");
        require(root() != nullptr,
            "array declarator recovery should still produce a compilation unit");
    }

    void testNestedFunctionArrayParametersParse()
    {
        parseRoot(kFunctionArrayParamSource);
        require(root()(ast()).topLevelItems.size() == 4,
            "function-array-parameter sample should preserve all helper functions and main");

        std::vector<Ptr<FuncDef>> funcs;
        for (const auto topLevelItem : root()(ast()).topLevelItems) {
            const auto funcDef_nn = MATCH(topLevelItem)
                WITH (
                    [](const Ptr<FuncDef>& funcDef_nn) {
                        return funcDef_nn;
                    },
                    [](const auto&) { return Ptr<FuncDef> {}; },
                );
            if (funcDef_nn) {
                funcs.push_back(funcDef_nn);
            }
        }

        require(funcs.size() == 4,
            "function-array-parameter sample should parse four function definitions");
        require(funcs[0](ast()).funcFParams[1].m_trailingDimensions.empty(),
            "f1 should preserve zero trailing array dimensions");
        require(funcs[1](ast()).funcFParams[1].m_trailingDimensions.size()
                == 1,
            "f2 should preserve one trailing array dimension");
        require(funcs[2](ast()).funcFParams[1].m_trailingDimensions.size()
                == 2,
            "f3 should preserve two trailing array dimensions");

        const auto mainVarDecl_nn = extractVarDecl(extractDeclNode(
            funcs[3](ast()).body(ast()).items[0]));
        require(mainVarDecl_nn(ast()).varDef[0](ast()).shape.size() == 3,
            "main should preserve all three array declarator dimensions");
    }
};

} // namespace

int main()
{
    ParserArrayTest test;
    test.testArrayDeclaratorsAndLValuesParse();
    test.testArrayRecoveryDiagnostic();
    test.testNestedFunctionArrayParametersParse();
    return 0;
}