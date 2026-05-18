#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

constexpr const char* kFunctionArrayParamSource =
    "void f1(int n, int a[]) {"
    "while (n > 0) {"
    "n = n - 1;"
    "a[n] = 0;"
    "}"
    "}"
    "void f2(int n, int a[][10]) {"
    "while (n > 0) {"
    "n = n - 1;"
    "f1(10, a[n]);"
    "}"
    "}"
    "void f3(int n, int a[][10][10]) {"
    "while (n > 0) {"
    "n = n - 1;"
    "f2(10, a[n]);"
    "}"
    "}"
    "int main() {"
    "int a[10][10][10];"
    "f3(10, a);"
    "}";

constexpr const char* kShadowedConstArraySource =
    "const int garr[10] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15};"
    "int main(){"
    "const int arr[10] = {1, 2, 3, 4, 5};"
    "int i = 0, sum = 0;"
    "while(i < 10){"
    "sum = sum + arr[i] + garr[i];"
    "i = i + 1;"
    "}"
    "const int garr[10] = {1};"
    "return sum + garr[0];"
    "}";

constexpr const char* kArrayInitializerExpressionSource =
    "const int a = 2, b = 3;"
    "const int c[2] = {a + b, a - b};"
    "int d[2][2] = {a + b, a - b, {a * 2, b * 2}};";

constexpr const char* kNonConstantConstArrayInitializerSource =
    "int a = 2, b = 3;"
    "const int c[2] = {a + b, a - b};";

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

ast::Handle<ast::DeclNode> requireTopLevelDeclNode(
    const ast::Handle<ast::TopLevelItemNode>& topLevelItem_nn)
{
    ast::Handle<ast::DeclNode> declNode_nn;
    std::visit(
        [&](const auto& topLevelAlt) {
            using AltType = std::decay_t<decltype(topLevelAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::DeclNode>>) {
                declNode_nn = topLevelAlt;
            }
        },
        topLevelItem_nn->m_topLevelItem);
    require(declNode_nn != nullptr, "expected top-level declaration");
    return declNode_nn;
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

ast::Handle<ast::ConstDecl> requireConstDecl(
    const ast::Handle<ast::DeclNode>& declNode_nn)
{
    ast::Handle<ast::ConstDecl> constDecl_nn;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::ConstDecl>>) {
                constDecl_nn = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(constDecl_nn != nullptr, "expected const declaration");
    return constDecl_nn;
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

ast::Handle<ast::WhileStmt> requireWhileStmt(
    const ast::Handle<ast::StmtNode>& stmtNode_nn)
{
    ast::Handle<ast::WhileStmt> whileStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, ast::Handle<ast::WhileStmt>>) {
                whileStmt_nn = stmtAlt;
            }
        },
        stmtNode_nn->m_stmt);
    require(whileStmt_nn != nullptr, "expected while statement");
    return whileStmt_nn;
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

ast::Exp::Binary requireBinaryExp(const ast::Handle<ast::Exp>& exp_nn)
{
    require(exp_nn != nullptr, "expected expression node");
    require(std::holds_alternative<ast::Exp::Binary>(exp_nn->m_kind),
        "expected binary expression");
    return std::get<ast::Exp::Binary>(exp_nn->m_kind);
}

ast::Handle<ast::Exp> requireLValExp(const ast::Handle<ast::Exp>& exp_nn)
{
    require(exp_nn != nullptr, "expected expression node");
    require(std::holds_alternative<ast::LVal>(exp_nn->m_kind),
        "expected lvalue expression");
    return exp_nn;
}

void testArraySymbolsAndArrayParameterCalls()
{
    const auto output = analyzeSource(
        "int take(int a[]){return a[0];} int main(){int arr[2]; arr[1] = 7; return take(arr);}"
    );
    require(output.success(), "array parameter call should pass semantic analysis");

    const auto takeFunc_nn = firstFuncDef(output.m_root);
    const auto& takeSymbol = requireSymbol(output, takeFunc_nn->m_funcFParams[0]->m_identifier_nn);
    require(takeSymbol.m_type.isArray(),
        "array parameter should preserve an array semantic type");
    require(takeSymbol.m_type.m_arrayLength == -1,
        "array parameter should preserve the unsized first dimension");

    ast::Handle<ast::FuncDef> mainFunc_nn;
    for (const auto topLevelItem_nn : output.m_root->m_topLevelItems) {
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType, ast::Handle<ast::FuncDef>>) {
                    if (topLevelAlt->m_identifier_nn->m_name == "main") {
                        mainFunc_nn = topLevelAlt;
                    }
                }
            },
            topLevelItem_nn->m_topLevelItem);
    }
    require(mainFunc_nn != nullptr, "expected main function definition");

    const auto varDecl_nn = requireVarDecl(requireDeclNode(mainFunc_nn->m_block_nn->m_blockItems[0]));
    const auto& arraySymbol = requireSymbol(output, varDecl_nn->m_varDefs[0]);
    require(arraySymbol.m_type.isArray(),
        "local array declaration should preserve an array semantic type");
    require(arraySymbol.m_type.m_arrayLength == 2,
        "local array declaration should preserve its constant length");

    const auto returnStmt_nn = requireReturnStmt(requireStmtNode(mainFunc_nn->m_block_nn->m_blockItems[2]));
    require(requireExpValueKind(output, returnStmt_nn->m_exp_nn) == ExpType::integer,
        "call through an array parameter should still produce an integer value");
}

void testConstArrayReadsDoNotFoldAsConstInitializers()
{
    const auto output = analyzeSource(
        "int main(){const int arr[2] = {1, 2}; const int value = arr[1]; return value;}"
    );
    require(!output.success(),
        "const array element reads should not participate in constant folding");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::nonConstantConstInitializer,
        "const array element read should report the non-constant const initializer label");
}

void testShadowedArrayNamesResolveToBoundSymbols()
{
    const auto output = analyzeSource(kShadowedConstArraySource);
    require(output.success(),
        "shadowed const arrays should still pass semantic analysis");

    const auto globalConstDecl_nn
        = requireConstDecl(requireTopLevelDeclNode(output.m_root->m_topLevelItems[0]));
    const auto& globalGarrSymbol = requireSymbol(output, globalConstDecl_nn->m_constDefs[0]);

    ast::Handle<ast::FuncDef> mainFunc_nn;
    for (const auto topLevelItem_nn : output.m_root->m_topLevelItems) {
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType, ast::Handle<ast::FuncDef>>) {
                    if (topLevelAlt->m_identifier_nn->m_name == "main") {
                        mainFunc_nn = topLevelAlt;
                    }
                }
            },
            topLevelItem_nn->m_topLevelItem);
    }
    require(mainFunc_nn != nullptr, "expected main function definition");

    const auto localConstDecl_nn = requireConstDecl(
        requireDeclNode(mainFunc_nn->m_block_nn->m_blockItems[3]));
    const auto& localGarrSymbol = requireSymbol(output, localConstDecl_nn->m_constDefs[0]);
    require(localGarrSymbol.m_id != globalGarrSymbol.m_id,
        "shadowed const arrays should bind distinct symbol identities");

    const auto whileStmt_nn = requireWhileStmt(
        requireStmtNode(mainFunc_nn->m_block_nn->m_blockItems[2]));
    const auto whileBody_nn = requireBlockStmt(whileStmt_nn->m_bodyStmt_nn);
    const auto sumAssign_nn = requireAssignStmt(
        requireStmtNode(whileBody_nn->m_blockItems[0]));
    const auto sumBinary = requireBinaryExp(sumAssign_nn->m_exp_nn);
    require(requireSymbol(output, requireLValExp(sumBinary.m_rhs_nn)).m_id
            == globalGarrSymbol.m_id,
        "garr[i] inside the loop should resolve to the global const array");

    const auto returnStmt_nn = requireReturnStmt(
        requireStmtNode(mainFunc_nn->m_block_nn->m_blockItems[4]));
    const auto returnBinary = requireBinaryExp(returnStmt_nn->m_exp_nn);
    require(requireSymbol(output, requireLValExp(returnBinary.m_rhs_nn)).m_id
            == localGarrSymbol.m_id,
        "garr[0] after the local declaration should resolve to the shadowing const array");
}

void testConstArrayAssignmentIsRejected()
{
    const auto output = analyzeSource(
        "int main(){const int arr[2] = {1, 2}; arr[1] = 3; return 0;}");
    require(!output.success(),
        "assigning through a const array element should fail semantic analysis");
    require(firstDiagnostic(output).m_kind == SemanticDiagnosticKind::assignToConst,
        "const array assignment should report the assign-to-const semantic label");
}

void testNestedFunctionArrayParametersAnalyze()
{
    const auto output = analyzeSource(kFunctionArrayParamSource);
    require(output.success(),
        "nested function array parameters should pass semantic analysis");

    std::vector<ast::Handle<ast::FuncDef>> funcs;
    for (const auto topLevelItem_nn : output.m_root->m_topLevelItems) {
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType, ast::Handle<ast::FuncDef>>) {
                    funcs.push_back(topLevelAlt);
                }
            },
            topLevelItem_nn->m_topLevelItem);
    }
    require(funcs.size() == 4,
        "function-array-parameter sample should analyze four function definitions");

    const auto& f1ArrayParam
        = requireSymbol(output, funcs[0]->m_funcFParams[1]->m_identifier_nn);
    require(f1ArrayParam.m_type.isArray() && f1ArrayParam.m_type.m_arrayLength == -1,
        "f1 parameter should preserve the unsized array semantic type");
    require(f1ArrayParam.m_type.m_elementType != nullptr
            && f1ArrayParam.m_type.m_elementType->m_kind == ast::SemanticTypeKind::integer,
        "f1 parameter element type should be integer");

    const auto& f2ArrayParam
        = requireSymbol(output, funcs[1]->m_funcFParams[1]->m_identifier_nn);
    require(f2ArrayParam.m_type.isArray() && f2ArrayParam.m_type.m_arrayLength == -1,
        "f2 parameter should preserve the unsized array semantic type");
    require(f2ArrayParam.m_type.m_elementType != nullptr
            && f2ArrayParam.m_type.m_elementType->isArray()
            && f2ArrayParam.m_type.m_elementType->m_arrayLength == 10,
        "f2 parameter should preserve one trailing array extent");

    const auto& f3ArrayParam
        = requireSymbol(output, funcs[2]->m_funcFParams[1]->m_identifier_nn);
    require(f3ArrayParam.m_type.isArray() && f3ArrayParam.m_type.m_arrayLength == -1,
        "f3 parameter should preserve the unsized array semantic type");
    require(f3ArrayParam.m_type.m_elementType != nullptr
            && f3ArrayParam.m_type.m_elementType->isArray()
            && f3ArrayParam.m_type.m_elementType->m_arrayLength == 10
            && f3ArrayParam.m_type.m_elementType->m_elementType != nullptr
            && f3ArrayParam.m_type.m_elementType->m_elementType->isArray()
            && f3ArrayParam.m_type.m_elementType->m_elementType->m_arrayLength == 10,
        "f3 parameter should preserve both trailing array extents");
}

void testArrayInitializersAcceptExpressions()
{
    const auto output = analyzeSource(kArrayInitializerExpressionSource);
    require(output.success(),
        "array initializers should accept expression elements when the required constness is satisfied");

    const auto topLevelDecl0 = requireConstDecl(
        requireTopLevelDeclNode(output.m_root->m_topLevelItems[0]));
    const auto topLevelDecl1 = requireConstDecl(
        requireTopLevelDeclNode(output.m_root->m_topLevelItems[1]));
    const auto topLevelDecl2 = requireVarDecl(
        requireTopLevelDeclNode(output.m_root->m_topLevelItems[2]));

    const auto& cSymbol = requireSymbol(output, topLevelDecl1->m_constDefs[0]);
    require(cSymbol.m_type.isArray() && cSymbol.m_type.m_arrayLength == 2,
        "const array expression initializer should preserve the declared array type");

    const auto& dSymbol = requireSymbol(output, topLevelDecl2->m_varDefs[0]);
    require(dSymbol.m_type.isArray() && dSymbol.m_type.m_arrayLength == 2,
        "mutable array expression initializer should preserve the outer array length");
    require(dSymbol.m_type.m_elementType != nullptr
            && dSymbol.m_type.m_elementType->isArray()
            && dSymbol.m_type.m_elementType->m_arrayLength == 2,
        "mutable array expression initializer should preserve the inner array length");

    (void)topLevelDecl0;
}

void testConstArrayInitializersRejectNonConstantExpressions()
{
    const auto output = analyzeSource(kNonConstantConstArrayInitializerSource);
    require(!output.success(),
        "const array initializers should reject non-constant expression elements");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::nonConstantConstInitializer,
        "non-constant const array initializer should report the dedicated semantic label");
}

} // namespace

int main()
{
    testArraySymbolsAndArrayParameterCalls();
    testConstArrayReadsDoNotFoldAsConstInitializers();
    testShadowedArrayNamesResolveToBoundSymbols();
    testConstArrayAssignmentIsRejected();
    testNestedFunctionArrayParametersAnalyze();
    testArrayInitializersAcceptExpressions();
    testConstArrayInitializersRejectNonConstantExpressions();
    return 0;
}