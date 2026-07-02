#include "backend/llvm.h"

#include "koopa/ir.h"
#include "utils.h"

#include <algorithm>
#include <functional>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yesod::backend {

namespace koopa_ir = yesod::koopa::ir;

namespace {

    constexpr std::string_view POLY_TYPE = "%struct.YesodPoly";
    constexpr std::string_view PV_TYPE = "%struct.YesodPointValues";
    constexpr int32_t INF_END = 2147483647;
    constexpr int32_t MINT_MOD = 998244353;
    constexpr int32_t MONT_U = 998244351;
    constexpr int32_t MONT_R2 = 932051910;

    int32_t montgomeryReduceConst(int64_t value)
    {
        const int32_t q = static_cast<int32_t>(value) * MONT_U;
        return static_cast<int32_t>(
            (value + static_cast<int64_t>(MINT_MOD) * q) >> 32);
    }

    int32_t intToMontgomeryConst(int32_t value)
    {
        return montgomeryReduceConst(static_cast<int64_t>(value) * MONT_R2);
    }

    // ─── Minified name allocator ──────────────────────────────────────────

    class NameAllocator {
    public:
        // Characters valid for the first position, in ASCII order.
        // First-char rule: if it's a digit, the entire name must be digits.
        // If it's a letter/$/_/., the rest can be any valid char.
        // First-position chars: only `[a-zA-Z$._]` — no digits.
        static constexpr std::string_view kFirstChars
            = "$."
              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
              "_"
              "abcdefghijklmnopqrstuvwxyz";

        // Non-first-position chars: `[a-zA-Z$._0-9]`.
        static constexpr std::string_view kRestChars
            = "$.0123456789"
              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
              "_"
              "abcdefghijklmnopqrstuvwxyz";

        void reserve(const std::string& name) { m_used.insert(name); }

        bool isReserved(const std::string& name) const
        {
            return m_reservedSet.contains(name);
        }

        void reservePermanent(const std::string& name)
        {
            m_reservedSet.insert(name);
            m_used.insert(name);
        }

        std::string allocate()
        {
            if (m_next.empty()) {
                m_next = std::string(1, kFirstChars[0]); // "$"
            }
            while (m_used.count(m_next)) {
                m_next = advance(m_next);
            }
            m_used.insert(m_next);
            const std::string result = m_next;
            m_next = advance(m_next);
            return result;
        }

        bool isMinified() const { return true; }

    private:
        std::set<std::string> m_used;
        std::set<std::string> m_reservedSet;
        std::string m_next;

        static std::string advance(const std::string& name)
        {
            if (name.empty()) {
                return std::string(1, kFirstChars[0]);
            }

            const int len = static_cast<int>(name.size());

            std::string s = name;
            int i = len - 1;
            while (i >= 0) {
                const auto& charSet = (i == 0) ? kFirstChars : kRestChars;
                const char c = s[i];
                const auto pos = charSet.find(c);
                if (pos != std::string_view::npos && pos + 1 < charSet.size()) {
                    s[i] = charSet[pos + 1];
                    for (int j = i + 1; j < len; ++j) {
                        s[j] = kRestChars[0];
                    }
                    return s;
                }
                --i;
            }
            // All positions at max → move to next length
            return std::string(len + 1, kFirstChars[0]);
        }
    };

    class NullNameAllocator {
    public:
        void reserve(const std::string&) { }
        bool isReserved(const std::string&) const { return false; }
        void reservePermanent(const std::string&) { }
        std::string allocate() { return { }; }
        static constexpr bool isMinified() { return false; }
    };

    // ─── Name helpers ─────────────────────────────────────────────────────

    std::string stripPrefix(const std::string& spelling)
    {
        if (!spelling.empty() && (spelling[0] == '@' || spelling[0] == '%')) {
            return spelling.substr(1);
        }
        return spelling;
    }

    const std::string& llvmValueName(const std::string& spelling)
    {
        return spelling;
    }

    // ─── Type emission ────────────────────────────────────────────────────

    std::string emitType(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        return MATCH(type) WITH(
            [](const koopa_ir::I32Type&) -> std::string { return "i32"; },
            [](const koopa_ir::MintType&) -> std::string { return "i32"; },
            [](const koopa_ir::PolyType&) -> std::string {
                return std::string(POLY_TYPE);
            },
            [&](const yesod::Ref<koopa_ir::ArrayType> arrayRef) -> std::string {
                const auto& arr = program[arrayRef];
                return "[" + std::to_string(arr.length) + " x "
                    + emitType(arr.elementType, program) + "]";
            },
            [](const yesod::Ref<koopa_ir::PointerType>&) -> std::string {
                return "ptr";
            },
            [&](const yesod::Ref<koopa_ir::FunctionType>& funcRef)
                -> std::string {
                const auto& ft = program[funcRef];
                std::string result;
                if (ft.returnType.has_value()) {
                    result = emitType(*ft.returnType, program);
                } else {
                    result = "void";
                }
                result += "(";
                for (size_t i = 0; i < ft.paramTypes.size(); ++i) {
                    if (i != 0) {
                        result += ", ";
                    }
                    result += emitType(ft.paramTypes[i], program);
                }
                result += ")";
                return result;
            });
    }

    // ─── Value operand emission ───────────────────────────────────────────

    std::string emitValueOperand(
        const koopa_ir::Value& value, const koopa_ir::Program& /*program*/)
    {
        return MATCH(value) WITH(
            [](const koopa_ir::Symbol& sym) -> std::string {
                return llvmValueName(sym.spelling);
            },
            [](const koopa_ir::IntegerLiteral& lit) -> std::string {
                return std::to_string(lit.value);
            },
            [](const koopa_ir::UndefValue&) -> std::string { return "undef"; });
    }

    // ─── Initializer emission ─────────────────────────────────────────────

    void emitInitializer(std::ostream& output,
        const koopa_ir::Initializer& initializer, const koopa_ir::Type& type,
        const koopa_ir::Program& program, bool emitLeadingType)
    {
        if (emitLeadingType) {
            output << emitType(type, program) << " ";
        }
        MATCH(initializer)
        WITH(
            [&](const koopa_ir::IntegerLiteral& lit) {
                if (std::holds_alternative<koopa_ir::MintType>(type)) {
                    output << intToMontgomeryConst(lit.value);
                } else {
                    output << lit.value;
                }
            },
            [&](const koopa_ir::UndefValue&) { output << "undef"; },
            [&](const koopa_ir::ZeroInit&) { output << "zeroinitializer"; },
            [&](const yesod::Ref<koopa_ir::AggregateInitializer>& aggRef) {
                const auto& agg = program[aggRef];
                koopa_ir::Type elementType = koopa_ir::I32Type { };
                if (const auto* arrayRef
                    = std::get_if<yesod::Ref<koopa_ir::ArrayType>>(&type)) {
                    elementType = program[*arrayRef].elementType;
                }
                output << "[";
                for (size_t i = 0; i < agg.elements.size(); ++i) {
                    if (i != 0) {
                        output << ", ";
                    }
                    emitInitializer(
                        output, agg.elements[i], elementType, program, true);
                }
                output << "]";
            });
    }

    // ─── Symbol type / pointee tracking (string-based) ────────────────────

    using PointeeMap = std::unordered_map<std::string, std::string>;
    using ValueTypeMap = std::unordered_map<std::string, std::string>;
    struct FuncSigInfo {
        std::string retType;
        std::vector<std::string> paramTypes;
    };

    using FuncSigMap = std::unordered_map<std::string, FuncSigInfo>;

    FuncSigMap buildFuncSigMap(const koopa_ir::Program& program)
    {
        FuncSigMap sigs;
        for (const auto& item : program.items) {
            std::visit(
                [&](auto itemRef) {
                    using Item
                        = std::remove_cvref_t<decltype(program[itemRef])>;
                    if constexpr (std::same_as<Item, koopa_ir::FunctionDef>) {
                        const auto& def = program[itemRef];
                        FuncSigInfo info;
                        if (def.returnType.has_value()) {
                            info.retType = emitType(*def.returnType, program);
                        } else {
                            info.retType = "void";
                        }
                        for (const auto& pref : def.params) {
                            info.paramTypes.push_back(
                                emitType(program[pref].type, program));
                        }
                        sigs[def.name.spelling] = info;
                    } else if constexpr (std::same_as<Item,
                                             koopa_ir::FunctionDecl>) {
                        const auto& decl = program[itemRef];
                        FuncSigInfo info;
                        if (decl.returnType.has_value()) {
                            info.retType = emitType(*decl.returnType, program);
                        } else {
                            info.retType = "void";
                        }
                        for (const auto& pt : decl.paramTypes) {
                            info.paramTypes.push_back(emitType(pt, program));
                        }
                        sigs[decl.name.spelling] = info;
                    }
                },
                item);
        }
        return sigs;
    }

    std::string pointeeTypeString(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        if (const auto* pref
            = std::get_if<yesod::Ref<koopa_ir::PointerType>>(&type)) {
            return emitType(program[*pref].pointeeType, program);
        }
        return { };
    }

    std::string loadedPointerPointeeTypeString(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        if (const auto* pref
            = std::get_if<yesod::Ref<koopa_ir::PointerType>>(&type)) {
            const auto& pointerType = program[*pref];
            return emitType(pointerType.pointeeType, program);
        }
        return { };
    }

    bool typeContainsPoly(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        return MATCH(type) WITH(
            [](const koopa_ir::PolyType&) -> bool { return true; },
            [](const koopa_ir::I32Type&) -> bool { return false; },
            [](const koopa_ir::MintType&) -> bool { return false; },
            [&](const yesod::Ref<koopa_ir::ArrayType> arrayRef) -> bool {
                return typeContainsPoly(program[arrayRef].elementType, program);
            },
            [&](const yesod::Ref<koopa_ir::PointerType> ptrRef) -> bool {
                return typeContainsPoly(program[ptrRef].pointeeType, program);
            },
            [](const yesod::Ref<koopa_ir::FunctionType>&) -> bool {
                return false;
            });
    }

    struct FunctionTypeContext {
        PointeeMap pointees;
        PointeeMap loadedPointees;
        ValueTypeMap valueTypes;
        ValueTypeMap logicalTypes;
        ValueTypeMap logicalPointees;
        std::vector<std::pair<std::string, koopa_ir::Type>>
            localPolyAllocations;
        std::vector<std::pair<std::string, koopa_ir::Type>>
            globalPolyAllocations;
    };

    std::string logicalTypeOfIrType(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        if (std::holds_alternative<koopa_ir::MintType>(type)) {
            return "mint";
        }
        if (std::holds_alternative<koopa_ir::PolyType>(type)) {
            return "poly";
        }
        if (std::holds_alternative<koopa_ir::I32Type>(type)) {
            return "int";
        }
        if (std::holds_alternative<yesod::Ref<koopa_ir::PointerType>>(type)) {
            return "ptr";
        }
        return emitType(type, program);
    }

    std::string valueTypeOfValue(
        const koopa_ir::Value& value, const ValueTypeMap& valueTypes)
    {
        if (const auto* symbol = std::get_if<koopa_ir::Symbol>(&value)) {
            const auto typeIt = valueTypes.find(symbol->spelling);
            return typeIt == valueTypes.end() ? "i32" : typeIt->second;
        }
        return "i32";
    }

    std::string logicalTypeOfValue(
        const koopa_ir::Value& value, const ValueTypeMap& logicalTypes)
    {
        if (const auto* symbol = std::get_if<koopa_ir::Symbol>(&value)) {
            const auto typeIt = logicalTypes.find(symbol->spelling);
            return typeIt == logicalTypes.end() ? "int" : typeIt->second;
        }
        return "int";
    }

    std::string arrayElementTypeString(const std::string& arrayType)
    {
        const auto xpos = arrayType.find(" x ");
        if (xpos == std::string::npos) {
            return arrayType;
        }
        std::string elemType = arrayType.substr(xpos + 3);
        if (!elemType.empty() && elemType.back() == ']') {
            elemType.pop_back();
        }
        return elemType;
    }

    void recordPointerType(FunctionTypeContext& context,
        const std::string& symbol, const koopa_ir::Type& type,
        const koopa_ir::Program& program)
    {
        const std::string pointee = pointeeTypeString(type, program);
        if (!pointee.empty()) {
            context.valueTypes[symbol] = "ptr";
            context.pointees[symbol] = pointee;
            context.logicalTypes[symbol] = "ptr";
        }
    }

    void recordGlobalType(FunctionTypeContext& context,
        const koopa_ir::GlobalMemoryDef& global,
        const koopa_ir::Program& program)
    {
        context.valueTypes[global.name.spelling] = "ptr";
        context.pointees[global.name.spelling]
            = emitType(global.allocType, program);
        context.logicalTypes[global.name.spelling] = "ptr";
        context.logicalPointees[global.name.spelling]
            = logicalTypeOfIrType(global.allocType, program);
        if (typeContainsPoly(global.allocType, program)) {
            context.globalPolyAllocations.emplace_back(
                global.name.spelling, global.allocType);
        }
    }

    template <typename Parameter>
    void recordParameterType(FunctionTypeContext& context,
        const Parameter& param, const koopa_ir::Program& program)
    {
        context.valueTypes[param.symbol.spelling]
            = emitType(param.type, program);
        context.logicalTypes[param.symbol.spelling]
            = logicalTypeOfIrType(param.type, program);
        if (const auto* prefType
            = std::get_if<yesod::Ref<koopa_ir::PointerType>>(&param.type)) {
            context.pointees[param.symbol.spelling]
                = emitType(program[*prefType].pointeeType, program);
            context.logicalPointees[param.symbol.spelling]
                = logicalTypeOfIrType(program[*prefType].pointeeType, program);
        }
    }

    void recordSymbolDefType(FunctionTypeContext& context,
        const koopa_ir::SymbolDef& symbolDef, const koopa_ir::Program& program,
        const FuncSigMap& sigs)
    {
        const std::string& sname = symbolDef.symbol.spelling;
        MATCH(symbolDef.rhs)
        WITH(
            [&](const yesod::Ref<koopa_ir::MemoryDeclaration>& memRef) -> void {
                const auto& mem = program[memRef];
                context.valueTypes[sname] = "ptr";
                context.logicalTypes[sname] = "ptr";
                context.pointees[sname] = emitType(mem.allocType, program);
                context.logicalPointees[sname]
                    = logicalTypeOfIrType(mem.allocType, program);
                const std::string loadedPointee
                    = loadedPointerPointeeTypeString(mem.allocType, program);
                if (!loadedPointee.empty()) {
                    context.loadedPointees[sname] = loadedPointee;
                }
                if (typeContainsPoly(mem.allocType, program)) {
                    context.localPolyAllocations.emplace_back(
                        sname, mem.allocType);
                }
            },
            [&](const yesod::Ref<koopa_ir::LoadExpr>& loadRef) -> void {
                const auto& load = program[loadRef];
                const auto typeIt = context.pointees.find(load.source.spelling);
                context.valueTypes[sname]
                    = typeIt != context.pointees.end() ? typeIt->second : "i32";
                const auto logicalIt
                    = context.logicalPointees.find(load.source.spelling);
                context.logicalTypes[sname]
                    = logicalIt == context.logicalPointees.end()
                    ? "int"
                    : logicalIt->second;
                if (context.valueTypes[sname] == "ptr") {
                    const auto loadedPointeeIt
                        = context.loadedPointees.find(load.source.spelling);
                    if (loadedPointeeIt != context.loadedPointees.end()) {
                        context.pointees[sname] = loadedPointeeIt->second;
                    }
                }
            },
            [&](const yesod::Ref<koopa_ir::GetPointerExpr>& getPtrRef) -> void {
                const auto& getPtr = program[getPtrRef];
                const auto valueIt
                    = context.valueTypes.find(getPtr.source.spelling);
                context.valueTypes[sname] = valueIt != context.valueTypes.end()
                    ? valueIt->second
                    : "ptr";
                context.logicalTypes[sname] = "ptr";
                const auto pointeeIt
                    = context.pointees.find(getPtr.source.spelling);
                if (pointeeIt != context.pointees.end()) {
                    context.pointees[sname] = pointeeIt->second;
                }
                const auto logicalPointeeIt
                    = context.logicalPointees.find(getPtr.source.spelling);
                if (logicalPointeeIt != context.logicalPointees.end()) {
                    context.logicalPointees[sname] = logicalPointeeIt->second;
                }
            },
            [&](const yesod::Ref<koopa_ir::GetElementPointerExpr>& gelemRef)
                -> void {
                const auto& gelem = program[gelemRef];
                context.valueTypes[sname] = "ptr";
                context.logicalTypes[sname] = "ptr";
                const auto pointeeIt
                    = context.pointees.find(gelem.source.spelling);
                if (pointeeIt != context.pointees.end()) {
                    context.pointees[sname]
                        = arrayElementTypeString(pointeeIt->second);
                }
                const auto logicalPointeeIt
                    = context.logicalPointees.find(gelem.source.spelling);
                if (logicalPointeeIt != context.logicalPointees.end()) {
                    context.logicalPointees[sname]
                        = arrayElementTypeString(logicalPointeeIt->second);
                }
            },
            [&](const yesod::Ref<koopa_ir::BinaryExpr>& binRef) -> void {
                const auto& bin = program[binRef];
                context.valueTypes[sname] = "i32";
                switch (bin.op) {
                case koopa_ir::BinaryOp::eq:
                case koopa_ir::BinaryOp::ne:
                case koopa_ir::BinaryOp::gt:
                case koopa_ir::BinaryOp::lt:
                case koopa_ir::BinaryOp::ge:
                case koopa_ir::BinaryOp::le:
                case koopa_ir::BinaryOp::mod:
                case koopa_ir::BinaryOp::bitAnd:
                case koopa_ir::BinaryOp::bitOr:
                case koopa_ir::BinaryOp::bitXor:
                case koopa_ir::BinaryOp::shl:
                case koopa_ir::BinaryOp::shr:
                case koopa_ir::BinaryOp::sar:
                    context.logicalTypes[sname] = "int";
                    break;
                default:
                    context.logicalTypes[sname]
                        = logicalTypeOfValue(bin.lhs, context.logicalTypes)
                                == "mint"
                            || logicalTypeOfValue(bin.rhs, context.logicalTypes)
                                == "mint"
                        ? "mint"
                        : "int";
                    break;
                }
            },
            [&](const yesod::Ref<koopa_ir::CallExpr>& callRef) -> void {
                const auto& call = program[callRef];
                const auto sigIt = sigs.find(call.callee.spelling);
                if (sigIt != sigs.end() && sigIt->second.retType != "void") {
                    context.valueTypes[sname] = sigIt->second.retType;
                    context.logicalTypes[sname] = "int";
                } else {
                    context.valueTypes[sname] = "i32";
                    context.logicalTypes[sname] = "int";
                }
            },
            [&](const yesod::Ref<koopa_ir::CopyExpr>& copyRef) -> void {
                const auto& copy = program[copyRef];
                context.valueTypes[sname]
                    = valueTypeOfValue(copy.value, context.valueTypes);
                context.logicalTypes[sname]
                    = logicalTypeOfValue(copy.value, context.logicalTypes);
            },
            [&](const yesod::Ref<koopa_ir::GetAttrExpr>& getAttrRef) -> void {
                const auto& getAttr = program[getAttrRef];
                context.valueTypes[sname]
                    = getAttr.attr == koopa_ir::PolyAttr::base
                        || getAttr.attr == koopa_ir::PolyAttr::addr
                    ? "ptr"
                    : "i32";
                context.logicalTypes[sname]
                    = context.valueTypes[sname] == "ptr" ? "ptr" : "int";
            },
            [&](const yesod::Ref<koopa_ir::SetAttrExpr>&) -> void {
                context.valueTypes[sname] = std::string(POLY_TYPE);
                context.logicalTypes[sname] = "poly";
            },
            [&](const yesod::Ref<koopa_ir::SelectExpr>& selectRef) -> void {
                const auto& select = program[selectRef];
                context.valueTypes[sname]
                    = valueTypeOfValue(select.trueValue, context.valueTypes);
                context.logicalTypes[sname] = logicalTypeOfValue(
                    select.trueValue, context.logicalTypes);
            },
            [&](const yesod::Ref<koopa_ir::NextPow2Expr>&) -> void {
                context.valueTypes[sname] = "i32";
                context.logicalTypes[sname] = "int";
            },
            [&](const yesod::Ref<koopa_ir::NttExpr>&) -> void {
                context.valueTypes[sname] = std::string(PV_TYPE);
                context.logicalTypes[sname] = "pv";
            },
            [&](const yesod::Ref<koopa_ir::PointwiseExpr>&) -> void {
                context.valueTypes[sname] = std::string(POLY_TYPE);
                context.logicalTypes[sname] = "poly";
            },
            [&](const yesod::Ref<koopa_ir::CombineExpr>&) -> void {
                context.valueTypes[sname] = std::string(POLY_TYPE);
                context.logicalTypes[sname] = "poly";
            },
            [&](const yesod::Ref<koopa_ir::GetCoeffExpr>&) -> void {
                context.valueTypes[sname] = "i32";
                context.logicalTypes[sname] = "mint";
            },
            [&](const yesod::Ref<koopa_ir::PolyConstructExpr>&) -> void {
                context.valueTypes[sname] = std::string(POLY_TYPE);
                context.logicalTypes[sname] = "poly";
            },
            [&](const yesod::Ref<koopa_ir::ConversionExpr>& convRef) -> void {
                context.valueTypes[sname] = "i32";
                context.logicalTypes[sname]
                    = program[convRef].op == koopa_ir::ConversionOp::int2mint
                    ? "mint"
                    : "int";
            },
            [&](const auto&) -> void {
                throw std::runtime_error(
                    "LLVM backend does not support native poly/pv "
                    "pseudo-instructions");
            });
    }

    FunctionTypeContext buildFunctionTypeContext(
        const koopa_ir::FunctionDef& function, const koopa_ir::Program& program,
        const FuncSigMap& sigs)
    {
        FunctionTypeContext context;
        for (const auto& item : program.items) {
            std::visit(
                [&](auto itemRef) -> void {
                    using Item
                        = std::remove_cvref_t<decltype(program[itemRef])>;
                    if constexpr (std::same_as<Item,
                                      koopa_ir::GlobalMemoryDef>) {
                        recordGlobalType(context, program[itemRef], program);
                    }
                },
                item);
        }

        for (const auto& paramRef : function.params) {
            recordParameterType(context, program[paramRef], program);
        }

        for (const auto& blockRef : function.blocks) {
            const auto& block = program[blockRef];
            for (const auto& paramRef : block.params) {
                recordParameterType(context, program[paramRef], program);
                recordPointerType(context, program[paramRef].symbol.spelling,
                    program[paramRef].type, program);
            }
            for (const auto& stmt : block.statements) {
                std::visit(
                    [&](auto stmtRef) -> void {
                        using StmtNode
                            = std::remove_cvref_t<decltype(program[stmtRef])>;
                        if constexpr (std::same_as<StmtNode,
                                          koopa_ir::SymbolDef>) {
                            recordSymbolDefType(
                                context, program[stmtRef], program, sigs);
                        }
                    },
                    stmt);
            }
        }
        return context;
    }

    struct LlvmNameGenerator {
        NameAllocator* allocator = nullptr;
        int32_t helperTempId = 0;

        std::string nextHelperName(const std::string& /*stem*/)
        {
            if (allocator) {
                return "%" + allocator->allocate();
            }
            return "%v" + std::to_string(helperTempId++);
        }
    };

    struct MintIrEmitter {
        std::ostream& output;
        LlvmNameGenerator& names;

        std::string emitMontgomeryReduce(const std::string& value)
        {
            const std::string truncated = names.nextHelperName("mont_trunc");
            output << "  " << truncated << " = trunc i64 " << value
                   << " to i32\n";
            const std::string q = names.nextHelperName("mont_q");
            output << "  " << q << " = mul i32 " << truncated << ", " << MONT_U
                   << "\n";
            const std::string q64 = names.nextHelperName("mont_q64");
            output << "  " << q64 << " = sext i32 " << q << " to i64\n";
            const std::string mq = names.nextHelperName("mont_mq");
            output << "  " << mq << " = mul i64 " << MINT_MOD << ", " << q64
                   << "\n";
            const std::string sum = names.nextHelperName("mont_sum");
            output << "  " << sum << " = add i64 " << value << ", " << mq
                   << "\n";
            const std::string shifted = names.nextHelperName("mont_shift");
            output << "  " << shifted << " = ashr i64 " << sum << ", 32\n";
            const std::string result = names.nextHelperName("mont_reduce");
            output << "  " << result << " = trunc i64 " << shifted
                   << " to i32\n";
            return result;
        }

        std::string emitIntToMint(const std::string& value)
        {
            const std::string wide = names.nextHelperName("mint_i64");
            output << "  " << wide << " = sext i32 " << value << " to i64\n";
            const std::string scaled = names.nextHelperName("mint_r2");
            output << "  " << scaled << " = mul i64 " << wide << ", " << MONT_R2
                   << "\n";
            return emitMontgomeryReduce(scaled);
        }

        std::string emitMintToInt(const std::string& value)
        {
            const std::string wide = names.nextHelperName("mint_to_i64");
            output << "  " << wide << " = sext i32 " << value << " to i64\n";
            const std::string reduced = emitMontgomeryReduce(wide);
            const std::string isNegative = names.nextHelperName("mint_neg");
            output << "  " << isNegative << " = icmp slt i32 " << reduced
                   << ", 0\n";
            const std::string adjusted = names.nextHelperName("mint_adjusted");
            output << "  " << adjusted << " = add i32 " << reduced << ", "
                   << MINT_MOD << "\n";
            const std::string result = names.nextHelperName("mint_int");
            output << "  " << result << " = select i1 " << isNegative
                   << ", i32 " << adjusted << ", i32 " << reduced << "\n";
            return result;
        }

        std::string emitMintAddSub(
            const std::string& lhs, const std::string& rhs, bool isSub)
        {
            const std::string raw
                = names.nextHelperName(isSub ? "mint_sub" : "mint_add");
            output << "  " << raw << " = " << (isSub ? "sub" : "add") << " i32 "
                   << lhs << ", " << rhs << "\n";
            const std::string isNegative
                = names.nextHelperName("mint_fold_neg");
            output << "  " << isNegative << " = icmp slt i32 " << raw
                   << ", 0\n";
            const std::string plus2m = names.nextHelperName("mint_plus_2m");
            output << "  " << plus2m << " = add i32 " << raw << ", "
                   << (2 * MINT_MOD) << "\n";
            const std::string nonNegative = names.nextHelperName("mint_nonneg");
            output << "  " << nonNegative << " = select i1 " << isNegative
                   << ", i32 " << plus2m << ", i32 " << raw << "\n";
            const std::string atLeastMod = names.nextHelperName("mint_ge_mod");
            output << "  " << atLeastMod << " = icmp sge i32 " << nonNegative
                   << ", " << MINT_MOD << "\n";
            const std::string minusMod = names.nextHelperName("mint_minus_mod");
            output << "  " << minusMod << " = sub i32 " << nonNegative << ", "
                   << MINT_MOD << "\n";
            const std::string result = names.nextHelperName("mint_folded");
            output << "  " << result << " = select i1 " << atLeastMod
                   << ", i32 " << minusMod << ", i32 " << nonNegative << "\n";
            return result;
        }

        std::string emitMintMul(const std::string& lhs, const std::string& rhs)
        {
            const std::string lhs64 = names.nextHelperName("mint_lhs64");
            output << "  " << lhs64 << " = sext i32 " << lhs << " to i64\n";
            const std::string rhs64 = names.nextHelperName("mint_rhs64");
            output << "  " << rhs64 << " = sext i32 " << rhs << " to i64\n";
            const std::string product = names.nextHelperName("mint_product");
            output << "  " << product << " = mul i64 " << lhs64 << ", " << rhs64
                   << "\n";
            return emitMontgomeryReduce(product);
        }

        std::string emitMintPowConst(const std::string& base, int32_t exponent)
        {
            std::string result = std::to_string(301989884);
            std::string power = base;
            int32_t exp = exponent;
            while (exp > 0) {
                if ((exp & 1) != 0) {
                    result = emitMintMul(result, power);
                }
                exp >>= 1;
                if (exp > 0) {
                    power = emitMintMul(power, power);
                }
            }
            return result;
        }
    };

    // ─── Phi-edge collection ──────────────────────────────────────────────

    struct IncomingEdge {
        std::string predLabel;
        koopa_ir::Value arg;
    };

    using IncomingMap = std::unordered_map<std::string,
        std::vector<std::vector<IncomingEdge>>>;
    using BlockLabelMap = std::unordered_map<std::string, std::string>;

    std::string edgeBlockLabel(
        const std::string& predLabel, const std::string& suffix)
    {
        return "__ssa_edge_" + predLabel + "_" + suffix;
    }

    bool needsSameTargetBranchSplit(const koopa_ir::BranchTerminator& branch)
    {
        return branch.trueTarget.spelling == branch.falseTarget.spelling
            && (!branch.trueArgs.empty() || !branch.falseArgs.empty());
    }

    void collectPhiEdges(const koopa_ir::FunctionDef& function,
        const koopa_ir::Program& program, IncomingMap& incoming,
        BlockLabelMap& blockLabels)
    {
        for (const auto& blockRef : function.blocks) {
            const auto& block = program[blockRef];
            const std::string rawLabel = stripPrefix(block.label.spelling);
            const std::string label = rawLabel.empty() ? "__empty" : rawLabel;
            blockLabels[block.label.spelling] = label;
            incoming[block.label.spelling].resize(block.params.size());
        }

        for (const auto& blockRef : function.blocks) {
            const auto& block = program[blockRef];
            const std::string predLabel = blockLabels[block.label.spelling];

            MATCH(block.terminator)
            WITH(
                [&](const yesod::Ref<koopa_ir::JumpTerminator>& jumpRef) {
                    const auto& jump = program[jumpRef];
                    const std::string targetLabel = jump.target.spelling;
                    auto& slots = incoming[targetLabel];
                    const std::string edgeLabel = jump.args.empty()
                        ? predLabel
                        : edgeBlockLabel(predLabel, "jump");
                    for (size_t i = 0; i < jump.args.size() && i < slots.size();
                        ++i) {
                        slots[i].push_back(
                            IncomingEdge { edgeLabel, jump.args[i] });
                    }
                },
                [&](const yesod::Ref<koopa_ir::BranchTerminator>& brRef) {
                    const auto& br = program[brRef];
                    if (needsSameTargetBranchSplit(br)) {
                        const std::string targetLabel = br.trueTarget.spelling;
                        auto& slots = incoming[targetLabel];
                        for (size_t i = 0;
                            i < br.trueArgs.size() && i < slots.size(); ++i) {
                            slots[i].push_back(IncomingEdge {
                                edgeBlockLabel(predLabel, "true"),
                                br.trueArgs[i],
                            });
                        }
                        for (size_t i = 0;
                            i < br.falseArgs.size() && i < slots.size(); ++i) {
                            slots[i].push_back(IncomingEdge {
                                edgeBlockLabel(predLabel, "false"),
                                br.falseArgs[i],
                            });
                        }
                        return;
                    }
                    {
                        const std::string tlabel = br.trueTarget.spelling;
                        auto& tslots = incoming[tlabel];
                        for (size_t i = 0;
                            i < br.trueArgs.size() && i < tslots.size(); ++i) {
                            tslots[i].push_back(IncomingEdge {
                                edgeBlockLabel(predLabel, "true"),
                                br.trueArgs[i],
                            });
                        }
                    }
                    {
                        const std::string flabel = br.falseTarget.spelling;
                        auto& fslots = incoming[flabel];
                        for (size_t i = 0;
                            i < br.falseArgs.size() && i < fslots.size(); ++i) {
                            fslots[i].push_back(IncomingEdge {
                                edgeBlockLabel(predLabel, "false"),
                                br.falseArgs[i],
                            });
                        }
                    }
                },
                [](const yesod::Ref<koopa_ir::ReturnTerminator>&) { });
        }
    }

    using SymbolSet = std::set<std::string>;

    struct EdgeInfo {
        size_t targetIndex = 0;
        std::vector<koopa_ir::Value> args;
        std::string suffix;
    };

    struct OwnershipAnalysis {
        std::vector<SymbolSet> liveIn;
        std::vector<SymbolSet> liveOut;
        std::vector<std::vector<SymbolSet>> liveAfterStmt;
        std::vector<std::vector<EdgeInfo>> edges;
        std::unordered_map<std::string, size_t> blockIndexByLabel;
    };

    struct ArgumentPlan {
        std::vector<std::string> operands;
        std::vector<std::pair<std::string, std::string>> clones;
        SymbolSet movedValues;
    };

    struct EdgeEmissionPlan {
        ArgumentPlan args;
        SymbolSet releases;
        std::string label;
        std::string targetLabel;
    };

    using EdgePlanMap
        = std::map<std::pair<size_t, std::string>, EdgeEmissionPlan>;

    bool isOwnedTypeString(const std::string& type)
    {
        return type == POLY_TYPE || type == PV_TYPE;
    }

    void addOwnedValueUse(const koopa_ir::Value& value,
        const ValueTypeMap& valueTypes, SymbolSet& uses)
    {
        if (const auto* symbol = std::get_if<koopa_ir::Symbol>(&value)) {
            const auto typeIt = valueTypes.find(symbol->spelling);
            if (typeIt != valueTypes.end()
                && isOwnedTypeString(typeIt->second)) {
                uses.insert(symbol->spelling);
            }
        }
    }

    void addPointwiseUses(yesod::Ref<koopa_ir::PointwiseNode> nodeRef,
        const koopa_ir::Program& program, const ValueTypeMap& valueTypes,
        SymbolSet& uses)
    {
        const auto& node = program[nodeRef];
        MATCH(node.kind)
        WITH(
            [&](const koopa_ir::PointwiseLeaf& leaf) -> void {
                addOwnedValueUse(leaf.value, valueTypes, uses);
            },
            [&](const koopa_ir::PointwiseBinary& binary) -> void {
                addPointwiseUses(binary.lhs, program, valueTypes, uses);
                addPointwiseUses(binary.rhs, program, valueTypes, uses);
            });
    }

    SymbolSet symbolDefUses(const koopa_ir::SymbolDef& def,
        const koopa_ir::Program& program, const FuncSigMap& sigs,
        const ValueTypeMap& valueTypes)
    {
        SymbolSet uses;
        MATCH(def.rhs)
        WITH([](const yesod::Ref<koopa_ir::MemoryDeclaration>&) -> void { },
            [&](const yesod::Ref<koopa_ir::LoadExpr>&) -> void { },
            [&](const yesod::Ref<koopa_ir::GetPointerExpr>& gpRef) -> void {
                addOwnedValueUse(program[gpRef].index, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::GetElementPointerExpr>& geRef)
                -> void {
                addOwnedValueUse(program[geRef].index, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::BinaryExpr>& binRef) -> void {
                const auto& bin = program[binRef];
                addOwnedValueUse(bin.lhs, valueTypes, uses);
                addOwnedValueUse(bin.rhs, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::CallExpr>& callRef) -> void {
                const auto& call = program[callRef];
                const auto sigIt = sigs.find(call.callee.spelling);
                for (size_t i = 0; i < call.args.size(); ++i) {
                    const std::string argType
                        = (sigIt != sigs.end()
                              && i < sigIt->second.paramTypes.size())
                        ? sigIt->second.paramTypes[i]
                        : "i32";
                    if (isOwnedTypeString(argType)) {
                        addOwnedValueUse(call.args[i], valueTypes, uses);
                    }
                }
            },
            [&](const yesod::Ref<koopa_ir::CopyExpr>& copyRef) -> void {
                addOwnedValueUse(program[copyRef].value, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::GetAttrExpr>& getAttrRef) -> void {
                addOwnedValueUse(program[getAttrRef].value, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::SetAttrExpr>& setAttrRef) -> void {
                const auto& setAttr = program[setAttrRef];
                addOwnedValueUse(setAttr.value, valueTypes, uses);
                addOwnedValueUse(setAttr.attrValue, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::SelectExpr>& selectRef) -> void {
                const auto& select = program[selectRef];
                addOwnedValueUse(select.condition, valueTypes, uses);
                addOwnedValueUse(select.trueValue, valueTypes, uses);
                addOwnedValueUse(select.falseValue, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::NextPow2Expr>& nextPow2Ref) -> void {
                addOwnedValueUse(program[nextPow2Ref].value, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::NttExpr>& nttRef) -> void {
                const auto& ntt = program[nttRef];
                addOwnedValueUse(ntt.value, valueTypes, uses);
                addOwnedValueUse(ntt.length, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::PointwiseExpr>& pointwiseRef)
                -> void {
                const auto& pointwise = program[pointwiseRef];
                addOwnedValueUse(pointwise.length, valueTypes, uses);
                addOwnedValueUse(pointwise.activeL, valueTypes, uses);
                addOwnedValueUse(pointwise.activeR, valueTypes, uses);
                addPointwiseUses(pointwise.root, program, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::CombineExpr>& combineRef) -> void {
                for (const auto& term : program[combineRef].terms) {
                    addOwnedValueUse(term.value, valueTypes, uses);
                    addOwnedValueUse(term.start, valueTypes, uses);
                    if (term.end.has_value()) {
                        addOwnedValueUse(*term.end, valueTypes, uses);
                    }
                    addOwnedValueUse(term.shift, valueTypes, uses);
                    addOwnedValueUse(term.scale, valueTypes, uses);
                }
            },
            [&](const yesod::Ref<koopa_ir::GetCoeffExpr>& getCoeffRef) -> void {
                const auto& getCoeff = program[getCoeffRef];
                addOwnedValueUse(getCoeff.value, valueTypes, uses);
                addOwnedValueUse(getCoeff.index, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::PolyConstructExpr>& constructRef)
                -> void {
                for (const auto& element : program[constructRef].elements) {
                    addOwnedValueUse(element, valueTypes, uses);
                }
            },
            [&](const yesod::Ref<koopa_ir::ConversionExpr>& convRef) -> void {
                addOwnedValueUse(program[convRef].value, valueTypes, uses);
            });
        return uses;
    }

    SymbolSet storeUses(const koopa_ir::StoreStmt& store,
        const PointeeMap& pointees, const ValueTypeMap& valueTypes)
    {
        SymbolSet uses;
        const auto ptIt = pointees.find(store.destination.spelling);
        const std::string elemTy
            = (ptIt != pointees.end()) ? ptIt->second : "i32";
        if (elemTy == POLY_TYPE) {
            if (const auto* symbol
                = std::get_if<koopa_ir::Symbol>(&store.value)) {
                addOwnedValueUse(*symbol, valueTypes, uses);
            }
        }
        return uses;
    }

    SymbolSet callStmtUses(const koopa_ir::CallExpr& call,
        const FuncSigMap& sigs, const ValueTypeMap& valueTypes)
    {
        SymbolSet uses;
        const auto sigIt = sigs.find(call.callee.spelling);
        for (size_t i = 0; i < call.args.size(); ++i) {
            const std::string argType
                = (sigIt != sigs.end() && i < sigIt->second.paramTypes.size())
                ? sigIt->second.paramTypes[i]
                : "i32";
            if (isOwnedTypeString(argType)) {
                addOwnedValueUse(call.args[i], valueTypes, uses);
            }
        }
        return uses;
    }

    SymbolSet statementUses(const koopa_ir::Statement& statement,
        const koopa_ir::Program& program, const FuncSigMap& sigs,
        const PointeeMap& pointees, const ValueTypeMap& valueTypes)
    {
        return std::visit(
            [&](auto stmtRef) -> SymbolSet {
                using StmtNode
                    = std::remove_cvref_t<decltype(program[stmtRef])>;
                if constexpr (std::same_as<StmtNode, koopa_ir::SymbolDef>) {
                    return symbolDefUses(
                        program[stmtRef], program, sigs, valueTypes);
                } else if constexpr (std::same_as<StmtNode,
                                         koopa_ir::StoreStmt>) {
                    return storeUses(program[stmtRef], pointees, valueTypes);
                } else {
                    return callStmtUses(program[stmtRef], sigs, valueTypes);
                }
            },
            statement);
    }

    std::optional<std::string> statementDef(
        const koopa_ir::Statement& statement, const koopa_ir::Program& program,
        const ValueTypeMap& valueTypes)
    {
        if (const auto* defRef
            = std::get_if<yesod::Ref<koopa_ir::SymbolDef>>(&statement)) {
            const auto& def = program[*defRef];
            const auto typeIt = valueTypes.find(def.symbol.spelling);
            if (typeIt != valueTypes.end()
                && isOwnedTypeString(typeIt->second)) {
                return def.symbol.spelling;
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> blockParamSymbols(
        const koopa_ir::BasicBlock& block, const koopa_ir::Program& program,
        const ValueTypeMap& valueTypes)
    {
        std::vector<std::string> result;
        result.reserve(block.params.size());
        for (const auto& paramRef : block.params) {
            const auto& param = program[paramRef];
            const auto typeIt = valueTypes.find(param.symbol.spelling);
            if (typeIt != valueTypes.end()
                && isOwnedTypeString(typeIt->second)) {
                result.push_back(param.symbol.spelling);
            } else {
                result.push_back({ });
            }
        }
        return result;
    }

    void addOwnedEdgeArgs(const std::vector<koopa_ir::Value>& args,
        const koopa_ir::BasicBlock& targetBlock,
        const koopa_ir::Program& program, const ValueTypeMap& valueTypes,
        SymbolSet& live)
    {
        for (size_t i = 0; i < args.size() && i < targetBlock.params.size();
            ++i) {
            const auto& param = program[targetBlock.params[i]];
            const auto typeIt = valueTypes.find(param.symbol.spelling);
            if (typeIt != valueTypes.end()
                && isOwnedTypeString(typeIt->second)) {
                addOwnedValueUse(args[i], valueTypes, live);
            }
        }
    }

    OwnershipAnalysis analyzeOwnership(const koopa_ir::FunctionDef& function,
        const koopa_ir::Program& program, const FuncSigMap& sigs,
        const PointeeMap& pointees, const ValueTypeMap& valueTypes)
    {
        OwnershipAnalysis analysis;
        const size_t blockCount = function.blocks.size();
        analysis.liveIn.resize(blockCount);
        analysis.liveOut.resize(blockCount);
        analysis.liveAfterStmt.resize(blockCount);
        analysis.edges.resize(blockCount);

        for (size_t i = 0; i < blockCount; ++i) {
            const auto& block = program[function.blocks[i]];
            analysis.blockIndexByLabel[block.label.spelling] = i;
            analysis.liveAfterStmt[i].resize(block.statements.size());
        }

        for (size_t i = 0; i < blockCount; ++i) {
            const auto& block = program[function.blocks[i]];
            const std::string predRaw = stripPrefix(block.label.spelling);
            const std::string predLabel = predRaw.empty() ? "__empty" : predRaw;
            MATCH(block.terminator)
            WITH(
                [&](const yesod::Ref<koopa_ir::JumpTerminator>& jumpRef)
                    -> void {
                    const auto& jump = program[jumpRef];
                    const auto targetIt
                        = analysis.blockIndexByLabel.find(jump.target.spelling);
                    if (targetIt != analysis.blockIndexByLabel.end()) {
                        analysis.edges[i].push_back(EdgeInfo {
                            .targetIndex = targetIt->second,
                            .args = jump.args,
                            .suffix = jump.args.empty() ? "" : "jump",
                        });
                    }
                },
                [&](const yesod::Ref<koopa_ir::BranchTerminator>& brRef)
                    -> void {
                    const auto& branch = program[brRef];
                    const auto trueIt = analysis.blockIndexByLabel.find(
                        branch.trueTarget.spelling);
                    if (trueIt != analysis.blockIndexByLabel.end()) {
                        analysis.edges[i].push_back(EdgeInfo {
                            .targetIndex = trueIt->second,
                            .args = branch.trueArgs,
                            .suffix = "true",
                        });
                    }
                    const auto falseIt = analysis.blockIndexByLabel.find(
                        branch.falseTarget.spelling);
                    if (falseIt != analysis.blockIndexByLabel.end()) {
                        analysis.edges[i].push_back(EdgeInfo {
                            .targetIndex = falseIt->second,
                            .args = branch.falseArgs,
                            .suffix = "false",
                        });
                    }
                },
                [](const yesod::Ref<koopa_ir::ReturnTerminator>&) -> void { });
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t reverseIndex = 0; reverseIndex < blockCount;
                ++reverseIndex) {
                const size_t blockIndex = blockCount - reverseIndex - 1;
                const auto& block = program[function.blocks[blockIndex]];

                SymbolSet liveOut;
                for (const auto& edge : analysis.edges[blockIndex]) {
                    const auto& targetBlock
                        = program[function.blocks[edge.targetIndex]];
                    SymbolSet edgeLive = analysis.liveIn[edge.targetIndex];
                    for (const auto& paramName :
                        blockParamSymbols(targetBlock, program, valueTypes)) {
                        if (!paramName.empty()) {
                            edgeLive.erase(paramName);
                        }
                    }
                    addOwnedEdgeArgs(
                        edge.args, targetBlock, program, valueTypes, edgeLive);
                    liveOut.insert(edgeLive.begin(), edgeLive.end());
                }

                SymbolSet live = liveOut;
                MATCH(block.terminator)
                WITH(
                    [&](const yesod::Ref<koopa_ir::ReturnTerminator>& retRef)
                        -> void {
                        const auto& ret = program[retRef];
                        if (ret.value.has_value()) {
                            addOwnedValueUse(*ret.value, valueTypes, live);
                        }
                    },
                    [](const yesod::Ref<koopa_ir::JumpTerminator>&) -> void { },
                    [&](const yesod::Ref<koopa_ir::BranchTerminator>& brRef)
                        -> void {
                        addOwnedValueUse(
                            program[brRef].condition, valueTypes, live);
                    });

                for (size_t reverseStmt = 0;
                    reverseStmt < block.statements.size(); ++reverseStmt) {
                    const size_t stmtIndex
                        = block.statements.size() - reverseStmt - 1;
                    analysis.liveAfterStmt[blockIndex][stmtIndex] = live;
                    if (const auto def = statementDef(
                            block.statements[stmtIndex], program, valueTypes);
                        def.has_value()) {
                        live.erase(*def);
                    }
                    const auto uses = statementUses(block.statements[stmtIndex],
                        program, sigs, pointees, valueTypes);
                    live.insert(uses.begin(), uses.end());
                }

                if (analysis.liveOut[blockIndex] != liveOut
                    || analysis.liveIn[blockIndex] != live) {
                    analysis.liveOut[blockIndex] = std::move(liveOut);
                    analysis.liveIn[blockIndex] = std::move(live);
                    changed = true;
                }
            }
        }

        return analysis;
    }

    // ─── Top-level emit helpers ───────────────────────────────────────────

    void emitModuleHeader(std::ostream& output)
    {
        output << "; ModuleID = 'KoopaIR'\n";
        output << "source_filename = \"KoopaIR\"\n";
        output << "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:"
                  "64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"\n";
        output << "target triple = \"x86_64-pc-linux-gnu\"\n\n";
        output << "%struct.YesodPoly = type { ptr, ptr, i32, i32, i32 }\n";
        output << "%struct.YesodPointValues = type { ptr, i32 }\n\n";
    }

    void emitGlobalDecl(const koopa_ir::GlobalMemoryDef& global,
        const koopa_ir::Program& program, std::ostream& output)
    {
        const std::string name = stripPrefix(global.name.spelling);
        const std::string llvmType = emitType(global.allocType, program);
        output << "@" << name << " = global " << llvmType << " ";
        emitInitializer(
            output, global.initializer, global.allocType, program, false);
        output << "\n";
    }

    void emitFuncDecl(const koopa_ir::FunctionDecl& decl,
        const koopa_ir::Program& program, std::ostream& output)
    {
        const std::string name = stripPrefix(decl.name.spelling);
        std::string retType = "void";
        if (decl.returnType.has_value()) {
            retType = emitType(*decl.returnType, program);
        }
        output << "declare " << retType << " @" << name << "(";
        for (size_t i = 0; i < decl.paramTypes.size(); ++i) {
            if (i != 0) {
                output << ", ";
            }
            output << emitType(decl.paramTypes[i], program);
        }
        output << ")\n";
    }

    using NameResolver
        = std::function<std::string(const std::string& spelling)>;

    void emitFunctionSignature(const koopa_ir::FunctionDef& function,
        const koopa_ir::Program& program, const std::string& name,
        std::ostream& output, const NameResolver& resolveName = nullptr)
    {
        std::string retType = "void";
        if (function.returnType.has_value()) {
            retType = emitType(*function.returnType, program);
        }

        output << "define " << retType << " @" << name << "(";
        for (size_t i = 0; i < function.params.size(); ++i) {
            if (i != 0) {
                output << ", ";
            }
            const auto& param = program[function.params[i]];
            output << emitType(param.type, program) << " ";
            if (resolveName) {
                output << resolveName(param.symbol.spelling);
            } else {
                output << llvmValueName(param.symbol.spelling);
            }
        }
        output << ") {\n";
    }

    struct PolyFields {
        std::string coeffs;
        std::string addr;
        std::string n;
        std::string l;
        std::string r;
    };

    struct PvFields {
        std::string values;
        std::string len;
    };

    struct PolyIrEmitter {
        std::ostream& output;
        LlvmNameGenerator& names;

        void emitPolyFromFields(
            const std::string& result, const PolyFields& fields)
        {
            const std::string s0 = names.nextHelperName("poly_make");
            const std::string s1 = names.nextHelperName("poly_make");
            const std::string s2 = names.nextHelperName("poly_make");
            const std::string s3 = names.nextHelperName("poly_make");
            output << "  " << s0 << " = insertvalue " << POLY_TYPE
                   << " undef, ptr " << fields.coeffs << ", 0\n";
            output << "  " << s1 << " = insertvalue " << POLY_TYPE << " " << s0
                   << ", ptr " << fields.addr << ", 1\n";
            output << "  " << s2 << " = insertvalue " << POLY_TYPE << " " << s1
                   << ", i32 " << fields.n << ", 2\n";
            output << "  " << s3 << " = insertvalue " << POLY_TYPE << " " << s2
                   << ", i32 " << fields.l << ", 3\n";
            output << "  " << result << " = insertvalue " << POLY_TYPE << " "
                   << s3 << ", i32 " << fields.r << ", 4\n";
        }

        void emitPvFromFields(const std::string& result, const PvFields& fields)
        {
            const std::string s0 = names.nextHelperName("pv_make");
            output << "  " << s0 << " = insertvalue " << PV_TYPE
                   << " undef, ptr " << fields.values << ", 0\n";
            output << "  " << result << " = insertvalue " << PV_TYPE << " "
                   << s0 << ", i32 " << fields.len << ", 1\n";
        }

        PolyFields emitPolyFields(const std::string& value)
        {
            PolyFields fields {
                .coeffs = names.nextHelperName("poly_coeffs"),
                .addr = names.nextHelperName("poly_addr"),
                .n = names.nextHelperName("poly_n"),
                .l = names.nextHelperName("poly_l"),
                .r = names.nextHelperName("poly_r"),
            };
            output << "  " << fields.coeffs << " = extractvalue " << POLY_TYPE
                   << " " << value << ", 0\n";
            output << "  " << fields.addr << " = extractvalue " << POLY_TYPE
                   << " " << value << ", 1\n";
            output << "  " << fields.n << " = extractvalue " << POLY_TYPE << " "
                   << value << ", 2\n";
            output << "  " << fields.l << " = extractvalue " << POLY_TYPE << " "
                   << value << ", 3\n";
            output << "  " << fields.r << " = extractvalue " << POLY_TYPE << " "
                   << value << ", 4\n";
            return fields;
        }

        PvFields emitPvFields(const std::string& value)
        {
            PvFields fields {
                .values = names.nextHelperName("pv_values"),
                .len = names.nextHelperName("pv_len"),
            };
            output << "  " << fields.values << " = extractvalue " << PV_TYPE
                   << " " << value << ", 0\n";
            output << "  " << fields.len << " = extractvalue " << PV_TYPE << " "
                   << value << ", 1\n";
            return fields;
        }

        std::string emitPolyCoeffPtrForAddr(const std::string& addr,
            const std::string& l, const std::string& isEmpty)
        {
            const std::string negL = names.nextHelperName("poly_neg_l");
            output << "  " << negL << " = sub i32 0, " << l << "\n";
            const std::string raw
                = names.nextHelperName("poly_coeffs_from_addr");
            output << "  " << raw << " = getelementptr i32, ptr " << addr
                   << ", i32 " << negL << "\n";
            const std::string coeffs
                = names.nextHelperName("poly_coeffs_select");
            output << "  " << coeffs << " = select i1 " << isEmpty
                   << ", ptr null, ptr " << raw << "\n";
            return coeffs;
        }

        void emitPolyCloneTo(
            const std::string& result, const std::string& value)
        {
            const PolyFields input = emitPolyFields(value);
            const std::string clonedAddr
                = names.nextHelperName("poly_clone_addr");
            output << "  " << clonedAddr
                   << " = call ptr @__yesod_poly_clone_data(ptr " << input.addr
                   << ", i32 " << input.n << ")\n";
            const std::string inputCoeffsInt
                = names.nextHelperName("poly_coeff_i");
            const std::string inputAddrInt
                = names.nextHelperName("poly_addr_i");
            const std::string byteOffset
                = names.nextHelperName("poly_byte_offset");
            const std::string elemOffset
                = names.nextHelperName("poly_elem_offset");
            const std::string clonedCoeffs
                = names.nextHelperName("poly_clone_coeffs");
            output << "  " << inputCoeffsInt << " = ptrtoint ptr "
                   << input.coeffs << " to i64\n";
            output << "  " << inputAddrInt << " = ptrtoint ptr " << input.addr
                   << " to i64\n";
            output << "  " << byteOffset << " = sub i64 " << inputCoeffsInt
                   << ", " << inputAddrInt << "\n";
            output << "  " << elemOffset << " = sdiv i64 " << byteOffset
                   << ", 4\n";
            output << "  " << clonedCoeffs << " = getelementptr i32, ptr "
                   << clonedAddr << ", i64 " << elemOffset << "\n";
            emitPolyFromFields(result,
                PolyFields { .coeffs = clonedCoeffs,
                    .addr = clonedAddr,
                    .n = input.n,
                    .l = input.l,
                    .r = input.r });
        }

        std::string emitPolyCloneValue(const std::string& value)
        {
            const std::string result = names.nextHelperName("poly_clone");
            emitPolyCloneTo(result, value);
            return result;
        }

        void emitPolyZeroTo(const std::string& result)
        {
            const std::string s0 = names.nextHelperName("poly_zero");
            const std::string s1 = names.nextHelperName("poly_zero");
            const std::string s2 = names.nextHelperName("poly_zero");
            const std::string s3 = names.nextHelperName("poly_zero");
            output << "  " << s0 << " = insertvalue " << POLY_TYPE
                   << " undef, ptr null, 0\n";
            output << "  " << s1 << " = insertvalue " << POLY_TYPE << " " << s0
                   << ", ptr null, 1\n";
            output << "  " << s2 << " = insertvalue " << POLY_TYPE << " " << s1
                   << ", i32 0, 2\n";
            output << "  " << s3 << " = insertvalue " << POLY_TYPE << " " << s2
                   << ", i32 0, 3\n";
            output << "  " << result << " = insertvalue " << POLY_TYPE << " "
                   << s3 << ", i32 0, 4\n";
        }
    };

    // ─── Function body emitter ────────────────────────────────────────────

    class FunctionEmitter {
    public:
        FunctionEmitter(const koopa_ir::FunctionDef& function,
            const koopa_ir::Program& program, const FuncSigMap& sigs,
            std::ostream& output, NameAllocator* alloc = nullptr)
            : m_function(function)
            , m_program(program)
            , m_sigs(sigs)
            , m_output(output)
            , m_name(stripPrefix(function.name.spelling))
            , m_typeContext(buildFunctionTypeContext(function, program, sigs))
            , m_pointees(m_typeContext.pointees)
            , m_valueTypes(m_typeContext.valueTypes)
            , m_logicalTypes(m_typeContext.logicalTypes)
            , m_logicalPointees(m_typeContext.logicalPointees)
            , m_localPolyAllocations(m_typeContext.localPolyAllocations)
            , m_globalPolyAllocations(m_typeContext.globalPolyAllocations)
            , m_alloc(alloc)
            , m_nameGenerator { alloc }
            , m_mintEmitter { m_output, m_nameGenerator }
            , m_polyEmitter { m_output, m_nameGenerator }
        {
        }

        void emit()
        {
            // ── Collect phi edges ──────────────────────────────────────────
            collectPhiEdges(m_function, m_program, m_incoming, m_blockLabels);
            // Minify block labels if needed — use a separate counter so
            // block labels are always non-numeric (llvm-cbe rejects numeric
            // labels).  We must also update the incoming edge labels to
            // match the new block label names.
            if (m_alloc) {
                BlockLabelMap oldLabels = m_blockLabels;
                // Build old-label → new-label mapping
                std::unordered_map<std::string, std::string> labelMap;
                int labelId = 0;
                for (const auto& [orig, oldLabel] : oldLabels) {
                    labelMap[oldLabel] = "L" + std::to_string(labelId++);
                }
                // Remap m_blockLabels
                BlockLabelMap minified;
                for (const auto& [orig, oldLabel] : oldLabels) {
                    minified[orig] = labelMap[oldLabel];
                    m_alloc->reservePermanent(minified[orig]);
                }
                m_blockLabels = std::move(minified);
                // Remap incoming edge predLabels
                for (auto& [targetLabel, slots] : m_incoming) {
                    for (auto& slot : slots) {
                        for (auto& edge : slot) {
                            // edge.predLabel might be a raw label or
                            // "edgeBlockLabel(oldLabel, suffix)"
                            // Reconstruct the new predLabel.
                            auto it = labelMap.find(edge.predLabel);
                            if (it != labelMap.end()) {
                                edge.predLabel = it->second;
                                m_alloc->reservePermanent(edge.predLabel);
                                continue;
                            }
                            // Check if it's an edge block label
                            const std::string prefix = "__ssa_edge_";
                            if (edge.predLabel.rfind(prefix, 0) == 0) {
                                std::string rest
                                    = edge.predLabel.substr(prefix.size());
                                // rest is "oldLabel_suffix"
                                auto uscore = rest.rfind('_');
                                if (uscore != std::string::npos) {
                                    std::string oldLabelPart
                                        = rest.substr(0, uscore);
                                    std::string suffix
                                        = rest.substr(uscore + 1);
                                    auto lit = labelMap.find(oldLabelPart);
                                    if (lit != labelMap.end()) {
                                        edge.predLabel = "__ssa_edge_"
                                            + lit->second + "_" + suffix;
                                        m_alloc->reservePermanent(
                                            edge.predLabel);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            m_ownership = analyzeOwnership(
                m_function, m_program, m_sigs, m_pointees, m_valueTypes);
            // ── Pre-allocate names for params and symbols ──────────────────
            if (m_alloc) {
                // Pre-bind all param names so resolveName finds them
                for (const auto& paramRef : m_function.params) {
                    const auto& param = m_program[paramRef];
                    const std::string minified = m_alloc->allocate();
                    m_nameCache[stripPrefix(param.symbol.spelling)] = minified;
                }
            }
            // ── Function signature ─────────────────────────────────────────
            if (m_alloc) {
                auto resolver = [this](const std::string& spelling) {
                    return resolveName(spelling);
                };
                emitFunctionSignature(
                    m_function, m_program, m_name, m_output, resolver);
            } else {
                emitFunctionSignature(m_function, m_program, m_name, m_output);
            }

            // ── Build edge plans ─────────────────────────────────────────
            buildEdgePlans();
            rewritePhiIncoming();

            // ── Emit blocks ────────────────────────────────────────────────
            emitBlocks();

            m_output << "}\n\n";
        }

    private:
        // ── State references ──────────────────────────────────────────
        const koopa_ir::FunctionDef& m_function;
        const koopa_ir::Program& m_program;
        const FuncSigMap& m_sigs;
        std::ostream& m_output;
        const std::string m_name;

        FunctionTypeContext m_typeContext;
        PointeeMap& m_pointees;
        ValueTypeMap& m_valueTypes;
        ValueTypeMap& m_logicalTypes;
        ValueTypeMap& m_logicalPointees;
        std::vector<std::pair<std::string, koopa_ir::Type>>&
            m_localPolyAllocations;
        std::vector<std::pair<std::string, koopa_ir::Type>>&
            m_globalPolyAllocations;

        NameAllocator* m_alloc;
        mutable std::unordered_map<std::string, std::string> m_nameCache;
        LlvmNameGenerator m_nameGenerator;
        MintIrEmitter m_mintEmitter;
        PolyIrEmitter m_polyEmitter;

        IncomingMap m_incoming;
        BlockLabelMap m_blockLabels;
        OwnershipAnalysis m_ownership;
        EdgePlanMap m_edgePlans;

        // ── Name resolution ──────────────────────────────────────────
        std::string resolveName(const std::string& spelling)
        {
            if (!m_alloc)
                return spelling;

            // spelling already includes @ or % prefix (or none for block
            // labels)
            const char prefix
                = (!spelling.empty()
                      && (spelling[0] == '@' || spelling[0] == '%'))
                ? spelling[0]
                : '\0';
            const std::string bare = prefix ? spelling.substr(1) : spelling;

            auto it = m_nameCache.find(bare);
            if (it != m_nameCache.end()) {
                if (prefix)
                    return prefix + it->second;
                return it->second;
            }
            std::string minified = m_alloc->allocate();
            m_nameCache[bare] = minified;
            if (prefix)
                return prefix + minified;
            return minified;
        }

        // ── Helper methods (replacing lambdas) ────────────────────────
        std::string nextHelperName(const std::string& stem)
        {
            return m_nameGenerator.nextHelperName(stem);
        }

        std::string nextLabelName(const std::string& stem)
        {
            std::string label = stripPrefix(nextHelperName(stem));
            if (!label.empty() && label[0] >= '0' && label[0] <= '9') {
                label = "L" + label;
            }
            return label;
        }

        std::string emitIntToMint(const std::string& value)
        {
            return m_mintEmitter.emitIntToMint(value);
        }

        std::string emitMintToInt(const std::string& value)
        {
            return m_mintEmitter.emitMintToInt(value);
        }

        std::string emitMintAddSub(
            const std::string& lhs, const std::string& rhs, bool isSub)
        {
            return m_mintEmitter.emitMintAddSub(lhs, rhs, isSub);
        }

        std::string emitMintMul(const std::string& lhs, const std::string& rhs)
        {
            return m_mintEmitter.emitMintMul(lhs, rhs);
        }

        std::string emitMintPowConst(const std::string& base, int32_t exponent)
        {
            return m_mintEmitter.emitMintPowConst(base, exponent);
        }

        std::string valueTypeOf(const koopa_ir::Value& value) const
        {
            if (const auto* symbol = std::get_if<koopa_ir::Symbol>(&value)) {
                const auto typeIt = m_valueTypes.find(symbol->spelling);
                return typeIt == m_valueTypes.end() ? "i32" : typeIt->second;
            }
            return "i32";
        }

        std::string logicalTypeOf(const koopa_ir::Value& value) const
        {
            if (const auto* symbol = std::get_if<koopa_ir::Symbol>(&value)) {
                const auto typeIt = m_logicalTypes.find(symbol->spelling);
                return typeIt == m_logicalTypes.end() ? "int" : typeIt->second;
            }
            return "int";
        }

        // Resolve names in Values: Symbol names get minified
        std::string emitValueOperandResolved(const koopa_ir::Value& value)
        {
            if (const auto* sym = std::get_if<koopa_ir::Symbol>(&value)) {
                return resolveName(sym->spelling);
            }
            return emitValueOperand(value, m_program);
        }

        std::string emitMintOperand(const koopa_ir::Value& value)
        {
            if (logicalTypeOf(value) == "mint") {
                return emitValueOperandResolved(value);
            }
            if (const auto* literal
                = std::get_if<koopa_ir::IntegerLiteral>(&value)) {
                return std::to_string(intToMontgomeryConst(literal->value));
            }
            return emitIntToMint(emitValueOperandResolved(value));
        }

        // ── Poly emitter delegates ────────────────────────────────────
        void emitPolyFromFields(
            const std::string& result, const PolyFields& fields)
        {
            m_polyEmitter.emitPolyFromFields(result, fields);
        }

        void emitPvFromFields(const std::string& result, const PvFields& fields)
        {
            m_polyEmitter.emitPvFromFields(result, fields);
        }

        PolyFields emitPolyFields(const std::string& value)
        {
            return m_polyEmitter.emitPolyFields(value);
        }

        PvFields emitPvFields(const std::string& value)
        {
            return m_polyEmitter.emitPvFields(value);
        }

        std::string emitPolyCoeffPtrForAddr(const std::string& addr,
            const std::string& l, const std::string& isEmpty)
        {
            return m_polyEmitter.emitPolyCoeffPtrForAddr(addr, l, isEmpty);
        }

        void emitPolyCloneTo(
            const std::string& result, const std::string& value)
        {
            m_polyEmitter.emitPolyCloneTo(result, value);
        }

        std::string emitPolyCloneValue(const std::string& value)
        {
            return m_polyEmitter.emitPolyCloneValue(value);
        }

        void emitPolyZeroTo(const std::string& result)
        {
            m_polyEmitter.emitPolyZeroTo(result);
        }

        void emitOwnedValueDropByType(
            const std::string& type, const std::string& value)
        {
            if (type == POLY_TYPE) {
                const PolyFields fields = emitPolyFields(value);
                m_output << "  call void @__yesod_rt_free_ints(ptr "
                         << fields.addr << ")\n";
            } else if (type == PV_TYPE) {
                const PvFields fields = emitPvFields(value);
                m_output << "  call void @__yesod_rt_free_ints(ptr "
                         << fields.values << ")\n";
            }
        }

        void emitOwnedValueDrop(const std::string& symbolName)
        {
            const auto typeIt = m_valueTypes.find(symbolName);
            if (typeIt == m_valueTypes.end()
                || !isOwnedTypeString(typeIt->second)) {
                return;
            }
            emitOwnedValueDropByType(typeIt->second, resolveName(symbolName));
        }

        void emitOwnedValueDrops(const SymbolSet& values)
        {
            for (const auto& value : values) {
                emitOwnedValueDrop(value);
            }
        }

        void emitStorageDrop(const koopa_ir::Type& type, const std::string& ptr)
        {
            MATCH(type)
            WITH(
                [&](const koopa_ir::PolyType&) -> void {
                    const std::string value = nextHelperName("poly_storage");
                    m_output << "  " << value << " = load " << POLY_TYPE
                             << ", ptr " << ptr << "\n";
                    emitOwnedValueDropByType(std::string(POLY_TYPE), value);
                },
                [](const koopa_ir::I32Type&) -> void { },
                [](const koopa_ir::MintType&) -> void { },
                [&](const yesod::Ref<koopa_ir::ArrayType> arrayRef) -> void {
                    const auto& arrayType = m_program[arrayRef];
                    const std::string llvmArrayType = emitType(type, m_program);
                    for (int32_t i = 0; i < arrayType.length; ++i) {
                        const std::string elemPtr
                            = nextHelperName("poly_elem_drop");
                        m_output << "  " << elemPtr << " = getelementptr "
                                 << llvmArrayType << ", ptr " << ptr
                                 << ", i32 0, i32 " << i << "\n";
                        emitStorageDrop(arrayType.elementType, elemPtr);
                    }
                },
                [](const yesod::Ref<koopa_ir::PointerType>&) -> void { },
                [](const yesod::Ref<koopa_ir::FunctionType>&) -> void { });
        }

        void emitFunctionStorageCleanup()
        {
            for (const auto& allocation : m_localPolyAllocations) {
                emitStorageDrop(
                    allocation.second, resolveName(allocation.first));
            }
            if (m_name == "main") {
                for (const auto& allocation : m_globalPolyAllocations) {
                    emitStorageDrop(
                        allocation.second, resolveName(allocation.first));
                }
            }
        }

        SymbolSet edgeReleaseSet(
            const SymbolSet& beforeTerm, const SymbolSet& edgeLive)
        {
            SymbolSet result;
            for (const auto& value : beforeTerm) {
                if (!edgeLive.contains(value)) {
                    result.insert(value);
                }
            }
            return result;
        }

        ArgumentPlan planOwnedArguments(
            const std::vector<koopa_ir::Value>& values,
            const std::vector<std::string>& types, const SymbolSet& liveAfter,
            const std::string& cloneStem)
        {
            ArgumentPlan plan;
            plan.operands.reserve(values.size());
            std::map<std::string, size_t> remainingUses;
            for (size_t i = 0; i < values.size(); ++i) {
                if (i >= types.size() || !isOwnedTypeString(types[i])) {
                    continue;
                }
                if (const auto* symbol
                    = std::get_if<koopa_ir::Symbol>(&values[i])) {
                    remainingUses[symbol->spelling] += 1;
                }
            }
            for (size_t i = 0; i < values.size(); ++i) {
                const std::string operand = emitValueOperandResolved(values[i]);
                if (i >= types.size() || !isOwnedTypeString(types[i])) {
                    plan.operands.push_back(operand);
                    continue;
                }
                const auto* symbol = std::get_if<koopa_ir::Symbol>(&values[i]);
                if (symbol == nullptr) {
                    plan.operands.push_back(operand);
                    continue;
                }
                size_t& remaining = remainingUses[symbol->spelling];
                const bool mustKeepOriginal
                    = liveAfter.contains(symbol->spelling);
                const bool needsClone = mustKeepOriginal || remaining > 1;
                if (needsClone) {
                    const std::string cloneName = nextHelperName(cloneStem);
                    plan.clones.emplace_back(cloneName, operand);
                    plan.operands.push_back(cloneName);
                } else {
                    plan.movedValues.insert(symbol->spelling);
                    plan.operands.push_back(operand);
                }
                remaining -= 1;
            }
            return plan;
        }

        void emitArgumentPlanClones(const ArgumentPlan& plan)
        {
            for (const auto& clone : plan.clones) {
                emitPolyCloneTo(clone.first, clone.second);
            }
        }

        // ── Edge plan construction ────────────────────────────────────
        void buildEdgePlans()
        {
            for (size_t predIndex = 0; predIndex < m_function.blocks.size();
                ++predIndex) {
                const auto& predBlock = m_program[m_function.blocks[predIndex]];
                const std::string predLabel
                    = m_blockLabels[predBlock.label.spelling];
                for (const auto& edge : m_ownership.edges[predIndex]) {
                    const auto& targetBlock
                        = m_program[m_function.blocks[edge.targetIndex]];
                    std::vector<std::string> targetParamTypes;
                    targetParamTypes.reserve(edge.args.size());
                    for (size_t i = 0; i < edge.args.size(); ++i) {
                        if (i < targetBlock.params.size()) {
                            targetParamTypes.push_back(
                                emitType(m_program[targetBlock.params[i]].type,
                                    m_program));
                        } else {
                            targetParamTypes.push_back("i32");
                        }
                    }

                    SymbolSet liveAfterEdge
                        = m_ownership.liveIn[edge.targetIndex];
                    for (const auto& paramName : blockParamSymbols(
                             targetBlock, m_program, m_valueTypes)) {
                        if (!paramName.empty()) {
                            liveAfterEdge.erase(paramName);
                        }
                    }

                    ArgumentPlan argPlan = planOwnedArguments(
                        edge.args, targetParamTypes, liveAfterEdge, "edge_arg");
                    SymbolSet edgeLive = liveAfterEdge;
                    for (const auto& moved : argPlan.movedValues) {
                        edgeLive.insert(moved);
                    }
                    for (const auto& clone : argPlan.clones) {
                        edgeLive.insert(clone.first);
                    }
                    SymbolSet releases = edgeReleaseSet(
                        m_ownership.liveOut[predIndex], edgeLive);
                    for (const auto& moved : argPlan.movedValues) {
                        releases.erase(moved);
                    }

                    const std::string edgeLabel = edge.suffix.empty()
                        ? predLabel
                        : edgeBlockLabel(predLabel, edge.suffix);
                    m_edgePlans[{ predIndex, edge.suffix }] = EdgeEmissionPlan {
                        .args = std::move(argPlan),
                        .releases = std::move(releases),
                        .label = edgeLabel,
                        .targetLabel
                        = m_blockLabels[targetBlock.label.spelling],
                    };
                }
            }
        }

        void rewritePhiIncoming()
        {
            for (const auto& blockRef : m_function.blocks) {
                const auto& block = m_program[blockRef];
                auto incomingIt = m_incoming.find(block.label.spelling);
                if (incomingIt == m_incoming.end()) {
                    continue;
                }
                for (size_t paramIndex = 0; paramIndex < block.params.size();
                    ++paramIndex) {
                    if (paramIndex >= incomingIt->second.size()) {
                        continue;
                    }
                    const auto& param = m_program[block.params[paramIndex]];
                    if (emitType(param.type, m_program) != POLY_TYPE) {
                        continue;
                    }
                    for (auto& edge : incomingIt->second[paramIndex]) {
                        for (const auto& planItem : m_edgePlans) {
                            const auto& plan = planItem.second;
                            if (plan.label == edge.predLabel
                                && paramIndex < plan.args.operands.size()) {
                                edge.arg = koopa_ir::Symbol {
                                    .sourcePos = { },
                                    .spelling = plan.args.operands[paramIndex],
                                };
                                break;
                            }
                        }
                    }
                }
            }
        }

        // ── Pointwise scalar recursion ───────────────────────────────
        std::string emitPointwiseScalar(
            yesod::Ref<koopa_ir::PointwiseNode> nodeRef,
            const std::string& index)
        {
            const auto& node = m_program[nodeRef];
            return MATCH(node.kind) WITH(
                [&](const koopa_ir::PointwiseLeaf& leaf) -> std::string {
                    if (leaf.kind == koopa_ir::PointwiseLeafKind::mint) {
                        return emitMintOperand(leaf.value);
                    }
                    const std::string pv = emitValueOperandResolved(leaf.value);
                    const std::string values = nextHelperName("pv_values");
                    m_output << "  " << values << " = extractvalue " << PV_TYPE
                             << " " << pv << ", 0\n";
                    const std::string elementPtr = nextHelperName("pv_elem");
                    m_output << "  " << elementPtr
                             << " = getelementptr i32, ptr " << values
                             << ", i32 " << index << "\n";
                    const std::string value = nextHelperName("pv_value");
                    m_output << "  " << value << " = load i32, ptr "
                             << elementPtr << "\n";
                    return value;
                },
                [&](const koopa_ir::PointwiseBinary& binary) -> std::string {
                    const std::string lhs
                        = emitPointwiseScalar(binary.lhs, index);
                    const std::string rhs
                        = emitPointwiseScalar(binary.rhs, index);
                    if (binary.op == koopa_ir::PvBinaryOp::times) {
                        return emitMintMul(lhs, rhs);
                    }
                    if (binary.op == koopa_ir::PvBinaryOp::sub) {
                        return emitMintAddSub(lhs, rhs, true);
                    }
                    if (binary.op == koopa_ir::PvBinaryOp::mul) {
                        return emitMintMul(lhs, rhs);
                    }
                    return emitMintAddSub(lhs, rhs, false);
                });
        }

        // ── Block emission ───────────────────────────────────────────
        void emitBlocks()
        {
            for (size_t blockIndex = 0; blockIndex < m_function.blocks.size();
                ++blockIndex) {
                const auto& blockRef = m_function.blocks[blockIndex];
                const auto& block = m_program[blockRef];
                const std::string llvmLabel
                    = m_blockLabels[block.label.spelling];
                m_output << llvmLabel << ":\n";

                emitBlockPrologue(blockIndex);
                emitStatements(blockIndex);

                // ── Terminator ─────────────────────────────────────────────
                MATCH(block.terminator)
                WITH(
                    [&](const yesod::Ref<koopa_ir::JumpTerminator>& jumpRef) {
                        emitTerminatorJump(
                            m_program[jumpRef], blockIndex, llvmLabel);
                    },
                    [&](const yesod::Ref<koopa_ir::BranchTerminator>& brRef) {
                        emitTerminatorBranch(
                            m_program[brRef], blockIndex, llvmLabel);
                    },
                    [&](const yesod::Ref<koopa_ir::ReturnTerminator>& retRef) {
                        emitTerminatorReturn(m_program[retRef]);
                    });
            }
        }

        void emitBlockPrologue(size_t blockIndex)
        {
            const auto& blockRef = m_function.blocks[blockIndex];
            const auto& block = m_program[blockRef];

            // Phi nodes from block parameters
            const auto& slots = m_incoming[block.label.spelling];
            for (size_t pi = 0; pi < block.params.size(); ++pi) {
                const auto& bp = m_program[block.params[pi]];
                const std::string paramType = emitType(bp.type, m_program);
                const std::string paramName = resolveName(bp.symbol.spelling);

                m_output << "  " << paramName << " = phi " << paramType;
                for (size_t ei = 0; ei < slots[pi].size(); ++ei) {
                    const auto& edge = slots[pi][ei];
                    if (ei != 0) {
                        m_output << ",";
                    }
                    m_output << " [ " << emitValueOperandResolved(edge.arg)
                             << ", %" << edge.predLabel << " ]";
                }
                m_output << "\n";
            }
            for (const auto& paramRef : block.params) {
                const auto& param = m_program[paramRef];
                const auto typeIt = m_valueTypes.find(param.symbol.spelling);
                if (typeIt != m_valueTypes.end()
                    && isOwnedTypeString(typeIt->second)
                    && !m_ownership.liveIn[blockIndex].contains(
                        param.symbol.spelling)) {
                    emitOwnedValueDrop(param.symbol.spelling);
                }
            }
            if (blockIndex == 0) {
                for (const auto& paramRef : m_function.params) {
                    const auto& param = m_program[paramRef];
                    const auto typeIt
                        = m_valueTypes.find(param.symbol.spelling);
                    if (typeIt != m_valueTypes.end()
                        && isOwnedTypeString(typeIt->second)
                        && !m_ownership.liveIn[blockIndex].contains(
                            param.symbol.spelling)) {
                        emitOwnedValueDrop(param.symbol.spelling);
                    }
                }
            }
        }

        void emitStatements(size_t blockIndex)
        {
            const auto& blockRef = m_function.blocks[blockIndex];
            const auto& block = m_program[blockRef];

            for (size_t stmtIndex = 0; stmtIndex < block.statements.size();
                ++stmtIndex) {
                const auto& stmt = block.statements[stmtIndex];
                SymbolSet movedValuesInStatement;
                std::visit(
                    [&](auto stmtRef) {
                        using StmtNode
                            = std::remove_cvref_t<decltype(m_program[stmtRef])>;
                        if constexpr (std::same_as<StmtNode,
                                          koopa_ir::SymbolDef>) {
                            const auto& sd = m_program[stmtRef];
                            const std::string& sname = sd.symbol.spelling;
                            emitSymbolDefRhs(sd, sname, blockIndex, stmtIndex,
                                movedValuesInStatement);
                        } else if constexpr (std::same_as<StmtNode,
                                                 koopa_ir::StoreStmt>) {
                            emitStoreStmtImpl(m_program[stmtRef]);
                        } else if constexpr (std::same_as<StmtNode,
                                                 koopa_ir::CallExpr>) {
                            emitCallStmtImpl(m_program[stmtRef], blockIndex,
                                stmtIndex, movedValuesInStatement);
                        }
                    },
                    stmt);
                const auto uses = statementUses(
                    stmt, m_program, m_sigs, m_pointees, m_valueTypes);
                SymbolSet valuesToDrop;
                for (const auto& use : uses) {
                    if (!m_ownership.liveAfterStmt[blockIndex][stmtIndex]
                            .contains(use)
                        && !movedValuesInStatement.contains(use)) {
                        valuesToDrop.insert(use);
                    }
                }
                const auto def = statementDef(stmt, m_program, m_valueTypes);
                if (def.has_value()
                    && !m_ownership.liveAfterStmt[blockIndex][stmtIndex]
                        .contains(*def)) {
                    valuesToDrop.insert(*def);
                }
                emitOwnedValueDrops(valuesToDrop);
            }
        }

        // ── SymbolDef rhs handler ─────────────────────────────────────
        void emitSymbolDefRhs(const koopa_ir::SymbolDef& sd,
            const std::string& sname, size_t blockIndex, size_t stmtIndex,
            SymbolSet& movedValuesInStatement)
        {
            MATCH(sd.rhs)
            WITH(
                [&](const yesod::Ref<koopa_ir::MemoryDeclaration>& memRef) {
                    const auto& mem = m_program[memRef];
                    m_output << "  " << resolveName(sname) << " = alloca "
                             << emitType(mem.allocType, m_program) << "\n";
                    if (typeContainsPoly(mem.allocType, m_program)) {
                        m_output
                            << "  store " << emitType(mem.allocType, m_program)
                            << " zeroinitializer, ptr " << resolveName(sname)
                            << "\n";
                    }
                },
                [&](const yesod::Ref<koopa_ir::LoadExpr>& loadRef) {
                    const auto& ld = m_program[loadRef];
                    const auto ptIt = m_pointees.find(ld.source.spelling);
                    const std::string elemTy
                        = (ptIt != m_pointees.end()) ? ptIt->second : "i32";
                    if (elemTy == POLY_TYPE) {
                        const std::string rawName = resolveName(sname) + "_raw";
                        m_output << "  " << rawName << " = load " << elemTy
                                 << ", ptr " << resolveName(ld.source.spelling)
                                 << "\n";
                        emitPolyCloneTo(resolveName(sname), rawName);
                    } else {
                        m_output << "  " << resolveName(sname) << " = load "
                                 << elemTy << ", ptr "
                                 << resolveName(ld.source.spelling) << "\n";
                    }
                },
                [&](const yesod::Ref<koopa_ir::GetPointerExpr>& getptrRef) {
                    const auto& gp = m_program[getptrRef];
                    const auto ptIt = m_pointees.find(gp.source.spelling);
                    const std::string gepElemTy
                        = (ptIt != m_pointees.end()) ? ptIt->second : "i32";
                    m_output << "  " << resolveName(sname)
                             << " = getelementptr " << gepElemTy << ", ptr "
                             << resolveName(gp.source.spelling) << ", i32 "
                             << emitValueOperandResolved(gp.index) << "\n";
                },
                [&](const yesod::Ref<koopa_ir::GetElementPointerExpr>&
                        gelemRef) {
                    const auto& ge = m_program[gelemRef];
                    const auto ptIt = m_pointees.find(ge.source.spelling);
                    const std::string gepTy = (ptIt != m_pointees.end())
                        ? ptIt->second
                        : "[0 x i32]";
                    m_output
                        << "  " << resolveName(sname) << " = getelementptr "
                        << gepTy << ", ptr " << resolveName(ge.source.spelling)
                        << ", i32 0, i32 " << emitValueOperandResolved(ge.index)
                        << "\n";
                },
                [&](const yesod::Ref<koopa_ir::BinaryExpr>& binRef) {
                    emitBinaryExprImpl(m_program[binRef], sname);
                },
                [&](const yesod::Ref<koopa_ir::CallExpr>& callRef) {
                    const auto& call = m_program[callRef];
                    const std::string callee
                        = stripPrefix(call.callee.spelling);
                    const auto sigIt = m_sigs.find(call.callee.spelling);
                    const std::string callRetTy = (sigIt != m_sigs.end())
                        ? sigIt->second.retType
                        : "void";
                    if (callRetTy != "void") {
                        m_valueTypes[sname] = callRetTy;
                    }
                    std::vector<std::string> argTypes;
                    argTypes.reserve(call.args.size());
                    for (size_t ai = 0; ai < call.args.size(); ++ai) {
                        argTypes.push_back(
                            (sigIt != m_sigs.end()
                                && ai < sigIt->second.paramTypes.size())
                                ? sigIt->second.paramTypes[ai]
                                : "i32");
                    }
                    const ArgumentPlan argPlan
                        = planOwnedArguments(call.args, argTypes,
                            m_ownership.liveAfterStmt[blockIndex][stmtIndex],
                            "call_arg");
                    emitArgumentPlanClones(argPlan);
                    movedValuesInStatement.insert(
                        argPlan.movedValues.begin(), argPlan.movedValues.end());
                    m_output << "  " << resolveName(sname) << " = call "
                             << callRetTy << " @" << callee << "(";
                    for (size_t ai = 0; ai < argPlan.operands.size(); ++ai) {
                        if (ai != 0) {
                            m_output << ", ";
                        }
                        m_output << argTypes[ai] << " " << argPlan.operands[ai];
                    }
                    m_output << ")\n";
                },
                [&](const yesod::Ref<koopa_ir::ConversionExpr>& convRef) {
                    const auto& conv = m_program[convRef];
                    const std::string input
                        = emitValueOperandResolved(conv.value);
                    const std::string converted
                        = conv.op == koopa_ir::ConversionOp::int2mint
                        ? emitIntToMint(input)
                        : emitMintToInt(input);
                    m_output << "  " << resolveName(sname) << " = add i32 "
                             << converted << ", 0\n";
                },
                [&](const yesod::Ref<koopa_ir::CopyExpr>& copyRef) {
                    const auto& copy = m_program[copyRef];
                    const std::string copyValueType = valueTypeOf(copy.value);
                    if (copyValueType == POLY_TYPE) {
                        emitPolyCloneTo(resolveName(sname),
                            emitValueOperandResolved(copy.value));
                    } else {
                        m_output << "  " << resolveName(sname) << " = add i32 "
                                 << emitValueOperandResolved(copy.value)
                                 << ", 0\n";
                    }
                },
                [&](const yesod::Ref<koopa_ir::GetAttrExpr>& getAttrRef) {
                    const auto& getAttr = m_program[getAttrRef];
                    const int32_t index
                        = getAttr.attr == koopa_ir::PolyAttr::base ? 0
                        : getAttr.attr == koopa_ir::PolyAttr::addr ? 1
                        : getAttr.attr == koopa_ir::PolyAttr::l    ? 3
                                                                   : 4;
                    m_output << "  " << resolveName(sname) << " = extractvalue "
                             << POLY_TYPE << " "
                             << emitValueOperandResolved(getAttr.value) << ", "
                             << index << "\n";
                },
                [&](const yesod::Ref<koopa_ir::SetAttrExpr>& setAttrRef) {
                    const auto& setAttr = m_program[setAttrRef];
                    if (const auto* symbol
                        = std::get_if<koopa_ir::Symbol>(&setAttr.value)) {
                        movedValuesInStatement.insert(symbol->spelling);
                    }
                    const std::string input
                        = emitValueOperandResolved(setAttr.value);
                    const std::string value
                        = emitValueOperandResolved(setAttr.attrValue);
                    const int32_t index
                        = setAttr.attr == koopa_ir::PolyAttr::base ? 0
                        : setAttr.attr == koopa_ir::PolyAttr::addr ? 1
                        : setAttr.attr == koopa_ir::PolyAttr::l    ? 3
                                                                   : 4;
                    const std::string valueType
                        = (setAttr.attr == koopa_ir::PolyAttr::base
                              || setAttr.attr == koopa_ir::PolyAttr::addr)
                        ? "ptr"
                        : "i32";
                    m_output << "  " << resolveName(sname) << " = insertvalue "
                             << POLY_TYPE << " " << input << ", " << valueType
                             << " " << value << ", " << index << "\n";
                },
                [&](const yesod::Ref<koopa_ir::SelectExpr>& selectRef) {
                    const auto& select = m_program[selectRef];
                    const std::string resultType
                        = valueTypeOf(select.trueValue);
                    const std::string cond = nextHelperName("select_cond");
                    m_output << "  " << cond << " = icmp ne i32 "
                             << emitValueOperandResolved(select.condition)
                             << ", 0\n";
                    const std::string raw = resultType == POLY_TYPE
                        ? nextHelperName("select_poly")
                        : resolveName(sname);
                    m_output << "  " << raw << " = select i1 " << cond << ", "
                             << resultType << " "
                             << emitValueOperandResolved(select.trueValue)
                             << ", " << resultType << " "
                             << emitValueOperandResolved(select.falseValue)
                             << "\n";
                    if (resultType == POLY_TYPE) {
                        emitPolyCloneTo(resolveName(sname), raw);
                    }
                },
                [&](const yesod::Ref<koopa_ir::NextPow2Expr>& nextPow2Ref) {
                    const auto& nextPow2 = m_program[nextPow2Ref];
                    m_output << "  " << resolveName(sname)
                             << " = call i32 @__yesod_next_pow2(i32 "
                             << emitValueOperandResolved(nextPow2.value)
                             << ")\n";
                },
                [&](const yesod::Ref<koopa_ir::NttExpr>& nttRef) {
                    const auto& ntt = m_program[nttRef];
                    const PolyFields input
                        = emitPolyFields(emitValueOperandResolved(ntt.value));
                    const std::string length
                        = emitValueOperandResolved(ntt.length);
                    const std::string values = nextHelperName("pv_ntt_values");
                    m_output << "  " << values
                             << " = call ptr @__yesod_poly_ntt_data(ptr "
                             << input.coeffs << ", i32 " << input.l << ", i32 "
                             << input.r << ", i32 " << length << ")\n";
                    emitPvFromFields(resolveName(sname),
                        PvFields { .values = values, .len = length });
                },
                [&](const yesod::Ref<koopa_ir::PointwiseExpr>& pointwiseRef) {
                    const auto& pointwise = m_program[pointwiseRef];
                    const std::string length
                        = emitValueOperandResolved(pointwise.length);
                    const std::string outputPv = nextHelperName("pv_fused");
                    const std::string outputValues
                        = nextHelperName("pv_values");
                    m_output << "  " << outputValues
                             << " = call ptr @__yesod_rt_alloc_ints(i32 "
                             << length << ")\n";
                    emitPvFromFields(outputPv,
                        PvFields { .values = outputValues, .len = length });
                    const std::string preheaderLabel
                        = nextLabelName("pointwise_preheader");
                    const std::string condLabel
                        = nextLabelName("pointwise_cond");
                    const std::string bodyLabel
                        = nextLabelName("pointwise_body");
                    const std::string endLabel = nextLabelName("pointwise_end");
                    m_output << "  br label %" << preheaderLabel << "\n";
                    m_output << preheaderLabel << ":\n";
                    m_output << "  br label %" << condLabel << "\n";
                    m_output << condLabel << ":\n";
                    const std::string index = nextHelperName("pointwise_i");
                    const std::string next = nextHelperName("pointwise_next");
                    m_output << "  " << index << " = phi i32 [ 0, %"
                             << preheaderLabel << " ], [ " << next << ", %"
                             << bodyLabel << " ]\n";
                    const std::string keep = nextHelperName("pointwise_keep");
                    m_output << "  " << keep << " = icmp slt i32 " << index
                             << ", " << length << "\n";
                    m_output << "  br i1 " << keep << ", label %" << bodyLabel
                             << ", label %" << endLabel << "\n";
                    m_output << bodyLabel << ":\n";
                    const std::string value
                        = emitPointwiseScalar(pointwise.root, index);
                    const std::string outputElement
                        = nextHelperName("pv_out_elem");
                    m_output << "  " << outputElement
                             << " = getelementptr i32, ptr " << outputValues
                             << ", i32 " << index << "\n";
                    m_output << "  store i32 " << value << ", ptr "
                             << outputElement << "\n";
                    m_output << "  " << next << " = add i32 " << index
                             << ", 1\n";
                    m_output << "  br label %" << condLabel << "\n";
                    m_output << endLabel << ":\n";
                    const std::string activeL
                        = emitValueOperandResolved(pointwise.activeL);
                    const std::string activeR
                        = emitValueOperandResolved(pointwise.activeR);
                    const std::string resultAddr
                        = nextHelperName("poly_from_pv_addr");
                    m_output
                        << "  " << resultAddr
                        << " = call ptr @__yesod_poly_from_pointwise_data(ptr "
                        << outputValues << ", i32 " << length << ", i32 "
                        << activeL << ", i32 " << activeR << ")\n";
                    const std::string activeLen
                        = nextHelperName("poly_from_pv_len");
                    m_output << "  " << activeLen << " = sub i32 " << activeR
                             << ", " << activeL << "\n";
                    const std::string isEmpty
                        = nextHelperName("poly_from_pv_empty");
                    m_output << "  " << isEmpty << " = icmp sge i32 " << activeL
                             << ", " << activeR << "\n";
                    const std::string rawN
                        = nextHelperName("poly_from_pv_n_raw");
                    m_output << "  " << rawN
                             << " = call i32 @__yesod_next_pow2(i32 "
                             << activeLen << ")\n";
                    const std::string resultN
                        = nextHelperName("poly_from_pv_n");
                    m_output << "  " << resultN << " = select i1 " << isEmpty
                             << ", i32 0, i32 " << rawN << "\n";
                    const std::string resultCoeffs
                        = emitPolyCoeffPtrForAddr(resultAddr, activeL, isEmpty);
                    emitPolyFromFields(resolveName(sname),
                        PolyFields { .coeffs = resultCoeffs,
                            .addr = resultAddr,
                            .n = resultN,
                            .l = activeL,
                            .r = activeR });
                    m_output << "  call void @__yesod_rt_free_ints(ptr "
                             << outputValues << ")\n";
                },
                [&](const yesod::Ref<koopa_ir::CombineExpr>& combineRef) {
                    emitCombineExprImpl(m_program[combineRef], sname,
                        blockIndex, stmtIndex, movedValuesInStatement);
                },
                [&](const yesod::Ref<koopa_ir::GetCoeffExpr>& getCoeffRef) {
                    const auto& getCoeff = m_program[getCoeffRef];
                    const PolyFields input = emitPolyFields(
                        emitValueOperandResolved(getCoeff.value));
                    m_output << "  " << resolveName(sname)
                             << " = call i32 @__yesod_poly_getcoeff_data(ptr "
                             << input.coeffs << ", i32 " << input.l << ", i32 "
                             << input.r << ", i32 "
                             << emitValueOperandResolved(getCoeff.index)
                             << ")\n";
                },
                [&](const yesod::Ref<koopa_ir::PolyConstructExpr>&
                        constructRef) {
                    const auto& construct = m_program[constructRef];
                    if (construct.elements.empty()) {
                        emitPolyZeroTo(resolveName(sname));
                        return;
                    }
                    const std::string count
                        = std::to_string(construct.elements.size());
                    const std::string addr
                        = nextHelperName("poly_construct_addr");
                    m_output << "  " << addr
                             << " = call ptr @__yesod_poly_alloc_zero_data(i32 "
                                "0, i32 "
                             << count << ")\n";
                    for (size_t i = 0; i < construct.elements.size(); ++i) {
                        const std::string elementPtr
                            = nextHelperName("poly_coeff_ptr");
                        m_output << "  " << elementPtr
                                 << " = getelementptr i32, ptr " << addr
                                 << ", i32 " << i << "\n";
                        const std::string element
                            = emitMintOperand(construct.elements[i]);
                        m_output << "  store i32 " << element << ", ptr "
                                 << elementPtr << "\n";
                    }
                    size_t constructN = 1;
                    while (constructN < construct.elements.size()) {
                        constructN <<= 1;
                    }
                    emitPolyFromFields(resolveName(sname),
                        PolyFields { .coeffs = addr,
                            .addr = addr,
                            .n = std::to_string(constructN),
                            .l = "0",
                            .r = count });
                },
                [&](const auto&) {
                    throw std::runtime_error(
                        "LLVM backend does not support native poly/pv "
                        "pseudo-instructions");
                });
        }

        // ── Statement implementations ─────────────────────────────────
        void emitStoreStmtImpl(const koopa_ir::StoreStmt& store)
        {
            const auto ptIt = m_pointees.find(store.destination.spelling);
            const std::string elemTy
                = (ptIt != m_pointees.end()) ? ptIt->second : "i32";
            std::string storeValue;
            std::string storeLogicalType = "int";
            MATCH(store.value)
            WITH(
                [&](const koopa_ir::Symbol& sv) {
                    storeValue = resolveName(sv.spelling);
                    const auto logicalIt = m_logicalTypes.find(sv.spelling);
                    storeLogicalType = logicalIt == m_logicalTypes.end()
                        ? "int"
                        : logicalIt->second;
                },
                [&](const koopa_ir::IntegerLiteral& sv) {
                    storeValue = std::to_string(sv.value);
                },
                [&](const koopa_ir::UndefValue&) { storeValue = "undef"; },
                [&](const koopa_ir::ZeroInit&) {
                    storeValue = "zeroinitializer";
                },
                [&](const yesod::Ref<koopa_ir::AggregateInitializer>&) {
                    storeValue = "zeroinitializer";
                });
            if (elemTy == POLY_TYPE) {
                const std::string oldValue = nextHelperName("poly_store_old");
                m_output << "  " << oldValue << " = load " << POLY_TYPE
                         << ", ptr " << resolveName(store.destination.spelling)
                         << "\n";
                emitOwnedValueDropByType(std::string(POLY_TYPE), oldValue);
                storeValue = emitPolyCloneValue(storeValue);
            } else {
                const auto logicalPtIt
                    = m_logicalPointees.find(store.destination.spelling);
                const std::string logicalElemTy
                    = logicalPtIt == m_logicalPointees.end()
                    ? "int"
                    : logicalPtIt->second;
                if (elemTy == "i32" && logicalElemTy == "mint"
                    && storeLogicalType != "mint") {
                    if (storeValue != "undef"
                        && storeValue != "zeroinitializer") {
                        storeValue = emitIntToMint(storeValue);
                    }
                }
            }
            m_output << "  store " << elemTy << " " << storeValue << ", ptr "
                     << resolveName(store.destination.spelling) << "\n";
        }

        void emitCallStmtImpl(const koopa_ir::CallExpr& call, size_t blockIndex,
            size_t stmtIndex, SymbolSet& movedValuesInStatement)
        {
            const std::string callee = stripPrefix(call.callee.spelling);
            const auto sigIt = m_sigs.find(call.callee.spelling);
            const std::string callRetTy
                = (sigIt != m_sigs.end()) ? sigIt->second.retType : "void";
            std::vector<std::string> argTypes;
            argTypes.reserve(call.args.size());
            for (size_t ai = 0; ai < call.args.size(); ++ai) {
                argTypes.push_back((sigIt != m_sigs.end()
                                       && ai < sigIt->second.paramTypes.size())
                        ? sigIt->second.paramTypes[ai]
                        : "i32");
            }
            const ArgumentPlan argPlan = planOwnedArguments(call.args, argTypes,
                m_ownership.liveAfterStmt[blockIndex][stmtIndex], "call_arg");
            emitArgumentPlanClones(argPlan);
            movedValuesInStatement.insert(
                argPlan.movedValues.begin(), argPlan.movedValues.end());
            m_output << "  call " << callRetTy << " @" << callee << "(";
            for (size_t ai = 0; ai < argPlan.operands.size(); ++ai) {
                if (ai != 0) {
                    m_output << ", ";
                }
                m_output << argTypes[ai] << " " << argPlan.operands[ai];
            }
            m_output << ")\n";
        }

        // ── Binary expression (the big switch) ────────────────────────
        void emitBinaryExprImpl(
            const koopa_ir::BinaryExpr& bin, const std::string& sname)
        {
            const std::string lhs_typed
                = "i32 " + emitValueOperandResolved(bin.lhs);
            const std::string lhs_bare = emitValueOperandResolved(bin.lhs);
            const std::string rhs_bare = emitValueOperandResolved(bin.rhs);
            const bool lhsIsMint = logicalTypeOf(bin.lhs) == "mint";
            const bool rhsIsMint = logicalTypeOf(bin.rhs) == "mint";
            const bool isMintBinary = lhsIsMint || rhsIsMint;
            const std::string lhs_mint = !isMintBinary || lhsIsMint
                ? lhs_bare
                : emitIntToMint(lhs_bare);
            const std::string rhs_mint = !isMintBinary || rhsIsMint
                ? rhs_bare
                : emitIntToMint(rhs_bare);
            using BOp = koopa_ir::BinaryOp;
            switch (bin.op) {
            case BOp::add:
                if (isMintBinary) {
                    const std::string result
                        = emitMintAddSub(lhs_mint, rhs_mint, false);
                    m_output << "  " << resolveName(sname) << " = add i32 "
                             << result << ", 0\n";
                } else {
                    m_output << "  " << resolveName(sname) << " = add "
                             << lhs_typed << ", " << rhs_bare << "\n";
                }
                break;
            case BOp::sub:
                if (isMintBinary) {
                    const std::string result
                        = emitMintAddSub(lhs_mint, rhs_mint, true);
                    m_output << "  " << resolveName(sname) << " = add i32 "
                             << result << ", 0\n";
                } else {
                    m_output << "  " << resolveName(sname) << " = sub "
                             << lhs_typed << ", " << rhs_bare << "\n";
                }
                break;
            case BOp::mul:
                if (isMintBinary) {
                    const std::string result = emitMintMul(lhs_mint, rhs_mint);
                    m_output << "  " << resolveName(sname) << " = add i32 "
                             << result << ", 0\n";
                } else {
                    m_output << "  " << resolveName(sname) << " = mul "
                             << lhs_typed << ", " << rhs_bare << "\n";
                }
                break;
            case BOp::div:
                if (isMintBinary) {
                    const std::string inverse
                        = emitMintPowConst(rhs_mint, MINT_MOD - 2);
                    const std::string result = emitMintMul(lhs_mint, inverse);
                    m_output << "  " << resolveName(sname) << " = add i32 "
                             << result << ", 0\n";
                } else {
                    m_output << "  " << resolveName(sname) << " = sdiv "
                             << lhs_typed << ", " << rhs_bare << "\n";
                }
                break;
            case BOp::mod:
                m_output << "  " << resolveName(sname) << " = srem "
                         << lhs_typed << ", " << rhs_bare << "\n";
                break;
            case BOp::bitAnd:
                m_output << "  " << resolveName(sname) << " = and " << lhs_typed
                         << ", " << rhs_bare << "\n";
                break;
            case BOp::bitOr:
                m_output << "  " << resolveName(sname) << " = or " << lhs_typed
                         << ", " << rhs_bare << "\n";
                break;
            case BOp::bitXor:
                m_output << "  " << resolveName(sname) << " = xor " << lhs_typed
                         << ", " << rhs_bare << "\n";
                break;
            case BOp::shl:
                m_output << "  " << resolveName(sname) << " = shl " << lhs_typed
                         << ", " << rhs_bare << "\n";
                break;
            case BOp::shr:
                m_output << "  " << resolveName(sname) << " = lshr "
                         << lhs_typed << ", " << rhs_bare << "\n";
                break;
            case BOp::sar:
                m_output << "  " << resolveName(sname) << " = ashr "
                         << lhs_typed << ", " << rhs_bare << "\n";
                break;
            case BOp::eq: {
                const std::string cn = sname + "_cmp";
                m_output << "  " << cn << " = icmp eq " << lhs_typed << ", "
                         << rhs_bare << "\n";
                m_output << "  " << resolveName(sname) << " = zext i1 " << cn
                         << " to i32\n";
                break;
            }
            case BOp::ne: {
                const std::string cn = sname + "_cmp";
                m_output << "  " << cn << " = icmp ne " << lhs_typed << ", "
                         << rhs_bare << "\n";
                m_output << "  " << resolveName(sname) << " = zext i1 " << cn
                         << " to i32\n";
                break;
            }
            case BOp::gt: {
                const std::string cn = sname + "_cmp";
                m_output << "  " << cn << " = icmp sgt " << lhs_typed << ", "
                         << rhs_bare << "\n";
                m_output << "  " << resolveName(sname) << " = zext i1 " << cn
                         << " to i32\n";
                break;
            }
            case BOp::lt: {
                const std::string cn = sname + "_cmp";
                m_output << "  " << cn << " = icmp slt " << lhs_typed << ", "
                         << rhs_bare << "\n";
                m_output << "  " << resolveName(sname) << " = zext i1 " << cn
                         << " to i32\n";
                break;
            }
            case BOp::ge: {
                const std::string cn = sname + "_cmp";
                m_output << "  " << cn << " = icmp sge " << lhs_typed << ", "
                         << rhs_bare << "\n";
                m_output << "  " << resolveName(sname) << " = zext i1 " << cn
                         << " to i32\n";
                break;
            }
            case BOp::le: {
                const std::string cn = sname + "_cmp";
                m_output << "  " << cn << " = icmp sle " << lhs_typed << ", "
                         << rhs_bare << "\n";
                m_output << "  " << resolveName(sname) << " = zext i1 " << cn
                         << " to i32\n";
                break;
            }
            }
        }

        // ── Combine expression ───────────────────────────────────────
        void emitCombineExprImpl(const koopa_ir::CombineExpr& combine,
            const std::string& sname, size_t blockIndex, size_t stmtIndex,
            SymbolSet& movedValuesInStatement)
        {
            struct CombineTermState {
                std::string srcCoeffs;
                std::string srcAddr;
                std::string srcN;
                std::string lower;
                std::string upper;
                std::string shift;
                std::string scale;
                std::string sourceSymbol;
                bool reuseCandidate = false;
                int reuseCandidateId = -1;
            };
            std::map<std::string, int> sourceUseCounts;
            for (const auto& term : combine.terms) {
                if (const auto* symbol
                    = std::get_if<koopa_ir::Symbol>(&term.value)) {
                    ++sourceUseCounts[symbol->spelling];
                }
            }
            std::vector<CombineTermState> termStates;
            termStates.reserve(combine.terms.size());
            std::string resultL = "0";
            std::string resultR = "0";
            std::string hasAny = "false";
            int nextReuseCandidateId = 0;
            for (const auto& term : combine.terms) {
                if (const auto* scaleLiteral
                    = std::get_if<koopa_ir::IntegerLiteral>(&term.scale);
                    scaleLiteral != nullptr && scaleLiteral->value == 0) {
                    continue;
                }
                const std::string src = emitValueOperandResolved(term.value);
                const std::string srcCoeffs
                    = nextHelperName("combine_src_coeffs");
                m_output << "  " << srcCoeffs << " = extractvalue " << POLY_TYPE
                         << " " << src << ", 0\n";
                const std::string srcAddr = nextHelperName("combine_src_addr");
                m_output << "  " << srcAddr << " = extractvalue " << POLY_TYPE
                         << " " << src << ", 1\n";
                const std::string srcN = nextHelperName("combine_src_n");
                m_output << "  " << srcN << " = extractvalue " << POLY_TYPE
                         << " " << src << ", 2\n";
                const std::string srcL = nextHelperName("combine_src_l");
                m_output << "  " << srcL << " = extractvalue " << POLY_TYPE
                         << " " << src << ", 3\n";
                const std::string srcR = nextHelperName("combine_src_r");
                m_output << "  " << srcR << " = extractvalue " << POLY_TYPE
                         << " " << src << ", 4\n";
                const std::string start = emitValueOperandResolved(term.start);
                const std::string endValue = term.end.has_value()
                    ? emitValueOperandResolved(*term.end)
                    : std::to_string(INF_END);
                const std::string endBeforeSrcR
                    = nextHelperName("combine_end_before_src_r");
                m_output << "  " << endBeforeSrcR << " = icmp slt i32 "
                         << endValue << ", " << srcR << "\n";
                const std::string upper = nextHelperName("combine_upper");
                m_output << "  " << upper << " = select i1 " << endBeforeSrcR
                         << ", i32 " << endValue << ", i32 " << srcR << "\n";
                const std::string startAfterSrcL
                    = nextHelperName("combine_start_after_src_l");
                m_output << "  " << startAfterSrcL << " = icmp sgt i32 "
                         << start << ", " << srcL << "\n";
                const std::string lower = nextHelperName("combine_lower");
                m_output << "  " << lower << " = select i1 " << startAfterSrcL
                         << ", i32 " << start << ", i32 " << srcL << "\n";
                const std::string hasTerm = nextHelperName("combine_has_term");
                m_output << "  " << hasTerm << " = icmp slt i32 " << lower
                         << ", " << upper << "\n";
                const std::string shift = emitValueOperandResolved(term.shift);
                const std::string termL = nextHelperName("combine_term_l");
                m_output << "  " << termL << " = sub i32 " << lower << ", "
                         << shift << "\n";
                const std::string termR = nextHelperName("combine_term_r");
                m_output << "  " << termR << " = sub i32 " << upper << ", "
                         << shift << "\n";
                const std::string isSmallerL
                    = nextHelperName("combine_l_smaller");
                m_output << "  " << isSmallerL << " = icmp slt i32 " << termL
                         << ", " << resultL << "\n";
                const std::string minL = nextHelperName("combine_min_l");
                m_output << "  " << minL << " = select i1 " << isSmallerL
                         << ", i32 " << termL << ", i32 " << resultL << "\n";
                const std::string isLargerR
                    = nextHelperName("combine_r_larger");
                m_output << "  " << isLargerR << " = icmp sgt i32 " << termR
                         << ", " << resultR << "\n";
                const std::string maxR = nextHelperName("combine_max_r");
                m_output << "  " << maxR << " = select i1 " << isLargerR
                         << ", i32 " << termR << ", i32 " << resultR << "\n";
                const std::string mergedL = nextHelperName("combine_merged_l");
                m_output << "  " << mergedL << " = select i1 " << hasAny
                         << ", i32 " << minL << ", i32 " << termL << "\n";
                const std::string mergedR = nextHelperName("combine_merged_r");
                m_output << "  " << mergedR << " = select i1 " << hasAny
                         << ", i32 " << maxR << ", i32 " << termR << "\n";
                const std::string nextResultL
                    = nextHelperName("combine_result_l");
                m_output << "  " << nextResultL << " = select i1 " << hasTerm
                         << ", i32 " << mergedL << ", i32 " << resultL << "\n";
                const std::string nextResultR
                    = nextHelperName("combine_result_r");
                m_output << "  " << nextResultR << " = select i1 " << hasTerm
                         << ", i32 " << mergedR << ", i32 " << resultR << "\n";
                const std::string nextHasAny = nextHelperName("combine_any");
                m_output << "  " << nextHasAny << " = or i1 " << hasAny << ", "
                         << hasTerm << "\n";
                resultL = nextResultL;
                resultR = nextResultR;
                hasAny = nextHasAny;
                std::string sourceSymbol;
                bool canBeReuseCandidate = false;
                if (const auto* symbol
                    = std::get_if<koopa_ir::Symbol>(&term.value)) {
                    sourceSymbol = symbol->spelling;
                    const auto typeIt = m_valueTypes.find(sourceSymbol);
                    const auto useCountIt = sourceUseCounts.find(sourceSymbol);
                    const auto* scaleLiteral
                        = std::get_if<koopa_ir::IntegerLiteral>(&term.scale);
                    const auto* shiftLiteral
                        = std::get_if<koopa_ir::IntegerLiteral>(&term.shift);
                    canBeReuseCandidate = typeIt != m_valueTypes.end()
                        && typeIt->second == POLY_TYPE
                        && useCountIt != sourceUseCounts.end()
                        && useCountIt->second == 1 && scaleLiteral != nullptr
                        && scaleLiteral->value == 1 && shiftLiteral != nullptr
                        && shiftLiteral->value == 0
                        && !m_ownership.liveAfterStmt[blockIndex][stmtIndex]
                                .contains(sourceSymbol);
                }
                const int reuseCandidateId
                    = canBeReuseCandidate ? nextReuseCandidateId++ : -1;
                termStates.push_back(CombineTermState {
                    .srcCoeffs = srcCoeffs,
                    .srcAddr = srcAddr,
                    .srcN = srcN,
                    .lower = termL,
                    .upper = termR,
                    .shift = shift,
                    .scale = emitMintOperand(term.scale),
                    .sourceSymbol = sourceSymbol,
                    .reuseCandidate = canBeReuseCandidate,
                    .reuseCandidateId = reuseCandidateId,
                });
            }
            if (termStates.empty()) {
                emitPolyZeroTo(resolveName(sname));
                return;
            }
            const std::string resultLen = nextHelperName("combine_result_len");
            m_output << "  " << resultLen << " = sub i32 " << resultR << ", "
                     << resultL << "\n";
            const std::string resultEmpty
                = nextHelperName("combine_result_empty");
            m_output << "  " << resultEmpty << " = icmp sge i32 " << resultL
                     << ", " << resultR << "\n";
            const std::string resultRawN
                = nextHelperName("combine_result_n_raw");
            m_output << "  " << resultRawN
                     << " = call i32 @__yesod_next_pow2(i32 " << resultLen
                     << ")\n";
            const std::string resultN = nextHelperName("combine_result_n");
            m_output << "  " << resultN << " = select i1 " << resultEmpty
                     << ", i32 0, i32 " << resultRawN << "\n";
            auto emitZeroRange
                = [&](const std::string& coeffs, const std::string& begin,
                      const std::string& end) -> std::string {
                const std::string preheaderLabel
                    = nextLabelName("combine_zero_preheader");
                const std::string condLabel
                    = nextLabelName("combine_zero_cond");
                const std::string bodyLabel
                    = nextLabelName("combine_zero_body");
                const std::string endLabel = nextLabelName("combine_zero_end");
                m_output << "  br label %" << preheaderLabel << "\n";
                m_output << preheaderLabel << ":\n";
                m_output << "  br label %" << condLabel << "\n";
                m_output << condLabel << ":\n";
                const std::string index = nextHelperName("combine_zero_i");
                const std::string next = nextHelperName("combine_zero_next");
                m_output << "  " << index << " = phi i32 [ " << begin << ", %"
                         << preheaderLabel << " ], [ " << next << ", %"
                         << bodyLabel << " ]\n";
                const std::string keep = nextHelperName("combine_zero_keep");
                m_output << "  " << keep << " = icmp slt i32 " << index << ", "
                         << end << "\n";
                m_output << "  br i1 " << keep << ", label %" << bodyLabel
                         << ", label %" << endLabel << "\n";
                m_output << bodyLabel << ":\n";
                const std::string dstPtr = nextHelperName("combine_zero_dst");
                m_output << "  " << dstPtr << " = getelementptr i32, ptr "
                         << coeffs << ", i32 " << index << "\n";
                m_output << "  store i32 0, ptr " << dstPtr << "\n";
                m_output << "  " << next << " = add i32 " << index << ", 1\n";
                m_output << "  br label %" << condLabel << "\n";
                m_output << endLabel << ":\n";
                return endLabel;
            };
            std::string resultAddr;
            std::string resultCoeffs;
            std::string resultOwnedN;
            std::string reusedCandidateFlag = "false";
            std::string selectedCandidateId = "-1";
            std::vector<const CombineTermState*> reuseCandidates;
            for (const auto& term : termStates) {
                if (term.reuseCandidate) {
                    reuseCandidates.push_back(&term);
                    movedValuesInStatement.insert(term.sourceSymbol);
                }
            }
            if (!reuseCandidates.empty()) {
                const std::string resultL64
                    = nextHelperName("combine_reuse_l64");
                m_output << "  " << resultL64 << " = sext i32 " << resultL
                         << " to i64\n";
                const std::string resultR64
                    = nextHelperName("combine_reuse_r64");
                m_output << "  " << resultR64 << " = sext i32 " << resultR
                         << " to i64\n";
                const std::string notEmpty
                    = nextHelperName("combine_reuse_not_empty");
                m_output << "  " << notEmpty << " = icmp slt i32 " << resultL
                         << ", " << resultR << "\n";

                std::string bestLength = "0";
                std::string selectedAddr = reuseCandidates.front()->srcAddr;
                std::string selectedCoeffs = reuseCandidates.front()->srcCoeffs;
                std::string selectedN = reuseCandidates.front()->srcN;
                std::string selectedLower = reuseCandidates.front()->lower;
                std::string selectedUpper = reuseCandidates.front()->upper;
                for (const CombineTermState* candidate : reuseCandidates) {
                    const std::string coeffsInt
                        = nextHelperName("combine_reuse_coeffs_i");
                    const std::string addrInt
                        = nextHelperName("combine_reuse_addr_i");
                    m_output << "  " << coeffsInt << " = ptrtoint ptr "
                             << candidate->srcCoeffs << " to i64\n";
                    m_output << "  " << addrInt << " = ptrtoint ptr "
                             << candidate->srcAddr << " to i64\n";
                    const std::string byteOffset
                        = nextHelperName("combine_reuse_byte_offset");
                    m_output << "  " << byteOffset << " = sub i64 " << coeffsInt
                             << ", " << addrInt << "\n";
                    const std::string elemOffset
                        = nextHelperName("combine_reuse_elem_offset");
                    m_output << "  " << elemOffset << " = sdiv i64 "
                             << byteOffset << ", 4\n";
                    const std::string resultBeginOffset
                        = nextHelperName("combine_reuse_begin");
                    m_output << "  " << resultBeginOffset << " = add i64 "
                             << elemOffset << ", " << resultL64 << "\n";
                    const std::string resultEndOffset
                        = nextHelperName("combine_reuse_end");
                    m_output << "  " << resultEndOffset << " = add i64 "
                             << elemOffset << ", " << resultR64 << "\n";
                    const std::string srcN64
                        = nextHelperName("combine_reuse_n64");
                    m_output << "  " << srcN64 << " = sext i32 "
                             << candidate->srcN << " to i64\n";
                    const std::string beginInBounds
                        = nextHelperName("combine_reuse_begin_ok");
                    m_output << "  " << beginInBounds << " = icmp sge i64 "
                             << resultBeginOffset << ", 0\n";
                    const std::string endInBounds
                        = nextHelperName("combine_reuse_end_ok");
                    m_output << "  " << endInBounds << " = icmp sle i64 "
                             << resultEndOffset << ", " << srcN64 << "\n";
                    const std::string capacityOk
                        = nextHelperName("combine_reuse_capacity");
                    m_output << "  " << capacityOk << " = and i1 "
                             << beginInBounds << ", " << endInBounds << "\n";
                    const std::string activeLength
                        = nextHelperName("combine_reuse_active_len");
                    m_output << "  " << activeLength << " = sub i32 "
                             << candidate->upper << ", " << candidate->lower
                             << "\n";
                    const std::string hasActive
                        = nextHelperName("combine_reuse_has_active");
                    m_output << "  " << hasActive << " = icmp sgt i32 "
                             << activeLength << ", 0\n";
                    const std::string capacityAndNotEmpty
                        = nextHelperName("combine_reuse_nonempty_capacity");
                    m_output << "  " << capacityAndNotEmpty << " = and i1 "
                             << capacityOk << ", " << notEmpty << "\n";
                    const std::string canReuse
                        = nextHelperName("combine_can_reuse");
                    m_output << "  " << canReuse << " = and i1 "
                             << capacityAndNotEmpty << ", " << hasActive
                             << "\n";
                    const std::string longer
                        = nextHelperName("combine_reuse_longer");
                    m_output << "  " << longer << " = icmp sgt i32 "
                             << activeLength << ", " << bestLength << "\n";
                    const std::string shouldSelect
                        = nextHelperName("combine_reuse_select");
                    m_output << "  " << shouldSelect << " = and i1 " << canReuse
                             << ", " << longer << "\n";

                    const std::string nextSelectedId
                        = nextHelperName("combine_selected_id");
                    m_output << "  " << nextSelectedId << " = select i1 "
                             << shouldSelect << ", i32 "
                             << candidate->reuseCandidateId << ", i32 "
                             << selectedCandidateId << "\n";
                    selectedCandidateId = nextSelectedId;
                    const std::string nextBestLength
                        = nextHelperName("combine_reuse_best_len");
                    m_output << "  " << nextBestLength << " = select i1 "
                             << shouldSelect << ", i32 " << activeLength
                             << ", i32 " << bestLength << "\n";
                    bestLength = nextBestLength;

                    const std::string selectedThis
                        = nextHelperName("combine_reuse_selected_this");
                    m_output << "  " << selectedThis << " = icmp eq i32 "
                             << selectedCandidateId << ", "
                             << candidate->reuseCandidateId << "\n";
                    const std::string nextSelectedAddr
                        = nextHelperName("combine_selected_addr");
                    m_output << "  " << nextSelectedAddr << " = select i1 "
                             << selectedThis << ", ptr " << candidate->srcAddr
                             << ", ptr " << selectedAddr << "\n";
                    selectedAddr = nextSelectedAddr;
                    const std::string nextSelectedCoeffs
                        = nextHelperName("combine_selected_coeffs");
                    m_output << "  " << nextSelectedCoeffs << " = select i1 "
                             << selectedThis << ", ptr " << candidate->srcCoeffs
                             << ", ptr " << selectedCoeffs << "\n";
                    selectedCoeffs = nextSelectedCoeffs;
                    const std::string nextSelectedN
                        = nextHelperName("combine_selected_n");
                    m_output << "  " << nextSelectedN << " = select i1 "
                             << selectedThis << ", i32 " << candidate->srcN
                             << ", i32 " << selectedN << "\n";
                    selectedN = nextSelectedN;
                    const std::string nextSelectedLower
                        = nextHelperName("combine_selected_lower");
                    m_output << "  " << nextSelectedLower << " = select i1 "
                             << selectedThis << ", i32 " << candidate->lower
                             << ", i32 " << selectedLower << "\n";
                    selectedLower = nextSelectedLower;
                    const std::string nextSelectedUpper
                        = nextHelperName("combine_selected_upper");
                    m_output << "  " << nextSelectedUpper << " = select i1 "
                             << selectedThis << ", i32 " << candidate->upper
                             << ", i32 " << selectedUpper << "\n";
                    selectedUpper = nextSelectedUpper;
                }

                reusedCandidateFlag = nextHelperName("combine_reused");
                m_output << "  " << reusedCandidateFlag << " = icmp sge i32 "
                         << selectedCandidateId << ", 0\n";
                const std::string reuseLabel = nextLabelName("combine_reuse");
                const std::string allocLabel = nextLabelName("combine_alloc");
                const std::string afterLabel
                    = nextLabelName("combine_after_alloc");
                m_output << "  br i1 " << reusedCandidateFlag << ", label %"
                         << reuseLabel << ", label %" << allocLabel << "\n";
                m_output << reuseLabel << ":\n";
                std::string reuseExitLabel
                    = emitZeroRange(selectedCoeffs, resultL, selectedLower);
                reuseExitLabel
                    = emitZeroRange(selectedCoeffs, selectedUpper, resultR);
                m_output << "  br label %" << afterLabel << "\n";
                m_output << allocLabel << ":\n";
                const std::string allocAddr
                    = nextHelperName("combine_result_addr");
                m_output << "  " << allocAddr
                         << " = call ptr @__yesod_poly_alloc_zero_data(i32 "
                         << resultL << ", i32 " << resultR << ")\n";
                const std::string allocCoeffs
                    = emitPolyCoeffPtrForAddr(allocAddr, resultL, resultEmpty);
                m_output << "  br label %" << afterLabel << "\n";
                m_output << afterLabel << ":\n";
                resultAddr = nextHelperName("combine_result_addr_phi");
                resultCoeffs = nextHelperName("combine_result_coeffs_phi");
                resultOwnedN = nextHelperName("combine_result_n_phi");
                m_output << "  " << resultAddr << " = phi ptr [ "
                         << selectedAddr << ", %" << reuseExitLabel << " ], [ "
                         << allocAddr << ", %" << allocLabel << " ]\n";
                m_output << "  " << resultCoeffs << " = phi ptr [ "
                         << selectedCoeffs << ", %" << reuseExitLabel
                         << " ], [ " << allocCoeffs << ", %" << allocLabel
                         << " ]\n";
                m_output << "  " << resultOwnedN << " = phi i32 [ " << selectedN
                         << ", %" << reuseExitLabel << " ], [ " << resultN
                         << ", %" << allocLabel << " ]\n";
            } else {
                resultAddr = nextHelperName("combine_result_addr");
                m_output << "  " << resultAddr
                         << " = call ptr @__yesod_poly_alloc_zero_data(i32 "
                         << resultL << ", i32 " << resultR << ")\n";
                resultCoeffs
                    = emitPolyCoeffPtrForAddr(resultAddr, resultL, resultEmpty);
                resultOwnedN = resultN;
            }
            emitPolyFromFields(resolveName(sname),
                PolyFields { .coeffs = resultCoeffs,
                    .addr = resultAddr,
                    .n = resultOwnedN,
                    .l = resultL,
                    .r = resultR });
            const std::string montOne = std::to_string(intToMontgomeryConst(1));
            const std::string montMinusOne
                = std::to_string(intToMontgomeryConst(-1));
            for (const auto& term : termStates) {
                const std::string loopStartIsNegative
                    = nextHelperName("combine_start_neg");
                m_output << "  " << loopStartIsNegative << " = icmp slt i32 "
                         << term.lower << ", 0\n";
                const std::string loopStart
                    = nextHelperName("combine_loop_start");
                m_output << "  " << loopStart << " = select i1 "
                         << loopStartIsNegative << ", i32 0, i32 " << term.lower
                         << "\n";
                const std::string preheaderLabel
                    = nextLabelName("combine_preheader");
                const std::string condLabel = nextLabelName("combine_cond");
                const std::string bodyLabel = nextLabelName("combine_body");
                const std::string endLabel = nextLabelName("combine_end");
                if (term.reuseCandidate) {
                    const std::string termSelected
                        = nextHelperName("combine_term_selected");
                    m_output << "  " << termSelected << " = icmp eq i32 "
                             << selectedCandidateId << ", "
                             << term.reuseCandidateId << "\n";
                    m_output << "  br i1 " << termSelected << ", label %"
                             << endLabel << ", label %" << preheaderLabel
                             << "\n";
                } else {
                    m_output << "  br label %" << preheaderLabel << "\n";
                }
                m_output << preheaderLabel << ":\n";
                m_output << "  br label %" << condLabel << "\n";
                m_output << condLabel << ":\n";
                const std::string index = nextHelperName("combine_i");
                const std::string next = nextHelperName("combine_next");
                m_output << "  " << index << " = phi i32 [ " << loopStart
                         << ", %" << preheaderLabel << " ], [ " << next << ", %"
                         << bodyLabel << " ]\n";
                const std::string keep = nextHelperName("combine_keep");
                m_output << "  " << keep << " = icmp slt i32 " << index << ", "
                         << term.upper << "\n";
                m_output << "  br i1 " << keep << ", label %" << bodyLabel
                         << ", label %" << endLabel << "\n";
                m_output << bodyLabel << ":\n";
                const std::string dstPtr = nextHelperName("combine_dst");
                m_output << "  " << dstPtr << " = getelementptr i32, ptr "
                         << resultCoeffs << ", i32 " << index << "\n";
                const std::string oldValue = nextHelperName("combine_old");
                m_output << "  " << oldValue << " = load i32, ptr " << dstPtr
                         << "\n";
                const std::string srcIndex
                    = nextHelperName("combine_src_index");
                m_output << "  " << srcIndex << " = add i32 " << index << ", "
                         << term.shift << "\n";
                const std::string srcPtr = nextHelperName("combine_src");
                m_output << "  " << srcPtr << " = getelementptr i32, ptr "
                         << term.srcCoeffs << ", i32 " << srcIndex << "\n";
                const std::string srcValue = nextHelperName("combine_src_val");
                m_output << "  " << srcValue << " = load i32, ptr " << srcPtr
                         << "\n";
                std::string combined;
                if (term.scale == montOne) {
                    combined = emitMintAddSub(oldValue, srcValue, false);
                } else if (term.scale == montMinusOne) {
                    combined = emitMintAddSub(oldValue, srcValue, true);
                } else {
                    const std::string scaled
                        = emitMintMul(srcValue, term.scale);
                    combined = emitMintAddSub(oldValue, scaled, false);
                }
                m_output << "  store i32 " << combined << ", ptr " << dstPtr
                         << "\n";
                m_output << "  " << next << " = add i32 " << index << ", 1\n";
                m_output << "  br label %" << condLabel << "\n";
                m_output << endLabel << ":\n";
            }
            for (const CombineTermState& candidate : termStates) {
                if (!candidate.reuseCandidate) {
                    continue;
                }
                const std::string keepCandidate
                    = nextHelperName("combine_keep_candidate");
                m_output << "  " << keepCandidate << " = icmp eq i32 "
                         << selectedCandidateId << ", "
                         << candidate.reuseCandidateId << "\n";
                const std::string dropOldLabel
                    = nextLabelName("combine_drop_old");
                const std::string doneLabel
                    = nextLabelName("combine_drop_done");
                m_output << "  br i1 " << keepCandidate << ", label %"
                         << doneLabel << ", label %" << dropOldLabel << "\n";
                m_output << dropOldLabel << ":\n";
                m_output << "  call void @__yesod_rt_free_ints(ptr "
                         << candidate.srcAddr << ")\n";
                m_output << "  br label %" << doneLabel << "\n";
                m_output << doneLabel << ":\n";
            }
        }

        // ── Terminators ──────────────────────────────────────────────
        void emitTerminatorJump(const koopa_ir::JumpTerminator& jump,
            size_t blockIndex, const std::string& llvmLabel)
        {
            const auto planIt = m_edgePlans.find(
                { blockIndex, jump.args.empty() ? std::string("") : "jump" });
            const bool splitJump = planIt != m_edgePlans.end()
                && planIt->second.label != llvmLabel;
            if (!splitJump && planIt != m_edgePlans.end()) {
                emitArgumentPlanClones(planIt->second.args);
                emitOwnedValueDrops(planIt->second.releases);
            }
            if (splitJump) {
                m_output << "  br label %" << planIt->second.label << "\n";
                m_output << planIt->second.label << ":\n";
                emitArgumentPlanClones(planIt->second.args);
                emitOwnedValueDrops(planIt->second.releases);
            }
            m_output << "  br label %" << m_blockLabels[jump.target.spelling]
                     << "\n";
        }

        void emitTerminatorBranch(const koopa_ir::BranchTerminator& br,
            size_t blockIndex, const std::string& llvmLabel)
        {
            const auto& block = m_program[m_function.blocks[blockIndex]];
            const std::string condName
                = "_cond_bool_" + stripPrefix(block.label.spelling);
            m_output << "  %" << condName << " = icmp ne i32 "
                     << emitValueOperandResolved(br.condition) << ", 0\n";
            const auto truePlanIt = m_edgePlans.find({ blockIndex, "true" });
            const auto falsePlanIt = m_edgePlans.find({ blockIndex, "false" });
            const SymbolSet trueReleases = truePlanIt == m_edgePlans.end()
                ? SymbolSet { }
                : truePlanIt->second.releases;
            const SymbolSet falseReleases = falsePlanIt == m_edgePlans.end()
                ? SymbolSet { }
                : falsePlanIt->second.releases;
            const bool trueHasClones = truePlanIt != m_edgePlans.end()
                && !truePlanIt->second.args.clones.empty();
            const bool falseHasClones = falsePlanIt != m_edgePlans.end()
                && !falsePlanIt->second.args.clones.empty();
            const bool splitTrue = !br.trueArgs.empty() || !trueReleases.empty()
                || trueHasClones;
            const bool splitFalse = !br.falseArgs.empty()
                || !falseReleases.empty() || falseHasClones;
            if (splitTrue || splitFalse || needsSameTargetBranchSplit(br)) {
                const std::string trueEdgeLabel
                    = edgeBlockLabel(llvmLabel, "true");
                const std::string falseEdgeLabel
                    = edgeBlockLabel(llvmLabel, "false");
                m_output << "  br i1 %" << condName << ", label %";
                m_output << (splitTrue ? trueEdgeLabel
                                       : m_blockLabels[br.trueTarget.spelling]);
                m_output << ", label %";
                m_output << (splitFalse
                        ? falseEdgeLabel
                        : m_blockLabels[br.falseTarget.spelling])
                         << "\n";
                if (splitTrue) {
                    m_output << trueEdgeLabel << ":\n";
                    if (truePlanIt != m_edgePlans.end()) {
                        emitArgumentPlanClones(truePlanIt->second.args);
                    }
                    emitOwnedValueDrops(trueReleases);
                    m_output << "  br label %"
                             << m_blockLabels[br.trueTarget.spelling] << "\n";
                }
                if (splitFalse) {
                    m_output << falseEdgeLabel << ":\n";
                    if (falsePlanIt != m_edgePlans.end()) {
                        emitArgumentPlanClones(falsePlanIt->second.args);
                    }
                    emitOwnedValueDrops(falseReleases);
                    m_output << "  br label %"
                             << m_blockLabels[br.falseTarget.spelling] << "\n";
                }
                return;
            }
            m_output << "  br i1 %" << condName << ", label %"
                     << m_blockLabels[br.trueTarget.spelling] << ", label %"
                     << m_blockLabels[br.falseTarget.spelling] << "\n";
        }

        void emitTerminatorReturn(const koopa_ir::ReturnTerminator& ret)
        {
            SymbolSet beforeReturn;
            if (ret.value.has_value()) {
                addOwnedValueUse(*ret.value, m_valueTypes, beforeReturn);
            }
            SymbolSet returnReleases = beforeReturn;
            if (ret.value.has_value()) {
                if (const auto* symbol
                    = std::get_if<koopa_ir::Symbol>(&*ret.value)) {
                    returnReleases.erase(symbol->spelling);
                }
            }
            emitOwnedValueDrops(returnReleases);
            emitFunctionStorageCleanup();
            if (ret.value.has_value()) {
                const std::string valueType = valueTypeOf(*ret.value);
                m_output << "  ret " << valueType << " "
                         << emitValueOperandResolved(*ret.value) << "\n";
            } else {
                m_output << "  ret void\n";
            }
        }
    };

    void emitFunctionDef(const koopa_ir::FunctionDef& function,
        const koopa_ir::Program& program, const FuncSigMap& sigs,
        std::ostream& output, NameAllocator* alloc = nullptr)
    {
        FunctionEmitter emitter(function, program, sigs, output, alloc);
        emitter.emit();
    }

} // anonymous namespace

// ─── Public API ───────────────────────────────────────────────────────

void LlvmGenerator::generate(
    const koopa_ir::Program& program, std::ostream& output)
{
    emitModuleHeader(output);
    const FuncSigMap sigs = buildFuncSigMap(program);

    // In minified mode, build a single shared name allocator.
    NameAllocator nameAlloc;
    NameAllocator* alloc = m_minify ? &nameAlloc : nullptr;

    if (alloc) {
        // Reserve externally-visible names (function names, global names)
        // so they are not reused as minified local names.
        for (const auto& item : program.items) {
            std::visit(
                [&](auto itemRef) {
                    using Item
                        = std::remove_cvref_t<decltype(program[itemRef])>;
                    if constexpr (std::same_as<Item,
                                      koopa_ir::GlobalMemoryDef>) {
                        alloc->reservePermanent(
                            stripPrefix(program[itemRef].name.spelling));
                    } else if constexpr (std::same_as<Item,
                                             koopa_ir::FunctionDecl>) {
                        alloc->reservePermanent(
                            stripPrefix(program[itemRef].name.spelling));
                    } else if constexpr (std::same_as<Item,
                                             koopa_ir::FunctionDef>) {
                        alloc->reservePermanent(
                            stripPrefix(program[itemRef].name.spelling));
                    }
                },
                item);
        }
    }

    // Emit globals and function declarations first.
    for (const auto& item : program.items) {
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                if constexpr (std::same_as<Item, koopa_ir::GlobalMemoryDef>) {
                    emitGlobalDecl(program[itemRef], program, output);
                } else if constexpr (std::same_as<Item,
                                         koopa_ir::FunctionDecl>) {
                    emitFuncDecl(program[itemRef], program, output);
                }
            },
            item);
    }

    // Emit function definitions.
    for (const auto& item : program.items) {
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                if constexpr (std::same_as<Item, koopa_ir::FunctionDef>) {
                    emitFunctionDef(
                        program[itemRef], program, sigs, output, alloc);
                }
            },
            item);
    }
}

} // namespace yesod::backend
