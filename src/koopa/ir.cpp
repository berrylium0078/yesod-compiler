#include "koopa/ir.h"

#include <algorithm>
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
            [&](const PvType&) { return std::string("pv"); },
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
                            } else if constexpr (std::same_as<Rhs,
                                                     UnaryPolyExpr>) {
                                const auto& expr = program[rhsRef];
                                output << toString(expr.op) << ' '
                                       << serializeValue(expr.value);
                            } else if constexpr (std::same_as<Rhs,
                                                     PvBinaryExpr>) {
                                const auto& expr = program[rhsRef];
                                output << toString(expr.op) << ' '
                                       << serializeValue(expr.lhs) << ", "
                                       << serializeValue(expr.rhs);
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

std::string_view toString(UnaryPolyOp op)
{
    switch (op) {
    case UnaryPolyOp::ntt:
        return "ntt";
    case UnaryPolyOp::intt:
        return "intt";
    }
    throw std::runtime_error("unknown unary poly operation");
}

std::string_view toString(PvBinaryOp op)
{
    switch (op) {
    case PvBinaryOp::add:
        return "pv_add";
    case PvBinaryOp::sub:
        return "pv_sub";
    case PvBinaryOp::mul:
        return "pv_mul";
    }
    throw std::runtime_error("unknown pv binary operation");
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

    enum class ValidationType {
        integer,
        mint,
        poly,
        pv,
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
            [](const PvType&) -> ValidationType { return ValidationType::pv; },
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
                fullMessage = "at offset " + std::to_string(offset)
                    + ": " + message;
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

    [[nodiscard]] int32_t sourceOffset(const std::optional<Value>& value)
    {
        if (value.has_value()) {
            return sourceOffset(*value);
        }
        return -1;
    }

} // namespace

void validate(const Program& program)
{
    std::unordered_map<std::string, ValidationType> valueTypes;
    std::unordered_map<std::string, Type> pointeeTypes;
    std::unordered_set<std::string> combineSymbols;

    for (const auto& item : program.items) {
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                if constexpr (std::same_as<Item, GlobalMemoryDef>) {
                    const auto& global = program[itemRef];
                    valueTypes[global.name.spelling] = ValidationType::pointer;
                    pointeeTypes[global.name.spelling] = global.allocType;
                } else if constexpr (std::same_as<Item, FunctionDef>) {
                    const auto& function = program[itemRef];
                    for (const auto paramRef : function.params) {
                        const auto& param = program[paramRef];
                        valueTypes[param.symbol.spelling]
                            = validationTypeOf(param.type);
                    }
                    for (const auto blockRef : function.blocks) {
                        const auto& block = program[blockRef];
                        for (const auto paramRef : block.params) {
                            const auto& param = program[paramRef];
                            valueTypes[param.symbol.spelling]
                                = validationTypeOf(param.type);
                        }
                        for (const auto& statement : block.statements) {
                            if (const auto* symbolDefRef
                                = std::get_if<Ref<SymbolDef>>(&statement)) {
                                const auto& symbolDef = program[*symbolDefRef];
                                if (std::holds_alternative<Ref<CombineExpr>>(
                                        symbolDef.rhs)) {
                                    combineSymbols.insert(
                                        symbolDef.symbol.spelling);
                                }
                            }
                        }
                    }
                }
            },
            item);
    }

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
                    || lhsType == ValidationType::pv
                    || rhsType == ValidationType::poly
                    || rhsType == ValidationType::pv) {
                    auto msg = std::string("at offset ")
                        + std::to_string(expr.sourcePos.m_offset)
                        + ": poly/pv values must use "
                          "dedicated pseudo-instructions";
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
                (void)ref;
                return ValidationType::unknown;
            },
            [&](Ref<UnaryPolyExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                auto offset = [&](const Value& v) { return sourceOffset(v); };
                if (expr.op == UnaryPolyOp::ntt) {
                    requireValueType(expr.value, ValidationType::poly,
                        valueTypes, "ntt expects a poly operand",
                        offset(expr.value));
                    return ValidationType::pv;
                }
                requireValueType(expr.value, ValidationType::pv, valueTypes,
                    "intt expects a pv operand", offset(expr.value));
                return ValidationType::poly;
            },
            [&](Ref<PvBinaryExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                const auto lhsType
                    = validationTypeOfValue(expr.lhs, valueTypes);
                const auto rhsType
                    = validationTypeOfValue(expr.rhs, valueTypes);
                auto offset = expr.sourcePos.m_offset;
                if (expr.op == PvBinaryOp::add || expr.op == PvBinaryOp::sub) {
                    if (lhsType != ValidationType::pv
                        || rhsType != ValidationType::pv) {
                        auto msg = std::string("at offset ")
                            + std::to_string(offset)
                            + ": pv_add/sub expect pv operands";
                        throw std::runtime_error(msg);
                    }
                } else if (!((lhsType == ValidationType::pv
                                 && rhsType == ValidationType::pv)
                               || (lhsType == ValidationType::pv
                                   && rhsType == ValidationType::mint)
                               || (lhsType == ValidationType::mint
                                   && rhsType == ValidationType::pv)
                               || (lhsType == ValidationType::pv
                                   && std::holds_alternative<IntegerLiteral>(
                                       expr.rhs))
                               || (rhsType == ValidationType::pv
                                   && std::holds_alternative<IntegerLiteral>(
                                       expr.lhs)))) {
                    auto msg = std::string("at offset ")
                        + std::to_string(offset)
                        + ": pv_mul expects pv/pv or pv/mint operands";
                    throw std::runtime_error(msg);
                }
                return ValidationType::pv;
            },
            [&](Ref<CombineExpr> ref) -> ValidationType {
                const auto& expr = program[ref];
                auto offset = [&](const Value& v) { return sourceOffset(v); };
                for (const auto& term : expr.terms) {
                    if (const auto* symbol = std::get_if<Symbol>(&term.value);
                        symbol != nullptr
                        && combineSymbols.contains(symbol->spelling)) {
                        throw std::runtime_error(
                            "combine term source cannot be another combine");
                    }
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
                    requireValueType(term.scale, ValidationType::mint,
                        valueTypes, "combine term scale must be mint",
                        offset(term.scale));
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
                    for (const auto blockRef : function.blocks) {
                        const auto& block = program[blockRef];
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
                                        } else if (const auto* getPtrRef
                                            = std::get_if<Ref<GetPointerExpr>>(
                                                &symbolDef.rhs)) {
                                            const auto& expr
                                                = program[*getPtrRef];
                                            pointeeTypes[symbolDef.symbol
                                                             .spelling]
                                                = pointeeTypes[expr.source
                                                                   .spelling];
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
                                                    = program[*std::get_if<
                                                        Ref<ArrayType>>(
                                                        &srcIt->second)]
                                                          .elementType;
                                            } else {
                                                pointeeTypes[symbolDef.symbol
                                                                 .spelling]
                                                    = srcIt == pointeeTypes.end()
                                                          ? Type { I32Type {} }
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