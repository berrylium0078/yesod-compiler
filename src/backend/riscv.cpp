#include "backend/riscv.h"

#include "koopa/ir.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace yesod::backend {

namespace koopa_ir = yesod::koopa::ir;

bool isAllDigits(const std::string& text)
{
    for (const auto ch : text) {
        if (!std::isdigit(ch)) {
            return false;
        }
    }
    return true;
}

std::string normalizeIdentifierStem(std::string stem)
{
    if (!stem.empty() && std::isdigit(stem.front()) && !isAllDigits(stem)) {
        stem.insert(stem.begin(), '_');
    }
    return stem;
}

std::string RiscvGenerator::genLabel(std::string hint)
{
    hint = normalizeIdentifierStem(std::move(hint));
    std::string label = "_local" + hint;
    const int duplicateCount = ++m_usedLabels[label];
    if (duplicateCount > 1) {
        label += "_" + std::to_string(duplicateCount);
    }
    return label;
}

namespace {

    enum class GlobalSection {
        rodata,
        data,
        bss,
    };

    enum class StorageClass {
        global,
        alloc,
        stackValue,
    };

    struct ValueShape {
        bool isPointer = false;
        koopa_ir::Type payloadType = koopa_ir::I32Type {};
    };

    struct SymbolInfo {
        StorageClass storageClass = StorageClass::stackValue;
        ValueShape shape;
        int stackOffset = -1;
    };

    struct FunctionSignature {
        std::optional<ValueShape> returnShape;
    };

    std::string symbolName(const std::string& koopaName)
    {
        if (!koopaName.empty() && koopaName.front() == '@') {
            return koopaName.substr(1);
        }
        return koopaName;
    }

    const koopa_ir::ArrayType& requireArrayType(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        if (const auto* arrayRef = std::get_if<yesod::Ref<koopa_ir::ArrayType>>(&type)) {
            return program[*arrayRef];
        }
        throw std::runtime_error("RISC-V backend expected an array type");
    }

    int typeSize(const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        return std::visit(
            [&](auto typeAlt) -> int {
                using TypeAlt = std::remove_cvref_t<decltype(typeAlt)>;
                if constexpr (std::same_as<TypeAlt, koopa_ir::I32Type>) {
                    return 4;
                } else if constexpr (std::same_as<TypeAlt, yesod::Ref<koopa_ir::ArrayType>>) {
                    const auto& arrayType = program[typeAlt];
                    return arrayType.length * typeSize(arrayType.elementType, program);
                } else if constexpr (std::same_as<TypeAlt, yesod::Ref<koopa_ir::PointerType>>) {
                    return 4;
                } else {
                    return 0;
                }
            },
            type);
    }

    ValueShape shapeFromType(const koopa_ir::Type& type,
        const koopa_ir::Program& program)
    {
        if (const auto* pointerRef = std::get_if<yesod::Ref<koopa_ir::PointerType>>(&type)) {
            return ValueShape {
                .isPointer = true,
                .payloadType = program[*pointerRef].pointeeType,
            };
        }
        return ValueShape {
            .isPointer = false,
            .payloadType = type,
        };
    }

    bool isConstArraySymbolName(const std::string& name)
    {
        return name.rfind("c_", 0) == 0;
    }

    bool isZeroInitializer(
        const koopa_ir::Initializer& initializer, const koopa_ir::Program& program)
    {
        return std::visit(
            [&](auto initAlt) -> bool {
                using InitAlt = std::remove_cvref_t<decltype(initAlt)>;
                if constexpr (std::same_as<InitAlt, koopa_ir::IntegerLiteral>) {
                    return initAlt.value == 0;
                } else if constexpr (std::same_as<InitAlt, koopa_ir::UndefValue>) {
                    return false;
                } else if constexpr (std::same_as<InitAlt, koopa_ir::ZeroInit>) {
                    return true;
                } else {
                    const auto& aggregate = program[initAlt];
                    return std::all_of(aggregate.elements.begin(),
                        aggregate.elements.end(), [&](const auto& element) {
                            return isZeroInitializer(element, program);
                        });
                }
            },
            initializer);
    }

    GlobalSection classifyGlobal(
        const koopa_ir::GlobalMemoryDef& global, const koopa_ir::Program& program)
    {
        if (isZeroInitializer(global.initializer, program)) {
            return GlobalSection::bss;
        }
        if (isConstArraySymbolName(symbolName(global.name.spelling))) {
            return GlobalSection::rodata;
        }
        return GlobalSection::data;
    }

    void emitGlobalInitializer(std::ostream& output,
        const koopa_ir::Initializer& initializer, const koopa_ir::Type& type,
        const koopa_ir::Program& program)
    {
        std::visit(
            [&](auto initAlt) {
                using InitAlt = std::remove_cvref_t<decltype(initAlt)>;
                if constexpr (std::same_as<InitAlt, koopa_ir::IntegerLiteral>) {
                    output << "  .word " << initAlt.value << "\n";
                } else if constexpr (std::same_as<InitAlt, koopa_ir::UndefValue>) {
                    throw std::runtime_error(
                        "RISC-V backend does not support undef global initializers");
                } else if constexpr (std::same_as<InitAlt, koopa_ir::ZeroInit>) {
                    output << "  .zero " << typeSize(type, program) << "\n";
                } else {
                    const auto& aggregate = program[initAlt];
                    const auto& arrayType = requireArrayType(type, program);
                    if (aggregate.elements.size()
                        != static_cast<size_t>(arrayType.length)) {
                        throw std::runtime_error(
                            "aggregate initializer length does not match array type");
                    }
                    for (const auto& element : aggregate.elements) {
                        emitGlobalInitializer(
                            output, element, arrayType.elementType, program);
                    }
                }
            },
            initializer);
    }

    void emitGlobal(std::ostream& output, const koopa_ir::GlobalMemoryDef& global,
        const koopa_ir::Program& program)
    {
        const std::string name = symbolName(global.name.spelling);
        output << "  .globl " << name << "\n";
        output << name << ":\n";
        emitGlobalInitializer(output, global.initializer, global.allocType, program);
    }

    class FunctionEmitter {
    public:
        FunctionEmitter(RiscvGenerator* parent, std::ostream& output,
            const koopa_ir::Program& program, const koopa_ir::FunctionDef& function,
            const std::unordered_map<std::string, SymbolInfo>& globals,
            const std::unordered_map<std::string, FunctionSignature>& functionSignatures)
            : m_parent(parent)
            , m_output(output)
            , m_program(program)
            , m_function(function)
            , m_globals(globals)
            , m_functionSignatures(functionSignatures)
        {
        }

        void emitFunction()
        {
            buildSymbolTable();
            assignFrameLayout();
            assignBlockLabels();

            const std::string asmName = symbolName(m_function.name.spelling);
            m_output << "  .globl " << asmName << "\n";
            m_output << asmName << ":\n";
            emitStackAdjustment(-m_frameSize);
            if (m_savesReturnAddress) {
                emitStoreToStackSlot("ra", m_savedRaOffset);
            }
            spillFunctionParams();

            for (const auto blockRef : m_function.blocks) {
                emitBasicBlock(m_program[blockRef]);
            }
        }

    private:
        RiscvGenerator* m_parent;
        std::ostream& m_output;
        const koopa_ir::Program& m_program;
        const koopa_ir::FunctionDef& m_function;
        const std::unordered_map<std::string, SymbolInfo>& m_globals;
        const std::unordered_map<std::string, FunctionSignature>& m_functionSignatures;
        std::unordered_map<std::string, SymbolInfo> m_symbols;
        std::unordered_map<std::string, std::string> m_blockLabels;
        int m_outgoingArgAreaSize = 0;
        int m_frameSize = 0;
        int m_savedRaOffset = 0;
        bool m_savesReturnAddress = false;

        const SymbolInfo& requireSymbolInfo(const std::string& symbol) const
        {
            const auto localIt = m_symbols.find(symbol);
            if (localIt != m_symbols.end()) {
                return localIt->second;
            }
            const auto globalIt = m_globals.find(symbol);
            if (globalIt != m_globals.end()) {
                return globalIt->second;
            }
            throw std::runtime_error("missing symbol metadata for RISC-V emission: " + symbol);
        }

        static bool fitsImm12(int value)
        {
            return value >= -2048 && value <= 2047;
        }

        static int align16(int value)
        {
            return value == 0 ? 0 : ((value + 15) / 16) * 16;
        }

        int stackStorageSize(const SymbolInfo& symbolInfo) const
        {
            if (symbolInfo.storageClass == StorageClass::alloc) {
                return typeSize(symbolInfo.shape.payloadType, m_program);
            }
            if (symbolInfo.shape.isPointer) {
                return 4;
            }
            return typeSize(symbolInfo.shape.payloadType, m_program);
        }

        int stackOffset(const std::string& symbol) const
        {
            const auto& symbolInfo = requireSymbolInfo(symbol);
            if (symbolInfo.stackOffset < 0) {
                throw std::runtime_error("missing stack slot for symbol: " + symbol);
            }
            return symbolInfo.stackOffset;
        }

        const FunctionSignature& requireFunctionSignature(
            const std::string& symbol) const
        {
            const auto it = m_functionSignatures.find(symbol);
            if (it == m_functionSignatures.end()) {
                throw std::runtime_error(
                    "missing function signature for call target: " + symbol);
            }
            return it->second;
        }

        void buildSymbolTable()
        {
            m_symbols.clear();
            for (const auto paramRef : m_function.params) {
                const auto& param = m_program[paramRef];
                m_symbols.emplace(param.symbol.spelling,
                    SymbolInfo {
                        .storageClass = StorageClass::stackValue,
                        .shape = shapeFromType(param.type, m_program),
                    });
            }
            for (const auto blockRef : m_function.blocks) {
                const auto& block = m_program[blockRef];
                for (const auto blockParamRef : block.params) {
                    const auto& blockParam = m_program[blockParamRef];
                    m_symbols.emplace(blockParam.symbol.spelling,
                        SymbolInfo {
                            .storageClass = StorageClass::stackValue,
                            .shape = shapeFromType(blockParam.type, m_program),
                        });
                }
                for (const auto& statement : block.statements) {
                    std::visit(
                        [&](auto statementRef) {
                            using StatementNode = std::remove_cvref_t<
                                decltype(m_program[statementRef])>;
                            if constexpr (!std::same_as<StatementNode, koopa_ir::SymbolDef>) {
                                return;
                            } else {
                                const auto& symbolDef = m_program[statementRef];
                                m_symbols.emplace(symbolDef.symbol.spelling,
                                    lowerSymbolDef(symbolDef));
                            }
                        },
                        statement);
                }
            }
        }

        SymbolInfo lowerSymbolDef(const koopa_ir::SymbolDef& symbolDef) const
        {
            return std::visit(
                [&](auto rhsRef) -> SymbolInfo {
                    using Rhs = std::remove_cvref_t<decltype(m_program[rhsRef])>;
                    const auto& rhs = m_program[rhsRef];
                    if constexpr (std::same_as<Rhs, koopa_ir::MemoryDeclaration>) {
                        return SymbolInfo {
                            .storageClass = StorageClass::alloc,
                            .shape = ValueShape {
                                .isPointer = true,
                                .payloadType = rhs.allocType,
                            },
                        };
                    } else if constexpr (std::same_as<Rhs, koopa_ir::LoadExpr>) {
                        const auto& sourceInfo = requireSymbolInfo(rhs.source.spelling);
                        if (!sourceInfo.shape.isPointer) {
                            throw std::runtime_error(
                                "RISC-V backend expected load source to be a pointer");
                        }
                        return SymbolInfo {
                            .storageClass = StorageClass::stackValue,
                            .shape = shapeFromType(sourceInfo.shape.payloadType, m_program),
                        };
                    } else if constexpr (std::same_as<Rhs, koopa_ir::GetPointerExpr>) {
                        const auto& sourceInfo = requireSymbolInfo(rhs.source.spelling);
                        if (!sourceInfo.shape.isPointer) {
                            throw std::runtime_error(
                                "RISC-V backend expected getptr source to be a pointer");
                        }
                        return SymbolInfo {
                            .storageClass = StorageClass::stackValue,
                            .shape = sourceInfo.shape,
                        };
                    } else if constexpr (std::same_as<Rhs, koopa_ir::GetElementPointerExpr>) {
                        const auto& sourceInfo = requireSymbolInfo(rhs.source.spelling);
                        if (!sourceInfo.shape.isPointer) {
                            throw std::runtime_error(
                                "RISC-V backend expected getelemptr source to be a pointer");
                        }
                        const auto& arrayType
                            = requireArrayType(sourceInfo.shape.payloadType, m_program);
                        return SymbolInfo {
                            .storageClass = StorageClass::stackValue,
                            .shape = ValueShape {
                                .isPointer = true,
                                .payloadType = arrayType.elementType,
                            },
                        };
                    } else if constexpr (std::same_as<Rhs, koopa_ir::BinaryExpr>) {
                        return SymbolInfo {
                            .storageClass = StorageClass::stackValue,
                            .shape = ValueShape {
                                .isPointer = false,
                                .payloadType = koopa_ir::I32Type {},
                            },
                        };
                    } else {
                        const auto& calleeSignature
                            = requireFunctionSignature(rhs.callee.spelling);
                        if (!calleeSignature.returnShape.has_value()) {
                            throw std::runtime_error(
                                "RISC-V backend found value-defining call to void function");
                        }
                        return SymbolInfo {
                            .storageClass = StorageClass::stackValue,
                            .shape = *calleeSignature.returnShape,
                        };
                    }
                },
                symbolDef.rhs);
        }

        void assignFrameLayout()
        {
            m_savesReturnAddress = false;
            size_t maxCallArgs = 0;
            int nextOffset = 0;

            for (const auto blockRef : m_function.blocks) {
                const auto& block = m_program[blockRef];
                for (const auto& statement : block.statements) {
                    const bool isCall = std::visit(
                        [&](auto statementRef) {
                            using StatementNode = std::remove_cvref_t<
                                decltype(m_program[statementRef])>;
                            if constexpr (std::same_as<StatementNode, koopa_ir::CallExpr>) {
                                maxCallArgs = std::max(maxCallArgs,
                                    m_program[statementRef].args.size());
                                return true;
                            } else if constexpr (std::same_as<StatementNode, koopa_ir::SymbolDef>) {
                                return std::visit(
                                    [&](auto rhsRef) {
                                        using Rhs = std::remove_cvref_t<
                                            decltype(m_program[rhsRef])>;
                                        if constexpr (std::same_as<Rhs, koopa_ir::CallExpr>) {
                                            maxCallArgs = std::max(maxCallArgs,
                                                m_program[rhsRef].args.size());
                                            return true;
                                        }
                                        return false;
                                    },
                                    m_program[statementRef].rhs);
                            }
                            return false;
                        },
                        statement);
                    m_savesReturnAddress = m_savesReturnAddress || isCall;
                }
            }

            m_outgoingArgAreaSize
                = static_cast<int>(maxCallArgs > 8 ? (maxCallArgs - 8) * 4 : 0);
            nextOffset = m_outgoingArgAreaSize;

            for (const auto paramRef : m_function.params) {
                auto& symbolInfo
                    = m_symbols.at(m_program[paramRef].symbol.spelling);
                symbolInfo.stackOffset = nextOffset;
                nextOffset += stackStorageSize(symbolInfo);
            }
            for (const auto blockRef : m_function.blocks) {
                const auto& block = m_program[blockRef];
                for (const auto blockParamRef : block.params) {
                    auto& symbolInfo
                        = m_symbols.at(m_program[blockParamRef].symbol.spelling);
                    symbolInfo.stackOffset = nextOffset;
                    nextOffset += stackStorageSize(symbolInfo);
                }
                for (const auto& statement : block.statements) {
                    std::visit(
                        [&](auto statementRef) {
                            using StatementNode = std::remove_cvref_t<
                                decltype(m_program[statementRef])>;
                            if constexpr (std::same_as<StatementNode, koopa_ir::SymbolDef>) {
                                auto& symbolInfo = m_symbols.at(
                                    m_program[statementRef].symbol.spelling);
                                symbolInfo.stackOffset = nextOffset;
                                nextOffset += stackStorageSize(symbolInfo);
                            }
                        },
                        statement);
                }
            }

            if (m_savesReturnAddress) {
                m_savedRaOffset = nextOffset;
                nextOffset += 4;
            } else {
                m_savedRaOffset = 0;
            }
            m_frameSize = align16(nextOffset);
        }

        static std::string sanitizeBlockName(const koopa_ir::BasicBlock& basicBlock)
        {
            std::string label;
            for (const char ch : basicBlock.label.spelling) {
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                    || (ch >= '0' && ch <= '9')) {
                    label.push_back(ch);
                } else if (ch == '%') {
                    continue;
                } else {
                    label.push_back('_');
                }
            }
            if (label.empty()) {
                label = "bb";
            }
            return normalizeIdentifierStem(std::move(label));
        }

        void assignBlockLabels()
        {
            m_blockLabels.clear();
            const std::string functionName = symbolName(m_function.name.spelling);
            for (const auto blockRef : m_function.blocks) {
                const auto& block = m_program[blockRef];
                std::string label
                    = functionName + "_" + sanitizeBlockName(block);
                m_blockLabels.emplace(
                    block.label.spelling, m_parent->genLabel(std::move(label)));
            }
        }

        const std::string& blockLabel(const std::string& symbol) const
        {
            const auto it = m_blockLabels.find(symbol);
            if (it == m_blockLabels.end()) {
                throw std::runtime_error("missing block label for symbol: " + symbol);
            }
            return it->second;
        }

        void emitStackAdjustment(int delta)
        {
            if (delta == 0) {
                return;
            }
            if (fitsImm12(delta)) {
                m_output << "  addi sp, sp, " << delta << "\n";
                return;
            }
            m_output << "  li t3, " << delta << "\n";
            m_output << "  add sp, sp, t3\n";
        }

        void emitAddressOfStackSlot(int offset, const std::string& targetRegister)
        {
            if (fitsImm12(offset)) {
                m_output << "  addi " << targetRegister << ", sp, " << offset
                         << "\n";
                return;
            }
            m_output << "  li " << targetRegister << ", " << offset << "\n";
            m_output << "  add " << targetRegister << ", sp, " << targetRegister
                     << "\n";
        }

        void emitLoadFromStackSlot(int offset, const std::string& targetRegister)
        {
            if (fitsImm12(offset)) {
                m_output << "  lw " << targetRegister << ", " << offset << "(sp)\n";
                return;
            }
            emitAddressOfStackSlot(offset, "t3");
            m_output << "  lw " << targetRegister << ", 0(t3)\n";
        }

        void emitStoreToStackSlot(const std::string& sourceRegister, int offset)
        {
            if (fitsImm12(offset)) {
                m_output << "  sw " << sourceRegister << ", " << offset << "(sp)\n";
                return;
            }
            emitAddressOfStackSlot(offset, "t3");
            m_output << "  sw " << sourceRegister << ", 0(t3)\n";
        }

        void spillFunctionParams()
        {
            for (size_t index = 0; index < m_function.params.size(); ++index) {
                const auto& param = m_program[m_function.params[index]];
                if (index < 8) {
                    emitStoreToStackSlot(
                        "a" + std::to_string(index), stackOffset(param.symbol.spelling));
                    continue;
                }
                const int incomingOffset
                    = m_frameSize + static_cast<int>((index - 8) * 4);
                emitLoadFromStackSlot(incomingOffset, "t0");
                emitStoreToStackSlot("t0", stackOffset(param.symbol.spelling));
            }
        }

        void emitBasicBlock(const koopa_ir::BasicBlock& basicBlock)
        {
            m_output << blockLabel(basicBlock.label.spelling) << ":\n";
            for (const auto& statement : basicBlock.statements) {
                emitStatement(statement);
            }
            emitTerminator(basicBlock.terminator);
        }

        void loadPointerSymbolToRegister(
            const std::string& symbol, const std::string& targetRegister)
        {
            const auto& symbolInfo = requireSymbolInfo(symbol);
            if (!symbolInfo.shape.isPointer) {
                throw std::runtime_error(
                    "RISC-V backend expected a pointer-typed symbol: " + symbol);
            }
            switch (symbolInfo.storageClass) {
            case StorageClass::global:
                m_output << "  la " << targetRegister << ", " << symbolName(symbol)
                         << "\n";
                return;
            case StorageClass::alloc:
                emitAddressOfStackSlot(symbolInfo.stackOffset, targetRegister);
                return;
            case StorageClass::stackValue:
                emitLoadFromStackSlot(symbolInfo.stackOffset, targetRegister);
                return;
            }
            throw std::runtime_error("unsupported pointer storage class");
        }

        void loadValueToRegister(
            const koopa_ir::Value& value, const std::string& targetRegister)
        {
            std::visit(
                [&](auto valueAlt) {
                    using ValueAlt = std::remove_cvref_t<decltype(valueAlt)>;
                    if constexpr (std::same_as<ValueAlt, koopa_ir::IntegerLiteral>) {
                        m_output << "  li " << targetRegister << ", " << valueAlt.value
                                 << "\n";
                    } else if constexpr (std::same_as<ValueAlt, koopa_ir::UndefValue>) {
                        throw std::runtime_error(
                            "RISC-V backend does not support undef operands");
                    } else {
                        const auto& symbolInfo = requireSymbolInfo(valueAlt.spelling);
                        if (symbolInfo.shape.isPointer) {
                            loadPointerSymbolToRegister(valueAlt.spelling, targetRegister);
                        } else {
                            emitLoadFromStackSlot(
                                symbolInfo.stackOffset, targetRegister);
                        }
                    }
                },
                value);
        }

        void loadStoreValueToRegister(
            const koopa_ir::StoreValue& value, const std::string& targetRegister,
            const std::string& context)
        {
            std::visit(
                [&](auto valueAlt) {
                    using ValueAlt = std::remove_cvref_t<decltype(valueAlt)>;
                    if constexpr (std::same_as<ValueAlt, koopa_ir::Symbol>
                        || std::same_as<ValueAlt, koopa_ir::IntegerLiteral>
                        || std::same_as<ValueAlt, koopa_ir::UndefValue>) {
                        loadValueToRegister(koopa_ir::Value { valueAlt }, targetRegister);
                    } else {
                        throw std::runtime_error(
                            "RISC-V backend does not support " + context
                            + " with aggregate or zeroinit values");
                    }
                },
                value);
        }

        void emitIndexedPointer(const std::string& sourceSymbol,
            const koopa_ir::Value& index, int stride,
            const std::string& resultSymbol)
        {
            loadPointerSymbolToRegister(sourceSymbol, "t0");
            loadValueToRegister(index, "t1");
            m_output << "  li t2, " << stride << "\n";
            m_output << "  mul t1, t1, t2\n";
            m_output << "  add t0, t0, t1\n";
            emitStoreToStackSlot("t0", stackOffset(resultSymbol));
        }

        void emitBinary(const koopa_ir::BinaryExpr& binaryExpr,
            const std::string& resultSymbol)
        {
            loadValueToRegister(binaryExpr.lhs, "t0");
            loadValueToRegister(binaryExpr.rhs, "t1");
            switch (binaryExpr.op) {
            case koopa_ir::BinaryOp::ne:
                m_output << "  xor t2, t0, t1\n";
                m_output << "  snez t2, t2\n";
                break;
            case koopa_ir::BinaryOp::eq:
                m_output << "  xor t2, t0, t1\n";
                m_output << "  seqz t2, t2\n";
                break;
            case koopa_ir::BinaryOp::gt:
                m_output << "  sgt t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::lt:
                m_output << "  slt t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::ge:
                m_output << "  slt t2, t0, t1\n";
                m_output << "  xori t2, t2, 1\n";
                break;
            case koopa_ir::BinaryOp::le:
                m_output << "  sgt t2, t0, t1\n";
                m_output << "  xori t2, t2, 1\n";
                break;
            case koopa_ir::BinaryOp::add:
                m_output << "  add t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::sub:
                m_output << "  sub t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::mul:
                m_output << "  mul t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::div:
                m_output << "  div t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::mod:
                m_output << "  rem t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::bitAnd:
                m_output << "  and t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::bitOr:
                m_output << "  or t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::bitXor:
                m_output << "  xor t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::shl:
                m_output << "  sll t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::shr:
                m_output << "  srl t2, t0, t1\n";
                break;
            case koopa_ir::BinaryOp::sar:
                m_output << "  sra t2, t0, t1\n";
                break;
            }
            emitStoreToStackSlot("t2", stackOffset(resultSymbol));
        }

        void emitEdgeArgumentStores(const koopa_ir::Symbol& target,
            const std::vector<koopa_ir::Value>& args)
        {
            const auto& targetBlock = findBlock(target.spelling);
            if (targetBlock.params.size() != args.size()) {
                throw std::runtime_error(
                    "RISC-V backend received mismatched block argument arity");
            }
            for (size_t index = 0; index < args.size(); ++index) {
                loadValueToRegister(args[index], "t0");
                emitStoreToStackSlot(
                    "t0", stackOffset(m_program[targetBlock.params[index]].symbol.spelling));
            }
        }

        const koopa_ir::BasicBlock& findBlock(const std::string& label) const
        {
            for (const auto blockRef : m_function.blocks) {
                const auto& block = m_program[blockRef];
                if (block.label.spelling == label) {
                    return block;
                }
            }
            throw std::runtime_error("missing target block in current function: " + label);
        }

        void emitJumpEdge(const koopa_ir::Symbol& target,
            const std::vector<koopa_ir::Value>& args)
        {
            emitEdgeArgumentStores(target, args);
            m_output << "  j " << blockLabel(target.spelling) << "\n";
        }

        void emitCall(const koopa_ir::CallExpr& callExpr,
            const std::optional<std::string>& resultSymbol)
        {
            for (size_t index = 0; index < callExpr.args.size(); ++index) {
                if (index < 8) {
                    loadValueToRegister(callExpr.args[index],
                        "a" + std::to_string(index));
                    continue;
                }
                loadValueToRegister(callExpr.args[index], "t0");
                emitStoreToStackSlot("t0", static_cast<int>((index - 8) * 4));
            }
            m_output << "  call " << symbolName(callExpr.callee.spelling) << "\n";
            if (resultSymbol.has_value()) {
                emitStoreToStackSlot("a0", stackOffset(*resultSymbol));
            }
        }

        void emitStatement(const koopa_ir::Statement& statement)
        {
            std::visit(
                [&](auto statementRef) {
                    using StatementNode = std::remove_cvref_t<
                        decltype(m_program[statementRef])>;
                    const auto& statementNode = m_program[statementRef];
                    if constexpr (std::same_as<StatementNode, koopa_ir::SymbolDef>) {
                        std::visit(
                            [&](auto rhsRef) {
                                using Rhs = std::remove_cvref_t<
                                    decltype(m_program[rhsRef])>;
                                const auto& rhs = m_program[rhsRef];
                                if constexpr (std::same_as<Rhs, koopa_ir::MemoryDeclaration>) {
                                    return;
                                } else if constexpr (std::same_as<Rhs, koopa_ir::LoadExpr>) {
                                    const auto& sourceInfo = requireSymbolInfo(rhs.source.spelling);
                                    if (sourceInfo.storageClass == StorageClass::alloc) {
                                        emitLoadFromStackSlot(
                                            sourceInfo.stackOffset, "t1");
                                    } else {
                                        loadPointerSymbolToRegister(rhs.source.spelling, "t0");
                                        m_output << "  lw t1, 0(t0)\n";
                                    }
                                    emitStoreToStackSlot(
                                        "t1", stackOffset(statementNode.symbol.spelling));
                                } else if constexpr (std::same_as<Rhs, koopa_ir::GetPointerExpr>) {
                                    const auto& sourceInfo = requireSymbolInfo(rhs.source.spelling);
                                    emitIndexedPointer(rhs.source.spelling, rhs.index,
                                        typeSize(sourceInfo.shape.payloadType, m_program),
                                        statementNode.symbol.spelling);
                                } else if constexpr (std::same_as<Rhs,
                                                           koopa_ir::GetElementPointerExpr>) {
                                    const auto& sourceInfo = requireSymbolInfo(rhs.source.spelling);
                                    const auto& arrayType = requireArrayType(
                                        sourceInfo.shape.payloadType, m_program);
                                    emitIndexedPointer(rhs.source.spelling, rhs.index,
                                        typeSize(arrayType.elementType, m_program),
                                        statementNode.symbol.spelling);
                                } else if constexpr (std::same_as<Rhs, koopa_ir::BinaryExpr>) {
                                    emitBinary(rhs, statementNode.symbol.spelling);
                                } else {
                                    emitCall(rhs, statementNode.symbol.spelling);
                                }
                            },
                            statementNode.rhs);
                    } else if constexpr (std::same_as<StatementNode, koopa_ir::StoreStmt>) {
                        const auto& destinationInfo
                            = requireSymbolInfo(statementNode.destination.spelling);
                        loadStoreValueToRegister(
                            statementNode.value, "t2", "store statements");
                        if (destinationInfo.storageClass == StorageClass::alloc) {
                            emitStoreToStackSlot(
                                "t2", destinationInfo.stackOffset);
                            return;
                        }
                        loadPointerSymbolToRegister(statementNode.destination.spelling, "t0");
                        m_output << "  sw t2, 0(t0)\n";
                    } else {
                        emitCall(statementNode, std::nullopt);
                    }
                },
                statement);
        }

        void emitTerminator(const koopa_ir::Terminator& terminator)
        {
            std::visit(
                [&](auto terminatorRef) {
                    using TerminatorNode = std::remove_cvref_t<
                        decltype(m_program[terminatorRef])>;
                    const auto& terminatorNode = m_program[terminatorRef];
                    if constexpr (std::same_as<TerminatorNode, koopa_ir::BranchTerminator>) {
                        loadValueToRegister(terminatorNode.condition, "t0");
                        if (terminatorNode.trueArgs.empty()
                            && terminatorNode.falseArgs.empty()) {
                            const std::string trueEdgeLabel = m_parent->genLabel(
                                blockLabel(terminatorNode.trueTarget.spelling) + "_args");
                            m_output << "  bnez t0, " << trueEdgeLabel << "\n";
                            m_output << "  j "
                                     << blockLabel(terminatorNode.falseTarget.spelling)
                                     << "\n";
                            m_output << trueEdgeLabel << ":\n";
                            m_output << "  j "
                                     << blockLabel(terminatorNode.trueTarget.spelling)
                                     << "\n";
                            return;
                        }
                        const std::string trueEdgeLabel = m_parent->genLabel(
                            blockLabel(terminatorNode.trueTarget.spelling) + "_args");
                        const std::string falseEdgeLabel = m_parent->genLabel(
                            blockLabel(terminatorNode.falseTarget.spelling) + "_args");
                        m_output << "  bnez t0, " << trueEdgeLabel << "\n";
                        m_output << "  j " << falseEdgeLabel << "\n";
                        m_output << trueEdgeLabel << ":\n";
                        emitJumpEdge(terminatorNode.trueTarget, terminatorNode.trueArgs);
                        m_output << falseEdgeLabel << ":\n";
                        emitJumpEdge(terminatorNode.falseTarget, terminatorNode.falseArgs);
                    } else if constexpr (std::same_as<TerminatorNode, koopa_ir::JumpTerminator>) {
                        emitJumpEdge(terminatorNode.target, terminatorNode.args);
                    } else {
                        if (terminatorNode.value.has_value()) {
                            loadValueToRegister(*terminatorNode.value, "a0");
                        }
                        if (m_savesReturnAddress) {
                            emitLoadFromStackSlot(m_savedRaOffset, "ra");
                        }
                        emitStackAdjustment(m_frameSize);
                        m_output << "  ret\n";
                    }
                },
                terminator);
        }
    };

} // namespace

void RiscvGenerator::generate(
    const koopa_ir::Program& program, std::ostream& output)
{
    std::unordered_map<std::string, SymbolInfo> globals;
    std::unordered_map<std::string, FunctionSignature> functionSignatures;
    bool hasAnyFunction = false;

    for (const auto& item : program.items) {
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                const auto& itemNode = program[itemRef];
                if constexpr (std::same_as<Item, koopa_ir::GlobalMemoryDef>) {
                    globals.emplace(itemNode.name.spelling,
                        SymbolInfo {
                            .storageClass = StorageClass::global,
                            .shape = ValueShape {
                                .isPointer = true,
                                .payloadType = itemNode.allocType,
                            },
                        });
                } else if constexpr (std::same_as<Item, koopa_ir::FunctionDecl>) {
                    hasAnyFunction = true;
                    functionSignatures.insert_or_assign(itemNode.name.spelling,
                        FunctionSignature {
                            .returnShape = itemNode.returnType.has_value()
                                ? std::optional<ValueShape> {
                                      shapeFromType(*itemNode.returnType, program) }
                                : std::nullopt,
                        });
                } else {
                    hasAnyFunction = true;
                    functionSignatures.insert_or_assign(itemNode.name.spelling,
                        FunctionSignature {
                            .returnShape = itemNode.returnType.has_value()
                                ? std::optional<ValueShape> {
                                      shapeFromType(*itemNode.returnType, program) }
                                : std::nullopt,
                        });
                }
            },
            item);
    }

    if (!hasAnyFunction) {
        throw std::runtime_error(
            "RISC-V backend expects at least one function");
    }

    auto emitGlobalSection = [&](GlobalSection section, const char* header) {
        bool emittedHeader = false;
        for (const auto& item : program.items) {
            if (const auto* globalRef
                = std::get_if<yesod::Ref<koopa_ir::GlobalMemoryDef>>(&item)) {
                const auto& global = program[*globalRef];
                if (classifyGlobal(global, program) != section) {
                    continue;
                }
                if (!emittedHeader) {
                    output << header;
                    emittedHeader = true;
                }
                emitGlobal(output, global, program);
            }
        }
    };

    emitGlobalSection(GlobalSection::rodata, "  .section .rodata\n");
    emitGlobalSection(GlobalSection::data, "  .data\n");
    emitGlobalSection(GlobalSection::bss, "  .bss\n");

    output << "  .text\n";
    for (const auto& item : program.items) {
        if (const auto* functionRef
            = std::get_if<yesod::Ref<koopa_ir::FunctionDef>>(&item)) {
            FunctionEmitter emitter(
                this, output, program, program[*functionRef], globals, functionSignatures);
            emitter.emitFunction();
        }
    }
}

} // namespace yesod::backend
