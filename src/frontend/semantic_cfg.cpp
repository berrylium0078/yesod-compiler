#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "frontend/semantic_cfg_impl.h"
#include "frontend/semantic_type.h"

namespace yesod::frontend {

SemanticCFG::SemanticCFG(const AST& ast)
    : ast(ast)
{
}

const std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>>&
SemanticCFG::loopByBreakStmt() const
{
    return m_loopByBreakStmt;
}

const std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>>&
SemanticCFG::loopByContinueStmt() const
{
    return m_loopByContinueStmt;
}

const SemanticFunctionControlFlow* SemanticCFG::findControlFlow(
    Ref<FuncDef> funcDef) const
{
    const auto controlFlowIt = m_controlFlowByFuncDef.find(funcDef);
    if (controlFlowIt == m_controlFlowByFuncDef.end()) {
        return nullptr;
    }
    return &controlFlowIt->second(m_controlFlowArena);
}

const SemanticControlFlowArena& SemanticCFG::controlFlowArena() const
{
    return m_controlFlowArena;
}

const std::vector<std::unique_ptr<Diagnostic>>& SemanticCFG::diagnostics() const
{
    return m_diagnostics;
}

namespace detail {
    namespace {
        bool isLogicalOperator(BinaryOpKeyword op)
        {
            return op == BinaryOpKeyword::andAnd || op == BinaryOpKeyword::orOr;
        }

        bool hasElseBody(const AST& ast, Stmt stmt)
        {
            return MATCH(stmt) WITH(
                [&](Ref<Block> block) { return !block(ast).items.empty(); },
                [&](const auto&) { return true; }, );
        }
    } // namespace

    class SemanticCFGBuilderImpl::FunctionBuilder {
    public:
        FunctionBuilder(SemanticCFGBuilderImpl& owner, Ref<FuncDef> funcDef)
            : m_owner(owner)
            , m_funcDef(funcDef)
        {
        }

        void build();

    private:
        [[nodiscard]] Ref<SemanticBasicBlock> createBlock(
            const std::string& stem);
        [[nodiscard]] SemanticBasicBlock& currentBlock();
        [[nodiscard]] bool currentBlockTerminated() const;
        void setJumpTerminator(Ref<SemanticBasicBlock> target);
        void setBranchTerminator(Ref<Exp> condition,
            Ref<SemanticBasicBlock> trueTarget,
            Ref<SemanticBasicBlock> falseTarget);
        void setReturnTerminator(std::optional<SemanticValue> value);
        void appendBlock(Ref<Block> block);
        void appendBlockItem(BlockItem item);
        void appendDecl(Decl decl);
        void appendStmt(Stmt stmt);
        void bindBlockOnly(Ref<Block> block);
        void bindBlockItemOnly(BlockItem item);
        void bindStmtOnly(Stmt stmt);
        void appendIfStmt(Ref<IfStmt> ifStmt);
        void appendWhileStmt(Ref<WhileStmt> whileStmt);
        void appendReturnStmt(Ref<ReturnStmt> returnStmt);
        void emitBooleanBranch(Ref<Exp> condition,
            Ref<SemanticBasicBlock> trueTarget,
            Ref<SemanticBasicBlock> falseTarget);

        SemanticCFGBuilderImpl& m_owner;
        Ref<FuncDef> m_funcDef;
        std::vector<Ref<SemanticBasicBlock>> m_blocks;
        std::optional<Ref<SemanticBasicBlock>> m_entryBlock;
        std::optional<Ref<SemanticBasicBlock>> m_currentBlock;
    };

    SemanticCFGBuilderImpl::SemanticCFGBuilderImpl(const AST& ast)
        : SemanticCFG(ast)
    {
    }

    void SemanticCFGBuilderImpl::analyze(Ref<CompUnit> compUnit)
    {
        m_loopByBreakStmt.clear();
        m_loopByContinueStmt.clear();
        m_controlFlowArena.clear();
        m_controlFlowByFuncDef.clear();
        m_loopStack.clear();
        m_diagnostics.clear();

        for (const auto topLevelItem : compUnit(ast).topLevelItems) {
            MATCH(topLevelItem)
            WITH(
                [&](Ref<FuncDef> funcDef) {
                    if (funcDef(ast).body == nullptr) {
                        return;
                    }
                    buildFunctionControlFlow(funcDef);
                },
                [&](const auto&) {}, );
        }
    }

    void SemanticCFGBuilderImpl::buildFunctionControlFlow(Ref<FuncDef> funcDef)
    {
        FunctionBuilder builder(*this, funcDef);
        builder.build();
    }

    void SemanticCFGBuilderImpl::bindBreakStmt(Ref<BreakStmt> breakStmt)
    {
        const auto loop = currentLoop();
        if (!loop.has_value()) {
            recordDiagnostic<BreakOutsideWhileDiagnostic>(
                breakStmt(ast).sourcePos.m_offset,
                "break statement is not inside a while loop");
            return;
        }
        m_loopByBreakStmt.insert_or_assign(breakStmt, loop->whileStmt);
    }

    void SemanticCFGBuilderImpl::bindContinueStmt(
        Ref<ContinueStmt> continueStmt)
    {
        const auto loop = currentLoop();
        if (!loop.has_value()) {
            recordDiagnostic<ContinueOutsideWhileDiagnostic>(
                continueStmt(ast).sourcePos.m_offset,
                "continue statement is not inside a while loop");
            return;
        }
        m_loopByContinueStmt.insert_or_assign(continueStmt, loop->whileStmt);
    }

    std::optional<SemanticCFGBuilderImpl::LoopContext>
    SemanticCFGBuilderImpl::currentLoop() const
    {
        if (m_loopStack.empty()) {
            return std::nullopt;
        }
        return m_loopStack.back();
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::build()
    {
        m_blocks.clear();
        m_entryBlock = createBlock("entry");
        m_currentBlock = m_entryBlock;

        appendBlock(m_funcDef(m_owner.ast).body.ref());

        const auto& funcDef = m_funcDef(m_owner.ast);
        const Ref<SemanticBasicBlock> endBlock = createBlock("end");
        if (!currentBlockTerminated()) {
            setJumpTerminator(endBlock);
        }

        m_currentBlock = endBlock;
        if (funcDef.m_funcType == FuncTypeKeyword::voidKeyword) {
            setReturnTerminator(std::nullopt);
        } else {
            setReturnTerminator(SemanticValue { .kind = int32_t { 0 } });
        }

        const Ref<SemanticFunctionControlFlow> cfg
            = m_owner.m_controlFlowArena.alloc<SemanticFunctionControlFlow>(
                SemanticFunctionControlFlow {
                    .funcDef = m_funcDef,
                    .entryBlock = *m_entryBlock,
                    .endBlock = endBlock,
                    .blocks = m_blocks,
                });
        m_owner.m_controlFlowByFuncDef.insert_or_assign(m_funcDef, cfg);
    }

    Ref<SemanticBasicBlock>
    SemanticCFGBuilderImpl::FunctionBuilder::createBlock(
        const std::string& stem)
    {
        const std::string nameHint = stem == "entry" || stem == "end"
            ? stem
            : stem + "_" + std::to_string(m_owner.m_nextGeneratedBlockId++);
        const Ref<SemanticBasicBlock> block
            = m_owner.m_controlFlowArena.alloc<SemanticBasicBlock>(
                SemanticBasicBlock {
                    .nameHint = nameHint,
                });
        m_blocks.push_back(block);
        return block;
    }

    SemanticBasicBlock& SemanticCFGBuilderImpl::FunctionBuilder::currentBlock()
    {
        return (*m_currentBlock)(m_owner.m_controlFlowArena);
    }

    bool SemanticCFGBuilderImpl::FunctionBuilder::currentBlockTerminated() const
    {
        return (*m_currentBlock)(m_owner.m_controlFlowArena)
            .terminator.has_value();
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::setJumpTerminator(
        Ref<SemanticBasicBlock> target)
    {
        currentBlock().terminator = SemanticJumpTerminator { .target = target };
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::setBranchTerminator(
        Ref<Exp> condition, Ref<SemanticBasicBlock> trueTarget,
        Ref<SemanticBasicBlock> falseTarget)
    {
        currentBlock().terminator = SemanticBranchTerminator {
            .condition = condition,
            .trueTarget = trueTarget,
            .falseTarget = falseTarget,
        };
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::setReturnTerminator(
        std::optional<SemanticValue> value)
    {
        currentBlock().terminator
            = SemanticReturnTerminator { .value = std::move(value) };
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::appendBlock(Ref<Block> block)
    {
        for (const auto item : block(m_owner.ast).items) {
            if (currentBlockTerminated()) {
                bindBlockItemOnly(item);
                continue;
            }
            appendBlockItem(item);
        }
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::appendBlockItem(
        BlockItem item)
    {
        MATCH(item)
        WITH([&](Decl decl) { appendDecl(decl); },
            [&](Stmt stmt) { appendStmt(stmt); }, );
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::appendDecl(Decl decl)
    {
        currentBlock().items.push_back(decl);
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::appendStmt(Stmt stmt)
    {
        if (currentBlockTerminated()) {
            return;
        }

        MATCH(stmt)
        WITH([&](Ref<IfStmt> ifStmt) { appendIfStmt(ifStmt); },
            [&](Ref<WhileStmt> whileStmt) { appendWhileStmt(whileStmt); },
            [&](Ref<BreakStmt> breakStmt) {
                m_owner.bindBreakStmt(breakStmt);
                const auto loop = m_owner.currentLoop();
                if (!loop.has_value()) {
                    return;
                }
                setJumpTerminator(loop->endBlock);
            },
            [&](Ref<ContinueStmt> continueStmt) {
                m_owner.bindContinueStmt(continueStmt);
                const auto loop = m_owner.currentLoop();
                if (!loop.has_value()) {
                    return;
                }
                setJumpTerminator(loop->condBlock);
            },
            [&](Ref<AssignStmt> assignStmt) {
                currentBlock().items.push_back(assignStmt);
            },
            [&](Ref<Block> block) { appendBlock(block); },
            [&](Ref<ReturnStmt> returnStmt) { appendReturnStmt(returnStmt); },
            [&](Ref<ExpStmt> expStmt) {
                currentBlock().items.push_back(expStmt);
            });
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::bindBlockOnly(
        Ref<Block> block)
    {
        for (const auto item : block(m_owner.ast).items) {
            bindBlockItemOnly(item);
        }
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::bindBlockItemOnly(
        BlockItem item)
    {
        MATCH(item)
        WITH([&](Decl) {}, [&](Stmt stmt) { bindStmtOnly(stmt); }, );
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::bindStmtOnly(Stmt stmt)
    {
        MATCH(stmt)
        WITH(
            [&](Ref<IfStmt> ifStmt) {
                bindStmtOnly(ifStmt(m_owner.ast).thenBody);
                bindStmtOnly(ifStmt(m_owner.ast).elseBody);
            },
            [&](Ref<WhileStmt> whileStmt) {
                m_owner.m_loopStack.push_back(
                    SemanticCFGBuilderImpl::LoopContext {
                        .whileStmt = whileStmt,
                        .condBlock = *m_currentBlock,
                        .endBlock = *m_currentBlock,
                    });
                bindStmtOnly(whileStmt(m_owner.ast).body);
                m_owner.m_loopStack.pop_back();
            },
            [&](Ref<BreakStmt> breakStmt) { m_owner.bindBreakStmt(breakStmt); },
            [&](Ref<ContinueStmt> continueStmt) {
                m_owner.bindContinueStmt(continueStmt);
            },
            [&](Ref<Block> block) { bindBlockOnly(block); },
            [&](const auto&) {});
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::appendIfStmt(
        Ref<IfStmt> ifStmt)
    {
        const auto& parsedIfStmt = ifStmt(m_owner.ast);
        const Ref<SemanticBasicBlock> thenBlock = createBlock("if_then");
        const bool hasElse = hasElseBody(m_owner.ast, parsedIfStmt.elseBody);
        Ref<SemanticBasicBlock> elseBlock = thenBlock;
        const Ref<SemanticBasicBlock> contBlock
            = hasElse ? createBlock("if_else") : createBlock("if_end");

        if (hasElse) {
            elseBlock = contBlock;
        }

        const Ref<SemanticBasicBlock> joinBlock
            = hasElse ? createBlock("if_end") : contBlock;

        if (!hasElse) {
            elseBlock = joinBlock;
        } else {
            elseBlock = contBlock;
        }

        emitBooleanBranch(parsedIfStmt.condition, thenBlock, elseBlock);

        m_currentBlock = thenBlock;
        appendStmt(parsedIfStmt.thenBody);
        if (!currentBlockTerminated()) {
            setJumpTerminator(joinBlock);
        }

        if (hasElse) {
            m_currentBlock = elseBlock;
            appendStmt(parsedIfStmt.elseBody);
            if (!currentBlockTerminated()) {
                setJumpTerminator(joinBlock);
            }
        }

        m_currentBlock = joinBlock;
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::appendWhileStmt(
        Ref<WhileStmt> whileStmt)
    {
        const auto& parsedWhileStmt = whileStmt(m_owner.ast);
        const Ref<SemanticBasicBlock> condBlock = createBlock("while_cond");
        const Ref<SemanticBasicBlock> bodyBlock = createBlock("while_body");
        const Ref<SemanticBasicBlock> endBlock = createBlock("while_end");

        setJumpTerminator(condBlock);

        m_owner.m_loopStack.push_back(SemanticCFGBuilderImpl::LoopContext {
            .whileStmt = whileStmt,
            .condBlock = condBlock,
            .endBlock = endBlock,
        });

        m_currentBlock = condBlock;
        emitBooleanBranch(parsedWhileStmt.condition, bodyBlock, endBlock);

        m_currentBlock = bodyBlock;
        appendStmt(parsedWhileStmt.body);
        if (!currentBlockTerminated()) {
            setJumpTerminator(condBlock);
        }

        m_owner.m_loopStack.pop_back();
        m_currentBlock = endBlock;
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::appendReturnStmt(
        Ref<ReturnStmt> returnStmt)
    {
        const auto& parsedReturnStmt = returnStmt(m_owner.ast);
        if (!parsedReturnStmt.exp) {
            setReturnTerminator(std::nullopt);
            return;
        }

        const auto& exp = parsedReturnStmt.exp.ref()(m_owner.ast);
        if (const auto* binaryExp = std::get_if<Exp::Binary>(&exp.kind);
            binaryExp != nullptr && isLogicalOperator(binaryExp->op)) {
            const Ref<SemanticBasicBlock> trueBlock = createBlock("bool_true");
            const Ref<SemanticBasicBlock> falseBlock
                = createBlock("bool_false");
            emitBooleanBranch(
                parsedReturnStmt.exp.ref(), trueBlock, falseBlock);

            m_currentBlock = trueBlock;
            setReturnTerminator(SemanticValue { .kind = int32_t { 1 } });

            m_currentBlock = falseBlock;
            setReturnTerminator(SemanticValue { .kind = int32_t { 0 } });
            return;
        }

        setReturnTerminator(
            SemanticValue { .kind = parsedReturnStmt.exp.ref() });
    }

    void SemanticCFGBuilderImpl::FunctionBuilder::emitBooleanBranch(
        Ref<Exp> condition, Ref<SemanticBasicBlock> trueTarget,
        Ref<SemanticBasicBlock> falseTarget)
    {
        const auto& parsedExp = condition(m_owner.ast);
        if (const auto* binaryExp = std::get_if<Exp::Binary>(&parsedExp.kind);
            binaryExp != nullptr && isLogicalOperator(binaryExp->op)) {
            const Ref<SemanticBasicBlock> rhsBlock = createBlock(
                binaryExp->op == BinaryOpKeyword::orOr ? "lor_rhs"
                                                       : "land_rhs");
            if (binaryExp->op == BinaryOpKeyword::orOr) {
                emitBooleanBranch(binaryExp->lhs, trueTarget, rhsBlock);
                m_currentBlock = rhsBlock;
                emitBooleanBranch(binaryExp->rhs, trueTarget, falseTarget);
                return;
            }

            emitBooleanBranch(binaryExp->lhs, rhsBlock, falseTarget);
            m_currentBlock = rhsBlock;
            emitBooleanBranch(binaryExp->rhs, trueTarget, falseTarget);
            return;
        }

        setBranchTerminator(condition, trueTarget, falseTarget);
    }

} // namespace detail

void SemanticCFG::simplify(
    const std::unordered_map<Ref<Exp>, SemanticExpInfo>& expInfo)
{
    // Phase 1: Replace constant-condition branches with unconditional jumps.
    // Uses emplace to construct the jump terminator in-place within the
    // optional, avoiding any variant valueless state issues.
    for (auto& [funcDef, funcCfgRef] : m_controlFlowByFuncDef) {
        (void)funcDef;
        auto& funcCfg = funcCfgRef(m_controlFlowArena);
        const auto pruneUnreachableBlocks = [&]() -> void {
            std::unordered_set<Ref<SemanticBasicBlock>> reachable;
            std::vector<Ref<SemanticBasicBlock>> worklist { funcCfg.entryBlock };
            while (!worklist.empty()) {
                const auto block = worklist.back();
                worklist.pop_back();
                if (!reachable.insert(block).second) {
                    continue;
                }
                const auto& blockInfo = block(m_controlFlowArena);
                if (!blockInfo.terminator.has_value()) {
                    continue;
                }
                MATCH(*blockInfo.terminator)
                WITH(
                    [&](const SemanticJumpTerminator& jump) {
                        worklist.push_back(jump.target);
                    },
                    [&](const SemanticBranchTerminator& branch) {
                        worklist.push_back(branch.trueTarget);
                        worklist.push_back(branch.falseTarget);
                    },
                    [&](const SemanticReturnTerminator&) {});
            }

            std::vector<Ref<SemanticBasicBlock>> prunedBlocks;
            prunedBlocks.reserve(funcCfg.blocks.size());
            for (const auto block : funcCfg.blocks) {
                const auto& blockInfo = block(m_controlFlowArena);
                if (reachable.contains(block) || blockInfo.nameHint == "end") {
                    prunedBlocks.push_back(block);
                }
            }
            funcCfg.blocks = std::move(prunedBlocks);
        };
        bool changed = true;

        while (changed) {
            changed = false;
            for (auto block : funcCfg.blocks) {
                auto& blockInfo = block(m_controlFlowArena);
                if (!blockInfo.terminator.has_value()) {
                    continue;
                }
                const auto* branch = std::get_if<SemanticBranchTerminator>(
                    &*blockInfo.terminator);
                if (branch == nullptr) {
                    continue;
                }
                const auto infoIt = expInfo.find(branch->condition);
                if (infoIt == expInfo.end()
                    || !infoIt->second.m_constantValue.has_value()) {
                    continue;
                }
                const bool constantVal
                    = infoIt->second.m_constantValue.value() != 0;
                // Construct a new optional with SemanticBlockTerminator holding
                // SemanticJumpTerminator, then move-assign to replace the old
                auto jumpTarget
                    = constantVal ? branch->trueTarget : branch->falseTarget;
                SemanticBlockTerminator newTerminator
                    = SemanticJumpTerminator { .target = jumpTarget };
                blockInfo.terminator.emplace(std::move(newTerminator));
                changed = true;
            }
        }

        pruneUnreachableBlocks();

        // Phase 2: Merge reachable blocks with single predecessor.
        // Single pass: for each block with an unconditional jump to a successor
        // that has exactly one incoming edge, merge the successor into this
        // block.
        {
            // Compute incoming edge counts for all blocks
            std::unordered_map<Ref<SemanticBasicBlock>, size_t> incomingCount;
            for (auto block : funcCfg.blocks) {
                auto& bbInfo = block(m_controlFlowArena);
                if (!bbInfo.terminator.has_value()) {
                    continue;
                }
                MATCH(*bbInfo.terminator)
                WITH(
                    [&](const SemanticJumpTerminator& j) {
                        ++incomingCount[j.target];
                    },
                    [&](const SemanticBranchTerminator& b) {
                        ++incomingCount[b.trueTarget];
                        ++incomingCount[b.falseTarget];
                    },
                    [&](const SemanticReturnTerminator&) {});
            }

            std::unordered_set<Ref<SemanticBasicBlock>> toRemove;
            for (auto block : funcCfg.blocks) {
                if (toRemove.contains(block)) {
                    continue;
                }
                auto& blockInfo = block(m_controlFlowArena);
                if (!blockInfo.terminator.has_value()) {
                    continue;
                }
                const auto* jump = std::get_if<SemanticJumpTerminator>(
                    &*blockInfo.terminator);
                if (jump == nullptr) {
                    continue;
                }
                const auto succ = jump->target;
                if (succ == block || toRemove.contains(succ)) {
                    continue;
                }
                // Only merge if the successor has exactly one incoming edge
                // and is not the entry block.
                auto it = incomingCount.find(succ);
                if (it == incomingCount.end() || it->second != 1) {
                    continue;
                }
                if (succ == funcCfg.entryBlock) {
                    continue;
                }
                // Never merge the synthesized %end block (default return guard)
                if (succ(m_controlFlowArena).nameHint == "end") {
                    continue;
                }

                auto& succInfo = succ(m_controlFlowArena);
                // Merge succ's items into block
                blockInfo.items.insert(blockInfo.items.end(),
                    succInfo.items.begin(), succInfo.items.end());
                // Replace block's terminator with succ's terminator
                blockInfo.terminator.emplace(std::move(*succInfo.terminator));
                // Redirect any block whose terminator referenced succ to block
                for (auto other : funcCfg.blocks) {
                    if (other == block || other == succ) {
                        continue;
                    }
                    auto& otherInfo = other(m_controlFlowArena);
                    if (!otherInfo.terminator.has_value()) {
                        continue;
                    }
                    MATCH(*otherInfo.terminator)
                    WITH(
                        [&](SemanticJumpTerminator& otherJump) {
                            if (otherJump.target == succ) {
                                otherJump.target = block;
                            }
                        },
                        [&](SemanticBranchTerminator& otherBranch) {
                            if (otherBranch.trueTarget == succ) {
                                otherBranch.trueTarget = block;
                            }
                            if (otherBranch.falseTarget == succ) {
                                otherBranch.falseTarget = block;
                            }
                        },
                        [&](const SemanticReturnTerminator&) {});
                }
                toRemove.insert(succ);
            }

            if (!toRemove.empty()) {
                std::vector<Ref<SemanticBasicBlock>> newBlocks;
                for (auto block : funcCfg.blocks) {
                    if (!toRemove.contains(block)) {
                        newBlocks.push_back(block);
                    }
                }
                funcCfg.blocks = std::move(newBlocks);
            }
        }

        pruneUnreachableBlocks();
    }
}

SemanticCFGBuilder::SemanticCFGBuilder(const AST& ast)
    : m_impl(std::make_unique<detail::SemanticCFGBuilderImpl>(ast))
{
}

SemanticCFGBuilder::~SemanticCFGBuilder() = default;

void SemanticCFGBuilder::analyze(Ref<CompUnit> compUnit)
{
    m_impl->analyze(compUnit);
}

void SemanticCFGBuilder::simplify(
    const std::unordered_map<Ref<Exp>, SemanticExpInfo>& expInfo)
{
    m_impl->simplify(expInfo);
}

const SemanticCFG* SemanticCFGBuilder::operator->() const
{
    return m_impl.get();
}

} // namespace yesod::frontend