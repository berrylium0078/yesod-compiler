#ifndef _YESOD_FRONTEND_PARSER_H_
#define _YESOD_FRONTEND_PARSER_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>

#include "frontend/ast.h"

namespace yesod::frontend {

[[nodiscard]] std::string prependBuiltinFunctionDeclarations(
    const std::string& source);

enum class DiagnosticKind {
    unexpectedToken,
    expectedIdentifier,
    expectedInteger,
    expectedKeyword,
    expectedSymbol,
    missingFuncRParen,
    malformedBlockItem,
    malformedDeclItem,
    missingDeclSemicolon,
    malformedIfCond,
    missingIfRParen,
    malformedIfThenStmt,
    malformedElseStmt,
    malformedWhileCond,
    missingWhileRParen,
    malformedWhileBody,
    malformedStmtHead,
    malformedAssignValue,
    malformedReturnValue,
    malformedPrimaryExp,
    missingParamArrayRBracket,
    malformedArrayBound,
    missingArrayRBracket,
    malformedConstInitializer,
    missingConstInitRBrace,
    malformedInitializer,
    missingInitRBrace,
    malformedSubscript,
    missingSubscriptRBracket,
    missingBreakSemicolon,
    missingContinueSemicolon,
    missingPrimaryRParen,
    missingAssignSemicolon,
    missingSemicolon,
    missingRBrace,
    trailingInput,
    integerOutOfRange,
};

struct Diagnostic {
    DiagnosticKind kind;
    int32_t m_offset;
    std::string m_message;
};

template <typename T, typename U = AST>
struct ParseResult {
    int32_t nextOffset;
    std::optional<T> value {};
};
template <typename T, typename... Ts>
    requires is_in_list<T, Ts...>
struct ParseResult<T, Arena<Ts...>> {
    int32_t nextOffset;
    Ptr<T> value {};
};

struct ParseOutput {
    AST m_ast;
    Ptr<CompUnit> m_root;
    std::vector<Diagnostic> m_diagnostics;

    bool success() const
    {
        return static_cast<bool>(m_root) && m_diagnostics.empty();
    }

    Ptr<CompUnit> root() {
        return m_root;
    }
};

class Parser {
  public:
    explicit Parser(std::string source);

    [[nodiscard]] ParseOutput parse();

  private:
    struct ParamArraySuffixParse {
        bool m_isArray = false;
        std::vector<Ref<Exp>> shape;
    };

    struct KeywordMatch {
        bool success = false;
        int32_t m_startOffset = 0;
        int32_t nextOffset = 0;
    };

    [[nodiscard]] ParseResult<CompUnit> parseCompUnit(int32_t offset);
    [[nodiscard]] ParseResult<FuncDef> parseFuncDef(int32_t offset);
    [[nodiscard]] ParseResult<FuncTypeKeyword> parseFuncType(int32_t offset);
    [[nodiscard]] ParseResult<Block> parseBlock(int32_t offset);
    [[nodiscard]] ParseResult<BlockItem> parseBlockItem(
        int32_t offset);
    [[nodiscard]] ParseResult<Decl> parseDecl(int32_t offset);
    [[nodiscard]] ParseResult<ConstDecl> parseConstDecl(int32_t offset);
    [[nodiscard]] ParseResult<VarDecl> parseVarDecl(int32_t offset);
    [[nodiscard]] ParseResult<BTypeKeyword> parseBType(int32_t offset);
    [[nodiscard]] ParseResult<ConstDef> parseConstDef(int32_t offset);
    [[nodiscard]] ParseResult<std::vector<Ref<Exp>>> parseArrayConstDims(
        int32_t offset);
    [[nodiscard]] ParseResult<ConstInitVal> parseConstInitVal(
        int32_t offset);
    [[nodiscard]] ParseResult<std::vector<Ref<ConstInitVal>>>
    parseConstInitValList(int32_t offset);
    [[nodiscard]] ParseResult<VarDef> parseVarDef(int32_t offset);
    [[nodiscard]] ParseResult<InitVal> parseInitVal(int32_t offset);
    [[nodiscard]] ParseResult<std::vector<Ref<InitVal>>> parseInitValList(
        int32_t offset);
    [[nodiscard]] ParseResult<Stmt> parseStmt(int32_t offset);
    [[nodiscard]] ParseResult<IfStmt> parseIfStmt(int32_t offset);
    [[nodiscard]] ParseResult<WhileStmt> parseWhileStmt(int32_t offset);
    [[nodiscard]] ParseResult<BreakStmt> parseBreakStmt(int32_t offset);
    [[nodiscard]] ParseResult<ContinueStmt> parseContinueStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<AssignStmt> parseAssignStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<ExpStmt> parseExpStmt(int32_t offset);
    [[nodiscard]] ParseResult<ReturnStmt> parseReturnStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseExp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseLOrExp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseLAndExp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseEqExp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseRelExp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseAddExp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseMulExp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parsePrimaryExp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseUnaryExp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseLVal(int32_t offset);
    [[nodiscard]] ParseResult<std::vector<Ref<Exp>>> parseLValIndices(
        int32_t offset);
    [[nodiscard]] ParseResult<UnaryOpKeyword> parseUnaryOp(int32_t offset);
    [[nodiscard]] ParseResult<BinaryOpKeyword> parseMulOp(int32_t offset);
    [[nodiscard]] ParseResult<BinaryOpKeyword> parseAddOp(int32_t offset);
    [[nodiscard]] ParseResult<BinaryOpKeyword> parseRelOp(int32_t offset);
    [[nodiscard]] ParseResult<BinaryOpKeyword> parseEqOp(int32_t offset);
    [[nodiscard]] ParseResult<Exp> parseNumber(int32_t offset);
    [[nodiscard]] ParseResult<Identifier> parseIdent(int32_t offset);
    [[nodiscard]] ParseResult<ParamArraySuffixParse> parseParamArraySuffix(
        int32_t offset);

    [[nodiscard]] ParseResult<int32_t> parseIntConst(int32_t offset);
    [[nodiscard]] ParseResult<int32_t> parseHexadecimalConst(int32_t offset);
    [[nodiscard]] ParseResult<int32_t> parseOctalConst(int32_t offset);
    [[nodiscard]] ParseResult<int32_t> parseDecimalConst(int32_t offset);

    [[nodiscard]] int32_t skipTrivia(int32_t offset) const;
    [[nodiscard]] int32_t recoverToFuncHeaderEnd(int32_t offset) const;
    [[nodiscard]] int32_t recoverToExprRParen(int32_t offset) const;
    [[nodiscard]] int32_t recoverToRBracket(int32_t offset) const;
    [[nodiscard]] int32_t recoverToInitBoundary(int32_t offset) const;
    [[nodiscard]] int32_t recoverToIfStmtHead(int32_t offset) const;
    [[nodiscard]] int32_t recoverToWhileStmtHead(int32_t offset) const;
    [[nodiscard]] int32_t recoverToDeclBoundary(int32_t offset) const;
    [[nodiscard]] int32_t recoverToStmtBoundary(int32_t offset) const;
    [[nodiscard]] int32_t recoverToBlockItemBoundary(int32_t offset) const;
    [[nodiscard]] int32_t recoverToBlockEnd(int32_t offset) const;
    [[nodiscard]] bool isAtEnd(int32_t offset) const;
    [[nodiscard]] bool isIdentifierStart(char ch) const;
    [[nodiscard]] bool isIdentifierContinue(char ch) const;
    [[nodiscard]] bool hasKeywordBoundary(int32_t offset) const;
    [[nodiscard]] KeywordMatch matchKeyword(
        int32_t offset, std::string_view keyword) const;
    [[nodiscard]] KeywordMatch matchSymbol(int32_t offset, char symbol) const;
    [[nodiscard]] KeywordMatch matchSymbol(
        int32_t offset, std::string_view symbol) const;
    [[nodiscard]] ParseResult<int32_t> parseBaseInteger(int32_t offset,
        int base, int32_t prefixLength, bool (*digitPredicate)(char));

    void recordFailure(
        int32_t offset, DiagnosticKind kind, std::string message);
    void recordCommittedFailure(
        int32_t offset, DiagnosticKind kind, std::string message);
    [[nodiscard]] ParseOutput failureOutput();

    std::string m_source;
    AST m_ast;
    int32_t m_bestFailureOffset = -1;
    std::vector<Diagnostic> m_bestDiagnostics;

    std::unordered_map<int32_t, ParseResult<CompUnit>> m_compUnitMemo;
    std::unordered_map<int32_t, ParseResult<FuncDef>> m_funcDefMemo;
    std::unordered_map<int32_t, ParseResult<FuncTypeKeyword>> m_funcTypeMemo;
    std::unordered_map<int32_t, ParseResult<Block>> m_blockMemo;
    std::unordered_map<int32_t, ParseResult<BlockItem>>
        m_blockItemMemo;
    std::unordered_map<int32_t, ParseResult<Decl>> m_declMemo;
    std::unordered_map<int32_t, ParseResult<ConstDecl>> m_constDeclMemo;
    std::unordered_map<int32_t, ParseResult<VarDecl>> m_varDeclMemo;
    std::unordered_map<int32_t, ParseResult<BTypeKeyword>> m_bTypeMemo;
    std::unordered_map<int32_t, ParseResult<ConstDef>> m_constDefMemo;
    std::unordered_map<int32_t, ParseResult<std::vector<Ref<Exp>>>>
        m_arrayConstDimsMemo;
    std::unordered_map<int32_t, ParseResult<ConstInitVal>>
        m_constInitValMemo;
    std::unordered_map<int32_t,
        ParseResult<std::vector<Ref<ConstInitVal>>>>
        m_constInitValListMemo;
    std::unordered_map<int32_t, ParseResult<VarDef>> m_varDefMemo;
    std::unordered_map<int32_t, ParseResult<InitVal>> m_initValMemo;
    std::unordered_map<int32_t, ParseResult<std::vector<Ref<InitVal>>>>
        m_initValListMemo;
    std::unordered_map<int32_t, ParseResult<Stmt>> m_stmtMemo;
    std::unordered_map<int32_t, ParseResult<IfStmt>> m_ifStmtMemo;
    std::unordered_map<int32_t, ParseResult<WhileStmt>> m_whileStmtMemo;
    std::unordered_map<int32_t, ParseResult<BreakStmt>> m_breakStmtMemo;
    std::unordered_map<int32_t, ParseResult<ContinueStmt>>
        m_continueStmtMemo;
    std::unordered_map<int32_t, ParseResult<AssignStmt>>
        m_assignStmtMemo;
    std::unordered_map<int32_t, ParseResult<ExpStmt>> m_expStmtMemo;
    std::unordered_map<int32_t, ParseResult<ReturnStmt>>
        m_returnStmtMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_expMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_lOrExpMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_lAndExpMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_eqExpMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_relExpMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_addExpMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_mulExpMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_primaryExpMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_unaryExpMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_lValMemo;
    std::unordered_map<int32_t, ParseResult<std::vector<Ref<Exp>>>>
        m_lValIndicesMemo;
    std::unordered_map<int32_t, ParseResult<UnaryOpKeyword>> m_unaryOpMemo;
    std::unordered_map<int32_t, ParseResult<BinaryOpKeyword>> m_mulOpMemo;
    std::unordered_map<int32_t, ParseResult<BinaryOpKeyword>> m_addOpMemo;
    std::unordered_map<int32_t, ParseResult<BinaryOpKeyword>> m_relOpMemo;
    std::unordered_map<int32_t, ParseResult<BinaryOpKeyword>> m_eqOpMemo;
    std::unordered_map<int32_t, ParseResult<Exp>> m_numberMemo;
    std::unordered_map<int32_t, ParseResult<Identifier>> m_identMemo;
    std::unordered_map<int32_t, ParseResult<ParamArraySuffixParse>>
        m_paramArraySuffixMemo;
};

} // namespace yesod::frontend

#endif