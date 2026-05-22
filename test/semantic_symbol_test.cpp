#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

struct SemanticSymbolTest : SemanticTestBase {
    void testDefinitionAndUsesShareOneSymbolBinding()
    {
        m_output = analyzeSource(
            "int main(){int value = 1; value = 2; value; return value;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto& blockItems = funcDef_nn(ast()).m_block_nn(ast()).m_blockItems;
        const auto varDecl_nn = extractVarDecl(extractDeclNode(blockItems[0]));
        const auto assignStmt_nn = extractAssignStmt(extractStmtNode(blockItems[1]));
        const auto expStmt_nn = extractExpStmt(extractStmtNode(blockItems[2]));
        const auto returnStmt_nn = extractReturnStmt(extractStmtNode(blockItems[3]));

        const auto& defSymbol = requireSymbol(m_output, varDecl_nn(ast()).m_varDefs[0]);
        require(requireSymbol(m_output, assignStmt_nn(ast()).m_lVal_nn).m_id
                == defSymbol.m_id,
            "assignment lvalue should resolve to the declaration symbol");
        require(requireSymbol(m_output, expStmt_nn(ast()).m_exp_nn).m_id
                == defSymbol.m_id,
            "expression statement use should resolve to the declaration symbol");
        require(requireSymbol(m_output, returnStmt_nn(ast()).m_exp_nn).m_id
                == defSymbol.m_id,
            "return expression should resolve to the declaration symbol");
    }

    void testNestedScopePrefersInnermostDefinition()
    {
        m_output = analyzeSource(
            "int main(){int value = 1; {int value = 2; value; return value;} return value;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto& outerItems = funcDef_nn(ast()).m_block_nn(ast()).m_blockItems;
        const auto outerDecl_nn = extractVarDecl(extractDeclNode(outerItems[0]));
        const auto nestedBlock_nn = extractBlockStmt(extractStmtNode(outerItems[1]));
        const auto outerReturn_nn = extractReturnStmt(extractStmtNode(outerItems[2]));

        const auto innerDecl_nn = extractVarDecl(
            extractDeclNode(nestedBlock_nn(ast()).m_blockItems[0]));
        const auto innerExpStmt_nn = extractExpStmt(
            extractStmtNode(nestedBlock_nn(ast()).m_blockItems[1]));
        const auto innerReturn_nn = extractReturnStmt(
            extractStmtNode(nestedBlock_nn(ast()).m_blockItems[2]));

        const auto& outerSymbol = requireSymbol(m_output, outerDecl_nn(ast()).m_varDefs[0]);
        const auto& innerSymbol = requireSymbol(m_output, innerDecl_nn(ast()).m_varDefs[0]);
        require(innerSymbol.m_id != outerSymbol.m_id,
            "shadowing declarations should create distinct symbol identities");

        require(requireSymbol(m_output, innerExpStmt_nn(ast()).m_exp_nn).m_id
                == innerSymbol.m_id,
            "nested scope lookup should prefer the innermost definition");
        require(requireSymbol(m_output, innerReturn_nn(ast()).m_exp_nn).m_id
                == innerSymbol.m_id,
            "inner return should resolve to the innermost definition");
        require(requireSymbol(m_output, outerReturn_nn(ast()).m_exp_nn).m_id
                == outerSymbol.m_id,
            "outer return should resolve to the outer definition");
    }

    void testUseBeforeDefinitionDiagnostic()
    {
        m_output = analyzeSource("int main(){value = 1; return 0;}");
        require(!success(),
            "use-before-definition should fail semantic analysis");
        require(firstDiagnostic().m_kind
                == SemanticDiagnosticKind::useBeforeDefinition,
            "use-before-definition should report the expected semantic label");
    }

    void testDoubleDefinitionDiagnostic()
    {
        m_output = analyzeSource("int main(){int value; int value; return 0;}");
        require(!success(), "double definition should fail semantic analysis");
        require(firstDiagnostic().m_kind
                == SemanticDiagnosticKind::doubleDefinition,
            "double definition should report the expected semantic label");
    }
};

} // namespace

int main()
{
    SemanticSymbolTest test;
    test.testDefinitionAndUsesShareOneSymbolBinding();
    test.testNestedScopePrefersInnermostDefinition();
    test.testUseBeforeDefinitionDiagnostic();
    test.testDoubleDefinitionDiagnostic();
    return 0;
}
