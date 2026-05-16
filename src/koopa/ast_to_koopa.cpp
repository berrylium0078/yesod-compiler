#include "koopa/ast_to_koopa.h"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace yesod::koopa {

namespace {

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

    Type* lowerFuncType(frontend::FuncTypeKeyword funcType)
    {
        switch (funcType) {
        case frontend::FuncTypeKeyword::voidKeyword:
            return UnitType::get();
        case frontend::FuncTypeKeyword::intKeyword:
            return Int32Type::get();
        }

        throw std::runtime_error("unsupported function type");
    }

    Type* lowerExpType(frontend::ExpType expType)
    {
        switch (expType) {
        case frontend::ExpType::integer:
        case frontend::ExpType::boolean:
            return Int32Type::get();
        case frontend::ExpType::voidType:
            return UnitType::get();
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

Program* Generator::generate(const frontend::AST& ast,
    frontend::Handle<frontend::CompUnit> compUnit,
    const frontend::SemanticInfo& semanticInfo) const
{
    auto* program = Program::create();
    const auto& parsedCompUnit = ast.get(compUnit);
    std::unordered_map<int32_t, Value*> globalStorageBySymbolId;
    std::unordered_map<int32_t, Function*> functionBySymbolId;
    std::unordered_map<int32_t, size_t> symbolUseCount;
    std::unordered_set<int32_t> definedFunctionSymbolIds;

    for (const auto& [identifier_nn, symbol] : semanticInfo.m_symbolByIdentifier) {
        (void)identifier_nn;
        ++symbolUseCount[symbol.m_id];
    }

    for (const auto topLevelItem_nn : parsedCompUnit.m_topLevelItems) {
        const auto& topLevelItem = ast.get(topLevelItem_nn);
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType, frontend::Handle<frontend::FuncDef>>) {
                    const auto& funcDef = ast.get(topLevelAlt);
                    const auto* functionSymbol
                        = semanticInfo.findSymbol(funcDef.m_identifier_nn);
                    if (functionSymbol == nullptr) {
                        throw std::runtime_error(
                            "function declaration missing semantic symbol during lowering");
                    }
                    definedFunctionSymbolIds.insert(functionSymbol->m_id);
                }
            },
            topLevelItem.m_topLevelItem);
    }

    for (const auto& [identifier_nn, symbol] : semanticInfo.m_symbolByIdentifier) {
        (void)identifier_nn;
        if (symbol.m_kind != frontend::SemanticSymbolKind::function) {
            continue;
        }
        if (definedFunctionSymbolIds.find(symbol.m_id)
            != definedFunctionSymbolIds.end()) {
            continue;
        }
        if (symbolUseCount[symbol.m_id] <= 1) {
            continue;
        }

        auto [it, inserted] = functionBySymbolId.try_emplace(symbol.m_id, nullptr);
        if (!inserted) {
            continue;
        }

        auto* function = createExternalFunctionDecl(symbol);
        it->second = function;
        program->pushFunc(function);
    }

    for (const auto topLevelItem_nn : parsedCompUnit.m_topLevelItems) {
        const auto& topLevelItem = ast.get(topLevelItem_nn);
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType,
                                  frontend::Handle<frontend::DeclNode>>) {
                    generateGlobalDecl(topLevelAlt, *program, ast,
                        semanticInfo, globalStorageBySymbolId);
                } else {
                    auto* function
                        = createFunctionDecl(ast, topLevelAlt, semanticInfo);
                    const auto& symbol = requireSymbolForIdentifier(
                        ast.get(topLevelAlt).m_identifier_nn, semanticInfo,
                        "function definition is missing a symbol binding");
                    functionBySymbolId[symbol.m_id] = function;
                    program->pushFunc(function);
                }
            },
            topLevelItem.m_topLevelItem);
    }

    for (const auto topLevelItem_nn : parsedCompUnit.m_topLevelItems) {
        const auto& topLevelItem = ast.get(topLevelItem_nn);
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType,
                                  frontend::Handle<frontend::FuncDef>>) {
                    const auto& symbol = requireSymbolForIdentifier(
                        ast.get(topLevelAlt).m_identifier_nn, semanticInfo,
                        "function definition is missing a symbol binding");
                    const auto functionIt = functionBySymbolId.find(symbol.m_id);
                    if (functionIt == functionBySymbolId.end()) {
                        throw std::runtime_error(
                            "function definition is missing a lowered function declaration");
                    }
                    (void)generateFuncDef(ast, topLevelAlt, semanticInfo,
                        globalStorageBySymbolId, functionBySymbolId,
                        functionIt->second);
                }
            },
            topLevelItem.m_topLevelItem);
    }

    return program;
}

Function* Generator::createFunctionDecl(const frontend::AST& ast,
    frontend::Handle<frontend::FuncDef> funcDef,
    const frontend::SemanticInfo&) const
{
    const auto& parsedFuncDef = ast.get(funcDef);
    const auto& identifier = ast.get(parsedFuncDef.m_identifier_nn);
    std::vector<Type*> paramTypes(parsedFuncDef.m_funcFParams.size(),
        Int32Type::get());
    auto* function = Function::create(
        FunctionType::get(lowerFuncType(parsedFuncDef.m_funcType), paramTypes),
        makeFunctionName(identifier.m_name));
    for (size_t i = 0; i < parsedFuncDef.m_funcFParams.size(); ++i) {
        function->pushParam(FuncArgRefValue::get(i, Int32Type::get()));
    }
    return function;
}

Function* Generator::createExternalFunctionDecl(
    const frontend::SemanticSymbol& symbol) const
{
    std::vector<Type*> paramTypes;
    paramTypes.reserve(symbol.m_functionSignature.m_paramTypes.size());
    for (const auto paramType : symbol.m_functionSignature.m_paramTypes) {
        paramTypes.push_back(lowerExpType(paramType));
    }
    auto* function = Function::create(FunctionType::get(
                                         lowerExpType(symbol.m_functionSignature.m_returnType),
                                         paramTypes),
        makeFunctionName(symbol.m_name));
    for (size_t i = 0; i != paramTypes.size(); ++i) {
        function->pushParam(FuncArgRefValue::get(i, paramTypes[i]));
    }
    return function;
}

Function* Generator::generateFuncDef(const frontend::AST& ast,
    frontend::Handle<frontend::FuncDef> funcDef,
    const frontend::SemanticInfo& semanticInfo,
    const std::unordered_map<int32_t, Value*>& globalStorageBySymbolId,
    const std::unordered_map<int32_t, Function*>& functionBySymbolId,
    Function* function_nn) const
{
    const auto& parsedFuncDef = ast.get(funcDef);
    auto* function = function_nn;
    auto* entryBlock = BasicBlock::createEntry("%entry");
    auto* endBlock = BasicBlock::createNonEntry("%end");
    function->pushBB(entryBlock);
    FunctionGenerationState state {
        .m_ast_nn = &ast,
        .m_semanticInfo_nn = &semanticInfo,
        .m_function_nn = function,
        .m_currentBasicBlock_nn = entryBlock,
        .m_endBlock_nn = endBlock,
        .m_storageBySymbolId = globalStorageBySymbolId,
        .m_functionBySymbolId = functionBySymbolId,
    };
    for (size_t i = 0; i < parsedFuncDef.m_funcFParams.size(); ++i) {
        const auto& funcFParam = ast.get(parsedFuncDef.m_funcFParams[i]);
        const auto& symbol = requireSymbolForIdentifier(funcFParam.m_identifier_nn,
            semanticInfo, "function parameter is missing a symbol binding");
        auto* alloc = AllocValue::get(Int32Type::get(),
            makeUniqueLocalName(symbol, state.m_usedSymbolNames));
        entryBlock->pushInst(alloc);
        entryBlock->pushInst(StoreValue::get(function->getParam(i), alloc));
        state.m_storageBySymbolId[symbol.m_id] = alloc;
    }
    generateBlock(parsedFuncDef.m_block_nn, state);
    finalizeBasicBlock(*state.m_currentBasicBlock_nn, *endBlock);
    if (parsedFuncDef.m_funcType == frontend::FuncTypeKeyword::voidKeyword) {
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

void Generator::generateGlobalDecl(frontend::Handle<frontend::DeclNode> declNode,
    Program& program, const frontend::AST& ast,
    const frontend::SemanticInfo& semanticInfo,
    std::unordered_map<int32_t, Value*>& globalStorageBySymbolId) const
{
    const auto& parsedDeclNode = ast.get(declNode);
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType,
                              frontend::Handle<frontend::ConstDecl>>) {
                return;
            } else {
                const auto& varDecl = ast.get(declAlt);
                for (const auto varDef_nn : varDecl.m_varDefs) {
                    const auto& varDef = ast.get(varDef_nn);
                    const auto& symbol = requireSymbolForIdentifier(
                        varDef.m_identifier_nn, semanticInfo,
                        "global variable is missing its symbol binding");
                    Value* initValue = ZeroInitValue::get(Int32Type::get());
                    if (varDef.m_initVal_nn) {
                        const auto& initVal = ast.get(varDef.m_initVal_nn);
                        const auto constantValue
                            = semanticInfo.findConstantValue(initVal.m_exp_nn);
                        if (!constantValue.has_value()) {
                            throw std::runtime_error(
                                "global variable initializer must be constant");
                        }
                        initValue = IntegerValue::get(*constantValue);
                    }
                    auto* globalAlloc = GlobalAllocValue::get(
                        initValue, makeGlobalName(symbol.m_name));
                    program.pushVal(globalAlloc);
                    globalStorageBySymbolId[symbol.m_id] = globalAlloc;
                }
            }
        },
        parsedDeclNode.m_decl);
}

void Generator::generateBlock(frontend::Handle<frontend::Block> block,
    FunctionGenerationState& state) const
{
    const auto& parsedBlock = node(block, state, "block is missing");
    for (const auto blockItem : parsedBlock.m_blockItems) {
        if (blockHasTerminator(*state.m_currentBasicBlock_nn)) {
            break;
        }
        generateBlockItem(blockItem, state);
    }
}

void Generator::generateBlockItem(
    frontend::Handle<frontend::BlockItemNode> blockItem,
    FunctionGenerationState& state) const
{
    if (blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        return;
    }

    const auto& parsedBlockItem = node(blockItem, state, "block item is null");
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType,
                              frontend::Handle<frontend::DeclNode>>) {
                generateDecl(blockItemAlt, state);
            } else {
                generateStmt(blockItemAlt, state);
            }
        },
        parsedBlockItem.m_blockItem);
}

void Generator::generateDecl(frontend::Handle<frontend::DeclNode> declNode,
    FunctionGenerationState& state) const
{
    const auto& parsedDeclNode = node(declNode, state, "declaration is null");
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType,
                              frontend::Handle<frontend::ConstDecl>>) {
                const auto& constDecl
                    = node(declAlt, state, "const declaration payload is null");
                for (const auto constDef : constDecl.m_constDefs) {
                    const auto& parsedConstDef = node(
                        constDef, state, "const declarator payload is null");
                    const auto& symbol = requireSymbolForIdentifier(
                        parsedConstDef.m_identifier_nn,
                        *state.m_semanticInfo_nn,
                        "const declarator is missing its symbol binding");
                    auto* alloc = AllocValue::get(Int32Type::get(),
                        makeUniqueLocalName(symbol, state.m_usedSymbolNames));
                    state.m_currentBasicBlock_nn->pushInst(alloc);
                    state.m_storageBySymbolId[symbol.m_id] = alloc;
                    const auto& constInitVal
                        = node(parsedConstDef.m_constInitVal_nn, state,
                            "const declarator is missing an initializer");
                    auto* initValue = generateExp(constInitVal.m_exp_nn, state);
                    state.m_currentBasicBlock_nn->pushInst(
                        StoreValue::get(initValue, alloc));
                }
            } else {
                const auto& varDecl
                    = node(declAlt, state, "var declaration payload is null");
                for (const auto varDef : varDecl.m_varDefs) {
                    const auto& resolvedVarDef
                        = node(varDef, state, "var declarator payload is null");
                    const auto& symbol = requireSymbolForIdentifier(
                        resolvedVarDef.m_identifier_nn,
                        *state.m_semanticInfo_nn,
                        "var declarator is missing its symbol binding");
                    auto* alloc = AllocValue::get(Int32Type::get(),
                        makeUniqueLocalName(symbol, state.m_usedSymbolNames));
                    state.m_currentBasicBlock_nn->pushInst(alloc);
                    state.m_storageBySymbolId[symbol.m_id] = alloc;
                    if (resolvedVarDef.m_initVal_nn) {
                        const auto& initVal = node(resolvedVarDef.m_initVal_nn,
                            state, "var declarator init payload is null");
                        auto* initValue = generateExp(initVal.m_exp_nn, state);
                        state.m_currentBasicBlock_nn->pushInst(
                            StoreValue::get(initValue, alloc));
                    }
                }
            }
        },
        parsedDeclNode.m_decl);
}

void Generator::generateStmt(frontend::Handle<frontend::StmtNode> stmtNode,
    FunctionGenerationState& state) const
{
    const auto& parsedStmtNode = node(stmtNode, state, "statement is null");
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              frontend::Handle<frontend::IfStmt>>) {
                generateIfStmt(stmtAlt, state);
            } else if constexpr (std::is_same_v<AltType,
                                     frontend::Handle<frontend::WhileStmt>>) {
                generateWhileStmt(stmtAlt, state);
            } else if constexpr (std::is_same_v<AltType,
                                     frontend::Handle<frontend::BreakStmt>>) {
                generateBreakStmt(stmtAlt, state);
            } else if constexpr (std::is_same_v<AltType,
                                     frontend::Handle<
                                         frontend::ContinueStmt>>) {
                generateContinueStmt(stmtAlt, state);
            } else if constexpr (std::is_same_v<AltType,
                                     frontend::Handle<frontend::AssignStmt>>) {
                generateAssignStmt(stmtAlt, state);
            } else if constexpr (std::is_same_v<AltType,
                                     frontend::Handle<frontend::Block>>) {
                generateBlock(stmtAlt, state);
            } else if constexpr (std::is_same_v<AltType,
                                     frontend::Handle<frontend::ExpStmt>>) {
                generateExpStmt(stmtAlt, state);
            } else {
                (void)generateReturnStmt(stmtAlt, state);
            }
        },
        parsedStmtNode.m_stmt);
}

void Generator::generateIfStmt(frontend::Handle<frontend::IfStmt> ifStmt,
    FunctionGenerationState& state) const
{
    const auto& parsedIfStmt = node(ifStmt, state, "if statement is null");
    auto* thenBlock = createBasicBlock("if_then", state);
    BasicBlock* elseBlock = nullptr;
    if (parsedIfStmt.m_elseStmt_nn) {
        elseBlock = createBasicBlock("if_else", state);
    }
    auto* contBlock = createBasicBlock("if_end", state);
    if (elseBlock == nullptr) {
        elseBlock = contBlock;
    }

    generateBooleanBranch(
        parsedIfStmt.m_condExp_nn, *thenBlock, *elseBlock, state);

    state.m_currentBasicBlock_nn = thenBlock;
    generateStmt(parsedIfStmt.m_thenStmt_nn, state);
    if (!blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        state.m_currentBasicBlock_nn->pushInst(JumpValue::get(contBlock, {}));
    }

    if (parsedIfStmt.m_elseStmt_nn) {
        state.m_currentBasicBlock_nn = elseBlock;
        generateStmt(parsedIfStmt.m_elseStmt_nn, state);
        if (!blockHasTerminator(*state.m_currentBasicBlock_nn)) {
            state.m_currentBasicBlock_nn->pushInst(
                JumpValue::get(contBlock, {}));
        }
    }

    state.m_currentBasicBlock_nn = contBlock;
}

void Generator::generateWhileStmt(
    frontend::Handle<frontend::WhileStmt> whileStmt,
    FunctionGenerationState& state) const
{
    const auto& parsedWhileStmt
        = node(whileStmt, state, "while statement is null");
    auto* condBlock = createBasicBlock("while_cond", state);
    auto* bodyBlock = createBasicBlock("while_body", state);
    auto* endBlock = createBasicBlock("while_end", state);

    state.m_currentBasicBlock_nn->pushInst(JumpValue::get(condBlock, {}));

    state.m_loopBlocksByWhileStmt[whileStmt]
        = FunctionGenerationState::LoopBlocks {
              .m_condBlock_nn = condBlock,
              .m_endBlock_nn = endBlock,
          };

    state.m_currentBasicBlock_nn = condBlock;
    generateBooleanBranch(
        parsedWhileStmt.m_condExp_nn, *bodyBlock, *endBlock, state);

    state.m_currentBasicBlock_nn = bodyBlock;
    generateStmt(parsedWhileStmt.m_bodyStmt_nn, state);
    if (!blockHasTerminator(*state.m_currentBasicBlock_nn)) {
        state.m_currentBasicBlock_nn->pushInst(JumpValue::get(condBlock, {}));
    }

    state.m_loopBlocksByWhileStmt.erase(whileStmt);
    state.m_currentBasicBlock_nn = endBlock;
}

void Generator::generateBreakStmt(
    frontend::Handle<frontend::BreakStmt> breakStmt,
    FunctionGenerationState& state) const
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
        JumpValue::get(loopIt->second.m_endBlock_nn, {}));
}

void Generator::generateContinueStmt(
    frontend::Handle<frontend::ContinueStmt> continueStmt,
    FunctionGenerationState& state) const
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
        JumpValue::get(loopIt->second.m_condBlock_nn, {}));
}

void Generator::generateAssignStmt(
    frontend::Handle<frontend::AssignStmt> assignStmt,
    FunctionGenerationState& state) const
{
    const auto& parsedAssignStmt
        = node(assignStmt, state, "assignment is null");
    const auto& lValExp = node(parsedAssignStmt.m_lVal_nn, state,
        "assignment lvalue expression is null");
    const auto* lVal = std::get_if<frontend::LVal>(&lValExp.m_kind);
    if (lVal == nullptr) {
        throw std::runtime_error("assignment lhs is not an lvalue expression");
    }
    const auto& symbol = requireSymbolForIdentifier(lVal->m_identifier_nn,
        *state.m_semanticInfo_nn, "assignment lvalue is missing a symbol");
    const auto storageIt = state.m_storageBySymbolId.find(symbol.m_id);
    if (storageIt == state.m_storageBySymbolId.end()) {
        throw std::runtime_error("assignment references undefined storage");
    }

    auto* value = generateExp(parsedAssignStmt.m_exp_nn, state);
    state.m_currentBasicBlock_nn->pushInst(
        StoreValue::get(value, storageIt->second));
}

void Generator::generateExpStmt(frontend::Handle<frontend::ExpStmt> expStmt,
    FunctionGenerationState& state) const
{
    const auto& parsedExpStmt
        = node(expStmt, state, "expression statement is null");
    if (parsedExpStmt.m_exp_nn) {
        (void)generateExp(parsedExpStmt.m_exp_nn, state);
    }
}

ReturnValue* Generator::generateReturnStmt(
    frontend::Handle<frontend::ReturnStmt> returnStmt,
    FunctionGenerationState& state) const
{
    const auto& parsedReturnStmt
        = node(returnStmt, state, "return statement is null");
    auto* returnValue = ReturnValue::get(parsedReturnStmt.m_exp_nn
            ? generateExp(parsedReturnStmt.m_exp_nn, state)
            : nullptr);
    state.m_currentBasicBlock_nn->pushInst(returnValue);
    return returnValue;
}

Value* Generator::generateExp(
    frontend::Handle<frontend::Exp> exp, FunctionGenerationState& state) const
{
    if (const auto constantValue
        = state.m_semanticInfo_nn->findConstantValue(exp);
        constantValue.has_value()) {
        return IntegerValue::get(*constantValue);
    }
    const auto& parsedExp = node(exp, state, "expression is missing");
    return std::visit(
        [&](const auto& expAlt) -> Value* {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, frontend::Exp::Binary>) {
                if (expAlt.m_op == frontend::BinaryOpKeyword::orOr
                    || expAlt.m_op == frontend::BinaryOpKeyword::andAnd) {
                    return generateBooleanAsInt(exp, state);
                }
                return generateBinaryExpValue(expAlt, state);
            } else if constexpr (std::is_same_v<AltType,
                                     frontend::Exp::Unary>) {
                return generateUnaryExpValue(expAlt, state);
            } else if constexpr (std::is_same_v<AltType,
                                     frontend::Exp::Call>) {
                const auto& symbol = requireSymbolForIdentifier(
                    expAlt.m_func_nn, *state.m_semanticInfo_nn,
                    "call target is missing a symbol binding");
                const auto functionIt = state.m_functionBySymbolId.find(symbol.m_id);
                if (functionIt == state.m_functionBySymbolId.end()) {
                    throw std::runtime_error(
                        "call target is missing a lowered function binding");
                }
                std::vector<Value*> args;
                args.reserve(expAlt.m_params.size());
                for (const auto arg_nn : expAlt.m_params) {
                    args.push_back(generateExp(arg_nn, state));
                }
                const auto* functionType = dynamic_cast<FunctionType*>(
                    functionIt->second->getFuncType());
                const std::string callName
                    = functionType->getResultType()->isUnitType()
                    ? std::string {}
                    : makeTempName(state.m_nextTempId);
                auto* callValue = CallValue::get(
                    functionIt->second, std::move(args), callName);
                state.m_currentBasicBlock_nn->pushInst(callValue);
                return callValue;
            } else if constexpr (std::is_same_v<AltType, frontend::LVal>) {
                const auto& symbol = requireSymbolForIdentifier(
                    expAlt.m_identifier_nn, *state.m_semanticInfo_nn,
                    "lvalue is missing a symbol binding");
                const auto storageIt
                    = state.m_storageBySymbolId.find(symbol.m_id);
                if (storageIt == state.m_storageBySymbolId.end()) {
                    throw std::runtime_error(
                        "lvalue references undefined storage");
                }
                auto* loadValue = LoadValue::get(
                    storageIt->second, makeTempName(state.m_nextTempId));
                state.m_currentBasicBlock_nn->pushInst(loadValue);
                return loadValue;
            } else {
                return generateNumber(expAlt);
            }
        },
        parsedExp.m_kind);
}

Value* Generator::generateBinaryExpValue(const frontend::Exp::Binary& binaryExp,
    FunctionGenerationState& state) const
{
    auto* lhs = generateExp(binaryExp.m_lhs_nn, state);
    auto* rhs = generateExp(binaryExp.m_rhs_nn, state);
    koopa_raw_binary_op op = KOOPA_RBO_ADD;
    switch (binaryExp.m_op) {
    case frontend::BinaryOpKeyword::star:
        op = KOOPA_RBO_MUL;
        break;
    case frontend::BinaryOpKeyword::slash:
        op = KOOPA_RBO_DIV;
        break;
    case frontend::BinaryOpKeyword::percent:
        op = KOOPA_RBO_MOD;
        break;
    case frontend::BinaryOpKeyword::plus:
        op = KOOPA_RBO_ADD;
        break;
    case frontend::BinaryOpKeyword::minus:
        op = KOOPA_RBO_SUB;
        break;
    case frontend::BinaryOpKeyword::less:
        op = KOOPA_RBO_LT;
        break;
    case frontend::BinaryOpKeyword::greater:
        op = KOOPA_RBO_GT;
        break;
    case frontend::BinaryOpKeyword::lessEqual:
        op = KOOPA_RBO_LE;
        break;
    case frontend::BinaryOpKeyword::greaterEqual:
        op = KOOPA_RBO_GE;
        break;
    case frontend::BinaryOpKeyword::equal:
        op = KOOPA_RBO_EQ;
        break;
    case frontend::BinaryOpKeyword::notEqual:
        op = KOOPA_RBO_NOT_EQ;
        break;
    case frontend::BinaryOpKeyword::andAnd:
    case frontend::BinaryOpKeyword::orOr:
        throw std::runtime_error("short-circuit binary expression should lower "
                                 "through boolean branching");
    }
    return generateBinaryValue(
        op, lhs, rhs, *state.m_currentBasicBlock_nn, state.m_nextTempId);
}

Value* Generator::generateUnaryExpValue(
    const frontend::Exp::Unary& unaryExp, FunctionGenerationState& state) const
{
    auto* operand = generateExp(unaryExp.m_lhs_nn, state);
    switch (unaryExp.m_op) {
    case frontend::UnaryOpKeyword::plus:
        return generateBinaryValue(KOOPA_RBO_ADD, IntegerValue::get(0), operand,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    case frontend::UnaryOpKeyword::minus:
        return generateBinaryValue(KOOPA_RBO_SUB, IntegerValue::get(0), operand,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    case frontend::UnaryOpKeyword::bang:
        return generateBinaryValue(KOOPA_RBO_EQ, IntegerValue::get(0), operand,
            *state.m_currentBasicBlock_nn, state.m_nextTempId);
    }
    throw std::runtime_error("unsupported unary operator");
}

Value* Generator::generateBooleanAsInt(
    frontend::Handle<frontend::Exp> exp, FunctionGenerationState& state) const
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
    trueBlock->pushInst(JumpValue::get(contBlock, {}));
    falseBlock->pushInst(StoreValue::get(IntegerValue::get(0), resultStorage));
    falseBlock->pushInst(JumpValue::get(contBlock, {}));

    state.m_currentBasicBlock_nn = contBlock;
    auto* loadValue
        = LoadValue::get(resultStorage, makeTempName(state.m_nextTempId));
    contBlock->pushInst(loadValue);
    return loadValue;
}

void Generator::generateBooleanBranch(frontend::Handle<frontend::Exp> exp,
    BasicBlock& trueBlock, BasicBlock& falseBlock,
    FunctionGenerationState& state) const
{
    if (const auto constantValue
        = state.m_semanticInfo_nn->findConstantValue(exp);
        constantValue.has_value()) {
        state.m_currentBasicBlock_nn->pushInst(
            BranchValue::get(IntegerValue::get(*constantValue), &trueBlock, {},
                &falseBlock, {}));
        return;
    }

    const auto& parsedExp = node(exp, state, "expression is missing");
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, frontend::Exp::Binary>) {
                if (expAlt.m_op == frontend::BinaryOpKeyword::orOr) {
                    generateLogicalOrBranch(
                        expAlt, trueBlock, falseBlock, state);
                    return;
                }
                if (expAlt.m_op == frontend::BinaryOpKeyword::andAnd) {
                    generateLogicalAndBranch(
                        expAlt, trueBlock, falseBlock, state);
                    return;
                }

                auto* value = generateBinaryExpValue(expAlt, state);
                state.m_currentBasicBlock_nn->pushInst(
                    BranchValue::get(value, &trueBlock, {}, &falseBlock, {}));
                return;
            } else {
                auto* value = generateExp(exp, state);
                state.m_currentBasicBlock_nn->pushInst(
                    BranchValue::get(value, &trueBlock, {}, &falseBlock, {}));
            }
        },
        parsedExp.m_kind);
}

void Generator::generateLogicalOrBranch(const frontend::Exp::Binary& binaryExp,
    BasicBlock& trueBlock, BasicBlock& falseBlock,
    FunctionGenerationState& state) const
{
    auto* nextOperandBlock = createBasicBlock("lor_rhs", state);
    generateBooleanBranch(
        binaryExp.m_lhs_nn, trueBlock, *nextOperandBlock, state);
    state.m_currentBasicBlock_nn = nextOperandBlock;
    generateBooleanBranch(binaryExp.m_rhs_nn, trueBlock, falseBlock, state);
}

void Generator::generateLogicalAndBranch(const frontend::Exp::Binary& binaryExp,
    BasicBlock& trueBlock, BasicBlock& falseBlock,
    FunctionGenerationState& state) const
{
    auto* nextOperandBlock = createBasicBlock("land_rhs", state);
    generateBooleanBranch(
        binaryExp.m_lhs_nn, *nextOperandBlock, falseBlock, state);
    state.m_currentBasicBlock_nn = nextOperandBlock;
    generateBooleanBranch(binaryExp.m_rhs_nn, trueBlock, falseBlock, state);
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

Value* Generator::generateNumber(const frontend::Number& number) const
{
    return IntegerValue::get(number.m_value);
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

    basicBlock.pushInst(JumpValue::get(&endBlock, {}));
}

std::string Generator::makeUniqueLocalName(
    const frontend::SemanticSymbol& symbol,
    std::unordered_set<std::string>& usedSymbolNames) const
{
    const std::string baseName
        = "%" + normalizeIdentifierStem(symbol.m_name);
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
