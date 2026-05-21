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
        return type.m_kind == SemanticTypeKind::integer
            || type.m_kind == SemanticTypeKind::boolean;
    }

    bool isArrayTypeImpl(const SemanticType& type)
    {
        return type.m_kind == SemanticTypeKind::array;
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
            return typesMatchExactly(*paramType.m_elementType,
                *argType.m_elementType);
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

SemanticOutput SemanticAnalyzer::analyze(AST ast, Handle<CompUnit> compUnit_nn)
{
    m_ast = std::move(ast);
    m_root_nn = compUnit_nn;
    m_info = SemanticInfo {};
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

void SemanticAnalyzer::analyzeCompUnit(Handle<CompUnit> compUnit_nn)
{
    const auto& compUnit = node(compUnit_nn, "compilation unit is missing");
    pushScope();
    declareBuiltinFunctions();

    for (const auto topLevelItem_nn : compUnit.m_topLevelItems) {
        const auto& topLevelItem
            = node(topLevelItem_nn, "top-level item is missing");
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType, Handle<DeclNode>>) {
                    const auto& declNode
                        = node(topLevelAlt, "declaration payload is missing");
                    std::visit(
                        [&](const auto& declAlt) {
                            using DeclAltType = std::decay_t<decltype(declAlt)>;
                            if constexpr (std::is_same_v<DeclAltType,
                                              Handle<ConstDecl>>) {
                                analyzeConstDecl(declAlt);
                            } else {
                                declareVarDecl(declAlt);
                            }
                        },
                        declNode.m_decl);
                } else {
                    declareFuncDef(topLevelAlt);
                }
            },
            topLevelItem.m_topLevelItem);
    }

    for (const auto topLevelItem_nn : compUnit.m_topLevelItems) {
        const auto& topLevelItem
            = node(topLevelItem_nn, "top-level item is missing");
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType, Handle<DeclNode>>) {
                    const auto& declNode
                        = node(topLevelAlt, "declaration payload is missing");
                    std::visit(
                        [&](const auto& declAlt) {
                            using DeclAltType = std::decay_t<decltype(declAlt)>;
                            if constexpr (std::is_same_v<DeclAltType,
                                              Handle<VarDecl>>) {
                                analyzeVarDecl(declAlt);
                            }
                        },
                        declNode.m_decl);
                }
            },
            topLevelItem.m_topLevelItem);
    }

    for (const auto topLevelItem_nn : compUnit.m_topLevelItems) {
        const auto& topLevelItem
            = node(topLevelItem_nn, "top-level item is missing");
        std::visit(
            [&](const auto& topLevelAlt) {
                using AltType = std::decay_t<decltype(topLevelAlt)>;
                if constexpr (std::is_same_v<AltType, Handle<FuncDef>>) {
                    analyzeFuncDef(topLevelAlt);
                }
            },
            topLevelItem.m_topLevelItem);
    }
}

void SemanticAnalyzer::declareBuiltinFunctions()
{
    struct BuiltinSpec {
        const char* m_name;
        SemanticType m_returnType;
        std::vector<SemanticType> m_paramTypes;
    };

    const std::vector<BuiltinSpec> builtins {
        { "getint", SemanticType::makeInteger(), {} },
        { "getch", SemanticType::makeInteger(), {} },
        { "getarray", SemanticType::makeInteger(),
            { SemanticType::makeUnsizedArray(SemanticType::makeInteger()) } },
        { "putint", SemanticType::makeVoid(), { SemanticType::makeInteger() } },
        { "putch", SemanticType::makeVoid(), { SemanticType::makeInteger() } },
        { "putarray", SemanticType::makeVoid(),
            { SemanticType::makeInteger(),
                SemanticType::makeUnsizedArray(SemanticType::makeInteger()) } },
        { "starttime", SemanticType::makeVoid(), {} },
        { "stoptime", SemanticType::makeVoid(), {} },
    };

    for (const auto& builtin : builtins) {
        const auto identifier_nn
            = m_ast.alloc<Identifier>(SourcePos(-1), std::string(builtin.m_name));
        const auto symbol = makeFunctionSymbol(
            identifier_nn, builtin.m_returnType, builtin.m_paramTypes);
        if (!defineSymbol(builtin.m_name, identifier_nn)) {
            continue;
        }
        bindSymbol(identifier_nn, symbol);
    }
}

void SemanticAnalyzer::declareFuncDef(Handle<FuncDef> funcDef_nn)
{
    const auto& funcDef
        = node(funcDef_nn, "compilation unit is missing a function");
    const auto& identifier = node(
        funcDef.m_identifier_nn, "function definition is missing an identifier");
    std::vector<SemanticType> paramTypes;
    paramTypes.reserve(funcDef.m_funcFParams.size());
    for (const auto funcFParam_nn : funcDef.m_funcFParams) {
        const auto& funcFParam
            = node(funcFParam_nn, "function parameter is missing");
        auto paramType = analyzeObjectType(
            funcFParam.m_trailingDimensions, funcFParam.m_sourcePos.m_offset);
        if (funcFParam.m_isArray) {
            paramType = SemanticType::makeUnsizedArray(paramType);
        }
        paramTypes.push_back(paramType);
    }
    const auto symbol = makeFunctionSymbol(
        funcDef.m_identifier_nn, lowerFuncType(funcDef.m_funcType), paramTypes);
    if (!defineSymbol(identifier.m_name, funcDef.m_identifier_nn)) {
        recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
            identifier.m_sourcePos.m_offset,
            "double definition of '" + identifier.m_name + "'");
    }
    bindSymbol(funcDef.m_identifier_nn, symbol);
}

void SemanticAnalyzer::analyzeFuncDef(Handle<FuncDef> funcDef_nn)
{
    const auto& funcDef
        = node(funcDef_nn, "compilation unit is missing a function");
    (void)node(funcDef.m_identifier_nn,
        "function definition is missing an identifier");
    const auto& block
        = node(funcDef.m_block_nn, "function definition is missing a block");
    const auto* functionSymbol = m_info.findSymbol(funcDef.m_identifier_nn);
    if (functionSymbol == nullptr) {
        throw std::runtime_error(
            "function definition is missing a declared function symbol");
    }

    const auto previousReturnType = m_currentFuncReturnType;
    m_currentFuncReturnType = functionSymbol->m_functionSignature.m_returnType;
    pushScope();
    for (size_t i = 0; i < funcDef.m_funcFParams.size(); ++i) {
        const auto funcFParam_nn = funcDef.m_funcFParams[i];
        const auto& funcFParam
            = node(funcFParam_nn, "function parameter is missing");
        const auto& identifier = node(funcFParam.m_identifier_nn,
            "function parameter is missing an identifier");
        const auto symbol = makeObjectSymbol(funcFParam.m_identifier_nn, false,
            false, 0,
            functionSymbol->m_functionSignature.m_paramTypes[i]);
        if (!defineSymbol(identifier.m_name, funcFParam.m_identifier_nn)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.m_sourcePos.m_offset,
                "double definition of '" + identifier.m_name + "'");
        }
        bindSymbol(funcFParam.m_identifier_nn, symbol);
    }
    for (const auto blockItem : block.m_blockItems) {
        analyzeBlockItemNode(blockItem);
    }
    popScope();
    m_currentFuncReturnType = previousReturnType;
}

void SemanticAnalyzer::analyzeBlock(Handle<Block> block_nn)
{
    const auto& block
        = node(block_nn, "function definition is missing a block");
    pushScope();
    for (const auto blockItem : block.m_blockItems) {
        analyzeBlockItemNode(blockItem);
    }
    popScope();
}

void SemanticAnalyzer::analyzeBlockItemNode(
    Handle<BlockItemNode> blockItemNode_nn)
{
    const auto& blockItemNode
        = node(blockItemNode_nn, "block contains a null item");
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<DeclNode>>) {
                analyzeDeclNode(blockItemAlt);
            } else {
                analyzeStmtNode(blockItemAlt);
            }
        },
        blockItemNode.m_blockItem);
}

void SemanticAnalyzer::analyzeDeclNode(Handle<DeclNode> declNode_nn)
{
    const auto& declNode = node(declNode_nn, "declaration payload is missing");
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<ConstDecl>>) {
                analyzeConstDecl(declAlt);
            } else {
                analyzeVarDecl(declAlt);
            }
        },
        declNode.m_decl);
}

void SemanticAnalyzer::analyzeConstDecl(Handle<ConstDecl> constDecl_nn)
{
    const auto& constDecl
        = node(constDecl_nn, "const declaration payload is missing");
    for (const auto constDef_nn : constDecl.m_constDefs) {
        const auto& parsedConstDef
            = node(constDef_nn, "const declaration contains a null declarator");
        const auto& identifier = node(parsedConstDef.m_identifier_nn,
            "const declarator is missing an identifier");
        (void)node(parsedConstDef.m_constInitVal_nn,
            "const declarator is missing an initializer");
        const auto objectType = analyzeObjectType(
            parsedConstDef.m_dimensions, parsedConstDef.m_sourcePos.m_offset);

        size_t nextIndex = 0;
        bool hasRemainingWarning = false;
        auto analyzedInit = analyzeConstInitVal(parsedConstDef.m_constInitVal_nn,
            objectType, true, nextIndex, hasRemainingWarning);

        if (!parsedConstDef.m_dimensions.empty()) {
            analyzedInit.m_isConstant = false;
            analyzedInit.m_constantValue = 0;
        } else {
            if (analyzedInit.m_type.isVoid() || analyzedInit.m_type.isArray()) {
                recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                    parsedConstDef.m_sourcePos.m_offset,
                    "const initializer must produce an integer value");
            }
            if (!analyzedInit.m_isConstant) {
                recordDiagnostic(
                    SemanticDiagnosticKind::nonConstantConstInitializer,
                    parsedConstDef.m_sourcePos.m_offset,
                    "const initializer must be a constant expression");
            }
        }

        const auto symbol = makeObjectSymbol(parsedConstDef.m_identifier_nn,
            true,
            parsedConstDef.m_dimensions.empty() && analyzedInit.m_isConstant,
            analyzedInit.m_constantValue, objectType);
        if (!defineSymbol(identifier.m_name, parsedConstDef.m_identifier_nn)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.m_sourcePos.m_offset,
                "double definition of '" + identifier.m_name + "'");
        }
        bindSymbol(parsedConstDef.m_identifier_nn, symbol);
    }
}

void SemanticAnalyzer::analyzeVarDecl(Handle<VarDecl> varDecl_nn)
{
    const auto& varDecl
        = node(varDecl_nn, "var declaration payload is missing");
    for (const auto varDef_nn : varDecl.m_varDefs) {
        const auto& parsedVarDef
            = node(varDef_nn, "var declaration contains a null declarator");
        const auto& identifier = node(parsedVarDef.m_identifier_nn,
            "var declarator is missing an identifier");
        const auto objectType = analyzeObjectType(
            parsedVarDef.m_dimensions, parsedVarDef.m_sourcePos.m_offset);
        if (parsedVarDef.m_initVal_nn) {
            size_t nextIndex = 0;
            bool hasRemainingWarning = false;
            (void)analyzeInitVal(parsedVarDef.m_initVal_nn, objectType,
                isGlobalScope(), true, nextIndex, hasRemainingWarning);
        }

        if (m_info.findSymbol(parsedVarDef.m_identifier_nn) == nullptr) {
            const auto symbol = makeObjectSymbol(parsedVarDef.m_identifier_nn,
                false, false, 0, objectType);
            if (!defineSymbol(identifier.m_name, parsedVarDef.m_identifier_nn)) {
                recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                    identifier.m_sourcePos.m_offset,
                    "double definition of '" + identifier.m_name + "'");
            }
            bindSymbol(parsedVarDef.m_identifier_nn, symbol);
        }
    }
}

void SemanticAnalyzer::declareVarDecl(Handle<VarDecl> varDecl_nn)
{
    const auto& varDecl
        = node(varDecl_nn, "var declaration payload is missing");
    for (const auto varDef_nn : varDecl.m_varDefs) {
        const auto& parsedVarDef
            = node(varDef_nn, "var declaration contains a null declarator");
        const auto& identifier = node(parsedVarDef.m_identifier_nn,
            "var declarator is missing an identifier");
        const auto objectType = analyzeObjectType(
            parsedVarDef.m_dimensions, parsedVarDef.m_sourcePos.m_offset);
        if (m_info.findSymbol(parsedVarDef.m_identifier_nn) != nullptr) {
            continue;
        }
        const auto symbol = makeObjectSymbol(parsedVarDef.m_identifier_nn,
            false, false, 0, objectType);
        if (!defineSymbol(identifier.m_name, parsedVarDef.m_identifier_nn)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.m_sourcePos.m_offset,
                "double definition of '" + identifier.m_name + "'");
            continue;
        }
        bindSymbol(parsedVarDef.m_identifier_nn, symbol);
    }
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeConstInitVal(
    Handle<ConstInitVal> constInitVal_nn, const SemanticType& expectedType,
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

    std::visit(
        [&](const auto& initAlt) {
            using AltType = std::decay_t<decltype(initAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<Exp>>) {
                if (!expectedType.isArray()) {
                    analyzedInit = analyzeExp(initAlt);
                    ++nextIndex;
                    if (analyzedInit.m_type.isVoid()
                        || analyzedInit.m_type.isArray()) {
                        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                            init.m_sourcePos.m_offset,
                            "const initializer must produce an integer value");
                    }
                    if (!analyzedInit.m_isConstant) {
                        recordDiagnostic(
                            SemanticDiagnosticKind::nonConstantConstInitializer,
                            init.m_sourcePos.m_offset,
                            "const initializer must be a constant expression");
                    }
                } else {
                    const std::vector<Handle<ConstInitVal>> singleton {
                        constInitVal_nn };
                    size_t nextValueIndex = 0;
                    analyzedInit = analyzeConstInitSequence(
                        singleton, nextValueIndex, expectedType,
                        hasRemainingWarning);
                    nextIndex += nextValueIndex;
                }
            } else {
                if (!expectedType.isArray()) {
                    if (!initAlt.m_values.empty()) {
                        size_t consumed = 0;
                        analyzedInit = analyzeConstInitVal(initAlt.m_values.front(),
                            expectedType, false, consumed, hasRemainingWarning);
                        nextIndex += consumed;
                    }
                    if (initAlt.m_values.size() > 1) {
                        recordExcessInitializer(init.m_sourcePos.m_offset);
                    }
                } else {
                    size_t nextValueIndex = 0;
                    analyzedInit = analyzeConstInitSequence(initAlt.m_values,
                        nextValueIndex, expectedType, hasRemainingWarning);
                    nextIndex += nextValueIndex;
                }
            }
        },
        init.m_kind);

    if (isOutermost && expectedType.isArray()) {
        analyzedInit.m_isConstant = false;
        analyzedInit.m_constantValue = 0;
    }
    return analyzedInit;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeConstInitSequence(
    const std::vector<Handle<ConstInitVal>>& values, size_t& nextValueIndex,
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
        const auto& child
            = node(values[nextValueIndex], "const initializer element is missing");
        std::visit(
            [&](const auto& childAlt) {
                using ChildAltType = std::decay_t<decltype(childAlt)>;
                if constexpr (std::is_same_v<ChildAltType, Handle<Exp>>) {
                    (void)analyzeConstInitSequence(values, nextValueIndex,
                        *expectedType.m_elementType, hasRemainingWarning);
                } else {
                    size_t consumed = 0;
                    (void)analyzeConstInitVal(values[nextValueIndex],
                        *expectedType.m_elementType, false, consumed,
                        hasRemainingWarning);
                    ++nextValueIndex;
                }
            },
            child.m_kind);
    }

    return AnalyzedExp {
        .m_type = expectedType,
        .m_valueKind = expectedType.valueKind(),
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeInitVal(
    Handle<InitVal> initVal_nn, const SemanticType& expectedType,
    bool isGlobal, bool isOutermost, size_t& nextIndex,
    bool& hasRemainingWarning)
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

    std::visit(
        [&](const auto& initAlt) {
            using AltType = std::decay_t<decltype(initAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<Exp>>) {
                if (!expectedType.isArray()) {
                    analyzedInit = analyzeExp(initAlt);
                    ++nextIndex;
                    if (analyzedInit.m_type.isVoid()
                        || analyzedInit.m_type.isArray()) {
                        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                            init.m_sourcePos.m_offset,
                            "variable initializer must produce an integer value");
                    }
                    if (isGlobal && !analyzedInit.m_isConstant) {
                        recordDiagnostic(
                            SemanticDiagnosticKind::nonConstantGlobalInitializer,
                            init.m_sourcePos.m_offset,
                            "global initializer must be a constant expression");
                    }
                } else {
                    const std::vector<Handle<InitVal>> singleton { initVal_nn };
                    size_t nextValueIndex = 0;
                    analyzedInit = analyzeInitSequence(singleton, nextValueIndex,
                        expectedType, isGlobal, hasRemainingWarning);
                    nextIndex += nextValueIndex;
                }
            } else {
                if (!expectedType.isArray()) {
                    if (!initAlt.m_values.empty()) {
                        size_t consumed = 0;
                        analyzedInit = analyzeInitVal(initAlt.m_values.front(),
                            expectedType, isGlobal, false, consumed,
                            hasRemainingWarning);
                        nextIndex += consumed;
                    }
                    if (initAlt.m_values.size() > 1) {
                        recordExcessInitializer(init.m_sourcePos.m_offset);
                    }
                } else {
                    size_t nextValueIndex = 0;
                    analyzedInit = analyzeInitSequence(initAlt.m_values,
                        nextValueIndex, expectedType, isGlobal,
                        hasRemainingWarning);
                    nextIndex += nextValueIndex;
                }
            }
        },
        init.m_kind);

    if (isOutermost && expectedType.isArray()) {
        analyzedInit.m_isConstant = false;
        analyzedInit.m_constantValue = 0;
    }
    return analyzedInit;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeInitSequence(
    const std::vector<Handle<InitVal>>& values, size_t& nextValueIndex,
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
        const auto& child
            = node(values[nextValueIndex], "initializer element is missing");
        std::visit(
            [&](const auto& childAlt) {
                using ChildAltType = std::decay_t<decltype(childAlt)>;
                if constexpr (std::is_same_v<ChildAltType, Handle<Exp>>) {
                    (void)analyzeInitSequence(values, nextValueIndex,
                        *expectedType.m_elementType, isGlobal,
                        hasRemainingWarning);
                } else {
                    size_t consumed = 0;
                    (void)analyzeInitVal(values[nextValueIndex],
                        *expectedType.m_elementType, isGlobal, false,
                        consumed, hasRemainingWarning);
                    ++nextValueIndex;
                }
            },
            child.m_kind);
    }

    return AnalyzedExp {
        .m_type = expectedType,
        .m_valueKind = expectedType.valueKind(),
        .m_isConstant = false,
        .m_constantValue = 0,
    };
}

void SemanticAnalyzer::analyzeStmtNode(Handle<StmtNode> stmtNode_nn)
{
    const auto& stmtNode = node(stmtNode_nn, "statement payload is missing");
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, Handle<IfStmt>>) {
                analyzeIfStmt(stmtAlt);
            } else if constexpr (std::is_same_v<AltType, Handle<WhileStmt>>) {
                analyzeWhileStmt(stmtAlt);
            } else if constexpr (std::is_same_v<AltType, Handle<BreakStmt>>) {
                analyzeBreakStmt(stmtAlt);
            } else if constexpr (std::is_same_v<AltType,
                                     Handle<ContinueStmt>>) {
                analyzeContinueStmt(stmtAlt);
            } else if constexpr (std::is_same_v<AltType, Handle<AssignStmt>>) {
                analyzeAssignStmt(stmtAlt);
            } else if constexpr (std::is_same_v<AltType, Handle<Block>>) {
                analyzeBlock(stmtAlt);
            } else if constexpr (std::is_same_v<AltType, Handle<ReturnStmt>>) {
                analyzeReturnStmt(stmtAlt);
            } else {
                analyzeExpStmt(stmtAlt);
            }
        },
        stmtNode.m_stmt);
}

void SemanticAnalyzer::analyzeIfStmt(Handle<IfStmt> ifStmt_nn)
{
    const auto& ifStmt = node(ifStmt_nn, "if statement payload is missing");
    (void)analyzeCondExp(ifStmt.m_condExp_nn);
    analyzeStmtNode(ifStmt.m_thenStmt_nn);
    if (ifStmt.m_elseStmt_nn) {
        analyzeStmtNode(ifStmt.m_elseStmt_nn);
    }
}

void SemanticAnalyzer::analyzeWhileStmt(Handle<WhileStmt> whileStmt_nn)
{
    const auto& whileStmt
        = node(whileStmt_nn, "while statement payload is missing");
    m_loopStack.push_back(whileStmt_nn);
    (void)analyzeCondExp(whileStmt.m_condExp_nn);
    analyzeStmtNode(whileStmt.m_bodyStmt_nn);
    m_loopStack.pop_back();
}

void SemanticAnalyzer::analyzeBreakStmt(Handle<BreakStmt> breakStmt_nn)
{
    const auto& breakStmt
        = node(breakStmt_nn, "break statement payload is missing");
    const auto loop = currentLoop();
    if (!loop.has_value()) {
        recordDiagnostic(SemanticDiagnosticKind::breakOutsideWhile,
            breakStmt.m_sourcePos.m_offset,
            "break statement is not inside a while loop");
        return;
    }
    bindLoop(breakStmt_nn, *loop);
}

void SemanticAnalyzer::analyzeContinueStmt(Handle<ContinueStmt> continueStmt_nn)
{
    const auto& continueStmt
        = node(continueStmt_nn, "continue statement payload is missing");
    const auto loop = currentLoop();
    if (!loop.has_value()) {
        recordDiagnostic(SemanticDiagnosticKind::continueOutsideWhile,
            continueStmt.m_sourcePos.m_offset,
            "continue statement is not inside a while loop");
        return;
    }
    bindLoop(continueStmt_nn, *loop);
}

void SemanticAnalyzer::analyzeAssignStmt(Handle<AssignStmt> assignStmt_nn)
{
    const auto& assignStmt
        = node(assignStmt_nn, "assignment statement payload is missing");
    const auto& lValExp
        = node(assignStmt.m_lVal_nn, "assignment is missing an lvalue");
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, LVal>) {
                const auto& identifier = node(expAlt.m_identifier_nn,
                    "assignment lvalue is missing an identifier");
                (void)resolveSymbol(expAlt.m_identifier_nn);
                const auto* symbol = m_info.findSymbol(expAlt.m_identifier_nn);
                if (symbol != nullptr && symbol->m_isConst) {
                    recordDiagnostic(SemanticDiagnosticKind::assignToConst,
                        identifier.m_sourcePos.m_offset,
                        "cannot assign to const '" + symbol->m_name + "'");
                }
                auto currentType
                    = symbol != nullptr ? symbol->m_type : SemanticType::makeInteger();
                for (const auto index_nn : expAlt.m_indices) {
                    const auto analyzedIndex = analyzeExp(index_nn);
                    if (!isScalarType(analyzedIndex.m_type)) {
                        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                            node(index_nn, "array subscript is missing")
                                .m_sourcePos.m_offset,
                            "array subscript must produce an integer value");
                    }
                    if (!currentType.isArray()
                        || currentType.m_elementType == nullptr) {
                        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                            identifier.m_sourcePos.m_offset,
                            "subscripted assignment target is not an array");
                        break;
                    }
                    currentType = *currentType.m_elementType;
                }
                if (currentType.isArray()) {
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        identifier.m_sourcePos.m_offset,
                        "assignment target must designate an integer object");
                }
            } else {
                throw std::runtime_error(
                    "assignment lhs is not an lvalue expression");
            }
        },
        lValExp.m_kind);

    const auto analyzedExp = analyzeExp(assignStmt.m_exp_nn);
    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            assignStmt.m_sourcePos.m_offset,
            "assignment rhs must produce an integer value");
    }
}

void SemanticAnalyzer::analyzeExpStmt(Handle<ExpStmt> expStmt_nn)
{
    const auto& expStmt
        = node(expStmt_nn, "expression statement payload is missing");
    if (expStmt.m_exp_nn) {
        (void)analyzeExp(expStmt.m_exp_nn);
    }
}

void SemanticAnalyzer::analyzeReturnStmt(Handle<ReturnStmt> returnStmt_nn)
{
    const auto& returnStmt
        = node(returnStmt_nn, "return statement payload is missing");
    if (!m_currentFuncReturnType.has_value()) {
        if (returnStmt.m_exp_nn) {
            (void)analyzeExp(returnStmt.m_exp_nn);
        }
        return;
    }

    if (!returnStmt.m_exp_nn) {
        if (m_currentFuncReturnType->m_kind != SemanticTypeKind::voidType) {
            recordDiagnostic(SemanticDiagnosticKind::returnTypeMismatch,
                returnStmt.m_sourcePos.m_offset,
                "non-void function must return an integer value");
        }
        return;
    }

    const auto analyzedExp = analyzeExp(returnStmt.m_exp_nn);
    if (m_currentFuncReturnType->m_kind == SemanticTypeKind::voidType) {
        recordDiagnostic(SemanticDiagnosticKind::returnTypeMismatch,
            returnStmt.m_sourcePos.m_offset,
            "void function must use 'return;' without a value");
        return;
    }

    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array) {
        recordDiagnostic(SemanticDiagnosticKind::returnTypeMismatch,
            returnStmt.m_sourcePos.m_offset,
            "return expression must produce an integer value");
    }
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeExp(Handle<Exp> exp_nn)
{
    const auto& exp = node(exp_nn, "expression is missing");
    auto analyzedExp = std::visit(
        [&](const auto& expAlt) -> AnalyzedExp {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, Exp::Binary>) {
                auto lhs = analyzeExp(expAlt.m_lhs_nn);
                auto rhs = analyzeExp(expAlt.m_rhs_nn);
                AnalyzedExp binaryExp {
                    .m_type = SemanticType::makeInteger(),
                    .m_valueKind = ExpType::integer,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };

                if (!isScalarType(lhs.m_type) || !isScalarType(rhs.m_type)) {
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        exp.m_sourcePos.m_offset,
                        "binary operator requires integer operands");
                    return binaryExp;
                }

                switch (expAlt.m_op) {
                case BinaryOpKeyword::orOr:
                    lhs = normalizeToBoolean(std::move(lhs));
                    rhs = normalizeToBoolean(std::move(rhs));
                    binaryExp.m_type = SemanticType::makeBoolean();
                    binaryExp.m_valueKind = ExpType::boolean;
                    if (lhs.m_isConstant && rhs.m_isConstant) {
                        binaryExp.m_isConstant = true;
                        binaryExp.m_constantValue = applyLOrOp(expAlt.m_op,
                            lhs.m_constantValue, rhs.m_constantValue);
                    }
                    break;
                case BinaryOpKeyword::andAnd:
                    lhs = normalizeToBoolean(std::move(lhs));
                    rhs = normalizeToBoolean(std::move(rhs));
                    binaryExp.m_type = SemanticType::makeBoolean();
                    binaryExp.m_valueKind = ExpType::boolean;
                    if (lhs.m_isConstant && rhs.m_isConstant) {
                        binaryExp.m_isConstant = true;
                        binaryExp.m_constantValue = applyLAndOp(expAlt.m_op,
                            lhs.m_constantValue, rhs.m_constantValue);
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
                        binaryExp.m_constantValue = applyEqOp(expAlt.m_op,
                            lhs.m_constantValue, rhs.m_constantValue);
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
                        binaryExp.m_constantValue = applyRelOp(expAlt.m_op,
                            lhs.m_constantValue, rhs.m_constantValue);
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
                        const auto folded = applyArithmeticOp(expAlt.m_op,
                            lhs.m_constantValue, rhs.m_constantValue);
                        if (folded.has_value()) {
                            binaryExp.m_isConstant = true;
                            binaryExp.m_constantValue = *folded;
                        }
                    }
                    break;
                }
                return binaryExp;
            } else if constexpr (std::is_same_v<AltType, Exp::Unary>) {
                auto operand = analyzeExp(expAlt.m_lhs_nn);
                if (!isScalarType(operand.m_type)) {
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        exp.m_sourcePos.m_offset,
                        "unary operator requires an integer value operand");
                    return AnalyzedExp {
                        .m_type = expAlt.m_op == UnaryOpKeyword::bang
                            ? SemanticType::makeBoolean()
                            : SemanticType::makeInteger(),
                        .m_valueKind = expAlt.m_op == UnaryOpKeyword::bang
                            ? ExpType::boolean
                            : ExpType::integer,
                        .m_isConstant = false,
                        .m_constantValue = 0,
                    };
                }
                if (expAlt.m_op == UnaryOpKeyword::bang) {
                    operand = normalizeToBoolean(std::move(operand));
                } else {
                    operand = normalizeToArithmetic(std::move(operand));
                }
                if (operand.m_isConstant) {
                    const auto folded
                        = applyUnaryOp(expAlt.m_op, operand.m_constantValue);
                    if (folded.has_value()) {
                        return AnalyzedExp {
                            .m_type = expAlt.m_op == UnaryOpKeyword::bang
                                ? SemanticType::makeBoolean()
                                : SemanticType::makeInteger(),
                            .m_valueKind = expAlt.m_op == UnaryOpKeyword::bang
                                ? ExpType::boolean
                                : ExpType::integer,
                            .m_isConstant = true,
                            .m_constantValue = *folded,
                        };
                    }
                }
                return AnalyzedExp {
                    .m_type = expAlt.m_op == UnaryOpKeyword::bang
                        ? SemanticType::makeBoolean()
                        : SemanticType::makeInteger(),
                    .m_valueKind = expAlt.m_op == UnaryOpKeyword::bang
                        ? ExpType::boolean
                        : ExpType::integer,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            } else if constexpr (std::is_same_v<AltType, Exp::Call>) {
                const auto definitionIdentifier
                    = lookupSymbol(node(expAlt.m_func_nn,
                                      "call expression is missing a callee")
                                       .m_name);
                if (!definitionIdentifier.has_value()) {
                    const auto& calleeIdentifier = node(
                        expAlt.m_func_nn, "call expression is missing a callee");
                    recordDiagnostic(SemanticDiagnosticKind::useBeforeDefinition,
                        calleeIdentifier.m_sourcePos.m_offset,
                        "use of '" + calleeIdentifier.m_name
                            + "' before definition");
                    bindSymbol(expAlt.m_func_nn,
                        makePlaceholderSymbol(expAlt.m_func_nn));
                    for (const auto arg_nn : expAlt.m_params) {
                        const auto analyzedArg = analyzeExp(arg_nn);
                        if (analyzedArg.m_valueKind == ExpType::voidType) {
                            recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                                exp.m_sourcePos.m_offset,
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
                bindSymbol(expAlt.m_func_nn, *calleeSymbol);
                if (calleeSymbol->m_kind != SemanticSymbolKind::function) {
                    recordDiagnostic(SemanticDiagnosticKind::invalidCallTarget,
                        exp.m_sourcePos.m_offset,
                        "call target '" + calleeSymbol->m_name
                            + "' is not a function");
                }
                if (calleeSymbol->m_kind == SemanticSymbolKind::function
                    && calleeSymbol->m_functionSignature.m_paramTypes.size()
                        != expAlt.m_params.size()) {
                    recordDiagnostic(SemanticDiagnosticKind::callArityMismatch,
                        exp.m_sourcePos.m_offset,
                        "call to '" + calleeSymbol->m_name
                            + "' uses the wrong number of arguments");
                }
                for (size_t i = 0; i < expAlt.m_params.size(); ++i) {
                    const auto analyzedArg = analyzeExp(expAlt.m_params[i]);
                    if (analyzedArg.m_valueKind == ExpType::voidType) {
                        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                            exp.m_sourcePos.m_offset,
                            "call arguments must produce integer values");
                    }
                    if (calleeSymbol->m_kind == SemanticSymbolKind::function
                        && i < calleeSymbol->m_functionSignature.m_paramTypes.size()
                        && !typesMatchForCall(
                            calleeSymbol->m_functionSignature.m_paramTypes[i],
                            analyzedArg.m_type)) {
                        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                            exp.m_sourcePos.m_offset,
                            "call argument type does not match parameter type");
                    }
                }
                return AnalyzedExp {
                    .m_type = calleeSymbol->m_kind == SemanticSymbolKind::function
                        ? calleeSymbol->m_functionSignature.m_returnType
                        : SemanticType::makeInteger(),
                    .m_valueKind = calleeSymbol->m_kind
                                == SemanticSymbolKind::function
                        ? calleeSymbol->m_functionSignature.m_returnType.valueKind()
                        : ExpType::integer,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            } else if constexpr (std::is_same_v<AltType, LVal>) {
                (void)resolveSymbol(expAlt.m_identifier_nn);
                const auto* symbol = m_info.findSymbol(expAlt.m_identifier_nn);
                if (symbol != nullptr
                    && symbol->m_kind == SemanticSymbolKind::function) {
                    recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                        exp.m_sourcePos.m_offset,
                        "function '" + symbol->m_name
                            + "' must be called before use");
                    return AnalyzedExp {
                        .m_type = SemanticType::makeInteger(),
                        .m_valueKind = ExpType::integer,
                        .m_isConstant = false,
                        .m_constantValue = 0,
                    };
                }
                auto currentType
                    = symbol != nullptr ? symbol->m_type : SemanticType::makeInteger();
                for (const auto index_nn : expAlt.m_indices) {
                    const auto analyzedIndex = analyzeExp(index_nn);
                    if (!isScalarType(analyzedIndex.m_type)) {
                        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                            node(index_nn, "array subscript is missing")
                                .m_sourcePos.m_offset,
                            "array subscript must produce an integer value");
                    }
                    if (!currentType.isArray()
                        || currentType.m_elementType == nullptr) {
                        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
                            exp.m_sourcePos.m_offset,
                            "subscripted expression is not an array");
                        currentType = SemanticType::makeInteger();
                        break;
                    }
                    currentType = *currentType.m_elementType;
                }
                if (symbol != nullptr && expAlt.m_indices.empty()
                    && symbol->m_isConst && symbol->m_hasConstantValue) {
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
            } else if constexpr (std::is_same_v<AltType, Number>) {
                return AnalyzedExp {
                    .m_type = SemanticType::makeInteger(),
                    .m_valueKind = ExpType::integer,
                    .m_isConstant = true,
                    .m_constantValue = expAlt.m_value,
                };
            } else {
                throw std::runtime_error("unsupported expression payload");
            }
        },
        exp.m_kind);
    if (analyzedExp.m_valueKind != ExpType::voidType
        && analyzedExp.m_valueKind != ExpType::array) {
        analyzedExp = normalizeToArithmetic(std::move(analyzedExp));
    }
    recordExpFacts(exp_nn, analyzedExp);
    return analyzedExp;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeCondExp(
    Handle<Exp> exp_nn)
{
    auto analyzedExp = analyzeExp(exp_nn);
    if (analyzedExp.m_valueKind == ExpType::voidType
        || analyzedExp.m_valueKind == ExpType::array) {
        recordDiagnostic(SemanticDiagnosticKind::typeMismatch,
            node(exp_nn, "condition expression is missing").m_sourcePos.m_offset,
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

std::optional<Handle<Identifier>> SemanticAnalyzer::lookupSymbol(
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

int32_t SemanticAnalyzer::resolveSymbol(Handle<Identifier> identifier_nn)
{
    const auto& identifier = node(identifier_nn, "identifier is missing");
    const auto definitionIdentifier = lookupSymbol(identifier.m_name);
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
        identifier.m_sourcePos.m_offset,
        "use of '" + identifier.m_name + "' before definition");
    const auto placeholder = makePlaceholderSymbol(identifier_nn);
    bindSymbol(identifier_nn, placeholder);
    return placeholder.m_id;
}

SemanticSymbol SemanticAnalyzer::makePlaceholderSymbol(
    Handle<Identifier> identifier_nn)
{
    return makeObjectSymbol(
        identifier_nn, false, false, 0, SemanticType::makeInteger());
}

SemanticType SemanticAnalyzer::analyzeObjectType(
    const std::vector<Handle<Exp>>& dimensions, int32_t offset,
    bool allowUnsizedFirstDimension)
{
    auto objectType = SemanticType::makeInteger();
    for (auto dimIt = dimensions.rbegin(); dimIt != dimensions.rend(); ++dimIt) {
        const auto analyzedDim = analyzeExp(*dimIt);
        if (!isScalarType(analyzedDim.m_type) || !analyzedDim.m_isConstant) {
            recordDiagnostic(SemanticDiagnosticKind::typeMismatch, offset,
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

SemanticSymbol SemanticAnalyzer::makeObjectSymbol(
    Handle<Identifier> identifier_nn, bool isConst, bool hasConstantValue,
    int32_t constantValue, const SemanticType& type)
{
    const auto& identifier = node(identifier_nn, "identifier is missing");
    const int32_t symbolId = ++m_nextSymbolId;
    const std::string symbolName = type.isArray()
        ? std::string(isConst ? "c_" : "v_") + identifier.m_name
        : identifier.m_name;
    return SemanticSymbol {
        .m_id = symbolId,
        .m_name = symbolName,
        .m_kind = SemanticSymbolKind::object,
        .m_isConst = isConst,
        .m_hasConstantValue = hasConstantValue,
        .m_constantValue = constantValue,
        .m_type = type,
        .m_functionSignature = {},
    };
}

SemanticSymbol SemanticAnalyzer::makeFunctionSymbol(
    Handle<Identifier> identifier_nn, const SemanticType& returnType,
    const std::vector<SemanticType>& paramTypes)
{
    const auto& identifier = node(identifier_nn, "identifier is missing");
    const int32_t symbolId = ++m_nextSymbolId;
    return SemanticSymbol {
        .m_id = symbolId,
        .m_name = identifier.m_name,
        .m_kind = SemanticSymbolKind::function,
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

bool SemanticAnalyzer::isGlobalScope() const { return m_scopeStack.size() == 1; }

bool SemanticAnalyzer::defineSymbol(
    const std::string& name, Handle<Identifier> identifier_nn)
{
    if (m_scopeStack.empty()) {
        pushScope();
    }

    auto& currentScope = m_scopeStack.back();
    return currentScope.emplace(name, identifier_nn).second;
}

std::optional<Handle<WhileStmt>> SemanticAnalyzer::currentLoop() const
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

void SemanticAnalyzer::recordDiagnostic(
    SemanticDiagnosticKind kind, int32_t offset, std::string message,
    SemanticDiagnosticSeverity severity)
{
    m_diagnostics.push_back(SemanticDiagnostic {
        .m_kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
        .m_severity = severity,
    });
}

} // namespace yesod::frontend
