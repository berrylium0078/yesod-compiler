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
    int32_t nextTempId = 1;
    std::unordered_map<const frontend::semantic::Symbol*, Value*> storageBySymbol;
    std::unordered_set<std::string> usedSymbolNames;
    auto* function
        = Function::create(FunctionType::get(lowerFuncType(funcDef.m_funcType),
                               std::vector<Type*> {}),
            makeFunctionName(funcDef.m_identifier));
    auto* entryBlock = BasicBlock::createEntry("%entry");
    auto* endBlock = BasicBlock::createNonEntry("%end");
    generateBlock(requireNode(funcDef.m_block_nn,
                      "function definition is missing a block"),
        *entryBlock, *endBlock, nextTempId, storageBySymbol, usedSymbolNames);
    finalizeBasicBlock(*entryBlock, *endBlock);
    endBlock->pushInst(ReturnValue::get(IntegerValue::get(0)));
    entryBlock->validate();
    endBlock->validate();
    function->pushBB(entryBlock);
    function->pushBB(endBlock);
    function->validate();
    return function;
}

void Generator::generateBlock(const frontend::semantic::Block& block,
    BasicBlock& basicBlock, BasicBlock& endBlock, int32_t& nextTempId,
    std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol,
    std::unordered_set<std::string>& usedSymbolNames)
    const
{
    for (const auto& blockItem : block.m_blockItems) {
        if (blockHasTerminator(basicBlock)) {
            break;
        }
        generateBlockItem(requireNode(blockItem, "block contains a null item"),
            basicBlock, nextTempId, storageBySymbol, usedSymbolNames);
    }
    finalizeBasicBlock(basicBlock, endBlock);
}

void Generator::generateBlockItem(
    const frontend::semantic::BlockItemNode& blockItem, BasicBlock& basicBlock,
    int32_t& nextTempId,
    std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol,
    std::unordered_set<std::string>& usedSymbolNames)
    const
{
    if (blockHasTerminator(basicBlock)) {
        return;
    }

    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::semantic::DeclNode>>) {
                generateDecl(requireNode(
                                 blockItemAlt, "block item declaration is null"),
                    basicBlock, nextTempId, storageBySymbol, usedSymbolNames);
            } else {
                generateStmt(requireNode(
                                 blockItemAlt, "block item statement is null"),
                    basicBlock, nextTempId, storageBySymbol, usedSymbolNames);
            }
        },
        blockItem.m_blockItem);
}

void Generator::generateDecl(const frontend::semantic::DeclNode& declNode,
    BasicBlock& basicBlock, int32_t& nextTempId,
    std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol,
    std::unordered_set<std::string>& usedSymbolNames)
    const
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
                        makeUniqueLocalName(symbol, usedSymbolNames));
                    basicBlock.pushInst(alloc);
                    storageBySymbol[resolvedConstDef.m_symbol_nn.get()] = alloc;
                    if (resolvedConstDef.m_initExp_nn != nullptr) {
                        auto* initValue = generateExp(*resolvedConstDef.m_initExp_nn,
                            basicBlock, nextTempId, storageBySymbol);
                        basicBlock.pushInst(StoreValue::get(initValue, alloc));
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
                        makeUniqueLocalName(symbol, usedSymbolNames));
                    basicBlock.pushInst(alloc);
                    storageBySymbol[resolvedVarDef.m_symbol_nn.get()] = alloc;
                    if (resolvedVarDef.m_initExp_nn != nullptr) {
                        auto* initValue = generateExp(*resolvedVarDef.m_initExp_nn,
                            basicBlock, nextTempId, storageBySymbol);
                        basicBlock.pushInst(StoreValue::get(initValue, alloc));
                    }
                }
            }
        },
        declNode.m_decl);
}

void Generator::generateStmt(const frontend::semantic::StmtNode& stmtNode,
    BasicBlock& basicBlock, int32_t& nextTempId,
    std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol,
    std::unordered_set<std::string>& usedSymbolNames)
    const
{
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::semantic::AssignStmt>>) {
                generateAssignStmt(requireNode(
                                       stmtAlt, "assignment statement is null"),
                    basicBlock, nextTempId, storageBySymbol);
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::semantic::Block>>) {
                const auto& nestedBlock
                    = requireNode(stmtAlt, "block statement is null");
                for (const auto& blockItem : nestedBlock.m_blockItems) {
                    if (blockHasTerminator(basicBlock)) {
                        break;
                    }
                    generateBlockItem(requireNode(
                                          blockItem,
                                          "nested block contains a null item"),
                        basicBlock, nextTempId, storageBySymbol,
                        usedSymbolNames);
                }
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<frontend::semantic::ExpStmt>>) {
                generateExpStmt(requireNode(
                                    stmtAlt, "expression statement is null"),
                    basicBlock, nextTempId, storageBySymbol);
            } else {
                (void)generateReturnStmt(requireNode(
                                            stmtAlt,
                                            "return statement is null"),
                    basicBlock, nextTempId, storageBySymbol);
            }
        },
        stmtNode.m_stmt);
}

void Generator::generateAssignStmt(
    const frontend::semantic::AssignStmt& assignStmt, BasicBlock& basicBlock,
    int32_t& nextTempId,
    std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol)
    const
{
    const auto& lVal
        = requireNode(assignStmt.m_lVal_nn, "assignment is missing an lvalue");
    const auto& symbol
        = requireNode(lVal.m_symbol_nn, "assignment lvalue is missing a symbol");
    const auto storageIt = storageBySymbol.find(&symbol);
    if (storageIt == storageBySymbol.end()) {
        throw std::runtime_error("assignment references undefined storage");
    }

    auto* value = generateExp(requireNode(assignStmt.m_exp_nn,
                                  "assignment is missing a value"),
        basicBlock, nextTempId, storageBySymbol);
    basicBlock.pushInst(StoreValue::get(value, storageIt->second));
}

void Generator::generateExpStmt(const frontend::semantic::ExpStmt& expStmt,
    BasicBlock& basicBlock, int32_t& nextTempId,
    std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol)
    const
{
    if (expStmt.m_exp_nn != nullptr) {
        (void)generateExp(requireNode(
            expStmt.m_exp_nn, "expression statement is missing a value"),
            basicBlock, nextTempId, storageBySymbol);
    }
}

ReturnValue* Generator::generateReturnStmt(
    const frontend::semantic::ReturnStmt& returnStmt, BasicBlock& basicBlock,
    int32_t& nextTempId,
    std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol)
    const
{
    auto* returnValue = ReturnValue::get(generateExp(
        requireNode(returnStmt.m_exp_nn, "return statement is missing a value"),
        basicBlock, nextTempId, storageBySymbol));
    basicBlock.pushInst(returnValue);
    return returnValue;
}

Value* Generator::generateExp(const frontend::semantic::Exp& exp,
    BasicBlock& basicBlock, int32_t& nextTempId,
    std::unordered_map<const frontend::semantic::Symbol*, Value*>& storageBySymbol)
    const
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
                const auto storageIt = storageBySymbol.find(&symbol);
                if (storageIt == storageBySymbol.end()) {
                    throw std::runtime_error("lvalue references undefined storage");
                }
                auto* loadValue
                    = LoadValue::get(storageIt->second, makeTempName(nextTempId));
                basicBlock.pushInst(loadValue);
                return loadValue;
            } else if constexpr (std::is_same_v<AltType,
                                     std::pair<frontend::UnaryOpKeyword,
                                         std::shared_ptr<frontend::semantic::Exp>>>) {
                auto* operand = generateExp(requireNode(expAlt.second,
                                               "unary expression is missing its operand"),
                    basicBlock, nextTempId, storageBySymbol);
                auto* zero = IntegerValue::get(0);
                switch (expAlt.first) {
                case frontend::UnaryOpKeyword::plus:
                    return generateBinaryValue(
                        KOOPA_RBO_ADD, zero, operand, basicBlock, nextTempId);
                case frontend::UnaryOpKeyword::minus:
                    return generateBinaryValue(
                        KOOPA_RBO_SUB, zero, operand, basicBlock, nextTempId);
                case frontend::UnaryOpKeyword::bang:
                    return generateBinaryValue(
                        KOOPA_RBO_EQ, zero, operand, basicBlock, nextTempId);
                }
            } else {
                const auto& binaryExp = requireNode(
                    expAlt, "semantic binary expression is missing");
                return std::visit(
                    [&](const auto& binaryOp) -> Value* {
                        using OpType = std::decay_t<decltype(binaryOp)>;
                        auto* lhs = generateExp(requireNode(binaryExp.m_lhs_nn,
                                                   "binary expression is missing its lhs"),
                            basicBlock, nextTempId, storageBySymbol);
                        auto* rhs = generateExp(requireNode(binaryExp.m_rhs_nn,
                                                   "binary expression is missing its rhs"),
                            basicBlock, nextTempId, storageBySymbol);
                        if constexpr (std::is_same_v<OpType,
                                          frontend::MulOpKeyword>) {
                            switch (binaryOp) {
                            case frontend::MulOpKeyword::star:
                                return generateBinaryValue(KOOPA_RBO_MUL, lhs, rhs,
                                    basicBlock, nextTempId);
                            case frontend::MulOpKeyword::slash:
                                return generateBinaryValue(KOOPA_RBO_DIV, lhs, rhs,
                                    basicBlock, nextTempId);
                            case frontend::MulOpKeyword::percent:
                                return generateBinaryValue(KOOPA_RBO_MOD, lhs, rhs,
                                    basicBlock, nextTempId);
                            }
                        } else if constexpr (std::is_same_v<OpType,
                                                 frontend::AddOpKeyword>) {
                            switch (binaryOp) {
                            case frontend::AddOpKeyword::plus:
                                return generateBinaryValue(KOOPA_RBO_ADD, lhs, rhs,
                                    basicBlock, nextTempId);
                            case frontend::AddOpKeyword::minus:
                                return generateBinaryValue(KOOPA_RBO_SUB, lhs, rhs,
                                    basicBlock, nextTempId);
                            }
                        } else if constexpr (std::is_same_v<OpType,
                                                 frontend::RelOpKeyword>) {
                            switch (binaryOp) {
                            case frontend::RelOpKeyword::less:
                                return generateBinaryValue(KOOPA_RBO_LT, lhs, rhs,
                                    basicBlock, nextTempId);
                            case frontend::RelOpKeyword::greater:
                                return generateBinaryValue(KOOPA_RBO_GT, lhs, rhs,
                                    basicBlock, nextTempId);
                            case frontend::RelOpKeyword::lessEqual:
                                return generateBinaryValue(KOOPA_RBO_LE, lhs, rhs,
                                    basicBlock, nextTempId);
                            case frontend::RelOpKeyword::greaterEqual:
                                return generateBinaryValue(KOOPA_RBO_GE, lhs, rhs,
                                    basicBlock, nextTempId);
                            }
                        } else if constexpr (std::is_same_v<OpType,
                                                 frontend::EqOpKeyword>) {
                            switch (binaryOp) {
                            case frontend::EqOpKeyword::equal:
                                return generateBinaryValue(KOOPA_RBO_EQ, lhs, rhs,
                                    basicBlock, nextTempId);
                            case frontend::EqOpKeyword::notEqual:
                                return generateBinaryValue(KOOPA_RBO_NOT_EQ, lhs,
                                    rhs, basicBlock, nextTempId);
                            }
                        } else if constexpr (std::is_same_v<OpType,
                                                 frontend::LAndOpKeyword>) {
                            auto* lhsBool = generateBooleanizedValue(
                                lhs, basicBlock, nextTempId);
                            auto* rhsBool = generateBooleanizedValue(
                                rhs, basicBlock, nextTempId);
                            return generateBinaryValue(KOOPA_RBO_AND, lhsBool,
                                rhsBool, basicBlock, nextTempId);
                        } else {
                            auto* lhsBool = generateBooleanizedValue(
                                lhs, basicBlock, nextTempId);
                            auto* rhsBool = generateBooleanizedValue(
                                rhs, basicBlock, nextTempId);
                            return generateBinaryValue(KOOPA_RBO_OR, lhsBool,
                                rhsBool, basicBlock, nextTempId);
                        }
                        throw std::runtime_error("unsupported binary operator");
                    },
                    binaryExp.m_op);
            }

            throw std::runtime_error("unsupported semantic expression");
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
