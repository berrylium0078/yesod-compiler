#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

std::shared_ptr<ast::DeclNode> requireDeclNode(
    const std::shared_ptr<ast::BlockItemNode>& blockItem_nn)
{
    std::shared_ptr<ast::DeclNode> declNode_nn;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                declNode_nn = blockItemAlt;
            }
        },
        blockItem_nn->m_blockItem);
    require(declNode_nn != nullptr, "expected declaration block item");
    return declNode_nn;
}

std::shared_ptr<ast::StmtNode> requireStmtNode(
    const std::shared_ptr<ast::BlockItemNode>& blockItem_nn)
{
    std::shared_ptr<ast::StmtNode> stmtNode_nn;
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                stmtNode_nn = blockItemAlt;
            }
        },
        blockItem_nn->m_blockItem);
    require(stmtNode_nn != nullptr, "expected statement block item");
    return stmtNode_nn;
}

std::shared_ptr<ast::VarDecl> requireVarDecl(
    const std::shared_ptr<ast::DeclNode>& declNode_nn)
{
    std::shared_ptr<ast::VarDecl> varDecl_nn;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::VarDecl>>) {
                varDecl_nn = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(varDecl_nn != nullptr, "expected var declaration");
    return varDecl_nn;
}

std::shared_ptr<ast::AssignStmt> requireAssignStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::AssignStmt> assignStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ast::AssignStmt>>) {
                assignStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(assignStmt_nn != nullptr, "expected assignment statement");
    return assignStmt_nn;
}

std::shared_ptr<ast::ExpStmt> requireExpStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::ExpStmt> expStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::ExpStmt>>) {
                expStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(expStmt_nn != nullptr, "expected expression statement");
    return expStmt_nn;
}

std::shared_ptr<ast::ReturnStmt> requireReturnStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::ReturnStmt> returnStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ast::ReturnStmt>>) {
                returnStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(returnStmt_nn != nullptr, "expected return statement");
    return returnStmt_nn;
}

std::shared_ptr<ast::Block> requireBlockStmt(
    const std::shared_ptr<ast::StmtNode>& stmtNode_nn)
{
    std::shared_ptr<ast::Block> block_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::Block>>) {
                block_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(block_nn != nullptr, "expected block statement");
    return block_nn;
}

const ast::LVal& requireSimpleLValExp(const ast::Exp& exp)
{
    const auto& lOrExp = *exp.m_lOrExp_nn;
    require(lOrExp.m_tail.empty(), "expected simple logical-or expression");
    const auto& lAndExp = *lOrExp.m_head_nn;
    require(lAndExp.m_tail.empty(), "expected simple logical-and expression");
    const auto& eqExp = *lAndExp.m_head_nn;
    require(eqExp.m_tail.empty(), "expected simple equality expression");
    const auto& relExp = *eqExp.m_head_nn;
    require(relExp.m_tail.empty(), "expected simple relational expression");
    const auto& addExp = *relExp.m_head_nn;
    require(addExp.m_tail.empty(), "expected simple additive expression");
    const auto& mulExp = *addExp.m_head_nn;
    require(mulExp.m_tail.empty(), "expected simple multiplicative expression");
    const auto& unaryExp = *mulExp.m_head_nn;

    const ast::PrimaryExp* primaryExp_nn = nullptr;
    std::visit(
        [&](const auto& unaryAlt) {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::PrimaryExp>>) {
                primaryExp_nn = unaryAlt.get();
            }
        },
        unaryExp.m_kind);
    require(primaryExp_nn != nullptr, "expected simple primary expression");

    const ast::LVal* lVal_nn = nullptr;
    std::visit(
        [&](const auto& primaryAlt) {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::LVal>>) {
                lVal_nn = primaryAlt.get();
            }
        },
        primaryExp_nn->m_kind);
    require(lVal_nn != nullptr, "expected lvalue expression");
    return *lVal_nn;
}

void testDefinitionAndUsesShareOneSymbolBinding()
{
    const auto output = analyzeSource(
        "int main(){int value = 1; value = 2; value; return value;}");
    require(output.success(), "expected semantic success");

    const auto& blockItems = output.m_root->m_funcDef_nn->m_block_nn->m_blockItems;
    const auto varDecl_nn = requireVarDecl(requireDeclNode(blockItems[0]));
    const auto assignStmt_nn = requireAssignStmt(requireStmtNode(blockItems[1]));
    const auto expStmt_nn = requireExpStmt(requireStmtNode(blockItems[2]));
    const auto returnStmt_nn = requireReturnStmt(requireStmtNode(blockItems[3]));

    const auto& defSymbol = requireSymbol(output, *varDecl_nn->m_varDefs[0]);
    require(requireSymbol(output, *assignStmt_nn->m_lVal_nn).m_id == defSymbol.m_id,
        "assignment lvalue should resolve to the declaration symbol");
    require(requireSymbol(output, requireSimpleLValExp(*expStmt_nn->m_exp_nn)).m_id
            == defSymbol.m_id,
        "expression statement use should resolve to the declaration symbol");
    require(requireSymbol(output, requireSimpleLValExp(*returnStmt_nn->m_exp_nn)).m_id
            == defSymbol.m_id,
        "return expression should resolve to the declaration symbol");
}

void testNestedScopePrefersInnermostDefinition()
{
    const auto output = analyzeSource(
        "int main(){int value = 1; {int value = 2; value; return value;} return value;}");
    require(output.success(), "expected semantic success");

    const auto& outerItems = output.m_root->m_funcDef_nn->m_block_nn->m_blockItems;
    const auto outerDecl_nn = requireVarDecl(requireDeclNode(outerItems[0]));
    const auto nestedBlock_nn = requireBlockStmt(requireStmtNode(outerItems[1]));
    const auto outerReturn_nn = requireReturnStmt(requireStmtNode(outerItems[2]));

    const auto innerDecl_nn
        = requireVarDecl(requireDeclNode(nestedBlock_nn->m_blockItems[0]));
    const auto innerExpStmt_nn
        = requireExpStmt(requireStmtNode(nestedBlock_nn->m_blockItems[1]));
    const auto innerReturn_nn
        = requireReturnStmt(requireStmtNode(nestedBlock_nn->m_blockItems[2]));

    const auto& outerSymbol = requireSymbol(output, *outerDecl_nn->m_varDefs[0]);
    const auto& innerSymbol = requireSymbol(output, *innerDecl_nn->m_varDefs[0]);
    require(innerSymbol.m_id != outerSymbol.m_id,
        "shadowing declarations should create distinct symbol identities");

    require(requireSymbol(output, requireSimpleLValExp(*innerExpStmt_nn->m_exp_nn)).m_id
            == innerSymbol.m_id,
        "nested scope lookup should prefer the innermost definition");
    require(requireSymbol(output, requireSimpleLValExp(*innerReturn_nn->m_exp_nn)).m_id
            == innerSymbol.m_id,
        "inner return should resolve to the innermost definition");
    require(requireSymbol(output, requireSimpleLValExp(*outerReturn_nn->m_exp_nn)).m_id
            == outerSymbol.m_id,
        "outer return should resolve to the outer definition");
}

void testUseBeforeDefinitionDiagnostic()
{
    const auto output = analyzeSource("int main(){value = 1; return 0;}");
    require(!output.success(), "use-before-definition should fail semantic analysis");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::useBeforeDefinition,
        "use-before-definition should report the expected semantic label");
}

void testDoubleDefinitionDiagnostic()
{
    const auto output = analyzeSource("int main(){int value; int value; return 0;}");
    require(!output.success(), "double definition should fail semantic analysis");
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
