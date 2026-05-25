#include "frontend/semantic_symbol_impl.h"

namespace yesod::frontend {

SymbolResolutionResult::SymbolResolutionResult(const AST& ast)
    : ast(ast)
{
}
const std::unordered_map<Ref<Identifier>, int32_t>&
SymbolResolutionResult::symbolIdsByIdentifier() const
{
    return m_symbolIdByIdentifier;
}

const std::vector<std::unique_ptr<Diagnostic>>&
SymbolResolutionResult::diagnostics() const
{
    return m_diagnostics;
}

std::optional<int32_t> SymbolResolutionResult::findSymbolId(
    Ref<Identifier> identifier) const
{
    const auto symbolIdIt = m_symbolIdByIdentifier.find(identifier);
    if (symbolIdIt == m_symbolIdByIdentifier.end()) {
        return std::nullopt;
    }
    return symbolIdIt->second;
}

bool SymbolResolutionResult::hasDeclaration(int32_t symbolId) const
{
    return m_declaredSymbolIds.contains(symbolId);
}

namespace detail {

    SemanticSymbolResolverImpl::SemanticSymbolResolverImpl(const AST& ast)
        : AstVisitor(ast)
        , SymbolResolutionResult(ast)
    {
    }

    void SemanticSymbolResolverImpl::analyze(Ref<CompUnit> compUnit)
    {
        m_symbolIdByIdentifier.clear();
        m_scopeStack.clear();
        m_diagnostics.clear();
        m_declaredSymbolIds.clear();
        m_functionSymbolIds.clear();
        m_definedFunctionSymbolIds.clear();
        m_nextSymbolId = 0;
        traverse(compUnit);
    }

    void SemanticSymbolResolverImpl::visitCompUnit(Ref<CompUnit> compUnit)
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

    void SemanticSymbolResolverImpl::declareFuncDef(Ref<FuncDef> funcDef_ref)
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
            if (!defineSymbol(identifier.name, funcDef.identifier)) {
                recordDiagnostic<DoubleDefinitionDiagnostic>(
                    identifier.sourcePos.m_offset,
                    "double definition of '" + identifier.name + "'");
            }
            const int32_t symbolId = makeFreshSymbolId();
            bindSymbolId(funcDef.identifier, symbolId);
            m_declaredSymbolIds.insert(symbolId);
            m_functionSymbolIds.insert(symbolId);
            if (funcDef.body != nullptr) {
                m_definedFunctionSymbolIds.insert(symbolId);
            }
            return;
        }

        const auto existingSymbolId = findSymbolId(*existingIdentifier);
        if (!existingSymbolId.has_value()) {
            throw std::runtime_error("function declaration is missing an "
                                     "existing symbol id binding");
        }

        if (!m_functionSymbolIds.contains(*existingSymbolId)) {
            recordDiagnostic<DoubleDefinitionDiagnostic>(
                identifier.sourcePos.m_offset,
                "double definition of '" + identifier.name + "'");
            bindSymbolId(funcDef.identifier, *existingSymbolId);
            return;
        }

        if (funcDef.body != nullptr
            && m_definedFunctionSymbolIds.contains(*existingSymbolId)) {
            recordDiagnostic<DoubleDefinitionDiagnostic>(
                identifier.sourcePos.m_offset,
                "redefinition of '" + identifier.name + "'");
        }
        if (funcDef.body != nullptr) {
            m_definedFunctionSymbolIds.insert(*existingSymbolId);
        }
        bindSymbolId(funcDef.identifier, *existingSymbolId);
    }

    void SemanticSymbolResolverImpl::visitFuncDef(Ref<FuncDef> funcDef_ref)
    {
        const auto funcDef = funcDef_ref(m_ast);
        if (funcDef.body == nullptr) {
            return;
        }

        pushScope();
        for (const auto& funcFParam : funcDef.funcFParams) {
            const auto& identifier = funcFParam.identifier(m_ast);
            if (!defineSymbol(identifier.name, funcFParam.identifier)) {
                recordDiagnostic<DoubleDefinitionDiagnostic>(
                    identifier.sourcePos.m_offset,
                    "double definition of '" + identifier.name + "'");
            }
            const int32_t symbolId = makeFreshSymbolId();
            bindSymbolId(funcFParam.identifier, symbolId);
            m_declaredSymbolIds.insert(symbolId);
        }

        for (const auto blockItem : funcDef.body(m_ast).items) {
            visitBlockItem(blockItem);
        }
        popScope();
    }

    void SemanticSymbolResolverImpl::visitBlock(Ref<Block> block)
    {
        pushScope();
        for (const auto blockItem : block(m_ast).items) {
            visitBlockItem(blockItem);
        }
        popScope();
    }

    void SemanticSymbolResolverImpl::visitConstDecl(Ref<ConstDecl> constDecl)
    {
        for (const auto constDef : constDecl(m_ast).constDef) {
            visitConstDef(constDef);
            const auto& parsedConstDef = constDef(m_ast);
            const auto& identifier = parsedConstDef.identifier(m_ast);
            if (!defineSymbol(identifier.name, parsedConstDef.identifier)) {
                recordDiagnostic<DoubleDefinitionDiagnostic>(
                    identifier.sourcePos.m_offset,
                    "double definition of '" + identifier.name + "'");
            }
            const int32_t symbolId = makeFreshSymbolId();
            bindSymbolId(parsedConstDef.identifier, symbolId);
            m_declaredSymbolIds.insert(symbolId);
        }
    }

    void SemanticSymbolResolverImpl::visitVarDecl(Ref<VarDecl> varDecl)
    {
        for (const auto varDef : varDecl(m_ast).varDef) {
            visitVarDef(varDef);
            const auto& parsedVarDef = varDef(m_ast);
            const auto& identifier = parsedVarDef.identifier(m_ast);
            if (!defineSymbol(identifier.name, parsedVarDef.identifier)) {
                recordDiagnostic<DoubleDefinitionDiagnostic>(
                    identifier.sourcePos.m_offset,
                    "double definition of '" + identifier.name + "'");
            }
            const int32_t symbolId = makeFreshSymbolId();
            bindSymbolId(parsedVarDef.identifier, symbolId);
            m_declaredSymbolIds.insert(symbolId);
        }
    }

    void SemanticSymbolResolverImpl::visitCallExp(
        const Exp&, const Exp::Call& call)
    {
        (void)resolveIdentifier(call.funcName);
        for (const auto param : call.params) {
            visitExp(param);
        }
    }

    void SemanticSymbolResolverImpl::visitLValExp(
        const Exp&, const Exp::LVal& lVal)
    {
        (void)resolveIdentifier(lVal.identifier);
        for (const auto index : lVal.indices) {
            visitExp(index);
        }
    }

    void SemanticSymbolResolverImpl::pushScope()
    {
        m_scopeStack.emplace_back();
    }

    void SemanticSymbolResolverImpl::popScope()
    {
        if (!m_scopeStack.empty()) {
            m_scopeStack.pop_back();
        }
    }

    bool SemanticSymbolResolverImpl::defineSymbol(
        const std::string& name, Ref<Identifier> identifier)
    {
        if (m_scopeStack.empty()) {
            pushScope();
        }

        auto& currentScope = m_scopeStack.back();
        return currentScope.emplace(name, identifier).second;
    }

    std::optional<Ref<Identifier>> SemanticSymbolResolverImpl::lookupSymbol(
        const std::string& name) const
    {
        for (auto scopeIt = m_scopeStack.rbegin();
            scopeIt != m_scopeStack.rend(); ++scopeIt) {
            const auto foundIt = scopeIt->find(name);
            if (foundIt != scopeIt->end()) {
                return foundIt->second;
            }
        }
        return std::nullopt;
    }

    int32_t SemanticSymbolResolverImpl::resolveIdentifier(Ref<Identifier> ident)
    {
        const auto definitionIdentifier = lookupSymbol(ident(m_ast).name);
        if (definitionIdentifier.has_value()) {
            const auto symbolId = findSymbolId(*definitionIdentifier);
            if (!symbolId.has_value()) {
                throw std::runtime_error(
                    "definition identifier is missing symbol id binding");
            }
            bindSymbolId(ident, *symbolId);
            return *symbolId;
        }

        recordDiagnostic<UseBeforeDefinitionDiagnostic>(
            ident(m_ast).sourcePos.m_offset,
            "use of '" + ident(m_ast).name + "' before definition");
        const int32_t placeholderSymbolId = makeFreshSymbolId();
        bindSymbolId(ident, placeholderSymbolId);
        return placeholderSymbolId;
    }

    void SemanticSymbolResolverImpl::bindSymbolId(
        Ref<Identifier> identifier, int32_t symbolId)
    {
        m_symbolIdByIdentifier[identifier] = symbolId;
    }

    int32_t SemanticSymbolResolverImpl::makeFreshSymbolId()
    {
        return ++m_nextSymbolId;
    }

}

SemanticSymbolResolver::SemanticSymbolResolver(const AST& ast)
    : m_impl(new detail::SemanticSymbolResolverImpl(ast))
{
}

SemanticSymbolResolver::~SemanticSymbolResolver() = default;

void SemanticSymbolResolver::analyze(Ref<CompUnit> compUnit)
{
    m_impl->analyze(compUnit);
}

const SymbolResolutionResult* SemanticSymbolResolver::operator->() const {
    return m_impl.get();
}

}