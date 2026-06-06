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
    const auto* endBlock = requireEndBlock(*function);

    // Constant operands cause CFG simplification to merge short-circuit
    // blocks and replace branches with jumps. The entire expression folds
    // to a constant return value (2 && 0 || 5 = 1).
    require(function->getNumBBs() >= 2,
        "logical returns should lower to a valid CFG with at least entry and "
        "end");
    require(endBlock != nullptr, "guard end block should be present");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testLogicalAndShortCircuitsThroughBranchBlocks()
{
    auto program
        = generateProgram("int main(){int a = 1; int b = 0; return a && b;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);
    const auto* entryBranch = requireBranchValue(
        entryBlock->getInst(entryBlock->getNumInsts() - 1));
    const auto* rhsBlock = entryBranch->getTrueBB();
    const auto* falseBlock = entryBranch->getFalseBB();
    const auto* rhsBranch
        = requireBranchValue(rhsBlock->getInst(rhsBlock->getNumInsts() - 1));
    const auto* trueBlock = rhsBranch->getTrueBB();
    const auto* continuedFalseBlock = rhsBranch->getFalseBB();

    require(function->getNumBBs() == 5,
        "non-constant logical-and should lower to entry, true, false, rhs, "
        "and synthesized end blocks");
    require(continuedFalseBlock == falseBlock,
        "logical-and false edges should converge on the same false block");
    requireInteger(requireReturn(trueBlock->getInst(0))->getVal(), 1);
    requireInteger(requireReturn(falseBlock->getInst(0))->getVal(), 0);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testLogicalOrShortCircuitsThroughBranchBlocks()
{
    auto program
        = generateProgram("int main(){int a = 1; int b = 0; return a || b;}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);
    const auto* entryBranch = requireBranchValue(
        entryBlock->getInst(entryBlock->getNumInsts() - 1));
    const auto* trueBlock = entryBranch->getTrueBB();
    const auto* rhsBlock = entryBranch->getFalseBB();
    const auto* rhsBranch
        = requireBranchValue(rhsBlock->getInst(rhsBlock->getNumInsts() - 1));
    const auto* continuedTrueBlock = rhsBranch->getTrueBB();
    const auto* falseBlock = rhsBranch->getFalseBB();

    require(function->getNumBBs() == 5,
        "non-constant logical-or should lower to entry, true, false, rhs, "
        "and synthesized end blocks");
    require(continuedTrueBlock == trueBlock,
        "logical-or true edges should converge on the same true block");
    requireInteger(requireReturn(trueBlock->getInst(0))->getVal(), 1);
    requireInteger(requireReturn(falseBlock->getInst(0))->getVal(), 0);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testGeneratedProgramValidatesWithKoopa()
{
    auto program = generateProgram(
        "int answer(){return !(1 + 2 * 3 <= 7 == 0 || 5 && 0);}");
    requireProgramWellFormed(*program);
}

void testBitwiseAndShiftOperatorsLowerToKoopa()
{
    auto program = generateProgram("int main(){int a = 6; int b = 2; return "
                                   "(~a ^ ((a << b) & 31)) | (32 >> b);}");
    requireProgramWellFormed(*program);

    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    bool sawXor = false;
    bool sawAnd = false;
    bool sawShl = false;
    bool sawSar = false;
    for (size_t instIndex = 0; instIndex < entryBlock->getNumInsts();
         ++instIndex) {
        const auto* binary
            = dynamic_cast<const BinaryValue*>(entryBlock->getInst(instIndex));
        if (binary == nullptr) {
            continue;
        }
        sawXor = sawXor || binary->getOp() == KOOPA_RBO_XOR;
        sawAnd = sawAnd || binary->getOp() == KOOPA_RBO_AND;
        sawShl = sawShl || binary->getOp() == KOOPA_RBO_SHL;
        sawSar = sawSar || binary->getOp() == KOOPA_RBO_SAR;
    }
    require(
        sawXor, "bitwise xor lowering should produce a Koopa xor instruction");
    require(
        sawAnd, "bitwise and lowering should produce a Koopa and instruction");
    require(
        sawShl, "left shift lowering should produce a Koopa shl instruction");
    require(
        sawSar, "right shift lowering should produce a Koopa sar instruction");
}

void testLogicalOrUsedAsArithmeticOperandMaterializesBeforeMultiply()
{
    auto program = generateProgram(
        "int main(){int b = 8, a = 1, c = 2; return b * (a || c);}");
    const auto* function = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*function);
    const auto* endBlock = requireEndBlock(*function);
    const auto* entryBranch = requireBranchValue(
        entryBlock->getInst(entryBlock->getNumInsts() - 1));
    const auto* trueBlock = entryBranch->getTrueBB();
    const auto* rhsBlock = entryBranch->getFalseBB();
    const auto* rhsBranch
        = requireBranchValue(rhsBlock->getInst(rhsBlock->getNumInsts() - 1));
    const auto* falseBlock = rhsBranch->getFalseBB();
    const auto* trueJump = requireJumpValue(trueBlock->getInst(1));
    const auto* falseJump = requireJumpValue(falseBlock->getInst(1));
    const auto* contBlock = trueJump->getTargetBB();
    require(rhsBranch->getTrueBB() == trueBlock,
        "logical-or used in arithmetic should preserve the true short-circuit "
        "edge");
    require(falseJump->getTargetBB() == contBlock,
        "logical-or false branch should join the continuation before "
        "arithmetic use");
    require(trueJump->getNumArgs() == 0 && falseJump->getNumArgs() == 0,
        "logical-or materialization continuation should not require block "
        "arguments");

    require(contBlock->getNumInsts() >= 2,
        "logical-or arithmetic continuation should compute a value and return "
        "it");
    const auto* multiply = dynamic_cast<const BinaryValue*>(
        contBlock->getInst(contBlock->getNumInsts() - 2));
    require(multiply != nullptr,
        "continuation should contain a multiply instruction");
    require(multiply->getOp() == KOOPA_RBO_MUL,
        "continuation should multiply the original scalar by the booleanized "
        "result");
    requireInteger(multiply->getLhs(), 8);
    require(requireReturn(contBlock->getInst(contBlock->getNumInsts() - 1))
                ->getVal()
            == multiply,
        "continuation should return the multiplication result");
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);

    requireProgramWellFormed(*program);
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
    testBitwiseAndShiftOperatorsLowerToKoopa();
    testLogicalOrUsedAsArithmeticOperandMaterializesBeforeMultiply();
    return 0;
}