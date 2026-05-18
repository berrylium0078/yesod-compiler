#ifndef _YESOD_TEST_KOOPA_TEST_SUPPORT_H_
#define _YESOD_TEST_KOOPA_TEST_SUPPORT_H_

#include <cstdlib>

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include <cctype>

#include "frontend/ast.h"
#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "koopa/ast_to_koopa.h"
#include "koopa/mykoopa.h"

namespace yesod::test_support::koopa {

using yesod::frontend::CompUnit;
using yesod::frontend::Handle;
using yesod::frontend::ParseOutput;
using yesod::frontend::Parser;
using yesod::frontend::SemanticAnalyzer;
using namespace yesod::koopa;

static_assert(std::is_same_v<decltype(std::declval<CompUnit>().m_topLevelItems),
    std::vector<Handle<yesod::frontend::TopLevelItemNode>>>);

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

inline bool matchesExpectedTempName(
    const std::string& actualName, const std::string& expectedName)
{
    if (actualName == expectedName) {
        return true;
    }

    if (expectedName.size() < 2 || expectedName.front() != '%') {
        return false;
    }

    for (size_t i = 1; i < expectedName.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(expectedName[i]))) {
            return false;
        }
    }

    return actualName == "%t" + expectedName.substr(1);
}

struct ParsedOutput {
    ParsedOutput(const ParsedOutput&) = delete;
    ParsedOutput& operator=(const ParsedOutput&) = delete;
    ParsedOutput(ParsedOutput&& other)
        : m_output(std::move(other.m_output))
        , m_root(m_output.m_root)
        , m_diagnostics(m_output.m_diagnostics)
        , m_scope(std::move(other.m_scope))
    {
        m_scope.rebind(m_output.m_ast);
    }

    ParsedOutput& operator=(ParsedOutput&& other)
    {
        m_output = std::move(other.m_output);
        m_root = m_output.m_root;
        m_diagnostics = m_output.m_diagnostics;
        m_scope = std::move(other.m_scope);
        m_scope.rebind(m_output.m_ast);
        return *this;
    }

    explicit ParsedOutput(ParseOutput output)
        : m_output(std::move(output))
        , m_root(m_output.m_root)
        , m_diagnostics(m_output.m_diagnostics)
        , m_scope(m_output.m_ast.bindCurrent())
    {
    }

    [[nodiscard]] bool success() const { return m_output.success(); }

    [[nodiscard]] const CompUnit* operator->() const
    {
        return m_root ? &m_output.m_ast.get(m_root) : nullptr;
    }

    ParseOutput m_output;
    Handle<CompUnit> m_root;
    std::vector<yesod::frontend::Diagnostic> m_diagnostics;
    yesod::frontend::AST::ScopedCurrent m_scope;
};

inline ParsedOutput parseSource(const std::string& source)
{
    Parser parser(source);
    return ParsedOutput(parser.parse());
}

inline ParsedOutput parseRoot(const std::string& source)
{
    auto output = parseSource(source);
    if (!output.success()) {
        fail("expected parse success before Koopa generation");
    }
    return output;
}

inline Handle<yesod::frontend::FuncDef> firstFuncDef(
    const Handle<CompUnit>& compUnit_nn)
{
    require(compUnit_nn != nullptr, "expected compilation unit node");
    for (const auto topLevelItem_nn : compUnit_nn->m_topLevelItems) {
        Handle<yesod::frontend::FuncDef> funcDef_nn;
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType,
                                  Handle<yesod::frontend::FuncDef>>) {
                    funcDef_nn = topLevelAlt;
                }
            },
            topLevelItem_nn->m_topLevelItem);
        if (funcDef_nn) {
            return funcDef_nn;
        }
    }
    fail("expected at least one function definition in compilation unit");
}

inline std::unique_ptr<Program> generateProgram(const std::string& source)
{
    auto rootOutput = parseRoot(source);
    SemanticAnalyzer semanticAnalyzer;
    auto semanticOutput = semanticAnalyzer.analyze(
        std::move(rootOutput.m_output.m_ast), rootOutput.m_root);
    if (!semanticOutput.success()) {
        fail("expected semantic success before Koopa generation");
    }

    Generator generator;
    return std::unique_ptr<Program>(generator.generate(
        semanticOutput.m_ast, semanticOutput.m_root, semanticOutput.m_info));
}

inline const Function* requireOnlyFunction(const Program& program)
{
    require(program.getNumFuncs() == 1, "expected exactly one function");
    return program.getFunc(0);
}

inline const Function* requireFunctionByName(
    const Program& program, const std::string& expectedName)
{
    for (const auto* function : program.funcs()) {
        if (function->getName() == expectedName) {
            return function;
        }
    }
    fail("expected function named '" + expectedName + "'");
}

inline const GlobalAllocValue* requireGlobalAlloc(
    const Value* value, const std::string& expectedName)
{
    require(value != nullptr, "expected global alloc value");
    require(value->isGlobalAllocValue(), "expected global alloc value kind");
    const auto* globalAlloc = dynamic_cast<const GlobalAllocValue*>(value);
    require(globalAlloc != nullptr, "expected global alloc value cast");
    require(globalAlloc->getName() == expectedName,
        "global alloc should preserve the expected name");
    return globalAlloc;
}

inline const AggregateValue* requireAggregate(
    const Value* value, size_t expectedNumElements)
{
    require(value != nullptr, "expected aggregate value");
    require(value->isAggregateValue(), "expected aggregate initializer value");
    const auto* aggregateValue = dynamic_cast<const AggregateValue*>(value);
    require(aggregateValue != nullptr, "expected aggregate initializer cast");
    require(aggregateValue->getNumElements() == expectedNumElements,
        "aggregate initializer should preserve the expected number of elements");
    return aggregateValue;
}

inline const BasicBlock* requireEntryBlock(const Function& function)
{
    require(function.getNumBBs() >= 2,
        "expected entry block plus synthesized guard end block");
    const auto* basicBlock = function.getBB(0);
    require(
        basicBlock->isEntry(), "first basic block should be the entry block");
    require(basicBlock->getName() == "%entry",
        "entry block should use the documented label");
    return basicBlock;
}

inline const BasicBlock* requireEndBlock(const Function& function)
{
    require(function.getNumBBs() >= 2,
        "expected entry block plus synthesized guard end block");
    const auto* basicBlock = function.getBB(function.getNumBBs() - 1);
    require(!basicBlock->isEntry(),
        "guard end block should not be the entry block");
    require(basicBlock->getName() == "%end",
        "guard end block should use the documented label");
    return basicBlock;
}

inline const BasicBlock* requireBlock(const Function& function, size_t index)
{
    require(index < function.getNumBBs(),
        "expected basic block at requested index");
    return function.getBB(index);
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
    require(matchesExpectedTempName(binaryValue->getName(), expectedName),
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
    require(matchesExpectedTempName(allocValue->getName(), expectedName)
            || allocValue->getName() == expectedName,
        "alloc instruction should preserve the expected storage name");
    return allocValue;
}

inline const LoadValue* requireLoad(const Value* value,
    const Value* expectedSource, const std::string& expectedName)
{
    require(value != nullptr, "expected load value");
    require(value->isLoadValue(), "expected load instruction");
    const auto* loadValue = dynamic_cast<const LoadValue*>(value);
    require(loadValue != nullptr, "expected load instruction cast");
    require(loadValue->getSource() == expectedSource,
        "load instruction should read from the expected storage");
    require(matchesExpectedTempName(loadValue->getName(), expectedName),
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

inline const CallValue* requireCall(
    const Value* value, const Function* expectedCallee)
{
    require(value != nullptr, "expected call value");
    require(value->isCallValue(), "expected call instruction");
    const auto* callValue = dynamic_cast<const CallValue*>(value);
    require(callValue != nullptr, "expected call instruction cast");
    require(callValue->getCallee() == expectedCallee,
        "call instruction should target the expected function");
    return callValue;
}

inline const JumpValue* requireJump(
    const Value* value, const BasicBlock* expectedTarget)
{
    require(value != nullptr, "expected jump value");
    require(value->isJumpValue(), "expected jump instruction");
    const auto* jumpValue = dynamic_cast<const JumpValue*>(value);
    require(jumpValue != nullptr, "expected jump instruction cast");
    require(jumpValue->getTargetBB() == expectedTarget,
        "jump instruction should target the expected basic block");
    require(jumpValue->getNumArgs() == 0,
        "current subset should not emit block arguments on jumps");
    return jumpValue;
}

inline const BranchValue* requireBranch(const Value* value,
    const BasicBlock* expectedTrueTarget, const BasicBlock* expectedFalseTarget)
{
    require(value != nullptr, "expected branch value");
    require(value->isBranchValue(), "expected branch instruction");
    const auto* branchValue = dynamic_cast<const BranchValue*>(value);
    require(branchValue != nullptr, "expected branch instruction cast");
    require(branchValue->getTrueBB() == expectedTrueTarget,
        "branch instruction should target the expected true basic block");
    require(branchValue->getFalseBB() == expectedFalseTarget,
        "branch instruction should target the expected false basic block");
    require(branchValue->getNumTrueArgs() == 0,
        "current subset should not emit block arguments on true branches");
    require(branchValue->getNumFalseArgs() == 0,
        "current subset should not emit block arguments on false branches");
    return branchValue;
}

inline const GetElemPtrValue* requireGetElemPtr(
    const Value* value, const Value* expectedSource)
{
    require(value != nullptr, "expected getelemptr value");
    require(value->isGetElemPtrValue(), "expected getelemptr instruction");
    const auto* getElemPtrValue = dynamic_cast<const GetElemPtrValue*>(value);
    require(getElemPtrValue != nullptr, "expected getelemptr instruction cast");
    require(getElemPtrValue->getSource() == expectedSource,
        "getelemptr should use the expected source storage");
    return getElemPtrValue;
}

inline const GetPtrValue* requireGetPtr(
    const Value* value, const Value* expectedSource)
{
    require(value != nullptr, "expected getptr value");
    require(value->isGetPtrValue(), "expected getptr instruction");
    const auto* getPtrValue = dynamic_cast<const GetPtrValue*>(value);
    require(getPtrValue != nullptr, "expected getptr instruction cast");
    require(getPtrValue->getSource() == expectedSource,
        "getptr should use the expected source storage");
    return getPtrValue;
}

} // namespace yesod::test_support::koopa

#endif