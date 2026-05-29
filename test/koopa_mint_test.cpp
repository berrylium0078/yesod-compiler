#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

size_t countCallsToCallee(const Function& function, const Function& callee)
{
    size_t callCount = 0;
    for (size_t bbIndex = 0; bbIndex < function.getNumBBs(); ++bbIndex) {
        const auto* basicBlock = function.getBB(bbIndex);
        for (size_t instIndex = 0; instIndex < basicBlock->getNumInsts();
             ++instIndex) {
            const auto* callValue
                = dynamic_cast<const CallValue*>(basicBlock->getInst(instIndex));
            if (callValue != nullptr && callValue->getCallee() == &callee) {
                ++callCount;
            }
        }
    }
    return callCount;
}

void testMintArithmeticAndCastsLowerThroughRuntimeHelpers()
{
    auto program = generateProgram(
        "int id(int x){return x;} int main(){mint a = mint(id(6)); mint b = "
        "mint(id(7)); return int(a * b);}");

    const auto* mainFunction = requireFunctionByName(*program, "@main");
    const auto* fromIntFunction
        = requireFunctionByName(*program, "@__yesod_mint_from_int");
    const auto* mulFunction
        = requireFunctionByName(*program, "@__yesod_mint_mul");
    const auto* toIntFunction
        = requireFunctionByName(*program, "@__yesod_mint_to_int");

    require(countCallsToCallee(*mainFunction, *fromIntFunction) == 2,
        "main should lower both explicit int-to-mint casts through the runtime helper");
    require(countCallsToCallee(*mainFunction, *mulFunction) == 1,
        "main should lower mint multiplication through the runtime helper");
    require(countCallsToCallee(*mainFunction, *toIntFunction) == 1,
        "main should lower the explicit mint-to-int cast through the runtime helper");
}

void testMintComparisonsDecodeBeforeKoopaComparison()
{
    auto program = generateProgram(
        "int id(int x){return x;} int main(){mint a = mint(id(6)); mint b = "
        "mint(id(7)); return a < b;}");

    const auto* mainFunction = requireFunctionByName(*program, "@main");
    const auto* toIntFunction
        = requireFunctionByName(*program, "@__yesod_mint_to_int");

    require(countCallsToCallee(*mainFunction, *toIntFunction) == 2,
        "mint comparisons should decode both operands before emitting the integer comparison");
}

} // namespace

int main()
{
    testMintArithmeticAndCastsLowerThroughRuntimeHelpers();
    testMintComparisonsDecodeBeforeKoopaComparison();
    return 0;
}