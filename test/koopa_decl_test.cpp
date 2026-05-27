#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testVarDeclarationAssignmentAndReturnLowering()
{
    auto program = generateProgram(
        "int main(){int value = 1; value = value + 2; return value;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* basicBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);

    require(basicBlock->getNumInsts() == 2,
        "scalar declaration and assignment should lower to add/return under SSA");
    require(endBlock->getNumInsts() == 1,
        "guard end body should contain only the synthesized default return");

    const auto* addValue
        = requireBinary(basicBlock->getInst(0), KOOPA_RBO_ADD, "%1");
    requireInteger(addValue->getLhs(), 1);
    requireInteger(addValue->getRhs(), 2);
    require(requireReturn(basicBlock->getInst(1))->getVal() == addValue,
        "return should use the SSA value produced by the assignment");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testDeclarationProgramValidatesWithKoopa()
{
    auto program = generateProgram("int main(){const int seed = 4; int value = "
                                   "seed; value = value * 3; return value;}");
    auto rawProgram = Program::dumpRaw(program.get());
    koopa_program_t koopaProgram = nullptr;
    const auto errorCode
        = koopa_generate_raw_to_koopa(&rawProgram, &koopaProgram);
    require(errorCode == KOOPA_EC_SUCCESS,
        "declaration-heavy generated raw program should be accepted by Koopa");
    koopa_delete_program(koopaProgram);
}

void testShadowedNamesGetUniqueKoopaStorage()
{
    auto program = generateProgram("int main(){int x = 1; {x; int x = 2; x;} "
                                   "{int x = 3; x;} x; return x;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* basicBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);

    require(basicBlock->getNumInsts() == 1,
        "shadowed scalar scopes should not allocate storage under SSA");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 1);
}

} // namespace

int main()
{
    testVarDeclarationAssignmentAndReturnLowering();
    testDeclarationProgramValidatesWithKoopa();
    testShadowedNamesGetUniqueKoopaStorage();
    return 0;
}