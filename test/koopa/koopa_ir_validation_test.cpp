#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "koopa/ast_to_koopa.h"
#include "koopa/ir.h"
#include "koopa/koopa_interpreter.h"
#include "koopa/koopa_simplify_assert.h"

namespace {

namespace koopa_ir = yesod::koopa::ir;
namespace frontend = yesod::frontend;
namespace koopa_interpreter = yesod::test_support::koopa::interpreter;

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
        addParam("%mp1",
            program.alloc<koopa_ir::PointerType>(koopa_ir::PointerType {
                .sourcePos = { },
                .pointeeType = koopa_ir::MintType { },
            }));
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

    void addGetAttr(
        const std::string& name, koopa_ir::PolyAttr attr, koopa_ir::Value value)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::GetAttrExpr>(koopa_ir::GetAttrExpr {
                .sourcePos = { },
                .attr = attr,
                .value = std::move(value),
                .annotations = { },
            }));
    }

    void addSetAttr(const std::string& name, koopa_ir::PolyAttr attr,
        koopa_ir::Value value, koopa_ir::Value attrValue)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::SetAttrExpr>(koopa_ir::SetAttrExpr {
                .sourcePos = { },
                .attr = attr,
                .value = std::move(value),
                .attrValue = std::move(attrValue),
                .annotations = { },
            }));
    }

    void addSelect(const std::string& name, koopa_ir::Value condition,
        koopa_ir::Value trueValue, koopa_ir::Value falseValue)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::SelectExpr>(koopa_ir::SelectExpr {
                .sourcePos = { },
                .condition = std::move(condition),
                .trueValue = std::move(trueValue),
                .falseValue = std::move(falseValue),
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

    void addPolyConstruct(
        const std::string& name, std::vector<koopa_ir::Value> elements)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::PolyConstructExpr>(
                koopa_ir::PolyConstructExpr {
                    .sourcePos = { },
                    .elements = std::move(elements),
                    .annotations = { },
                }));
    }

    void addGetCoeff(
        const std::string& name, koopa_ir::Value value, koopa_ir::Value index)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::GetCoeffExpr>(koopa_ir::GetCoeffExpr {
                .sourcePos = { },
                .value = std::move(value),
                .index = std::move(index),
                .annotations = { },
            }));
    }

    void addConversion(const std::string& name, koopa_ir::ConversionOp op,
        koopa_ir::Value value)
    {
        addSymbolDef(name,
            program.alloc<koopa_ir::ConversionExpr>(koopa_ir::ConversionExpr {
                .sourcePos = { },
                .op = op,
                .value = std::move(value),
                .annotations = { },
            }));
    }

    void setReturn(koopa_ir::Value value)
    {
        program[blockRef.ref()].terminator
            = program.alloc<koopa_ir::ReturnTerminator>(
                koopa_ir::ReturnTerminator {
                    .sourcePos = { },
                    .value = std::move(value),
                    .annotations = { },
                });
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

void testAcceptsPolyAttrPseudoInstructions()
{
    ValidationProgramBuilder builder;
    builder.addGetAttr("%base", koopa_ir::PolyAttr::base, symbol("%p1"));
    builder.addGetAttr("%left", koopa_ir::PolyAttr::l, symbol("%p1"));
    builder.addSetAttr(
        "%with_base", koopa_ir::PolyAttr::base, symbol("%p1"), symbol("%base"));
    builder.addSetAttr("%with_l", koopa_ir::PolyAttr::l, symbol("%with_base"),
        symbol("%left"));
    builder.addSelect("%selected_l", integer(1), symbol("%left"), integer(0));
    builder.addSelect(
        "%selected_ptr", integer(1), symbol("%base"), symbol("%mp1"));
    koopa_ir::validate(builder.program);
}

void testRejectsBadPolyAttrTypes()
{
    requireInvalid("get_attr non-poly", "get_attr expects a poly operand",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addGetAttr("%bad", koopa_ir::PolyAttr::l, symbol("%i1"));
        });
    requireInvalid("set_attr non-poly", "set_attr expects a poly operand",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addSetAttr(
                "%bad", koopa_ir::PolyAttr::l, symbol("%i1"), integer(0));
        });
    requireInvalid("set_attr l pointer", "set_attr l/r expects int",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addSetAttr(
                "%bad", koopa_ir::PolyAttr::l, symbol("%p1"), symbol("%mp1"));
        });
    requireInvalid("set_attr base int",
        "set_attr base/addr expects mint pointer",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addSetAttr(
                "%bad", koopa_ir::PolyAttr::base, symbol("%p1"), integer(0));
        });
    requireInvalid("select mismatched",
        "select operands must have matching types",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addSelect("%bad", integer(1), symbol("%i1"), symbol("%p1"));
        });
    requireInvalid("get_attr set_attr result",
        "get_attr input must not be a set_attr result",
        [](ValidationProgramBuilder& builder) -> void {
            builder.addSetAttr(
                "%set", koopa_ir::PolyAttr::l, symbol("%p1"), integer(0));
            builder.addGetAttr("%bad", koopa_ir::PolyAttr::l, symbol("%set"));
        });
}

void testKoopaInterpreterPolyAttrActiveInterval()
{
    ValidationProgramBuilder builder;
    builder.program[builder.functionRef.ref()].params.clear();
    builder.program[builder.functionRef.ref()].returnType
        = koopa_ir::I32Type { };
    builder.addPolyConstruct(
        "%p", { integer(1), integer(2), integer(3), integer(4) });
    builder.addSetAttr("%p_l", koopa_ir::PolyAttr::l, symbol("%p"), integer(1));
    builder.addSetAttr(
        "%p_r", koopa_ir::PolyAttr::r, symbol("%p_l"), integer(3));
    builder.addGetCoeff("%c0", symbol("%p_r"), integer(0));
    builder.addGetCoeff("%c1", symbol("%p_r"), integer(1));
    builder.addGetCoeff("%c3", symbol("%p_r"), integer(3));
    builder.addConversion(
        "%i0", koopa_ir::ConversionOp::mint2int, symbol("%c0"));
    builder.addConversion(
        "%i1", koopa_ir::ConversionOp::mint2int, symbol("%c1"));
    builder.addConversion(
        "%i3", koopa_ir::ConversionOp::mint2int, symbol("%c3"));
    builder.addBinary(
        "%sum01", koopa_ir::BinaryOp::add, symbol("%i0"), symbol("%i1"));
    builder.addBinary(
        "%sum", koopa_ir::BinaryOp::add, symbol("%sum01"), symbol("%i3"));
    builder.setReturn(symbol("%sum"));
    koopa_ir::validate(builder.program);

    std::istringstream input;
    std::ostringstream output;
    const auto result = koopa_interpreter::execute(
        builder.program, input, output, std::stop_token { });
    require(result.status == koopa_interpreter::ExecuteStatus::normal,
        std::string("active interval execution failed: ") + result.message);
    require(result.returnValue == 2,
        "active interval should zero coefficients outside [l, r)");
}

void testKoopaInterpreterRejectsInvalidPolyActiveInterval()
{
    ValidationProgramBuilder builder;
    builder.program[builder.functionRef.ref()].params.clear();
    builder.program[builder.functionRef.ref()].returnType
        = koopa_ir::I32Type { };
    builder.addPolyConstruct("%p", { integer(1), integer(2) });
    builder.addSetAttr(
        "%p_l", koopa_ir::PolyAttr::l, symbol("%p"), integer(-1));
    builder.addSetAttr(
        "%p_r", koopa_ir::PolyAttr::r, symbol("%p_l"), integer(1));
    builder.addGetCoeff("%c", symbol("%p_r"), integer(0));
    builder.addConversion("%i", koopa_ir::ConversionOp::mint2int, symbol("%c"));
    builder.setReturn(symbol("%i"));
    koopa_ir::validate(builder.program);

    std::istringstream input;
    std::ostringstream output;
    const auto result = koopa_interpreter::execute(
        builder.program, input, output, std::stop_token { });
    require(result.status == koopa_interpreter::ExecuteStatus::runtimeError,
        "invalid active interval should be rejected at runtime");
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
    testAcceptsPolyAttrPseudoInstructions();
    testRejectsBadPolyAttrTypes();
    testKoopaInterpreterPolyAttrActiveInterval();
    testKoopaInterpreterRejectsInvalidPolyActiveInterval();
    testPolyLocalsLowerToSsaValues();
    testScalarParamArrayInitializerReadsSsaValue();
    return 0;
}
