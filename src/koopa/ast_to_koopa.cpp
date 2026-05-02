#include "koopa/ast_to_koopa.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
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

std::string makeFunctionName(const frontend::Identifier& identifier)
{
    return "@" + identifier.m_name;
}

std::string makeTempName(int32_t& nextTempId)
{
    return "%" + std::to_string(nextTempId++);
}

Program* Generator::generate(const frontend::CompUnit& compUnit) const
{
    auto* program = Program::create();
    program->pushFunc(generateFuncDef(requireNode(compUnit.m_funcDef_nn,
        "compilation unit is missing a function definition")));
    return program;
}

Function* Generator::generateFuncDef(const frontend::FuncDef& funcDef) const
{
    int32_t nextTempId = 1;
    auto* function
        = Function::create(FunctionType::get(lowerFuncType(funcDef.m_funcType),
                               std::vector<Type*> {}),
            makeFunctionName(requireNode(funcDef.m_identifier_nn,
                "function definition is missing an identifier")));
    function->pushBB(
        generateBlock(requireNode(funcDef.m_block_nn,
                          "function definition is missing a block"),
            nextTempId));
    function->validate();
    return function;
}

BasicBlock* Generator::generateBlock(
    const frontend::Block& block, int32_t& nextTempId) const
{
    auto* basicBlock = BasicBlock::createEntry("%entry");
    for (const auto& stmtNode : block.m_statements) {
        generateStmt(requireNode(stmtNode, "block contains a null statement"),
            *basicBlock, nextTempId);
    }
    basicBlock->validate();
    return basicBlock;
}

void Generator::generateStmt(const frontend::StmtNode& stmtNode,
    BasicBlock& basicBlock, int32_t& nextTempId) const
{
    std::visit(
        [&](const auto& stmtAlt) -> Value* {
            return generateReturnStmt(
                requireNode(
                    stmtAlt, "statement variant contains a null payload"),
                basicBlock, nextTempId);
        },
        stmtNode.m_stmt);
}

ReturnValue* Generator::generateReturnStmt(
    const frontend::ReturnStmt& returnStmt, BasicBlock& basicBlock,
    int32_t& nextTempId) const
{
    auto* returnValue = ReturnValue::get(generateExp(
        requireNode(returnStmt.m_exp_nn, "return statement is missing a value"),
        basicBlock, nextTempId));
    basicBlock.pushInst(returnValue);
    return returnValue;
}

Value* Generator::generateExp(
    const frontend::Exp& exp, BasicBlock& basicBlock, int32_t& nextTempId) const
{
    return generateLOrExp(requireNode(exp.m_lOrExp_nn,
                              "expression is missing a logical-or expression"),
        basicBlock, nextTempId);
}

Value* Generator::generateLOrExp(const frontend::LOrExp& lOrExp,
    BasicBlock& basicBlock, int32_t& nextTempId) const
{
    auto* currentValue
        = generateLAndExp(requireNode(lOrExp.m_head_nn,
                              "logical-or expression is missing its head"),
            basicBlock, nextTempId);
    for (const auto& tailEntry : lOrExp.m_tail) {
        auto* lhsBool
            = generateBooleanizedValue(currentValue, basicBlock, nextTempId);
        auto* rhsValue = generateLAndExp(
            requireNode(tailEntry.second,
                "logical-or expression is missing its operand"),
            basicBlock, nextTempId);
        auto* rhsBool
            = generateBooleanizedValue(rhsValue, basicBlock, nextTempId);
        currentValue = generateBinaryValue(
            KOOPA_RBO_OR, lhsBool, rhsBool, basicBlock, nextTempId);
    }
    return currentValue;
}

Value* Generator::generateLAndExp(const frontend::LAndExp& lAndExp,
    BasicBlock& basicBlock, int32_t& nextTempId) const
{
    auto* currentValue
        = generateEqExp(requireNode(lAndExp.m_head_nn,
                            "logical-and expression is missing its head"),
            basicBlock, nextTempId);
    for (const auto& tailEntry : lAndExp.m_tail) {
        auto* lhsBool
            = generateBooleanizedValue(currentValue, basicBlock, nextTempId);
        auto* rhsValue = generateEqExp(
            requireNode(tailEntry.second,
                "logical-and expression is missing its operand"),
            basicBlock, nextTempId);
        auto* rhsBool
            = generateBooleanizedValue(rhsValue, basicBlock, nextTempId);
        currentValue = generateBinaryValue(
            KOOPA_RBO_AND, lhsBool, rhsBool, basicBlock, nextTempId);
    }
    return currentValue;
}

Value* Generator::generateEqExp(const frontend::EqExp& eqExp,
    BasicBlock& basicBlock, int32_t& nextTempId) const
{
    auto* currentValue = generateRelExp(
        requireNode(eqExp.m_head_nn, "equality expression is missing its head"),
        basicBlock, nextTempId);
    for (const auto& tailEntry : eqExp.m_tail) {
        auto* rhsValue
            = generateRelExp(requireNode(tailEntry.second,
                                 "equality expression is missing its operand"),
                basicBlock, nextTempId);
        switch (tailEntry.first) {
        case frontend::EqOpKeyword::equal:
            currentValue = generateBinaryValue(
                KOOPA_RBO_EQ, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::EqOpKeyword::notEqual:
            currentValue = generateBinaryValue(KOOPA_RBO_NOT_EQ, currentValue,
                rhsValue, basicBlock, nextTempId);
            break;
        }
    }
    return currentValue;
}

Value* Generator::generateRelExp(const frontend::RelExp& relExp,
    BasicBlock& basicBlock, int32_t& nextTempId) const
{
    auto* currentValue
        = generateAddExp(requireNode(relExp.m_head_nn,
                             "relational expression is missing its head"),
            basicBlock, nextTempId);
    for (const auto& tailEntry : relExp.m_tail) {
        auto* rhsValue = generateAddExp(
            requireNode(tailEntry.second,
                "relational expression is missing its operand"),
            basicBlock, nextTempId);
        switch (tailEntry.first) {
        case frontend::RelOpKeyword::less:
            currentValue = generateBinaryValue(
                KOOPA_RBO_LT, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::RelOpKeyword::greater:
            currentValue = generateBinaryValue(
                KOOPA_RBO_GT, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::RelOpKeyword::lessEqual:
            currentValue = generateBinaryValue(
                KOOPA_RBO_LE, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::RelOpKeyword::greaterEqual:
            currentValue = generateBinaryValue(
                KOOPA_RBO_GE, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        }
    }
    return currentValue;
}

Value* Generator::generateAddExp(const frontend::AddExp& addExp,
    BasicBlock& basicBlock, int32_t& nextTempId) const
{
    auto* currentValue
        = generateMulExp(requireNode(addExp.m_head_nn,
                             "additive expression is missing its head"),
            basicBlock, nextTempId);
    for (const auto& tailEntry : addExp.m_tail) {
        auto* rhsValue
            = generateMulExp(requireNode(tailEntry.second,
                                 "additive expression is missing its operand"),
                basicBlock, nextTempId);
        switch (tailEntry.first) {
        case frontend::AddOpKeyword::plus:
            currentValue = generateBinaryValue(
                KOOPA_RBO_ADD, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::AddOpKeyword::minus:
            currentValue = generateBinaryValue(
                KOOPA_RBO_SUB, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        }
    }
    return currentValue;
}

Value* Generator::generateMulExp(const frontend::MulExp& mulExp,
    BasicBlock& basicBlock, int32_t& nextTempId) const
{
    auto* currentValue
        = generateUnaryExp(requireNode(mulExp.m_head_nn,
                               "multiplicative expression is missing its head"),
            basicBlock, nextTempId);
    for (const auto& tailEntry : mulExp.m_tail) {
        auto* rhsValue = generateUnaryExp(
            requireNode(tailEntry.second,
                "multiplicative expression is missing its operand"),
            basicBlock, nextTempId);
        switch (tailEntry.first) {
        case frontend::MulOpKeyword::star:
            currentValue = generateBinaryValue(
                KOOPA_RBO_MUL, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::MulOpKeyword::slash:
            currentValue = generateBinaryValue(
                KOOPA_RBO_DIV, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        case frontend::MulOpKeyword::percent:
            currentValue = generateBinaryValue(
                KOOPA_RBO_MOD, currentValue, rhsValue, basicBlock, nextTempId);
            break;
        }
    }
    return currentValue;
}

Value* Generator::generatePrimaryExp(const frontend::PrimaryExp& primaryExp,
    BasicBlock& basicBlock, int32_t& nextTempId) const
{
    return std::visit(
        [&](const auto& primaryAlt) -> Value* {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::Exp>>) {
                return generateExp(requireNode(primaryAlt,
                                       "parenthesized primary expression is "
                                       "missing an inner expression"),
                    basicBlock, nextTempId);
            } else {
                return generateNumber(requireNode(
                    primaryAlt, "primary expression number is missing"));
            }
        },
        primaryExp.m_kind);
}

Value* Generator::generateUnaryExp(const frontend::UnaryExp& unaryExp,
    BasicBlock& basicBlock, int32_t& nextTempId) const
{
    return std::visit(
        [&](const auto& unaryAlt) -> Value* {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<frontend::PrimaryExp>>) {
                return generatePrimaryExp(
                    requireNode(unaryAlt,
                        "unary expression is missing its primary expression"),
                    basicBlock, nextTempId);
            } else {
                auto* operand = generateUnaryExp(
                    requireNode(unaryAlt.second,
                        "unary expression is missing its operand"),
                    basicBlock, nextTempId);
                auto* zero = IntegerValue::get(0);
                switch (unaryAlt.first) {
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
            }
            throw std::runtime_error("unsupported unary operator");
        },
        unaryExp.m_kind);
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

} // namespace yesod::koopa