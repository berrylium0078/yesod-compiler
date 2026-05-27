#include "koopa/ast_to_koopa.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace yesod::koopa {

using namespace frontend;

namespace {

    struct FunctionGenerator {
        struct LoopBlocks {
            BasicBlock* m_condBlock_nn;
            BasicBlock* m_endBlock_nn;
        };
        const frontend::AST& m_ast;
        const frontend::SemanticInfo* m_semanticInfo;
        Function* m_function;
        BasicBlock* m_currentBasicBlock;
        BasicBlock* m_endBlock;
        int32_t m_nextTempId = 1;
        int32_t m_nextBlockId = 1;
        std::unordered_map<int32_t, Value*> m_storageBySymbolId;
        const frontend::SemanticFunctionSSA* m_ssa = nullptr;
        std::unordered_map<int64_t, Value*> m_valueByAliasKey;
        std::unordered_map<int32_t, Function*> m_functionBySymbolId;
        std::unordered_map<Ref<frontend::SemanticBasicBlock>, BasicBlock*>
            m_basicBlockBySemanticBlock;
        std::unordered_map<Ptr<frontend::WhileStmt>, LoopBlocks>
            m_loopBlocksByWhileStmt;
        std::unordered_set<std::string> m_usedSymbolNames;

        template <typename T>
        [[nodiscard]] const T& node(Ptr<T> handle, const char* message) const
        {
            if (!handle) {
                throw std::runtime_error(message);
            }
            return handle(m_ast);
        }
        template <typename T>
        [[nodiscard]] const T& node(Ptr<T> handle,
            const FunctionGenerator& state, const char* message) const
        {
            if (!handle) {
                throw std::runtime_error(message);
            }
            return handle(state.m_ast);
        }
        [[nodiscard]] const frontend::SemanticSymbol&
        requireSymbolForIdentifier(
            Ref<frontend::Identifier> identifier, const char* message) const
        {
            const auto* symbol = m_semanticInfo->findSymbol(identifier);
            if (symbol == nullptr) {
                throw std::runtime_error(message);
            }
            return *symbol;
        }
        [[nodiscard]] const frontend::SemanticSymbol&
        requireSymbolForIdentifier(Ref<frontend::Identifier> identifier,
            const frontend::SemanticInfo& semanticInfo,
            const char* message) const
        {
            const auto* symbol = semanticInfo.findSymbol(identifier);
            if (symbol == nullptr) {
                throw std::runtime_error(message);
            }
            return *symbol;
        }
        [[nodiscard]] BasicBlock* createBasicBlock(const std::string& stem);
        [[nodiscard]] bool blockHasTerminator(
            const BasicBlock& basicBlock) const;
        void finalizeBasicBlock(
            BasicBlock& basicBlock, BasicBlock& endBlock) const;
        [[nodiscard]] std::string makeUniqueLocalName(
            const frontend::SemanticSymbol& symbol);
        [[nodiscard]] static int64_t aliasKey(
            const frontend::SemanticSsaAlias& alias);
        void bindAlias(Ref<frontend::Identifier> identifier, Value* value);
        [[nodiscard]] Value* requireAliasValue(
            Ref<frontend::Identifier> identifier, const char* message) const;
        [[nodiscard]] std::vector<Value*> edgeArgs(
            Ref<frontend::SemanticBasicBlock> source,
            Ref<frontend::SemanticBasicBlock> target) const;

        void generateBlock(Ptr<frontend::Block> body);
        void generateBlockItem(const frontend::BlockItem& blockItem);
        void generateDecl(frontend::Decl declNode);
        void generateSemanticBlock(Ref<frontend::SemanticBasicBlock> semanticBlock);
        void generateSemanticBlockItem(
            const frontend::SemanticBlockItem& semanticBlockItem);
        void generateSemanticTerminator(
            Ref<frontend::SemanticBasicBlock> source,
            const frontend::SemanticBlockTerminator& terminator);
        void generateStmt(frontend::Stmt stmtNode);
        void generateIfStmt(Ptr<frontend::IfStmt> ifStmt);
        void generateWhileStmt(Ptr<frontend::WhileStmt> whileStmt);
        void generateBreakStmt(Ptr<frontend::BreakStmt> breakStmt);
        void generateContinueStmt(Ptr<frontend::ContinueStmt> continueStmt);
        void generateAssignStmt(Ptr<frontend::AssignStmt> assignStmt);
        void generateExpStmt(Ptr<frontend::ExpStmt> expStmt);
        [[nodiscard]] ReturnValue* generateReturnStmt(
            Ptr<frontend::ReturnStmt> returnStmt);
        [[nodiscard]] Value* generateExp(Ref<frontend::Exp> exp);
        [[nodiscard]] Value* generateBinaryExpValue(
            const frontend::Exp::Binary& binaryExp);
        [[nodiscard]] Value* generateUnaryExpValue(
            const frontend::Exp::Unary& unaryExp);
        [[nodiscard]] Value* generateBooleanAsInt(Ref<frontend::Exp> exp);
        void generateBooleanBranch(Ref<frontend::Exp> exp,
            BasicBlock& trueBlock, BasicBlock& falseBlock);
        void generateLogicalOrBranch(const frontend::Exp::Binary& binaryExp,
            BasicBlock& trueBlock, BasicBlock& falseBlock);
        void generateLogicalAndBranch(const frontend::Exp::Binary& binaryExp,
            BasicBlock& trueBlock, BasicBlock& falseBlock);
        void generateLocalArrayInitializer(Value* address,
            const frontend::SemanticType& type,
            const std::vector<Ptr<frontend::Exp>>& scalarExprs,
            size_t& nextScalarIndex);
        [[nodiscard]] Value* generateLValueAddress(
            const frontend::Exp::LVal& lVal);
        [[nodiscard]] Value* generateSemanticValue(
            const frontend::SemanticValue& value);
    };

    size_t countScalarSlots(const SemanticType& type)
    {
        if (!type.isArray()) {
            return 1;
        }

        if (type.m_elementType == nullptr) {
            throw std::runtime_error("array type is missing element type");
        }

        return static_cast<size_t>(type.m_arrayLength)
            * countScalarSlots(*type.m_elementType);
    }

    template <typename InitNode>
    void fillObjectFromNode(const AST& ast, Ref<InitNode> init_nn,
        const SemanticType& type, size_t baseOffset,
        std::vector<Ptr<Exp>>& scalarExprs);

    template <typename InitNode>
    void assignScalarInitializerFromNode(const AST& ast, Ref<InitNode> init_nn,
        size_t baseOffset, std::vector<Ptr<Exp>>& scalarExprs);

    template <typename InitNode>
    void fillObjectFromSequence(const AST& ast,
        const std::vector<Ref<InitNode>>& values, size_t& nextValueIndex,
        const SemanticType& type, size_t baseOffset,
        std::vector<Ptr<Exp>>& scalarExprs)
    {
        if (nextValueIndex >= values.size()) {
            return;
        }

        if (!type.isArray()) {
            assignScalarInitializerFromNode(
                ast, values[nextValueIndex], baseOffset, scalarExprs);
            ++nextValueIndex;
            return;
        }

        if (type.m_elementType == nullptr) {
            throw std::runtime_error("array type is missing element type");
        }

        const size_t elementSlots = countScalarSlots(*type.m_elementType);
        for (int32_t i = 0;
            i < type.m_arrayLength && nextValueIndex < values.size(); ++i) {
            const auto& child = values[nextValueIndex](ast);
            MATCH(child.kind)
            WITH(
                [&](Ref<Exp>) {
                    fillObjectFromSequence(ast, values, nextValueIndex,
                        *type.m_elementType,
                        baseOffset + static_cast<size_t>(i) * elementSlots,
                        scalarExprs);
                },
                [&](const typename InitNode::List&) {
                    fillObjectFromNode(ast, values[nextValueIndex],
                        *type.m_elementType,
                        baseOffset + static_cast<size_t>(i) * elementSlots,
                        scalarExprs);
                    ++nextValueIndex;
                });
        }
    }

    template <typename InitNode>
    void fillObjectFromNode(const AST& ast, Ref<InitNode> init_nn,
        const SemanticType& type, size_t baseOffset,
        std::vector<Ptr<Exp>>& scalarExprs)
    {
        const auto& init = init_nn(ast);
        MATCH(init.kind)
        WITH(
            [&](Ref<Exp>) {
                size_t nextValueIndex = 0;
                const std::vector<Ref<InitNode>> singleton { init_nn };
                fillObjectFromSequence(ast, singleton, nextValueIndex, type,
                    baseOffset, scalarExprs);
            },
            [&](const typename InitNode::List& initAlt) {
                size_t nextValueIndex = 0;
                fillObjectFromSequence(ast, initAlt, nextValueIndex, type,
                    baseOffset, scalarExprs);
            }, );
    }

    template <typename InitNode>
    void assignScalarInitializerFromNode(const AST& ast, Ref<InitNode> init_nn,
        size_t baseOffset, std::vector<Ptr<Exp>>& scalarExprs)
    {
        const auto& init = init_nn(ast);
        MATCH(init.kind)
        WITH(
            [&](Ptr<Exp> initAlt) {
                if (baseOffset < scalarExprs.size()) {
                    scalarExprs[baseOffset] = initAlt;
                }
            },
            [&](const typename InitNode::List& initAlt) {
                if (!initAlt.empty()) {
                    assignScalarInitializerFromNode(
                        ast, initAlt.front(), baseOffset, scalarExprs);
                }
            }, );
    }

    template <typename InitNode>
    std::vector<Ptr<Exp>> flattenArrayInitializer(
        const AST& ast, Ref<InitNode> init_nn, const SemanticType& type)
    {
        std::vector<Ptr<Exp>> scalarExprs(countScalarSlots(type));
        fillObjectFromNode(ast, init_nn, type, 0, scalarExprs);
        return scalarExprs;
    }

    bool isZeroInitializerValue(const Value* value)
    {
        if (value->isZeroInitValue()) {
            return true;
        }
        if (value->isIntegerValue()) {
            return dynamic_cast<const IntegerValue*>(value)->getVal() == 0;
        }
        if (!value->isAggregateValue()) {
            return false;
        }

        const auto* aggregate = dynamic_cast<const AggregateValue*>(value);
        for (size_t i = 0; i < aggregate->getNumElements(); ++i) {
            if (!isZeroInitializerValue(aggregate->getElement(i))) {
                return false;
            }
        }
        return true;
    }

    bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

    bool isAllDigits(const std::string& text)
    {
        if (text.empty()) {
            return false;
        }
        for (const char ch : text) {
            if (!isDigit(ch)) {
                return false;
            }
        }
        return true;
    }

    std::string normalizeIdentifierStem(std::string stem)
    {
        if (!stem.empty() && isDigit(stem.front()) && !isAllDigits(stem)) {
            stem.insert(stem.begin(), '_');
        }
        return stem;
    }

    Type* lowerSemanticType(const SemanticType& semanticType,
        bool decayUnsizedArrayToPointer = true)
    {
        switch (semanticType.kind) {
        case SemanticTypeKind::integer:
        case SemanticTypeKind::boolean:
            return Int32Type::get();
        case SemanticTypeKind::voidType:
            return UnitType::get();
        case SemanticTypeKind::array:
            if (semanticType.m_elementType == nullptr) {
                throw std::runtime_error("array type is missing element type");
            }
            if (semanticType.m_arrayLength == -1
                && decayUnsizedArrayToPointer) {
                return PointerType::get(
                    lowerSemanticType(*semanticType.m_elementType, false));
            }
            return ArrayType::get(
                lowerSemanticType(*semanticType.m_elementType, false),
                static_cast<size_t>(semanticType.m_arrayLength));
        }

        throw std::runtime_error("unsupported semantic expression type");
    }

    std::string makeFunctionName(const std::string& identifier)
    {
        return "@" + identifier;
    }

    std::string makeGlobalName(const std::string& identifier)
    {
        return "@" + identifier;
    }

    std::string makeTempName(int32_t& nextTempId)
    {
        return "%t" + std::to_string(nextTempId++);
    }

    BinaryValue* generateBinaryValue(koopa_raw_binary_op op, Value* lhs,
        Value* rhs, BasicBlock& basicBlock, int32_t& nextTempId)
    {
        auto* binaryValue
            = BinaryValue::get(op, lhs, rhs, makeTempName(nextTempId));
        basicBlock.pushInst(binaryValue);
        return binaryValue;
    }

    Value* generateNumber(const Exp::Number& number)
    {
        return IntegerValue::get(number.value);
    }

    const SemanticSymbol& requireSymbolForIdentifier(Ref<Identifier> identifier,
        const SemanticInfo& semanticInfo, const char* message)
    {
        const auto* symbol = semanticInfo.findSymbol(identifier);
        if (symbol == nullptr) {
            throw std::runtime_error(message);
        }
        return *symbol;
    }

} // namespace

Program* Generator::generate(const AST& ast, Ptr<CompUnit> compUnit,
    const SemanticInfo& semanticInfo) const
{
    auto* program = Program::create();
    const auto& parsedCompUnit = compUnit(ast);
    std::unordered_map<int32_t, Value*> globalStorageBySymbolId;
    std::unordered_map<int32_t, Function*> functionBySymbolId;
    std::unordered_map<int32_t, size_t> symbolUseCount;
    std::unordered_set<int32_t> definedFunctionSymbolIds;

    for (const auto& [identifier_nn, symbolId] :
        semanticInfo.symbolIdByIdentifier()) {
        (void)identifier_nn;
        ++symbolUseCount[symbolId];
    }

    for (const auto topLevelItem : parsedCompUnit.topLevelItems) {
        MATCH(topLevelItem)
        WITH(
            [&](Ref<FuncDef> topLevelAlt) {
                const auto& funcDef = topLevelAlt(ast);
                if (funcDef.body == nullptr) {
                    return;
                }
                const auto* functionSymbol
                    = semanticInfo.findSymbol(funcDef.identifier);
                if (functionSymbol == nullptr) {
                    throw std::runtime_error("function declaration missing "
                                             "semantic symbol during lowering");
                }
                definedFunctionSymbolIds.insert(functionSymbol->m_id);
            },
            [&](const auto&) { }, );
    }

    for (const auto& [symbolId, symbol] : semanticInfo.symbolById()) {
        if (!symbol.isFunction()) {
            continue;
        }
        if (definedFunctionSymbolIds.find(symbolId)
            != definedFunctionSymbolIds.end()) {
            continue;
        }
        if (symbolUseCount[symbolId] <= 1) {
            continue;
        }

        auto [it, inserted] = functionBySymbolId.try_emplace(symbolId, nullptr);
        if (!inserted) {
            continue;
        }

        auto* function = createExternalFunctionDecl(symbol);
        it->second = function;
        program->pushFunc(function);
    }

    for (const auto topLevelItem : parsedCompUnit.topLevelItems) {
        MATCH(topLevelItem)
        WITH(
            [&](Decl topLevelAlt) {
                generateGlobalDecl(topLevelAlt, *program, ast, semanticInfo,
                    globalStorageBySymbolId);
            },
            [&](Ptr<FuncDef> topLevelAlt) {
                const auto& symbol = requireSymbolForIdentifier(
                    topLevelAlt(ast).identifier, semanticInfo,
                    "function declaration is missing a symbol binding");
                auto [functionIt, inserted]
                    = functionBySymbolId.try_emplace(symbol.m_id, nullptr);
                if (!inserted) {
                    return;
                }

                auto* function = topLevelAlt(ast).body != nullptr
                    ? createFunctionDecl(ast, topLevelAlt, semanticInfo)
                    : createExternalFunctionDecl(symbol);
                functionIt->second = function;
                program->pushFunc(function);
            }, );
    }

    for (const auto topLevelItem : parsedCompUnit.topLevelItems) {
        MATCH(topLevelItem)
        WITH(
            [&](Ref<FuncDef> topLevelAlt) {
                if (topLevelAlt(ast).body == nullptr) {
                    return;
                }
                const auto& symbol = requireSymbolForIdentifier(
                    topLevelAlt(ast).identifier, semanticInfo,
                    "function definition is missing a symbol binding");
                const auto functionIt = functionBySymbolId.find(symbol.m_id);
                if (functionIt == functionBySymbolId.end()) {
                    throw std::runtime_error("function definition is missing a "
                                             "lowered function declaration");
                }
                (void)generateFuncDef(ast, topLevelAlt.ptr(), semanticInfo,
                    globalStorageBySymbolId, functionBySymbolId,
                    functionIt->second);
            },
            [&](const auto&) { }, );
    }

    return program;
}

Function* Generator::createFunctionDecl(const AST& ast, Ptr<FuncDef> funcDef,
    const SemanticInfo& semanticInfo) const
{
    const auto& parsedFuncDef = funcDef(ast);
    const auto& identifier = parsedFuncDef.identifier(ast);
    const auto& symbol = requireSymbolForIdentifier(parsedFuncDef.identifier,
        semanticInfo, "function definition is missing a symbol binding");
    // TODO
    assert(symbol.isFunction());
    const auto& funcInfo = symbol.function();
    std::vector<Type*> paramTypes;
    paramTypes.reserve(funcInfo.m_paramTypes.size());
    for (const auto& paramType : funcInfo.m_paramTypes) {
        paramTypes.push_back(lowerSemanticType(paramType));
    }
    auto* function = Function::create(
        FunctionType::get(lowerSemanticType(funcInfo.m_returnType), paramTypes),
        makeFunctionName(identifier.name));
    for (size_t i = 0; i < parsedFuncDef.funcFParams.size(); ++i) {
        function->pushParam(FuncArgRefValue::get(i, paramTypes[i]));
    }
    return function;
}

Function* Generator::createExternalFunctionDecl(
    const SemanticSymbol& symbol) const
{
    std::vector<Type*> paramTypes;
    // TODO
    assert(symbol.isFunction());
    const auto& funcInfo = symbol.function();

    paramTypes.reserve(funcInfo.m_paramTypes.size());
    for (const auto& paramType : funcInfo.m_paramTypes) {
        paramTypes.push_back(lowerSemanticType(paramType));
    }
    auto* function = Function::create(
        FunctionType::get(lowerSemanticType(funcInfo.m_returnType), paramTypes),
        makeFunctionName(symbol.name));
    for (size_t i = 0; i != paramTypes.size(); ++i) {
        function->pushParam(FuncArgRefValue::get(i, paramTypes[i]));
    }
    return function;
}

Function* Generator::generateFuncDef(const AST& ast, Ptr<FuncDef> funcDef,
    const SemanticInfo& semanticInfo,
    const std::unordered_map<int32_t, Value*>& globalStorageBySymbolId,
    const std::unordered_map<int32_t, Function*>& functionBySymbolId,
    Function* function_nn) const
{
    const auto& parsedFuncDef = funcDef(ast);
    const auto* controlFlow = semanticInfo.findControlFlow(funcDef.ref());
    if (controlFlow == nullptr) {
        throw std::runtime_error(
            "function definition is missing semantic control flow");
    }

    auto* function = function_nn;
    const auto* ssa = semanticInfo.findSSA(funcDef.ref());
    if (ssa == nullptr) {
        throw std::runtime_error("function definition is missing semantic SSA");
    }
    FunctionGenerator state {
        .m_ast = ast,
        .m_semanticInfo = &semanticInfo,
        .m_function = function,
        .m_currentBasicBlock = nullptr,
        .m_endBlock = nullptr,
        .m_storageBySymbolId = globalStorageBySymbolId,
        .m_ssa = ssa,
        .m_functionBySymbolId = functionBySymbolId,
    };

    const auto& controlFlowArena = semanticInfo.controlFlowArena();
    for (const auto semanticBlockRef : controlFlow->blocks) {
        const auto& semanticBlock = semanticBlockRef(controlFlowArena);
        auto* basicBlock = semanticBlockRef == controlFlow->entryBlock
            ? BasicBlock::createEntry("%" + semanticBlock.nameHint)
            : BasicBlock::createNonEntry("%" + semanticBlock.nameHint);
        const auto ssaBlockIt = ssa->m_blockInfoByBlock.find(semanticBlockRef);
        if (ssaBlockIt == ssa->m_blockInfoByBlock.end()) {
            throw std::runtime_error("semantic basic block is missing SSA info");
        }
        for (size_t i = 0; i < ssaBlockIt->second.m_params.size(); ++i) {
            const auto& param = ssaBlockIt->second.m_params[i];
            const auto* symbol = semanticInfo.findSymbolById(param.m_symbolId);
            if (symbol == nullptr || !symbol->isObject()) {
                throw std::runtime_error("basic block parameter is missing object type");
            }
            basicBlock->pushParam(BlockArgRefValue::get(i,
                lowerSemanticType(symbol->object().m_type, false)));
        }
        state.m_basicBlockBySemanticBlock[semanticBlockRef] = basicBlock;
        function->pushBB(basicBlock);
    }

    state.m_currentBasicBlock
        = state.m_basicBlockBySemanticBlock.at(controlFlow->entryBlock);
    auto* entryBlock = state.m_currentBasicBlock;
    for (size_t i = 0; i < parsedFuncDef.funcFParams.size(); ++i) {
        const auto& funcFParam = parsedFuncDef.funcFParams[i];
        const auto& symbol = requireSymbolForIdentifier(funcFParam.identifier,
            semanticInfo, "function parameter is missing a symbol binding");
        if (semanticInfo.findAlias(funcFParam.identifier).has_value()) {
            state.bindAlias(funcFParam.identifier, function->getParam(i));
            continue;
        }
        auto* alloc = AllocValue::get(function->getParam(i)->getVType(),
            state.makeUniqueLocalName(symbol));
        entryBlock->pushInst(alloc);
        entryBlock->pushInst(StoreValue::get(function->getParam(i), alloc));
        state.m_storageBySymbolId[symbol.m_id] = alloc;
    }

    for (const auto semanticBlockRef : controlFlow->blocks) {
        state.m_currentBasicBlock
            = state.m_basicBlockBySemanticBlock.at(semanticBlockRef);
        state.generateSemanticBlock(semanticBlockRef);
    }

    for (auto* basicBlock : function->bbs()) {
        basicBlock->validate();
    }
    function->validate();
    return function;
}

void FunctionGenerator::generateSemanticBlock(
    Ref<frontend::SemanticBasicBlock> semanticBlockRef)
{
    const auto& semanticBlock = semanticBlockRef(m_semanticInfo->controlFlowArena());
    const auto ssaBlockIt = m_ssa->m_blockInfoByBlock.find(semanticBlockRef);
    if (ssaBlockIt == m_ssa->m_blockInfoByBlock.end()) {
        throw std::runtime_error("semantic basic block is missing SSA info");
    }
    if (ssaBlockIt->second.m_params.size() != m_currentBasicBlock->getNumParams()) {
        throw std::runtime_error(
            "basic block parameter count should match SSA parameters");
    }
    for (size_t i = 0; i < ssaBlockIt->second.m_params.size(); ++i) {
        auto* blockArg = m_currentBasicBlock->getParam(i);
        m_valueByAliasKey[aliasKey(ssaBlockIt->second.m_params[i].m_alias)] = blockArg;
    }
    for (const auto& item : semanticBlock.items) {
        generateSemanticBlockItem(item);
    }
    if (!semanticBlock.terminator.has_value()) {
        throw std::runtime_error("semantic basic block is missing terminator");
    }
    generateSemanticTerminator(semanticBlockRef, *semanticBlock.terminator);
}

void FunctionGenerator::generateSemanticBlockItem(
    const frontend::SemanticBlockItem& semanticBlockItem)
{
    MATCH(semanticBlockItem)
    WITH([&](frontend::Decl decl) { generateDecl(decl); },
        [&](Ref<AssignStmt> assignStmt) { generateAssignStmt(assignStmt.ptr()); },
        [&](Ref<ExpStmt> expStmt) { generateExpStmt(expStmt.ptr()); });
}

void FunctionGenerator::generateSemanticTerminator(
    Ref<frontend::SemanticBasicBlock> source,
    const frontend::SemanticBlockTerminator& terminator)
{
    auto& state = *this;
    MATCH(terminator)
    WITH(
        [&](const frontend::SemanticJumpTerminator& jump) {
            state.m_currentBasicBlock->pushInst(JumpValue::get(
                state.m_basicBlockBySemanticBlock.at(jump.target),
                state.edgeArgs(source, jump.target)));
        },
        [&](const frontend::SemanticBranchTerminator& branch) {
            const auto trueArgs = state.edgeArgs(source, branch.trueTarget);
            const auto falseArgs = state.edgeArgs(source, branch.falseTarget);
            if (const auto constantValue
                = state.m_semanticInfo->findConstantValue(branch.condition);
                constantValue.has_value()) {
                state.m_currentBasicBlock->pushInst(BranchValue::get(
                    IntegerValue::get(*constantValue),
                    state.m_basicBlockBySemanticBlock.at(branch.trueTarget),
                    trueArgs,
                    state.m_basicBlockBySemanticBlock.at(branch.falseTarget),
                    falseArgs));
                return;
            }

            auto* conditionValue = generateExp(branch.condition);
            state.m_currentBasicBlock->pushInst(BranchValue::get(
                conditionValue,
                state.m_basicBlockBySemanticBlock.at(branch.trueTarget),
                trueArgs,
                state.m_basicBlockBySemanticBlock.at(branch.falseTarget),
                falseArgs));
        },
        [&](const frontend::SemanticReturnTerminator& returnTerminator) {
            auto* value = returnTerminator.value.has_value()
                ? generateSemanticValue(*returnTerminator.value)
                : nullptr;
            state.m_currentBasicBlock->pushInst(ReturnValue::get(value));
        });
}

Value* FunctionGenerator::generateSemanticValue(
    const frontend::SemanticValue& value)
{
    return MATCH(value.kind) WITH(
        [&](int32_t constantValue) -> Value* {
            return IntegerValue::get(constantValue);
        },
        [&](Ref<Exp> exp) -> Value* { return generateExp(exp); });
}

void Generator::generateGlobalDecl(Decl decl, Program& program, const AST& ast,
    const SemanticInfo& semanticInfo,
    std::unordered_map<int32_t, Value*>& globalStorageBySymbolId) const
{
    MATCH(decl)
    WITH(
        [&](Ptr<ConstDecl> declAlt) {
            const auto& constDecl = declAlt(ast);
            for (const auto constDef_nn : constDecl.constDef) {
                const auto& constDef = constDef_nn(ast);
                const auto& symbol = requireSymbolForIdentifier(
                    constDef.identifier, semanticInfo,
                    "global const is missing its symbol binding");
                if (!symbol.isObject())
                    continue;
                const auto& type = symbol.object().m_type;
                if (!type.isArray())
                    continue;
                auto scalarExprs = flattenArrayInitializer(
                    ast, constDef.constInitVal.ref(), type);
                size_t nextScalarIndex = 0;
                Value* initValue = generateGlobalArrayInitializer(
                    type, scalarExprs, nextScalarIndex, semanticInfo);
                auto* globalAlloc = GlobalAllocValue::get(
                    initValue, makeGlobalName(symbol.name));
                program.pushVal(globalAlloc);
                globalStorageBySymbolId[symbol.m_id] = globalAlloc;
            }
        },
        [&](Ptr<VarDecl> declAlt) {
            const auto& varDecl = declAlt(ast);
            for (const auto varDef_nn : varDecl.varDef) {
                const auto& varDef = varDef_nn(ast);
                const auto& symbol = requireSymbolForIdentifier(
                    varDef.identifier, semanticInfo,
                    "global variable is missing its symbol binding");
                assert(symbol.isObject());
                const auto &type = symbol.object().m_type;
                Value* initValue = ZeroInitValue::get(
                    lowerSemanticType(type, false));
                if (type.isArray() && varDef.initVal) {
                    auto scalarExprs = flattenArrayInitializer(
                        ast, varDef.initVal.ref(), type);
                    size_t nextScalarIndex = 0;
                    initValue = generateGlobalArrayInitializer(type,
                        scalarExprs, nextScalarIndex, semanticInfo);
                } else if (varDef.initVal) {
                    const auto& initVal = varDef.initVal(ast);
                    MATCH(initVal.kind)
                    WITH(
                        [&](Ref<Exp> initAlt) {
                            const auto constantValue
                                = semanticInfo.findConstantValue(initAlt);
                            if (!constantValue.has_value()) {
                                throw std::runtime_error(
                                    "global variable initializer must be "
                                    "constant");
                            }
                            initValue = IntegerValue::get(*constantValue);
                        },
                        [&](const auto&) { }, );
                }
                auto* globalAlloc = GlobalAllocValue::get(
                    initValue, makeGlobalName(symbol.name));
                program.pushVal(globalAlloc);
                globalStorageBySymbolId[symbol.m_id] = globalAlloc;
            }
        }, );
}

void FunctionGenerator::generateBlock(Ptr<Block> body)
{
    auto& state = *this;
    const auto& parsedBlock = node(body, state, "body is missing");
    for (const auto blockItem : parsedBlock.items) {
        if (blockHasTerminator(*state.m_currentBasicBlock)) {
            break;
        }
        generateBlockItem(blockItem);
    }
}

void FunctionGenerator::generateBlockItem(const BlockItem& blockItem)
{
    auto& state = *this;
    if (blockHasTerminator(*state.m_currentBasicBlock)) {
        return;
    }
    MATCH(blockItem)
    WITH([&](Decl decl) { generateDecl(decl); },
        [&](Stmt stmt) { generateStmt(stmt); }, );
}

void FunctionGenerator::generateDecl(Decl decl)
{
    auto& state = *this;
    MATCH(decl)
    WITH(
        [&](Ptr<ConstDecl> declAlt) {
            const auto& constDecl
                = node(declAlt, state, "const declaration payload is null");
            for (const auto constDef : constDecl.constDef) {
                const auto& parsedConstDef = constDef(state.m_ast);
                const auto& symbol = requireSymbolForIdentifier(
                    parsedConstDef.identifier, *state.m_semanticInfo,
                    "const declarator is missing its symbol binding");
                assert(symbol.isObject());
                const auto &type = symbol.object().m_type;
                if (state.m_semanticInfo->findAlias(parsedConstDef.identifier)
                        .has_value()
                    && type.isScalar()) {
                    const auto& constInitVal = parsedConstDef.constInitVal(state.m_ast);
                    MATCH(constInitVal.kind)
                    WITH(
                        [&](Ref<Exp> initAlt) {
                            state.bindAlias(parsedConstDef.identifier,
                                generateExp(initAlt));
                        },
                        [&](const auto&) {
                            throw std::runtime_error(
                                "scalar const initializer should be a scalar expression");
                        });
                    continue;
                }
                auto* alloc = AllocValue::get(
                    ::yesod::koopa::lowerSemanticType(type, false),
                    makeUniqueLocalName(symbol));
                state.m_currentBasicBlock->pushInst(alloc);
                state.m_storageBySymbolId[symbol.m_id] = alloc;
                const auto& constInitVal
                    = parsedConstDef.constInitVal(state.m_ast);
                if (type.isArray()) {
                    auto scalarExprs = flattenArrayInitializer(state.m_ast,
                        parsedConstDef.constInitVal.ref(), type);
                    size_t nextScalarIndex = 0;
                    generateLocalArrayInitializer(
                        alloc, type, scalarExprs, nextScalarIndex);
                } else {
                    MATCH(constInitVal.kind)
                    WITH(
                        [&](Ref<Exp> initAlt) {
                            auto* initValue = generateExp(initAlt);
                            state.m_currentBasicBlock->pushInst(
                                StoreValue::get(initValue, alloc));
                        },
                        [&](const auto&) { });
                }
            }
        },
        [&](Ptr<VarDecl> declAlt) {
            const auto& varDecl
                = node(declAlt, state, "var declaration payload is null");
            for (const auto varDef : varDecl.varDef) {
                const auto& resolvedVarDef = varDef(state.m_ast);
                const auto& symbol = requireSymbolForIdentifier(
                    resolvedVarDef.identifier, *state.m_semanticInfo,
                    "var declarator is missing its symbol binding");
                const auto &type = symbol.object().m_type;
                if (state.m_semanticInfo->findAlias(resolvedVarDef.identifier)
                        .has_value()
                    && type.isScalar()) {
                    if (resolvedVarDef.initVal) {
                        const auto& initVal = resolvedVarDef.initVal(state.m_ast);
                        MATCH(initVal.kind)
                        WITH(
                            [&](Ref<Exp> initAlt) {
                                state.bindAlias(resolvedVarDef.identifier,
                                    generateExp(initAlt));
                            },
                            [&](const auto&) {
                                throw std::runtime_error(
                                    "scalar var initializer should be a scalar expression");
                            });
                    } else {
                        state.bindAlias(resolvedVarDef.identifier,
                            UndefValue::get(
                                ::yesod::koopa::lowerSemanticType(type, false)));
                    }
                    continue;
                }
                auto* alloc = AllocValue::get(
                    ::yesod::koopa::lowerSemanticType(type, false),
                    makeUniqueLocalName(symbol));
                state.m_currentBasicBlock->pushInst(alloc);
                state.m_storageBySymbolId[symbol.m_id] = alloc;
                if (resolvedVarDef.initVal) {
                    if (type.isArray()) {
                        auto scalarExprs = flattenArrayInitializer(state.m_ast,
                            resolvedVarDef.initVal.ref(), type);
                        size_t nextScalarIndex = 0;
                        generateLocalArrayInitializer(
                            alloc, type, scalarExprs, nextScalarIndex);
                    } else {
                        const auto& initVal
                            = resolvedVarDef.initVal(state.m_ast);
                        MATCH(initVal.kind)
                        WITH(
                            [&](Ref<Exp> initAlt) {
                                auto* initValue = generateExp(initAlt);
                                state.m_currentBasicBlock->pushInst(
                                    StoreValue::get(initValue, alloc));
                            },
                            [&](const auto&) { });
                    }
                }
            }
        }, );
}

void FunctionGenerator::generateStmt(Stmt stmt)
{
    MATCH(stmt)
    WITH([&](Ptr<IfStmt> stmtAlt) { generateIfStmt(stmtAlt); },
        [&](Ptr<WhileStmt> stmtAlt) { generateWhileStmt(stmtAlt); },
        [&](Ptr<BreakStmt> stmtAlt) { generateBreakStmt(stmtAlt); },
        [&](Ptr<ContinueStmt> stmtAlt) { generateContinueStmt(stmtAlt); },
        [&](Ptr<AssignStmt> stmtAlt) { generateAssignStmt(stmtAlt); },
        [&](Ptr<Block> stmtAlt) { generateBlock(stmtAlt); },
        [&](Ptr<ReturnStmt> stmtAlt) { (void)generateReturnStmt(stmtAlt); },
        [&](Ptr<ExpStmt> stmtAlt) { generateExpStmt(stmtAlt); });
}

void FunctionGenerator::generateIfStmt(Ptr<IfStmt> ifStmt)
{
    auto& state = *this;
    const auto& parsedIfStmt = node(ifStmt, state, "if statement is null");
    auto* thenBlock = createBasicBlock("if_then");
    BasicBlock* elseBlock = nullptr;
    BasicBlock* contBlock = nullptr;

    bool hasElse = MATCH(parsedIfStmt.elseBody) WITH(
        [&](Ref<Block> block) { return !block(state.m_ast).items.empty(); },
        [&](const auto&) { return true; }, );
    if (hasElse) {
        elseBlock = createBasicBlock("if_else");
        contBlock = createBasicBlock("if_end");
    } else {
        contBlock = createBasicBlock("if_end");
        elseBlock = contBlock;
    }

    generateBooleanBranch(parsedIfStmt.condition, *thenBlock, *elseBlock);

    state.m_currentBasicBlock = thenBlock;
    generateStmt(parsedIfStmt.thenBody);
    if (!blockHasTerminator(*state.m_currentBasicBlock)) {
        state.m_currentBasicBlock->pushInst(JumpValue::get(contBlock, { }));
    }

    if (hasElse) {
        state.m_currentBasicBlock = elseBlock;
        generateStmt(parsedIfStmt.elseBody);
        if (!blockHasTerminator(*state.m_currentBasicBlock)) {
            state.m_currentBasicBlock->pushInst(JumpValue::get(contBlock, { }));
        }
    }

    state.m_currentBasicBlock = contBlock;
}

void FunctionGenerator::generateWhileStmt(Ptr<WhileStmt> whileStmt)
{
    auto& state = *this;
    const auto& parsedWhileStmt
        = node(whileStmt, state, "while statement is null");
    auto* condBlock = createBasicBlock("while_cond");
    auto* bodyBlock = createBasicBlock("while_body");
    auto* endBlock = createBasicBlock("while_end");

    state.m_currentBasicBlock->pushInst(JumpValue::get(condBlock, { }));

    state.m_loopBlocksByWhileStmt[whileStmt] = FunctionGenerator::LoopBlocks {
        .m_condBlock_nn = condBlock,
        .m_endBlock_nn = endBlock,
    };

    state.m_currentBasicBlock = condBlock;
    generateBooleanBranch(parsedWhileStmt.condition, *bodyBlock, *endBlock);

    state.m_currentBasicBlock = bodyBlock;
    generateStmt(parsedWhileStmt.body);
    if (!blockHasTerminator(*state.m_currentBasicBlock)) {
        state.m_currentBasicBlock->pushInst(JumpValue::get(condBlock, { }));
    }

    state.m_loopBlocksByWhileStmt.erase(whileStmt);
    state.m_currentBasicBlock = endBlock;
}

void FunctionGenerator::generateBreakStmt(Ptr<BreakStmt> breakStmt)
{
    auto& state = *this;
    const auto loop = state.m_semanticInfo->findLoop(breakStmt.ref());
    if (!loop.has_value()) {
        throw std::runtime_error("break statement references no loop binding");
    }
    const auto loopIt = state.m_loopBlocksByWhileStmt.find(*loop);
    if (loopIt == state.m_loopBlocksByWhileStmt.end()) {
        throw std::runtime_error(
            "break statement references unknown loop target");
    }
    state.m_currentBasicBlock->pushInst(
        JumpValue::get(loopIt->second.m_endBlock_nn, { }));
}

void FunctionGenerator::generateContinueStmt(Ptr<ContinueStmt> continueStmt)
{
    auto& state = *this;
    const auto loop = state.m_semanticInfo->findLoop(continueStmt.ref());
    if (!loop.has_value()) {
        throw std::runtime_error(
            "continue statement references no loop binding");
    }
    const auto loopIt = state.m_loopBlocksByWhileStmt.find(*loop);
    if (loopIt == state.m_loopBlocksByWhileStmt.end()) {
        throw std::runtime_error(
            "continue statement references unknown loop target");
    }
    state.m_currentBasicBlock->pushInst(
        JumpValue::get(loopIt->second.m_condBlock_nn, { }));
}

void FunctionGenerator::generateAssignStmt(Ptr<AssignStmt> assignStmt)
{
    auto& state = *this;
    const auto& parsedAssignStmt
        = node(assignStmt, state, "assignment is null");
    const auto& lValExp = parsedAssignStmt.lval(state.m_ast);
    MATCH(lValExp.kind)
    WITH(
        [&](Exp::LVal expAlt) {
            if (state.m_semanticInfo->findAlias(expAlt.identifier).has_value()
                && expAlt.indices.empty()) {
                state.bindAlias(expAlt.identifier, generateExp(parsedAssignStmt.exp));
                return;
            }
            auto* address = generateLValueAddress(expAlt);
            auto* value = generateExp(parsedAssignStmt.exp);
            state.m_currentBasicBlock->pushInst(
                StoreValue::get(value, address));
        },
        [&](const auto&) {
            throw std::runtime_error(
                "assignment lhs is not an lvalue expression");
        }, );
}

void FunctionGenerator::generateExpStmt(Ptr<ExpStmt> expStmt)
{
    auto& state = *this;
    const auto& parsedExpStmt
        = node(expStmt, state, "expression statement is null");
    if (parsedExpStmt.exp) {
        (void)generateExp(parsedExpStmt.exp.ref());
    }
}

ReturnValue* FunctionGenerator::generateReturnStmt(Ptr<ReturnStmt> returnStmt)
{
    auto& state = *this;
    const auto& parsedReturnStmt
        = node(returnStmt, state, "return statement is null");
    auto* value = parsedReturnStmt.exp
        ? generateExp(parsedReturnStmt.exp.ref())
        : nullptr;
    auto* returnValue = ReturnValue::get(value);
    state.m_currentBasicBlock->pushInst(returnValue);
    return returnValue;
}

Value* FunctionGenerator::generateExp(Ref<Exp> exp)
{
    auto& state = *this;
    if (const auto constantValue = state.m_semanticInfo->findConstantValue(exp);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    const auto& parsedExp = exp(state.m_ast);
    return MATCH(parsedExp.kind) WITH(
        [&](Exp::Binary expAlt) -> Value* {
            if (expAlt.op == BinaryOpKeyword::orOr
                || expAlt.op == BinaryOpKeyword::andAnd) {
                return generateBooleanAsInt(exp);
            }
            return generateBinaryExpValue(expAlt);
        },
        [&](Exp::Unary expAlt) -> Value* {
            return generateUnaryExpValue(expAlt);
        },
        [&](Exp::Call expAlt) -> Value* {
            const auto& symbol = requireSymbolForIdentifier(expAlt.funcName,
                *state.m_semanticInfo,
                "call target is missing a symbol binding");
            const auto functionIt
                = state.m_functionBySymbolId.find(symbol.m_id);
            if (functionIt == state.m_functionBySymbolId.end()) {
                throw std::runtime_error(
                    "call target is missing a lowered function binding");
            }
            std::vector<Value*> args;
            args.reserve(expAlt.params.size());
            for (const auto arg_nn : expAlt.params) {
                args.push_back(generateExp(arg_nn));
            }
            const auto* functionType = dynamic_cast<FunctionType*>(
                functionIt->second->getFuncType());
            const std::string callName
                = functionType->getResultType()->isUnitType()
                ? std::string { }
                : makeTempName(state.m_nextTempId);
            auto* callValue
                = CallValue::get(functionIt->second, std::move(args), callName);
            state.m_currentBasicBlock->pushInst(callValue);
            return callValue;
        },
        [&](Exp::LVal expAlt) -> Value* {
            if (state.m_semanticInfo->findAlias(expAlt.identifier).has_value()
                && expAlt.indices.empty()) {
                return state.requireAliasValue(expAlt.identifier,
                    "scalar lvalue is missing an SSA alias value");
            }
            auto* address = generateLValueAddress(expAlt);
            const auto expType = state.m_semanticInfo->findExpType(exp);
            if (expType.has_value() && expType->isArray()) {
                const auto pointeeType
                    = dynamic_cast<PointerType*>(address->getVType())
                          ->getPointeeType();
                if (pointeeType->isArrayType()) {
                    auto* decayed = GetElemPtrValue::get(address,
                        IntegerValue::get(0), makeTempName(state.m_nextTempId));
                    state.m_currentBasicBlock->pushInst(decayed);
                    return decayed;
                }
                return address;
            }
            auto* loadValue
                = LoadValue::get(address, makeTempName(state.m_nextTempId));
            state.m_currentBasicBlock->pushInst(loadValue);
            return loadValue;
        },
        [&](Exp::Number expAlt) -> Value* { return generateNumber(expAlt); });
}

Value* FunctionGenerator::generateBinaryExpValue(const Exp::Binary& binaryExp)
{
    auto& state = *this;
    auto* lhs = generateExp(binaryExp.lhs);
    auto* rhs = generateExp(binaryExp.rhs);
    koopa_raw_binary_op op = KOOPA_RBO_ADD;
    switch (binaryExp.op) {
    case BinaryOpKeyword::star:
        op = KOOPA_RBO_MUL;
        break;
    case BinaryOpKeyword::slash:
        op = KOOPA_RBO_DIV;
        break;
    case BinaryOpKeyword::percent:
        op = KOOPA_RBO_MOD;
        break;
    case BinaryOpKeyword::plus:
        op = KOOPA_RBO_ADD;
        break;
    case BinaryOpKeyword::minus:
        op = KOOPA_RBO_SUB;
        break;
    case BinaryOpKeyword::less:
        op = KOOPA_RBO_LT;
        break;
    case BinaryOpKeyword::greater:
        op = KOOPA_RBO_GT;
        break;
    case BinaryOpKeyword::lessEqual:
        op = KOOPA_RBO_LE;
        break;
    case BinaryOpKeyword::greaterEqual:
        op = KOOPA_RBO_GE;
        break;
    case BinaryOpKeyword::equal:
        op = KOOPA_RBO_EQ;
        break;
    case BinaryOpKeyword::notEqual:
        op = KOOPA_RBO_NOT_EQ;
        break;
    case BinaryOpKeyword::andAnd:
    case BinaryOpKeyword::orOr:
        throw std::runtime_error("short-circuit binary expression should lower "
                                 "through boolean branching");
    }
    return generateBinaryValue(
        op, lhs, rhs, *state.m_currentBasicBlock, state.m_nextTempId);
}

Value* FunctionGenerator::generateUnaryExpValue(const Exp::Unary& unaryExp)
{
    auto& state = *this;
    auto* operand = generateExp(unaryExp.lhs);
    switch (unaryExp.op) {
    case UnaryOpKeyword::plus:
        return generateBinaryValue(KOOPA_RBO_ADD, IntegerValue::get(0), operand,
            *state.m_currentBasicBlock, state.m_nextTempId);
    case UnaryOpKeyword::minus:
        return generateBinaryValue(KOOPA_RBO_SUB, IntegerValue::get(0), operand,
            *state.m_currentBasicBlock, state.m_nextTempId);
    case UnaryOpKeyword::bang:
        return generateBinaryValue(KOOPA_RBO_EQ, IntegerValue::get(0), operand,
            *state.m_currentBasicBlock, state.m_nextTempId);
    }
    throw std::runtime_error("unsupported unary operator");
}

Value* FunctionGenerator::generateBooleanAsInt(Ref<Exp> exp)
{
    auto& state = *this;
    auto* resultStorage
        = AllocValue::get(Int32Type::get(), makeTempName(state.m_nextTempId));
    state.m_currentBasicBlock->pushInst(resultStorage);
    state.m_currentBasicBlock->pushInst(
        StoreValue::get(IntegerValue::get(0), resultStorage));

    auto* trueBlock = createBasicBlock("bool_true");
    auto* falseBlock = createBasicBlock("bool_false");
    auto* contBlock = createBasicBlock("bool_end");

    generateBooleanBranch(exp, *trueBlock, *falseBlock);

    trueBlock->pushInst(StoreValue::get(IntegerValue::get(1), resultStorage));
    trueBlock->pushInst(JumpValue::get(contBlock, { }));
    falseBlock->pushInst(StoreValue::get(IntegerValue::get(0), resultStorage));
    falseBlock->pushInst(JumpValue::get(contBlock, { }));

    state.m_currentBasicBlock = contBlock;
    auto* loadValue
        = LoadValue::get(resultStorage, makeTempName(state.m_nextTempId));
    contBlock->pushInst(loadValue);
    return loadValue;
}

void FunctionGenerator::generateBooleanBranch(
    Ref<Exp> exp, BasicBlock& trueBlock, BasicBlock& falseBlock)
{
    auto& state = *this;
    if (const auto constantValue = state.m_semanticInfo->findConstantValue(exp);
        constantValue.has_value()) {
        state.m_currentBasicBlock->pushInst(
            BranchValue::get(IntegerValue::get(*constantValue), &trueBlock, { },
                &falseBlock, { }));
        return;
    }

    const auto& parsedExp = exp(state.m_ast);
    MATCH(parsedExp.kind)
    WITH(
        [&](Exp::Binary expAlt) {
            if (expAlt.op == BinaryOpKeyword::orOr) {
                generateLogicalOrBranch(expAlt, trueBlock, falseBlock);
                return;
            }
            if (expAlt.op == BinaryOpKeyword::andAnd) {
                generateLogicalAndBranch(expAlt, trueBlock, falseBlock);
                return;
            }

            auto* value = generateBinaryExpValue(expAlt);
            state.m_currentBasicBlock->pushInst(
                BranchValue::get(value, &trueBlock, { }, &falseBlock, { }));
        },
        [&](const auto&) {
            auto* value = generateExp(exp);
            state.m_currentBasicBlock->pushInst(
                BranchValue::get(value, &trueBlock, { }, &falseBlock, { }));
        });
}

void FunctionGenerator::generateLogicalOrBranch(
    const Exp::Binary& binaryExp, BasicBlock& trueBlock, BasicBlock& falseBlock)
{
    auto& state = *this;
    auto* nextOperandBlock = createBasicBlock("lor_rhs");
    generateBooleanBranch(binaryExp.lhs, trueBlock, *nextOperandBlock);
    state.m_currentBasicBlock = nextOperandBlock;
    generateBooleanBranch(binaryExp.rhs, trueBlock, falseBlock);
}

void FunctionGenerator::generateLogicalAndBranch(
    const Exp::Binary& binaryExp, BasicBlock& trueBlock, BasicBlock& falseBlock)
{
    auto& state = *this;
    auto* nextOperandBlock = createBasicBlock("land_rhs");
    generateBooleanBranch(binaryExp.lhs, *nextOperandBlock, falseBlock);
    state.m_currentBasicBlock = nextOperandBlock;
    generateBooleanBranch(binaryExp.rhs, trueBlock, falseBlock);
}

Type* Generator::lowerSemanticType(
    const SemanticType& type, bool decayUnsizedArrayToPointer) const
{
    return ::yesod::koopa::lowerSemanticType(type, decayUnsizedArrayToPointer);
}

Value* Generator::generateGlobalArrayInitializer(const SemanticType& type,
    const std::vector<Ptr<Exp>>& scalarExprs, size_t& nextScalarIndex,
    const SemanticInfo& semanticInfo) const
{
    if (!type.isArray()) {
        if (nextScalarIndex >= scalarExprs.size()) {
            return IntegerValue::get(0);
        }

        const auto exp_nn = scalarExprs[nextScalarIndex++];
        if (!exp_nn) {
            return IntegerValue::get(0);
        }

        const auto constantValue = semanticInfo.findConstantValue(exp_nn.ref());
        if (!constantValue.has_value()) {
            throw std::runtime_error(
                "global array initializer element must be constant");
        }
        return IntegerValue::get(*constantValue);
    }

    if (type.m_elementType == nullptr) {
        throw std::runtime_error("array type is missing element type");
    }

    std::vector<Value*> elements;
    elements.reserve(static_cast<size_t>(type.m_arrayLength));
    for (int32_t i = 0; i < type.m_arrayLength; ++i) {
        elements.push_back(generateGlobalArrayInitializer(
            *type.m_elementType, scalarExprs, nextScalarIndex, semanticInfo));
    }

    auto* arrayType = lowerSemanticType(type, false);
    if (elements.empty()) {
        return ZeroInitValue::get(arrayType);
    }
    if (std::all_of(elements.begin(), elements.end(), [](const Value* element) {
            return isZeroInitializerValue(element);
        })) {
        return ZeroInitValue::get(arrayType);
    }
    return AggregateValue::get(std::move(elements), arrayType);
}

void FunctionGenerator::generateLocalArrayInitializer(Value* address,
    const SemanticType& type, const std::vector<Ptr<Exp>>& scalarExprs,
    size_t& nextScalarIndex)
{
    auto& state = *this;
    if (!type.isArray()) {
        Value* initValue = IntegerValue::get(0);
        if (nextScalarIndex < scalarExprs.size()) {
            const auto exp_nn = scalarExprs[nextScalarIndex++];
            if (exp_nn) {
                initValue = generateExp(exp_nn.ref());
            }
        }
        state.m_currentBasicBlock->pushInst(
            StoreValue::get(initValue, address));
        return;
    }

    if (type.m_elementType == nullptr) {
        throw std::runtime_error("array type is missing element type");
    }

    for (int32_t i = 0; i < type.m_arrayLength; ++i) {
        auto* elementAddress = GetElemPtrValue::get(
            address, IntegerValue::get(i), makeTempName(state.m_nextTempId));
        state.m_currentBasicBlock->pushInst(elementAddress);
        generateLocalArrayInitializer(
            elementAddress, *type.m_elementType, scalarExprs, nextScalarIndex);
    }
}

Value* FunctionGenerator::generateLValueAddress(const Exp::LVal& lVal)
{
    auto& state = *this;
    const auto& symbol = requireSymbolForIdentifier(lVal.identifier,
        *state.m_semanticInfo, "lvalue is missing a symbol binding");
    const auto storageIt = state.m_storageBySymbolId.find(symbol.m_id);
    if (storageIt == state.m_storageBySymbolId.end()) {
        throw std::runtime_error("lvalue references undefined storage");
    }

    auto* address = storageIt->second;
    auto currentType = symbol.object().m_type;
    bool indexesDecayedArrayParameter = false;
    if (currentType.isArray() && currentType.m_arrayLength == -1) {
        auto* loadedPointer
            = LoadValue::get(address, makeTempName(state.m_nextTempId));
        state.m_currentBasicBlock->pushInst(loadedPointer);
        address = loadedPointer;
        indexesDecayedArrayParameter = true;
    }

    for (const auto index_nn : lVal.indices) {
        auto* indexValue = generateExp(index_nn);
        Value* nextAddress = nullptr;
        if (indexesDecayedArrayParameter) {
            nextAddress = GetPtrValue::get(
                address, indexValue, makeTempName(state.m_nextTempId));
        } else {
            const auto pointeeType
                = dynamic_cast<PointerType*>(address->getVType())
                      ->getPointeeType();
            if (pointeeType->isArrayType()) {
                nextAddress = GetElemPtrValue::get(
                    address, indexValue, makeTempName(state.m_nextTempId));
            } else {
                nextAddress = GetPtrValue::get(
                    address, indexValue, makeTempName(state.m_nextTempId));
            }
        }
        state.m_currentBasicBlock->pushInst(nextAddress);
        address = nextAddress;
        if (currentType.isArray() && currentType.m_elementType != nullptr) {
            currentType = *currentType.m_elementType;
        }
        indexesDecayedArrayParameter = false;
    }

    return address;
}

BasicBlock* FunctionGenerator::createBasicBlock(const std::string& stem)
{
    auto& state = *this;
    auto* basicBlock = BasicBlock::createNonEntry(
        "%" + stem + "_" + std::to_string(state.m_nextBlockId++));
    state.m_function->pushBB(basicBlock);
    return basicBlock;
}

bool FunctionGenerator::blockHasTerminator(const BasicBlock& basicBlock) const
{
    return basicBlock.getNumInsts() > 0
        && basicBlock.getInst(basicBlock.getNumInsts() - 1)
               ->canTerminateBlock();
}

void FunctionGenerator::finalizeBasicBlock(
    BasicBlock& basicBlock, BasicBlock& endBlock) const
{
    if (blockHasTerminator(basicBlock)) {
        return;
    }

    basicBlock.pushInst(JumpValue::get(&endBlock, { }));
}

std::string FunctionGenerator::makeUniqueLocalName(const SemanticSymbol& symbol)
{
    auto& state = *this;
    const std::string baseName = "%" + normalizeIdentifierStem(symbol.name);
    if (state.m_usedSymbolNames.insert(baseName).second) {
        return baseName;
    }

    int32_t suffix = 1;
    while (true) {
        const std::string candidate = baseName + "_" + std::to_string(suffix++);
        if (state.m_usedSymbolNames.insert(candidate).second) {
            return candidate;
        }
    }
}

int64_t FunctionGenerator::aliasKey(const frontend::SemanticSsaAlias& alias)
{
    return (static_cast<int64_t>(alias.m_symbolId) << 32)
        ^ static_cast<uint32_t>(alias.m_version);
}

void FunctionGenerator::bindAlias(
    Ref<frontend::Identifier> identifier, Value* value)
{
    const auto alias = m_semanticInfo->findAlias(identifier);
    if (!alias.has_value()) {
        throw std::runtime_error("identifier is missing semantic SSA alias");
    }
    m_valueByAliasKey[aliasKey(*alias)] = value;
}

Value* FunctionGenerator::requireAliasValue(
    Ref<frontend::Identifier> identifier, const char* message) const
{
    const auto alias = m_semanticInfo->findAlias(identifier);
    if (!alias.has_value()) {
        throw std::runtime_error(message);
    }
    const auto valueIt = m_valueByAliasKey.find(aliasKey(*alias));
    if (valueIt == m_valueByAliasKey.end()) {
        throw std::runtime_error(message);
    }
    return valueIt->second;
}

std::vector<Value*> FunctionGenerator::edgeArgs(
    Ref<frontend::SemanticBasicBlock> source,
    Ref<frontend::SemanticBasicBlock> target) const
{
    const auto sourceIt = m_ssa->m_blockInfoByBlock.find(source);
    if (sourceIt == m_ssa->m_blockInfoByBlock.end()) {
        throw std::runtime_error("source block is missing SSA info");
    }
    const auto argsIt = sourceIt->second.m_outgoingArgsByTarget.find(target);
    if (argsIt == sourceIt->second.m_outgoingArgsByTarget.end()) {
        return { };
    }
    std::vector<Value*> values;
    values.reserve(argsIt->second.size());
    for (const auto& alias : argsIt->second) {
        const auto valueIt = m_valueByAliasKey.find(aliasKey(alias));
        if (valueIt == m_valueByAliasKey.end()) {
            throw std::runtime_error("SSA edge argument is missing a value binding");
        }
        values.push_back(valueIt->second);
    }
    return values;
}

} // namespace yesod::koopa
