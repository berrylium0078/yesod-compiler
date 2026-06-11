#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

namespace {

std::string generateKoopaText(const std::string& source)
{
    auto program = generateIrProgram(source);
    koopa_ir::validate(*program);
    return koopa_ir::serializeToKoopa(*program);
}

void requireContains(const std::string& text, const std::string& needle,
    const std::string& message)
{
    require(text.find(needle) != std::string::npos, message);
}

void requireNotContains(const std::string& text, const std::string& needle,
    const std::string& message)
{
    require(text.find(needle) == std::string::npos, message);
}

void testMintArithmeticAndCastsLowerToNativeKoopa()
{
    const auto text = generateKoopaText(
        "int id(int x){return x;} int main(){mint a = mint(id(6)); mint b = "
        "mint(id(7)); return int(a * b);}");

    requireContains(text, "int2mint", "int-to-mint cast should use int2mint");
    requireContains(text, "mint2int", "mint-to-int cast should use mint2int");
    requireContains(text, "mul", "mint multiplication should use native mul");
    requireNotContains(text, "__yesod_mint_",
        "Koopa IR should not declare or call mint runtime helpers");
}

void testMintComparisonsDecodeBeforeKoopaComparison()
{
    const auto text = generateKoopaText(
        "int id(int x){return x;} int main(){mint a = mint(id(6)); mint b = "
        "mint(id(7)); return a < b;}");

    size_t count = 0;
    size_t pos = 0;
    while ((pos = text.find("mint2int", pos)) != std::string::npos) {
        ++count;
        pos += std::string("mint2int").size();
    }
    require(count == 2,
        "mint comparisons should decode both operands before integer "
        "comparison");
}

} // namespace

int main()
{
    testMintArithmeticAndCastsLowerToNativeKoopa();
    testMintComparisonsDecodeBeforeKoopaComparison();
    return 0;
}
