#ifndef _YESOD_FRONTEND_PARSER_H_
#define _YESOD_FRONTEND_PARSER_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "frontend/ast.h"

namespace yesod::frontend {

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
    DiagnosticKind m_kind;
    int32_t m_offset;
    std::string m_message;
};

template <typename T> struct ParseResult {
    bool m_success = false;
    int32_t m_nextOffset = 0;
    T m_value {};
};

struct ParseOutput {
    AST m_ast;
    Handle<CompUnit> m_root;
    std::vector<Diagnostic> m_diagnostics;

    bool success() const
    {
        return static_cast<bool>(m_root) && m_diagnostics.empty();
    }

    Handle<CompUnit> root() {
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
        std::vector<Handle<Exp>> m_trailingDimensions;
    };

    struct KeywordMatch {
        bool m_success = false;
        int32_t m_startOffset = 0;
        int32_t m_nextOffset = 0;
    };

    [[nodiscard]] ParseResult<Handle<CompUnit>> parseCompUnit(int32_t offset);
    [[nodiscard]] ParseResult<Handle<FuncDef>> parseFuncDef(int32_t offset);
    [[nodiscard]] ParseResult<FuncTypeKeyword> parseFuncType(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Block>> parseBlock(int32_t offset);
    [[nodiscard]] ParseResult<Handle<BlockItemNode>> parseBlockItem(
        int32_t offset);
    [[nodiscard]] ParseResult<Handle<DeclNode>> parseDecl(int32_t offset);
    [[nodiscard]] ParseResult<Handle<ConstDecl>> parseConstDecl(int32_t offset);
    [[nodiscard]] ParseResult<Handle<VarDecl>> parseVarDecl(int32_t offset);
    [[nodiscard]] ParseResult<BTypeKeyword> parseBType(int32_t offset);
    [[nodiscard]] ParseResult<Handle<ConstDef>> parseConstDef(int32_t offset);
    [[nodiscard]] ParseResult<std::vector<Handle<Exp>>> parseArrayConstDims(
        int32_t offset);
    [[nodiscard]] ParseResult<Handle<ConstInitVal>> parseConstInitVal(
        int32_t offset);
    [[nodiscard]] ParseResult<std::vector<Handle<ConstInitVal>>>
    parseConstInitValList(int32_t offset);
    [[nodiscard]] ParseResult<Handle<VarDef>> parseVarDef(int32_t offset);
    [[nodiscard]] ParseResult<Handle<InitVal>> parseInitVal(int32_t offset);
    [[nodiscard]] ParseResult<std::vector<Handle<InitVal>>> parseInitValList(
        int32_t offset);
    [[nodiscard]] ParseResult<Handle<StmtNode>> parseStmt(int32_t offset);
    [[nodiscard]] ParseResult<Handle<IfStmt>> parseIfStmt(int32_t offset);
    [[nodiscard]] ParseResult<Handle<WhileStmt>> parseWhileStmt(int32_t offset);
    [[nodiscard]] ParseResult<Handle<BreakStmt>> parseBreakStmt(int32_t offset);
    [[nodiscard]] ParseResult<Handle<ContinueStmt>> parseContinueStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<Handle<AssignStmt>> parseAssignStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<Handle<ExpStmt>> parseExpStmt(int32_t offset);
    [[nodiscard]] ParseResult<Handle<ReturnStmt>> parseReturnStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseExp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseLOrExp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseLAndExp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseEqExp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseRelExp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseAddExp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseMulExp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parsePrimaryExp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseUnaryExp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseLVal(int32_t offset);
    [[nodiscard]] ParseResult<std::vector<Handle<Exp>>> parseLValIndices(
        int32_t offset);
    [[nodiscard]] ParseResult<UnaryOpKeyword> parseUnaryOp(int32_t offset);
    [[nodiscard]] ParseResult<BinaryOpKeyword> parseMulOp(int32_t offset);
    [[nodiscard]] ParseResult<BinaryOpKeyword> parseAddOp(int32_t offset);
    [[nodiscard]] ParseResult<BinaryOpKeyword> parseRelOp(int32_t offset);
    [[nodiscard]] ParseResult<BinaryOpKeyword> parseEqOp(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Exp>> parseNumber(int32_t offset);
    [[nodiscard]] ParseResult<Handle<Identifier>> parseIdent(int32_t offset);
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

    std::unordered_map<int32_t, ParseResult<Handle<CompUnit>>> m_compUnitMemo;
    std::unordered_map<int32_t, ParseResult<Handle<FuncDef>>> m_funcDefMemo;
    std::unordered_map<int32_t, ParseResult<FuncTypeKeyword>> m_funcTypeMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Block>>> m_blockMemo;
    std::unordered_map<int32_t, ParseResult<Handle<BlockItemNode>>>
        m_blockItemMemo;
    std::unordered_map<int32_t, ParseResult<Handle<DeclNode>>> m_declMemo;
    std::unordered_map<int32_t, ParseResult<Handle<ConstDecl>>> m_constDeclMemo;
    std::unordered_map<int32_t, ParseResult<Handle<VarDecl>>> m_varDeclMemo;
    std::unordered_map<int32_t, ParseResult<BTypeKeyword>> m_bTypeMemo;
    std::unordered_map<int32_t, ParseResult<Handle<ConstDef>>> m_constDefMemo;
    std::unordered_map<int32_t, ParseResult<std::vector<Handle<Exp>>>>
        m_arrayConstDimsMemo;
    std::unordered_map<int32_t, ParseResult<Handle<ConstInitVal>>>
        m_constInitValMemo;
    std::unordered_map<int32_t,
        ParseResult<std::vector<Handle<ConstInitVal>>>>
        m_constInitValListMemo;
    std::unordered_map<int32_t, ParseResult<Handle<VarDef>>> m_varDefMemo;
    std::unordered_map<int32_t, ParseResult<Handle<InitVal>>> m_initValMemo;
    std::unordered_map<int32_t, ParseResult<std::vector<Handle<InitVal>>>>
        m_initValListMemo;
    std::unordered_map<int32_t, ParseResult<Handle<StmtNode>>> m_stmtMemo;
    std::unordered_map<int32_t, ParseResult<Handle<IfStmt>>> m_ifStmtMemo;
    std::unordered_map<int32_t, ParseResult<Handle<WhileStmt>>> m_whileStmtMemo;
    std::unordered_map<int32_t, ParseResult<Handle<BreakStmt>>> m_breakStmtMemo;
    std::unordered_map<int32_t, ParseResult<Handle<ContinueStmt>>>
        m_continueStmtMemo;
    std::unordered_map<int32_t, ParseResult<Handle<AssignStmt>>>
        m_assignStmtMemo;
    std::unordered_map<int32_t, ParseResult<Handle<ExpStmt>>> m_expStmtMemo;
    std::unordered_map<int32_t, ParseResult<Handle<ReturnStmt>>>
        m_returnStmtMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_expMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_lOrExpMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_lAndExpMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_eqExpMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_relExpMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_addExpMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_mulExpMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_primaryExpMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_unaryExpMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_lValMemo;
    std::unordered_map<int32_t, ParseResult<std::vector<Handle<Exp>>>>
        m_lValIndicesMemo;
    std::unordered_map<int32_t, ParseResult<UnaryOpKeyword>> m_unaryOpMemo;
    std::unordered_map<int32_t, ParseResult<BinaryOpKeyword>> m_mulOpMemo;
    std::unordered_map<int32_t, ParseResult<BinaryOpKeyword>> m_addOpMemo;
    std::unordered_map<int32_t, ParseResult<BinaryOpKeyword>> m_relOpMemo;
    std::unordered_map<int32_t, ParseResult<BinaryOpKeyword>> m_eqOpMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Exp>>> m_numberMemo;
    std::unordered_map<int32_t, ParseResult<Handle<Identifier>>> m_identMemo;
    std::unordered_map<int32_t, ParseResult<ParamArraySuffixParse>>
        m_paramArraySuffixMemo;
};

} // namespace yesod::frontend

#endif