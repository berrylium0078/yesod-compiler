#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserMintTest : ParserTestBase {
    void testMintTypesAndCastParse()
    {
        parseRoot("mint id(mint x){return x;} int main(){mint value = mint(42); return int(value);}");

        const auto funcDef_nn = firstFuncDef();
        require(funcDef_nn(ast()).m_funcType == FuncTypeKeyword::mintKeyword,
            "mint function type should parse");
        require(funcDef_nn(ast()).funcFParams.size() == 1,
            "mint function parameter should parse");
        require(funcDef_nn(ast()).funcFParams.front().bType
                == BTypeKeyword::mintKeyword,
            "mint parameter base type should parse");

        const auto compUnit_nn = root();
        require(compUnit_nn, "expected compilation unit root");
        const auto mainFunc_nn
            = *std::get_if<Ref<FuncDef>>(&compUnit_nn(ast()).topLevelItems[1]);
        const auto valueDecl_nn = extractVarDecl(
            extractDeclNode(mainFunc_nn(ast()).body(ast()).items[0]));
        require(valueDecl_nn(ast()).bType == BTypeKeyword::mintKeyword,
            "mint variable declaration should parse");
        require(valueDecl_nn(ast()).varDef.front()(ast()).initVal != nullptr,
            "mint variable initializer should be present");
        const auto* initExp_nn = std::get_if<Ref<Exp>>(
            &valueDecl_nn(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp_nn != nullptr,
            "mint variable initializer should be scalar");
        const auto& initCast = requireCastExp(*initExp_nn);
        require(initCast.targetType == BTypeKeyword::mintKeyword,
            "mint cast target type should parse");

        const auto returnStmt_nn
            = extractReturnStmt(mainFunc_nn(ast()).body(ast()).items[1]);
        const auto& returnCast = requireCastExp(returnStmt_nn(ast()).exp.ref());
        require(returnCast.targetType == BTypeKeyword::intKeyword,
            "int cast target type should parse");
    }

    void testCastPrecedence()
    {
        parseRoot("int main(){return mint(1) + mint(2);}");
        const auto returnStmt_nn = extractReturnStmt(
            firstFuncDef()(ast()).body(ast()).items.front());
        const auto& binary = requireBinaryExp(returnStmt_nn(ast()).exp.ref());
        requireCastExp(binary.lhs);
        requireCastExp(binary.rhs);
    }

    void testCastRecoveryDiagnostics()
    {
        parseSource("int main(){return mint(;}");
        require(!success(), "missing cast operand should fail parsing");
        require(isDiagnostic<MalformedCastValueDiagnostic>(firstDiagnostic()),
            "missing cast operand should report a cast-value diagnostic");

        parseSource("int main(){return mint(1;}");
        require(!success(), "missing ')' after cast should produce a diagnostic");
        require(root(), "missing ')' after cast should still recover the root");
        require(isDiagnostic<MissingCastRParenDiagnostic>(firstDiagnostic()),
            "missing ')' after cast should report the cast delimiter diagnostic");
        const auto returnStmt_nn = extractReturnStmt(
            firstFuncDef()(ast()).body(ast()).items.front());
        require(requireCastExp(returnStmt_nn(ast()).exp.ref()).targetType
                == BTypeKeyword::mintKeyword,
            "cast recovery should preserve the parsed target type");
    }
};

} // namespace

int main()
{
    ParserMintTest test;
    try {
        test.testMintTypesAndCastParse();
        test.testCastPrecedence();
        test.testCastRecoveryDiagnostics();
    } catch (std::exception& e) {
        fail(e.what());
    }
    return 0;
}
