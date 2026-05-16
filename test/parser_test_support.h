#ifndef _YESOD_TEST_PARSER_TEST_SUPPORT_H_
#define _YESOD_TEST_PARSER_TEST_SUPPORT_H_

#include <cstdlib>

#include <iostream>
#include <string>
#include <type_traits>
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
static_assert(
    std::is_same_v<decltype(std::declval<ConstDecl>().m_bType), BTypeKeyword>);
static_assert(
    std::is_same_v<decltype(std::declval<ReturnStmt>().m_exp_nn), Handle<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<AssignStmt>().m_lVal_nn),
    Handle<Exp>>);
static_assert(
    std::is_same_v<decltype(std::declval<ExpStmt>().m_exp_nn), Handle<Exp>>);
static_assert(
    std::is_same_v<decltype(std::declval<IfStmt>().m_condExp_nn), Handle<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<IfStmt>().m_thenStmt_nn),
    Handle<StmtNode>>);
static_assert(std::is_same_v<decltype(std::declval<IfStmt>().m_elseStmt_nn),
    Handle<StmtNode>>);
static_assert(std::is_same_v<decltype(std::declval<WhileStmt>().m_condExp_nn),
    Handle<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<WhileStmt>().m_bodyStmt_nn),
    Handle<StmtNode>>);
static_assert(std::is_enum_v<UnaryOpKeyword>);
static_assert(std::is_enum_v<BinaryOpKeyword>);
static_assert(
    std::variant_size_v<decltype(std::declval<DeclNode>().m_decl)> == 2);
static_assert(
    std::variant_size_v<decltype(std::declval<StmtNode>().m_stmt)> == 8);
static_assert(
    std::variant_size_v<decltype(std::declval<BlockItemNode>().m_blockItem)>
    == 2);
static_assert(std::variant_size_v<Exp::Kind> == 4);

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

struct ParsedOutput {
    explicit ParsedOutput(ParseOutput output)
        : m_output(std::move(output))
        , m_root(m_output.m_root)
        , m_diagnostics(m_output.m_diagnostics)
        , m_scope(m_output.m_ast.bindCurrent())
    {
    }

    ParsedOutput(const ParsedOutput&) = delete;
    ParsedOutput& operator=(const ParsedOutput&) = delete;
    ParsedOutput(ParsedOutput&& other)
        : m_output(std::move(other.m_output))
        , m_root(m_output.m_root)
        , m_diagnostics(m_output.m_diagnostics)
        , m_scope(std::move(other.m_scope))
    {
        m_scope.rebind(m_output.m_ast);
    }

    ParsedOutput& operator=(ParsedOutput&& other)
    {
        m_output = std::move(other.m_output);
        m_root = m_output.m_root;
        m_diagnostics = m_output.m_diagnostics;
        m_scope = std::move(other.m_scope);
        m_scope.rebind(m_output.m_ast);
        return *this;
    }

    [[nodiscard]] bool success() const { return m_output.success(); }

    [[nodiscard]] const CompUnit* operator->() const
    {
        return m_root ? &m_output.m_ast.get(m_root) : nullptr;
    }

    ParseOutput m_output;
    Handle<CompUnit> m_root;
    std::vector<Diagnostic> m_diagnostics;
    AST::ScopedCurrent m_scope;
};

inline ParsedOutput parseSource(const std::string& source)
{
    Parser parser(source);
    return ParsedOutput(parser.parse());
}

inline ParsedOutput parseRoot(const std::string& source)
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
    return output;
}

inline const Diagnostic& firstDiagnostic(const ParseOutput& output)
{
    require(!output.m_diagnostics.empty(), "expected at least one diagnostic");
    return output.m_diagnostics.front();
}

inline const Diagnostic& firstDiagnostic(const ParsedOutput& output)
{
    require(!output.m_diagnostics.empty(), "expected at least one diagnostic");
    return output.m_diagnostics.front();
}

inline const Exp& requireExp(const Handle<Exp>& exp_nn)
{
    require(exp_nn != nullptr, "expected expression node");
    return *exp_nn;
}

inline const Exp::Binary& requireBinaryExp(const Exp& exp)
{
    const auto* binaryExp = std::get_if<Exp::Binary>(&exp.m_kind);
    require(binaryExp != nullptr, "expected binary expression root");
    return *binaryExp;
}

inline const Exp::Binary& requireBinaryExp(const Handle<Exp>& exp_nn)
{
    return requireBinaryExp(requireExp(exp_nn));
}

inline const Exp::Unary& requireUnaryExp(const Exp& exp)
{
    const auto* unaryExp = std::get_if<Exp::Unary>(&exp.m_kind);
    require(unaryExp != nullptr, "expected unary expression root");
    return *unaryExp;
}

inline const Exp::Unary& requireUnaryExp(const Handle<Exp>& exp_nn)
{
    return requireUnaryExp(requireExp(exp_nn));
}

inline const LVal& requireLVal(const Exp& exp)
{
    const auto* lVal = std::get_if<LVal>(&exp.m_kind);
    require(lVal != nullptr, "expected lvalue expression");
    return *lVal;
}

inline const LVal& requireLVal(const Handle<Exp>& exp_nn)
{
    return requireLVal(requireExp(exp_nn));
}

inline const Number& requireNumber(const Exp& exp)
{
    const auto* number = std::get_if<Number>(&exp.m_kind);
    require(number != nullptr, "expected number expression");
    return *number;
}

inline const Number& requireNumber(const Handle<Exp>& exp_nn)
{
    return requireNumber(requireExp(exp_nn));
}

inline Handle<BlockItemNode> requireBlockItem(
    const Handle<BlockItemNode>& blockItemNode_nn)
{
    require(blockItemNode_nn != nullptr, "expected block item node");
    return blockItemNode_nn;
}

inline Handle<StmtNode> extractStmtNode(
    const Handle<BlockItemNode>& blockItemNode_nn)
{
    Handle<StmtNode> stmtNode;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<StmtNode>>) {
                stmtNode = blockItemAlt;
            }
        },
        requireBlockItem(blockItemNode_nn)->m_blockItem);
    require(stmtNode != nullptr, "expected statement block item variant");
    return stmtNode;
}

inline Handle<DeclNode> extractDeclNode(
    const Handle<BlockItemNode>& blockItemNode_nn)
{
    Handle<DeclNode> declNode;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<DeclNode>>) {
                declNode = blockItemAlt;
            }
        },
        requireBlockItem(blockItemNode_nn)->m_blockItem);
    require(declNode != nullptr, "expected declaration block item variant");
    return declNode;
}

inline Handle<ReturnStmt> extractReturnStmt(const Handle<StmtNode>& stmtNode_nn)
{
    Handle<ReturnStmt> returnStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<ReturnStmt>>) {
                returnStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(returnStmt != nullptr, "expected return statement variant");
    return returnStmt;
}

inline Handle<ReturnStmt> extractReturnStmt(
    const Handle<BlockItemNode>& blockItemNode_nn)
{
    return extractReturnStmt(extractStmtNode(blockItemNode_nn));
}

inline Handle<IfStmt> extractIfStmt(const Handle<StmtNode>& stmtNode_nn)
{
    Handle<IfStmt> ifStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<IfStmt>>) {
                ifStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(ifStmt != nullptr, "expected if statement variant");
    return ifStmt;
}

inline Handle<IfStmt> extractIfStmt(
    const Handle<BlockItemNode>& blockItemNode_nn)
{
    return extractIfStmt(extractStmtNode(blockItemNode_nn));
}

inline Handle<WhileStmt> extractWhileStmt(const Handle<StmtNode>& stmtNode_nn)
{
    Handle<WhileStmt> whileStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<WhileStmt>>) {
                whileStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(whileStmt != nullptr, "expected while statement variant");
    return whileStmt;
}

inline Handle<WhileStmt> extractWhileStmt(
    const Handle<BlockItemNode>& blockItemNode_nn)
{
    return extractWhileStmt(extractStmtNode(blockItemNode_nn));
}

inline Handle<BreakStmt> extractBreakStmt(const Handle<StmtNode>& stmtNode_nn)
{
    Handle<BreakStmt> breakStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<BreakStmt>>) {
                breakStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(breakStmt != nullptr, "expected break statement variant");
    return breakStmt;
}

inline Handle<ContinueStmt> extractContinueStmt(
    const Handle<StmtNode>& stmtNode_nn)
{
    Handle<ContinueStmt> continueStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<ContinueStmt>>) {
                continueStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(continueStmt != nullptr, "expected continue statement variant");
    return continueStmt;
}

inline Handle<AssignStmt> extractAssignStmt(const Handle<StmtNode>& stmtNode_nn)
{
    Handle<AssignStmt> assignStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<AssignStmt>>) {
                assignStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(assignStmt != nullptr, "expected assignment statement variant");
    return assignStmt;
}

inline Handle<AssignStmt> extractAssignStmt(
    const Handle<BlockItemNode>& blockItemNode_nn)
{
    return extractAssignStmt(extractStmtNode(blockItemNode_nn));
}

inline Handle<ExpStmt> extractExpStmt(const Handle<StmtNode>& stmtNode_nn)
{
    Handle<ExpStmt> expStmt;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<ExpStmt>>) {
                expStmt = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(expStmt != nullptr, "expected expression statement variant");
    return expStmt;
}

inline Handle<ExpStmt> extractExpStmt(
    const Handle<BlockItemNode>& blockItemNode_nn)
{
    return extractExpStmt(extractStmtNode(blockItemNode_nn));
}

inline Handle<Block> extractBlockStmt(const Handle<StmtNode>& stmtNode_nn)
{
    Handle<Block> block;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<Block>>) {
                block = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(block != nullptr, "expected block statement variant");
    return block;
}

inline Handle<Block> extractBlockStmt(
    const Handle<BlockItemNode>& blockItemNode_nn)
{
    return extractBlockStmt(extractStmtNode(blockItemNode_nn));
}

inline Handle<ConstDecl> extractConstDecl(const Handle<DeclNode>& declNode_nn)
{
    Handle<ConstDecl> constDecl;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<ConstDecl>>) {
                constDecl = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(constDecl != nullptr, "expected const declaration variant");
    return constDecl;
}

inline Handle<VarDecl> extractVarDecl(const Handle<DeclNode>& declNode_nn)
{
    Handle<VarDecl> varDecl;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<VarDecl>>) {
                varDecl = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(varDecl != nullptr, "expected var declaration variant");
    return varDecl;
}

inline int32_t evaluateExp(const Exp& exp)
{
    return std::visit(
        [&](const auto& expAlt) -> int32_t {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, Exp::Binary>) {
                const auto lhsValue = evaluateExp(requireExp(expAlt.m_lhs_nn));
                const auto rhsValue = evaluateExp(requireExp(expAlt.m_rhs_nn));
                switch (expAlt.m_op) {
                case BinaryOpKeyword::star:
                    return lhsValue * rhsValue;
                case BinaryOpKeyword::slash:
                    return lhsValue / rhsValue;
                case BinaryOpKeyword::percent:
                    return lhsValue % rhsValue;
                case BinaryOpKeyword::plus:
                    return lhsValue + rhsValue;
                case BinaryOpKeyword::minus:
                    return lhsValue - rhsValue;
                case BinaryOpKeyword::less:
                    return lhsValue < rhsValue ? 1 : 0;
                case BinaryOpKeyword::greater:
                    return lhsValue > rhsValue ? 1 : 0;
                case BinaryOpKeyword::lessEqual:
                    return lhsValue <= rhsValue ? 1 : 0;
                case BinaryOpKeyword::greaterEqual:
                    return lhsValue >= rhsValue ? 1 : 0;
                case BinaryOpKeyword::equal:
                    return lhsValue == rhsValue ? 1 : 0;
                case BinaryOpKeyword::notEqual:
                    return lhsValue != rhsValue ? 1 : 0;
                case BinaryOpKeyword::andAnd:
                    return (lhsValue != 0 && rhsValue != 0) ? 1 : 0;
                case BinaryOpKeyword::orOr:
                    return (lhsValue != 0 || rhsValue != 0) ? 1 : 0;
                }
                fail("unexpected binary operator");
            } else if constexpr (std::is_same_v<AltType, Exp::Unary>) {
                const auto operandValue
                    = evaluateExp(requireExp(expAlt.m_lhs_nn));
                switch (expAlt.m_op) {
                case UnaryOpKeyword::plus:
                    return operandValue;
                case UnaryOpKeyword::minus:
                    return -operandValue;
                case UnaryOpKeyword::bang:
                    return operandValue == 0 ? 1 : 0;
                }
                fail("unexpected unary operator");
            } else if constexpr (std::is_same_v<AltType, LVal>) {
                fail("cannot evaluate lvalue expression");
            } else {
                return expAlt.m_value;
            }
        },
        exp.m_kind);
}

} // namespace yesod::test_support::parser

#endif
