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
        "expected generated assembly to contain: " + needle
            + "\nactual output:\n" + text);
}

void requireNotContains(const std::string& text, const std::string& needle)
{
    require(text.find(needle) == std::string::npos,
        "expected generated assembly not to contain: " + needle
            + "\nactual output:\n" + text);
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
    const std::string assembly = generateAssembly(
        "int main(){int value = 4; value = value + 5; return value;}");

    requireContains(assembly, "  addi sp, sp, -16\n");
    requireContains(assembly, "  li t2, 4\n");
    requireContains(assembly, "  sw t2, 0(sp)\n");
    requireContains(assembly, "  lw t1, 0(sp)\n");
    requireContains(assembly, "  sw t1, 4(sp)\n");
    requireContains(assembly, "  li t1, 5\n");
    requireContains(assembly, "  add t2, t0, t1\n");
    requireContains(assembly, "  sw t2, 8(sp)\n");
    requireContains(assembly, "  sw t2, 0(sp)\n");
    requireContains(assembly, "  lw a0, 12(sp)\n");
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
    auto function = Function::create(
        FunctionType::get(yesod::koopa::Int32Type::get(), {}), "@main");
    auto entry = BasicBlock::createEntry("%entry");
    auto firstTarget = BasicBlock::createNonEntry("%branch-target");
    auto secondTarget = BasicBlock::createNonEntry("%branch_target");

    entry->pushInst(BranchValue::get(
        IntegerValue::get(1), firstTarget, {}, secondTarget, {}));
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

void testFunctionCallLowering()
{
    const std::string assembly = generateAssembly(
        "int add(int lhs, int rhs){return lhs + rhs;}"
        "int main(){return add(4, 5);}");

    requireContains(assembly, "  .globl add\n");
    requireContains(assembly, "add:\n");
    requireContains(assembly, "  .globl main\n");
    requireContains(assembly, "main:\n");
    requireContains(assembly, "  li a0, 4\n");
    requireContains(assembly, "  li a1, 5\n");
    requireContains(assembly, "  call add\n");
}

void testNonLeafFunctionSavesReturnAddress()
{
    const std::string assembly = generateAssembly(
        "int id(int value){return value;}"
        "int main(){return id(7);}");

    requireContains(assembly, "  sw ra, ");
    requireContains(assembly, "  lw ra, ");
    requireContains(assembly, "  call id\n");
}

void testGlobalVariableLowering()
{
    const std::string assembly
        = generateAssembly("int global = 3; int main(){return global;}");

    requireContains(assembly, "  .data\n");
    requireContains(assembly, "  .globl global\n");
    requireContains(assembly, "global:\n");
    requireContains(assembly, "  .word 3\n");
    requireContains(assembly, "  .text\n");
    requireContains(assembly, "  la t0, global\n");
}

void testGlobalStoreLowering()
{
    const std::string assembly = generateAssembly(
        "int global = 1; int main(){global = global + 4; return global;}");

    requireContains(assembly, "  la t0, global\n");
    requireContains(assembly, "  lw t1, 0(t0)\n");
    requireContains(assembly, "  sw t2, 0(t0)\n");
}

void testExternalLibraryCallLowering()
{
    const std::string assembly
        = generateAssembly("int main(){putint(7); return 0;}");

    requireContains(assembly, "  li a0, 7\n");
    requireContains(assembly, "  call putint\n");
    requireContains(assembly, "  sw ra, ");
    requireContains(assembly, "  lw ra, ");
    requireNotContains(assembly, "putint:\n");
}

void testCallWithMoreThanEightArgumentsLowering()
{
    const std::string assembly = generateAssembly(
        "int sum10(int a0,int a1,int a2,int a3,int a4,int a5,int a6,int a7,int a8,int a9){"
        "return a0+a1+a2+a3+a4+a5+a6+a7+a8+a9;}"
        "int main(){return sum10(1,2,3,4,5,6,7,8,9,10);}");

    requireContains(assembly, "  li a0, 1\n");
    requireContains(assembly, "  li a7, 8\n");
    requireContains(assembly, "  sw t0, 0(sp)\n");
    requireContains(assembly, "  sw t0, 4(sp)\n");
    requireContains(assembly, "  call sum10\n");
}

void testRecursiveFunctionLowering()
{
    const std::string assembly = generateAssembly(
        "int fib(int n){"
        "if(n <= 1){return n;}"
        "return fib(n - 1) + fib(n - 2);"
        "}"
        "int main(){return fib(4);}");

    requireContains(assembly, "  .globl fib\n");
    requireContains(assembly, "fib:\n");
    requireContains(assembly, "  call fib\n");
    requireContains(assembly, "  sw ra, ");
    requireContains(assembly, "  lw ra, ");
}

void testCalleeReadsArgumentsBeyondA7FromStack()
{
    const std::string assembly = generateAssembly(
        "int sum10(int a0,int a1,int a2,int a3,int a4,int a5,int a6,int a7,int a8,int a9){"
        "return a8 + a9;"
        "}"
        "int main(){return sum10(1,2,3,4,5,6,7,8,9,10);}");

    requireContains(assembly, "sum10:\n");
    requireContains(assembly, "  lw t0, ");
    requireContains(assembly, "  call sum10\n");
    requireContains(assembly, "  sw t0, 0(sp)\n");
    requireContains(assembly, "  sw t0, 4(sp)\n");
}

void testGlobalArraySectionsAndNames()
{
    const std::string assembly = generateAssembly(
        "const int carr[3] = {1, 2, 3};"
        "int varr[3] = {4, 5, 6};"
        "int zeros[3];"
        "int main(){return carr[1] + varr[1] + zeros[1];}");

    requireContains(assembly, "  .section .rodata\n");
    requireContains(assembly, "  .globl c_carr\n");
    requireContains(assembly, "c_carr:\n");
    requireContains(assembly, "  .word 1\n");
    requireContains(assembly, "  .word 2\n");
    requireContains(assembly, "  .word 3\n");

    requireContains(assembly, "  .data\n");
    requireContains(assembly, "  .globl v_varr\n");
    requireContains(assembly, "v_varr:\n");
    requireContains(assembly, "  .word 4\n");
    requireContains(assembly, "  .word 5\n");
    requireContains(assembly, "  .word 6\n");

    requireContains(assembly, "  .bss\n");
    requireContains(assembly, "  .globl v_zeros\n");
    requireContains(assembly, "v_zeros:\n");
    requireContains(assembly, "  .zero 12\n");
}

void testArrayAddressingUsesScaledOffsets()
{
    const std::string assembly = generateAssembly(
        "int main(){int arr[4]; int i = 2; arr[i] = 7; return arr[i];}");

    requireContains(assembly, "  li t2, 4\n");
    requireContains(assembly, "  mul t1, t1, t2\n");
    requireContains(assembly, "  add t0, t0, t1\n");
}

} // namespace

int main()
{
    testLiteralReturn();
    testStackAllocatedStraightLineLowering();
    testConditionalBranchLowering();
    testSanitizedBlockLabelUniqueness();
    testFunctionCallLowering();
    testNonLeafFunctionSavesReturnAddress();
    testGlobalVariableLowering();
    testGlobalStoreLowering();
    testExternalLibraryCallLowering();
    testCallWithMoreThanEightArgumentsLowering();
    testRecursiveFunctionLowering();
    testCalleeReadsArgumentsBeyondA7FromStack();
    testGlobalArraySectionsAndNames();
    testArrayAddressingUsesScaledOffsets();
    return 0;
}