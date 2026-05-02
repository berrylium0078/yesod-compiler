#ifndef _YESOD_KOOPA_AST_TO_KOOPA_H_
#define _YESOD_KOOPA_AST_TO_KOOPA_H_

#include "frontend/ast.h"
#include "koopa/mykoopa.h"

namespace yesod::koopa {

class Generator {
  public:
    [[nodiscard]] Program* generate(const frontend::CompUnit& compUnit) const;

  private:
    [[nodiscard]] Function* generateFuncDef(
        const frontend::FuncDef& funcDef) const;
    [[nodiscard]] BasicBlock* generateBlock(
        const frontend::Block& block, int32_t& nextTempId) const;
    void generateStmt(const frontend::StmtNode& stmtNode,
        BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] ReturnValue* generateReturnStmt(
        const frontend::ReturnStmt& returnStmt, BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] Value* generateExp(const frontend::Exp& exp,
        BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] Value* generateLOrExp(const frontend::LOrExp& lOrExp,
        BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] Value* generateLAndExp(const frontend::LAndExp& lAndExp,
        BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] Value* generateEqExp(const frontend::EqExp& eqExp,
        BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] Value* generateRelExp(const frontend::RelExp& relExp,
        BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] Value* generateAddExp(const frontend::AddExp& addExp,
        BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] Value* generateMulExp(const frontend::MulExp& mulExp,
        BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] Value* generatePrimaryExp(
        const frontend::PrimaryExp& primaryExp, BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] Value* generateUnaryExp(const frontend::UnaryExp& unaryExp,
        BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] Value* generateBooleanizedValue(
        Value* value, BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] BinaryValue* generateBinaryValue(koopa_raw_binary_op op,
        Value* lhs, Value* rhs, BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] Value* generateNumber(const frontend::Number& number) const;
};

} // namespace yesod::koopa

#endif