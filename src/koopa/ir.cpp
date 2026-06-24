#include "koopa/ir.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace yesod::koopa::ir {

namespace {

    std::string serializeType(const Type& type, const Program& program);
    std::string serializeValue(const Value& value);
    std::string serializeInitializer(
        const Initializer& initializer, const Program& program);
    std::string serializeCombineTerm(const CombineTerm& term);
    std::string serializePointwiseNode(
        Ref<PointwiseNode> nodeRef, const Program& program);

    std::string serializeSymbol(const Symbol& symbol)
    {
        return symbol.spelling;
    }

    std::string serializeType(const Type& type, const Program& program)
    {
        return MATCH(type) WITH(
            [&](const I32Type&) { return std::string("i32"); },
            [&](const MintType&) { return std::string("mint"); },
            [&](const PolyType&) { return std::string("poly"); },
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
                for (size_t index = 0; index < functionType.paramTypes.size();
                    ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    output << serializeType(
                        functionType.paramTypes[index], program);
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
        return MATCH(value)
            WITH([&](const Symbol& symbol) { return serializeSymbol(symbol); },
                [&](const IntegerLiteral& literal) {
                    return std::to_string(literal.value);
                },
                [&](const UndefValue&) { return std::string("undef"); });
    }

    std::string serializeCombineTerm(const CombineTerm& term)
    {
        std::ostringstream output;
        output << '(' << serializeValue(term.value) << ", "
               << serializeValue(term.start) << ", ";
        if (term.end.has_value()) {
            output << serializeValue(*term.end);
        } else {
            output << "inf";
        }
        output << ", " << serializeValue(term.shift) << ", "
               << serializeValue(term.scale) << ')';
        return output.str();
    }

    std::string serializePointwiseNode(
        Ref<PointwiseNode> nodeRef, const Program& program)
    {
        const auto& node = program[nodeRef];
        return MATCH(node.kind) WITH(
            [&](const PointwiseLeaf& leaf) {
                return serializeValue(leaf.value);
            },
            [&](const PointwiseBinary& binary) {
                std::ostringstream output;
                output << toString(binary.op) << '('
                       << serializePointwiseNode(binary.lhs, program) << ", "
                       << serializePointwiseNode(binary.rhs, program) << ')';
                return output.str();
            });
    }

    std::string serializeInitializer(
        const Initializer& initializer, const Program& program)
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
                for (size_t index = 0; index < aggregate.elements.size();
                    ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    output << serializeInitializer(
                        aggregate.elements[index], program);
                }
                output << '}';
                return output.str();
            });
    }

    std::string serializeStoreValue(
        const StoreValue& value, const Program& program)
    {
        return MATCH(value)
            WITH([&](const Symbol& symbol) { return serializeSymbol(symbol); },
                [&](const IntegerLiteral& literal) {
                    return std::to_string(literal.value);
                },
                [&](const UndefValue&) { return std::string("undef"); },
                [&](const ZeroInit&) { return std::string("zeroinit"); },
                [&](Ref<AggregateInitializer> aggregateRef) {
                    return serializeInitializer(
                        Initializer { aggregateRef }, program);
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
                    output << "  " << serializeSymbol(symbolDef.symbol)
                           << " = ";
                    std::visit(
                        [&](auto rhsRef) {
                            using Rhs = std::remove_cvref_t<
                                decltype(program[rhsRef])>;
                            if constexpr (std::same_as<Rhs,
                                              MemoryDeclaration>) {
                                output
                                    << "alloc "
                                    << serializeType(
                                           program[rhsRef].allocType, program);
                            } else if constexpr (std::same_as<Rhs, LoadExpr>) {
                                output
                                    << "load "
                                    << serializeSymbol(program[rhsRef].source);
                            } else if constexpr (std::same_as<Rhs,
                                                     GetPointerExpr>) {
                                const auto& expr = program[rhsRef];
                                output << "getptr "
                                       << serializeSymbol(expr.source) << ", "
                                       << serializeValue(expr.index);
                            } else if constexpr (std::same_as<Rhs,
                                                     GetElementPointerExpr>) {
                                const auto& expr = program[rhsRef];
                                output << "getelemptr "
                                       << serializeSymbol(expr.source) << ", "
                                       << serializeValue(expr.index);
                            } else if constexpr (std::same_as<Rhs,
                                                     BinaryExpr>) {
                                const auto& expr = program[rhsRef];
                                output << toString(expr.op) << ' '
                                       << serializeValue(expr.lhs) << ", "
                                       << serializeValue(expr.rhs);
                            } else if constexpr (std::same_as<Rhs, CallExpr>) {
                                const auto& expr = program[rhsRef];
                                output << "call "
                                       << serializeSymbol(expr.callee) << "(";
                                for (size_t index = 0; index < expr.args.size();
                                    ++index) {
                                    if (index != 0) {
                                        output << ", ";
                                    }
                                    output << serializeValue(expr.args[index]);
                                }
                                output << ')';
                            } else if constexpr (std::same_as<Rhs, CopyExpr>) {
                                const auto& expr = program[rhsRef];
                                output << serializeValue(expr.value);
                            } else if constexpr (std::same_as<Rhs,
                                                     PointwiseExpr>) {
                                const auto& expr = program[rhsRef];
                                output << "pointwise "
                                       << serializePointwiseNode(
                                              expr.root, program);
                            } else if constexpr (std::same_as<Rhs,
                                                     CombineExpr>) {
                                const auto& expr = program[rhsRef];
                                output << "combine";
                                if (!expr.terms.empty()) {
                                    output << ' ';
                                }
                                for (size_t index = 0;
                                    index < expr.terms.size(); ++index) {
                                    if (index != 0) {
                                        output << ", ";
                                    }
                                    output << serializeCombineTerm(
                                        expr.terms[index]);
                                }
                            } else if constexpr (std::same_as<Rhs,
                                                     GetCoeffExpr>) {
                                const auto& expr = program[rhsRef];
                                output << "getcoeff "
                                       << serializeValue(expr.value) << ", "
                                       << serializeValue(expr.index);
                            } else if constexpr (std::same_as<Rhs,
                                                     PolyConstructExpr>) {
                                const auto& expr = program[rhsRef];
                                output << "poly_construct [";
                                for (size_t index = 0;
                                    index < expr.elements.size(); ++index) {
                                    if (index != 0) {
                                        output << ", ";
                                    }
                                    output
                                        << serializeValue(expr.elements[index]);
                                }
                                output << ']';
                            } else if constexpr (std::same_as<Rhs,
                                                     ConversionExpr>) {
                                const auto& expr = program[rhsRef];
                                output << toString(expr.op) << ' '
                                       << serializeValue(expr.value);
                            }
                        },
                        symbolDef.rhs);
                    output << '\n';
                } else if constexpr (std::same_as<StatementNode, StoreStmt>) {
                    const auto& storeStmt = program[statementRef];
                    output << "  store "
                           << serializeStoreValue(storeStmt.value, program)
                           << ", " << serializeSymbol(storeStmt.destination)
                           << '\n';
                } else if constexpr (std::same_as<StatementNode, CallExpr>) {
                    const auto& callExpr = program[statementRef];
                    output << "  call " << serializeSymbol(callExpr.callee)
                           << "(";
                    for (size_t index = 0; index < callExpr.args.size();
                        ++index) {
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
                    output << "  br " << serializeValue(branch.condition)
                           << ", " << serializeSymbol(branch.trueTarget);
                    if (!branch.trueArgs.empty()) {
                        output << '(';
                        for (size_t index = 0; index < branch.trueArgs.size();
                            ++index) {
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
                        for (size_t index = 0; index < branch.falseArgs.size();
                            ++index) {
                            if (index != 0) {
                                output << ", ";
                            }
                            output << serializeValue(branch.falseArgs[index]);
                        }
                        output << ')';
                    }
                    output << '\n';
                } else if constexpr (std::same_as<TerminatorNode,
                                         JumpTerminator>) {
                    const auto& jump = program[terminatorRef];
                    output << "  jump " << serializeSymbol(jump.target);
                    if (!jump.args.empty()) {
                        output << '(';
                        for (size_t index = 0; index < jump.args.size();
                            ++index) {
                            if (index != 0) {
                                output << ", ";
                            }
                            output << serializeValue(jump.args[index]);
                        }
                        output << ')';
                    }
                    output << '\n';
                } else if constexpr (std::same_as<TerminatorNode,
                                         ReturnTerminator>) {
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

    void serializeBlock(
        std::ostream& output, const BasicBlock& block, const Program& program)
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

    void serializeTopLevelItem(
        std::ostream& output, const TopLevelItem& item, const Program& program)
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
                    for (size_t index = 0; index < function.paramTypes.size();
                        ++index) {
                        if (index != 0) {
                            output << ", ";
                        }
                        output << serializeType(
                            function.paramTypes[index], program);
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
                    for (size_t index = 0; index < function.params.size();
                        ++index) {
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
                    for (size_t blockIndex = 0;
                        blockIndex < function.blocks.size(); ++blockIndex) {
                        serializeBlock(output,
                            program[function.blocks[blockIndex]], program);
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
    sourcePos = SourcePos { };
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

std::string_view toString(PvBinaryOp op)
{
    switch (op) {
    case PvBinaryOp::add:
        return "add";
    case PvBinaryOp::sub:
        return "sub";
    case PvBinaryOp::mul:
        return "mul";
    case PvBinaryOp::times:
        return "times";
    }
    throw std::runtime_error("unknown pointwise binary operation");
}

std::string_view toString(ConversionOp op)
{
    switch (op) {
    case ConversionOp::int2mint:
        return "int2mint";
    case ConversionOp::mint2int:
        return "mint2int";
    }
    throw std::runtime_error("unknown conversion operation");
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

namespace {

    [[nodiscard]] std::optional<std::string> symbolSpelling(const Value& value)
    {
        if (const auto* symbol = std::get_if<Symbol>(&value)) {
            return symbol->spelling;
        }
        return std::nullopt;
    }

    template <typename Visit> void visitValue(Value& value, Visit&& visit)
    {
        visit(value);
    }

    template <typename Visit> void visitValue(const Value& value, Visit&& visit)
    {
        visit(value);
    }

    template <typename Visit>
    void visitStoreValue(
        const StoreValue& value, const Program& program, Visit&& visit)
    {
        MATCH(value)
        WITH([&](const Symbol& symbol) { visit(Value { symbol }); },
            [&](const IntegerLiteral&) { }, [&](const UndefValue&) { },
            [&](const ZeroInit&) { },
            [&](Ref<AggregateInitializer> aggregateRef) {
                const auto& aggregate = program[aggregateRef];
                for (const auto& element : aggregate.elements) {
                    MATCH(element)
                    WITH([&](const IntegerLiteral&) { },
                        [&](const UndefValue&) { }, [&](const ZeroInit&) { },
                        [&](Ref<AggregateInitializer> nestedRef) {
                            visitStoreValue(
                                StoreValue { nestedRef }, program, visit);
                        });
                }
            });
    }

    template <typename Visit>
    void visitPointwiseNodeValues(
        Program& program, Ref<PointwiseNode> nodeRef, Visit&& visit)
    {
        auto& node = program[nodeRef];
        MATCH(node.kind)
        WITH([&](PointwiseLeaf& leaf) { visitValue(leaf.value, visit); },
            [&](PointwiseBinary& binary) {
                visitPointwiseNodeValues(program, binary.lhs, visit);
                visitPointwiseNodeValues(program, binary.rhs, visit);
            });
    }

    template <typename Visit>
    void visitPointwiseNodeValues(
        const Program& program, Ref<PointwiseNode> nodeRef, Visit&& visit)
    {
        const auto& node = program[nodeRef];
        MATCH(node.kind)
        WITH([&](const PointwiseLeaf& leaf) { visitValue(leaf.value, visit); },
            [&](const PointwiseBinary& binary) {
                visitPointwiseNodeValues(program, binary.lhs, visit);
                visitPointwiseNodeValues(program, binary.rhs, visit);
            });
    }

    template <typename Visit>
    void visitRhsValues(Program& program, SymbolRhs& rhs, Visit&& visit)
    {
        MATCH(rhs)
        WITH([&](Ref<MemoryDeclaration>) { }, [&](Ref<LoadExpr>) { },
            [&](Ref<GetPointerExpr> ref) {
                visitValue(program[ref].index, visit);
            },
            [&](Ref<GetElementPointerExpr> ref) {
                visitValue(program[ref].index, visit);
            },
            [&](Ref<BinaryExpr> ref) {
                auto& expr = program[ref];
                visitValue(expr.lhs, visit);
                visitValue(expr.rhs, visit);
            },
            [&](Ref<CallExpr> ref) {
                for (auto& arg : program[ref].args) {
                    visitValue(arg, visit);
                }
            },
            [&](Ref<CopyExpr> ref) { visitValue(program[ref].value, visit); },
            [&](Ref<PointwiseExpr> ref) {
                visitPointwiseNodeValues(program, program[ref].root, visit);
            },
            [&](Ref<CombineExpr> ref) {
                for (auto& term : program[ref].terms) {
                    visitValue(term.value, visit);
                    visitValue(term.start, visit);
                    if (term.end.has_value()) {
                        visitValue(*term.end, visit);
                    }
                    visitValue(term.shift, visit);
                    visitValue(term.scale, visit);
                }
            },
            [&](Ref<GetCoeffExpr> ref) {
                auto& expr = program[ref];
                visitValue(expr.value, visit);
                visitValue(expr.index, visit);
            },
            [&](Ref<PolyConstructExpr> ref) {
                for (auto& element : program[ref].elements) {
                    visitValue(element, visit);
                }
            },
            [&](Ref<ConversionExpr> ref) {
                visitValue(program[ref].value, visit);
            });
    }

    template <typename Visit>
    void visitRhsValues(
        const Program& program, const SymbolRhs& rhs, Visit&& visit)
    {
        MATCH(rhs)
        WITH([&](Ref<MemoryDeclaration>) { }, [&](Ref<LoadExpr>) { },
            [&](Ref<GetPointerExpr> ref) { visit(program[ref].index); },
            [&](Ref<GetElementPointerExpr> ref) { visit(program[ref].index); },
            [&](Ref<BinaryExpr> ref) {
                const auto& expr = program[ref];
                visit(expr.lhs);
                visit(expr.rhs);
            },
            [&](Ref<CallExpr> ref) {
                for (const auto& arg : program[ref].args) {
                    visit(arg);
                }
            },
            [&](Ref<CopyExpr> ref) { visit(program[ref].value); },
            [&](Ref<PointwiseExpr> ref) {
                visitPointwiseNodeValues(program, program[ref].root, visit);
            },
            [&](Ref<CombineExpr> ref) {
                for (const auto& term : program[ref].terms) {
                    visit(term.value);
                    visit(term.start);
                    if (term.end.has_value()) {
                        visit(*term.end);
                    }
                    visit(term.shift);
                    visit(term.scale);
                }
            },
            [&](Ref<GetCoeffExpr> ref) {
                const auto& expr = program[ref];
                visit(expr.value);
                visit(expr.index);
            },
            [&](Ref<PolyConstructExpr> ref) {
                for (const auto& element : program[ref].elements) {
                    visit(element);
                }
            },
            [&](Ref<ConversionExpr> ref) { visit(program[ref].value); });
    }

    [[nodiscard]] bool isIdentityTerm(const CombineTerm& term)
    {
        const auto* start = std::get_if<IntegerLiteral>(&term.start);
        const auto* shift = std::get_if<IntegerLiteral>(&term.shift);
        const auto* scale = std::get_if<IntegerLiteral>(&term.scale);
        return start != nullptr && start->value == 0 && !term.end.has_value()
            && shift != nullptr && shift->value == 0 && scale != nullptr
            && scale->value == 1;
    }

    [[nodiscard]] bool rhsIsRemovable(const SymbolRhs& rhs)
    {
        return MATCH(rhs)
            WITH([](Ref<MemoryDeclaration>) -> bool { return false; },
                [](Ref<LoadExpr>) -> bool { return false; },
                [](Ref<GetPointerExpr>) -> bool { return false; },
                [](Ref<GetElementPointerExpr>) -> bool { return false; },
                [](Ref<CallExpr>) -> bool { return false; },
                [](const auto&) -> bool {
                    // Arithmetic and poly pseudo-instructions are pure SSA
                    // values.
                    return true;
                });
    }

    enum class ValidationType {
        integer,
        mint,
        poly,
        pointer,
        other,
        unknown,
    };

    ValidationType validationTypeOf(const Type& type)
    {
        return MATCH(type) WITH(
            [](const I32Type&) -> ValidationType {
                return ValidationType::integer;
            },
            [](const MintType&) -> ValidationType {
                return ValidationType::mint;
            },
            [](const PolyType&) -> ValidationType {
                return ValidationType::poly;
            },
            [](Ref<PointerType>) -> ValidationType {
                return ValidationType::pointer;
            },
            [](const auto&) -> ValidationType {
                return ValidationType::other;
            });
    }

    ValidationType validationTypeOfValue(const Value& value,
        const std::unordered_map<std::string, ValidationType>& valueTypes)
    {
        return MATCH(value) WITH(
            [&](const Symbol& symbol) -> ValidationType {
                const auto it = valueTypes.find(symbol.spelling);
                return it == valueTypes.end() ? ValidationType::unknown
                                              : it->second;
            },
            [](const IntegerLiteral&) -> ValidationType {
                return ValidationType::integer;
            },
            [](const UndefValue&) -> ValidationType {
                return ValidationType::unknown;
            });
    }

    bool valueMatchesType(const Value& value, ValidationType expected,
        const std::unordered_map<std::string, ValidationType>& valueTypes)
    {
        if (std::holds_alternative<IntegerLiteral>(value)) {
            return expected == ValidationType::integer
                || expected == ValidationType::mint;
        }
        const auto actual = validationTypeOfValue(value, valueTypes);
        return actual == expected || actual == ValidationType::unknown;
    }

    void requireValueType(const Value& value, ValidationType expected,
        const std::unordered_map<std::string, ValidationType>& valueTypes,
        const char* message, int32_t offset = -1)
    {
        if (!valueMatchesType(value, expected, valueTypes)) {
            std::string fullMessage;
            if (offset >= 0) {
                fullMessage
                    = "at offset " + std::to_string(offset) + ": " + message;
            } else {
                fullMessage = message;
            }
            throw std::runtime_error(fullMessage);
        }
    }

    [[nodiscard]] int32_t sourceOffset(const Value& value)
    {
        return MATCH(value) WITH(
            [](const Symbol& sym) { return sym.sourcePos.m_offset; },
            [](const IntegerLiteral& lit) { return lit.sourcePos.m_offset; },
            [](const UndefValue& undef) { return undef.sourcePos.m_offset; });
    }

} // namespace

void simplifyLocalValues(Program& program, BasicBlock& block,
    size_t firstStatementIndex, const std::vector<Value>& liveValues,
    bool eliminateDeadValues)
{
    struct DefInfo {
        Ref<SymbolDef> def;
        size_t index = 0;
    };

    enum class UseSiteKind {
        value,
        combineTermValue,
    };

    struct UseSite {
        UseSiteKind kind = UseSiteKind::value;
        size_t useIndex = 0;
        Value* value = nullptr;
        CombineTerm* term = nullptr;
        Ptr<SymbolDef> userDef;
    };

    struct UseIndex {
        std::unordered_map<std::string, size_t> counts;
        std::unordered_map<std::string, std::vector<UseSite>> sites;
    };

    auto collectDefs = [&]() -> std::unordered_map<std::string, DefInfo> {
        std::unordered_map<std::string, DefInfo> defs;
        for (size_t index = 0; index < block.statements.size(); ++index) {
            const auto& statement = block.statements[index];
            if (const auto* defRef = std::get_if<Ref<SymbolDef>>(&statement)) {
                defs.insert_or_assign(program[*defRef].symbol.spelling,
                    DefInfo {
                        .def = *defRef,
                        .index = index,
                    });
            }
        }
        return defs;
    };

    auto buildUseIndex = [&]() -> UseIndex {
        UseIndex index;
        auto countValue = [&](const Value& value) -> void {
            if (const auto spelling = symbolSpelling(value);
                spelling.has_value()) {
                ++index.counts[*spelling];
            }
        };
        auto recordValueSite
            = [&](Value& value, size_t useIndex, UseSiteKind kind,
                  CombineTerm* term, Ptr<SymbolDef> userDef) -> void {
            const auto spelling = symbolSpelling(value);
            if (!spelling.has_value()) {
                return;
            }
            ++index.counts[*spelling];
            index.sites[*spelling].push_back(UseSite {
                .kind = kind,
                .useIndex = useIndex,
                .value = &value,
                .term = term,
                .userDef = userDef,
            });
        };
        auto recordRhsSites = [&](SymbolRhs& rhs, size_t useIndex,
                                  Ptr<SymbolDef> userDef) -> void {
            MATCH(rhs)
            WITH([&](Ref<MemoryDeclaration>) -> void { },
                [&](Ref<LoadExpr>) -> void { },
                [&](Ref<GetPointerExpr> ref) -> void {
                    recordValueSite(program[ref].index, useIndex,
                        UseSiteKind::value, nullptr, userDef);
                },
                [&](Ref<GetElementPointerExpr> ref) -> void {
                    recordValueSite(program[ref].index, useIndex,
                        UseSiteKind::value, nullptr, userDef);
                },
                [&](Ref<BinaryExpr> ref) -> void {
                    auto& expr = program[ref];
                    recordValueSite(expr.lhs, useIndex, UseSiteKind::value,
                        nullptr, userDef);
                    recordValueSite(expr.rhs, useIndex, UseSiteKind::value,
                        nullptr, userDef);
                },
                [&](Ref<CallExpr> ref) -> void {
                    for (auto& arg : program[ref].args) {
                        recordValueSite(arg, useIndex, UseSiteKind::value,
                            nullptr, userDef);
                    }
                },
                [&](Ref<CopyExpr> ref) -> void {
                    recordValueSite(program[ref].value, useIndex,
                        UseSiteKind::value, nullptr, userDef);
                },
                [&](Ref<PointwiseExpr> ref) -> void {
                    visitPointwiseNodeValues(
                        program, program[ref].root, [&](Value& value) -> void {
                            recordValueSite(value, useIndex, UseSiteKind::value,
                                nullptr, userDef);
                        });
                },
                [&](Ref<CombineExpr> ref) -> void {
                    for (auto& term : program[ref].terms) {
                        recordValueSite(term.value, useIndex,
                            UseSiteKind::combineTermValue, &term, userDef);
                        recordValueSite(term.start, useIndex,
                            UseSiteKind::value, nullptr, userDef);
                        if (term.end.has_value()) {
                            recordValueSite(*term.end, useIndex,
                                UseSiteKind::value, nullptr, userDef);
                        }
                        recordValueSite(term.shift, useIndex,
                            UseSiteKind::value, nullptr, userDef);
                        recordValueSite(term.scale, useIndex,
                            UseSiteKind::value, nullptr, userDef);
                    }
                },
                [&](Ref<GetCoeffExpr> ref) -> void {
                    auto& expr = program[ref];
                    recordValueSite(expr.value, useIndex, UseSiteKind::value,
                        nullptr, userDef);
                    recordValueSite(expr.index, useIndex, UseSiteKind::value,
                        nullptr, userDef);
                },
                [&](Ref<PolyConstructExpr> ref) -> void {
                    for (auto& element : program[ref].elements) {
                        recordValueSite(element, useIndex, UseSiteKind::value,
                            nullptr, userDef);
                    }
                },
                [&](Ref<ConversionExpr> ref) -> void {
                    recordValueSite(program[ref].value, useIndex,
                        UseSiteKind::value, nullptr, userDef);
                });
        };
        for (const auto& value : liveValues) {
            countValue(value);
        }
        for (size_t statementIndex = 0;
            statementIndex < block.statements.size(); ++statementIndex) {
            auto& statement = block.statements[statementIndex];
            MATCH(statement)
            WITH(
                [&](Ref<SymbolDef> defRef) -> void {
                    if (statementIndex >= firstStatementIndex) {
                        recordRhsSites(
                            program[defRef].rhs, statementIndex, defRef.ptr());
                    } else {
                        visitRhsValues(
                            program, program[defRef].rhs, countValue);
                    }
                },
                [&](Ref<StoreStmt> storeRef) -> void {
                    visitStoreValue(
                        program[storeRef].value, program, countValue);
                    countValue(program[storeRef].destination);
                },
                [&](Ref<CallExpr> callRef) -> void {
                    for (auto& arg : program[callRef].args) {
                        if (statementIndex >= firstStatementIndex) {
                            recordValueSite(arg, statementIndex,
                                UseSiteKind::value, nullptr, nullptr);
                        } else {
                            countValue(arg);
                        }
                    }
                });
        }
        const size_t terminatorUseIndex = block.statements.size();
        auto recordTerminatorValue = [&](Value& value) -> void {
            recordValueSite(value, terminatorUseIndex, UseSiteKind::value,
                nullptr, nullptr);
        };
        MATCH(block.terminator)
        WITH(
            [&](Ref<BranchTerminator> terminatorRef) -> void {
                auto& terminator = program[terminatorRef];
                recordTerminatorValue(terminator.condition);
                for (auto& arg : terminator.trueArgs) {
                    recordTerminatorValue(arg);
                }
                for (auto& arg : terminator.falseArgs) {
                    recordTerminatorValue(arg);
                }
            },
            [&](Ref<JumpTerminator> terminatorRef) -> void {
                for (auto& arg : program[terminatorRef].args) {
                    recordTerminatorValue(arg);
                }
            },
            [&](Ref<ReturnTerminator> terminatorRef) -> void {
                auto& terminator = program[terminatorRef];
                if (terminator.value.has_value()) {
                    recordTerminatorValue(*terminator.value);
                }
            });
        return index;
    };

    auto localDepsAvailable
        = [&](const SymbolRhs& rhs, size_t useIndex,
              const std::unordered_map<std::string, DefInfo>& defs) -> bool {
        bool available = true;
        visitRhsValues(program, rhs, [&](const Value& value) -> void {
            const auto spelling = symbolSpelling(value);
            if (!spelling.has_value()) {
                return;
            }
            const auto defIt = defs.find(*spelling);
            if (defIt != defs.end() && defIt->second.index >= useIndex) {
                available = false;
            }
        });
        return available;
    };

    auto countValueUse = [](std::unordered_map<std::string, size_t>& counts,
                             const Value& value) -> void {
        if (const auto spelling = symbolSpelling(value); spelling.has_value()) {
            ++counts[*spelling];
        }
    };

    auto countCombineTermUses
        = [&](std::unordered_map<std::string, size_t>& counts,
              const CombineTerm& term) -> void {
        countValueUse(counts, term.value);
        countValueUse(counts, term.start);
        if (term.end.has_value()) {
            countValueUse(counts, *term.end);
        }
        countValueUse(counts, term.shift);
        countValueUse(counts, term.scale);
    };

    auto siteUsesSymbol
        = [](const UseSite& site, const std::string& spelling) -> bool {
        if (site.value == nullptr) {
            return false;
        }
        const auto currentSpelling = symbolSpelling(*site.value);
        return currentSpelling.has_value() && *currentSpelling == spelling;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        auto defs = collectDefs();
        auto useIndex = buildUseIndex();

        for (size_t defIndex = firstStatementIndex;
            defIndex < block.statements.size(); ++defIndex) {
            const auto* defRef
                = std::get_if<Ref<SymbolDef>>(&block.statements[defIndex]);
            if (defRef == nullptr) {
                continue;
            }
            auto& def = program[*defRef];
            const auto* copyRef = std::get_if<Ref<CopyExpr>>(&def.rhs);
            const auto* combineRef = std::get_if<Ref<CombineExpr>>(&def.rhs);
            if (copyRef == nullptr && combineRef == nullptr) {
                continue;
            }

            const auto replacement
                = copyRef == nullptr ? Value { } : program[*copyRef].value;
            auto siteIt = useIndex.sites.find(def.symbol.spelling);
            if (siteIt == useIndex.sites.end()) {
                continue;
            }

            if (copyRef != nullptr) {
                bool replacedAny = false;
                for (auto& site : siteIt->second) {
                    if (!siteUsesSymbol(site, def.symbol.spelling)) {
                        continue;
                    }
                    if (site.useIndex == defIndex) {
                        continue;
                    }
                    if (!localDepsAvailable(def.rhs, site.useIndex, defs)) {
                        continue;
                    }
                    *site.value = replacement;
                    --useIndex.counts[def.symbol.spelling];
                    countValueUse(useIndex.counts, replacement);
                    replacedAny = true;
                }
                changed = changed || replacedAny;
                continue;
            }

            const auto countIt = useIndex.counts.find(def.symbol.spelling);
            if (countIt == useIndex.counts.end() || countIt->second != 1) {
                continue;
            }
            UseSite* singleSite = nullptr;
            size_t activeSiteCount = 0;
            for (auto& site : siteIt->second) {
                if (siteUsesSymbol(site, def.symbol.spelling)) {
                    singleSite = &site;
                    ++activeSiteCount;
                }
            }
            if (activeSiteCount != 1 || singleSite == nullptr) {
                continue;
            }
            if (singleSite->useIndex == defIndex) {
                continue;
            }

            bool replaced = false;
            if (singleSite->kind != UseSiteKind::combineTermValue
                || singleSite->term == nullptr
                || singleSite->userDef == nullptr) {
                continue;
            }
            auto& useDef = program[singleSite->userDef.ref()];
            auto* useCombineRef = std::get_if<Ref<CombineExpr>>(&useDef.rhs);
            if (useCombineRef == nullptr) {
                continue;
            }
            const auto& sourceExpr = program[*combineRef];
            if (sourceExpr.terms.size() != 1) {
                continue;
            }
            if (!localDepsAvailable(def.rhs, singleSite->useIndex, defs)) {
                continue;
            }
            if (!isIdentityTerm(*singleSite->term)) {
                continue;
            }
            *singleSite->term = sourceExpr.terms.front();
            --useIndex.counts[def.symbol.spelling];
            countCombineTermUses(useIndex.counts, sourceExpr.terms.front());
            replaced = true;

            changed = changed || replaced;
        }
    }

    if (!eliminateDeadValues) {
        return;
    }

    changed = true;
    while (changed) {
        changed = false;
        const auto useCounts = buildUseIndex().counts;
        std::erase_if(block.statements, [&](const Statement& statement) {
            const auto* defRef = std::get_if<Ref<SymbolDef>>(&statement);
            if (defRef == nullptr) {
                return false;
            }
            const auto index
                = static_cast<size_t>(&statement - block.statements.data());
            if (index < firstStatementIndex) {
                return false;
            }
            const auto& def = program[*defRef];
            if (!rhsIsRemovable(def.rhs)) {
                return false;
            }
            if (useCounts.contains(def.symbol.spelling)) {
                return false;
            }
            changed = true;
            return true;
        });
    }
}

void eliminateDeadValues(Program& program, FunctionDef& function)
{
    bool changed = true;
    while (changed) {
        changed = false;
        std::unordered_map<std::string, size_t> useCounts;
        auto countValue = [&](const Value& value) {
            if (const auto spelling = symbolSpelling(value);
                spelling.has_value()) {
                ++useCounts[*spelling];
            }
        };
        for (const auto paramRef : function.params) {
            countValue(program[paramRef].symbol);
        }
        for (const auto blockRef : function.blocks) {
            const auto& block = program[blockRef];
            for (const auto& statement : block.statements) {
                MATCH(statement)
                WITH(
                    [&](Ref<SymbolDef> defRef) {
                        visitRhsValues(
                            program, program[defRef].rhs, countValue);
                    },
                    [&](Ref<StoreStmt> storeRef) {
                        visitStoreValue(
                            program[storeRef].value, program, countValue);
                        countValue(program[storeRef].destination);
                    },
                    [&](Ref<CallExpr> callRef) {
                        for (const auto& arg : program[callRef].args) {
                            countValue(arg);
                        }
                    });
            }
            MATCH(block.terminator)
            WITH(
                [&](Ref<BranchTerminator> terminatorRef) {
                    const auto& terminator = program[terminatorRef];
                    countValue(terminator.condition);
                    for (const auto& arg : terminator.trueArgs) {
                        countValue(arg);
                    }
                    for (const auto& arg : terminator.falseArgs) {
                        countValue(arg);
                    }
                },
                [&](Ref<JumpTerminator> terminatorRef) {
                    for (const auto& arg : program[terminatorRef].args) {
                        countValue(arg);
                    }
                },
                [&](Ref<ReturnTerminator> terminatorRef) {
                    const auto& terminator = program[terminatorRef];
                    if (terminator.value.has_value()) {
                        countValue(*terminator.value);
                    }
                });
        }

        for (const auto blockRef : function.blocks) {
            auto& block = program[blockRef];
            const size_t oldSize = block.statements.size();
            std::erase_if(block.statements, [&](const Statement& statement) {
                const auto* defRef = std::get_if<Ref<SymbolDef>>(&statement);
                if (defRef == nullptr) {
                    return false;
                }
                const auto& def = program[*defRef];
                return rhsIsRemovable(def.rhs)
                    && !useCounts.contains(def.symbol.spelling);
            });
            changed = changed || oldSize != block.statements.size();
        }

        std::unordered_map<std::string, std::vector<size_t>>
            removedParamIndexes;
        for (const auto blockRef : function.blocks) {
            auto& block = program[blockRef];
            std::vector<size_t> removeIndexes;
            for (size_t index = 0; index < block.params.size(); ++index) {
                const auto& param = program[block.params[index]];
                if (!useCounts.contains(param.symbol.spelling)) {
                    removeIndexes.push_back(index);
                }
            }
            if (removeIndexes.empty()) {
                continue;
            }
            removedParamIndexes[block.label.spelling] = removeIndexes;
            std::erase_if(block.params, [&](Ref<BlockParameter> paramRef) {
                return !useCounts.contains(program[paramRef].symbol.spelling);
            });
            changed = true;
        }

        if (removedParamIndexes.empty()) {
            continue;
        }
        auto eraseArgs = [&](const Symbol& target, std::vector<Value>& args) {
            const auto removedIt = removedParamIndexes.find(target.spelling);
            if (removedIt == removedParamIndexes.end()) {
                return;
            }
            const auto& indexes = removedIt->second;
            for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) {
                if (*it < args.size()) {
                    args.erase(args.begin() + static_cast<std::ptrdiff_t>(*it));
                }
            }
        };
        for (const auto blockRef : function.blocks) {
            auto& block = program[blockRef];
            MATCH(block.terminator)
            WITH(
                [&](Ref<BranchTerminator> terminatorRef) {
                    auto& terminator = program[terminatorRef];
                    eraseArgs(terminator.trueTarget, terminator.trueArgs);
                    eraseArgs(terminator.falseTarget, terminator.falseArgs);
                },
                [&](Ref<JumpTerminator> terminatorRef) {
                    auto& terminator = program[terminatorRef];
                    eraseArgs(terminator.target, terminator.args);
                },
                [&](Ref<ReturnTerminator>) { });
        }
    }
}

void eliminateEmptyBasicBlocks(Program& program, FunctionDef& function)
{
    if (function.blocks.empty()) {
        return;
    }

    const Ref<BasicBlock> entryBlockRef = function.blocks.front();
    auto buildBlockByLabel
        = [&]() -> std::unordered_map<std::string, Ref<BasicBlock>> {
        std::unordered_map<std::string, Ref<BasicBlock>> blockByLabel;
        blockByLabel.reserve(function.blocks.size());
        for (const auto blockRef : function.blocks) {
            blockByLabel.insert_or_assign(
                program[blockRef].label.spelling, blockRef);
        }
        return blockByLabel;
    };

    const auto blockByLabel = buildBlockByLabel();

    std::unordered_map<std::string, size_t> valueUseCounts;
    auto countValue = [&](const Value& value) -> void {
        if (const auto spelling = symbolSpelling(value); spelling.has_value()) {
            ++valueUseCounts[*spelling];
        }
    };
    for (const auto blockRef : function.blocks) {
        const auto& block = program[blockRef];
        for (const auto& statement : block.statements) {
            MATCH(statement)
            WITH(
                [&](Ref<SymbolDef> defRef) -> void {
                    visitRhsValues(program, program[defRef].rhs, countValue);
                },
                [&](Ref<StoreStmt> storeRef) -> void {
                    visitStoreValue(
                        program[storeRef].value, program, countValue);
                    countValue(program[storeRef].destination);
                },
                [&](Ref<CallExpr> callRef) -> void {
                    for (const auto& arg : program[callRef].args) {
                        countValue(arg);
                    }
                });
        }
        MATCH(block.terminator)
        WITH(
            [&](Ref<BranchTerminator> terminatorRef) -> void {
                const auto& terminator = program[terminatorRef];
                countValue(terminator.condition);
                for (const auto& arg : terminator.trueArgs) {
                    countValue(arg);
                }
                for (const auto& arg : terminator.falseArgs) {
                    countValue(arg);
                }
            },
            [&](Ref<JumpTerminator> terminatorRef) -> void {
                for (const auto& arg : program[terminatorRef].args) {
                    countValue(arg);
                }
            },
            [&](Ref<ReturnTerminator> terminatorRef) -> void {
                const auto& terminator = program[terminatorRef];
                if (terminator.value.has_value()) {
                    countValue(*terminator.value);
                }
            });
    }

    auto isEmptyForwarder = [&](Ref<BasicBlock> blockRef) -> bool {
        if (blockRef == entryBlockRef) {
            return false;
        }
        const auto& block = program[blockRef];
        if (!block.statements.empty()
            || !std::holds_alternative<Ref<JumpTerminator>>(block.terminator)) {
            return false;
        }
        const auto jumpRef = std::get<Ref<JumpTerminator>>(block.terminator);
        std::unordered_map<std::string, size_t> localForwardUseCounts;
        for (const auto& arg : program[jumpRef].args) {
            if (const auto spelling = symbolSpelling(arg);
                spelling.has_value()) {
                ++localForwardUseCounts[*spelling];
            }
        }
        for (const auto paramRef : block.params) {
            const auto& param = program[paramRef];
            const auto totalUseCount
                = valueUseCounts.contains(param.symbol.spelling)
                ? valueUseCounts.at(param.symbol.spelling)
                : 0;
            const auto forwardUseCount
                = localForwardUseCounts.contains(param.symbol.spelling)
                ? localForwardUseCounts.at(param.symbol.spelling)
                : 0;
            if (totalUseCount != forwardUseCount) {
                return false;
            }
        }
        return true;
    };

    auto substituteValue
        = [](const Value& value,
              const std::unordered_map<std::string, Value>& replacementByParam)
        -> Value {
        const auto* symbol = std::get_if<Symbol>(&value);
        if (symbol == nullptr) {
            return value;
        }
        const auto replacementIt = replacementByParam.find(symbol->spelling);
        if (replacementIt == replacementByParam.end()) {
            return value;
        }
        return replacementIt->second;
    };

    struct ResolvedEdge {
        Symbol target;
        std::vector<Value> args;
        bool changed = false;
    };

    auto resolveEdge = [&](const Symbol& target,
                           const std::vector<Value>& args) -> ResolvedEdge {
        ResolvedEdge resolved {
            .target = target,
            .args = args,
            .changed = false,
        };
        std::unordered_set<std::string> visited;

        while (true) {
            const auto blockIt = blockByLabel.find(resolved.target.spelling);
            if (blockIt == blockByLabel.end()
                || !isEmptyForwarder(blockIt->second)) {
                return resolved;
            }
            if (!visited.insert(resolved.target.spelling).second) {
                return resolved;
            }

            const auto& block = program[blockIt->second];
            assert(block.params.size() == resolved.args.size());
            std::unordered_map<std::string, Value> replacementByParam;
            replacementByParam.reserve(block.params.size());
            for (size_t index = 0;
                index < block.params.size() && index < resolved.args.size();
                ++index) {
                replacementByParam.insert_or_assign(
                    program[block.params[index]].symbol.spelling,
                    resolved.args[index]);
            }

            const auto jumpRef
                = std::get<Ref<JumpTerminator>>(block.terminator);
            const auto& jump = program[jumpRef];
            std::vector<Value> nextArgs;
            nextArgs.reserve(jump.args.size());
            for (const auto& arg : jump.args) {
                nextArgs.push_back(substituteValue(arg, replacementByParam));
            }
            resolved.target = jump.target;
            resolved.args = std::move(nextArgs);
            resolved.changed = true;
        }
    };

    for (const auto blockRef : function.blocks) {
        auto& block = program[blockRef];
        MATCH(block.terminator)
        WITH(
            [&](Ref<BranchTerminator> terminatorRef) -> void {
                auto& terminator = program[terminatorRef];
                const auto trueEdge
                    = resolveEdge(terminator.trueTarget, terminator.trueArgs);
                if (trueEdge.changed) {
                    terminator.trueTarget = trueEdge.target;
                    terminator.trueArgs = trueEdge.args;
                }
                const auto falseEdge
                    = resolveEdge(terminator.falseTarget, terminator.falseArgs);
                if (falseEdge.changed) {
                    terminator.falseTarget = falseEdge.target;
                    terminator.falseArgs = falseEdge.args;
                }
            },
            [&](Ref<JumpTerminator> terminatorRef) -> void {
                auto& terminator = program[terminatorRef];
                const auto edge
                    = resolveEdge(terminator.target, terminator.args);
                if (edge.changed) {
                    terminator.target = edge.target;
                    terminator.args = edge.args;
                }
            },
            [&](Ref<ReturnTerminator>) -> void { });
    }

    std::vector<Ref<BasicBlock>> worklist { entryBlockRef };
    std::unordered_set<Ref<BasicBlock>> reachable;
    reachable.reserve(function.blocks.size());
    while (!worklist.empty()) {
        const auto blockRef = worklist.back();
        worklist.pop_back();
        if (!reachable.insert(blockRef).second) {
            continue;
        }

        const auto& block = program[blockRef];
        MATCH(block.terminator)
        WITH(
            [&](Ref<BranchTerminator> terminatorRef) -> void {
                const auto& terminator = program[terminatorRef];
                if (const auto trueIt
                    = blockByLabel.find(terminator.trueTarget.spelling);
                    trueIt != blockByLabel.end()) {
                    worklist.push_back(trueIt->second);
                }
                if (const auto falseIt
                    = blockByLabel.find(terminator.falseTarget.spelling);
                    falseIt != blockByLabel.end()) {
                    worklist.push_back(falseIt->second);
                }
            },
            [&](Ref<JumpTerminator> terminatorRef) -> void {
                const auto& terminator = program[terminatorRef];
                if (const auto targetIt
                    = blockByLabel.find(terminator.target.spelling);
                    targetIt != blockByLabel.end()) {
                    worklist.push_back(targetIt->second);
                }
            },
            [&](Ref<ReturnTerminator>) -> void { });
    }

    std::erase_if(function.blocks, [&](Ref<BasicBlock> blockRef) -> bool {
        return !reachable.contains(blockRef);
    });
}

void validate(const Program& program)
{
    std::unordered_map<std::string, ValidationType> valueTypes;
    std::unordered_map<std::string, Type> pointeeTypes;
    std::unordered_map<std::string, ValidationType> functionReturnTypes;
    std::unordered_set<std::string> combineSymbols;
    std::unordered_set<std::string> pointwiseSymbols;
    std::unordered_map<std::string, int32_t> fusionSourceBySymbol;

    auto recordTypedValue
        = [&](const std::string& symbol, const Type& type) -> void {
        valueTypes[symbol] = validationTypeOf(type);
        if (const auto* pointerRef = std::get_if<Ref<PointerType>>(&type);
            pointerRef != nullptr) {
            pointeeTypes[symbol] = program[*pointerRef].pointeeType;
        }
    };

    for (const auto& item : program.items) {
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                if constexpr (std::same_as<Item, GlobalMemoryDef>) {
                    const auto& global = program[itemRef];
                    valueTypes[global.name.spelling] = ValidationType::pointer;
                    pointeeTypes[global.name.spelling] = global.allocType;
                } else if constexpr (std::same_as<Item, FunctionDecl>) {
                    const auto& function = program[itemRef];
                    functionReturnTypes[function.name.spelling]
                        = function.returnType.has_value()
                        ? validationTypeOf(*function.returnType)
                        : ValidationType::other;
                } else if constexpr (std::same_as<Item, FunctionDef>) {
                    const auto& function = program[itemRef];
                    functionReturnTypes[function.name.spelling]
                        = function.returnType.has_value()
                        ? validationTypeOf(*function.returnType)
                        : ValidationType::other;
                    for (const auto paramRef : function.params) {
                        const auto& param = program[paramRef];
                        recordTypedValue(param.symbol.spelling, param.type);
                    }
                    for (const auto blockRef : function.blocks) {
                        const auto& block = program[blockRef];
                        for (const auto paramRef : block.params) {
                            const auto& param = program[paramRef];
                            recordTypedValue(param.symbol.spelling, param.type);
                        }
                        for (const auto& statement : block.statements) {
                            if (const auto* symbolDefRef
                                = std::get_if<Ref<SymbolDef>>(&statement)) {
                                const auto& symbolDef = program[*symbolDefRef];
                                if (std::holds_alternative<Ref<CombineExpr>>(
                                        symbolDef.rhs)) {
                                    combineSymbols.insert(
                                        symbolDef.symbol.spelling);
                                    fusionSourceBySymbol.insert_or_assign(
                                        symbolDef.symbol.spelling,
                                        symbolDef.sourcePos.m_offset);
                                }
                                if (std::holds_alternative<Ref<PointwiseExpr>>(
                                        symbolDef.rhs)) {
                                    pointwiseSymbols.insert(
                                        symbolDef.symbol.spelling);
                                    fusionSourceBySymbol.insert_or_assign(
                                        symbolDef.symbol.spelling,
                                        symbolDef.sourcePos.m_offset);
                                }
                            }
                        }
                    }
                }
            },
            item);
    }

    std::erase_if(valueTypes, [](const auto& item) {
        return !item.first.empty() && item.first.front() == '%';
    });
    auto eraseLocalSymbol = [](const std::string& name) {
        return !name.empty() && name.front() == '%';
    };
    std::erase_if(combineSymbols, eraseLocalSymbol);
    std::erase_if(pointwiseSymbols, eraseLocalSymbol);
    std::erase_if(fusionSourceBySymbol,
        [&](const auto& item) { return eraseLocalSymbol(item.first); });
    const auto globalValueTypes = valueTypes;
    const auto globalPointeeTypes = pointeeTypes;
    const auto globalCombineSymbols = combineSymbols;
    const auto globalPointwiseSymbols = pointwiseSymbols;
    const auto globalFusionSourceBySymbol = fusionSourceBySymbol;

    auto hasSameFusionSource
        = [&](const std::string& symbol, int32_t sourceOffset) -> bool {
        const auto it = fusionSourceBySymbol.find(symbol);
        return it != fusionSourceBySymbol.end() && it->second == sourceOffset;
    };

    auto resultTypeForRhs = [&](const SymbolRhs& rhs) -> ValidationType {
        return MATCH(rhs) WITH(
            [&](Ref<MemoryDeclaration> ref) -> ValidationType {
                (void)ref;
                return ValidationType::pointer;
            },
            [&](Ref<LoadExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                const auto it = pointeeTypes.find(expr.source.spelling);
                return it == pointeeTypes.end() ? ValidationType::unknown
                                                : validationTypeOf(it->second);
            },
            [&](Ref<GetPointerExpr> ref) -> ValidationType {
                (void)ref;
                return ValidationType::pointer;
            },
            [&](Ref<GetElementPointerExpr> ref) -> ValidationType {
                (void)ref;
                return ValidationType::pointer;
            },
            [&](Ref<BinaryExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                const auto lhsType
                    = validationTypeOfValue(expr.lhs, valueTypes);
                const auto rhsType
                    = validationTypeOfValue(expr.rhs, valueTypes);
                const bool isIntOnly = expr.op == BinaryOp::mod
                    || expr.op == BinaryOp::bitAnd || expr.op == BinaryOp::bitOr
                    || expr.op == BinaryOp::bitXor || expr.op == BinaryOp::shl
                    || expr.op == BinaryOp::shr || expr.op == BinaryOp::sar;
                if (isIntOnly) {
                    requireValueType(expr.lhs, ValidationType::integer,
                        valueTypes,
                        "integer-only binary operator has non-int lhs",
                        expr.sourcePos.m_offset);
                    requireValueType(expr.rhs, ValidationType::integer,
                        valueTypes,
                        "integer-only binary operator has non-int rhs",
                        expr.sourcePos.m_offset);
                }
                if (lhsType == ValidationType::poly
                    || rhsType == ValidationType::poly) {
                    auto msg = std::string("at offset ")
                        + std::to_string(expr.sourcePos.m_offset)
                        + ": poly values must use dedicated "
                          "pseudo-instructions: "
                        + serializeValue(expr.lhs) + ", "
                        + serializeValue(expr.rhs);
                    throw std::runtime_error(msg);
                }
                switch (expr.op) {
                case BinaryOp::ne:
                case BinaryOp::eq:
                case BinaryOp::gt:
                case BinaryOp::lt:
                case BinaryOp::ge:
                case BinaryOp::le:
                    return ValidationType::integer;
                default:
                    return lhsType == ValidationType::mint
                            || rhsType == ValidationType::mint
                        ? ValidationType::mint
                        : ValidationType::integer;
                }
            },
            [&](Ref<CallExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                const auto it = functionReturnTypes.find(expr.callee.spelling);
                return it == functionReturnTypes.end() ? ValidationType::unknown
                                                       : it->second;
            },
            [&](Ref<CopyExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                return validationTypeOfValue(expr.value, valueTypes);
            },
            [&](Ref<PointwiseExpr> ref) -> ValidationType {
                std::function<ValidationType(Ref<PointwiseNode>)>
                    validatePointwiseNode
                    = [&](Ref<PointwiseNode> nodeRef) -> ValidationType {
                    const auto& node = program[nodeRef];
                    return MATCH(node.kind) WITH(
                        [&](const PointwiseLeaf& leaf) -> ValidationType {
                            if (const auto* symbol
                                = std::get_if<Symbol>(&leaf.value);
                                symbol != nullptr
                                && pointwiseSymbols.contains(symbol->spelling)
                                && hasSameFusionSource(symbol->spelling,
                                    program[ref].sourcePos.m_offset)) {
                                throw std::runtime_error(
                                    "pointwise operand cannot be another "
                                    "pointwise result from the same "
                                    "expression");
                            }
                            const auto valueType
                                = validationTypeOfValue(leaf.value, valueTypes);
                            if (valueType != ValidationType::poly
                                && valueType != ValidationType::mint
                                && valueType != ValidationType::unknown
                                && !std::holds_alternative<IntegerLiteral>(
                                    leaf.value)) {
                                throw std::runtime_error(
                                    "pointwise leaf must be poly or mint");
                            }
                            return std::holds_alternative<IntegerLiteral>(
                                       leaf.value)
                                ? ValidationType::mint
                                : valueType;
                        },
                        [&](const PointwiseBinary& binary) -> ValidationType {
                            const auto lhsType
                                = validatePointwiseNode(binary.lhs);
                            const auto rhsType
                                = validatePointwiseNode(binary.rhs);
                            switch (binary.op) {
                            case PvBinaryOp::add:
                            case PvBinaryOp::sub:
                            case PvBinaryOp::mul:
                                if (lhsType != ValidationType::poly
                                    || rhsType != ValidationType::poly) {
                                    throw std::runtime_error(
                                        "pointwise add/sub/mul expect poly "
                                        "operands: "
                                        + serializePointwiseNode(
                                            nodeRef, program));
                                }
                                return ValidationType::poly;
                            case PvBinaryOp::times:
                                if (lhsType != ValidationType::poly
                                    || rhsType != ValidationType::mint) {
                                    throw std::runtime_error(
                                        "pointwise times expects poly and mint "
                                        "operands: "
                                        + serializePointwiseNode(
                                            nodeRef, program));
                                }
                                return ValidationType::poly;
                            }
                            throw std::runtime_error(
                                "unknown pointwise operation");
                        });
                };

                const auto& expr = program[ref];
                const auto resultType = validatePointwiseNode(expr.root);
                if (resultType != ValidationType::poly) {
                    throw std::runtime_error(
                        "pointwise expression root must produce poly");
                }
                return ValidationType::poly;
            },
            [&](Ref<CombineExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                auto offset = [&](const Value& v) { return sourceOffset(v); };
                auto isZero = [](const Value& value) -> bool {
                    const auto* literal = std::get_if<IntegerLiteral>(&value);
                    return literal != nullptr && literal->value == 0;
                };
                bool allTermsAreTrivialPointwise = expr.terms.size() >= 2;
                for (const auto& term : expr.terms) {
                    if (const auto* symbol = std::get_if<Symbol>(&term.value);
                        symbol != nullptr
                        && combineSymbols.contains(symbol->spelling)
                        && hasSameFusionSource(symbol->spelling,
                            program[ref].sourcePos.m_offset)) {
                        throw std::runtime_error(
                            "combine term source cannot be another combine "
                            "from the same expression: "
                            + symbol->spelling + " source "
                            + std::to_string(program[ref].sourcePos.m_offset));
                    }
                    const auto* termSymbol = std::get_if<Symbol>(&term.value);
                    allTermsAreTrivialPointwise = allTermsAreTrivialPointwise
                        && termSymbol != nullptr
                        && pointwiseSymbols.contains(termSymbol->spelling)
                        && isZero(term.start) && !term.end.has_value()
                        && isZero(term.shift);
                    requireValueType(term.value, ValidationType::poly,
                        valueTypes, "combine term source must be poly",
                        offset(term.value));
                    requireValueType(term.start, ValidationType::integer,
                        valueTypes, "combine term start must be int",
                        offset(term.start));
                    if (term.end.has_value()) {
                        requireValueType(*term.end, ValidationType::integer,
                            valueTypes, "combine term end must be int",
                            offset(*term.end));
                    }
                    requireValueType(term.shift, ValidationType::integer,
                        valueTypes, "combine term shift must be int",
                        offset(term.shift));
                    if (!valueMatchesType(
                            term.scale, ValidationType::mint, valueTypes)) {
                        throw std::runtime_error(
                            "combine term scale must be mint: "
                            + serializeValue(term.scale));
                    }
                }
                if (allTermsAreTrivialPointwise) {
                    throw std::runtime_error(
                        "combine cannot be a trivial linear combination of "
                        "pointwise results");
                }
                return ValidationType::poly;
            },
            [&](Ref<GetCoeffExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                requireValueType(expr.value, ValidationType::poly, valueTypes,
                    "getcoeff expects a poly operand",
                    sourceOffset(expr.value));
                requireValueType(expr.index, ValidationType::integer,
                    valueTypes, "getcoeff index must be int",
                    sourceOffset(expr.index));
                return ValidationType::mint;
            },
            [&](Ref<PolyConstructExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                for (const auto& element : expr.elements) {
                    requireValueType(element, ValidationType::mint, valueTypes,
                        "poly_construct elements must be mint",
                        sourceOffset(element));
                }
                return ValidationType::poly;
            },
            [&](Ref<ConversionExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                if (expr.op == ConversionOp::int2mint) {
                    requireValueType(expr.value, ValidationType::integer,
                        valueTypes, "int2mint expects an int operand",
                        sourceOffset(expr.value));
                    return ValidationType::mint;
                }
                requireValueType(expr.value, ValidationType::mint, valueTypes,
                    "mint2int expects a mint operand",
                    sourceOffset(expr.value));
                return ValidationType::integer;
            });
    };

    for (const auto& item : program.items) {
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                if constexpr (std::same_as<Item, FunctionDef>) {
                    const auto& function = program[itemRef];
                    valueTypes = globalValueTypes;
                    pointeeTypes = globalPointeeTypes;
                    combineSymbols = globalCombineSymbols;
                    pointwiseSymbols = globalPointwiseSymbols;
                    fusionSourceBySymbol = globalFusionSourceBySymbol;
                    for (const auto paramRef : function.params) {
                        const auto& param = program[paramRef];
                        recordTypedValue(param.symbol.spelling, param.type);
                    }
                    for (const auto scanBlockRef : function.blocks) {
                        const auto& scanBlock = program[scanBlockRef];
                        for (const auto& statement : scanBlock.statements) {
                            if (const auto* symbolDefRef
                                = std::get_if<Ref<SymbolDef>>(&statement)) {
                                const auto& symbolDef = program[*symbolDefRef];
                                if (std::holds_alternative<Ref<CombineExpr>>(
                                        symbolDef.rhs)) {
                                    combineSymbols.insert(
                                        symbolDef.symbol.spelling);
                                    fusionSourceBySymbol.insert_or_assign(
                                        symbolDef.symbol.spelling,
                                        symbolDef.sourcePos.m_offset);
                                }
                                if (std::holds_alternative<Ref<PointwiseExpr>>(
                                        symbolDef.rhs)) {
                                    pointwiseSymbols.insert(
                                        symbolDef.symbol.spelling);
                                    fusionSourceBySymbol.insert_or_assign(
                                        symbolDef.symbol.spelling,
                                        symbolDef.sourcePos.m_offset);
                                }
                            }
                        }
                    }
                    for (const auto blockRef : function.blocks) {
                        const auto& block = program[blockRef];
                        for (const auto paramRef : block.params) {
                            const auto& param = program[paramRef];
                            recordTypedValue(param.symbol.spelling, param.type);
                        }
                        for (const auto& statement : block.statements) {
                            std::visit(
                                [&](auto statementRef) {
                                    using StatementNode = std::remove_cvref_t<
                                        decltype(program[statementRef])>;
                                    if constexpr (std::same_as<StatementNode,
                                                      SymbolDef>) {
                                        const auto& symbolDef
                                            = program[statementRef];
                                        const auto resultType
                                            = resultTypeForRhs(symbolDef.rhs);
                                        valueTypes[symbolDef.symbol.spelling]
                                            = resultType;
                                        if (const auto* memRef = std::get_if<
                                                Ref<MemoryDeclaration>>(
                                                &symbolDef.rhs)) {
                                            pointeeTypes[symbolDef.symbol
                                                    .spelling]
                                                = program[*memRef].allocType;
                                        } else if (const auto* loadRef
                                            = std::get_if<Ref<LoadExpr>>(
                                                &symbolDef.rhs)) {
                                            const auto& expr
                                                = program[*loadRef];
                                            const auto srcIt
                                                = pointeeTypes.find(
                                                    expr.source.spelling);
                                            if (srcIt != pointeeTypes.end()) {
                                                if (const auto* pointerRef
                                                    = std::get_if<
                                                        Ref<PointerType>>(
                                                        &srcIt->second);
                                                    pointerRef != nullptr) {
                                                    pointeeTypes[symbolDef
                                                            .symbol.spelling]
                                                        = program[*pointerRef]
                                                              .pointeeType;
                                                }
                                            }
                                        } else if (const auto* getPtrRef
                                            = std::get_if<Ref<GetPointerExpr>>(
                                                &symbolDef.rhs)) {
                                            const auto& expr
                                                = program[*getPtrRef];
                                            const auto srcIt
                                                = pointeeTypes.find(
                                                    expr.source.spelling);
                                            if (srcIt != pointeeTypes.end()) {
                                                pointeeTypes[symbolDef.symbol
                                                        .spelling]
                                                    = srcIt->second;
                                            }
                                        } else if (const auto* gepRef
                                            = std::get_if<
                                                Ref<GetElementPointerExpr>>(
                                                &symbolDef.rhs)) {
                                            const auto& expr = program[*gepRef];
                                            const auto srcIt
                                                = pointeeTypes.find(
                                                    expr.source.spelling);
                                            if (srcIt != pointeeTypes.end()
                                                && std::holds_alternative<
                                                    Ref<ArrayType>>(
                                                    srcIt->second)) {
                                                pointeeTypes[symbolDef.symbol
                                                        .spelling]
                                                    = program
                                                          [*std::get_if<
                                                               Ref<ArrayType>>(
                                                               &srcIt->second)]
                                                              .elementType;
                                            } else {
                                                pointeeTypes[symbolDef.symbol
                                                        .spelling] = srcIt
                                                        == pointeeTypes.end()
                                                    ? Type { I32Type { } }
                                                    : srcIt->second;
                                            }
                                        }
                                    }
                                },
                                statement);
                        }
                    }
                }
            },
            item);
    }
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
