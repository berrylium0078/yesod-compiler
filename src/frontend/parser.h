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
    missingRParen,
    malformedStmtHead,
    malformedReturnValue,
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

template <typename T>
struct ParseResult {
    bool m_success = false;
    int32_t m_nextOffset = 0;
    T m_value {};
};

struct ParseOutput {
    std::shared_ptr<CompUnit> m_root;
    std::vector<Diagnostic> m_diagnostics;

    [[nodiscard]] bool success() const {
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

    [[nodiscard]] ParseResult<std::shared_ptr<CompUnit>> parseCompUnit(int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<FuncDef>> parseFuncDef(int32_t offset);
    [[nodiscard]] ParseResult<FuncTypeKeyword> parseFuncType(int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<Block>> parseBlock(int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<StmtNode>> parseStmt(int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<Number>> parseNumber(int32_t offset);
    [[nodiscard]] ParseResult<std::shared_ptr<Identifier>> parseIdent(int32_t offset);

    [[nodiscard]] ParseResult<int32_t> parseIntConst(int32_t offset);
    [[nodiscard]] ParseResult<int32_t> parseHexadecimalConst(int32_t offset);
    [[nodiscard]] ParseResult<int32_t> parseOctalConst(int32_t offset);
    [[nodiscard]] ParseResult<int32_t> parseDecimalConst(int32_t offset);

    [[nodiscard]] int32_t skipTrivia(int32_t offset) const;
    [[nodiscard]] int32_t recoverToRParen(int32_t offset) const;
    [[nodiscard]] int32_t recoverToStmtBoundary(int32_t offset) const;
    [[nodiscard]] int32_t recoverToBlockEnd(int32_t offset) const;
    [[nodiscard]] bool isAtEnd(int32_t offset) const;
    [[nodiscard]] bool isIdentifierStart(char ch) const;
    [[nodiscard]] bool isIdentifierContinue(char ch) const;
    [[nodiscard]] bool hasKeywordBoundary(int32_t offset) const;
    [[nodiscard]] KeywordMatch matchKeyword(int32_t offset, std::string_view keyword) const;
    [[nodiscard]] KeywordMatch matchSymbol(int32_t offset, char symbol) const;
    [[nodiscard]] ParseResult<int32_t> parseBaseInteger(
        int32_t offset,
        int base,
        int32_t prefixLength,
        bool (*digitPredicate)(char));

    void recordFailure(int32_t offset, DiagnosticKind kind, std::string message);
    void recordCommittedFailure(int32_t offset, DiagnosticKind kind, std::string message);
    [[nodiscard]] ParseOutput failureOutput() const;

    std::string m_source;
    int32_t m_bestFailureOffset = -1;
    std::vector<Diagnostic> m_bestDiagnostics;

    std::unordered_map<int32_t, ParseResult<std::shared_ptr<CompUnit>>> m_compUnitMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<FuncDef>>> m_funcDefMemo;
    std::unordered_map<int32_t, ParseResult<FuncTypeKeyword>> m_funcTypeMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<Block>>> m_blockMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<StmtNode>>> m_stmtMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<Number>>> m_numberMemo;
    std::unordered_map<int32_t, ParseResult<std::shared_ptr<Identifier>>> m_identMemo;
};

}  // namespace yesod::frontend

#endif