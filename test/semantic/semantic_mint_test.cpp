#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

struct SemanticMintTest : SemanticTestBase {
    void testExplicitMintConversionsAnalyze()
    {
        m_output = analyzeRoot(
            "mint id(mint x){return x;} int main(){mint value = mint(7); return int(value);}"
        );

        const auto mainFunc_nn = requireFuncDefByName(root(), "main");
        const auto returnStmt_nn
            = extractReturnStmt(mainFunc_nn(ast()).body(ast()).items[1]);
        const auto& returnCast = requireCastExp(returnStmt_nn(ast()).exp.ref());
        require(returnCast.targetType == BTypeKeyword::intKeyword,
            "return expression should preserve explicit int cast");
        require(requireExpValueKind(m_output, returnStmt_nn(ast()).exp.ref())
                == ExpType::integer,
            "int cast should produce an integer expression kind");
    }

    void testImplicitIntToMintIsRejected()
    {
        m_output = analyzeSource("int main(){mint value = 1; return 0;}");
        require(!success(), "implicit int-to-mint initialization should fail");
        require(isDiagnostic<TypeMismatchDiagnostic>(firstDiagnostic()),
            "implicit int-to-mint initialization should report a type mismatch");
    }

    void testImplicitMintToIntIsRejected()
    {
        m_output = analyzeSource(
            "int main(){mint value = mint(1); return value;}"
        );
        require(!success(), "implicit mint-to-int return should fail");
        require(isDiagnostic<ReturnTypeMismatchDiagnostic>(firstDiagnostic()),
            "implicit mint-to-int return should report a return mismatch");
    }

    void testArrayElementTypeMismatchIsRejected()
    {
        m_output = analyzeSource(
            "void takesMint(mint a[]); int main(){int values[2]; takesMint(values); return 0;}"
        );
        require(!success(), "array element type mismatch should fail");
        require(isDiagnostic<TypeMismatchDiagnostic>(firstDiagnostic()),
            "array element type mismatch should require element-wise conversion");
    }

    void testMintConditionRequiresExplicitIntCast()
    {
        m_output = analyzeSource(
            "int main(){mint value = mint(1); if(value){return 1;} return 0;}"
        );
        require(!success(), "mint condition without explicit int cast should fail");
        require(isDiagnostic<TypeMismatchDiagnostic>(firstDiagnostic()),
            "mint condition should report an integer-condition mismatch");
    }

    void testMintBitwiseOperatorsAreRejected()
    {
        m_output = analyzeSource(
            "int main(){mint value = mint(1); return int(~value);}" 
        );
        require(!success(), "mint bitwise not should fail semantic analysis");
        require(isDiagnostic<TypeMismatchDiagnostic>(firstDiagnostic()),
            "mint bitwise not should report a type mismatch");

        m_output = analyzeSource(
            "int main(){mint lhs = mint(1); mint rhs = mint(2); return int(lhs << rhs);}"
        );
        require(!success(), "mint shifts should fail semantic analysis");
        require(isDiagnostic<TypeMismatchDiagnostic>(firstDiagnostic()),
            "mint shifts should report a type mismatch");
    }
};

} // namespace

int main()
{
    SemanticMintTest test;
    test.testExplicitMintConversionsAnalyze();
    test.testImplicitIntToMintIsRejected();
    test.testImplicitMintToIntIsRejected();
    test.testArrayElementTypeMismatchIsRejected();
    test.testMintConditionRequiresExplicitIntCast();
    test.testMintBitwiseOperatorsAreRejected();
    return 0;
}
