#ifndef _YESOD_TEST_KOOPA_TEST_SUPPORT_H_
#define _YESOD_TEST_KOOPA_TEST_SUPPORT_H_

#include <cctype>
#include <cstdint>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "koopa.h"

#include "frontend/ast.h"
#include "frontend/parser.h"
#include "frontend/semantic.h"
#include "koopa/ast_to_koopa.h"
#include "test_support.h"

namespace yesod::test_support::koopa {

namespace frontend = yesod::frontend;
namespace koopa_ir = yesod::koopa::ir;

using yesod::test_support::fail;
using yesod::test_support::OutputAstBase;
using yesod::test_support::require;
using yesod::test_support::TestBase;

using TopLevelItem = decltype(frontend::CompUnit::topLevelItems.front());

struct KoopaTestBase : OutputAstBase<frontend::ParseOutput>, TestBase {
    template <class Self> auto&& ast(this Self& self)
    {
        return self.m_output.m_ast;
    }
};

class BasicBlock;
class Function;

class Value {
public:
    enum class Kind {
        integer,
        zeroInit,
        undef,
        aggregate,
        funcArgRef,
        blockArgRef,
        alloc,
        globalAlloc,
        load,
        store,
        getPtr,
        getElemPtr,
        binary,
        branch,
        jump,
        call,
        ret,
    };

    explicit Value(Kind kind, std::string name = {})
        : m_kind(kind)
        , m_name(std::move(name))
    {
    }

    virtual ~Value() = default;

    [[nodiscard]] const std::string& getName() const { return m_name; }
    [[nodiscard]] bool hasName() const { return !m_name.empty(); }

    [[nodiscard]] bool isIntegerValue() const
    {
        return m_kind == Kind::integer;
    }
    [[nodiscard]] bool isZeroInitValue() const
    {
        return m_kind == Kind::zeroInit;
    }
    [[nodiscard]] bool isUndefValue() const { return m_kind == Kind::undef; }
    [[nodiscard]] bool isAggregateValue() const
    {
        return m_kind == Kind::aggregate;
    }
    [[nodiscard]] bool isFuncArgRefValue() const
    {
        return m_kind == Kind::funcArgRef;
    }
    [[nodiscard]] bool isBlockArgRefValue() const
    {
        return m_kind == Kind::blockArgRef;
    }
    [[nodiscard]] bool isAllocValue() const { return m_kind == Kind::alloc; }
    [[nodiscard]] bool isGlobalAllocValue() const
    {
        return m_kind == Kind::globalAlloc;
    }
    [[nodiscard]] bool isLoadValue() const { return m_kind == Kind::load; }
    [[nodiscard]] bool isStoreValue() const { return m_kind == Kind::store; }
    [[nodiscard]] bool isGetPtrValue() const { return m_kind == Kind::getPtr; }
    [[nodiscard]] bool isGetElemPtrValue() const
    {
        return m_kind == Kind::getElemPtr;
    }
    [[nodiscard]] bool isBinaryValue() const { return m_kind == Kind::binary; }
    [[nodiscard]] bool isBranchValue() const { return m_kind == Kind::branch; }
    [[nodiscard]] bool isJumpValue() const { return m_kind == Kind::jump; }
    [[nodiscard]] bool isCallValue() const { return m_kind == Kind::call; }
    [[nodiscard]] bool isReturnValue() const { return m_kind == Kind::ret; }

    [[nodiscard]] virtual bool canBeInitializer() const
    {
        return isIntegerValue() || isZeroInitValue() || isUndefValue();
    }

    [[nodiscard]] bool canTerminateBlock() const
    {
        return isBranchValue() || isJumpValue() || isReturnValue();
    }

    Value* adopt(std::unique_ptr<Value> value)
    {
        auto* ptr = value.get();
        m_ownedValues.push_back(std::move(value));
        return ptr;
    }

private:
    Kind m_kind;
    std::string m_name;
    std::vector<std::unique_ptr<Value>> m_ownedValues;
};

class IntegerValue : public Value {
public:
    explicit IntegerValue(int32_t value)
        : Value(Kind::integer)
        , m_value(value)
    {
    }

    [[nodiscard]] int32_t getVal() const { return m_value; }

private:
    int32_t m_value = 0;
};

class ZeroInitValue : public Value {
public:
    ZeroInitValue()
        : Value(Kind::zeroInit)
    {
    }
};

class UndefValue : public Value {
public:
    UndefValue()
        : Value(Kind::undef)
    {
    }
};

class AggregateValue : public Value {
public:
    AggregateValue()
        : Value(Kind::aggregate)
    {
    }

    void pushElement(std::unique_ptr<Value> element)
    {
        m_elements.push_back(adopt(std::move(element)));
    }

    [[nodiscard]] size_t getNumElements() const { return m_elements.size(); }
    [[nodiscard]] Value* getElement(size_t index) const
    {
        return m_elements.at(index);
    }
    [[nodiscard]] const std::vector<Value*>& elements() const
    {
        return m_elements;
    }

    [[nodiscard]] bool canBeInitializer() const override
    {
        if (m_elements.empty()) {
            return false;
        }
        for (const auto* element : m_elements) {
            if (!element->canBeInitializer()) {
                return false;
            }
        }
        return true;
    }

private:
    std::vector<Value*> m_elements;
};

class FuncArgRefValue : public Value {
public:
    explicit FuncArgRefValue(size_t index)
        : Value(Kind::funcArgRef)
        , m_index(index)
    {
    }

    [[nodiscard]] size_t getIndex() const { return m_index; }

private:
    size_t m_index = 0;
};

class BlockArgRefValue : public Value {
public:
    explicit BlockArgRefValue(size_t index)
        : Value(Kind::blockArgRef)
        , m_index(index)
    {
    }

    [[nodiscard]] size_t getIndex() const { return m_index; }

private:
    size_t m_index = 0;
};

class AllocValue : public Value {
public:
    explicit AllocValue(std::string name)
        : Value(Kind::alloc, std::move(name))
    {
    }
};

class GlobalAllocValue : public Value {
public:
    explicit GlobalAllocValue(std::string name)
        : Value(Kind::globalAlloc, std::move(name))
    {
    }

    void setInitVal(std::unique_ptr<Value> initVal)
    {
        m_initVal = adopt(std::move(initVal));
    }

    [[nodiscard]] Value* getInitVal() const { return m_initVal; }

private:
    Value* m_initVal = nullptr;
};

class LoadValue : public Value {
public:
    explicit LoadValue(std::string name)
        : Value(Kind::load, std::move(name))
    {
    }

    [[nodiscard]] Value* getSource() const { return m_source; }
    void setSource(Value* source) { m_source = source; }

private:
    Value* m_source = nullptr;
};

class StoreValue : public Value {
public:
    StoreValue()
        : Value(Kind::store)
    {
    }

    [[nodiscard]] Value* getVal() const { return m_value; }
    [[nodiscard]] Value* getDestination() const { return m_destination; }
    void setVal(Value* value) { m_value = value; }
    void setDestination(Value* destination) { m_destination = destination; }

private:
    Value* m_value = nullptr;
    Value* m_destination = nullptr;
};

class GetPtrValue : public Value {
public:
    explicit GetPtrValue(std::string name)
        : Value(Kind::getPtr, std::move(name))
    {
    }

    [[nodiscard]] Value* getSource() const { return m_source; }
    [[nodiscard]] Value* getIndex() const { return m_index; }
    void setSource(Value* source) { m_source = source; }
    void setIndex(Value* index) { m_index = index; }

private:
    Value* m_source = nullptr;
    Value* m_index = nullptr;
};

class GetElemPtrValue : public Value {
public:
    explicit GetElemPtrValue(std::string name)
        : Value(Kind::getElemPtr, std::move(name))
    {
    }

    [[nodiscard]] Value* getSource() const { return m_source; }
    [[nodiscard]] Value* getIndex() const { return m_index; }
    void setSource(Value* source) { m_source = source; }
    void setIndex(Value* index) { m_index = index; }

private:
    Value* m_source = nullptr;
    Value* m_index = nullptr;
};

class BinaryValue : public Value {
public:
    BinaryValue(koopa_raw_binary_op op, std::string name)
        : Value(Kind::binary, std::move(name))
        , m_op(op)
    {
    }

    [[nodiscard]] koopa_raw_binary_op getOp() const { return m_op; }
    [[nodiscard]] Value* getLhs() const { return m_lhs; }
    [[nodiscard]] Value* getRhs() const { return m_rhs; }
    void setLhs(Value* lhs) { m_lhs = lhs; }
    void setRhs(Value* rhs) { m_rhs = rhs; }

private:
    koopa_raw_binary_op m_op = KOOPA_RBO_ADD;
    Value* m_lhs = nullptr;
    Value* m_rhs = nullptr;
};

class BranchValue : public Value {
public:
    BranchValue()
        : Value(Kind::branch)
    {
    }

    [[nodiscard]] Value* getCondition() const { return m_condition; }
    [[nodiscard]] BasicBlock* getTrueBB() const { return m_trueBB; }
    [[nodiscard]] size_t getNumTrueArgs() const { return m_trueArgs.size(); }
    [[nodiscard]] Value* getTrueArg(size_t index) const
    {
        return m_trueArgs.at(index);
    }
    [[nodiscard]] const std::vector<Value*>& trueArgs() const
    {
        return m_trueArgs;
    }
    [[nodiscard]] BasicBlock* getFalseBB() const { return m_falseBB; }
    [[nodiscard]] size_t getNumFalseArgs() const { return m_falseArgs.size(); }
    [[nodiscard]] Value* getFalseArg(size_t index) const
    {
        return m_falseArgs.at(index);
    }
    [[nodiscard]] const std::vector<Value*>& falseArgs() const
    {
        return m_falseArgs;
    }

    void setCondition(Value* condition) { m_condition = condition; }
    void setTrueBB(BasicBlock* trueBB) { m_trueBB = trueBB; }
    void setFalseBB(BasicBlock* falseBB) { m_falseBB = falseBB; }
    void pushTrueArg(Value* arg) { m_trueArgs.push_back(arg); }
    void pushFalseArg(Value* arg) { m_falseArgs.push_back(arg); }

private:
    Value* m_condition = nullptr;
    BasicBlock* m_trueBB = nullptr;
    std::vector<Value*> m_trueArgs;
    BasicBlock* m_falseBB = nullptr;
    std::vector<Value*> m_falseArgs;
};

class JumpValue : public Value {
public:
    JumpValue()
        : Value(Kind::jump)
    {
    }

    [[nodiscard]] BasicBlock* getTargetBB() const { return m_targetBB; }
    [[nodiscard]] size_t getNumArgs() const { return m_args.size(); }
    [[nodiscard]] Value* getArg(size_t index) const { return m_args.at(index); }
    [[nodiscard]] const std::vector<Value*>& args() const { return m_args; }

    void setTargetBB(BasicBlock* targetBB) { m_targetBB = targetBB; }
    void pushArg(Value* arg) { m_args.push_back(arg); }

private:
    BasicBlock* m_targetBB = nullptr;
    std::vector<Value*> m_args;
};

class CallValue : public Value {
public:
    explicit CallValue(std::string name = {})
        : Value(Kind::call, std::move(name))
    {
    }

    [[nodiscard]] Function* getCallee() const { return m_callee; }
    [[nodiscard]] size_t getNumArgs() const { return m_args.size(); }
    [[nodiscard]] Value* getArg(size_t index) const { return m_args.at(index); }
    [[nodiscard]] const std::vector<Value*>& args() const { return m_args; }

    void setCallee(Function* callee) { m_callee = callee; }
    void pushArg(Value* arg) { m_args.push_back(arg); }

private:
    Function* m_callee = nullptr;
    std::vector<Value*> m_args;
};

class ReturnValue : public Value {
public:
    ReturnValue()
        : Value(Kind::ret)
    {
    }

    [[nodiscard]] Value* getVal() const { return m_value; }
    void setVal(Value* value) { m_value = value; }

private:
    Value* m_value = nullptr;
};

class BasicBlock {
public:
    BasicBlock(bool isEntry, std::string name)
        : m_isEntry(isEntry)
        , m_name(std::move(name))
    {
    }

    [[nodiscard]] bool isEntry() const { return m_isEntry; }
    [[nodiscard]] size_t getNumParams() const { return m_params.size(); }
    [[nodiscard]] Value* getParam(size_t index) const
    {
        return m_params.at(index);
    }
    [[nodiscard]] const std::vector<Value*>& params() const { return m_params; }
    [[nodiscard]] size_t getNumInsts() const { return m_insts.size(); }
    [[nodiscard]] Value* getInst(size_t index) const
    {
        return m_insts.at(index);
    }
    [[nodiscard]] const std::vector<Value*>& insts() const { return m_insts; }
    [[nodiscard]] const std::string& getName() const { return m_name; }

    template <typename T, typename... Args> T* addParam(Args&&... args)
    {
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        auto* ptr = value.get();
        m_paramStorage.push_back(std::move(value));
        m_params.push_back(ptr);
        return ptr;
    }

    template <typename T, typename... Args> T* addInst(Args&&... args)
    {
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        auto* ptr = value.get();
        m_instStorage.push_back(std::move(value));
        m_insts.push_back(ptr);
        return ptr;
    }

private:
    bool m_isEntry = false;
    std::string m_name;
    std::vector<std::unique_ptr<Value>> m_paramStorage;
    std::vector<Value*> m_params;
    std::vector<std::unique_ptr<Value>> m_instStorage;
    std::vector<Value*> m_insts;
};

class Function {
public:
    explicit Function(std::string name)
        : m_name(std::move(name))
    {
    }

    [[nodiscard]] size_t getNumParams() const { return m_params.size(); }
    [[nodiscard]] Value* getParam(size_t index) const
    {
        return m_params.at(index);
    }
    [[nodiscard]] const std::vector<Value*>& params() const { return m_params; }
    [[nodiscard]] size_t getNumBBs() const { return m_bbs.size(); }
    [[nodiscard]] BasicBlock* getBB(size_t index) const
    {
        return m_bbs.at(index);
    }
    [[nodiscard]] const std::vector<BasicBlock*>& bbs() const { return m_bbs; }
    [[nodiscard]] const std::string& getName() const { return m_name; }

    template <typename T, typename... Args> T* addParam(Args&&... args)
    {
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        auto* ptr = value.get();
        m_paramStorage.push_back(std::move(value));
        m_params.push_back(ptr);
        return ptr;
    }

    BasicBlock* addBlock(bool isEntry, const std::string& name)
    {
        auto block = std::make_unique<BasicBlock>(isEntry, name);
        auto* ptr = block.get();
        m_blockStorage.push_back(std::move(block));
        m_bbs.push_back(ptr);
        return ptr;
    }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Value>> m_paramStorage;
    std::vector<Value*> m_params;
    std::vector<std::unique_ptr<BasicBlock>> m_blockStorage;
    std::vector<BasicBlock*> m_bbs;
};

class Program {
public:
    [[nodiscard]] size_t getNumVals() const { return m_vals.size(); }
    [[nodiscard]] Value* getVal(size_t index) const { return m_vals.at(index); }
    [[nodiscard]] const std::vector<Value*>& vals() const { return m_vals; }
    [[nodiscard]] size_t getNumFuncs() const { return m_funcs.size(); }
    [[nodiscard]] Function* getFunc(size_t index) const
    {
        return m_funcs.at(index);
    }
    [[nodiscard]] const std::vector<Function*>& funcs() const
    {
        return m_funcs;
    }

    template <typename T, typename... Args> T* addGlobal(Args&&... args)
    {
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        auto* ptr = value.get();
        m_valStorage.push_back(std::move(value));
        m_vals.push_back(ptr);
        return ptr;
    }

    Function* addFunction(const std::string& name)
    {
        auto function = std::make_unique<Function>(name);
        auto* ptr = function.get();
        m_functionStorage.push_back(std::move(function));
        m_funcs.push_back(ptr);
        return ptr;
    }

private:
    std::vector<std::unique_ptr<Value>> m_valStorage;
    std::vector<Value*> m_vals;
    std::vector<std::unique_ptr<Function>> m_functionStorage;
    std::vector<Function*> m_funcs;
};

inline bool matchesExpectedTempName(
    const std::string& actualName, const std::string& expectedName)
{
    if (actualName == expectedName) {
        return true;
    }

    if (expectedName.size() < 2 || expectedName.front() != '%') {
        return false;
    }

    for (size_t i = 1; i < expectedName.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(expectedName[i]))) {
            return false;
        }
    }

    return actualName == "%t" + expectedName.substr(1);
}

inline frontend::ParseOutput parseSource(const std::string& source)
{
    frontend::Parser parser(source);
    auto output = parser.parse();
    bindCurrentAst(output.m_ast);
    return output;
}

inline frontend::ParseOutput parseRoot(const std::string& source)
{
    auto output = parseSource(source);
    if (!output.success()) {
        fail("expected parse success before Koopa generation");
    }
    return output;
}

namespace detail {

    inline koopa_raw_binary_op toRawBinaryOp(koopa_ir::BinaryOp op)
    {
        switch (op) {
        case koopa_ir::BinaryOp::ne:
            return KOOPA_RBO_NOT_EQ;
        case koopa_ir::BinaryOp::eq:
            return KOOPA_RBO_EQ;
        case koopa_ir::BinaryOp::gt:
            return KOOPA_RBO_GT;
        case koopa_ir::BinaryOp::lt:
            return KOOPA_RBO_LT;
        case koopa_ir::BinaryOp::ge:
            return KOOPA_RBO_GE;
        case koopa_ir::BinaryOp::le:
            return KOOPA_RBO_LE;
        case koopa_ir::BinaryOp::add:
            return KOOPA_RBO_ADD;
        case koopa_ir::BinaryOp::sub:
            return KOOPA_RBO_SUB;
        case koopa_ir::BinaryOp::mul:
            return KOOPA_RBO_MUL;
        case koopa_ir::BinaryOp::div:
            return KOOPA_RBO_DIV;
        case koopa_ir::BinaryOp::mod:
            return KOOPA_RBO_MOD;
        case koopa_ir::BinaryOp::bitAnd:
            return KOOPA_RBO_AND;
        case koopa_ir::BinaryOp::bitOr:
            return KOOPA_RBO_OR;
        case koopa_ir::BinaryOp::bitXor:
            return KOOPA_RBO_XOR;
        case koopa_ir::BinaryOp::shl:
            return KOOPA_RBO_SHL;
        case koopa_ir::BinaryOp::shr:
            return KOOPA_RBO_SHR;
        case koopa_ir::BinaryOp::sar:
            return KOOPA_RBO_SAR;
        }

        throw std::runtime_error("unsupported Koopa IR binary op");
    }

    class IrProgramViewBuilder {
    public:
        explicit IrProgramViewBuilder(const koopa_ir::Program& program)
            : m_ir(program)
        {
        }

        [[nodiscard]] std::unique_ptr<Program> build()
        {
            auto program = std::make_unique<Program>();

            for (const auto& item : m_ir.items) {
                std::visit(
                    [&](auto itemRef) {
                        using Item
                            = std::remove_cvref_t<decltype(m_ir[itemRef])>;
                        if constexpr (std::same_as<Item,
                                          koopa_ir::GlobalMemoryDef>) {
                            const auto& global = m_ir[itemRef];
                            auto* value = program->addGlobal<GlobalAllocValue>(
                                global.name.spelling);
                            value->setInitVal(
                                lowerInitializer(global.initializer));
                            m_globalsByName.emplace(
                                global.name.spelling, value);
                        } else if constexpr (std::same_as<Item,
                                                 koopa_ir::FunctionDecl>) {
                            const auto& function = m_ir[itemRef];
                            auto* viewFunction
                                = program->addFunction(function.name.spelling);
                            m_functionsByName.emplace(
                                function.name.spelling, viewFunction);
                        } else if constexpr (std::same_as<Item,
                                                 koopa_ir::FunctionDef>) {
                            const auto& function = m_ir[itemRef];
                            auto* viewFunction
                                = program->addFunction(function.name.spelling);
                            m_functionsByName.emplace(
                                function.name.spelling, viewFunction);
                        }
                    },
                    item);
            }

            size_t functionIndex = 0;
            for (const auto& item : m_ir.items) {
                std::visit(
                    [&](auto itemRef) {
                        using Item
                            = std::remove_cvref_t<decltype(m_ir[itemRef])>;
                        if constexpr (std::same_as<Item,
                                          koopa_ir::FunctionDecl>) {
                            fillFunctionDecl(m_ir[itemRef],
                                *program->getFunc(functionIndex++));
                        } else if constexpr (std::same_as<Item,
                                                 koopa_ir::FunctionDef>) {
                            fillFunctionDef(m_ir[itemRef],
                                *program->getFunc(functionIndex++));
                        }
                    },
                    item);
            }

            return program;
        }

    private:
        struct FunctionContext {
            Function* function = nullptr;
            std::unordered_map<std::string, Value*> valuesBySymbol;
            std::unordered_map<std::string, BasicBlock*> blocksByLabel;
        };

        const koopa_ir::Program& m_ir;
        std::unordered_map<std::string, GlobalAllocValue*> m_globalsByName;
        std::unordered_map<std::string, Function*> m_functionsByName;

        [[nodiscard]] std::unique_ptr<Value> lowerInitializer(
            const koopa_ir::Initializer& initializer)
        {
            return std::visit(
                [&](const auto& initAlt) -> std::unique_ptr<Value> {
                    using Init = std::remove_cvref_t<decltype(initAlt)>;
                    if constexpr (std::same_as<Init,
                                      koopa_ir::IntegerLiteral>) {
                        return std::make_unique<IntegerValue>(initAlt.value);
                    } else if constexpr (std::same_as<Init,
                                             koopa_ir::UndefValue>) {
                        return std::make_unique<UndefValue>();
                    } else if constexpr (std::same_as<Init,
                                             koopa_ir::ZeroInit>) {
                        return std::make_unique<ZeroInitValue>();
                    } else {
                        const auto& aggregate = m_ir[initAlt];
                        auto value = std::make_unique<AggregateValue>();
                        for (const auto& element : aggregate.elements) {
                            value->pushElement(lowerInitializer(element));
                        }
                        return value;
                    }
                },
                initializer);
        }

        [[nodiscard]] Value* lookupValue(
            std::string_view symbol, const FunctionContext& context) const
        {
            const std::string spelling(symbol);
            if (const auto valueIt = context.valuesBySymbol.find(spelling);
                valueIt != context.valuesBySymbol.end()) {
                return valueIt->second;
            }
            if (const auto globalIt = m_globalsByName.find(spelling);
                globalIt != m_globalsByName.end()) {
                return globalIt->second;
            }
            throw std::runtime_error(
                "unknown Koopa value symbol in test view: " + spelling);
        }

        [[nodiscard]] Function* lookupFunction(std::string_view symbol) const
        {
            const auto functionIt = m_functionsByName.find(std::string(symbol));
            if (functionIt == m_functionsByName.end()) {
                throw std::runtime_error(
                    "unknown Koopa callee symbol in test view");
            }
            return functionIt->second;
        }

        [[nodiscard]] BasicBlock* lookupBlock(
            std::string_view label, const FunctionContext& context) const
        {
            const auto blockIt = context.blocksByLabel.find(std::string(label));
            if (blockIt == context.blocksByLabel.end()) {
                throw std::runtime_error(
                    "unknown Koopa basic block label in test view");
            }
            return blockIt->second;
        }

        [[nodiscard]] Value* lowerValueOperand(const koopa_ir::Value& value,
            Value& owner, const FunctionContext& context)
        {
            return std::visit(
                [&](const auto& valueAlt) -> Value* {
                    using Operand = std::remove_cvref_t<decltype(valueAlt)>;
                    if constexpr (std::same_as<Operand, koopa_ir::Symbol>) {
                        return lookupValue(valueAlt.spelling, context);
                    } else if constexpr (std::same_as<Operand,
                                             koopa_ir::IntegerLiteral>) {
                        return owner.adopt(
                            std::make_unique<IntegerValue>(valueAlt.value));
                    } else {
                        return owner.adopt(std::make_unique<UndefValue>());
                    }
                },
                value);
        }

        [[nodiscard]] Value* lowerStoreOperand(
            const koopa_ir::StoreValue& value, Value& owner,
            const FunctionContext& context)
        {
            return std::visit(
                [&](const auto& valueAlt) -> Value* {
                    using Operand = std::remove_cvref_t<decltype(valueAlt)>;
                    if constexpr (std::same_as<Operand, koopa_ir::Symbol>) {
                        return lookupValue(valueAlt.spelling, context);
                    } else if constexpr (std::same_as<Operand,
                                             koopa_ir::IntegerLiteral>) {
                        return owner.adopt(
                            std::make_unique<IntegerValue>(valueAlt.value));
                    } else if constexpr (std::same_as<Operand,
                                             koopa_ir::UndefValue>) {
                        return owner.adopt(std::make_unique<UndefValue>());
                    } else if constexpr (std::same_as<Operand,
                                             koopa_ir::ZeroInit>) {
                        return owner.adopt(std::make_unique<ZeroInitValue>());
                    } else {
                        return owner.adopt(lowerInitializer(
                            koopa_ir::Initializer { valueAlt }));
                    }
                },
                value);
        }

        void fillFunctionDecl(
            const koopa_ir::FunctionDecl& function, Function& view)
        {
            for (size_t index = 0; index < function.paramTypes.size();
                 ++index) {
                (void)view.addParam<FuncArgRefValue>(index);
            }
        }

        void fillFunctionDef(
            const koopa_ir::FunctionDef& function, Function& view)
        {
            FunctionContext context { .function = &view };

            for (size_t index = 0; index < function.params.size(); ++index) {
                const auto& param = m_ir[function.params[index]];
                auto* paramValue = view.addParam<FuncArgRefValue>(index);
                context.valuesBySymbol.emplace(
                    param.symbol.spelling, paramValue);
            }

            for (size_t blockIndex = 0; blockIndex < function.blocks.size();
                 ++blockIndex) {
                const auto& block = m_ir[function.blocks[blockIndex]];
                auto* blockView
                    = view.addBlock(blockIndex == 0, block.label.spelling);
                context.blocksByLabel.emplace(block.label.spelling, blockView);
            }

            for (size_t blockIndex = 0; blockIndex < function.blocks.size();
                 ++blockIndex) {
                const auto& block = m_ir[function.blocks[blockIndex]];
                auto* blockView = view.getBB(blockIndex);

                for (size_t paramIndex = 0; paramIndex < block.params.size();
                     ++paramIndex) {
                    const auto& param = m_ir[block.params[paramIndex]];
                    auto* paramValue
                        = blockView->addParam<BlockArgRefValue>(paramIndex);
                    context.valuesBySymbol[param.symbol.spelling] = paramValue;
                }

                for (const auto& statement : block.statements) {
                    std::visit(
                        [&](auto statementRef) {
                            using Statement = std::remove_cvref_t<
                                decltype(m_ir[statementRef])>;
                            if constexpr (std::same_as<Statement,
                                              koopa_ir::SymbolDef>) {
                                lowerSymbolDef(
                                    m_ir[statementRef], *blockView, context);
                            } else if constexpr (std::same_as<Statement,
                                                     koopa_ir::StoreStmt>) {
                                lowerStoreStmt(
                                    m_ir[statementRef], *blockView, context);
                            } else if constexpr (std::same_as<Statement,
                                                     koopa_ir::CallExpr>) {
                                (void)lowerCallExpr(m_ir[statementRef],
                                    *blockView, context, std::nullopt);
                            }
                        },
                        statement);
                }

                std::visit(
                    [&](auto terminatorRef) {
                        using Terminator = std::remove_cvref_t<
                            decltype(m_ir[terminatorRef])>;
                        if constexpr (std::same_as<Terminator,
                                          koopa_ir::BranchTerminator>) {
                            lowerBranch(
                                m_ir[terminatorRef], *blockView, context);
                        } else if constexpr (std::same_as<Terminator,
                                                 koopa_ir::JumpTerminator>) {
                            lowerJump(m_ir[terminatorRef], *blockView, context);
                        } else {
                            lowerReturn(
                                m_ir[terminatorRef], *blockView, context);
                        }
                    },
                    block.terminator);
            }
        }

        void lowerSymbolDef(const koopa_ir::SymbolDef& symbolDef,
            BasicBlock& block, FunctionContext& context)
        {
            Value* definedValue = std::visit(
                [&](auto rhsRef) -> Value* {
                    using Rhs = std::remove_cvref_t<decltype(m_ir[rhsRef])>;
                    if constexpr (std::same_as<Rhs,
                                      koopa_ir::MemoryDeclaration>) {
                        return block.addInst<AllocValue>(
                            symbolDef.symbol.spelling);
                    } else if constexpr (std::same_as<Rhs,
                                             koopa_ir::LoadExpr>) {
                        const auto& loadExpr = m_ir[rhsRef];
                        auto* value = block.addInst<LoadValue>(
                            symbolDef.symbol.spelling);
                        value->setSource(
                            lookupValue(loadExpr.source.spelling, context));
                        return value;
                    } else if constexpr (std::same_as<Rhs,
                                             koopa_ir::GetPointerExpr>) {
                        const auto& getPtrExpr = m_ir[rhsRef];
                        auto* value = block.addInst<GetPtrValue>(
                            symbolDef.symbol.spelling);
                        value->setSource(
                            lookupValue(getPtrExpr.source.spelling, context));
                        value->setIndex(lowerValueOperand(
                            getPtrExpr.index, *value, context));
                        return value;
                    } else if constexpr (std::same_as<Rhs,
                                             koopa_ir::GetElementPointerExpr>) {
                        const auto& getElemPtrExpr = m_ir[rhsRef];
                        auto* value = block.addInst<GetElemPtrValue>(
                            symbolDef.symbol.spelling);
                        value->setSource(lookupValue(
                            getElemPtrExpr.source.spelling, context));
                        value->setIndex(lowerValueOperand(
                            getElemPtrExpr.index, *value, context));
                        return value;
                    } else if constexpr (std::same_as<Rhs,
                                             koopa_ir::BinaryExpr>) {
                        const auto& binaryExpr = m_ir[rhsRef];
                        auto* value = block.addInst<BinaryValue>(
                            toRawBinaryOp(binaryExpr.op),
                            symbolDef.symbol.spelling);
                        value->setLhs(
                            lowerValueOperand(binaryExpr.lhs, *value, context));
                        value->setRhs(
                            lowerValueOperand(binaryExpr.rhs, *value, context));
                        return value;
                    } else if constexpr (std::same_as<Rhs,
                                             koopa_ir::CallExpr>) {
                        return lowerCallExpr(m_ir[rhsRef], block, context,
                            symbolDef.symbol.spelling);
                    } else {
                        return block.addInst<Value>(
                            Value::Kind::call, symbolDef.symbol.spelling);
                    }
                },
                symbolDef.rhs);

            context.valuesBySymbol[symbolDef.symbol.spelling] = definedValue;
        }

        Value* lowerCallExpr(const koopa_ir::CallExpr& callExpr,
            BasicBlock& block, const FunctionContext& context,
            const std::optional<std::string>& name)
        {
            auto* value
                = block.addInst<CallValue>(name.value_or(std::string {}));
            value->setCallee(lookupFunction(callExpr.callee.spelling));
            for (const auto& arg : callExpr.args) {
                value->pushArg(lowerValueOperand(arg, *value, context));
            }
            return value;
        }

        void lowerStoreStmt(const koopa_ir::StoreStmt& storeStmt,
            BasicBlock& block, const FunctionContext& context)
        {
            auto* value = block.addInst<StoreValue>();
            value->setVal(lowerStoreOperand(storeStmt.value, *value, context));
            value->setDestination(
                lookupValue(storeStmt.destination.spelling, context));
        }

        void lowerBranch(const koopa_ir::BranchTerminator& branch,
            BasicBlock& block, const FunctionContext& context)
        {
            auto* value = block.addInst<BranchValue>();
            value->setCondition(
                lowerValueOperand(branch.condition, *value, context));
            value->setTrueBB(lookupBlock(branch.trueTarget.spelling, context));
            for (const auto& arg : branch.trueArgs) {
                value->pushTrueArg(lowerValueOperand(arg, *value, context));
            }
            value->setFalseBB(
                lookupBlock(branch.falseTarget.spelling, context));
            for (const auto& arg : branch.falseArgs) {
                value->pushFalseArg(lowerValueOperand(arg, *value, context));
            }
        }

        void lowerJump(const koopa_ir::JumpTerminator& jump, BasicBlock& block,
            const FunctionContext& context)
        {
            auto* value = block.addInst<JumpValue>();
            value->setTargetBB(lookupBlock(jump.target.spelling, context));
            for (const auto& arg : jump.args) {
                value->pushArg(lowerValueOperand(arg, *value, context));
            }
        }

        void lowerReturn(const koopa_ir::ReturnTerminator& ret,
            BasicBlock& block, const FunctionContext& context)
        {
            auto* value = block.addInst<ReturnValue>();
            if (ret.value.has_value()) {
                value->setVal(lowerValueOperand(*ret.value, *value, context));
            }
        }
    };

} // namespace detail

inline std::unique_ptr<koopa_ir::Program> generateIrProgram(
    const std::string& source)
{
    auto rootOutput = parseRoot(source);
    frontend::SemanticAnalyzer semanticAnalyzer;
    auto semanticOutput = semanticAnalyzer.analyze(
        std::move(rootOutput.m_ast), rootOutput.m_root.ref());
    bindCurrentAst(semanticOutput.m_ast);
    if (!semanticOutput.success()) {
        fail("expected semantic success before Koopa generation");
    }

    yesod::koopa::Generator generator;
    return generator.generateIr(
        semanticOutput.m_ast, semanticOutput.m_root, semanticOutput.m_info);
}

inline std::unique_ptr<Program> generateProgram(const std::string& source)
{
    auto irProgram = generateIrProgram(source);
    detail::IrProgramViewBuilder builder(*irProgram);
    return builder.build();
}

inline void requireProgramWellFormed(const Program& program)
{
    std::unordered_set<const Function*> functionSet(
        program.funcs().begin(), program.funcs().end());
    for (const auto* value : program.vals()) {
        require(value->isGlobalAllocValue(),
            "top-level values should all be global allocs");
        const auto* globalAlloc = dynamic_cast<const GlobalAllocValue*>(value);
        require(globalAlloc != nullptr, "expected global alloc cast");
        require(globalAlloc->getInitVal() != nullptr,
            "global alloc should preserve an initializer");
        require(globalAlloc->getInitVal()->canBeInitializer(),
            "global alloc initializer should remain structurally valid");
    }

    for (const auto* function : program.funcs()) {
        require(function != nullptr, "function view should be non-null");
        if (function->getNumBBs() == 0) {
            continue;
        }

        require(function->getBB(0)->isEntry(),
            "function definitions should preserve the entry block at index 0");

        std::unordered_set<const BasicBlock*> blockSet(
            function->bbs().begin(), function->bbs().end());
        for (const auto* block : function->bbs()) {
            require(block->getNumInsts() > 0,
                "basic block should contain at least one terminator "
                "instruction");
            const auto* terminator = block->getInst(block->getNumInsts() - 1);
            require(terminator->canTerminateBlock(),
                "basic block should end with a terminator");

            if (const auto* branch
                = dynamic_cast<const BranchValue*>(terminator)) {
                require(branch->getTrueBB() != nullptr
                        && branch->getFalseBB() != nullptr,
                    "branch targets should be preserved");
                require(blockSet.contains(branch->getTrueBB())
                        && blockSet.contains(branch->getFalseBB()),
                    "branch targets should belong to the same function");
                require(branch->getNumTrueArgs()
                        == branch->getTrueBB()->getNumParams(),
                    "branch true-edge args should match target block params");
                require(branch->getNumFalseArgs()
                        == branch->getFalseBB()->getNumParams(),
                    "branch false-edge args should match target block params");
            }

            if (const auto* jump = dynamic_cast<const JumpValue*>(terminator)) {
                require(jump->getTargetBB() != nullptr,
                    "jump target should be preserved");
                require(blockSet.contains(jump->getTargetBB()),
                    "jump target should belong to the same function");
                require(
                    jump->getNumArgs() == jump->getTargetBB()->getNumParams(),
                    "jump args should match target block params");
            }

            if (const auto* call = dynamic_cast<const CallValue*>(terminator)) {
                require(functionSet.contains(call->getCallee()),
                    "call callee should resolve to a function view");
            }
        }
    }
}

inline const Function* requireOnlyFunction(const Program& program)
{
    require(program.getNumFuncs() == 1, "expected exactly one function");
    return program.getFunc(0);
}

inline const Function* requireFunctionByName(
    const Program& program, const std::string& expectedName)
{
    for (const auto* function : program.funcs()) {
        if (function->getName() == expectedName) {
            return function;
        }
    }
    fail("expected function named '" + expectedName + "'");
}

inline const GlobalAllocValue* requireGlobalAlloc(
    const Value* value, const std::string& expectedName)
{
    require(value != nullptr, "expected global alloc value");
    require(value->isGlobalAllocValue(), "expected global alloc value kind");
    const auto* globalAlloc = dynamic_cast<const GlobalAllocValue*>(value);
    require(globalAlloc != nullptr, "expected global alloc value cast");
    require(globalAlloc->getName() == expectedName,
        "global alloc should preserve the expected name");
    return globalAlloc;
}

inline const AggregateValue* requireAggregate(
    const Value* value, size_t expectedNumElements)
{
    require(value != nullptr, "expected aggregate value");
    require(value->isAggregateValue(), "expected aggregate initializer value");
    const auto* aggregateValue = dynamic_cast<const AggregateValue*>(value);
    require(aggregateValue != nullptr, "expected aggregate initializer cast");
    require(aggregateValue->getNumElements() == expectedNumElements,
        "aggregate initializer should preserve the expected number of "
        "elements");
    return aggregateValue;
}

inline const BasicBlock* requireEntryBlock(const Function& function)
{
    require(function.getNumBBs() >= 2,
        "expected entry body plus synthesized guard end body");
    const auto* basicBlock = function.getBB(0);
    require(basicBlock->isEntry(), "first basic body should be the entry body");
    require(basicBlock->getName() == "%entry",
        "entry body should use the documented label");
    return basicBlock;
}

inline const BasicBlock* requireEndBlock(const Function& function)
{
    require(function.getNumBBs() >= 2,
        "expected entry body plus synthesized guard end body");
    for (size_t index = 0; index < function.getNumBBs(); ++index) {
        const auto* basicBlock = function.getBB(index);
        if (basicBlock->getName() != "%end") {
            continue;
        }
        require(!basicBlock->isEntry(),
            "guard end body should not be the entry body");
        return basicBlock;
    }

    fail("expected basic body named '%end'");
}

inline const BasicBlock* requireBlock(const Function& function, size_t index)
{
    require(
        index < function.getNumBBs(), "expected basic body at requested index");
    return function.getBB(index);
}

inline const IntegerValue* requireInteger(
    const Value* value, int32_t expectedValue)
{
    require(value != nullptr, "expected integer value");
    require(value->isIntegerValue(), "expected integer value kind");
    const auto* integerValue = dynamic_cast<const IntegerValue*>(value);
    require(integerValue != nullptr, "expected integer value cast");
    require(integerValue->getVal() == expectedValue,
        "integer literal should preserve its payload");
    return integerValue;
}

inline const BinaryValue* requireBinary(const Value* value,
    koopa_raw_binary_op expectedOp, const std::string& expectedName)
{
    require(value != nullptr, "expected binary value");
    require(value->isBinaryValue(), "expected binary instruction");
    const auto* binaryValue = dynamic_cast<const BinaryValue*>(value);
    require(binaryValue != nullptr, "expected binary instruction cast");
    require(binaryValue->getOp() == expectedOp,
        "binary instruction should use the expected opcode");
    require(matchesExpectedTempName(binaryValue->getName(), expectedName),
        "binary instruction should use the expected temporary name");
    return binaryValue;
}

inline const AllocValue* requireAlloc(
    const Value* value, const std::string& expectedName)
{
    require(value != nullptr, "expected alloc value");
    require(value->isAllocValue(), "expected alloc instruction");
    const auto* allocValue = dynamic_cast<const AllocValue*>(value);
    require(allocValue != nullptr, "expected alloc instruction cast");
    require(matchesExpectedTempName(allocValue->getName(), expectedName)
            || allocValue->getName() == expectedName,
        "alloc instruction should preserve the expected storage name");
    return allocValue;
}

inline const LoadValue* requireLoad(const Value* value,
    const Value* expectedSource, const std::string& expectedName)
{
    require(value != nullptr, "expected load value");
    require(value->isLoadValue(), "expected load instruction");
    const auto* loadValue = dynamic_cast<const LoadValue*>(value);
    require(loadValue != nullptr, "expected load instruction cast");
    require(loadValue->getSource() == expectedSource,
        "load instruction should read from the expected storage");
    require(matchesExpectedTempName(loadValue->getName(), expectedName),
        "load instruction should use the expected temporary name");
    return loadValue;
}

inline const StoreValue* requireStore(
    const Value* value, const Value* expectedDestination)
{
    require(value != nullptr, "expected store value");
    require(value->isStoreValue(), "expected store instruction");
    const auto* storeValue = dynamic_cast<const StoreValue*>(value);
    require(storeValue != nullptr, "expected store instruction cast");
    require(storeValue->getDestination() == expectedDestination,
        "store instruction should target the expected storage");
    return storeValue;
}

inline const ReturnValue* requireReturn(const Value* value)
{
    require(value != nullptr, "expected return value");
    require(value->isReturnValue(), "expected return instruction");
    const auto* returnValue = dynamic_cast<const ReturnValue*>(value);
    require(returnValue != nullptr, "expected return instruction cast");
    return returnValue;
}

inline const CallValue* requireCall(
    const Value* value, const Function* expectedCallee)
{
    require(value != nullptr, "expected call value");
    require(value->isCallValue(), "expected call instruction");
    const auto* callValue = dynamic_cast<const CallValue*>(value);
    require(callValue != nullptr, "expected call instruction cast");
    require(callValue->getCallee() == expectedCallee,
        "call instruction should target the expected function");
    return callValue;
}

inline const JumpValue* requireJumpValue(const Value* value)
{
    require(value != nullptr, "expected jump value");
    require(value->isJumpValue(), "expected jump instruction");
    const auto* jumpValue = dynamic_cast<const JumpValue*>(value);
    require(jumpValue != nullptr, "expected jump instruction cast");
    return jumpValue;
}

inline const JumpValue* requireJump(
    const Value* value, const BasicBlock* expectedTarget)
{
    const auto* jumpValue = requireJumpValue(value);
    require(jumpValue->getTargetBB() == expectedTarget,
        "jump instruction should target the expected basic body");
    return jumpValue;
}

inline const BranchValue* requireBranchValue(const Value* value)
{
    require(value != nullptr, "expected branch value");
    require(value->isBranchValue(), "expected branch instruction");
    const auto* branchValue = dynamic_cast<const BranchValue*>(value);
    require(branchValue != nullptr, "expected branch instruction cast");
    return branchValue;
}

inline const BranchValue* requireBranch(const Value* value,
    const BasicBlock* expectedTrueTarget, const BasicBlock* expectedFalseTarget)
{
    const auto* branchValue = requireBranchValue(value);
    require(branchValue->getTrueBB() == expectedTrueTarget,
        "branch instruction should target the expected true basic body");
    require(branchValue->getFalseBB() == expectedFalseTarget,
        "branch instruction should target the expected false basic body");
    return branchValue;
}

inline const GetElemPtrValue* requireGetElemPtr(
    const Value* value, const Value* expectedSource)
{
    require(value != nullptr, "expected getelemptr value");
    require(value->isGetElemPtrValue(), "expected getelemptr instruction");
    const auto* getElemPtrValue = dynamic_cast<const GetElemPtrValue*>(value);
    require(getElemPtrValue != nullptr, "expected getelemptr instruction cast");
    require(getElemPtrValue->getSource() == expectedSource,
        "getelemptr should use the expected source storage");
    return getElemPtrValue;
}

inline const GetPtrValue* requireGetPtr(
    const Value* value, const Value* expectedSource)
{
    require(value != nullptr, "expected getptr value");
    require(value->isGetPtrValue(), "expected getptr instruction");
    const auto* getPtrValue = dynamic_cast<const GetPtrValue*>(value);
    require(getPtrValue != nullptr, "expected getptr instruction cast");
    require(getPtrValue->getSource() == expectedSource,
        "getptr should use the expected source storage");
    return getPtrValue;
}

} // namespace yesod::test_support::koopa

#endif
