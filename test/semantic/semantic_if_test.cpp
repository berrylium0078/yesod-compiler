#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

struct SemanticIfTest : SemanticTestBase {
    void testIfConditionMarksPlainValueAsBooleanContext()
    {
        m_output
            = analyzeSource("int main(){int a; if (a) return 1; return 0;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto ifStmt_nn
            = extractIfStmt(funcDef_nn(ast()).body(ast()).items[1]);
        require(requireExpValueKind(m_output, ifStmt_nn(ast()).condition)
                == ExpType::boolean,
            "if conditions should be recorded as boolean expressions");
        require(requireSymbol(m_output, ifStmt_nn(ast()).condition).name == "a",
            "plain lvalue conditions should still preserve their symbol "
            "binding");
    }

    void testMixedArithmeticAndLogicalSubexpressionsKeepDistinctKinds()
    {
        m_output = analyzeSource("int main(){int a; int b; int c; if (a + "
                                 "(b || c)) return 1; return 0;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto ifStmt_nn
            = extractIfStmt(funcDef_nn(ast()).body(ast()).items[3]);
        const auto& outerBinaryExp
            = requireBinaryExp(ifStmt_nn(ast()).condition);
        const auto& nestedBinaryExp = requireBinaryExp(outerBinaryExp.rhs);

        require(requireExpValueKind(m_output, ifStmt_nn(ast()).condition)
                == ExpType::boolean,
            "the full if condition should be classified as boolean");
        require(outerBinaryExp.op == BinaryOpKeyword::plus,
            "the outer condition should stay an additive binary expression");
        require(requireExpValueKind(m_output, ifStmt_nn(ast()).condition)
                == ExpType::boolean,
            "the condition root should be classified as boolean even if its "
            "operator remains additive");
        require(nestedBinaryExp.op == BinaryOpKeyword::orOr,
            "the grouped rhs subexpression should remain logical-or");
        require(requireExpValueKind(m_output, outerBinaryExp.rhs)
                == ExpType::integer,
            "logical-or subexpressions used as additive operands should be "
            "normalized to integer");
    }

    void testLogicalConditionRecordsFoldedBooleanConstant()
    {
        m_output = analyzeSource("int main(){if (1 || 0) return 1; return 0;}");
        require(success(), "expected semantic success");

        const auto funcDef_nn = firstFuncDef();
        const auto ifStmt_nn
            = extractIfStmt(funcDef_nn(ast()).body(ast()).items[0]);
        require(requireConstantValue(m_output, ifStmt_nn(ast()).condition) == 1,
            "constant logical conditions should record their folded truth "
            "value");
    }

    void testIfElseBuildsSemanticControlFlowBlocks()
    {
        m_output
            = analyzeRoot("int main(){int a; if (a) return 1; else return 0;}");

        const auto funcDef_nn = firstFuncDef();
        const auto& controlFlow = requireControlFlow(m_output, funcDef_nn);
        // Both branches return directly, so the join block becomes unreachable
        // after CFG simplification and is removed. The synthesized end block is
        // still preserved as the default return guard.
        require(controlFlow.blocks.size() == 4,
            "if-else semantic CFG should include entry, then, else, and end "
            "blocks");

        const auto& entryBlock
            = requireControlFlowBlock(m_output, controlFlow.entryBlock);
        const auto& thenBlock
            = requireControlFlowBlock(m_output, controlFlow.blocks[1]);
        const auto& elseBlock
            = requireControlFlowBlock(m_output, controlFlow.blocks[2]);
        const auto& endBlock
            = requireControlFlowBlock(m_output, controlFlow.endBlock);

        const auto& entryTerminator = requireBranchTerminator(entryBlock);
        require(entryTerminator.trueTarget == controlFlow.blocks[1],
            "if entry block should branch to the then block on true");
        require(entryTerminator.falseTarget == controlFlow.blocks[2],
            "if entry block should branch to the else block on false");
        require(requireReturnTerminator(thenBlock).value.has_value(),
            "then block should terminate with a return");
        require(requireReturnTerminator(elseBlock).value.has_value(),
            "else block should terminate with a return");
        require(!requireReturnTerminator(endBlock).value.has_value()
                || std::holds_alternative<int32_t>(
                    requireReturnTerminator(endBlock).value->kind),
            "synthesized end block should carry the default return value");
    }
};

} // namespace

int main()
{
    SemanticIfTest test;
    test.testIfConditionMarksPlainValueAsBooleanContext();
    test.testMixedArithmeticAndLogicalSubexpressionsKeepDistinctKinds();
    test.testLogicalConditionRecordsFoldedBooleanConstant();
    test.testIfElseBuildsSemanticControlFlowBlocks();
    return 0;
}
