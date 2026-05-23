#include "frontend/semantic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace yesod::frontend {

namespace {

    bool isScalarTypeImpl(const SemanticType& type)
    {
        return type.kind == SemanticTypeKind::integer
            || type.kind == SemanticTypeKind::boolean;
    }

    bool isArrayTypeImpl(const SemanticType& type)
    {
        return type.kind == SemanticTypeKind::array;
    }

    bool typesMatchExactly(const SemanticType& lhs, const SemanticType& rhs)
    {
        return lhs == rhs;
    }

    bool typesMatchForCallImpl(
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

    std::optional<int32_t> applyUnaryOp(UnaryOpKeyword op, int32_t operand)
    {
        switch (op) {
        case UnaryOpKeyword::plus:
            return operand;
        case UnaryOpKeyword::minus:
            return -operand;
        case UnaryOpKeyword::bang:
            return operand == 0 ? 1 : 0;
        }
        return std::nullopt;
    }

    std::optional<int32_t> applyArithmeticOp(
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

    int32_t applyRelOp(BinaryOpKeyword op, int32_t lhs, int32_t rhs)
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

    int32_t applyEqOp(BinaryOpKeyword op, int32_t lhs, int32_t rhs)
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

    int32_t applyLAndOp(BinaryOpKeyword, int32_t lhs, int32_t rhs)
    {
        return (lhs != 0 && rhs != 0) ? 1 : 0;
    }

    int32_t applyLOrOp(BinaryOpKeyword, int32_t lhs, int32_t rhs)
    {
        return (lhs != 0 || rhs != 0) ? 1 : 0;
    }

} // namespace

SemanticOutput SemanticAnalyzer::analyze(AST ast, Ptr<CompUnit> compUnit_nn)
{
    m_ast = std::move(ast);
    m_root_nn = compUnit_nn;
    m_info = SemanticInfo { };
    m_scopeStack.clear();
    m_loopStack.clear();
    m_diagnostics.clear();
    m_currentFuncReturnType.reset();
    m_nextSymbolId = 0;

    if (compUnit_nn) {
        analyzeCompUnit(compUnit_nn);
    }

    return SemanticOutput {
        .m_ast = std::move(m_ast),
        .m_root = m_root_nn,
        .m_info = m_info,
        .m_diagnostics = m_diagnostics,
    };
}

void SemanticAnalyzer::analyzeCompUnit(Ptr<CompUnit> compUnit_nn)
{
    const auto& compUnit = node(compUnit_nn, "compilation unit is missing");
    pushScope();
    declareBuiltinFunctions();

    for (const auto topLevelItem : compUnit.topLevelItems) {
        MATCH(topLevelItem)
        WITH([&](Decl decl) { analyzeDeclNode(decl); },
            [&](Ptr<FuncDef> funcDef) { declareFuncDef(funcDef); });
    }

    for (const auto topLevelItem : compUnit.topLevelItems) {
        MATCH(topLevelItem)
        WITH(
            [&](Decl decl) {
                MATCH(decl)
                WITH([&](Ref<VarDecl> vardecl) {
                    analyzeVarDecl(vardecl.ptr());
                },
                    [](const auto&) { });
            },
            [&](Ref<FuncDef>) { });
    }

    for (const auto topLevelItem : compUnit.topLevelItems) {
        MATCH(topLevelItem)
        WITH([&](Ref<FuncDef> funcDef) { analyzeFuncDef(funcDef.ptr()); },
            [](const auto&) { });
    }
}

void SemanticAnalyzer::declareBuiltinFunctions()
{
    struct BuiltinSpec {
        const char* name;
        SemanticType m_returnType;
        std::vector<SemanticType> m_paramTypes;
    };

    const std::vector<BuiltinSpec> builtins {
        { "getint", SemanticType::makeInteger(), { } },
        { "getch", SemanticType::makeInteger(), { } },
        { "getarray", SemanticType::makeInteger(),
            { SemanticType::makeUnsizedArray(SemanticType::makeInteger()) } },
        { "putint", SemanticType::makeVoid(), { SemanticType::makeInteger() } },
        { "putch", SemanticType::makeVoid(), { SemanticType::makeInteger() } },
        { "putarray", SemanticType::makeVoid(),
            { SemanticType::makeInteger(),
                SemanticType::makeUnsizedArray(SemanticType::makeInteger()) } },
        { "starttime", SemanticType::makeVoid(), { } },
        { "stoptime", SemanticType::makeVoid(), { } },
    };

    for (const auto& builtin : builtins) {
        const auto identifier_nn = m_ast.alloc<Identifier>(
            SourcePos(-1), std::string(builtin.name));
        const auto symbol = makeFunctionSymbol(
            identifier_nn, builtin.m_returnType, builtin.m_paramTypes);
        if (!defineSymbol(builtin.name, identifier_nn)) {
            continue;
        }
        bindSymbol(identifier_nn, symbol);
    }
}

void SemanticAnalyzer::declareFuncDef(Ptr<FuncDef> funcDef_nn)
{
    const auto& funcDef
        = node(funcDef_nn, "compilation unit is missing a function");
    const auto& identifier = funcDef.identifier(m_ast);
    std::vector<SemanticType> paramTypes;
    paramTypes.reserve(funcDef.funcFParams.size());
    for (const auto& funcFParam : funcDef.funcFParams) {
        auto paramType = analyzeObjectType(
            funcFParam.shape, funcFParam.sourcePos.m_offset);
        if (funcFParam.shape.empty()) {
            paramType = SemanticType::makeUnsizedArray(paramType);
        }
        paramTypes.push_back(paramType);
    }
    const auto symbol = makeFunctionSymbol(
        funcDef.identifier, lowerFuncType(funcDef.m_funcType), paramTypes);
    if (!defineSymbol(identifier.name, funcDef.identifier)) {
        recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
            identifier.sourcePos.m_offset,
            "double definition of '" + identifier.name + "'");
    }
    bindSymbol(funcDef.identifier, symbol);
}

void SemanticAnalyzer::analyzeFuncDef(Ptr<FuncDef> funcDef_nn)
{
    const auto& funcDef
        = node(funcDef_nn, "compilation unit is missing a function");
    const auto& body = funcDef.body;
    const auto* functionSymbol = m_info.findSymbol(funcDef.identifier);
    if (functionSymbol == nullptr) {
        throw std::runtime_error(
            "function definition is missing a declared function symbol");
    }

    const auto previousReturnType = m_currentFuncReturnType;
    m_currentFuncReturnType = functionSymbol->m_functionSignature.m_returnType;
    pushScope();
    for (size_t i = 0; i != funcDef.funcFParams.size(); i++) {
        const auto& funcFParam = funcDef.funcFParams[i];
        const auto& identifier = funcFParam.identifier(m_ast);
        const auto symbol = makeObjectSymbol(funcFParam.identifier, false,
            false, 0, functionSymbol->m_functionSignature.m_paramTypes[i]);
        if (!defineSymbol(identifier.name, funcFParam.identifier)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.sourcePos.m_offset,
                "double definition of '" + identifier.name + "'");
        }
        bindSymbol(funcFParam.identifier, symbol);
    }
    for (const auto blockItem : body(m_ast).items) {
        analyzeBlockItemNode(blockItem);
    }
    popScope();
    m_currentFuncReturnType = previousReturnType;
}

void SemanticAnalyzer::analyzeBlock(Ptr<Block> block_nn)
{
    const auto& body = node(block_nn, "function definition is missing a body");
    pushScope();
    for (const auto blockItem : body.items) {
        analyzeBlockItemNode(blockItem);
    }
    popScope();
}

void SemanticAnalyzer::analyzeBlockItemNode(BlockItem blockItem)
{
    MATCH(blockItem)
    WITH([&](Decl decl) { analyzeDeclNode(decl); },
        [&](Stmt stmt) { analyzeStmtNode(stmt); });
}

void SemanticAnalyzer::analyzeDeclNode(Decl declNode)
{
    MATCH(declNode)
    WITH([&](Ptr<ConstDecl> constDecl) { analyzeConstDecl(constDecl); },
        [&](Ptr<VarDecl> varDecl) { analyzeVarDecl(varDecl); });
}

void SemanticAnalyzer::analyzeConstDecl(Ptr<ConstDecl> constDecl_nn)
{
    const auto& constDecl
        = node(constDecl_nn, "const declaration payload is missing");
    for (const auto constDef : constDecl.constDef) {
        const auto& parsedConstDef = constDef(m_ast);
        const auto& identifier = parsedConstDef.identifier(m_ast);
        const auto objectType = analyzeObjectType(
            parsedConstDef.shape, parsedConstDef.sourcePos.m_offset);

        size_t nextIndex = 0;
        bool hasRemainingWarning = false;
        auto analyzedInit = analyzeConstInitVal(parsedConstDef.constInitVal,
            objectType, true, nextIndex, hasRemainingWarning);

        if (!parsedConstDef.shape.empty()) {
            analyzedInit.m_isConstant = false;
            analyzedInit.m_constantValue = 0;
        } else {
            if (analyzedInit.m_type.isVoid() || analyzedInit.m_type.isArray()) {
                recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                    parsedConstDef.sourcePos.m_offset,
                    "const initializer must produce an integer value");
            }
            if (!analyzedInit.m_isConstant) {
                recordDiagnostic(
                    SemanticDiagnosticKind::nonConstantConstInitializer,
                    parsedConstDef.sourcePos.m_offset,
                    "const initializer must be a constant expression");
            }
        }

        const auto symbol = makeObjectSymbol(parsedConstDef.identifier, true,
            parsedConstDef.shape.empty() && analyzedInit.m_isConstant,
            analyzedInit.m_constantValue, objectType);
        if (!defineSymbol(identifier.name, parsedConstDef.identifier)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.sourcePos.m_offset,
                "double definition of '" + identifier.name + "'");
        }
        bindSymbol(parsedConstDef.identifier, symbol);
    }
}

void SemanticAnalyzer::analyzeVarDecl(Ptr<VarDecl> varDecl_nn)
{
    const auto& varDecl
        = node(varDecl_nn, "var declaration payload is missing");
    for (const auto varDef : varDecl.varDef) {
        const auto& parsedVarDef = varDef(m_ast);
        const auto& identifier = parsedVarDef.identifier(m_ast);
        const auto objectType = analyzeObjectType(
            parsedVarDef.shape, parsedVarDef.sourcePos.m_offset);
        if (parsedVarDef.initVal) {
            size_t nextIndex = 0;
            bool hasRemainingWarning = false;
            (void)analyzeInitVal(parsedVarDef.initVal, objectType,
                isGlobalScope(), true, nextIndex, hasRemainingWarning);
        }

        if (m_info.findSymbol(parsedVarDef.identifier) == nullptr) {
            const auto symbol = makeObjectSymbol(
                parsedVarDef.identifier, false, false, 0, objectType);
            if (!defineSymbol(identifier.name, parsedVarDef.identifier)) {
                recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                    identifier.sourcePos.m_offset,
                    "double definition of '" + identifier.name + "'");
            }
            bindSymbol(parsedVarDef.identifier, symbol);
        }
    }
}

void SemanticAnalyzer::declareVarDecl(Ptr<VarDecl> varDecl_nn)
{
    const auto& varDecl
        = node(varDecl_nn, "var declaration payload is missing");
    for (const auto varDef : varDecl.varDef) {
        const auto& parsedVarDef = varDef(m_ast);
        const auto& identifier = parsedVarDef.identifier(m_ast);
        const auto objectType = analyzeObjectType(
            parsedVarDef.shape, parsedVarDef.sourcePos.m_offset);
        if (m_info.findSymbol(parsedVarDef.identifier) != nullptr) {
            continue;
        }
        const auto symbol = makeObjectSymbol(
            parsedVarDef.identifier, false, false, 0, objectType);
        if (!defineSymbol(identifier.name, parsedVarDef.identifier)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.sourcePos.m_offset,
                "double definition of '" + identifier.name + "'");
            continue;
        }
        bindSymbol(parsedVarDef.identifier, symbol);
    }
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeConstInitVal(
    Ptr<ConstInitVal> constInitVal_nn, const SemanticType& expectedType,
    bool isOutermost, size_t& nextIndex, bool& hasRemainingWarning)
{
    const auto& init
        = node(constInitVal_nn, "const initializer element is missing");
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
        recordDiagnostic(SemanticDiagnosticKind::excessInitializerElements,
            offset, "excess initializer elements",
            SemanticDiagnosticSeverity::warning);
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
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        init.sourcePos.m_offset,
                        "const initializer must produce an integer value");
                }
                if (!analyzedInit.m_isConstant) {
                    recordDiagnostic(
                        SemanticDiagnosticKind::nonConstantConstInitializer,
                        init.sourcePos.m_offset,
                        "const initializer must be a constant expression");
                }
            } else {
                const std::vector<Ref<ConstInitVal>> singleton {
                    constInitVal_nn.ref()
                };
                size_t nextValueIndex = 0;
                analyzedInit = analyzeConstInitSequence(singleton,
                    nextValueIndex, expectedType, hasRemainingWarning);
                nextIndex += nextValueIndex;
            }
        },
        [&](const ConstInitVal::List& initList) {
            auto& initValues = initList;
            if (!expectedType.isArray()) {
                if (!initValues.empty()) {
                    size_t consumed = 0;
                    analyzedInit = analyzeConstInitVal(initValues.front(),
                        expectedType, false, consumed, hasRemainingWarning);
                    nextIndex += consumed;
                }
                if (initValues.size() > 1) {
                    recordExcessInitializer(init.sourcePos.m_offset);
                }
            } else {
                size_t nextValueIndex = 0;
                analyzedInit = analyzeConstInitSequence(initValues,
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

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeConstInitSequence(
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
        i < expectedType.m_arrayLength && nextValueIndex < values.size(); ++i) {
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

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeInitVal(
    Ptr<InitVal> initVal_nn, const SemanticType& expectedType, bool isGlobal,
    bool isOutermost, size_t& nextIndex, bool& hasRemainingWarning)
{
    const auto& init = node(initVal_nn, "initializer element is missing");
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
        recordDiagnostic(SemanticDiagnosticKind::excessInitializerElements,
            offset, "excess initializer elements",
            SemanticDiagnosticSeverity::warning);
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
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        init.sourcePos.m_offset,
                        "variable initializer must produce an integer "
                        "value");
                }
                if (isGlobal && !analyzedInit.m_isConstant) {
                    recordDiagnostic(
                        SemanticDiagnosticKind::nonConstantGlobalInitializer,
                        init.sourcePos.m_offset,
                        "global initializer must be a constant "
                        "expression");
                }
            } else {
                const std::vector<Ref<InitVal>> singleton { initVal_nn.ref() };
                size_t nextValueIndex = 0;
                analyzedInit = analyzeInitSequence(singleton, nextValueIndex,
                    expectedType, isGlobal, hasRemainingWarning);
                nextIndex += nextValueIndex;
            }
        },
        [&](const InitVal::List& initAlt) {
            if (!expectedType.isArray()) {
                if (!initAlt.empty()) {
                    size_t consumed = 0;
                    analyzedInit = analyzeInitVal(initAlt.front(), expectedType,
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

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeInitSequence(
    const std::vector<Ref<InitVal>>& values, size_t& nextValueIndex,
    const SemanticType& expectedType, bool isGlobal, bool& hasRemainingWarning)
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
        auto analyzedInit = analyzeInitVal(values[nextValueIndex], expectedType,
            isGlobal, false, consumed, hasRemainingWarning);
        ++nextValueIndex;
        return analyzedInit;
    }

    if (expectedType.m_elementType == nullptr) {
        throw std::runtime_error("array type is missing element type");
    }

    for (int32_t i = 0;
        i < expectedType.m_arrayLength && nextValueIndex < values.size(); ++i) {
        MATCH(values[nextValueIndex](m_ast).kind)
        WITH(
            [&](Ref<Exp>) {
                (void)analyzeInitSequence(values, nextValueIndex,
                    *expectedType.m_elementType, isGlobal, hasRemainingWarning);
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

void SemanticAnalyzer::analyzeStmtNode(Stmt stmtNode)
{
    MATCH(stmtNode)
    WITH([&](Ptr<IfStmt> ifStmt) { analyzeIfStmt(ifStmt); },
        [&](Ptr<WhileStmt> whileStmt) { analyzeWhileStmt(whileStmt); },
        [&](Ptr<BreakStmt> breakStmt) { analyzeBreakStmt(breakStmt); },
        [&](Ptr<ContinueStmt> continueStmt) {
            analyzeContinueStmt(continueStmt);
        },
        [&](Ptr<AssignStmt> assignStmt) { analyzeAssignStmt(assignStmt); },
        [&](Ptr<Block> body) { analyzeBlock(body); },
        [&](Ptr<ReturnStmt> returnStmt) { analyzeReturnStmt(returnStmt); },
        [&](Ptr<ExpStmt> expStmt) { analyzeExpStmt(expStmt); });
}

void SemanticAnalyzer::analyzeIfStmt(Ptr<IfStmt> ifStmt_nn)
{
    const auto& ifStmt = node(ifStmt_nn, "if statement payload is missing");
    (void)analyzeCondExp(ifStmt.condition);
    analyzeStmtNode(ifStmt.thenBody);
    analyzeStmtNode(ifStmt.elseBody);
}

void SemanticAnalyzer::analyzeWhileStmt(Ptr<WhileStmt> whileStmt_nn)
{
    const auto& whileStmt
        = node(whileStmt_nn, "while statement payload is missing");
    m_loopStack.push_back(whileStmt_nn);
    (void)analyzeCondExp(whileStmt.condition);
    analyzeStmtNode(whileStmt.body);
    m_loopStack.pop_back();
}

void SemanticAnalyzer::analyzeBreakStmt(Ptr<BreakStmt> breakStmt_nn)
{
    const auto& breakStmt
        = node(breakStmt_nn, "break statement payload is missing");
    const auto loop = currentLoop();
    if (!loop.has_value()) {
        recordDiagnostic(SemanticDiagnosticKind::breakOutsideWhile,
            breakStmt.sourcePos.m_offset,
            "break statement is not inside a while loop");
        return;
    }
    bindLoop(breakStmt_nn, *loop);
}

void SemanticAnalyzer::analyzeContinueStmt(Ptr<ContinueStmt> continueStmt_nn)
{
    const auto& continueStmt
        = node(continueStmt_nn, "continue statement payload is missing");
    const auto loop = currentLoop();
    if (!loop.has_value()) {
        recordDiagnostic(SemanticDiagnosticKind::continueOutsideWhile,
            continueStmt.sourcePos.m_offset,
            "continue statement is not inside a while loop");
        return;
    }
    bindLoop(continueStmt_nn, *loop);
}

void SemanticAnalyzer::analyzeAssignStmt(Ptr<AssignStmt> assignStmt_nn)
{
    const auto& assignStmt
        = node(assignStmt_nn, "assignment statement payload is missing");
    const auto& lValExp = assignStmt.lval(m_ast);
    MATCH(lValExp.kind)
    WITH(
        [&](Exp::Exp::LVal expAlt) {
            const auto& identifier = expAlt.identifier(m_ast);
            (void)resolveSymbol(expAlt.identifier);
            const auto* symbol = m_info.findSymbol(expAlt.identifier);
            if (symbol != nullptr && symbol->m_isConst) {
                recordDiagnostic(SemanticDiagnosticKind::assignToConst,
                    identifier.sourcePos.m_offset,
                    "cannot assign to const '" + symbol->name + "'");
            }
            auto currentType = symbol != nullptr ? symbol->m_type
                                                 : SemanticType::makeInteger();
            for (const auto index_nn : expAlt.indices) {
                const auto analyzedIndex = analyzeExp(index_nn);
                if (!isScalarType(analyzedIndex.m_type)) {
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        index_nn(m_ast).sourcePos.m_offset,
                        "array subscript must produce an integer value");
                }
                if (!currentType.isArray()
                    || currentType.m_elementType == nullptr) {
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        identifier.sourcePos.m_offset,
                        "subscripted assignment target is not an array");
                    break;
                }
                currentType = *currentType.m_elementType;
            }
            if (currentType.isArray()) {
                recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                    identifier.sourcePos.m_offset,
                    "assignment target must designate an integer object");
            }
        },
        [&](const auto&) {
            throw std::runtime_error(
                "assignment lhs is not an lvalue expression");
        });

    const auto analyzedExp = analyzeExp(assignStmt.exp);
    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            assignStmt.sourcePos.m_offset,
            "assignment rhs must produce an integer value");
    }
}

void SemanticAnalyzer::analyzeExpStmt(Ptr<ExpStmt> expStmt_nn)
{
    const auto& expStmt
        = node(expStmt_nn, "expression statement payload is missing");
    if (expStmt.exp) {
        (void)analyzeExp(expStmt.exp.ref());
    }
}

void SemanticAnalyzer::analyzeReturnStmt(Ptr<ReturnStmt> returnStmt_nn)
{
    const auto& returnStmt
        = node(returnStmt_nn, "return statement payload is missing");
    if (!m_currentFuncReturnType.has_value()) {
        if (returnStmt.exp) {
            (void)analyzeExp(returnStmt.exp.ref());
        }
        return;
    }

    if (!returnStmt.exp) {
        if (m_currentFuncReturnType->kind != SemanticTypeKind::voidType) {
            recordDiagnostic(SemanticDiagnosticKind::returnTypeMismatch,
                returnStmt.sourcePos.m_offset,
                "non-void function must return an integer value");
        }
        return;
    }

    const auto analyzedExp = analyzeExp(returnStmt.exp.ref());
    if (m_currentFuncReturnType->kind == SemanticTypeKind::voidType) {
        recordDiagnostic(SemanticDiagnosticKind::returnTypeMismatch,
            returnStmt.sourcePos.m_offset,
            "void function must use 'return;' without a value");
        return;
    }

    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array) {
        recordDiagnostic(SemanticDiagnosticKind::returnTypeMismatch,
            returnStmt.sourcePos.m_offset,
            "return expression must produce an integer value");
    }
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeBinaryExp(
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

    if (!isScalarType(lhs.m_type) || !isScalarType(rhs.m_type)) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            exp.sourcePos.m_offset,
            "binary operator requires integer operands");
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
        if (lhs.m_isConstant && rhs.m_isConstant) {
            const auto folded = applyArithmeticOp(
                binary.op, lhs.m_constantValue, rhs.m_constantValue);
            if (folded.has_value()) {
                binaryExp.m_isConstant = true;
                binaryExp.m_constantValue = *folded;
            }
        }
        break;
    }
    return binaryExp;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeUnaryExp(
    const Exp& exp, const Exp::Unary& unary)
{
    auto operand = analyzeExp(unary.lhs);
    if (!isScalarType(operand.m_type)) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            exp.sourcePos.m_offset,
            "unary operator requires an integer value operand");
        return AnalyzedExp {
            .m_type = unary.op == UnaryOpKeyword::bang
                ? SemanticType::makeBoolean()
                : SemanticType::makeInteger(),
            .m_valueKind = unary.op == UnaryOpKeyword::bang ? ExpType::boolean
                                                            : ExpType::integer,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    if (unary.op == UnaryOpKeyword::bang) {
        operand = normalizeToBoolean(std::move(operand));
    } else {
        operand = normalizeToArithmetic(std::move(operand));
    }
    if (operand.m_isConstant) {
        const auto folded = applyUnaryOp(unary.op, operand.m_constantValue);
        if (folded.has_value()) {
            return AnalyzedExp {
                .m_type = unary.op == UnaryOpKeyword::bang
                    ? SemanticType::makeBoolean()
                    : SemanticType::makeInteger(),
                .m_valueKind = unary.op == UnaryOpKeyword::bang
                    ? ExpType::boolean
                    : ExpType::integer,
                .m_isConstant = true,
                .m_constantValue = *folded,
            };
        }
    }
    return AnalyzedExp {
        .m_type = unary.op == UnaryOpKeyword::bang
            ? SemanticType::makeBoolean()
            : SemanticType::makeInteger(),
        .m_valueKind = unary.op == UnaryOpKeyword::bang ? ExpType::boolean
                                                        : ExpType::integer,
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeCallExp(
    const Exp& exp, const Exp::Call& call)
{
    const auto definitionIdentifier = lookupSymbol(call.funcName(m_ast).name);
    if (!definitionIdentifier.has_value()) {
        const auto& calleeIdentifier = call.funcName(m_ast);
        recordDiagnostic(SemanticDiagnosticKind::useBeforeDefinition,
            calleeIdentifier.sourcePos.m_offset,
            "use of '" + calleeIdentifier.name + "' before definition");
        bindSymbol(call.funcName, makePlaceholderSymbol(call.funcName));
        for (const auto arg_nn : call.params) {
            const auto analyzedArg = analyzeExp(arg_nn);
            if (analyzedArg.m_valueKind == ExpType::voidType) {
                recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                    exp.sourcePos.m_offset,
                    "call arguments must produce integer values");
            }
        }
        return AnalyzedExp {
            .m_type = SemanticType::makeInteger(),
            .m_valueKind = ExpType::integer,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    const auto* calleeSymbol = m_info.findSymbol(*definitionIdentifier);
    if (calleeSymbol == nullptr) {
        throw std::runtime_error(
            "call target definition is missing symbol binding");
    }
    bindSymbol(call.funcName, *calleeSymbol);
    if (calleeSymbol->kind != SemanticSymbolKind::function) {
        recordDiagnostic(SemanticDiagnosticKind::invalidCallTarget,
            exp.sourcePos.m_offset,
            "call target '" + calleeSymbol->name + "' is not a function");
    }
    if (calleeSymbol->kind == SemanticSymbolKind::function
        && calleeSymbol->m_functionSignature.m_paramTypes.size()
            != call.params.size()) {
        recordDiagnostic(SemanticDiagnosticKind::callArityMismatch,
            exp.sourcePos.m_offset,
            "call to '" + calleeSymbol->name
                + "' uses the wrong number of arguments");
    }
    for (size_t i = 0; i < call.params.size(); ++i) {
        const auto analyzedArg = analyzeExp(call.params[i]);
        if (analyzedArg.m_valueKind == ExpType::voidType) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                exp.sourcePos.m_offset,
                "call arguments must produce integer values");
        }
        if (calleeSymbol->kind == SemanticSymbolKind::function
            && i < calleeSymbol->m_functionSignature.m_paramTypes.size()
            && !typesMatchForCall(
                calleeSymbol->m_functionSignature.m_paramTypes[i],
                analyzedArg.m_type)) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                exp.sourcePos.m_offset,
                "call argument type does not match parameter type");
        }
    }
    return AnalyzedExp {
        .m_type = calleeSymbol->kind == SemanticSymbolKind::function
            ? calleeSymbol->m_functionSignature.m_returnType
            : SemanticType::makeInteger(),
        .m_valueKind = calleeSymbol->kind == SemanticSymbolKind::function
            ? calleeSymbol->m_functionSignature.m_returnType.valueKind()
            : ExpType::integer,
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeLvalExp(
    const Exp& exp, const Exp::LVal& lval)
{
    (void)resolveSymbol(lval.identifier);
    const auto* symbol = m_info.findSymbol(lval.identifier);
    if (symbol != nullptr && symbol->kind == SemanticSymbolKind::function) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            exp.sourcePos.m_offset,
            "function '" + symbol->name + "' must be called before use");
        return AnalyzedExp {
            .m_type = SemanticType::makeInteger(),
            .m_valueKind = ExpType::integer,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    auto currentType
        = symbol != nullptr ? symbol->m_type : SemanticType::makeInteger();
    for (const auto index_nn : lval.indices) {
        const auto analyzedIndex = analyzeExp(index_nn);
        if (!isScalarType(analyzedIndex.m_type)) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                index_nn(m_ast).sourcePos.m_offset,
                "array subscript must produce an integer value");
        }
        if (!currentType.isArray() || currentType.m_elementType == nullptr) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                exp.sourcePos.m_offset,
                "subscripted expression is not an array");
            currentType = SemanticType::makeInteger();
            break;
        }
        currentType = *currentType.m_elementType;
    }
    if (symbol != nullptr && lval.indices.empty() && symbol->m_isConst
        && symbol->m_hasConstantValue) {
        return AnalyzedExp {
            .m_type = currentType,
            .m_valueKind = currentType.valueKind(),
            .m_isConstant = true,
            .m_constantValue = symbol->m_constantValue,
        };
    }

    return AnalyzedExp {
        .m_type = currentType,
        .m_valueKind = currentType.valueKind(),
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}
SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeExp(Ref<Exp> exp_nn)
{
    const auto& exp = exp_nn(m_ast);
    auto analyzedExp = MATCH(exp.kind) WITH(
        [&](const Exp::Binary& binary) {
            return analyzeBinaryExp(exp, binary);
        },
        [&](const Exp::Unary& unary) { return analyzeUnaryExp(exp, unary); },
        [&](const Exp::Call& call) { return analyzeCallExp(exp, call); },
        [&](const Exp::LVal& lval) { return analyzeLvalExp(exp, lval); },
        [&](Exp::Number number) {
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
    recordExpFacts(exp_nn, analyzedExp);
    return analyzedExp;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeCondExp(Ref<Exp> exp_nn)
{
    auto analyzedExp = analyzeExp(exp_nn);
    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            exp_nn(m_ast).sourcePos.m_offset,
            "condition expression must produce an integer value");
        analyzedExp.m_type = SemanticType::makeBoolean();
        analyzedExp.m_valueKind = ExpType::boolean;
        analyzedExp.m_isConstant = false;
        analyzedExp.m_constantValue = 0;
    }
    analyzedExp = normalizeToBoolean(std::move(analyzedExp));
    recordExpFacts(exp_nn, analyzedExp);
    return analyzedExp;
}

std::optional<Ref<Identifier>> SemanticAnalyzer::lookupSymbol(
    const std::string& name) const
{
    for (auto scopeIt = m_scopeStack.rbegin(); scopeIt != m_scopeStack.rend();
        ++scopeIt) {
        const auto foundIt = scopeIt->find(name);
        if (foundIt != scopeIt->end()) {
            return foundIt->second;
        }
    }
    return std::nullopt;
}

int32_t SemanticAnalyzer::resolveSymbol(Ref<Identifier> identifier_nn)
{
    const auto& identifier = identifier_nn(m_ast);
    const auto definitionIdentifier = lookupSymbol(identifier.name);
    if (definitionIdentifier.has_value()) {
        const auto* symbol = m_info.findSymbol(*definitionIdentifier);
        if (symbol == nullptr) {
            throw std::runtime_error(
                "definition identifier is missing symbol binding");
        }
        bindSymbol(identifier_nn, *symbol);
        return symbol->m_id;
    }

    recordDiagnostic(SemanticDiagnosticKind::useBeforeDefinition,
        identifier.sourcePos.m_offset,
        "use of '" + identifier.name + "' before definition");
    const auto placeholder = makePlaceholderSymbol(identifier_nn);
    bindSymbol(identifier_nn, placeholder);
    return placeholder.m_id;
}

SemanticSymbol SemanticAnalyzer::makePlaceholderSymbol(
    Ref<Identifier> identifier_nn)
{
    return makeObjectSymbol(
        identifier_nn, false, false, 0, SemanticType::makeInteger());
}

SemanticType SemanticAnalyzer::analyzeObjectType(
    const std::vector<Ref<Exp>>& dimensions, int32_t offset,
    bool allowUnsizedFirstDimension)
{
    auto objectType = SemanticType::makeInteger();
    for (auto dimIt = dimensions.rbegin(); dimIt != dimensions.rend();
        ++dimIt) {
        const auto analyzedDim = analyzeExp(*dimIt);
        if (!isScalarType(analyzedDim.m_type) || !analyzedDim.m_isConstant) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch, offset,
                "array dimension must be a constant integer expression");
            objectType = SemanticType::makeArray(objectType, 0);
            continue;
        }
        objectType
            = SemanticType::makeArray(objectType, analyzedDim.m_constantValue);
    }
    if (allowUnsizedFirstDimension) {
        objectType = SemanticType::makeUnsizedArray(objectType);
    }
    return objectType;
}

bool SemanticAnalyzer::typesMatchForCall(
    const SemanticType& paramType, const SemanticType& argType) const
{
    return typesMatchForCallImpl(paramType, argType);
}

bool SemanticAnalyzer::isScalarType(const SemanticType& type) const
{
    return isScalarTypeImpl(type);
}

bool SemanticAnalyzer::isArrayType(const SemanticType& type) const
{
    return isArrayTypeImpl(type);
}

SemanticSymbol SemanticAnalyzer::makeObjectSymbol(Ref<Identifier> identifier_nn,
    bool isConst, bool hasConstantValue, int32_t constantValue,
    const SemanticType& type)
{
    const auto& identifier = identifier_nn(m_ast);
    const int32_t symbolId = ++m_nextSymbolId;
    const std::string symbolName = type.isArray()
        ? std::string(isConst ? "c_" : "v_") + identifier.name
        : identifier.name;
    return SemanticSymbol {
        .m_id = symbolId,
        .name = symbolName,
        .kind = SemanticSymbolKind::object,
        .m_isConst = isConst,
        .m_hasConstantValue = hasConstantValue,
        .m_constantValue = constantValue,
        .m_type = type,
        .m_functionSignature = { },
    };
}

SemanticSymbol SemanticAnalyzer::makeFunctionSymbol(
    Ref<Identifier> identifier_nn, const SemanticType& returnType,
    const std::vector<SemanticType>& paramTypes)
{
    const auto& identifier = identifier_nn(m_ast);
    const int32_t symbolId = ++m_nextSymbolId;
    return SemanticSymbol {
        .m_id = symbolId,
        .name = identifier.name,
        .kind = SemanticSymbolKind::function,
        .m_isConst = false,
        .m_hasConstantValue = false,
        .m_constantValue = 0,
        .m_type = returnType,
        .m_functionSignature = SemanticFunctionSignature {
            .m_returnType = returnType,
            .m_paramTypes = paramTypes,
        },
    };
}

void SemanticAnalyzer::pushScope() { m_scopeStack.emplace_back(); }

void SemanticAnalyzer::popScope()
{
    if (!m_scopeStack.empty()) {
        m_scopeStack.pop_back();
    }
}

bool SemanticAnalyzer::isGlobalScope() const
{
    return m_scopeStack.size() == 1;
}

bool SemanticAnalyzer::defineSymbol(
    const std::string& name, Ref<Identifier> identifier_nn)
{
    if (m_scopeStack.empty()) {
        pushScope();
    }

    auto& currentScope = m_scopeStack.back();
    return currentScope.emplace(name, identifier_nn).second;
}

std::optional<Ptr<WhileStmt>> SemanticAnalyzer::currentLoop() const
{
    if (m_loopStack.empty()) {
        return std::nullopt;
    }
    return m_loopStack.back();
}

SemanticType SemanticAnalyzer::lowerFuncType(FuncTypeKeyword funcType) const
{
    switch (funcType) {
    case FuncTypeKeyword::voidKeyword:
        return SemanticType::makeVoid();
    case FuncTypeKeyword::intKeyword:
        return SemanticType::makeInteger();
    }

    throw std::runtime_error("unsupported function type keyword");
}

void SemanticAnalyzer::recordDiagnostic(SemanticDiagnosticKind kind,
    int32_t offset, std::string message, SemanticDiagnosticSeverity severity)
{
    m_diagnostics.push_back(SemanticDiagnostic {
        .kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
        .m_severity = severity,
    });
}

} // namespace yesod::frontend
