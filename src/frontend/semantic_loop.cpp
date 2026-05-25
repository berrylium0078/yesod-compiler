#include <optional>
#include <string>
#include <utility>

#include "frontend/semantic_loop_impl.h"

namespace yesod::frontend {

SemanticLoopBindingResult::SemanticLoopBindingResult(const AST& ast)
    : ast(ast)
{
}

const std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>>&
SemanticLoopBindingResult::loopByBreakStmt() const
{
    return m_loopByBreakStmt;
}

const std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>>&
SemanticLoopBindingResult::loopByContinueStmt() const
{
    return m_loopByContinueStmt;
}

const std::vector<std::unique_ptr<Diagnostic>>&
SemanticLoopBindingResult::diagnostics() const
{
    return m_diagnostics;
}

namespace detail {
    SemanticLoopBinderImpl::SemanticLoopBinderImpl(const AST& ast)
        : AstVisitor(ast)
        , SemanticLoopBindingResult(ast)
    {
    }

    void SemanticLoopBinderImpl::analyze(Ref<CompUnit> compUnit)
    {
        m_loopByBreakStmt.clear();
        m_loopByContinueStmt.clear();
        m_loopStack.clear();
        m_diagnostics.clear();
        traverse(compUnit);
    }

    void SemanticLoopBinderImpl::visitWhileStmt(Ref<WhileStmt> whileStmt)
    {
        m_loopStack.push_back(whileStmt);
        AstVisitor::visitWhileStmt(whileStmt);
        m_loopStack.pop_back();
    }

    void SemanticLoopBinderImpl::visitBreakStmt(Ref<BreakStmt> breakStmt)
    {
        const auto loop = currentLoop();
        if (!loop.has_value()) {
            recordDiagnostic<BreakOutsideWhileDiagnostic>(
                breakStmt(m_ast).sourcePos.m_offset,
                "break statement is not inside a while loop");
            return;
        }
        m_loopByBreakStmt.insert_or_assign(breakStmt, *loop);
    }

    void SemanticLoopBinderImpl::visitContinueStmt(
        Ref<ContinueStmt> continueStmt)
    {
        const auto loop = currentLoop();
        if (!loop.has_value()) {
            recordDiagnostic<ContinueOutsideWhileDiagnostic>(
                continueStmt(m_ast).sourcePos.m_offset,
                "continue statement is not inside a while loop");
            return;
        }
        m_loopByContinueStmt.insert_or_assign(continueStmt, *loop);
    }

    std::optional<Ref<WhileStmt>> SemanticLoopBinderImpl::currentLoop() const
    {
        if (m_loopStack.empty()) {
            return std::nullopt;
        }
        return m_loopStack.back();
    }

} // namespace detail

SemanticLoopBinder::SemanticLoopBinder(const AST& ast)
    : m_impl(std::make_unique<detail::SemanticLoopBinderImpl>(ast))
{
}

SemanticLoopBinder::~SemanticLoopBinder() = default;

void SemanticLoopBinder::analyze(Ref<CompUnit> compUnit)
{
    m_impl->analyze(compUnit);
}

const SemanticLoopBindingResult* SemanticLoopBinder::operator->() const
{
    return m_impl.get();
}

} // namespace yesod::frontend