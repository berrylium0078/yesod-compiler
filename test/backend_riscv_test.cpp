#include <cstdlib>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "backend/riscv.h"
#include "koopa_test_support.h"

namespace {

using yesod::backend::RiscvGenerator;
using yesod::koopa::BasicBlock;
using yesod::koopa::BranchValue;
using yesod::koopa::Function;
using yesod::koopa::FunctionType;
using yesod::koopa::IntegerValue;
using yesod::koopa::Program;
using yesod::koopa::ReturnValue;
using yesod::test_support::koopa::generateProgram;

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "backend_riscv_test failure: " << message << std::endl;
    std::exit(1);
}

void require(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

std::string generateAssembly(const std::string& source)
{
    auto program = generateProgram(source);
    std::ostringstream output;
    RiscvGenerator generator;
    generator.generate(*program, output);
    return output.str();
}

std::string generateAssembly(const Program& program)
{
    std::ostringstream output;
    RiscvGenerator generator;
    generator.generate(program, output);
    return output.str();
}

void requireContains(const std::string& text, const std::string& needle)
{
    require(text.find(needle) != std::string::npos,
        "expected generated assembly to contain: " + needle + "\nactual output:\n" + text);
}

void testLiteralReturn()
{
    const std::string assembly = generateAssembly("int main(){return 0;}");

    requireContains(assembly, "  .text\n");
    requireContains(assembly, "  .globl main\n");
    requireContains(assembly, "main:\n");
    requireContains(assembly, "  li a0, 0\n");
    requireContains(assembly, "  ret\n");
}

void testStackAllocatedStraightLineLowering()
{
    const std::string assembly
        = generateAssembly("int main(){int value = 4; value = value + 5; return value;}");

    requireContains(assembly, "  addi sp, sp, -16\n");
    requireContains(assembly, "  li t0, 4\n");
    requireContains(assembly, "  sw t0, -4(sp)\n");
    requireContains(assembly, "  lw t0, -4(sp)\n");
    requireContains(assembly, "  sw t0, -8(sp)\n");
    requireContains(assembly, "  li t1, 5\n");
    requireContains(assembly, "  add t2, t0, t1\n");
    requireContains(assembly, "  sw t2, -12(sp)\n");
    requireContains(assembly, "  sw t0, -16(sp)\n");
    requireContains(assembly, "  lw a0, -16(sp)\n");
    requireContains(assembly, "  addi sp, sp, 16\n");
}

void testConditionalBranchLowering()
{
    const std::string assembly
        = generateAssembly("int main(){if(1){return 2;} return 3;}");

    requireContains(assembly, "  bnez t0, main_if_then_1\n");
    requireContains(assembly, "  j main_if_end_2\n");
    requireContains(assembly, "main_if_then_1:\n");
    requireContains(assembly, "  li a0, 2\n");
    requireContains(assembly, "main_if_end_2:\n");
    requireContains(assembly, "  li a0, 3\n");
}

void testSanitizedBlockLabelUniqueness()
{
    auto program = std::unique_ptr<Program>(Program::create());
    auto function = Function::create(FunctionType::get(yesod::koopa::Int32Type::get(), {}), "@main");
    auto entry = BasicBlock::createEntry("%entry");
    auto firstTarget = BasicBlock::createNonEntry("%branch-target");
    auto secondTarget = BasicBlock::createNonEntry("%branch_target");

    entry->pushInst(BranchValue::get(IntegerValue::get(1), firstTarget, {}, secondTarget, {}));
    firstTarget->pushInst(ReturnValue::get(IntegerValue::get(1)));
    secondTarget->pushInst(ReturnValue::get(IntegerValue::get(0)));

    function->pushBB(entry);
    function->pushBB(firstTarget);
    function->pushBB(secondTarget);
    function->validate();
    program->pushFunc(function);

    const std::string assembly = generateAssembly(*program);

    requireContains(assembly, "  bnez t0, main_branch_target\n");
    requireContains(assembly, "  j main_branch_target_2\n");
    requireContains(assembly, "main_branch_target:\n");
    requireContains(assembly, "main_branch_target_2:\n");
}

} // namespace

int main()
{
    testLiteralReturn();
    testStackAllocatedStraightLineLowering();
    testConditionalBranchLowering();
    testSanitizedBlockLabelUniqueness();
    return 0;
}