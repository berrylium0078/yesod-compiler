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

koopa_ir::Symbol symbol(std::string spelling)
{
    return koopa_ir::Symbol { .sourcePos = {},
        .spelling = std::move(spelling) };
}

koopa_ir::Value intValue(int32_t value)
{
    return koopa_ir::IntegerLiteral { .sourcePos = {}, .value = value };
}

koopa_ir::CombineTerm combineTerm(const koopa_ir::Value& value)
{
    return koopa_ir::CombineTerm {
        .value = value,
        .start = intValue(0),
        .end = std::nullopt,
        .shift = intValue(0),
        .scale = intValue(1),
    };
}

void expectValidationFailure(
    koopa_ir::Program& program, const std::string& message)
{
    try {
        koopa_ir::validate(program);
    } catch (const std::runtime_error&) {
        return;
    }
    fail(message);
}

koopa_ir::Program makeProgramWithParams(
    const std::vector<std::pair<std::string, koopa_ir::Type>>& paramDescs)
{
    koopa_ir::Program program;
    std::vector<yesod::Ref<koopa_ir::FunctionParameter>> params;
    for (const auto& [name, type] : paramDescs) {
        params.push_back(program.alloc<koopa_ir::FunctionParameter>(
            koopa_ir::FunctionParameter {
                .sourcePos = {},
                .symbol = symbol(name),
                .type = type,
                .annotations = {},
            }));
    }
    auto retRef
        = program.alloc<koopa_ir::ReturnTerminator>(koopa_ir::ReturnTerminator {
            .sourcePos = {},
            .value = intValue(0),
            .annotations = {},
        });
    auto blockRef = program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
        .sourcePos = {},
        .label = symbol("%entry"),
        .params = {},
        .statements = {},
        .terminator = retRef,
        .annotations = {},
    });
    auto functionRef
        = program.alloc<koopa_ir::FunctionDef>(koopa_ir::FunctionDef {
            .sourcePos = {},
            .name = symbol("@main"),
            .params = std::move(params),
            .returnType = koopa_ir::Type { koopa_ir::I32Type {} },
            .blocks = { blockRef },
            .annotations = {},
        });
    program.items.push_back(functionRef);
    return program;
}

void testPolyDefaultConstructAndGetCoeff()
{
    const auto text
        = generateKoopaText("int main(){poly p; return int(p[0]);}");
    requireContains(
        text, "alloc poly", "poly locals should allocate poly type");
    requireContains(text, "poly_construct []",
        "default poly initialization should use an empty construct list");
    requireContains(
        text, "getcoeff", "poly coefficient read should use getcoeff");
    requireContains(text, "mint2int",
        "returning a coefficient as int should convert mint to int");
}

void testPolyConstructAndMultiplicationRewrite()
{
    const auto text = generateKoopaText(
        "int main(){poly a=poly(mint(1)); poly b=poly(mint(2)); "
        "poly c=a*b; return int(c[0]);}");
    requireContains(text, "poly_construct [1]",
        "poly(mint) should lower to poly_construct with a mint element");
    requireContains(
        text, "ntt", "poly multiplication should transform lhs/rhs to pv");
    requireContains(
        text, "pv_mul", "poly multiplication should multiply pv values");
    requireContains(text, "intt",
        "poly multiplication should transform pv result back to poly");
}

void testPolyLinearCombine()
{
    const auto text = generateKoopaText(
        "int main(){poly a=poly(mint(1)); poly b=poly(mint(2)); "
        "poly c=(a+b)[0,10]>>1; return int(c[0]);}");
    requireContains(
        text, "combine (", "linear poly expression should lower to combine");
    requireContains(
        text, ", 1)", "combine terms should include mint scale values");
}

void testValidateRejectsNestedCombine()
{
    koopa_ir::Program program;
    program = makeProgramWithParams({ { "%p", koopa_ir::PolyType {} } });
    auto functionRef
        = std::get<yesod::Ref<koopa_ir::FunctionDef>>(program.items.front());
    auto blockRef = program[functionRef].blocks.front();
    auto firstRef = program.alloc<koopa_ir::CombineExpr>(koopa_ir::CombineExpr {
        .sourcePos = {},
        .terms = { combineTerm(symbol("%p")) },
        .annotations = {},
    });
    program[blockRef].statements.push_back(
        program.alloc<koopa_ir::SymbolDef>(koopa_ir::SymbolDef {
            .sourcePos = {},
            .symbol = symbol("%c1"),
            .rhs = firstRef,
            .annotations = {},
        }));
    auto secondRef
        = program.alloc<koopa_ir::CombineExpr>(koopa_ir::CombineExpr {
            .sourcePos = {},
            .terms = { combineTerm(symbol("%c1")) },
            .annotations = {},
        });
    program[blockRef].statements.push_back(
        program.alloc<koopa_ir::SymbolDef>(koopa_ir::SymbolDef {
            .sourcePos = {},
            .symbol = symbol("%c2"),
            .rhs = secondRef,
            .annotations = {},
        }));
    expectValidationFailure(
        program, "validate should reject nested combine sources");
}

void testValidateRejectsInvalidPvMul()
{
    koopa_ir::Program program;
    program = makeProgramWithParams({
        { "%p", koopa_ir::PolyType {} },
        { "%m", koopa_ir::MintType {} },
    });
    auto functionRef
        = std::get<yesod::Ref<koopa_ir::FunctionDef>>(program.items.front());
    auto blockRef = program[functionRef].blocks.front();
    auto mulRef = program.alloc<koopa_ir::PvBinaryExpr>(koopa_ir::PvBinaryExpr {
        .sourcePos = {},
        .op = koopa_ir::PvBinaryOp::mul,
        .lhs = symbol("%p"),
        .rhs = symbol("%m"),
        .annotations = {},
    });
    program[blockRef].statements.push_back(
        program.alloc<koopa_ir::SymbolDef>(koopa_ir::SymbolDef {
            .sourcePos = {},
            .symbol = symbol("%bad"),
            .rhs = mulRef,
            .annotations = {},
        }));
    expectValidationFailure(program, "validate should reject poly/mint pv_mul");
}

} // namespace

int main()
{
    testPolyDefaultConstructAndGetCoeff();
    testPolyConstructAndMultiplicationRewrite();
    testPolyLinearCombine();
    testValidateRejectsNestedCombine();
    testValidateRejectsInvalidPvMul();
    return 0;
}
