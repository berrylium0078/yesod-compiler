#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "frontend/semantic_symbol.h"
#include "frontend/semantic_type_impl.h"

namespace yesod::frontend {

SemanticTypeAnalysisResult::SemanticTypeAnalysisResult(const AST& ast)
    : ast(ast)
{
}

const std::unordered_map<Ref<Exp>, SemanticExpInfo>&
SemanticTypeAnalysisResult::expInfoByExp() const
{
    return m_expInfoByExp;
}

const std::unordered_map<int32_t, SemanticSymbol>&
SemanticTypeAnalysisResult::symbolsById() const
{
    return m_symbolById;
}

const std::vector<std::unique_ptr<Diagnostic>>&
SemanticTypeAnalysisResult::diagnostics() const
{
    return m_diagnostics;
}

const SemanticSymbol* SemanticTypeAnalysisResult::findSymbolById(
    int32_t symbolId) const
{
    const auto symbolIt = m_symbolById.find(symbolId);
    if (symbolIt == m_symbolById.end()) {
        return nullptr;
    }
    return &symbolIt->second;
}

namespace {

    constexpr int32_t kMintMod = 998244353;

    struct ShiftFoldResult {
        int32_t value = 0;
        bool warnedOutOfRange = false;
    };

    [[nodiscard]] int32_t normalizeMintValue(int64_t value)
    {
        value %= kMintMod;
        if (value < 0) {
            value += kMintMod;
        }
        return static_cast<int32_t>(value);
    }

    [[nodiscard]] bool isScalarTypeImpl(const SemanticType& type)
    {
        return type.kind == SemanticTypeKind::integer
            || type.kind == SemanticTypeKind::mint
            || type.kind == SemanticTypeKind::boolean;
    }

    [[nodiscard]] SemanticType lowerBType(BTypeKeyword bType)
    {
        switch (bType) {
        case BTypeKeyword::intKeyword:
            return SemanticType::makeInteger();
        case BTypeKeyword::mintKeyword:
            return SemanticType::makeMint();
        case BTypeKeyword::polyKeyword:
            return SemanticType::makePoly();
        }
        throw std::runtime_error("unsupported base type keyword");
    }

    [[nodiscard]] bool typesMatchExactly(
        const SemanticType& lhs, const SemanticType& rhs)
    {
        return lhs == rhs;
    }

    [[nodiscard]] bool functionSignaturesMatch(const SemanticSymbol& symbol,
        const SemanticType& returnType,
        const std::vector<SemanticType>& paramTypes)
    {
        return symbol.isFunction()
            && symbol.function().m_returnType == returnType
            && symbol.function().m_paramTypes == paramTypes;
    }

    [[nodiscard]] bool typesMatchForCallImpl(
        const SemanticType& paramType, const SemanticType& argType)
    {
        if (!paramType.isArray()) {
            return typesMatchExactly(paramType, argType);
        }
        if (!argType.isArray()) {
            return false;
        }
        if (paramType.m_arrayLength == -1) {
            if (paramType.m_elementType == nullptr
                || argType.m_elementType == nullptr) {
                return false;
            }
            return typesMatchExactly(
                *paramType.m_elementType, *argType.m_elementType);
        }
        return typesMatchExactly(paramType, argType);
    }

    [[nodiscard]] std::optional<int32_t> applyUnaryOp(
        UnaryOpKeyword op, int32_t operand)
    {
        switch (op) {
        case UnaryOpKeyword::plus:
            return operand;
        case UnaryOpKeyword::minus:
            return -operand;
        case UnaryOpKeyword::bang:
            return operand == 0 ? 1 : 0;
        case UnaryOpKeyword::tilde:
            return ~operand;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<int32_t> applyArithmeticOp(
        BinaryOpKeyword op, int32_t lhs, int32_t rhs)
    {
        switch (op) {
        case BinaryOpKeyword::star:
            return static_cast<int32_t>(static_cast<int64_t>(lhs) * rhs);
        case BinaryOpKeyword::slash:
            if (rhs == 0) {
                return std::nullopt;
            }
            return lhs / rhs;
        case BinaryOpKeyword::percent:
            if (rhs == 0) {
                return std::nullopt;
            }
            return lhs % rhs;
        case BinaryOpKeyword::plus:
            return static_cast<int32_t>(static_cast<int64_t>(lhs) + rhs);
        case BinaryOpKeyword::minus:
            return static_cast<int32_t>(static_cast<int64_t>(lhs) - rhs);
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<int32_t> applyBitwiseOp(
        BinaryOpKeyword op, int32_t lhs, int32_t rhs)
    {
        switch (op) {
        case BinaryOpKeyword::bitAnd:
            return lhs & rhs;
        case BinaryOpKeyword::bitXor:
            return lhs ^ rhs;
        case BinaryOpKeyword::bitOr:
            return lhs | rhs;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] ShiftFoldResult applyShiftOp(
        BinaryOpKeyword op, int32_t lhs, int32_t rhs)
    {
        const int32_t normalizedShift = ((rhs % 32) + 32) % 32;
        switch (op) {
        case BinaryOpKeyword::shl:
            return ShiftFoldResult {
                .value = static_cast<int32_t>(lhs << normalizedShift),
                .warnedOutOfRange = rhs < 0 || rhs >= 32,
            };
        case BinaryOpKeyword::sar:
            return ShiftFoldResult {
                .value = static_cast<int32_t>(lhs >> normalizedShift),
                .warnedOutOfRange = rhs < 0 || rhs >= 32,
            };
        default:
            throw std::runtime_error("unsupported shift operator");
        }
    }

    [[nodiscard]] std::optional<int32_t> applyMintArithmeticOp(
        BinaryOpKeyword op, int32_t lhs, int32_t rhs)
    {
        switch (op) {
        case BinaryOpKeyword::plus:
            return normalizeMintValue(static_cast<int64_t>(lhs) + rhs);
        case BinaryOpKeyword::minus:
            return normalizeMintValue(static_cast<int64_t>(lhs) - rhs);
        case BinaryOpKeyword::star:
            return normalizeMintValue(static_cast<int64_t>(lhs) * rhs);
        case BinaryOpKeyword::slash:
            if (rhs == 0) {
                return std::nullopt;
            }
            {
                int64_t base = rhs;
                int64_t exponent = kMintMod - 2;
                int64_t inverse = 1;
                while (exponent > 0) {
                    if ((exponent & 1) != 0) {
                        inverse = (inverse * base) % kMintMod;
                    }
                    base = (base * base) % kMintMod;
                    exponent >>= 1;
                }
                return normalizeMintValue(static_cast<int64_t>(lhs) * inverse);
            }
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] int32_t applyRelOp(
        BinaryOpKeyword op, int32_t lhs, int32_t rhs)
    {
        switch (op) {
        case BinaryOpKeyword::less:
            return lhs < rhs ? 1 : 0;
        case BinaryOpKeyword::greater:
            return lhs > rhs ? 1 : 0;
        case BinaryOpKeyword::lessEqual:
            return lhs <= rhs ? 1 : 0;
        case BinaryOpKeyword::greaterEqual:
            return lhs >= rhs ? 1 : 0;
        default:
            break;
        }
        throw std::runtime_error("unsupported relational operator");
    }

    [[nodiscard]] int32_t applyEqOp(
        BinaryOpKeyword op, int32_t lhs, int32_t rhs)
    {
        switch (op) {
        case BinaryOpKeyword::equal:
            return lhs == rhs ? 1 : 0;
        case BinaryOpKeyword::notEqual:
            return lhs != rhs ? 1 : 0;
        default:
            break;
        }
        throw std::runtime_error("unsupported equality operator");
    }

    [[nodiscard]] int32_t applyLAndOp(BinaryOpKeyword, int32_t lhs, int32_t rhs)
    {
        return (lhs != 0 && rhs != 0) ? 1 : 0;
    }

    [[nodiscard]] int32_t applyLOrOp(BinaryOpKeyword, int32_t lhs, int32_t rhs)
    {
        return (lhs != 0 || rhs != 0) ? 1 : 0;
    }

    [[nodiscard]] SemanticType lowerFuncType(FuncTypeKeyword funcType)
    {
        switch (funcType) {
        case FuncTypeKeyword::voidKeyword:
            return SemanticType::makeVoid();
        case FuncTypeKeyword::intKeyword:
            return SemanticType::makeInteger();
        case FuncTypeKeyword::mintKeyword:
            return SemanticType::makeMint();
        case FuncTypeKeyword::polyKeyword:
            return SemanticType::makePoly();
        }
        throw std::runtime_error("unsupported function type keyword");
    }

} // namespace

namespace detail {

    SemanticTypeAnalyzerImpl::SemanticTypeAnalyzerImpl(
        const AST& ast, const SemanticSymbolResolver& symbolResolver)
        : AstVisitor(ast)
        , SemanticTypeAnalysisResult(ast)
        , m_symbolResolver(symbolResolver)
    {
    }

    void SemanticTypeAnalyzerImpl::analyze(Ref<CompUnit> compUnit)
    {
        m_expInfoByExp.clear();
        m_symbolById.clear();
        m_diagnostics.clear();
        m_currentFuncReturnType.reset();
        m_definedFunctionSymbolIds.clear();
        traverse(compUnit);
    }

    void SemanticTypeAnalyzerImpl::visitCompUnit(Ref<CompUnit> compUnit)
    {
        for (const auto topLevelItem : compUnit(m_ast).topLevelItems) {
            MATCH(topLevelItem)
            WITH([&](Decl decl) { visitDecl(decl); },
                [&](Ref<FuncDef> funcDef) { declareFuncDef(funcDef); });
        }

        for (const auto topLevelItem : compUnit(m_ast).topLevelItems) {
            MATCH(topLevelItem)
            WITH(
                [&](Ref<FuncDef> funcDef) {
                    if (funcDef(m_ast).body != nullptr) {
                        visitFuncDef(funcDef);
                    }
                },
                [&](const auto&) { });
        }
    }

    void SemanticTypeAnalyzerImpl::declareFuncDef(Ref<FuncDef> funcDefRef)
    {
        const auto& funcDef = funcDefRef(m_ast);
        const auto resolvedFunctionId = resolvedSymbolId(funcDef.identifier);
        if (!resolvedFunctionId.has_value()) {
            return;
        }

        std::vector<SemanticType> paramTypes;
        paramTypes.reserve(funcDef.funcFParams.size());
        for (const auto& funcFParam : funcDef.funcFParams) {
            auto paramType = analyzeObjectType(funcFParam.bType,
                funcFParam.shape, funcFParam.sourcePos.m_offset);
            if (funcFParam.m_isArray) {
                paramType = SemanticType::makeUnsizedArray(paramType);
            }
            paramTypes.push_back(paramType);
        }

        const auto returnType = lowerFuncType(funcDef.m_funcType);
        const auto* existingSymbol = findSymbolById(*resolvedFunctionId);
        if (existingSymbol == nullptr) {
            m_symbolById.emplace(*resolvedFunctionId,
                makeFunctionSymbol(funcDef.identifier, *resolvedFunctionId,
                    returnType, paramTypes));
        } else if (!functionSignaturesMatch(
                       *existingSymbol, returnType, paramTypes)) {
            const auto& identifier = funcDef.identifier(m_ast);
            recordDiagnostic<DoubleDefinitionDiagnostic>(
                identifier.sourcePos.m_offset,
                "conflicting declaration of '" + identifier.name + "'");
        }

        if (funcDef.body != nullptr
            && m_definedFunctionSymbolIds.contains(*resolvedFunctionId)) {
            const auto& identifier = funcDef.identifier(m_ast);
            recordDiagnostic<DoubleDefinitionDiagnostic>(
                identifier.sourcePos.m_offset,
                "redefinition of '" + identifier.name + "'");
        }
        if (funcDef.body != nullptr) {
            m_definedFunctionSymbolIds.insert(*resolvedFunctionId);
        }
    }

    void SemanticTypeAnalyzerImpl::visitFuncDef(Ref<FuncDef> funcDefRef)
    {
        const auto& funcDef = funcDefRef(m_ast);
        if (funcDef.body == nullptr) {
            return;
        }

        const auto functionSymbolId = resolvedSymbolId(funcDef.identifier);
        if (!functionSymbolId.has_value()) {
            throw std::runtime_error(
                "function definition is missing a declared function symbol");
        }

        const auto* declaredFunction = findSymbolById(*functionSymbolId);
        const auto previousReturnType = m_currentFuncReturnType;
        if (declaredFunction != nullptr && declaredFunction->isFunction()) {
            m_currentFuncReturnType = declaredFunction->function().m_returnType;
            for (size_t i = 0; i != funcDef.funcFParams.size(); ++i) {
                const auto resolvedParamId
                    = resolvedSymbolId(funcDef.funcFParams[i].identifier);
                if (!resolvedParamId.has_value()) {
                    continue;
                }
                if (i >= declaredFunction->function().m_paramTypes.size()) {
                    continue;
                }
                m_symbolById.insert_or_assign(*resolvedParamId,
                    makeObjectSymbol(*resolvedParamId,
                        funcDef.funcFParams[i].identifier, false, std::nullopt,
                        declaredFunction->function().m_paramTypes[i]));
            }
        } else {
            m_currentFuncReturnType = lowerFuncType(funcDef.m_funcType);
        }

        for (const auto blockItem : funcDef.body(m_ast).items) {
            visitBlockItem(blockItem);
        }
        m_currentFuncReturnType = previousReturnType;
    }

    void SemanticTypeAnalyzerImpl::visitConstDecl(Ref<ConstDecl> constDecl)
    {
        for (const auto constDef : constDecl(m_ast).constDef) {
            const auto& parsedConstDef = constDef(m_ast);
            const auto objectType = analyzeObjectType(constDecl(m_ast).bType,
                parsedConstDef.shape, parsedConstDef.sourcePos.m_offset);

            size_t nextIndex = 0;
            bool hasRemainingWarning = false;
            auto analyzedInit
                = analyzeConstInitVal(parsedConstDef.constInitVal.ref(),
                    objectType, true, nextIndex, hasRemainingWarning);

            if (!parsedConstDef.shape.empty()) {
                analyzedInit.m_isConstant = false;
                analyzedInit.m_constantValue = 0;
            } else {
                if (analyzedInit.m_type.isVoid()
                    || analyzedInit.m_type.isArray()) {
                    recordDiagnostic<TypeMismatchDiagnostic>(
                        parsedConstDef.sourcePos.m_offset,
                        "const initializer must produce a scalar value");
                }
                if (!analyzedInit.m_isConstant) {
                    recordDiagnostic<NonConstantConstInitializerDiagnostic>(
                        parsedConstDef.sourcePos.m_offset,
                        "const initializer must be a constant expression");
                }
            }

            const auto resolvedConstId
                = resolvedSymbolId(parsedConstDef.identifier);
            if (!resolvedConstId.has_value()) {
                continue;
            }
            m_symbolById.insert_or_assign(*resolvedConstId,
                makeObjectSymbol(*resolvedConstId, parsedConstDef.identifier,
                    true,
                    parsedConstDef.shape.empty() && analyzedInit.m_isConstant
                        ? std::make_optional(analyzedInit.m_constantValue)
                        : std::nullopt,
                    objectType));
        }
    }

    void SemanticTypeAnalyzerImpl::visitVarDecl(Ref<VarDecl> varDecl)
    {
        for (const auto varDef : varDecl(m_ast).varDef) {
            const auto& parsedVarDef = varDef(m_ast);
            const auto objectType = analyzeObjectType(varDecl(m_ast).bType,
                parsedVarDef.shape, parsedVarDef.sourcePos.m_offset);
            if (parsedVarDef.initVal != nullptr) {
                size_t nextIndex = 0;
                bool hasRemainingWarning = false;
                (void)analyzeInitVal(parsedVarDef.initVal.ref(), objectType,
                    m_currentFuncReturnType == std::nullopt, true, nextIndex,
                    hasRemainingWarning);
            }

            const auto resolvedVarId
                = resolvedSymbolId(parsedVarDef.identifier);
            if (!resolvedVarId.has_value()) {
                continue;
            }
            m_symbolById.insert_or_assign(*resolvedVarId,
                makeObjectSymbol(*resolvedVarId, parsedVarDef.identifier, false,
                    std::nullopt, objectType));
        }
    }

    void SemanticTypeAnalyzerImpl::visitIfStmt(Ref<IfStmt> ifStmt)
    {
        (void)analyzeCondExp(ifStmt(m_ast).condition);
        visitStmt(ifStmt(m_ast).thenBody);
        visitStmt(ifStmt(m_ast).elseBody);
    }

    void SemanticTypeAnalyzerImpl::visitWhileStmt(Ref<WhileStmt> whileStmt)
    {
        (void)analyzeCondExp(whileStmt(m_ast).condition);
        visitStmt(whileStmt(m_ast).body);
    }

    void SemanticTypeAnalyzerImpl::visitAssignStmt(Ref<AssignStmt> assignStmt)
    {
        const auto& lValExp = assignStmt(m_ast).lval(m_ast);
        MATCH(lValExp.kind)
        WITH(
            [&](const Exp::LVal& lVal) {
                const auto boundSymbolId = resolvedSymbolId(lVal.identifier);
                const auto* symbol = boundSymbolId.has_value()
                    ? findSymbolById(*boundSymbolId)
                    : nullptr;
                if (symbol != nullptr && !symbol->isObject()) {
                    const auto& identifier = lVal.identifier(m_ast);
                    recordDiagnostic<TypeMismatchDiagnostic>(
                        identifier.sourcePos.m_offset,
                        "assignment target must designate a scalar object");
                }

                const auto* objectInfo = symbol != nullptr && symbol->isObject()
                    ? &symbol->object()
                    : nullptr;
                if (objectInfo != nullptr && objectInfo->m_isConst) {
                    const auto& identifier = lVal.identifier(m_ast);
                    recordDiagnostic<AssignToConstDiagnostic>(
                        identifier.sourcePos.m_offset,
                        "cannot assign to const '" + symbol->name + "'");
                }

                auto currentType = objectInfo != nullptr
                    ? objectInfo->m_type
                    : SemanticType::makeInteger();
                for (const auto index : lVal.indices) {
                    const auto analyzedIndex = analyzeExp(index);
                    if (analyzedIndex.m_type.kind
                        != SemanticTypeKind::integer) {
                        recordDiagnostic<TypeMismatchDiagnostic>(
                            index(m_ast).sourcePos.m_offset,
                            "array subscript must produce an integer value");
                    }
                    if (!currentType.isArray()
                        || currentType.m_elementType == nullptr) {
                        const auto& identifier = lVal.identifier(m_ast);
                        recordDiagnostic<TypeMismatchDiagnostic>(
                            identifier.sourcePos.m_offset,
                            "subscripted assignment target is not an array");
                        break;
                    }
                    currentType = *currentType.m_elementType;
                }
                if (currentType.isArray()) {
                    const auto& identifier = lVal.identifier(m_ast);
                    recordDiagnostic<TypeMismatchDiagnostic>(
                        identifier.sourcePos.m_offset,
                        "assignment target must designate a scalar object");
                }
            },
            [&](const auto&) {
                throw std::runtime_error(
                    "assignment lhs is not an lvalue expression");
            });

        const auto analyzedExp = analyzeExp(assignStmt(m_ast).exp);
        if (analyzedExp.m_valueKind == ExpType::voidType
            || analyzedExp.m_valueKind == ExpType::array) {
            recordDiagnostic<TypeMismatchDiagnostic>(
                assignStmt(m_ast).sourcePos.m_offset,
                "assignment rhs must produce a scalar value");
            return;
        }

        MATCH(lValExp.kind)
        WITH(
            [&](const Exp::LVal& lVal) {
                const auto boundSymbolId = resolvedSymbolId(lVal.identifier);
                const auto* symbol = boundSymbolId.has_value()
                    ? findSymbolById(*boundSymbolId)
                    : nullptr;
                const auto* objectInfo = symbol != nullptr && symbol->isObject()
                    ? &symbol->object()
                    : nullptr;
                if (objectInfo == nullptr) {
                    return;
                }

                auto targetType = objectInfo->m_type;
                for (size_t i = 0; i < lVal.indices.size(); ++i) {
                    if (!targetType.isArray()
                        || targetType.m_elementType == nullptr) {
                        break;
                    }
                    targetType = *targetType.m_elementType;
                }
                if (targetType.isArray()) {
                    return;
                }
                if (targetType != analyzedExp.m_type) {
                    recordDiagnostic<TypeMismatchDiagnostic>(
                        assignStmt(m_ast).sourcePos.m_offset,
                        "assignment rhs type does not match lhs type; use an "
                        "explicit cast for int/mint conversion");
                }
            },
            [&](const auto&) { });
    }

    void SemanticTypeAnalyzerImpl::visitExpStmt(Ref<ExpStmt> expStmt)
    {
        if (expStmt(m_ast).exp != nullptr) {
            (void)analyzeExp(expStmt(m_ast).exp.ref());
        }
    }

    void SemanticTypeAnalyzerImpl::visitReturnStmt(Ref<ReturnStmt> returnStmt)
    {
        const auto& expr = returnStmt(m_ast).exp;
        const int32_t offset = returnStmt(m_ast).sourcePos.m_offset;
        if (!m_currentFuncReturnType.has_value()) {
            if (expr != nullptr) {
                (void)analyzeExp(expr.ref());
            }
            return;
        }
        if (expr == nullptr) {
            if (m_currentFuncReturnType->kind != SemanticTypeKind::voidType) {
                recordDiagnostic<ReturnTypeMismatchDiagnostic>(
                    offset, "non-void function must return a value");
            }
            return;
        }
        const auto analyzedExp = analyzeExp(expr.ref());
        if (m_currentFuncReturnType->kind == SemanticTypeKind::voidType) {
            recordDiagnostic<ReturnTypeMismatchDiagnostic>(
                offset, "void function must use 'return;' without a value");
            return;
        }

        if (analyzedExp.m_valueKind == ExpType::voidType
            || analyzedExp.m_valueKind == ExpType::array) {
            recordDiagnostic<ReturnTypeMismatchDiagnostic>(
                offset, "return expression must produce a scalar value");
            return;
        }
        if (*m_currentFuncReturnType != analyzedExp.m_type) {
            recordDiagnostic<ReturnTypeMismatchDiagnostic>(offset,
                "return expression type does not match function return type; "
                "use an explicit cast for int/mint conversion");
        }
    }

    void SemanticTypeAnalyzerImpl::visitExp(Ref<Exp> exp)
    {
        (void)analyzeExp(exp);
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeBinaryExp(
        const Exp& exp, const Exp::Binary& binary)
    {
        auto lhs = analyzeExp(binary.lhs);
        auto rhs = analyzeExp(binary.rhs);
        AnalyzedExp binaryExp {
            .m_type = SemanticType::makeInteger(),
            .m_valueKind = ExpType::integer,
            .m_isConstant = false,
            .m_constantValue = 0,
        };

        // Handle poly binary operations before the scalar-only check.
        if (lhs.m_type.isPoly() || rhs.m_type.isPoly()) {
            // Shift: poly << int, poly >> int
            if (binary.op == BinaryOpKeyword::shl
                || binary.op == BinaryOpKeyword::sar) {
                if (!lhs.m_type.isPoly()
                    || rhs.m_type.kind != SemanticTypeKind::integer) {
                    recordDiagnostic<TypeMismatchDiagnostic>(
                        exp.sourcePos.m_offset,
                        "shift expects a poly left operand and an int right "
                        "operand");
                    return binaryExp;
                }
                binaryExp.m_type = SemanticType::makePoly();
                binaryExp.m_valueKind = ExpType::poly;
                return binaryExp;
            }
            // Poly arithmetic: poly +-* poly, poly +-* mint, mint +-* poly
            if (binary.op == BinaryOpKeyword::plus
                || binary.op == BinaryOpKeyword::minus
                || binary.op == BinaryOpKeyword::star) {
                const bool lhsPoly = lhs.m_type.isPoly();
                const bool rhsPoly = rhs.m_type.isPoly();
                const bool lhsNumeric = lhs.m_type.isNumeric();
                const bool rhsNumeric = rhs.m_type.isNumeric();
                if (lhsPoly && rhsPoly) {
                    binaryExp.m_type = SemanticType::makePoly();
                    binaryExp.m_valueKind = ExpType::poly;
                    return binaryExp;
                }
                // Scalar multiplication: poly * mint, mint * poly
                if (lhsPoly && rhsNumeric) {
                    binaryExp.m_type = SemanticType::makePoly();
                    binaryExp.m_valueKind = ExpType::poly;
                    return binaryExp;
                }
                if (lhsNumeric && rhsPoly) {
                    binaryExp.m_type = SemanticType::makePoly();
                    binaryExp.m_valueKind = ExpType::poly;
                    return binaryExp;
                }
                recordDiagnostic<TypeMismatchDiagnostic>(
                    exp.sourcePos.m_offset, "type mismatch in poly arithmetic");
                return binaryExp;
            }
            recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                "operation not supported for poly operands");
            return binaryExp;
        }

        if (!isScalarTypeImpl(lhs.m_type) || !isScalarTypeImpl(rhs.m_type)) {
            recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                "binary operator requires scalar operands");
            return binaryExp;
        }

        switch (binary.op) {
        case BinaryOpKeyword::orOr:
            lhs = normalizeToBoolean(std::move(lhs));
            rhs = normalizeToBoolean(std::move(rhs));
            binaryExp.m_type = SemanticType::makeBoolean();
            binaryExp.m_valueKind = ExpType::boolean;
            if (lhs.m_isConstant && rhs.m_isConstant) {
                binaryExp.m_isConstant = true;
                binaryExp.m_constantValue = applyLOrOp(
                    binary.op, lhs.m_constantValue, rhs.m_constantValue);
            }
            break;
        case BinaryOpKeyword::andAnd:
            lhs = normalizeToBoolean(std::move(lhs));
            rhs = normalizeToBoolean(std::move(rhs));
            binaryExp.m_type = SemanticType::makeBoolean();
            binaryExp.m_valueKind = ExpType::boolean;
            if (lhs.m_isConstant && rhs.m_isConstant) {
                binaryExp.m_isConstant = true;
                binaryExp.m_constantValue = applyLAndOp(
                    binary.op, lhs.m_constantValue, rhs.m_constantValue);
            }
            break;
        case BinaryOpKeyword::equal:
        case BinaryOpKeyword::notEqual:
            lhs = normalizeToArithmetic(std::move(lhs));
            rhs = normalizeToArithmetic(std::move(rhs));
            if (lhs.m_type != rhs.m_type) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "comparison operands must have the same type; use an "
                    "explicit cast for int/mint conversion");
                break;
            }
            binaryExp.m_type = SemanticType::makeBoolean();
            binaryExp.m_valueKind = ExpType::boolean;
            if (lhs.m_isConstant && rhs.m_isConstant) {
                binaryExp.m_isConstant = true;
                binaryExp.m_constantValue = applyEqOp(
                    binary.op, lhs.m_constantValue, rhs.m_constantValue);
            }
            break;
        case BinaryOpKeyword::less:
        case BinaryOpKeyword::greater:
        case BinaryOpKeyword::lessEqual:
        case BinaryOpKeyword::greaterEqual:
            lhs = normalizeToArithmetic(std::move(lhs));
            rhs = normalizeToArithmetic(std::move(rhs));
            if (lhs.m_type != rhs.m_type) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "comparison operands must have the same type; use an "
                    "explicit cast for int/mint conversion");
                break;
            }
            binaryExp.m_type = SemanticType::makeBoolean();
            binaryExp.m_valueKind = ExpType::boolean;
            if (lhs.m_isConstant && rhs.m_isConstant) {
                binaryExp.m_isConstant = true;
                binaryExp.m_constantValue = applyRelOp(
                    binary.op, lhs.m_constantValue, rhs.m_constantValue);
            }
            break;
        case BinaryOpKeyword::star:
        case BinaryOpKeyword::slash:
        case BinaryOpKeyword::percent:
        case BinaryOpKeyword::plus:
        case BinaryOpKeyword::minus:
            lhs = normalizeToArithmetic(std::move(lhs));
            rhs = normalizeToArithmetic(std::move(rhs));
            if (lhs.m_type != rhs.m_type) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "arithmetic operands must have the same type; use an "
                    "explicit cast for int/mint conversion");
                break;
            }
            binaryExp.m_type = lhs.m_type;
            binaryExp.m_valueKind = lhs.m_type.valueKind();
            if (lhs.m_type.kind == SemanticTypeKind::mint
                && binary.op == BinaryOpKeyword::percent) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "operator '%' is not supported for mint values");
                break;
            }
            if (lhs.m_isConstant && rhs.m_isConstant) {
                const auto folded = lhs.m_type.kind == SemanticTypeKind::mint
                    ? applyMintArithmeticOp(
                          binary.op, lhs.m_constantValue, rhs.m_constantValue)
                    : applyArithmeticOp(
                          binary.op, lhs.m_constantValue, rhs.m_constantValue);
                if (folded.has_value()) {
                    binaryExp.m_isConstant = true;
                    binaryExp.m_constantValue = *folded;
                }
            }
            break;
        case BinaryOpKeyword::shl:
        case BinaryOpKeyword::sar:
        case BinaryOpKeyword::bitAnd:
        case BinaryOpKeyword::bitXor:
        case BinaryOpKeyword::bitOr:
            lhs = normalizeToArithmetic(std::move(lhs));
            rhs = normalizeToArithmetic(std::move(rhs));
            if (lhs.m_type.kind != SemanticTypeKind::integer
                || rhs.m_type.kind != SemanticTypeKind::integer) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "bitwise and shift operands must be integer values");
                break;
            }
            binaryExp.m_type = SemanticType::makeInteger();
            binaryExp.m_valueKind = ExpType::integer;
            if (lhs.m_isConstant && rhs.m_isConstant) {
                if (binary.op == BinaryOpKeyword::shl
                    || binary.op == BinaryOpKeyword::sar) {
                    const auto folded = applyShiftOp(
                        binary.op, lhs.m_constantValue, rhs.m_constantValue);
                    binaryExp.m_isConstant = true;
                    binaryExp.m_constantValue = folded.value;
                    if (folded.warnedOutOfRange) {
                        recordDiagnostic<ShiftOperandOutOfRangeDiagnostic>(
                            binary.rhs(m_ast).sourcePos.m_offset,
                            "shift operand is outside [0, 32); constant "
                            "folding applies modulo 32",
                            DiagnosticSeverity::warning);
                    }
                } else {
                    const auto folded = applyBitwiseOp(
                        binary.op, lhs.m_constantValue, rhs.m_constantValue);
                    if (folded.has_value()) {
                        binaryExp.m_isConstant = true;
                        binaryExp.m_constantValue = *folded;
                    }
                }
            }
            break;
        }
        return binaryExp;
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeUnaryExp(
        const Exp& exp, const Exp::Unary& unary)
    {
        auto operand = analyzeExp(unary.lhs);
        if (operand.m_type.isPoly()) {
            recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                "unary operator requires a scalar operand");
            return AnalyzedExp {
                .m_type = unary.op == UnaryOpKeyword::bang
                    ? SemanticType::makeBoolean()
                    : SemanticType::makeInteger(),
                .m_valueKind = unary.op == UnaryOpKeyword::bang
                    ? ExpType::boolean
                    : ExpType::integer,
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }
        if (!isScalarTypeImpl(operand.m_type)) {
            recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                "unary operator requires a scalar operand");
            return AnalyzedExp {
                .m_type = unary.op == UnaryOpKeyword::bang
                    ? SemanticType::makeBoolean()
                    : SemanticType::makeInteger(),
                .m_valueKind = unary.op == UnaryOpKeyword::bang
                    ? ExpType::boolean
                    : ExpType::integer,
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }
        if (unary.op == UnaryOpKeyword::bang) {
            operand = normalizeToBoolean(std::move(operand));
        } else {
            operand = normalizeToArithmetic(std::move(operand));
            if (unary.op == UnaryOpKeyword::tilde
                && operand.m_type.kind != SemanticTypeKind::integer) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "bitwise not requires an integer operand");
                return AnalyzedExp {
                    .m_type = SemanticType::makeInteger(),
                    .m_valueKind = ExpType::integer,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            }
        }
        if (operand.m_isConstant) {
            const auto folded = applyUnaryOp(unary.op, operand.m_constantValue);
            if (folded.has_value()) {
                return AnalyzedExp {
                    .m_type = unary.op == UnaryOpKeyword::bang
                        ? SemanticType::makeBoolean()
                        : unary.op == UnaryOpKeyword::tilde
                        ? SemanticType::makeInteger()
                        : operand.m_type,
                    .m_valueKind = unary.op == UnaryOpKeyword::bang
                        ? ExpType::boolean
                        : unary.op == UnaryOpKeyword::tilde
                        ? ExpType::integer
                        : operand.m_type.valueKind(),
                    .m_isConstant = true,
                    .m_constantValue = unary.op == UnaryOpKeyword::minus
                            && operand.m_type.kind == SemanticTypeKind::mint
                        ? normalizeMintValue(
                              -static_cast<int64_t>(operand.m_constantValue))
                        : *folded,
                };
            }
        }
        return AnalyzedExp {
            .m_type = unary.op == UnaryOpKeyword::bang
                ? SemanticType::makeBoolean()
                : unary.op == UnaryOpKeyword::tilde
                ? SemanticType::makeInteger()
                : operand.m_type,
            .m_valueKind = unary.op == UnaryOpKeyword::bang ? ExpType::boolean
                : unary.op == UnaryOpKeyword::tilde
                ? ExpType::integer
                : operand.m_type.valueKind(),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeCastExp(
        const Exp& exp, const Exp::Cast& cast)
    {
        auto operand = analyzeExp(cast.value);
        const auto targetType = lowerBType(cast.targetType);

        // Handle poly(exp) construction: exp must be int or mint
        if (targetType.isPoly()) {
            if (operand.m_type.kind != SemanticTypeKind::integer
                && operand.m_type.kind != SemanticTypeKind::mint) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "poly() constructor requires an int or mint operand");
            }
            return AnalyzedExp {
                .m_type = targetType,
                .m_valueKind = targetType.valueKind(),
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }

        if (operand.m_valueKind == ExpType::voidType
            || operand.m_valueKind == ExpType::array) {
            recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                "explicit casts only support scalar int/mint values; arrays "
                "must be converted element by element");
            return AnalyzedExp {
                .m_type = targetType,
                .m_valueKind = targetType.valueKind(),
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }

        operand = normalizeToArithmetic(std::move(operand));
        if (operand.m_type.kind != SemanticTypeKind::integer
            && operand.m_type.kind != SemanticTypeKind::mint) {
            recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                "explicit casts only support int and mint operands");
        }

        AnalyzedExp result {
            .m_type = targetType,
            .m_valueKind = targetType.valueKind(),
            .m_isConstant = operand.m_isConstant,
            .m_constantValue = operand.m_constantValue,
        };
        if (!result.m_isConstant) {
            return result;
        }
        if (targetType.kind == SemanticTypeKind::mint) {
            result.m_constantValue = normalizeMintValue(result.m_constantValue);
        }
        return result;
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeCallExp(
        const Exp& exp, const Exp::Call& call)
    {
        const auto boundSymbolId = resolvedSymbolId(call.funcName);
        const bool hasDeclaration = boundSymbolId.has_value()
            && m_symbolResolver->hasDeclaration(*boundSymbolId);
        const auto* calleeSymbol = boundSymbolId.has_value()
            ? findSymbolById(*boundSymbolId)
            : nullptr;

        if (!hasDeclaration) {
            for (const auto arg : call.params) {
                const auto analyzedArg = analyzeExp(arg);
                if (analyzedArg.m_valueKind == ExpType::voidType) {
                    recordDiagnostic<TypeMismatchDiagnostic>(
                        exp.sourcePos.m_offset,
                        "call arguments must produce scalar values");
                }
            }
            return AnalyzedExp {
                .m_type = SemanticType::makeInteger(),
                .m_valueKind = ExpType::integer,
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }

        if (calleeSymbol != nullptr && !calleeSymbol->isFunction()) {
            recordDiagnostic<InvalidCallTargetDiagnostic>(
                exp.sourcePos.m_offset,
                "call target '" + calleeSymbol->name + "' is not a function");
        }
        if (calleeSymbol != nullptr && calleeSymbol->isFunction()
            && calleeSymbol->function().m_paramTypes.size()
                != call.params.size()) {
            recordDiagnostic<CallArityMismatchDiagnostic>(
                exp.sourcePos.m_offset,
                "call to '" + calleeSymbol->name
                    + "' uses the wrong number of arguments");
        }

        for (size_t i = 0; i < call.params.size(); ++i) {
            const auto analyzedArg = analyzeExp(call.params[i]);
            if (analyzedArg.m_valueKind == ExpType::voidType) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "call arguments must produce scalar values");
            }
            if (calleeSymbol != nullptr && calleeSymbol->isFunction()
                && i < calleeSymbol->function().m_paramTypes.size()
                && !typesMatchForCall(calleeSymbol->function().m_paramTypes[i],
                    analyzedArg.m_type)) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "call argument type does not match parameter type; use an "
                    "explicit cast for int/mint conversion");
            }
        }

        return AnalyzedExp {
            .m_type = calleeSymbol != nullptr && calleeSymbol->isFunction()
                ? calleeSymbol->function().m_returnType
                : SemanticType::makeInteger(),
            .m_valueKind = calleeSymbol != nullptr && calleeSymbol->isFunction()
                ? calleeSymbol->function().m_returnType.valueKind()
                : ExpType::integer,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeLValExp(
        const Exp& exp, const Exp::LVal& lVal)
    {
        const auto boundSymbolId = resolvedSymbolId(lVal.identifier);
        const auto* symbol = boundSymbolId.has_value()
            ? findSymbolById(*boundSymbolId)
            : nullptr;

        if (symbol != nullptr && symbol->isFunction()) {
            recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                "function '" + symbol->name + "' must be called before use");
            return AnalyzedExp {
                .m_type = SemanticType::makeInteger(),
                .m_valueKind = ExpType::integer,
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }

        auto currentType = symbol != nullptr ? symbol->object().m_type
                                             : SemanticType::makeInteger();
        for (const auto index : lVal.indices) {
            const auto analyzedIndex = analyzeExp(index);
            if (analyzedIndex.m_type.kind != SemanticTypeKind::integer) {
                recordDiagnostic<TypeMismatchDiagnostic>(
                    index(m_ast).sourcePos.m_offset,
                    "array subscript must produce an integer value");
            }
            // Handle poly[k] -> mint (coefficient extraction)
            if (currentType.isPoly()) {
                currentType = SemanticType::makeMint();
                continue;
            }
            if (!currentType.isArray()
                || currentType.m_elementType == nullptr) {
                recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                    "subscripted expression is not an array");
                currentType = SemanticType::makeInteger();
                break;
            }
            currentType = *currentType.m_elementType;
        }

        if (symbol != nullptr && symbol->isObject() && lVal.indices.empty()
            && symbol->object().constantValue) {
            return AnalyzedExp {
                .m_type = currentType,
                .m_valueKind = currentType.valueKind(),
                .m_isConstant = true,
                .m_constantValue = *symbol->object().constantValue,
            };
        }

        return AnalyzedExp {
            .m_type = currentType,
            .m_valueKind = currentType.valueKind(),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeSliceExp(
        const Exp& exp, const Exp::Slice& slice)
    {
        auto base = analyzeExp(slice.base);
        auto start = analyzeExp(slice.start);
        auto end = analyzeExp(slice.end);

        if (!base.m_type.isPoly()) {
            recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                "slice operation requires a poly base expression");
            return AnalyzedExp {
                .m_type = SemanticType::makePoly(),
                .m_valueKind = ExpType::poly,
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }
        if (start.m_type.kind != SemanticTypeKind::integer) {
            recordDiagnostic<TypeMismatchDiagnostic>(
                slice.start(m_ast).sourcePos.m_offset,
                "slice start index must be an integer");
        }
        if (end.m_type.kind != SemanticTypeKind::integer) {
            recordDiagnostic<TypeMismatchDiagnostic>(
                slice.end(m_ast).sourcePos.m_offset,
                "slice end index must be an integer");
        }
        return AnalyzedExp {
            .m_type = SemanticType::makePoly(),
            .m_valueKind = ExpType::poly,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeSubscriptExp(
        const Exp& exp, const Exp::Subscript& subscript)
    {
        auto base = analyzeExp(subscript.base);
        auto index = analyzeExp(subscript.index);

        if (!base.m_type.isPoly()) {
            recordDiagnostic<TypeMismatchDiagnostic>(exp.sourcePos.m_offset,
                "subscript operation requires a poly base expression");
            return AnalyzedExp {
                .m_type = SemanticType::makeMint(),
                .m_valueKind = ExpType::mint,
                .m_isConstant = false,
                .m_constantValue = 0,
            };
        }
        if (index.m_type.kind != SemanticTypeKind::integer) {
            recordDiagnostic<TypeMismatchDiagnostic>(
                subscript.index(m_ast).sourcePos.m_offset,
                "subscript index must be an integer");
        }
        return AnalyzedExp {
            .m_type = SemanticType::makeMint(),
            .m_valueKind = ExpType::mint,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp SemanticTypeAnalyzerImpl::analyzeExp(
        Ref<Exp> expRef)
    {
        const auto& exp = expRef(m_ast);
        auto analyzedExp = MATCH(exp.kind) WITH(
            [&](const Exp::Binary& binary) {
                return analyzeBinaryExp(exp, binary);
            },
            [&](const Exp::Unary& unary) {
                return analyzeUnaryExp(exp, unary);
            },
            [&](const Exp::Cast& cast) { return analyzeCastExp(exp, cast); },
            [&](const Exp::Call& call) { return analyzeCallExp(exp, call); },
            [&](const Exp::LVal& lVal) { return analyzeLValExp(exp, lVal); },
            [&](const Exp::Slice& slice) {
                return analyzeSliceExp(exp, slice);
            },
            [&](const Exp::Subscript& subscript) {
                return analyzeSubscriptExp(exp, subscript);
            },
            [&](const Exp::Ntt& ntt) {
                (void)analyzeExp(ntt.value);
                return AnalyzedExp {
                    .m_type = SemanticType::makePv(),
                    .m_valueKind = ExpType::pv,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            },
            [&](const Exp::Intt& intt) {
                (void)analyzeExp(intt.value);
                return AnalyzedExp {
                    .m_type = SemanticType::makePoly(),
                    .m_valueKind = ExpType::poly,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            },
            [&](const Exp::PvBinary& binary) {
                (void)analyzeExp(binary.lhs);
                (void)analyzeExp(binary.rhs);
                return AnalyzedExp {
                    .m_type = SemanticType::makePv(),
                    .m_valueKind = ExpType::pv,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            },
            [&](const Exp::Combine& combine) {
                for (const auto& term : combine.terms) {
                    (void)analyzeExp(term.value);
                    (void)analyzeExp(term.start);
                    if (term.end != nullptr) {
                        (void)analyzeExp(term.end.ref());
                    }
                    (void)analyzeExp(term.shift);
                    (void)analyzeExp(term.scale);
                }
                return AnalyzedExp {
                    .m_type = SemanticType::makePoly(),
                    .m_valueKind = ExpType::poly,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            },
            [&](const Exp::GetCoeff& getCoeff) {
                (void)analyzeExp(getCoeff.value);
                (void)analyzeExp(getCoeff.index);
                return AnalyzedExp {
                    .m_type = SemanticType::makeMint(),
                    .m_valueKind = ExpType::mint,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            },
            [&](const Exp::PolyConstruct& construct) {
                for (const auto element : construct.elements) {
                    (void)analyzeExp(element);
                }
                return AnalyzedExp {
                    .m_type = SemanticType::makePoly(),
                    .m_valueKind = ExpType::poly,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            },
            [&](const Exp::IntToMint& conversion) {
                (void)analyzeExp(conversion.value);
                return AnalyzedExp {
                    .m_type = SemanticType::makeMint(),
                    .m_valueKind = ExpType::mint,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            },
            [&](const Exp::MintToInt& conversion) {
                (void)analyzeExp(conversion.value);
                return AnalyzedExp {
                    .m_type = SemanticType::makeInteger(),
                    .m_valueKind = ExpType::integer,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            },
            [&](const Exp::Number& number) {
                return AnalyzedExp {
                    .m_type = SemanticType::makeInteger(),
                    .m_valueKind = ExpType::integer,
                    .m_isConstant = true,
                    .m_constantValue = number.value,
                };
            });

        if (analyzedExp.m_valueKind != ExpType::voidType
            && analyzedExp.m_valueKind != ExpType::array) {
            analyzedExp = normalizeToArithmetic(std::move(analyzedExp));
        }
        recordExpFacts(expRef, analyzedExp);
        return analyzedExp;
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeCondExp(Ref<Exp> exp)
    {
        auto analyzedExp = analyzeExp(exp);
        if (analyzedExp.m_valueKind == ExpType::voidType
            || analyzedExp.m_valueKind == ExpType::array
            || analyzedExp.m_valueKind == ExpType::mint) {
            recordDiagnostic<TypeMismatchDiagnostic>(
                exp(m_ast).sourcePos.m_offset,
                "condition expression must produce an integer value");
            analyzedExp.m_type = SemanticType::makeBoolean();
            analyzedExp.m_valueKind = ExpType::boolean;
            analyzedExp.m_isConstant = false;
            analyzedExp.m_constantValue = 0;
        }
        analyzedExp = normalizeToBoolean(std::move(analyzedExp));
        recordExpFacts(exp, analyzedExp);
        return analyzedExp;
    }

    SemanticType SemanticTypeAnalyzerImpl::analyzeObjectType(BTypeKeyword bType,
        const std::vector<Ref<Exp>>& dimensions, int32_t offset,
        bool allowUnsizedFirstDimension)
    {
        auto objectType = lowerBType(bType);
        for (auto dimIt = dimensions.rbegin(); dimIt != dimensions.rend();
            ++dimIt) {
            const auto analyzedDim = analyzeExp(*dimIt);
            if (analyzedDim.m_type.kind != SemanticTypeKind::integer
                || !analyzedDim.m_isConstant) {
                recordDiagnostic<TypeMismatchDiagnostic>(offset,
                    "array dimension must be a constant integer expression");
                objectType = SemanticType::makeArray(objectType, 0);
                continue;
            }
            objectType = SemanticType::makeArray(
                objectType, analyzedDim.m_constantValue);
        }
        if (allowUnsizedFirstDimension) {
            objectType = SemanticType::makeUnsizedArray(objectType);
        }
        return objectType;
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeConstInitVal(
        Ref<ConstInitVal> constInitVal, const SemanticType& expectedType,
        bool isOutermost, size_t& nextIndex, bool& hasRemainingWarning)
    {
        const auto& init = constInitVal(m_ast);
        AnalyzedExp analyzedInit {
            .m_type = expectedType,
            .m_valueKind = expectedType.valueKind(),
            .m_isConstant = false,
            .m_constantValue = 0,
        };

        const auto recordExcessInitializer = [&](int32_t offset) {
            if (hasRemainingWarning) {
                return;
            }
            recordDiagnostic<ExcessInitializerElementsDiagnostic>(offset,
                "excess initializer elements", DiagnosticSeverity::warning);
            hasRemainingWarning = true;
        };

        MATCH(init.kind)
        WITH(
            [&](Ref<Exp> expr) {
                if (!expectedType.isArray()) {
                    analyzedInit = analyzeExp(expr);
                    ++nextIndex;
                    if (analyzedInit.m_type.isVoid()
                        || analyzedInit.m_type.isArray()) {
                        recordDiagnostic<TypeMismatchDiagnostic>(
                            init.sourcePos.m_offset,
                            "const initializer must produce a scalar value");
                    } else if (analyzedInit.m_type != expectedType) {
                        recordDiagnostic<TypeMismatchDiagnostic>(
                            init.sourcePos.m_offset,
                            "const initializer type does not match declaration "
                            "type; use an explicit cast for int/mint "
                            "conversion");
                    }
                    if (!analyzedInit.m_isConstant) {
                        recordDiagnostic<NonConstantConstInitializerDiagnostic>(
                            init.sourcePos.m_offset,
                            "const initializer must be a constant expression");
                    }
                } else {
                    const std::vector<Ref<ConstInitVal>> singleton {
                        constInitVal
                    };
                    size_t nextValueIndex = 0;
                    analyzedInit = analyzeConstInitSequence(singleton,
                        nextValueIndex, expectedType, hasRemainingWarning);
                    nextIndex += nextValueIndex;
                }
            },
            [&](const ConstInitVal::List& initList) {
                if (!expectedType.isArray()) {
                    if (!initList.empty()) {
                        size_t consumed = 0;
                        analyzedInit = analyzeConstInitVal(initList.front(),
                            expectedType, false, consumed, hasRemainingWarning);
                        nextIndex += consumed;
                    }
                    if (initList.size() > 1) {
                        recordExcessInitializer(init.sourcePos.m_offset);
                    }
                } else {
                    size_t nextValueIndex = 0;
                    analyzedInit = analyzeConstInitSequence(initList,
                        nextValueIndex, expectedType, hasRemainingWarning);
                    nextIndex += nextValueIndex;
                }
            });

        if (isOutermost && expectedType.isArray()) {
            analyzedInit.m_isConstant = false;
            analyzedInit.m_constantValue = 0;
        }
        return analyzedInit;
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeConstInitSequence(
        const std::vector<Ref<ConstInitVal>>& values, size_t& nextValueIndex,
        const SemanticType& expectedType, bool& hasRemainingWarning)
    {
        if (!expectedType.isArray()) {
            if (nextValueIndex >= values.size()) {
                return AnalyzedExp {
                    .m_type = expectedType,
                    .m_valueKind = expectedType.valueKind(),
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            }

            size_t consumed = 0;
            auto analyzedInit = analyzeConstInitVal(values[nextValueIndex],
                expectedType, false, consumed, hasRemainingWarning);
            ++nextValueIndex;
            return analyzedInit;
        }

        if (expectedType.m_elementType == nullptr) {
            throw std::runtime_error("array type is missing element type");
        }

        for (int32_t i = 0;
            i < expectedType.m_arrayLength && nextValueIndex < values.size();
            ++i) {
            MATCH(values[nextValueIndex](m_ast).kind)
            WITH(
                [&](Ref<Exp>) {
                    (void)analyzeConstInitSequence(values, nextValueIndex,
                        *expectedType.m_elementType, hasRemainingWarning);
                },
                [&](const ConstInitVal::List&) {
                    size_t consumed = 0;
                    (void)analyzeConstInitVal(values[nextValueIndex],
                        *expectedType.m_elementType, false, consumed,
                        hasRemainingWarning);
                    ++nextValueIndex;
                });
        }

        return AnalyzedExp {
            .m_type = expectedType,
            .m_valueKind = expectedType.valueKind(),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeInitVal(Ref<InitVal> initVal,
        const SemanticType& expectedType, bool isGlobal, bool isOutermost,
        size_t& nextIndex, bool& hasRemainingWarning)
    {
        const auto& init = initVal(m_ast);
        AnalyzedExp analyzedInit {
            .m_type = expectedType,
            .m_valueKind = expectedType.valueKind(),
            .m_isConstant = false,
            .m_constantValue = 0,
        };

        const auto recordExcessInitializer = [&](int32_t offset) {
            if (hasRemainingWarning) {
                return;
            }
            recordDiagnostic<ExcessInitializerElementsDiagnostic>(offset,
                "excess initializer elements", DiagnosticSeverity::warning);
            hasRemainingWarning = true;
        };

        MATCH(init.kind)
        WITH(
            [&](Ref<Exp> initAlt) {
                if (!expectedType.isArray()) {
                    analyzedInit = analyzeExp(initAlt);
                    ++nextIndex;
                    if (analyzedInit.m_type.isVoid()
                        || analyzedInit.m_type.isArray()) {
                        recordDiagnostic<TypeMismatchDiagnostic>(
                            init.sourcePos.m_offset,
                            "variable initializer must produce a scalar value");
                    } else if (analyzedInit.m_type != expectedType) {
                        recordDiagnostic<TypeMismatchDiagnostic>(
                            init.sourcePos.m_offset,
                            "variable initializer type does not match "
                            "declaration type; use an explicit cast for "
                            "int/mint conversion");
                    }
                    if (isGlobal && !analyzedInit.m_isConstant) {
                        recordDiagnostic<
                            NonConstantGlobalInitializerDiagnostic>(
                            init.sourcePos.m_offset,
                            "global initializer must be a constant expression");
                    }
                } else {
                    const std::vector<Ref<InitVal>> singleton { initVal };
                    size_t nextValueIndex = 0;
                    analyzedInit
                        = analyzeInitSequence(singleton, nextValueIndex,
                            expectedType, isGlobal, hasRemainingWarning);
                    nextIndex += nextValueIndex;
                }
            },
            [&](const InitVal::List& initAlt) {
                if (!expectedType.isArray()) {
                    if (!initAlt.empty()) {
                        size_t consumed = 0;
                        analyzedInit
                            = analyzeInitVal(initAlt.front(), expectedType,
                                isGlobal, false, consumed, hasRemainingWarning);
                        nextIndex += consumed;
                    }
                    if (initAlt.size() > 1) {
                        recordExcessInitializer(init.sourcePos.m_offset);
                    }
                } else {
                    size_t nextValueIndex = 0;
                    analyzedInit = analyzeInitSequence(initAlt, nextValueIndex,
                        expectedType, isGlobal, hasRemainingWarning);
                    nextIndex += nextValueIndex;
                }
            });

        if (isOutermost && expectedType.isArray()) {
            analyzedInit.m_isConstant = false;
            analyzedInit.m_constantValue = 0;
        }
        return analyzedInit;
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::analyzeInitSequence(
        const std::vector<Ref<InitVal>>& values, size_t& nextValueIndex,
        const SemanticType& expectedType, bool isGlobal,
        bool& hasRemainingWarning)
    {
        if (!expectedType.isArray()) {
            if (nextValueIndex >= values.size()) {
                return AnalyzedExp {
                    .m_type = expectedType,
                    .m_valueKind = expectedType.valueKind(),
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            }

            size_t consumed = 0;
            auto analyzedInit = analyzeInitVal(values[nextValueIndex],
                expectedType, isGlobal, false, consumed, hasRemainingWarning);
            ++nextValueIndex;
            return analyzedInit;
        }

        if (expectedType.m_elementType == nullptr) {
            throw std::runtime_error("array type is missing element type");
        }

        for (int32_t i = 0;
            i < expectedType.m_arrayLength && nextValueIndex < values.size();
            ++i) {
            MATCH(values[nextValueIndex](m_ast).kind)
            WITH(
                [&](Ref<Exp>) {
                    (void)analyzeInitSequence(values, nextValueIndex,
                        *expectedType.m_elementType, isGlobal,
                        hasRemainingWarning);
                },
                [&](const InitVal::List&) {
                    size_t consumed = 0;
                    (void)analyzeInitVal(values[nextValueIndex],
                        *expectedType.m_elementType, isGlobal, false, consumed,
                        hasRemainingWarning);
                    ++nextValueIndex;
                });
        }

        return AnalyzedExp {
            .m_type = expectedType,
            .m_valueKind = expectedType.valueKind(),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }

    bool SemanticTypeAnalyzerImpl::typesMatchForCall(
        const SemanticType& paramType, const SemanticType& argType) const
    {
        return typesMatchForCallImpl(paramType, argType);
    }

    std::optional<int32_t> SemanticTypeAnalyzerImpl::resolvedSymbolId(
        Ref<Identifier> identifier) const
    {
        return m_symbolResolver->findSymbolId(identifier);
    }

    SemanticSymbol SemanticTypeAnalyzerImpl::makeObjectSymbol(int32_t symbolId,
        Ref<Identifier> ident, bool isConst,
        std::optional<int32_t> constantValue, const SemanticType& type) const
    {
        const auto& identifier = ident(m_ast);
        const std::string symbolName = type.isArray()
            ? std::string(isConst ? "c_" : "v_") + identifier.name
            : identifier.name;
        return SemanticSymbol { .m_id = symbolId,
            .name = symbolName,
            .info = SemanticSymbol::ObjectInfo {
                .m_isConst = isConst,
                .constantValue = constantValue,
                .m_type = type,
            } };
    }

    SemanticSymbol SemanticTypeAnalyzerImpl::makeFunctionSymbol(
        Ref<Identifier> ident, int32_t symbolId, const SemanticType& returnType,
        const std::vector<SemanticType>& paramTypes) const
    {
        const auto& identifier = ident(m_ast);
        return SemanticSymbol { .m_id = symbolId,
            .name = identifier.name,
            .info = SemanticSymbol::FunctionInfo {
                .m_returnType = returnType,
                .m_paramTypes = paramTypes,
            } };
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::normalizeToArithmetic(
        AnalyzedExp analyzedExp) const
    {
        if (analyzedExp.m_valueKind == ExpType::voidType
            || analyzedExp.m_valueKind == ExpType::array
            || analyzedExp.m_valueKind == ExpType::integer
            || analyzedExp.m_valueKind == ExpType::mint
            || analyzedExp.m_valueKind == ExpType::poly
            || analyzedExp.m_valueKind == ExpType::pv) {
            return analyzedExp;
        }

        analyzedExp.m_type = SemanticType::makeInteger();
        analyzedExp.m_valueKind = ExpType::integer;
        if (analyzedExp.m_isConstant) {
            analyzedExp.m_constantValue
                = analyzedExp.m_constantValue != 0 ? 1 : 0;
        }
        return analyzedExp;
    }

    SemanticTypeAnalyzerImpl::AnalyzedExp
    SemanticTypeAnalyzerImpl::normalizeToBoolean(AnalyzedExp analyzedExp) const
    {
        if (analyzedExp.m_valueKind == ExpType::voidType
            || analyzedExp.m_valueKind == ExpType::array
            || analyzedExp.m_valueKind == ExpType::boolean
            || analyzedExp.m_valueKind == ExpType::poly
            || analyzedExp.m_valueKind == ExpType::pv) {
            return analyzedExp;
        }

        analyzedExp.m_type = SemanticType::makeBoolean();
        analyzedExp.m_valueKind = ExpType::boolean;
        if (analyzedExp.m_isConstant) {
            analyzedExp.m_constantValue
                = analyzedExp.m_constantValue != 0 ? 1 : 0;
        }
        return analyzedExp;
    }

    void SemanticTypeAnalyzerImpl::recordExpFacts(
        Ref<Exp> exp, const AnalyzedExp& analyzedExp)
    {
        m_expInfoByExp[exp] = SemanticExpInfo {
            .m_type = analyzedExp.m_valueKind,
            .m_semanticType = analyzedExp.m_type,
            .m_constantValue = analyzedExp.m_isConstant
                ? std::optional<int32_t>(analyzedExp.m_constantValue)
                : std::nullopt,
        };
    }

} // namespace detail

SemanticTypeAnalyzer::SemanticTypeAnalyzer(
    const AST& ast, const SemanticSymbolResolver& symbolResult)
    : m_impl(
          std::make_unique<detail::SemanticTypeAnalyzerImpl>(ast, symbolResult))
{
}

SemanticTypeAnalyzer::~SemanticTypeAnalyzer() = default;

void SemanticTypeAnalyzer::analyze(Ref<CompUnit> compUnit)
{
    m_impl->analyze(compUnit);
}

const SemanticTypeAnalysisResult* SemanticTypeAnalyzer::operator->() const
{
    return m_impl.get();
}

} // namespace yesod::frontend
