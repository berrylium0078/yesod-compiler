#ifndef _YESOD_FRONTEND_PARSER_H_
#define _YESOD_FRONTEND_PARSER_H_

#include <cstdint>
#include <memory>
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
    malformedStmtHead,
    malformedAssignValue,
    malformedReturnValue,
    malformedPrimaryExp,
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
    std::shared_ptr<CompUnit> m_root;
    std::vector<Diagnostic> m_diagnostics;

    [[nodiscard]] bool success() const
    {
        return m_root != nullptr && m_diagnostics.empty();
    }
};

class Parser {
  public:
    explicit Parser(std::string source);

    [[nodiscard]] ParseOutput parse();

  private:
    struct KeywordMatch {
        bool m_success = false;
        int32_t m_startOffset = 0;
        int32_t m_nextOffset = 0;
    };

    [[nodiscard]] ParseResult<std::shared_ptr<CompUnit>> parseCompUnit(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<FuncDef>> parseFuncDef(
        int32_t offset);
    [[nodiscard]] ParseResult<FuncTypeKeyword> parseFuncType(int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<Block>> parseBlock(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<BlockItemNode>> parseBlockItem(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<DeclNode>> parseDecl(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<ConstDecl>> parseConstDecl(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<VarDecl>> parseVarDecl(
        int32_t offset);
    [[nodiscard]] ParseResult<BTypeKeyword> parseBType(int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<ConstDef>> parseConstDef(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<ConstInitVal>> parseConstInitVal(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<VarDef>> parseVarDef(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<InitVal>> parseInitVal(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<ConstExp>> parseConstExp(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<StmtNode>> parseStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<AssignStmt>> parseAssignStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<ExpStmt>> parseExpStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<ReturnStmt>> parseReturnStmt(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<Exp>> parseExp(int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<LOrExp>> parseLOrExp(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<LAndExp>> parseLAndExp(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<EqExp>> parseEqExp(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<RelExp>> parseRelExp(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<AddExp>> parseAddExp(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<MulExp>> parseMulExp(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<PrimaryExp>> parsePrimaryExp(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<UnaryExp>> parseUnaryExp(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<LVal>> parseLVal(int32_t offset);
    [[nodiscard]] ParseResult<UnaryOpKeyword> parseUnaryOp(int32_t offset);
    [[nodiscard]] ParseResult<MulOpKeyword> parseMulOp(int32_t offset);
    [[nodiscard]] ParseResult<AddOpKeyword> parseAddOp(int32_t offset);
    [[nodiscard]] ParseResult<RelOpKeyword> parseRelOp(int32_t offset);
    [[nodiscard]] ParseResult<EqOpKeyword> parseEqOp(int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<Number>> parseNumber(
        int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<Identifier>> parseIdent(
        int32_t offset);

    [[nodiscard]] ParseResult<int32_t> parseIntConst(int32_t offset);
    [[nodiscard]] ParseResult<int32_t> parseHexadecimalConst(int32_t offset);
    [[nodiscard]] ParseResult<int32_t> parseOctalConst(int32_t offset);
    [[nodiscard]] ParseResult<int32_t> parseDecimalConst(int32_t offset);

    [[nodiscard]] int32_t skipTrivia(int32_t offset) const;
    [[nodiscard]] int32_t recoverToFuncHeaderEnd(int32_t offset) const;
    [[nodiscard]] int32_t recoverToExprRParen(int32_t offset) const;
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
    [[nodiscard]] ParseOutput failureOutput() const;

    std::string m_source;
    int32_t m_bestFailureOffset = -1;
    std::vector<Diagnostic> m_bestDiagnostics;

    std::unordered_map<int32_t, ParseResult<std::shared_ptr<CompUnit>>>
        m_compUnitMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<FuncDef>>>
        m_funcDefMemo;
    std::unordered_map<int32_t, ParseResult<FuncTypeKeyword>> m_funcTypeMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<Block>>>
        m_blockMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<BlockItemNode>>>
        m_blockItemMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<DeclNode>>>
        m_declMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<ConstDecl>>>
        m_constDeclMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<VarDecl>>>
        m_varDeclMemo;
    std::unordered_map<int32_t, ParseResult<BTypeKeyword>> m_bTypeMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<ConstDef>>>
        m_constDefMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<ConstInitVal>>>
        m_constInitValMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<VarDef>>>
        m_varDefMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<InitVal>>>
        m_initValMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<ConstExp>>>
        m_constExpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<StmtNode>>>
        m_stmtMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<AssignStmt>>>
        m_assignStmtMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<ExpStmt>>>
        m_expStmtMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<ReturnStmt>>>
        m_returnStmtMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<Exp>>> m_expMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<LOrExp>>>
        m_lOrExpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<LAndExp>>>
        m_lAndExpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<EqExp>>>
        m_eqExpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<RelExp>>>
        m_relExpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<AddExp>>>
        m_addExpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<MulExp>>>
        m_mulExpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<PrimaryExp>>>
        m_primaryExpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<UnaryExp>>>
        m_unaryExpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<LVal>>> m_lValMemo;
    std::unordered_map<int32_t, ParseResult<UnaryOpKeyword>> m_unaryOpMemo;
    std::unordered_map<int32_t, ParseResult<MulOpKeyword>> m_mulOpMemo;
    std::unordered_map<int32_t, ParseResult<AddOpKeyword>> m_addOpMemo;
    std::unordered_map<int32_t, ParseResult<RelOpKeyword>> m_relOpMemo;
    std::unordered_map<int32_t, ParseResult<EqOpKeyword>> m_eqOpMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<Number>>>
        m_numberMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<Identifier>>>
        m_identMemo;
};

} // namespace yesod::frontend

#endif