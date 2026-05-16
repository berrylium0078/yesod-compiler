#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

ast::Handle<ast::StmtNode> requireStmtNode(
    const ast::Handle<ast::BlockItemNode>& blockItem_nn)
{
    ast::Handle<ast::StmtNode> stmtNode_nn;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::StmtNode>>) {
                stmtNode_nn = blockItemAlt;
            }
        },
        blockItem_nn->m_blockItem);
    require(stmtNode_nn != nullptr, "expected statement block item");
    return stmtNode_nn;
}

ast::Handle<ast::IfStmt> requireIfStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::IfStmt> ifStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::IfStmt>>) {
                ifStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(ifStmt_nn != nullptr, "expected if statement");
    return ifStmt_nn;
}

ast::Handle<ast::Exp> requireSimpleLValExp(const ast::Handle<ast::Exp>& exp_nn)
{
    require(exp_nn != nullptr, "expected expression node");
    require(std::holds_alternative<ast::LVal>(exp_nn->m_kind),
        "expected lvalue expression");
    return exp_nn;
}

ast::Handle<ast::Exp> requireBinaryExp(const ast::Handle<ast::Exp>& exp_nn)
{
    require(exp_nn != nullptr, "expected expression node");
    require(std::holds_alternative<ast::Exp::Binary>(exp_nn->m_kind),
        "expected binary expression");
    return exp_nn;
}

const ast::Exp::Binary& requireBinaryPayload(
    const ast::Handle<ast::Exp>& exp_nn)
{
    return std::get<ast::Exp::Binary>(exp_nn->m_kind);
}

void testIfConditionMarksPlainValueAsBooleanContext()
{
    const auto output
        = analyzeSource("int main(){int a; if (a) return 1; return 0;}");
    require(output.success(), "expected semantic success");

    const auto ifStmt_nn = requireIfStmt(requireStmtNode(
        output.m_root->m_funcDef_nn->m_block_nn->m_blockItems[1]));
    require(requireExpValueKind(output, ifStmt_nn->m_condExp_nn)
            == ExpType::boolean,
        "if conditions should be recorded as boolean expressions");
    require(requireSymbol(output, requireSimpleLValExp(ifStmt_nn->m_condExp_nn))
                .m_name
            == "a",
        "plain lvalue conditions should still preserve their symbol binding");
}

void testMixedArithmeticAndLogicalSubexpressionsKeepDistinctKinds()
{
    const auto output = analyzeSource("int main(){int a; int b; int c; if (a + "
                                      "(b || c)) return 1; return 0;}");
    require(output.success(), "expected semantic success");

    const auto ifStmt_nn = requireIfStmt(requireStmtNode(
        output.m_root->m_funcDef_nn->m_block_nn->m_blockItems[3]));
    const auto outerBinaryExp_nn = requireBinaryExp(ifStmt_nn->m_condExp_nn);
    const auto& outerBinaryExp = requireBinaryPayload(outerBinaryExp_nn);
    const auto nestedBinaryExp_nn = requireBinaryExp(outerBinaryExp.m_rhs_nn);
    const auto& nestedBinaryExp = requireBinaryPayload(nestedBinaryExp_nn);

    require(requireExpValueKind(output, ifStmt_nn->m_condExp_nn)
            == ExpType::boolean,
        "the full if condition should be classified as boolean");
    require(outerBinaryExp.m_op == ast::BinaryOpKeyword::plus,
        "the outer condition should stay an additive binary expression");
    require(requireExpValueKind(output, outerBinaryExp_nn) == ExpType::boolean,
        "the condition root should be classified as boolean even if its "
        "operator remains additive");
    require(nestedBinaryExp.m_op == ast::BinaryOpKeyword::orOr,
        "the grouped rhs subexpression should remain logical-or");
    require(requireExpValueKind(output, nestedBinaryExp_nn) == ExpType::integer,
        "logical-or subexpressions used as additive operands should be "
        "normalized to integer");
}

void testLogicalConditionRecordsFoldedBooleanConstant()
{
    const auto output
        = analyzeSource("int main(){if (1 || 0) return 1; return 0;}");
    require(output.success(), "expected semantic success");

    const auto ifStmt_nn = requireIfStmt(requireStmtNode(
        output.m_root->m_funcDef_nn->m_block_nn->m_blockItems[0]));
    require(requireConstantValue(output, ifStmt_nn->m_condExp_nn) == 1,
        "constant logical conditions should record their folded truth value");
}

} // namespace

int main()
{
    testIfConditionMarksPlainValueAsBooleanContext();
    testMixedArithmeticAndLogicalSubexpressionsKeepDistinctKinds();
    testLogicalConditionRecordsFoldedBooleanConstant();
    return 0;
}
