#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testGeneratorBuildsExpectedWrapperObjects()
{
    auto program = generateProgram("int main(){return 42;}");

    require(program->getNumVals() == 0,
        "minimal SysY subset should not emit globals");
    require(program->getNumFuncs() == 1,
        "minimal SysY subset should emit one function");

    const auto* function = requireOnlyFunction(*program);
    require(function->getName() == "@main",
        "function name should use Koopa symbol format");
    require(function->getNumParams() == 0,
        "minimal function should not emit parameters");

    const auto* basicBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);
    require(basicBlock->getNumInsts() == 1,
        "literal return should only contain the return instruction");
    require(endBlock->getNumInsts() == 1,
        "guard end body should contain only the synthesized default return");

    const auto* returnValue = requireReturn(basicBlock->getInst(0));
    requireInteger(returnValue->getVal(), 42);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testEmptyFunctionFallsThroughToGuardReturn()
{
    auto program = generateProgram("int main(){/*I am empty*/}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);

    require(entryBlock->getNumInsts() == 1,
        "empty function body should synthesize a jump to the guard body");
    (void)requireJump(entryBlock->getInst(0), endBlock);
    require(endBlock->getNumInsts() == 1,
        "guard end body should contain only the synthesized default return");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testGeneratedProgramValidatesWithKoopa()
{
    auto program = generateProgram("int answer(){return 0x2a;}");
    requireProgramWellFormed(*program);
}

} // namespace

int main()
{
    testGeneratorBuildsExpectedWrapperObjects();
    testEmptyFunctionFallsThroughToGuardReturn();
    testGeneratedProgramValidatesWithKoopa();
    return 0;
}