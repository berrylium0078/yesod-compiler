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
#include "test_support.h"

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
    std::is_same_v<decltype(std::declval<ReturnStmt>().m_exp_nn), Ptr<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<AssignStmt>().m_lVal_nn),
    Ptr<Exp>>);
static_assert(
    std::is_same_v<decltype(std::declval<ExpStmt>().m_exp_nn), Ptr<Exp>>);
static_assert(
    std::is_same_v<decltype(std::declval<IfStmt>().m_condExp_nn), Ptr<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<IfStmt>().m_thenStmt_nn),
    Ptr<StmtNode>>);
static_assert(std::is_same_v<decltype(std::declval<IfStmt>().m_elseStmt_nn),
    Ptr<StmtNode>>);
static_assert(std::is_same_v<decltype(std::declval<WhileStmt>().m_condExp_nn),
    Ptr<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<WhileStmt>().m_bodyStmt_nn),
    Ptr<StmtNode>>);
static_assert(std::is_enum_v<UnaryOpKeyword>);
static_assert(std::is_enum_v<BinaryOpKeyword>);
static_assert(
    std::variant_size_v<decltype(std::declval<DeclNode>().m_decl)> == 2);
static_assert(
    std::variant_size_v<decltype(std::declval<StmtNode>().m_stmt)> == 8);
static_assert(
    std::variant_size_v<decltype(std::declval<BlockItemNode>().m_blockItem)>
    == 2);
static_assert(std::variant_size_v<Exp::Kind> == 5);

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "parser_test failure: " << message << std::endl;
    std::exit(1);
}

void require(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

class ParserTestBase {
public:
    void parseSource(const std::string& source)
    {
        Parser parser(source);
        m_output = parser.parse();
    }
    void parseRoot(const std::string& source)
    {
        parseSource(source);
        if (!m_output.success()) {
            std::string message = "expected parse success";
            if (!m_output.m_diagnostics.empty()) {
                message += ": ";
                message += m_output.m_diagnostics.front().m_message;
            }
            fail(message);
        }
    }
    template <class Self> auto&& ast(this Self& self)
    {
        return self.m_output.m_ast;
    }
    Ptr<CompUnit> root() const { return m_output.m_root; }
    bool success() const { return m_output.success(); }

    const Diagnostic& firstDiagnostic()
    {
        require(!m_output.m_diagnostics.empty(),
            "expected at least one diagnostic");
        return m_output.m_diagnostics.front();
    }
    Ptr<FuncDef> firstFuncDef()
    {
        auto compUnit_nn = root();
        require(compUnit_nn, "expected compilation unit node");
        for (const auto topLevelItem_nn : compUnit_nn(ast()).m_topLevelItems) {
            auto funcDef = std::get_if<Ptr<FuncDef>>(
                &topLevelItem_nn(ast()).m_topLevelItem);
            if (funcDef)
                return *funcDef;
        }
        fail("expected at least one function definition in compilation unit");
    }

    const Exp::Binary& requireBinaryExp(const Ptr<Exp>& exp_nn)
    {
        const auto* binaryExp = std::get_if<Exp::Binary>(&exp_nn(ast()).m_kind);
        require(binaryExp != nullptr, "expected binary expression root");
        return *binaryExp;
    }
    const Exp::Unary& requireUnaryExp(const Ptr<Exp>& exp_nn)
    {
        const auto* unaryExp = std::get_if<Exp::Unary>(&exp_nn(ast()).m_kind);
        require(unaryExp != nullptr, "expected unary expression root");
        return *unaryExp;
    }
    const LVal& requireLVal(const Ptr<Exp>& exp_nn)
    {
        const auto* lVal = std::get_if<LVal>(&exp_nn(ast()).m_kind);
        require(lVal != nullptr, "expected lvalue expression");
        return *lVal;
    }
    const Number& requireNumber(const Ptr<Exp>& exp_nn)
    {
        const auto* number = std::get_if<Number>(&exp_nn(ast()).m_kind);
        require(number != nullptr, "expected number expression");
        return *number;
    }
    Ptr<BlockItemNode> requireBlockItem(const Ptr<BlockItemNode>& node)
    {
        require(node, "expected block item node");
        return node;
    }
    Ptr<StmtNode> extractStmtNode(
        const Ptr<BlockItemNode>& blockItemNode_nn)
    {
        auto& blockItem = requireBlockItem(blockItemNode_nn)(ast()).m_blockItem;
        auto* stmtNode = std::get_if<Ptr<StmtNode>>(&blockItem);
        require(stmtNode, "expected statement block item variant");
        return *stmtNode;
    }

    Ptr<DeclNode> extractDeclNode(
        const Ptr<BlockItemNode>& blockItemNode_nn)
    {
        auto& blockItem = requireBlockItem(blockItemNode_nn)(ast()).m_blockItem;
        auto* declNode = std::get_if<Ptr<DeclNode>>(&blockItem);
        require(declNode, "expected declaration block item variant");
        return *declNode;
    }

    Ptr<ReturnStmt> extractReturnStmt(const Ptr<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(ast()).m_stmt;
        auto* returnStmt = std::get_if<Ptr<ReturnStmt>>(&stmt);
        require(returnStmt, "expected return statement variant");
        return *returnStmt;
    }
    Ptr<ReturnStmt> extractReturnStmt(
        const Ptr<BlockItemNode>& blockItemNode_nn)
    {
        return extractReturnStmt(extractStmtNode(blockItemNode_nn));
    }
    Ptr<IfStmt> extractIfStmt(const Ptr<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(ast()).m_stmt;
        auto* ifStmt = std::get_if<Ptr<IfStmt>>(&stmt);
        require(ifStmt != nullptr, "expected if statement variant");
        return *ifStmt;
    }
    Ptr<IfStmt> extractIfStmt(const Ptr<BlockItemNode>& blockItemNode_nn)
    {
        return extractIfStmt(extractStmtNode(blockItemNode_nn));
    }
    Ptr<WhileStmt> extractWhileStmt(const Ptr<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(ast()).m_stmt;
        auto* whileStmt = std::get_if<Ptr<WhileStmt>>(&stmt);
        require(whileStmt != nullptr, "expected while statement variant");
        return *whileStmt;
    }
    Ptr<WhileStmt> extractWhileStmt(
        const Ptr<BlockItemNode>& blockItemNode_nn)
    {
        return extractWhileStmt(extractStmtNode(blockItemNode_nn));
    }
    Ptr<BreakStmt> extractBreakStmt(const Ptr<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(ast()).m_stmt;
        auto* breakStmt = std::get_if<Ptr<BreakStmt>>(&stmt);
        require(breakStmt != nullptr, "expected break statement variant");
        return *breakStmt;
    }
    Ptr<ContinueStmt> extractContinueStmt(const Ptr<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(ast()).m_stmt;
        auto* continueStmt = std::get_if<Ptr<ContinueStmt>>(&stmt);
        require(continueStmt != nullptr, "expected continue statement variant");
        return *continueStmt;
    }
    Ptr<AssignStmt> extractAssignStmt(const Ptr<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(ast()).m_stmt;
        auto* assignStmt = std::get_if<Ptr<AssignStmt>>(&stmt);
        require(assignStmt != nullptr, "expected assignment statement variant");
        return *assignStmt;
    }
    Ptr<AssignStmt> extractAssignStmt(
        const Ptr<BlockItemNode>& blockItemNode_nn)
    {
        return extractAssignStmt(extractStmtNode(blockItemNode_nn));
    }
    Ptr<ExpStmt> extractExpStmt(const Ptr<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(ast()).m_stmt;
        auto* expStmt = std::get_if<Ptr<ExpStmt>>(&stmt);
        require(expStmt != nullptr, "expected expression statement variant");
        return *expStmt;
    }
    Ptr<ExpStmt> extractExpStmt(
        const Ptr<BlockItemNode>& blockItemNode_nn)
    {
        return extractExpStmt(extractStmtNode(blockItemNode_nn));
    }
    Ptr<Block> extractBlockStmt(const Ptr<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(ast()).m_stmt;
        auto* block = std::get_if<Ptr<Block>>(&stmt);
        require(block != nullptr, "expected block statement variant");
        return *block;
    }
    Ptr<Block> extractBlockStmt(
        const Ptr<BlockItemNode>& blockItemNode_nn)
    {
        return extractBlockStmt(extractStmtNode(blockItemNode_nn));
    }

    Ptr<ConstDecl> extractConstDecl(const Ptr<DeclNode>& declNode_nn)
    {
        auto& decl = declNode_nn(ast()).m_decl;
        auto* constDecl = std::get_if<Ptr<ConstDecl>>(&decl);
        require(constDecl, "expected const declaration variant");
        return *constDecl;
    }

    Ptr<VarDecl> extractVarDecl(const Ptr<DeclNode>& declNode_nn)
    {
        auto& decl = declNode_nn(ast()).m_decl;
        auto* varDecl = std::get_if<Ptr<VarDecl>>(&decl);
        require(varDecl, "expected var declaration variant");
        return *varDecl;
    }

    int32_t evaluateExp(Ptr<Exp> exp)
    {
        return MATCH(exp(ast()).m_kind)
            WITH([](const Number& number) -> int32_t { return number.m_value; },
                [](const LVal& lval) -> int32_t {
                    fail("cannot evaluate lvalue expression");
                },
                [](const Exp::Call& call) -> int32_t {
                    fail("cannot evaluate call expression");
                },
                [&](const Exp::Unary& unary) -> int32_t {
                    const auto value = evaluateExp(unary.m_lhs_nn);
                    switch (unary.m_op) {
                    case UnaryOpKeyword::plus:
                        return value;
                    case UnaryOpKeyword::minus:
                        return -value;
                    case UnaryOpKeyword::bang:
                        return value == 0 ? 1 : 0;
                    }
                    fail("unexpected unary operator");
                },
                [&](const Exp::Binary& binary) -> int32_t {
                    const auto lhsValue = evaluateExp(binary.m_lhs_nn);
                    const auto rhsValue = evaluateExp(binary.m_rhs_nn);
                    switch (binary.m_op) {
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
                });
    }

private:
    ParseOutput m_output;
};

} // namespace yesod::test_support::parser

#endif
