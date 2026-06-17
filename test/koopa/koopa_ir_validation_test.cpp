#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "koopa/ir.h"

namespace {

namespace koopa_ir = yesod::koopa::ir;

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "test failure: " << message << std::endl;
    std::exit(1);
}

void require(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] koopa_ir::Symbol symbol(const std::string& spelling)
{
    return koopa_ir::Symbol {
        .sourcePos = { },
        .spelling = spelling,
    };
}

[[nodiscard]] koopa_ir::IntegerLiteral integer(int32_t value)
{
    return koopa_ir::IntegerLiteral {
        .sourcePos = { },
        .value = value,
    };
}

struct ValidationProgramBuilder {
    koopa_ir::Program program;
    yesod::Ptr<koopa_ir::FunctionDef> functionRef;
    yesod::Ptr<koopa_ir::BasicBlock> blockRef;

    ValidationProgramBuilder()
    {
        auto returnRef = program.alloc<koopa_ir::ReturnTerminator>(
            koopa_ir::ReturnTerminator {
                .sourcePos = { },
                .value = std::nullopt,
                .annotations = { },
            });
        blockRef = program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
            .sourcePos = { },
            .label = symbol("%entry"),
            .params = { },
            .statements = { },
            .terminator = returnRef,
            .annotations = { },
        });
        functionRef
            = program.alloc<koopa_ir::FunctionDef>(koopa_ir::FunctionDef {
                .sourcePos = { },
                .name = symbol("@main"),
                .params = { },
                .returnType = std::nullopt,
                .blocks = { blockRef.ref() },
                .annotations = { },
            });
        program.items.push_back(functionRef.ref());

        addParam("%p1", koopa_ir::PolyType { });
        addParam("%p2", koopa_ir::PolyType { });
        addParam("%pv1", koopa_ir::PvType { });
        addParam("%pv2", koopa_ir::PvType { });
        addParam("%m1", koopa_ir::MintType { });
        addParam("%i1", koopa_ir::I32Type { });
    }

    void addParam(const std::string& name, koopa_ir::Type type)
    {
        program[functionRef.ref()].params.push_back(
            program.alloc<koopa_ir::FunctionParameter>(
                koopa_ir::FunctionParameter {
                    .sourcePos = { },
                    .symbol = symbol(name),
                    .type = std::move(type),
                    .annotations = { },
                }));
    }

    void addSymbolDef(const std::string& name, koopa_ir::SymbolRhs rhs)
    {
        auto defRef = program.alloc<koopa_ir::SymbolDef>(koopa_ir::SymbolDef {
            .sourcePos = { },
            .symbol = symbol(name),
            .rhs = std::move(rhs),
            .annotations = { },
        });
        program[blockRef.ref()].statements.push_back(defRef);
    }

    void addPvBinary(const std::string& name, koopa_ir::PvBinaryOp op,
        koopa_ir::Value lhs, koopa_ir::Value rhs)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::PvBinaryExpr>(koopa_ir::PvBinaryExpr {
                .sourcePos = { },
                .op = op,
                .lhs = std::move(lhs),
                .rhs = std::move(rhs),
                .annotations = { },
            }));
    }

    void addUnaryPoly(const std::string& name, koopa_ir::UnaryPolyOp op,
        koopa_ir::Value value)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::UnaryPolyExpr>(koopa_ir::UnaryPolyExpr {
                .sourcePos = { },
                .op = op,
                .value = std::move(value),
                .annotations = { },
            }));
    }

    void addCombine(const std::string& name, koopa_ir::Value value,
        koopa_ir::Value start = integer(0), koopa_ir::Value scale = integer(1))
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::CombineExpr>(koopa_ir::CombineExpr {
                .sourcePos = { },
                .terms = { koopa_ir::CombineTerm {
                    .value = std::move(value),
                    .start = std::move(start),
                    .end = std::nullopt,
                    .shift = integer(0),
                    .scale = std::move(scale),
                } },
                .annotations = { },
            }));
    }

    void addPolyLenCall(const std::string& name, koopa_ir::Value value)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::CallExpr>(koopa_ir::CallExpr {
                .sourcePos = { },
                .callee = symbol("@__yesod_poly_len"),
                .args = { std::move(value) },
                .annotations = { },
            }));
    }

    void addBinary(const std::string& name, koopa_ir::BinaryOp op,
        koopa_ir::Value lhs, koopa_ir::Value rhs)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::BinaryExpr>(koopa_ir::BinaryExpr {
                .sourcePos = { },
                .op = op,
                .lhs = std::move(lhs),
                .rhs = std::move(rhs),
                .annotations = { },
            }));
    }
};

template <typename Build>
void requireInvalid(
    const std::string& name, const std::string& expectedMessage, Build build)
{
    ValidationProgramBuilder builder;
    build(builder);

    try {
        koopa_ir::validate(builder.program);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        require(message.find(expectedMessage) != std::string::npos,
            name + " failed with unexpected message: " + message);
        return;
    }

    fail(name + " should be rejected by IR validation");
}

void testRejectsNestedPvMul()
{
    requireInvalid("nested pv_mul", "pv_mul input cannot be another pv_mul",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPvBinary("%mul1", koopa_ir::PvBinaryOp::mul,
                symbol("%pv1"), symbol("%pv2"));
            builder.addPvBinary("%bad", koopa_ir::PvBinaryOp::mul,
                symbol("%mul1"), symbol("%pv1"));
        });
}

void testRejectsNestedPvAdd()
{
    requireInvalid("nested pv_add/sub",
        "pv_add/sub input cannot be another pv_add/sub",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPvBinary("%add1", koopa_ir::PvBinaryOp::add,
                symbol("%pv1"), symbol("%pv2"));
            builder.addPvBinary("%bad", koopa_ir::PvBinaryOp::sub,
                symbol("%add1"), symbol("%pv1"));
        });
}

void testRejectsNestedCombine()
{
    requireInvalid("nested combine",
        "combine term source cannot be another combine",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addCombine("%combine1", symbol("%p1"));
            builder.addCombine("%bad", symbol("%combine1"));
        });
}

void testRejectsPolyLenOfCombine()
{
    requireInvalid("poly_len of combine",
        "poly_len cannot consume a fused poly result",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addCombine("%combine1", symbol("%p1"));
            builder.addPolyLenCall("%bad", symbol("%combine1"));
        });
}

void testRejectsPolyLenOfIntt()
{
    requireInvalid("poly_len of intt",
        "poly_len cannot consume a fused poly result",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addUnaryPoly(
                "%intt1", koopa_ir::UnaryPolyOp::intt, symbol("%pv1"));
            builder.addPolyLenCall("%bad", symbol("%intt1"));
        });
}

void testRejectsPlainPolyBinary()
{
    requireInvalid("plain poly binary",
        "poly/pv values must use dedicated pseudo-instructions",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addBinary(
                "%bad", koopa_ir::BinaryOp::add, symbol("%p1"), symbol("%p2"));
        });
}

void testRejectsPlainPvBinary()
{
    requireInvalid("plain pv binary",
        "poly/pv values must use dedicated pseudo-instructions",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addBinary("%bad", koopa_ir::BinaryOp::add, symbol("%pv1"),
                symbol("%pv2"));
        });
}

void testRejectsBadCombineStartType()
{
    requireInvalid("combine start type", "combine term start must be int",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addCombine("%bad", symbol("%p1"), symbol("%p2"));
        });
}

void testRejectsBadCombineScaleType()
{
    requireInvalid("combine scale type", "combine term scale must be mint",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addCombine(
                "%bad", symbol("%p1"), integer(0), symbol("%p2"));
        });
}

void testRejectsPvAddTypeMismatch()
{
    requireInvalid("pv_add type mismatch", "pv_add/sub expect pv operands",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPvBinary("%bad", koopa_ir::PvBinaryOp::add,
                symbol("%pv1"), symbol("%p1"));
        });
}

void testRejectsPvMulTypeMismatch()
{
    requireInvalid("pv_mul type mismatch",
        "pv_mul expects pv/pv or pv/mint operands",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPvBinary("%bad", koopa_ir::PvBinaryOp::mul,
                symbol("%m1"), symbol("%i1"));
        });
}

} // namespace

int main()
{
    testRejectsNestedPvMul();
    testRejectsNestedPvAdd();
    testRejectsNestedCombine();
    testRejectsPolyLenOfCombine();
    testRejectsPolyLenOfIntt();
    testRejectsPlainPolyBinary();
    testRejectsPlainPvBinary();
    testRejectsBadCombineStartType();
    testRejectsBadCombineScaleType();
    testRejectsPvAddTypeMismatch();
    testRejectsPvMulTypeMismatch();
    return 0;
}
