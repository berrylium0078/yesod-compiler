#include "koopa/ast_to_koopa.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace yesod::koopa_ir {

namespace {

template <typename T>
const T& requireNode(const std::shared_ptr<T>& node, const char* message) {
    if (node == nullptr) {
        throw std::runtime_error(message);
    }
    return *node;
}

::koopa::Type* lowerFuncType(frontend::FuncTypeKeyword funcType) {
    switch (funcType) {
    case frontend::FuncTypeKeyword::intKeyword:
        return ::koopa::Int32Type::get();
    }

    throw std::runtime_error("unsupported function type");
}

std::string makeFunctionName(const frontend::Identifier& identifier) {
    return "@" + identifier.m_name;
}

std::string makeTempName(int32_t& nextTempId) {
    return "%" + std::to_string(nextTempId++);
}

}  // namespace

::koopa::Program* Generator::generate(const frontend::CompUnit& compUnit) const {
    auto* program = ::koopa::Program::create();
    program->pushFunc(generateFuncDef(requireNode(compUnit.m_funcDef_nn, "compilation unit is missing a function definition")));
    return program;
}

::koopa::Function* Generator::generateFuncDef(const frontend::FuncDef& funcDef) const {
    int32_t nextTempId = 1;
    auto* function = ::koopa::Function::create(
        ::koopa::FunctionType::get(lowerFuncType(funcDef.m_funcType), std::vector<::koopa::Type*> {}),
        makeFunctionName(requireNode(funcDef.m_identifier_nn, "function definition is missing an identifier")));
    function->pushBB(generateBlock(requireNode(funcDef.m_block_nn, "function definition is missing a block"), nextTempId));
    function->validate();
    return function;
}

::koopa::BasicBlock* Generator::generateBlock(const frontend::Block& block, int32_t& nextTempId) const {
    auto* basicBlock = ::koopa::BasicBlock::createEntry("%entry");
    for (const auto& stmtNode : block.m_statements) {
        generateStmt(requireNode(stmtNode, "block contains a null statement"), *basicBlock, nextTempId);
    }
    basicBlock->validate();
    return basicBlock;
}

void Generator::generateStmt(const frontend::StmtNode& stmtNode, ::koopa::BasicBlock& basicBlock, int32_t& nextTempId) const {
    std::visit(
        [&](const auto& stmtAlt) -> ::koopa::Value* {
            return generateReturnStmt(requireNode(stmtAlt, "statement variant contains a null payload"), basicBlock, nextTempId);
        },
        stmtNode.m_stmt);
}

::koopa::ReturnValue* Generator::generateReturnStmt(
    const frontend::ReturnStmt& returnStmt,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    auto* returnValue = ::koopa::ReturnValue::get(
        generateExp(requireNode(returnStmt.m_exp_nn, "return statement is missing a value"), basicBlock, nextTempId));
    basicBlock.pushInst(returnValue);
    return returnValue;
}

::koopa::Value* Generator::generateExp(
    const frontend::Exp& exp,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    return generateLOrExp(requireNode(exp.m_lOrExp_nn, "expression is missing a logical-or expression"), basicBlock, nextTempId);
}

::koopa::Value* Generator::generateLOrExp(
    const frontend::LOrExp& lOrExp,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    auto* currentValue = generateLAndExp(requireNode(lOrExp.m_head_nn, "logical-or expression is missing its head"), basicBlock, nextTempId);
    for (const auto& tailEntry : lOrExp.m_tail) {
        auto* lhsBool = generateBooleanizedValue(currentValue, basicBlock, nextTempId);
        auto* rhsValue = generateLAndExp(requireNode(tailEntry.second, "logical-or expression is missing its operand"), basicBlock, nextTempId);
        auto* rhsBool = generateBooleanizedValue(rhsValue, basicBlock, nextTempId);
        currentValue = generateBinaryValue(KOOPA_RBO_OR, lhsBool, rhsBool, basicBlock, nextTempId);
    }
    return currentValue;
}

::koopa::Value* Generator::generateLAndExp(
    const frontend::LAndExp& lAndExp,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    auto* currentValue = generateEqExp(requireNode(lAndExp.m_head_nn, "logical-and expression is missing its head"), basicBlock, nextTempId);
    for (const auto& tailEntry : lAndExp.m_tail) {
        auto* lhsBool = generateBooleanizedValue(currentValue, basicBlock, nextTempId);
        auto* rhsValue = generateEqExp(requireNode(tailEntry.second, "logical-and expression is missing its operand"), basicBlock, nextTempId);
        auto* rhsBool = generateBooleanizedValue(rhsValue, basicBlock, nextTempId);
        currentValue = generateBinaryValue(KOOPA_RBO_AND, lhsBool, rhsBool, basicBlock, nextTempId);
    }
    return currentValue;
}

::koopa::Value* Generator::generateEqExp(
    const frontend::EqExp& eqExp,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    auto* currentValue = generateRelExp(requireNode(eqExp.m_head_nn, "equality expression is missing its head"), basicBlock, nextTempId);
    for (const auto& tailEntry : eqExp.m_tail) {
        auto* rhsValue = generateRelExp(requireNode(tailEntry.second, "equality expression is missing its operand"), basicBlock, nextTempId);
        switch (tailEntry.first) {
        case frontend::EqOpKeyword::equal:
            currentValue = generateBinaryValue(KOOPA_RBO_EQ, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::EqOpKeyword::notEqual:
            currentValue = generateBinaryValue(KOOPA_RBO_NOT_EQ, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        }
    }
    return currentValue;
}

::koopa::Value* Generator::generateRelExp(
    const frontend::RelExp& relExp,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    auto* currentValue = generateAddExp(requireNode(relExp.m_head_nn, "relational expression is missing its head"), basicBlock, nextTempId);
    for (const auto& tailEntry : relExp.m_tail) {
        auto* rhsValue = generateAddExp(requireNode(tailEntry.second, "relational expression is missing its operand"), basicBlock, nextTempId);
        switch (tailEntry.first) {
        case frontend::RelOpKeyword::less:
            currentValue = generateBinaryValue(KOOPA_RBO_LT, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::RelOpKeyword::greater:
            currentValue = generateBinaryValue(KOOPA_RBO_GT, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::RelOpKeyword::lessEqual:
            currentValue = generateBinaryValue(KOOPA_RBO_LE, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::RelOpKeyword::greaterEqual:
            currentValue = generateBinaryValue(KOOPA_RBO_GE, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        }
    }
    return currentValue;
}

::koopa::Value* Generator::generateAddExp(
    const frontend::AddExp& addExp,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    auto* currentValue = generateMulExp(requireNode(addExp.m_head_nn, "additive expression is missing its head"), basicBlock, nextTempId);
    for (const auto& tailEntry : addExp.m_tail) {
        auto* rhsValue = generateMulExp(requireNode(tailEntry.second, "additive expression is missing its operand"), basicBlock, nextTempId);
        switch (tailEntry.first) {
        case frontend::AddOpKeyword::plus:
            currentValue = generateBinaryValue(KOOPA_RBO_ADD, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::AddOpKeyword::minus:
            currentValue = generateBinaryValue(KOOPA_RBO_SUB, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        }
    }
    return currentValue;
}

::koopa::Value* Generator::generateMulExp(
    const frontend::MulExp& mulExp,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    auto* currentValue = generateUnaryExp(requireNode(mulExp.m_head_nn, "multiplicative expression is missing its head"), basicBlock, nextTempId);
    for (const auto& tailEntry : mulExp.m_tail) {
        auto* rhsValue = generateUnaryExp(requireNode(tailEntry.second, "multiplicative expression is missing its operand"), basicBlock, nextTempId);
        switch (tailEntry.first) {
        case frontend::MulOpKeyword::star:
            currentValue = generateBinaryValue(KOOPA_RBO_MUL, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::MulOpKeyword::slash:
            currentValue = generateBinaryValue(KOOPA_RBO_DIV, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::MulOpKeyword::percent:
            currentValue = generateBinaryValue(KOOPA_RBO_MOD, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        }
    }
    return currentValue;
}

::koopa::Value* Generator::generatePrimaryExp(
    const frontend::PrimaryExp& primaryExp,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    return std::visit(
        [&](const auto& primaryAlt) -> ::koopa::Value* {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<frontend::Exp>>) {
                return generateExp(requireNode(primaryAlt, "parenthesized primary expression is missing an inner expression"), basicBlock, nextTempId);
            } else {
                return generateNumber(requireNode(primaryAlt, "primary expression number is missing"));
            }
        },
        primaryExp.m_kind);
}

::koopa::Value* Generator::generateUnaryExp(
    const frontend::UnaryExp& unaryExp,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    return std::visit(
        [&](const auto& unaryAlt) -> ::koopa::Value* {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<frontend::PrimaryExp>>) {
                return generatePrimaryExp(requireNode(unaryAlt, "unary expression is missing its primary expression"), basicBlock, nextTempId);
            } else {
                auto* operand = generateUnaryExp(requireNode(unaryAlt.second, "unary expression is missing its operand"), basicBlock, nextTempId);
                auto* zero = ::koopa::IntegerValue::get(0);
                switch (unaryAlt.first) {
                case frontend::UnaryOpKeyword::plus:
                    return generateBinaryValue(KOOPA_RBO_ADD, zero, operand, basicBlock, nextTempId);
                case frontend::UnaryOpKeyword::minus:
                    return generateBinaryValue(KOOPA_RBO_SUB, zero, operand, basicBlock, nextTempId);
                case frontend::UnaryOpKeyword::bang:
                    return generateBinaryValue(KOOPA_RBO_EQ, zero, operand, basicBlock, nextTempId);
                }
            }
            throw std::runtime_error("unsupported unary operator");
        },
        unaryExp.m_kind);
}

::koopa::Value* Generator::generateBooleanizedValue(
    ::koopa::Value* value,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    return generateBinaryValue(KOOPA_RBO_NOT_EQ, ::koopa::IntegerValue::get(0), value, basicBlock, nextTempId);
}

::koopa::BinaryValue* Generator::generateBinaryValue(
    koopa_raw_binary_op op,
    ::koopa::Value* lhs,
    ::koopa::Value* rhs,
    ::koopa::BasicBlock& basicBlock,
    int32_t& nextTempId) const {
    auto* binaryValue = ::koopa::BinaryValue::get(op, lhs, rhs, makeTempName(nextTempId));
    basicBlock.pushInst(binaryValue);
    return binaryValue;
}

::koopa::Value* Generator::generateNumber(const frontend::Number& number) const {
    return ::koopa::IntegerValue::get(number.m_value);
}

}  // namespace yesod::koopa_ir