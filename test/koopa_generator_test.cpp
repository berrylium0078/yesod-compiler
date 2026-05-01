#include <cstdlib>

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>

#include "frontend/ast.h"
#include "frontend/parser.h"
#include "koopa/ast_to_koopa.h"
#include "koopa/mykoopa.h"

using yesod::frontend::CompUnit;
using yesod::frontend::ParseOutput;
using yesod::frontend::Parser;
using yesod::koopa_ir::Generator;

namespace {

static_assert(std::is_same_v<decltype(std::declval<CompUnit>().m_funcDef_nn), std::shared_ptr<yesod::frontend::FuncDef>>);

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "koopa_generator_test failure: " << message << std::endl;
    std::exit(1);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

ParseOutput parseSource(const std::string& source) {
    Parser parser(source);
    return parser.parse();
}

std::shared_ptr<CompUnit> parseRoot(const std::string& source) {
    auto output = parseSource(source);
    if (!output.success()) {
        fail("expected parse success before Koopa generation");
    }
    return output.m_root;
}

void testGeneratorBuildsExpectedWrapperObjects() {
    const auto root_nn = parseRoot("int main(){return 42;}");

    Generator generator;
    std::unique_ptr<::koopa::Program> program(generator.generate(*root_nn));

    require(program->getNumVals() == 0, "minimal SysY subset should not emit globals");
    require(program->getNumFuncs() == 1, "minimal SysY subset should emit one function");

    const auto* function = program->getFunc(0);
    require(function->getName() == "@main", "function name should use Koopa symbol format");
    require(function->getNumParams() == 0, "minimal function should not emit parameters");
    require(function->getNumBBs() == 1, "function should contain one entry block");

    const auto* basicBlock = function->getBB(0);
    require(basicBlock->isEntry(), "single block should be the entry block");
    require(basicBlock->getName() == "%entry", "entry block should use the documented Koopa label");
    require(basicBlock->getNumInsts() == 1, "entry block should only contain the return instruction");

    const auto* returnValue = basicBlock->getInst(0);
    require(returnValue->isReturnValue(), "statement should lower to a return instruction");
    const auto* integerValue = dynamic_cast<const ::koopa::ReturnValue*>(returnValue)->getVal();
    require(integerValue != nullptr, "return instruction should carry an integer payload");
    require(integerValue->isIntegerValue(), "return payload should be lowered as an integer value");
    require(dynamic_cast<const ::koopa::IntegerValue*>(integerValue)->getVal() == 42, "return payload should preserve the parsed integer literal");
}

void testGeneratedProgramValidatesWithKoopa() {
    const auto root_nn = parseRoot("int answer(){return 0x2a;}");

    Generator generator;
    std::unique_ptr<::koopa::Program> program(generator.generate(*root_nn));
    auto rawProgram = ::koopa::Program::dumpRaw(program.get());
    koopa_program_t koopaProgram = nullptr;
    const auto errorCode = koopa_generate_raw_to_koopa(&rawProgram, &koopaProgram);
    require(errorCode == KOOPA_EC_SUCCESS, "generated raw program should be accepted by Koopa");
    koopa_delete_program(koopaProgram);
}

}  // namespace

int main() {
    testGeneratorBuildsExpectedWrapperObjects();
    testGeneratedProgramValidatesWithKoopa();
    return 0;
}