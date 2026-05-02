#ifndef _YESOD_KOOPA_AST_TO_KOOPA_H_
#define _YESOD_KOOPA_AST_TO_KOOPA_H_

#include "frontend/ast.h"
#include "koopa/mykoopa.h"

namespace yesod::koopa_ir {

class Generator {
public:
    [[nodiscard]] ::koopa::Program* generate(const frontend::CompUnit& compUnit) const;

private:
    [[nodiscard]] ::koopa::Function* generateFuncDef(const frontend::FuncDef& funcDef) const;
    [[nodiscard]] ::koopa::BasicBlock* generateBlock(const frontend::Block& block, int32_t& nextTempId) const;
    void generateStmt(const frontend::StmtNode& stmtNode, ::koopa::BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::ReturnValue* generateReturnStmt(
        const frontend::ReturnStmt& returnStmt,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateExp(
        const frontend::Exp& exp,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateLOrExp(
        const frontend::LOrExp& lOrExp,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateLAndExp(
        const frontend::LAndExp& lAndExp,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateEqExp(
        const frontend::EqExp& eqExp,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateRelExp(
        const frontend::RelExp& relExp,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateAddExp(
        const frontend::AddExp& addExp,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateMulExp(
        const frontend::MulExp& mulExp,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generatePrimaryExp(
        const frontend::PrimaryExp& primaryExp,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateUnaryExp(
        const frontend::UnaryExp& unaryExp,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateBooleanizedValue(
        ::koopa::Value* value,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::BinaryValue* generateBinaryValue(
        koopa_raw_binary_op op,
        ::koopa::Value* lhs,
        ::koopa::Value* rhs,
        ::koopa::BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] ::koopa::Value* generateNumber(const frontend::Number& number) const;
};

}  // namespace yesod::koopa_ir

#endif