#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

size_t countBranchInstructions(const Function& function)
{
    size_t branchCount = 0;
    for (size_t bbIndex = 0; bbIndex < function.getNumBBs(); ++bbIndex) {
        const auto* basicBlock = function.getBB(bbIndex);
        for (size_t instIndex = 0; instIndex < basicBlock->getNumInsts();
             ++instIndex) {
            if (basicBlock->getInst(instIndex)->isBranchValue()) {
                ++branchCount;
            }
        }
    }
    return branchCount;
}

void testIfElseLoweringUsesExplicitBranchBlocks()
{
    auto program = generateProgram(
        "int main(){int a = 1; if (a) return 1; else return 0;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* thenBlock = requireBlock(*function, 1);
    const auto* elseBlock = requireBlock(*function, 2);
    const auto* endBlock = requireEndBlock(*function);

    // Both then and else return directly, so the continuation block is
    // unreachable and gets pruned. Only entry, then, else, and end remain.
    require(function->getNumBBs() == 4,
        "if-else lowering should include entry, then, else, and end blocks");
    (void)requireBranch(entryBlock->getInst(entryBlock->getNumInsts() - 1),
        thenBlock, elseBlock);
    requireInteger(requireReturn(thenBlock->getInst(0))->getVal(), 1);
    requireInteger(requireReturn(elseBlock->getInst(0))->getVal(), 0);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testIfWithoutElseFallsThroughToContinuation()
{
    auto program
        = generateProgram("int main(){int a = 1; if (a) return 1; return 0;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* thenBlock = requireBlock(*function, 1);
    const auto* contBlock = requireBlock(*function, 2);
    const auto* endBlock = requireEndBlock(*function);

    require(function->getNumBBs() == 4,
        "if-without-else lowering should include entry, then, cont, and end blocks");
    (void)requireBranch(entryBlock->getInst(entryBlock->getNumInsts() - 1),
        thenBlock, contBlock);
    requireInteger(requireReturn(thenBlock->getInst(0))->getVal(), 1);
    // cont block has the explicit return 0; the synthesized end block is
    // unreachable but preserved as the default return guard.
    requireInteger(requireReturn(contBlock->getInst(0))->getVal(), 0);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testDanglingElseLowersAsNestedBranches()
{
    auto program = generateProgram("int main(){int a = 1; int b = 1; if (a) if "
                                   "(b) return 1; else return 2; return 3;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* outerThenBlock = requireBlock(*function, 1);
    const auto* outerContBlock = requireBlock(*function, 2);
    const auto* innerThenBlock = requireBlock(*function, 3);
    const auto* innerElseBlock = requireBlock(*function, 4);
    const auto* endBlock = requireEndBlock(*function);

    // Both inner branches return directly, so the inner continuation is
    // unreachable and gets pruned. Only 6 blocks remain.
    require(function->getNumBBs() == 6,
        "dangling-else lowering should produce entry, outer-then, outer-cont, "
        "inner-then, inner-else, and end blocks");
    (void)requireBranch(entryBlock->getInst(entryBlock->getNumInsts() - 1),
        outerThenBlock, outerContBlock);
    (void)requireBranch(
        outerThenBlock->getInst(outerThenBlock->getNumInsts() - 1),
        innerThenBlock, innerElseBlock);
    requireInteger(requireReturn(innerThenBlock->getInst(0))->getVal(), 1);
    requireInteger(requireReturn(innerElseBlock->getInst(0))->getVal(), 2);
    // outerCont is reached via the entry branch (false target of outer if),
    // not via the inner continuation (which was pruned).
    requireInteger(requireReturn(outerContBlock->getInst(0))->getVal(), 3);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testMixedArithmeticBooleanIfBuildsMultipleBranchSites()
{
    auto program = generateProgram(
        "int main(){int a = 1; int b = 0; int c = 1; int d = 1; if (a + ((b || "
        "c) && d)) return 1; else return 0;}");
    const auto* function = requireOnlyFunction(*program);

    require(function->getNumBBs() >= 8,
        "mixed integer/boolean if conditions should introduce multiple "
        "control-flow blocks");
    require(countBranchInstructions(*function) >= 4,
        "mixed integer/boolean lowering should emit nested short-circuit "
        "branches before the final if branch");
    requireProgramWellFormed(*program);
}

} // namespace

int main()
{
    testIfElseLoweringUsesExplicitBranchBlocks();
    testIfWithoutElseFallsThroughToContinuation();
    testDanglingElseLowersAsNestedBranches();
    testMixedArithmeticBooleanIfBuildsMultipleBranchSites();
    return 0;
}