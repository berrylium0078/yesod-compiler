#ifndef _YESOD_KOOPA_AST_TO_KOOPA_H_
#define _YESOD_KOOPA_AST_TO_KOOPA_H_

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "frontend/semantic_ast.h"
#include "koopa/mykoopa.h"

namespace yesod::koopa {

class Generator {
  public:
    [[nodiscard]] Program* generate(
        const frontend::semantic::CompUnit& compUnit) const;

  private:
    [[nodiscard]] Function* generateFuncDef(
        const frontend::semantic::FuncDef& funcDef) const;
    void generateBlock(const frontend::semantic::Block& block,
        BasicBlock& basicBlock, BasicBlock& endBlock, int32_t& nextTempId,
        std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol,
        std::unordered_set<std::string>& usedSymbolNames)
        const;
    void generateBlockItem(const frontend::semantic::BlockItemNode& blockItem,
        BasicBlock& basicBlock, int32_t& nextTempId,
        std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol,
        std::unordered_set<std::string>& usedSymbolNames)
        const;
    void generateDecl(const frontend::semantic::DeclNode& declNode,
        BasicBlock& basicBlock, int32_t& nextTempId,
        std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol,
        std::unordered_set<std::string>& usedSymbolNames)
        const;
    void generateStmt(const frontend::semantic::StmtNode& stmtNode,
        BasicBlock& basicBlock, int32_t& nextTempId,
        std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol,
        std::unordered_set<std::string>& usedSymbolNames)
        const;
    void generateAssignStmt(const frontend::semantic::AssignStmt& assignStmt,
        BasicBlock& basicBlock, int32_t& nextTempId,
        std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol)
        const;
    void generateExpStmt(const frontend::semantic::ExpStmt& expStmt,
        BasicBlock& basicBlock, int32_t& nextTempId,
        std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol)
        const;
    [[nodiscard]] ReturnValue* generateReturnStmt(
        const frontend::semantic::ReturnStmt& returnStmt,
        BasicBlock& basicBlock, int32_t& nextTempId,
        std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol)
        const;
    [[nodiscard]] Value* generateExp(const frontend::semantic::Exp& exp,
        BasicBlock& basicBlock, int32_t& nextTempId,
        std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol)
        const;
    [[nodiscard]] Value* generateBooleanizedValue(
        Value* value, BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] BinaryValue* generateBinaryValue(koopa_raw_binary_op op,
        Value* lhs, Value* rhs, BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] Value* generateNumber(
        const frontend::semantic::Number& number) const;
    [[nodiscard]] bool blockHasTerminator(const BasicBlock& basicBlock) const;
    void finalizeBasicBlock(BasicBlock& basicBlock, BasicBlock& endBlock) const;
    [[nodiscard]] std::string makeUniqueLocalName(
        const frontend::semantic::Symbol& symbol,
        std::unordered_set<std::string>& usedSymbolNames) const;
};

} // namespace yesod::koopa

#endif