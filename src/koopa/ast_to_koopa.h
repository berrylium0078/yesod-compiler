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
    struct FunctionGenerationState {
        struct LoopBlocks {
            BasicBlock* m_condBlock_nn;
            BasicBlock* m_endBlock_nn;
        };

        Function* m_function_nn;
        BasicBlock* m_currentBasicBlock_nn;
        BasicBlock* m_endBlock_nn;
        int32_t m_nextTempId = 1;
        int32_t m_nextBlockId = 1;
        std::unordered_map<const frontend::semantic::Symbol*, Value*>
            m_storageBySymbol;
        std::unordered_map<const frontend::semantic::LoopTarget*, LoopBlocks>
            m_loopBlocksByTarget;
        std::unordered_set<std::string> m_usedSymbolNames;
    };

    [[nodiscard]] Function* generateFuncDef(
        const frontend::semantic::FuncDef& funcDef) const;
    void generateBlock(
        const frontend::semantic::Block& block,
        FunctionGenerationState& state) const;
    void generateBlockItem(
        const frontend::semantic::BlockItemNode& blockItem,
        FunctionGenerationState& state) const;
    void generateDecl(const frontend::semantic::DeclNode& declNode,
        FunctionGenerationState& state) const;
    void generateStmt(const frontend::semantic::StmtNode& stmtNode,
        FunctionGenerationState& state) const;
    void generateIfStmt(const frontend::semantic::IfStmt& ifStmt,
        FunctionGenerationState& state) const;
    void generateWhileStmt(const frontend::semantic::WhileStmt& whileStmt,
        FunctionGenerationState& state) const;
    void generateBreakStmt(const frontend::semantic::BreakStmt& breakStmt,
        FunctionGenerationState& state) const;
    void generateContinueStmt(
        const frontend::semantic::ContinueStmt& continueStmt,
        FunctionGenerationState& state) const;
    void generateAssignStmt(const frontend::semantic::AssignStmt& assignStmt,
        FunctionGenerationState& state) const;
    void generateExpStmt(const frontend::semantic::ExpStmt& expStmt,
        FunctionGenerationState& state) const;
    [[nodiscard]] ReturnValue* generateReturnStmt(
        const frontend::semantic::ReturnStmt& returnStmt,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateExp(
        const frontend::semantic::Exp& exp,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateBooleanAsInt(
        const frontend::semantic::Exp& exp,
        FunctionGenerationState& state) const;
    void generateBooleanBranch(const frontend::semantic::Exp& exp,
        BasicBlock& trueBlock, BasicBlock& falseBlock,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateBooleanizedValue(
        Value* value, BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] BinaryValue* generateBinaryValue(koopa_raw_binary_op op,
        Value* lhs, Value* rhs, BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] Value* generateNumber(
        const frontend::semantic::Number& number) const;
    [[nodiscard]] BasicBlock* createBasicBlock(
        const std::string& stem, FunctionGenerationState& state) const;
    [[nodiscard]] bool blockHasTerminator(const BasicBlock& basicBlock) const;
    void finalizeBasicBlock(BasicBlock& basicBlock, BasicBlock& endBlock) const;
    [[nodiscard]] std::string makeUniqueLocalName(
        const frontend::semantic::Symbol& symbol,
        std::unordered_set<std::string>& usedSymbolNames) const;
};

} // namespace yesod::koopa

#endif