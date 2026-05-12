#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

std::shared_ptr<ast::Number> requireNumberExp(
    const std::shared_ptr<ast::Exp>& exp_nn)
{
    std::shared_ptr<ast::Number> number;
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::Number>>) {
                number = expAlt;
            }
        },
        exp_nn->m_kind);
    require(number != nullptr, "expected folded number semantic expression");
    return number;
}

void testConstExpressionsFoldToNumbers()
{
    const auto root_nn = analyzeRoot(
        "int main(){const int answer = 40 + 2; int value = answer + 1 * 2; return answer + 1 * 2;}");
    const auto& blockItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;

    std::shared_ptr<ast::DeclNode> constDeclNode_nn;
    std::shared_ptr<ast::DeclNode> varDeclNode_nn;
    std::shared_ptr<ast::StmtNode> returnNode_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                constDeclNode_nn = item;
            }
        },
        blockItems[0]->m_blockItem);
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                varDeclNode_nn = item;
            }
        },
        blockItems[1]->m_blockItem);
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                returnNode_nn = item;
            }
        },
        blockItems[2]->m_blockItem);

    std::shared_ptr<ast::ConstDecl> constDecl_nn;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ast::ConstDecl>>) {
                constDecl_nn = declAlt;
            }
        },
        constDeclNode_nn->m_decl);
    require(constDecl_nn != nullptr, "expected const declaration");
    require(requireNumberExp(constDecl_nn->m_constDefs[0]->m_initExp_nn)->m_value == 42,
        "const initializer should fold to a number literal");
    require(constDecl_nn->m_constDefs[0]->m_symbol_nn->m_hasConstantValue,
        "const symbol should cache its folded constant value");
    require(constDecl_nn->m_constDefs[0]->m_symbol_nn->m_constantValue == 42,
        "const symbol should record its folded constant value");

    std::shared_ptr<ast::VarDecl> varDecl_nn;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::VarDecl>>) {
                varDecl_nn = declAlt;
            }
        },
        varDeclNode_nn->m_decl);
    require(varDecl_nn != nullptr, "expected var declaration");
    require(requireNumberExp(varDecl_nn->m_varDefs[0]->m_initExp_nn)->m_value == 44,
        "var initializer should fold const-backed arithmetic to a number literal");

    std::shared_ptr<ast::ReturnStmt> returnStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ast::ReturnStmt>>) {
                returnStmt_nn = stmtAlt;
            }
        },
        returnNode_nn->m_stmt);
    require(returnStmt_nn != nullptr, "expected return statement");
    require(requireNumberExp(returnStmt_nn->m_exp_nn)->m_value == 44,
        "return expression should fold const-backed arithmetic to a number literal");
}

void testConstSemanticDiagnostics()
{
    const auto nonConstantInit
        = analyzeSource("int main(){int value = 1; const int answer = value; return answer;}");
    require(!nonConstantInit.success(),
        "non-constant const initializer should fail semantic analysis");
    require(firstDiagnostic(nonConstantInit).m_kind
            == SemanticDiagnosticKind::nonConstantConstInitializer,
        "non-constant const initializer should report the expected semantic label");

    const auto assignToConst
        = analyzeSource("int main(){const int answer = 1; answer = 2; return answer;}");
    require(!assignToConst.success(),
        "assignment to const should fail semantic analysis");
    require(firstDiagnostic(assignToConst).m_kind
            == SemanticDiagnosticKind::assignToConst,
        "assignment to const should report the expected semantic label");
}

} // namespace

int main()
{
    testConstExpressionsFoldToNumbers();
    testConstSemanticDiagnostics();
    return 0;
}
