#include "frontend/semantic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace yesod::frontend {

namespace {

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
    analyzeFuncDef(compUnit.m_funcDef_nn);
}

void SemanticAnalyzer::analyzeFuncDef(Handle<FuncDef> funcDef_nn)
{
    const auto& funcDef
        = node(funcDef_nn, "compilation unit is missing a function");
    (void)node(funcDef.m_identifier_nn,
        "function definition is missing an identifier");
    analyzeBlock(funcDef.m_block_nn);
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
        const auto& constInitVal = node(parsedConstDef.m_constInitVal_nn,
            "const declarator is missing an initializer");
        const auto analyzedInit = analyzeExp(constInitVal.m_exp_nn);
        if (!analyzedInit.m_isConstant) {
            recordDiagnostic(
                SemanticDiagnosticKind::nonConstantConstInitializer,
                parsedConstDef.m_sourcePos.m_offset,
                "const initializer must be a constant expression");
        }

        const auto symbol = makeSymbol(parsedConstDef.m_identifier_nn, true,
            analyzedInit.m_isConstant, analyzedInit.m_constantValue);
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
        if (parsedVarDef.m_initVal_nn) {
            const auto& initVal = node(parsedVarDef.m_initVal_nn,
                "var declarator init payload is missing");
            (void)analyzeExp(initVal.m_exp_nn);
        }

        const auto symbol
            = makeSymbol(parsedVarDef.m_identifier_nn, false, false, 0);
        if (!defineSymbol(identifier.m_name, parsedVarDef.m_identifier_nn)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.m_sourcePos.m_offset,
                "double definition of '" + identifier.m_name + "'");
        }
        bindSymbol(parsedVarDef.m_identifier_nn, symbol);
    }
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
    const auto* lVal = std::get_if<LVal>(&lValExp.m_kind);
    if (lVal == nullptr) {
        throw std::runtime_error("assignment lhs is not an lvalue expression");
    }

    const auto& identifier = node(
        lVal->m_identifier_nn, "assignment lvalue is missing an identifier");
    (void)resolveSymbol(lVal->m_identifier_nn);
    const auto* symbol = m_info.findSymbol(lVal->m_identifier_nn);
    if (symbol != nullptr && symbol->m_isConst) {
        recordDiagnostic(SemanticDiagnosticKind::assignToConst,
            identifier.m_sourcePos.m_offset,
            "cannot assign to const '" + symbol->m_name + "'");
    }

    (void)analyzeExp(assignStmt.m_exp_nn);
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
    (void)analyzeExp(returnStmt.m_exp_nn);
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
                    .m_valueKind = ExpType::integer,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };

                switch (expAlt.m_op) {
                case BinaryOpKeyword::orOr:
                    lhs = normalizeToBoolean(std::move(lhs));
                    rhs = normalizeToBoolean(std::move(rhs));
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
                            .m_valueKind = expAlt.m_op == UnaryOpKeyword::bang
                                ? ExpType::boolean
                                : ExpType::integer,
                            .m_isConstant = true,
                            .m_constantValue = *folded,
                        };
                    }
                }
                return AnalyzedExp {
                    .m_valueKind = expAlt.m_op == UnaryOpKeyword::bang
                        ? ExpType::boolean
                        : ExpType::integer,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            } else if constexpr (std::is_same_v<AltType, LVal>) {
                (void)resolveSymbol(expAlt.m_identifier_nn);
                const auto* symbol = m_info.findSymbol(expAlt.m_identifier_nn);
                if (symbol != nullptr && symbol->m_isConst
                    && symbol->m_hasConstantValue) {
                    return AnalyzedExp {
                        .m_valueKind = ExpType::integer,
                        .m_isConstant = true,
                        .m_constantValue = symbol->m_constantValue,
                    };
                }

                return AnalyzedExp {
                    .m_valueKind = ExpType::integer,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            } else {
                return AnalyzedExp {
                    .m_valueKind = ExpType::integer,
                    .m_isConstant = true,
                    .m_constantValue = expAlt.m_value,
                };
            }
        },
        exp.m_kind);
    analyzedExp = normalizeToArithmetic(std::move(analyzedExp));
    recordExpFacts(exp_nn, analyzedExp);
    return analyzedExp;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeCondExp(
    Handle<Exp> exp_nn)
{
    auto analyzedExp = analyzeExp(exp_nn);
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
    return makeSymbol(identifier_nn, false, false, 0);
}

SemanticSymbol SemanticAnalyzer::makeSymbol(Handle<Identifier> identifier_nn,
    bool isConst, bool hasConstantValue, int32_t constantValue)
{
    const auto& identifier = node(identifier_nn, "identifier is missing");
    const int32_t symbolId = ++m_nextSymbolId;
    return SemanticSymbol {
        .m_id = symbolId,
        .m_name = identifier.m_name,
        .m_isConst = isConst,
        .m_hasConstantValue = hasConstantValue,
        .m_constantValue = constantValue,
    };
}

void SemanticAnalyzer::pushScope() { m_scopeStack.emplace_back(); }

void SemanticAnalyzer::popScope()
{
    if (!m_scopeStack.empty()) {
        m_scopeStack.pop_back();
    }
}

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

void SemanticAnalyzer::recordDiagnostic(
    SemanticDiagnosticKind kind, int32_t offset, std::string message)
{
    m_diagnostics.push_back(SemanticDiagnostic {
        .m_kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
    });
}

} // namespace yesod::frontend
