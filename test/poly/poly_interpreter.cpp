#include "poly/poly_interpreter.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "poly/poly_runtime.h"
#include "utils.h"

namespace yesod::test_support::poly::interpreter {

namespace {

    namespace frontend = yesod::frontend;
    using yesod::Ref;
    namespace runner = yesod::test_support::interpreter_runner;

    struct ArrayObject;
    using ArrayPtr = std::shared_ptr<ArrayObject>;
    using RuntimeValue = std::variant<int32_t, Mint, Poly, ArrayPtr>;

    struct ArrayObject {
        frontend::SemanticType elementType;
        std::vector<int32_t> dimensions;
        std::vector<int32_t> strides;
        std::shared_ptr<std::vector<RuntimeValue>> cells;
        int32_t offset = 0;
    };

    struct RuntimeFault : std::exception {
        RuntimeFault(ExecuteStatus status, std::string message)
            : status(status)
            , message(std::move(message))
        {
        }

        const char* what() const noexcept override { return message.c_str(); }

        ExecuteStatus status;
        std::string message;
    };

    struct ReturnSignal {
        RuntimeValue value = int32_t { 0 };
    };

    struct BreakSignal { };

    struct ContinueSignal { };

    class Interpreter {
    public:
        Interpreter(const frontend::AST& ast,
            frontend::Ref<frontend::CompUnit> root,
            const frontend::SemanticInfo& semanticInfo, std::istream& is,
            std::ostream& os, std::stop_token stopToken)
            : m_ast(ast)
            , m_root(root)
            , m_semanticInfo(semanticInfo)
            , m_is(is)
            , m_os(os)
            , m_stopToken(stopToken)
        {
        }

        [[nodiscard]] ExecuteResult execute()
        {
            try {
                collectFunctions();
                m_scopes.emplace_back();
                for (const auto& item : m_root(m_ast).topLevelItems) {
                    MATCH(item)
                    WITH(
                        [&](const frontend::Decl& decl) -> void {
                            executeDecl(decl);
                        },
                        [&](const Ref<frontend::FuncDef>&) -> void { });
                }
                RuntimeValue result = callFunction("main", { });
                return ExecuteResult {
                    .status = ExecuteStatus::normal,
                    .returnValue = asInt(result),
                    .message = { },
                };
            } catch (const RuntimeFault& fault) {
                return ExecuteResult {
                    .status = fault.status,
                    .returnValue = 0,
                    .message = fault.message,
                };
            }
        }

    private:
        void checkStop() const
        {
            if (m_stopToken.stop_requested()) {
                throw RuntimeFault(ExecuteStatus::stopped, "stop requested");
            }
        }

        void collectFunctions()
        {
            for (const auto& item : m_root(m_ast).topLevelItems) {
                if (const auto* funcDef
                    = std::get_if<Ref<frontend::FuncDef>>(&item)) {
                    const auto& func = (*funcDef)(m_ast);
                    m_functions.emplace(func.identifier(m_ast).name, *funcDef);
                }
            }
        }

        [[nodiscard]] RuntimeValue callFunction(
            const std::string& name, const std::vector<RuntimeValue>& params)
        {
            checkStop();
            if (tryCallBuiltin(name, params)) {
                return *m_builtinResult;
            }

            const auto it = m_functions.find(name);
            if (it == m_functions.end()) {
                throw RuntimeFault(
                    ExecuteStatus::runtimeError, "unknown function: " + name);
            }
            const auto& func = it->second(m_ast);
            if (func.funcFParams.size() != params.size()) {
                throw RuntimeFault(ExecuteStatus::runtimeError,
                    "function argument count mismatch");
            }

            m_scopes.emplace_back();
            for (size_t index = 0; index < params.size(); ++index) {
                const auto& param = func.funcFParams[index];
                const int32_t symbolId = requireSymbolId(param.identifier);
                m_scopes.back().emplace(symbolId, params[index]);
            }

            try {
                if (func.body != nullptr) {
                    executeBlock(func.body.ref(), true);
                }
            } catch (const ReturnSignal& signal) {
                m_scopes.pop_back();
                return signal.value;
            }

            m_scopes.pop_back();
            return int32_t { 0 };
        }

        [[nodiscard]] bool tryCallBuiltin(
            const std::string& name, const std::vector<RuntimeValue>& params)
        {
            m_builtinResult.reset();
            if (name == "getint") {
                int32_t value = 0;
                m_is >> value;
                m_builtinResult = value;
            } else if (name == "getch") {
                const int value = m_is.get();
                if (value == std::char_traits<char>::eof()) {
                    m_builtinResult = int32_t { -1 };
                } else {
                    m_builtinResult = static_cast<int32_t>(
                        static_cast<unsigned char>(value));
                }
            } else if (name == "getarray") {
                ArrayPtr array = asArray(params.at(0));
                int32_t count = 0;
                m_is >> count;
                for (int32_t index = 0; index < count; ++index) {
                    int32_t value = 0;
                    m_is >> value;
                    arrayCell(*array, { index }) = value;
                }
                m_builtinResult = count;
            } else if (name == "putint") {
                m_os << asInt(params.at(0));
                m_builtinResult = int32_t { 0 };
            } else if (name == "putch") {
                m_os << static_cast<char>(asInt(params.at(0)));
                m_builtinResult = int32_t { 0 };
            } else if (name == "putarray") {
                const int32_t count = asInt(params.at(0));
                ArrayPtr array = asArray(params.at(1));
                m_os << count << ':';
                for (int32_t index = 0; index < count; ++index) {
                    m_os << ' ' << asInt(arrayCell(*array, { index }));
                }
                m_os << '\n';
                m_builtinResult = int32_t { 0 };
            } else if (name == "starttime" || name == "stoptime") {
                m_builtinResult = int32_t { 0 };
            }
            return m_builtinResult.has_value();
        }

        void executeBlock(Ref<frontend::Block> block, bool reuseScope)
        {
            const size_t scopeDepth = m_scopes.size();
            if (!reuseScope) {
                m_scopes.emplace_back();
            }
            try {
                for (const auto& item : block(m_ast).items) {
                    checkStop();
                    MATCH(item)
                    WITH(
                        [&](const frontend::Decl& decl) -> void {
                            executeDecl(decl);
                        },
                        [&](const frontend::Stmt& stmt) -> void {
                            executeStmt(stmt);
                        });
                }
            } catch (...) {
                m_scopes.resize(scopeDepth);
                throw;
            }
            m_scopes.resize(scopeDepth);
        }

        void executeDecl(const frontend::Decl& decl)
        {
            MATCH(decl)
            WITH(
                [&](const Ref<frontend::ConstDecl>& constDecl) -> void {
                    for (const auto& def : constDecl(m_ast).constDef) {
                        defineObject(
                            def(m_ast).identifier, def(m_ast).constInitVal);
                    }
                },
                [&](const Ref<frontend::VarDecl>& varDecl) -> void {
                    for (const auto& def : varDecl(m_ast).varDef) {
                        defineObject(def(m_ast).identifier, def(m_ast).initVal);
                    }
                });
        }

        void defineObject(Ref<frontend::Identifier> identifier,
            frontend::Ptr<frontend::InitVal> init)
        {
            const int32_t symbolId = requireSymbolId(identifier);
            const auto& symbol = requireObjectSymbol(symbolId);
            RuntimeValue value = defaultValue(symbol.m_type);
            if (init != nullptr) {
                initializeValue(symbol.m_type, value, init.ref());
            }
            m_scopes.back().emplace(symbolId, std::move(value));
        }

        void defineObject(Ref<frontend::Identifier> identifier,
            frontend::Ptr<frontend::ConstInitVal> init)
        {
            const int32_t symbolId = requireSymbolId(identifier);
            const auto& symbol = requireObjectSymbol(symbolId);
            RuntimeValue value = defaultValue(symbol.m_type);
            if (init != nullptr) {
                initializeValue(symbol.m_type, value, init.ref());
            }
            m_scopes.back().emplace(symbolId, std::move(value));
        }

        void executeStmt(const frontend::Stmt& stmt)
        {
            MATCH(stmt)
            WITH(
                [&](const Ref<frontend::IfStmt>& ifStmt) -> void {
                    const auto& node = ifStmt(m_ast);
                    if (isTruthy(evaluateExp(node.condition))) {
                        executeStmt(node.thenBody);
                    } else {
                        executeStmt(node.elseBody);
                    }
                },
                [&](const Ref<frontend::WhileStmt>& whileStmt) -> void {
                    const auto& node = whileStmt(m_ast);
                    while (isTruthy(evaluateExp(node.condition))) {
                        checkStop();
                        try {
                            executeStmt(node.body);
                        } catch (const ContinueSignal&) {
                        } catch (const BreakSignal&) {
                            break;
                        }
                    }
                },
                [&](const Ref<frontend::BreakStmt>&) -> void {
                    throw BreakSignal();
                },
                [&](const Ref<frontend::ContinueStmt>&) -> void {
                    throw ContinueSignal();
                },
                [&](const Ref<frontend::AssignStmt>& assignStmt) -> void {
                    RuntimeValue* target
                        = resolveLValue(assignStmt(m_ast).lval);
                    *target = evaluateExp(assignStmt(m_ast).exp);
                },
                [&](const Ref<frontend::Block>& block) -> void {
                    executeBlock(block, false);
                },
                [&](const Ref<frontend::ReturnStmt>& returnStmt) -> void {
                    const auto& node = returnStmt(m_ast);
                    if (node.exp == nullptr) {
                        throw ReturnSignal { };
                    }
                    throw ReturnSignal { evaluateExp(node.exp.ref()) };
                },
                [&](const Ref<frontend::ExpStmt>& expStmt) -> void {
                    if (expStmt(m_ast).exp != nullptr) {
                        (void)evaluateExp(expStmt(m_ast).exp.ref());
                    }
                });
        }

        [[nodiscard]] RuntimeValue evaluateExp(Ref<frontend::Exp> exp)
        {
            checkStop();
            const auto& node = exp(m_ast);
            return MATCH(node.kind) WITH(
                [&](const frontend::Exp::Binary& binary) -> RuntimeValue {
                    return evaluateBinary(binary);
                },
                [&](const frontend::Exp::Unary& unary) -> RuntimeValue {
                    return evaluateUnary(unary);
                },
                [&](const frontend::Exp::Cast& cast) -> RuntimeValue {
                    return evaluateCast(cast);
                },
                [&](const frontend::Exp::Call& call) -> RuntimeValue {
                    std::vector<RuntimeValue> params;
                    params.reserve(call.params.size());
                    for (const auto& param : call.params) {
                        params.push_back(evaluateExp(param));
                    }
                    return callFunction(call.funcName(m_ast).name, params);
                },
                [&](const frontend::Exp::Slice& slice) -> RuntimeValue {
                    return asPoly(evaluateExp(slice.base))
                        .slice(asInt(evaluateExp(slice.start)),
                            asInt(evaluateExp(slice.end)));
                },
                [&](const frontend::Exp::Subscript& subscript) -> RuntimeValue {
                    return asPoly(evaluateExp(subscript.base))
                        .coeff(asInt(evaluateExp(subscript.index)));
                },
                [&](const frontend::Exp::Ntt&) -> RuntimeValue {
                    throw unsupported(
                        "ntt is not supported by the AST interpreter");
                },
                [&](const frontend::Exp::Intt&) -> RuntimeValue {
                    throw unsupported(
                        "intt is not supported by the AST interpreter");
                },
                [&](const frontend::Exp::PvBinary&) -> RuntimeValue {
                    throw unsupported(
                        "pv operation is not supported by the AST interpreter");
                },
                [&](const frontend::Exp::Combine&) -> RuntimeValue {
                    throw unsupported(
                        "combine is not supported by the AST interpreter");
                },
                [&](const frontend::Exp::GetCoeff&) -> RuntimeValue {
                    throw unsupported(
                        "getcoeff is not supported by the AST interpreter");
                },
                [&](const frontend::Exp::PolyConstruct& construct)
                    -> RuntimeValue {
                    std::vector<Mint> coefficients;
                    coefficients.reserve(construct.elements.size());
                    for (const auto& element : construct.elements) {
                        coefficients.push_back(asMint(evaluateExp(element)));
                    }
                    return Poly(std::move(coefficients));
                },
                [&](const frontend::Exp::IntToMint& conversion)
                    -> RuntimeValue {
                    return Mint(asInt(evaluateExp(conversion.value)));
                },
                [&](const frontend::Exp::MintToInt& conversion)
                    -> RuntimeValue {
                    return asInt(evaluateExp(conversion.value));
                },
                [&](const frontend::Exp::LVal& lVal) -> RuntimeValue {
                    return *resolveLValue(lVal);
                },
                [&](const frontend::Exp::Number& number) -> RuntimeValue {
                    return number.value;
                });
        }

        [[nodiscard]] RuntimeValue evaluateBinary(
            const frontend::Exp::Binary& binary)
        {
            if (binary.op == frontend::BinaryOpKeyword::andAnd) {
                return isTruthy(evaluateExp(binary.lhs))
                    && isTruthy(evaluateExp(binary.rhs));
            }
            if (binary.op == frontend::BinaryOpKeyword::orOr) {
                return isTruthy(evaluateExp(binary.lhs))
                    || isTruthy(evaluateExp(binary.rhs));
            }

            RuntimeValue lhs = evaluateExp(binary.lhs);
            RuntimeValue rhs = evaluateExp(binary.rhs);
            if (std::holds_alternative<Poly>(lhs)
                || std::holds_alternative<Poly>(rhs)) {
                return evaluatePolyBinary(binary.op, lhs, rhs);
            }
            if (std::holds_alternative<Mint>(lhs)
                || std::holds_alternative<Mint>(rhs)) {
                return evaluateMintBinary(binary.op, lhs, rhs);
            }
            return evaluateIntBinary(binary.op, asInt(lhs), asInt(rhs));
        }

        [[nodiscard]] RuntimeValue evaluatePolyBinary(
            frontend::BinaryOpKeyword op, const RuntimeValue& lhs,
            const RuntimeValue& rhs)
        {
            switch (op) {
            case frontend::BinaryOpKeyword::plus:
                return asPoly(lhs) + asPoly(rhs);
            case frontend::BinaryOpKeyword::minus:
                return asPoly(lhs) - asPoly(rhs);
            case frontend::BinaryOpKeyword::star:
                if (std::holds_alternative<Poly>(lhs)
                    && std::holds_alternative<Poly>(rhs)) {
                    return asPoly(lhs) * asPoly(rhs);
                }
                if (std::holds_alternative<Poly>(lhs)) {
                    return asPoly(lhs) * asMint(rhs);
                }
                return asMint(lhs) * asPoly(rhs);
            case frontend::BinaryOpKeyword::shl:
                return asPoly(lhs).shiftLeft(asInt(rhs));
            case frontend::BinaryOpKeyword::sar:
                return asPoly(lhs).shiftRight(asInt(rhs));
            default:
                throw unsupported("unsupported poly binary operator");
            }
        }

        [[nodiscard]] RuntimeValue evaluateMintBinary(
            frontend::BinaryOpKeyword op, const RuntimeValue& lhs,
            const RuntimeValue& rhs)
        {
            const Mint left = asMint(lhs);
            const Mint right = asMint(rhs);
            switch (op) {
            case frontend::BinaryOpKeyword::plus:
                return left + right;
            case frontend::BinaryOpKeyword::minus:
                return left - right;
            case frontend::BinaryOpKeyword::star:
                return left * right;
            case frontend::BinaryOpKeyword::slash:
                return left / right;
            case frontend::BinaryOpKeyword::equal:
                return left == right;
            case frontend::BinaryOpKeyword::notEqual:
                return left != right;
            default:
                return evaluateIntBinary(op, left.value(), right.value());
            }
        }

        [[nodiscard]] RuntimeValue evaluateIntBinary(
            frontend::BinaryOpKeyword op, int32_t lhs, int32_t rhs) const
        {
            switch (op) {
            case frontend::BinaryOpKeyword::star:
                return lhs * rhs;
            case frontend::BinaryOpKeyword::slash:
                if (rhs == 0) {
                    throw RuntimeFault(ExecuteStatus::divisionByZero,
                        "division by zero: " + std::to_string(lhs) + " / "
                            + std::to_string(rhs));
                }
                return lhs / rhs;
            case frontend::BinaryOpKeyword::percent:
                if (rhs == 0) {
                    throw RuntimeFault(ExecuteStatus::divisionByZero,
                        "modulo by zero: " + std::to_string(lhs) + " % "
                            + std::to_string(rhs));
                }
                return lhs % rhs;
            case frontend::BinaryOpKeyword::plus:
                return lhs + rhs;
            case frontend::BinaryOpKeyword::minus:
                return lhs - rhs;
            case frontend::BinaryOpKeyword::shl:
                return lhs << rhs;
            case frontend::BinaryOpKeyword::sar:
                return lhs >> rhs;
            case frontend::BinaryOpKeyword::less:
                return lhs < rhs;
            case frontend::BinaryOpKeyword::greater:
                return lhs > rhs;
            case frontend::BinaryOpKeyword::lessEqual:
                return lhs <= rhs;
            case frontend::BinaryOpKeyword::greaterEqual:
                return lhs >= rhs;
            case frontend::BinaryOpKeyword::equal:
                return lhs == rhs;
            case frontend::BinaryOpKeyword::notEqual:
                return lhs != rhs;
            case frontend::BinaryOpKeyword::bitAnd:
                return lhs & rhs;
            case frontend::BinaryOpKeyword::bitXor:
                return lhs ^ rhs;
            case frontend::BinaryOpKeyword::bitOr:
                return lhs | rhs;
            case frontend::BinaryOpKeyword::andAnd:
            case frontend::BinaryOpKeyword::orOr:
                break;
            }
            throw RuntimeFault(
                ExecuteStatus::runtimeError, "invalid binary op");
        }

        [[nodiscard]] RuntimeValue evaluateUnary(
            const frontend::Exp::Unary& unary)
        {
            RuntimeValue value = evaluateExp(unary.lhs);
            if (std::holds_alternative<Poly>(value)
                && unary.op == frontend::UnaryOpKeyword::bang) {
                return asPoly(value).length();
            }
            if (std::holds_alternative<Mint>(value)) {
                const Mint mint = asMint(value);
                switch (unary.op) {
                case frontend::UnaryOpKeyword::plus:
                    return mint;
                case frontend::UnaryOpKeyword::minus:
                    return -mint;
                case frontend::UnaryOpKeyword::bang:
                    return mint.value() == 0;
                case frontend::UnaryOpKeyword::tilde:
                    throw unsupported("bitwise not on mint is unsupported");
                }
            }
            const int32_t integer = asInt(value);
            switch (unary.op) {
            case frontend::UnaryOpKeyword::plus:
                return integer;
            case frontend::UnaryOpKeyword::minus:
                return -integer;
            case frontend::UnaryOpKeyword::bang:
                return integer == 0;
            case frontend::UnaryOpKeyword::tilde:
                return ~integer;
            }
            throw RuntimeFault(ExecuteStatus::runtimeError, "invalid unary op");
        }

        [[nodiscard]] RuntimeValue evaluateCast(const frontend::Exp::Cast& cast)
        {
            RuntimeValue value = evaluateExp(cast.value);
            switch (cast.targetType) {
            case frontend::BTypeKeyword::intKeyword:
                return asInt(value);
            case frontend::BTypeKeyword::mintKeyword:
                return asMint(value);
            case frontend::BTypeKeyword::polyKeyword:
                return Poly(asMint(value));
            }
            throw RuntimeFault(ExecuteStatus::runtimeError, "invalid cast");
        }

        [[nodiscard]] RuntimeValue* resolveLValue(Ref<frontend::Exp> exp)
        {
            const auto* lVal
                = std::get_if<frontend::Exp::LVal>(&exp(m_ast).kind);
            if (lVal == nullptr) {
                throw RuntimeFault(ExecuteStatus::runtimeError,
                    "assignment target is not an lvalue");
            }
            return resolveLValue(*lVal);
        }

        [[nodiscard]] RuntimeValue* resolveLValue(
            const frontend::Exp::LVal& lVal)
        {
            const int32_t symbolId = requireSymbolId(lVal.identifier);
            RuntimeValue* base = lookup(symbolId);
            if (lVal.indices.empty()) {
                return base;
            }
            std::vector<int32_t> indices;
            indices.reserve(lVal.indices.size());
            for (const auto& index : lVal.indices) {
                indices.push_back(asInt(evaluateExp(index)));
            }
            if (std::holds_alternative<Poly>(*base)) {
                if (indices.size() != 1) {
                    throw RuntimeFault(ExecuteStatus::runtimeError,
                        "poly lvalue expects one coefficient index");
                }
                m_temporaryValues.push_back(asPoly(*base).coeff(indices[0]));
                return &m_temporaryValues.back();
            }
            if (!std::holds_alternative<ArrayPtr>(*base)) {
                throw RuntimeFault(ExecuteStatus::runtimeError,
                    "value is not array: " + lVal.identifier(m_ast).name);
            }
            ArrayPtr array = asArray(*base);
            if (indices.size() > array->dimensions.size()) {
                if (array->elementType.kind == frontend::SemanticTypeKind::poly
                    && indices.size() == array->dimensions.size() + 1) {
                    std::vector<int32_t> arrayIndices(indices.begin(),
                        indices.begin()
                            + static_cast<int64_t>(array->dimensions.size()));
                    RuntimeValue& polyValue = arrayCell(*array, arrayIndices);
                    m_temporaryValues.push_back(
                        asPoly(polyValue).coeff(indices.back()));
                    return &m_temporaryValues.back();
                }
                throw RuntimeFault(
                    ExecuteStatus::arrayOutOfBounds, "too many array indices");
            }
            if (static_cast<int32_t>(indices.size())
                == static_cast<int32_t>(array->dimensions.size())) {
                return &arrayCell(*array, indices);
            }
            RuntimeValue view = makeArrayView(*array, indices);
            m_temporaryValues.push_back(std::move(view));
            return &m_temporaryValues.back();
        }

        [[nodiscard]] RuntimeValue makeArrayView(
            const ArrayObject& array, const std::vector<int32_t>& indices) const
        {
            int32_t offset = array.offset;
            for (size_t index = 0; index < indices.size(); ++index) {
                const int32_t value = indices[index];
                if (value < 0 || value >= array.dimensions[index]) {
                    throw RuntimeFault(ExecuteStatus::arrayOutOfBounds,
                        "array index out of bounds");
                }
                offset += value * array.strides[index];
            }
            auto view = std::make_shared<ArrayObject>();
            view->elementType = array.elementType;
            view->dimensions.assign(
                array.dimensions.begin() + static_cast<int64_t>(indices.size()),
                array.dimensions.end());
            view->strides.assign(
                array.strides.begin() + static_cast<int64_t>(indices.size()),
                array.strides.end());
            view->cells = array.cells;
            view->offset = offset;
            if (!view->dimensions.empty()) {
                const int32_t remainingCells
                    = static_cast<int32_t>(view->cells->size()) - offset;
                view->dimensions[0] = remainingCells / view->strides[0];
            }
            return view;
        }

        [[nodiscard]] RuntimeValue& arrayCell(
            ArrayObject& array, const std::vector<int32_t>& indices) const
        {
            if (indices.size() > array.dimensions.size()) {
                throw RuntimeFault(
                    ExecuteStatus::arrayOutOfBounds, "too many array indices");
            }
            int32_t offset = array.offset;
            for (size_t index = 0; index < indices.size(); ++index) {
                if (indices[index] < 0
                    || indices[index] >= array.dimensions[index]) {
                    throw RuntimeFault(ExecuteStatus::arrayOutOfBounds,
                        "array index out of bounds");
                }
                offset += indices[index] * array.strides[index];
            }
            if (offset < 0
                || offset >= static_cast<int32_t>(array.cells->size())) {
                throw RuntimeFault(ExecuteStatus::arrayOutOfBounds,
                    "array offset out of bounds");
            }
            return (*array.cells)[static_cast<size_t>(offset)];
        }

        [[nodiscard]] RuntimeValue defaultValue(
            const frontend::SemanticType& type) const
        {
            switch (type.kind) {
            case frontend::SemanticTypeKind::integer:
            case frontend::SemanticTypeKind::boolean:
                return int32_t { 0 };
            case frontend::SemanticTypeKind::mint:
                return Mint(0);
            case frontend::SemanticTypeKind::poly:
                return Poly();
            case frontend::SemanticTypeKind::array:
                return makeArray(type);
            case frontend::SemanticTypeKind::voidType:
            case frontend::SemanticTypeKind::pv:
                break;
            }
            throw unsupported("unsupported default value type");
        }

        [[nodiscard]] RuntimeValue makeArray(
            const frontend::SemanticType& type) const
        {
            auto array = std::make_shared<ArrayObject>();
            const frontend::SemanticType* current = &type;
            while (current->kind == frontend::SemanticTypeKind::array) {
                array->dimensions.push_back(current->m_arrayLength);
                current = current->m_elementType.get();
                assert(current != nullptr);
            }
            array->elementType = *current;
            array->strides.resize(array->dimensions.size(), 1);
            for (int32_t index
                = static_cast<int32_t>(array->dimensions.size()) - 2;
                index >= 0; --index) {
                array->strides[static_cast<size_t>(index)]
                    = array->strides[static_cast<size_t>(index + 1)]
                    * array->dimensions[static_cast<size_t>(index + 1)];
            }
            const int32_t cells = leafCount(type);
            array->cells = std::make_shared<std::vector<RuntimeValue>>(
                static_cast<size_t>(cells), defaultValue(*current));
            return array;
        }

        [[nodiscard]] int32_t leafCount(
            const frontend::SemanticType& type) const
        {
            if (type.kind != frontend::SemanticTypeKind::array) {
                return 1;
            }
            return type.m_arrayLength * leafCount(*type.m_elementType);
        }

        void initializeValue(const frontend::SemanticType& type,
            RuntimeValue& value, Ref<frontend::InitVal> init)
        {
            MATCH(init(m_ast).kind)
            WITH(
                [&](const Ref<frontend::Exp>& exp) -> void {
                    value = coerceToType(type, evaluateExp(exp));
                },
                [&](const frontend::InitVal::List&) -> void {
                    int32_t cursor = 0;
                    initializeList(type, value, init, cursor, true);
                });
        }

        void initializeValue(const frontend::SemanticType& type,
            RuntimeValue& value, Ref<frontend::ConstInitVal> init)
        {
            MATCH(init(m_ast).kind)
            WITH(
                [&](const Ref<frontend::Exp>& exp) -> void {
                    value = coerceToType(type, evaluateExp(exp));
                },
                [&](const frontend::ConstInitVal::List&) -> void {
                    int32_t cursor = 0;
                    initializeList(type, value, init, cursor, true);
                });
        }

        void initializeList(const frontend::SemanticType& type,
            RuntimeValue& value, Ref<frontend::InitVal> init, int32_t& cursor,
            bool braced)
        {
            const auto* list
                = std::get_if<frontend::InitVal::List>(&init(m_ast).kind);
            if (list == nullptr) {
                initializeScalarOrArrayElement(type, value, init, cursor);
                return;
            }
            const int32_t start = cursor;
            for (const auto& child : *list) {
                initializeListElement(type, value, child, cursor);
            }
            if (braced) {
                cursor = start + leafCount(type);
            }
        }

        void initializeList(const frontend::SemanticType& type,
            RuntimeValue& value, Ref<frontend::ConstInitVal> init,
            int32_t& cursor, bool braced)
        {
            const auto* list
                = std::get_if<frontend::ConstInitVal::List>(&init(m_ast).kind);
            if (list == nullptr) {
                initializeScalarOrArrayElement(type, value, init, cursor);
                return;
            }
            const int32_t start = cursor;
            for (const auto& child : *list) {
                initializeListElement(type, value, child, cursor);
            }
            if (braced) {
                cursor = start + leafCount(type);
            }
        }

        void initializeListElement(const frontend::SemanticType& type,
            RuntimeValue& value, Ref<frontend::InitVal> init, int32_t& cursor)
        {
            if (type.kind != frontend::SemanticTypeKind::array) {
                initializeScalarOrArrayElement(type, value, init, cursor);
                return;
            }
            const auto& elementType = *type.m_elementType;
            const bool childBraced
                = std::holds_alternative<frontend::InitVal::List>(
                    init(m_ast).kind);
            initializeList(elementType, value, init, cursor, childBraced);
        }

        void initializeListElement(const frontend::SemanticType& type,
            RuntimeValue& value, Ref<frontend::ConstInitVal> init,
            int32_t& cursor)
        {
            if (type.kind != frontend::SemanticTypeKind::array) {
                initializeScalarOrArrayElement(type, value, init, cursor);
                return;
            }
            const auto& elementType = *type.m_elementType;
            const bool childBraced
                = std::holds_alternative<frontend::ConstInitVal::List>(
                    init(m_ast).kind);
            initializeList(elementType, value, init, cursor, childBraced);
        }

        void initializeScalarOrArrayElement(const frontend::SemanticType& type,
            RuntimeValue& value, Ref<frontend::InitVal> init, int32_t& cursor)
        {
            const auto* exp
                = std::get_if<Ref<frontend::Exp>>(&init(m_ast).kind);
            if (exp == nullptr) {
                initializeList(type, value, init, cursor, true);
                return;
            }
            writeInitializerValue(type, value, evaluateExp(*exp), cursor);
        }

        void initializeScalarOrArrayElement(const frontend::SemanticType& type,
            RuntimeValue& value, Ref<frontend::ConstInitVal> init,
            int32_t& cursor)
        {
            const auto* exp
                = std::get_if<Ref<frontend::Exp>>(&init(m_ast).kind);
            if (exp == nullptr) {
                initializeList(type, value, init, cursor, true);
                return;
            }
            writeInitializerValue(type, value, evaluateExp(*exp), cursor);
        }

        void writeInitializerValue(const frontend::SemanticType& type,
            RuntimeValue& value, RuntimeValue initValue, int32_t& cursor)
        {
            if (std::holds_alternative<ArrayPtr>(value)) {
                ArrayPtr array = asArray(value);
                if (cursor >= static_cast<int32_t>(array->cells->size())) {
                    return;
                }
                (*array->cells)[static_cast<size_t>(cursor++)] = coerceToType(
                    type.kind == frontend::SemanticTypeKind::array
                        ? array->elementType
                        : type,
                    std::move(initValue));
                return;
            }
            value = coerceToType(type, std::move(initValue));
            ++cursor;
        }

        [[nodiscard]] RuntimeValue coerceToType(
            const frontend::SemanticType& type, RuntimeValue value)
        {
            switch (type.kind) {
            case frontend::SemanticTypeKind::integer:
            case frontend::SemanticTypeKind::boolean:
                return asInt(value);
            case frontend::SemanticTypeKind::mint:
                return asMint(value);
            case frontend::SemanticTypeKind::poly:
                if (std::holds_alternative<Poly>(value)) {
                    return value;
                }
                return Poly(asMint(value));
            case frontend::SemanticTypeKind::array:
                return value;
            case frontend::SemanticTypeKind::voidType:
            case frontend::SemanticTypeKind::pv:
                break;
            }
            throw unsupported("unsupported coercion type");
        }

        [[nodiscard]] RuntimeValue* lookup(int32_t symbolId)
        {
            for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
                const auto valueIt = it->find(symbolId);
                if (valueIt != it->end()) {
                    return &valueIt->second;
                }
            }
            throw RuntimeFault(ExecuteStatus::runtimeError, "unknown variable");
        }

        [[nodiscard]] int32_t requireSymbolId(
            Ref<frontend::Identifier> identifier) const
        {
            const auto symbolId = m_semanticInfo.findSymbolId(identifier);
            if (!symbolId.has_value()) {
                throw RuntimeFault(
                    ExecuteStatus::runtimeError, "identifier has no symbol id");
            }
            return *symbolId;
        }

        [[nodiscard]] const frontend::SemanticSymbol::ObjectInfo&
        requireObjectSymbol(int32_t symbolId) const
        {
            const auto* symbol = m_semanticInfo.findSymbolById(symbolId);
            if (symbol == nullptr || !symbol->isObject()) {
                throw RuntimeFault(
                    ExecuteStatus::runtimeError, "symbol is not an object");
            }
            return symbol->object();
        }

        [[nodiscard]] static RuntimeFault unsupported(std::string message)
        {
            return RuntimeFault(ExecuteStatus::unsupported, std::move(message));
        }

        [[nodiscard]] static int32_t asInt(const RuntimeValue& value)
        {
            if (const auto* integer = std::get_if<int32_t>(&value)) {
                return *integer;
            }
            if (const auto* mint = std::get_if<Mint>(&value)) {
                return mint->value();
            }
            throw RuntimeFault(
                ExecuteStatus::runtimeError, "value is not integer");
        }

        [[nodiscard]] static Mint asMint(const RuntimeValue& value)
        {
            if (const auto* mint = std::get_if<Mint>(&value)) {
                return *mint;
            }
            if (const auto* integer = std::get_if<int32_t>(&value)) {
                return Mint(*integer);
            }
            throw RuntimeFault(
                ExecuteStatus::runtimeError, "value is not mint");
        }

        [[nodiscard]] static const Poly& asPoly(const RuntimeValue& value)
        {
            if (const auto* poly = std::get_if<Poly>(&value)) {
                return *poly;
            }
            throw RuntimeFault(
                ExecuteStatus::runtimeError, "value is not poly");
        }

        [[nodiscard]] static ArrayPtr asArray(const RuntimeValue& value)
        {
            if (const auto* array = std::get_if<ArrayPtr>(&value)) {
                return *array;
            }
            throw RuntimeFault(
                ExecuteStatus::runtimeError, "value is not array");
        }

        [[nodiscard]] static bool isTruthy(const RuntimeValue& value)
        {
            if (const auto* poly = std::get_if<Poly>(&value)) {
                return poly->length() != 0;
            }
            return asInt(value) != 0;
        }

        const frontend::AST& m_ast;
        frontend::Ref<frontend::CompUnit> m_root;
        const frontend::SemanticInfo& m_semanticInfo;
        std::istream& m_is;
        std::ostream& m_os;
        std::stop_token m_stopToken;
        std::unordered_map<std::string, Ref<frontend::FuncDef>> m_functions;
        std::vector<std::unordered_map<int32_t, RuntimeValue>> m_scopes;
        std::vector<RuntimeValue> m_temporaryValues;
        std::optional<RuntimeValue> m_builtinResult;
    };

} // namespace

ExecuteResult execute(const frontend::AST& ast,
    frontend::Ref<frontend::CompUnit> root,
    const frontend::SemanticInfo& semanticInfo, std::istream& is,
    std::ostream& os, std::stop_token stopToken)
{
    return Interpreter(ast, root, semanticInfo, is, os, stopToken).execute();
}

} // namespace yesod::test_support::poly::interpreter
