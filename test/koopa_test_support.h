#ifndef _YESOD_TEST_KOOPA_TEST_SUPPORT_H_
#define _YESOD_TEST_KOOPA_TEST_SUPPORT_H_

#include <cstdlib>

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "frontend/ast.h"
#include "frontend/parser.h"
#include "koopa/ast_to_koopa.h"
#include "koopa/mykoopa.h"

namespace yesod::test_support::koopa {

using yesod::frontend::CompUnit;
using yesod::frontend::ParseOutput;
using yesod::frontend::Parser;
using yesod::koopa_ir::Generator;

static_assert(std::is_same_v<decltype(std::declval<CompUnit>().m_funcDef_nn), std::shared_ptr<yesod::frontend::FuncDef>>);

[[noreturn]] inline void fail(const std::string& message) {
    std::cerr << "koopa_generator_test failure: " << message << std::endl;
    std::exit(1);
}

inline void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

inline ParseOutput parseSource(const std::string& source) {
    Parser parser(source);
    return parser.parse();
}

inline std::shared_ptr<CompUnit> parseRoot(const std::string& source) {
    auto output = parseSource(source);
    if (!output.success()) {
        fail("expected parse success before Koopa generation");
    }
    return output.m_root;
}

inline std::unique_ptr<::koopa::Program> generateProgram(const std::string& source) {
    Generator generator;
    return std::unique_ptr<::koopa::Program>(generator.generate(*parseRoot(source)));
}

inline const ::koopa::Function* requireOnlyFunction(const ::koopa::Program& program) {
    require(program.getNumFuncs() == 1, "expected exactly one function");
    return program.getFunc(0);
}

inline const ::koopa::BasicBlock* requireEntryBlock(const ::koopa::Function& function) {
    require(function.getNumBBs() == 1, "expected one basic block");
    const auto* basicBlock = function.getBB(0);
    require(basicBlock->isEntry(), "single basic block should be the entry block");
    require(basicBlock->getName() == "%entry", "entry block should use the documented label");
    return basicBlock;
}

inline const ::koopa::IntegerValue* requireInteger(const ::koopa::Value* value, int32_t expectedValue) {
    require(value != nullptr, "expected integer value");
    require(value->isIntegerValue(), "expected integer value kind");
    const auto* integerValue = dynamic_cast<const ::koopa::IntegerValue*>(value);
    require(integerValue != nullptr, "expected integer value cast");
    require(integerValue->getVal() == expectedValue, "integer literal should preserve its payload");
    return integerValue;
}

inline const ::koopa::BinaryValue* requireBinary(
    const ::koopa::Value* value,
    koopa_raw_binary_op expectedOp,
    const std::string& expectedName) {
    require(value != nullptr, "expected binary value");
    require(value->isBinaryValue(), "expected binary instruction");
    const auto* binaryValue = dynamic_cast<const ::koopa::BinaryValue*>(value);
    require(binaryValue != nullptr, "expected binary instruction cast");
    require(binaryValue->getOp() == expectedOp, "binary instruction should use the expected opcode");
    require(binaryValue->getName() == expectedName, "binary instruction should use the expected temporary name");
    return binaryValue;
}

inline const ::koopa::ReturnValue* requireReturn(const ::koopa::Value* value) {
    require(value != nullptr, "expected return value");
    require(value->isReturnValue(), "expected return instruction");
    const auto* returnValue = dynamic_cast<const ::koopa::ReturnValue*>(value);
    require(returnValue != nullptr, "expected return instruction cast");
    return returnValue;
}

}  // namespace yesod::test_support::koopa

#endif