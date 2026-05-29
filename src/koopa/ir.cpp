#include "koopa/ir.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace yesod::koopa::ir {

namespace {

std::string serializeType(const Type& type, const Program& program);
std::string serializeValue(const Value& value);
std::string serializeInitializer(const Initializer& initializer,
    const Program& program);

std::string serializeSymbol(const Symbol& symbol)
{
    return symbol.spelling;
}

std::string serializeType(const Type& type, const Program& program)
{
    return MATCH(type) WITH(
        [&](const I32Type&) { return std::string("i32"); },
        [&](Ref<ArrayType> arrayRef) {
            const auto& arrayType = program[arrayRef];
            return std::string("[")
                + serializeType(arrayType.elementType, program) + ", "
                + std::to_string(arrayType.length) + "]";
        },
        [&](Ref<PointerType> pointerRef) {
            return std::string("*")
                + serializeType(program[pointerRef].pointeeType, program);
        },
        [&](Ref<FunctionType> functionRef) {
            const auto& functionType = program[functionRef];
            std::ostringstream output;
            output << '(';
            for (size_t index = 0; index < functionType.paramTypes.size(); ++index) {
                if (index != 0) {
                    output << ", ";
                }
                output << serializeType(functionType.paramTypes[index], program);
            }
            output << ')';
            if (functionType.returnType.has_value()) {
                output << ": "
                       << serializeType(*functionType.returnType, program);
            }
            return output.str();
        });
}

std::string serializeValue(const Value& value)
{
    return MATCH(value) WITH(
        [&](const Symbol& symbol) { return serializeSymbol(symbol); },
        [&](const IntegerLiteral& literal) {
            return std::to_string(literal.value);
        },
        [&](const UndefValue&) { return std::string("undef"); });
}

std::string serializeInitializer(const Initializer& initializer,
    const Program& program)
{
    return MATCH(initializer) WITH(
        [&](const IntegerLiteral& literal) {
            return std::to_string(literal.value);
        },
        [&](const UndefValue&) { return std::string("undef"); },
        [&](const ZeroInit&) { return std::string("zeroinit"); },
        [&](Ref<AggregateInitializer> aggregateRef) {
            const auto& aggregate = program[aggregateRef];
            std::ostringstream output;
            output << '{';
            for (size_t index = 0; index < aggregate.elements.size(); ++index) {
                if (index != 0) {
                    output << ", ";
                }
                output << serializeInitializer(aggregate.elements[index], program);
            }
            output << '}';
            return output.str();
        });
}

std::string serializeStoreValue(const StoreValue& value, const Program& program)
{
    return MATCH(value) WITH(
        [&](const Symbol& symbol) { return serializeSymbol(symbol); },
        [&](const IntegerLiteral& literal) {
            return std::to_string(literal.value);
        },
        [&](const UndefValue&) { return std::string("undef"); },
        [&](const ZeroInit&) { return std::string("zeroinit"); },
        [&](Ref<AggregateInitializer> aggregateRef) {
            return serializeInitializer(Initializer { aggregateRef }, program);
        });
}

void serializeStatement(std::ostream& output, const Statement& statement,
    const Program& program)
{
    std::visit(
        [&](auto statementRef) {
            using StatementNode
                = std::remove_cvref_t<decltype(program[statementRef])>;
            if constexpr (std::same_as<StatementNode, SymbolDef>) {
                const auto& symbolDef = program[statementRef];
                output << "  " << serializeSymbol(symbolDef.symbol) << " = ";
                std::visit(
                    [&](auto rhsRef) {
                        using Rhs = std::remove_cvref_t<decltype(program[rhsRef])>;
                        if constexpr (std::same_as<Rhs, MemoryDeclaration>) {
                            output << "alloc "
                                   << serializeType(program[rhsRef].allocType, program);
                        } else if constexpr (std::same_as<Rhs, LoadExpr>) {
                            output << "load "
                                   << serializeSymbol(program[rhsRef].source);
                        } else if constexpr (std::same_as<Rhs, GetPointerExpr>) {
                            const auto& expr = program[rhsRef];
                            output << "getptr " << serializeSymbol(expr.source)
                                   << ", " << serializeValue(expr.index);
                        } else if constexpr (std::same_as<Rhs,
                                               GetElementPointerExpr>) {
                            const auto& expr = program[rhsRef];
                            output << "getelemptr "
                                   << serializeSymbol(expr.source) << ", "
                                   << serializeValue(expr.index);
                        } else if constexpr (std::same_as<Rhs, BinaryExpr>) {
                            const auto& expr = program[rhsRef];
                            output << toString(expr.op) << ' '
                                   << serializeValue(expr.lhs) << ", "
                                   << serializeValue(expr.rhs);
                        } else if constexpr (std::same_as<Rhs, CallExpr>) {
                            const auto& expr = program[rhsRef];
                            output << "call " << serializeSymbol(expr.callee) << "(";
                            for (size_t index = 0; index < expr.args.size(); ++index) {
                                if (index != 0) {
                                    output << ", ";
                                }
                                output << serializeValue(expr.args[index]);
                            }
                            output << ')';
                        }
                    },
                    symbolDef.rhs);
                output << '\n';
            } else if constexpr (std::same_as<StatementNode, StoreStmt>) {
                const auto& storeStmt = program[statementRef];
                output << "  store "
                       << serializeStoreValue(storeStmt.value, program) << ", "
                       << serializeSymbol(storeStmt.destination) << '\n';
            } else if constexpr (std::same_as<StatementNode, CallExpr>) {
                const auto& callExpr = program[statementRef];
                output << "  call " << serializeSymbol(callExpr.callee) << "(";
                for (size_t index = 0; index < callExpr.args.size(); ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    output << serializeValue(callExpr.args[index]);
                }
                output << ")\n";
            }
        },
        statement);
}

void serializeTerminator(std::ostream& output, const Terminator& terminator,
    const Program& program)
{
    std::visit(
        [&](auto terminatorRef) {
            using TerminatorNode
                = std::remove_cvref_t<decltype(program[terminatorRef])>;
            if constexpr (std::same_as<TerminatorNode, BranchTerminator>) {
                const auto& branch = program[terminatorRef];
                output << "  br " << serializeValue(branch.condition) << ", "
                       << serializeSymbol(branch.trueTarget);
                if (!branch.trueArgs.empty()) {
                    output << '(';
                    for (size_t index = 0; index < branch.trueArgs.size(); ++index) {
                        if (index != 0) {
                            output << ", ";
                        }
                        output << serializeValue(branch.trueArgs[index]);
                    }
                    output << ')';
                }
                output << ", " << serializeSymbol(branch.falseTarget);
                if (!branch.falseArgs.empty()) {
                    output << '(';
                    for (size_t index = 0; index < branch.falseArgs.size(); ++index) {
                        if (index != 0) {
                            output << ", ";
                        }
                        output << serializeValue(branch.falseArgs[index]);
                    }
                    output << ')';
                }
                output << '\n';
            } else if constexpr (std::same_as<TerminatorNode, JumpTerminator>) {
                const auto& jump = program[terminatorRef];
                output << "  jump " << serializeSymbol(jump.target);
                if (!jump.args.empty()) {
                    output << '(';
                    for (size_t index = 0; index < jump.args.size(); ++index) {
                        if (index != 0) {
                            output << ", ";
                        }
                        output << serializeValue(jump.args[index]);
                    }
                    output << ')';
                }
                output << '\n';
            } else if constexpr (std::same_as<TerminatorNode, ReturnTerminator>) {
                const auto& ret = program[terminatorRef];
                output << "  ret";
                if (ret.value.has_value()) {
                    output << ' ' << serializeValue(*ret.value);
                }
                output << '\n';
            }
        },
        terminator);
}

void serializeBlock(std::ostream& output, const BasicBlock& block,
    const Program& program)
{
    output << serializeSymbol(block.label);
    if (!block.params.empty()) {
        output << '(';
        for (size_t index = 0; index < block.params.size(); ++index) {
            if (index != 0) {
                output << ", ";
            }
            const auto& param = program[block.params[index]];
            output << serializeSymbol(param.symbol) << ": "
                   << serializeType(param.type, program);
        }
        output << ')';
    }
    output << ":\n";
    for (const auto& statement : block.statements) {
        serializeStatement(output, statement, program);
    }
    serializeTerminator(output, block.terminator, program);
}

void serializeTopLevelItem(std::ostream& output, const TopLevelItem& item,
    const Program& program)
{
    std::visit(
        [&](auto itemRef) {
            using Item = std::remove_cvref_t<decltype(program[itemRef])>;
            if constexpr (std::same_as<Item, GlobalMemoryDef>) {
                const auto& global = program[itemRef];
                output << "global " << serializeSymbol(global.name)
                       << " = alloc "
                       << serializeType(global.allocType, program) << ", "
                       << serializeInitializer(global.initializer, program)
                       << "\n\n";
            } else if constexpr (std::same_as<Item, FunctionDecl>) {
                const auto& function = program[itemRef];
                output << "decl " << serializeSymbol(function.name) << '(';
                for (size_t index = 0; index < function.paramTypes.size(); ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    output << serializeType(function.paramTypes[index], program);
                }
                output << ')';
                if (function.returnType.has_value()) {
                    output << ": "
                           << serializeType(*function.returnType, program);
                }
                output << "\n\n";
            } else if constexpr (std::same_as<Item, FunctionDef>) {
                const auto& function = program[itemRef];
                output << "fun " << serializeSymbol(function.name) << '(';
                for (size_t index = 0; index < function.params.size(); ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    const auto& param = program[function.params[index]];
                    output << serializeSymbol(param.symbol) << ": "
                           << serializeType(param.type, program);
                }
                output << ')';
                if (function.returnType.has_value()) {
                    output << ": "
                           << serializeType(*function.returnType, program);
                }
                output << " {\n";
                for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex) {
                    serializeBlock(output, program[function.blocks[blockIndex]], program);
                    if (blockIndex + 1 != function.blocks.size()) {
                        output << '\n';
                    }
                }
                output << "}\n\n";
            }
        },
        item);
}

} // namespace

void Program::clear()
{
    sourcePos = SourcePos {};
    annotations.clear();
    items.clear();
    m_nodes.clear();
}

std::string_view toString(BinaryOp op)
{
    switch (op) {
    case BinaryOp::ne:
        return "ne";
    case BinaryOp::eq:
        return "eq";
    case BinaryOp::gt:
        return "gt";
    case BinaryOp::lt:
        return "lt";
    case BinaryOp::ge:
        return "ge";
    case BinaryOp::le:
        return "le";
    case BinaryOp::add:
        return "add";
    case BinaryOp::sub:
        return "sub";
    case BinaryOp::mul:
        return "mul";
    case BinaryOp::div:
        return "div";
    case BinaryOp::mod:
        return "mod";
    case BinaryOp::bitAnd:
        return "and";
    case BinaryOp::bitOr:
        return "or";
    case BinaryOp::bitXor:
        return "xor";
    case BinaryOp::shl:
        return "shl";
    case BinaryOp::shr:
        return "shr";
    case BinaryOp::sar:
        return "sar";
    }

    throw std::runtime_error("unknown BinaryOp");
}

bool hasReturnType(const FunctionType& type)
{
    return type.returnType.has_value();
}

bool hasReturnValue(const ReturnTerminator& terminator)
{
    return terminator.value.has_value();
}

bool usesSsaExtension(const BranchTerminator& terminator)
{
    return !terminator.trueArgs.empty() || !terminator.falseArgs.empty();
}

bool usesSsaExtension(const JumpTerminator& terminator)
{
    return !terminator.args.empty();
}

bool usesSsaExtension(const BasicBlock& block, const Program& program)
{
    if (!block.params.empty()) {
        return true;
    }

    return MATCH(block.terminator) WITH(
        [&](Ref<BranchTerminator> terminatorRef) {
            return usesSsaExtension(program[terminatorRef]);
        },
        [&](Ref<JumpTerminator> terminatorRef) {
            return usesSsaExtension(program[terminatorRef]);
        },
        [&](Ref<ReturnTerminator>) { return false; });
}

std::string serializeToKoopa(const Program& program)
{
    std::ostringstream output;
    for (const auto& item : program.items) {
        serializeTopLevelItem(output, item, program);
    }
    return output.str();
}

} // namespace yesod::koopa::ir