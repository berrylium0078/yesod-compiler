#include <algorithm>
#include <utility>

#include "frontend/semantic.h"
#include "frontend/semantic_cfg_impl.h"
#include "frontend/semantic_ssa.h"
#include "frontend/semantic_symbol_impl.h"
#include "frontend/semantic_type_impl.h"

namespace yesod::frontend {

const std::unordered_map<Ref<Identifier>, int32_t>&
SemanticInfo::symbolIdByIdentifier() const
{
    return m_symbolResult->symbolIdsByIdentifier();
}
const std::unordered_map<int32_t, SemanticSymbol>&
SemanticInfo::symbolById() const
{
    return m_typeResult->symbolsById();
}
std::optional<int32_t> SemanticInfo::findSymbolId(
    Ref<Identifier> identifier) const
{
    return m_symbolResult->findSymbolId(identifier);
}

const SemanticSymbol* SemanticInfo::findSymbolById(int32_t symbolId) const
{
    const auto symbolIt = m_typeResult->symbolsById().find(symbolId);
    if (symbolIt == m_typeResult->symbolsById().end()) {
        return nullptr;
    }
    return &symbolIt->second;
}

const SemanticSymbol* SemanticInfo::findSymbol(Ref<Identifier> identifier) const
{
    const auto symbolId = findSymbolId(identifier);
    if (!symbolId.has_value()) {
        return nullptr;
    }
    return findSymbolById(*symbolId);
}

std::optional<ExpType> SemanticInfo::findExpValueKind(Ref<Exp> node) const
{
    const auto infoIt = m_typeResult->expInfoByExp().find(node);
    if (infoIt == m_typeResult->expInfoByExp().end()) {
        return std::nullopt;
    }
    return infoIt->second.m_type;
}

std::optional<int32_t> SemanticInfo::findConstantValue(Ref<Exp> node) const
{
    const auto infoIt = m_typeResult->expInfoByExp().find(node);
    if (infoIt == m_typeResult->expInfoByExp().end()
        || !infoIt->second.m_constantValue.has_value()) {
        return std::nullopt;
    }
    return infoIt->second.m_constantValue;
}

std::optional<SemanticType> SemanticInfo::findExpType(Ref<Exp> node) const
{
    const auto infoIt = m_typeResult->expInfoByExp().find(node);
    if (infoIt == m_typeResult->expInfoByExp().end()) {
        return std::nullopt;
    }
    return infoIt->second.m_semanticType;
}

std::optional<Ref<WhileStmt>> SemanticInfo::findLoop(Ref<BreakStmt> node) const
{
    const auto loopIt = m_bindingResult->loopByBreakStmt().find(node);
    if (loopIt == m_bindingResult->loopByBreakStmt().end()) {
        return std::nullopt;
    }
    return loopIt->second;
}

std::optional<Ref<WhileStmt>> SemanticInfo::findLoop(
    Ref<ContinueStmt> node) const
{
    const auto loopIt = m_bindingResult->loopByContinueStmt().find(node);
    if (loopIt == m_bindingResult->loopByContinueStmt().end()) {
        return std::nullopt;
    }
    return loopIt->second;
}

const SemanticFunctionControlFlow* SemanticInfo::findControlFlow(
    Ref<FuncDef> node) const
{
    return m_bindingResult->findControlFlow(node);
}

const SemanticFunctionSSA* SemanticInfo::findSSA(Ref<FuncDef> node) const
{
    return m_ssaResult->findFunction(node);
}

const SemanticControlFlowArena& SemanticInfo::controlFlowArena() const
{
    return m_bindingResult->controlFlowArena();
}

std::optional<SemanticSsaAlias> SemanticInfo::findAlias(
    Ref<Identifier> identifier) const
{
    return m_ssaResult->findAlias(identifier);
}

const std::unordered_map<Ref<Identifier>, SemanticSsaAlias>&
SemanticInfo::aliasByIdentifier() const
{
    return m_ssaResult->aliasByIdentifier();
}

bool SemanticOutput::success() const
{
    if (!m_root) {
        return false;
    }
    for (const auto& diagnostic : m_diagnostics) {
        if (diagnostic->severity == DiagnosticSeverity::error) {
            return false;
        }
    }
    return true;
}

Ref<CompUnit> SemanticOutput::root() { return m_root.ref(); }

SemanticOutput SemanticAnalyzer::analyze(const AST& ast, Ref<CompUnit> compUnit)
{
    SemanticInfo info;
    std::vector<std::unique_ptr<Diagnostic>> diagnostics;

    if (compUnit) {
        info.m_symbolResolver = std::make_unique<SemanticSymbolResolver>(ast);
        info.m_typeAnalyzer = std::make_unique<SemanticTypeAnalyzer>(
            ast, *info.m_symbolResolver);
        info.m_loopBinder = std::make_unique<SemanticCFGBuilder>(ast);

        info.m_symbolResolver->analyze(compUnit);
        info.m_typeAnalyzer->analyze(compUnit);
        info.m_loopBinder->analyze(compUnit);

        // Pre-SSA CFG simplification: replace constant branches with jumps
        info.m_loopBinder->simplify((*info.m_typeAnalyzer)->expInfoByExp());

        info.m_ssaAnalyzer = std::make_unique<SemanticSSAAnalyzer>(ast,
            *info.m_loopBinder->operator->(),
            *info.m_symbolResolver->operator->(),
            *info.m_typeAnalyzer->operator->());
        info.m_ssaAnalyzer->analyze(compUnit);

        auto typeAnalyzer = info.m_typeAnalyzer.get();
        auto symbolResolver = info.m_symbolResolver.get();
        auto loopBinder = info.m_loopBinder.get();
        auto ssaAnalyzer = info.m_ssaAnalyzer.get();
        info.m_symbolResult = symbolResolver->operator->();
        info.m_typeResult = typeAnalyzer->operator->();
        info.m_bindingResult = loopBinder->operator->();
        info.m_ssaResult = ssaAnalyzer->operator->();

        diagnostics.reserve((*symbolResolver)->diagnostics().size()
            + (*typeAnalyzer)->diagnostics().size()
            + (*loopBinder)->diagnostics().size());
        for (const auto& diagnostic : (*symbolResolver)->diagnostics()) {
            diagnostics.emplace_back(diagnostic->clone());
        }
        for (const auto& diagnostic : (*typeAnalyzer)->diagnostics()) {
            diagnostics.push_back(diagnostic->clone());
        }
        for (const auto& diagnostic : (*loopBinder)->diagnostics()) {
            diagnostics.push_back(diagnostic->clone());
        }
        std::stable_sort(diagnostics.begin(), diagnostics.end(),
            [](const std::unique_ptr<Diagnostic>& lhs,
                const std::unique_ptr<Diagnostic>& rhs) {
                return lhs->offset < rhs->offset;
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