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

template <typename Output> struct OutputAstBase {
    Output m_output;

    template <class Self> auto&& ast(this Self& self)
    {
        return self.m_output.m_ast;
    }

    [[nodiscard]] bool success() const { return m_output.success(); }
    [[nodiscard]] Ptr<CompUnit> root() const { return m_output.m_root; }
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
    [[nodiscard]] Ptr<FuncDef> firstFuncDef(this Self& self)
    {
        auto compUnit_nn = self.root();
        require(compUnit_nn, "expected compilation unit node");
        const auto& compUnit = compUnit_nn(self.ast());
        for (const auto topLevelItem : compUnit.topLevelItems) {
            const auto funcDef_nn = MATCH(topLevelItem)
                WITH([](const Ref<FuncDef>& funcDef_nn) {
                    return funcDef_nn.ptr();
                },
                    [](const auto&) { return Ptr<FuncDef> { }; }, );
            if (funcDef_nn) {
                return funcDef_nn;
            }
        }
        fail("expected at least one function definition in compilation unit");
    }

    template <class Self>
    [[nodiscard]] const Exp::Binary& requireBinaryExp(
        this Self& self, const Ref<Exp>& exp_nn)
    {
        const auto binaryExp = MATCH(exp_nn(self.ast()).kind)
            WITH([](const Exp::Binary& binaryExp) { return &binaryExp; },
                [](const auto&) {
                    return static_cast<const Exp::Binary*>(nullptr);
                }, );
        require(binaryExp != nullptr, "expected binary expression root");
        return *binaryExp;
    }

    template <class Self>
    [[nodiscard]] const Exp::Unary& requireUnaryExp(
        this Self& self, const Ref<Exp>& exp_nn)
    {
        const auto unaryExp = MATCH(exp_nn(self.ast()).kind)
            WITH([](const Exp::Unary& unaryExp) { return &unaryExp; },
                [](const auto&) {
                    return static_cast<const Exp::Unary*>(nullptr);
                }, );
        require(unaryExp != nullptr, "expected unary expression root");
        return *unaryExp;
    }

    template <class Self>
    [[nodiscard]] const LVal& requireLVal(
        this Self& self, const Ref<Exp>& exp_nn)
    {
        const auto lVal = MATCH(exp_nn(self.ast()).kind) WITH(
            [](const LVal& lVal) { return &lVal; },
            [](const auto&) { return static_cast<const LVal*>(nullptr); }, );
        require(lVal != nullptr, "expected lvalue expression");
        return *lVal;
    }

    template <class Self>
    [[nodiscard]] const Number& requireNumber(
        this Self& self, const Ref<Exp>& exp_nn)
    {
        const auto number = MATCH(exp_nn(self.ast()).kind) WITH(
            [](const Number& number) { return &number; },
            [](const auto&) { return static_cast<const Number*>(nullptr); }, );
        require(number != nullptr, "expected number expression");
        return *number;
    }

    template <class Self>
    [[nodiscard]] Stmt extractStmtNode(
        this Self& self, const BlockItem& blockItem)
    {
        return MATCH(blockItem) WITH([](const Stmt& stmt) { return stmt; },
            [](const auto&) -> Stmt {
                fail("expected statement body item variant");
                std::unreachable();
            });
    }

    template <class Self>
    [[nodiscard]] Decl extractDeclNode(
        this Self& self, const BlockItem& blockItem)
    {
        return MATCH(blockItem) WITH([](const Decl& decl) { return decl; },
            [](const auto&) -> Decl {
                fail("expected declaration body item variant");
                std::unreachable();
            });
    }

    template <class Self>
    [[nodiscard]] Ptr<ReturnStmt> extractReturnStmt(
        this Self& self, const Stmt& stmt)
    {
        const auto returnStmt = MATCH(stmt)
            WITH([](const Ref<ReturnStmt>& returnStmt) {
                return returnStmt.ptr();
            },
                [](const auto&) { return Ptr<ReturnStmt> { }; }, );
        require(returnStmt, "expected return statement variant");
        return returnStmt;
    }

    template <class Self>
    [[nodiscard]] Ptr<ReturnStmt> extractReturnStmt(
        this Self& self, const BlockItem& blockItemNode_nn)
    {
        return self.extractReturnStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Ptr<IfStmt> extractIfStmt(this Self& self, const Stmt& stmt)
    {
        const auto ifStmt
            = MATCH(stmt) WITH([](const Ref<IfStmt>& ifStmt) {
                return ifStmt.ptr();
            },
                [](const auto&) { return Ptr<IfStmt> { }; }, );
        require(ifStmt != nullptr, "expected if statement variant");
        return ifStmt;
    }

    template <class Self>
    [[nodiscard]] Ptr<IfStmt> extractIfStmt(
        this Self& self, const BlockItem& blockItemNode_nn)
    {
        return self.extractIfStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Ptr<WhileStmt> extractWhileStmt(
        this Self& self, const Stmt& stmt)
    {
        const auto whileStmt = MATCH(stmt)
            WITH([](const Ref<WhileStmt>& whileStmt) {
                return whileStmt.ptr();
            },
                [](const auto&) { return Ptr<WhileStmt> { }; }, );
        require(whileStmt != nullptr, "expected while statement variant");
        return whileStmt;
    }

    template <class Self>
    [[nodiscard]] Ptr<WhileStmt> extractWhileStmt(
        this Self& self, const BlockItem& blockItemNode_nn)
    {
        return self.extractWhileStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Ptr<BreakStmt> extractBreakStmt(
        this Self& self, const Stmt& stmt)
    {
        const auto breakStmt = MATCH(stmt)
            WITH([](const Ref<BreakStmt>& breakStmt) {
                return breakStmt.ptr();
            },
                [](const auto&) { return Ptr<BreakStmt> { }; }, );
        require(breakStmt != nullptr, "expected break statement variant");
        return breakStmt;
    }

    template <class Self>
    [[nodiscard]] Ptr<ContinueStmt> extractContinueStmt(
        this Self& self, const Stmt& stmt)
    {
        const auto continueStmt = MATCH(stmt) WITH(
            [](const Ref<ContinueStmt>& continueStmt) {
                return continueStmt.ptr();
            },
            [](const auto&) { return Ptr<ContinueStmt> { }; }, );
        require(continueStmt != nullptr, "expected continue statement variant");
        return continueStmt;
    }

    template <class Self>
    [[nodiscard]] Ptr<AssignStmt> extractAssignStmt(
        this Self& self, const Stmt& stmt)
    {
        const auto assignStmt = MATCH(stmt)
            WITH([](const Ref<AssignStmt>& assignStmt) {
                return assignStmt.ptr();
            },
                [](const auto&) { return Ptr<AssignStmt> { }; }, );
        require(assignStmt != nullptr, "expected assignment statement variant");
        return assignStmt;
    }

    template <class Self>
    [[nodiscard]] Ptr<AssignStmt> extractAssignStmt(
        this Self& self, const BlockItem& blockItemNode_nn)
    {
        return self.extractAssignStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Ptr<ExpStmt> extractExpStmt(this Self& self, const Stmt& stmt)
    {
        const auto expStmt = MATCH(stmt)
            WITH([](const Ref<ExpStmt>& expStmt) {
                return expStmt.ptr();
            },
                [](const auto&) { return Ptr<ExpStmt> { }; }, );
        require(expStmt != nullptr, "expected expression statement variant");
        return expStmt;
    }

    template <class Self>
    [[nodiscard]] Ptr<ExpStmt> extractExpStmt(
        this Self& self, const BlockItem& blockItemNode_nn)
    {
        return self.extractExpStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Ptr<Block> extractBlockStmt(this Self& self, const Stmt& stmt)
    {
        const auto body
            = MATCH(stmt) WITH([](const Ref<Block>& body) {
                  return body.ptr();
              },
                [](const auto&) { return Ptr<Block> { }; }, );
        require(body != nullptr, "expected body statement variant");
        return body;
    }

    template <class Self>
    [[nodiscard]] Ptr<Block> extractBlockStmt(
        this Self& self, const BlockItem& blockItemNode_nn)
    {
        return self.extractBlockStmt(self.extractStmtNode(blockItemNode_nn));
    }

    template <class Self>
    [[nodiscard]] Ptr<ConstDecl> extractConstDecl(
        this Self& self, const Decl& decl)
    {
        const auto constDecl = MATCH(decl)
            WITH([](const Ref<ConstDecl>& constDecl) {
                return constDecl.ptr();
            },
                [](const auto&) { return Ptr<ConstDecl> { }; }, );
        require(constDecl, "expected const declaration variant");
        return constDecl;
    }

    template <class Self>
    [[nodiscard]] Ptr<VarDecl> extractVarDecl(this Self& self, const Decl& decl)
    {
        const auto varDecl = MATCH(decl)
            WITH([](const Ref<VarDecl>& varDecl) {
                return varDecl.ptr();
            },
                [](const auto&) { return Ptr<VarDecl> { }; }, );
        require(varDecl, "expected var declaration variant");
        return varDecl;
    }

    template <class Self>
    [[nodiscard]] int32_t evaluateExp(this Self& self, const Ref<Exp>& exp)
    {
        return MATCH(exp(self.ast()).kind)
            WITH([](const Number& number) -> int32_t { return number.value; },
                [](const LVal&) -> int32_t {
                    fail("cannot evaluate lvalue expression");
                },
                [](const Exp::Call&) -> int32_t {
                    fail("cannot evaluate call expression");
                },
                [&](const Exp::Unary& unary) -> int32_t {
                    const auto value = self.evaluateExp(unary.lhs);
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
                    const auto lhsValue = self.evaluateExp(binary.lhs);
                    const auto rhsValue = self.evaluateExp(binary.rhs);
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
                }, );
    }

    template <class Self>
    [[nodiscard]] Ref<Exp> requireScalarConstInitExp(
        this Self& self, const Ptr<ConstInitVal>& initVal_nn)
    {
        return MATCH(initVal_nn(self.ast()).kind)
            WITH([](const Ref<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) -> Ref<Exp> {
                    fail("expected scalar const initializer expression");
                    std::unreachable();
                });
    }

    template <class Self>
    [[nodiscard]] Ref<Exp> requireScalarInitExp(
        this Self& self, const Ptr<InitVal>& initVal_nn)
    {
        return MATCH(initVal_nn(self.ast()).kind)
            WITH([](const Ref<Exp>& exp_nn) { return exp_nn; },
                [](const auto&) -> Ref<Exp> {
                    fail("expected scalar initializer expression");
                    std::unreachable();
                });
    }

    template <class Self>
    [[nodiscard]] const ConstInitVal::List& requireConstInitList(
        this Self& self, const Ptr<ConstInitVal>& initVal_nn)
    {
        return *MATCH(initVal_nn(self.ast()).kind)
            WITH([](const ConstInitVal::List& list) { return &list; },
                [](const auto&) { fail("expected brace initializer list"); });
    }

    template <class Self>
    [[nodiscard]] const InitVal::List& requireInitList(
        this Self& self, const Ptr<InitVal>& initVal_nn)
    {
        const auto list = MATCH(initVal_nn(self.ast()).kind)
            WITH([](const InitVal::List& list) { return &list; },
                [](const auto&) {
                    return static_cast<const InitVal::List*>(nullptr);
                }, );
        require(list != nullptr, "expected brace initializer list");
        return *list;
    }

    template <class Self>
    [[nodiscard]] Decl requireTopLevelDecl(
        this Self& self, const CompUnit::Item& topLevelItem)
    {
        return MATCH(topLevelItem)
            WITH([](const Decl& declNode) { return declNode; },
                [](const auto&) -> Decl {
                    fail("expected top-level declaration");
                    std::unreachable();
                });
    }

    template <class Self>
    [[nodiscard]] Ptr<FuncDef> requireTopLevelFunc(
        this Self& self, const CompUnit::Item& topLevelItem)
    {
        return MATCH(topLevelItem)
            WITH([](const Ref<FuncDef>& funcDef_nn) {
                return funcDef_nn.ptr();
            },
                [](const auto&) {
                    fail("expected top-level function definition");
                    std::unreachable();
                });
    }

    template <class Self>
    [[nodiscard]] Ptr<FuncDef> requireFuncDefByName(this Self& self,
        const Ptr<CompUnit>& compUnit_nn, const std::string& expectedName)
    {
        require(compUnit_nn != nullptr, "expected compilation unit node");
        for (const auto topLevelItem : compUnit_nn(self.ast()).topLevelItems) {
            const auto funcDef_nn = MATCH(topLevelItem)
                WITH([](const Ref<FuncDef>& funcDef_nn) {
                    return funcDef_nn.ptr();
                },
                    [](const auto&) { return Ptr<FuncDef> { }; }, );
            if (funcDef_nn != nullptr
                && funcDef_nn(self.ast()).identifier(self.ast()).name
                    == expectedName) {
                return funcDef_nn;
            }
        }
        fail("expected function definition named '" + expectedName + "'");
    }

    template <class Self>
    [[nodiscard]] const Exp::Call& requireCallExp(
        this Self& self, const Ref<Exp>& exp_nn)
    {
        const auto callExp = MATCH(exp_nn(self.ast()).kind)
            WITH([](const Exp::Call& callExp) { return &callExp; },
                [](const auto&) {
                    return static_cast<const Exp::Call*>(nullptr);
                }, );
        require(callExp != nullptr, "expected call expression root");
        return *callExp;
    }
};

struct TestBase : AstTestHelperBase { };

inline Ref<FuncDef> firstFuncDef(const Ptr<CompUnit>& compUnit_nn)
{
    require(compUnit_nn != nullptr, "expected compilation unit node");
    for (const auto topLevelItem : compUnit_nn(currentAst()).topLevelItems) {
        const auto funcDef = MATCH(topLevelItem)
            WITH([](const Ref<FuncDef>& funcDef_nn) {
                return funcDef_nn.ptr();
            },
                [](const auto&) { return Ptr<FuncDef> { }; });
        if (funcDef)
            return funcDef.ref();
    }
    fail("expected at least one function definition in compilation unit");
}

} // namespace yesod::test_support

#endif