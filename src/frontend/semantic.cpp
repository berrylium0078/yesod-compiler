#include "frontend/semantic.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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

} // namespace

SemanticOutput SemanticAnalyzer::analyze(const CompUnit& compUnit)
{
    m_scopeStack.clear();
    m_diagnostics.clear();
    return SemanticOutput {
        .m_root = analyzeCompUnit(compUnit),
        .m_diagnostics = m_diagnostics,
    };
}

std::shared_ptr<semantic::CompUnit> SemanticAnalyzer::analyzeCompUnit(
    const CompUnit& compUnit)
{
    const auto& funcDef = requireNode(
        compUnit.m_funcDef_nn, "compilation unit is missing a function");
    return std::make_shared<semantic::CompUnit>(compUnit.m_startOffset,
        analyzeFuncDef(funcDef));
}

std::shared_ptr<semantic::FuncDef> SemanticAnalyzer::analyzeFuncDef(
    const FuncDef& funcDef)
{
    const auto& identifier = requireNode(
        funcDef.m_identifier_nn, "function definition is missing an identifier");
    return std::make_shared<semantic::FuncDef>(funcDef.m_startOffset,
        funcDef.m_funcType, identifier.m_name,
        analyzeBlock(requireNode(funcDef.m_block_nn,
            "function definition is missing a block")));
}

std::shared_ptr<semantic::Block> SemanticAnalyzer::analyzeBlock(
    const Block& block)
{
    pushScope();
    std::vector<std::shared_ptr<semantic::BlockItemNode>> blockItems;
    for (const auto& blockItem : block.m_blockItems) {
        blockItems.push_back(analyzeBlockItemNode(
            requireNode(blockItem, "block contains a null item")));
    }
    popScope();
    return std::make_shared<semantic::Block>(
        block.m_startOffset, std::move(blockItems));
}

std::shared_ptr<semantic::BlockItemNode> SemanticAnalyzer::analyzeBlockItemNode(
    const BlockItemNode& blockItemNode)
{
    return std::visit(
        [&](const auto& blockItemAlt) -> std::shared_ptr<semantic::BlockItemNode> {
            using AltType = std::decay_t<decltype(blockItemAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<DeclNode>>) {
                auto declNode = analyzeDeclNode(requireNode(blockItemAlt,
                    "block item declaration payload is missing"));
                return std::make_shared<semantic::BlockItemNode>(
                    blockItemNode.m_startOffset, semantic::BlockItem { declNode });
            } else {
                auto stmtNode = analyzeStmtNode(requireNode(blockItemAlt,
                    "block item statement payload is missing"));
                return std::make_shared<semantic::BlockItemNode>(
                    blockItemNode.m_startOffset, semantic::BlockItem { stmtNode });
            }
        },
        blockItemNode.m_blockItem);
}

std::shared_ptr<semantic::DeclNode> SemanticAnalyzer::analyzeDeclNode(
    const DeclNode& declNode)
{
    return std::visit(
        [&](const auto& declAlt) -> std::shared_ptr<semantic::DeclNode> {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ConstDecl>>) {
                auto constDecl = analyzeConstDecl(requireNode(declAlt,
                    "const declaration payload is missing"));
                return std::make_shared<semantic::DeclNode>(declNode.m_startOffset,
                    semantic::Decl { constDecl });
            } else {
                auto varDecl = analyzeVarDecl(requireNode(declAlt,
                    "var declaration payload is missing"));
                return std::make_shared<semantic::DeclNode>(declNode.m_startOffset,
                    semantic::Decl { varDecl });
            }
        },
        declNode.m_decl);
}

std::shared_ptr<semantic::ConstDecl> SemanticAnalyzer::analyzeConstDecl(
    const ConstDecl& constDecl)
{
    std::vector<std::shared_ptr<semantic::ConstDef>> constDefs;
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

        const auto symbol_nn = makeSymbol(identifier, true,
            analyzedInit.m_isConstant, analyzedInit.m_constantValue);
        if (!defineSymbol(symbol_nn)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.m_startOffset,
                "double definition of '" + identifier.m_name + "'");
        }

        constDefs.push_back(std::make_shared<semantic::ConstDef>(
            parsedConstDef.m_startOffset, symbol_nn, analyzedInit.m_exp_nn));
    }

    return std::make_shared<semantic::ConstDecl>(constDecl.m_startOffset,
        constDecl.m_bType, std::move(constDefs));
}

std::shared_ptr<semantic::VarDecl> SemanticAnalyzer::analyzeVarDecl(
    const VarDecl& varDecl)
{
    std::vector<std::shared_ptr<semantic::VarDef>> varDefs;
    for (const auto& varDef : varDecl.m_varDefs) {
        const auto& parsedVarDef
            = requireNode(varDef, "var declaration contains a null declarator");
        const auto& identifier = requireNode(parsedVarDef.m_identifier_nn,
            "var declarator is missing an identifier");
        std::shared_ptr<semantic::Exp> initExp_nn;
        if (parsedVarDef.m_initVal_nn != nullptr) {
            initExp_nn = analyzeExp(requireNode(
                requireNode(parsedVarDef.m_initVal_nn,
                    "var declarator init payload is missing")
                    .m_exp_nn,
                "var initializer is missing its expression"))
                             .m_exp_nn;
        }

        const auto symbol_nn = makeSymbol(identifier, false, false, 0);
        if (!defineSymbol(symbol_nn)) {
            recordDiagnostic(SemanticDiagnosticKind::doubleDefinition,
                identifier.m_startOffset,
                "double definition of '" + identifier.m_name + "'");
        }

        varDefs.push_back(std::make_shared<semantic::VarDef>(
            parsedVarDef.m_startOffset, symbol_nn, initExp_nn));
    }

    return std::make_shared<semantic::VarDecl>(
        varDecl.m_startOffset, varDecl.m_bType, std::move(varDefs));
}

std::shared_ptr<semantic::StmtNode> SemanticAnalyzer::analyzeStmtNode(
    const StmtNode& stmtNode)
{
    return std::visit(
        [&](const auto& stmtAlt) -> std::shared_ptr<semantic::StmtNode> {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<IfStmt>>) {
                auto ifStmt = analyzeIfStmt(requireNode(
                    stmtAlt, "if statement payload is missing"));
                return std::make_shared<semantic::StmtNode>(stmtNode.m_startOffset,
                    semantic::Stmt { ifStmt });
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<AssignStmt>>) {
                auto assignStmt = analyzeAssignStmt(requireNode(stmtAlt,
                    "assignment statement payload is missing"));
                return std::make_shared<semantic::StmtNode>(stmtNode.m_startOffset,
                    semantic::Stmt { assignStmt });
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<Block>>) {
                auto block = analyzeBlock(requireNode(stmtAlt,
                    "block statement payload is missing"));
                return std::make_shared<semantic::StmtNode>(stmtNode.m_startOffset,
                    semantic::Stmt { block });
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<ExpStmt>>) {
                auto expStmt = analyzeExpStmt(requireNode(stmtAlt,
                    "expression statement payload is missing"));
                return std::make_shared<semantic::StmtNode>(stmtNode.m_startOffset,
                    semantic::Stmt { expStmt });
            } else {
                auto returnStmt = analyzeReturnStmt(requireNode(stmtAlt,
                    "return statement payload is missing"));
                return std::make_shared<semantic::StmtNode>(stmtNode.m_startOffset,
                    semantic::Stmt { returnStmt });
            }
        },
        stmtNode.m_stmt);
}

std::shared_ptr<semantic::IfStmt> SemanticAnalyzer::analyzeIfStmt(
    const IfStmt& ifStmt)
{
    std::shared_ptr<semantic::StmtNode> elseStmt_nn;
    if (ifStmt.m_elseStmt_nn != nullptr) {
        elseStmt_nn = analyzeStmtNode(requireNode(
            ifStmt.m_elseStmt_nn, "if statement else-branch is missing"));
    }

    return std::make_shared<semantic::IfStmt>(ifStmt.m_startOffset,
        analyzeExp(requireNode(
            ifStmt.m_condExp_nn, "if statement is missing its condition"))
            .m_exp_nn,
        analyzeStmtNode(requireNode(
            ifStmt.m_thenStmt_nn, "if statement is missing its then-branch")),
        elseStmt_nn);
}

std::shared_ptr<semantic::AssignStmt> SemanticAnalyzer::analyzeAssignStmt(
    const AssignStmt& assignStmt)
{
    const auto& identifier = requireNode(
        requireNode(assignStmt.m_lVal_nn, "assignment is missing an lvalue")
            .m_identifier_nn,
        "assignment lvalue is missing an identifier");
    const auto symbol_nn = resolveSymbol(identifier);
    if (symbol_nn->m_isConst) {
        recordDiagnostic(SemanticDiagnosticKind::assignToConst,
            identifier.m_startOffset,
            "cannot assign to const '" + symbol_nn->m_name + "'");
    }

    return std::make_shared<semantic::AssignStmt>(assignStmt.m_startOffset,
        std::make_shared<semantic::LVal>(identifier.m_startOffset, symbol_nn),
        analyzeExp(requireNode(assignStmt.m_exp_nn,
            "assignment statement is missing a value"))
            .m_exp_nn);
}

std::shared_ptr<semantic::ExpStmt> SemanticAnalyzer::analyzeExpStmt(
    const ExpStmt& expStmt)
{
    std::shared_ptr<semantic::Exp> exp_nn;
    if (expStmt.m_exp_nn != nullptr) {
        exp_nn = analyzeExp(requireNode(
            expStmt.m_exp_nn, "expression statement is missing its expression"))
                     .m_exp_nn;
    }

    return std::make_shared<semantic::ExpStmt>(expStmt.m_startOffset, exp_nn);
}

std::shared_ptr<semantic::ReturnStmt> SemanticAnalyzer::analyzeReturnStmt(
    const ReturnStmt& returnStmt)
{
    return std::make_shared<semantic::ReturnStmt>(returnStmt.m_startOffset,
        analyzeExp(requireNode(returnStmt.m_exp_nn,
            "return statement is missing a value"))
            .m_exp_nn);
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeExp(const Exp& exp)
{
    return analyzeLOrExp(requireNode(
        exp.m_lOrExp_nn, "expression is missing a logical-or expression"));
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeLOrExp(
    const LOrExp& lOrExp)
{
    auto current = analyzeLAndExp(requireNode(
        lOrExp.m_head_nn, "logical-or expression is missing its head"));
    for (const auto& tailEntry : lOrExp.m_tail) {
        const auto rhs = analyzeLAndExp(requireNode(
            tailEntry.second, "logical-or expression is missing its operand"));
        if (current.m_isConstant && rhs.m_isConstant) {
            current.m_constantValue = applyLOrOp(
                tailEntry.first, current.m_constantValue, rhs.m_constantValue);
            current.m_exp_nn = makeNumberExp(lOrExp.m_startOffset,
                current.m_constantValue);
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_exp_nn = makeBinaryExp(lOrExp.m_startOffset,
                semantic::BinaryExp::Op { tailEntry.first }, current.m_exp_nn,
                rhs.m_exp_nn),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeLAndExp(
    const LAndExp& lAndExp)
{
    auto current = analyzeEqExp(requireNode(
        lAndExp.m_head_nn, "logical-and expression is missing its head"));
    for (const auto& tailEntry : lAndExp.m_tail) {
        const auto rhs = analyzeEqExp(requireNode(
            tailEntry.second, "logical-and expression is missing its operand"));
        if (current.m_isConstant && rhs.m_isConstant) {
            current.m_constantValue = applyLAndOp(
                tailEntry.first, current.m_constantValue, rhs.m_constantValue);
            current.m_exp_nn = makeNumberExp(lAndExp.m_startOffset,
                current.m_constantValue);
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_exp_nn = makeBinaryExp(lAndExp.m_startOffset,
                semantic::BinaryExp::Op { tailEntry.first }, current.m_exp_nn,
                rhs.m_exp_nn),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeEqExp(const EqExp& eqExp)
{
    auto current = analyzeRelExp(requireNode(
        eqExp.m_head_nn, "equality expression is missing its head"));
    for (const auto& tailEntry : eqExp.m_tail) {
        const auto rhs = analyzeRelExp(requireNode(
            tailEntry.second, "equality expression is missing its operand"));
        if (current.m_isConstant && rhs.m_isConstant) {
            current.m_constantValue = applyEqOp(
                tailEntry.first, current.m_constantValue, rhs.m_constantValue);
            current.m_exp_nn = makeNumberExp(eqExp.m_startOffset,
                current.m_constantValue);
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_exp_nn = makeBinaryExp(eqExp.m_startOffset,
                semantic::BinaryExp::Op { tailEntry.first }, current.m_exp_nn,
                rhs.m_exp_nn),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeRelExp(
    const RelExp& relExp)
{
    auto current = analyzeAddExp(requireNode(
        relExp.m_head_nn, "relational expression is missing its head"));
    for (const auto& tailEntry : relExp.m_tail) {
        const auto rhs = analyzeAddExp(requireNode(
            tailEntry.second, "relational expression is missing its operand"));
        if (current.m_isConstant && rhs.m_isConstant) {
            current.m_constantValue = applyRelOp(
                tailEntry.first, current.m_constantValue, rhs.m_constantValue);
            current.m_exp_nn = makeNumberExp(relExp.m_startOffset,
                current.m_constantValue);
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_exp_nn = makeBinaryExp(relExp.m_startOffset,
                semantic::BinaryExp::Op { tailEntry.first }, current.m_exp_nn,
                rhs.m_exp_nn),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeAddExp(
    const AddExp& addExp)
{
    auto current = analyzeMulExp(requireNode(
        addExp.m_head_nn, "additive expression is missing its head"));
    for (const auto& tailEntry : addExp.m_tail) {
        const auto rhs = analyzeMulExp(requireNode(
            tailEntry.second, "additive expression is missing its operand"));
        const auto folded = current.m_isConstant && rhs.m_isConstant
            ? applyAddOp(
                  tailEntry.first, current.m_constantValue, rhs.m_constantValue)
            : std::nullopt;
        if (folded.has_value()) {
            current.m_constantValue = *folded;
            current.m_exp_nn = makeNumberExp(addExp.m_startOffset, *folded);
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_exp_nn = makeBinaryExp(addExp.m_startOffset,
                semantic::BinaryExp::Op { tailEntry.first }, current.m_exp_nn,
                rhs.m_exp_nn),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeMulExp(
    const MulExp& mulExp)
{
    auto current = analyzeUnaryExp(requireNode(
        mulExp.m_head_nn, "multiplicative expression is missing its head"));
    for (const auto& tailEntry : mulExp.m_tail) {
        const auto rhs = analyzeUnaryExp(requireNode(
            tailEntry.second,
            "multiplicative expression is missing its operand"));
        const auto folded = current.m_isConstant && rhs.m_isConstant
            ? applyMulOp(
                  tailEntry.first, current.m_constantValue, rhs.m_constantValue)
            : std::nullopt;
        if (folded.has_value()) {
            current.m_constantValue = *folded;
            current.m_exp_nn = makeNumberExp(mulExp.m_startOffset, *folded);
            current.m_isConstant = true;
            continue;
        }

        current = AnalyzedExp {
            .m_exp_nn = makeBinaryExp(mulExp.m_startOffset,
                semantic::BinaryExp::Op { tailEntry.first }, current.m_exp_nn,
                rhs.m_exp_nn),
            .m_isConstant = false,
            .m_constantValue = 0,
        };
    }
    return current;
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzeUnaryExp(
    const UnaryExp& unaryExp)
{
    return std::visit(
        [&](const auto& unaryAlt) -> AnalyzedExp {
            using AltType = std::decay_t<decltype(unaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<PrimaryExp>>) {
                return analyzePrimaryExp(requireNode(unaryAlt,
                    "unary expression is missing its primary expression"));
            } else {
                auto operand = analyzeUnaryExp(requireNode(unaryAlt.second,
                    "unary expression is missing its operand"));
                if (operand.m_isConstant) {
                    const auto folded
                        = applyUnaryOp(unaryAlt.first, operand.m_constantValue);
                    if (folded.has_value()) {
                        return AnalyzedExp {
                            .m_exp_nn = makeNumberExp(unaryExp.m_startOffset,
                                *folded),
                            .m_isConstant = true,
                            .m_constantValue = *folded,
                        };
                    }
                }
                return AnalyzedExp {
                    .m_exp_nn = makeUnaryExp(
                        unaryExp.m_startOffset, unaryAlt.first, operand.m_exp_nn),
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            }
        },
        unaryExp.m_kind);
}

SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::analyzePrimaryExp(
    const PrimaryExp& primaryExp)
{
    return std::visit(
        [&](const auto& primaryAlt) -> AnalyzedExp {
            using AltType = std::decay_t<decltype(primaryAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<Exp>>) {
                return analyzeExp(requireNode(primaryAlt,
                    "parenthesized primary is missing its inner expression"));
            } else if constexpr (std::is_same_v<AltType,
                                     std::shared_ptr<LVal>>) {
                const auto& identifier = requireNode(
                    requireNode(primaryAlt, "lvalue primary is missing")
                        .m_identifier_nn,
                    "lvalue primary is missing its identifier");
                const auto symbol_nn = resolveSymbol(identifier);
                if (symbol_nn->m_isConst && symbol_nn->m_hasConstantValue) {
                    return AnalyzedExp {
                        .m_exp_nn = makeNumberExp(
                            primaryExp.m_startOffset, symbol_nn->m_constantValue),
                        .m_isConstant = true,
                        .m_constantValue = symbol_nn->m_constantValue,
                    };
                }

                return AnalyzedExp {
                    .m_exp_nn = makeLValExp(primaryExp.m_startOffset, symbol_nn),
                    .m_isConstant = false,
                    .m_constantValue = 0,
                };
            } else {
                const auto& number = requireNode(
                    primaryAlt, "number primary expression is missing");
                return AnalyzedExp {
                    .m_exp_nn = makeNumberExp(number.m_startOffset, number.m_value),
                    .m_isConstant = true,
                    .m_constantValue = number.m_value,
                };
            }
        },
        primaryExp.m_kind);
}

std::shared_ptr<semantic::Symbol> SemanticAnalyzer::resolveSymbol(
    const Identifier& identifier)
{
    const auto symbol_nn = lookupSymbol(identifier.m_name);
    if (symbol_nn.has_value()) {
        return *symbol_nn;
    }

    recordDiagnostic(SemanticDiagnosticKind::useBeforeDefinition,
        identifier.m_startOffset,
        "use of '" + identifier.m_name + "' before definition");
    return makePlaceholderSymbol(identifier);
}

std::optional<std::shared_ptr<semantic::Symbol>> SemanticAnalyzer::lookupSymbol(
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

std::shared_ptr<semantic::Symbol> SemanticAnalyzer::makePlaceholderSymbol(
    const Identifier& identifier) const
{
    return std::make_shared<semantic::Symbol>(
        identifier.m_startOffset, identifier.m_name, false, false, 0);
}

std::shared_ptr<semantic::Symbol> SemanticAnalyzer::makeSymbol(
    const Identifier& identifier, bool isConst, bool hasConstantValue,
    int32_t constantValue) const
{
    return std::make_shared<semantic::Symbol>(identifier.m_startOffset,
        identifier.m_name, isConst, hasConstantValue, constantValue);
}

std::shared_ptr<semantic::Exp> SemanticAnalyzer::makeNumberExp(
    int32_t startOffset, int32_t value) const
{
    return std::make_shared<semantic::Exp>(startOffset,
        semantic::Exp::Kind {
            std::make_shared<semantic::Number>(startOffset, value) });
}

std::shared_ptr<semantic::Exp> SemanticAnalyzer::makeLValExp(int32_t startOffset,
    const std::shared_ptr<semantic::Symbol>& symbol_nn) const
{
    return std::make_shared<semantic::Exp>(startOffset,
        semantic::Exp::Kind {
            std::make_shared<semantic::LVal>(startOffset, symbol_nn) });
}

std::shared_ptr<semantic::Exp> SemanticAnalyzer::makeUnaryExp(
    int32_t startOffset, UnaryOpKeyword op,
    const std::shared_ptr<semantic::Exp>& operand_nn) const
{
    return std::make_shared<semantic::Exp>(
        startOffset, semantic::Exp::Kind { std::make_pair(op, operand_nn) });
}

std::shared_ptr<semantic::Exp> SemanticAnalyzer::makeBinaryExp(
    int32_t startOffset, semantic::BinaryExp::Op op,
    const std::shared_ptr<semantic::Exp>& lhs_nn,
    const std::shared_ptr<semantic::Exp>& rhs_nn) const
{
    return std::make_shared<semantic::Exp>(startOffset,
        semantic::Exp::Kind {
            std::make_shared<semantic::BinaryExp>(startOffset, std::move(op),
                lhs_nn, rhs_nn) });
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

bool SemanticAnalyzer::defineSymbol(
    const std::shared_ptr<semantic::Symbol>& symbol_nn)
{
    if (m_scopeStack.empty()) {
        pushScope();
    }

    const auto existingIt = m_scopeStack.back().find(symbol_nn->m_name);
    if (existingIt != m_scopeStack.back().end()) {
        return false;
    }

    m_scopeStack.back().emplace(symbol_nn->m_name, symbol_nn);
    return true;
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
