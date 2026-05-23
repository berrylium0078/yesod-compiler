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

    for (const auto& [identifier_nn, symbol] :
        semanticInfo.m_symbolByIdentifier) {
        (void)identifier_nn;
        ++symbolUseCount[symbol.m_id];
    }

    for (const auto topLevelItem : parsedCompUnit.topLevelItems) {
        MATCH(topLevelItem)
        WITH(
            [&](Ptr<FuncDef> topLevelAlt) {
                const auto& funcDef = topLevelAlt(ast);
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

    for (const auto& [identifier_nn, symbol] :
        semanticInfo.m_symbolByIdentifier) {
        (void)identifier_nn;
        if (symbol.kind != SemanticSymbolKind::function) {
            continue;
        }
        if (definedFunctionSymbolIds.find(symbol.m_id)
            != definedFunctionSymbolIds.end()) {
            continue;
        }
        if (symbolUseCount[symbol.m_id] <= 1) {
            continue;
        }

        auto [it, inserted]
            = functionBySymbolId.try_emplace(symbol.m_id, nullptr);
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
                auto* function
                    = createFunctionDecl(ast, topLevelAlt, semanticInfo);
                const auto& symbol = requireSymbolForIdentifier(
                    topLevelAlt(ast).identifier, semanticInfo,
                    "function definition is missing a symbol binding");
                functionBySymbolId[symbol.m_id] = function;
                program->pushFunc(function);
            }, );
    }

    for (const auto topLevelItem : parsedCompUnit.topLevelItems) {
        MATCH(topLevelItem)
        WITH(
            [&](Ptr<FuncDef> topLevelAlt) {
                const auto& symbol = requireSymbolForIdentifier(
                    topLevelAlt(ast).identifier, semanticInfo,
                    "function definition is missing a symbol binding");
                const auto functionIt = functionBySymbolId.find(symbol.m_id);
                if (functionIt == functionBySymbolId.end()) {
                    throw std::runtime_error("function definition is missing a "
                                             "lowered function declaration");
                }
                (void)generateFuncDef(ast, topLevelAlt, semanticInfo,
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
    std::vector<Type*> paramTypes;
    paramTypes.reserve(symbol.m_functionSignature.m_paramTypes.size());
    for (const auto& paramType : symbol.m_functionSignature.m_paramTypes) {
        paramTypes.push_back(lowerSemanticType(paramType));
    }
    auto* function = Function::create(
        FunctionType::get(
            lowerSemanticType(symbol.m_functionSignature.m_returnType),
            paramTypes),
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
    paramTypes.reserve(symbol.m_functionSignature.m_paramTypes.size());
    for (const auto& paramType : symbol.m_functionSignature.m_paramTypes) {
        paramTypes.push_back(lowerSemanticType(paramType));
    }
    auto* function = Function::create(
        FunctionType::get(
            lowerSemanticType(symbol.m_functionSignature.m_returnType),
            paramTypes),
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
    auto* function = function_nn;
    auto* entryBlock = BasicBlock::createEntry("%entry");
    auto* endBlock = BasicBlock::createNonEntry("%end");
    function->pushBB(entryBlock);
    FunctionGenerationState state {
        .m_ast_nn = ast,
        .m_semanticInfo_nn = &semanticInfo,
        .m_function_nn = function,
        .m_currentBasicBlock_nn = entryBlock,
        .m_endBlock_nn = endBlock,
        .m_storageBySymbolId = globalStorageBySymbolId,
        .m_functionBySymbolId = functionBySymbolId,
    };
    for (size_t i = 0; i < parsedFuncDef.funcFParams.size(); ++i) {
        const auto& funcFParam = parsedFuncDef.funcFParams[i];
        const auto& symbol = requireSymbolForIdentifier(funcFParam.identifier,
            semanticInfo, "function parameter is missing a symbol binding");
        auto* alloc = AllocValue::get(function->getParam(i)->getVType(),
            makeUniqueLocalName(symbol, state.m_usedSymbolNames));
        entryBlock->pushInst(alloc);
        entryBlock->pushInst(StoreValue::get(function->getParam(i), alloc));
        state.m_storageBySymbolId[symbol.m_id] = alloc;
    }
    generateBlock(parsedFuncDef.body, state);
    finalizeBasicBlock(*state.m_currentBasicBlock_nn, *endBlock);
    if (parsedFuncDef.m_funcType == FuncTypeKeyword::voidKeyword) {
        endBlock->pushInst(ReturnValue::get(nullptr));
    } else {
        endBlock->pushInst(ReturnValue::get(IntegerValue::get(0)));
    }
    function->pushBB(endBlock);
    for (auto* basicBlock : function->bbs()) {
        basicBlock->validate();
    }
    function->validate();
    return function;
}

void Generator::generateGlobalDecl(Decl decl, Program& program,
    const AST& ast, const SemanticInfo& semanticInfo,
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
                if (!symbol.m_type.isArray()) {
                    continue;
                }
                auto scalarExprs = flattenArrayInitializer(
                    ast, constDef.constInitVal, symbol.m_type);
                size_t nextScalarIndex = 0;
                Value* initValue = generateGlobalArrayInitializer(
                    symbol.m_type, scalarExprs, nextScalarIndex, semanticInfo);
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
                Value* initValue = ZeroInitValue::get(
                    lowerSemanticType(symbol.m_type, false));
                if (symbol.m_type.isArray() && varDef.initVal) {
                    auto scalarExprs = flattenArrayInitializer(
                        ast, varDef.initVal.ref(), symbol.m_type);
                    size_t nextScalarIndex = 0;
                    initValue = generateGlobalArrayInitializer(symbol.m_type,
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

void Generator::generateBlock(
    Ptr<Block> body, FunctionGenerationState& state) const
{
    const auto& parsedBlock = node(body, state, "body is missing");
    for (const auto blockItem : parsedBlock.items) {
        if (blockHasTerminator(*state.m_currentBasicBlock_nn)) {
            break;
        }
        generateBlockItem(blockItem, state);
    }
}

void Generator::generateBlockItem(
    const BlockItem& blockItem, FunctionGenerationState& state) const
{
    if (blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        return;
    }
    MATCH(blockItem)
    WITH([&](Decl decl) { generateDecl(decl, state); },
        [&](Stmt stmt) { generateStmt(stmt, state); }, );
}

void Generator::generateDecl(
    Decl decl, FunctionGenerationState& state) const
{
    MATCH(decl)
    WITH(
        [&](Ptr<ConstDecl> declAlt) {
            const auto& constDecl
                = node(declAlt, state, "const declaration payload is null");
            for (const auto constDef : constDecl.constDef) {
                const auto& parsedConstDef = constDef(state.m_ast_nn);
                const auto& symbol = requireSymbolForIdentifier(
                    parsedConstDef.identifier, *state.m_semanticInfo_nn,
                    "const declarator is missing its symbol binding");
                auto* alloc
                    = AllocValue::get(lowerSemanticType(symbol.m_type, false),
                        makeUniqueLocalName(symbol, state.m_usedSymbolNames));
                state.m_currentBasicBlock_nn->pushInst(alloc);
                state.m_storageBySymbolId[symbol.m_id] = alloc;
                const auto& constInitVal
                    = parsedConstDef.constInitVal(state.m_ast_nn);
                if (symbol.m_type.isArray()) {
                    auto scalarExprs = flattenArrayInitializer(state.m_ast_nn,
                        parsedConstDef.constInitVal, symbol.m_type);
                    size_t nextScalarIndex = 0;
                    generateLocalArrayInitializer(alloc, symbol.m_type,
                        scalarExprs, nextScalarIndex, state);
                } else {
                    MATCH(constInitVal.kind)
                    WITH(
                        [&](Ref<Exp> initAlt) {
                            auto* initValue = generateExp(initAlt, state);
                            state.m_currentBasicBlock_nn->pushInst(
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
                const auto& resolvedVarDef = varDef(state.m_ast_nn);
                const auto& symbol = requireSymbolForIdentifier(
                    resolvedVarDef.identifier, *state.m_semanticInfo_nn,
                    "var declarator is missing its symbol binding");
                auto* alloc
                    = AllocValue::get(lowerSemanticType(symbol.m_type, false),
                        makeUniqueLocalName(symbol, state.m_usedSymbolNames));
                state.m_currentBasicBlock_nn->pushInst(alloc);
                state.m_storageBySymbolId[symbol.m_id] = alloc;
                if (resolvedVarDef.initVal) {
                    if (symbol.m_type.isArray()) {
                        auto scalarExprs
                            = flattenArrayInitializer(state.m_ast_nn,
                                resolvedVarDef.initVal.ref(), symbol.m_type);
                        size_t nextScalarIndex = 0;
                        generateLocalArrayInitializer(alloc, symbol.m_type,
                            scalarExprs, nextScalarIndex, state);
                    } else {
                        const auto& initVal = resolvedVarDef.initVal(state.m_ast_nn);
                        MATCH(initVal.kind)
                        WITH(
                            [&](Ref<Exp> initAlt) {
                                auto* initValue = generateExp(initAlt, state);
                                state.m_currentBasicBlock_nn->pushInst(
                                    StoreValue::get(initValue, alloc));
                            },
                            [&](const auto&) { });
                    }
                }
            }
        }, );
}

void Generator::generateStmt(
    Stmt stmt, FunctionGenerationState& state) const
{
    MATCH(stmt)
    WITH([&](Ptr<IfStmt> stmtAlt) { generateIfStmt(stmtAlt, state); },
        [&](Ptr<WhileStmt> stmtAlt) { generateWhileStmt(stmtAlt, state); },
        [&](Ptr<BreakStmt> stmtAlt) { generateBreakStmt(stmtAlt, state); },
        [&](Ptr<ContinueStmt> stmtAlt) {
            generateContinueStmt(stmtAlt, state);
        },
        [&](Ptr<AssignStmt> stmtAlt) { generateAssignStmt(stmtAlt, state); },
        [&](Ptr<Block> stmtAlt) { generateBlock(stmtAlt, state); },
        [&](Ptr<ReturnStmt> stmtAlt) {
            (void)generateReturnStmt(stmtAlt, state);
        },
        [&](Ptr<ExpStmt> stmtAlt) { generateExpStmt(stmtAlt, state); }, );
}

void Generator::generateIfStmt(
    Ptr<IfStmt> ifStmt, FunctionGenerationState& state) const
{
    const auto& parsedIfStmt = node(ifStmt, state, "if statement is null");
    auto* thenBlock = createBasicBlock("if_then", state);
    BasicBlock* elseBlock = nullptr;
    BasicBlock* contBlock = nullptr;
    if (parsedIfStmt.m_hasElse) {
        elseBlock = createBasicBlock("if_else", state);
        contBlock = createBasicBlock("if_end", state);
    } else {
        contBlock = createBasicBlock("if_end", state);
        elseBlock = contBlock;
    }

    generateBooleanBranch(
        parsedIfStmt.condition, *thenBlock, *elseBlock, state);

    state.m_currentBasicBlock_nn = thenBlock;
    generateStmt(parsedIfStmt.thenBody, state);
    if (!blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        state.m_currentBasicBlock_nn->pushInst(JumpValue::get(contBlock, { }));
    }

    if (parsedIfStmt.m_hasElse) {
        state.m_currentBasicBlock_nn = elseBlock;
        generateStmt(parsedIfStmt.elseBody, state);
        if (!blockHasTerminator(*state.m_currentBasicBlock_nn)) {
            state.m_currentBasicBlock_nn->pushInst(
                JumpValue::get(contBlock, { }));
        }
    }

    state.m_currentBasicBlock_nn = contBlock;
}

void Generator::generateWhileStmt(
    Ptr<WhileStmt> whileStmt, FunctionGenerationState& state) const
{
    const auto& parsedWhileStmt
        = node(whileStmt, state, "while statement is null");
    auto* condBlock = createBasicBlock("while_cond", state);
    auto* bodyBlock = createBasicBlock("while_body", state);
    auto* endBlock = createBasicBlock("while_end", state);

    state.m_currentBasicBlock_nn->pushInst(JumpValue::get(condBlock, { }));

    state.m_loopBlocksByWhileStmt[whileStmt]
        = FunctionGenerationState::LoopBlocks {
              .m_condBlock_nn = condBlock,
              .m_endBlock_nn = endBlock,
          };

    state.m_currentBasicBlock_nn = condBlock;
    generateBooleanBranch(
        parsedWhileStmt.condition, *bodyBlock, *endBlock, state);

    state.m_currentBasicBlock_nn = bodyBlock;
    generateStmt(parsedWhileStmt.body, state);
    if (!blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        state.m_currentBasicBlock_nn->pushInst(JumpValue::get(condBlock, { }));
    }

    state.m_loopBlocksByWhileStmt.erase(whileStmt);
    state.m_currentBasicBlock_nn = endBlock;
}

void Generator::generateBreakStmt(
    Ptr<BreakStmt> breakStmt, FunctionGenerationState& state) const
{
    const auto loop = state.m_semanticInfo_nn->findLoop(breakStmt);
    if (!loop.has_value()) {
        throw std::runtime_error("break statement references no loop binding");
    }
    const auto loopIt = state.m_loopBlocksByWhileStmt.find(*loop);
    if (loopIt == state.m_loopBlocksByWhileStmt.end()) {
        throw std::runtime_error(
            "break statement references unknown loop target");
    }
    state.m_currentBasicBlock_nn->pushInst(
        JumpValue::get(loopIt->second.m_endBlock_nn, { }));
}

void Generator::generateContinueStmt(
    Ptr<ContinueStmt> continueStmt, FunctionGenerationState& state) const
{
    const auto loop = state.m_semanticInfo_nn->findLoop(continueStmt);
    if (!loop.has_value()) {
        throw std::runtime_error(
            "continue statement references no loop binding");
    }
    const auto loopIt = state.m_loopBlocksByWhileStmt.find(*loop);
    if (loopIt == state.m_loopBlocksByWhileStmt.end()) {
        throw std::runtime_error(
            "continue statement references unknown loop target");
    }
    state.m_currentBasicBlock_nn->pushInst(
        JumpValue::get(loopIt->second.m_condBlock_nn, { }));
}

void Generator::generateAssignStmt(
    Ptr<AssignStmt> assignStmt, FunctionGenerationState& state) const
{
    const auto& parsedAssignStmt
        = node(assignStmt, state, "assignment is null");
    const auto& lValExp = parsedAssignStmt.lval(state.m_ast_nn);
    MATCH(lValExp.kind)
    WITH(
        [&](Exp::LVal expAlt) {
            auto* address = generateLValueAddress(expAlt, state);
            auto* value = generateExp(parsedAssignStmt.exp, state);
            state.m_currentBasicBlock_nn->pushInst(
                StoreValue::get(value, address));
        },
        [&](const auto&) {
            throw std::runtime_error(
                "assignment lhs is not an lvalue expression");
        }, );
}

void Generator::generateExpStmt(
    Ptr<ExpStmt> expStmt, FunctionGenerationState& state) const
{
    const auto& parsedExpStmt
        = node(expStmt, state, "expression statement is null");
    if (parsedExpStmt.exp) {
        (void)generateExp(parsedExpStmt.exp.ref(), state);
    }
}

ReturnValue* Generator::generateReturnStmt(
    Ptr<ReturnStmt> returnStmt, FunctionGenerationState& state) const
{
    const auto& parsedReturnStmt
        = node(returnStmt, state, "return statement is null");
    auto* returnValue = ReturnValue::get(parsedReturnStmt.exp
            ? generateExp(parsedReturnStmt.exp.ref(), state)
            : nullptr);
    state.m_currentBasicBlock_nn->pushInst(returnValue);
    return returnValue;
}

Value* Generator::generateExp(
    Ref<Exp> exp, FunctionGenerationState& state) const
{
    if (const auto constantValue
        = state.m_semanticInfo_nn->findConstantValue(exp);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    const auto& parsedExp = exp(state.m_ast_nn);
    return MATCH(parsedExp.kind) WITH(
        [&](Exp::Binary expAlt) -> Value* {
            if (expAlt.op == BinaryOpKeyword::orOr
                || expAlt.op == BinaryOpKeyword::andAnd) {
                return generateBooleanAsInt(exp, state);
            }
            return generateBinaryExpValue(expAlt, state);
        },
        [&](Exp::Unary expAlt) -> Value* {
            return generateUnaryExpValue(expAlt, state);
        },
        [&](Exp::Call expAlt) -> Value* {
            const auto& symbol = requireSymbolForIdentifier(expAlt.funcName,
                *state.m_semanticInfo_nn,
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
                args.push_back(generateExp(arg_nn, state));
            }
            const auto* functionType = dynamic_cast<FunctionType*>(
                functionIt->second->getFuncType());
            const std::string callName
                = functionType->getResultType()->isUnitType()
                ? std::string { }
                : makeTempName(state.m_nextTempId);
            auto* callValue
                = CallValue::get(functionIt->second, std::move(args), callName);
            state.m_currentBasicBlock_nn->pushInst(callValue);
            return callValue;
        },
        [&](Exp::LVal expAlt) -> Value* {
            auto* address = generateLValueAddress(expAlt, state);
            const auto expType = state.m_semanticInfo_nn->findExpType(exp);
            if (expType.has_value() && expType->isArray()) {
                const auto pointeeType
                    = dynamic_cast<PointerType*>(address->getVType())
                          ->getPointeeType();
                if (pointeeType->isArrayType()) {
                    auto* decayed = GetElemPtrValue::get(address,
                        IntegerValue::get(0), makeTempName(state.m_nextTempId));
                    state.m_currentBasicBlock_nn->pushInst(decayed);
                    return decayed;
                }
                return address;
            }
            auto* loadValue
                = LoadValue::get(address, makeTempName(state.m_nextTempId));
            state.m_currentBasicBlock_nn->pushInst(loadValue);
            return loadValue;
        },
        [&](Exp::Number expAlt) -> Value* { return generateNumber(expAlt); });
}

Value* Generator::generateBinaryExpValue(
    const Exp::Binary& binaryExp, FunctionGenerationState& state) const
{
    auto* lhs = generateExp(binaryExp.lhs, state);
    auto* rhs = generateExp(binaryExp.rhs, state);
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
        op, lhs, rhs, *state.m_currentBasicBlock_nn, state.m_nextTempId);
}

Value* Generator::generateUnaryExpValue(
    const Exp::Unary& unaryExp, FunctionGenerationState& state) const
{
    auto* operand = generateExp(unaryExp.lhs, state);
    switch (unaryExp.op) {
    case UnaryOpKeyword::plus:
        return generateBinaryValue(KOOPA_RBO_ADD, IntegerValue::get(0), operand,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    case UnaryOpKeyword::minus:
        return generateBinaryValue(KOOPA_RBO_SUB, IntegerValue::get(0), operand,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    case UnaryOpKeyword::bang:
        return generateBinaryValue(KOOPA_RBO_EQ, IntegerValue::get(0), operand,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    }
    throw std::runtime_error("unsupported unary operator");
}

Value* Generator::generateBooleanAsInt(
    Ref<Exp> exp, FunctionGenerationState& state) const
{
    auto* resultStorage
        = AllocValue::get(Int32Type::get(), makeTempName(state.m_nextTempId));
    state.m_currentBasicBlock_nn->pushInst(resultStorage);
    state.m_currentBasicBlock_nn->pushInst(
        StoreValue::get(IntegerValue::get(0), resultStorage));

    auto* trueBlock = createBasicBlock("bool_true", state);
    auto* falseBlock = createBasicBlock("bool_false", state);
    auto* contBlock = createBasicBlock("bool_end", state);

    generateBooleanBranch(exp, *trueBlock, *falseBlock, state);

    trueBlock->pushInst(StoreValue::get(IntegerValue::get(1), resultStorage));
    trueBlock->pushInst(JumpValue::get(contBlock, { }));
    falseBlock->pushInst(StoreValue::get(IntegerValue::get(0), resultStorage));
    falseBlock->pushInst(JumpValue::get(contBlock, { }));

    state.m_currentBasicBlock_nn = contBlock;
    auto* loadValue
        = LoadValue::get(resultStorage, makeTempName(state.m_nextTempId));
    contBlock->pushInst(loadValue);
    return loadValue;
}

void Generator::generateBooleanBranch(Ref<Exp> exp, BasicBlock& trueBlock,
    BasicBlock& falseBlock, FunctionGenerationState& state) const
{
    if (const auto constantValue
        = state.m_semanticInfo_nn->findConstantValue(exp);
        constantValue.has_value()) {
        state.m_currentBasicBlock_nn->pushInst(
            BranchValue::get(IntegerValue::get(*constantValue), &trueBlock, { },
                &falseBlock, { }));
        return;
    }

    const auto& parsedExp = exp(state.m_ast_nn);
    MATCH(parsedExp.kind)
    WITH(
        [&](Exp::Binary expAlt) {
            if (expAlt.op == BinaryOpKeyword::orOr) {
                generateLogicalOrBranch(expAlt, trueBlock, falseBlock, state);
                return;
            }
            if (expAlt.op == BinaryOpKeyword::andAnd) {
                generateLogicalAndBranch(expAlt, trueBlock, falseBlock, state);
                return;
            }

            auto* value = generateBinaryExpValue(expAlt, state);
            state.m_currentBasicBlock_nn->pushInst(
                BranchValue::get(value, &trueBlock, { }, &falseBlock, { }));
        },
        [&](const auto&) {
            auto* value = generateExp(exp, state);
            state.m_currentBasicBlock_nn->pushInst(
                BranchValue::get(value, &trueBlock, { }, &falseBlock, { }));
        });
}

void Generator::generateLogicalOrBranch(const Exp::Binary& binaryExp,
    BasicBlock& trueBlock, BasicBlock& falseBlock,
    FunctionGenerationState& state) const
{
    auto* nextOperandBlock = createBasicBlock("lor_rhs", state);
    generateBooleanBranch(binaryExp.lhs, trueBlock, *nextOperandBlock, state);
    state.m_currentBasicBlock_nn = nextOperandBlock;
    generateBooleanBranch(binaryExp.rhs, trueBlock, falseBlock, state);
}

void Generator::generateLogicalAndBranch(const Exp::Binary& binaryExp,
    BasicBlock& trueBlock, BasicBlock& falseBlock,
    FunctionGenerationState& state) const
{
    auto* nextOperandBlock = createBasicBlock("land_rhs", state);
    generateBooleanBranch(binaryExp.lhs, *nextOperandBlock, falseBlock, state);
    state.m_currentBasicBlock_nn = nextOperandBlock;
    generateBooleanBranch(binaryExp.rhs, trueBlock, falseBlock, state);
}

Value* Generator::generateBooleanizedValue(
    Value* value, BasicBlock& basicBlock, int32_t& nextTempId) const
{
    return generateBinaryValue(
        KOOPA_RBO_NOT_EQ, IntegerValue::get(0), value, basicBlock, nextTempId);
}

BinaryValue* Generator::generateBinaryValue(koopa_raw_binary_op op, Value* lhs,
    Value* rhs, BasicBlock& basicBlock, int32_t& nextTempId) const
{
    auto* binaryValue
        = BinaryValue::get(op, lhs, rhs, makeTempName(nextTempId));
    basicBlock.pushInst(binaryValue);
    return binaryValue;
}

Value* Generator::generateNumber(const Exp::Number& number) const
{
    return IntegerValue::get(number.value);
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

void Generator::generateLocalArrayInitializer(Value* address,
    const SemanticType& type, const std::vector<Ptr<Exp>>& scalarExprs,
    size_t& nextScalarIndex, FunctionGenerationState& state) const
{
    if (!type.isArray()) {
        Value* initValue = IntegerValue::get(0);
        if (nextScalarIndex < scalarExprs.size()) {
            const auto exp_nn = scalarExprs[nextScalarIndex++];
            if (exp_nn) {
                initValue = generateExp(exp_nn.ref(), state);
            }
        }
        state.m_currentBasicBlock_nn->pushInst(
            StoreValue::get(initValue, address));
        return;
    }

    if (type.m_elementType == nullptr) {
        throw std::runtime_error("array type is missing element type");
    }

    for (int32_t i = 0; i < type.m_arrayLength; ++i) {
        auto* elementAddress = GetElemPtrValue::get(
            address, IntegerValue::get(i), makeTempName(state.m_nextTempId));
        state.m_currentBasicBlock_nn->pushInst(elementAddress);
        generateLocalArrayInitializer(elementAddress, *type.m_elementType,
            scalarExprs, nextScalarIndex, state);
    }
}

Value* Generator::generateLValueAddress(
    const Exp::LVal& lVal, FunctionGenerationState& state) const
{
    const auto& symbol = requireSymbolForIdentifier(lVal.identifier,
        *state.m_semanticInfo_nn, "lvalue is missing a symbol binding");
    const auto storageIt = state.m_storageBySymbolId.find(symbol.m_id);
    if (storageIt == state.m_storageBySymbolId.end()) {
        throw std::runtime_error("lvalue references undefined storage");
    }

    auto* address = storageIt->second;
    auto currentType = symbol.m_type;
    bool indexesDecayedArrayParameter = false;
    if (currentType.isArray() && currentType.m_arrayLength == -1) {
        auto* loadedPointer
            = LoadValue::get(address, makeTempName(state.m_nextTempId));
        state.m_currentBasicBlock_nn->pushInst(loadedPointer);
        address = loadedPointer;
        indexesDecayedArrayParameter = true;
    }

    for (const auto index_nn : lVal.indices) {
        auto* indexValue = generateExp(index_nn, state);
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
        state.m_currentBasicBlock_nn->pushInst(nextAddress);
        address = nextAddress;
        if (currentType.isArray() && currentType.m_elementType != nullptr) {
            currentType = *currentType.m_elementType;
        }
        indexesDecayedArrayParameter = false;
    }

    return address;
}

BasicBlock* Generator::createBasicBlock(
    const std::string& stem, FunctionGenerationState& state) const
{
    auto* basicBlock = BasicBlock::createNonEntry(
        "%" + stem + "_" + std::to_string(state.m_nextBlockId++));
    state.m_function_nn->pushBB(basicBlock);
    return basicBlock;
}

bool Generator::blockHasTerminator(const BasicBlock& basicBlock) const
{
    return basicBlock.getNumInsts() > 0
        && basicBlock.getInst(basicBlock.getNumInsts() - 1)
               ->canTerminateBlock();
}

void Generator::finalizeBasicBlock(
    BasicBlock& basicBlock, BasicBlock& endBlock) const
{
    if (blockHasTerminator(basicBlock)) {
        return;
    }

    basicBlock.pushInst(JumpValue::get(&endBlock, { }));
}

std::string Generator::makeUniqueLocalName(const SemanticSymbol& symbol,
    std::unordered_set<std::string>& usedSymbolNames) const
{
    const std::string baseName = "%" + normalizeIdentifierStem(symbol.name);
    if (usedSymbolNames.insert(baseName).second) {
        return baseName;
    }

    int32_t suffix = 1;
    while (true) {
        const std::string candidate = baseName + "_" + std::to_string(suffix++);
        if (usedSymbolNames.insert(candidate).second) {
            return candidate;
        }
    }
}

} // namespace yesod::koopa
