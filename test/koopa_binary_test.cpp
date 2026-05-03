#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testArithmeticPrecedenceLowering()
{
    auto program = generateProgram("int main(){return 1 + 2 * 3;}");
    const auto* basicBlock = requireEntryBlock(*requireOnlyFunction(*program));

    require(basicBlock->getNumInsts() == 1,
        "constant arithmetic should fold to a literal return");
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 7);
}

void testComparisonAndEqualityLowering()
{
    auto program = generateProgram("int main(){return 1 + 2 * 3 <= 7 == 1;}");
    const auto* basicBlock = requireEntryBlock(*requireOnlyFunction(*program));

    require(basicBlock->getNumInsts() == 1,
        "constant comparison chain should fold to a literal return");
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 1);
}

void testLogicalOperatorsBooleanizeOperands()
{
    auto program = generateProgram("int main(){return 2 && 0 || 5;}");
    const auto* basicBlock = requireEntryBlock(*requireOnlyFunction(*program));

    require(basicBlock->getNumInsts() == 1,
        "constant logical expressions should fold to a literal return");
    requireInteger(requireReturn(basicBlock->getInst(0))->getVal(), 1);
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
    testGeneratedProgramValidatesWithKoopa();
    return 0;
}