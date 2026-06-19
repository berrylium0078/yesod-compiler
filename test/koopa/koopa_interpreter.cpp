#include "koopa/koopa_interpreter.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "poly/poly_runtime.h"
#include "utils.h"

namespace yesod::test_support::koopa::interpreter {

namespace {

    using koopa_ir::ArrayType;
    using koopa_ir::BasicBlock;
    using koopa_ir::BinaryExpr;
    using koopa_ir::BlockParameter;
    using koopa_ir::BranchTerminator;
    using koopa_ir::CallExpr;
    using koopa_ir::FunctionDef;
    using koopa_ir::GetElementPointerExpr;
    using koopa_ir::GetPointerExpr;
    using koopa_ir::GlobalMemoryDef;
    using koopa_ir::Initializer;
    using koopa_ir::IntegerLiteral;
    using koopa_ir::JumpTerminator;
    using koopa_ir::LoadExpr;
    using koopa_ir::MemoryDeclaration;
    using koopa_ir::PointerType;
    using koopa_ir::Program;
    using koopa_ir::Ref;
    using koopa_ir::ReturnTerminator;
    using koopa_ir::StoreStmt;
    using koopa_ir::StoreValue;
    using koopa_ir::Symbol;
    using koopa_ir::SymbolDef;
    using koopa_ir::Type;
    using koopa_ir::Value;
    using yesod::test_support::poly::Mint;
    using yesod::test_support::poly::Poly;

    class ExecuteException : public std::runtime_error {
    public:
        ExecuteException(ExecuteStatus status, const std::string& message)
            : std::runtime_error(message)
            , m_status(status)
        {
        }

        [[nodiscard]] ExecuteStatus status() const { return m_status; }

    private:
        ExecuteStatus m_status = ExecuteStatus::runtimeError;
    };

    struct TypeInfo {
        enum class Kind {
            i32,
            mint,
            poly,
            array,
            pointer,
            unsupported,
        };

        Kind kind = Kind::unsupported;
        int32_t length = 0;
        std::shared_ptr<TypeInfo> element;
    };

    struct Address {
        size_t objectId = 0;
        int32_t offset = 0;
        std::shared_ptr<TypeInfo> pointeeType;
    };

    struct RuntimeValue {
        std::variant<int32_t, Address, Mint, Poly> value = int32_t { 0 };
    };

    struct MemoryObject {
        std::vector<RuntimeValue> cells;
    };

    struct Frame {
        const FunctionDef* function = nullptr;
        const BasicBlock* currentBlock = nullptr;
        std::string functionName;
        std::string blockName;
        std::unordered_map<std::string, RuntimeValue> values;
        std::unordered_map<std::string, std::shared_ptr<TypeInfo>> types;
    };

    struct TerminatorStep {
        enum class Kind {
            jump,
            ret,
        };

        Kind kind = Kind::ret;
        const BasicBlock* targetBlock = nullptr;
        std::vector<RuntimeValue> args;
        std::optional<RuntimeValue> returnValue;
    };

    [[nodiscard]] std::string symbolName(const Symbol& symbol)
    {
        return symbol.spelling;
    }

    [[nodiscard]] int32_t requireInt(const RuntimeValue& value)
    {
        if (const auto* intValue = std::get_if<int32_t>(&value.value)) {
            return *intValue;
        }
        throw ExecuteException(
            ExecuteStatus::runtimeError, "expected integer runtime value");
    }

    [[nodiscard]] Address requireAddress(const RuntimeValue& value)
    {
        if (const auto* address = std::get_if<Address>(&value.value)) {
            return *address;
        }
        throw ExecuteException(
            ExecuteStatus::runtimeError, "expected address runtime value");
    }

    [[nodiscard]] Mint requireMint(const RuntimeValue& value)
    {
        if (const auto* mintValue = std::get_if<Mint>(&value.value)) {
            return *mintValue;
        }
        if (const auto* intValue = std::get_if<int32_t>(&value.value)) {
            return Mint(*intValue);
        }
        throw ExecuteException(
            ExecuteStatus::runtimeError, "expected mint runtime value");
    }

    [[nodiscard]] Poly requirePoly(const RuntimeValue& value)
    {
        if (const auto* polyValue = std::get_if<Poly>(&value.value)) {
            return *polyValue;
        }
        throw ExecuteException(
            ExecuteStatus::runtimeError, "expected poly runtime value");
    }

    [[nodiscard]] RuntimeValue makeInt(int32_t value)
    {
        return RuntimeValue { .value = value };
    }

    [[nodiscard]] RuntimeValue makeAddress(Address address)
    {
        return RuntimeValue { .value = std::move(address) };
    }

    [[nodiscard]] RuntimeValue makeMint(Mint value)
    {
        return RuntimeValue { .value = value };
    }

    [[nodiscard]] RuntimeValue makePoly(Poly value)
    {
        return RuntimeValue { .value = std::move(value) };
    }

    class Interpreter {
    public:
        Interpreter(
            const Program& program, std::istream& input, std::ostream& output)
            : m_program(program)
            , m_input(input)
            , m_output(output)
        {
        }

        [[nodiscard]] ExecuteResult execute(std::stop_token stopToken)
        {
            m_stopToken = stopToken;
            indexProgram();
            initializeGlobals();

            const auto* mainFunction = findFunction("@main");
            if (mainFunction == nullptr) {
                throw ExecuteException(
                    ExecuteStatus::runtimeError, "missing @main function");
            }

            ExecuteResult result;
            result.returnValue
                = requireInt(callUserFunction(*mainFunction, { }));
            return result;
        }

    private:
        [[nodiscard]] std::shared_ptr<TypeInfo> makeTypeInfo(
            const Type& type) const
        {
            return MATCH(type) WITH(
                [&](const koopa_ir::I32Type&) -> std::shared_ptr<TypeInfo> {
                    return std::make_shared<TypeInfo>(
                        TypeInfo { .kind = TypeInfo::Kind::i32 });
                },
                [&](const koopa_ir::MintType&) -> std::shared_ptr<TypeInfo> {
                    return std::make_shared<TypeInfo>(
                        TypeInfo { .kind = TypeInfo::Kind::mint });
                },
                [&](const koopa_ir::PolyType&) -> std::shared_ptr<TypeInfo> {
                    return std::make_shared<TypeInfo>(
                        TypeInfo { .kind = TypeInfo::Kind::poly });
                },
                [&](Ref<ArrayType> arrayRef) -> std::shared_ptr<TypeInfo> {
                    const auto& arrayType = m_program[arrayRef];
                    return std::make_shared<TypeInfo>(TypeInfo {
                        .kind = TypeInfo::Kind::array,
                        .length = arrayType.length,
                        .element = makeTypeInfo(arrayType.elementType),
                    });
                },
                [&](Ref<PointerType> pointerRef) -> std::shared_ptr<TypeInfo> {
                    const auto& pointerType = m_program[pointerRef];
                    return std::make_shared<TypeInfo>(TypeInfo {
                        .kind = TypeInfo::Kind::pointer,
                        .element = makeTypeInfo(pointerType.pointeeType),
                    });
                },
                [&](Ref<koopa_ir::FunctionType>) -> std::shared_ptr<TypeInfo> {
                    return std::make_shared<TypeInfo>(
                        TypeInfo { .kind = TypeInfo::Kind::unsupported });
                });
        }

        [[nodiscard]] int32_t typeSize(
            const std::shared_ptr<TypeInfo>& type) const
        {
            if (!type) {
                throw ExecuteException(
                    ExecuteStatus::runtimeError, "missing type information");
            }
            switch (type->kind) {
            case TypeInfo::Kind::i32:
            case TypeInfo::Kind::mint:
            case TypeInfo::Kind::poly:
                return 1;
            case TypeInfo::Kind::array:
                return type->length * typeSize(type->element);
            case TypeInfo::Kind::pointer:
                return 1;
            case TypeInfo::Kind::unsupported:
                throw ExecuteException(
                    ExecuteStatus::unsupported, "unsupported Koopa IR type");
            }
            throw ExecuteException(ExecuteStatus::runtimeError, "unknown type");
        }

        [[nodiscard]] RuntimeValue defaultCellValue(
            const std::shared_ptr<TypeInfo>& type) const
        {
            if (!type) {
                return makeInt(0);
            }
            switch (type->kind) {
            case TypeInfo::Kind::i32:
                return makeInt(0);
            case TypeInfo::Kind::mint:
                return makeMint(Mint(0));
            case TypeInfo::Kind::poly:
                return makePoly(Poly());
            case TypeInfo::Kind::array:
                return defaultCellValue(type->element);
            case TypeInfo::Kind::pointer:
            case TypeInfo::Kind::unsupported:
                return makeInt(0);
            }
            return makeInt(0);
        }

        [[nodiscard]] std::shared_ptr<TypeInfo> pointerTo(
            const std::shared_ptr<TypeInfo>& pointeeType) const
        {
            return std::make_shared<TypeInfo>(TypeInfo {
                .kind = TypeInfo::Kind::pointer,
                .element = pointeeType,
            });
        }

        [[nodiscard]] int32_t normalizeIndex(int64_t value) const
        {
            if (value < std::numeric_limits<int32_t>::min()
                || value > std::numeric_limits<int32_t>::max()) {
                throw ExecuteException(
                    ExecuteStatus::arrayOutOfBounds, "array index overflow");
            }
            return static_cast<int32_t>(value);
        }

        void checkStopped() const
        {
            if (m_stopToken.stop_requested()) {
                throw ExecuteException(
                    ExecuteStatus::stopped, "execution stopped by stop_token");
            }
        }

        [[nodiscard]] MemoryObject& memory(Address address)
        {
            if (address.objectId >= m_memory.size()) {
                throw ExecuteException(
                    ExecuteStatus::runtimeError, "invalid memory object");
            }
            return m_memory[address.objectId];
        }

        [[nodiscard]] const MemoryObject& memory(Address address) const
        {
            if (address.objectId >= m_memory.size()) {
                throw ExecuteException(
                    ExecuteStatus::runtimeError, "invalid memory object");
            }
            return m_memory[address.objectId];
        }

        void checkAddressRange(const Address& address, int32_t cellCount) const
        {
            const auto& object = memory(address);
            if (address.offset < 0 || cellCount < 0
                || static_cast<int64_t>(address.offset) + cellCount
                    > static_cast<int64_t>(object.cells.size())) {
                throw ExecuteException(ExecuteStatus::arrayOutOfBounds,
                    "array access out of bounds");
            }
        }

        [[nodiscard]] Address addPointerOffset(
            const Address& base, int32_t index, bool checkArrayLength) const
        {
            if (!base.pointeeType) {
                throw ExecuteException(ExecuteStatus::runtimeError,
                    "pointer is missing pointee type");
            }
            const int32_t stride = typeSize(base.pointeeType);
            if (checkArrayLength
                && base.pointeeType->kind == TypeInfo::Kind::array
                && (index < 0 || index >= base.pointeeType->length)) {
                throw ExecuteException(ExecuteStatus::arrayOutOfBounds,
                    "array index out of bounds");
            }
            Address result = base;
            result.offset = normalizeIndex(static_cast<int64_t>(base.offset)
                + static_cast<int64_t>(stride) * index);
            checkAddressRange(result, stride);
            return result;
        }

        [[nodiscard]] Address addElementPointerOffset(
            const Address& base, int32_t index) const
        {
            if (!base.pointeeType
                || base.pointeeType->kind != TypeInfo::Kind::array) {
                throw ExecuteException(ExecuteStatus::runtimeError,
                    "getelemptr requires pointer to array");
            }
            if (index < 0 || index >= base.pointeeType->length) {
                throw ExecuteException(ExecuteStatus::arrayOutOfBounds,
                    "array index out of bounds");
            }
            const int32_t stride = typeSize(base.pointeeType->element);
            Address result = base;
            result.offset = normalizeIndex(static_cast<int64_t>(base.offset)
                + static_cast<int64_t>(stride) * index);
            result.pointeeType = base.pointeeType->element;
            checkAddressRange(result, stride);
            return result;
        }

        [[nodiscard]] RuntimeValue evalValue(
            const Value& value, const Frame& frame)
        {
            return MATCH(value) WITH(
                [&](const Symbol& symbol) -> RuntimeValue {
                    return lookupValue(symbolName(symbol), frame);
                },
                [&](const IntegerLiteral& literal) -> RuntimeValue {
                    return makeInt(literal.value);
                },
                [&](const koopa_ir::UndefValue&) -> RuntimeValue {
                    return makeInt(0);
                });
        }

        [[nodiscard]] RuntimeValue evalStoreValue(
            const StoreValue& value, const Frame& frame)
        {
            return MATCH(value) WITH(
                [&](const Symbol& symbol) -> RuntimeValue {
                    return lookupValue(symbolName(symbol), frame);
                },
                [&](const IntegerLiteral& literal) -> RuntimeValue {
                    return makeInt(literal.value);
                },
                [&](const koopa_ir::UndefValue&) -> RuntimeValue {
                    return makeInt(0);
                },
                [&](const koopa_ir::ZeroInit&) -> RuntimeValue {
                    return makeInt(0);
                },
                [&](Ref<koopa_ir::AggregateInitializer>) -> RuntimeValue {
                    throw ExecuteException(ExecuteStatus::runtimeError,
                        "aggregate store should be handled structurally");
                });
        }

        [[nodiscard]] RuntimeValue lookupValue(
            const std::string& name, const Frame& frame) const
        {
            const auto localIt = frame.values.find(name);
            if (localIt != frame.values.end()) {
                return localIt->second;
            }
            const auto globalIt = m_globalValues.find(name);
            if (globalIt != m_globalValues.end()) {
                return globalIt->second;
            }
            throw ExecuteException(
                ExecuteStatus::runtimeError, "unknown symbol: " + name);
        }

        [[nodiscard]] std::shared_ptr<TypeInfo> lookupType(
            const std::string& name, const Frame& frame) const
        {
            const auto localIt = frame.types.find(name);
            if (localIt != frame.types.end()) {
                return localIt->second;
            }
            const auto globalIt = m_globalTypes.find(name);
            if (globalIt != m_globalTypes.end()) {
                return globalIt->second;
            }
            throw ExecuteException(
                ExecuteStatus::runtimeError, "unknown symbol type: " + name);
        }

        [[nodiscard]] RuntimeValue coerceValueToType(
            RuntimeValue value, const std::shared_ptr<TypeInfo>& type) const
        {
            if (type && type->kind == TypeInfo::Kind::pointer) {
                if (auto* address = std::get_if<Address>(&value.value)) {
                    address->pointeeType = type->element;
                }
            }
            return value;
        }

        void indexProgram()
        {
            for (const auto& item : m_program.items) {
                MATCH(item)
                WITH(
                    [&](Ref<GlobalMemoryDef> globalRef) -> void {
                        const auto& global = m_program[globalRef];
                        m_globals.emplace(symbolName(global.name), &global);
                    },
                    [&](Ref<koopa_ir::FunctionDecl>) -> void { },
                    [&](Ref<FunctionDef> functionRef) -> void {
                        const auto& function = m_program[functionRef];
                        m_functions.emplace(
                            symbolName(function.name), &function);
                    });
            }
        }

        void initializeGlobals()
        {
            for (const auto& [name, global] : m_globals) {
                const auto allocType = makeTypeInfo(global->allocType);
                const int32_t size = typeSize(allocType);
                const size_t objectId = m_memory.size();
                m_memory.push_back(MemoryObject {
                    .cells = std::vector<RuntimeValue>(
                        static_cast<size_t>(size), defaultCellValue(allocType)),
                });
                initializeCells(global->initializer, allocType,
                    Address {
                        .objectId = objectId,
                        .offset = 0,
                        .pointeeType = allocType,
                    });
                m_globalValues.emplace(name,
                    makeAddress(Address {
                        .objectId = objectId,
                        .offset = 0,
                        .pointeeType = allocType,
                    }));
                m_globalTypes.emplace(name, pointerTo(allocType));
            }
        }

        void initializeCells(const Initializer& initializer,
            const std::shared_ptr<TypeInfo>& type, const Address& address)
        {
            MATCH(initializer)
            WITH(
                [&](const IntegerLiteral& literal) -> void {
                    checkAddressRange(address, 1);
                    memory(address).cells[static_cast<size_t>(address.offset)]
                        = makeInt(literal.value);
                },
                [&](const koopa_ir::UndefValue&) -> void { },
                [&](const koopa_ir::ZeroInit&) -> void { },
                [&](Ref<koopa_ir::AggregateInitializer> aggregateRef) -> void {
                    const auto& aggregate = m_program[aggregateRef];
                    if (!type || type->kind != TypeInfo::Kind::array) {
                        throw ExecuteException(ExecuteStatus::runtimeError,
                            "aggregate initializer requires array type");
                    }
                    const int32_t stride = typeSize(type->element);
                    for (size_t index = 0; index < aggregate.elements.size();
                        ++index) {
                        if (index >= static_cast<size_t>(type->length)) {
                            throw ExecuteException(
                                ExecuteStatus::arrayOutOfBounds,
                                "initializer exceeds array length");
                        }
                        initializeCells(aggregate.elements[index],
                            type->element,
                            Address {
                                .objectId = address.objectId,
                                .offset = normalizeIndex(
                                    static_cast<int64_t>(address.offset)
                                    + static_cast<int64_t>(stride)
                                        * static_cast<int64_t>(index)),
                                .pointeeType = type->element,
                            });
                    }
                });
        }

        void storeInitializer(const StoreValue& value, const Frame& frame,
            const std::shared_ptr<TypeInfo>& type, const Address& address)
        {
            if (std::holds_alternative<Ref<koopa_ir::AggregateInitializer>>(
                    value)
                || std::holds_alternative<koopa_ir::ZeroInit>(value)) {
                Initializer initializer = MATCH(value) WITH(
                    [&](const Symbol&) -> Initializer {
                        throw ExecuteException(ExecuteStatus::runtimeError,
                            "symbol store is not an initializer");
                    },
                    [&](const IntegerLiteral& literal) -> Initializer {
                        return literal;
                    },
                    [&](const koopa_ir::UndefValue& undef) -> Initializer {
                        return undef;
                    },
                    [&](const koopa_ir::ZeroInit& zero) -> Initializer {
                        return zero;
                    },
                    [&](Ref<koopa_ir::AggregateInitializer> aggregate)
                        -> Initializer { return aggregate; });
                initializeCells(initializer, type, address);
                return;
            }

            checkAddressRange(address, 1);
            memory(address).cells[static_cast<size_t>(address.offset)]
                = evalStoreValue(value, frame);
        }

        [[nodiscard]] RuntimeValue callUserFunction(
            const FunctionDef& function, const std::vector<RuntimeValue>& args)
        {
            checkStopped();
            if (args.size() != function.params.size()) {
                throw ExecuteException(ExecuteStatus::runtimeError,
                    "function argument count mismatch");
            }
            if (function.blocks.empty()) {
                throw ExecuteException(
                    ExecuteStatus::runtimeError, "function has no blocks");
            }

            Frame frame;
            frame.function = &function;
            frame.currentBlock = &m_program[function.blocks.front()];
            frame.functionName = symbolName(function.name);
            frame.blockName = symbolName(frame.currentBlock->label);
            for (size_t index = 0; index < function.params.size(); ++index) {
                const auto& param = m_program[function.params[index]];
                const std::string name = symbolName(param.symbol);
                const auto paramType = makeTypeInfo(param.type);
                frame.values.emplace(
                    name, coerceValueToType(args[index], paramType));
                frame.types.emplace(name, paramType);
            }

            try {
                while (true) {
                    checkStopped();
                    executeBlockStatements(frame);
                    TerminatorStep step = executeTerminator(frame);
                    if (step.kind == TerminatorStep::Kind::ret) {
                        return step.returnValue.value_or(makeInt(0));
                    }
                    assert(step.targetBlock != nullptr);
                    bindBlockArgs(*step.targetBlock, step.args, frame);
                    frame.currentBlock = step.targetBlock;
                    frame.blockName = symbolName(frame.currentBlock->label);
                }
            } catch (const ExecuteException& exception) {
                throw ExecuteException(exception.status(),
                    std::string(exception.what()) + " while executing "
                        + frame.functionName + " " + frame.blockName);
            }
        }

        void executeBlockStatements(Frame& frame)
        {
            assert(frame.currentBlock != nullptr);
            for (const auto& statement : frame.currentBlock->statements) {
                checkStopped();
                MATCH(statement)
                WITH(
                    [&](Ref<SymbolDef> symbolDefRef) -> void {
                        executeSymbolDef(m_program[symbolDefRef], frame);
                    },
                    [&](Ref<StoreStmt> storeStmtRef) -> void {
                        executeStore(m_program[storeStmtRef], frame);
                    },
                    [&](Ref<CallExpr> callRef) -> void {
                        (void)executeCall(m_program[callRef], frame);
                    });
            }
        }

        void executeSymbolDef(const SymbolDef& symbolDef, Frame& frame)
        {
            const std::string name = symbolName(symbolDef.symbol);
            MATCH(symbolDef.rhs)
            WITH(
                [&](Ref<MemoryDeclaration> memoryRef) -> void {
                    const auto& declaration = m_program[memoryRef];
                    const auto allocType = makeTypeInfo(declaration.allocType);
                    const int32_t size = typeSize(allocType);
                    const size_t objectId = m_memory.size();
                    m_memory.push_back(MemoryObject {
                        .cells
                        = std::vector<RuntimeValue>(static_cast<size_t>(size),
                            defaultCellValue(allocType)),
                    });
                    frame.values[name] = makeAddress(Address {
                        .objectId = objectId,
                        .offset = 0,
                        .pointeeType = allocType,
                    });
                    frame.types[name] = pointerTo(allocType);
                },
                [&](Ref<LoadExpr> loadRef) -> void {
                    const auto& load = m_program[loadRef];
                    const Address address = requireAddress(
                        lookupValue(symbolName(load.source), frame));
                    checkAddressRange(address, typeSize(address.pointeeType));
                    frame.values[name] = memory(address).cells.at(
                        static_cast<size_t>(address.offset));
                    frame.types[name] = address.pointeeType;
                },
                [&](Ref<GetPointerExpr> getPtrRef) -> void {
                    const auto& expr = m_program[getPtrRef];
                    const Address source = requireAddress(
                        lookupValue(symbolName(expr.source), frame));
                    const int32_t index
                        = requireInt(evalValue(expr.index, frame));
                    const Address result
                        = addPointerOffset(source, index, false);
                    frame.values[name] = makeAddress(result);
                    frame.types[name]
                        = lookupType(symbolName(expr.source), frame);
                },
                [&](Ref<GetElementPointerExpr> getElemPtrRef) -> void {
                    const auto& expr = m_program[getElemPtrRef];
                    const Address source = requireAddress(
                        lookupValue(symbolName(expr.source), frame));
                    const int32_t index
                        = requireInt(evalValue(expr.index, frame));
                    Address result = addElementPointerOffset(source, index);
                    frame.values[name] = makeAddress(result);
                    frame.types[name] = pointerTo(result.pointeeType);
                },
                [&](Ref<BinaryExpr> binaryRef) -> void {
                    frame.values[name]
                        = executeBinary(m_program[binaryRef], frame);
                    frame.types[name] = std::make_shared<TypeInfo>(
                        typeInfoForRuntimeValue(frame.values[name]));
                },
                [&](Ref<CallExpr> callRef) -> void {
                    const auto result = executeCall(m_program[callRef], frame);
                    frame.values[name] = result.value_or(makeInt(0));
                    frame.types[name] = std::make_shared<TypeInfo>(
                        typeInfoForRuntimeValue(frame.values[name]));
                },
                [&](Ref<koopa_ir::CopyExpr> copyRef) -> void {
                    const auto& expr = m_program[copyRef];
                    frame.values[name] = evalValue(expr.value, frame);
                    frame.types[name] = std::make_shared<TypeInfo>(
                        typeInfoForRuntimeValue(frame.values[name]));
                },
                [&](Ref<koopa_ir::PolyLenExpr> lenRef) -> void {
                    frame.values[name]
                        = makeInt(executePolyLen(m_program[lenRef], frame));
                    frame.types[name] = std::make_shared<TypeInfo>(
                        TypeInfo { .kind = TypeInfo::Kind::i32 });
                },
                [&](Ref<koopa_ir::PointwiseExpr> pointwiseRef) -> void {
                    frame.values[name] = makePoly(
                        executePointwise(m_program[pointwiseRef], frame));
                    frame.types[name] = std::make_shared<TypeInfo>(
                        TypeInfo { .kind = TypeInfo::Kind::poly });
                },
                [&](Ref<koopa_ir::CombineExpr> combineRef) -> void {
                    frame.values[name] = makePoly(
                        executeCombine(m_program[combineRef], frame));
                    frame.types[name] = std::make_shared<TypeInfo>(
                        TypeInfo { .kind = TypeInfo::Kind::poly });
                },
                [&](Ref<koopa_ir::GetCoeffExpr> getCoeffRef) -> void {
                    const auto& expr = m_program[getCoeffRef];
                    frame.values[name] = makeMint(
                        requirePoly(evalValue(expr.value, frame))
                            .coeff(requireInt(evalValue(expr.index, frame))));
                    frame.types[name] = std::make_shared<TypeInfo>(
                        TypeInfo { .kind = TypeInfo::Kind::mint });
                },
                [&](Ref<koopa_ir::PolyConstructExpr> constructRef) -> void {
                    const auto& expr = m_program[constructRef];
                    std::vector<Mint> coefficients;
                    coefficients.reserve(expr.elements.size());
                    for (const auto& element : expr.elements) {
                        coefficients.push_back(
                            requireMint(evalValue(element, frame)));
                    }
                    frame.values[name]
                        = makePoly(Poly(std::move(coefficients)));
                    frame.types[name] = std::make_shared<TypeInfo>(
                        TypeInfo { .kind = TypeInfo::Kind::poly });
                },
                [&](Ref<koopa_ir::ConversionExpr> conversionRef) -> void {
                    const auto& expr = m_program[conversionRef];
                    const RuntimeValue value = evalValue(expr.value, frame);
                    if (expr.op == koopa_ir::ConversionOp::int2mint) {
                        frame.values[name] = makeMint(Mint(requireInt(value)));
                        frame.types[name] = std::make_shared<TypeInfo>(
                            TypeInfo { .kind = TypeInfo::Kind::mint });
                    } else {
                        frame.values[name]
                            = makeInt(requireMint(value).value());
                        frame.types[name] = std::make_shared<TypeInfo>(
                            TypeInfo { .kind = TypeInfo::Kind::i32 });
                    }
                });
        }

        [[nodiscard]] RuntimeValue executePointwiseNode(
            Ref<koopa_ir::PointwiseNode> nodeRef, Frame& frame)
        {
            const auto& node = m_program[nodeRef];
            return MATCH(node.kind) WITH(
                [&](const koopa_ir::PointwiseLeaf& leaf) -> RuntimeValue {
                    return evalValue(leaf.value, frame);
                },
                [&](const koopa_ir::PointwiseBinary& binary) -> RuntimeValue {
                    const RuntimeValue lhs
                        = executePointwiseNode(binary.lhs, frame);
                    const RuntimeValue rhs
                        = executePointwiseNode(binary.rhs, frame);
                    switch (binary.op) {
                    case koopa_ir::PvBinaryOp::add:
                        return makePoly(requirePoly(lhs) + requirePoly(rhs));
                    case koopa_ir::PvBinaryOp::sub:
                        return makePoly(requirePoly(lhs) - requirePoly(rhs));
                    case koopa_ir::PvBinaryOp::mul:
                        return makePoly(requirePoly(lhs) * requirePoly(rhs));
                    case koopa_ir::PvBinaryOp::times:
                        return makePoly(requirePoly(lhs) * requireMint(rhs));
                    }
                    throw ExecuteException(ExecuteStatus::runtimeError,
                        "unknown pointwise operation");
                });
        }

        [[nodiscard]] Poly executePointwise(
            const koopa_ir::PointwiseExpr& expr, Frame& frame)
        {
            return requirePoly(executePointwiseNode(expr.root, frame));
        }

        [[nodiscard]] int32_t executePolyLen(
            const koopa_ir::PolyLenExpr& expr, Frame& frame)
        {
            auto intArg = [&](size_t index) -> int32_t {
                return requireInt(evalValue(expr.args.at(index), frame));
            };
            switch (expr.op) {
            case koopa_ir::PolyLenOp::len:
                return requirePoly(evalValue(expr.args.at(0), frame)).length();
            case koopa_ir::PolyLenOp::max:
                return std::max(intArg(0), intArg(1));
            case koopa_ir::PolyLenOp::min:
                return std::min(intArg(0), intArg(1));
            case koopa_ir::PolyLenOp::mulLen: {
                const int32_t lhs = intArg(0);
                const int32_t rhs = intArg(1);
                return lhs != 0 && rhs != 0 ? lhs + rhs - 1 : 0;
            }
            case koopa_ir::PolyLenOp::shiftLen:
                return std::max(0, intArg(0) - intArg(1));
            case koopa_ir::PolyLenOp::sliceLen: {
                const int32_t length = intArg(0);
                const int32_t start = intArg(1);
                const int32_t end = intArg(2);
                if (end <= 0 || start >= end || start >= length) {
                    return 0;
                }
                return std::min(end, length);
            }
            }
            throw ExecuteException(
                ExecuteStatus::runtimeError, "unknown poly length operation");
        }

        [[nodiscard]] Poly executeCombine(
            const koopa_ir::CombineExpr& expr, Frame& frame)
        {
            Poly result;
            for (const auto& term : expr.terms) {
                const int32_t start = requireInt(evalValue(term.start, frame));
                const int32_t end = term.end.has_value()
                    ? requireInt(evalValue(*term.end, frame))
                    : std::numeric_limits<int32_t>::max();
                const int32_t shift = requireInt(evalValue(term.shift, frame));
                const Mint scale = requireMint(evalValue(term.scale, frame));
                Poly value = requirePoly(evalValue(term.value, frame))
                                 .slice(start, end)
                                 .shiftRight(shift)
                    * scale;
                result = result + value;
            }
            return result;
        }

        [[nodiscard]] TypeInfo typeInfoForRuntimeValue(
            const RuntimeValue& value) const
        {
            return MATCH(value.value) WITH(
                [](int32_t) -> TypeInfo {
                    return TypeInfo { .kind = TypeInfo::Kind::i32 };
                },
                [](const Address& address) -> TypeInfo {
                    return TypeInfo {
                        .kind = TypeInfo::Kind::pointer,
                        .element = address.pointeeType,
                    };
                },
                [](const Mint&) -> TypeInfo {
                    return TypeInfo { .kind = TypeInfo::Kind::mint };
                },
                [](const Poly&) -> TypeInfo {
                    return TypeInfo { .kind = TypeInfo::Kind::poly };
                });
        }

        void executeStore(const StoreStmt& storeStmt, Frame& frame)
        {
            const Address address = requireAddress(
                lookupValue(symbolName(storeStmt.destination), frame));
            storeInitializer(
                storeStmt.value, frame, address.pointeeType, address);
        }

        [[nodiscard]] RuntimeValue executeBinary(
            const BinaryExpr& binaryExpr, Frame& frame)
        {
            const RuntimeValue lhsValue = evalValue(binaryExpr.lhs, frame);
            const RuntimeValue rhsValue = evalValue(binaryExpr.rhs, frame);
            if (std::holds_alternative<Mint>(lhsValue.value)
                || std::holds_alternative<Mint>(rhsValue.value)) {
                const Mint lhs = requireMint(lhsValue);
                const Mint rhs = requireMint(rhsValue);
                switch (binaryExpr.op) {
                case koopa_ir::BinaryOp::add:
                    return makeMint(lhs + rhs);
                case koopa_ir::BinaryOp::sub:
                    return makeMint(lhs - rhs);
                case koopa_ir::BinaryOp::mul:
                    return makeMint(lhs * rhs);
                case koopa_ir::BinaryOp::div:
                    return makeMint(lhs / rhs);
                case koopa_ir::BinaryOp::eq:
                    return makeInt(lhs == rhs);
                case koopa_ir::BinaryOp::ne:
                    return makeInt(lhs != rhs);
                case koopa_ir::BinaryOp::gt:
                    return makeInt(lhs.value() > rhs.value());
                case koopa_ir::BinaryOp::lt:
                    return makeInt(lhs.value() < rhs.value());
                case koopa_ir::BinaryOp::ge:
                    return makeInt(lhs.value() >= rhs.value());
                case koopa_ir::BinaryOp::le:
                    return makeInt(lhs.value() <= rhs.value());
                case koopa_ir::BinaryOp::mod:
                case koopa_ir::BinaryOp::bitAnd:
                case koopa_ir::BinaryOp::bitOr:
                case koopa_ir::BinaryOp::bitXor:
                case koopa_ir::BinaryOp::shl:
                case koopa_ir::BinaryOp::shr:
                case koopa_ir::BinaryOp::sar:
                    throw ExecuteException(ExecuteStatus::runtimeError,
                        "integer-only binary operation received mint");
                }
            }

            const int32_t lhs = requireInt(lhsValue);
            const int32_t rhs = requireInt(rhsValue);
            switch (binaryExpr.op) {
            case koopa_ir::BinaryOp::ne:
                return makeInt(lhs != rhs);
            case koopa_ir::BinaryOp::eq:
                return makeInt(lhs == rhs);
            case koopa_ir::BinaryOp::gt:
                return makeInt(lhs > rhs);
            case koopa_ir::BinaryOp::lt:
                return makeInt(lhs < rhs);
            case koopa_ir::BinaryOp::ge:
                return makeInt(lhs >= rhs);
            case koopa_ir::BinaryOp::le:
                return makeInt(lhs <= rhs);
            case koopa_ir::BinaryOp::add:
                return makeInt(lhs + rhs);
            case koopa_ir::BinaryOp::sub:
                return makeInt(lhs - rhs);
            case koopa_ir::BinaryOp::mul:
                return makeInt(lhs * rhs);
            case koopa_ir::BinaryOp::div:
                if (rhs == 0) {
                    throw ExecuteException(
                        ExecuteStatus::divisionByZero, "division by zero");
                }
                return makeInt(lhs / rhs);
            case koopa_ir::BinaryOp::mod:
                if (rhs == 0) {
                    throw ExecuteException(
                        ExecuteStatus::divisionByZero, "modulo by zero");
                }
                return makeInt(lhs % rhs);
            case koopa_ir::BinaryOp::bitAnd:
                return makeInt(lhs & rhs);
            case koopa_ir::BinaryOp::bitOr:
                return makeInt(lhs | rhs);
            case koopa_ir::BinaryOp::bitXor:
                return makeInt(lhs ^ rhs);
            case koopa_ir::BinaryOp::shl:
                return makeInt(lhs << rhs);
            case koopa_ir::BinaryOp::shr:
                return makeInt(static_cast<int32_t>(
                    static_cast<uint32_t>(lhs) >> static_cast<uint32_t>(rhs)));
            case koopa_ir::BinaryOp::sar:
                return makeInt(lhs >> rhs);
            }
            throw ExecuteException(
                ExecuteStatus::runtimeError, "unknown binary operation");
        }

        [[nodiscard]] std::optional<RuntimeValue> executeCall(
            const CallExpr& callExpr, Frame& frame)
        {
            const std::string callee = symbolName(callExpr.callee);
            std::vector<RuntimeValue> args;
            args.reserve(callExpr.args.size());
            for (const auto& arg : callExpr.args) {
                args.push_back(evalValue(arg, frame));
            }

            if (auto builtinResult = executeBuiltin(callee, args)) {
                return *builtinResult;
            }

            const auto* function = findFunction(callee);
            if (function == nullptr) {
                throw ExecuteException(
                    ExecuteStatus::runtimeError, "unknown callee: " + callee);
            }
            return callUserFunction(*function, args);
        }

        [[nodiscard]] std::optional<std::optional<RuntimeValue>> executeBuiltin(
            const std::string& callee, const std::vector<RuntimeValue>& args)
        {
            if (callee == "@getint") {
                int32_t value = 0;
                m_input >> value;
                return std::optional<RuntimeValue> { makeInt(value) };
            }
            if (callee == "@getch") {
                const int value = m_input.get();
                if (value == std::char_traits<char>::eof()) {
                    return std::optional<RuntimeValue> { makeInt(-1) };
                }
                return std::optional<RuntimeValue> { makeInt(
                    static_cast<int32_t>(static_cast<unsigned char>(value))) };
            }
            if (callee == "@getarray") {
                requireArgCount(callee, args, 1);
                Address array = requireAddress(args[0]);
                int32_t count = 0;
                m_input >> count;
                for (int32_t index = 0; index < count; ++index) {
                    Address element = array;
                    element.offset = normalizeIndex(
                        static_cast<int64_t>(array.offset) + index);
                    checkAddressRange(element, 1);
                    int32_t value = 0;
                    m_input >> value;
                    memory(element).cells[static_cast<size_t>(element.offset)]
                        = makeInt(value);
                }
                return std::optional<RuntimeValue> { makeInt(count) };
            }
            if (callee == "@putint") {
                requireArgCount(callee, args, 1);
                m_output << requireInt(args[0]);
                return std::optional<RuntimeValue> { };
            }
            if (callee == "@putch") {
                requireArgCount(callee, args, 1);
                m_output << static_cast<char>(requireInt(args[0]));
                return std::optional<RuntimeValue> { };
            }
            if (callee == "@putarray") {
                requireArgCount(callee, args, 2);
                const int32_t count = requireInt(args[0]);
                Address array = requireAddress(args[1]);
                m_output << count << ':';
                for (int32_t index = 0; index < count; ++index) {
                    Address element = array;
                    element.offset = normalizeIndex(
                        static_cast<int64_t>(array.offset) + index);
                    checkAddressRange(element, 1);
                    m_output
                        << ' '
                        << requireInt(memory(element)
                                   .cells[static_cast<size_t>(element.offset)]);
                }
                m_output << '\n';
                return std::optional<RuntimeValue> { };
            }
            if (callee == "@putpoly") {
                requireArgCount(callee, args, 1);
                m_output << requirePoly(args[0]);
                return std::optional<RuntimeValue> { };
            }
            if (callee == "@starttime" || callee == "@stoptime") {
                return std::optional<RuntimeValue> { };
            }
            return std::nullopt;
        }

        void requireArgCount(const std::string& callee,
            const std::vector<RuntimeValue>& args, size_t expected) const
        {
            if (args.size() != expected) {
                throw ExecuteException(ExecuteStatus::runtimeError,
                    callee + " argument count mismatch");
            }
        }

        [[nodiscard]] TerminatorStep executeTerminator(Frame& frame)
        {
            assert(frame.currentBlock != nullptr);
            return MATCH(frame.currentBlock->terminator) WITH(
                [&](Ref<BranchTerminator> branchRef) -> TerminatorStep {
                    const auto& branch = m_program[branchRef];
                    const bool condition
                        = requireInt(evalValue(branch.condition, frame)) != 0;
                    const auto& targetName
                        = condition ? branch.trueTarget : branch.falseTarget;
                    const auto& edgeArgs
                        = condition ? branch.trueArgs : branch.falseArgs;
                    return TerminatorStep {
                        .kind = TerminatorStep::Kind::jump,
                        .targetBlock
                        = requireBlock(*frame.function, symbolName(targetName)),
                        .args = evalArgs(edgeArgs, frame),
                    };
                },
                [&](Ref<JumpTerminator> jumpRef) -> TerminatorStep {
                    const auto& jump = m_program[jumpRef];
                    return TerminatorStep {
                        .kind = TerminatorStep::Kind::jump,
                        .targetBlock = requireBlock(
                            *frame.function, symbolName(jump.target)),
                        .args = evalArgs(jump.args, frame),
                    };
                },
                [&](Ref<ReturnTerminator> returnRef) -> TerminatorStep {
                    const auto& terminator = m_program[returnRef];
                    return TerminatorStep {
                        .kind = TerminatorStep::Kind::ret,
                        .returnValue = terminator.value.has_value()
                            ? std::optional<RuntimeValue> { evalValue(
                                  *terminator.value, frame) }
                            : std::nullopt,
                    };
                });
        }

        [[nodiscard]] std::vector<RuntimeValue> evalArgs(
            const std::vector<Value>& args, Frame& frame)
        {
            std::vector<RuntimeValue> values;
            values.reserve(args.size());
            for (const auto& arg : args) {
                values.push_back(evalValue(arg, frame));
            }
            return values;
        }

        void bindBlockArgs(const BasicBlock& block,
            const std::vector<RuntimeValue>& args, Frame& frame)
        {
            if (args.size() != block.params.size()) {
                throw ExecuteException(ExecuteStatus::runtimeError,
                    "basic block argument count mismatch");
            }
            for (size_t index = 0; index < block.params.size(); ++index) {
                const BlockParameter& param = m_program[block.params[index]];
                const std::string name = symbolName(param.symbol);
                frame.values[name] = args[index];
                frame.types[name] = makeTypeInfo(param.type);
            }
        }

        [[nodiscard]] const FunctionDef* findFunction(
            const std::string& name) const
        {
            const auto it = m_functions.find(name);
            if (it == m_functions.end()) {
                return nullptr;
            }
            return it->second;
        }

        [[nodiscard]] const BasicBlock* requireBlock(
            const FunctionDef& function, const std::string& name) const
        {
            for (Ref<BasicBlock> blockRef : function.blocks) {
                const auto& block = m_program[blockRef];
                if (symbolName(block.label) == name) {
                    return &block;
                }
            }
            throw ExecuteException(
                ExecuteStatus::runtimeError, "unknown block: " + name);
        }

        const Program& m_program;
        std::istream& m_input;
        std::ostream& m_output;
        std::stop_token m_stopToken;
        std::unordered_map<std::string, const GlobalMemoryDef*> m_globals;
        std::unordered_map<std::string, const FunctionDef*> m_functions;
        std::unordered_map<std::string, RuntimeValue> m_globalValues;
        std::unordered_map<std::string, std::shared_ptr<TypeInfo>>
            m_globalTypes;
        std::vector<MemoryObject> m_memory;
    };

} // namespace

const char* toString(ExecuteStatus status)
{
    switch (status) {
    case ExecuteStatus::normal:
        return "normal";
    case ExecuteStatus::stopped:
        return "stopped";
    case ExecuteStatus::arrayOutOfBounds:
        return "arrayOutOfBounds";
    case ExecuteStatus::divisionByZero:
        return "divisionByZero";
    case ExecuteStatus::unsupported:
        return "unsupported";
    case ExecuteStatus::runtimeError:
        return "runtimeError";
    }
    return "unknown";
}

ExecuteResult execute(const koopa_ir::Program& program, std::istream& is,
    std::ostream& os, std::stop_token stopToken)
{
    try {
        Interpreter interpreter(program, is, os);
        return interpreter.execute(stopToken);
    } catch (const ExecuteException& exception) {
        return ExecuteResult {
            .status = exception.status(),
            .returnValue = 0,
            .message = exception.what(),
        };
    } catch (const std::exception& exception) {
        return ExecuteResult {
            .status = ExecuteStatus::runtimeError,
            .returnValue = 0,
            .message = exception.what(),
        };
    }
}

} // namespace yesod::test_support::koopa::interpreter
