#include <cstddef>
#include <functional>

#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

size_t countInstructionKind(const Function& function,
    const std::function<bool(const Value*)>& predicate)
{
    size_t count = 0;
    for (const auto* block : function.bbs()) {
        for (const auto* inst : block->insts()) {
            if (predicate(inst)) {
                ++count;
            }
        }
    }
    return count;
}

void testLocalScalarJoinLowersToBlockArguments()
{
    auto program = generateProgram(
        "int main(){int x = 0; if (x) { x = 1; } else { x = 2; } return x;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* thenBlock = requireBlock(*function, 1);
    const auto* elseBlock = requireBlock(*function, 2);
    const auto* joinBlock = requireBlock(*function, 3);

    require(countInstructionKind(*function,
                [](const Value* value) { return value->isAllocValue(); }) == 0,
        "SSA lowering should not allocate storage for local scalar x");
    require(countInstructionKind(*function,
                [](const Value* value) { return value->isLoadValue(); }) == 0,
        "SSA lowering should not load local scalar x from memory");
    require(countInstructionKind(*function,
                [](const Value* value) { return value->isStoreValue(); }) == 0,
        "SSA lowering should not store local scalar x to memory");

    const auto* entryBranch
        = dynamic_cast<const BranchValue*>(entryBlock->getInst(entryBlock->getNumInsts() - 1));
    require(entryBranch != nullptr, "entry block should end with a branch");
    require(entryBranch->getNumTrueArgs() == 0 && entryBranch->getNumFalseArgs() == 0,
        "entry branch should not need SSA arguments for then/else blocks");

    const auto* thenJump = dynamic_cast<const JumpValue*>(thenBlock->getInst(0));
    const auto* elseJump = dynamic_cast<const JumpValue*>(elseBlock->getInst(0));
    require(thenJump != nullptr, "then block should end with a jump");
    require(elseJump != nullptr, "else block should end with a jump");
    require(thenJump->getTargetBB() == joinBlock,
        "then block should jump to the join block");
    require(elseJump->getTargetBB() == joinBlock,
        "else block should jump to the join block");
    require(joinBlock->getNumParams() == 1,
        "join block should expose one block parameter");
    require(thenJump->getNumArgs() == 1,
        "then jump should forward one SSA argument");
    require(elseJump->getNumArgs() == 1,
        "else jump should forward one SSA argument");
    require(thenJump->getArg(0)->isIntegerValue()
            && dynamic_cast<const IntegerValue*>(thenJump->getArg(0))->getVal() == 1,
        "then edge should pass the then-assigned SSA value");
    require(elseJump->getArg(0)->isIntegerValue()
            && dynamic_cast<const IntegerValue*>(elseJump->getArg(0))->getVal() == 2,
        "else edge should pass the else-assigned SSA value");

    const auto* returnValue = requireReturn(joinBlock->getInst(0));
    require(returnValue->getVal() == joinBlock->getParam(0),
        "join block should return its block parameter directly");
}

void testScalarAndArrayMixKeepsOnlyArrayInMemory()
{
    auto program = generateProgram(
        "int main(){int x = 1; int arr[2]; arr[0] = x; x = x + arr[0]; return x;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);

    size_t allocCount = 0;
    const AllocValue* arrayAlloc = nullptr;
    size_t loadCount = 0;
    size_t storeCount = 0;
    for (const auto* inst : entryBlock->insts()) {
        if (const auto* alloc = dynamic_cast<const AllocValue*>(inst)) {
            ++allocCount;
            arrayAlloc = alloc;
        }
        if (inst->isLoadValue()) {
            ++loadCount;
        }
        if (inst->isStoreValue()) {
            ++storeCount;
        }
    }

    require(allocCount == 1,
        "mixed scalar/array lowering should allocate storage only for the array");
    require(arrayAlloc != nullptr, "expected the array allocation to be present");
    require(loadCount == 1,
        "mixed scalar/array lowering should load only the array element");
    require(storeCount == 1,
        "mixed scalar/array lowering should store only into the array element");

    const auto* firstElemPtr = requireGetElemPtr(entryBlock->getInst(1), arrayAlloc);
    requireInteger(firstElemPtr->getIndex(), 0);
    const auto* elementStore = requireStore(entryBlock->getInst(2), firstElemPtr);
    requireInteger(elementStore->getVal(), 1);
    const auto* secondElemPtr = requireGetElemPtr(entryBlock->getInst(3), arrayAlloc);
    requireInteger(secondElemPtr->getIndex(), 0);
    const auto* elementLoad = dynamic_cast<const LoadValue*>(entryBlock->getInst(4));
    require(elementLoad != nullptr, "expected a load from the array element pointer");
    require(elementLoad->getSource() == secondElemPtr,
        "mixed scalar/array lowering should load from the computed array element address");
    const auto* addValue = dynamic_cast<const BinaryValue*>(entryBlock->getInst(5));
    require(addValue != nullptr, "expected add instruction for the scalar update");
    require(addValue->getOp() == KOOPA_RBO_ADD,
        "scalar update should use addition");
    requireInteger(addValue->getLhs(), 1);
    require(addValue->getRhs() == elementLoad,
        "scalar update should add the loaded array element to the SSA scalar value");
    require(requireReturn(entryBlock->getInst(6))->getVal() == addValue,
        "mixed scalar/array lowering should return the updated SSA scalar value");
}

} // namespace

int main()
{
    testLocalScalarJoinLowersToBlockArguments();
    testScalarAndArrayMixKeepsOnlyArrayInMemory();
    return 0;
}