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

ast::Handle<ast::ConstDecl> requireConstDecl(
    const ast::Handle<ast::DeclNode>& declNode_nn)
{
    ast::Handle<ast::ConstDecl> constDecl_nn;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType,
                              ast::Handle<ast::ConstDecl>>) {
                constDecl_nn = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(constDecl_nn != nullptr, "expected const declaration");
    return constDecl_nn;
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

void testConstExpressionsRecordFoldedValues()
{
    const auto output
        = analyzeSource("int main(){const int answer = 40 + 2; int value = "
                        "answer + 1 * 2; return answer + 1 * 2;}");
    require(output.success(), "expected semantic success");

    const auto funcDef_nn = firstFuncDef(output.m_root);
    const auto& blockItems
        = funcDef_nn->m_block_nn->m_blockItems;
    const auto constDecl_nn = requireConstDecl(requireDeclNode(blockItems[0]));
    const auto varDecl_nn = requireVarDecl(requireDeclNode(blockItems[1]));
    const auto returnStmt_nn
        = requireReturnStmt(requireStmtNode(blockItems[2]));

    const auto constExp
        = constDecl_nn->m_constDefs[0]->m_constInitVal_nn->m_exp_nn;
    require(requireConstantValue(output, constExp) == 42,
        "const initializer should fold to a constant value");

    const auto& constSymbol
        = requireSymbol(output, constDecl_nn->m_constDefs[0]);
    require(constSymbol.m_hasConstantValue,
        "const declaration should cache its folded constant value");
    require(constSymbol.m_constantValue == 42,
        "const declaration should expose the folded constant value");

    const auto varInitExp = varDecl_nn->m_varDefs[0]->m_initVal_nn->m_exp_nn;
    require(requireConstantValue(output, varInitExp) == 44,
        "var initializer should fold const-backed integer");

    require(requireConstantValue(output, returnStmt_nn->m_exp_nn) == 44,
        "return expression should fold const-backed integer");
}

void testConstSemanticDiagnostics()
{
    const auto nonConstantInit = analyzeSource(
        "int main(){int value = 1; const int answer = value; return answer;}");
    require(!nonConstantInit.success(),
        "non-constant const initializer should fail semantic analysis");
    require(firstDiagnostic(nonConstantInit).m_kind
            == SemanticDiagnosticKind::nonConstantConstInitializer,
        "non-constant const initializer should report the expected semantic "
        "label");

    const auto assignToConst = analyzeSource(
        "int main(){const int answer = 1; answer = 2; return answer;}");
    require(!assignToConst.success(),
        "assignment to const should fail semantic analysis");
    require(firstDiagnostic(assignToConst).m_kind
            == SemanticDiagnosticKind::assignToConst,
        "assignment to const should report the expected semantic label");
}

} // namespace

int main()
{
    testConstExpressionsRecordFoldedValues();
    testConstSemanticDiagnostics();
    return 0;
}
