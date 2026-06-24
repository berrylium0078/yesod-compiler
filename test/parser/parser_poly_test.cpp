#include "parser_test_support.h"

using namespace yesod::test_support::parser;

namespace {

struct ParserPolyTest : ParserTestBase {
    // poly local variable declaration with poly(exp) construction
    void testPolyVarDeclAndCast()
    {
        parseRoot("int main() {"
                  "  poly a = poly(42);"
                  "  poly b = poly(3);"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl1 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[0]));
        require(decl1(ast()).bType == BTypeKeyword::polyKeyword,
            "poly variable declaration should parse");
        require(decl1(ast()).varDef.front()(ast()).initVal != nullptr,
            "poly variable initializer should be present");

        const auto* initExp1 = std::get_if<Ref<Exp>>(
            &decl1(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp1 != nullptr,
            "poly variable initializer should be scalar exp");
        const auto& cast1 = requireCastExp(*initExp1);
        require(cast1.targetType == BTypeKeyword::polyKeyword,
            "poly cast target type should parse");

        const auto decl2 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[1]));
        const auto* initExp2 = std::get_if<Ref<Exp>>(
            &decl2(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(
            initExp2 != nullptr, "second poly initializer should be scalar");
        const auto& cast2 = requireCastExp(*initExp2);
        require(cast2.targetType == BTypeKeyword::polyKeyword,
            "second poly cast target type should parse");
    }

    // !poly still parses as a unary expression; semantic analysis rejects it.
    void testPolyBangSyntax()
    {
        parseRoot("int main() {"
                  "  poly p = poly(7);"
                  "  int len = !p;"
                  "  return len;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl2 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[1]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl2(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "bang initializer should be scalar");
        const auto& unary = requireUnaryExp(*initExp);
        require(unary.op == UnaryOpKeyword::bang,
            "bang should parse as a unary operator");
        const auto& lVal = requireLVal(unary.lhs);
        require(lVal.indices.empty(), "bang lval should have no indices");
    }

    // poly[k] coefficient extraction on an LVal
    void testPolyCoeffExtract()
    {
        parseRoot("int main() {"
                  "  poly p = poly(7);"
                  "  mint c = p[2];"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl2 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[1]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl2(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "coefficient initializer should be scalar");

        const auto& lVal = requireLVal(*initExp);
        require(lVal.indices.size() == 1,
            "poly[k] should produce one subscript index");
        const auto& index = requireNumber(lVal.indices[0]);
        require(index.value == 2, "poly[2] subscript index should be 2");
    }

    // p[n,m] slice syntax
    void testPolySlice()
    {
        parseRoot("int main() {"
                  "  poly p = poly(7);"
                  "  poly s = p[1, 5];"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl2 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[1]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl2(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "slice initializer should be scalar");

        const auto& slice = requireSliceExp(*initExp);
        requireLVal(slice.base);

        const auto& start = requireNumber(slice.start);
        require(start.value == 1, "slice start index should be 1");
        const auto& end = requireNumber(slice.end);
        require(end.value == 5, "slice end index should be 5");
    }

    // (a + b)[1] — coefficient extraction on parenthesized binary expression.
    // NOTE: This syntax requires PrimaryExp "[" Exp "]" grammar support,
    // which is not yet implemented in the parser (only LVal-based subscripts
    // are supported). This test documents the current limitation.
    // void testCoeffOnParenBinary() { }

    // (a + b)[1, n + m] — slice on parenthesized binary expression.
    // NOTE: Same limitation as above — the parser does not yet support
    // post-PrimaryExp slice syntax.
    // void testSliceOnParenBinary() { }

    // poly >> k shift expression
    void testPolyShift()
    {
        parseRoot("int main() {"
                  "  poly p = poly(7);"
                  "  poly r = p >> 2;"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl2 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[1]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl2(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "shift initializer should be scalar");

        const auto& binary = requireBinaryExp(*initExp);
        require(binary.op == BinaryOpKeyword::sar,
            ">> should parse as sar binary op");
        const auto& lhsLVal = requireLVal(binary.lhs);
        require(lhsLVal.indices.empty(), "shift lhs should be simple lval");
        const auto& rhsNumber = requireNumber(binary.rhs);
        require(rhsNumber.value == 2, "shift rhs should be 2");
    }

    // poly << k shift expression
    void testPolyLeftShift()
    {
        parseRoot("int main() {"
                  "  poly p = poly(7);"
                  "  poly r = p << 3;"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl2 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[1]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl2(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "left shift initializer should be scalar");

        const auto& binary = requireBinaryExp(*initExp);
        require(binary.op == BinaryOpKeyword::shl,
            "<< should parse as shl binary op");
    }

    // poly binary arithmetic (poly + poly)
    void testPolyArithmetic()
    {
        parseRoot("int main() {"
                  "  poly a = poly(1);"
                  "  poly b = poly(2);"
                  "  poly s = a + b;"
                  "  poly d = a - b;"
                  "  poly m = a * b;"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);

        const auto decl3 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[2]));
        const auto* init3 = std::get_if<Ref<Exp>>(
            &decl3(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(init3 != nullptr, "poly+poly initializer");
        require(requireBinaryExp(*init3).op == BinaryOpKeyword::plus,
            "a + b should parse as plus");

        const auto decl4 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[3]));
        const auto* init4 = std::get_if<Ref<Exp>>(
            &decl4(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(init4 != nullptr, "poly-poly initializer");
        require(requireBinaryExp(*init4).op == BinaryOpKeyword::minus,
            "a - b should parse as minus");

        const auto decl5 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[4]));
        const auto* init5 = std::get_if<Ref<Exp>>(
            &decl5(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(init5 != nullptr, "poly*poly initializer");
        require(requireBinaryExp(*init5).op == BinaryOpKeyword::star,
            "a * b should parse as star");
    }

    // poly(exp) with non-constant expression inside
    void testPolyCastWithIntExpr()
    {
        parseRoot("int main() {"
                  "  int x = 42;"
                  "  poly p = poly(x);"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl2 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[1]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl2(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "poly cast initializer should be scalar");

        const auto& cast = requireCastExp(*initExp);
        require(cast.targetType == BTypeKeyword::polyKeyword,
            "poly(x) cast target should be poly");
        // The inner expression should be an LVal referencing 'x'
        const auto& innerLVal = requireLVal(cast.value);
        require(innerLVal.identifier(ast()).name == "x",
            "poly(x) value should reference variable x");
    }

    // poly slice with variable start/end indices
    void testPolySliceWithVars()
    {
        parseRoot("int main() {"
                  "  poly p = poly(7);"
                  "  int n = 1;"
                  "  int m = 5;"
                  "  poly s = p[n, m];"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl4 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[3]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl4(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "slice-with-vars initializer");

        const auto& slice = requireSliceExp(*initExp);
        // Base should be LVal 'p'
        const auto& baseLVal = requireLVal(slice.base);
        require(
            baseLVal.identifier(ast()).name == "p", "slice base should be p");
        // Start should be LVal 'n'
        const auto& startLVal = requireLVal(slice.start);
        require(
            startLVal.identifier(ast()).name == "n", "slice start should be n");
        // End should be LVal 'm'
        const auto& endLVal = requireLVal(slice.end);
        require(endLVal.identifier(ast()).name == "m", "slice end should be m");
    }

    // err: poly cast with missing ')'
    void testPolyCastMissingRParen()
    {
        parseSource("int main() {"
                    "  poly p = poly(42;"
                    "  return 0;"
                    "}");

        require(!success(), "poly cast missing ')' should fail");
        require(isDiagnostic<MissingCastRParenDiagnostic>(firstDiagnostic()),
            "missing ')' after poly cast should report cast delimiter "
            "diagnostic");
    }

    // err: poly slice with missing bracket
    void testPolySliceMissingBracket()
    {
        parseSource("int main() {"
                    "  poly p = poly(1);"
                    "  poly s = p[1, 5;"
                    "  return 0;"
                    "}");

        // The parser will try to parse p[1 as an LVal subscript, then
        // encounter ,5; and produce diagnostics.
        require(!success(), "malformed slice should fail parsing");
    }

    // (a + b)[k] — subscript on parenthesized binary expression
    void testSubscriptOnParenExpr()
    {
        parseRoot("int main() {"
                  "  poly a = poly(1);"
                  "  poly b = poly(2);"
                  "  mint c = (a + b)[3];"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl3 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[2]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl3(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "coeff-on-paren initializer");

        const auto& subscript = requireSubscriptExp(*initExp);
        // base should be a parenthesized binary expression (a + b)
        const auto& binary = requireBinaryExp(subscript.base);
        require(binary.op == BinaryOpKeyword::plus,
            "subscript base should be binary +");
        const auto& index = requireNumber(subscript.index);
        require(index.value == 3, "subscript index should be 3");
    }

    // (a + b)[1, n + m] — slice on parenthesized binary expression
    void testSliceOnParenExpr()
    {
        parseRoot("int main() {"
                  "  poly a = poly(1);"
                  "  poly b = poly(2);"
                  "  int n = 1;"
                  "  poly s = (a + b)[1, n + 5];"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl4 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[3]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl4(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "slice-on-paren initializer");

        const auto& slice = requireSliceExp(*initExp);
        // base should be a parenthesized binary expression (a + b)
        requireBinaryExp(slice.base);
        const auto& start = requireNumber(slice.start);
        require(start.value == 1, "slice start should be 1");
        // end should be binary expression n + 5
        const auto& endBinary = requireBinaryExp(slice.end);
        require(endBinary.op == BinaryOpKeyword::plus,
            "slice end should be binary +");
    }

    // Chained: (a + b)[1,5][2] — slice then subscript on paren expr
    void testSliceThenSubscriptOnParenExpr()
    {
        parseRoot("int main() {"
                  "  poly a = poly(1);"
                  "  poly b = poly(2);"
                  "  mint c = (a + b)[1, 5][2];"
                  "  return 0;"
                  "}");

        const auto& mainFunc
            = *std::get_if<Ref<FuncDef>>(&root()(ast()).topLevelItems[0]);
        const auto decl3 = extractVarDecl(
            extractDeclNode(mainFunc(ast()).body(ast()).items[2]));
        const auto* initExp = std::get_if<Ref<Exp>>(
            &decl3(ast()).varDef.front()(ast()).initVal(ast()).kind);
        require(initExp != nullptr, "chained init");

        // Should be: Subscript(Slice(ParenExpr, 1, 5), 2)
        const auto& outerSubscript = requireSubscriptExp(*initExp);
        require(requireNumber(outerSubscript.index).value == 2,
            "outer subscript index should be 2");
        const auto& innerSlice = requireSliceExp(outerSubscript.base);
        require(requireNumber(innerSlice.start).value == 1,
            "inner slice start should be 1");
        require(requireNumber(innerSlice.end).value == 5,
            "inner slice end should be 5");
        requireBinaryExp(innerSlice.base);
    }
};

} // namespace

int main()
{
    ParserPolyTest test;
    try {
        test.testPolyVarDeclAndCast();
        test.testPolyBangSyntax();
        test.testPolyCoeffExtract();
        test.testPolySlice();
        test.testPolyShift();
        test.testPolyLeftShift();
        test.testPolyArithmetic();
        test.testPolyCastWithIntExpr();
        test.testPolySliceWithVars();
        test.testPolyCastMissingRParen();
        test.testPolySliceMissingBracket();
        test.testSubscriptOnParenExpr();
        test.testSliceOnParenExpr();
        test.testSliceThenSubscriptOnParenExpr();
    } catch (std::exception& e) {
        fail(e.what());
    }
    return 0;
}
