
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "frontend/semantic.h"
namespace yesod::frontend {

namespace {

    [[nodiscard]] bool isScalarTypeImpl(const SemanticType& type)
    {
        return type.kind == SemanticTypeKind::integer
            || type.kind == SemanticTypeKind::boolean;
    }

    [[nodiscard]] bool typesMatchExactly(
        const SemanticType& lhs, const SemanticType& rhs)
    {
        return lhs == rhs;
    }

    [[nodiscard]] bool functionSignaturesMatch(const SemanticSymbol& symbol,
        const SemanticType& returnType,
        const std::vector<SemanticType>& paramTypes)
    {
        return symbol.kind == SemanticSymbolKind::function
            && symbol.m_functionSignature.m_returnType == returnType
            && symbol.m_functionSignature.m_paramTypes == paramTypes;
    }

    [[nodiscard]] bool typesMatchForCallImpl(
        const SemanticType& paramType, const SemanticType& argType)
    {
        if (!paramType.isArray()) {
            return typesMatchExactly(paramType, argType);
        }
        if (!argType.isArray()) {
            return false;
        }
        if (paramType.m_arrayLength == -1) {
            if (paramType.m_elementType == nullptr
                || argType.m_elementType == nullptr) {
                return false;
            }
            return typesMatchExactly(
                *paramType.m_elementType, *argType.m_elementType);
        }
        return typesMatchExactly(paramType, argType);
    }

    [[nodiscard]] std::optional<int32_t> applyUnaryOp(
        UnaryOpKeyword op, int32_t operand)
    {
        switch (op) {
        case UnaryOpKeyword::plus:
            return operand;
        case UnaryOpKeyword::minus:
            return -operand;
        case UnaryOpKeyword::bang:
            return operand == 0 ? 1 : 0;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<int32_t> applyArithmeticOp(
        BinaryOpKeyword op, int32_t lhs, int32_t rhs)
    {
        switch (op) {
        case BinaryOpKeyword::star:
            return static_cast<int32_t>(static_cast<int64_t>(lhs) * rhs);
        case BinaryOpKeyword::slash:
            if (rhs == 0) {
                return std::nullopt;
            }
            return lhs / rhs;
        case BinaryOpKeyword::percent:
            if (rhs == 0) {
                return std::nullopt;
            }
            return lhs % rhs;
        case BinaryOpKeyword::plus:
            return static_cast<int32_t>(static_cast<int64_t>(lhs) + rhs);
        case BinaryOpKeyword::minus:
            return static_cast<int32_t>(static_cast<int64_t>(lhs) - rhs);
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] int32_t applyRelOp(
        BinaryOpKeyword op, int32_t lhs, int32_t rhs)
    {
        switch (op) {
        case BinaryOpKeyword::less:
            return lhs < rhs ? 1 : 0;
        case BinaryOpKeyword::greater:
            return lhs > rhs ? 1 : 0;
        case BinaryOpKeyword::lessEqual:
            return lhs <= rhs ? 1 : 0;
        case BinaryOpKeyword::greaterEqual:
            return lhs >= rhs ? 1 : 0;
        default:
            break;
        }
        throw std::runtime_error("unsupported relational operator");
    }

    [[nodiscard]] int32_t applyEqOp(
        BinaryOpKeyword op, int32_t lhs, int32_t rhs)
    {
        switch (op) {
        case BinaryOpKeyword::equal:
            return lhs == rhs ? 1 : 0;
        case BinaryOpKeyword::notEqual:
            return lhs != rhs ? 1 : 0;
        default:
            break;
        }
        throw std::runtime_error("unsupported equality operator");
    }

    [[nodiscard]] int32_t applyLAndOp(BinaryOpKeyword, int32_t lhs, int32_t rhs)
    {
        return (lhs != 0 && rhs != 0) ? 1 : 0;
    }

    [[nodiscard]] int32_t applyLOrOp(BinaryOpKeyword, int32_t lhs, int32_t rhs)
    {
        return (lhs != 0 || rhs != 0) ? 1 : 0;
    }

    [[nodiscard]] SemanticType lowerFuncType(FuncTypeKeyword funcType)
    {
        switch (funcType) {
        case FuncTypeKeyword::voidKeyword:
            return SemanticType::makeVoid();
        case FuncTypeKeyword::intKeyword:
            return SemanticType::makeInteger();
        }
        throw std::runtime_error("unsupported function type keyword");
    }

} // namespace

SemanticSymbolResolver::SemanticSymbolResolver(const AST& ast)
    : AstVisitor(ast)
{
}

void SemanticSymbolResolver::analyze(Ref<CompUnit> compUnit)
{
    m_symbolByIdentifier.clear();
    m_scopeStack.clear();
    m_diagnostics.clear();
    m_declaredSymbolIds.clear();
    m_definedFunctionSymbolIds.clear();
    m_nextSymbolId = 0;
    traverse(compUnit);
}

const std::unordered_map<Ref<Identifier>, SemanticSymbol>&
SemanticSymbolResolver::symbolsByIdentifier() const
{
    return m_symbolByIdentifier;
}

const std::vector<SemanticDiagnostic>&
SemanticSymbolResolver::diagnostics() const
{
    return m_diagnostics;
}

const SemanticSymbol* SemanticSymbolResolver::findSymbol(
    Ref<Identifier> identifier) const
{
    const auto symbolIt = m_symbolByIdentifier.find(identifier);
    if (symbolIt == m_symbolByIdentifier.end()) {
        return nullptr;
    }
    return &symbolIt->second;
}

bool SemanticSymbolResolver::hasDeclaration(int32_t symbolId) const
{
    return m_declaredSymbolIds.contains(symbolId);
}

void SemanticSymbolResolver::visitCompUnit(Ref<CompUnit> compUnit)
{
    pushScope();

    for (const auto topLevelItem : compUnit(m_ast).topLevelItems) {
        MATCH(topLevelItem)
        WITH([&](Decl decl) { visitDecl(decl); },
            [&](Ref<FuncDef> funcDef) { declareFuncDef(funcDef); });
    }

    for (const auto topLevelItem : compUnit(m_ast).topLevelItems) {
        MATCH(topLevelItem)
        WITH(
            [&](Ref<FuncDef> funcDef) {
                if (funcDef(m_ast).body != nullptr) {
                    visitFuncDef(funcDef);
                }
            },
            [&](const auto&) { });
    }

    popScope();
}

void SemanticSymbolResolver::declareFuncDef(Ref<FuncDef> funcDef_ref)
{
    const auto funcDef = funcDef_ref(m_ast);
    for (const auto& funcFParam : funcDef.funcFParams) {
        for (const auto dimension : funcFParam.shape) {
            visitExp(dimension);
        }
    }

    const auto& identifier = funcDef.identifier(m_ast);
    const auto existingIdentifier = lookupSymbol(identifier.name);
    if (!existingIdentifier.has_value()) {
        const auto symbol = makeFunctionSymbol(funcDef.identifier);
        if (!defineSymbol(identifier.name, funcDef.identifier)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.sourcePos.m_offset,
                "double definition of '" + identifier.name + "'");
        }
        bindSymbol(funcDef.identifier, symbol);
        m_declaredSymbolIds.insert(symbol.m_id);
        if (funcDef.body != nullptr) {
            m_definedFunctionSymbolIds.insert(symbol.m_id);
        }
        return;
    }

    const auto* existingSymbol = findSymbol(*existingIdentifier);
    if (existingSymbol == nullptr) {
        throw std::runtime_error(
            "function declaration is missing an existing symbol binding");
    }

    if (existingSymbol->kind != SemanticSymbolKind::function) {
        recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
            identifier.sourcePos.m_offset,
            "double definition of '" + identifier.name + "'");
        bindSymbol(funcDef.identifier, *existingSymbol);
        return;
    }

    if (funcDef.body != nullptr
        && m_definedFunctionSymbolIds.contains(existingSymbol->m_id)) {
        recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
            identifier.sourcePos.m_offset,
            "redefinition of '" + identifier.name + "'");
    }
    if (funcDef.body != nullptr) {
        m_definedFunctionSymbolIds.insert(existingSymbol->m_id);
    }
    bindSymbol(funcDef.identifier, *existingSymbol);
}

void SemanticSymbolResolver::visitFuncDef(Ref<FuncDef> funcDef_ref)
{
    const auto funcDef = funcDef_ref(m_ast);
    if (funcDef.body == nullptr) {
        return;
    }

    pushScope();
    for (const auto& funcFParam : funcDef.funcFParams) {
        const auto& identifier = funcFParam.identifier(m_ast);
        const auto symbol = makeObjectSymbol(funcFParam.identifier, false);
        if (!defineSymbol(identifier.name, funcFParam.identifier)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.sourcePos.m_offset,
                "double definition of '" + identifier.name + "'");
        }
        bindSymbol(funcFParam.identifier, symbol);
        m_declaredSymbolIds.insert(symbol.m_id);
    }

    for (const auto blockItem : funcDef.body(m_ast).items) {
        visitBlockItem(blockItem);
    }
    popScope();
}

void SemanticSymbolResolver::visitBlock(Ref<Block> block)
{
    pushScope();
    for (const auto blockItem : block(m_ast).items) {
        visitBlockItem(blockItem);
    }
    popScope();
}

void SemanticSymbolResolver::visitConstDecl(Ref<ConstDecl> constDecl)
{
    for (const auto constDef : constDecl(m_ast).constDef) {
        visitConstDef(constDef);
        const auto& parsedConstDef = constDef(m_ast);
        const auto& identifier = parsedConstDef.identifier(m_ast);
        const auto symbol = makeObjectSymbol(parsedConstDef.identifier, true);
        if (!defineSymbol(identifier.name, parsedConstDef.identifier)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.sourcePos.m_offset,
                "double definition of '" + identifier.name + "'");
        }
        bindSymbol(parsedConstDef.identifier, symbol);
        m_declaredSymbolIds.insert(symbol.m_id);
    }
}

void SemanticSymbolResolver::visitVarDecl(Ref<VarDecl> varDecl)
{
    for (const auto varDef : varDecl(m_ast).varDef) {
        visitVarDef(varDef);
        const auto& parsedVarDef = varDef(m_ast);
        const auto& identifier = parsedVarDef.identifier(m_ast);
        const auto symbol = makeObjectSymbol(parsedVarDef.identifier, false);
        if (!defineSymbol(identifier.name, parsedVarDef.identifier)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.sourcePos.m_offset,
                "double definition of '" + identifier.name + "'");
        }
        bindSymbol(parsedVarDef.identifier, symbol);
        m_declaredSymbolIds.insert(symbol.m_id);
    }
}

void SemanticSymbolResolver::visitCallExp(const Exp&, const Exp::Call& call)
{
    (void)resolveIdentifier(call.funcName);
    for (const auto param : call.params) {
        visitExp(param);
    }
}

void SemanticSymbolResolver::visitLValExp(const Exp&, const Exp::LVal& lVal)
{
    (void)resolveIdentifier(lVal.identifier);
    for (const auto index : lVal.indices) {
        visitExp(index);
    }
}

void SemanticSymbolResolver::pushScope() { m_scopeStack.emplace_back(); }

void SemanticSymbolResolver::popScope()
{
    if (!m_scopeStack.empty()) {
        m_scopeStack.pop_back();
    }
}

bool SemanticSymbolResolver::defineSymbol(
    const std::string& name, Ref<Identifier> identifier)
{
    if (m_scopeStack.empty()) {
        pushScope();
    }

    auto& currentScope = m_scopeStack.back();
    return currentScope.emplace(name, identifier).second;
}

std::optional<Ref<Identifier>> SemanticSymbolResolver::lookupSymbol(
    const std::string& name) const
{
    for (auto scopeIt = m_scopeStack.rbegin(); scopeIt != m_scopeStack.rend();
        ++scopeIt) {
        const auto foundIt = scopeIt->find(name);
        if (foundIt != scopeIt->end()) {
            return foundIt->second;
        }
    }
    return std::nullopt;
}

int32_t SemanticSymbolResolver::resolveIdentifier(Ref<Identifier> ident)
{
    const auto definitionIdentifier = lookupSymbol(ident(m_ast).name);
    if (definitionIdentifier.has_value()) {
        const auto* symbol = findSymbol(*definitionIdentifier);
        if (symbol == nullptr) {
            throw std::runtime_error(
                "definition identifier is missing symbol binding");
        }
        bindSymbol(ident, *symbol);
        return symbol->m_id;
    }

    recordDiagnostic(SemanticDiagnosticKind::useBeforeDefinition,
        ident(m_ast).sourcePos.m_offset,
        "use of '" + ident(m_ast).name + "' before definition");
    const auto placeholder = makePlaceholderSymbol(ident);
    bindSymbol(ident, placeholder);
    return placeholder.m_id;
}

void SemanticSymbolResolver::bindSymbol(
    Ref<Identifier> identifier, const SemanticSymbol& symbol)
{
    m_symbolByIdentifier[identifier] = symbol;
}

SemanticSymbol SemanticSymbolResolver::makePlaceholderSymbol(
    Ref<Identifier> identifier)
{
    return makeObjectSymbol(identifier, false);
}

SemanticSymbol SemanticSymbolResolver::makeObjectSymbol(
    Ref<Identifier> identifier, bool isConst)
{
    return SemanticSymbol {
        .m_id = ++m_nextSymbolId,
        .name = identifier(m_ast).name,
        .kind = SemanticSymbolKind::object,
        .m_isConst = isConst,
        .m_hasConstantValue = false,
        .m_constantValue = 0,
        .m_type = SemanticType::makeInteger(),
        .m_functionSignature = { },
    };
}

SemanticSymbol SemanticSymbolResolver::makeFunctionSymbol(
    Ref<Identifier> identifier)
{
    return SemanticSymbol {
        .m_id = ++m_nextSymbolId,
        .name = identifier(m_ast).name,
        .kind = SemanticSymbolKind::function,
        .m_isConst = false,
        .m_hasConstantValue = false,
        .m_constantValue = 0,
        .m_type = SemanticType::makeInteger(),
        .m_functionSignature = { },
    };
}

void SemanticSymbolResolver::recordDiagnostic(SemanticDiagnosticKind kind,
    int32_t offset, std::string message, SemanticDiagnosticSeverity severity)
{
    m_diagnostics.push_back(SemanticDiagnostic {
        .kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
        .m_severity = severity,
    });
}

SemanticTypeAnalyzer::SemanticTypeAnalyzer(
    const AST& ast, const SemanticSymbolResolver& symbolResolver)
    : AstVisitor(ast)
    , m_symbolResolver(symbolResolver)
{
}

void SemanticTypeAnalyzer::analyze(Ref<CompUnit> compUnit)
{
    m_expInfoByExp.clear();
    m_symbolById.clear();
    m_diagnostics.clear();
    m_currentFuncReturnType.reset();
    m_definedFunctionSymbolIds.clear();
    m_globalSymbolIds.clear();
    traverse(compUnit);
}

const std::unordered_map<Ref<Exp>, SemanticExpInfo>&
SemanticTypeAnalyzer::expInfoByExp() const
{
    return m_expInfoByExp;
}

const std::unordered_map<int32_t, SemanticSymbol>&
SemanticTypeAnalyzer::symbolsById() const
{
    return m_symbolById;
}

const std::vector<SemanticDiagnostic>& SemanticTypeAnalyzer::diagnostics() const
{
    return m_diagnostics;
}

const SemanticSymbol* SemanticTypeAnalyzer::findSymbolById(
    int32_t symbolId) const
{
    const auto symbolIt = m_symbolById.find(symbolId);
    if (symbolIt == m_symbolById.end()) {
        return nullptr;
    }
    return &symbolIt->second;
}

void SemanticTypeAnalyzer::visitCompUnit(Ref<CompUnit> compUnit)
{

    for (const auto topLevelItem : compUnit(m_ast).topLevelItems) {
        MATCH(topLevelItem)
        WITH([&](Decl decl) { visitDecl(decl); },
            [&](Ref<FuncDef> funcDef) { declareFuncDef(funcDef); });
    }

    for (const auto topLevelItem : compUnit(m_ast).topLevelItems) {
        MATCH(topLevelItem)
        WITH(
            [&](Ref<FuncDef> funcDef) {
                if (funcDef(m_ast).body != nullptr) {
                    visitFuncDef(funcDef);
                }
            },
            [&](const auto&) { });
    }
}

void SemanticTypeAnalyzer::declareFuncDef(Ref<FuncDef> funcDef_ref)
{
    const auto& funcDef = funcDef_ref(m_ast);
    const auto* resolvedFunction = resolvedSymbol(funcDef.identifier);
    if (resolvedFunction == nullptr
        || resolvedFunction->kind != SemanticSymbolKind::function) {
        return;
    }

    std::vector<SemanticType> paramTypes;
    paramTypes.reserve(funcDef.funcFParams.size());
    for (const auto& funcFParam : funcDef.funcFParams) {
        auto paramType = analyzeObjectType(
            funcFParam.shape, funcFParam.sourcePos.m_offset);
        if (funcFParam.m_isArray) {
            paramType = SemanticType::makeUnsizedArray(paramType);
        }
        paramTypes.push_back(paramType);
    }

    const auto returnType = lowerFuncType(funcDef.m_funcType);
    const auto* existingSymbol = findSymbolById(resolvedFunction->m_id);
    if (existingSymbol == nullptr) {
        m_symbolById.emplace(resolvedFunction->m_id,
            makeFunctionSymbol(funcDef.identifier, resolvedFunction->m_id,
                returnType, paramTypes));
    } else if (!functionSignaturesMatch(
                   *existingSymbol, returnType, paramTypes)) {
        const auto& identifier = funcDef.identifier(m_ast);
        recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
            identifier.sourcePos.m_offset,
            "conflicting declaration of '" + identifier.name + "'");
    }

    if (funcDef.body != nullptr
        && m_definedFunctionSymbolIds.contains(resolvedFunction->m_id)) {
        const auto& identifier = funcDef.identifier(m_ast);
        recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
            identifier.sourcePos.m_offset,
            "redefinition of '" + identifier.name + "'");
    }
    if (funcDef.body != nullptr) {
        m_definedFunctionSymbolIds.insert(resolvedFunction->m_id);
    }
}

void SemanticTypeAnalyzer::visitFuncDef(Ref<FuncDef> funcDef_ref)
{
    const auto& funcDef = funcDef_ref(m_ast);
    if (funcDef.body == nullptr) {
        return;
    }

    const auto* functionSymbol = resolvedSymbol(funcDef.identifier);
    if (functionSymbol == nullptr) {
        throw std::runtime_error(
            "function definition is missing a declared function symbol");
    }

    const auto* declaredFunction = findSymbolById(functionSymbol->m_id);
    const auto previousReturnType = m_currentFuncReturnType;
    if (declaredFunction != nullptr
        && declaredFunction->kind == SemanticSymbolKind::function) {
        m_currentFuncReturnType
            = declaredFunction->m_functionSignature.m_returnType;
        for (size_t i = 0; i != funcDef.funcFParams.size(); ++i) {
            const auto* resolvedParam
                = resolvedSymbol(funcDef.funcFParams[i].identifier);
            if (resolvedParam == nullptr) {
                continue;
            }
            m_symbolById[resolvedParam->m_id] = makeObjectSymbol(
                funcDef.funcFParams[i].identifier, false, false, 0,
                declaredFunction->m_functionSignature.m_paramTypes[i]);
        }
    } else {
        m_currentFuncReturnType = lowerFuncType(funcDef.m_funcType);
    }

    for (const auto blockItem : funcDef.body(m_ast).items) {
        visitBlockItem(blockItem);
    }
    m_currentFuncReturnType = previousReturnType;
}

void SemanticTypeAnalyzer::visitConstDecl(Ref<ConstDecl> constDecl)
{
    for (const auto constDef : constDecl(m_ast).constDef) {
        const auto& parsedConstDef = constDef(m_ast);
        const auto objectType = analyzeObjectType(
            parsedConstDef.shape, parsedConstDef.sourcePos.m_offset);

        size_t nextIndex = 0;
        bool hasRemainingWarning = false;
        auto analyzedInit
            = analyzeConstInitVal(parsedConstDef.constInitVal.ref(), objectType,
                true, nextIndex, hasRemainingWarning);

        if (!parsedConstDef.shape.empty()) {
            analyzedInit.m_isConstant = false;
            analyzedInit.m_constantValue = 0;
        } else {
            if (analyzedInit.m_type.isVoid() || analyzedInit.m_type.isArray()) {
                recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                    parsedConstDef.sourcePos.m_offset,
                    "const initializer must produce an integer value");
            }
            if (!analyzedInit.m_isConstant) {
                recordDiagnostic(
                    SemanticDiagnosticKind::nonConstantConstInitializer,
                    parsedConstDef.sourcePos.m_offset,
                    "const initializer must be a constant expression");
            }
        }

        const auto* resolvedConst = resolvedSymbol(parsedConstDef.identifier);
        if (resolvedConst == nullptr) {
            continue;
        }
        m_symbolById[resolvedConst->m_id]
            = makeObjectSymbol(parsedConstDef.identifier, true,
                parsedConstDef.shape.empty() && analyzedInit.m_isConstant,
                analyzedInit.m_constantValue, objectType);
        if (m_currentFuncReturnType == std::nullopt) {
            m_globalSymbolIds.insert(resolvedConst->m_id);
        }
    }
}

void SemanticTypeAnalyzer::visitVarDecl(Ref<VarDecl> varDecl)
{
    for (const auto varDef : varDecl(m_ast).varDef) {
        const auto& parsedVarDef = varDef(m_ast);
        const auto objectType = analyzeObjectType(
            parsedVarDef.shape, parsedVarDef.sourcePos.m_offset);
        if (parsedVarDef.initVal != nullptr) {
            size_t nextIndex = 0;
            bool hasRemainingWarning = false;
            (void)analyzeInitVal(parsedVarDef.initVal.ref(), objectType,
                m_currentFuncReturnType == std::nullopt, true, nextIndex,
                hasRemainingWarning);
        }

        const auto* resolvedVar = resolvedSymbol(parsedVarDef.identifier);
        if (resolvedVar == nullptr) {
            continue;
        }
        m_symbolById[resolvedVar->m_id] = makeObjectSymbol(
            parsedVarDef.identifier, false, false, 0, objectType);
        if (m_currentFuncReturnType == std::nullopt) {
            m_globalSymbolIds.insert(resolvedVar->m_id);
        }
    }
}

void SemanticTypeAnalyzer::visitIfStmt(Ref<IfStmt> ifStmt)
{
    (void)analyzeCondExp(ifStmt(m_ast).condition);
    visitStmt(ifStmt(m_ast).thenBody);
    visitStmt(ifStmt(m_ast).elseBody);
}

void SemanticTypeAnalyzer::visitWhileStmt(Ref<WhileStmt> whileStmt)
{
    (void)analyzeCondExp(whileStmt(m_ast).condition);
    visitStmt(whileStmt(m_ast).body);
}

void SemanticTypeAnalyzer::visitAssignStmt(Ref<AssignStmt> assignStmt)
{
    const auto& lValExp = assignStmt(m_ast).lval(m_ast);
    MATCH(lValExp.kind)
    WITH(
        [&](const Exp::LVal& lVal) {
            const auto* boundSymbol = resolvedSymbol(lVal.identifier);
            const auto* symbol = boundSymbol != nullptr
                ? findSymbolById(boundSymbol->m_id)
                : nullptr;
            const auto* effectiveSymbol
                = symbol != nullptr ? symbol : boundSymbol;
            if (effectiveSymbol != nullptr && effectiveSymbol->m_isConst) {
                const auto& identifier = lVal.identifier(m_ast);
                recordDiagnostic(SemanticDiagnosticKind::assignToConst,
                    identifier.sourcePos.m_offset,
                    "cannot assign to const '" + effectiveSymbol->name + "'");
            }

            auto currentType = effectiveSymbol != nullptr
                ? effectiveSymbol->m_type
                : SemanticType::makeInteger();
            for (const auto index : lVal.indices) {
                const auto analyzedIndex = analyzeExp(index);
                if (!isScalarTypeImpl(analyzedIndex.m_type)) {
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        index(m_ast).sourcePos.m_offset,
                        "array subscript must produce an integer value");
                }
                if (!currentType.isArray()
                    || currentType.m_elementType == nullptr) {
                    const auto& identifier = lVal.identifier(m_ast);
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        identifier.sourcePos.m_offset,
                        "subscripted assignment target is not an array");
                    break;
                }
                currentType = *currentType.m_elementType;
            }
            if (currentType.isArray()) {
                const auto& identifier = lVal.identifier(m_ast);
                recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                    identifier.sourcePos.m_offset,
                    "assignment target must designate an integer object");
            }
        },
        [&](const auto&) {
            throw std::runtime_error(
                "assignment lhs is not an lvalue expression");
        });

    const auto analyzedExp = analyzeExp(assignStmt(m_ast).exp);
    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            assignStmt(m_ast).sourcePos.m_offset,
            "assignment rhs must produce an integer value");
    }
}

void SemanticTypeAnalyzer::visitExpStmt(Ref<ExpStmt> expStmt)
{
    if (expStmt(m_ast).exp != nullptr) {
        (void)analyzeExp(expStmt(m_ast).exp.ref());
    }
}

void SemanticTypeAnalyzer::visitReturnStmt(Ref<ReturnStmt> returnStmt)
{
    const auto& expr = returnStmt(m_ast).exp;
    const int32_t offset = returnStmt(m_ast).sourcePos.m_offset;
    if (!m_currentFuncReturnType.has_value()) {
        if (expr != nullptr) {
            (void)analyzeExp(expr.ref());
        }
        return;
    }
    if (expr == nullptr) {
        if (m_currentFuncReturnType->kind != SemanticTypeKind::voidType) {
            recordDiagnostic(SemanticDiagnosticKind::returnTypeMismatch, offset,
                "non-void function must return an integer value");
        }
        return;
    }
    const auto analyzedExp = analyzeExp(expr.ref());
    if (m_currentFuncReturnType->kind == SemanticTypeKind::voidType) {
        recordDiagnostic(SemanticDiagnosticKind::returnTypeMismatch, offset,
            "void function must use 'return;' without a value");
        return;
    }

    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array) {
        recordDiagnostic(SemanticDiagnosticKind::returnTypeMismatch, offset,
            "return expression must produce an integer value");
    }
}

void SemanticTypeAnalyzer::visitExp(Ref<Exp> exp) { (void)analyzeExp(exp); }

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::analyzeBinaryExp(
    const Exp& exp, const Exp::Binary& binary)
{
    auto lhs = analyzeExp(binary.lhs);
    auto rhs = analyzeExp(binary.rhs);
    AnalyzedExp binaryExp {
        .m_type = SemanticType::makeInteger(),
        .m_valueKind = ExpType::integer,
        .m_isConstant = false,
        .m_constantValue = 0,
    };

    if (!isScalarTypeImpl(lhs.m_type) || !isScalarTypeImpl(rhs.m_type)) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            exp.sourcePos.m_offset,
            "binary operator requires integer operands");
        return binaryExp;
    }

    switch (binary.op) {
    case BinaryOpKeyword::orOr:
        lhs = normalizeToBoolean(std::move(lhs));
        rhs = normalizeToBoolean(std::move(rhs));
        binaryExp.m_type = SemanticType::makeBoolean();
        binaryExp.m_valueKind = ExpType::boolean;
        if (lhs.m_isConstant && rhs.m_isConstant) {
            binaryExp.m_isConstant = true;
            binaryExp.m_constantValue = applyLOrOp(
                binary.op, lhs.m_constantValue, rhs.m_constantValue);
        }
        break;
    case BinaryOpKeyword::andAnd:
        lhs = normalizeToBoolean(std::move(lhs));
        rhs = normalizeToBoolean(std::move(rhs));
        binaryExp.m_type = SemanticType::makeBoolean();
        binaryExp.m_valueKind = ExpType::boolean;
        if (lhs.m_isConstant && rhs.m_isConstant) {
            binaryExp.m_isConstant = true;
            binaryExp.m_constantValue = applyLAndOp(
                binary.op, lhs.m_constantValue, rhs.m_constantValue);
        }
        break;
    case BinaryOpKeyword::equal:
    case BinaryOpKeyword::notEqual:
        lhs = normalizeToArithmetic(std::move(lhs));
        rhs = normalizeToArithmetic(std::move(rhs));
        binaryExp.m_type = SemanticType::makeBoolean();
        binaryExp.m_valueKind = ExpType::boolean;
        if (lhs.m_isConstant && rhs.m_isConstant) {
            binaryExp.m_isConstant = true;
            binaryExp.m_constantValue = applyEqOp(
                binary.op, lhs.m_constantValue, rhs.m_constantValue);
        }
        break;
    case BinaryOpKeyword::less:
    case BinaryOpKeyword::greater:
    case BinaryOpKeyword::lessEqual:
    case BinaryOpKeyword::greaterEqual:
        lhs = normalizeToArithmetic(std::move(lhs));
        rhs = normalizeToArithmetic(std::move(rhs));
        binaryExp.m_type = SemanticType::makeBoolean();
        binaryExp.m_valueKind = ExpType::boolean;
        if (lhs.m_isConstant && rhs.m_isConstant) {
            binaryExp.m_isConstant = true;
            binaryExp.m_constantValue = applyRelOp(
                binary.op, lhs.m_constantValue, rhs.m_constantValue);
        }
        break;
    case BinaryOpKeyword::star:
    case BinaryOpKeyword::slash:
    case BinaryOpKeyword::percent:
    case BinaryOpKeyword::plus:
    case BinaryOpKeyword::minus:
        lhs = normalizeToArithmetic(std::move(lhs));
        rhs = normalizeToArithmetic(std::move(rhs));
        if (lhs.m_isConstant && rhs.m_isConstant) {
            const auto folded = applyArithmeticOp(
                binary.op, lhs.m_constantValue, rhs.m_constantValue);
            if (folded.has_value()) {
                binaryExp.m_isConstant = true;
                binaryExp.m_constantValue = *folded;
            }
        }
        break;
    }
    return binaryExp;
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::analyzeUnaryExp(
    const Exp& exp, const Exp::Unary& unary)
{
    auto operand = analyzeExp(unary.lhs);
    if (!isScalarTypeImpl(operand.m_type)) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            exp.sourcePos.m_offset,
            "unary operator requires an integer value operand");
        return AnalyzedExp {
            .m_type = unary.op == UnaryOpKeyword::bang
                ? SemanticType::makeBoolean()
                : SemanticType::makeInteger(),
            .m_valueKind = unary.op == UnaryOpKeyword::bang ? ExpType::boolean
                                                            : ExpType::integer,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    if (unary.op == UnaryOpKeyword::bang) {
        operand = normalizeToBoolean(std::move(operand));
    } else {
        operand = normalizeToArithmetic(std::move(operand));
    }
    if (operand.m_isConstant) {
        const auto folded = applyUnaryOp(unary.op, operand.m_constantValue);
        if (folded.has_value()) {
            return AnalyzedExp {
                .m_type = unary.op == UnaryOpKeyword::bang
                    ? SemanticType::makeBoolean()
                    : SemanticType::makeInteger(),
                .m_valueKind = unary.op == UnaryOpKeyword::bang
                    ? ExpType::boolean
                    : ExpType::integer,
                .m_isConstant = true,
                .m_constantValue = *folded,
            };
        }
    }
    return AnalyzedExp {
        .m_type = unary.op == UnaryOpKeyword::bang
            ? SemanticType::makeBoolean()
            : SemanticType::makeInteger(),
        .m_valueKind = unary.op == UnaryOpKeyword::bang ? ExpType::boolean
                                                        : ExpType::integer,
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::analyzeCallExp(
    const Exp& exp, const Exp::Call& call)
{
    const auto* boundSymbol = resolvedSymbol(call.funcName);
    const auto hasDeclaration = boundSymbol != nullptr
        && m_symbolResolver.hasDeclaration(boundSymbol->m_id);
    const auto* semanticSymbol
        = boundSymbol != nullptr ? findSymbolById(boundSymbol->m_id) : nullptr;
    const auto* calleeSymbol
        = semanticSymbol != nullptr ? semanticSymbol : boundSymbol;

    if (!hasDeclaration) {
        for (const auto arg : call.params) {
            const auto analyzedArg = analyzeExp(arg);
            if (analyzedArg.m_valueKind == ExpType::voidType) {
                recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                    exp.sourcePos.m_offset,
                    "call arguments must produce integer values");
            }
        }
        return AnalyzedExp {
            .m_type = SemanticType::makeInteger(),
            .m_valueKind = ExpType::integer,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }

    if (calleeSymbol != nullptr
        && calleeSymbol->kind != SemanticSymbolKind::function) {
        recordDiagnostic(SemanticDiagnosticKind::invalidCallTarget,
            exp.sourcePos.m_offset,
            "call target '" + calleeSymbol->name + "' is not a function");
    }
    if (calleeSymbol != nullptr
        && calleeSymbol->kind == SemanticSymbolKind::function
        && calleeSymbol->m_functionSignature.m_paramTypes.size()
            != call.params.size()) {
        recordDiagnostic(SemanticDiagnosticKind::callArityMismatch,
            exp.sourcePos.m_offset,
            "call to '" + calleeSymbol->name
                + "' uses the wrong number of arguments");
    }

    for (size_t i = 0; i < call.params.size(); ++i) {
        const auto analyzedArg = analyzeExp(call.params[i]);
        if (analyzedArg.m_valueKind == ExpType::voidType) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                exp.sourcePos.m_offset,
                "call arguments must produce integer values");
        }
        if (calleeSymbol != nullptr
            && calleeSymbol->kind == SemanticSymbolKind::function
            && i < calleeSymbol->m_functionSignature.m_paramTypes.size()
            && !typesMatchForCall(
                calleeSymbol->m_functionSignature.m_paramTypes[i],
                analyzedArg.m_type)) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                exp.sourcePos.m_offset,
                "call argument type does not match parameter type");
        }
    }

    return AnalyzedExp {
        .m_type = calleeSymbol != nullptr
                && calleeSymbol->kind == SemanticSymbolKind::function
            ? calleeSymbol->m_functionSignature.m_returnType
            : SemanticType::makeInteger(),
        .m_valueKind = calleeSymbol != nullptr
                && calleeSymbol->kind == SemanticSymbolKind::function
            ? calleeSymbol->m_functionSignature.m_returnType.valueKind()
            : ExpType::integer,
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::analyzeLValExp(
    const Exp& exp, const Exp::LVal& lVal)
{
    const auto* boundSymbol = resolvedSymbol(lVal.identifier);
    const auto* semanticSymbol
        = boundSymbol != nullptr ? findSymbolById(boundSymbol->m_id) : nullptr;
    const auto* symbol
        = semanticSymbol != nullptr ? semanticSymbol : boundSymbol;

    if (symbol != nullptr && symbol->kind == SemanticSymbolKind::function) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            exp.sourcePos.m_offset,
            "function '" + symbol->name + "' must be called before use");
        return AnalyzedExp {
            .m_type = SemanticType::makeInteger(),
            .m_valueKind = ExpType::integer,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }

    auto currentType
        = symbol != nullptr ? symbol->m_type : SemanticType::makeInteger();
    for (const auto index : lVal.indices) {
        const auto analyzedIndex = analyzeExp(index);
        if (!isScalarTypeImpl(analyzedIndex.m_type)) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                index(m_ast).sourcePos.m_offset,
                "array subscript must produce an integer value");
        }
        if (!currentType.isArray() || currentType.m_elementType == nullptr) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                exp.sourcePos.m_offset,
                "subscripted expression is not an array");
            currentType = SemanticType::makeInteger();
            break;
        }
        currentType = *currentType.m_elementType;
    }

    if (symbol != nullptr && lVal.indices.empty() && symbol->m_isConst
        && symbol->m_hasConstantValue) {
        return AnalyzedExp {
            .m_type = currentType,
            .m_valueKind = currentType.valueKind(),
            .m_isConstant = true,
            .m_constantValue = symbol->m_constantValue,
        };
    }

    return AnalyzedExp {
        .m_type = currentType,
        .m_valueKind = currentType.valueKind(),
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::analyzeExp(
    Ref<Exp> exp_ref)
{
    const auto& exp = exp_ref(m_ast);
    auto analyzedExp = MATCH(exp.kind) WITH(
        [&](const Exp::Binary& binary) {
            return analyzeBinaryExp(exp, binary);
        },
        [&](const Exp::Unary& unary) { return analyzeUnaryExp(exp, unary); },
        [&](const Exp::Call& call) { return analyzeCallExp(exp, call); },
        [&](const Exp::LVal& lVal) { return analyzeLValExp(exp, lVal); },
        [&](const Exp::Number& number) {
            return AnalyzedExp {
                .m_type = SemanticType::makeInteger(),
                .m_valueKind = ExpType::integer,
                .m_isConstant = true,
                .m_constantValue = number.value,
            };
        });

    if (analyzedExp.m_valueKind != ExpType::voidType
        && analyzedExp.m_valueKind != ExpType::array) {
        analyzedExp = normalizeToArithmetic(std::move(analyzedExp));
    }
    recordExpFacts(exp_ref, analyzedExp);
    return analyzedExp;
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::analyzeCondExp(
    Ref<Exp> exp)
{
    auto analyzedExp = analyzeExp(exp);
    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            exp(m_ast).sourcePos.m_offset,
            "condition expression must produce an integer value");
        analyzedExp.m_type = SemanticType::makeBoolean();
        analyzedExp.m_valueKind = ExpType::boolean;
        analyzedExp.m_isConstant = false;
        analyzedExp.m_constantValue = 0;
    }
    analyzedExp = normalizeToBoolean(std::move(analyzedExp));
    recordExpFacts(exp, analyzedExp);
    return analyzedExp;
}

SemanticType SemanticTypeAnalyzer::analyzeObjectType(
    const std::vector<Ref<Exp>>& dimensions, int32_t offset,
    bool allowUnsizedFirstDimension)
{
    auto objectType = SemanticType::makeInteger();
    for (auto dimIt = dimensions.rbegin(); dimIt != dimensions.rend();
        ++dimIt) {
        const auto analyzedDim = analyzeExp(*dimIt);
        if (!isScalarTypeImpl(analyzedDim.m_type)
            || !analyzedDim.m_isConstant) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch, offset,
                "array dimension must be a constant integer expression");
            objectType = SemanticType::makeArray(objectType, 0);
            continue;
        }
        objectType
            = SemanticType::makeArray(objectType, analyzedDim.m_constantValue);
    }
    if (allowUnsizedFirstDimension) {
        objectType = SemanticType::makeUnsizedArray(objectType);
    }
    return objectType;
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::analyzeConstInitVal(
    Ref<ConstInitVal> constInitVal, const SemanticType& expectedType,
    bool isOutermost, size_t& nextIndex, bool& hasRemainingWarning)
{
    const auto &init = constInitVal(m_ast);
    AnalyzedExp analyzedInit {
        .m_type = expectedType,
        .m_valueKind = expectedType.valueKind(),
        .m_isConstant = false,
        .m_constantValue = 0,
    };

    const auto recordExcessInitializer = [&](int32_t offset) {
        if (hasRemainingWarning) {
            return;
        }
        recordDiagnostic(SemanticDiagnosticKind::excessInitializerElements,
            offset, "excess initializer elements",
            SemanticDiagnosticSeverity::warning);
        hasRemainingWarning = true;
    };

    MATCH(init.kind)
    WITH(
        [&](Ref<Exp> expr) {
            if (!expectedType.isArray()) {
                analyzedInit = analyzeExp(expr);
                ++nextIndex;
                if (analyzedInit.m_type.isVoid()
                    || analyzedInit.m_type.isArray()) {
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        init.sourcePos.m_offset,
                        "const initializer must produce an integer value");
                }
                if (!analyzedInit.m_isConstant) {
                    recordDiagnostic(
                        SemanticDiagnosticKind::nonConstantConstInitializer,
                        init.sourcePos.m_offset,
                        "const initializer must be a constant expression");
                }
            } else {
                const std::vector<Ref<ConstInitVal>> singleton {
                    constInitVal
                };
                size_t nextValueIndex = 0;
                analyzedInit = analyzeConstInitSequence(singleton,
                    nextValueIndex, expectedType, hasRemainingWarning);
                nextIndex += nextValueIndex;
            }
        },
        [&](const ConstInitVal::List& initList) {
            if (!expectedType.isArray()) {
                if (!initList.empty()) {
                    size_t consumed = 0;
                    analyzedInit = analyzeConstInitVal(initList.front(),
                        expectedType, false, consumed, hasRemainingWarning);
                    nextIndex += consumed;
                }
                if (initList.size() > 1) {
                    recordExcessInitializer(init.sourcePos.m_offset);
                }
            } else {
                size_t nextValueIndex = 0;
                analyzedInit = analyzeConstInitSequence(initList,
                    nextValueIndex, expectedType, hasRemainingWarning);
                nextIndex += nextValueIndex;
            }
        });

    if (isOutermost && expectedType.isArray()) {
        analyzedInit.m_isConstant = false;
        analyzedInit.m_constantValue = 0;
    }
    return analyzedInit;
}

SemanticTypeAnalyzer::AnalyzedExp
SemanticTypeAnalyzer::analyzeConstInitSequence(
    const std::vector<Ref<ConstInitVal>>& values, size_t& nextValueIndex,
    const SemanticType& expectedType, bool& hasRemainingWarning)
{
    if (!expectedType.isArray()) {
        if (nextValueIndex >= values.size()) {
            return AnalyzedExp {
                .m_type = expectedType,
                .m_valueKind = expectedType.valueKind(),
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }

        size_t consumed = 0;
        auto analyzedInit = analyzeConstInitVal(values[nextValueIndex],
            expectedType, false, consumed, hasRemainingWarning);
        ++nextValueIndex;
        return analyzedInit;
    }

    if (expectedType.m_elementType == nullptr) {
        throw std::runtime_error("array type is missing element type");
    }

    for (int32_t i = 0;
        i < expectedType.m_arrayLength && nextValueIndex < values.size(); ++i) {
        MATCH(values[nextValueIndex](m_ast).kind)
        WITH(
            [&](Ref<Exp>) {
                (void)analyzeConstInitSequence(values, nextValueIndex,
                    *expectedType.m_elementType, hasRemainingWarning);
            },
            [&](const ConstInitVal::List&) {
                size_t consumed = 0;
                (void)analyzeConstInitVal(values[nextValueIndex],
                    *expectedType.m_elementType, false, consumed,
                    hasRemainingWarning);
                ++nextValueIndex;
            });
    }

    return AnalyzedExp {
        .m_type = expectedType,
        .m_valueKind = expectedType.valueKind(),
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::analyzeInitVal(
    Ref<InitVal> initVal, const SemanticType& expectedType, bool isGlobal,
    bool isOutermost, size_t& nextIndex, bool& hasRemainingWarning)
{
    const auto &init = initVal(m_ast);
    AnalyzedExp analyzedInit {
        .m_type = expectedType,
        .m_valueKind = expectedType.valueKind(),
        .m_isConstant = false,
        .m_constantValue = 0,
    };

    const auto recordExcessInitializer = [&](int32_t offset) {
        if (hasRemainingWarning) {
            return;
        }
        recordDiagnostic(SemanticDiagnosticKind::excessInitializerElements,
            offset, "excess initializer elements",
            SemanticDiagnosticSeverity::warning);
        hasRemainingWarning = true;
    };

    MATCH(init.kind)
    WITH(
        [&](Ref<Exp> initAlt) {
            if (!expectedType.isArray()) {
                analyzedInit = analyzeExp(initAlt);
                ++nextIndex;
                if (analyzedInit.m_type.isVoid()
                    || analyzedInit.m_type.isArray()) {
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        init.sourcePos.m_offset,
                        "variable initializer must produce an integer value");
                }
                if (isGlobal && !analyzedInit.m_isConstant) {
                    recordDiagnostic(
                        SemanticDiagnosticKind::nonConstantGlobalInitializer,
                        init.sourcePos.m_offset,
                        "global initializer must be a constant expression");
                }
            } else {
                const std::vector<Ref<InitVal>> singleton { initVal };
                size_t nextValueIndex = 0;
                analyzedInit = analyzeInitSequence(singleton, nextValueIndex,
                    expectedType, isGlobal, hasRemainingWarning);
                nextIndex += nextValueIndex;
            }
        },
        [&](const InitVal::List& initAlt) {
            if (!expectedType.isArray()) {
                if (!initAlt.empty()) {
                    size_t consumed = 0;
                    analyzedInit = analyzeInitVal(initAlt.front(), expectedType,
                        isGlobal, false, consumed, hasRemainingWarning);
                    nextIndex += consumed;
                }
                if (initAlt.size() > 1) {
                    recordExcessInitializer(init.sourcePos.m_offset);
                }
            } else {
                size_t nextValueIndex = 0;
                analyzedInit = analyzeInitSequence(initAlt, nextValueIndex,
                    expectedType, isGlobal, hasRemainingWarning);
                nextIndex += nextValueIndex;
            }
        });

    if (isOutermost && expectedType.isArray()) {
        analyzedInit.m_isConstant = false;
        analyzedInit.m_constantValue = 0;
    }
    return analyzedInit;
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::analyzeInitSequence(
    const std::vector<Ref<InitVal>>& values, size_t& nextValueIndex,
    const SemanticType& expectedType, bool isGlobal, bool& hasRemainingWarning)
{
    if (!expectedType.isArray()) {
        if (nextValueIndex >= values.size()) {
            return AnalyzedExp {
                .m_type = expectedType,
                .m_valueKind = expectedType.valueKind(),
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }

        size_t consumed = 0;
        auto analyzedInit = analyzeInitVal(values[nextValueIndex], expectedType,
            isGlobal, false, consumed, hasRemainingWarning);
        ++nextValueIndex;
        return analyzedInit;
    }

    if (expectedType.m_elementType == nullptr) {
        throw std::runtime_error("array type is missing element type");
    }

    for (int32_t i = 0;
        i < expectedType.m_arrayLength && nextValueIndex < values.size(); ++i) {
        MATCH(values[nextValueIndex](m_ast).kind)
        WITH(
            [&](Ref<Exp>) {
                (void)analyzeInitSequence(values, nextValueIndex,
                    *expectedType.m_elementType, isGlobal, hasRemainingWarning);
            },
            [&](const InitVal::List&) {
                size_t consumed = 0;
                (void)analyzeInitVal(values[nextValueIndex],
                    *expectedType.m_elementType, isGlobal, false, consumed,
                    hasRemainingWarning);
                ++nextValueIndex;
            });
    }

    return AnalyzedExp {
        .m_type = expectedType,
        .m_valueKind = expectedType.valueKind(),
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}

bool SemanticTypeAnalyzer::typesMatchForCall(
    const SemanticType& paramType, const SemanticType& argType) const
{
    return typesMatchForCallImpl(paramType, argType);
}

const SemanticSymbol* SemanticTypeAnalyzer::resolvedSymbol(
    Ref<Identifier> identifier) const
{
    return m_symbolResolver.findSymbol(identifier);
}

SemanticSymbol SemanticTypeAnalyzer::makeObjectSymbol(
    Ref<Identifier> ident, bool isConst, bool hasConstantValue,
    int32_t constantValue, const SemanticType& type) const
{
    const auto& identifier = ident(m_ast);
    const auto* resolved = resolvedSymbol(ident);
    if (resolved == nullptr) {
        throw std::runtime_error(
            "object declaration is missing symbol resolution");
    }
    const std::string symbolName = type.isArray()
        ? std::string(isConst ? "c_" : "v_") + identifier.name
        : identifier.name;
    return SemanticSymbol {
        .m_id = resolved->m_id,
        .name = symbolName,
        .kind = SemanticSymbolKind::object,
        .m_isConst = isConst,
        .m_hasConstantValue = hasConstantValue,
        .m_constantValue = constantValue,
        .m_type = type,
        .m_functionSignature = { },
    };
}

SemanticSymbol SemanticTypeAnalyzer::makeFunctionSymbol(
    Ref<Identifier> ident, int32_t symbolId,
    const SemanticType& returnType,
    const std::vector<SemanticType>& paramTypes) const
{
    const auto& identifier = ident(m_ast);
    return SemanticSymbol {
        .m_id = symbolId,
        .name = identifier.name,
        .kind = SemanticSymbolKind::function,
        .m_isConst = false,
        .m_hasConstantValue = false,
        .m_constantValue = 0,
        .m_type = returnType,
        .m_functionSignature = SemanticFunctionSignature {
            .m_returnType = returnType,
            .m_paramTypes = paramTypes,
        },
    };
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::normalizeToArithmetic(
    AnalyzedExp analyzedExp) const
{
    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array
        || analyzedExp.m_valueKind == ExpType::integer) {
        return analyzedExp;
    }

    analyzedExp.m_type = SemanticType::makeInteger();
    analyzedExp.m_valueKind = ExpType::integer;
    if (analyzedExp.m_isConstant) {
        analyzedExp.m_constantValue = analyzedExp.m_constantValue != 0 ? 1 : 0;
    }
    return analyzedExp;
}

SemanticTypeAnalyzer::AnalyzedExp SemanticTypeAnalyzer::normalizeToBoolean(
    AnalyzedExp analyzedExp) const
{
    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array
        || analyzedExp.m_valueKind == ExpType::boolean) {
        return analyzedExp;
    }

    analyzedExp.m_type = SemanticType::makeBoolean();
    analyzedExp.m_valueKind = ExpType::boolean;
    if (analyzedExp.m_isConstant) {
        analyzedExp.m_constantValue = analyzedExp.m_constantValue != 0 ? 1 : 0;
    }
    return analyzedExp;
}

void SemanticTypeAnalyzer::recordExpFacts(
    Ref<Exp> exp, const AnalyzedExp& analyzedExp)
{
    m_expInfoByExp[exp] = SemanticExpInfo {
        .m_type = analyzedExp.m_valueKind,
        .m_semanticType = analyzedExp.m_type,
        .m_hasConstantValue = analyzedExp.m_isConstant,
        .m_constantValue = analyzedExp.m_constantValue,
    };
}

void SemanticTypeAnalyzer::recordDiagnostic(SemanticDiagnosticKind kind,
    int32_t offset, std::string message, SemanticDiagnosticSeverity severity)
{
    m_diagnostics.push_back(SemanticDiagnostic {
        .kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
        .m_severity = severity,
    });
}

bool SemanticTypeAnalyzer::isGlobalSymbol(int32_t symbolId) const
{
    return m_currentFuncReturnType == std::nullopt
        || m_globalSymbolIds.contains(symbolId);
}

SemanticLoopBinder::SemanticLoopBinder(const AST& ast)
    : AstVisitor(ast)
{
}

void SemanticLoopBinder::analyze(Ref<CompUnit> compUnit)
{
    m_loopByBreakStmt.clear();
    m_loopByContinueStmt.clear();
    m_loopStack.clear();
    m_diagnostics.clear();
    traverse(compUnit);
}

const std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>>&
SemanticLoopBinder::loopByBreakStmt() const
{
    return m_loopByBreakStmt;
}

const std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>>&
SemanticLoopBinder::loopByContinueStmt() const
{
    return m_loopByContinueStmt;
}

const std::vector<SemanticDiagnostic>& SemanticLoopBinder::diagnostics() const
{
    return m_diagnostics;
}

void SemanticLoopBinder::visitWhileStmt(Ref<WhileStmt> whileStmt)
{
    m_loopStack.push_back(whileStmt);
    AstVisitor::visitWhileStmt(whileStmt);
    m_loopStack.pop_back();
}

void SemanticLoopBinder::visitBreakStmt(Ref<BreakStmt> breakStmt)
{
    const auto loop = currentLoop();
    if (!loop.has_value()) {
        recordDiagnostic(SemanticDiagnosticKind::breakOutsideWhile,
            breakStmt(m_ast).sourcePos.m_offset,
            "break statement is not inside a while loop");
        return;
    }
    m_loopByBreakStmt.insert_or_assign(breakStmt, *loop);
}

void SemanticLoopBinder::visitContinueStmt(Ref<ContinueStmt> continueStmt)
{
        const auto loop = currentLoop();
        if (!loop.has_value()) {
            recordDiagnostic(SemanticDiagnosticKind::continueOutsideWhile,
                continueStmt(m_ast).sourcePos.m_offset,
                "continue statement is not inside a while loop");
            return;
        }
        m_loopByContinueStmt.insert_or_assign(continueStmt, *loop);
}

std::optional<Ref<WhileStmt>> SemanticLoopBinder::currentLoop() const
{
    if (m_loopStack.empty()) {
        return std::nullopt;
    }
    return m_loopStack.back();
}

void SemanticLoopBinder::recordDiagnostic(SemanticDiagnosticKind kind,
    int32_t offset, std::string message, SemanticDiagnosticSeverity severity)
{
    m_diagnostics.push_back(SemanticDiagnostic {
        .kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
        .m_severity = severity,
    });
}

SemanticOutput SemanticAnalyzer::analyze(const AST &ast, Ref<CompUnit> compUnit)
{
    SemanticInfo info;
    std::vector<SemanticDiagnostic> diagnostics;

    if (compUnit) {
        SemanticSymbolResolver symbolResolver(ast);
        symbolResolver.analyze(compUnit);

        SemanticTypeAnalyzer typeAnalyzer(ast, symbolResolver);
        typeAnalyzer.analyze(compUnit);

        SemanticLoopBinder loopBinder(ast);
        loopBinder.analyze(compUnit);

        for (const auto& [identifier, resolvedSymbol] :
            symbolResolver.symbolsByIdentifier()) {
            const auto* semanticSymbol
                = typeAnalyzer.findSymbolById(resolvedSymbol.m_id);
            info.m_symbolByIdentifier.emplace(identifier,
                semanticSymbol != nullptr ? *semanticSymbol : resolvedSymbol);
        }
        info.m_expInfoByExp = typeAnalyzer.expInfoByExp();
        info.m_loopByBreakStmt = loopBinder.loopByBreakStmt();
        info.m_loopByContinueStmt = loopBinder.loopByContinueStmt();

        diagnostics.reserve(symbolResolver.diagnostics().size()
            + typeAnalyzer.diagnostics().size()
            + loopBinder.diagnostics().size());
        diagnostics.insert(diagnostics.end(),
            symbolResolver.diagnostics().begin(),
            symbolResolver.diagnostics().end());
        diagnostics.insert(diagnostics.end(),
            typeAnalyzer.diagnostics().begin(),
            typeAnalyzer.diagnostics().end());
        diagnostics.insert(diagnostics.end(), loopBinder.diagnostics().begin(),
            loopBinder.diagnostics().end());
        std::stable_sort(diagnostics.begin(), diagnostics.end(),
            [](const SemanticDiagnostic& lhs, const SemanticDiagnostic& rhs) {
                return lhs.m_offset < rhs.m_offset;
            });
    }

    return SemanticOutput {
        .m_ast = std::move(ast),
        .m_root = compUnit,
        .m_info = std::move(info),
        .m_diagnostics = std::move(diagnostics),
    };
}

} // namespace yesod::frontend
