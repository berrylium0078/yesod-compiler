#ifndef _YESOD_FRONTEND_AST_H_
#define _YESOD_FRONTEND_AST_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace yesod::frontend {

struct AstNode {
    explicit AstNode(int32_t startOffset)
        : m_startOffset(startOffset)
    {
    }
    virtual ~AstNode() = default;

    int32_t m_startOffset;
};

enum class FuncTypeKeyword {
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

enum class MulOpKeyword {
    star,
    slash,
    percent,
};

enum class AddOpKeyword {
    plus,
    minus,
};

enum class RelOpKeyword {
    less,
    greater,
    lessEqual,
    greaterEqual,
};

enum class EqOpKeyword {
    equal,
    notEqual,
};

enum class LAndOpKeyword {
    andAnd,
};

enum class LOrOpKeyword {
    orOr,
};

struct Exp;
struct LOrExp;
struct LAndExp;
struct EqExp;
struct RelExp;
struct AddExp;
struct MulExp;
struct PrimaryExp;
struct UnaryExp;
struct LVal;
struct ConstExp;
struct ConstInitVal;
struct InitVal;
struct ConstDef;
struct VarDef;
struct ConstDecl;
struct VarDecl;
struct DeclNode;
struct AssignStmt;
struct ExpStmt;
struct ReturnStmt;
struct StmtNode;
struct BlockItemNode;
struct Block;

struct Identifier final : AstNode {
    Identifier(int32_t startOffset, std::string name)
        : AstNode(startOffset)
        , m_name(std::move(name))
    {
    }

    std::string m_name;
};

struct Number final : AstNode {
    Number(int32_t startOffset, int32_t value)
        : AstNode(startOffset)
        , m_value(value)
    {
    }

    int32_t m_value;
};

struct LVal final : AstNode {
    LVal(int32_t startOffset, std::shared_ptr<Identifier> identifier_nn)
        : AstNode(startOffset)
        , m_identifier_nn(std::move(identifier_nn))
    {
    }

    std::shared_ptr<Identifier> m_identifier_nn;
};

struct PrimaryExp final : AstNode {
    using Kind = std::variant<std::shared_ptr<Exp>, std::shared_ptr<LVal>,
        std::shared_ptr<Number>>;

    PrimaryExp(int32_t startOffset, Kind kind)
        : AstNode(startOffset)
        , m_kind(std::move(kind))
    {
    }

    Kind m_kind;
};

struct UnaryExp final : AstNode {
    using Kind = std::variant<std::shared_ptr<PrimaryExp>,
        std::pair<UnaryOpKeyword, std::shared_ptr<UnaryExp>>>;

    UnaryExp(int32_t startOffset, Kind kind)
        : AstNode(startOffset)
        , m_kind(std::move(kind))
    {
    }

    Kind m_kind;
};

struct MulExp final : AstNode {
    using Tail = std::pair<MulOpKeyword, std::shared_ptr<UnaryExp>>;

    MulExp(int32_t startOffset, std::shared_ptr<UnaryExp> head_nn,
        std::vector<Tail> tail)
        : AstNode(startOffset)
        , m_head_nn(std::move(head_nn))
        , m_tail(std::move(tail))
    {
    }

    std::shared_ptr<UnaryExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct AddExp final : AstNode {
    using Tail = std::pair<AddOpKeyword, std::shared_ptr<MulExp>>;

    AddExp(int32_t startOffset, std::shared_ptr<MulExp> head_nn,
        std::vector<Tail> tail)
        : AstNode(startOffset)
        , m_head_nn(std::move(head_nn))
        , m_tail(std::move(tail))
    {
    }

    std::shared_ptr<MulExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct RelExp final : AstNode {
    using Tail = std::pair<RelOpKeyword, std::shared_ptr<AddExp>>;

    RelExp(int32_t startOffset, std::shared_ptr<AddExp> head_nn,
        std::vector<Tail> tail)
        : AstNode(startOffset)
        , m_head_nn(std::move(head_nn))
        , m_tail(std::move(tail))
    {
    }

    std::shared_ptr<AddExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct EqExp final : AstNode {
    using Tail = std::pair<EqOpKeyword, std::shared_ptr<RelExp>>;

    EqExp(int32_t startOffset, std::shared_ptr<RelExp> head_nn,
        std::vector<Tail> tail)
        : AstNode(startOffset)
        , m_head_nn(std::move(head_nn))
        , m_tail(std::move(tail))
    {
    }

    std::shared_ptr<RelExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct LAndExp final : AstNode {
    using Tail = std::pair<LAndOpKeyword, std::shared_ptr<EqExp>>;

    LAndExp(int32_t startOffset, std::shared_ptr<EqExp> head_nn,
        std::vector<Tail> tail)
        : AstNode(startOffset)
        , m_head_nn(std::move(head_nn))
        , m_tail(std::move(tail))
    {
    }

    std::shared_ptr<EqExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct LOrExp final : AstNode {
    using Tail = std::pair<LOrOpKeyword, std::shared_ptr<LAndExp>>;

    LOrExp(int32_t startOffset, std::shared_ptr<LAndExp> head_nn,
        std::vector<Tail> tail)
        : AstNode(startOffset)
        , m_head_nn(std::move(head_nn))
        , m_tail(std::move(tail))
    {
    }

    std::shared_ptr<LAndExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct Exp final : AstNode {
    Exp(int32_t startOffset, std::shared_ptr<LOrExp> lOrExp_nn)
        : AstNode(startOffset)
        , m_lOrExp_nn(std::move(lOrExp_nn))
    {
    }

    std::shared_ptr<LOrExp> m_lOrExp_nn;
};

struct ConstExp final : AstNode {
    ConstExp(int32_t startOffset, std::shared_ptr<Exp> exp_nn)
        : AstNode(startOffset)
        , m_exp_nn(std::move(exp_nn))
    {
    }

    std::shared_ptr<Exp> m_exp_nn;
};

struct ConstInitVal final : AstNode {
    ConstInitVal(int32_t startOffset, std::shared_ptr<ConstExp> constExp_nn)
        : AstNode(startOffset)
        , m_constExp_nn(std::move(constExp_nn))
    {
    }

    std::shared_ptr<ConstExp> m_constExp_nn;
};

struct InitVal final : AstNode {
    InitVal(int32_t startOffset, std::shared_ptr<Exp> exp_nn)
        : AstNode(startOffset)
        , m_exp_nn(std::move(exp_nn))
    {
    }

    std::shared_ptr<Exp> m_exp_nn;
};

struct ConstDef final : AstNode {
    ConstDef(int32_t startOffset, std::shared_ptr<Identifier> identifier_nn,
        std::shared_ptr<ConstInitVal> constInitVal_nn)
        : AstNode(startOffset)
        , m_identifier_nn(std::move(identifier_nn))
        , m_constInitVal_nn(std::move(constInitVal_nn))
    {
    }

    std::shared_ptr<Identifier> m_identifier_nn;
    std::shared_ptr<ConstInitVal> m_constInitVal_nn;
};

struct VarDef final : AstNode {
    VarDef(int32_t startOffset, std::shared_ptr<Identifier> identifier_nn,
        std::shared_ptr<InitVal> initVal_nn)
        : AstNode(startOffset)
        , m_identifier_nn(std::move(identifier_nn))
        , m_initVal_nn(std::move(initVal_nn))
    {
    }

    std::shared_ptr<Identifier> m_identifier_nn;
    std::shared_ptr<InitVal> m_initVal_nn;
};

struct ConstDecl final : AstNode {
    ConstDecl(int32_t startOffset, BTypeKeyword bType,
        std::vector<std::shared_ptr<ConstDef>> constDefs)
        : AstNode(startOffset)
        , m_bType(bType)
        , m_constDefs(std::move(constDefs))
    {
    }

    BTypeKeyword m_bType;
    std::vector<std::shared_ptr<ConstDef>> m_constDefs;
};

struct VarDecl final : AstNode {
    VarDecl(int32_t startOffset, BTypeKeyword bType,
        std::vector<std::shared_ptr<VarDef>> varDefs)
        : AstNode(startOffset)
        , m_bType(bType)
        , m_varDefs(std::move(varDefs))
    {
    }

    BTypeKeyword m_bType;
    std::vector<std::shared_ptr<VarDef>> m_varDefs;
};

using Decl = std::variant<std::shared_ptr<ConstDecl>, std::shared_ptr<VarDecl>>;

struct DeclNode final : AstNode {
    DeclNode(int32_t startOffset, Decl decl)
        : AstNode(startOffset)
        , m_decl(std::move(decl))
    {
    }

    Decl m_decl;
};

struct AssignStmt final : AstNode {
    AssignStmt(int32_t startOffset, std::shared_ptr<LVal> lVal_nn,
        std::shared_ptr<Exp> exp_nn)
        : AstNode(startOffset)
        , m_lVal_nn(std::move(lVal_nn))
        , m_exp_nn(std::move(exp_nn))
    {
    }

    std::shared_ptr<LVal> m_lVal_nn;
    std::shared_ptr<Exp> m_exp_nn;
};

struct ExpStmt final : AstNode {
    ExpStmt(int32_t startOffset, std::shared_ptr<Exp> exp_nn)
        : AstNode(startOffset)
        , m_exp_nn(std::move(exp_nn))
    {
    }

    std::shared_ptr<Exp> m_exp_nn;
};

struct ReturnStmt final : AstNode {
    ReturnStmt(int32_t startOffset, std::shared_ptr<Exp> exp_nn)
        : AstNode(startOffset)
        , m_exp_nn(std::move(exp_nn))
    {
    }

    std::shared_ptr<Exp> m_exp_nn;
};

using Stmt = std::variant<std::shared_ptr<AssignStmt>, std::shared_ptr<Block>,
    std::shared_ptr<ReturnStmt>, std::shared_ptr<ExpStmt>>;

struct StmtNode final : AstNode {
    StmtNode(int32_t startOffset, Stmt stmt)
        : AstNode(startOffset)
        , m_stmt(std::move(stmt))
    {
    }

    Stmt m_stmt;
};

using BlockItem = std::variant<std::shared_ptr<DeclNode>, std::shared_ptr<StmtNode>>;

struct BlockItemNode final : AstNode {
    BlockItemNode(int32_t startOffset, BlockItem blockItem)
        : AstNode(startOffset)
        , m_blockItem(std::move(blockItem))
    {
    }

    BlockItem m_blockItem;
};

struct Block final : AstNode {
    Block(int32_t startOffset,
        std::vector<std::shared_ptr<BlockItemNode>> blockItems)
        : AstNode(startOffset)
        , m_blockItems(std::move(blockItems))
    {
    }

    std::vector<std::shared_ptr<BlockItemNode>> m_blockItems;
};

struct FuncDef final : AstNode {
    FuncDef(int32_t startOffset, FuncTypeKeyword funcType,
        std::shared_ptr<Identifier> identifier_nn,
        std::shared_ptr<Block> block_nn)
        : AstNode(startOffset)
        , m_funcType(funcType)
        , m_identifier_nn(std::move(identifier_nn))
        , m_block_nn(std::move(block_nn))
    {
    }

    FuncTypeKeyword m_funcType;
    std::shared_ptr<Identifier> m_identifier_nn;
    std::shared_ptr<Block> m_block_nn;
};

struct CompUnit final : AstNode {
    CompUnit(int32_t startOffset, std::shared_ptr<FuncDef> funcDef_nn)
        : AstNode(startOffset)
        , m_funcDef_nn(std::move(funcDef_nn))
    {
    }

    std::shared_ptr<FuncDef> m_funcDef_nn;
};

} // namespace yesod::frontend

#endif