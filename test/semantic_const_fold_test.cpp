#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

struct SemanticConstFoldTest : SemanticTestBase {
    void testConstExpressionsRecordFoldedValues()
    {
        m_output = analyzeSource(
            "int main(){const int answer = 40 + 2; int value = answer + 1 * 2; return answer + 1 * 2;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto& blockItems = funcDef_nn(ast()).body(ast()).items;
        const auto constDecl_nn = extractConstDecl(extractDeclNode(blockItems[0]));
        const auto varDecl_nn = extractVarDecl(extractDeclNode(blockItems[1]));
        const auto returnStmt_nn = extractReturnStmt(extractStmtNode(blockItems[2]));

        const auto constExp = requireScalarConstInitExp(
            constDecl_nn(ast()).constDef[0](ast()).constInitVal);
        require(requireConstantValue(m_output, constExp) == 42,
            "const initializer should fold to a constant value");

        const auto& constSymbol = requireSymbol(
            m_output, constDecl_nn(ast()).constDef[0]);
        require(constSymbol.m_hasConstantValue,
            "const declaration should cache its folded constant value");
        require(constSymbol.m_constantValue == 42,
            "const declaration should expose the folded constant value");

        const auto varInitExp = requireScalarInitExp(
            varDecl_nn(ast()).varDef[0](ast()).initVal);
        require(requireConstantValue(m_output, varInitExp) == 44,
            "var initializer should fold const-backed integer");

        require(requireConstantValue(m_output, returnStmt_nn(ast()).m_exp_nn.ref())
                == 44,
            "return expression should fold const-backed integer");
    }

    void testConstSemanticDiagnostics()
    {
        m_output = analyzeSource(
            "int main(){int value = 1; const int answer = value; return answer;}");
        require(!success(),
            "non-constant const initializer should fail semantic analysis");
        require(firstDiagnostic().kind
                == SemanticDiagnosticKind::nonConstantConstInitializer,
            "non-constant const initializer should report the expected semantic label");

        m_output = analyzeSource(
            "int main(){const int answer = 1; answer = 2; return answer;}");
        require(!success(),
            "assignment to const should fail semantic analysis");
        require(firstDiagnostic().kind == SemanticDiagnosticKind::assignToConst,
            "assignment to const should report the expected semantic label");
    }
};

} // namespace

int main()
{
    SemanticConstFoldTest test;
    test.testConstExpressionsRecordFoldedValues();
    test.testConstSemanticDiagnostics();
    return 0;
}
