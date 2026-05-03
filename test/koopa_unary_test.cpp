#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testParenthesizedLiteralDoesNotAddInstructions()
{
    auto program = generateProgram("int main(){return (5);}");
    const auto* function = requireOnlyFunction(*program);
    const auto* basicBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);

    require(basicBlock->getNumInsts() == 1,
        "parenthesized literal should not add intermediate instructions");
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 5);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testUnaryLoweringUsesCorrectBinaryOps()
{
    {
        auto program = generateProgram("int main(){return +5;}");
        const auto* function = requireOnlyFunction(*program);
        const auto* basicBlock = requireEntryBlock(*function);
        const auto* endBlock = requireEndBlock(*function);
        require(basicBlock->getNumInsts() == 1,
            "constant unary plus should fold to a literal return");
        requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 5);
        requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
    }

    {
        auto program = generateProgram("int main(){return -42;}");
        const auto* function = requireOnlyFunction(*program);
        const auto* basicBlock = requireEntryBlock(*function);
        const auto* endBlock = requireEndBlock(*function);
        require(basicBlock->getNumInsts() == 1,
            "constant unary minus should fold to a literal return");
        requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), -42);
        requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
    }

    {
        auto program = generateProgram("int main(){return !10;}");
        const auto* function = requireOnlyFunction(*program);
        const auto* basicBlock = requireEntryBlock(*function);
        const auto* endBlock = requireEndBlock(*function);
        require(basicBlock->getNumInsts() == 1,
            "constant logical not should fold to a literal return");
        requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 0);
        requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
    }
}

void testNestedUnaryExpressionsAllocateSequentialTemps()
{
    auto program = generateProgram("int main(){return -(+42);}");
    const auto* function = requireOnlyFunction(*program);
    const auto* basicBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);

    require(basicBlock->getNumInsts() == 1,
        "nested constant unary expression should fold to a literal return");
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), -42);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

} // namespace

int main()
{
    testParenthesizedLiteralDoesNotAddInstructions();
    testUnaryLoweringUsesCorrectBinaryOps();
    testNestedUnaryExpressionsAllocateSequentialTemps();
    return 0;
}