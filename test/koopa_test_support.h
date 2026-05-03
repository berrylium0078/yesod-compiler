#ifndef _YESOD_TEST_KOOPA_TEST_SUPPORT_H_
#define _YESOD_TEST_KOOPA_TEST_SUPPORT_H_

#include <cstdlib>

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "frontend/ast.h"
#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "koopa/ast_to_koopa.h"
#include "koopa/mykoopa.h"

namespace yesod::test_support::koopa {

using yesod::frontend::CompUnit;
using yesod::frontend::ParseOutput;
using yesod::frontend::Parser;
using yesod::frontend::SemanticAnalyzer;
using namespace yesod::koopa;

static_assert(std::is_same_v<decltype(std::declval<CompUnit>().m_funcDef_nn),
    std::shared_ptr<yesod::frontend::FuncDef>>);

[[noreturn]] inline void fail(const std::string& message)
{
    std::cerr << "koopa_generator_test failure: " << message << std::endl;
    std::exit(1);
}

inline void require(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

inline ParseOutput parseSource(const std::string& source)
{
    Parser parser(source);
    return parser.parse();
}

inline std::shared_ptr<CompUnit> parseRoot(const std::string& source)
{
    auto output = parseSource(source);
    if (!output.success()) {
        fail("expected parse success before Koopa generation");
    }
    return output.m_root;
}

inline std::unique_ptr<Program> generateProgram(const std::string& source)
{
    SemanticAnalyzer semanticAnalyzer;
    const auto semanticOutput = semanticAnalyzer.analyze(*parseRoot(source));
    if (!semanticOutput.success()) {
        fail("expected semantic success before Koopa generation");
    }

    Generator generator;
    return std::unique_ptr<Program>(generator.generate(*semanticOutput.m_root));
}

inline const Function* requireOnlyFunction(const Program& program)
{
    require(program.getNumFuncs() == 1, "expected exactly one function");
    return program.getFunc(0);
}

inline const BasicBlock* requireEntryBlock(const Function& function)
{
    require(function.getNumBBs() == 1, "expected one basic block");
    const auto* basicBlock = function.getBB(0);
    require(
        basicBlock->isEntry(), "single basic block should be the entry block");
    require(basicBlock->getName() == "%entry",
        "entry block should use the documented label");
    return basicBlock;
}

inline const IntegerValue* requireInteger(
    const Value* value, int32_t expectedValue)
{
    require(value != nullptr, "expected integer value");
    require(value->isIntegerValue(), "expected integer value kind");
    const auto* integerValue = dynamic_cast<const IntegerValue*>(value);
    require(integerValue != nullptr, "expected integer value cast");
    require(integerValue->getVal() == expectedValue,
        "integer literal should preserve its payload");
    return integerValue;
}

inline const BinaryValue* requireBinary(const Value* value,
    koopa_raw_binary_op expectedOp, const std::string& expectedName)
{
    require(value != nullptr, "expected binary value");
    require(value->isBinaryValue(), "expected binary instruction");
    const auto* binaryValue = dynamic_cast<const BinaryValue*>(value);
    require(binaryValue != nullptr, "expected binary instruction cast");
    require(binaryValue->getOp() == expectedOp,
        "binary instruction should use the expected opcode");
    require(binaryValue->getName() == expectedName,
        "binary instruction should use the expected temporary name");
    return binaryValue;
}

inline const AllocValue* requireAlloc(
    const Value* value, const std::string& expectedName)
{
    require(value != nullptr, "expected alloc value");
    require(value->isAllocValue(), "expected alloc instruction");
    const auto* allocValue = dynamic_cast<const AllocValue*>(value);
    require(allocValue != nullptr, "expected alloc instruction cast");
    require(allocValue->getName() == expectedName,
        "alloc instruction should preserve the expected storage name");
    return allocValue;
}

inline const LoadValue* requireLoad(
    const Value* value, const Value* expectedSource, const std::string& expectedName)
{
    require(value != nullptr, "expected load value");
    require(value->isLoadValue(), "expected load instruction");
    const auto* loadValue = dynamic_cast<const LoadValue*>(value);
    require(loadValue != nullptr, "expected load instruction cast");
    require(loadValue->getSource() == expectedSource,
        "load instruction should read from the expected storage");
    require(loadValue->getName() == expectedName,
        "load instruction should use the expected temporary name");
    return loadValue;
}

inline const StoreValue* requireStore(
    const Value* value, const Value* expectedDestination)
{
    require(value != nullptr, "expected store value");
    require(value->isStoreValue(), "expected store instruction");
    const auto* storeValue = dynamic_cast<const StoreValue*>(value);
    require(storeValue != nullptr, "expected store instruction cast");
    require(storeValue->getDestination() == expectedDestination,
        "store instruction should target the expected storage");
    return storeValue;
}

inline const ReturnValue* requireReturn(const Value* value)
{
    require(value != nullptr, "expected return value");
    require(value->isReturnValue(), "expected return instruction");
    const auto* returnValue = dynamic_cast<const ReturnValue*>(value);
    require(returnValue != nullptr, "expected return instruction cast");
    return returnValue;
}

} // namespace yesod::test_support::koopa

#endif