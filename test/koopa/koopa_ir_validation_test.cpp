#include <cstdlib>
#include <iostream>
#include <optional>
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
    return koopa_ir::Symbol { .sourcePos = { }, .spelling = spelling };
}

[[nodiscard]] koopa_ir::IntegerLiteral integer(int32_t value)
{
    return koopa_ir::IntegerLiteral { .sourcePos = { }, .value = value };
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

    [[nodiscard]] yesod::Ref<koopa_ir::PointwiseNode> leaf(
        koopa_ir::Value value)
    {
        return program.alloc<koopa_ir::PointwiseNode>(
            koopa_ir::PointwiseNode {
                .sourcePos = { },
                .kind = koopa_ir::PointwiseLeaf {
                    .value = std::move(value),
                },
            });
    }

    [[nodiscard]] yesod::Ref<koopa_ir::PointwiseNode> binary(
        koopa_ir::PvBinaryOp op, yesod::Ref<koopa_ir::PointwiseNode> lhs,
        yesod::Ref<koopa_ir::PointwiseNode> rhs)
    {
        return program.alloc<koopa_ir::PointwiseNode>(
            koopa_ir::PointwiseNode {
                .sourcePos = { },
                .kind = koopa_ir::PointwiseBinary {
                    .sourcePos = { },
                    .op = op,
                    .lhs = lhs,
                    .rhs = rhs,
                },
            });
    }

    [[nodiscard]] yesod::Ref<koopa_ir::PointwiseNode> polyMulRoot()
    {
        return binary(koopa_ir::PvBinaryOp::mul, leaf(symbol("%p1")),
            leaf(symbol("%p2")));
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

    void addPointwise(
        const std::string& name, yesod::Ref<koopa_ir::PointwiseNode> root)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::PointwiseExpr>(koopa_ir::PointwiseExpr {
                .sourcePos = { },
                .root = root,
                .annotations = { },
            }));
    }

    void addCombine(const std::string& name, koopa_ir::Value value,
        koopa_ir::Value start = integer(0),
        std::optional<koopa_ir::Value> end = std::nullopt,
        koopa_ir::Value shift = integer(0), koopa_ir::Value scale = integer(1))
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::CombineExpr>(koopa_ir::CombineExpr {
                .sourcePos = { },
                .terms = { koopa_ir::CombineTerm {
                    .value = std::move(value),
                    .start = std::move(start),
                    .end = std::move(end),
                    .shift = std::move(shift),
                    .scale = std::move(scale),
                } },
                .annotations = { },
            }));
    }

    void addCombineTerms(
        const std::string& name, std::vector<koopa_ir::CombineTerm> terms)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::CombineExpr>(koopa_ir::CombineExpr {
                .sourcePos = { },
                .terms = std::move(terms),
                .annotations = { },
            }));
    }

    void addPolyLen(const std::string& name, koopa_ir::Value value)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::PolyLenExpr>(koopa_ir::PolyLenExpr {
                .sourcePos = { },
                .op = koopa_ir::PolyLenOp::len,
                .args = { std::move(value) },
                .annotations = { },
            }));
    }

    void addCopy(const std::string& name, koopa_ir::Value value)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::CopyExpr>(koopa_ir::CopyExpr {
                .sourcePos = { },
                .value = std::move(value),
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

void testRejectsPointwiseOperandThatIsPointwiseResult()
{
    requireInvalid("nested pointwise operand",
        "pointwise operand cannot be another pointwise result",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPointwise("%pw1", builder.polyMulRoot());
            builder.addPointwise("%bad",
                builder.binary(koopa_ir::PvBinaryOp::mul,
                    builder.leaf(symbol("%pw1")), builder.leaf(symbol("%p1"))));
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
            builder.addPolyLen("%bad", symbol("%combine1"));
        });
}

void testRejectsPolyLenOfPointwise()
{
    requireInvalid("poly_len of pointwise",
        "poly_len cannot consume a fused poly result",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPointwise("%pw1", builder.polyMulRoot());
            builder.addPolyLen("%bad", symbol("%pw1"));
        });
}

void testAcceptsCopyPseudoInstruction()
{
    ValidationProgramBuilder builder;
    builder.addCopy("%copy1", symbol("%i1"));
    koopa_ir::validate(builder.program);
}

void testRejectsPlainPolyBinary()
{
    requireInvalid("plain poly binary",
        "poly values must use dedicated pseudo-instructions",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addBinary(
                "%bad", koopa_ir::BinaryOp::add, symbol("%p1"), symbol("%p2"));
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
            builder.addCombine("%bad", symbol("%p1"), integer(0), std::nullopt,
                integer(0), symbol("%p2"));
        });
}

void testRejectsPointwiseAddTypeMismatch()
{
    requireInvalid("pointwise add type mismatch",
        "pointwise add/sub/mul expect poly operands",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPointwise("%bad",
                builder.binary(koopa_ir::PvBinaryOp::add,
                    builder.leaf(symbol("%p1")), builder.leaf(symbol("%m1"))));
        });
}

void testRejectsPointwiseMulTypeMismatch()
{
    requireInvalid("pointwise mul type mismatch",
        "pointwise add/sub/mul expect poly operands",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPointwise("%bad",
                builder.binary(koopa_ir::PvBinaryOp::mul,
                    builder.leaf(symbol("%p1")), builder.leaf(symbol("%m1"))));
        });
}

void testRejectsPointwiseTimesTypeMismatch()
{
    requireInvalid("pointwise times type mismatch",
        "pointwise times expects poly and mint operands",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPointwise("%bad",
                builder.binary(koopa_ir::PvBinaryOp::times,
                    builder.leaf(symbol("%m1")), builder.leaf(symbol("%p1"))));
        });
}

void testRejectsTrivialCombineOfPointwiseResults()
{
    requireInvalid("trivial combine of pointwise results",
        "combine cannot be a trivial linear combination of pointwise results",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addPointwise("%pw1", builder.polyMulRoot());
            builder.addPointwise("%pw2",
                builder.binary(koopa_ir::PvBinaryOp::mul,
                    builder.leaf(symbol("%p2")), builder.leaf(symbol("%p1"))));
            builder.addCombineTerms("%bad",
                {
                    koopa_ir::CombineTerm {
                        .value = symbol("%pw1"),
                        .start = integer(0),
                        .end = std::nullopt,
                        .shift = integer(0),
                        .scale = integer(1),
                    },
                    koopa_ir::CombineTerm {
                        .value = symbol("%pw2"),
                        .start = integer(0),
                        .end = std::nullopt,
                        .shift = integer(0),
                        .scale = integer(7),
                    },
                });
        });
}

} // namespace

int main()
{
    testRejectsPointwiseOperandThatIsPointwiseResult();
    testRejectsNestedCombine();
    testRejectsPolyLenOfCombine();
    testRejectsPolyLenOfPointwise();
    testRejectsPlainPolyBinary();
    testRejectsBadCombineStartType();
    testRejectsBadCombineScaleType();
    testRejectsPointwiseAddTypeMismatch();
    testRejectsPointwiseMulTypeMismatch();
    testRejectsPointwiseTimesTypeMismatch();
    testRejectsTrivialCombineOfPointwiseResults();
    testAcceptsCopyPseudoInstruction();
    return 0;
}
