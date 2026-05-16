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

    require(basicBlock->getNumInsts() == 7,
        "var declaration and assignment should emit "
        "alloc/store/load/add/store/load/return");
    require(endBlock->getNumInsts() == 1,
        "guard end block should contain only the synthesized default return");

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

    require(basicBlock->getNumInsts() == 12,
        "shadowed scope example should emit unique storage for both inner "
        "blocks and keep outer loads distinct");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);

    const auto* outerAlloc = requireAlloc(basicBlock->getInst(0), "%x");
    const auto* outerInitStore
        = requireStore(basicBlock->getInst(1), outerAlloc);
    requireInteger(outerInitStore->getVal(), 1);

    const auto* firstInnerOuterLoad
        = requireLoad(basicBlock->getInst(2), outerAlloc, "%1");

    const auto* firstInnerAlloc
        = dynamic_cast<const AllocValue*>(basicBlock->getInst(3));
    require(firstInnerAlloc != nullptr,
        "expected alloc instruction for first inner shadowed symbol");
    require(firstInnerAlloc->getName() != outerAlloc->getName(),
        "first inner shadowed symbol should get a distinct Koopa storage name");
    require(firstInnerAlloc->getName().rfind("%x", 0) == 0,
        "first inner shadowed symbol name should preserve the identifier "
        "prefix");

    const auto* firstInnerInitStore
        = requireStore(basicBlock->getInst(4), firstInnerAlloc);
    requireInteger(firstInnerInitStore->getVal(), 2);

    const auto* firstInnerLoad
        = requireLoad(basicBlock->getInst(5), firstInnerAlloc, "%2");

    const auto* secondInnerAlloc
        = dynamic_cast<const AllocValue*>(basicBlock->getInst(6));
    require(secondInnerAlloc != nullptr,
        "expected alloc instruction for second inner shadowed symbol");
    require(secondInnerAlloc->getName() != outerAlloc->getName(),
        "second inner shadowed symbol should get a distinct Koopa storage "
        "name");
    require(secondInnerAlloc->getName() != firstInnerAlloc->getName(),
        "separate inner scopes should get different Koopa storage names");
    require(secondInnerAlloc->getName().rfind("%x", 0) == 0,
        "second inner shadowed symbol name should preserve the identifier "
        "prefix");

    const auto* secondInnerInitStore
        = requireStore(basicBlock->getInst(7), secondInnerAlloc);
    requireInteger(secondInnerInitStore->getVal(), 3);

    const auto* secondInnerLoad
        = requireLoad(basicBlock->getInst(8), secondInnerAlloc, "%3");
    const auto* outerLoadAfterBlocks
        = requireLoad(basicBlock->getInst(9), outerAlloc, "%4");
    const auto* outerReturnLoad
        = requireLoad(basicBlock->getInst(10), outerAlloc, "%5");
    require(requireReturn(basicBlock->getInst(11))->getVal() == outerReturnLoad,
        "return should use the outer symbol after the nested scope ends");

    require(firstInnerOuterLoad->getSource() == outerAlloc,
        "use before the first shadowing declaration should load the outer "
        "symbol");
    require(firstInnerLoad->getSource() == firstInnerAlloc,
        "use after the first shadowing declaration should load the first inner "
        "symbol");
    require(secondInnerLoad->getSource() == secondInnerAlloc,
        "use inside the second nested block should load the second inner "
        "symbol");
    require(outerLoadAfterBlocks->getSource() == outerAlloc,
        "use after both inner blocks should load the outer symbol");
}

} // namespace

int main()
{
    testVarDeclarationAssignmentAndReturnLowering();
    testDeclarationProgramValidatesWithKoopa();
    testShadowedNamesGetUniqueKoopaStorage();
    return 0;
}