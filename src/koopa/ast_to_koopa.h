#ifndef _YESOD_KOOPA_AST_TO_KOOPA_H_
#define _YESOD_KOOPA_AST_TO_KOOPA_H_

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "frontend/semantic.h"
#include "koopa/mykoopa.h"

namespace yesod::koopa {

class Generator {
  public:
    [[nodiscard]] Program* generate(const frontend::CompUnit& compUnit,
        const frontend::SemanticInfo& semanticInfo) const;

  private:
    struct FunctionGenerationState {
        struct LoopBlocks {
            BasicBlock* m_condBlock_nn;
            BasicBlock* m_endBlock_nn;
        };

        const frontend::SemanticInfo* m_semanticInfo_nn;
        Function* m_function_nn;
        BasicBlock* m_currentBasicBlock_nn;
        BasicBlock* m_endBlock_nn;
        int32_t m_nextTempId = 1;
        int32_t m_nextBlockId = 1;
        std::unordered_map<int32_t, Value*> m_storageBySymbolId;
        std::unordered_map<int32_t, LoopBlocks> m_loopBlocksById;
        std::unordered_set<std::string> m_usedSymbolNames;
    };

    [[nodiscard]] Function* generateFuncDef(const frontend::FuncDef& funcDef,
        const frontend::SemanticInfo& semanticInfo) const;
    void generateBlock(
        const frontend::Block& block, FunctionGenerationState& state) const;
    void generateBlockItem(const frontend::BlockItemNode& blockItem,
        FunctionGenerationState& state) const;
    void generateDecl(const frontend::DeclNode& declNode,
        FunctionGenerationState& state) const;
    void generateStmt(const frontend::StmtNode& stmtNode,
        FunctionGenerationState& state) const;
    void generateIfStmt(const frontend::IfStmt& ifStmt,
        FunctionGenerationState& state) const;
    void generateWhileStmt(const frontend::WhileStmt& whileStmt,
        FunctionGenerationState& state) const;
    void generateBreakStmt(const frontend::BreakStmt& breakStmt,
        FunctionGenerationState& state) const;
    void generateContinueStmt(const frontend::ContinueStmt& continueStmt,
        FunctionGenerationState& state) const;
    void generateAssignStmt(const frontend::AssignStmt& assignStmt,
        FunctionGenerationState& state) const;
    void generateExpStmt(const frontend::ExpStmt& expStmt,
        FunctionGenerationState& state) const;
    [[nodiscard]] ReturnValue* generateReturnStmt(
        const frontend::ReturnStmt& returnStmt,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateExp(
        const frontend::Exp& exp, FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateLOrExpValue(
        const frontend::LOrExp& lOrExp, FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateLAndExpValue(
        const frontend::LAndExp& lAndExp, FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateEqExpValue(
        const frontend::EqExp& eqExp, FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateRelExpValue(
        const frontend::RelExp& relExp, FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateAddExpValue(
        const frontend::AddExp& addExp, FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateMulExpValue(
        const frontend::MulExp& mulExp, FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateUnaryExpValue(
        const frontend::UnaryExp& unaryExp, FunctionGenerationState& state) const;
    [[nodiscard]] Value* generatePrimaryExpValue(const frontend::PrimaryExp& primaryExp,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateBooleanAsInt(
        const frontend::Exp& exp, FunctionGenerationState& state) const;
    void generateBooleanBranch(const frontend::Exp& exp, BasicBlock& trueBlock,
        BasicBlock& falseBlock, FunctionGenerationState& state) const;
    void generateLOrExpBranch(const frontend::LOrExp& lOrExp,
        BasicBlock& trueBlock, BasicBlock& falseBlock,
        FunctionGenerationState& state) const;
    void generateLAndExpBranch(const frontend::LAndExp& lAndExp,
        BasicBlock& trueBlock, BasicBlock& falseBlock,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateBooleanizedValue(
        Value* value, BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] BinaryValue* generateBinaryValue(koopa_raw_binary_op op,
        Value* lhs, Value* rhs, BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] Value* generateNumber(
        const frontend::Number& number) const;
    [[nodiscard]] const frontend::SemanticSymbol& requireSymbolForNode(
        const frontend::AstNode& node, const frontend::SemanticInfo& semanticInfo,
        const char* message) const;
    [[nodiscard]] BasicBlock* createBasicBlock(
        const std::string& stem, FunctionGenerationState& state) const;
    [[nodiscard]] bool blockHasTerminator(const BasicBlock& basicBlock) const;
    void finalizeBasicBlock(BasicBlock& basicBlock, BasicBlock& endBlock) const;
    [[nodiscard]] std::string makeUniqueLocalName(
        const frontend::SemanticSymbol& symbol,
        std::unordered_set<std::string>& usedSymbolNames) const;
};

} // namespace yesod::koopa

#endif
