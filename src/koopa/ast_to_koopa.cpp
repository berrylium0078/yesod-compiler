#include "koopa/ast_to_koopa.h"

#include <memory>
#include <stdexcept>
#include <string>
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

}  // namespace

::koopa::Program* Generator::generate(const frontend::CompUnit& compUnit) const {
    auto* program = ::koopa::Program::create();
    program->pushFunc(generateFuncDef(requireNode(compUnit.m_funcDef_nn, "compilation unit is missing a function definition")));
    return program;
}

::koopa::Function* Generator::generateFuncDef(const frontend::FuncDef& funcDef) const {
    auto* function = ::koopa::Function::create(
        ::koopa::FunctionType::get(lowerFuncType(funcDef.m_funcType), std::vector<::koopa::Type*> {}),
        makeFunctionName(requireNode(funcDef.m_identifier_nn, "function definition is missing an identifier")));
    function->pushBB(generateBlock(requireNode(funcDef.m_block_nn, "function definition is missing a block")));
    function->validate();
    return function;
}

::koopa::BasicBlock* Generator::generateBlock(const frontend::Block& block) const {
    auto* basicBlock = ::koopa::BasicBlock::createEntry("%entry");
    for (const auto& stmtNode : block.m_statements) {
        basicBlock->pushInst(generateStmt(requireNode(stmtNode, "block contains a null statement")));
    }
    basicBlock->validate();
    return basicBlock;
}

::koopa::Value* Generator::generateStmt(const frontend::StmtNode& stmtNode) const {
    return std::visit(
        [&](const auto& stmtAlt) -> ::koopa::Value* {
            return generateReturnStmt(requireNode(stmtAlt, "statement variant contains a null payload"));
        },
        stmtNode.m_stmt);
}

::koopa::ReturnValue* Generator::generateReturnStmt(const frontend::ReturnStmt& returnStmt) const {
    return ::koopa::ReturnValue::get(generateNumber(requireNode(returnStmt.m_value_nn, "return statement is missing a value")));
}

::koopa::Value* Generator::generateNumber(const frontend::Number& number) const {
    return ::koopa::IntegerValue::get(number.m_value);
}

}  // namespace yesod::koopa_ir