#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testParenthesizedLiteralDoesNotAddInstructions()
{
    auto program = generateProgram("int main(){return (5);}");
    const auto* basicBlock = requireEntryBlock(*requireOnlyFunction(*program));

    require(basicBlock->getNumInsts() == 1,
        "parenthesized literal should not add intermediate instructions");
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 5);
}

void testUnaryLoweringUsesCorrectBinaryOps()
{
    {
        auto program = generateProgram("int main(){return +5;}");
        const auto* basicBlock
            = requireEntryBlock(*requireOnlyFunction(*program));
        require(basicBlock->getNumInsts() == 2,
            "unary plus should emit one binary temp and one return");
        const auto* binaryValue
            = requireBinary(basicBlock->getInst(0), KOOPA_RBO_ADD, "%1");
        requireInteger(binaryValue->getLhs(), 0);
        requireInteger(binaryValue->getRhs(), 5);
        require(requireReturn(basicBlock->getInst(1))->getVal() == binaryValue,
            "return should use the unary plus temporary");
    }

    {
        auto program = generateProgram("int main(){return -42;}");
        const auto* basicBlock
            = requireEntryBlock(*requireOnlyFunction(*program));
        require(basicBlock->getNumInsts() == 2,
            "unary minus should emit one binary temp and one return");
        const auto* binaryValue
            = requireBinary(basicBlock->getInst(0), KOOPA_RBO_SUB, "%1");
        requireInteger(binaryValue->getLhs(), 0);
        requireInteger(binaryValue->getRhs(), 42);
        require(requireReturn(basicBlock->getInst(1))->getVal() == binaryValue,
            "return should use the unary minus temporary");
    }

    {
        auto program = generateProgram("int main(){return !10;}");
        const auto* basicBlock
            = requireEntryBlock(*requireOnlyFunction(*program));
        require(basicBlock->getNumInsts() == 2,
            "logical not should emit one binary temp and one return");
        const auto* binaryValue
            = requireBinary(basicBlock->getInst(0), KOOPA_RBO_EQ, "%1");
        requireInteger(binaryValue->getLhs(), 0);
        requireInteger(binaryValue->getRhs(), 10);
        require(requireReturn(basicBlock->getInst(1))->getVal() == binaryValue,
            "return should use the logical-not temporary");
    }
}

void testNestedUnaryExpressionsAllocateSequentialTemps()
{
    auto program = generateProgram("int main(){return -(+42);}");
    const auto* basicBlock = requireEntryBlock(*requireOnlyFunction(*program));

    require(basicBlock->getNumInsts() == 3,
        "nested unary expression should emit two binary temps and one return");

    const auto* firstBinary
        = requireBinary(basicBlock->getInst(0), KOOPA_RBO_ADD, "%1");
    requireInteger(firstBinary->getLhs(), 0);
    requireInteger(firstBinary->getRhs(), 42);

    const auto* secondBinary
        = requireBinary(basicBlock->getInst(1), KOOPA_RBO_SUB, "%2");
    requireInteger(secondBinary->getLhs(), 0);
    require(secondBinary->getRhs() == firstBinary,
        "outer unary operator should consume the previous temporary");

    require(requireReturn(basicBlock->getInst(2))->getVal() == secondBinary,
        "return should use the final unary temporary");
}

} // namespace

int main()
{
    testParenthesizedLiteralDoesNotAddInstructions();
    testUnaryLoweringUsesCorrectBinaryOps();
    testNestedUnaryExpressionsAllocateSequentialTemps();
    return 0;
}