#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testArithmeticPrecedenceLowering()
{
    auto program = generateProgram("int main(){return 1 + 2 * 3;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* basicBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);

    require(basicBlock->getNumInsts() == 1,
        "constant integer should fold to a literal return");
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 7);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testComparisonAndEqualityLowering()
{
    auto program = generateProgram("int main(){return 1 + 2 * 3 <= 7 == 1;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* basicBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);

    require(basicBlock->getNumInsts() == 1,
        "constant comparison chain should fold to a literal return");
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 1);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testLogicalOperatorsBooleanizeOperands()
{
    auto program = generateProgram("int main(){return 2 && 0 || 5;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* basicBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);

    require(basicBlock->getNumInsts() == 1,
        "constant logical expressions should fold to a literal return");
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 1);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testLogicalAndShortCircuitsThroughBranchBlocks()
{
    auto program
        = generateProgram("int main(){int a = 1; int b = 0; return a && b;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* trueBlock = requireBlock(*function, 1);
    const auto* falseBlock = requireBlock(*function, 2);
    const auto* contBlock = requireBlock(*function, 3);
    const auto* rhsBlock = requireBlock(*function, 4);
    const auto* endBlock = requireEndBlock(*function);

    require(function->getNumBBs() == 6,
        "non-constant logical-and should materialize short-circuit "
        "control-flow blocks");
    (void)requireBranch(entryBlock->getInst(entryBlock->getNumInsts() - 1),
        rhsBlock, falseBlock);
    (void)requireBranch(
        rhsBlock->getInst(rhsBlock->getNumInsts() - 1), trueBlock, falseBlock);
    (void)requireJump(trueBlock->getInst(1), contBlock);
    (void)requireJump(falseBlock->getInst(1), contBlock);
    require(contBlock->getInst(contBlock->getNumInsts() - 1)->isReturnValue(),
        "materialized logical-and continuation should return the loaded "
        "boolean result");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testLogicalOrShortCircuitsThroughBranchBlocks()
{
    auto program
        = generateProgram("int main(){int a = 1; int b = 0; return a || b;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* trueBlock = requireBlock(*function, 1);
    const auto* falseBlock = requireBlock(*function, 2);
    const auto* contBlock = requireBlock(*function, 3);
    const auto* rhsBlock = requireBlock(*function, 4);
    const auto* endBlock = requireEndBlock(*function);

    require(function->getNumBBs() == 6,
        "non-constant logical-or should materialize short-circuit control-flow "
        "blocks");
    (void)requireBranch(entryBlock->getInst(entryBlock->getNumInsts() - 1),
        trueBlock, rhsBlock);
    (void)requireBranch(
        rhsBlock->getInst(rhsBlock->getNumInsts() - 1), trueBlock, falseBlock);
    (void)requireJump(trueBlock->getInst(1), contBlock);
    (void)requireJump(falseBlock->getInst(1), contBlock);
    require(contBlock->getInst(contBlock->getNumInsts() - 1)->isReturnValue(),
        "materialized logical-or continuation should return the loaded boolean "
        "result");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testGeneratedProgramValidatesWithKoopa()
{
    auto program = generateProgram(
        "int answer(){return !(1 + 2 * 3 <= 7 == 0 || 5 && 0);}");
    auto rawProgram = Program::dumpRaw(program.get());
    koopa_program_t koopaProgram = nullptr;
    const auto errorCode
        = koopa_generate_raw_to_koopa(&rawProgram, &koopaProgram);
    require(errorCode == KOOPA_EC_SUCCESS,
        "generated raw program should be accepted by Koopa");
    koopa_delete_program(koopaProgram);
}

} // namespace

int main()
{
    testArithmeticPrecedenceLowering();
    testComparisonAndEqualityLowering();
    testLogicalOperatorsBooleanizeOperands();
    testLogicalAndShortCircuitsThroughBranchBlocks();
    testLogicalOrShortCircuitsThroughBranchBlocks();
    testGeneratedProgramValidatesWithKoopa();
    return 0;
}