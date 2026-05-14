#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

std::shared_ptr<ast::StmtNode> requireStmtNode(
    const std::shared_ptr<ast::BlockItemNode>& blockItem_nn)
{
    std::shared_ptr<ast::StmtNode> stmtNode_nn;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                stmtNode_nn = blockItemAlt;
            }
        },
        blockItem_nn->m_blockItem);
    require(stmtNode_nn != nullptr, "expected statement block item");
    return stmtNode_nn;
}

std::shared_ptr<ast::IfStmt> requireIfStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::IfStmt> ifStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::IfStmt>>) {
                ifStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(ifStmt_nn != nullptr, "expected if statement");
    return ifStmt_nn;
}

const ast::LVal& requireSimpleLValExp(const ast::Exp& exp)
{
    const auto& lOrExp = *exp.m_lOrExp_nn;
    require(lOrExp.m_tail.empty(), "expected simple logical-or expression");
    const auto& lAndExp = *lOrExp.m_head_nn;
    require(lAndExp.m_tail.empty(), "expected simple logical-and expression");
    const auto& eqExp = *lAndExp.m_head_nn;
    require(eqExp.m_tail.empty(), "expected simple equality expression");
    const auto& relExp = *eqExp.m_head_nn;
    require(relExp.m_tail.empty(), "expected simple relational expression");
    const auto& addExp = *relExp.m_head_nn;
    require(addExp.m_tail.empty(), "expected simple additive expression");
    const auto& mulExp = *addExp.m_head_nn;
    require(mulExp.m_tail.empty(), "expected simple multiplicative expression");
    const auto& unaryExp = *mulExp.m_head_nn;

    const ast::PrimaryExp* primaryExp_nn = nullptr;
    std::visit(
        [&](const auto& unaryAlt) {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::PrimaryExp>>) {
                primaryExp_nn = unaryAlt.get();
            }
        },
        unaryExp.m_kind);
    require(primaryExp_nn != nullptr, "expected simple primary expression");

    const ast::LVal* lVal_nn = nullptr;
    std::visit(
        [&](const auto& primaryAlt) {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::LVal>>) {
                lVal_nn = primaryAlt.get();
            }
        },
        primaryExp_nn->m_kind);
    require(lVal_nn != nullptr, "expected lvalue expression");
    return *lVal_nn;
}

const ast::AddExp& requireConditionAddExp(const ast::IfStmt& ifStmt)
{
    const auto& lOrExp = *ifStmt.m_condExp_nn->m_lOrExp_nn;
    const auto& lAndExp = *lOrExp.m_head_nn;
    const auto& eqExp = *lAndExp.m_head_nn;
    const auto& relExp = *eqExp.m_head_nn;
    return *relExp.m_head_nn;
}

const ast::LOrExp& requireNestedParenthesizedLOrExp(const ast::AddExp& addExp)
{
    require(!addExp.m_tail.empty(), "expected additive tail");
    const auto& tailMulExp = *addExp.m_tail[0].second;
    const auto& tailUnaryExp = *tailMulExp.m_head_nn;

    const ast::PrimaryExp* primaryExp_nn = nullptr;
    std::visit(
        [&](const auto& unaryAlt) {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::PrimaryExp>>) {
                primaryExp_nn = unaryAlt.get();
            }
        },
        tailUnaryExp.m_kind);
    require(primaryExp_nn != nullptr, "expected parenthesized primary expression");

    const ast::Exp* nestedExp_nn = nullptr;
    std::visit(
        [&](const auto& primaryAlt) {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::Exp>>) {
                nestedExp_nn = primaryAlt.get();
            }
        },
        primaryExp_nn->m_kind);
    require(nestedExp_nn != nullptr, "expected parenthesized nested expression");
    return *nestedExp_nn->m_lOrExp_nn;
}

void testIfConditionMarksPlainValueAsBooleanContext()
{
    const auto output = analyzeSource(
        "int main(){int a; if (a) return 1; return 0;}");
    require(output.success(), "expected semantic success");

    const auto ifStmt_nn = requireIfStmt(requireStmtNode(
        output.m_root->m_funcDef_nn->m_block_nn->m_blockItems[1]));
    require(requireExpValueKind(output, *ifStmt_nn->m_condExp_nn)
            == SemanticExpValueKind::boolean,
        "if conditions should be recorded as boolean expressions");
    require(requireExpValueKind(output, requireSimpleLValExp(*ifStmt_nn->m_condExp_nn))
            == SemanticExpValueKind::arithmetic,
        "the referenced variable should remain an arithmetic value");
}

void testMixedArithmeticAndLogicalSubexpressionsKeepDistinctKinds()
{
    const auto output = analyzeSource(
        "int main(){int a; int b; int c; if (a + (b || c)) return 1; return 0;}");
    require(output.success(), "expected semantic success");

    const auto ifStmt_nn = requireIfStmt(requireStmtNode(
        output.m_root->m_funcDef_nn->m_block_nn->m_blockItems[3]));
    const auto& outerAddExp = requireConditionAddExp(*ifStmt_nn);
    const auto& nestedLOrExp = requireNestedParenthesizedLOrExp(outerAddExp);

    require(requireExpValueKind(output, *ifStmt_nn->m_condExp_nn)
            == SemanticExpValueKind::boolean,
        "the full if condition should be classified as boolean");
    require(requireExpValueKind(output, outerAddExp)
            == SemanticExpValueKind::arithmetic,
        "additive expressions should stay arithmetic even inside conditions");
    require(requireExpValueKind(output, nestedLOrExp)
            == SemanticExpValueKind::boolean,
        "logical-or subexpressions should be classified as boolean");
}

void testLogicalConditionRecordsFoldedBooleanConstant()
{
    const auto output = analyzeSource(
        "int main(){if (1 || 0) return 1; return 0;}");
    require(output.success(), "expected semantic success");

    const auto ifStmt_nn = requireIfStmt(requireStmtNode(
        output.m_root->m_funcDef_nn->m_block_nn->m_blockItems[0]));
    require(requireConstantValue(output, *ifStmt_nn->m_condExp_nn) == 1,
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
