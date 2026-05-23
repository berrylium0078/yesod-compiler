#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

struct SemanticIfTest : SemanticTestBase {
    void testIfConditionMarksPlainValueAsBooleanContext()
    {
        m_output = analyzeSource(
            "int main(){int a; if (a) return 1; return 0;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto ifStmt_nn = extractIfStmt(
            funcDef_nn(ast()).body(ast()).items[1]);
        require(requireExpValueKind(m_output, ifStmt_nn(ast()).condition)
                == ExpType::boolean,
            "if conditions should be recorded as boolean expressions");
        require(requireSymbol(m_output, ifStmt_nn(ast()).condition).name
                == "a",
            "plain lvalue conditions should still preserve their symbol binding");
    }

    void testMixedArithmeticAndLogicalSubexpressionsKeepDistinctKinds()
    {
        m_output = analyzeSource("int main(){int a; int b; int c; if (a + "
                                 "(b || c)) return 1; return 0;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto ifStmt_nn = extractIfStmt(
            funcDef_nn(ast()).body(ast()).items[3]);
        const auto& outerBinaryExp = requireBinaryExp(
            ifStmt_nn(ast()).condition);
        const auto& nestedBinaryExp = requireBinaryExp(outerBinaryExp.rhs);

        require(requireExpValueKind(m_output, ifStmt_nn(ast()).condition)
                == ExpType::boolean,
            "the full if condition should be classified as boolean");
        require(outerBinaryExp.op == ast::BinaryOpKeyword::plus,
            "the outer condition should stay an additive binary expression");
        require(requireExpValueKind(m_output, ifStmt_nn(ast()).condition)
                == ExpType::boolean,
            "the condition root should be classified as boolean even if its "
            "operator remains additive");
        require(nestedBinaryExp.op == ast::BinaryOpKeyword::orOr,
            "the grouped rhs subexpression should remain logical-or");
        require(requireExpValueKind(m_output, outerBinaryExp.rhs)
                == ExpType::integer,
            "logical-or subexpressions used as additive operands should be "
            "normalized to integer");
    }

    void testLogicalConditionRecordsFoldedBooleanConstant()
    {
        m_output = analyzeSource(
            "int main(){if (1 || 0) return 1; return 0;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto ifStmt_nn = extractIfStmt(
            funcDef_nn(ast()).body(ast()).items[0]);
        require(requireConstantValue(m_output, ifStmt_nn(ast()).condition)
                == 1,
            "constant logical conditions should record their folded truth "
            "value");
    }
};

} // namespace

int main()
{
    SemanticIfTest test;
    test.testIfConditionMarksPlainValueAsBooleanContext();
    test.testMixedArithmeticAndLogicalSubexpressionsKeepDistinctKinds();
    test.testLogicalConditionRecordsFoldedBooleanConstant();
    return 0;
}
