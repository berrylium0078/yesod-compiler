#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

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

ast::Handle<ast::FuncDef> requireFuncDefByName(
    const ast::Handle<ast::CompUnit>& compUnit_nn, const std::string& name)
{
    require(compUnit_nn != nullptr, "expected compilation unit node");
    for (const auto topLevelItem_nn : compUnit_nn->m_topLevelItems) {
        ast::Handle<ast::FuncDef> funcDef_nn;
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType,
                                  ast::Handle<ast::FuncDef>>) {
                    funcDef_nn = topLevelAlt;
                }
            },
            topLevelItem_nn->m_topLevelItem);
        if (funcDef_nn != nullptr
            && funcDef_nn->m_identifier_nn->m_name == name) {
            return funcDef_nn;
        }
    }
    fail("expected function definition named '" + name + "'");
}

const ast::Exp::Call& requireCallExp(const ast::Handle<ast::Exp>& exp_nn)
{
    require(exp_nn != nullptr, "expected expression node");
    const auto* callExp = std::get_if<ast::Exp::Call>(&exp_nn->m_kind);
    require(callExp != nullptr, "expected call expression");
    return *callExp;
}

void testGlobalsAndFunctionCallsAnalyze()
{
    const auto output = analyzeRoot(
        "const int seed = 4; int counter = 2; int add(int lhs, int rhs)"
        "{return lhs + rhs;} int main(){counter = counter + 1; return "
        "add(seed, counter);}");

    const auto mainFunc_nn = requireFuncDefByName(output.m_root, "main");
    const auto returnStmt_nn
        = requireReturnStmt(requireStmtNode(mainFunc_nn->m_block_nn->m_blockItems[1]));
    const auto& callExp = requireCallExp(returnStmt_nn->m_exp_nn);

    require(callExp.m_params.size() == 2,
        "call expression should preserve both arguments");
    require(requireSymbol(output, callExp.m_func_nn).m_kind
            == ast::SemanticSymbolKind::function,
        "call callee should bind to a function symbol");
    require(requireExpValueKind(output, returnStmt_nn->m_exp_nn)
            == ExpType::integer,
        "int-returning call should produce an integer expression type");
    require(requireConstantValue(output, callExp.m_params[0]) == 4,
        "global const arguments should preserve their folded constant value");
    require(requireSymbol(output, callExp.m_params[1]).m_name == "counter",
        "global variable arguments should resolve through global scope");
}

void testVoidFunctionCallExpressionStatementAnalyzes()
{
    const auto output
        = analyzeRoot("void noop(){return;} int main(){noop(); return 0;}");

    const auto mainFunc_nn = requireFuncDefByName(output.m_root, "main");
    const auto expStmt_nn
        = requireExpStmt(requireStmtNode(mainFunc_nn->m_block_nn->m_blockItems[0]));
    require(requireExpValueKind(output, expStmt_nn->m_exp_nn)
            == ExpType::voidType,
        "void-returning calls should preserve a void expression type");
}

void testCallArityMismatchDiagnostic()
{
    const auto output
        = analyzeSource("int add(int lhs){return lhs;} int main(){return add(1, 2);}");
    require(!output.success(), "wrong-arity call should fail semantic analysis");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::callArityMismatch,
        "wrong-arity call should report the expected semantic label");
}

void testInvalidCallTargetDiagnostic()
{
    const auto output
        = analyzeSource("int value = 1; int main(){return value();}");
    require(!output.success(), "calling a variable should fail semantic analysis");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::invalidCallTarget,
        "calling a variable should report an invalid-call-target diagnostic");
}

void testNonConstantGlobalInitializerDiagnostic()
{
    const auto output = analyzeSource(
        "int add(int lhs){return lhs;} int value = add(1); int main(){return value;}");
    require(!output.success(),
        "non-constant global initializer should fail semantic analysis");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::nonConstantGlobalInitializer,
        "non-constant global initializer should report the expected semantic label");
}

void testReturnTypeMismatchDiagnostics()
{
    const auto voidValueReturn
        = analyzeSource("void noop(){return 1;} int main(){return 0;}");
    require(!voidValueReturn.success(),
        "void function returning a value should fail semantic analysis");
    require(firstDiagnostic(voidValueReturn).m_kind
            == SemanticDiagnosticKind::returnTypeMismatch,
        "void function returning a value should report return type mismatch");

    const auto missingIntReturn
        = analyzeSource("int main(){return;}\n");
    require(!missingIntReturn.success(),
        "int function returning no value should fail semantic analysis");
    require(firstDiagnostic(missingIntReturn).m_kind
            == SemanticDiagnosticKind::returnTypeMismatch,
        "missing int return value should report return type mismatch");
}

} // namespace

int main()
{
    testGlobalsAndFunctionCallsAnalyze();
    testVoidFunctionCallExpressionStatementAnalyzes();
    testCallArityMismatchDiagnostic();
    testInvalidCallTargetDiagnostic();
    testNonConstantGlobalInitializerDiagnostic();
    testReturnTypeMismatchDiagnostics();
    return 0;
}
