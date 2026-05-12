#include "semantic_test_support.h"

#include <type_traits>

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

std::shared_ptr<ast::VarDecl> requireVarDecl(
    const std::shared_ptr<ast::BlockItemNode>& blockItem_nn)
{
    std::shared_ptr<ast::DeclNode> declNode_nn;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                declNode_nn = blockItemAlt;
            }
        },
        blockItem_nn->m_blockItem);
    require(declNode_nn != nullptr, "expected declaration block item");

    std::shared_ptr<ast::VarDecl> varDecl_nn;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::VarDecl>>) {
                varDecl_nn = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(varDecl_nn != nullptr, "expected var declaration");
    return varDecl_nn;
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

std::shared_ptr<ast::Number> requireNumberExp(
    const std::shared_ptr<ast::Exp>& exp_nn)
{
    std::shared_ptr<ast::Number> number_nn;
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::Number>>) {
                number_nn = expAlt;
            }
        },
        exp_nn->m_kind);
    require(number_nn != nullptr, "expected number semantic expression");
    return number_nn;
}

std::shared_ptr<ast::LVal> requireLValExp(
    const std::shared_ptr<ast::Exp>& exp_nn)
{
    std::shared_ptr<ast::LVal> lVal_nn;
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::LVal>>) {
                lVal_nn = expAlt;
            }
        },
        exp_nn->m_kind);
    require(lVal_nn != nullptr, "expected lvalue semantic expression");
    return lVal_nn;
}

std::shared_ptr<ast::BinaryExp> requireBinaryExp(
    const std::shared_ptr<ast::Exp>& exp_nn)
{
    std::shared_ptr<ast::BinaryExp> binaryExp_nn;
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::BinaryExp>>) {
                binaryExp_nn = expAlt;
            }
        },
        exp_nn->m_kind);
    require(binaryExp_nn != nullptr, "expected binary semantic expression");
    return binaryExp_nn;
}

std::shared_ptr<ast::IntToBoolExp> requireIntToBoolExp(
    const std::shared_ptr<ast::Exp>& exp_nn)
{
    std::shared_ptr<ast::IntToBoolExp> conversion_nn;
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ast::IntToBoolExp>>) {
                conversion_nn = expAlt;
            }
        },
        exp_nn->m_kind);
    require(conversion_nn != nullptr, "expected int-to-bool semantic expression");
    return conversion_nn;
}

std::shared_ptr<ast::BoolToIntExp> requireBoolToIntExp(
    const std::shared_ptr<ast::Exp>& exp_nn)
{
    std::shared_ptr<ast::BoolToIntExp> conversion_nn;
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ast::BoolToIntExp>>) {
                conversion_nn = expAlt;
            }
        },
        exp_nn->m_kind);
    require(conversion_nn != nullptr, "expected bool-to-int semantic expression");
    return conversion_nn;
}

void requireAddOp(const ast::BinaryExp& binaryExp, yesod::frontend::AddOpKeyword expectedOp)
{
    bool matched = false;
    std::visit(
        [&](const auto& op) {
            using AltType = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<AltType, yesod::frontend::AddOpKeyword>) {
                matched = op == expectedOp;
            }
        },
        binaryExp.m_op);
    require(matched, "expected additive semantic operator");
}

void requireLogicalOrOp(const ast::BinaryExp& binaryExp)
{
    bool matched = false;
    std::visit(
        [&](const auto& op) {
            using AltType = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<AltType, yesod::frontend::LOrOpKeyword>) {
                matched = true;
            }
        },
        binaryExp.m_op);
    require(matched, "expected logical-or semantic operator");
}

void requireLogicalAndOp(const ast::BinaryExp& binaryExp)
{
    bool matched = false;
    std::visit(
        [&](const auto& op) {
            using AltType = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<AltType, yesod::frontend::LAndOpKeyword>) {
                matched = true;
            }
        },
        binaryExp.m_op);
    require(matched, "expected logical-and semantic operator");
}

void testIfConditionNormalizesPlainValueToBoolean()
{
    const auto root_nn = analyzeRoot(
        "int main(){int a; if (a) return 1; return 0;}");
    const auto aSymbol_nn = requireVarDecl(
        root_nn->m_funcDef_nn->m_block_nn->m_blockItems[0])
                                ->m_varDefs[0]
                                ->m_symbol_nn;
    const auto ifStmt_nn = requireIfStmt(requireStmtNode(
        root_nn->m_funcDef_nn->m_block_nn->m_blockItems[1]));

    const auto cond_nn = requireIntToBoolExp(ifStmt_nn->m_condExp_nn);
    require(requireLValExp(cond_nn->m_operand_nn)->m_symbol_nn == aSymbol_nn,
        "if conditions should normalize plain arithmetic values to boolean form");
}

void testMixedArithmeticAndLogicalConditionNormalizesBothWays()
{
    const auto root_nn = analyzeRoot(
        "int main(){int a; int b; int c; if (a + (b || c)) return 1; return 0;}");
    const auto& blockItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;
    const auto aSymbol_nn = requireVarDecl(blockItems[0])->m_varDefs[0]->m_symbol_nn;
    const auto bSymbol_nn = requireVarDecl(blockItems[1])->m_varDefs[0]->m_symbol_nn;
    const auto cSymbol_nn = requireVarDecl(blockItems[2])->m_varDefs[0]->m_symbol_nn;
    const auto ifStmt_nn = requireIfStmt(requireStmtNode(blockItems[3]));

    const auto cond_nn = requireIntToBoolExp(ifStmt_nn->m_condExp_nn);
    const auto addExp_nn = requireBinaryExp(cond_nn->m_operand_nn);
    requireAddOp(*addExp_nn, yesod::frontend::AddOpKeyword::plus);
    require(requireLValExp(addExp_nn->m_lhs_nn)->m_symbol_nn == aSymbol_nn,
        "mixed arithmetic parents should keep their arithmetic lhs unchanged");

    const auto logicalAsInt_nn = requireBoolToIntExp(addExp_nn->m_rhs_nn);
    const auto logicalOr_nn = requireBinaryExp(logicalAsInt_nn->m_operand_nn);
    requireLogicalOrOp(*logicalOr_nn);
    require(requireLValExp(requireIntToBoolExp(logicalOr_nn->m_lhs_nn)->m_operand_nn)
                ->m_symbol_nn
            == bSymbol_nn,
        "logical-or lhs should normalize to boolean form");
    require(requireLValExp(requireIntToBoolExp(logicalOr_nn->m_rhs_nn)->m_operand_nn)
                ->m_symbol_nn
            == cSymbol_nn,
        "logical-or rhs should normalize to boolean form");
}

void testNestedLogicalConditionKeepsBooleanSubexpressionsExplicit()
{
    const auto root_nn = analyzeRoot(
        "int main(){int a; int b; int c; int d; if (a + ((b || c) && d)) return 1; return 0;}");
    const auto& blockItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;
    const auto bSymbol_nn = requireVarDecl(blockItems[1])->m_varDefs[0]->m_symbol_nn;
    const auto cSymbol_nn = requireVarDecl(blockItems[2])->m_varDefs[0]->m_symbol_nn;
    const auto dSymbol_nn = requireVarDecl(blockItems[3])->m_varDefs[0]->m_symbol_nn;
    const auto ifStmt_nn = requireIfStmt(requireStmtNode(blockItems[4]));

    const auto outerCond_nn = requireIntToBoolExp(ifStmt_nn->m_condExp_nn);
    const auto addExp_nn = requireBinaryExp(outerCond_nn->m_operand_nn);
    const auto andAsInt_nn = requireBoolToIntExp(addExp_nn->m_rhs_nn);
    const auto logicalAnd_nn = requireBinaryExp(andAsInt_nn->m_operand_nn);
    requireLogicalAndOp(*logicalAnd_nn);

    const auto rebalancedOr_nn = requireIntToBoolExp(logicalAnd_nn->m_lhs_nn);
    const auto logicalOrAsInt_nn = requireBoolToIntExp(rebalancedOr_nn->m_operand_nn);
    const auto logicalOr_nn = requireBinaryExp(logicalOrAsInt_nn->m_operand_nn);
    requireLogicalOrOp(*logicalOr_nn);
    require(requireLValExp(requireIntToBoolExp(logicalOr_nn->m_lhs_nn)->m_operand_nn)
                ->m_symbol_nn
            == bSymbol_nn,
        "nested logical-or lhs should stay explicitly booleanized");
    require(requireLValExp(requireIntToBoolExp(logicalOr_nn->m_rhs_nn)->m_operand_nn)
                ->m_symbol_nn
            == cSymbol_nn,
        "nested logical-or rhs should stay explicitly booleanized");
    require(requireLValExp(requireIntToBoolExp(logicalAnd_nn->m_rhs_nn)->m_operand_nn)
                ->m_symbol_nn
            == dSymbol_nn,
        "logical-and rhs should normalize plain arithmetic values to boolean form");
}

void testConstantFoldableBooleanChainsStayBooleanInIfConditions()
{
    const auto root_nn = analyzeRoot(
        "int main(){if ((1 || 0) && 2) return 1; return 0;}");
    const auto ifStmt_nn = requireIfStmt(requireStmtNode(
        root_nn->m_funcDef_nn->m_block_nn->m_blockItems[0]));

    const auto cond_nn = requireIntToBoolExp(ifStmt_nn->m_condExp_nn);
    require(requireNumberExp(cond_nn->m_operand_nn)->m_value == 1,
        "constant-folded boolean if conditions should remain explicit boolean expressions");
}

} // namespace

int main()
{
    testIfConditionNormalizesPlainValueToBoolean();
    testMixedArithmeticAndLogicalConditionNormalizesBothWays();
    testNestedLogicalConditionKeepsBooleanSubexpressionsExplicit();
    testConstantFoldableBooleanChainsStayBooleanInIfConditions();
    return 0;
}