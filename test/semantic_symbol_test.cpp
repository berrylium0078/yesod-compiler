#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

ast::Handle<ast::DeclNode> requireDeclNode(
    const ast::Handle<ast::BlockItemNode>& blockItem_nn)
{
    ast::Handle<ast::DeclNode> declNode_nn;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::DeclNode>>) {
                declNode_nn = blockItemAlt;
            }
        },
        blockItem_nn->m_blockItem);
    require(declNode_nn != nullptr, "expected declaration block item");
    return declNode_nn;
}

ast::Handle<ast::StmtNode> requireStmtNode(
    const ast::Handle<ast::BlockItemNode>& blockItem_nn)
{
    ast::Handle<ast::StmtNode> stmtNode_nn;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::StmtNode>>) {
                stmtNode_nn = blockItemAlt;
            }
        },
        blockItem_nn->m_blockItem);
    require(stmtNode_nn != nullptr, "expected statement block item");
    return stmtNode_nn;
}

ast::Handle<ast::VarDecl> requireVarDecl(
    const ast::Handle<ast::DeclNode>& declNode_nn)
{
    ast::Handle<ast::VarDecl> varDecl_nn;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::VarDecl>>) {
                varDecl_nn = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(varDecl_nn != nullptr, "expected var declaration");
    return varDecl_nn;
}

ast::Handle<ast::AssignStmt> requireAssignStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::AssignStmt> assignStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              ast::Handle<ast::AssignStmt>>) {
                assignStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(assignStmt_nn != nullptr, "expected assignment statement");
    return assignStmt_nn;
}

ast::Handle<ast::ExpStmt> requireExpStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::ExpStmt> expStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::ExpStmt>>) {
                expStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(expStmt_nn != nullptr, "expected expression statement");
    return expStmt_nn;
}

ast::Handle<ast::ReturnStmt> requireReturnStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::ReturnStmt> returnStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              ast::Handle<ast::ReturnStmt>>) {
                returnStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(returnStmt_nn != nullptr, "expected return statement");
    return returnStmt_nn;
}

ast::Handle<ast::Block> requireBlockStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::Block> block_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::Block>>) {
                block_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(block_nn != nullptr, "expected block statement");
    return block_nn;
}

ast::Handle<ast::Exp> requireSimpleLValExp(const ast::Handle<ast::Exp>& exp_nn)
{
    require(exp_nn != nullptr, "expected expression node");
    require(std::holds_alternative<ast::LVal>(exp_nn->m_kind),
        "expected lvalue expression");
    return exp_nn;
}

void testDefinitionAndUsesShareOneSymbolBinding()
{
    const auto output = analyzeSource(
        "int main(){int value = 1; value = 2; value; return value;}");
    require(output.success(), "expected semantic success");

    const auto& blockItems
        = output.m_root->m_funcDef_nn->m_block_nn->m_blockItems;
    const auto varDecl_nn = requireVarDecl(requireDeclNode(blockItems[0]));
    const auto assignStmt_nn
        = requireAssignStmt(requireStmtNode(blockItems[1]));
    const auto expStmt_nn = requireExpStmt(requireStmtNode(blockItems[2]));
    const auto returnStmt_nn
        = requireReturnStmt(requireStmtNode(blockItems[3]));

    const auto& defSymbol = requireSymbol(output, varDecl_nn->m_varDefs[0]);
    require(
        requireSymbol(output, assignStmt_nn->m_lVal_nn).m_id == defSymbol.m_id,
        "assignment lvalue should resolve to the declaration symbol");
    require(
        requireSymbol(output, requireSimpleLValExp(expStmt_nn->m_exp_nn)).m_id
            == defSymbol.m_id,
        "expression statement use should resolve to the declaration symbol");
    require(requireSymbol(output, requireSimpleLValExp(returnStmt_nn->m_exp_nn))
                .m_id
            == defSymbol.m_id,
        "return expression should resolve to the declaration symbol");
}

void testNestedScopePrefersInnermostDefinition()
{
    const auto output
        = analyzeSource("int main(){int value = 1; {int value = 2; value; "
                        "return value;} return value;}");
    require(output.success(), "expected semantic success");

    const auto& outerItems
        = output.m_root->m_funcDef_nn->m_block_nn->m_blockItems;
    const auto outerDecl_nn = requireVarDecl(requireDeclNode(outerItems[0]));
    const auto nestedBlock_nn
        = requireBlockStmt(requireStmtNode(outerItems[1]));
    const auto outerReturn_nn
        = requireReturnStmt(requireStmtNode(outerItems[2]));

    const auto innerDecl_nn
        = requireVarDecl(requireDeclNode(nestedBlock_nn->m_blockItems[0]));
    const auto innerExpStmt_nn
        = requireExpStmt(requireStmtNode(nestedBlock_nn->m_blockItems[1]));
    const auto innerReturn_nn
        = requireReturnStmt(requireStmtNode(nestedBlock_nn->m_blockItems[2]));

    const auto& outerSymbol = requireSymbol(output, outerDecl_nn->m_varDefs[0]);
    const auto& innerSymbol = requireSymbol(output, innerDecl_nn->m_varDefs[0]);
    require(innerSymbol.m_id != outerSymbol.m_id,
        "shadowing declarations should create distinct symbol identities");

    require(
        requireSymbol(output, requireSimpleLValExp(innerExpStmt_nn->m_exp_nn))
                .m_id
            == innerSymbol.m_id,
        "nested scope lookup should prefer the innermost definition");
    require(
        requireSymbol(output, requireSimpleLValExp(innerReturn_nn->m_exp_nn))
                .m_id
            == innerSymbol.m_id,
        "inner return should resolve to the innermost definition");
    require(
        requireSymbol(output, requireSimpleLValExp(outerReturn_nn->m_exp_nn))
                .m_id
            == outerSymbol.m_id,
        "outer return should resolve to the outer definition");
}

void testUseBeforeDefinitionDiagnostic()
{
    const auto output = analyzeSource("int main(){value = 1; return 0;}");
    require(!output.success(),
        "use-before-definition should fail semantic analysis");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::useBeforeDefinition,
        "use-before-definition should report the expected semantic label");
}

void testDoubleDefinitionDiagnostic()
{
    const auto output
        = analyzeSource("int main(){int value; int value; return 0;}");
    require(
        !output.success(), "double definition should fail semantic analysis");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::doubleDefinition,
        "double definition should report the expected semantic label");
}

} // namespace

int main()
{
    testDefinitionAndUsesShareOneSymbolBinding();
    testNestedScopePrefersInnermostDefinition();
    testUseBeforeDefinitionDiagnostic();
    testDoubleDefinitionDiagnostic();
    return 0;
}
