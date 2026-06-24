#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "koopa/ast_to_koopa.h"
#include "koopa/ir.h"
#include "koopa/koopa_simplify_assert.h"

namespace {

namespace koopa_ir = yesod::koopa::ir;
namespace frontend = yesod::frontend;

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

    void addSymbolDef(
        const std::string& name, koopa_ir::SymbolRhs rhs, int32_t sourceId = 1)
    {
        auto defRef = program.alloc<koopa_ir::SymbolDef>(koopa_ir::SymbolDef {
            .sourcePos = koopa_ir::SourcePos { sourceId },
            .symbol = symbol(name),
            .rhs = std::move(rhs),
            .annotations = { },
        });
        program[blockRef.ref()].statements.push_back(defRef);
    }

    void addPointwise(const std::string& name,
        yesod::Ref<koopa_ir::PointwiseNode> root, int32_t sourceId = 1)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::PointwiseExpr>(koopa_ir::PointwiseExpr {
                .sourcePos = koopa_ir::SourcePos { sourceId },
                .root = root,
                .annotations = { },
            }),
            sourceId);
    }

    void addCombine(const std::string& name, koopa_ir::Value value,
        koopa_ir::Value start = integer(0),
        std::optional<koopa_ir::Value> end = std::nullopt,
        koopa_ir::Value shift = integer(0), koopa_ir::Value scale = integer(1),
        int32_t sourceId = 1)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::CombineExpr>(koopa_ir::CombineExpr {
                .sourcePos = koopa_ir::SourcePos { sourceId },
                .terms = { koopa_ir::CombineTerm {
                    .value = std::move(value),
                    .start = std::move(start),
                    .end = std::move(end),
                    .shift = std::move(shift),
                    .scale = std::move(scale),
                } },
                .annotations = { },
            }),
            sourceId);
    }

    void addCombineTerms(const std::string& name,
        std::vector<koopa_ir::CombineTerm> terms, int32_t sourceId = 1)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::CombineExpr>(koopa_ir::CombineExpr {
                .sourcePos = koopa_ir::SourcePos { sourceId },
                .terms = std::move(terms),
                .annotations = { },
            }),
            sourceId);
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
        "pointwise operand cannot be another pointwise result from the same "
        "expression",
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
        "combine term source cannot be another combine from the same "
        "expression",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addCombine("%combine1", symbol("%p1"));
            builder.addCombine("%bad", symbol("%combine1"));
        });
}

void testAcceptsCrossStatementNestedFusionValues()
{
    ValidationProgramBuilder builder;
    builder.addPointwise("%pw1", builder.polyMulRoot(), 1);
    builder.addPointwise("%pw2",
        builder.binary(koopa_ir::PvBinaryOp::mul, builder.leaf(symbol("%pw1")),
            builder.leaf(symbol("%p1"))),
        2);
    builder.addCombine("%combine1", symbol("%p1"), integer(0), std::nullopt,
        integer(0), integer(1), 3);
    builder.addCombine("%combine2", symbol("%combine1"), integer(0),
        std::nullopt, integer(0), integer(1), 4);
    koopa_ir::validate(builder.program);
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

void testPolyLocalsLowerToSsaValues()
{
    constexpr const char* source = R"(
poly add_one(poly p)
{
    poly q = p;
    q = q + poly(1);
    return q;
}

poly add_twice(poly p, poly q)
{
    return p + q + p;
}

int main()
{
    return 0;
}
)";
    frontend::Parser parser(
        frontend::prependBuiltinFunctionDeclarations(std::string(source)));
    auto parseOutput = parser.parse();
    require(parseOutput.success(), "poly local SSA test should parse");

    frontend::SemanticAnalyzer semanticAnalyzer;
    auto semanticOutput = semanticAnalyzer.analyze(
        std::move(parseOutput.m_ast), parseOutput.m_root.ref());
    require(semanticOutput.success(), "poly local SSA test should be semantic");

    yesod::koopa::Generator generator;
    auto program = generator.generateIr(
        semanticOutput.m_ast, semanticOutput.m_root, semanticOutput.m_info);
    koopa_ir::validate(*program);
    const auto text = koopa_ir::serializeToKoopa(*program);
    require(text.find("alloc poly") == std::string::npos,
        "poly locals and params should not lower to alloc poly");
    require(text.find("load %v_") == std::string::npos,
        "poly locals and params should not lower to local loads");
    require(text.find("store %arg_0") == std::string::npos,
        "poly params should not be stored into local slots");
    require(text.find("= %arg_0") == std::string::npos,
        "poly function params should not be copied before use");
    require(text.find("= %arg_1") == std::string::npos,
        "poly function params should not be copied before use");
}

void testScalarParamArrayInitializerReadsSsaValue()
{
    constexpr const char* source = R"(
int array_init_from_params(int a1, int a2)
{
    int arr[2] = {a1, a2};
    return arr[0] + arr[1];
}

int main()
{
    return array_init_from_params(1, 2);
}
)";
    frontend::Parser parser(
        frontend::prependBuiltinFunctionDeclarations(std::string(source)));
    auto parseOutput = parser.parse();
    require(parseOutput.success(),
        "scalar param array initializer test should parse");

    frontend::SemanticAnalyzer semanticAnalyzer;
    auto semanticOutput = semanticAnalyzer.analyze(
        std::move(parseOutput.m_ast), parseOutput.m_root.ref());
    require(semanticOutput.success(),
        "scalar param array initializer test should be semantic");

    yesod::koopa::Generator generator;
    auto program = generator.generateIr(
        semanticOutput.m_ast, semanticOutput.m_root, semanticOutput.m_info);
    koopa_ir::validate(*program);
}

} // namespace

int main()
{
    yesod::test_support::koopa::assertPolySimplificationPassApplied();
    testRejectsPointwiseOperandThatIsPointwiseResult();
    testRejectsNestedCombine();
    testAcceptsCrossStatementNestedFusionValues();
    testRejectsPlainPolyBinary();
    testRejectsBadCombineStartType();
    testRejectsBadCombineScaleType();
    testRejectsPointwiseAddTypeMismatch();
    testRejectsPointwiseMulTypeMismatch();
    testRejectsPointwiseTimesTypeMismatch();
    testRejectsTrivialCombineOfPointwiseResults();
    testAcceptsCopyPseudoInstruction();
    testPolyLocalsLowerToSsaValues();
    testScalarParamArrayInitializerReadsSsaValue();
    return 0;
}
