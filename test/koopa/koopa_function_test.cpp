#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

size_t countCallsToCallee(const BasicBlock& basicBlock, const Function& callee)
{
    size_t callCount = 0;
    for (size_t instIndex = 0; instIndex < basicBlock.getNumInsts();
         ++instIndex) {
        const auto* callValue
            = dynamic_cast<const CallValue*>(basicBlock.getInst(instIndex));
        if (callValue != nullptr && callValue->getCallee() == &callee) {
            ++callCount;
        }
    }
    return callCount;
}

size_t countCallsToCallee(const Function& function, const Function& callee)
{
    size_t callCount = 0;
    for (size_t bbIndex = 0; bbIndex < function.getNumBBs(); ++bbIndex) {
        callCount += countCallsToCallee(*function.getBB(bbIndex), callee);
    }
    return callCount;
}

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

void testGlobalVariableAndForwardCallLowering()
{
    auto program = generateProgram(
        "const int seed = 4; int counter = 2; int main(){counter = counter + "
        "1; return add(seed, counter);} int add(int lhs, int rhs){return "
        "lhs + rhs;}");

    require(program->getNumVals() == 1,
        "only mutable globals should lower to Koopa global allocations");
    const auto* counterGlobal = requireGlobalAlloc(program->getVal(0), "@counter");
    requireInteger(counterGlobal->getInitVal(), 2);

    require(program->getNumFuncs() == 2,
        "program with main and helper should emit two functions");
// 2 3 6 7
    const auto* mainFunction = requireFunctionByName(*program, "@main");
    const auto* addFunction = requireFunctionByName(*program, "@add");
    require(mainFunction->getNumParams() == 0,
        "main should not emit parameters");
    require(addFunction->getNumParams() == 2,
        "helper function should preserve both parameters");

    const auto* mainEntry = requireEntryBlock(*mainFunction);
    const auto* mainEnd = requireEndBlock(*mainFunction);
    require(mainEntry->getNumInsts() == 6,
        "main should emit load/add/store/load/call/return for the global "
        "update and helper call");
    require(mainEnd->getNumInsts() == 1,
        "main guard end body should keep the synthesized default return");

    const auto* counterLoad
        = requireLoad(mainEntry->getInst(0), counterGlobal, "%1");
    const auto* increment
        = requireBinary(mainEntry->getInst(1), KOOPA_RBO_ADD, "%2");
    require(increment->getLhs() == counterLoad,
        "global increment should consume the loaded global value");
    requireInteger(increment->getRhs(), 1);

    const auto* counterStore
        = requireStore(mainEntry->getInst(2), counterGlobal);
    require(counterStore->getVal() == increment,
        "global update should store the computed incremented value");

    const auto* counterReload
        = requireLoad(mainEntry->getInst(3), counterGlobal, "%3");
    const auto* helperCall = requireCall(mainEntry->getInst(4), addFunction);
    require(helperCall->getNumArgs() == 2,
        "helper call should preserve both arguments");
    requireInteger(helperCall->getArg(0), 4);
    require(helperCall->getArg(1) == counterReload,
        "helper call should use the reloaded global variable value");
    require(requireReturn(mainEntry->getInst(5))->getVal() == helperCall,
        "main should return the helper call result");
    requireInteger(requireReturn(mainEnd->getInst(0))->getVal(), 0);
}

void testVoidFunctionCallLowering()
{
    auto program
        = generateProgram("void noop(){return;} int main(){noop(); return 0;}");

    require(program->getNumVals() == 0,
        "void-call program should not emit globals");
    require(program->getNumFuncs() == 2,
        "void-call program should emit both functions");

    const auto* noopFunction = requireFunctionByName(*program, "@noop");
    const auto* mainFunction = requireFunctionByName(*program, "@main");

    const auto* noopEntry = requireEntryBlock(*noopFunction);
    const auto* noopEnd = requireEndBlock(*noopFunction);
    require(noopEntry->getNumInsts() == 1,
        "explicit void return should emit a single return in the entry body");
    require(requireReturn(noopEntry->getInst(0))->getVal() == nullptr,
        "explicit void return should lower to a valueless return");
    require(requireReturn(noopEnd->getInst(0))->getVal() == nullptr,
        "void guard end body should synthesize a valueless return");

    const auto* mainEntry = requireEntryBlock(*mainFunction);
    const auto* mainCall = requireCall(mainEntry->getInst(0), noopFunction);
    require(mainCall->getNumArgs() == 0,
        "void helper call should preserve an empty argument list");
    require(!mainCall->hasName(),
        "void-returning call should not fabricate a result temporary");
    requireInteger(requireReturn(mainEntry->getInst(1))->getVal(), 0);
}

void testVoidCallBeforeIntegerReturnCallLowering()
{
    auto program = generateProgram(
        "int half(int x){return x / 2;} void f(){} int main(){f(); return "
        "half(10);}");

    require(program->getNumVals() == 0,
        "helper-call example should not emit globals");
    require(program->getNumFuncs() == 3,
        "helper-call example should emit half, f, and main");

    const auto* halfFunction = requireFunctionByName(*program, "@half");
    const auto* fFunction = requireFunctionByName(*program, "@f");
    const auto* mainFunction = requireFunctionByName(*program, "@main");

    require(halfFunction->getNumParams() == 1,
        "half should preserve its single parameter");
    require(fFunction->getNumParams() == 0,
        "f should preserve an empty parameter list");

    const auto* halfEntry = requireEntryBlock(*halfFunction);
    const auto* halfEnd = requireEndBlock(*halfFunction);
    require(halfEntry->getNumInsts() == 2,
        "half should lower its scalar parameter directly in SSA form");
    const auto* halfDiv
        = requireBinary(halfEntry->getInst(0), KOOPA_RBO_DIV, "%1");
    require(halfDiv->getLhs() == halfFunction->getParam(0),
        "half should divide the incoming parameter value directly");
    requireInteger(halfDiv->getRhs(), 2);
    require(requireReturn(halfEntry->getInst(1))->getVal() == halfDiv,
        "half should return the division result");
    requireInteger(requireReturn(halfEnd->getInst(0))->getVal(), 0);

    const auto* fEntry = requireEntryBlock(*fFunction);
    const auto* fEnd = requireEndBlock(*fFunction);
    require(fEntry->getNumInsts() == 1,
        "empty void function should fall through to the synthesized end body");
    (void)requireJump(fEntry->getInst(0), fEnd);
    require(requireReturn(fEnd->getInst(0))->getVal() == nullptr,
        "void function end body should synthesize a valueless return");

    const auto* mainEntry = requireEntryBlock(*mainFunction);
    const auto* mainEnd = requireEndBlock(*mainFunction);
    require(mainEntry->getNumInsts() == 3,
        "main should emit call/call/return for the helper-call example");

    const auto* fCall = requireCall(mainEntry->getInst(0), fFunction);
    require(fCall->getNumArgs() == 0,
        "void helper call should preserve an empty argument list");
    require(!fCall->hasName(),
        "void helper call should not fabricate a result temporary");

    const auto* halfCall = requireCall(mainEntry->getInst(1), halfFunction);
    require(halfCall->getNumArgs() == 1,
        "half call should preserve its single argument");
    requireInteger(halfCall->getArg(0), 10);
    require(requireReturn(mainEntry->getInst(2))->getVal() == halfCall,
        "main should return the half call result");
    requireInteger(requireReturn(mainEnd->getInst(0))->getVal(), 0);
}

void testLogicalAndShortCircuitKeepsDivisionCallOnRhsPath()
{
    auto program = generateProgram(
        "int div(int x, int y){return x / y;} int id(int x){return x;} int "
        "main(){int x = id(3), y = id(0); return y != 0 && div(x, y);}");

    require(program->getNumVals() == 0,
        "short-circuit example should not emit globals");
    require(program->getNumFuncs() == 3,
        "short-circuit example should emit div, id, and main");

    const auto* divFunction = requireFunctionByName(*program, "@div");
    const auto* idFunction = requireFunctionByName(*program, "@id");
    const auto* mainFunction = requireFunctionByName(*program, "@main");

    const auto* mainEntry = requireEntryBlock(*mainFunction);
    const auto* endBlock = requireEndBlock(*mainFunction);
    const auto* entryBranch
        = requireBranchValue(mainEntry->getInst(mainEntry->getNumInsts() - 1));
    const auto* rhsBlock = entryBranch->getTrueBB();
    const auto* falseBlock = entryBranch->getFalseBB();
    const auto* rhsBranch
        = requireBranchValue(rhsBlock->getInst(rhsBlock->getNumInsts() - 1));
    const auto* trueBlock = rhsBranch->getTrueBB();
    const auto* continuedFalseBlock = rhsBranch->getFalseBB();

    require(mainFunction->getNumBBs() == 5,
        "logical-and return lowering should introduce rhs, true, false, and synthesized end blocks");
    require(mainEntry->getNumInsts() >= 4,
        "main entry should initialize the two call results, compare the lhs, and branch before evaluating the rhs call");
    require(countCallsToCallee(*mainEntry, *divFunction) == 0,
        "division call must not be emitted before the lhs short-circuit check");
    require(countCallsToCallee(*mainEntry, *idFunction) == 2,
        "both id initializers should remain in the entry body");
    (void)requireCall(mainEntry->getInst(0), idFunction);
    (void)requireCall(mainEntry->getInst(1), idFunction);

    require(rhsBlock->getNumInsts() >= 2,
        "rhs body should contain the division call and branch on its result");
    const auto* divCall = requireCall(rhsBlock->getInst(0), divFunction);
    require(divCall->getNumArgs() == 2,
        "div call should preserve both arguments on the rhs path");

    require(countCallsToCallee(*rhsBlock, *divFunction) == 1,
        "division call should appear exactly once on the rhs path");
    require(continuedFalseBlock == falseBlock,
        "logical-and false edges should converge on the same false return block");
    requireInteger(requireReturn(trueBlock->getInst(0))->getVal(), 1);
    requireInteger(requireReturn(falseBlock->getInst(0))->getVal(), 0);
    requireInteger(requireReturn(endBlock->getInst(0))->getVal(), 0);
}

void testWideArityCallsLowerAllArguments()
{
    auto program = generateProgram(
        "int sum(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int "
        "a7){return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;} int sum2(int a0, "
        "int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int "
        "a9, int a10, int a11, int a12, int a13, int a14, int a15){return a0 + "
        "a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + "
        "a14 + a15;} int main(){int x = sum(1, 2, 3, 4, 5, 6, 7, 8); int y = "
        "sum2(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16); return x + "
        "y;}");

    require(program->getNumVals() == 0,
        "wide-arity call example should not emit globals");
    require(program->getNumFuncs() == 3,
        "wide-arity call example should emit sum, sum2, and main");

    const auto* sumFunction = requireFunctionByName(*program, "@sum");
    const auto* sum2Function = requireFunctionByName(*program, "@sum2");
    const auto* mainFunction = requireFunctionByName(*program, "@main");

    require(sumFunction->getNumParams() == 8,
        "sum should preserve all eight parameters");
    require(sum2Function->getNumParams() == 16,
        "sum2 should preserve all sixteen parameters");

    const auto* sumEntry = requireEntryBlock(*sumFunction);
    const auto* sum2Entry = requireEntryBlock(*sum2Function);
    require(sumEntry->getNumInsts() == 8,
        "sum should lower eight parameters into a direct add chain under SSA");
    require(sum2Entry->getNumInsts() == 16,
        "sum2 should lower sixteen parameters into a direct add chain under SSA");

    const auto* mainEntry = requireEntryBlock(*mainFunction);
    const auto* mainEnd = requireEndBlock(*mainFunction);
    require(mainEntry->getNumInsts() == 4,
        "main should emit two calls, one add, and one return under SSA");

    const auto* sumCall = requireCall(mainEntry->getInst(0), sumFunction);
    require(sumCall->getNumArgs() == 8,
        "sum call should preserve all eight arguments");
    requireInteger(sumCall->getArg(0), 1);
    requireInteger(sumCall->getArg(7), 8);

    const auto* sum2Call = requireCall(mainEntry->getInst(1), sum2Function);
    require(sum2Call->getNumArgs() == 16,
        "sum2 call should preserve all sixteen arguments");
    requireInteger(sum2Call->getArg(0), 1);
    requireInteger(sum2Call->getArg(15), 16);
    const auto* addValue
        = requireBinary(mainEntry->getInst(2), KOOPA_RBO_ADD, "%3");
    require(addValue->getLhs() == sumCall,
        "final addition should consume the first call result directly");
    require(addValue->getRhs() == sum2Call,
        "final addition should consume the second call result directly");
    require(requireReturn(mainEntry->getInst(3))->getVal() == addValue,
        "main should return the sum of x and y");
    requireInteger(requireReturn(mainEnd->getInst(0))->getVal(), 0);
}

void testNestedCallsAndBooleanShortCircuitLowerCorrectly()
{
    auto program = generateProgram(
        "int add(int a, int b){return a + b;} int sub(int a, int b){return a "
        "- b;} int mul(int a, int b){return a * b;} int div(int a, int b){return "
        "a / b;} int main(){int x = add(sub(1, 2), mul(3, div(4, 5))); int y = "
        "add(1 || 0, 0 && sub(1, x) || mul(3, div(x || add(1, 2) > 10, 5))); "
        "return x + y;}");

    require(program->getNumVals() == 0,
        "nested-call example should not emit globals");
    require(program->getNumFuncs() == 5,
        "nested-call example should emit four helpers and main");

    const auto* addFunction = requireFunctionByName(*program, "@add");
    const auto* subFunction = requireFunctionByName(*program, "@sub");
    const auto* mulFunction = requireFunctionByName(*program, "@mul");
    const auto* divFunction = requireFunctionByName(*program, "@div");
    const auto* mainFunction = requireFunctionByName(*program, "@main");

    require(countCallsToCallee(*mainFunction, *addFunction) == 3,
        "main should contain three add calls across nested expressions");
    require(countCallsToCallee(*mainFunction, *subFunction) == 2,
        "main should contain two sub calls across nested expressions");
    require(countCallsToCallee(*mainFunction, *mulFunction) == 2,
        "main should contain two mul calls across nested expressions");
    require(countCallsToCallee(*mainFunction, *divFunction) == 2,
        "main should contain two div calls across nested expressions");
    require(mainFunction->getNumBBs() >= 8,
        "boolean short-circuit lowering should introduce multiple helper blocks");
    require(countBranchInstructions(*mainFunction) >= 4,
        "boolean short-circuit lowering should emit several branches in main");

    requireProgramWellFormed(*program);
}

void testRecursiveFunctionCallsLowerAgainstOwnDeclaration()
{
    auto program = generateProgram(
        "int fib(int n){if (n < 2) {return n;} return fib(n - 1) + fib(n - "
        "2);} int main(){return fib(20);}");

    require(program->getNumVals() == 0,
        "recursive fib example should not emit globals");
    require(program->getNumFuncs() == 2,
        "recursive fib example should emit fib and main");

    const auto* fibFunction = requireFunctionByName(*program, "@fib");
    const auto* mainFunction = requireFunctionByName(*program, "@main");

    require(fibFunction->getNumParams() == 1,
        "fib should preserve its single parameter");
    require(countCallsToCallee(*fibFunction, *fibFunction) == 2,
        "fib should lower both recursive self-calls against its own function declaration");
    require(countBranchInstructions(*fibFunction) >= 1,
        "fib should branch on its base-case condition");

    const auto* mainEntry = requireEntryBlock(*mainFunction);
    const auto* mainEnd = requireEndBlock(*mainFunction);
    require(mainEntry->getNumInsts() == 2,
        "main should emit one fib call and one return");
    const auto* fibCall = requireCall(mainEntry->getInst(0), fibFunction);
    require(fibCall->getNumArgs() == 1,
        "main fib call should preserve its single argument");
    requireInteger(fibCall->getArg(0), 20);
    require(requireReturn(mainEntry->getInst(1))->getVal() == fibCall,
        "main should return the fib call result");
    requireInteger(requireReturn(mainEnd->getInst(0))->getVal(), 0);
}

void testFunctionDeclarationLowersToExternalFunction()
{
    auto program = generateProgram(
        "int add(int lhs, int rhs); int main(){return add(1, 2);}");

    require(program->getNumVals() == 0,
        "declaration-only program should not emit globals");
    require(program->getNumFuncs() == 2,
        "declaration-only program should emit the external declaration plus main");

    const auto* addFunction = requireFunctionByName(*program, "@add");
    const auto* mainFunction = requireFunctionByName(*program, "@main");

    require(addFunction->getNumBBs() == 0,
        "body-less function declaration should lower as an external function");
    require(addFunction->getNumParams() == 2,
        "external declaration should preserve parameter arity");

    const auto* mainEntry = requireEntryBlock(*mainFunction);
    const auto* addCall = requireCall(mainEntry->getInst(0), addFunction);
    require(addCall->getNumArgs() == 2,
        "call through declaration should preserve both arguments");
    requireInteger(addCall->getArg(0), 1);
    requireInteger(addCall->getArg(1), 2);
}

} // namespace

int main()
{
    testGlobalVariableAndForwardCallLowering();
    testVoidFunctionCallLowering();
    testVoidCallBeforeIntegerReturnCallLowering();
    testLogicalAndShortCircuitKeepsDivisionCallOnRhsPath();
    testWideArityCallsLowerAllArguments();
    testNestedCallsAndBooleanShortCircuitLowerCorrectly();
    testRecursiveFunctionCallsLowerAgainstOwnDeclaration();
    testFunctionDeclarationLowersToExternalFunction();
    return 0;
}
