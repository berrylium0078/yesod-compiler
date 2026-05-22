#ifndef _YESOD_TEST_TEST_SUPPORT_H_
#define _YESOD_TEST_TEST_SUPPORT_H_

#include <cstdlib>

#include <iostream>
#include <string>
#include <type_traits>

#include "frontend/ast.h"
#include "frontend/parser.h"

namespace yesod::test_support {

using namespace yesod::frontend;
using LVal = Exp::LVal;
using Number = Exp::Number;

[[noreturn]] inline void fail(const std::string& message)
{
    std::cerr << "test failure: " << message << std::endl;
    std::exit(1);
}

inline void require(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

inline thread_local const AST* g_currentAst = nullptr;

inline void bindCurrentAst(const AST& ast) { g_currentAst = &ast; }

[[nodiscard]] inline const AST& currentAst()
{
    require(g_currentAst != nullptr, "expected bound AST for handle access");
    return *g_currentAst;
}

template <typename T> class HandleView {
public:
    HandleView() = default;
    HandleView(Handle<T> handle)
        : m_handle(handle)
    {
    }

    [[nodiscard]] operator Handle<T>() const { return m_handle; }

    [[nodiscard]] auto operator->() const { return &m_handle(currentAst()); }

    [[nodiscard]] auto operator()(const AST& ast) const
    {
        return m_handle(ast);
    }

    [[nodiscard]] explicit operator bool() const
    {
        return static_cast<bool>(m_handle);
    }

private:
    Handle<T> m_handle;
};

template <typename Output> struct OutputAstBase {
    Output m_output;

    template <class Self> auto&& ast(this Self& self)
    {
        return self.m_output.m_ast;
    }

    [[nodiscard]] bool success() const { return m_output.success(); }
    [[nodiscard]] Handle<CompUnit> root() const { return m_output.m_root; }
};

struct AstTestHelperBase {
    template <class Self>
    [[nodiscard]] const Diagnostic& firstDiagnostic(this Self& self)
    {
        require(!self.m_output.m_diagnostics.empty(),
            "expected at least one diagnostic");
        return self.m_output.m_diagnostics.front();
    }

    template <class Self>
    [[nodiscard]] Handle<FuncDef> firstFuncDef(this Self& self)
    {
        auto compUnit_nn = self.root();
        require(compUnit_nn, "expected compilation unit node");
        const auto& compUnit = compUnit_nn(self.ast());
        for (const auto topLevelItem_nn : compUnit.m_topLevelItems) {
            const auto& topLevelItem = topLevelItem_nn(self.ast());
            const auto funcDef_nn = MATCH(topLevelItem.m_topLevelItem) WITH(
                [](const Handle<FuncDef>& funcDef_nn) { return funcDef_nn; },
                [](const auto&) { return Handle<FuncDef> { }; }, );
            if (funcDef_nn) {
                return funcDef_nn;
            }
        }
        fail("expected at least one function definition in compilation unit");
    }

    template <class Self>
    [[nodiscard]] const Exp::Binary& requireBinaryExp(
        this Self& self, const Handle<Exp>& exp_nn)
    {
        const auto binaryExp = MATCH(exp_nn(self.ast()).m_kind)
            WITH([](const Exp::Binary& binaryExp) { return &binaryExp; },
                [](const auto&) {
                    return static_cast<const Exp::Binary*>(nullptr);
                }, );
        require(binaryExp != nullptr, "expected binary expression root");
        return *binaryExp;
    }

    template <class Self>
    [[nodiscard]] const Exp::Unary& requireUnaryExp(
        this Self& self, const Handle<Exp>& exp_nn)
    {
        const auto unaryExp = MATCH(exp_nn(self.ast()).m_kind)
            WITH([](const Exp::Unary& unaryExp) { return &unaryExp; },
                [](const auto&) {
                    return static_cast<const Exp::Unary*>(nullptr);
                }, );
        require(unaryExp != nullptr, "expected unary expression root");
        return *unaryExp;
    }

    template <class Self>
    [[nodiscard]] const LVal& requireLVal(
        this Self& self, const Handle<Exp>& exp_nn)
    {
        const auto lVal = MATCH(exp_nn(self.ast()).m_kind) WITH(
            [](const LVal& lVal) { return &lVal; },
            [](const auto&) { return static_cast<const LVal*>(nullptr); }, );
        require(lVal != nullptr, "expected lvalue expression");
        return *lVal;
    }

    template <class Self>
    [[nodiscard]] const Number& requireNumber(
        this Self& self, const Handle<Exp>& exp_nn)
    {
        const auto number = MATCH(exp_nn(self.ast()).m_kind) WITH(
            [](const Number& number) { return &number; },
            [](const auto&) { return static_cast<const Number*>(nullptr); }, );
        require(number != nullptr, "expected number expression");
        return *number;
    }

    template <class Self>
    [[nodiscard]] Handle<BlockItemNode> requireBlockItem(
        this Self&, const Handle<BlockItemNode>& node)
    {
        require(node, "expected block item node");
        return node;
    }

    template <class Self>
    [[nodiscard]] Handle<StmtNode> extractStmtNode(
        this Self& self, const Handle<BlockItemNode>& blockItemNode_nn)
    {
        auto& blockItem
            = self.requireBlockItem(blockItemNode_nn)(self.ast()).m_blockItem;
        const auto stmtNode = MATCH(blockItem)
            WITH([](const Handle<StmtNode>& stmtNode) { return stmtNode; },
                [](const auto&) { return Handle<StmtNode> { }; }, );
        require(stmtNode, "expected statement block item variant");
        return stmtNode;
    }

    template <class Self>
    [[nodiscard]] Handle<DeclNode> extractDeclNode(
        this Self& self, const Handle<BlockItemNode>& blockItemNode_nn)
    {
        auto& blockItem
            = self.requireBlockItem(blockItemNode_nn)(self.ast()).m_blockItem;
        const auto declNode = MATCH(blockItem)
            WITH([](const Handle<DeclNode>& declNode) { return declNode; },
                [](const auto&) { return Handle<DeclNode> { }; }, );
        require(declNode, "expected declaration block item variant");
        return declNode;
    }

    template <class Self>
    [[nodiscard]] Handle<ReturnStmt> extractReturnStmt(
        this Self& self, const Handle<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(self.ast()).m_stmt;
        const auto returnStmt = MATCH(stmt) WITH(
            [](const Handle<ReturnStmt>& returnStmt) { return returnStmt; },
            [](const auto&) { return Handle<ReturnStmt> { }; }, );
        require(returnStmt, "expected return statement variant");
        return returnStmt;
    }

    template <class Self>
    [[nodiscard]] Handle<ReturnStmt> extractReturnStmt(
        this Self& self, const Handle<BlockItemNode>& blockItemNode_nn)
    {
        return self.extractReturnStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Handle<IfStmt> extractIfStmt(
        this Self& self, const Handle<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(self.ast()).m_stmt;
        const auto ifStmt = MATCH(stmt)
            WITH([](const Handle<IfStmt>& ifStmt) { return ifStmt; },
                [](const auto&) { return Handle<IfStmt> { }; }, );
        require(ifStmt != nullptr, "expected if statement variant");
        return ifStmt;
    }

    template <class Self>
    [[nodiscard]] Handle<IfStmt> extractIfStmt(
        this Self& self, const Handle<BlockItemNode>& blockItemNode_nn)
    {
        return self.extractIfStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Handle<WhileStmt> extractWhileStmt(
        this Self& self, const Handle<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(self.ast()).m_stmt;
        const auto whileStmt = MATCH(stmt)
            WITH([](const Handle<WhileStmt>& whileStmt) { return whileStmt; },
                [](const auto&) { return Handle<WhileStmt> { }; }, );
        require(whileStmt != nullptr, "expected while statement variant");
        return whileStmt;
    }

    template <class Self>
    [[nodiscard]] Handle<WhileStmt> extractWhileStmt(
        this Self& self, const Handle<BlockItemNode>& blockItemNode_nn)
    {
        return self.extractWhileStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Handle<BreakStmt> extractBreakStmt(
        this Self& self, const Handle<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(self.ast()).m_stmt;
        const auto breakStmt = MATCH(stmt)
            WITH([](const Handle<BreakStmt>& breakStmt) { return breakStmt; },
                [](const auto&) { return Handle<BreakStmt> { }; }, );
        require(breakStmt != nullptr, "expected break statement variant");
        return breakStmt;
    }

    template <class Self>
    [[nodiscard]] Handle<ContinueStmt> extractContinueStmt(
        this Self& self, const Handle<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(self.ast()).m_stmt;
        const auto continueStmt = MATCH(stmt) WITH(
            [](const Handle<ContinueStmt>& continueStmt) {
                return continueStmt;
            },
            [](const auto&) { return Handle<ContinueStmt> { }; }, );
        require(continueStmt != nullptr, "expected continue statement variant");
        return continueStmt;
    }

    template <class Self>
    [[nodiscard]] Handle<AssignStmt> extractAssignStmt(
        this Self& self, const Handle<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(self.ast()).m_stmt;
        const auto assignStmt = MATCH(stmt) WITH(
            [](const Handle<AssignStmt>& assignStmt) { return assignStmt; },
            [](const auto&) { return Handle<AssignStmt> { }; }, );
        require(assignStmt != nullptr, "expected assignment statement variant");
        return assignStmt;
    }

    template <class Self>
    [[nodiscard]] Handle<AssignStmt> extractAssignStmt(
        this Self& self, const Handle<BlockItemNode>& blockItemNode_nn)
    {
        return self.extractAssignStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Handle<ExpStmt> extractExpStmt(
        this Self& self, const Handle<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(self.ast()).m_stmt;
        const auto expStmt = MATCH(stmt)
            WITH([](const Handle<ExpStmt>& expStmt) { return expStmt; },
                [](const auto&) { return Handle<ExpStmt> { }; }, );
        require(expStmt != nullptr, "expected expression statement variant");
        return expStmt;
    }

    template <class Self>
    [[nodiscard]] Handle<ExpStmt> extractExpStmt(
        this Self& self, const Handle<BlockItemNode>& blockItemNode_nn)
    {
        return self.extractExpStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Handle<Block> extractBlockStmt(
        this Self& self, const Handle<StmtNode>& stmtNode)
    {
        auto& stmt = stmtNode(self.ast()).m_stmt;
        const auto block
            = MATCH(stmt) WITH([](const Handle<Block>& block) { return block; },
                [](const auto&) { return Handle<Block> { }; }, );
        require(block != nullptr, "expected block statement variant");
        return block;
    }

    template <class Self>
    [[nodiscard]] Handle<Block> extractBlockStmt(
        this Self& self, const Handle<BlockItemNode>& blockItemNode_nn)
    {
        return self.extractBlockStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Handle<ConstDecl> extractConstDecl(
        this Self& self, const Handle<DeclNode>& declNode_nn)
    {
        auto& decl = declNode_nn(self.ast()).m_decl;
        const auto constDecl = MATCH(decl)
            WITH([](const Handle<ConstDecl>& constDecl) { return constDecl; },
                [](const auto&) { return Handle<ConstDecl> { }; }, );
        require(constDecl, "expected const declaration variant");
        return constDecl;
    }

    template <class Self>
    [[nodiscard]] Handle<VarDecl> extractVarDecl(
        this Self& self, const Handle<DeclNode>& declNode_nn)
    {
        auto& decl = declNode_nn(self.ast()).m_decl;
        const auto varDecl = MATCH(decl)
            WITH([](const Handle<VarDecl>& varDecl) { return varDecl; },
                [](const auto&) { return Handle<VarDecl> { }; }, );
        require(varDecl, "expected var declaration variant");
        return varDecl;
    }

    template <class Self>
    [[nodiscard]] int32_t evaluateExp(this Self& self, const Handle<Exp>& exp)
    {
        return MATCH(exp(self.ast()).m_kind)
            WITH([](const Number& number) -> int32_t { return number.m_value; },
                [](const LVal&) -> int32_t {
                    fail("cannot evaluate lvalue expression");
                },
                [](const Exp::Call&) -> int32_t {
                    fail("cannot evaluate call expression");
                },
                [&](const Exp::Unary& unary) -> int32_t {
                    const auto value = self.evaluateExp(unary.m_lhs_nn);
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
                    const auto lhsValue = self.evaluateExp(binary.m_lhs_nn);
                    const auto rhsValue = self.evaluateExp(binary.m_rhs_nn);
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
                }, );
    }

    template <class Self>
    [[nodiscard]] Handle<Exp> requireScalarConstInitExp(
        this Self& self, const Handle<ConstInitVal>& initVal_nn)
    {
        const auto exp_nn = MATCH(initVal_nn(self.ast()).m_kind)
            WITH([](const Handle<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) { return Handle<Exp> { }; }, );
        require(
            exp_nn != nullptr, "expected scalar const initializer expression");
        return exp_nn;
    }

    template <class Self>
    [[nodiscard]] Handle<Exp> requireScalarInitExp(
        this Self& self, const Handle<InitVal>& initVal_nn)
    {
        const auto exp_nn = MATCH(initVal_nn(self.ast()).m_kind)
            WITH([](const Handle<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) { return Handle<Exp> { }; }, );
        require(exp_nn != nullptr, "expected scalar initializer expression");
        return exp_nn;
    }

    template <class Self>
    [[nodiscard]] const ConstInitVal::List& requireConstInitList(
        this Self& self, const Handle<ConstInitVal>& initVal_nn)
    {
        const auto list = MATCH(initVal_nn(self.ast()).m_kind)
            WITH([](const ConstInitVal::List& list) { return &list; },
                [](const auto&) {
                    return static_cast<const ConstInitVal::List*>(nullptr);
                }, );
        require(list != nullptr, "expected brace initializer list");
        return *list;
    }

    template <class Self>
    [[nodiscard]] const InitVal::List& requireInitList(
        this Self& self, const Handle<InitVal>& initVal_nn)
    {
        const auto list = MATCH(initVal_nn(self.ast()).m_kind)
            WITH([](const InitVal::List& list) { return &list; },
                [](const auto&) {
                    return static_cast<const InitVal::List*>(nullptr);
                }, );
        require(list != nullptr, "expected brace initializer list");
        return *list;
    }

    template <class Self>
    [[nodiscard]] Handle<DeclNode> requireTopLevelDecl(
        this Self& self, const Handle<TopLevelItemNode>& topLevelItemNode_nn)
    {
        const auto declNode_nn
            = MATCH(topLevelItemNode_nn(self.ast()).m_topLevelItem) WITH(
                [](const Handle<DeclNode>& declNode_nn) { return declNode_nn; },
                [](const auto&) { return Handle<DeclNode> { }; });
        require(declNode_nn != nullptr, "expected top-level declaration");
        return declNode_nn;
    }

    template <class Self>
    [[nodiscard]] Handle<FuncDef> requireTopLevelFunc(
        this Self& self, const Handle<TopLevelItemNode>& topLevelItemNode_nn)
    {
        const auto funcDef_nn
            = MATCH(topLevelItemNode_nn(self.ast()).m_topLevelItem) WITH(
                [](const Handle<FuncDef>& funcDef_nn) { return funcDef_nn; },
                [](const auto&) { return Handle<FuncDef> { }; }, );
        require(
            funcDef_nn != nullptr, "expected top-level function definition");
        return funcDef_nn;
    }

    template <class Self>
    [[nodiscard]] Handle<FuncDef> requireFuncDefByName(this Self& self,
        const Handle<CompUnit>& compUnit_nn, const std::string& expectedName)
    {
        require(compUnit_nn != nullptr, "expected compilation unit node");
        for (const auto topLevelItem_nn :
            compUnit_nn(self.ast()).m_topLevelItems) {
            const auto funcDef_nn
                = MATCH(topLevelItem_nn(self.ast()).m_topLevelItem) WITH(
                    [](const Handle<FuncDef>& funcDef_nn) {
                        return funcDef_nn;
                    },
                    [](const auto&) { return Handle<FuncDef> { }; }, );
            if (funcDef_nn != nullptr
                && funcDef_nn(self.ast()).m_identifier_nn(self.ast()).m_name
                    == expectedName) {
                return funcDef_nn;
            }
        }
        fail("expected function definition named '" + expectedName + "'");
    }

    template <class Self>
    [[nodiscard]] const Exp::Call& requireCallExp(
        this Self& self, const Handle<Exp>& exp_nn)
    {
        const auto callExp = MATCH(exp_nn(self.ast()).m_kind)
            WITH([](const Exp::Call& callExp) { return &callExp; },
                [](const auto&) {
                    return static_cast<const Exp::Call*>(nullptr);
                }, );
        require(callExp != nullptr, "expected call expression root");
        return *callExp;
    }
};

struct TestBase : AstTestHelperBase { };

inline HandleView<FuncDef> firstFuncDef(const Handle<CompUnit>& compUnit_nn)
{
    require(compUnit_nn != nullptr, "expected compilation unit node");
    for (const auto topLevelItem_nn :
        compUnit_nn(currentAst()).m_topLevelItems) {
        const auto funcDef_nn
            = MATCH(topLevelItem_nn(currentAst()).m_topLevelItem) WITH(
                [](const Handle<FuncDef>& funcDef_nn) {
                    return HandleView<FuncDef>(funcDef_nn);
                },
                [](const auto&) { return HandleView<FuncDef> { }; }, );
        if (funcDef_nn) {
            return funcDef_nn;
        }
    }
    fail("expected at least one function definition in compilation unit");
}

} // namespace yesod::test_support

#endif