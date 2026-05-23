#include "koopa_test_support.h"

using namespace yesod::test_support::koopa;

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

constexpr const char* kBuiltinArrayDeclSource =
    "int main(){int a[2]; putarray(2, a); return getarray(a);}";

constexpr const char* kMixedBraceArrayInitializerSource =
    "int a[3][3] = {{0, 1}, {2, 3}};"
    "int b[3][3] = {0, 1, 2, {3}, 4, 5, 6};"
    "int main(){return 0;}";

constexpr const char* kRecursiveArrayInitializerSource =
    "const int N = 3;"
    "int a[N][N][N] = {0, 1, 2, {3}, 4};"
    "const int b[N][N][N] = {0, 1, 2, {3}, 4};"
    "int main(){return 0;}";

constexpr const char* kStressGeneratedArrayInitializerSource =
    "const int N1 = 6;"
    "const int N2 = 3;"
    "const int N3 = 7;"
    "const int N4 = 5;"
    "const int N5 = 1;"
    "int a[N1][N2][N3][N4][N5] = {{}, {48, {19, 53, {{20, 4}, 55, 8}}, 38, {3, 54}}, 34, 35, 56};"
    "int main(){return 0;}";

const BasicBlock* requireBlockNameContains(
    const Function& function, const std::string& infix)
{
    for (size_t bbIndex = 0; bbIndex < function.getNumBBs(); ++bbIndex) {
        const auto* basicBlock = function.getBB(bbIndex);
        if (basicBlock->getName().find(infix) != std::string::npos) {
            return basicBlock;
        }
    }
    fail("expected basic body name containing '" + infix + "'");
}

const AllocValue* requireAllocByName(
    const BasicBlock& basicBlock, const std::string& expectedName)
{
    for (size_t instIndex = 0; instIndex < basicBlock.getNumInsts(); ++instIndex) {
        const auto* allocValue
            = dynamic_cast<const AllocValue*>(basicBlock.getInst(instIndex));
        if (allocValue != nullptr && allocValue->getName() == expectedName) {
            return allocValue;
        }
    }
    fail("expected alloc named '" + expectedName + "'");
}

const GetElemPtrValue* findGetElemPtrBySource(
    const BasicBlock& basicBlock, const Value* expectedSource)
{
    for (size_t instIndex = 0; instIndex < basicBlock.getNumInsts(); ++instIndex) {
        const auto* getElemPtrValue = dynamic_cast<const GetElemPtrValue*>(
            basicBlock.getInst(instIndex));
        if (getElemPtrValue != nullptr
            && getElemPtrValue->getSource() == expectedSource) {
            return getElemPtrValue;
        }
    }
    return nullptr;
}

const LoadValue* findLoadBySource(
    const BasicBlock& basicBlock, const Value* expectedSource)
{
    for (size_t instIndex = 0; instIndex < basicBlock.getNumInsts(); ++instIndex) {
        const auto* loadValue
            = dynamic_cast<const LoadValue*>(basicBlock.getInst(instIndex));
        if (loadValue != nullptr && loadValue->getSource() == expectedSource) {
            return loadValue;
        }
    }
    return nullptr;
}

const StoreValue* findStoreByDestination(
    const BasicBlock& basicBlock, const Value* expectedDestination)
{
    for (size_t instIndex = 0; instIndex < basicBlock.getNumInsts(); ++instIndex) {
        const auto* storeValue
            = dynamic_cast<const StoreValue*>(basicBlock.getInst(instIndex));
        if (storeValue != nullptr
            && storeValue->getDestination() == expectedDestination) {
            return storeValue;
        }
    }
    return nullptr;
}

const CallValue* findCallToCallee(
    const BasicBlock& basicBlock, const Function* expectedCallee)
{
    for (size_t instIndex = 0; instIndex < basicBlock.getNumInsts(); ++instIndex) {
        const auto* callValue
            = dynamic_cast<const CallValue*>(basicBlock.getInst(instIndex));
        if (callValue != nullptr && callValue->getCallee() == expectedCallee) {
            return callValue;
        }
    }
    return nullptr;
}

const GetPtrValue* findGetPtrBySource(
    const BasicBlock& basicBlock, const Value* expectedSource)
{
    for (size_t instIndex = 0; instIndex < basicBlock.getNumInsts(); ++instIndex) {
        const auto* getPtrValue
            = dynamic_cast<const GetPtrValue*>(basicBlock.getInst(instIndex));
        if (getPtrValue != nullptr && getPtrValue->getSource() == expectedSource) {
            return getPtrValue;
        }
    }
    return nullptr;
}

bool hasLoadFromElementPointerSource(
    const BasicBlock& basicBlock, const Value* expectedArraySource)
{
    for (size_t instIndex = 0; instIndex < basicBlock.getNumInsts(); ++instIndex) {
        const auto* loadValue
            = dynamic_cast<const LoadValue*>(basicBlock.getInst(instIndex));
        if (loadValue == nullptr) {
            continue;
        }
        const auto* getElemPtrValue
            = dynamic_cast<const GetElemPtrValue*>(loadValue->getSource());
        if (getElemPtrValue != nullptr
            && getElemPtrValue->getSource() == expectedArraySource) {
            return true;
        }
    }
    return false;
}

void testLocalArrayAccessLowersThroughElementPointers()
{ // 6 8 9 10 12 13 14 21
    auto program = generateProgram(
        "int main(){int arr[2]; arr[1] = 7; return arr[1];}");

    const auto* mainFunction = requireOnlyFunction(*program);
    const auto* entryBlock = requireEntryBlock(*mainFunction);

    require(entryBlock->getNumInsts() >= 6,
        "local array lowering should allocate storage, compute element pointers, store, load, and return");
    const auto* alloc = requireAlloc(entryBlock->getInst(0), "%v_arr");
    const auto* firstElemPtr = requireGetElemPtr(entryBlock->getInst(1), alloc);
    requireInteger(firstElemPtr->getIndex(), 1);
    const auto* storeValue = requireStore(entryBlock->getInst(2), firstElemPtr);
    requireInteger(storeValue->getVal(), 7);
    const auto* secondElemPtr = requireGetElemPtr(entryBlock->getInst(3), alloc);
    requireInteger(secondElemPtr->getIndex(), 1);
    const auto* loadedValue = requireLoad(entryBlock->getInst(4), secondElemPtr, "%3");
    require(requireReturn(entryBlock->getInst(5))->getVal() == loadedValue,
        "array read should return the loaded element value");
}

void testConstArraysLowerToAllocatedStorageAndRespectShadowing()
{
    auto program = generateProgram(kShadowedConstArraySource);

    require(program->getNumVals() == 1,
        "global const arrays should lower to global storage");
    const auto* globalGarr = requireGlobalAlloc(program->getVal(0), "@c_garr");
    const auto* globalInit = requireAggregate(globalGarr->getInitVal(), 10);
    for (int32_t i = 0; i < 10; ++i) {
        requireInteger(globalInit->getElement(static_cast<size_t>(i)), i + 6);
    }

    const auto* mainFunction = requireFunctionByName(*program, "@main");
    const auto* entryBlock = requireEntryBlock(*mainFunction);
    const auto* whileBodyBlock = requireBlockNameContains(*mainFunction, "while_body");
    const auto* whileEndBlock = requireBlockNameContains(*mainFunction, "while_end");

    const auto* arrAlloc = requireAllocByName(*entryBlock, "%c_arr");
    require(findGetElemPtrBySource(*entryBlock, arrAlloc) != nullptr,
        "local const array should allocate storage and initialize elements through element pointers");

    const auto* globalElemPtr = findGetElemPtrBySource(*whileBodyBlock, globalGarr);
    require(globalElemPtr != nullptr,
        "loop body should index the global const array through global storage");
    require(findLoadBySource(*whileBodyBlock, globalElemPtr) != nullptr,
        "loop body should load the indexed global const array element");

    const auto* localGarrAlloc = requireAllocByName(*whileEndBlock, "%c_garr");
    const auto* localElemPtr = findGetElemPtrBySource(*whileEndBlock, localGarrAlloc);
    require(localElemPtr != nullptr,
        "shadowing local const array should allocate local storage and compute element addresses");
    const auto* localInitStore = findStoreByDestination(*whileEndBlock, localElemPtr);
    require(localInitStore != nullptr,
        "shadowing local const array should materialize its initializer into memory");
    requireInteger(localInitStore->getVal(), 1);
    require(hasLoadFromElementPointerSource(*whileEndBlock, localGarrAlloc),
        "return path should read the shadowing local const array from local storage");
}

void testFunctionArrayParametersLowerThroughPointerDepths()
{
    auto program = generateProgram(kFunctionArrayParamSource);

    const auto* f1Function = requireFunctionByName(*program, "@f1");
    const auto* f2Function = requireFunctionByName(*program, "@f2");
    const auto* f3Function = requireFunctionByName(*program, "@f3");
    const auto* mainFunction = requireFunctionByName(*program, "@main");

    const auto* f1Entry = requireEntryBlock(*f1Function);
    const auto* f1Body = requireBlockNameContains(*f1Function, "while_body");
    const auto* f1ParamAlloc = requireAllocByName(*f1Entry, "%v_a");
    const auto* loadedF1Param = findLoadBySource(*f1Body, f1ParamAlloc);
    require(loadedF1Param != nullptr,
        "f1 should reload the decayed array parameter from its local storage slot");
    require(findGetPtrBySource(*f1Body, loadedF1Param) != nullptr,
        "f1 should index an unsized array parameter through getptr");

    const auto* f2Body = requireBlockNameContains(*f2Function, "while_body");
    const auto* f1Call = findCallToCallee(*f2Body, f1Function);
    require(f1Call != nullptr,
        "f2 should lower its nested call to f1 inside the loop body");
    require(f1Call->getNumArgs() == 2,
        "f2 should pass both arguments to f1");
    require(f1Call->getArg(1)->getVType()->isPointerType(),
        "f2 should pass an address as the array argument to f1");
    require(dynamic_cast<const PointerType*>(f1Call->getArg(1)->getVType())
                ->getPointeeType()
                ->isInt32Type(),
        "f2 should decay a[n] to an int pointer when calling f1");

    const auto* f3Body = requireBlockNameContains(*f3Function, "while_body");
    const auto* f2Call = findCallToCallee(*f3Body, f2Function);
    require(f2Call != nullptr,
        "f3 should lower its nested call to f2 inside the loop body");
    require(f2Call->getNumArgs() == 2,
        "f3 should pass both arguments to f2");
    require(f2Call->getArg(1)->getVType()->isPointerType(),
        "f3 should pass an address as the array argument to f2");
    const auto* f3ArgPointee
        = dynamic_cast<const PointerType*>(f2Call->getArg(1)->getVType())
              ->getPointeeType();
    require(f3ArgPointee->isArrayType(),
        "f3 should decay a[n] to a pointer-to-array when calling f2");
    const auto* pointeeArray = dynamic_cast<const ArrayType*>(f3ArgPointee);
    require(pointeeArray->getNumElements() == 10,
        "f3 should preserve the inner row extent when passing a[n] to f2");
    require(pointeeArray->getElementType()->isInt32Type(),
        "f3 should pass rows of ten integers to f2");

    const auto* mainEntry = requireEntryBlock(*mainFunction);
    const auto* f3Call = findCallToCallee(*mainEntry, f3Function);
    require(f3Call != nullptr,
        "main should lower its call to f3");
    require(f3Call->getArg(1)->getVType()->isPointerType(),
        "main should pass the local three-dimensional array by address");
}

void testGlobalArrayInitializerExpressionsLowerToComputedAggregates()
{
    auto program = generateProgram(kArrayInitializerExpressionSource);

    require(program->getNumVals() == 2,
        "expression-based global array initializers should lower both c and d to global storage");

    const auto* cGlobal = requireGlobalAlloc(program->getVal(0), "@c_c");
    const auto* cInit = requireAggregate(cGlobal->getInitVal(), 2);
    requireInteger(cInit->getElement(0), 5);
    requireInteger(cInit->getElement(1), -1);

    const auto* dGlobal = requireGlobalAlloc(program->getVal(1), "@v_d");
    const auto* dInit = requireAggregate(dGlobal->getInitVal(), 2);
    const auto* dRow0 = requireAggregate(dInit->getElement(0), 2);
    const auto* dRow1 = requireAggregate(dInit->getElement(1), 2);
    requireInteger(dRow0->getElement(0), 5);
    requireInteger(dRow0->getElement(1), -1);
    requireInteger(dRow1->getElement(0), 4);
    requireInteger(dRow1->getElement(1), 6);
}

void testBuiltinArrayLibraryDeclarationsLowerToExternalFunctions()
{
    auto program = generateProgram(kBuiltinArrayDeclSource);

    require(program->getNumFuncs() == 3,
        "using getarray and putarray should emit two external declarations plus main");
    const auto* getarrayFunction = requireFunctionByName(*program, "@getarray");
    const auto* putarrayFunction = requireFunctionByName(*program, "@putarray");
    const auto* mainFunction = requireFunctionByName(*program, "@main");

    require(getarrayFunction->getNumBBs() == 0,
        "getarray should lower as an external function declaration");
    require(putarrayFunction->getNumBBs() == 0,
        "putarray should lower as an external function declaration");

    const auto* getarrayType
        = dynamic_cast<const FunctionType*>(getarrayFunction->getFuncType());
    const auto* putarrayType
        = dynamic_cast<const FunctionType*>(putarrayFunction->getFuncType());
    require(getarrayType != nullptr && putarrayType != nullptr,
        "builtin array library declarations should lower to function types");
    require(getarrayType->getNumParams() == 1,
        "getarray should preserve its single pointer parameter");
    require(getarrayType->getResultType()->isInt32Type(),
        "getarray should return int in Koopa IR");
    require(getarrayType->getParamType(0)->isPointerType(),
        "getarray parameter should lower to *i32");
    require(dynamic_cast<const PointerType*>(getarrayType->getParamType(0))
                ->getPointeeType()
                ->isInt32Type(),
        "getarray parameter should point to i32 elements");

    require(putarrayType->getNumParams() == 2,
        "putarray should preserve both parameters");
    require(putarrayType->getResultType()->isUnitType(),
        "putarray should return void in Koopa IR");
    require(putarrayType->getParamType(0)->isInt32Type(),
        "putarray first parameter should lower to i32");
    require(putarrayType->getParamType(1)->isPointerType(),
        "putarray second parameter should lower to *i32");
    require(dynamic_cast<const PointerType*>(putarrayType->getParamType(1))
                ->getPointeeType()
                ->isInt32Type(),
        "putarray second parameter should point to i32 elements");

    const auto* mainEntry = requireEntryBlock(*mainFunction);
    require(findCallToCallee(*mainEntry, putarrayFunction) != nullptr,
        "main should call the lowered putarray declaration");
    require(findCallToCallee(*mainEntry, getarrayFunction) != nullptr,
        "main should call the lowered getarray declaration");
}

void testMixedBraceArrayInitializersPreserveSubobjectBoundaries()
{
    auto program = generateProgram(kMixedBraceArrayInitializerSource);

    require(program->getNumVals() == 2,
        "mixed brace/scalar global array initializers should lower to two globals");

    const auto* aGlobal = requireGlobalAlloc(program->getVal(0), "@v_a");
    const auto* aInit = requireAggregate(aGlobal->getInitVal(), 3);
    const auto* aRow0 = requireAggregate(aInit->getElement(0), 3);
    const auto* aRow1 = requireAggregate(aInit->getElement(1), 3);
    requireInteger(aRow0->getElement(0), 0);
    requireInteger(aRow0->getElement(1), 1);
    requireInteger(aRow0->getElement(2), 0);
    requireInteger(aRow1->getElement(0), 2);
    requireInteger(aRow1->getElement(1), 3);
    requireInteger(aRow1->getElement(2), 0);
    require(aInit->getElement(2)->isZeroInitValue(),
        "fully zero trailing rows may be represented as zeroinit");

    const auto* bGlobal = requireGlobalAlloc(program->getVal(1), "@v_b");
    const auto* bInit = requireAggregate(bGlobal->getInitVal(), 3);
    const auto* bRow0 = requireAggregate(bInit->getElement(0), 3);
    const auto* bRow1 = requireAggregate(bInit->getElement(1), 3);
    const auto* bRow2 = requireAggregate(bInit->getElement(2), 3);
    requireInteger(bRow0->getElement(0), 0);
    requireInteger(bRow0->getElement(1), 1);
    requireInteger(bRow0->getElement(2), 2);
    requireInteger(bRow1->getElement(0), 3);
    requireInteger(bRow1->getElement(1), 0);
    requireInteger(bRow1->getElement(2), 0);
    requireInteger(bRow2->getElement(0), 4);
    requireInteger(bRow2->getElement(1), 5);
    requireInteger(bRow2->getElement(2), 6);
}

void testRecursiveThreeDimensionalArrayInitializersPreserveBoundaries()
{
    auto program = generateProgram(kRecursiveArrayInitializerSource);

    require(program->getNumVals() == 2,
        "three-dimensional recursive array initializers should lower to two globals");

    const auto* aGlobal = requireGlobalAlloc(program->getVal(0), "@v_a");
    const auto* aInit = requireAggregate(aGlobal->getInitVal(), 3);
    const auto* aPlane0 = requireAggregate(aInit->getElement(0), 3);
    const auto* aRow00 = requireAggregate(aPlane0->getElement(0), 3);
    const auto* aRow01 = requireAggregate(aPlane0->getElement(1), 3);
    const auto* aRow02 = requireAggregate(aPlane0->getElement(2), 3);
    requireInteger(aRow00->getElement(0), 0);
    requireInteger(aRow00->getElement(1), 1);
    requireInteger(aRow00->getElement(2), 2);
    requireInteger(aRow01->getElement(0), 3);
    requireInteger(aRow01->getElement(1), 0);
    requireInteger(aRow01->getElement(2), 0);
    requireInteger(aRow02->getElement(0), 4);
    requireInteger(aRow02->getElement(1), 0);
    requireInteger(aRow02->getElement(2), 0);
    require(aInit->getElement(1)->isZeroInitValue(),
        "remaining planes of mutable array may be represented as zeroinit");
    require(aInit->getElement(2)->isZeroInitValue(),
        "trailing mutable plane should be zeroinit");

    const auto* bGlobal = requireGlobalAlloc(program->getVal(1), "@c_b");
    const auto* bInit = requireAggregate(bGlobal->getInitVal(), 3);
    const auto* bPlane0 = requireAggregate(bInit->getElement(0), 3);
    const auto* bRow00 = requireAggregate(bPlane0->getElement(0), 3);
    const auto* bRow01 = requireAggregate(bPlane0->getElement(1), 3);
    const auto* bRow02 = requireAggregate(bPlane0->getElement(2), 3);
    requireInteger(bRow00->getElement(0), 0);
    requireInteger(bRow00->getElement(1), 1);
    requireInteger(bRow00->getElement(2), 2);
    requireInteger(bRow01->getElement(0), 3);
    requireInteger(bRow01->getElement(1), 0);
    requireInteger(bRow01->getElement(2), 0);
    requireInteger(bRow02->getElement(0), 4);
    requireInteger(bRow02->getElement(1), 0);
    requireInteger(bRow02->getElement(2), 0);
    require(bInit->getElement(1)->isZeroInitValue(),
        "remaining planes of const array may be represented as zeroinit");
    require(bInit->getElement(2)->isZeroInitValue(),
        "trailing const plane should be zeroinit");
}

void testStressGeneratedArrayInitializerLowers()
{
    auto program = generateProgram(kStressGeneratedArrayInitializerSource);

    require(program->getNumVals() == 1,
        "stress-generated deep array initializer should lower to one global");
    const auto* global = requireGlobalAlloc(program->getVal(0), "@v_a");
    require(global->getInitVal()->isAggregateValue(),
        "stress-generated deep array initializer should lower without a fake non-constant error");
}

} // namespace

int main()
{
    testLocalArrayAccessLowersThroughElementPointers();
    testConstArraysLowerToAllocatedStorageAndRespectShadowing();
    testFunctionArrayParametersLowerThroughPointerDepths();
    testGlobalArrayInitializerExpressionsLowerToComputedAggregates();
    testBuiltinArrayLibraryDeclarationsLowerToExternalFunctions();
    testMixedBraceArrayInitializersPreserveSubobjectBoundaries();
    testRecursiveThreeDimensionalArrayInitializersPreserveBoundaries();
    testStressGeneratedArrayInitializerLowers();
    return 0;
}