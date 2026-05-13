#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testWhileLoopAccumulatorLowersAndValidates()
{
    auto program = generateProgram(
        "int main(){int x = 0, y = 0; while (x <= 10) { y = y + x; x = x + 1; } return y;}"
    );
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* condBlock = requireBlock(*function, 1);
    const auto* bodyBlock = requireBlock(*function, 2);
    const auto* whileEndBlock = requireBlock(*function, 3);
    const auto* endBlock = requireEndBlock(*function);

    require(function->getNumBBs() == 5,
        "while lowering should add condition, body, and loop-exit blocks");

    const auto* xAlloc = requireAlloc(entryBlock->getInst(0), "%x");
    const auto* xInitStore = requireStore(entryBlock->getInst(1), xAlloc);
    requireInteger(xInitStore->getVal(), 0);
    const auto* yAlloc = requireAlloc(entryBlock->getInst(2), "%y");
    const auto* yInitStore = requireStore(entryBlock->getInst(3), yAlloc);
    requireInteger(yInitStore->getVal(), 0);
    (void)requireJump(entryBlock->getInst(4), condBlock);

    const auto* condLoad = requireLoad(condBlock->getInst(0), xAlloc, "%1");
    const auto* condValue
        = requireBinary(condBlock->getInst(1), KOOPA_RBO_LE, "%2");
    require(condValue->getLhs() == condLoad,
        "while condition should compare the loaded x value");
    requireInteger(condValue->getRhs(), 10);
    (void)requireBranch(condBlock->getInst(2), bodyBlock, whileEndBlock);

    const auto* yLoad = requireLoad(bodyBlock->getInst(0), yAlloc, "%3");
    const auto* xLoadForY
        = requireLoad(bodyBlock->getInst(1), xAlloc, "%4");
    const auto* addY = requireBinary(bodyBlock->getInst(2), KOOPA_RBO_ADD, "%5");
    require(addY->getLhs() == yLoad,
        "accumulator update should use the current y value");
    require(addY->getRhs() == xLoadForY,
        "accumulator update should add the current x value");
    const auto* yStore = requireStore(bodyBlock->getInst(3), yAlloc);
    require(yStore->getVal() == addY,
        "accumulator update should store the computed y temporary");

    const auto* xLoadForInc
        = requireLoad(bodyBlock->getInst(4), xAlloc, "%6");
    const auto* addX = requireBinary(bodyBlock->getInst(5), KOOPA_RBO_ADD, "%7");
    require(addX->getLhs() == xLoadForInc,
        "loop increment should use the current x value");
    requireInteger(addX->getRhs(), 1);
    const auto* xStore = requireStore(bodyBlock->getInst(6), xAlloc);
    require(xStore->getVal() == addX,
        "loop increment should store the incremented x temporary");
    (void)requireJump(bodyBlock->getInst(7), condBlock);

    const auto* returnLoad
        = requireLoad(whileEndBlock->getInst(0), yAlloc, "%8");
    require(requireReturn(whileEndBlock->getInst(1))->getVal() == returnLoad,
        "loop exit should return the final accumulated y value");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);

    auto rawProgram = Program::dumpRaw(program.get());
    koopa_program_t koopaProgram = nullptr;
    const auto errorCode
        = koopa_generate_raw_to_koopa(&rawProgram, &koopaProgram);
    require(errorCode == KOOPA_EC_SUCCESS,
        "while-loop accumulator program should validate as raw Koopa");
    koopa_delete_program(koopaProgram);
}

} // namespace

int main()
{
    testWhileLoopAccumulatorLowersAndValidates();
    return 0;
}