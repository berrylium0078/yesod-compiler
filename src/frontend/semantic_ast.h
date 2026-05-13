#ifndef _YESOD_FRONTEND_SEMANTIC_AST_H_
#define _YESOD_FRONTEND_SEMANTIC_AST_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "frontend/ast.h"

namespace yesod::frontend::semantic {

struct AstNode {
    explicit AstNode(int32_t startOffset)
        : m_startOffset(startOffset)
    {
    }
    virtual ~AstNode() = default;

    int32_t m_startOffset;
};

struct Symbol final : AstNode {
    Symbol(int32_t startOffset, std::string name, bool isConst,
        bool hasConstantValue, int32_t constantValue)
        : AstNode(startOffset)
        , m_name(std::move(name))
        , m_isConst(isConst)
        , m_hasConstantValue(hasConstantValue)
        , m_constantValue(constantValue)
    {
    }

    std::string m_name;
    bool m_isConst;
    bool m_hasConstantValue;
    int32_t m_constantValue;
};

struct Exp;
struct Number;
struct LVal;
struct BinaryExp;
struct IntToBoolExp;
struct BoolToIntExp;
struct ConstDef;
struct VarDef;
struct ConstDecl;
struct VarDecl;
struct DeclNode;
struct AssignStmt;
struct ExpStmt;
struct ReturnStmt;
struct WhileStmt;
struct LoopTarget;
struct BreakStmt;
struct ContinueStmt;
struct StmtNode;
struct BlockItemNode;
struct Block;
struct FuncDef;
struct CompUnit;

struct Number final : AstNode {
    Number(int32_t startOffset, int32_t value)
        : AstNode(startOffset)
        , m_value(value)
    {
    }

    int32_t m_value;
};

struct LVal final : AstNode {
    LVal(int32_t startOffset, std::shared_ptr<Symbol> symbol_nn)
        : AstNode(startOffset)
        , m_symbol_nn(std::move(symbol_nn))
    {
    }

    std::shared_ptr<Symbol> m_symbol_nn;
};

struct BinaryExp final : AstNode {
    using Op = std::variant<MulOpKeyword, AddOpKeyword, RelOpKeyword,
        EqOpKeyword, LAndOpKeyword, LOrOpKeyword>;

    BinaryExp(int32_t startOffset, Op op, std::shared_ptr<Exp> lhs_nn,
        std::shared_ptr<Exp> rhs_nn)
        : AstNode(startOffset)
        , m_op(std::move(op))
        , m_lhs_nn(std::move(lhs_nn))
        , m_rhs_nn(std::move(rhs_nn))
    {
    }

    Op m_op;
    std::shared_ptr<Exp> m_lhs_nn;
    std::shared_ptr<Exp> m_rhs_nn;
};

struct IntToBoolExp final : AstNode {
    IntToBoolExp(int32_t startOffset, std::shared_ptr<Exp> operand_nn)
        : AstNode(startOffset)
        , m_operand_nn(std::move(operand_nn))
    {
    }

    std::shared_ptr<Exp> m_operand_nn;
};

struct BoolToIntExp final : AstNode {
    BoolToIntExp(int32_t startOffset, std::shared_ptr<Exp> operand_nn)
        : AstNode(startOffset)
        , m_operand_nn(std::move(operand_nn))
    {
    }

    std::shared_ptr<Exp> m_operand_nn;
};

struct Exp final : AstNode {
    using Kind = std::variant<std::shared_ptr<Number>, std::shared_ptr<LVal>,
        std::pair<UnaryOpKeyword, std::shared_ptr<Exp>>,
        std::shared_ptr<BinaryExp>, std::shared_ptr<IntToBoolExp>,
        std::shared_ptr<BoolToIntExp>>;

    Exp(int32_t startOffset, Kind kind)
        : AstNode(startOffset)
        , m_kind(std::move(kind))
    {
    }

    Kind m_kind;
};

struct ConstDef final : AstNode {
    ConstDef(int32_t startOffset, std::shared_ptr<Symbol> symbol_nn,
        std::shared_ptr<Exp> initExp_nn)
        : AstNode(startOffset)
        , m_symbol_nn(std::move(symbol_nn))
        , m_initExp_nn(std::move(initExp_nn))
    {
    }

    std::shared_ptr<Symbol> m_symbol_nn;
    std::shared_ptr<Exp> m_initExp_nn;
};

struct VarDef final : AstNode {
    VarDef(int32_t startOffset, std::shared_ptr<Symbol> symbol_nn,
        std::shared_ptr<Exp> initExp_nn)
        : AstNode(startOffset)
        , m_symbol_nn(std::move(symbol_nn))
        , m_initExp_nn(std::move(initExp_nn))
    {
    }

    std::shared_ptr<Symbol> m_symbol_nn;
    std::shared_ptr<Exp> m_initExp_nn;
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

struct IfStmt final : AstNode {
    IfStmt(int32_t startOffset, std::shared_ptr<Exp> condExp_nn,
        std::shared_ptr<StmtNode> thenStmt_nn,
        std::shared_ptr<StmtNode> elseStmt_nn)
        : AstNode(startOffset)
        , m_condExp_nn(std::move(condExp_nn))
        , m_thenStmt_nn(std::move(thenStmt_nn))
        , m_elseStmt_nn(std::move(elseStmt_nn))
    {
    }

    std::shared_ptr<Exp> m_condExp_nn;
    std::shared_ptr<StmtNode> m_thenStmt_nn;
    std::shared_ptr<StmtNode> m_elseStmt_nn;
};

struct LoopTarget final : AstNode {
    explicit LoopTarget(int32_t startOffset)
        : AstNode(startOffset)
    {
    }
};

struct WhileStmt final : AstNode {
    WhileStmt(int32_t startOffset, std::shared_ptr<Exp> condExp_nn,
        std::shared_ptr<StmtNode> bodyStmt_nn,
        std::shared_ptr<LoopTarget> loopTarget_nn)
        : AstNode(startOffset)
        , m_condExp_nn(std::move(condExp_nn))
        , m_bodyStmt_nn(std::move(bodyStmt_nn))
        , m_loopTarget_nn(std::move(loopTarget_nn))
    {
    }

    std::shared_ptr<Exp> m_condExp_nn;
    std::shared_ptr<StmtNode> m_bodyStmt_nn;
    std::shared_ptr<LoopTarget> m_loopTarget_nn;
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

struct BreakStmt final : AstNode {
    BreakStmt(int32_t startOffset, std::shared_ptr<LoopTarget> loopTarget_nn)
        : AstNode(startOffset)
        , m_loopTarget_nn(std::move(loopTarget_nn))
    {
    }

    std::shared_ptr<LoopTarget> m_loopTarget_nn;
};

struct ContinueStmt final : AstNode {
    ContinueStmt(
        int32_t startOffset, std::shared_ptr<LoopTarget> loopTarget_nn)
        : AstNode(startOffset)
        , m_loopTarget_nn(std::move(loopTarget_nn))
    {
    }

    std::shared_ptr<LoopTarget> m_loopTarget_nn;
};

using Stmt = std::variant<std::shared_ptr<IfStmt>, std::shared_ptr<WhileStmt>,
    std::shared_ptr<BreakStmt>, std::shared_ptr<ContinueStmt>,
    std::shared_ptr<AssignStmt>, std::shared_ptr<Block>,
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
        std::string identifier, std::shared_ptr<Block> block_nn)
        : AstNode(startOffset)
        , m_funcType(funcType)
        , m_identifier(std::move(identifier))
        , m_block_nn(std::move(block_nn))
    {
    }

    FuncTypeKeyword m_funcType;
    std::string m_identifier;
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

} // namespace yesod::frontend::semantic

#endif
