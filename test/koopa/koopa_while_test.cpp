#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testWhileLoopAccumulatorLowersAndValidates()
{
    auto program = generateProgram("int main(){int x = 0, y = 0; while (x <= "
                                   "10) { y = y + x; x = x + 1; } return y;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* condBlock = requireBlock(*function, 1);
    const auto* bodyBlock = requireBlock(*function, 2);
    const auto* whileEndBlock = requireBlock(*function, 3);
    const auto* endBlock = requireEndBlock(*function);

    require(function->getNumBBs() == 5,
        "while lowering should add condition, body, and loop-exit blocks");

    const auto* entryJump = requireJumpValue(entryBlock->getInst(0));
    require(entryJump->getTargetBB() == condBlock,
        "entry block should jump to the loop condition block");
    require(entryJump->getNumArgs() == 2,
        "entry block should seed x and y into the loop header");
    requireInteger(entryJump->getArg(0), 0);
    requireInteger(entryJump->getArg(1), 0);
    require(condBlock->getNumParams() == 2,
        "loop header should expose x and y as block parameters");

    const auto* condValue
        = requireBinary(condBlock->getInst(0), KOOPA_RBO_LE, "%1");
    require(condValue->getLhs() == condBlock->getParam(0),
        "while condition should compare the current x block parameter");
    requireInteger(condValue->getRhs(), 10);
    const auto* condBranch = requireBranchValue(condBlock->getInst(1));
    require(condBranch->getTrueBB() == bodyBlock,
        "true branch should enter the loop body");
    require(condBranch->getFalseBB() == whileEndBlock,
        "false branch should exit the loop");
    require(condBranch->getNumTrueArgs() == 0,
        "loop body should not need block parameters");
    require(condBranch->getNumFalseArgs() == 0,
        "loop exit should not need block arguments when it is dominated by the header");
    require(whileEndBlock->getNumParams() == 0,
        "loop exit block should not expose block parameters for dominated values");

    const auto* addY
        = requireBinary(bodyBlock->getInst(0), KOOPA_RBO_ADD, "%2");
    require(addY->getLhs() == condBlock->getParam(1),
        "accumulator update should use the current y header parameter");
    require(addY->getRhs() == condBlock->getParam(0),
        "accumulator update should add the current x header parameter");
    const auto* addX
        = requireBinary(bodyBlock->getInst(1), KOOPA_RBO_ADD, "%3");
    require(addX->getLhs() == condBlock->getParam(0),
        "loop increment should use the current x header parameter");
    requireInteger(addX->getRhs(), 1);
    const auto* backedge = requireJumpValue(bodyBlock->getInst(2));
    require(backedge->getTargetBB() == condBlock,
        "loop body should jump back to the condition block");
    require(backedge->getNumArgs() == 2,
        "backedge should forward updated x and y values");
    require(backedge->getArg(0) == addX,
        "backedge should pass the incremented x value first");
    require(backedge->getArg(1) == addY,
        "backedge should pass the updated y value second");

    require(requireReturn(whileEndBlock->getInst(0))->getVal()
            == condBlock->getParam(1),
        "loop exit should return the dominated y header parameter directly");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);

    requireProgramWellFormed(*program);
}

} // namespace

int main()
{
    testWhileLoopAccumulatorLowersAndValidates();
    return 0;
}