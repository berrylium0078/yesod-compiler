#include "frontend/semantic_ssa.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace yesod::frontend {

namespace {

    using Block = Ref<SemanticBasicBlock>;

    [[nodiscard]] bool isTrackedObject(const SemanticSymbol* symbol)
    {
        return symbol != nullptr && symbol->isObject()
            && (symbol->object().m_type.isScalar()
                || symbol->object().m_type.isPoly());
    }

    [[nodiscard]] std::optional<Ref<Identifier>> scalarLValIdentifier(
        const AST& ast, Ref<Exp> exp)
    {
        const auto* lVal = std::get_if<Exp::LVal>(&exp(ast).kind);
        if (lVal == nullptr || !lVal->indices.empty()) {
            return std::nullopt;
        }
        return lVal->identifier;
    }

    [[nodiscard]] Ptr<Exp> scalarInitExp(const AST& ast, Ref<ConstDef> def)
    {
        if (!def(ast).shape.empty() || def(ast).constInitVal == nullptr) {
            return nullptr;
        }
        const auto* exp
            = std::get_if<Ref<Exp>>(&def(ast).constInitVal(ast).kind);
        return exp == nullptr ? Ptr<Exp>(nullptr) : exp->ptr();
    }

    [[nodiscard]] Ptr<Exp> scalarInitExp(const AST& ast, Ref<VarDef> def)
    {
        if (!def(ast).shape.empty() || def(ast).initVal == nullptr) {
            return nullptr;
        }
        const auto* exp = std::get_if<Ref<Exp>>(&def(ast).initVal(ast).kind);
        return exp == nullptr ? Ptr<Exp>(nullptr) : exp->ptr();
    }

    struct ExpressionWalker {
        const AST& m_ast;
        const SymbolResolutionResult& m_symbolResult;
        const std::unordered_set<int32_t>& m_localTrackedSymbols;
        const std::function<void(Ref<Identifier>, int32_t)>& m_onRead;

        void walkExp(Ref<Exp> exp) const
        {
            MATCH(exp(m_ast).kind)
            WITH(
                [&](const Exp::Binary& binaryExp) {
                    walkExp(binaryExp.lhs);
                    walkExp(binaryExp.rhs);
                },
                [&](const Exp::Unary& unaryExp) { walkExp(unaryExp.lhs); },
                [&](const Exp::Cast& castExp) { walkExp(castExp.value); },
                [&](const Exp::Number&) {},
                [&](const Exp::Call& funcCall) {
                    for (auto arg : funcCall.params) {
                        walkExp(arg);
                    }
                },
                [&](const Exp::Slice& slice) {
                    walkExp(slice.base);
                    walkExp(slice.start);
                    walkExp(slice.end);
                },
                [&](const Exp::Subscript& subscript) {
                    walkExp(subscript.base);
                    walkExp(subscript.index);
                },
                [&](const Exp::Ntt& ntt) { walkExp(ntt.value); },
                [&](const Exp::Intt& intt) { walkExp(intt.value); },
                [&](const Exp::PvBinary& binary) {
                    walkExp(binary.lhs);
                    walkExp(binary.rhs);
                },
                [&](const Exp::Combine& combine) {
                    for (const auto& term : combine.terms) {
                        walkExp(term.value);
                        walkExp(term.start);
                        if (term.end != nullptr) {
                            walkExp(term.end.ref());
                        }
                        walkExp(term.shift);
                        walkExp(term.scale);
                    }
                },
                [&](const Exp::GetCoeff& getCoeff) {
                    walkExp(getCoeff.value);
                    walkExp(getCoeff.index);
                },
                [&](const Exp::PolyConstruct& construct) {
                    for (auto element : construct.elements) {
                        walkExp(element);
                    }
                },
                [&](const Exp::IntToMint& conversion) {
                    walkExp(conversion.value);
                },
                [&](const Exp::MintToInt& conversion) {
                    walkExp(conversion.value);
                },
                [&](const Exp::LVal& lVal) {
                    for (auto index : lVal.indices) {
                        walkExp(index);
                    }
                    if (!lVal.indices.empty()) {
                        return;
                    }
                    const auto symbolId
                        = m_symbolResult.findSymbolId(lVal.identifier);
                    if (!symbolId.has_value()
                        || !m_localTrackedSymbols.contains(*symbolId)) {
                        return;
                    }
                    m_onRead(lVal.identifier, *symbolId);
                });
        }
    };

    struct FunctionAnalyzer {
        const AST& m_ast;
        const SemanticCFG& m_controlFlow;
        const SemanticControlFlowArena& m_cfgArena;
        const SymbolResolutionResult& m_symbolResult;
        const SemanticTypeAnalysisResult& m_typeResult;
        SemanticFunctionSSA& m_result;
        std::unordered_map<Ref<Identifier>, SemanticSsaAlias>&
            m_aliasByIdentifier;

        std::vector<Block> m_blocks;
        std::unordered_map<Block, size_t> m_blockOrder;
        std::unordered_set<int32_t> m_localTrackedSymbols;
        std::unordered_map<int32_t, std::vector<Block>> m_defBlocksBySymbol;
        std::unordered_map<Block, std::unordered_set<Block>>
            m_dominatorsByBlock;
        std::unordered_map<Block, size_t> m_dfsInByBlock;
        std::unordered_map<Block, size_t> m_subtreeSizeByBlock;
        std::unordered_map<int32_t, int32_t> m_nextAliasVersionBySymbol;

        void analyze()
        {
            collectBlocks();
            collectLocalScalarSymbols();
            initializeEdges();
            computeDominators();
            computeUseDefSets();
            computeLiveness();
            generateBlockParams();
            assignAliases();
        }

        void collectBlocks()
        {
            const auto* cfg = m_controlFlow.findControlFlow(m_result.m_funcDef);
            if (cfg == nullptr) {
                return;
            }
            m_blocks = cfg->blocks;
            for (size_t index = 0; index < m_blocks.size(); ++index) {
                m_blockOrder[m_blocks[index]] = index;
            }
        }

        [[nodiscard]] const SemanticSymbol* findSymbol(
            Ref<Identifier> identifier) const
        {
            const auto symbolId = m_symbolResult.findSymbolId(identifier);
            return symbolId.has_value() ? m_typeResult.findSymbolById(*symbolId)
                                        : nullptr;
        }

        void collectLocalScalarSymbols()
        {
            for (const auto& param : m_result.m_funcDef(m_ast).funcFParams) {
                const auto symbolId
                    = m_symbolResult.findSymbolId(param.identifier);
                if (!symbolId.has_value()
                    || !isTrackedObject(findSymbol(param.identifier))) {
                    continue;
                }
                m_localTrackedSymbols.insert(*symbolId);
            }
            for (auto block : m_blocks) {
                for (const auto& item : block(m_cfgArena).items) {
                    MATCH(item)
                    WITH(
                        [&](Decl decl) {
                            MATCH(decl)
                            WITH(
                                [&](Ref<ConstDecl> constDecl) {
                                    for (auto def : constDecl(m_ast).constDef) {
                                        const auto symbolId
                                            = m_symbolResult.findSymbolId(
                                                def(m_ast).identifier);
                                        if (!symbolId.has_value()
                                            || !isTrackedObject(findSymbol(
                                                def(m_ast).identifier))) {
                                            continue;
                                        }
                                        m_localTrackedSymbols.insert(*symbolId);
                                    }
                                },
                                [&](Ref<VarDecl> varDecl) {
                                    for (auto def : varDecl(m_ast).varDef) {
                                        const auto symbolId
                                            = m_symbolResult.findSymbolId(
                                                def(m_ast).identifier);
                                        if (!symbolId.has_value()
                                            || !isTrackedObject(findSymbol(
                                                def(m_ast).identifier))) {
                                            continue;
                                        }
                                        m_localTrackedSymbols.insert(*symbolId);
                                    }
                                });
                        },
                        [&](const auto&) {});
                }
            }
        }

        void initializeEdges()
        {
            for (auto block : m_blocks) {
                auto& info = m_result.m_blockInfoByBlock.at(block);
                info.m_predecessors.clear();
                info.m_successors.clear();
                info.m_dominatorTreeChildren.clear();
                info.m_dominanceFrontier.clear();
                info.m_outgoingArgsByTarget.clear();
                if (!block(m_cfgArena).terminator.has_value()) {
                    continue;
                }
                MATCH(*block(m_cfgArena).terminator)
                WITH(
                    [&](const SemanticBranchTerminator& branch) {
                        info.m_successors.push_back(branch.trueTarget);
                        info.m_successors.push_back(branch.falseTarget);
                    },
                    [&](const SemanticJumpTerminator& jump) {
                        info.m_successors.push_back(jump.target);
                    },
                    [&](const SemanticReturnTerminator&) {});
            }
            for (auto block : m_blocks) {
                for (auto succ :
                    m_result.m_blockInfoByBlock.at(block).m_successors) {
                    m_result.m_blockInfoByBlock.at(succ)
                        .m_predecessors.push_back(block);
                }
            }
        }

        [[nodiscard]] std::unordered_set<Block> reachableWithout(
            Block removed) const
        {
            std::unordered_set<Block> visited;
            const auto* cfg = m_controlFlow.findControlFlow(m_result.m_funcDef);
            if (cfg == nullptr || cfg->entryBlock == removed) {
                return visited;
            }
            std::vector<Block> stack = { cfg->entryBlock };
            while (!stack.empty()) {
                const auto block = stack.back();
                stack.pop_back();
                if (block == removed || visited.contains(block)) {
                    continue;
                }
                visited.insert(block);
                for (auto succ :
                    m_result.m_blockInfoByBlock.at(block).m_successors) {
                    if (succ != removed) {
                        stack.push_back(succ);
                    }
                }
            }
            return visited;
        }

        [[nodiscard]] bool dominates(Block lhs, Block rhs) const
        {
            const auto lhsIt = m_dfsInByBlock.find(lhs);
            const auto rhsIt = m_dfsInByBlock.find(rhs);
            if (lhsIt == m_dfsInByBlock.end()
                || rhsIt == m_dfsInByBlock.end()) {
                return false;
            }
            return lhsIt->second <= rhsIt->second
                && rhsIt->second < lhsIt->second + m_subtreeSizeByBlock.at(lhs);
        }

        [[nodiscard]] bool strictlyDominates(Block lhs, Block rhs) const
        {
            return lhs != rhs && dominates(lhs, rhs);
        }

        void computeDominators()
        {
            const auto* cfg = m_controlFlow.findControlFlow(m_result.m_funcDef);
            if (cfg == nullptr) {
                return;
            }
            for (auto dominator : m_blocks) {
                const auto reachable = reachableWithout(dominator);
                for (auto block : m_blocks) {
                    if (block == dominator || !reachable.contains(block)) {
                        m_dominatorsByBlock[block].insert(dominator);
                    }
                }
            }
            m_result.m_blockInfoByBlock.at(cfg->entryBlock)
                .m_immediateDominator.reset();
            for (auto block : m_blocks) {
                if (block == cfg->entryBlock) {
                    continue;
                }
                std::vector<Block> candidates;
                for (auto candidate : m_dominatorsByBlock.at(block)) {
                    if (candidate != block) {
                        candidates.push_back(candidate);
                    }
                }
                std::sort(candidates.begin(), candidates.end(),
                    [&](Block lhs, Block rhs) {
                        return m_blockOrder.at(lhs) < m_blockOrder.at(rhs);
                    });
                for (auto candidate : candidates) {
                    bool isImmediate = true;
                    for (auto other : candidates) {
                        if (other == candidate) {
                            continue;
                        }
                        if (!m_dominatorsByBlock.at(candidate).contains(
                                other)) {
                            isImmediate = false;
                            break;
                        }
                    }
                    if (!isImmediate) {
                        continue;
                    }
                    m_result.m_blockInfoByBlock.at(block).m_immediateDominator
                        = candidate;
                    m_result.m_blockInfoByBlock.at(candidate)
                        .m_dominatorTreeChildren.push_back(block);
                    break;
                }
            }

            size_t dfsIndex = 0;
            std::function<void(Block)> dfs = [&](Block block) {
                m_dfsInByBlock[block] = dfsIndex++;
                size_t subtreeSize = 1;
                auto& children = m_result.m_blockInfoByBlock.at(block)
                                     .m_dominatorTreeChildren;
                std::sort(children.begin(), children.end(),
                    [&](Block lhs, Block rhs) {
                        return m_blockOrder.at(lhs) < m_blockOrder.at(rhs);
                    });
                for (auto child : children) {
                    dfs(child);
                    subtreeSize += m_subtreeSizeByBlock.at(child);
                }
                m_subtreeSizeByBlock[block] = subtreeSize;
            };
            dfs(cfg->entryBlock);

            for (auto x : m_blocks) {
                for (auto y : m_blocks) {
                    bool onFrontier = false;
                    for (auto pred :
                        m_result.m_blockInfoByBlock.at(y).m_predecessors) {
                        if (dominates(x, pred) && !strictlyDominates(x, y)) {
                            onFrontier = true;
                            break;
                        }
                    }
                    if (onFrontier) {
                        m_result.m_blockInfoByBlock.at(x)
                            .m_dominanceFrontier.push_back(y);
                    }
                }
            }
        }

        void recordRead(std::unordered_set<int32_t>& useSet,
            const std::unordered_set<int32_t>& defSet, int32_t symbolId) const
        {
            if (!defSet.contains(symbolId)) {
                useSet.insert(symbolId);
            }
        }

        void computeUseDefSets()
        {
            for (auto block : m_blocks) {
                auto& info = m_result.m_blockInfoByBlock.at(block);
                info.m_useSet.clear();
                info.m_defSet.clear();

                const auto onRead = [&](Ref<Identifier>, int32_t symbolId) {
                    recordRead(info.m_useSet, info.m_defSet, symbolId);
                };
                const ExpressionWalker walker {
                    .m_ast = m_ast,
                    .m_symbolResult = m_symbolResult,
                    .m_localTrackedSymbols = m_localTrackedSymbols,
                    .m_onRead = onRead,
                };

                for (const auto& item : block(m_cfgArena).items) {
                    MATCH(item)
                    WITH(
                        [&](Decl decl) {
                            MATCH(decl)
                            WITH(
                                [&](Ref<ConstDecl> constDecl) {
                                    for (auto def : constDecl(m_ast).constDef) {
                                        const auto initExp
                                            = scalarInitExp(m_ast, def);
                                        if (initExp != nullptr) {
                                            walker.walkExp(initExp.ref());
                                        }
                                        const auto symbolId
                                            = m_symbolResult.findSymbolId(
                                                def(m_ast).identifier);
                                        if (symbolId.has_value()
                                            && m_localTrackedSymbols.contains(
                                                *symbolId)) {
                                            info.m_defSet.insert(*symbolId);
                                            m_defBlocksBySymbol[*symbolId]
                                                .push_back(block);
                                        }
                                    }
                                },
                                [&](Ref<VarDecl> varDecl) {
                                    for (auto def : varDecl(m_ast).varDef) {
                                        const auto initExp
                                            = scalarInitExp(m_ast, def);
                                        if (initExp != nullptr) {
                                            walker.walkExp(initExp.ref());
                                        }
                                        const auto symbolId
                                            = m_symbolResult.findSymbolId(
                                                def(m_ast).identifier);
                                        if (symbolId.has_value()
                                            && m_localTrackedSymbols.contains(
                                                *symbolId)) {
                                            info.m_defSet.insert(*symbolId);
                                            m_defBlocksBySymbol[*symbolId]
                                                .push_back(block);
                                        }
                                    }
                                });
                        },
                        [&](Ref<AssignStmt> assignStmt) {
                            const auto* lVal = std::get_if<Exp::LVal>(
                                &assignStmt(m_ast).lval(m_ast).kind);
                            if (lVal != nullptr) {
                                for (auto index : lVal->indices) {
                                    walker.walkExp(index);
                                }
                            }
                            walker.walkExp(assignStmt(m_ast).exp);
                            const auto identifier = scalarLValIdentifier(
                                m_ast, assignStmt(m_ast).lval);
                            if (!identifier.has_value()) {
                                return;
                            }
                            const auto symbolId
                                = m_symbolResult.findSymbolId(*identifier);
                            if (symbolId.has_value()
                                && m_localTrackedSymbols.contains(*symbolId)) {
                                info.m_defSet.insert(*symbolId);
                                m_defBlocksBySymbol[*symbolId].push_back(block);
                            }
                        },
                        [&](Ref<ExpStmt> expStmt) {
                            if (expStmt(m_ast).exp != nullptr) {
                                walker.walkExp(expStmt(m_ast).exp.ref());
                            }
                        });
                }

                if (block(m_cfgArena).terminator.has_value()) {
                    MATCH(*block(m_cfgArena).terminator)
                    WITH(
                        [&](const SemanticBranchTerminator& branch) {
                            walker.walkExp(branch.condition);
                        },
                        [&](const SemanticJumpTerminator&) {},
                        [&](const SemanticReturnTerminator& ret) {
                            if (!ret.value.has_value()) {
                                return;
                            }
                            if (const auto* exp
                                = std::get_if<Ref<Exp>>(&ret.value->kind)) {
                                walker.walkExp(*exp);
                            }
                        });
                }
            }

            for (auto& [symbolId, blocks] : m_defBlocksBySymbol) {
                std::sort(
                    blocks.begin(), blocks.end(), [&](Block lhs, Block rhs) {
                        return m_blockOrder.at(lhs) < m_blockOrder.at(rhs);
                    });
                blocks.erase(
                    std::unique(blocks.begin(), blocks.end()), blocks.end());
                (void)symbolId;
            }
        }

        void computeLiveness()
        {
            bool changed = true;
            while (changed) {
                changed = false;
                for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it) {
                    const auto block = *it;
                    auto& info = m_result.m_blockInfoByBlock.at(block);
                    std::unordered_set<int32_t> liveOut;
                    for (auto succ : info.m_successors) {
                        const auto& succLiveIn
                            = m_result.m_blockInfoByBlock.at(succ).m_liveIn;
                        liveOut.insert(succLiveIn.begin(), succLiveIn.end());
                    }
                    std::unordered_set<int32_t> liveIn = info.m_useSet;
                    for (auto symbolId : liveOut) {
                        if (!info.m_defSet.contains(symbolId)) {
                            liveIn.insert(symbolId);
                        }
                    }
                    if (liveOut != info.m_liveOut || liveIn != info.m_liveIn) {
                        info.m_liveOut = std::move(liveOut);
                        info.m_liveIn = std::move(liveIn);
                        changed = true;
                    }
                }
            }
        }

        void generateBlockParams()
        {
            std::vector<int32_t> symbols(
                m_localTrackedSymbols.begin(), m_localTrackedSymbols.end());
            std::sort(symbols.begin(), symbols.end());
            for (auto symbolId : symbols) {
                const auto defIt = m_defBlocksBySymbol.find(symbolId);
                if (defIt == m_defBlocksBySymbol.end()) {
                    continue;
                }
                std::queue<Block> worklist;
                std::unordered_set<Block> enqueued;
                for (auto block : defIt->second) {
                    worklist.push(block);
                    enqueued.insert(block);
                }
                while (!worklist.empty()) {
                    const auto block = worklist.front();
                    worklist.pop();
                    for (auto frontier : m_result.m_blockInfoByBlock.at(block)
                                             .m_dominanceFrontier) {
                        auto& frontierInfo
                            = m_result.m_blockInfoByBlock.at(frontier);
                        if (!frontierInfo.m_liveIn.contains(symbolId)) {
                            continue;
                        }
                        const bool hasParam
                            = std::any_of(frontierInfo.m_params.begin(),
                                frontierInfo.m_params.end(),
                                [&](const auto& param) {
                                    return param.m_symbolId == symbolId;
                                });
                        if (!hasParam) {
                            frontierInfo.m_params.push_back(
                                SemanticSsaBlockParam {
                                    .m_symbolId = symbolId });
                        }
                        if (enqueued.insert(frontier).second) {
                            worklist.push(frontier);
                        }
                    }
                }
            }
            for (auto block : m_blocks) {
                auto& params = m_result.m_blockInfoByBlock.at(block).m_params;
                std::sort(params.begin(), params.end(),
                    [](const auto& lhs, const auto& rhs) {
                        return lhs.m_symbolId < rhs.m_symbolId;
                    });
            }
        }

        [[nodiscard]] SemanticSsaAlias createAlias(int32_t symbolId)
        {
            return SemanticSsaAlias {
                .m_symbolId = symbolId,
                .m_version = m_nextAliasVersionBySymbol[symbolId]++,
            };
        }

        void bindScalarReads(Ref<Exp> exp,
            const std::unordered_map<int32_t, SemanticSsaAlias>& aliases)
        {
            const auto onRead
                = [&](Ref<Identifier> identifier, int32_t symbolId) {
                      const auto it = aliases.find(symbolId);
                      if (it != aliases.end()) {
                          m_aliasByIdentifier.insert_or_assign(
                              identifier, it->second);
                      }
                  };
            const ExpressionWalker walker {
                .m_ast = m_ast,
                .m_symbolResult = m_symbolResult,
                .m_localTrackedSymbols = m_localTrackedSymbols,
                .m_onRead = onRead,
            };
            walker.walkExp(exp);
        }

        void assignAliases()
        {
            const auto* cfg = m_controlFlow.findControlFlow(m_result.m_funcDef);
            if (cfg == nullptr) {
                return;
            }
            std::unordered_map<int32_t, SemanticSsaAlias> entryAliases;
            for (const auto& param : m_result.m_funcDef(m_ast).funcFParams) {
                const auto symbolId
                    = m_symbolResult.findSymbolId(param.identifier);
                if (!symbolId.has_value()
                    || !m_localTrackedSymbols.contains(*symbolId)) {
                    continue;
                }
                const auto alias = createAlias(*symbolId);
                entryAliases[*symbolId] = alias;
                m_aliasByIdentifier.insert_or_assign(param.identifier, alias);
            }

            std::function<void(
                Block, const std::unordered_map<int32_t, SemanticSsaAlias>&)>
                dfs = [&](Block block,
                          const std::unordered_map<int32_t, SemanticSsaAlias>&
                              inheritedAliases) {
                    auto aliases = inheritedAliases;
                    auto& blockInfo = m_result.m_blockInfoByBlock.at(block);
                    for (auto& param : blockInfo.m_params) {
                        param.m_alias = createAlias(param.m_symbolId);
                        aliases[param.m_symbolId] = param.m_alias;
                    }

                    const auto defineIdentifier =
                        [&](Ref<Identifier> identifier) {
                            const auto symbolId
                                = m_symbolResult.findSymbolId(identifier);
                            if (!symbolId.has_value()
                                || !m_localTrackedSymbols.contains(*symbolId)) {
                                return;
                            }
                            const auto alias = createAlias(*symbolId);
                            aliases[*symbolId] = alias;
                            m_aliasByIdentifier.insert_or_assign(
                                identifier, alias);
                        };

                    for (const auto& item : block(m_cfgArena).items) {
                        MATCH(item)
                        WITH(
                            [&](Decl decl) {
                                MATCH(decl)
                                WITH(
                                    [&](Ref<ConstDecl> constDecl) {
                                        for (auto def :
                                            constDecl(m_ast).constDef) {
                                            const auto initExp
                                                = scalarInitExp(m_ast, def);
                                            if (initExp != nullptr) {
                                                bindScalarReads(
                                                    initExp.ref(), aliases);
                                            }
                                            defineIdentifier(
                                                def(m_ast).identifier);
                                        }
                                    },
                                    [&](Ref<VarDecl> varDecl) {
                                        for (auto def : varDecl(m_ast).varDef) {
                                            const auto initExp
                                                = scalarInitExp(m_ast, def);
                                            if (initExp != nullptr) {
                                                bindScalarReads(
                                                    initExp.ref(), aliases);
                                            }
                                            defineIdentifier(
                                                def(m_ast).identifier);
                                        }
                                    });
                            },
                            [&](Ref<AssignStmt> assignStmt) {
                                const auto* lVal = std::get_if<Exp::LVal>(
                                    &assignStmt(m_ast).lval(m_ast).kind);
                                if (lVal != nullptr) {
                                    for (auto index : lVal->indices) {
                                        bindScalarReads(index, aliases);
                                    }
                                }
                                bindScalarReads(assignStmt(m_ast).exp, aliases);
                                const auto identifier = scalarLValIdentifier(
                                    m_ast, assignStmt(m_ast).lval);
                                if (identifier.has_value()) {
                                    defineIdentifier(*identifier);
                                }
                            },
                            [&](Ref<ExpStmt> expStmt) {
                                if (expStmt(m_ast).exp != nullptr) {
                                    bindScalarReads(
                                        expStmt(m_ast).exp.ref(), aliases);
                                }
                            });
                    }

                    if (block(m_cfgArena).terminator.has_value()) {
                        MATCH(*block(m_cfgArena).terminator)
                        WITH(
                            [&](const SemanticBranchTerminator& branch) {
                                bindScalarReads(branch.condition, aliases);
                            },
                            [&](const SemanticJumpTerminator&) {},
                            [&](const SemanticReturnTerminator& ret) {
                                if (!ret.value.has_value()) {
                                    return;
                                }
                                if (const auto* exp
                                    = std::get_if<Ref<Exp>>(&ret.value->kind)) {
                                    bindScalarReads(*exp, aliases);
                                }
                            });
                    }

                    for (auto succ : blockInfo.m_successors) {
                        std::vector<SemanticSsaAlias> args;
                        for (const auto& param :
                            m_result.m_blockInfoByBlock.at(succ).m_params) {
                            const auto it = aliases.find(param.m_symbolId);
                            if (it != aliases.end()) {
                                args.push_back(it->second);
                            }
                        }
                        blockInfo.m_outgoingArgsByTarget.insert_or_assign(
                            succ, std::move(args));
                    }

                    for (auto child : blockInfo.m_dominatorTreeChildren) {
                        dfs(child, aliases);
                    }
                };

            dfs(cfg->entryBlock, entryAliases);
        }
    };

} // namespace

namespace detail {

    class SemanticSSAAnalyzerImpl : public SemanticSSA {
    public:
        SemanticSSAAnalyzerImpl(const AST& ast, const SemanticCFG& controlFlow,
            const SymbolResolutionResult& symbolResult,
            const SemanticTypeAnalysisResult& typeResult)
            : SemanticSSA(ast, controlFlow, symbolResult, typeResult)
        {
        }

        void analyze(Ref<CompUnit> compUnit)
        {
            m_functionByFuncDef.clear();
            m_aliasByIdentifier.clear();
            for (const auto& topLevelItem : compUnit(ast).topLevelItems) {
                const auto* funcDef = std::get_if<Ref<FuncDef>>(&topLevelItem);
                if (funcDef == nullptr) {
                    continue;
                }
                const auto* cfg = m_controlFlow.findControlFlow(*funcDef);
                if (cfg == nullptr) {
                    continue;
                }
                SemanticFunctionSSA functionSSA { .m_funcDef = *funcDef };
                for (auto block : cfg->blocks) {
                    functionSSA.m_blockInfoByBlock.emplace(
                        block, SemanticSsaBlockInfo { .m_block = block });
                }
                auto [it, inserted] = m_functionByFuncDef.emplace(
                    *funcDef, std::move(functionSSA));
                (void)inserted;
                FunctionAnalyzer analyzer {
                    .m_ast = ast,
                    .m_controlFlow = m_controlFlow,
                    .m_cfgArena = m_controlFlow.controlFlowArena(),
                    .m_symbolResult = m_symbolResult,
                    .m_typeResult = m_typeResult,
                    .m_result = it->second,
                    .m_aliasByIdentifier = m_aliasByIdentifier,
                };
                analyzer.analyze();
            }
        }
    };

} // namespace detail

SemanticSSA::SemanticSSA(const AST& ast, const SemanticCFG& controlFlow,
    const SymbolResolutionResult& symbolResult,
    const SemanticTypeAnalysisResult& typeResult)
    : ast(ast)
    , m_controlFlow(controlFlow)
    , m_symbolResult(symbolResult)
    , m_typeResult(typeResult)
{
}

const SemanticFunctionSSA* SemanticSSA::findFunction(Ref<FuncDef> funcDef) const
{
    const auto it = m_functionByFuncDef.find(funcDef);
    return it == m_functionByFuncDef.end() ? nullptr : &it->second;
}

std::optional<SemanticSsaAlias> SemanticSSA::findAlias(
    Ref<Identifier> identifier) const
{
    const auto it = m_aliasByIdentifier.find(identifier);
    if (it == m_aliasByIdentifier.end()) {
        return std::nullopt;
    }
    return it->second;
}

const std::unordered_map<Ref<Identifier>, SemanticSsaAlias>&
SemanticSSA::aliasByIdentifier() const
{
    return m_aliasByIdentifier;
}

SemanticSSAAnalyzer::SemanticSSAAnalyzer(const AST& ast,
    const SemanticCFG& controlFlow, const SymbolResolutionResult& symbolResult,
    const SemanticTypeAnalysisResult& typeResult)
    : m_impl(std::make_unique<detail::SemanticSSAAnalyzerImpl>(
          ast, controlFlow, symbolResult, typeResult))
{
}

SemanticSSAAnalyzer::~SemanticSSAAnalyzer() = default;

void SemanticSSAAnalyzer::analyze(Ref<CompUnit> compUnit)
{
    m_impl->analyze(compUnit);
}

const SemanticSSA* SemanticSSAAnalyzer::operator->() const
{
    return m_impl.get();
}

} // namespace yesod::frontend