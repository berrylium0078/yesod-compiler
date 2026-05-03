#include <cstdlib>

#include <iostream>
#include <sstream>
#include <string>

#include "backend/riscv.h"
#include "koopa_test_support.h"

namespace {

using yesod::backend::RiscvGenerator;
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

} // namespace

int main()
{
    testLiteralReturn();
    testStackAllocatedStraightLineLowering();
    return 0;
}