#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

constexpr const char* kFunctionArrayParamSource
    = "void f1(int n, int a[]) {"
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

constexpr const char* kShadowedConstArraySource
    = "const int garr[10] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15};"
      "int main(){"
      "const int arr[10] = {1, 2, 3, 4, 5};"
      "int i = 0, sum = 0;"
      "while(i < 10){"
      "sum = sum + arr[i] + garr[i];"
      "i = i + 1;"
      "}"
      "const int garr[10] = {1};"
      "return sum + garr[0];"
      "}";

constexpr const char* kArrayInitializerExpressionSource
    = "const int a = 2, b = 3;"
      "const int c[2] = {a + b, a - b};"
      "int d[2][2] = {a + b, a - b, {a * 2, b * 2}};";

constexpr const char* kNonConstantConstArrayInitializerSource
    = "int a = 2, b = 3;"
      "const int c[2] = {a + b, a - b};";

constexpr const char* kConstFoldedParamDimensionSource = "const int N = 10;"
                                                         "int a[N][N];"
                                                         "int foo(int a[][N]) {"
                                                         "}"
                                                         "int main() {"
                                                         "foo(a);"
                                                         "return 0;"
                                                         "}";

constexpr const char* kRecursiveArrayInitializerSource
    = "const int N = 3;"
      "int a[N][N][N] = {0, 1, 2, {3}, 4};"
      "const int b[N][N][N] = {0, 1, 2, {3}, 4};";

constexpr const char* kStressGeneratedArrayInitializerSource
    = "const int N1 = 6;"
      "const int N2 = 3;"
      "const int N3 = 7;"
      "const int N4 = 5;"
      "const int N5 = 1;"
      "int a[N1][N2][N3][N4][N5] = {{}, {48, {19, 53, {{20, 4}, 55, 8}}, 38, "
      "{3, 54}}, 34, 35, 56};"
      "int main(){return 0;}";

struct SemanticArrayTest : SemanticTestBase {
    std::vector<Ptr<ast::FuncDef>> collectFunctions() const
    {
        std::vector<Ptr<ast::FuncDef>> funcs;
        for (const auto topLevelItem : root()(ast()).topLevelItems) {
            const auto funcDef_nn
                = MATCH(topLevelItem) WITH(
                    [](const Ref<ast::FuncDef>& funcDef_nn) {
                        return funcDef_nn.ptr();
                    },
                    [](const auto&) { return Ptr<ast::FuncDef> { }; }, );
            if (funcDef_nn) {
                funcs.push_back(funcDef_nn);
            }
        }
        return funcs;
    }

    void testArraySymbolsAndArrayParameterCalls()
    {
        m_output
            = analyzeSource("int take(int a[]){return a[0];} int main(){int "
                            "arr[2]; arr[1] = 7; return take(arr);}");
        require(
            success(), "array parameter call should pass semantic analysis");

        const auto takeFunc_nn = firstFuncDef();
        const auto& takeSymbol = requireSymbol(m_output,
            takeFunc_nn(ast()).funcFParams[0].identifier);
        require(takeSymbol.m_type.isArray(),
            "array parameter should preserve an array semantic type");
        require(takeSymbol.m_type.m_arrayLength == -1,
            "array parameter should preserve the unsized first dimension");

        const auto mainFunc_nn = requireFuncDefByName(root(), "main");
        const auto varDecl_nn = extractVarDecl(extractDeclNode(
            mainFunc_nn(ast()).body(ast()).items[0]));
        const auto& arraySymbol
            = requireSymbol(m_output, varDecl_nn(ast()).varDef[0]);
        require(arraySymbol.m_type.isArray(),
            "local array declaration should preserve an array semantic type");
        require(arraySymbol.m_type.m_arrayLength == 2,
            "local array declaration should preserve its constant length");

        const auto returnStmt_nn = extractReturnStmt(extractStmtNode(
            mainFunc_nn(ast()).body(ast()).items[2]));
        require(requireExpValueKind(m_output, returnStmt_nn(ast()).exp.ref())
                == ExpType::integer,
            "call through an array parameter should still produce an integer "
            "value");
    }

    void testConstArrayReadsDoNotFoldAsConstInitializers()
    {
        m_output = analyzeSource("int main(){const int arr[2] = {1, 2}; const "
                                 "int value = arr[1]; return value;}");
        require(!success(),
            "const array element reads should not participate in constant "
            "folding");
        require(firstDiagnostic().kind
                == SemanticDiagnosticKind::nonConstantConstInitializer,
            "const array element read should report the non-constant const "
            "initializer label");
    }

    void testShadowedArrayNamesResolveToBoundSymbols()
    {
        m_output = analyzeSource(kShadowedConstArraySource);
        require(success(),
            "shadowed const arrays should still pass semantic analysis");

        const auto globalConstDecl_nn = extractConstDecl(
            requireTopLevelDecl(root()(ast()).topLevelItems[0]));
        const auto& globalGarrSymbol
            = requireSymbol(m_output, globalConstDecl_nn(ast()).constDef[0]);

        const auto mainFunc_nn = requireFuncDefByName(root(), "main");
        const auto localConstDecl_nn = extractConstDecl(extractDeclNode(
            mainFunc_nn(ast()).body(ast()).items[3]));
        const auto& localGarrSymbol
            = requireSymbol(m_output, localConstDecl_nn(ast()).constDef[0]);
        require(localGarrSymbol.m_id != globalGarrSymbol.m_id,
            "shadowed const arrays should bind distinct symbol identities");

        const auto whileStmt_nn = extractWhileStmt(extractStmtNode(
            mainFunc_nn(ast()).body(ast()).items[2]));
        const auto whileBody_nn
            = extractBlockStmt(whileStmt_nn(ast()).body);
        const auto sumAssign_nn = extractAssignStmt(
            extractStmtNode(whileBody_nn(ast()).items[0]));
        const auto& sumBinary = requireBinaryExp(sumAssign_nn(ast()).exp);
        require(requireSymbol(m_output, sumBinary.rhs).m_id
                == globalGarrSymbol.m_id,
            "garr[i] inside the loop should resolve to the global const array");

        const auto returnStmt_nn = extractReturnStmt(extractStmtNode(
            mainFunc_nn(ast()).body(ast()).items[4]));
        const auto& returnBinary
            = requireBinaryExp(returnStmt_nn(ast()).exp.ref());
        require(requireSymbol(m_output, returnBinary.rhs).m_id
                == localGarrSymbol.m_id,
            "garr[0] after the local declaration should resolve to the "
            "shadowing const array");
    }

    void testConstArrayAssignmentIsRejected()
    {
        m_output = analyzeSource(
            "int main(){const int arr[2] = {1, 2}; arr[1] = 3; return 0;}");
        require(!success(),
            "assigning through a const array element should fail semantic "
            "analysis");
        require(
            firstDiagnostic().kind == SemanticDiagnosticKind::assignToConst,
            "const array assignment should report the assign-to-const semantic "
            "label");
    }

    void testNestedFunctionArrayParametersAnalyze()
    {
        m_output = analyzeSource(kFunctionArrayParamSource);
        require(success(),
            "nested function array parameters should pass semantic analysis");

        const auto funcs = collectFunctions();
        require(funcs.size() == 4,
            "function-array-parameter sample should analyze four function "
            "definitions");

        const auto& f1ArrayParam = requireSymbol(
            m_output, funcs[0](ast()).funcFParams[1].identifier);
        require(f1ArrayParam.m_type.isArray()
                && f1ArrayParam.m_type.m_arrayLength == -1,
            "f1 parameter should preserve the unsized array semantic type");
        require(f1ArrayParam.m_type.m_elementType != nullptr
                && f1ArrayParam.m_type.m_elementType->kind
                    == ast::SemanticTypeKind::integer,
            "f1 parameter element type should be integer");

        const auto& f2ArrayParam = requireSymbol(
            m_output, funcs[1](ast()).funcFParams[1].identifier);
        require(f2ArrayParam.m_type.isArray()
                && f2ArrayParam.m_type.m_arrayLength == -1,
            "f2 parameter should preserve the unsized array semantic type");
        require(f2ArrayParam.m_type.m_elementType != nullptr
                && f2ArrayParam.m_type.m_elementType->isArray()
                && f2ArrayParam.m_type.m_elementType->m_arrayLength == 10,
            "f2 parameter should preserve one trailing array extent");

        const auto& f3ArrayParam = requireSymbol(
            m_output, funcs[2](ast()).funcFParams[1].identifier);
        require(f3ArrayParam.m_type.isArray()
                && f3ArrayParam.m_type.m_arrayLength == -1,
            "f3 parameter should preserve the unsized array semantic type");
        require(f3ArrayParam.m_type.m_elementType != nullptr
                && f3ArrayParam.m_type.m_elementType->isArray()
                && f3ArrayParam.m_type.m_elementType->m_arrayLength == 10
                && f3ArrayParam.m_type.m_elementType->m_elementType != nullptr
                && f3ArrayParam.m_type.m_elementType->m_elementType->isArray()
                && f3ArrayParam.m_type.m_elementType->m_elementType
                        ->m_arrayLength
                    == 10,
            "f3 parameter should preserve both trailing array extents");
    }

    void testArrayInitializersAcceptExpressions()
    {
        m_output = analyzeSource(kArrayInitializerExpressionSource);
        require(success(),
            "array initializers should accept expression elements when the "
            "required constness is satisfied");

        const auto topLevelDecl0 = extractConstDecl(
            requireTopLevelDecl(root()(ast()).topLevelItems[0]));
        const auto topLevelDecl1 = extractConstDecl(
            requireTopLevelDecl(root()(ast()).topLevelItems[1]));
        const auto topLevelDecl2 = extractVarDecl(
            requireTopLevelDecl(root()(ast()).topLevelItems[2]));

        const auto& cSymbol
            = requireSymbol(m_output, topLevelDecl1(ast()).constDef[0]);
        require(cSymbol.m_type.isArray() && cSymbol.m_type.m_arrayLength == 2,
            "const array expression initializer should preserve the declared "
            "array type");

        const auto& dSymbol
            = requireSymbol(m_output, topLevelDecl2(ast()).varDef[0]);
        require(dSymbol.m_type.isArray() && dSymbol.m_type.m_arrayLength == 2,
            "mutable array expression initializer should preserve the outer "
            "array length");
        require(dSymbol.m_type.m_elementType != nullptr
                && dSymbol.m_type.m_elementType->isArray()
                && dSymbol.m_type.m_elementType->m_arrayLength == 2,
            "mutable array expression initializer should preserve the inner "
            "array length");

        (void)topLevelDecl0;
    }

    void testConstArrayInitializersRejectNonConstantExpressions()
    {
        m_output = analyzeSource(kNonConstantConstArrayInitializerSource);
        require(!success(),
            "const array initializers should reject non-constant expression "
            "elements");
        require(firstDiagnostic().kind
                == SemanticDiagnosticKind::nonConstantConstInitializer,
            "non-constant const array initializer should report the dedicated "
            "semantic label");
    }

    void testConstExpressionsFoldInFunctionArrayDimensions()
    {
        m_output = analyzeSource(kConstFoldedParamDimensionSource);
        require(success(),
            "const expressions in function array parameter dimensions should "
            "fold during semantic analysis");

        const auto globalVarDecl = extractVarDecl(
            requireTopLevelDecl(root()(ast()).topLevelItems[1]));
        const auto& globalArraySymbol
            = requireSymbol(m_output, globalVarDecl(ast()).varDef[0]);
        require(globalArraySymbol.m_type.isArray()
                && globalArraySymbol.m_type.m_arrayLength == 10,
            "global array declaration should preserve the folded outer "
            "dimension");
        require(globalArraySymbol.m_type.m_elementType != nullptr
                && globalArraySymbol.m_type.m_elementType->isArray()
                && globalArraySymbol.m_type.m_elementType->m_arrayLength == 10,
            "global array declaration should preserve the folded inner "
            "dimension");

        const auto fooFunc_nn = requireFuncDefByName(root(), "foo");
        const auto& paramSymbol = requireSymbol(m_output,
            fooFunc_nn(ast()).funcFParams[0].identifier);
        require(paramSymbol.m_type.isArray()
                && paramSymbol.m_type.m_arrayLength == -1,
            "function parameter should preserve the unsized first dimension");
        require(paramSymbol.m_type.m_elementType != nullptr
                && paramSymbol.m_type.m_elementType->isArray()
                && paramSymbol.m_type.m_elementType->m_arrayLength == 10,
            "function parameter trailing dimension should fold the const "
            "expression to 10");
    }

    void testRecursiveMultidimensionalArrayInitializersAnalyze()
    {
        m_output = analyzeSource(kRecursiveArrayInitializerSource);
        require(success(),
            "recursive multidimensional array initializers should pass "
            "semantic analysis");

        const auto aDecl = extractVarDecl(
            requireTopLevelDecl(root()(ast()).topLevelItems[1]));
        const auto bDecl = extractConstDecl(
            requireTopLevelDecl(root()(ast()).topLevelItems[2]));

        const auto& aSymbol
            = requireSymbol(m_output, aDecl(ast()).varDef[0]);
        require(aSymbol.m_type.isArray() && aSymbol.m_type.m_arrayLength == 3,
            "mutable three-dimensional array initializer should preserve outer "
            "length");
        require(aSymbol.m_type.m_elementType != nullptr
                && aSymbol.m_type.m_elementType->isArray()
                && aSymbol.m_type.m_elementType->m_arrayLength == 3
                && aSymbol.m_type.m_elementType->m_elementType != nullptr
                && aSymbol.m_type.m_elementType->m_elementType->isArray()
                && aSymbol.m_type.m_elementType->m_elementType->m_arrayLength
                    == 3,
            "mutable three-dimensional array initializer should preserve inner "
            "lengths");

        const auto& bSymbol
            = requireSymbol(m_output, bDecl(ast()).constDef[0]);
        require(bSymbol.m_type == aSymbol.m_type,
            "const three-dimensional array initializer should preserve the "
            "same folded type");
    }

    void testStressGeneratedArrayInitializerAnalyzes()
    {
        m_output = analyzeSource(kStressGeneratedArrayInitializerSource);
        require(success(),
            "stress-generated deep array initializer should pass semantic "
            "analysis");
    }
};

} // namespace

int main()
{
    SemanticArrayTest test;
    test.testArraySymbolsAndArrayParameterCalls();
    test.testConstArrayReadsDoNotFoldAsConstInitializers();
    test.testShadowedArrayNamesResolveToBoundSymbols();
    test.testConstArrayAssignmentIsRejected();
    test.testNestedFunctionArrayParametersAnalyze();
    test.testArrayInitializersAcceptExpressions();
    test.testConstArrayInitializersRejectNonConstantExpressions();
    test.testConstExpressionsFoldInFunctionArrayDimensions();
    test.testRecursiveMultidimensionalArrayInitializersAnalyze();
    test.testStressGeneratedArrayInitializerAnalyzes();
    return 0;
}