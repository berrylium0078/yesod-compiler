#include "frontend/semantic.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace yesod::frontend {

namespace {

template <typename T>
const T& requireNode(const std::shared_ptr<T>& node, const char* message)
{
    if (node == nullptr) {
        throw std::runtime_error(message);
    }
    return *node;
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

std::optional<int32_t> applyMulOp(MulOpKeyword op, int32_t lhs, int32_t rhs)
{
    switch (op) {
    case MulOpKeyword::star:
        return static_cast<int32_t>(static_cast<int64_t>(lhs) * rhs);
    case MulOpKeyword::slash:
        if (rhs == 0) {
            return std::nullopt;
        }
        return lhs / rhs;
    case MulOpKeyword::percent:
        if (rhs == 0) {
            return std::nullopt;
        }
        return lhs % rhs;
    }
    return std::nullopt;
}

std::optional<int32_t> applyAddOp(AddOpKeyword op, int32_t lhs, int32_t rhs)
{
    switch (op) {
    case AddOpKeyword::plus:
        return static_cast<int32_t>(static_cast<int64_t>(lhs) + rhs);
    case AddOpKeyword::minus:
        return static_cast<int32_t>(static_cast<int64_t>(lhs) - rhs);
    }
    return std::nullopt;
}

int32_t applyRelOp(RelOpKeyword op, int32_t lhs, int32_t rhs)
{
    switch (op) {
    case RelOpKeyword::less:
        return lhs < rhs ? 1 : 0;
    case RelOpKeyword::greater:
        return lhs > rhs ? 1 : 0;
    case RelOpKeyword::lessEqual:
        return lhs <= rhs ? 1 : 0;
    case RelOpKeyword::greaterEqual:
        return lhs >= rhs ? 1 : 0;
    }
    throw std::runtime_error("unsupported relational operator");
}

int32_t applyEqOp(EqOpKeyword op, int32_t lhs, int32_t rhs)
{
    switch (op) {
    case EqOpKeyword::equal:
        return lhs == rhs ? 1 : 0;
    case EqOpKeyword::notEqual:
        return lhs != rhs ? 1 : 0;
    }
    throw std::runtime_error("unsupported equality operator");
}

int32_t applyLAndOp(LAndOpKeyword, int32_t lhs, int32_t rhs)
{
    return (lhs != 0 && rhs != 0) ? 1 : 0;
}

int32_t applyLOrOp(LOrOpKeyword, int32_t lhs, int32_t rhs)
{
    return (lhs != 0 || rhs != 0) ? 1 : 0;
}

int32_t normalizeBooleanConstant(int32_t value)
{
    return value != 0 ? 1 : 0;
}

} // namespace

SemanticOutput SemanticAnalyzer::analyze(
    const std::shared_ptr<CompUnit>& compUnit_nn)
{
    m_root_nn = compUnit_nn;
    m_info = SemanticInfo {};
    m_scopeStack.clear();
    m_loopIdStack.clear();
    m_diagnostics.clear();
    m_nextSymbolId = 0;
    m_nextLoopId = 0;

    if (compUnit_nn != nullptr) {
        analyzeCompUnit(*compUnit_nn);
    }

    return SemanticOutput {
        .m_root = m_root_nn,
        .m_info = m_info,
        .m_diagnostics = m_diagnostics,
    };
}

void SemanticAnalyzer::analyzeCompUnit(const CompUnit& compUnit)
{
    analyzeFuncDef(requireNode(
        compUnit.m_funcDef_nn, "compilation unit is missing a function"));
}

void SemanticAnalyzer::analyzeFuncDef(const FuncDef& funcDef)
{
    (void)requireNode(
        funcDef.m_identifier_nn, "function definition is missing an identifier");
    analyzeBlock(requireNode(
        funcDef.m_block_nn, "function definition is missing a block"));
}

void SemanticAnalyzer::analyzeBlock(const Block& block)
{
    pushScope();
    for (const auto& blockItem : block.m_blockItems) {
        analyzeBlockItemNode(requireNode(blockItem, "block contains a null item"));
    }
    popScope();
}

void SemanticAnalyzer::analyzeBlockItemNode(const BlockItemNode& blockItemNode)
{
    std::visit(
        [&](const auto& blockItemAlt) {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<DeclNode>>) {
                analyzeDeclNode(requireNode(blockItemAlt,
                    "block item declaration payload is missing"));
            } else {
                analyzeStmtNode(requireNode(blockItemAlt,
                    "block item statement payload is missing"));
            }
        },
        blockItemNode.m_blockItem);
}

void SemanticAnalyzer::analyzeDeclNode(const DeclNode& declNode)
{
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ConstDecl>>) {
                analyzeConstDecl(requireNode(declAlt,
                    "const declaration payload is missing"));
            } else {
                analyzeVarDecl(requireNode(declAlt,
                    "var declaration payload is missing"));
            }
        },
        declNode.m_decl);
}

void SemanticAnalyzer::analyzeConstDecl(const ConstDecl& constDecl)
{
    for (const auto& constDef : constDecl.m_constDefs) {
        const auto& parsedConstDef = requireNode(
            constDef, "const declaration contains a null declarator");
        const auto& identifier = requireNode(parsedConstDef.m_identifier_nn,
            "const declarator is missing an identifier");
        const auto& constInitVal = requireNode(parsedConstDef.m_constInitVal_nn,
            "const declarator is missing an initializer");
        const auto& constExp = requireNode(constInitVal.m_constExp_nn,
            "const initializer is missing its expression wrapper");
        const auto analyzedInit = analyzeExp(requireNode(constExp.m_exp_nn,
            "const initializer is missing its expression"));
        if (!analyzedInit.m_isConstant) {
            recordDiagnostic(
                SemanticDiagnosticKind::nonConstantConstInitializer,
                parsedConstDef.m_startOffset,
                "const initializer must be a constant expression");
        }

        const int32_t symbolId = makeSymbol(identifier, true,
            analyzedInit.m_isConstant, analyzedInit.m_constantValue);
        if (!defineSymbol(identifier.m_name, symbolId)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.m_startOffset,
                "double definition of '" + identifier.m_name + "'");
        }
        bindSymbol(parsedConstDef, symbolId);
        bindSymbol(identifier, symbolId);
    }
}

void SemanticAnalyzer::analyzeVarDecl(const VarDecl& varDecl)
{
    for (const auto& varDef : varDecl.m_varDefs) {
        const auto& parsedVarDef
            = requireNode(varDef, "var declaration contains a null declarator");
        const auto& identifier = requireNode(parsedVarDef.m_identifier_nn,
            "var declarator is missing an identifier");
        if (parsedVarDef.m_initVal_nn != nullptr) {
            (void)analyzeExp(requireNode(
                requireNode(parsedVarDef.m_initVal_nn,
                    "var declarator init payload is missing")
                    .m_exp_nn,
                "var initializer is missing its expression"));
        }

        const int32_t symbolId = makeSymbol(identifier, false, false, 0);
        if (!defineSymbol(identifier.m_name, symbolId)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.m_startOffset,
                "double definition of '" + identifier.m_name + "'");
        }
        bindSymbol(parsedVarDef, symbolId);
        bindSymbol(identifier, symbolId);
    }
}

void SemanticAnalyzer::analyzeStmtNode(const StmtNode& stmtNode)
{
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<IfStmt>>) {
                analyzeIfStmt(requireNode(
                    stmtAlt, "if statement payload is missing"));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<WhileStmt>>) {
                analyzeWhileStmt(requireNode(
                    stmtAlt, "while statement payload is missing"));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<BreakStmt>>) {
                analyzeBreakStmt(requireNode(
                    stmtAlt, "break statement payload is missing"));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<ContinueStmt>>) {
                analyzeContinueStmt(requireNode(
                    stmtAlt, "continue statement payload is missing"));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<AssignStmt>>) {
                analyzeAssignStmt(requireNode(
                    stmtAlt, "assignment statement payload is missing"));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<Block>>) {
                analyzeBlock(requireNode(
                    stmtAlt, "block statement payload is missing"));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<ExpStmt>>) {
                analyzeExpStmt(requireNode(
                    stmtAlt, "expression statement payload is missing"));
            } else {
                analyzeReturnStmt(requireNode(
                    stmtAlt, "return statement payload is missing"));
            }
        },
        stmtNode.m_stmt);
}

void SemanticAnalyzer::analyzeIfStmt(const IfStmt& ifStmt)
{
    (void)analyzeCondExp(requireNode(
        ifStmt.m_condExp_nn, "if statement is missing its condition"));
    analyzeStmtNode(requireNode(
        ifStmt.m_thenStmt_nn, "if statement is missing its then-branch"));
    if (ifStmt.m_elseStmt_nn != nullptr) {
        analyzeStmtNode(requireNode(
            ifStmt.m_elseStmt_nn, "if statement else-branch is missing"));
    }
}

void SemanticAnalyzer::analyzeWhileStmt(const WhileStmt& whileStmt)
{
    const int32_t loopId = ++m_nextLoopId;
    bindLoop(whileStmt, loopId);
    m_loopIdStack.push_back(loopId);
    (void)analyzeCondExp(requireNode(
        whileStmt.m_condExp_nn, "while statement is missing its condition"));
    analyzeStmtNode(requireNode(
        whileStmt.m_bodyStmt_nn, "while statement is missing its body"));
    m_loopIdStack.pop_back();
}

void SemanticAnalyzer::analyzeBreakStmt(const BreakStmt& breakStmt)
{
    const auto loopId = currentLoopId();
    if (!loopId.has_value()) {
        recordDiagnostic(SemanticDiagnosticKind::breakOutsideWhile,
            breakStmt.m_startOffset, "break statement is not inside a while loop");
        return;
    }
    bindLoop(breakStmt, *loopId);
}

void SemanticAnalyzer::analyzeContinueStmt(const ContinueStmt& continueStmt)
{
    const auto loopId = currentLoopId();
    if (!loopId.has_value()) {
        recordDiagnostic(SemanticDiagnosticKind::continueOutsideWhile,
            continueStmt.m_startOffset,
            "continue statement is not inside a while loop");
        return;
    }
    bindLoop(continueStmt, *loopId);
}

void SemanticAnalyzer::analyzeAssignStmt(const AssignStmt& assignStmt)
{
    const auto& lVal = requireNode(
        assignStmt.m_lVal_nn, "assignment is missing an lvalue");
    const auto& identifier = requireNode(
        lVal.m_identifier_nn, "assignment lvalue is missing an identifier");
    const int32_t symbolId = resolveSymbol(identifier);
    bindSymbol(lVal, symbolId);
    bindSymbol(identifier, symbolId);
    const auto* symbol = m_info.findSymbolById(symbolId);
    if (symbol != nullptr && symbol->m_isConst) {
        recordDiagnostic(SemanticDiagnosticKind::assignToConst,
            identifier.m_startOffset,
            "cannot assign to const '" + symbol->m_name + "'");
    }

    (void)analyzeExp(requireNode(assignStmt.m_exp_nn,
        "assignment statement is missing a value"));
}

void SemanticAnalyzer::analyzeExpStmt(const ExpStmt& expStmt)
{
    if (expStmt.m_exp_nn != nullptr) {
        (void)analyzeExp(requireNode(
            expStmt.m_exp_nn, "expression statement is missing its expression"));
    }
}

void SemanticAnalyzer::analyzeReturnStmt(const ReturnStmt& returnStmt)
{
    (void)analyzeExp(requireNode(
        returnStmt.m_exp_nn, "return statement is missing a value"));
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeExp(const Exp& exp)
{
    auto analyzedExp = normalizeToArithmetic(exp,
        analyzeLOrExp(requireNode(
            exp.m_lOrExp_nn, "expression is missing a logical-or expression")));
    recordExpFacts(exp, analyzedExp);
    return analyzedExp;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeCondExp(const Exp& exp)
{
    auto analyzedExp = normalizeToBoolean(exp,
        analyzeLOrExp(requireNode(
            exp.m_lOrExp_nn, "expression is missing a logical-or expression")));
    recordExpFacts(exp, analyzedExp);
    return analyzedExp;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeLOrExp(
    const LOrExp& lOrExp)
{
    auto current = analyzeLAndExp(requireNode(
        lOrExp.m_head_nn, "logical-or expression is missing its head"));
    for (const auto& tailEntry : lOrExp.m_tail) {
        auto rhs = analyzeLAndExp(requireNode(
            tailEntry.second, "logical-or expression is missing its operand"));
        current = normalizeToBoolean(lOrExp, std::move(current));
        rhs = normalizeToBoolean(lOrExp, std::move(rhs));
        if (current.m_isConstant && rhs.m_isConstant) {
            current.m_constantValue = applyLOrOp(
                tailEntry.first, current.m_constantValue, rhs.m_constantValue);
            current.m_valueKind = SemanticExpValueKind::boolean;
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_valueKind = SemanticExpValueKind::boolean,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    recordExpFacts(lOrExp, current);
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeLAndExp(
    const LAndExp& lAndExp)
{
    auto current = analyzeEqExp(requireNode(
        lAndExp.m_head_nn, "logical-and expression is missing its head"));
    for (const auto& tailEntry : lAndExp.m_tail) {
        auto rhs = analyzeEqExp(requireNode(
            tailEntry.second, "logical-and expression is missing its operand"));
        current = normalizeToBoolean(lAndExp, std::move(current));
        rhs = normalizeToBoolean(lAndExp, std::move(rhs));
        if (current.m_isConstant && rhs.m_isConstant) {
            current.m_constantValue = applyLAndOp(
                tailEntry.first, current.m_constantValue, rhs.m_constantValue);
            current.m_valueKind = SemanticExpValueKind::boolean;
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_valueKind = SemanticExpValueKind::boolean,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    recordExpFacts(lAndExp, current);
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeEqExp(const EqExp& eqExp)
{
    auto current = analyzeRelExp(requireNode(
        eqExp.m_head_nn, "equality expression is missing its head"));
    for (const auto& tailEntry : eqExp.m_tail) {
        auto rhs = analyzeRelExp(requireNode(
            tailEntry.second, "equality expression is missing its operand"));
        current = normalizeToArithmetic(eqExp, std::move(current));
        rhs = normalizeToArithmetic(eqExp, std::move(rhs));
        if (current.m_isConstant && rhs.m_isConstant) {
            current.m_constantValue = applyEqOp(
                tailEntry.first, current.m_constantValue, rhs.m_constantValue);
            current.m_valueKind = SemanticExpValueKind::boolean;
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_valueKind = SemanticExpValueKind::boolean,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    recordExpFacts(eqExp, current);
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeRelExp(
    const RelExp& relExp)
{
    auto current = analyzeAddExp(requireNode(
        relExp.m_head_nn, "relational expression is missing its head"));
    for (const auto& tailEntry : relExp.m_tail) {
        auto rhs = analyzeAddExp(requireNode(
            tailEntry.second, "relational expression is missing its operand"));
        current = normalizeToArithmetic(relExp, std::move(current));
        rhs = normalizeToArithmetic(relExp, std::move(rhs));
        if (current.m_isConstant && rhs.m_isConstant) {
            current.m_constantValue = applyRelOp(
                tailEntry.first, current.m_constantValue, rhs.m_constantValue);
            current.m_valueKind = SemanticExpValueKind::boolean;
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_valueKind = SemanticExpValueKind::boolean,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    recordExpFacts(relExp, current);
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeAddExp(
    const AddExp& addExp)
{
    auto current = analyzeMulExp(requireNode(
        addExp.m_head_nn, "additive expression is missing its head"));
    for (const auto& tailEntry : addExp.m_tail) {
        auto rhs = analyzeMulExp(requireNode(
            tailEntry.second, "additive expression is missing its operand"));
        current = normalizeToArithmetic(addExp, std::move(current));
        rhs = normalizeToArithmetic(addExp, std::move(rhs));
        const auto folded = current.m_isConstant && rhs.m_isConstant
            ? applyAddOp(
                  tailEntry.first, current.m_constantValue, rhs.m_constantValue)
            : std::nullopt;
        if (folded.has_value()) {
            current.m_constantValue = *folded;
            current.m_valueKind = SemanticExpValueKind::arithmetic;
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_valueKind = SemanticExpValueKind::arithmetic,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    recordExpFacts(addExp, current);
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeMulExp(
    const MulExp& mulExp)
{
    auto current = analyzeUnaryExp(requireNode(
        mulExp.m_head_nn, "multiplicative expression is missing its head"));
    for (const auto& tailEntry : mulExp.m_tail) {
        auto rhs = analyzeUnaryExp(requireNode(
            tailEntry.second,
            "multiplicative expression is missing its operand"));
        current = normalizeToArithmetic(mulExp, std::move(current));
        rhs = normalizeToArithmetic(mulExp, std::move(rhs));
        const auto folded = current.m_isConstant && rhs.m_isConstant
            ? applyMulOp(
                  tailEntry.first, current.m_constantValue, rhs.m_constantValue)
            : std::nullopt;
        if (folded.has_value()) {
            current.m_constantValue = *folded;
            current.m_valueKind = SemanticExpValueKind::arithmetic;
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_valueKind = SemanticExpValueKind::arithmetic,
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    recordExpFacts(mulExp, current);
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeUnaryExp(
    const UnaryExp& unaryExp)
{
    auto analyzedExp = std::visit(
        [&](const auto& unaryAlt) -> AnalyzedExp {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<PrimaryExp>>) {
                return analyzePrimaryExp(requireNode(unaryAlt,
                    "unary expression is missing its primary expression"));
            } else {
                auto operand = analyzeUnaryExp(requireNode(unaryAlt.second,
                    "unary expression is missing its operand"));
                if (unaryAlt.first == UnaryOpKeyword::bang) {
                    operand = normalizeToBoolean(unaryExp, std::move(operand));
                } else {
                    operand = normalizeToArithmetic(unaryExp, std::move(operand));
                }
                if (operand.m_isConstant) {
                    const auto folded
                        = applyUnaryOp(unaryAlt.first, operand.m_constantValue);
                    if (folded.has_value()) {
                        return AnalyzedExp {
                            .m_valueKind = unaryAlt.first == UnaryOpKeyword::bang
                                ? SemanticExpValueKind::boolean
                                : SemanticExpValueKind::arithmetic,
                            .m_isConstant = true,
                            .m_constantValue = *folded,
                        };
                    }
                }
                return AnalyzedExp {
                    .m_valueKind = unaryAlt.first == UnaryOpKeyword::bang
                        ? SemanticExpValueKind::boolean
                        : SemanticExpValueKind::arithmetic,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            }
        },
        unaryExp.m_kind);
    recordExpFacts(unaryExp, analyzedExp);
    return analyzedExp;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzePrimaryExp(
    const PrimaryExp& primaryExp)
{
    auto analyzedExp = std::visit(
        [&](const auto& primaryAlt) -> AnalyzedExp {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<Exp>>) {
                return analyzeExp(requireNode(primaryAlt,
                    "parenthesized primary is missing its inner expression"));
            } else if constexpr (std::is_same_v<AltType, std::shared_ptr<LVal>>) {
                const auto& lVal = requireNode(primaryAlt, "lvalue primary is missing");
                const auto& identifier = requireNode(
                    lVal.m_identifier_nn, "lvalue primary is missing its identifier");
                const int32_t symbolId = resolveSymbol(identifier);
                bindSymbol(lVal, symbolId);
                bindSymbol(identifier, symbolId);
                const auto* symbol = m_info.findSymbolById(symbolId);
                if (symbol != nullptr && symbol->m_isConst
                    && symbol->m_hasConstantValue) {
                    recordExpFacts(lVal,
                        AnalyzedExp {
                            .m_valueKind = SemanticExpValueKind::arithmetic,
                            .m_isConstant = true,
                            .m_constantValue = symbol->m_constantValue,
                        });
                    return AnalyzedExp {
                        .m_valueKind = SemanticExpValueKind::arithmetic,
                        .m_isConstant = true,
                        .m_constantValue = symbol->m_constantValue,
                    };
                }

                recordExpFacts(lVal,
                    AnalyzedExp {
                        .m_valueKind = SemanticExpValueKind::arithmetic,
                        .m_isConstant = false,
                        .m_constantValue = 0,
                    });
                return AnalyzedExp {
                    .m_valueKind = SemanticExpValueKind::arithmetic,
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            } else {
                const auto& number = requireNode(
                    primaryAlt, "number primary expression is missing");
                recordExpFacts(number,
                    AnalyzedExp {
                        .m_valueKind = SemanticExpValueKind::arithmetic,
                        .m_isConstant = true,
                        .m_constantValue = number.m_value,
                    });
                return AnalyzedExp {
                    .m_valueKind = SemanticExpValueKind::arithmetic,
                    .m_isConstant = true,
                    .m_constantValue = number.m_value,
                };
            }
        },
        primaryExp.m_kind);
    recordExpFacts(primaryExp, analyzedExp);
    return analyzedExp;
}

std::optional<int32_t> SemanticAnalyzer::lookupSymbol(const std::string& name) const
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

int32_t SemanticAnalyzer::resolveSymbol(const Identifier& identifier)
{
    const auto symbolId = lookupSymbol(identifier.m_name);
    if (symbolId.has_value()) {
        return *symbolId;
    }

    recordDiagnostic(SemanticDiagnosticKind::useBeforeDefinition,
        identifier.m_startOffset,
        "use of '" + identifier.m_name + "' before definition");
    return makePlaceholderSymbol(identifier);
}

int32_t SemanticAnalyzer::makePlaceholderSymbol(const Identifier& identifier)
{
    return makeSymbol(identifier, false, false, 0);
}

int32_t SemanticAnalyzer::makeSymbol(const Identifier& identifier, bool isConst,
    bool hasConstantValue, int32_t constantValue)
{
    const int32_t symbolId = ++m_nextSymbolId;
    m_info.m_symbolsById.emplace(symbolId,
        SemanticSymbol {
            .m_id = symbolId,
            .m_name = identifier.m_name,
            .m_isConst = isConst,
            .m_hasConstantValue = hasConstantValue,
            .m_constantValue = constantValue,
        });
    return symbolId;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::normalizeToArithmetic(
    const AstNode&, AnalyzedExp analyzedExp)
{
    if (analyzedExp.m_valueKind == SemanticExpValueKind::arithmetic) {
        return analyzedExp;
    }

    analyzedExp.m_valueKind = SemanticExpValueKind::arithmetic;
    if (analyzedExp.m_isConstant) {
        analyzedExp.m_constantValue
            = normalizeBooleanConstant(analyzedExp.m_constantValue);
    }
    return analyzedExp;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::normalizeToBoolean(
    const AstNode&, AnalyzedExp analyzedExp)
{
    if (analyzedExp.m_valueKind == SemanticExpValueKind::boolean) {
        return analyzedExp;
    }

    analyzedExp.m_valueKind = SemanticExpValueKind::boolean;
    if (analyzedExp.m_isConstant) {
        analyzedExp.m_constantValue
            = normalizeBooleanConstant(analyzedExp.m_constantValue);
    }
    return analyzedExp;
}

void SemanticAnalyzer::bindSymbol(const AstNode& node, int32_t symbolId)
{
    m_info.m_symbolIdByNodeId[node.m_id] = symbolId;
}

void SemanticAnalyzer::bindLoop(const AstNode& node, int32_t loopId)
{
    m_info.m_loopIdByNodeId[node.m_id] = loopId;
}

void SemanticAnalyzer::recordExpFacts(
    const AstNode& node, const AnalyzedExp& analyzedExp)
{
    m_info.m_exprKindByNodeId[node.m_id] = analyzedExp.m_valueKind;
    if (analyzedExp.m_isConstant) {
        m_info.m_constantValueByNodeId[node.m_id] = analyzedExp.m_constantValue;
    } else {
        m_info.m_constantValueByNodeId.erase(node.m_id);
    }
}

void SemanticAnalyzer::pushScope()
{
    m_scopeStack.emplace_back();
}

void SemanticAnalyzer::popScope()
{
    if (!m_scopeStack.empty()) {
        m_scopeStack.pop_back();
    }
}

bool SemanticAnalyzer::defineSymbol(const std::string& name, int32_t symbolId)
{
    if (m_scopeStack.empty()) {
        pushScope();
    }

    auto& currentScope = m_scopeStack.back();
    return currentScope.emplace(name, symbolId).second;
}

std::optional<int32_t> SemanticAnalyzer::currentLoopId() const
{
    if (m_loopIdStack.empty()) {
        return std::nullopt;
    }
    return m_loopIdStack.back();
}

void SemanticAnalyzer::recordDiagnostic(SemanticDiagnosticKind kind,
    int32_t offset, std::string message)
{
    m_diagnostics.push_back(SemanticDiagnostic {
        .m_kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
    });
}

} // namespace yesod::frontend
