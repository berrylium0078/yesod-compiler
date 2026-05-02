#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

void testArithmeticPrecedenceLowering() {
    auto program = generateProgram("int main(){return 1 + 2 * 3;}");
    const auto* basicBlock = requireEntryBlock(*requireOnlyFunction(*program));

    require(basicBlock->getNumInsts() == 3, "1 + 2 * 3 should emit mul, add, then return");

    const auto* mulValue = requireBinary(basicBlock->getInst(0), KOOPA_RBO_MUL, "%1");
    requireInteger(mulValue->getLhs(), 2);
    requireInteger(mulValue->getRhs(), 3);

    const auto* addValue = requireBinary(basicBlock->getInst(1), KOOPA_RBO_ADD, "%2");
    requireInteger(addValue->getLhs(), 1);
    require(addValue->getRhs() == mulValue, "add should consume the multiplicative temporary");

    require(requireReturn(basicBlock->getInst(2))->getVal() == addValue, "return should use the final arithmetic temporary");
}

void testComparisonAndEqualityLowering() {
    auto program = generateProgram("int main(){return 1 + 2 * 3 <= 7 == 1;}");
    const auto* basicBlock = requireEntryBlock(*requireOnlyFunction(*program));

    require(basicBlock->getNumInsts() == 5, "comparison/equality chain should emit mul, add, le, eq, then return");

    const auto* mulValue = requireBinary(basicBlock->getInst(0), KOOPA_RBO_MUL, "%1");
    const auto* addValue = requireBinary(basicBlock->getInst(1), KOOPA_RBO_ADD, "%2");
    const auto* leValue = requireBinary(basicBlock->getInst(2), KOOPA_RBO_LE, "%3");
    const auto* eqValue = requireBinary(basicBlock->getInst(3), KOOPA_RBO_EQ, "%4");

    require(addValue->getRhs() == mulValue, "add should consume the multiplicative temporary");
    require(leValue->getLhs() == addValue, "relational comparison should consume the additive temporary");
    requireInteger(leValue->getRhs(), 7);
    require(eqValue->getLhs() == leValue, "equality comparison should consume the relational temporary");
    requireInteger(eqValue->getRhs(), 1);
    require(requireReturn(basicBlock->getInst(4))->getVal() == eqValue, "return should use the final equality temporary");
}

void testLogicalOperatorsBooleanizeOperands() {
    auto program = generateProgram("int main(){return 2 && 0 || 5;}");
    const auto* basicBlock = requireEntryBlock(*requireOnlyFunction(*program));

    require(basicBlock->getNumInsts() == 7, "logical operators should booleanize both operands, emit logical temporaries, and end with return");

    const auto* lhsAndBool = requireBinary(basicBlock->getInst(0), KOOPA_RBO_NOT_EQ, "%1");
    requireInteger(lhsAndBool->getLhs(), 0);
    requireInteger(lhsAndBool->getRhs(), 2);

    const auto* rhsAndBool = requireBinary(basicBlock->getInst(1), KOOPA_RBO_NOT_EQ, "%2");
    requireInteger(rhsAndBool->getLhs(), 0);
    requireInteger(rhsAndBool->getRhs(), 0);

    const auto* andValue = requireBinary(basicBlock->getInst(2), KOOPA_RBO_AND, "%3");
    require(andValue->getLhs() == lhsAndBool, "logical and should use the booleanized lhs");
    require(andValue->getRhs() == rhsAndBool, "logical and should use the booleanized rhs");

    const auto* lhsOrBool = requireBinary(basicBlock->getInst(3), KOOPA_RBO_NOT_EQ, "%4");
    requireInteger(lhsOrBool->getLhs(), 0);
    require(lhsOrBool->getRhs() == andValue, "logical or should booleanize the and temporary");

    const auto* rhsOrBool = requireBinary(basicBlock->getInst(4), KOOPA_RBO_NOT_EQ, "%5");
    requireInteger(rhsOrBool->getLhs(), 0);
    requireInteger(rhsOrBool->getRhs(), 5);

    const auto* orValue = requireBinary(basicBlock->getInst(5), KOOPA_RBO_OR, "%6");
    require(orValue->getLhs() == lhsOrBool, "logical or should use the booleanized lhs");
    require(orValue->getRhs() == rhsOrBool, "logical or should use the booleanized rhs");
    require(requireReturn(basicBlock->getInst(6))->getVal() == orValue, "return should use the final logical temporary");
}

void testGeneratedProgramValidatesWithKoopa() {
    auto program = generateProgram("int answer(){return !(1 + 2 * 3 <= 7 == 0 || 5 && 0);}");
    auto rawProgram = ::koopa::Program::dumpRaw(program.get());
    koopa_program_t koopaProgram = nullptr;
    const auto errorCode = koopa_generate_raw_to_koopa(&rawProgram, &koopaProgram);
    require(errorCode == KOOPA_EC_SUCCESS, "generated raw program should be accepted by Koopa");
    koopa_delete_program(koopaProgram);
}

}  // namespace

int main() {
    testArithmeticPrecedenceLowering();
    testComparisonAndEqualityLowering();
    testLogicalOperatorsBooleanizeOperands();
    testGeneratedProgramValidatesWithKoopa();
    return 0;
}