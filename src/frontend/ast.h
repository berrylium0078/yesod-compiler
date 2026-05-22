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
struct DeclNode;
struct FuncFParam;
struct IfStmt;
struct WhileStmt;
struct BreakStmt;
struct ContinueStmt;
struct AssignStmt;
struct ExpStmt;
struct ReturnStmt;
struct StmtNode;
struct BlockItemNode;
struct Block;
struct TopLevelItemNode;

struct Identifier {
    SourcePos m_sourcePos;
    std::string m_name;
};
struct Exp {
    struct Binary {
        Handle<Exp> m_lhs_nn;
        Handle<Exp> m_rhs_nn;
        BinaryOpKeyword m_op = BinaryOpKeyword::plus;
    };

    struct Unary {
        Handle<Exp> m_lhs_nn;
        UnaryOpKeyword m_op = UnaryOpKeyword::plus;
    };

    struct Call {
        Handle<Identifier> m_func_nn;
        std::vector<Handle<Exp>> m_params;
    };
    struct Number {
        int32_t m_value;
    };
    struct LVal {
        Handle<Identifier> m_identifier_nn;
        std::vector<Handle<Exp>> m_indices;
    };

    using Kind = std::variant<Binary, Unary, Call, LVal, Number>;

    SourcePos m_sourcePos;
    Kind m_kind;
};

struct ConstInitVal {
    using List = std::vector<Handle<ConstInitVal>>;
    using Kind = std::variant<Handle<Exp>, List>;
    SourcePos m_sourcePos;
    Kind m_kind;
};

struct InitVal {
    using List = std::vector<Handle<InitVal>>;
    using Kind = std::variant<Handle<Exp>, List>;
    SourcePos m_sourcePos;
    Kind m_kind;
};

struct ConstDef {
    SourcePos m_sourcePos;
    Handle<Identifier> m_identifier_nn;
    std::vector<Handle<Exp>> m_dimensions;
    Handle<ConstInitVal> m_constInitVal_nn;
};

struct VarDef {
    SourcePos m_sourcePos;
    Handle<Identifier> m_identifier_nn;
    std::vector<Handle<Exp>> m_dimensions;
    Handle<InitVal> m_initVal_nn;
};

struct ConstDecl {
    SourcePos m_sourcePos;
    BTypeKeyword m_bType = BTypeKeyword::intKeyword;
    std::vector<Handle<ConstDef>> m_constDefs;
};

struct VarDecl {
    SourcePos m_sourcePos;
    BTypeKeyword m_bType = BTypeKeyword::intKeyword;
    std::vector<Handle<VarDef>> m_varDefs;
};

using Decl = std::variant<Handle<ConstDecl>, Handle<VarDecl>>;

struct DeclNode {
    SourcePos m_sourcePos;
    Decl m_decl;
};

struct IfStmt {
    SourcePos m_sourcePos;
    Handle<Exp> m_condExp_nn;
    Handle<StmtNode> m_thenStmt_nn;
    Handle<StmtNode> m_elseStmt_nn;
};

struct WhileStmt {
    SourcePos m_sourcePos;
    Handle<Exp> m_condExp_nn;
    Handle<StmtNode> m_bodyStmt_nn;
};

struct BreakStmt {
    SourcePos m_sourcePos;
};

struct ContinueStmt {
    SourcePos m_sourcePos;
};

struct AssignStmt {
    SourcePos m_sourcePos;
    Handle<Exp> m_lVal_nn;
    Handle<Exp> m_exp_nn;
};

struct ExpStmt {
    SourcePos m_sourcePos;
    Handle<Exp> m_exp_nn;
};

struct ReturnStmt {
    SourcePos m_sourcePos;
    Handle<Exp> m_exp_nn;
};

using Stmt = std::variant<Handle<IfStmt>, Handle<WhileStmt>, Handle<BreakStmt>,
    Handle<ContinueStmt>, Handle<AssignStmt>, Handle<Block>, Handle<ReturnStmt>,
    Handle<ExpStmt>>;

struct StmtNode {
    SourcePos m_sourcePos;
    Stmt m_stmt;
};

using BlockItem = std::variant<Handle<DeclNode>, Handle<StmtNode>>;

struct BlockItemNode {
    SourcePos m_sourcePos;
    BlockItem m_blockItem;
};

struct Block {
    SourcePos m_sourcePos;
    std::vector<Handle<BlockItemNode>> m_blockItems;
};

struct FuncFParam {
    SourcePos m_sourcePos;
    BTypeKeyword m_bType = BTypeKeyword::intKeyword;
    Handle<Identifier> m_identifier_nn;
    bool m_isArray = false;
    std::vector<Handle<Exp>> m_trailingDimensions;
};

struct FuncDef {
    SourcePos m_sourcePos;
    FuncTypeKeyword m_funcType = FuncTypeKeyword::intKeyword;
    Handle<Identifier> m_identifier_nn;
    std::vector<Handle<FuncFParam>> m_funcFParams;
    Handle<Block> m_block_nn;
};

using TopLevelItem = std::variant<Handle<DeclNode>, Handle<FuncDef>>;

struct TopLevelItemNode {
    SourcePos m_sourcePos;
    TopLevelItem m_topLevelItem;
};

struct CompUnit {
    SourcePos m_sourcePos;
    std::vector<Handle<TopLevelItemNode>> m_topLevelItems;
};

using AST = Arena<Identifier, Exp,
        ConstInitVal, InitVal, ConstDef, VarDef,
        ConstDecl, VarDecl, DeclNode, FuncFParam,
        IfStmt, WhileStmt, BreakStmt,
        ContinueStmt, AssignStmt, ExpStmt,
        ReturnStmt, StmtNode, BlockItemNode, Block,
        FuncDef, TopLevelItemNode, CompUnit>;

} // namespace yesod::frontend

#endif