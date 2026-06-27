#ifndef _YESOD_TEST_KOOPA_KOOPA_SIMPLIFY_ASSERT_H_
#define _YESOD_TEST_KOOPA_KOOPA_SIMPLIFY_ASSERT_H_

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "koopa/cse.h"
#include "koopa/ir.h"

namespace yesod::test_support::koopa {

namespace detail {

    namespace koopa_ir = yesod::koopa::ir;

    [[noreturn]] inline void failSimplifyAssert(const std::string& message)
    {
        std::cerr << "simplify assertion failure: " << message << std::endl;
        std::exit(1);
    }

    inline void requireSimplifyAssert(
        bool condition, const std::string& message)
    {
        if (!condition) {
            failSimplifyAssert(message);
        }
    }

    [[nodiscard]] inline koopa_ir::Symbol symbol(const std::string& spelling)
    {
        return koopa_ir::Symbol { .sourcePos = { }, .spelling = spelling };
    }

    [[nodiscard]] inline koopa_ir::IntegerLiteral integer(int32_t value)
    {
        return koopa_ir::IntegerLiteral { .sourcePos = { }, .value = value };
    }

    struct SimplifyProgramBuilder {
        koopa_ir::Program program;
        yesod::Ptr<koopa_ir::FunctionDef> functionRef;
        yesod::Ptr<koopa_ir::BasicBlock> entryRef;

        SimplifyProgramBuilder() { functionRef = makeFunction().ptr(); }

        [[nodiscard]] yesod::Ref<koopa_ir::FunctionDef> makeFunction()
        {
            auto returnRef = program.alloc<koopa_ir::ReturnTerminator>(
                koopa_ir::ReturnTerminator {
                    .sourcePos = { },
                    .value = std::nullopt,
                    .annotations = { },
                });
            entryRef
                = program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                    .sourcePos = { },
                    .label = symbol("%entry"),
                    .params = { },
                    .statements = { },
                    .terminator = returnRef,
                    .annotations = { },
                });
            auto function
                = program.alloc<koopa_ir::FunctionDef>(koopa_ir::FunctionDef {
                    .sourcePos = { },
                    .name = symbol("@main"),
                    .params = { },
                    .returnType = koopa_ir::PolyType { },
                    .blocks = { entryRef.ref() },
                    .annotations = { },
                });
            program.items.push_back(function);
            addParam(function, "%p1", koopa_ir::PolyType { });
            addParam(function, "%p2", koopa_ir::PolyType { });
            addParam(function, "%m1", koopa_ir::MintType { });
            addParam(function, "%i1", koopa_ir::I32Type { });
            return function;
        }

        void addParam(const std::string& name, koopa_ir::Type type)
        {
            addParam(functionRef.ref(), name, std::move(type));
        }

        void addParam(yesod::Ref<koopa_ir::FunctionDef> function,
            const std::string& name, koopa_ir::Type type)
        {
            program[function].params.push_back(
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
                .sourcePos = koopa_ir::SourcePos { 1 },
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
                .sourcePos = koopa_ir::SourcePos { 1 },
                .kind = koopa_ir::PointwiseBinary {
                    .sourcePos = koopa_ir::SourcePos { 1 },
                    .op = op,
                    .lhs = lhs,
                    .rhs = rhs,
                },
            });
        }

        void addSymbolDef(const std::string& name, koopa_ir::SymbolRhs rhs)
        {
            auto defRef
                = program.alloc<koopa_ir::SymbolDef>(koopa_ir::SymbolDef {
                    .sourcePos = koopa_ir::SourcePos { 1 },
                    .symbol = symbol(name),
                    .rhs = std::move(rhs),
                    .annotations = { },
                });
            program[entryRef.ref()].statements.push_back(defRef);
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

        [[nodiscard]] yesod::Ref<koopa_ir::SymbolDef> makeBinaryDef(
            const std::string& name, koopa_ir::BinaryOp op, koopa_ir::Value lhs,
            koopa_ir::Value rhs)
        {
            return program.alloc<koopa_ir::SymbolDef>(koopa_ir::SymbolDef {
                .sourcePos = { },
                .symbol = symbol(name),
                .rhs
                = program.alloc<koopa_ir::BinaryExpr>(koopa_ir::BinaryExpr {
                    .sourcePos = { },
                    .op = op,
                    .lhs = std::move(lhs),
                    .rhs = std::move(rhs),
                    .annotations = { },
                }),
                .annotations = { },
            });
        }

        void addPointwise(
            const std::string& name, yesod::Ref<koopa_ir::PointwiseNode> root)
        {
            addSymbolDef(name,
                program.alloc<koopa_ir::PointwiseExpr>(koopa_ir::PointwiseExpr {
                    .sourcePos = koopa_ir::SourcePos { 1 },
                    .length = integer(4),
                    .activeL = integer(0),
                    .activeR = integer(4),
                    .root = root,
                    .annotations = { },
                }));
        }

        void addCallStatement(
            const std::string& callee, std::vector<koopa_ir::Value> args)
        {
            program[entryRef.ref()].statements.push_back(
                program.alloc<koopa_ir::CallExpr>(koopa_ir::CallExpr {
                    .sourcePos = { },
                    .callee = symbol(callee),
                    .args = std::move(args),
                    .annotations = { },
                }));
        }

        void addCombine(const std::string& name, koopa_ir::Value value,
            koopa_ir::Value start = integer(0),
            std::optional<koopa_ir::Value> end = std::nullopt,
            koopa_ir::Value shift = integer(0),
            koopa_ir::Value scale = integer(1))
        {
            addSymbolDef(name,
                program.alloc<koopa_ir::CombineExpr>(koopa_ir::CombineExpr {
                    .sourcePos = koopa_ir::SourcePos { 1 },
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

        void addGetAttr(const std::string& name, koopa_ir::PolyAttr attr,
            koopa_ir::Value value)
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

        [[nodiscard]] bool hasDef(const std::string& name) const
        {
            const auto& block = program[entryRef.ref()];
            for (const auto& statement : block.statements) {
                const auto* defRef
                    = std::get_if<yesod::Ref<koopa_ir::SymbolDef>>(&statement);
                if (defRef != nullptr
                    && program[*defRef].symbol.spelling == name) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool hasBlock(const std::string& label) const
        {
            const auto& function = program[functionRef.ref()];
            for (const auto blockRef : function.blocks) {
                if (program[blockRef].label.spelling == label) {
                    return true;
                }
            }
            return false;
        }
    };

    inline void assertCopyForwardingAndDce()
    {
        SimplifyProgramBuilder builder;
        builder.addCopy("%copy", symbol("%p1"));
        builder.addPointwise("%use", builder.leaf(symbol("%copy")));

        koopa_ir::simplifyLocalValues(builder.program,
            builder.program[builder.entryRef.ref()], 0, { symbol("%use") });

        requireSimplifyAssert(!builder.hasDef("%copy"),
            "copy forwarding should delete the forwarded value");
        const auto defRef = std::get<yesod::Ref<koopa_ir::SymbolDef>>(
            builder.program[builder.entryRef.ref()].statements.front());
        const auto pointwiseRef = std::get<yesod::Ref<koopa_ir::PointwiseExpr>>(
            builder.program[defRef].rhs);
        const auto& leaf = std::get<koopa_ir::PointwiseLeaf>(
            builder.program[builder.program[pointwiseRef].root].kind);
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(leaf.value).spelling == "%p1",
            "copy forwarding should rewrite the unique use");
    }

    inline void assertCombineForwardingAndDce()
    {
        SimplifyProgramBuilder builder;
        builder.addCombine("%slice", symbol("%p1"), integer(2),
            koopa_ir::Value { integer(5) }, integer(-1), symbol("%m1"));
        builder.addCombine("%use", symbol("%slice"));

        koopa_ir::simplifyLocalValues(builder.program,
            builder.program[builder.entryRef.ref()], 0, { symbol("%use") });

        requireSimplifyAssert(!builder.hasDef("%slice"),
            "combine forwarding should delete the forwarded value");
        const auto defRef = std::get<yesod::Ref<koopa_ir::SymbolDef>>(
            builder.program[builder.entryRef.ref()].statements.front());
        const auto combineRef = std::get<yesod::Ref<koopa_ir::CombineExpr>>(
            builder.program[defRef].rhs);
        const auto& term = builder.program[combineRef].terms.front();
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(term.value).spelling == "%p1",
            "combine forwarding should preserve source value");
        requireSimplifyAssert(
            std::get<koopa_ir::IntegerLiteral>(term.start).value == 2,
            "combine forwarding should preserve slice start");
    }

    inline void assertCopyForwardingAllowsRepeatedUses()
    {
        SimplifyProgramBuilder builder;
        builder.addCopy("%copy", symbol("%p1"));
        builder.addPointwise("%use",
            builder.binary(koopa_ir::PvBinaryOp::add,
                builder.leaf(symbol("%copy")), builder.leaf(symbol("%copy"))));

        koopa_ir::simplifyLocalValues(builder.program,
            builder.program[builder.entryRef.ref()], 0, { symbol("%use") });

        requireSimplifyAssert(!builder.hasDef("%copy"),
            "copy forwarding should apply even when the copy has repeated "
            "uses");
        const auto defRef = std::get<yesod::Ref<koopa_ir::SymbolDef>>(
            builder.program[builder.entryRef.ref()].statements.front());
        const auto pointwiseRef = std::get<yesod::Ref<koopa_ir::PointwiseExpr>>(
            builder.program[defRef].rhs);
        const auto& root = builder.program[builder.program[pointwiseRef].root];
        const auto& binary = std::get<koopa_ir::PointwiseBinary>(root.kind);
        const auto& lhs = std::get<koopa_ir::PointwiseLeaf>(
            builder.program[binary.lhs].kind);
        const auto& rhs = std::get<koopa_ir::PointwiseLeaf>(
            builder.program[binary.rhs].kind);
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(lhs.value).spelling == "%p1",
            "copy forwarding should rewrite the first repeated use");
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(rhs.value).spelling == "%p1",
            "copy forwarding should rewrite the second repeated use");
    }

    inline void assertCopyForwardingToCallArgument()
    {
        SimplifyProgramBuilder builder;
        builder.addCopy("%copy", symbol("%p1"));
        builder.addCallStatement("@putpoly", { symbol("%copy") });

        koopa_ir::simplifyLocalValues(
            builder.program, builder.program[builder.entryRef.ref()], 0, { });

        requireSimplifyAssert(!builder.hasDef("%copy"),
            "copy forwarding to call argument should delete the forwarded "
            "value");
        const auto callRef = std::get<yesod::Ref<koopa_ir::CallExpr>>(
            builder.program[builder.entryRef.ref()].statements.front());
        requireSimplifyAssert(builder.program[callRef].args.size() == 1,
            "call should keep the argument count");
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(builder.program[callRef].args.front())
                    .spelling
                == "%p1",
            "call argument should be rewritten to the forwarded value");
    }

    inline void assertCopyForwardingToJumpBlockArgument()
    {
        SimplifyProgramBuilder builder;
        auto retRef = builder.program.alloc<koopa_ir::ReturnTerminator>(
            koopa_ir::ReturnTerminator {
                .sourcePos = { },
                .value = symbol("%arg"),
                .annotations = { },
            });
        auto exitRef = builder.program.alloc<koopa_ir::BasicBlock>(
            koopa_ir::BasicBlock {
                .sourcePos = { },
                .label = symbol("%exit"),
                .params = {
                    builder.program.alloc<koopa_ir::BlockParameter>(
                        koopa_ir::BlockParameter {
                            .sourcePos = { },
                            .symbol = symbol("%arg"),
                            .type = koopa_ir::PolyType { },
                            .annotations = { },
                        }),
                },
                .statements = { },
                .terminator = retRef,
                .annotations = { },
            });
        builder.program[builder.functionRef.ref()].blocks.push_back(exitRef);
        builder.addCopy("%copy", symbol("%p1"));
        builder.program[builder.entryRef.ref()].terminator
            = builder.program.alloc<koopa_ir::JumpTerminator>(
                koopa_ir::JumpTerminator {
                    .sourcePos = { },
                    .target = symbol("%exit"),
                    .args = { symbol("%copy") },
                    .annotations = { },
                });

        koopa_ir::simplifyLocalValues(
            builder.program, builder.program[builder.entryRef.ref()], 0, { });

        requireSimplifyAssert(!builder.hasDef("%copy"),
            "copy forwarding to jump block argument should delete the "
            "forwarded value");
        const auto jumpRef = std::get<yesod::Ref<koopa_ir::JumpTerminator>>(
            builder.program[builder.entryRef.ref()].terminator);
        requireSimplifyAssert(builder.program[jumpRef].args.size() == 1,
            "jump should keep the block argument count");
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(builder.program[jumpRef].args.front())
                    .spelling
                == "%p1",
            "jump block argument should be rewritten to the forwarded value");
    }

    inline void assertDeadBlockParamElimination()
    {
        SimplifyProgramBuilder builder;
        auto retRef = builder.program.alloc<koopa_ir::ReturnTerminator>(
            koopa_ir::ReturnTerminator {
                .sourcePos = { },
                .value = symbol("%live"),
                .annotations = { },
            });
        auto exitRef = builder.program.alloc<koopa_ir::BasicBlock>(
        koopa_ir::BasicBlock {
            .sourcePos = { },
            .label = symbol("%exit"),
            .params = {
                builder.program.alloc<koopa_ir::BlockParameter>(
                    koopa_ir::BlockParameter {
                        .sourcePos = { },
                        .symbol = symbol("%dead"),
                        .type = koopa_ir::PolyType { },
                        .annotations = { },
                    }),
                builder.program.alloc<koopa_ir::BlockParameter>(
                    koopa_ir::BlockParameter {
                        .sourcePos = { },
                        .symbol = symbol("%live"),
                        .type = koopa_ir::PolyType { },
                        .annotations = { },
                    }),
            },
            .statements = { },
            .terminator = retRef,
            .annotations = { },
        });
        builder.program[builder.functionRef.ref()].blocks.push_back(exitRef);
        builder.program[builder.entryRef.ref()].terminator
            = builder.program.alloc<koopa_ir::JumpTerminator>(
                koopa_ir::JumpTerminator {
                    .sourcePos = { },
                    .target = symbol("%exit"),
                    .args = { symbol("%p1"), symbol("%p2") },
                    .annotations = { },
                });

        koopa_ir::eliminateDeadValues(
            builder.program, builder.program[builder.functionRef.ref()]);

        const auto& exitBlock = builder.program[exitRef];
        requireSimplifyAssert(exitBlock.params.size() == 1,
            "dead block parameter should be removed");
        requireSimplifyAssert(
            builder.program[exitBlock.params.front()].symbol.spelling
                == "%live",
            "live block parameter should be preserved");
        const auto jumpRef = std::get<yesod::Ref<koopa_ir::JumpTerminator>>(
            builder.program[builder.entryRef.ref()].terminator);
        requireSimplifyAssert(builder.program[jumpRef].args.size() == 1,
            "edge argument for dead block parameter should be removed");
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(builder.program[jumpRef].args.front())
                    .spelling
                == "%p2",
            "edge argument for live block parameter should be preserved");
    }

    inline void assertEmptyBlockForwardingAndPruning()
    {
        SimplifyProgramBuilder builder;
        auto mergeParamRef = builder.program.alloc<koopa_ir::BlockParameter>(
            koopa_ir::BlockParameter {
                .sourcePos = { },
                .symbol = symbol("%result"),
                .type = koopa_ir::PolyType { },
                .annotations = { },
            });
        auto mergeRetRef = builder.program.alloc<koopa_ir::ReturnTerminator>(
            koopa_ir::ReturnTerminator {
                .sourcePos = { },
                .value = symbol("%result"),
                .annotations = { },
            });
        auto mergeRef
            = builder.program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                .sourcePos = { },
                .label = symbol("%merge"),
                .params = { mergeParamRef },
                .statements = { },
                .terminator = mergeRetRef,
                .annotations = { },
            });

        auto middleParamRef = builder.program.alloc<koopa_ir::BlockParameter>(
            koopa_ir::BlockParameter {
                .sourcePos = { },
                .symbol = symbol("%middleArg"),
                .type = koopa_ir::PolyType { },
                .annotations = { },
            });
        auto middleJumpRef = builder.program.alloc<koopa_ir::JumpTerminator>(
            koopa_ir::JumpTerminator {
                .sourcePos = { },
                .target = symbol("%merge"),
                .args = { symbol("%middleArg") },
                .annotations = { },
            });
        auto middleRef
            = builder.program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                .sourcePos = { },
                .label = symbol("%middle"),
                .params = { middleParamRef },
                .statements = { },
                .terminator = middleJumpRef,
                .annotations = { },
            });

        auto emptyParamRef = builder.program.alloc<koopa_ir::BlockParameter>(
            koopa_ir::BlockParameter {
                .sourcePos = { },
                .symbol = symbol("%emptyArg"),
                .type = koopa_ir::PolyType { },
                .annotations = { },
            });
        auto emptyJumpRef = builder.program.alloc<koopa_ir::JumpTerminator>(
            koopa_ir::JumpTerminator {
                .sourcePos = { },
                .target = symbol("%middle"),
                .args = { symbol("%emptyArg") },
                .annotations = { },
            });
        auto emptyRef
            = builder.program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                .sourcePos = { },
                .label = symbol("%empty"),
                .params = { emptyParamRef },
                .statements = { },
                .terminator = emptyJumpRef,
                .annotations = { },
            });

        auto unreachableJumpRef
            = builder.program.alloc<koopa_ir::JumpTerminator>(
                koopa_ir::JumpTerminator {
                    .sourcePos = { },
                    .target = symbol("%merge"),
                    .args = { symbol("%p2") },
                    .annotations = { },
                });
        auto unreachableRef
            = builder.program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                .sourcePos = { },
                .label = symbol("%unreachable"),
                .params = { },
                .statements = { },
                .terminator = unreachableJumpRef,
                .annotations = { },
            });

        auto branchRef = builder.program.alloc<koopa_ir::BranchTerminator>(
            koopa_ir::BranchTerminator {
                .sourcePos = { },
                .condition = integer(1),
                .trueTarget = symbol("%empty"),
                .trueArgs = { symbol("%p1") },
                .falseTarget = symbol("%merge"),
                .falseArgs = { symbol("%p2") },
                .annotations = { },
            });
        builder.program[builder.entryRef.ref()].terminator = branchRef;
        auto& function = builder.program[builder.functionRef.ref()];
        function.blocks.push_back(emptyRef);
        function.blocks.push_back(middleRef);
        function.blocks.push_back(mergeRef);
        function.blocks.push_back(unreachableRef);

        koopa_ir::eliminateEmptyBasicBlocks(builder.program, function);
        koopa_ir::validate(builder.program);

        const auto& branch = builder.program[branchRef];
        requireSimplifyAssert(branch.trueTarget.spelling == "%merge",
            "empty block forwarding should rewrite branch true target");
        requireSimplifyAssert(branch.falseTarget.spelling == "%merge",
            "branch false target should still reach merge");
        requireSimplifyAssert(branch.trueArgs.size() == 1,
            "forwarded branch edge should keep one block argument");
        requireSimplifyAssert(branch.falseArgs.size() == 1,
            "direct branch edge should keep one block argument");
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(branch.trueArgs.front()).spelling
                == "%p1",
            "forwarded branch edge should substitute empty block parameters");
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(branch.falseArgs.front()).spelling
                == "%p2",
            "direct branch edge argument should be preserved");
        requireSimplifyAssert(function.blocks.size() == 2,
            "only entry and merge blocks should remain reachable");
        requireSimplifyAssert(!builder.hasBlock("%empty"),
            "forwarded empty block should be removed");
        requireSimplifyAssert(!builder.hasBlock("%middle"),
            "chained empty block should be removed");
        requireSimplifyAssert(!builder.hasBlock("%unreachable"),
            "unreachable block should be removed");
    }

    inline void assertCommonSubexpressionElimination()
    {
        SimplifyProgramBuilder builder;
        builder.addBinary(
            "%first", koopa_ir::BinaryOp::add, symbol("%i1"), integer(2));
        builder.addBinary(
            "%second", koopa_ir::BinaryOp::add, integer(2), symbol("%i1"));
        builder.addCallStatement("@use", { symbol("%second") });

        koopa_ir::eliminateCommonSubexpressions(
            builder.program, builder.program[builder.functionRef.ref()]);

        requireSimplifyAssert(builder.hasDef("%first"),
            "the first common expression should remain as the representative");
        requireSimplifyAssert(!builder.hasDef("%second"),
            "the duplicate common expression should be removed");
        const auto callRef = std::get<yesod::Ref<koopa_ir::CallExpr>>(
            builder.program[builder.entryRef.ref()].statements.back());
        requireSimplifyAssert(builder.program[callRef].args.size() == 1,
            "call using the duplicate expression should keep one argument");
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(builder.program[callRef].args.front())
                    .spelling
                == "%first",
            "uses of the duplicate expression should point at the "
            "representative");
    }

    inline void assertCommonSubexpressionRequiresDominance()
    {
        SimplifyProgramBuilder builder;
        auto leftRef
            = builder.program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                .sourcePos = { },
                .label = symbol("%left"),
                .params = { },
                .statements = { builder.makeBinaryDef("%leftValue",
                    koopa_ir::BinaryOp::add, symbol("%i1"), integer(3)) },
                .terminator = builder.program.alloc<koopa_ir::JumpTerminator>(
                    koopa_ir::JumpTerminator {
                        .sourcePos = { },
                        .target = symbol("%merge"),
                        .args = { symbol("%leftValue") },
                        .annotations = { },
                    }),
                .annotations = { },
            });
        auto rightRef
            = builder.program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                .sourcePos = { },
                .label = symbol("%right"),
                .params = { },
                .statements = { builder.makeBinaryDef("%rightValue",
                    koopa_ir::BinaryOp::add, symbol("%i1"), integer(3)) },
                .terminator = builder.program.alloc<koopa_ir::JumpTerminator>(
                    koopa_ir::JumpTerminator {
                        .sourcePos = { },
                        .target = symbol("%merge"),
                        .args = { symbol("%rightValue") },
                        .annotations = { },
                    }),
                .annotations = { },
            });
        auto mergeParamRef = builder.program.alloc<koopa_ir::BlockParameter>(
            koopa_ir::BlockParameter {
                .sourcePos = { },
                .symbol = symbol("%merged"),
                .type = koopa_ir::I32Type { },
                .annotations = { },
            });
        auto mergeRef
            = builder.program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                .sourcePos = { },
                .label = symbol("%merge"),
                .params = { mergeParamRef },
                .statements = { },
                .terminator = builder.program.alloc<koopa_ir::ReturnTerminator>(
                    koopa_ir::ReturnTerminator {
                        .sourcePos = { },
                        .value = symbol("%merged"),
                        .annotations = { },
                    }),
                .annotations = { },
            });
        builder.program[builder.entryRef.ref()].terminator
            = builder.program.alloc<koopa_ir::BranchTerminator>(
                koopa_ir::BranchTerminator {
                    .sourcePos = { },
                    .condition = symbol("%i1"),
                    .trueTarget = symbol("%left"),
                    .trueArgs = { },
                    .falseTarget = symbol("%right"),
                    .falseArgs = { },
                    .annotations = { },
                });
        auto& function = builder.program[builder.functionRef.ref()];
        function.returnType = koopa_ir::I32Type { };
        function.blocks.push_back(leftRef);
        function.blocks.push_back(rightRef);
        function.blocks.push_back(mergeRef);

        koopa_ir::eliminateCommonSubexpressions(builder.program, function);

        requireSimplifyAssert(builder.program[leftRef].statements.size() == 1,
            "left branch expression should remain");
        requireSimplifyAssert(builder.program[rightRef].statements.size() == 1,
            "right branch expression must not be replaced by a non-dominating "
            "definition");
        const auto rightJumpRef
            = std::get<yesod::Ref<koopa_ir::JumpTerminator>>(
                builder.program[rightRef].terminator);
        requireSimplifyAssert(std::get<koopa_ir::Symbol>(
                                  builder.program[rightJumpRef].args.front())
                                  .spelling
                == "%rightValue",
            "right branch should still pass its own value to merge");
    }

    inline void assertConstantFoldFeedsCopyPropagation()
    {
        SimplifyProgramBuilder builder;
        builder.addBinary(
            "%ge", koopa_ir::BinaryOp::ge, integer(4), integer(3));
        builder.addBinary(
            "%use", koopa_ir::BinaryOp::add, symbol("%ge"), integer(0));
        builder.addCallStatement("@use", { symbol("%use") });

        koopa_ir::eliminateCommonSubexpressions(
            builder.program, builder.program[builder.functionRef.ref()]);

        requireSimplifyAssert(!builder.hasDef("%ge"),
            "constant folded ge should be propagated and removed");
        requireSimplifyAssert(!builder.hasDef("%use"),
            "expression simplified after propagation should also be removed");
        const auto callRef = std::get<yesod::Ref<koopa_ir::CallExpr>>(
            builder.program[builder.entryRef.ref()].statements.back());
        requireSimplifyAssert(builder.program[callRef].args.size() == 1,
            "call should keep the propagated argument");
        requireSimplifyAssert(std::get<koopa_ir::IntegerLiteral>(
                                  builder.program[callRef].args.front())
                                  .value
                == 1,
            "ge constant folding should feed the later copy propagation");
    }

    inline void assertCopyRhsFeedsExpressionSimplification()
    {
        SimplifyProgramBuilder builder;
        builder.addCopy("%alias", symbol("%i1"));
        builder.addBinary(
            "%use", koopa_ir::BinaryOp::add, symbol("%alias"), integer(0));
        builder.addCallStatement("@use", { symbol("%use") });

        koopa_ir::eliminateCommonSubexpressions(
            builder.program, builder.program[builder.functionRef.ref()]);

        requireSimplifyAssert(!builder.hasDef("%alias"),
            "copy RHS should be propagated by the CSE pass");
        requireSimplifyAssert(!builder.hasDef("%use"),
            "expression exposed by copy propagation should be simplified");
        const auto callRef = std::get<yesod::Ref<koopa_ir::CallExpr>>(
            builder.program[builder.entryRef.ref()].statements.back());
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(builder.program[callRef].args.front())
                    .spelling
                == "%i1",
            "copy propagation should expose the original value");
    }

    inline void assertGetAttrRecursesThroughSetAttr()
    {
        SimplifyProgramBuilder builder;
        builder.addSetAttr(
            "%with_l", koopa_ir::PolyAttr::l, symbol("%p1"), integer(4));
        builder.addSetAttr(
            "%with_r", koopa_ir::PolyAttr::r, symbol("%with_l"), integer(8));
        builder.addGetAttr("%left", koopa_ir::PolyAttr::l, symbol("%with_r"));
        builder.addCallStatement("@use", { symbol("%left") });

        koopa_ir::eliminateCommonSubexpressions(
            builder.program, builder.program[builder.functionRef.ref()]);

        requireSimplifyAssert(!builder.hasDef("%left"),
            "get_attr should recurse through set_attr and expose the stored "
            "attribute value");
        const auto callRef = std::get<yesod::Ref<koopa_ir::CallExpr>>(
            builder.program[builder.entryRef.ref()].statements.back());
        requireSimplifyAssert(std::get<koopa_ir::IntegerLiteral>(
                                  builder.program[callRef].args.front())
                                  .value
                == 4,
            "get_attr l should resolve through set_attr r to set_attr l");
    }

    inline void assertGetAttrOfPolyConstructBounds()
    {
        SimplifyProgramBuilder builder;
        builder.addPolyConstruct(
            "%poly", { integer(7), symbol("%m1"), integer(9) });
        builder.addGetAttr("%left", koopa_ir::PolyAttr::l, symbol("%poly"));
        builder.addGetAttr("%right", koopa_ir::PolyAttr::r, symbol("%poly"));
        builder.addCallStatement("@use", { symbol("%left"), symbol("%right") });

        koopa_ir::eliminateCommonSubexpressions(
            builder.program, builder.program[builder.functionRef.ref()]);

        requireSimplifyAssert(!builder.hasDef("%left"),
            "get_attr l of poly_construct should fold to zero");
        requireSimplifyAssert(!builder.hasDef("%right"),
            "get_attr r of poly_construct should fold to element count");
        const auto callRef = std::get<yesod::Ref<koopa_ir::CallExpr>>(
            builder.program[builder.entryRef.ref()].statements.back());
        requireSimplifyAssert(builder.program[callRef].args.size() == 2,
            "call should keep both folded attribute values");
        requireSimplifyAssert(
            std::get<koopa_ir::IntegerLiteral>(builder.program[callRef].args[0])
                    .value
                == 0,
            "left bound should be zero");
        requireSimplifyAssert(
            std::get<koopa_ir::IntegerLiteral>(builder.program[callRef].args[1])
                    .value
                == 3,
            "right bound should be the poly_construct element count");
    }

    inline void assertSelectSameBranchFeedsCopyPropagation()
    {
        SimplifyProgramBuilder builder;
        builder.addSelect(
            "%selected", symbol("%i1"), symbol("%m1"), symbol("%m1"));
        builder.addCopy("%use", symbol("%selected"));
        builder.addCallStatement("@use", { symbol("%use") });

        koopa_ir::eliminateCommonSubexpressions(
            builder.program, builder.program[builder.functionRef.ref()]);

        requireSimplifyAssert(!builder.hasDef("%selected"),
            "select with equal branches should be replaced by that branch");
        requireSimplifyAssert(!builder.hasDef("%use"),
            "copy exposed by select simplification should be propagated");
        const auto callRef = std::get<yesod::Ref<koopa_ir::CallExpr>>(
            builder.program[builder.entryRef.ref()].statements.back());
        requireSimplifyAssert(
            std::get<koopa_ir::Symbol>(builder.program[callRef].args.front())
                    .spelling
                == "%m1",
            "uses of same-branch select should point at the common value");
    }

} // namespace detail

inline void assertPolySimplificationPassApplied()
{
    detail::assertCopyForwardingAndDce();
    detail::assertCombineForwardingAndDce();
    detail::assertCopyForwardingAllowsRepeatedUses();
    detail::assertCopyForwardingToCallArgument();
    detail::assertCopyForwardingToJumpBlockArgument();
    detail::assertDeadBlockParamElimination();
    detail::assertEmptyBlockForwardingAndPruning();
    detail::assertCommonSubexpressionElimination();
    detail::assertCommonSubexpressionRequiresDominance();
    detail::assertConstantFoldFeedsCopyPropagation();
    detail::assertCopyRhsFeedsExpressionSimplification();
    detail::assertGetAttrRecursesThroughSetAttr();
    detail::assertGetAttrOfPolyConstructBounds();
    detail::assertSelectSameBranchFeedsCopyPropagation();
}

} // namespace yesod::test_support::koopa

#endif // _YESOD_TEST_KOOPA_KOOPA_SIMPLIFY_ASSERT_H_
