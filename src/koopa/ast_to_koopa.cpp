#include "koopa/ast_to_koopa.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace yesod::koopa {

template <typename T>
const T& requireNode(const std::shared_ptr<T>& node, const char* message)
{
    if (node == nullptr) {
        throw std::runtime_error(message);
    }
    return *node;
}

Type* lowerFuncType(frontend::FuncTypeKeyword funcType)
{
    switch (funcType) {
    case frontend::FuncTypeKeyword::intKeyword:
        return Int32Type::get();
    }

    throw std::runtime_error("unsupported function type");
}

std::string makeFunctionName(const std::string& identifier)
{
    return "@" + identifier;
}

std::string makeTempName(int32_t& nextTempId)
{
    return "%" + std::to_string(nextTempId++);
}

Program* Generator::generate(const frontend::CompUnit& compUnit,
    const frontend::SemanticInfo& semanticInfo) const
{
    auto* program = Program::create();
    program->pushFunc(generateFuncDef(requireNode(compUnit.m_funcDef_nn,
        "compilation unit is missing a function definition"), semanticInfo));
    return program;
}

Function* Generator::generateFuncDef(const frontend::FuncDef& funcDef,
    const frontend::SemanticInfo& semanticInfo) const
{
    const auto& identifier = requireNode(
        funcDef.m_identifier_nn, "function definition is missing an identifier");
    auto* function
        = Function::create(FunctionType::get(lowerFuncType(funcDef.m_funcType),
                               std::vector<Type*> {}),
            makeFunctionName(identifier.m_name));
    auto* entryBlock = BasicBlock::createEntry("%entry");
    auto* endBlock = BasicBlock::createNonEntry("%end");
    function->pushBB(entryBlock);
    FunctionGenerationState state {
        .m_semanticInfo_nn = &semanticInfo,
        .m_function_nn = function,
        .m_currentBasicBlock_nn = entryBlock,
        .m_endBlock_nn = endBlock,
    };
    generateBlock(
        requireNode(funcDef.m_block_nn, "function definition is missing a block"),
        state);
    finalizeBasicBlock(*state.m_currentBasicBlock_nn, *endBlock);
    endBlock->pushInst(ReturnValue::get(IntegerValue::get(0)));
    function->pushBB(endBlock);
    for (auto* basicBlock : function->bbs()) {
        basicBlock->validate();
    }
    function->validate();
    return function;
}

void Generator::generateBlock(
    const frontend::Block& block, FunctionGenerationState& state) const
{
    for (const auto& blockItem : block.m_blockItems) {
        if (blockHasTerminator(*state.m_currentBasicBlock_nn)) {
            break;
        }
        generateBlockItem(requireNode(blockItem, "block contains a null item"), state);
    }
}

void Generator::generateBlockItem(
    const frontend::BlockItemNode& blockItem, FunctionGenerationState& state) const
{
    if (blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        return;
    }

    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<frontend::DeclNode>>) {
                generateDecl(requireNode(
                                 blockItemAlt, "block item declaration is null"),
                    state);
            } else {
                generateStmt(requireNode(
                                 blockItemAlt, "block item statement is null"),
                    state);
            }
        },
        blockItem.m_blockItem);
}

void Generator::generateDecl(
    const frontend::DeclNode& declNode, FunctionGenerationState& state) const
{
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<frontend::ConstDecl>>) {
                const auto& constDecl = requireNode(
                    declAlt, "const declaration payload is null");
                for (const auto& constDef : constDecl.m_constDefs) {
                    const auto& resolvedConstDef = requireNode(
                        constDef, "const declarator payload is null");
                    const auto& symbol = requireSymbolForNode(resolvedConstDef,
                        *state.m_semanticInfo_nn,
                        "const declarator is missing its symbol binding");
                    auto* alloc = AllocValue::get(Int32Type::get(),
                        makeUniqueLocalName(symbol, state.m_usedSymbolNames));
                    state.m_currentBasicBlock_nn->pushInst(alloc);
                    state.m_storageBySymbolId[symbol.m_id] = alloc;
                    const auto& constInitVal = requireNode(
                        resolvedConstDef.m_constInitVal_nn,
                        "const declarator is missing an initializer");
                    const auto& constExp = requireNode(constInitVal.m_constExp_nn,
                        "const initializer is missing its expression wrapper");
                    auto* initValue = generateExp(requireNode(constExp.m_exp_nn,
                        "const initializer is missing its expression"), state);
                    state.m_currentBasicBlock_nn->pushInst(
                        StoreValue::get(initValue, alloc));
                }
            } else {
                const auto& varDecl = requireNode(
                    declAlt, "var declaration payload is null");
                for (const auto& varDef : varDecl.m_varDefs) {
                    const auto& resolvedVarDef = requireNode(
                        varDef, "var declarator payload is null");
                    const auto& symbol = requireSymbolForNode(resolvedVarDef,
                        *state.m_semanticInfo_nn,
                        "var declarator is missing its symbol binding");
                    auto* alloc = AllocValue::get(Int32Type::get(),
                        makeUniqueLocalName(symbol, state.m_usedSymbolNames));
                    state.m_currentBasicBlock_nn->pushInst(alloc);
                    state.m_storageBySymbolId[symbol.m_id] = alloc;
                    if (resolvedVarDef.m_initVal_nn != nullptr) {
                        auto* initValue = generateExp(requireNode(
                            requireNode(resolvedVarDef.m_initVal_nn,
                                "var declarator init payload is null")
                                .m_exp_nn,
                            "var initializer is missing its expression"),
                            state);
                        state.m_currentBasicBlock_nn->pushInst(
                            StoreValue::get(initValue, alloc));
                    }
                }
            }
        },
        declNode.m_decl);
}

void Generator::generateStmt(
    const frontend::StmtNode& stmtNode, FunctionGenerationState& state) const
{
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<frontend::IfStmt>>) {
                generateIfStmt(requireNode(stmtAlt, "if statement is null"), state);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::WhileStmt>>) {
                generateWhileStmt(
                    requireNode(stmtAlt, "while statement is null"), state);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::BreakStmt>>) {
                generateBreakStmt(
                    requireNode(stmtAlt, "break statement is null"), state);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::ContinueStmt>>) {
                generateContinueStmt(
                    requireNode(stmtAlt, "continue statement is null"), state);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::AssignStmt>>) {
                generateAssignStmt(
                    requireNode(stmtAlt, "assignment statement is null"), state);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::Block>>) {
                generateBlock(requireNode(stmtAlt, "block statement is null"), state);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::ExpStmt>>) {
                generateExpStmt(requireNode(
                                    stmtAlt, "expression statement is null"),
                    state);
            } else {
                (void)generateReturnStmt(
                    requireNode(stmtAlt, "return statement is null"), state);
            }
        },
        stmtNode.m_stmt);
}

void Generator::generateIfStmt(
    const frontend::IfStmt& ifStmt, FunctionGenerationState& state) const
{
    auto* thenBlock = createBasicBlock("if_then", state);
    BasicBlock* elseBlock = nullptr;
    if (ifStmt.m_elseStmt_nn != nullptr) {
        elseBlock = createBasicBlock("if_else", state);
    }
    auto* contBlock = createBasicBlock("if_end", state);
    if (elseBlock == nullptr) {
        elseBlock = contBlock;
    }

    generateBooleanBranch(requireNode(
                              ifStmt.m_condExp_nn,
                              "if statement is missing a condition"),
        *thenBlock, *elseBlock, state);

    state.m_currentBasicBlock_nn = thenBlock;
    generateStmt(requireNode(
                     ifStmt.m_thenStmt_nn,
                     "if statement is missing a then branch"),
        state);
    if (!blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        state.m_currentBasicBlock_nn->pushInst(JumpValue::get(contBlock, {}));
    }

    if (ifStmt.m_elseStmt_nn != nullptr) {
        state.m_currentBasicBlock_nn = elseBlock;
        generateStmt(requireNode(
                         ifStmt.m_elseStmt_nn,
                         "if statement else branch is missing"),
            state);
        if (!blockHasTerminator(*state.m_currentBasicBlock_nn)) {
            state.m_currentBasicBlock_nn->pushInst(
                JumpValue::get(contBlock, {}));
        }
    }

    state.m_currentBasicBlock_nn = contBlock;
}

void Generator::generateWhileStmt(
    const frontend::WhileStmt& whileStmt, FunctionGenerationState& state) const
{
    auto* condBlock = createBasicBlock("while_cond", state);
    auto* bodyBlock = createBasicBlock("while_body", state);
    auto* endBlock = createBasicBlock("while_end", state);

    state.m_currentBasicBlock_nn->pushInst(JumpValue::get(condBlock, {}));

    const auto loopId = state.m_semanticInfo_nn->findLoopId(whileStmt.m_id);
    if (!loopId.has_value()) {
        throw std::runtime_error("while statement is missing a loop binding");
    }
    state.m_loopBlocksById[*loopId] = FunctionGenerationState::LoopBlocks {
        .m_condBlock_nn = condBlock,
        .m_endBlock_nn = endBlock,
    };

    state.m_currentBasicBlock_nn = condBlock;
    generateBooleanBranch(requireNode(
                              whileStmt.m_condExp_nn,
                              "while statement is missing a condition"),
        *bodyBlock, *endBlock, state);

    state.m_currentBasicBlock_nn = bodyBlock;
    generateStmt(requireNode(
                     whileStmt.m_bodyStmt_nn,
                     "while statement is missing a body"),
        state);
    if (!blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        state.m_currentBasicBlock_nn->pushInst(JumpValue::get(condBlock, {}));
    }

    state.m_loopBlocksById.erase(*loopId);
    state.m_currentBasicBlock_nn = endBlock;
}

void Generator::generateBreakStmt(
    const frontend::BreakStmt& breakStmt, FunctionGenerationState& state) const
{
    const auto loopId = state.m_semanticInfo_nn->findLoopId(breakStmt.m_id);
    if (!loopId.has_value()) {
        throw std::runtime_error("break statement references no loop binding");
    }
    const auto loopIt = state.m_loopBlocksById.find(*loopId);
    if (loopIt == state.m_loopBlocksById.end()) {
        throw std::runtime_error("break statement references unknown loop target");
    }
    state.m_currentBasicBlock_nn->pushInst(
        JumpValue::get(loopIt->second.m_endBlock_nn, {}));
}

void Generator::generateContinueStmt(const frontend::ContinueStmt& continueStmt,
    FunctionGenerationState& state) const
{
    const auto loopId = state.m_semanticInfo_nn->findLoopId(continueStmt.m_id);
    if (!loopId.has_value()) {
        throw std::runtime_error("continue statement references no loop binding");
    }
    const auto loopIt = state.m_loopBlocksById.find(*loopId);
    if (loopIt == state.m_loopBlocksById.end()) {
        throw std::runtime_error(
            "continue statement references unknown loop target");
    }
    state.m_currentBasicBlock_nn->pushInst(
        JumpValue::get(loopIt->second.m_condBlock_nn, {}));
}

void Generator::generateAssignStmt(const frontend::AssignStmt& assignStmt,
    FunctionGenerationState& state) const
{
    const auto& lVal
        = requireNode(assignStmt.m_lVal_nn, "assignment is missing an lvalue");
    const auto& symbol = requireSymbolForNode(
        lVal, *state.m_semanticInfo_nn, "assignment lvalue is missing a symbol");
    const auto storageIt = state.m_storageBySymbolId.find(symbol.m_id);
    if (storageIt == state.m_storageBySymbolId.end()) {
        throw std::runtime_error("assignment references undefined storage");
    }

    auto* value = generateExp(requireNode(assignStmt.m_exp_nn,
                                  "assignment is missing a value"), state);
    state.m_currentBasicBlock_nn->pushInst(StoreValue::get(value, storageIt->second));
}

void Generator::generateExpStmt(
    const frontend::ExpStmt& expStmt, FunctionGenerationState& state) const
{
    if (expStmt.m_exp_nn != nullptr) {
        (void)generateExp(requireNode(
            expStmt.m_exp_nn, "expression statement is missing a value"), state);
    }
}

ReturnValue* Generator::generateReturnStmt(
    const frontend::ReturnStmt& returnStmt, FunctionGenerationState& state) const
{
    auto* returnValue = ReturnValue::get(generateExp(
        requireNode(returnStmt.m_exp_nn, "return statement is missing a value"),
        state));
    state.m_currentBasicBlock_nn->pushInst(returnValue);
    return returnValue;
}

Value* Generator::generateExp(
    const frontend::Exp& exp, FunctionGenerationState& state) const
{
    if (const auto constantValue
        = state.m_semanticInfo_nn->findConstantValue(exp.m_id);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    return generateLOrExpValue(
        requireNode(exp.m_lOrExp_nn, "expression is missing a logical-or node"),
        state);
}

Value* Generator::generateLOrExpValue(
    const frontend::LOrExp& lOrExp, FunctionGenerationState& state) const
{
    if (const auto constantValue
        = state.m_semanticInfo_nn->findConstantValue(lOrExp.m_id);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    if (lOrExp.m_tail.empty()) {
        return generateLAndExpValue(
            requireNode(lOrExp.m_head_nn, "logical-or expression is missing its head"),
            state);
    }
    auto* resultStorage = AllocValue::get(
        Int32Type::get(), makeTempName(state.m_nextTempId));
    state.m_currentBasicBlock_nn->pushInst(resultStorage);
    state.m_currentBasicBlock_nn->pushInst(
        StoreValue::get(IntegerValue::get(0), resultStorage));

    auto* trueBlock = createBasicBlock("lor_true", state);
    auto* falseBlock = createBasicBlock("lor_false", state);
    auto* contBlock = createBasicBlock("lor_end", state);

    generateLOrExpBranch(lOrExp, *trueBlock, *falseBlock, state);

    trueBlock->pushInst(StoreValue::get(IntegerValue::get(1), resultStorage));
    trueBlock->pushInst(JumpValue::get(contBlock, {}));
    falseBlock->pushInst(StoreValue::get(IntegerValue::get(0), resultStorage));
    falseBlock->pushInst(JumpValue::get(contBlock, {}));

    state.m_currentBasicBlock_nn = contBlock;
    auto* loadValue
        = LoadValue::get(resultStorage, makeTempName(state.m_nextTempId));
    contBlock->pushInst(loadValue);
    return loadValue;
}

Value* Generator::generateLAndExpValue(
    const frontend::LAndExp& lAndExp, FunctionGenerationState& state) const
{
    if (const auto constantValue
        = state.m_semanticInfo_nn->findConstantValue(lAndExp.m_id);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    if (lAndExp.m_tail.empty()) {
        return generateEqExpValue(
            requireNode(lAndExp.m_head_nn, "logical-and expression is missing its head"),
            state);
    }
    auto* resultStorage = AllocValue::get(
        Int32Type::get(), makeTempName(state.m_nextTempId));
    state.m_currentBasicBlock_nn->pushInst(resultStorage);
    state.m_currentBasicBlock_nn->pushInst(
        StoreValue::get(IntegerValue::get(0), resultStorage));

    auto* trueBlock = createBasicBlock("land_true", state);
    auto* falseBlock = createBasicBlock("land_false", state);
    auto* contBlock = createBasicBlock("land_end", state);

    generateLAndExpBranch(lAndExp, *trueBlock, *falseBlock, state);

    trueBlock->pushInst(StoreValue::get(IntegerValue::get(1), resultStorage));
    trueBlock->pushInst(JumpValue::get(contBlock, {}));
    falseBlock->pushInst(StoreValue::get(IntegerValue::get(0), resultStorage));
    falseBlock->pushInst(JumpValue::get(contBlock, {}));

    state.m_currentBasicBlock_nn = contBlock;
    auto* loadValue
        = LoadValue::get(resultStorage, makeTempName(state.m_nextTempId));
    contBlock->pushInst(loadValue);
    return loadValue;
}

Value* Generator::generateEqExpValue(
    const frontend::EqExp& eqExp, FunctionGenerationState& state) const
{
    if (const auto constantValue = state.m_semanticInfo_nn->findConstantValue(eqExp.m_id);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    auto* current = generateRelExpValue(
        requireNode(eqExp.m_head_nn, "equality expression is missing its head"),
        state);
    for (const auto& tailEntry : eqExp.m_tail) {
        auto* rhs = generateRelExpValue(requireNode(
            tailEntry.second, "equality expression is missing its operand"), state);
        const auto op = tailEntry.first == frontend::EqOpKeyword::equal
            ? KOOPA_RBO_EQ
            : KOOPA_RBO_NOT_EQ;
        current = generateBinaryValue(op, current, rhs,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    }
    return current;
}

Value* Generator::generateRelExpValue(
    const frontend::RelExp& relExp, FunctionGenerationState& state) const
{
    if (const auto constantValue = state.m_semanticInfo_nn->findConstantValue(relExp.m_id);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    auto* current = generateAddExpValue(
        requireNode(relExp.m_head_nn, "relational expression is missing its head"),
        state);
    for (const auto& tailEntry : relExp.m_tail) {
        auto* rhs = generateAddExpValue(requireNode(
            tailEntry.second, "relational expression is missing its operand"), state);
        koopa_raw_binary_op op = KOOPA_RBO_LT;
        switch (tailEntry.first) {
        case frontend::RelOpKeyword::less:
            op = KOOPA_RBO_LT;
            break;
        case frontend::RelOpKeyword::greater:
            op = KOOPA_RBO_GT;
            break;
        case frontend::RelOpKeyword::lessEqual:
            op = KOOPA_RBO_LE;
            break;
        case frontend::RelOpKeyword::greaterEqual:
            op = KOOPA_RBO_GE;
            break;
        }
        current = generateBinaryValue(op, current, rhs,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    }
    return current;
}

Value* Generator::generateAddExpValue(
    const frontend::AddExp& addExp, FunctionGenerationState& state) const
{
    if (const auto constantValue = state.m_semanticInfo_nn->findConstantValue(addExp.m_id);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    auto* current = generateMulExpValue(
        requireNode(addExp.m_head_nn, "additive expression is missing its head"),
        state);
    for (const auto& tailEntry : addExp.m_tail) {
        auto* rhs = generateMulExpValue(requireNode(
            tailEntry.second, "additive expression is missing its operand"), state);
        const auto op = tailEntry.first == frontend::AddOpKeyword::plus
            ? KOOPA_RBO_ADD
            : KOOPA_RBO_SUB;
        current = generateBinaryValue(op, current, rhs,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    }
    return current;
}

Value* Generator::generateMulExpValue(
    const frontend::MulExp& mulExp, FunctionGenerationState& state) const
{
    if (const auto constantValue = state.m_semanticInfo_nn->findConstantValue(mulExp.m_id);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    auto* current = generateUnaryExpValue(
        requireNode(mulExp.m_head_nn, "multiplicative expression is missing its head"),
        state);
    for (const auto& tailEntry : mulExp.m_tail) {
        auto* rhs = generateUnaryExpValue(requireNode(tailEntry.second,
            "multiplicative expression is missing its operand"), state);
        koopa_raw_binary_op op = KOOPA_RBO_MUL;
        switch (tailEntry.first) {
        case frontend::MulOpKeyword::star:
            op = KOOPA_RBO_MUL;
            break;
        case frontend::MulOpKeyword::slash:
            op = KOOPA_RBO_DIV;
            break;
        case frontend::MulOpKeyword::percent:
            op = KOOPA_RBO_MOD;
            break;
        }
        current = generateBinaryValue(op, current, rhs,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    }
    return current;
}

Value* Generator::generateUnaryExpValue(
    const frontend::UnaryExp& unaryExp, FunctionGenerationState& state) const
{
    if (const auto constantValue = state.m_semanticInfo_nn->findConstantValue(unaryExp.m_id);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    return std::visit(
        [&](const auto& unaryAlt) -> Value* {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<frontend::PrimaryExp>>) {
                return generatePrimaryExpValue(requireNode(unaryAlt,
                    "unary expression is missing its primary expression"), state);
            } else {
                auto* operand = generateUnaryExpValue(requireNode(unaryAlt.second,
                    "unary expression is missing its operand"), state);
                switch (unaryAlt.first) {
                case frontend::UnaryOpKeyword::plus:
                    return generateBinaryValue(KOOPA_RBO_ADD, IntegerValue::get(0),
                        operand, *state.m_currentBasicBlock_nn, state.m_nextTempId);
                case frontend::UnaryOpKeyword::minus:
                    return generateBinaryValue(KOOPA_RBO_SUB, IntegerValue::get(0),
                        operand, *state.m_currentBasicBlock_nn, state.m_nextTempId);
                case frontend::UnaryOpKeyword::bang:
                    return generateBinaryValue(KOOPA_RBO_EQ, IntegerValue::get(0),
                        operand, *state.m_currentBasicBlock_nn, state.m_nextTempId);
                }
            }
            throw std::runtime_error("unsupported unary operator");
        },
        unaryExp.m_kind);
}

Value* Generator::generatePrimaryExpValue(
    const frontend::PrimaryExp& primaryExp, FunctionGenerationState& state) const
{
    if (const auto constantValue
        = state.m_semanticInfo_nn->findConstantValue(primaryExp.m_id);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    return std::visit(
        [&](const auto& primaryAlt) -> Value* {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<frontend::Exp>>) {
                return generateExp(requireNode(primaryAlt,
                    "parenthesized primary is missing its inner expression"), state);
            } else if constexpr (std::is_same_v<AltType, std::shared_ptr<frontend::LVal>>) {
                const auto& lVal = requireNode(primaryAlt, "lvalue primary is missing");
                const auto& symbol = requireSymbolForNode(
                    lVal, *state.m_semanticInfo_nn, "lvalue is missing a symbol binding");
                const auto storageIt = state.m_storageBySymbolId.find(symbol.m_id);
                if (storageIt == state.m_storageBySymbolId.end()) {
                    throw std::runtime_error("lvalue references undefined storage");
                }
                auto* loadValue = LoadValue::get(
                    storageIt->second, makeTempName(state.m_nextTempId));
                state.m_currentBasicBlock_nn->pushInst(loadValue);
                return loadValue;
            } else {
                return generateNumber(requireNode(
                    primaryAlt, "number primary expression is missing"));
            }
        },
        primaryExp.m_kind);
}

Value* Generator::generateBooleanAsInt(
    const frontend::Exp& exp, FunctionGenerationState& state) const
{
    auto* resultStorage = AllocValue::get(
        Int32Type::get(), makeTempName(state.m_nextTempId));
    state.m_currentBasicBlock_nn->pushInst(resultStorage);
    state.m_currentBasicBlock_nn->pushInst(
        StoreValue::get(IntegerValue::get(0), resultStorage));

    auto* trueBlock = createBasicBlock("bool_true", state);
    auto* falseBlock = createBasicBlock("bool_false", state);
    auto* contBlock = createBasicBlock("bool_end", state);

    generateBooleanBranch(exp, *trueBlock, *falseBlock, state);

    trueBlock->pushInst(StoreValue::get(IntegerValue::get(1), resultStorage));
    trueBlock->pushInst(JumpValue::get(contBlock, {}));
    falseBlock->pushInst(StoreValue::get(IntegerValue::get(0), resultStorage));
    falseBlock->pushInst(JumpValue::get(contBlock, {}));

    state.m_currentBasicBlock_nn = contBlock;
    auto* loadValue
        = LoadValue::get(resultStorage, makeTempName(state.m_nextTempId));
    contBlock->pushInst(loadValue);
    return loadValue;
}

void Generator::generateBooleanBranch(const frontend::Exp& exp,
    BasicBlock& trueBlock, BasicBlock& falseBlock,
    FunctionGenerationState& state) const
{
    generateLOrExpBranch(requireNode(
        exp.m_lOrExp_nn, "expression is missing a logical-or node"), trueBlock,
        falseBlock, state);
}

void Generator::generateLOrExpBranch(const frontend::LOrExp& lOrExp,
    BasicBlock& trueBlock, BasicBlock& falseBlock,
    FunctionGenerationState& state) const
{
    const auto& head = requireNode(
        lOrExp.m_head_nn, "logical-or expression is missing its head");
    if (lOrExp.m_tail.empty()) {
        auto* value = generateLAndExpValue(head, state);
        state.m_currentBasicBlock_nn->pushInst(
            BranchValue::get(value, &trueBlock, {}, &falseBlock, {}));
        return;
    }

    auto* nextOperandBlock = createBasicBlock("lor_rhs", state);
    generateLAndExpBranch(head, trueBlock, *nextOperandBlock, state);
    for (size_t index = 0; index < lOrExp.m_tail.size(); ++index) {
        state.m_currentBasicBlock_nn = nextOperandBlock;
        const auto& rhs = requireNode(lOrExp.m_tail[index].second,
            "logical-or expression is missing its operand");
        if (index + 1 == lOrExp.m_tail.size()) {
            generateLAndExpBranch(rhs, trueBlock, falseBlock, state);
            return;
        }
        nextOperandBlock = createBasicBlock("lor_rhs", state);
        generateLAndExpBranch(rhs, trueBlock, *nextOperandBlock, state);
    }
}

void Generator::generateLAndExpBranch(const frontend::LAndExp& lAndExp,
    BasicBlock& trueBlock, BasicBlock& falseBlock,
    FunctionGenerationState& state) const
{
    const auto& head = requireNode(
        lAndExp.m_head_nn, "logical-and expression is missing its head");
    if (lAndExp.m_tail.empty()) {
        auto* value = generateEqExpValue(head, state);
        state.m_currentBasicBlock_nn->pushInst(
            BranchValue::get(value, &trueBlock, {}, &falseBlock, {}));
        return;
    }

    auto* nextOperandBlock = createBasicBlock("land_rhs", state);
    auto* headValue = generateEqExpValue(head, state);
    state.m_currentBasicBlock_nn->pushInst(
        BranchValue::get(headValue, nextOperandBlock, {}, &falseBlock, {}));
    for (size_t index = 0; index < lAndExp.m_tail.size(); ++index) {
        state.m_currentBasicBlock_nn = nextOperandBlock;
        const auto& rhs = requireNode(lAndExp.m_tail[index].second,
            "logical-and expression is missing its operand");
        auto* rhsValue = generateEqExpValue(rhs, state);
        if (index + 1 == lAndExp.m_tail.size()) {
            state.m_currentBasicBlock_nn->pushInst(
                BranchValue::get(rhsValue, &trueBlock, {}, &falseBlock, {}));
            return;
        }
        nextOperandBlock = createBasicBlock("land_rhs", state);
        state.m_currentBasicBlock_nn->pushInst(
            BranchValue::get(rhsValue, nextOperandBlock, {}, &falseBlock, {}));
    }
}

Value* Generator::generateBooleanizedValue(
    Value* value, BasicBlock& basicBlock, int32_t& nextTempId) const
{
    return generateBinaryValue(
        KOOPA_RBO_NOT_EQ, IntegerValue::get(0), value, basicBlock, nextTempId);
}

BinaryValue* Generator::generateBinaryValue(koopa_raw_binary_op op, Value* lhs,
    Value* rhs, BasicBlock& basicBlock, int32_t& nextTempId) const
{
    auto* binaryValue
        = BinaryValue::get(op, lhs, rhs, makeTempName(nextTempId));
    basicBlock.pushInst(binaryValue);
    return binaryValue;
}

Value* Generator::generateNumber(const frontend::Number& number) const
{
    return IntegerValue::get(number.m_value);
}

const frontend::SemanticSymbol& Generator::requireSymbolForNode(
    const frontend::AstNode& node, const frontend::SemanticInfo& semanticInfo,
    const char* message) const
{
    const auto* symbol = semanticInfo.findSymbolByNodeId(node.m_id);
    if (symbol == nullptr) {
        throw std::runtime_error(message);
    }
    return *symbol;
}

BasicBlock* Generator::createBasicBlock(
    const std::string& stem, FunctionGenerationState& state) const
{
    auto* basicBlock = BasicBlock::createNonEntry(
        "%" + stem + "_" + std::to_string(state.m_nextBlockId++));
    state.m_function_nn->pushBB(basicBlock);
    return basicBlock;
}

bool Generator::blockHasTerminator(const BasicBlock& basicBlock) const
{
    return basicBlock.getNumInsts() > 0
        && basicBlock.getInst(basicBlock.getNumInsts() - 1)->canTerminateBlock();
}

void Generator::finalizeBasicBlock(
    BasicBlock& basicBlock, BasicBlock& endBlock) const
{
    if (blockHasTerminator(basicBlock)) {
        return;
    }

    basicBlock.pushInst(JumpValue::get(&endBlock, {}));
}

std::string Generator::makeUniqueLocalName(
    const frontend::SemanticSymbol& symbol,
    std::unordered_set<std::string>& usedSymbolNames) const
{
    const std::string baseName = "%" + symbol.m_name;
    if (usedSymbolNames.insert(baseName).second) {
        return baseName;
    }

    int32_t suffix = 1;
    while (true) {
        const std::string candidate
            = baseName + "_" + std::to_string(suffix++);
        if (usedSymbolNames.insert(candidate).second) {
            return candidate;
        }
    }
}

} // namespace yesod::koopa
