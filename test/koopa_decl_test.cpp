#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testVarDeclarationAssignmentAndReturnLowering()
{
    auto program = generateProgram(
        "int main(){int value = 1; value = value + 2; return value;}");
    const auto* basicBlock = requireEntryBlock(*requireOnlyFunction(*program));

    require(basicBlock->getNumInsts() == 7,
        "var declaration and assignment should emit alloc/store/load/add/store/load/return");

    const auto* allocValue = requireAlloc(basicBlock->getInst(0), "%value");

    const auto* initStore = requireStore(basicBlock->getInst(1), allocValue);
    requireInteger(initStore->getVal(), 1);

    const auto* loadedValue
        = requireLoad(basicBlock->getInst(2), allocValue, "%1");

    const auto* addValue
        = requireBinary(basicBlock->getInst(3), KOOPA_RBO_ADD, "%2");
    require(addValue->getLhs() == loadedValue,
        "assignment add should consume the loaded variable value");
    requireInteger(addValue->getRhs(), 2);

    const auto* assignStore = requireStore(basicBlock->getInst(4), allocValue);
    require(assignStore->getVal() == addValue,
        "assignment store should write the computed temporary");

    const auto* returnLoad
        = requireLoad(basicBlock->getInst(5), allocValue, "%3");
    require(requireReturn(basicBlock->getInst(6))->getVal() == returnLoad,
        "return should use a freshly loaded variable value");
}

void testDeclarationProgramValidatesWithKoopa()
{
    auto program = generateProgram(
        "int main(){const int seed = 4; int value = seed; value = value * 3; return value;}"
    );
    auto rawProgram = Program::dumpRaw(program.get());
    koopa_program_t koopaProgram = nullptr;
    const auto errorCode
        = koopa_generate_raw_to_koopa(&rawProgram, &koopaProgram);
    require(errorCode == KOOPA_EC_SUCCESS,
        "declaration-heavy generated raw program should be accepted by Koopa");
    koopa_delete_program(koopaProgram);
}

} // namespace

int main()
{
    testVarDeclarationAssignmentAndReturnLowering();
    testDeclarationProgramValidatesWithKoopa();
    return 0;
}