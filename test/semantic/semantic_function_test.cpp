#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

struct SemanticFunctionTest : SemanticTestBase {
    void testGlobalsAndFunctionCallsAnalyze()
    {
        m_output = analyzeRoot(
            "const int seed = 4; int counter = 2; int add(int lhs, int rhs)"
            "{return lhs + rhs;} int main(){counter = counter + 1; return "
            "add(seed, counter);}");

        const auto mainFunc_nn = requireFuncDefByName(root(), "main");
        const auto returnStmt_nn
            = extractReturnStmt(mainFunc_nn(ast()).body(ast()).items[1]);
        const auto& callExp = requireCallExp(returnStmt_nn(ast()).exp.ref());

        require(callExp.params.size() == 2,
            "call expression should preserve both arguments");
        require(requireSymbol(m_output, callExp.funcName).isFunction(),
            "call callee should bind to a function symbol");
        require(requireExpValueKind(m_output, returnStmt_nn(ast()).exp.ref())
                == ExpType::integer,
            "int-returning call should produce an integer expression type");
        require(requireConstantValue(m_output, callExp.params[0]) == 4,
            "global const arguments should preserve their folded constant "
            "value");
        require(requireSymbol(m_output, callExp.params[1]).name == "counter",
            "global variable arguments should resolve through global scope");
    }

    void testVoidFunctionCallExpressionStatementAnalyzes()
    {
        m_output
            = analyzeRoot("void noop(){return;} int main(){noop(); return 0;}");

        const auto mainFunc_nn = requireFuncDefByName(root(), "main");
        const auto expStmt_nn
            = extractExpStmt(mainFunc_nn(ast()).body(ast()).items[0]);
        require(requireExpValueKind(m_output, expStmt_nn(ast()).exp.ref())
                == ExpType::voidType,
            "void-returning calls should preserve a void expression type");
    }

    void testCallArityMismatchDiagnostic()
    {
        m_output = analyzeSource(
            "int add(int lhs){return lhs;} int main(){return add(1, 2);}");
        require(!success(), "wrong-arity call should fail semantic analysis");
        require(
            isDiagnostic<CallArityMismatchDiagnostic>(firstDiagnostic()),
            "wrong-arity call should report the expected semantic label");
    }

    void testInvalidCallTargetDiagnostic()
    {
        m_output = analyzeSource("int value = 1; int main(){return value();}");
        require(!success(), "calling a variable should fail semantic analysis");
        require(
            isDiagnostic<InvalidCallTargetDiagnostic>(firstDiagnostic()),
            "calling a variable should report an invalid-call-target "
            "diagnostic");
    }

    void testNonConstantGlobalInitializerDiagnostic()
    {
        m_output = analyzeSource("int add(int lhs){return lhs;} int value = "
                                 "add(1); int main(){return value;}");
        require(!success(),
            "non-constant global initializer should fail semantic analysis");
        require(isDiagnostic<NonConstantGlobalInitializerDiagnostic>(
                    firstDiagnostic()),
            "non-constant global initializer should report the expected "
            "semantic label");
    }

    void testReturnTypeMismatchDiagnostics()
    {
        m_output
            = analyzeSource("void noop(){return 1;} int main(){return 0;}");
        require(!success(),
            "void function returning a value should fail semantic analysis");
        require(isDiagnostic<ReturnTypeMismatchDiagnostic>(firstDiagnostic()),
            "void function returning a value should report return type "
            "mismatch");

        m_output = analyzeSource("int main(){return;}\n");
        require(!success(),
            "int function returning no value should fail semantic analysis");
        require(isDiagnostic<ReturnTypeMismatchDiagnostic>(firstDiagnostic()),
            "missing int return value should report return type mismatch");
    }

    void testCompatibleFunctionRedeclarationsAnalyze()
    {
        m_output = analyzeRoot(
            "int add(int lhs, int rhs); int add(int lhs, int rhs); int add(int "
            "lhs, int rhs){return lhs + rhs;} int main(){return add(1, 2);}");

        const auto mainFunc_nn = requireFuncDefByName(root(), "main");
        const auto returnStmt_nn
            = extractReturnStmt(mainFunc_nn(ast()).body(ast()).items.front());
        const auto& callExp = requireCallExp(returnStmt_nn(ast()).exp.ref());

        require(requireSymbol(m_output, callExp.funcName).isFunction(),
            "compatible redeclarations should still bind calls to the shared "
            "function symbol");
    }

    void testConflictingFunctionRedeclarationDiagnostic()
    {
        m_output = analyzeSource("int add(int lhs); int add(int lhs, int rhs); "
                                 "int main(){return 0;}");
        require(!success(),
            "conflicting function declarations should fail semantic analysis");
        require(
            isDiagnostic<DoubleDefinitionDiagnostic>(firstDiagnostic()),
            "conflicting function declarations should report a "
            "double-definition diagnostic");
    }

    void testMultipleFunctionDefinitionsDiagnostic()
    {
        m_output = analyzeSource("int add(){return 1;} int add(){return 2;} "
                                 "int main(){return add();}");
        require(!success(),
            "multiple function definitions should fail semantic analysis");
        require(
            isDiagnostic<DoubleDefinitionDiagnostic>(firstDiagnostic()),
            "multiple function definitions should report a double-definition "
            "diagnostic");
    }
};

} // namespace

int main()
{
    SemanticFunctionTest test;
    test.testGlobalsAndFunctionCallsAnalyze();
    test.testVoidFunctionCallExpressionStatementAnalyzes();
    test.testCallArityMismatchDiagnostic();
    test.testInvalidCallTargetDiagnostic();
    test.testNonConstantGlobalInitializerDiagnostic();
    test.testReturnTypeMismatchDiagnostics();
    test.testCompatibleFunctionRedeclarationsAnalyze();
    test.testConflictingFunctionRedeclarationDiagnostic();
    test.testMultipleFunctionDefinitionsDiagnostic();
    return 0;
}
