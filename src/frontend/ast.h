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

#include "frontend/arena.h"

namespace yesod::frontend {

/** 
 *  Helpers for std::visit
 *  Example usage adapted from cppreference:
 * 
 *  std::variant<int, std::string, Derived>
 *  MATCH (...) WITH (
 *      [](int i){ std::print("int = {}\n", i); },
 *      [](std::string_view s){ std::println("string = “{}”", s); },
 *      [](const Base&){ std::println("base"); }
 *  );
*/

template<class... Ts>
struct overloads : Ts... { using Ts::operator()...; };
#define MATCH(exp) [&,&expr=(exp)]
#define WITH(...) { return std::visit(yesod::frontend::overloads{__VA_ARGS__}, expr); }()

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
};

enum class BTypeKeyword {
    intKeyword,
};

enum class UnaryOpKeyword {
    plus,
    minus,
    bang,
};

enum class BinaryOpKeyword {
    star,
    slash,
    percent,
    plus,
    minus,
    less,
    greater,
    lessEqual,
    greaterEqual,
    equal,
    notEqual,
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
    using Kind = std::variant<Binary, Unary, Call, LVal, Number>;

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
    Ref<ContinueStmt>, Ref<AssignStmt>, Ref<Block>, Ref<ReturnStmt>,
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
    Ref<Block> body;
};
struct CompUnit {
    using Item = std::variant<Decl, Ref<FuncDef>>;
    SourcePos sourcePos;
    std::vector<Item> topLevelItems;
};

using AST = Arena<Identifier, Exp,
        ConstInitVal, InitVal, ConstDef, VarDef,
        ConstDecl, VarDecl, FuncFParam,
        IfStmt, WhileStmt, BreakStmt,
        ContinueStmt, AssignStmt, ExpStmt,
        ReturnStmt, Block,
        FuncDef, CompUnit>;

} // namespace yesod::frontend

#endif