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
    explicit AstNode(int32_t startOffset) : m_startOffset(startOffset) {}
    virtual ~AstNode() = default;

    int32_t m_startOffset;
};

enum class FuncTypeKeyword {
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

struct Identifier final : AstNode {
    Identifier(int32_t startOffset, std::string name)
        : AstNode(startOffset), m_name(std::move(name)) {}

    std::string m_name;
};

struct Number final : AstNode {
    Number(int32_t startOffset, int32_t value)
        : AstNode(startOffset), m_value(value) {}

    int32_t m_value;
};

struct PrimaryExp final : AstNode {
    using Kind = std::variant<std::shared_ptr<Exp>, std::shared_ptr<Number>>;

    PrimaryExp(int32_t startOffset, Kind kind)
        : AstNode(startOffset), m_kind(std::move(kind)) {}

    Kind m_kind;
};

struct UnaryExp final : AstNode {
    using Kind = std::variant<std::shared_ptr<PrimaryExp>, std::pair<UnaryOpKeyword, std::shared_ptr<UnaryExp>>>;

    UnaryExp(int32_t startOffset, Kind kind)
        : AstNode(startOffset), m_kind(std::move(kind)) {}

    Kind m_kind;
};

struct MulExp final : AstNode {
    using Tail = std::pair<MulOpKeyword, std::shared_ptr<UnaryExp>>;

    MulExp(int32_t startOffset, std::shared_ptr<UnaryExp> head_nn, std::vector<Tail> tail)
        : AstNode(startOffset),
          m_head_nn(std::move(head_nn)),
          m_tail(std::move(tail)) {}

    std::shared_ptr<UnaryExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct AddExp final : AstNode {
    using Tail = std::pair<AddOpKeyword, std::shared_ptr<MulExp>>;

    AddExp(int32_t startOffset, std::shared_ptr<MulExp> head_nn, std::vector<Tail> tail)
        : AstNode(startOffset),
          m_head_nn(std::move(head_nn)),
          m_tail(std::move(tail)) {}

    std::shared_ptr<MulExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct RelExp final : AstNode {
    using Tail = std::pair<RelOpKeyword, std::shared_ptr<AddExp>>;

    RelExp(int32_t startOffset, std::shared_ptr<AddExp> head_nn, std::vector<Tail> tail)
        : AstNode(startOffset),
          m_head_nn(std::move(head_nn)),
          m_tail(std::move(tail)) {}

    std::shared_ptr<AddExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct EqExp final : AstNode {
    using Tail = std::pair<EqOpKeyword, std::shared_ptr<RelExp>>;

    EqExp(int32_t startOffset, std::shared_ptr<RelExp> head_nn, std::vector<Tail> tail)
        : AstNode(startOffset),
          m_head_nn(std::move(head_nn)),
          m_tail(std::move(tail)) {}

    std::shared_ptr<RelExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct LAndExp final : AstNode {
    using Tail = std::pair<LAndOpKeyword, std::shared_ptr<EqExp>>;

    LAndExp(int32_t startOffset, std::shared_ptr<EqExp> head_nn, std::vector<Tail> tail)
        : AstNode(startOffset),
          m_head_nn(std::move(head_nn)),
          m_tail(std::move(tail)) {}

    std::shared_ptr<EqExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct LOrExp final : AstNode {
    using Tail = std::pair<LOrOpKeyword, std::shared_ptr<LAndExp>>;

    LOrExp(int32_t startOffset, std::shared_ptr<LAndExp> head_nn, std::vector<Tail> tail)
        : AstNode(startOffset),
          m_head_nn(std::move(head_nn)),
          m_tail(std::move(tail)) {}

    std::shared_ptr<LAndExp> m_head_nn;
    std::vector<Tail> m_tail;
};

struct Exp final : AstNode {
    Exp(int32_t startOffset, std::shared_ptr<LOrExp> lOrExp_nn)
        : AstNode(startOffset), m_lOrExp_nn(std::move(lOrExp_nn)) {}

    std::shared_ptr<LOrExp> m_lOrExp_nn;
};

struct ReturnStmt final : AstNode {
    ReturnStmt(int32_t startOffset, std::shared_ptr<Exp> exp_nn)
        : AstNode(startOffset), m_exp_nn(std::move(exp_nn)) {}

    std::shared_ptr<Exp> m_exp_nn;
};

using Stmt = std::variant<std::shared_ptr<ReturnStmt>>;

struct StmtNode final : AstNode {
    StmtNode(int32_t startOffset, Stmt stmt)
        : AstNode(startOffset), m_stmt(std::move(stmt)) {}

    Stmt m_stmt;
};

struct Block final : AstNode {
    Block(int32_t startOffset, std::vector<std::shared_ptr<StmtNode>> statements)
        : AstNode(startOffset), m_statements(std::move(statements)) {}

    std::vector<std::shared_ptr<StmtNode>> m_statements;
};

struct FuncDef final : AstNode {
    FuncDef(
        int32_t startOffset,
        FuncTypeKeyword funcType,
        std::shared_ptr<Identifier> identifier_nn,
        std::shared_ptr<Block> block_nn)
        : AstNode(startOffset),
          m_funcType(funcType),
          m_identifier_nn(std::move(identifier_nn)),
          m_block_nn(std::move(block_nn)) {}

    FuncTypeKeyword m_funcType;
    std::shared_ptr<Identifier> m_identifier_nn;
    std::shared_ptr<Block> m_block_nn;
};

struct CompUnit final : AstNode {
    CompUnit(int32_t startOffset, std::shared_ptr<FuncDef> funcDef_nn)
        : AstNode(startOffset), m_funcDef_nn(std::move(funcDef_nn)) {}

    std::shared_ptr<FuncDef> m_funcDef_nn;
};

}  // namespace yesod::frontend

#endif