#include "frontend/parser.h"

#include <charconv>
#include <cctype>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace yesod::frontend {

namespace {

bool isOctalDigit(char ch) {
    return ch >= '0' && ch <= '7';
}

bool isDecimalDigit(char ch) {
    return ch >= '0' && ch <= '9';
}

bool isHexadecimalDigit(char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

}  // namespace

Parser::Parser(std::string source) : m_source(std::move(source)) {}

ParseOutput Parser::parse() {
    m_bestFailureOffset = -1;
    m_bestDiagnostics.clear();
    m_compUnitMemo.clear();
    m_funcDefMemo.clear();
    m_funcTypeMemo.clear();
    m_blockMemo.clear();
    m_stmtMemo.clear();
    m_numberMemo.clear();
    m_identMemo.clear();

    const auto compUnit = parseCompUnit(0);
    if (!compUnit.m_success) {
        return failureOutput();
    }

    const auto trailingOffset = skipTrivia(compUnit.m_nextOffset);
    if (!isAtEnd(trailingOffset)) {
        return ParseOutput {
            .m_root = nullptr,
            .m_diagnostics = {Diagnostic {
                .m_kind = DiagnosticKind::trailingInput,
                .m_offset = trailingOffset,
                .m_message = "unexpected trailing input",
            }},
        };
    }

    return ParseOutput {
        .m_root = compUnit.m_value,
        .m_diagnostics = m_bestDiagnostics,
    };
}

ParseResult<std::shared_ptr<CompUnit>> Parser::parseCompUnit(int32_t offset) {
    if (const auto memoIt = m_compUnitMemo.find(offset); memoIt != m_compUnitMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto funcDef = parseFuncDef(normalizedOffset);
    if (!funcDef.m_success) {
        const auto failure = ParseResult<std::shared_ptr<CompUnit>> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = nullptr,
        };
        m_compUnitMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<std::shared_ptr<CompUnit>> {
        .m_success = true,
        .m_nextOffset = funcDef.m_nextOffset,
        .m_value = std::make_shared<CompUnit>(normalizedOffset, funcDef.m_value),
    };
    m_compUnitMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<FuncDef>> Parser::parseFuncDef(int32_t offset) {
    if (const auto memoIt = m_funcDefMemo.find(offset); memoIt != m_funcDefMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto funcType = parseFuncType(normalizedOffset);
    if (!funcType.m_success) {
        const auto failure = ParseResult<std::shared_ptr<FuncDef>> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = nullptr,
        };
        m_funcDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto identifier = parseIdent(funcType.m_nextOffset);
    if (!identifier.m_success) {
        const auto failure = ParseResult<std::shared_ptr<FuncDef>> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = nullptr,
        };
        m_funcDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto openParen = matchSymbol(identifier.m_nextOffset, '(');
    if (!openParen.m_success) {
        recordFailure(skipTrivia(identifier.m_nextOffset), DiagnosticKind::expectedSymbol, "expected '('");
        const auto failure = ParseResult<std::shared_ptr<FuncDef>> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = nullptr,
        };
        m_funcDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto closeParen = matchSymbol(openParen.m_nextOffset, ')');
    if (!closeParen.m_success) {
        const auto recoveryOffset = recoverToRParen(openParen.m_nextOffset);
        recordCommittedFailure(skipTrivia(openParen.m_nextOffset), DiagnosticKind::missingRParen, "missing ')' in function declarator");

        auto blockOffset = recoveryOffset;
        const auto recoveredParen = matchSymbol(recoveryOffset, ')');
        if (recoveredParen.m_success) {
            blockOffset = recoveredParen.m_nextOffset;
        }

        const auto block = parseBlock(blockOffset);
        if (!block.m_success) {
            const auto failure = ParseResult<std::shared_ptr<FuncDef>> {
                .m_success = false,
                .m_nextOffset = recoveryOffset,
                .m_value = nullptr,
            };
            m_funcDefMemo.emplace(offset, failure);
            return failure;
        }

        const auto recoveredResult = ParseResult<std::shared_ptr<FuncDef>> {
            .m_success = true,
            .m_nextOffset = block.m_nextOffset,
            .m_value = std::make_shared<FuncDef>(
                normalizedOffset,
                funcType.m_value,
                identifier.m_value,
                block.m_value),
        };
        m_funcDefMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto block = parseBlock(closeParen.m_nextOffset);
    if (!block.m_success) {
        const auto failure = ParseResult<std::shared_ptr<FuncDef>> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = nullptr,
        };
        m_funcDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<std::shared_ptr<FuncDef>> {
        .m_success = true,
        .m_nextOffset = block.m_nextOffset,
        .m_value = std::make_shared<FuncDef>(
            normalizedOffset,
            funcType.m_value,
            identifier.m_value,
            block.m_value),
    };
    m_funcDefMemo.emplace(offset, result);
    return result;
}

ParseResult<FuncTypeKeyword> Parser::parseFuncType(int32_t offset) {
    if (const auto memoIt = m_funcTypeMemo.find(offset); memoIt != m_funcTypeMemo.end()) {
        return memoIt->second;
    }

    const auto keyword = matchKeyword(offset, "int");
    if (!keyword.m_success) {
        recordFailure(skipTrivia(offset), DiagnosticKind::expectedKeyword, "expected 'int'");
        const auto failure = ParseResult<FuncTypeKeyword> {
            .m_success = false,
            .m_nextOffset = skipTrivia(offset),
            .m_value = FuncTypeKeyword::intKeyword,
        };
        m_funcTypeMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<FuncTypeKeyword> {
        .m_success = true,
        .m_nextOffset = keyword.m_nextOffset,
        .m_value = FuncTypeKeyword::intKeyword,
    };
    m_funcTypeMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<Block>> Parser::parseBlock(int32_t offset) {
    if (const auto memoIt = m_blockMemo.find(offset); memoIt != m_blockMemo.end()) {
        return memoIt->second;
    }

    const auto openBrace = matchSymbol(offset, '{');
    if (!openBrace.m_success) {
        recordFailure(skipTrivia(offset), DiagnosticKind::expectedSymbol, "expected '{'");
        const auto failure = ParseResult<std::shared_ptr<Block>> {
            .m_success = false,
            .m_nextOffset = skipTrivia(offset),
            .m_value = nullptr,
        };
        m_blockMemo.emplace(offset, failure);
        return failure;
    }

    const auto stmt = parseStmt(openBrace.m_nextOffset);
    if (!stmt.m_success) {
        recordCommittedFailure(skipTrivia(openBrace.m_nextOffset), DiagnosticKind::malformedStmtHead, "malformed statement head");
        const auto failure = ParseResult<std::shared_ptr<Block>> {
            .m_success = false,
            .m_nextOffset = recoverToStmtBoundary(openBrace.m_nextOffset),
            .m_value = nullptr,
        };
        m_blockMemo.emplace(offset, failure);
        return failure;
    }

    const auto closeBrace = matchSymbol(stmt.m_nextOffset, '}');
    if (!closeBrace.m_success) {
        recordCommittedFailure(skipTrivia(stmt.m_nextOffset), DiagnosticKind::missingRBrace, "missing '}' at end of block");
        const auto recoveryOffset = recoverToBlockEnd(stmt.m_nextOffset);
        std::vector<std::shared_ptr<StmtNode>> statements;
        statements.push_back(stmt.m_value);
        const auto recoveredResult = ParseResult<std::shared_ptr<Block>> {
            .m_success = true,
            .m_nextOffset = recoveryOffset,
            .m_value = std::make_shared<Block>(openBrace.m_startOffset, std::move(statements)),
        };
        m_blockMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    std::vector<std::shared_ptr<StmtNode>> statements;
    statements.push_back(stmt.m_value);
    const auto result = ParseResult<std::shared_ptr<Block>> {
        .m_success = true,
        .m_nextOffset = closeBrace.m_nextOffset,
        .m_value = std::make_shared<Block>(openBrace.m_startOffset, std::move(statements)),
    };
    m_blockMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<StmtNode>> Parser::parseStmt(int32_t offset) {
    if (const auto memoIt = m_stmtMemo.find(offset); memoIt != m_stmtMemo.end()) {
        return memoIt->second;
    }

    const auto keyword = matchKeyword(offset, "return");
    if (!keyword.m_success) {
        const auto failure = ParseResult<std::shared_ptr<StmtNode>> {
            .m_success = false,
            .m_nextOffset = skipTrivia(offset),
            .m_value = nullptr,
        };
        m_stmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto number = parseNumber(keyword.m_nextOffset);
    if (!number.m_success) {
        bool keepInnerDiagnostic = false;
        for (const auto& diagnostic : m_bestDiagnostics) {
            if (diagnostic.m_offset == m_bestFailureOffset
                && diagnostic.m_kind == DiagnosticKind::integerOutOfRange) {
                keepInnerDiagnostic = true;
                break;
            }
        }
        if (!keepInnerDiagnostic) {
            const auto recoveryDiagnosticOffset =
                m_bestFailureOffset > skipTrivia(keyword.m_nextOffset)
                ? m_bestFailureOffset
                : skipTrivia(keyword.m_nextOffset);
            recordCommittedFailure(recoveryDiagnosticOffset, DiagnosticKind::malformedReturnValue, "malformed return value");
        }
        const auto failure = ParseResult<std::shared_ptr<StmtNode>> {
            .m_success = false,
            .m_nextOffset = recoverToStmtBoundary(keyword.m_nextOffset),
            .m_value = nullptr,
        };
        m_stmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto semicolon = matchSymbol(number.m_nextOffset, ';');
    if (!semicolon.m_success) {
        recordCommittedFailure(skipTrivia(number.m_nextOffset), DiagnosticKind::missingSemicolon, "missing ';' after return statement");
        auto recoveredOffset = recoverToStmtBoundary(number.m_nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.m_success) {
            recoveredOffset = recoveredSemicolon.m_nextOffset;
        }

        auto returnStmt = std::make_shared<ReturnStmt>(keyword.m_startOffset, number.m_value);
        const auto recoveredResult = ParseResult<std::shared_ptr<StmtNode>> {
            .m_success = true,
            .m_nextOffset = recoveredOffset,
            .m_value = std::make_shared<StmtNode>(keyword.m_startOffset, Stmt {returnStmt}),
        };
        m_stmtMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    auto returnStmt = std::make_shared<ReturnStmt>(keyword.m_startOffset, number.m_value);
    const auto result = ParseResult<std::shared_ptr<StmtNode>> {
        .m_success = true,
        .m_nextOffset = semicolon.m_nextOffset,
        .m_value = std::make_shared<StmtNode>(keyword.m_startOffset, Stmt {returnStmt}),
    };
    m_stmtMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<Number>> Parser::parseNumber(int32_t offset) {
    if (const auto memoIt = m_numberMemo.find(offset); memoIt != m_numberMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto intConst = parseIntConst(normalizedOffset);
    if (!intConst.m_success) {
        const auto failure = ParseResult<std::shared_ptr<Number>> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = nullptr,
        };
        m_numberMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<std::shared_ptr<Number>> {
        .m_success = true,
        .m_nextOffset = intConst.m_nextOffset,
        .m_value = std::make_shared<Number>(normalizedOffset, intConst.m_value),
    };
    m_numberMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<Identifier>> Parser::parseIdent(int32_t offset) {
    if (const auto memoIt = m_identMemo.find(offset); memoIt != m_identMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    if (isAtEnd(normalizedOffset) || !isIdentifierStart(m_source[normalizedOffset])) {
        recordFailure(normalizedOffset, DiagnosticKind::expectedIdentifier, "expected identifier");
        const auto failure = ParseResult<std::shared_ptr<Identifier>> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = nullptr,
        };
        m_identMemo.emplace(offset, failure);
        return failure;
    }

    auto nextOffset = normalizedOffset + 1;
    while (!isAtEnd(nextOffset) && isIdentifierContinue(m_source[nextOffset])) {
        ++nextOffset;
    }

    const auto result = ParseResult<std::shared_ptr<Identifier>> {
        .m_success = true,
        .m_nextOffset = nextOffset,
        .m_value = std::make_shared<Identifier>(
            normalizedOffset,
            m_source.substr(normalizedOffset, nextOffset - normalizedOffset)),
    };
    m_identMemo.emplace(offset, result);
    return result;
}

ParseResult<int32_t> Parser::parseIntConst(int32_t offset) {
    const auto normalizedOffset = skipTrivia(offset);

    if (normalizedOffset + 1 < static_cast<int32_t>(m_source.size())
        && m_source[normalizedOffset] == '0'
        && (m_source[normalizedOffset + 1] == 'x' || m_source[normalizedOffset + 1] == 'X')) {
        return parseHexadecimalConst(normalizedOffset);
    }

    const auto octal = parseOctalConst(normalizedOffset);
    if (octal.m_success) {
        return octal;
    }

    const auto decimal = parseDecimalConst(normalizedOffset);
    if (decimal.m_success) {
        return decimal;
    }

    recordFailure(normalizedOffset, DiagnosticKind::expectedInteger, "expected integer constant");
    return ParseResult<int32_t> {
        .m_success = false,
        .m_nextOffset = normalizedOffset,
        .m_value = 0,
    };
}

ParseResult<int32_t> Parser::parseHexadecimalConst(int32_t offset) {
    const auto normalizedOffset = skipTrivia(offset);
    if (normalizedOffset + 1 >= static_cast<int32_t>(m_source.size()) || m_source[normalizedOffset] != '0'
        || (m_source[normalizedOffset + 1] != 'x' && m_source[normalizedOffset + 1] != 'X')) {
        return ParseResult<int32_t> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = 0,
        };
    }

    return parseBaseInteger(normalizedOffset, 16, 2, isHexadecimalDigit);
}

ParseResult<int32_t> Parser::parseOctalConst(int32_t offset) {
    const auto normalizedOffset = skipTrivia(offset);
    if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] != '0') {
        return ParseResult<int32_t> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = 0,
        };
    }

    return parseBaseInteger(normalizedOffset, 8, 0, isOctalDigit);
}

ParseResult<int32_t> Parser::parseDecimalConst(int32_t offset) {
    const auto normalizedOffset = skipTrivia(offset);
    if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] < '1' || m_source[normalizedOffset] > '9') {
        return ParseResult<int32_t> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = 0,
        };
    }

    return parseBaseInteger(normalizedOffset, 10, 0, isDecimalDigit);
}

int32_t Parser::skipTrivia(int32_t offset) const {
    auto nextOffset = offset;
    while (!isAtEnd(nextOffset)) {
        if (std::isspace(static_cast<unsigned char>(m_source[nextOffset])) != 0) {
            ++nextOffset;
            continue;
        }

        if (nextOffset + 1 >= static_cast<int32_t>(m_source.size()) || m_source[nextOffset] != '/') {
            break;
        }

        if (m_source[nextOffset + 1] == '/') {
            nextOffset += 2;
            while (!isAtEnd(nextOffset) && m_source[nextOffset] != '\n' && m_source[nextOffset] != '\r') {
                ++nextOffset;
            }
            continue;
        }

        if (m_source[nextOffset + 1] == '*') {
            auto commentOffset = nextOffset + 2;
            bool terminated = false;
            while (commentOffset + 1 < static_cast<int32_t>(m_source.size())) {
                if (m_source[commentOffset] == '*' && m_source[commentOffset + 1] == '/') {
                    commentOffset += 2;
                    terminated = true;
                    break;
                }
                ++commentOffset;
            }
            if (!terminated) {
                break;
            }
            nextOffset = commentOffset;
            continue;
        }

        break;
    }
    return nextOffset;
}

int32_t Parser::recoverToRParen(int32_t offset) const {
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == ')' || m_source[normalizedOffset] == '{') {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToStmtBoundary(int32_t offset) const {
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == ';' || m_source[normalizedOffset] == '}') {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToBlockEnd(int32_t offset) const {
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == '}') {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

bool Parser::isAtEnd(int32_t offset) const {
    return offset >= static_cast<int32_t>(m_source.size());
}

bool Parser::isIdentifierStart(char ch) const {
    return ch == '_' || std::isalpha(static_cast<unsigned char>(ch)) != 0;
}

bool Parser::isIdentifierContinue(char ch) const {
    return isIdentifierStart(ch) || std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

bool Parser::hasKeywordBoundary(int32_t offset) const {
    return isAtEnd(offset) || !isIdentifierContinue(m_source[offset]);
}

Parser::KeywordMatch Parser::matchKeyword(int32_t offset, std::string_view keyword) const {
    const auto normalizedOffset = skipTrivia(offset);
    const auto endOffset = normalizedOffset + static_cast<int32_t>(keyword.size());
    if (endOffset > static_cast<int32_t>(m_source.size())) {
        return KeywordMatch {
            .m_success = false,
            .m_startOffset = normalizedOffset,
            .m_nextOffset = normalizedOffset,
        };
    }

    if (m_source.compare(normalizedOffset, keyword.size(), keyword) != 0 || !hasKeywordBoundary(endOffset)) {
        return KeywordMatch {
            .m_success = false,
            .m_startOffset = normalizedOffset,
            .m_nextOffset = normalizedOffset,
        };
    }

    return KeywordMatch {
        .m_success = true,
        .m_startOffset = normalizedOffset,
        .m_nextOffset = endOffset,
    };
}

Parser::KeywordMatch Parser::matchSymbol(int32_t offset, char symbol) const {
    const auto normalizedOffset = skipTrivia(offset);
    if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] != symbol) {
        return KeywordMatch {
            .m_success = false,
            .m_startOffset = normalizedOffset,
            .m_nextOffset = normalizedOffset,
        };
    }

    return KeywordMatch {
        .m_success = true,
        .m_startOffset = normalizedOffset,
        .m_nextOffset = normalizedOffset + 1,
    };
}

ParseResult<int32_t> Parser::parseBaseInteger(
    int32_t offset,
    int base,
    int32_t prefixLength,
    bool (*digitPredicate)(char)) {
    const auto normalizedOffset = skipTrivia(offset);
    auto digitOffset = normalizedOffset + prefixLength;

    if (isAtEnd(digitOffset) || !digitPredicate(m_source[digitOffset])) {
        recordFailure(digitOffset, DiagnosticKind::expectedInteger, "expected integer digits");
        return ParseResult<int32_t> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = 0,
        };
    }

    auto nextOffset = digitOffset + 1;
    while (!isAtEnd(nextOffset) && digitPredicate(m_source[nextOffset])) {
        ++nextOffset;
    }

    int32_t parsedValue = 0;
    const auto digits = std::string_view(m_source).substr(
        normalizedOffset + prefixLength,
        nextOffset - normalizedOffset - prefixLength);
    const auto parseStart = digits.data();
    const auto parseEnd = digits.data() + digits.size();
    const auto parseResult = std::from_chars(parseStart, parseEnd, parsedValue, base);
    if (parseResult.ec == std::errc::result_out_of_range) {
        recordFailure(normalizedOffset, DiagnosticKind::integerOutOfRange, "integer literal is out of int32_t range");
        return ParseResult<int32_t> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = 0,
        };
    }

    if (parseResult.ec != std::errc() || parseResult.ptr != parseEnd) {
        recordFailure(normalizedOffset, DiagnosticKind::expectedInteger, "invalid integer constant");
        return ParseResult<int32_t> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = 0,
        };
    }

    return ParseResult<int32_t> {
        .m_success = true,
        .m_nextOffset = nextOffset,
        .m_value = parsedValue,
    };
}

void Parser::recordFailure(int32_t offset, DiagnosticKind kind, std::string message) {
    if (offset < m_bestFailureOffset) {
        return;
    }

    if (offset > m_bestFailureOffset) {
        m_bestFailureOffset = offset;
        m_bestDiagnostics.clear();
    }

    m_bestDiagnostics.push_back(Diagnostic {
        .m_kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
    });
}

void Parser::recordCommittedFailure(int32_t offset, DiagnosticKind kind, std::string message) {
    if (offset < m_bestFailureOffset) {
        return;
    }

    if (offset > m_bestFailureOffset) {
        m_bestFailureOffset = offset;
        m_bestDiagnostics.clear();
    }

    if (offset == m_bestFailureOffset) {
        m_bestDiagnostics.clear();
    }

    m_bestDiagnostics.push_back(Diagnostic {
        .m_kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
    });
}

ParseOutput Parser::failureOutput() const {
    if (!m_bestDiagnostics.empty()) {
        return ParseOutput {
            .m_root = nullptr,
            .m_diagnostics = m_bestDiagnostics,
        };
    }

    return ParseOutput {
        .m_root = nullptr,
        .m_diagnostics = {Diagnostic {
            .m_kind = DiagnosticKind::unexpectedToken,
            .m_offset = 0,
            .m_message = "parse failed",
        }},
    };
}

}  // namespace yesod::frontend