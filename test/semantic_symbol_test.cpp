#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

std::shared_ptr<ast::LVal> requireLValExp(
    const std::shared_ptr<ast::Exp>& exp_nn)
{
    std::shared_ptr<ast::LVal> lVal;
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::LVal>>) {
                lVal = expAlt;
            }
        },
        exp_nn->m_kind);
    require(lVal != nullptr, "expected lvalue semantic expression");
    return lVal;
}

std::shared_ptr<ast::BinaryExp> requireBinaryExp(
    const std::shared_ptr<ast::Exp>& exp_nn)
{
    std::shared_ptr<ast::BinaryExp> binaryExp;
    std::visit(
        [&](const auto& expAlt) {
            using AltType = std::decay_t<decltype(expAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::BinaryExp>>) {
                binaryExp = expAlt;
            }
        },
        exp_nn->m_kind);
    require(binaryExp != nullptr, "expected binary semantic expression");
    return binaryExp;
}

void testIdentifierUsesShareOneCanonicalSymbol()
{
    const auto root_nn = analyzeRoot(
        "int main(){int value = 1; value = value + 1; return value;}");
    const auto& blockItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;

    std::shared_ptr<ast::DeclNode> declNode_nn;
    std::shared_ptr<ast::StmtNode> assignNode_nn;
    std::shared_ptr<ast::StmtNode> returnNode_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                declNode_nn = item;
            }
        },
        blockItems[0]->m_blockItem);
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                assignNode_nn = item;
            }
        },
        blockItems[1]->m_blockItem);
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                returnNode_nn = item;
            }
        },
        blockItems[2]->m_blockItem);

    std::shared_ptr<ast::VarDecl> varDecl_nn;
    std::visit(
        [&](const auto& declAlt) {
            using AltType = std::decay_t<decltype(declAlt)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::VarDecl>>) {
                varDecl_nn = declAlt;
            }
        },
        declNode_nn->m_decl);
    require(varDecl_nn != nullptr, "expected var declaration");
    const auto defSymbol_nn = varDecl_nn->m_varDefs[0]->m_symbol_nn;

    std::shared_ptr<ast::AssignStmt> assignStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ast::AssignStmt>>) {
                assignStmt_nn = stmtAlt;
            }
        },
        assignNode_nn->m_stmt);
    require(assignStmt_nn != nullptr, "expected assignment statement");
    require(assignStmt_nn->m_lVal_nn->m_symbol_nn == defSymbol_nn,
        "assignment lvalue should resolve to the declaration symbol");

    const auto binaryExp_nn = requireBinaryExp(assignStmt_nn->m_exp_nn);
    require(requireLValExp(binaryExp_nn->m_lhs_nn)->m_symbol_nn == defSymbol_nn,
        "assignment rhs should reuse the declaration symbol for value reads");

    std::shared_ptr<ast::ReturnStmt> returnStmt_nn;
    std::visit(
        [&](const auto& stmtAlt) {
            using AltType = std::decay_t<decltype(stmtAlt)>;
            if constexpr (std::is_same_v<AltType,
                              std::shared_ptr<ast::ReturnStmt>>) {
                returnStmt_nn = stmtAlt;
            }
        },
        returnNode_nn->m_stmt);
    require(returnStmt_nn != nullptr, "expected return statement");
    require(requireLValExp(returnStmt_nn->m_exp_nn)->m_symbol_nn == defSymbol_nn,
        "return expression should reuse the declaration symbol");
}

void testUseBeforeDefinitionDiagnostic()
{
    const auto output = analyzeSource("int main(){value = 1; return 0;}");
    require(!output.success(), "use-before-definition should fail semantic analysis");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::useBeforeDefinition,
        "use-before-definition should report the expected semantic label");
}

void testDoubleDefinitionDiagnostic()
{
    const auto output = analyzeSource("int main(){int value; int value; return 0;}");
    require(!output.success(), "double definition should fail semantic analysis");
    require(firstDiagnostic(output).m_kind
            == SemanticDiagnosticKind::doubleDefinition,
        "double definition should report the expected semantic label");
}

void testNestedScopeLookupPrefersInnermostDefinition()
{
    const auto root_nn = analyzeRoot(
        "int main(){int value = 1; {int value = 2; value; return value;} return value;}");
    const auto& outerItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;

    std::shared_ptr<ast::VarDecl> outerDecl_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                std::visit(
                    [&](const auto& declAlt) {
                        using DeclAltType = std::decay_t<decltype(declAlt)>;
                        if constexpr (std::is_same_v<DeclAltType,
                                          std::shared_ptr<ast::VarDecl>>) {
                            outerDecl_nn = declAlt;
                        }
                    },
                    item->m_decl);
            }
        },
        outerItems[0]->m_blockItem);
    require(outerDecl_nn != nullptr, "expected outer var declaration");
    const auto outerSymbol_nn = outerDecl_nn->m_varDefs[0]->m_symbol_nn;

    std::shared_ptr<ast::Block> nestedBlock_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::Block>>) {
                            nestedBlock_nn = stmtAlt;
                        }
                    },
                    item->m_stmt);
            }
        },
        outerItems[1]->m_blockItem);
    require(nestedBlock_nn != nullptr, "expected nested block statement");

    std::shared_ptr<ast::VarDecl> innerDecl_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                std::visit(
                    [&](const auto& declAlt) {
                        using DeclAltType = std::decay_t<decltype(declAlt)>;
                        if constexpr (std::is_same_v<DeclAltType,
                                          std::shared_ptr<ast::VarDecl>>) {
                            innerDecl_nn = declAlt;
                        }
                    },
                    item->m_decl);
            }
        },
        nestedBlock_nn->m_blockItems[0]->m_blockItem);
    require(innerDecl_nn != nullptr, "expected inner var declaration");
    const auto innerSymbol_nn = innerDecl_nn->m_varDefs[0]->m_symbol_nn;
    require(innerSymbol_nn != outerSymbol_nn,
        "shadowing declarations in nested scopes should produce distinct symbols");

    std::shared_ptr<ast::ExpStmt> innerExpStmt_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::ExpStmt>>) {
                            innerExpStmt_nn = stmtAlt;
                        }
                    },
                    item->m_stmt);
            }
        },
        nestedBlock_nn->m_blockItems[1]->m_blockItem);
    require(innerExpStmt_nn != nullptr, "expected inner expression statement");
    require(requireLValExp(innerExpStmt_nn->m_exp_nn)->m_symbol_nn == innerSymbol_nn,
        "nested scope lookup should prefer the innermost definition");

    std::shared_ptr<ast::ReturnStmt> innerReturn_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::ReturnStmt>>) {
                            innerReturn_nn = stmtAlt;
                        }
                    },
                    item->m_stmt);
            }
        },
        nestedBlock_nn->m_blockItems[2]->m_blockItem);
    require(innerReturn_nn != nullptr, "expected inner return statement");
    require(requireLValExp(innerReturn_nn->m_exp_nn)->m_symbol_nn == innerSymbol_nn,
        "inner return should resolve to the innermost definition");

    std::shared_ptr<ast::ReturnStmt> outerReturn_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::ReturnStmt>>) {
                            outerReturn_nn = stmtAlt;
                        }
                    },
                    item->m_stmt);
            }
        },
        outerItems[2]->m_blockItem);
    require(outerReturn_nn != nullptr, "expected outer return statement");
    require(requireLValExp(outerReturn_nn->m_exp_nn)->m_symbol_nn == outerSymbol_nn,
        "lookup after leaving the nested block should resolve back to the outer definition");
}

void testNameScopeExampleUsesOuterThenInnerThenOuter()
{
    const auto root_nn = analyzeRoot(
        "int main(){int x; {x; int x; x;} {int x; x;} x; return 0;}");
    const auto& outerItems = root_nn->m_funcDef_nn->m_block_nn->m_blockItems;

    std::shared_ptr<ast::VarDecl> outerDecl_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                std::visit(
                    [&](const auto& declAlt) {
                        using DeclAltType = std::decay_t<decltype(declAlt)>;
                        if constexpr (std::is_same_v<DeclAltType,
                                          std::shared_ptr<ast::VarDecl>>) {
                            outerDecl_nn = declAlt;
                        }
                    },
                    item->m_decl);
            }
        },
        outerItems[0]->m_blockItem);
    require(outerDecl_nn != nullptr, "expected outer declaration");
    const auto outerSymbol_nn = outerDecl_nn->m_varDefs[0]->m_symbol_nn;

    std::shared_ptr<ast::Block> firstNestedBlock_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::Block>>) {
                            firstNestedBlock_nn = stmtAlt;
                        }
                    },
                    item->m_stmt);
            }
        },
        outerItems[1]->m_blockItem);
    require(firstNestedBlock_nn != nullptr, "expected first nested block statement");

    std::shared_ptr<ast::ExpStmt> innerOuterUse_nn;
    std::shared_ptr<ast::VarDecl> innerDecl_nn;
    std::shared_ptr<ast::ExpStmt> innerInnerUse_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::ExpStmt>>) {
                            if (innerOuterUse_nn == nullptr) {
                                innerOuterUse_nn = stmtAlt;
                            } else {
                                innerInnerUse_nn = stmtAlt;
                            }
                        }
                    },
                    item->m_stmt);
            } else {
                std::visit(
                    [&](const auto& declAlt) {
                        using DeclAltType = std::decay_t<decltype(declAlt)>;
                        if constexpr (std::is_same_v<DeclAltType,
                                          std::shared_ptr<ast::VarDecl>>) {
                            innerDecl_nn = declAlt;
                        }
                    },
                    item->m_decl);
            }
        },
        firstNestedBlock_nn->m_blockItems[0]->m_blockItem);
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                std::visit(
                    [&](const auto& declAlt) {
                        using DeclAltType = std::decay_t<decltype(declAlt)>;
                        if constexpr (std::is_same_v<DeclAltType,
                                          std::shared_ptr<ast::VarDecl>>) {
                            innerDecl_nn = declAlt;
                        }
                    },
                    item->m_decl);
            }
        },
        firstNestedBlock_nn->m_blockItems[1]->m_blockItem);
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::ExpStmt>>) {
                            innerInnerUse_nn = stmtAlt;
                        }
                    },
                    item->m_stmt);
            }
        },
        firstNestedBlock_nn->m_blockItems[2]->m_blockItem);
    require(innerOuterUse_nn != nullptr, "expected inner outer-use statement");
    require(innerDecl_nn != nullptr, "expected inner declaration");
    require(innerInnerUse_nn != nullptr, "expected inner inner-use statement");

    const auto innerSymbol_nn = innerDecl_nn->m_varDefs[0]->m_symbol_nn;
    require(requireLValExp(innerOuterUse_nn->m_exp_nn)->m_symbol_nn == outerSymbol_nn,
        "use before the shadowing declaration should resolve to the outer symbol");
    require(requireLValExp(innerInnerUse_nn->m_exp_nn)->m_symbol_nn == innerSymbol_nn,
        "use after the shadowing declaration should resolve to the inner symbol");

    std::shared_ptr<ast::Block> secondNestedBlock_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::Block>>) {
                            secondNestedBlock_nn = stmtAlt;
                        }
                    },
                    item->m_stmt);
            }
        },
        outerItems[2]->m_blockItem);
    require(secondNestedBlock_nn != nullptr, "expected second nested block statement");

    std::shared_ptr<ast::VarDecl> secondInnerDecl_nn;
    std::shared_ptr<ast::ExpStmt> secondInnerUse_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::DeclNode>>) {
                std::visit(
                    [&](const auto& declAlt) {
                        using DeclAltType = std::decay_t<decltype(declAlt)>;
                        if constexpr (std::is_same_v<DeclAltType,
                                          std::shared_ptr<ast::VarDecl>>) {
                            secondInnerDecl_nn = declAlt;
                        }
                    },
                    item->m_decl);
            }
        },
        secondNestedBlock_nn->m_blockItems[0]->m_blockItem);
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::ExpStmt>>) {
                            secondInnerUse_nn = stmtAlt;
                        }
                    },
                    item->m_stmt);
            }
        },
        secondNestedBlock_nn->m_blockItems[1]->m_blockItem);
    require(secondInnerDecl_nn != nullptr, "expected second inner declaration");
    require(secondInnerUse_nn != nullptr, "expected second inner use");

    const auto secondInnerSymbol_nn = secondInnerDecl_nn->m_varDefs[0]->m_symbol_nn;
    require(secondInnerSymbol_nn != outerSymbol_nn,
        "second nested block should also shadow the outer symbol");
    require(secondInnerSymbol_nn != innerSymbol_nn,
        "separate nested blocks should get distinct inner symbols");
    require(requireLValExp(secondInnerUse_nn->m_exp_nn)->m_symbol_nn
            == secondInnerSymbol_nn,
        "use inside the second nested block should resolve to its own inner symbol");

    std::shared_ptr<ast::ExpStmt> outerUse_nn;
    std::visit(
        [&](const auto& item) {
            using AltType = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<AltType, std::shared_ptr<ast::StmtNode>>) {
                std::visit(
                    [&](const auto& stmtAlt) {
                        using StmtAltType = std::decay_t<decltype(stmtAlt)>;
                        if constexpr (std::is_same_v<StmtAltType,
                                          std::shared_ptr<ast::ExpStmt>>) {
                            outerUse_nn = stmtAlt;
                        }
                    },
                    item->m_stmt);
            }
        },
        outerItems[3]->m_blockItem);
    require(outerUse_nn != nullptr, "expected outer use after nested block");
    require(requireLValExp(outerUse_nn->m_exp_nn)->m_symbol_nn == outerSymbol_nn,
        "use after the nested block should resolve back to the outer symbol");
}

} // namespace

int main()
{
    testIdentifierUsesShareOneCanonicalSymbol();
    testUseBeforeDefinitionDiagnostic();
    testDoubleDefinitionDiagnostic();
    testNestedScopeLookupPrefersInnermostDefinition();
    testNameScopeExampleUsesOuterThenInnerThenOuter();
    return 0;
}
