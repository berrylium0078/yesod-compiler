#ifndef _YESOD_FRONTEND_SEMANTIC_SSA_H_
#define _YESOD_FRONTEND_SEMANTIC_SSA_H_

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontend/ast.h"
#include "frontend/semantic_cfg.h"
#include "frontend/semantic_symbol.h"
#include "frontend/semantic_type.h"

namespace yesod::frontend {

struct SemanticSsaAlias {
    int32_t m_symbolId = 0;
    int32_t m_version = 0;

    [[nodiscard]] bool operator==(const SemanticSsaAlias& other) const
    {
        return m_symbolId == other.m_symbolId && m_version == other.m_version;
    }

    [[nodiscard]] bool operator!=(const SemanticSsaAlias& other) const
    {
        return !(*this == other);
    }
};

struct SemanticSsaBlockParam {
    int32_t m_symbolId = 0;
    SemanticSsaAlias m_alias;
};

struct SemanticSsaBlockInfo {
    Ref<SemanticBasicBlock> m_block;
    std::vector<Ref<SemanticBasicBlock>> m_predecessors;
    std::vector<Ref<SemanticBasicBlock>> m_successors;
    std::optional<Ref<SemanticBasicBlock>> m_immediateDominator;
    std::vector<Ref<SemanticBasicBlock>> m_dominatorTreeChildren;
    std::vector<Ref<SemanticBasicBlock>> m_dominanceFrontier;
    std::unordered_set<int32_t> m_useSet;
    std::unordered_set<int32_t> m_defSet;
    std::unordered_set<int32_t> m_liveIn;
    std::unordered_set<int32_t> m_liveOut;
    std::vector<SemanticSsaBlockParam> m_params;
    std::unordered_map<Ref<SemanticBasicBlock>, std::vector<SemanticSsaAlias>>
        m_outgoingArgsByTarget;
};

struct SemanticFunctionSSA {
    Ref<FuncDef> m_funcDef;
    std::unordered_map<Ref<SemanticBasicBlock>, SemanticSsaBlockInfo>
        m_blockInfoByBlock;
};

class SemanticSSA {
public:
    SemanticSSA(const AST& ast, const SemanticCFG& controlFlow,
        const SymbolResolutionResult& symbolResult,
        const SemanticTypeAnalysisResult& typeResult);
    virtual ~SemanticSSA() = default;

    [[nodiscard]] const SemanticFunctionSSA* findFunction(
        Ref<FuncDef> funcDef) const;
    [[nodiscard]] std::optional<SemanticSsaAlias> findAlias(
        Ref<Identifier> identifier) const;
    [[nodiscard]] const std::unordered_map<Ref<Identifier>, SemanticSsaAlias>&
    aliasByIdentifier() const;

protected:
    const AST& ast;
    const SemanticCFG& m_controlFlow;
    const SymbolResolutionResult& m_symbolResult;
    const SemanticTypeAnalysisResult& m_typeResult;
    std::unordered_map<Ref<FuncDef>, SemanticFunctionSSA>
        m_functionByFuncDef;
    std::unordered_map<Ref<Identifier>, SemanticSsaAlias> m_aliasByIdentifier;
};

namespace detail {
    class SemanticSSAAnalyzerImpl;
}

class SemanticSSAAnalyzer {
    friend class SemanticAnalyzer;

public:
    SemanticSSAAnalyzer(const AST& ast, const SemanticCFG& controlFlow,
        const SymbolResolutionResult& symbolResult,
        const SemanticTypeAnalysisResult& typeResult);
    ~SemanticSSAAnalyzer();

    void analyze(Ref<CompUnit> compUnit);
    [[nodiscard]] const SemanticSSA* operator->() const;

private:
    std::unique_ptr<detail::SemanticSSAAnalyzerImpl> m_impl;
};

} // namespace yesod::frontend

#endif