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

Program* Generator::generate(const frontend::semantic::CompUnit& compUnit) const
{
    auto* program = Program::create();
    program->pushFunc(generateFuncDef(requireNode(compUnit.m_funcDef_nn,
        "compilation unit is missing a function definition")));
    return program;
}

Function* Generator::generateFuncDef(
    const frontend::semantic::FuncDef& funcDef) const
{
    auto* function
        = Function::create(FunctionType::get(lowerFuncType(funcDef.m_funcType),
                               std::vector<Type*> {}),
            makeFunctionName(funcDef.m_identifier));
    auto* entryBlock = BasicBlock::createEntry("%entry");
    auto* endBlock = BasicBlock::createNonEntry("%end");
    function->pushBB(entryBlock);
    FunctionGenerationState state {
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
    const frontend::semantic::Block& block, FunctionGenerationState& state) const
{
    for (const auto& blockItem : block.m_blockItems) {
        if (blockHasTerminator(*state.m_currentBasicBlock_nn)) {
            break;
        }
        generateBlockItem(
            requireNode(blockItem, "block contains a null item"), state);
    }
}

void Generator::generateBlockItem(
    const frontend::semantic::BlockItemNode& blockItem,
    FunctionGenerationState& state) const
{
    if (blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        return;
    }

    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::semantic::DeclNode>>) {
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

void Generator::generateDecl(const frontend::semantic::DeclNode& declNode,
    FunctionGenerationState& state) const
{
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::semantic::ConstDecl>>) {
                const auto& constDecl = requireNode(
                    declAlt, "const declaration payload is null");
                for (const auto& constDef : constDecl.m_constDefs) {
                    const auto& resolvedConstDef = requireNode(
                        constDef, "const declarator payload is null");
                    const auto& symbol = requireNode(resolvedConstDef.m_symbol_nn,
                        "const declarator is missing its symbol");
                    auto* alloc = AllocValue::get(Int32Type::get(),
                        makeUniqueLocalName(symbol, state.m_usedSymbolNames));
                    state.m_currentBasicBlock_nn->pushInst(alloc);
                    state.m_storageBySymbol[resolvedConstDef.m_symbol_nn.get()]
                        = alloc;
                    if (resolvedConstDef.m_initExp_nn != nullptr) {
                        auto* initValue
                            = generateExp(*resolvedConstDef.m_initExp_nn, state);
                        state.m_currentBasicBlock_nn->pushInst(
                            StoreValue::get(initValue, alloc));
                    }
                }
            } else {
                const auto& varDecl = requireNode(
                    declAlt, "var declaration payload is null");
                for (const auto& varDef : varDecl.m_varDefs) {
                    const auto& resolvedVarDef = requireNode(
                        varDef, "var declarator payload is null");
                    const auto& symbol = requireNode(resolvedVarDef.m_symbol_nn,
                        "var declarator is missing its symbol");
                    auto* alloc = AllocValue::get(Int32Type::get(),
                        makeUniqueLocalName(symbol, state.m_usedSymbolNames));
                    state.m_currentBasicBlock_nn->pushInst(alloc);
                    state.m_storageBySymbol[resolvedVarDef.m_symbol_nn.get()]
                        = alloc;
                    if (resolvedVarDef.m_initExp_nn != nullptr) {
                        auto* initValue
                            = generateExp(*resolvedVarDef.m_initExp_nn, state);
                        state.m_currentBasicBlock_nn->pushInst(
                            StoreValue::get(initValue, alloc));
                    }
                }
            }
        },
        declNode.m_decl);
}

void Generator::generateStmt(const frontend::semantic::StmtNode& stmtNode,
    FunctionGenerationState& state) const
{
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::semantic::IfStmt>>) {
                generateIfStmt(
                    requireNode(stmtAlt, "if statement is null"), state);
            } else if constexpr (std::is_same_v<AltType,
                         std::shared_ptr<frontend::semantic::WhileStmt>>) {
            generateWhileStmt(requireNode(
                          stmtAlt, "while statement is null"),
                state);
            } else if constexpr (std::is_same_v<AltType,
                         std::shared_ptr<frontend::semantic::BreakStmt>>) {
            generateBreakStmt(requireNode(
                          stmtAlt, "break statement is null"),
                state);
            } else if constexpr (std::is_same_v<AltType,
                         std::shared_ptr<frontend::semantic::ContinueStmt>>) {
            generateContinueStmt(requireNode(
                         stmtAlt,
                         "continue statement is null"),
                state);
            } else if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::semantic::AssignStmt>>) {
                generateAssignStmt(requireNode(
                                       stmtAlt, "assignment statement is null"),
                    state);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::semantic::Block>>) {
                const auto& nestedBlock
                    = requireNode(stmtAlt, "block statement is null");
                generateBlock(nestedBlock, state);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::semantic::ExpStmt>>) {
                generateExpStmt(requireNode(
                                    stmtAlt, "expression statement is null"),
                    state);
            } else {
                (void)generateReturnStmt(requireNode(
                                            stmtAlt,
                                            "return statement is null"),
                    state);
            }
        },
        stmtNode.m_stmt);
}

void Generator::generateIfStmt(const frontend::semantic::IfStmt& ifStmt,
    FunctionGenerationState& state) const
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

void Generator::generateWhileStmt(const frontend::semantic::WhileStmt& whileStmt,
    FunctionGenerationState& state) const
{
    auto* condBlock = createBasicBlock("while_cond", state);
    auto* bodyBlock = createBasicBlock("while_body", state);
    auto* endBlock = createBasicBlock("while_end", state);

    state.m_currentBasicBlock_nn->pushInst(JumpValue::get(condBlock, {}));

    const auto& loopTarget = requireNode(
        whileStmt.m_loopTarget_nn, "while statement is missing a loop target");
    state.m_loopBlocksByTarget[&loopTarget] = FunctionGenerationState::LoopBlocks {
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

    state.m_loopBlocksByTarget.erase(&loopTarget);
    state.m_currentBasicBlock_nn = endBlock;
}

void Generator::generateBreakStmt(const frontend::semantic::BreakStmt& breakStmt,
    FunctionGenerationState& state) const
{
    const auto& loopTarget = requireNode(
        breakStmt.m_loopTarget_nn, "break statement is missing a loop target");
    const auto loopIt = state.m_loopBlocksByTarget.find(&loopTarget);
    if (loopIt == state.m_loopBlocksByTarget.end()) {
        throw std::runtime_error("break statement references unknown loop target");
    }
    state.m_currentBasicBlock_nn->pushInst(
        JumpValue::get(loopIt->second.m_endBlock_nn, {}));
}

void Generator::generateContinueStmt(
    const frontend::semantic::ContinueStmt& continueStmt,
    FunctionGenerationState& state) const
{
    const auto& loopTarget = requireNode(continueStmt.m_loopTarget_nn,
        "continue statement is missing a loop target");
    const auto loopIt = state.m_loopBlocksByTarget.find(&loopTarget);
    if (loopIt == state.m_loopBlocksByTarget.end()) {
        throw std::runtime_error(
            "continue statement references unknown loop target");
    }
    state.m_currentBasicBlock_nn->pushInst(
        JumpValue::get(loopIt->second.m_condBlock_nn, {}));
}

void Generator::generateAssignStmt(
    const frontend::semantic::AssignStmt& assignStmt,
    FunctionGenerationState& state) const
{
    const auto& lVal
        = requireNode(assignStmt.m_lVal_nn, "assignment is missing an lvalue");
    const auto& symbol
        = requireNode(lVal.m_symbol_nn, "assignment lvalue is missing a symbol");
    const auto storageIt = state.m_storageBySymbol.find(&symbol);
    if (storageIt == state.m_storageBySymbol.end()) {
        throw std::runtime_error("assignment references undefined storage");
    }

    auto* value = generateExp(requireNode(assignStmt.m_exp_nn,
                                  "assignment is missing a value"), state);
    state.m_currentBasicBlock_nn->pushInst(StoreValue::get(value, storageIt->second));
}

void Generator::generateExpStmt(const frontend::semantic::ExpStmt& expStmt,
    FunctionGenerationState& state) const
{
    if (expStmt.m_exp_nn != nullptr) {
        (void)generateExp(requireNode(
            expStmt.m_exp_nn, "expression statement is missing a value"), state);
    }
}

ReturnValue* Generator::generateReturnStmt(
    const frontend::semantic::ReturnStmt& returnStmt,
    FunctionGenerationState& state) const
{
    auto* returnValue = ReturnValue::get(generateExp(
        requireNode(returnStmt.m_exp_nn, "return statement is missing a value"),
        state));
    state.m_currentBasicBlock_nn->pushInst(returnValue);
    return returnValue;
}

Value* Generator::generateExp(
    const frontend::semantic::Exp& exp, FunctionGenerationState& state) const
{
    return std::visit(
        [&](const auto& expAlt) -> Value* {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::semantic::Number>>) {
                return generateNumber(requireNode(
                    expAlt, "semantic number expression is missing"));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::semantic::LVal>>) {
                const auto& lVal
                    = requireNode(expAlt, "semantic lvalue expression is missing");
                const auto& symbol = requireNode(
                    lVal.m_symbol_nn, "semantic lvalue is missing its symbol");
                const auto storageIt = state.m_storageBySymbol.find(&symbol);
                if (storageIt == state.m_storageBySymbol.end()) {
                    throw std::runtime_error("lvalue references undefined storage");
                }
                auto* loadValue = LoadValue::get(
                    storageIt->second, makeTempName(state.m_nextTempId));
                state.m_currentBasicBlock_nn->pushInst(loadValue);
                return loadValue;
            } else if constexpr (std::is_same_v<AltType,
                                     std::pair<frontend::UnaryOpKeyword,
                                         std::shared_ptr<frontend::semantic::Exp>>>) {
                if (expAlt.first == frontend::UnaryOpKeyword::bang) {
                    return generateBooleanAsInt(exp, state);
                }

                auto* operand = generateExp(requireNode(
                                               expAlt.second,
                                               "unary expression is missing its operand"),
                    state);
                auto* zero = IntegerValue::get(0);
                if (expAlt.first == frontend::UnaryOpKeyword::plus) {
                    return generateBinaryValue(KOOPA_RBO_ADD, zero, operand,
                        *state.m_currentBasicBlock_nn, state.m_nextTempId);
                }
                return generateBinaryValue(KOOPA_RBO_SUB, zero, operand,
                    *state.m_currentBasicBlock_nn, state.m_nextTempId);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::semantic::BinaryExp>>) {
                const auto& binaryExp = requireNode(
                    expAlt, "semantic binary expression is missing");
                const bool materializeBoolean = std::visit(
                    [](const auto& binaryOp) {
                        using OpType = std::decay_t<decltype(binaryOp)>;
                        return !std::is_same_v<OpType, frontend::MulOpKeyword>
                            && !std::is_same_v<OpType, frontend::AddOpKeyword>;
                    },
                    binaryExp.m_op);
                if (materializeBoolean) {
                    return generateBooleanAsInt(exp, state);
                }

                return std::visit(
                    [&](const auto& binaryOp) -> Value* {
                        using OpType = std::decay_t<decltype(binaryOp)>;
                        auto* lhs = generateExp(requireNode(binaryExp.m_lhs_nn,
                                                   "binary expression is missing its lhs"),
                            state);
                        auto* rhs = generateExp(requireNode(binaryExp.m_rhs_nn,
                                                   "binary expression is missing its rhs"),
                            state);
                        if constexpr (std::is_same_v<OpType,
                                          frontend::MulOpKeyword>) {
                            switch (binaryOp) {
                            case frontend::MulOpKeyword::star:
                                return generateBinaryValue(KOOPA_RBO_MUL, lhs, rhs,
                                    *state.m_currentBasicBlock_nn,
                                    state.m_nextTempId);
                            case frontend::MulOpKeyword::slash:
                                return generateBinaryValue(KOOPA_RBO_DIV, lhs, rhs,
                                    *state.m_currentBasicBlock_nn,
                                    state.m_nextTempId);
                            case frontend::MulOpKeyword::percent:
                                return generateBinaryValue(KOOPA_RBO_MOD, lhs, rhs,
                                    *state.m_currentBasicBlock_nn,
                                    state.m_nextTempId);
                            }
                        } else if constexpr (std::is_same_v<OpType,
                                                 frontend::AddOpKeyword>) {
                            switch (binaryOp) {
                            case frontend::AddOpKeyword::plus:
                                return generateBinaryValue(KOOPA_RBO_ADD, lhs, rhs,
                                    *state.m_currentBasicBlock_nn,
                                    state.m_nextTempId);
                            case frontend::AddOpKeyword::minus:
                                return generateBinaryValue(KOOPA_RBO_SUB, lhs, rhs,
                                    *state.m_currentBasicBlock_nn,
                                    state.m_nextTempId);
                            }
                        }
                        throw std::runtime_error("unsupported binary operator");
                    },
                    binaryExp.m_op);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::semantic::IntToBoolExp>>) {
                return generateBooleanAsInt(exp, state);
            } else {
                const auto& boolToIntExp = requireNode(
                    expAlt, "semantic bool-to-int expression is missing");
                return generateBooleanAsInt(requireNode(
                    boolToIntExp.m_operand_nn,
                    "bool-to-int expression is missing its operand"), state);
            }

            throw std::runtime_error("unsupported semantic expression");
        },
        exp.m_kind);
}

Value* Generator::generateBooleanAsInt(
    const frontend::semantic::Exp& exp, FunctionGenerationState& state) const
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

void Generator::generateBooleanBranch(const frontend::semantic::Exp& exp,
    BasicBlock& trueBlock, BasicBlock& falseBlock,
    FunctionGenerationState& state) const
{
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::semantic::IntToBoolExp>>) {
                const auto& intToBoolExp = requireNode(
                    expAlt, "semantic int-to-bool expression is missing");
                auto* operand = generateExp(requireNode(
                                                intToBoolExp.m_operand_nn,
                                                "int-to-bool expression is missing its operand"),
                    state);
                auto* condition = generateBooleanizedValue(operand,
                    *state.m_currentBasicBlock_nn, state.m_nextTempId);
                state.m_currentBasicBlock_nn->pushInst(BranchValue::get(
                    condition, &trueBlock, {}, &falseBlock, {}));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::semantic::BoolToIntExp>>) {
                const auto& boolToIntExp = requireNode(
                    expAlt, "semantic bool-to-int expression is missing");
                generateBooleanBranch(requireNode(boolToIntExp.m_operand_nn,
                                          "bool-to-int expression is missing its operand"),
                    trueBlock, falseBlock, state);
            } else if constexpr (std::is_same_v<AltType,
                                     std::pair<frontend::UnaryOpKeyword,
                                         std::shared_ptr<frontend::semantic::Exp>>>) {
                if (expAlt.first == frontend::UnaryOpKeyword::bang) {
                    generateBooleanBranch(requireNode(
                                              expAlt.second,
                                              "unary boolean expression is missing its operand"),
                        falseBlock, trueBlock, state);
                    return;
                }

                auto* value = generateExp(exp, state);
                auto* condition = generateBooleanizedValue(value,
                    *state.m_currentBasicBlock_nn, state.m_nextTempId);
                state.m_currentBasicBlock_nn->pushInst(BranchValue::get(
                    condition, &trueBlock, {}, &falseBlock, {}));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::semantic::BinaryExp>>) {
                const auto& binaryExp = requireNode(
                    expAlt, "semantic binary expression is missing");
                std::visit(
                    [&](const auto& binaryOp) {
                        using OpType = std::decay_t<decltype(binaryOp)>;
                        if constexpr (std::is_same_v<OpType,
                                          frontend::LAndOpKeyword>) {
                            auto* rhsBlock = createBasicBlock("land_rhs", state);
                            generateBooleanBranch(requireNode(binaryExp.m_lhs_nn,
                                                      "logical-and expression is missing its lhs"),
                                *rhsBlock, falseBlock, state);
                            state.m_currentBasicBlock_nn = rhsBlock;
                            generateBooleanBranch(requireNode(binaryExp.m_rhs_nn,
                                                      "logical-and expression is missing its rhs"),
                                trueBlock, falseBlock, state);
                        } else if constexpr (std::is_same_v<OpType,
                                                 frontend::LOrOpKeyword>) {
                            auto* rhsBlock = createBasicBlock("lor_rhs", state);
                            generateBooleanBranch(requireNode(binaryExp.m_lhs_nn,
                                                      "logical-or expression is missing its lhs"),
                                trueBlock, *rhsBlock, state);
                            state.m_currentBasicBlock_nn = rhsBlock;
                            generateBooleanBranch(requireNode(binaryExp.m_rhs_nn,
                                                      "logical-or expression is missing its rhs"),
                                trueBlock, falseBlock, state);
                        } else if constexpr (std::is_same_v<OpType,
                                                 frontend::RelOpKeyword>
                            || std::is_same_v<OpType, frontend::EqOpKeyword>) {
                            auto* lhs = generateExp(requireNode(binaryExp.m_lhs_nn,
                                                       "comparison expression is missing its lhs"),
                                state);
                            auto* rhs = generateExp(requireNode(binaryExp.m_rhs_nn,
                                                       "comparison expression is missing its rhs"),
                                state);
                            koopa_raw_binary_op op = KOOPA_RBO_EQ;
                            if constexpr (std::is_same_v<OpType,
                                              frontend::RelOpKeyword>) {
                                switch (binaryOp) {
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
                            } else {
                                switch (binaryOp) {
                                case frontend::EqOpKeyword::equal:
                                    op = KOOPA_RBO_EQ;
                                    break;
                                case frontend::EqOpKeyword::notEqual:
                                    op = KOOPA_RBO_NOT_EQ;
                                    break;
                                }
                            }
                            auto* condition = generateBinaryValue(op, lhs, rhs,
                                *state.m_currentBasicBlock_nn,
                                state.m_nextTempId);
                            state.m_currentBasicBlock_nn->pushInst(
                                BranchValue::get(condition, &trueBlock, {},
                                    &falseBlock, {}));
                        } else {
                            auto* value = generateExp(exp, state);
                            auto* condition = generateBooleanizedValue(value,
                                *state.m_currentBasicBlock_nn,
                                state.m_nextTempId);
                            state.m_currentBasicBlock_nn->pushInst(
                                BranchValue::get(condition, &trueBlock, {},
                                    &falseBlock, {}));
                        }
                    },
                    binaryExp.m_op);
            } else {
                auto* value = generateExp(exp, state);
                auto* condition = generateBooleanizedValue(
                    value, *state.m_currentBasicBlock_nn, state.m_nextTempId);
                state.m_currentBasicBlock_nn->pushInst(BranchValue::get(
                    condition, &trueBlock, {}, &falseBlock, {}));
            }
        },
        exp.m_kind);
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

Value* Generator::generateNumber(const frontend::semantic::Number& number) const
{
    return IntegerValue::get(number.m_value);
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
    const frontend::semantic::Symbol& symbol,
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
