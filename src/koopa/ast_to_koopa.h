#ifndef _YESOD_KOOPA_AST_TO_KOOPA_H_
#define _YESOD_KOOPA_AST_TO_KOOPA_H_

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "frontend/semantic.h"
#include "koopa/mykoopa.h"

namespace yesod::koopa {

class Generator {
  public:
    [[nodiscard]] Program* generate(const frontend::AST& ast,
        frontend::Ptr<frontend::CompUnit> compUnit,
        const frontend::SemanticInfo& semanticInfo) const;

  private:
    struct FunctionGenerationState {
        struct LoopBlocks {
            BasicBlock* m_condBlock_nn;
            BasicBlock* m_endBlock_nn;
        };

        const frontend::AST* m_ast_nn;
        const frontend::SemanticInfo* m_semanticInfo_nn;
        Function* m_function_nn;
        BasicBlock* m_currentBasicBlock_nn;
        BasicBlock* m_endBlock_nn;
        int32_t m_nextTempId = 1;
        int32_t m_nextBlockId = 1;
        std::unordered_map<int32_t, Value*> m_storageBySymbolId;
        std::unordered_map<int32_t, Function*> m_functionBySymbolId;
        std::unordered_map<frontend::Ptr<frontend::WhileStmt>, LoopBlocks>
            m_loopBlocksByWhileStmt;
        std::unordered_set<std::string> m_usedSymbolNames;
    };

    [[nodiscard]] Function* createFunctionDecl(const frontend::AST& ast,
        frontend::Ptr<frontend::FuncDef> funcDef,
        const frontend::SemanticInfo& semanticInfo) const;
    [[nodiscard]] Function* createExternalFunctionDecl(
        const frontend::SemanticSymbol& symbol) const;
    [[nodiscard]] Function* generateFuncDef(const frontend::AST& ast,
        frontend::Ptr<frontend::FuncDef> funcDef,
        const frontend::SemanticInfo& semanticInfo,
        const std::unordered_map<int32_t, Value*>& globalStorageBySymbolId,
        const std::unordered_map<int32_t, Function*>& functionBySymbolId,
        Function* function_nn) const;
    void generateGlobalDecl(frontend::Ptr<frontend::DeclNode> declNode,
        Program& program, const frontend::AST& ast,
        const frontend::SemanticInfo& semanticInfo,
        std::unordered_map<int32_t, Value*>& globalStorageBySymbolId) const;
    void generateBlock(frontend::Ptr<frontend::Block> block,
        FunctionGenerationState& state) const;
    void generateBlockItem(frontend::Ptr<frontend::BlockItemNode> blockItem,
        FunctionGenerationState& state) const;
    void generateDecl(frontend::Ptr<frontend::DeclNode> declNode,
        FunctionGenerationState& state) const;
    void generateStmt(frontend::Ptr<frontend::StmtNode> stmtNode,
        FunctionGenerationState& state) const;
    void generateIfStmt(frontend::Ptr<frontend::IfStmt> ifStmt,
        FunctionGenerationState& state) const;
    void generateWhileStmt(frontend::Ptr<frontend::WhileStmt> whileStmt,
        FunctionGenerationState& state) const;
    void generateBreakStmt(frontend::Ptr<frontend::BreakStmt> breakStmt,
        FunctionGenerationState& state) const;
    void generateContinueStmt(
        frontend::Ptr<frontend::ContinueStmt> continueStmt,
        FunctionGenerationState& state) const;
    void generateAssignStmt(frontend::Ptr<frontend::AssignStmt> assignStmt,
        FunctionGenerationState& state) const;
    void generateExpStmt(frontend::Ptr<frontend::ExpStmt> expStmt,
        FunctionGenerationState& state) const;
    [[nodiscard]] ReturnValue* generateReturnStmt(
        frontend::Ptr<frontend::ReturnStmt> returnStmt,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateExp(frontend::Ptr<frontend::Exp> exp,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateBinaryExpValue(
        const frontend::Exp::Binary& binaryExp,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateUnaryExpValue(
        const frontend::Exp::Unary& unaryExp,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateBooleanAsInt(
        frontend::Ptr<frontend::Exp> exp,
        FunctionGenerationState& state) const;
    void generateBooleanBranch(frontend::Ptr<frontend::Exp> exp,
        BasicBlock& trueBlock, BasicBlock& falseBlock,
        FunctionGenerationState& state) const;
    void generateLogicalOrBranch(const frontend::Exp::Binary& binaryExp,
        BasicBlock& trueBlock, BasicBlock& falseBlock,
        FunctionGenerationState& state) const;
    void generateLogicalAndBranch(const frontend::Exp::Binary& binaryExp,
        BasicBlock& trueBlock, BasicBlock& falseBlock,
        FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateBooleanizedValue(
        Value* value, BasicBlock& basicBlock, int32_t& nextTempId) const;
    [[nodiscard]] BinaryValue* generateBinaryValue(koopa_raw_binary_op op,
        Value* lhs, Value* rhs, BasicBlock& basicBlock,
        int32_t& nextTempId) const;
    [[nodiscard]] Value* generateNumber(const frontend::Exp::Number& number) const;
    [[nodiscard]] Type* lowerSemanticType(
        const frontend::SemanticType& type,
        bool decayUnsizedArrayToPointer = true) const;
    [[nodiscard]] Value* generateGlobalArrayInitializer(
        const frontend::SemanticType& type,
        const std::vector<frontend::Ptr<frontend::Exp>>& scalarExprs,
        size_t& nextScalarIndex,
        const frontend::SemanticInfo& semanticInfo) const;
    void generateLocalArrayInitializer(Value* address,
        const frontend::SemanticType& type,
        const std::vector<frontend::Ptr<frontend::Exp>>& scalarExprs,
        size_t& nextScalarIndex, FunctionGenerationState& state) const;
    [[nodiscard]] Value* generateLValueAddress(const frontend::Exp::LVal& lVal,
        FunctionGenerationState& state) const;
    template <typename T>
    [[nodiscard]] const T& node(frontend::Ptr<T> handle,
        const FunctionGenerationState& state, const char* message) const;
    [[nodiscard]] const frontend::SemanticSymbol& requireSymbolForIdentifier(
        frontend::Ptr<frontend::Identifier> identifier,
        const frontend::SemanticInfo& semanticInfo, const char* message) const;
    [[nodiscard]] BasicBlock* createBasicBlock(
        const std::string& stem, FunctionGenerationState& state) const;
    [[nodiscard]] bool blockHasTerminator(const BasicBlock& basicBlock) const;
    void finalizeBasicBlock(BasicBlock& basicBlock, BasicBlock& endBlock) const;
    [[nodiscard]] std::string makeUniqueLocalName(
        const frontend::SemanticSymbol& symbol,
        std::unordered_set<std::string>& usedSymbolNames) const;
};

template <typename T>
const T& Generator::node(frontend::Ptr<T> handle,
    const FunctionGenerationState& state, const char* message) const
{
    if (!handle) {
        throw std::runtime_error(message);
    }
    return handle(*state.m_ast_nn);
}

inline const frontend::SemanticSymbol& Generator::requireSymbolForIdentifier(
    frontend::Ptr<frontend::Identifier> identifier,
    const frontend::SemanticInfo& semanticInfo, const char* message) const
{
    const auto* symbol = semanticInfo.findSymbol(identifier);
    if (symbol == nullptr) {
        throw std::runtime_error(message);
    }
    return *symbol;
}

} // namespace yesod::koopa

#endif
