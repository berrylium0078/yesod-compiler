#ifndef _YESOD_FRONTEND_AST_H_
#define _YESOD_FRONTEND_AST_H_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "utils.h"

namespace yesod::frontend {

using yesod::Ref;
using yesod::Ptr;
using yesod::Arena;

struct SourcePos {
    constexpr SourcePos() = default;

    constexpr SourcePos(int32_t offset)
        : m_offset(offset)
    {
    }

    int32_t m_offset = 0;
};

enum class FuncTypeKeyword {
    voidKeyword,
    intKeyword,
    mintKeyword,
    polyKeyword,
};

enum class BTypeKeyword {
    intKeyword,
    mintKeyword,
    polyKeyword,
};

enum class UnaryOpKeyword {
    plus,
    minus,
    bang,
    tilde,
};

enum class BinaryOpKeyword {
    star,
    slash,
    percent,
    plus,
    minus,
    shl,
    sar,
    less,
    greater,
    lessEqual,
    greaterEqual,
    equal,
    notEqual,
    bitAnd,
    bitXor,
    bitOr,
    andAnd,
    orOr,
};

struct Exp;
struct ConstInitVal;
struct InitVal;
struct ConstDef;
struct VarDef;
struct ConstDecl;
struct VarDecl;
struct FuncFParam;
struct IfStmt;
struct WhileStmt;
struct BreakStmt;
struct ContinueStmt;
struct AssignStmt;
struct ExpStmt;
struct ReturnStmt;
struct Block;

struct Identifier {
    SourcePos sourcePos;
    std::string name;
};
struct Exp {
    struct Binary {
        Ref<Exp> lhs;
        Ref<Exp> rhs;
        BinaryOpKeyword op;
    };

    struct Unary {
        Ref<Exp> lhs;
        UnaryOpKeyword op;
    };

    struct Cast {
        BTypeKeyword targetType;
        Ref<Exp> value;
    };

    struct Call {
        Ref<Identifier> funcName;
        std::vector<Ref<Exp>> params;
    };
    struct Number {
        int32_t value;
    };
    struct LVal {
        Ref<Identifier> identifier;
        std::vector<Ref<Exp>> indices;
    };
    struct Slice {
        Ref<Exp> base;
        Ref<Exp> start;
        Ref<Exp> end;
    };
    struct Subscript {
        Ref<Exp> base;
        Ref<Exp> index;
    };
    using Kind = std::variant<Binary, Unary, Cast, Call, Slice, Subscript, LVal, Number>;

    SourcePos sourcePos;
    Kind kind;
};

struct ConstInitVal {
    using List = std::vector<Ref<ConstInitVal>>;
    using Kind = std::variant<Ref<Exp>, List>;
    SourcePos sourcePos;
    Kind kind;
};

struct InitVal {
    using List = std::vector<Ref<InitVal>>;
    using Kind = std::variant<Ref<Exp>, List>;
    SourcePos sourcePos;
    Kind kind;
};

struct ConstDef {
    SourcePos sourcePos;
    Ref<Identifier> identifier;
    std::vector<Ref<Exp>> shape;
    Ptr<ConstInitVal> constInitVal;
};

struct VarDef {
    SourcePos sourcePos;
    Ref<Identifier> identifier;
    std::vector<Ref<Exp>> shape;
    Ptr<InitVal> initVal;
};

struct ConstDecl {
    SourcePos sourcePos;
    BTypeKeyword bType = BTypeKeyword::intKeyword;
    std::vector<Ref<ConstDef>> constDef;
};

struct VarDecl {
    SourcePos sourcePos;
    BTypeKeyword bType = BTypeKeyword::intKeyword;
    std::vector<Ref<VarDef>> varDef;
};

using Decl = std::variant<Ref<ConstDecl>, Ref<VarDecl>>;

using Stmt = std::variant<Ref<IfStmt>, Ref<WhileStmt>, Ref<BreakStmt>,
    Ref<ContinueStmt>, Ref<AssignStmt>, Ref<yesod::frontend::Block>, Ref<ReturnStmt>,
    Ref<ExpStmt>>;

struct IfStmt {
    SourcePos sourcePos;
    Ref<Exp> condition;
    Stmt thenBody;
    Stmt elseBody;
};

struct WhileStmt {
    SourcePos sourcePos;
    Ref<Exp> condition;
    Stmt body;
};

struct BreakStmt {
    SourcePos sourcePos;
};

struct ContinueStmt {
    SourcePos sourcePos;
};

struct AssignStmt {
    SourcePos sourcePos;
    Ref<Exp> lval;
    Ref<Exp> exp;
};

struct ExpStmt {
    SourcePos sourcePos;
    Ptr<Exp> exp;
};

struct ReturnStmt {
    SourcePos sourcePos;
    Ptr<Exp> exp;
};

using BlockItem = std::variant<Decl, Stmt>;
struct Block {
    SourcePos sourcePos;
    std::vector<BlockItem> items;
};

struct FuncFParam {
    SourcePos sourcePos;
    BTypeKeyword bType = BTypeKeyword::intKeyword;
    Ref<Identifier> identifier;
    bool m_isArray = false;
    std::vector<Ref<Exp>> shape;
};

struct FuncDef {
    SourcePos sourcePos;
    FuncTypeKeyword m_funcType = FuncTypeKeyword::intKeyword;
    Ref<Identifier> identifier;
    std::vector<FuncFParam> funcFParams;
    Ptr<yesod::frontend::Block> body;
};
struct CompUnit {
    using Item = std::variant<Decl, Ref<FuncDef>>;
    SourcePos sourcePos;
    std::vector<Item> topLevelItems;
};

using AST = Arena<Identifier, Exp, ConstInitVal, InitVal, ConstDef, VarDef,
    ConstDecl, VarDecl, FuncFParam, IfStmt, WhileStmt, BreakStmt, ContinueStmt,
    AssignStmt, ExpStmt, ReturnStmt, yesod::frontend::Block, FuncDef, CompUnit>;

// basic visitor that traverses the whole AST. Override the visit* methods to
// implement a visitor that does something useful.
class AstVisitor {
public:
    explicit AstVisitor(const AST& ast);
    virtual ~AstVisitor() = default;

    void traverse(Ref<CompUnit> compUnit);

protected:
    const AST& m_ast;

    virtual void visitCompUnit(Ref<CompUnit> compUnit);
    virtual void visitFuncDef(Ref<FuncDef> funcDef);
    virtual void visitFuncFParam(const FuncFParam& funcFParam);
    virtual void visitBlock(Ref<yesod::frontend::Block> block);
    virtual void visitBlockItem(BlockItem blockItem);
    virtual void visitDecl(Decl decl);
    virtual void visitConstDecl(Ref<ConstDecl> constDecl);
    virtual void visitConstDef(Ref<ConstDef> constDef);
    virtual void visitConstInitVal(Ref<ConstInitVal> constInitVal);
    virtual void visitVarDecl(Ref<VarDecl> varDecl);
    virtual void visitVarDef(Ref<VarDef> varDef);
    virtual void visitInitVal(Ref<InitVal> initVal);
    virtual void visitStmt(Stmt stmt);
    virtual void visitIfStmt(Ref<IfStmt> ifStmt);
    virtual void visitWhileStmt(Ref<WhileStmt> whileStmt);
    virtual void visitBreakStmt(Ref<BreakStmt> breakStmt);
    virtual void visitContinueStmt(Ref<ContinueStmt> continueStmt);
    virtual void visitAssignStmt(Ref<AssignStmt> assignStmt);
    virtual void visitExpStmt(Ref<ExpStmt> expStmt);
    virtual void visitReturnStmt(Ref<ReturnStmt> returnStmt);
    virtual void visitExp(Ref<Exp> exp);
    virtual void visitBinaryExp(const Exp& exp, const Exp::Binary& binary);
    virtual void visitUnaryExp(const Exp& exp, const Exp::Unary& unary);
    virtual void visitCastExp(const Exp& exp, const Exp::Cast& cast);
    virtual void visitCallExp(const Exp& exp, const Exp::Call& call);
    virtual void visitSliceExp(const Exp& exp, const Exp::Slice& slice);
    virtual void visitSubscriptExp(const Exp& exp, const Exp::Subscript& subscript);
    virtual void visitLValExp(const Exp& exp, const Exp::LVal& lVal);
    virtual void visitNumberExp(const Exp& exp, const Exp::Number& number);
};

} // namespace yesod::frontend

#endif // _YESOD_FRONTEND_AST_H_