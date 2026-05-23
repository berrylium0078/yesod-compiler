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
    std::is_same_v<decltype(std::declval<Identifier>().name), std::string>);
static_assert(std::is_same_v<decltype(std::declval<Number>().value), int32_t>);
static_assert(std::is_same_v<decltype(std::declval<FuncDef>().m_funcType),
    FuncTypeKeyword>);
static_assert(
    std::is_same_v<decltype(std::declval<ConstDecl>().bType), BTypeKeyword>);
static_assert(
    std::is_same_v<decltype(std::declval<ReturnStmt>().m_exp_nn), Ptr<Exp>>);
static_assert(
    std::is_same_v<decltype(std::declval<AssignStmt>().m_lVal_nn), Ref<Exp>>);
static_assert(
    std::is_same_v<decltype(std::declval<ExpStmt>().m_exp_nn), Ptr<Exp>>);
static_assert(
    std::is_same_v<decltype(std::declval<IfStmt>().condition), Ref<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<IfStmt>().thenBody), Stmt>);
static_assert(std::is_same_v<decltype(std::declval<IfStmt>().elseBody), Stmt>);
static_assert(
    std::is_same_v<decltype(std::declval<WhileStmt>().condition), Ref<Exp>>);
static_assert(std::is_same_v<decltype(std::declval<WhileStmt>().body), Stmt>);
static_assert(std::is_enum_v<UnaryOpKeyword>);
static_assert(std::is_enum_v<BinaryOpKeyword>);
static_assert(std::variant_size_v<Decl> == 2);
static_assert(std::variant_size_v<Stmt> == 8);
static_assert(std::variant_size_v<BlockItem> == 2);
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
        for (const auto topLevelItem : compUnit_nn(ast()).topLevelItems) {
            auto funcDef = std::get_if<Ptr<FuncDef>>(&topLevelItem);
            if (funcDef)
                return *funcDef;
        }
        fail("expected at least one function definition in compilation unit");
    }

    const Exp::Binary& requireBinaryExp(const Ref<Exp>& exp_nn)
    {
        const auto* binaryExp = std::get_if<Exp::Binary>(&exp_nn(ast()).kind);
        require(binaryExp != nullptr, "expected binary expression root");
        return *binaryExp;
    }
    const Exp::Unary& requireUnaryExp(const Ref<Exp>& exp_nn)
    {
        const auto* unaryExp = std::get_if<Exp::Unary>(&exp_nn(ast()).kind);
        require(unaryExp != nullptr, "expected unary expression root");
        return *unaryExp;
    }
    const LVal& requireLVal(const Ref<Exp>& exp_nn)
    {
        const auto* lVal = std::get_if<LVal>(&exp_nn(ast()).kind);
        require(lVal != nullptr, "expected lvalue expression");
        return *lVal;
    }
    const Number& requireNumber(const Ref<Exp>& exp_nn)
    {
        const auto* number = std::get_if<Number>(&exp_nn(ast()).kind);
        require(number != nullptr, "expected number expression");
        return *number;
    }
    Stmt extractStmtNode(const BlockItem& blockItem)
    {
        auto* stmtNode = std::get_if<Stmt>(&blockItem);
        require(stmtNode, "expected statement body item variant");
        return *stmtNode;
    }

    Decl extractDeclNode(const BlockItem& blockItem)
    {
        auto* declNode = std::get_if<Decl>(&blockItem);
        require(declNode, "expected declaration body item variant");
        return *declNode;
    }

    Ref<ReturnStmt> extractReturnStmt(const Stmt& stmt)
    {
        auto* returnStmt = std::get_if<Ptr<ReturnStmt>>(&stmt);
        require(returnStmt, "expected return statement variant");
        return returnStmt->ref();
    }
    Ref<ReturnStmt> extractReturnStmt(const BlockItem& blockItemNode_nn)
    {
        return extractReturnStmt(extractStmtNode(blockItemNode_nn));
    }
    Ref<IfStmt> extractIfStmt(const Stmt& stmt)
    {
        auto* ifStmt = std::get_if<Ptr<IfStmt>>(&stmt);
        require(ifStmt != nullptr, "expected if statement variant");
        return ifStmt->ref();
    }
    Ref<IfStmt> extractIfStmt(const BlockItem& blockItem)
    {
        return extractIfStmt(extractStmtNode(blockItem));
    }
    Ref<WhileStmt> extractWhileStmt(const Stmt& stmt)
    {
        auto* whileStmt = std::get_if<Ptr<WhileStmt>>(&stmt);
        require(whileStmt != nullptr, "expected while statement variant");
        return whileStmt->ref();
    }
    Ref<WhileStmt> extractWhileStmt(const BlockItem& blockItem)
    {
        return extractWhileStmt(extractStmtNode(blockItem));
    }
    Ref<BreakStmt> extractBreakStmt(const Stmt& stmt)
    {
        auto* breakStmt = std::get_if<Ptr<BreakStmt>>(&stmt);
        require(breakStmt != nullptr, "expected break statement variant");
        return breakStmt->ref();
    }
    Ref<ContinueStmt> extractContinueStmt(const Stmt& stmt)
    {
        auto* continueStmt = std::get_if<Ptr<ContinueStmt>>(&stmt);
        require(continueStmt != nullptr, "expected continue statement variant");
        return continueStmt->ref();
    }
    Ref<AssignStmt> extractAssignStmt(const Stmt& stmt)
    {
        auto* assignStmt = std::get_if<Ptr<AssignStmt>>(&stmt);
        require(assignStmt != nullptr, "expected assignment statement variant");
        return assignStmt->ref();
    }
    Ref<AssignStmt> extractAssignStmt(const BlockItem& blockItem)
    {
        return extractAssignStmt(extractStmtNode(blockItem));
    }
    Ref<ExpStmt> extractExpStmt(const Stmt& stmt)
    {
        auto* expStmt = std::get_if<Ptr<ExpStmt>>(&stmt);
        require(expStmt != nullptr, "expected expression statement variant");
        return expStmt->ref();
    }
    Ref<ExpStmt> extractExpStmt(const BlockItem& blockItemNode_nn)
    {
        return extractExpStmt(extractStmtNode(blockItemNode_nn));
    }
    Ref<Block> extractBlockStmt(const Stmt& stmt)
    {
        auto* body = std::get_if<Ptr<Block>>(&stmt);
        require(body != nullptr, "expected body statement variant");
        return body->ref();
    }
    Ref<Block> extractBlockStmt(const BlockItem& blockItem)
    {
        return extractBlockStmt(extractStmtNode(blockItem));
    }

    Ref<ConstDecl> extractConstDecl(const Decl& decl)
    {
        auto* constDecl = std::get_if<Ptr<ConstDecl>>(&decl);
        require(constDecl, "expected const declaration variant");
        return constDecl->ref();
    }

    Ref<VarDecl> extractVarDecl(const Decl& decl)
    {
        auto* varDecl = std::get_if<Ptr<VarDecl>>(&decl);
        require(varDecl, "expected var declaration variant");
        return varDecl->ref();
    }

    int32_t evaluateExp(Ref<Exp> exp)
    {
        return MATCH(exp(ast()).kind)
            WITH([](const Number& number) -> int32_t { return number.value; },
                [](const LVal& lval) -> int32_t {
                    fail("cannot evaluate lvalue expression");
                },
                [](const Exp::Call& call) -> int32_t {
                    fail("cannot evaluate call expression");
                },
                [&](const Exp::Unary& unary) -> int32_t {
                    const auto value = evaluateExp(unary.lhs);
                    switch (unary.op) {
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
                    const auto lhsValue = evaluateExp(binary.lhs);
                    const auto rhsValue = evaluateExp(binary.rhs);
                    switch (binary.op) {
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
