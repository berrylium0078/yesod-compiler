#include "koopa/cse.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "utils.h"

namespace yesod::koopa::ir {

namespace {
    using DefMap = std::unordered_map<std::string, Ref<SymbolDef>>;

    [[nodiscard]] std::string valueKey(const Value& value)
    {
        return MATCH(value) WITH(
            [](const Symbol& symbol) -> std::string {
                return "s:" + symbol.spelling;
            },
            [](const IntegerLiteral& literal) -> std::string {
                return "i:" + std::to_string(literal.value);
            },
            [](const UndefValue&) -> std::string { return "undef"; });
    }

    [[nodiscard]] Value symbolValue(const std::string& spelling)
    {
        return Symbol { .sourcePos = { }, .spelling = spelling };
    }

    [[nodiscard]] bool isIntegerLiteral(const Value& value, int32_t expected)
    {
        const auto* literal = std::get_if<IntegerLiteral>(&value);
        return literal != nullptr && literal->value == expected;
    }

    [[nodiscard]] std::optional<int32_t> integerLiteralValue(const Value& value)
    {
        if (const auto* literal = std::get_if<IntegerLiteral>(&value)) {
            return literal->value;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool isCommutative(BinaryOp op)
    {
        switch (op) {
        case BinaryOp::eq:
        case BinaryOp::ne:
        case BinaryOp::add:
        case BinaryOp::mul:
        case BinaryOp::bitAnd:
        case BinaryOp::bitOr:
        case BinaryOp::bitXor:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool isPureBinary(BinaryOp op)
    {
        return op != BinaryOp::div && op != BinaryOp::mod;
    }

    [[nodiscard]] bool isCommutative(PvBinaryOp op)
    {
        return op == PvBinaryOp::add || op == PvBinaryOp::mul;
    }

    [[nodiscard]] int32_t nextPow2(int32_t value)
    {
        if (value <= 1) {
            return 1;
        }
        int32_t result = 1;
        while (result < value) {
            if (result > std::numeric_limits<int32_t>::max() / 2) {
                return value;
            }
            result *= 2;
        }
        return result;
    }

    [[nodiscard]] std::optional<int32_t> evaluateBinary(
        BinaryOp op, int32_t lhs, int32_t rhs)
    {
        switch (op) {
        case BinaryOp::ne:
            return lhs != rhs;
        case BinaryOp::eq:
            return lhs == rhs;
        case BinaryOp::gt:
            return lhs > rhs;
        case BinaryOp::lt:
            return lhs < rhs;
        case BinaryOp::ge:
            return lhs >= rhs;
        case BinaryOp::le:
            return lhs <= rhs;
        case BinaryOp::add:
            return static_cast<int32_t>(
                static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs));
        case BinaryOp::sub:
            return static_cast<int32_t>(
                static_cast<uint32_t>(lhs) - static_cast<uint32_t>(rhs));
        case BinaryOp::mul:
            return static_cast<int32_t>(
                static_cast<uint32_t>(lhs) * static_cast<uint32_t>(rhs));
        case BinaryOp::bitAnd:
            return lhs & rhs;
        case BinaryOp::bitOr:
            return lhs | rhs;
        case BinaryOp::bitXor:
            return lhs ^ rhs;
        case BinaryOp::shl:
            if (rhs < 0 || rhs >= 32) {
                return std::nullopt;
            }
            return static_cast<int32_t>(
                static_cast<uint32_t>(lhs) << static_cast<uint32_t>(rhs));
        case BinaryOp::shr:
            if (rhs < 0 || rhs >= 32) {
                return std::nullopt;
            }
            return static_cast<int32_t>(
                static_cast<uint32_t>(lhs) >> static_cast<uint32_t>(rhs));
        case BinaryOp::sar:
            if (rhs < 0 || rhs >= 32) {
                return std::nullopt;
            }
            return lhs >> rhs;
        case BinaryOp::div:
        case BinaryOp::mod:
            return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string pointwiseNodeKey(
        const Program& program, Ref<PointwiseNode> nodeRef)
    {
        const auto& node = program[nodeRef];
        return MATCH(node.kind) WITH(
            [&](const PointwiseLeaf& leaf) -> std::string {
                return "leaf:" + std::to_string(static_cast<int>(leaf.kind))
                    + ":" + valueKey(leaf.value);
            },
            [&](const PointwiseBinary& binary) -> std::string {
                std::string lhs = pointwiseNodeKey(program, binary.lhs);
                std::string rhs = pointwiseNodeKey(program, binary.rhs);
                if (isCommutative(binary.op) && rhs < lhs) {
                    std::swap(lhs, rhs);
                }
                return "pv:" + std::string(toString(binary.op)) + "(" + lhs
                    + "," + rhs + ")";
            });
    }

    [[nodiscard]] std::string combineTermKey(const CombineTerm& term)
    {
        std::string result
            = "(" + valueKey(term.value) + "," + valueKey(term.start) + ",";
        result += term.end.has_value() ? valueKey(*term.end) : "inf";
        result += "," + valueKey(term.shift) + "," + valueKey(term.scale) + ")";
        return result;
    }

    [[nodiscard]] std::optional<std::string> rhsKey(
        const Program& program, const SymbolRhs& rhs)
    {
        return MATCH(rhs) WITH(
            [](Ref<MemoryDeclaration>) -> std::optional<std::string> {
                return std::nullopt;
            },
            [](Ref<LoadExpr>) -> std::optional<std::string> {
                return std::nullopt;
            },
            [](Ref<GetPointerExpr>) -> std::optional<std::string> {
                return std::nullopt;
            },
            [](Ref<GetElementPointerExpr>) -> std::optional<std::string> {
                return std::nullopt;
            },
            [&](Ref<BinaryExpr> ref) -> std::optional<std::string> {
                const auto& expr = program[ref];
                if (!isPureBinary(expr.op)) {
                    return std::nullopt;
                }
                std::string lhs = valueKey(expr.lhs);
                std::string rhsKey = valueKey(expr.rhs);
                if (isCommutative(expr.op) && rhsKey < lhs) {
                    std::swap(lhs, rhsKey);
                }
                return "bin:" + std::string(toString(expr.op)) + ":" + lhs + ","
                    + rhsKey;
            },
            [](Ref<CallExpr>) -> std::optional<std::string> {
                return std::nullopt;
            },
            [](Ref<CopyExpr>) -> std::optional<std::string> {
                return std::nullopt;
            },
            [&](Ref<GetAttrExpr> ref) -> std::optional<std::string> {
                const auto& expr = program[ref];
                return "get_attr:" + std::string(toString(expr.attr)) + ":"
                    + valueKey(expr.value);
            },
            [&](Ref<SetAttrExpr> ref) -> std::optional<std::string> {
                const auto& expr = program[ref];
                return "set_attr:" + std::string(toString(expr.attr)) + ":"
                    + valueKey(expr.value) + "," + valueKey(expr.attrValue);
            },
            [&](Ref<SelectExpr> ref) -> std::optional<std::string> {
                const auto& expr = program[ref];
                return "select:" + valueKey(expr.condition) + ","
                    + valueKey(expr.trueValue) + ","
                    + valueKey(expr.falseValue);
            },
            [&](Ref<NextPow2Expr> ref) -> std::optional<std::string> {
                return "next_pow2:" + valueKey(program[ref].value);
            },
            [&](Ref<NttExpr> ref) -> std::optional<std::string> {
                const auto& expr = program[ref];
                return "ntt:" + valueKey(expr.value) + ","
                    + valueKey(expr.length);
            },
            [&](Ref<PointwiseExpr> ref) -> std::optional<std::string> {
                const auto& expr = program[ref];
                return "pointwise:" + valueKey(expr.length) + ","
                    + valueKey(expr.activeL) + "," + valueKey(expr.activeR)
                    + ":" + pointwiseNodeKey(program, expr.root);
            },
            [&](Ref<CombineExpr> ref) -> std::optional<std::string> {
                std::vector<std::string> termKeys;
                for (const auto& term : program[ref].terms) {
                    termKeys.push_back(combineTermKey(term));
                }
                std::sort(termKeys.begin(), termKeys.end());
                std::ostringstream output;
                output << "combine:";
                for (const auto& termKey : termKeys) {
                    output << termKey << ";";
                }
                return output.str();
            },
            [&](Ref<GetCoeffExpr> ref) -> std::optional<std::string> {
                const auto& expr = program[ref];
                return "getcoeff:" + valueKey(expr.value) + ","
                    + valueKey(expr.index);
            },
            [&](Ref<PolyConstructExpr> ref) -> std::optional<std::string> {
                std::ostringstream output;
                output << "poly_construct:";
                for (const auto& element : program[ref].elements) {
                    output << valueKey(element) << ",";
                }
                return output.str();
            },
            [&](Ref<ConversionExpr> ref) -> std::optional<std::string> {
                const auto& expr = program[ref];
                return "conversion:" + std::string(toString(expr.op)) + ":"
                    + valueKey(expr.value);
            });
    }

    [[nodiscard]] std::optional<Ref<SetAttrExpr>> findSetAttrDefinition(
        const Program& program, const DefMap& defBySymbol, const Value& value)
    {
        const auto* symbol = std::get_if<Symbol>(&value);
        if (symbol == nullptr) {
            return std::nullopt;
        }
        const auto defIt = defBySymbol.find(symbol->spelling);
        if (defIt == defBySymbol.end()) {
            return std::nullopt;
        }
        if (const auto* setAttrRef
            = std::get_if<Ref<SetAttrExpr>>(&program[defIt->second].rhs)) {
            return *setAttrRef;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Ref<PolyConstructExpr>>
    findPolyConstructDefinition(
        const Program& program, const DefMap& defBySymbol, const Value& value)
    {
        const auto* symbol = std::get_if<Symbol>(&value);
        if (symbol == nullptr) {
            return std::nullopt;
        }
        const auto defIt = defBySymbol.find(symbol->spelling);
        if (defIt == defBySymbol.end()) {
            return std::nullopt;
        }
        if (const auto* polyConstructRef = std::get_if<Ref<PolyConstructExpr>>(
                &program[defIt->second].rhs)) {
            return *polyConstructRef;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<SymbolRhs> simplifyGetAttr(
        Program& program, const DefMap& defBySymbol, PolyAttr attr, Value value)
    {
        if (attr == PolyAttr::l || attr == PolyAttr::r) {
            const auto polyConstructRef
                = findPolyConstructDefinition(program, defBySymbol, value);
            if (polyConstructRef.has_value()) {
                const auto result = attr == PolyAttr::l
                    ? 0
                    : static_cast<int32_t>(
                          program[*polyConstructRef].elements.size());
                return program.alloc<CopyExpr>(CopyExpr {
                    .sourcePos = program[*polyConstructRef].sourcePos,
                    .value = IntegerLiteral {
                        .sourcePos = { },
                        .value = result,
                    },
                    .annotations = { },
                });
            }
        }

        std::unordered_set<std::string> visited;
        while (true) {
            const auto* symbol = std::get_if<Symbol>(&value);
            if (symbol == nullptr || !visited.insert(symbol->spelling).second) {
                return std::nullopt;
            }
            const auto setAttrRef
                = findSetAttrDefinition(program, defBySymbol, value);
            if (!setAttrRef.has_value()) {
                return std::nullopt;
            }

            const auto& setAttr = program[*setAttrRef];
            if (setAttr.attr == attr) {
                return program.alloc<CopyExpr>(CopyExpr {
                    .sourcePos = setAttr.sourcePos,
                    .value = setAttr.attrValue,
                    .annotations = { },
                });
            }

            value = setAttr.value;
            if (attr == PolyAttr::l || attr == PolyAttr::r) {
                const auto polyConstructRef
                    = findPolyConstructDefinition(program, defBySymbol, value);
                if (polyConstructRef.has_value()) {
                    const auto result = attr == PolyAttr::l
                        ? 0
                        : static_cast<int32_t>(
                              program[*polyConstructRef].elements.size());
                    return program.alloc<CopyExpr>(CopyExpr {
                        .sourcePos = program[*polyConstructRef].sourcePos,
                        .value = IntegerLiteral {
                            .sourcePos = { },
                            .value = result,
                        },
                        .annotations = { },
                    });
                }
            }
            if (!findSetAttrDefinition(program, defBySymbol, value)
                    .has_value()) {
                return program.alloc<GetAttrExpr>(GetAttrExpr {
                    .sourcePos = setAttr.sourcePos,
                    .attr = attr,
                    .value = std::move(value),
                    .annotations = { },
                });
            }
        }
    }

    [[nodiscard]] std::optional<SymbolRhs> simplifyRhs(
        Program& program, const DefMap& defBySymbol, const SymbolRhs& rhs)
    {
        return MATCH(rhs) WITH(
            [&](Ref<BinaryExpr> ref) -> std::optional<SymbolRhs> {
                auto& expr = program[ref];
                const auto lhsLiteral = integerLiteralValue(expr.lhs);
                const auto rhsLiteral = integerLiteralValue(expr.rhs);
                if (lhsLiteral.has_value() && rhsLiteral.has_value()) {
                    const auto result
                        = evaluateBinary(expr.op, *lhsLiteral, *rhsLiteral);
                    if (result.has_value()) {
                        return program.alloc<CopyExpr>(CopyExpr {
                            .sourcePos = expr.sourcePos,
                            .value = IntegerLiteral {
                                .sourcePos = { },
                                .value = *result,
                            },
                            .annotations = { },
                        });
                    }
                }
                if (expr.op == BinaryOp::add) {
                    if (isIntegerLiteral(expr.lhs, 0)) {
                        return program.alloc<CopyExpr>(CopyExpr {
                            .sourcePos = expr.sourcePos,
                            .value = expr.rhs,
                            .annotations = { },
                        });
                    }
                    if (isIntegerLiteral(expr.rhs, 0)) {
                        return program.alloc<CopyExpr>(CopyExpr {
                            .sourcePos = expr.sourcePos,
                            .value = expr.lhs,
                            .annotations = { },
                        });
                    }
                }
                if (expr.op == BinaryOp::sub && isIntegerLiteral(expr.rhs, 0)) {
                    return program.alloc<CopyExpr>(CopyExpr {
                        .sourcePos = expr.sourcePos,
                        .value = expr.lhs,
                        .annotations = { },
                    });
                }
                if (expr.op == BinaryOp::mul) {
                    if (isIntegerLiteral(expr.lhs, 0)
                        || isIntegerLiteral(expr.rhs, 0)) {
                        return program.alloc<CopyExpr>(CopyExpr {
                            .sourcePos = expr.sourcePos,
                            .value = IntegerLiteral {
                                .sourcePos = { },
                                .value = 0,
                            },
                            .annotations = { },
                        });
                    }
                    if (isIntegerLiteral(expr.lhs, 1)) {
                        return program.alloc<CopyExpr>(CopyExpr {
                            .sourcePos = expr.sourcePos,
                            .value = expr.rhs,
                            .annotations = { },
                        });
                    }
                    if (isIntegerLiteral(expr.rhs, 1)) {
                        return program.alloc<CopyExpr>(CopyExpr {
                            .sourcePos = expr.sourcePos,
                            .value = expr.lhs,
                            .annotations = { },
                        });
                    }
                }
                return std::nullopt;
            },
            [&](Ref<SelectExpr> ref) -> std::optional<SymbolRhs> {
                const auto& expr = program[ref];
                if (valueKey(expr.trueValue) == valueKey(expr.falseValue)) {
                    return program.alloc<CopyExpr>(CopyExpr {
                        .sourcePos = expr.sourcePos,
                        .value = expr.trueValue,
                        .annotations = { },
                    });
                }
                const auto condition = integerLiteralValue(expr.condition);
                if (!condition.has_value()) {
                    return std::nullopt;
                }
                return program.alloc<CopyExpr>(CopyExpr {
                    .sourcePos = expr.sourcePos,
                    .value = *condition != 0 ? expr.trueValue : expr.falseValue,
                    .annotations = { },
                });
            },
            [&](Ref<NextPow2Expr> ref) -> std::optional<SymbolRhs> {
                const auto value = integerLiteralValue(program[ref].value);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                return program.alloc<CopyExpr>(CopyExpr {
                    .sourcePos = program[ref].sourcePos,
                    .value = IntegerLiteral {
                        .sourcePos = { },
                        .value = nextPow2(*value),
                    },
                    .annotations = { },
                });
            },
            [&](Ref<CopyExpr> ref) -> std::optional<SymbolRhs> {
                return program.alloc<CopyExpr>(CopyExpr {
                    .sourcePos = program[ref].sourcePos,
                    .value = program[ref].value,
                    .annotations = { },
                });
            },
            [&](Ref<GetAttrExpr> ref) -> std::optional<SymbolRhs> {
                const auto& expr = program[ref];
                return simplifyGetAttr(
                    program, defBySymbol, expr.attr, expr.value);
            },
            [](const auto&) -> std::optional<SymbolRhs> {
                return std::nullopt;
            });
    }

    void replaceValue(
        Value& value, const std::string& oldName, const Value& replacement)
    {
        if (const auto* symbol = std::get_if<Symbol>(&value);
            symbol != nullptr && symbol->spelling == oldName) {
            value = replacement;
        }
    }

    [[nodiscard]] DefMap collectDefinitions(
        const Program& program, const FunctionDef& function)
    {
        DefMap result;
        for (const auto blockRef : function.blocks) {
            for (const auto& statement : program[blockRef].statements) {
                const auto* defRef = std::get_if<Ref<SymbolDef>>(&statement);
                if (defRef == nullptr) {
                    continue;
                }
                result.insert_or_assign(
                    program[*defRef].symbol.spelling, *defRef);
            }
        }
        return result;
    }

    void replacePointwiseNodeValues(Program& program,
        Ref<PointwiseNode> nodeRef, const std::string& oldName,
        const Value& replacement)
    {
        auto& node = program[nodeRef];
        MATCH(node.kind)
        WITH(
            [&](PointwiseLeaf& leaf) -> void {
                replaceValue(leaf.value, oldName, replacement);
            },
            [&](PointwiseBinary& binary) -> void {
                replacePointwiseNodeValues(
                    program, binary.lhs, oldName, replacement);
                replacePointwiseNodeValues(
                    program, binary.rhs, oldName, replacement);
            });
    }

    void replaceRhsValues(Program& program, SymbolRhs& rhs,
        const std::string& oldName, const Value& replacement)
    {
        MATCH(rhs)
        WITH([&](Ref<MemoryDeclaration>) -> void { },
            [&](Ref<LoadExpr>) -> void { },
            [&](Ref<GetPointerExpr> ref) -> void {
                replaceValue(program[ref].index, oldName, replacement);
            },
            [&](Ref<GetElementPointerExpr> ref) -> void {
                replaceValue(program[ref].index, oldName, replacement);
            },
            [&](Ref<BinaryExpr> ref) -> void {
                auto& expr = program[ref];
                replaceValue(expr.lhs, oldName, replacement);
                replaceValue(expr.rhs, oldName, replacement);
            },
            [&](Ref<CallExpr> ref) -> void {
                for (auto& arg : program[ref].args) {
                    replaceValue(arg, oldName, replacement);
                }
            },
            [&](Ref<CopyExpr> ref) -> void {
                replaceValue(program[ref].value, oldName, replacement);
            },
            [&](Ref<GetAttrExpr> ref) -> void {
                replaceValue(program[ref].value, oldName, replacement);
            },
            [&](Ref<SetAttrExpr> ref) -> void {
                auto& expr = program[ref];
                replaceValue(expr.value, oldName, replacement);
                replaceValue(expr.attrValue, oldName, replacement);
            },
            [&](Ref<SelectExpr> ref) -> void {
                auto& expr = program[ref];
                replaceValue(expr.condition, oldName, replacement);
                replaceValue(expr.trueValue, oldName, replacement);
                replaceValue(expr.falseValue, oldName, replacement);
            },
            [&](Ref<NextPow2Expr> ref) -> void {
                replaceValue(program[ref].value, oldName, replacement);
            },
            [&](Ref<NttExpr> ref) -> void {
                auto& expr = program[ref];
                replaceValue(expr.value, oldName, replacement);
                replaceValue(expr.length, oldName, replacement);
            },
            [&](Ref<PointwiseExpr> ref) -> void {
                auto& expr = program[ref];
                replaceValue(expr.length, oldName, replacement);
                replaceValue(expr.activeL, oldName, replacement);
                replaceValue(expr.activeR, oldName, replacement);
                replacePointwiseNodeValues(
                    program, expr.root, oldName, replacement);
            },
            [&](Ref<CombineExpr> ref) -> void {
                for (auto& term : program[ref].terms) {
                    replaceValue(term.value, oldName, replacement);
                    replaceValue(term.start, oldName, replacement);
                    if (term.end.has_value()) {
                        replaceValue(*term.end, oldName, replacement);
                    }
                    replaceValue(term.shift, oldName, replacement);
                    replaceValue(term.scale, oldName, replacement);
                }
            },
            [&](Ref<GetCoeffExpr> ref) -> void {
                auto& expr = program[ref];
                replaceValue(expr.value, oldName, replacement);
                replaceValue(expr.index, oldName, replacement);
            },
            [&](Ref<PolyConstructExpr> ref) -> void {
                for (auto& element : program[ref].elements) {
                    replaceValue(element, oldName, replacement);
                }
            },
            [&](Ref<ConversionExpr> ref) -> void {
                replaceValue(program[ref].value, oldName, replacement);
            });
    }

    void replaceStoreValue(
        StoreValue& value, const std::string& oldName, const Value& replacement)
    {
        if (auto* symbol = std::get_if<Symbol>(&value);
            symbol != nullptr && symbol->spelling == oldName) {
            MATCH(replacement)
            WITH([&](const Symbol& newSymbol) -> void { value = newSymbol; },
                [&](const IntegerLiteral& literal) -> void { value = literal; },
                [&](const UndefValue& undefValue) -> void {
                    value = undefValue;
                });
        }
    }

    void replaceUses(Program& program, FunctionDef& function,
        const std::string& oldName, const Value& replacement)
    {
        for (const auto blockRef : function.blocks) {
            auto& block = program[blockRef];
            for (auto& statement : block.statements) {
                MATCH(statement)
                WITH(
                    [&](Ref<SymbolDef> ref) -> void {
                        replaceRhsValues(
                            program, program[ref].rhs, oldName, replacement);
                    },
                    [&](Ref<StoreStmt> ref) -> void {
                        auto& store = program[ref];
                        replaceStoreValue(store.value, oldName, replacement);
                    },
                    [&](Ref<CallExpr> ref) -> void {
                        for (auto& arg : program[ref].args) {
                            replaceValue(arg, oldName, replacement);
                        }
                    });
            }
            MATCH(block.terminator)
            WITH(
                [&](Ref<BranchTerminator> ref) -> void {
                    auto& terminator = program[ref];
                    replaceValue(terminator.condition, oldName, replacement);
                    for (auto& arg : terminator.trueArgs) {
                        replaceValue(arg, oldName, replacement);
                    }
                    for (auto& arg : terminator.falseArgs) {
                        replaceValue(arg, oldName, replacement);
                    }
                },
                [&](Ref<JumpTerminator> ref) -> void {
                    for (auto& arg : program[ref].args) {
                        replaceValue(arg, oldName, replacement);
                    }
                },
                [&](Ref<ReturnTerminator> ref) -> void {
                    auto& terminator = program[ref];
                    if (terminator.value.has_value()) {
                        replaceValue(*terminator.value, oldName, replacement);
                    }
                });
        }
    }

    [[nodiscard]] std::vector<size_t> successors(const Program& program,
        const FunctionDef& function, size_t blockIndex,
        const std::unordered_map<std::string, size_t>& blockIndexByLabel)
    {
        const auto& block = program[function.blocks[blockIndex]];
        return MATCH(block.terminator) WITH(
            [&](Ref<BranchTerminator> ref) -> std::vector<size_t> {
                std::vector<size_t> result;
                const auto& terminator = program[ref];
                if (const auto it
                    = blockIndexByLabel.find(terminator.trueTarget.spelling);
                    it != blockIndexByLabel.end()) {
                    result.push_back(it->second);
                }
                if (const auto it
                    = blockIndexByLabel.find(terminator.falseTarget.spelling);
                    it != blockIndexByLabel.end()
                    && std::find(result.begin(), result.end(), it->second)
                        == result.end()) {
                    result.push_back(it->second);
                }
                return result;
            },
            [&](Ref<JumpTerminator> ref) -> std::vector<size_t> {
                const auto& terminator = program[ref];
                if (const auto it
                    = blockIndexByLabel.find(terminator.target.spelling);
                    it != blockIndexByLabel.end()) {
                    return { it->second };
                }
                return { };
            },
            [&](Ref<ReturnTerminator>) -> std::vector<size_t> { return { }; });
    }

    [[nodiscard]] std::vector<std::unordered_set<size_t>> dominators(
        const Program& program, const FunctionDef& function)
    {
        const size_t n = function.blocks.size();
        std::unordered_map<std::string, size_t> blockIndexByLabel;
        for (size_t i = 0; i < n; ++i) {
            blockIndexByLabel[program[function.blocks[i]].label.spelling] = i;
        }
        std::vector<std::vector<size_t>> preds(n);
        for (size_t i = 0; i < n; ++i) {
            for (const auto successor :
                successors(program, function, i, blockIndexByLabel)) {
                preds[successor].push_back(i);
            }
        }
        std::vector<std::unordered_set<size_t>> dom(n);
        for (size_t i = 0; i < n; ++i) {
            if (i == 0) {
                dom[i].insert(i);
            } else {
                for (size_t j = 0; j < n; ++j) {
                    dom[i].insert(j);
                }
            }
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t i = 1; i < n; ++i) {
                std::unordered_set<size_t> next;
                if (!preds[i].empty()) {
                    next = dom[preds[i].front()];
                    for (size_t predIndex = 1; predIndex < preds[i].size();
                        ++predIndex) {
                        std::erase_if(next, [&](size_t block) {
                            return !dom[preds[i][predIndex]].contains(block);
                        });
                    }
                }
                next.insert(i);
                if (next != dom[i]) {
                    dom[i] = std::move(next);
                    changed = true;
                }
            }
        }
        return dom;
    }

    struct DefSite {
        std::string symbol;
        size_t blockIndex = 0;
        size_t statementIndex = 0;
    };

    [[nodiscard]] bool dominates(const DefSite& lhs, size_t rhsBlock,
        size_t rhsStatement,
        const std::vector<std::unordered_set<size_t>>& dominators)
    {
        if (lhs.blockIndex == rhsBlock) {
            return lhs.statementIndex < rhsStatement;
        }
        return dominators[rhsBlock].contains(lhs.blockIndex);
    }

} // namespace

void eliminateCommonSubexpressions(Program& program, FunctionDef& function)
{
    const auto dom = dominators(program, function);
    const auto defBySymbol = collectDefinitions(program, function);
    std::unordered_map<std::string, std::vector<DefSite>> defsByKey;

    for (size_t blockIndex = 0; blockIndex < function.blocks.size();
        ++blockIndex) {
        auto& block = program[function.blocks[blockIndex]];
        for (size_t statementIndex = 0;
            statementIndex < block.statements.size(); ++statementIndex) {
            const auto* defRef = std::get_if<Ref<SymbolDef>>(
                &block.statements[statementIndex]);
            if (defRef == nullptr) {
                continue;
            }
            auto& def = program[*defRef];
            if (const auto simplified
                = simplifyRhs(program, defBySymbol, def.rhs);
                simplified.has_value()) {
                const auto oldName = def.symbol.spelling;
                if (const auto* copyRef
                    = std::get_if<Ref<CopyExpr>>(&*simplified)) {
                    replaceUses(
                        program, function, oldName, program[*copyRef].value);
                }
                def.rhs = *simplified;
            }

            const auto key = rhsKey(program, def.rhs);
            if (!key.has_value()) {
                continue;
            }

            auto& candidates = defsByKey[*key];
            auto replacement = std::find_if(
                candidates.begin(), candidates.end(), [&](const DefSite& site) {
                    return dominates(site, blockIndex, statementIndex, dom);
                });
            if (replacement != candidates.end()) {
                const auto oldName = def.symbol.spelling;
                replaceUses(program, function, oldName,
                    symbolValue(replacement->symbol));
                def.rhs = program.alloc<CopyExpr>(CopyExpr {
                    .sourcePos = def.sourcePos,
                    .value = symbolValue(replacement->symbol),
                    .annotations = { },
                });
                continue;
            }
            candidates.push_back(DefSite {
                .symbol = def.symbol.spelling,
                .blockIndex = blockIndex,
                .statementIndex = statementIndex,
            });
        }
    }

    eliminateDeadValues(program, function);
}

void eliminateCommonSubexpressions(Program& program)
{
    for (const auto& item : program.items) {
        if (const auto* functionRef = std::get_if<Ref<FunctionDef>>(&item)) {
            eliminateCommonSubexpressions(program, program[*functionRef]);
        }
    }
}

} // namespace yesod::koopa::ir
