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

struct ReturnStmt final : AstNode {
    ReturnStmt(int32_t startOffset, std::shared_ptr<Number> value_nn)
        : AstNode(startOffset), m_value_nn(std::move(value_nn)) {}

    std::shared_ptr<Number> m_value_nn;
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