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
 *  Helper class for std::visit
 *  Example usage (WTF?):
 *  match(..., with {
 *      [](int i){ std::print("int = {}\n", i); },
 *      [](std::string_view s){ std::println("string = “{}”", s); },
 *      [](const Base&){ std::println("base"); }
 *  });
*/
template<class... Ts>
struct with : Ts... { using Ts::operator()...; };
template <class T, class Visitor>
decltype(auto) match(T&& variant, Visitor&& visitor)
{
    return std::visit(std::forward<Visitor>(visitor), std::forward<T>(variant));
}

struct SourcePos {
    constexpr SourcePos() = default;

    explicit constexpr SourcePos(int32_t offset)
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
struct LVal;
struct Number;
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
    static constexpr std::string_view name = "identifier";
    Identifier() = default;

    Identifier(int32_t startOffset, std::string name)
        : m_sourcePos(startOffset)
        , m_name(std::move(name))
    {
    }

    Identifier(SourcePos sourcePos, std::string name)
        : m_sourcePos(sourcePos)
        , m_name(std::move(name))
    {
    }

    SourcePos m_sourcePos;
    std::string m_name;
};

struct Number {
    static constexpr std::string_view name = "number";
    Number() = default;

    explicit Number(int32_t value)
        : m_value(value)
    {
    }

    explicit Number(SourcePos, int32_t value)
        : m_value(value)
    {
    }

    int32_t m_value = 0;
};

struct LVal {
    static constexpr std::string_view name = "lvalue";
    LVal() = default;

    explicit LVal(Handle<Identifier> identifier_nn,
        std::vector<Handle<Exp>> indices = {})
        : m_identifier_nn(identifier_nn)
        , m_indices(std::move(indices))
    {
    }

    explicit LVal(SourcePos, Handle<Identifier> identifier_nn,
        std::vector<Handle<Exp>> indices = {})
        : m_identifier_nn(identifier_nn)
        , m_indices(std::move(indices))
    {
    }

    Handle<Identifier> m_identifier_nn;
    std::vector<Handle<Exp>> m_indices;
};

struct Exp {
    static constexpr std::string_view name = "expression";
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

    using Kind = std::variant<Binary, Unary, Call, LVal, Number>;

    Exp()
        : m_kind(Number {})
    {
    }

    Exp(int32_t startOffset, Kind kind)
        : m_sourcePos(startOffset)
        , m_kind(std::move(kind))
    {
    }

    Exp(SourcePos sourcePos, Kind kind)
        : m_sourcePos(sourcePos)
        , m_kind(std::move(kind))
    {
    }

    SourcePos m_sourcePos;
    Kind m_kind;
};

struct ConstInitVal {
    static constexpr std::string_view name = "constant init value";
    struct List {
        std::vector<Handle<ConstInitVal>> m_values;
    };

    using Kind = std::variant<Handle<Exp>, List>;

    ConstInitVal() = default;

    ConstInitVal(int32_t startOffset, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_kind(exp_nn)
    {
    }

    ConstInitVal(SourcePos sourcePos, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_kind(exp_nn)
    {
    }

    ConstInitVal(int32_t startOffset, List list)
        : m_sourcePos(startOffset)
        , m_kind(std::move(list))
    {
    }

    ConstInitVal(SourcePos sourcePos, List list)
        : m_sourcePos(sourcePos)
        , m_kind(std::move(list))
    {
    }

    SourcePos m_sourcePos;
    Kind m_kind;
};

struct InitVal {
    static constexpr std::string_view name = "init value";
    struct List {
        std::vector<Handle<InitVal>> m_values;
    };

    using Kind = std::variant<Handle<Exp>, List>;

    InitVal() = default;

    InitVal(int32_t startOffset, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_kind(exp_nn)
    {
    }

    InitVal(SourcePos sourcePos, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_kind(exp_nn)
    {
    }

    InitVal(int32_t startOffset, List list)
        : m_sourcePos(startOffset)
        , m_kind(std::move(list))
    {
    }

    InitVal(SourcePos sourcePos, List list)
        : m_sourcePos(sourcePos)
        , m_kind(std::move(list))
    {
    }

    SourcePos m_sourcePos;
    Kind m_kind;
};

struct ConstDef {
    static constexpr std::string_view name = "constant definition";
    ConstDef() = default;
    ConstDef(int32_t startOffset, Handle<Identifier> identifier_nn,
        std::vector<Handle<Exp>> dimensions,
        Handle<ConstInitVal> constInitVal_nn)
        : m_sourcePos(startOffset)
        , m_identifier_nn(identifier_nn)
        , m_dimensions(std::move(dimensions))
        , m_constInitVal_nn(constInitVal_nn)
    {
    }

    ConstDef(SourcePos sourcePos, Handle<Identifier> identifier_nn,
        std::vector<Handle<Exp>> dimensions,
        Handle<ConstInitVal> constInitVal_nn)
        : m_sourcePos(sourcePos)
        , m_identifier_nn(identifier_nn)
        , m_dimensions(std::move(dimensions))
        , m_constInitVal_nn(constInitVal_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Identifier> m_identifier_nn;
    std::vector<Handle<Exp>> m_dimensions;
    Handle<ConstInitVal> m_constInitVal_nn;
};

struct VarDef {
    static constexpr std::string_view name = "variable definition";
    VarDef() = default;

    VarDef(int32_t startOffset, Handle<Identifier> identifier_nn,
        std::vector<Handle<Exp>> dimensions, Handle<InitVal> initVal_nn)
        : m_sourcePos(startOffset)
        , m_identifier_nn(identifier_nn)
        , m_dimensions(std::move(dimensions))
        , m_initVal_nn(initVal_nn)
    {
    }

    VarDef(SourcePos sourcePos, Handle<Identifier> identifier_nn,
        std::vector<Handle<Exp>> dimensions, Handle<InitVal> initVal_nn)
        : m_sourcePos(sourcePos)
        , m_identifier_nn(identifier_nn)
        , m_dimensions(std::move(dimensions))
        , m_initVal_nn(initVal_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Identifier> m_identifier_nn;
    std::vector<Handle<Exp>> m_dimensions;
    Handle<InitVal> m_initVal_nn;
};

struct ConstDecl {
    static constexpr std::string_view name = "constant declaration";
    ConstDecl() = default;

    ConstDecl(int32_t startOffset, BTypeKeyword bType,
        std::vector<Handle<ConstDef>> constDefs)
        : m_sourcePos(startOffset)
        , m_bType(bType)
        , m_constDefs(std::move(constDefs))
    {
    }

    ConstDecl(SourcePos sourcePos, BTypeKeyword bType,
        std::vector<Handle<ConstDef>> constDefs)
        : m_sourcePos(sourcePos)
        , m_bType(bType)
        , m_constDefs(std::move(constDefs))
    {
    }

    SourcePos m_sourcePos;
    BTypeKeyword m_bType = BTypeKeyword::intKeyword;
    std::vector<Handle<ConstDef>> m_constDefs;
};

struct VarDecl {
    static constexpr std::string_view name = "variable declaration";
    VarDecl() = default;

    VarDecl(int32_t startOffset, BTypeKeyword bType,
        std::vector<Handle<VarDef>> varDefs)
        : m_sourcePos(startOffset)
        , m_bType(bType)
        , m_varDefs(std::move(varDefs))
    {
    }

    VarDecl(SourcePos sourcePos, BTypeKeyword bType,
        std::vector<Handle<VarDef>> varDefs)
        : m_sourcePos(sourcePos)
        , m_bType(bType)
        , m_varDefs(std::move(varDefs))
    {
    }

    SourcePos m_sourcePos;
    BTypeKeyword m_bType = BTypeKeyword::intKeyword;
    std::vector<Handle<VarDef>> m_varDefs;
};

using Decl = std::variant<Handle<ConstDecl>, Handle<VarDecl>>;

struct DeclNode {
    static constexpr std::string_view name = "declaration";
    DeclNode() = default;

    DeclNode(int32_t startOffset, Decl decl)
        : m_sourcePos(startOffset)
        , m_decl(std::move(decl))
    {
    }

    DeclNode(SourcePos sourcePos, Decl decl)
        : m_sourcePos(sourcePos)
        , m_decl(std::move(decl))
    {
    }

    SourcePos m_sourcePos;
    Decl m_decl;
};

struct IfStmt {
    static constexpr std::string_view name = "if statement";
    IfStmt() = default;

    IfStmt(int32_t startOffset, Handle<Exp> condExp_nn,
        Handle<StmtNode> thenStmt_nn, Handle<StmtNode> elseStmt_nn)
        : m_sourcePos(startOffset)
        , m_condExp_nn(condExp_nn)
        , m_thenStmt_nn(thenStmt_nn)
        , m_elseStmt_nn(elseStmt_nn)
    {
    }

    IfStmt(SourcePos sourcePos, Handle<Exp> condExp_nn,
        Handle<StmtNode> thenStmt_nn, Handle<StmtNode> elseStmt_nn)
        : m_sourcePos(sourcePos)
        , m_condExp_nn(condExp_nn)
        , m_thenStmt_nn(thenStmt_nn)
        , m_elseStmt_nn(elseStmt_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_condExp_nn;
    Handle<StmtNode> m_thenStmt_nn;
    Handle<StmtNode> m_elseStmt_nn;
};

struct WhileStmt {
    static constexpr std::string_view name = "while statement";
    WhileStmt() = default;

    WhileStmt(int32_t startOffset, Handle<Exp> condExp_nn,
        Handle<StmtNode> bodyStmt_nn)
        : m_sourcePos(startOffset)
        , m_condExp_nn(condExp_nn)
        , m_bodyStmt_nn(bodyStmt_nn)
    {
    }

    WhileStmt(SourcePos sourcePos, Handle<Exp> condExp_nn,
        Handle<StmtNode> bodyStmt_nn)
        : m_sourcePos(sourcePos)
        , m_condExp_nn(condExp_nn)
        , m_bodyStmt_nn(bodyStmt_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_condExp_nn;
    Handle<StmtNode> m_bodyStmt_nn;
};

struct BreakStmt {
    static constexpr std::string_view name = "break statement";
    BreakStmt() = default;

    explicit BreakStmt(int32_t startOffset)
        : m_sourcePos(startOffset)
    {
    }

    explicit BreakStmt(SourcePos sourcePos)
        : m_sourcePos(sourcePos)
    {
    }

    SourcePos m_sourcePos;
};

struct ContinueStmt {
    static constexpr std::string_view name = "continue statement";
    ContinueStmt() = default;

    explicit ContinueStmt(int32_t startOffset)
        : m_sourcePos(startOffset)
    {
    }

    explicit ContinueStmt(SourcePos sourcePos)
        : m_sourcePos(sourcePos)
    {
    }

    SourcePos m_sourcePos;
};

struct AssignStmt {
    static constexpr std::string_view name = "assignment statement";
    AssignStmt() = default;

    AssignStmt(int32_t startOffset, Handle<Exp> lVal_nn, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_lVal_nn(lVal_nn)
        , m_exp_nn(exp_nn)
    {
    }

    AssignStmt(SourcePos sourcePos, Handle<Exp> lVal_nn, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_lVal_nn(lVal_nn)
        , m_exp_nn(exp_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_lVal_nn;
    Handle<Exp> m_exp_nn;
};

struct ExpStmt {
    static constexpr std::string_view name = "expression statement";
    ExpStmt() = default;

    ExpStmt(int32_t startOffset, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_exp_nn(exp_nn)
    {
    }

    ExpStmt(SourcePos sourcePos, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_exp_nn(exp_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_exp_nn;
};

struct ReturnStmt {
    static constexpr std::string_view name = "return statement";
    ReturnStmt() = default;

    ReturnStmt(int32_t startOffset, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_exp_nn(exp_nn)
    {
    }

    ReturnStmt(SourcePos sourcePos, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_exp_nn(exp_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_exp_nn;
};

using Stmt = std::variant<Handle<IfStmt>, Handle<WhileStmt>, Handle<BreakStmt>,
    Handle<ContinueStmt>, Handle<AssignStmt>, Handle<Block>, Handle<ReturnStmt>,
    Handle<ExpStmt>>;

struct StmtNode {
    static constexpr std::string_view name = "statement";
    StmtNode() = default;

    StmtNode(int32_t startOffset, Stmt stmt)
        : m_sourcePos(startOffset)
        , m_stmt(std::move(stmt))
    {
    }

    StmtNode(SourcePos sourcePos, Stmt stmt)
        : m_sourcePos(sourcePos)
        , m_stmt(std::move(stmt))
    {
    }

    SourcePos m_sourcePos;
    Stmt m_stmt;
};

using BlockItem = std::variant<Handle<DeclNode>, Handle<StmtNode>>;

struct BlockItemNode {
    static constexpr std::string_view name = "block item";
    BlockItemNode() = default;

    BlockItemNode(int32_t startOffset, BlockItem blockItem)
        : m_sourcePos(startOffset)
        , m_blockItem(std::move(blockItem))
    {
    }

    BlockItemNode(SourcePos sourcePos, BlockItem blockItem)
        : m_sourcePos(sourcePos)
        , m_blockItem(std::move(blockItem))
    {
    }

    SourcePos m_sourcePos;
    BlockItem m_blockItem;
};

struct Block {
    static constexpr std::string_view name = "block";
    Block() = default;

    Block(int32_t startOffset, std::vector<Handle<BlockItemNode>> blockItems)
        : m_sourcePos(startOffset)
        , m_blockItems(std::move(blockItems))
    {
    }

    Block(SourcePos sourcePos, std::vector<Handle<BlockItemNode>> blockItems)
        : m_sourcePos(sourcePos)
        , m_blockItems(std::move(blockItems))
    {
    }

    SourcePos m_sourcePos;
    std::vector<Handle<BlockItemNode>> m_blockItems;
};

struct FuncFParam {
    static constexpr std::string_view name = "function parameter";
    FuncFParam() = default;

    FuncFParam(int32_t startOffset, BTypeKeyword bType,
        Handle<Identifier> identifier_nn, bool isArray,
        std::vector<Handle<Exp>> trailingDimensions)
        : m_sourcePos(startOffset)
        , m_bType(bType)
        , m_identifier_nn(identifier_nn)
        , m_isArray(isArray)
        , m_trailingDimensions(std::move(trailingDimensions))
    {
    }

    FuncFParam(SourcePos sourcePos, BTypeKeyword bType,
        Handle<Identifier> identifier_nn, bool isArray,
        std::vector<Handle<Exp>> trailingDimensions)
        : m_sourcePos(sourcePos)
        , m_bType(bType)
        , m_identifier_nn(identifier_nn)
        , m_isArray(isArray)
        , m_trailingDimensions(std::move(trailingDimensions))
    {
    }

    SourcePos m_sourcePos;
    BTypeKeyword m_bType = BTypeKeyword::intKeyword;
    Handle<Identifier> m_identifier_nn;
    bool m_isArray = false;
    std::vector<Handle<Exp>> m_trailingDimensions;
};

struct FuncDef {
    static constexpr std::string_view name = "function definition";
    FuncDef() = default;

    FuncDef(int32_t startOffset, FuncTypeKeyword funcType,
        Handle<Identifier> identifier_nn,
        std::vector<Handle<FuncFParam>> funcFParams, Handle<Block> block_nn)
        : m_sourcePos(startOffset)
        , m_funcType(funcType)
        , m_identifier_nn(identifier_nn)
        , m_funcFParams(std::move(funcFParams))
        , m_block_nn(block_nn)
    {
    }

    FuncDef(SourcePos sourcePos, FuncTypeKeyword funcType,
        Handle<Identifier> identifier_nn,
        std::vector<Handle<FuncFParam>> funcFParams, Handle<Block> block_nn)
        : m_sourcePos(sourcePos)
        , m_funcType(funcType)
        , m_identifier_nn(identifier_nn)
        , m_funcFParams(std::move(funcFParams))
        , m_block_nn(block_nn)
    {
    }

    SourcePos m_sourcePos;
    FuncTypeKeyword m_funcType = FuncTypeKeyword::intKeyword;
    Handle<Identifier> m_identifier_nn;
    std::vector<Handle<FuncFParam>> m_funcFParams;
    Handle<Block> m_block_nn;
};

using TopLevelItem = std::variant<Handle<DeclNode>, Handle<FuncDef>>;

struct TopLevelItemNode {
    static constexpr std::string_view name = "top level item";
    TopLevelItemNode() = default;

    TopLevelItemNode(int32_t startOffset, TopLevelItem topLevelItem)
        : m_sourcePos(startOffset)
        , m_topLevelItem(std::move(topLevelItem))
    {
    }

    TopLevelItemNode(SourcePos sourcePos, TopLevelItem topLevelItem)
        : m_sourcePos(sourcePos)
        , m_topLevelItem(std::move(topLevelItem))
    {
    }

    SourcePos m_sourcePos;
    TopLevelItem m_topLevelItem;
};

struct CompUnit {
    static constexpr std::string_view name = "AST root";
    CompUnit() = default;

    CompUnit(int32_t startOffset,
        std::vector<Handle<TopLevelItemNode>> topLevelItems)
        : m_sourcePos(startOffset)
        , m_topLevelItems(std::move(topLevelItems))
    {
    }

    CompUnit(SourcePos sourcePos,
        std::vector<Handle<TopLevelItemNode>> topLevelItems)
        : m_sourcePos(sourcePos)
        , m_topLevelItems(std::move(topLevelItems))
    {
    }

    SourcePos m_sourcePos;
    std::vector<Handle<TopLevelItemNode>> m_topLevelItems;
};

class AST: public Arena<Identifier, Exp,
        ConstInitVal, InitVal, ConstDef, VarDef,
        ConstDecl, VarDecl, DeclNode, FuncFParam,
        IfStmt, WhileStmt, BreakStmt,
        ContinueStmt, AssignStmt, ExpStmt,
        ReturnStmt, StmtNode, BlockItemNode, Block,
        FuncDef, TopLevelItemNode, CompUnit> {
  public:

};

} // namespace yesod::frontend

#endif