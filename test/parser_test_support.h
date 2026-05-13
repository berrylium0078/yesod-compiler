#ifndef _YESOD_TEST_PARSER_TEST_SUPPORT_H_
#define _YESOD_TEST_PARSER_TEST_SUPPORT_H_

#include <cstdlib>

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "frontend/ast.h"
#include "frontend/parser.h"

namespace yesod::test_support::parser {

using namespace yesod::frontend;

static_assert(
    std::is_same_v<decltype(std::declval<Identifier>().m_name), std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<Number>().m_value), int32_t>);
static_assert(std::is_same_v<decltype(std::declval<FuncDef>().m_funcType),
    FuncTypeKeyword>);
static_assert(std::is_same_v<decltype(std::declval<ConstDecl>().m_bType),
    BTypeKeyword>);
static_assert(std::is_same_v<decltype(std::declval<ReturnStmt>().m_exp_nn),
    std::shared_ptr<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<AssignStmt>().m_lVal_nn),
    std::shared_ptr<LVal>>);
static_assert(std::is_same_v<decltype(std::declval<ExpStmt>().m_exp_nn),
    std::shared_ptr<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<IfStmt>().m_condExp_nn),
    std::shared_ptr<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<IfStmt>().m_thenStmt_nn),
    std::shared_ptr<StmtNode>>);
static_assert(std::is_same_v<decltype(std::declval<IfStmt>().m_elseStmt_nn),
    std::shared_ptr<StmtNode>>);
static_assert(std::is_same_v<decltype(std::declval<WhileStmt>().m_condExp_nn),
    std::shared_ptr<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<WhileStmt>().m_bodyStmt_nn),
    std::shared_ptr<StmtNode>>);
static_assert(std::is_enum_v<UnaryOpKeyword>);
static_assert(std::is_enum_v<MulOpKeyword>);
static_assert(std::is_enum_v<AddOpKeyword>);
static_assert(std::is_enum_v<RelOpKeyword>);
static_assert(std::is_enum_v<EqOpKeyword>);
static_assert(
    std::variant_size_v<decltype(std::declval<DeclNode>().m_decl)> == 2);
static_assert(
    std::variant_size_v<decltype(std::declval<StmtNode>().m_stmt)> == 8);
static_assert(
    std::variant_size_v<decltype(std::declval<BlockItemNode>().m_blockItem)>
    == 2);
static_assert(std::variant_size_v<PrimaryExp::Kind> == 3);
static_assert(std::variant_size_v<UnaryExp::Kind> == 2);

[[noreturn]] inline void fail(const std::string& message)
{
    std::cerr << "parser_test failure: " << message << std::endl;
    std::exit(1);
}

inline void require(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

inline ParseOutput parseSource(const std::string& source)
{
    Parser parser(source);
    return parser.parse();
}

inline std::shared_ptr<CompUnit> parseRoot(const std::string& source)
{
    auto output = parseSource(source);
    if (!output.success()) {
        std::string message = "expected parse success";
        if (!output.m_diagnostics.empty()) {
            message += ": ";
            message += output.m_diagnostics.front().m_message;
        }
        fail(message);
    }
    return output.m_root;
}

inline const Diagnostic& firstDiagnostic(const ParseOutput& output)
{
    require(!output.m_diagnostics.empty(), "expected at least one diagnostic");
    return output.m_diagnostics.front();
}

inline const Exp& requireExp(const std::shared_ptr<Exp>& exp_nn)
{
    require(exp_nn != nullptr, "expected expression node");
    return *exp_nn;
}

inline const LOrExp& requireLOrExp(const std::shared_ptr<LOrExp>& lOrExp_nn)
{
    require(lOrExp_nn != nullptr, "expected logical-or expression node");
    return *lOrExp_nn;
}

inline const LAndExp& requireLAndExp(const std::shared_ptr<LAndExp>& lAndExp_nn)
{
    require(lAndExp_nn != nullptr, "expected logical-and expression node");
    return *lAndExp_nn;
}

inline const EqExp& requireEqExp(const std::shared_ptr<EqExp>& eqExp_nn)
{
    require(eqExp_nn != nullptr, "expected equality expression node");
    return *eqExp_nn;
}

inline const RelExp& requireRelExp(const std::shared_ptr<RelExp>& relExp_nn)
{
    require(relExp_nn != nullptr, "expected relational expression node");
    return *relExp_nn;
}

inline const AddExp& requireAddExp(const std::shared_ptr<AddExp>& addExp_nn)
{
    require(addExp_nn != nullptr, "expected additive expression node");
    return *addExp_nn;
}

inline const MulExp& requireMulExp(const std::shared_ptr<MulExp>& mulExp_nn)
{
    require(mulExp_nn != nullptr, "expected multiplicative expression node");
    return *mulExp_nn;
}

inline const UnaryExp& requireUnaryExp(
    const std::shared_ptr<UnaryExp>& unaryExp_nn)
{
    require(unaryExp_nn != nullptr, "expected unary expression node");
    return *unaryExp_nn;
}

inline const PrimaryExp& requirePrimaryExp(
    const std::shared_ptr<PrimaryExp>& primaryExp_nn)
{
    require(primaryExp_nn != nullptr, "expected primary expression node");
    return *primaryExp_nn;
}

inline const LVal& requireLVal(const std::shared_ptr<LVal>& lVal_nn)
{
    require(lVal_nn != nullptr, "expected lvalue node");
    return *lVal_nn;
}

inline std::shared_ptr<BlockItemNode> requireBlockItem(
    const std::shared_ptr<BlockItemNode>& blockItemNode_nn)
{
    require(blockItemNode_nn != nullptr, "expected block item node");
    return blockItemNode_nn;
}

inline std::shared_ptr<StmtNode> extractStmtNode(
    const std::shared_ptr<BlockItemNode>& blockItemNode_nn)
{
    std::shared_ptr<StmtNode> stmtNode;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<StmtNode>>) {
                stmtNode = blockItemAlt;
            }
        },
        requireBlockItem(blockItemNode_nn)->m_blockItem);
    require(stmtNode != nullptr, "expected statement block item variant");
    return stmtNode;
}

inline std::shared_ptr<DeclNode> extractDeclNode(
    const std::shared_ptr<BlockItemNode>& blockItemNode_nn)
{
    std::shared_ptr<DeclNode> declNode;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<DeclNode>>) {
                declNode = blockItemAlt;
            }
        },
        requireBlockItem(blockItemNode_nn)->m_blockItem);
    require(declNode != nullptr, "expected declaration block item variant");
    return declNode;
}

inline std::shared_ptr<ReturnStmt> extractReturnStmt(
    const std::shared_ptr<StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ReturnStmt> returnStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ReturnStmt>>) {
                returnStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(returnStmt != nullptr, "expected return statement variant");
    return returnStmt;
}

inline std::shared_ptr<ReturnStmt> extractReturnStmt(
    const std::shared_ptr<BlockItemNode>& blockItemNode_nn)
{
    return extractReturnStmt(extractStmtNode(blockItemNode_nn));
}

inline std::shared_ptr<IfStmt> extractIfStmt(
    const std::shared_ptr<StmtNode>& stmtNode_nn)
{
    std::shared_ptr<IfStmt> ifStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<IfStmt>>) {
                ifStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(ifStmt != nullptr, "expected if statement variant");
    return ifStmt;
}

inline std::shared_ptr<IfStmt> extractIfStmt(
    const std::shared_ptr<BlockItemNode>& blockItemNode_nn)
{
    return extractIfStmt(extractStmtNode(blockItemNode_nn));
}

inline std::shared_ptr<WhileStmt> extractWhileStmt(
    const std::shared_ptr<StmtNode>& stmtNode_nn)
{
    std::shared_ptr<WhileStmt> whileStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<WhileStmt>>) {
                whileStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(whileStmt != nullptr, "expected while statement variant");
    return whileStmt;
}

inline std::shared_ptr<WhileStmt> extractWhileStmt(
    const std::shared_ptr<BlockItemNode>& blockItemNode_nn)
{
    return extractWhileStmt(extractStmtNode(blockItemNode_nn));
}

inline std::shared_ptr<BreakStmt> extractBreakStmt(
    const std::shared_ptr<StmtNode>& stmtNode_nn)
{
    std::shared_ptr<BreakStmt> breakStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<BreakStmt>>) {
                breakStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(breakStmt != nullptr, "expected break statement variant");
    return breakStmt;
}

inline std::shared_ptr<ContinueStmt> extractContinueStmt(
    const std::shared_ptr<StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ContinueStmt> continueStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ContinueStmt>>) {
                continueStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(continueStmt != nullptr, "expected continue statement variant");
    return continueStmt;
}

inline std::shared_ptr<AssignStmt> extractAssignStmt(
    const std::shared_ptr<StmtNode>& stmtNode_nn)
{
    std::shared_ptr<AssignStmt> assignStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<AssignStmt>>) {
                assignStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(assignStmt != nullptr, "expected assignment statement variant");
    return assignStmt;
}

inline std::shared_ptr<AssignStmt> extractAssignStmt(
    const std::shared_ptr<BlockItemNode>& blockItemNode_nn)
{
    return extractAssignStmt(extractStmtNode(blockItemNode_nn));
}

inline std::shared_ptr<ExpStmt> extractExpStmt(
    const std::shared_ptr<StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ExpStmt> expStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ExpStmt>>) {
                expStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(expStmt != nullptr, "expected expression statement variant");
    return expStmt;
}

inline std::shared_ptr<ExpStmt> extractExpStmt(
    const std::shared_ptr<BlockItemNode>& blockItemNode_nn)
{
    return extractExpStmt(extractStmtNode(blockItemNode_nn));
}

inline std::shared_ptr<Block> extractBlockStmt(
    const std::shared_ptr<StmtNode>& stmtNode_nn)
{
    std::shared_ptr<Block> block;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<Block>>) {
                block = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(block != nullptr, "expected block statement variant");
    return block;
}

inline std::shared_ptr<Block> extractBlockStmt(
    const std::shared_ptr<BlockItemNode>& blockItemNode_nn)
{
    return extractBlockStmt(extractStmtNode(blockItemNode_nn));
}

inline std::shared_ptr<ConstDecl> extractConstDecl(
    const std::shared_ptr<DeclNode>& declNode_nn)
{
    std::shared_ptr<ConstDecl> constDecl;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ConstDecl>>) {
                constDecl = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(constDecl != nullptr, "expected const declaration variant");
    return constDecl;
}

inline std::shared_ptr<VarDecl> extractVarDecl(
    const std::shared_ptr<DeclNode>& declNode_nn)
{
    std::shared_ptr<VarDecl> varDecl;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<VarDecl>>) {
                varDecl = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(varDecl != nullptr, "expected var declaration variant");
    return varDecl;
}

inline int32_t evaluateExp(const Exp& exp);
inline int32_t evaluateLOrExp(const LOrExp& lOrExp);
inline int32_t evaluateLAndExp(const LAndExp& lAndExp);
inline int32_t evaluateEqExp(const EqExp& eqExp);
inline int32_t evaluateRelExp(const RelExp& relExp);
inline int32_t evaluateAddExp(const AddExp& addExp);
inline int32_t evaluateMulExp(const MulExp& mulExp);
inline int32_t evaluateUnaryExp(const UnaryExp& unaryExp);
inline int32_t evaluatePrimaryExp(const PrimaryExp& primaryExp);

inline int32_t evaluateExp(const Exp& exp)
{
    return evaluateLOrExp(requireLOrExp(exp.m_lOrExp_nn));
}

inline int32_t evaluateLOrExp(const LOrExp& lOrExp)
{
    auto currentValue = evaluateLAndExp(requireLAndExp(lOrExp.m_head_nn));
    for (const auto& tailEntry : lOrExp.m_tail) {
        const auto rhsValue = evaluateLAndExp(requireLAndExp(tailEntry.second));
        currentValue = (currentValue != 0 || rhsValue != 0) ? 1 : 0;
    }
    return currentValue;
}

inline int32_t evaluateLAndExp(const LAndExp& lAndExp)
{
    auto currentValue = evaluateEqExp(requireEqExp(lAndExp.m_head_nn));
    for (const auto& tailEntry : lAndExp.m_tail) {
        const auto rhsValue = evaluateEqExp(requireEqExp(tailEntry.second));
        currentValue = (currentValue != 0 && rhsValue != 0) ? 1 : 0;
    }
    return currentValue;
}

inline int32_t evaluateEqExp(const EqExp& eqExp)
{
    auto currentValue = evaluateRelExp(requireRelExp(eqExp.m_head_nn));
    for (const auto& tailEntry : eqExp.m_tail) {
        const auto rhsValue = evaluateRelExp(requireRelExp(tailEntry.second));
        switch (tailEntry.first) {
        case EqOpKeyword::equal:
            currentValue = currentValue == rhsValue ? 1 : 0;
            break;
        case EqOpKeyword::notEqual:
            currentValue = currentValue != rhsValue ? 1 : 0;
            break;
        }
    }
    return currentValue;
}

inline int32_t evaluateRelExp(const RelExp& relExp)
{
    auto currentValue = evaluateAddExp(requireAddExp(relExp.m_head_nn));
    for (const auto& tailEntry : relExp.m_tail) {
        const auto rhsValue = evaluateAddExp(requireAddExp(tailEntry.second));
        switch (tailEntry.first) {
        case RelOpKeyword::less:
            currentValue = currentValue < rhsValue ? 1 : 0;
            break;
        case RelOpKeyword::greater:
            currentValue = currentValue > rhsValue ? 1 : 0;
            break;
        case RelOpKeyword::lessEqual:
            currentValue = currentValue <= rhsValue ? 1 : 0;
            break;
        case RelOpKeyword::greaterEqual:
            currentValue = currentValue >= rhsValue ? 1 : 0;
            break;
        }
    }
    return currentValue;
}

inline int32_t evaluateAddExp(const AddExp& addExp)
{
    auto currentValue = evaluateMulExp(requireMulExp(addExp.m_head_nn));
    for (const auto& tailEntry : addExp.m_tail) {
        const auto rhsValue = evaluateMulExp(requireMulExp(tailEntry.second));
        switch (tailEntry.first) {
        case AddOpKeyword::plus:
            currentValue += rhsValue;
            break;
        case AddOpKeyword::minus:
            currentValue -= rhsValue;
            break;
        }
    }
    return currentValue;
}

inline int32_t evaluateMulExp(const MulExp& mulExp)
{
    auto currentValue = evaluateUnaryExp(requireUnaryExp(mulExp.m_head_nn));
    for (const auto& tailEntry : mulExp.m_tail) {
        const auto rhsValue
            = evaluateUnaryExp(requireUnaryExp(tailEntry.second));
        switch (tailEntry.first) {
        case MulOpKeyword::star:
            currentValue *= rhsValue;
            break;
        case MulOpKeyword::slash:
            currentValue /= rhsValue;
            break;
        case MulOpKeyword::percent:
            currentValue %= rhsValue;
            break;
        }
    }
    return currentValue;
}

inline int32_t evaluateUnaryExp(const UnaryExp& unaryExp)
{
    return std::visit(
        [&](const auto& unaryAlt) -> int32_t {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<PrimaryExp>>) {
                return evaluatePrimaryExp(requirePrimaryExp(unaryAlt));
            } else {
                const auto operandValue
                    = evaluateUnaryExp(requireUnaryExp(unaryAlt.second));
                switch (unaryAlt.first) {
                case UnaryOpKeyword::plus:
                    return operandValue;
                case UnaryOpKeyword::minus:
                    return -operandValue;
                case UnaryOpKeyword::bang:
                    return operandValue == 0 ? 1 : 0;
                }
            }
            fail("unexpected unary operator");
        },
        unaryExp.m_kind);
}

inline int32_t evaluatePrimaryExp(const PrimaryExp& primaryExp)
{
    return std::visit(
        [&](const auto& primaryAlt) -> int32_t {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<Exp>>) {
                return evaluateExp(requireExp(primaryAlt));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<LVal>>) {
                fail("cannot evaluate lvalue-backed primary expression");
            } else {
                return primaryAlt->m_value;
            }
        },
        primaryExp.m_kind);
}

inline bool primaryIsParenthesized(const PrimaryExp& primaryExp)
{
    return std::visit(
        [&](const auto& primaryAlt) -> bool {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            return std::is_same_v<AltType, std::shared_ptr<Exp>>;
        },
        primaryExp.m_kind);
}

inline bool containsParenthesizedUnary(const UnaryExp& unaryExp)
{
    return std::visit(
        [&](const auto& unaryAlt) -> bool {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<PrimaryExp>>) {
                if (primaryIsParenthesized(requirePrimaryExp(unaryAlt))) {
                    return true;
                }
                return false;
            } else {
                return containsParenthesizedUnary(
                    requireUnaryExp(unaryAlt.second));
            }
        },
        unaryExp.m_kind);
}

inline bool containsParenthesizedMul(const MulExp& mulExp)
{
    if (containsParenthesizedUnary(requireUnaryExp(mulExp.m_head_nn))) {
        return true;
    }
    for (const auto& tailEntry : mulExp.m_tail) {
        if (containsParenthesizedUnary(requireUnaryExp(tailEntry.second))) {
            return true;
        }
    }
    return false;
}

inline bool containsParenthesizedAdd(const AddExp& addExp)
{
    if (containsParenthesizedMul(requireMulExp(addExp.m_head_nn))) {
        return true;
    }
    for (const auto& tailEntry : addExp.m_tail) {
        if (containsParenthesizedMul(requireMulExp(tailEntry.second))) {
            return true;
        }
    }
    return false;
}

inline bool containsParenthesizedRel(const RelExp& relExp)
{
    if (containsParenthesizedAdd(requireAddExp(relExp.m_head_nn))) {
        return true;
    }
    for (const auto& tailEntry : relExp.m_tail) {
        if (containsParenthesizedAdd(requireAddExp(tailEntry.second))) {
            return true;
        }
    }
    return false;
}

inline bool containsParenthesizedEq(const EqExp& eqExp)
{
    if (containsParenthesizedRel(requireRelExp(eqExp.m_head_nn))) {
        return true;
    }
    for (const auto& tailEntry : eqExp.m_tail) {
        if (containsParenthesizedRel(requireRelExp(tailEntry.second))) {
            return true;
        }
    }
    return false;
}

inline bool containsParenthesizedLAnd(const LAndExp& lAndExp)
{
    if (containsParenthesizedEq(requireEqExp(lAndExp.m_head_nn))) {
        return true;
    }
    for (const auto& tailEntry : lAndExp.m_tail) {
        if (containsParenthesizedEq(requireEqExp(tailEntry.second))) {
            return true;
        }
    }
    return false;
}

inline bool expressionContainsParenthesizedPrimary(const Exp& exp)
{
    const auto& lOrExp = requireLOrExp(exp.m_lOrExp_nn);
    if (containsParenthesizedLAnd(requireLAndExp(lOrExp.m_head_nn))) {
        return true;
    }
    for (const auto& tailEntry : lOrExp.m_tail) {
        if (containsParenthesizedLAnd(requireLAndExp(tailEntry.second))) {
            return true;
        }
    }
    return false;
}

} // namespace yesod::test_support::parser

#endif