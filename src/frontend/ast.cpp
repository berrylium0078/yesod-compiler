#include "frontend/ast.h"
#include "utils.h"

namespace yesod::frontend {

AstVisitor::AstVisitor(const AST& ast)
    : m_ast(ast)
{
}

void AstVisitor::traverse(Ref<CompUnit> compUnit)
{
    if (compUnit) {
        visitCompUnit(compUnit);
    }
}

void AstVisitor::visitCompUnit(Ref<CompUnit> compUnit)
{
    for (const auto topLevelItem : compUnit(m_ast).topLevelItems) {
        MATCH(topLevelItem)
        WITH([&](Decl decl) { visitDecl(decl); },
            [&](Ref<FuncDef> funcDef) { visitFuncDef(funcDef); });
    }
}

void AstVisitor::visitFuncDef(Ref<FuncDef> funcDef)
{
    for (const auto& funcFParam : funcDef(m_ast).funcFParams) {
        visitFuncFParam(funcFParam);
    }
    if (funcDef(m_ast).body != nullptr) {
        visitBlock(funcDef(m_ast).body.ref());
    }
}

void AstVisitor::visitFuncFParam(const FuncFParam& funcFParam)
{
    for (const auto dimension : funcFParam.shape) {
        visitExp(dimension);
    }
}

void AstVisitor::visitBlock(Ref<Block> block)
{
    for (const auto blockItem : block(m_ast).items) {
        visitBlockItem(blockItem);
    }
}

void AstVisitor::visitBlockItem(BlockItem blockItem)
{
    MATCH(blockItem)
    WITH([&](Decl decl) { visitDecl(decl); },
        [&](Stmt stmt) { visitStmt(stmt); });
}

void AstVisitor::visitDecl(Decl decl)
{
    MATCH(decl)
    WITH([&](Ref<ConstDecl> constDecl) { visitConstDecl(constDecl); },
        [&](Ref<VarDecl> varDecl) { visitVarDecl(varDecl); });
}

void AstVisitor::visitConstDecl(Ref<ConstDecl> constDecl)
{
    for (const auto constDef : constDecl(m_ast).constDef) {
        visitConstDef(constDef);
    }
}

void AstVisitor::visitConstDef(Ref<ConstDef> constDef)
{
    const auto& parsedConstDef = constDef(m_ast);
    for (const auto dimension : parsedConstDef.shape) {
        visitExp(dimension);
    }
    if (parsedConstDef.constInitVal != nullptr) {
        visitConstInitVal(parsedConstDef.constInitVal.ref());
    }
}

void AstVisitor::visitConstInitVal(Ref<ConstInitVal> constInitVal)
{
    MATCH(constInitVal(m_ast).kind)
    WITH([&](Ref<Exp> exp) { visitExp(exp); },
        [&](const ConstInitVal::List& values) {
            for (const auto value : values) {
                visitConstInitVal(value);
            }
        });
}

void AstVisitor::visitVarDecl(Ref<VarDecl> varDecl)
{
    for (const auto varDef : varDecl(m_ast).varDef) {
        visitVarDef(varDef);
    }
}

void AstVisitor::visitVarDef(Ref<VarDef> varDef)
{
    const auto& parsedVarDef = varDef(m_ast);
    for (const auto dimension : parsedVarDef.shape) {
        visitExp(dimension);
    }
    if (parsedVarDef.initVal != nullptr)
        visitInitVal(parsedVarDef.initVal.ref());
}

void AstVisitor::visitInitVal(Ref<InitVal> initVal)
{
    MATCH(initVal(m_ast).kind)
    WITH([&](Ref<Exp> exp) { visitExp(exp); },
        [&](const InitVal::List& values) {
            for (const auto value : values) {
                visitInitVal(value);
            }
        });
}

void AstVisitor::visitStmt(Stmt stmt)
{
    MATCH(stmt)
    WITH([&](Ref<IfStmt> ifStmt) { visitIfStmt(ifStmt); },
        [&](Ref<WhileStmt> whileStmt) { visitWhileStmt(whileStmt); },
        [&](Ref<BreakStmt> breakStmt) { visitBreakStmt(breakStmt); },
        [&](Ref<ContinueStmt> continueStmt) {
            visitContinueStmt(continueStmt);
        },
        [&](Ref<AssignStmt> assignStmt) { visitAssignStmt(assignStmt); },
        [&](Ref<Block> block) { visitBlock(block); },
        [&](Ref<ReturnStmt> returnStmt) { visitReturnStmt(returnStmt); },
        [&](Ref<ExpStmt> expStmt) { visitExpStmt(expStmt); });
}

void AstVisitor::visitIfStmt(Ref<IfStmt> ifStmt)
{
    visitExp(ifStmt(m_ast).condition);
    visitStmt(ifStmt(m_ast).thenBody);
    visitStmt(ifStmt(m_ast).elseBody);
}

void AstVisitor::visitWhileStmt(Ref<WhileStmt> whileStmt)
{
    visitExp(whileStmt(m_ast).condition);
    visitStmt(whileStmt(m_ast).body);
}
void AstVisitor::visitBreakStmt(Ref<BreakStmt> breakStmt)
{
    (void)breakStmt;
}
void AstVisitor::visitContinueStmt(Ref<ContinueStmt> continueStmt)
{
    (void)continueStmt;
}

void AstVisitor::visitAssignStmt(Ref<AssignStmt> assignStmt)
{
    visitExp(assignStmt(m_ast).lval);
    visitExp(assignStmt(m_ast).exp);
}

void AstVisitor::visitExpStmt(Ref<ExpStmt> expStmt)
{
    if (expStmt(m_ast).exp != nullptr) {
        visitExp(expStmt(m_ast).exp.ref());
    }
}

void AstVisitor::visitReturnStmt(Ref<ReturnStmt> returnStmt)
{
    if (returnStmt(m_ast).exp != nullptr) {
        visitExp(returnStmt(m_ast).exp.ref());
    }
}

void AstVisitor::visitExp(Ref<Exp> exp_ref)
{
    const auto& exp = exp_ref(m_ast);
    MATCH(exp.kind)
    WITH([&](const Exp::Binary& binary) { visitBinaryExp(exp, binary); },
        [&](const Exp::Unary& unary) { visitUnaryExp(exp, unary); },
    [&](const Exp::Cast& cast) { visitCastExp(exp, cast); },
        [&](const Exp::Call& call) { visitCallExp(exp, call); },
        [&](const Exp::LVal& lVal) { visitLValExp(exp, lVal); },
        [&](const Exp::Number& number) { visitNumberExp(exp, number); });
}

void AstVisitor::visitBinaryExp(const Exp&, const Exp::Binary& binary)
{
    visitExp(binary.lhs);
    visitExp(binary.rhs);
}

void AstVisitor::visitUnaryExp(const Exp&, const Exp::Unary& unary)
{
    visitExp(unary.lhs);
}

void AstVisitor::visitCastExp(const Exp&, const Exp::Cast& cast)
{
    visitExp(cast.value);
}

void AstVisitor::visitCallExp(const Exp&, const Exp::Call& call)
{
    for (const auto param : call.params) {
        visitExp(param);
    }
}

void AstVisitor::visitLValExp(const Exp&, const Exp::LVal& lVal)
{
    for (const auto index : lVal.indices) {
        visitExp(index);
    }
}
void AstVisitor::visitNumberExp(const Exp&, const Exp::Number& number)
{
    (void)number;
}

} // namespace yesod::frontend