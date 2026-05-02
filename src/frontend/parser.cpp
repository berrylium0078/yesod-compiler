#include "frontend/parser.h"

#include <charconv>
#include <cctype>
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
    m_expMemo.clear();
    m_lOrExpMemo.clear();
    m_lAndExpMemo.clear();
    m_eqExpMemo.clear();
    m_relExpMemo.clear();
    m_addExpMemo.clear();
    m_mulExpMemo.clear();
    m_primaryExpMemo.clear();
    m_unaryExpMemo.clear();
    m_unaryOpMemo.clear();
    m_mulOpMemo.clear();
    m_addOpMemo.clear();
    m_relOpMemo.clear();
    m_eqOpMemo.clear();
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
        const auto recoveryOffset = recoverToFuncHeaderEnd(openParen.m_nextOffset);
        recordCommittedFailure(skipTrivia(openParen.m_nextOffset), DiagnosticKind::missingFuncRParen, "missing ')' in function declarator");

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
            .m_value = std::make_shared<FuncDef>(normalizedOffset, funcType.m_value, identifier.m_value, block.m_value),
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
        .m_value = std::make_shared<FuncDef>(normalizedOffset, funcType.m_value, identifier.m_value, block.m_value),
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

    const auto exp = parseExp(keyword.m_nextOffset);
    if (!exp.m_success) {
        bool keepInnerDiagnostic = false;
        for (const auto& diagnostic : m_bestDiagnostics) {
            if (diagnostic.m_offset != m_bestFailureOffset) {
                continue;
            }

            if (diagnostic.m_kind == DiagnosticKind::integerOutOfRange
                || diagnostic.m_kind == DiagnosticKind::malformedPrimaryExp) {
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

    const auto semicolon = matchSymbol(exp.m_nextOffset, ';');
    if (!semicolon.m_success) {
        recordCommittedFailure(skipTrivia(exp.m_nextOffset), DiagnosticKind::missingSemicolon, "missing ';' after return statement");
        auto recoveredOffset = recoverToStmtBoundary(exp.m_nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.m_success) {
            recoveredOffset = recoveredSemicolon.m_nextOffset;
        }

        auto returnStmt = std::make_shared<ReturnStmt>(keyword.m_startOffset, exp.m_value);
        const auto recoveredResult = ParseResult<std::shared_ptr<StmtNode>> {
            .m_success = true,
            .m_nextOffset = recoveredOffset,
            .m_value = std::make_shared<StmtNode>(keyword.m_startOffset, Stmt {returnStmt}),
        };
        m_stmtMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    auto returnStmt = std::make_shared<ReturnStmt>(keyword.m_startOffset, exp.m_value);
    const auto result = ParseResult<std::shared_ptr<StmtNode>> {
        .m_success = true,
        .m_nextOffset = semicolon.m_nextOffset,
        .m_value = std::make_shared<StmtNode>(keyword.m_startOffset, Stmt {returnStmt}),
    };
    m_stmtMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<Exp>> Parser::parseExp(int32_t offset) {
    if (const auto memoIt = m_expMemo.find(offset); memoIt != m_expMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto lOrExp = parseLOrExp(normalizedOffset);
    if (!lOrExp.m_success) {
        const auto failure = ParseResult<std::shared_ptr<Exp>> {
            .m_success = false,
            .m_nextOffset = lOrExp.m_nextOffset,
            .m_value = nullptr,
        };
        m_expMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<std::shared_ptr<Exp>> {
        .m_success = true,
        .m_nextOffset = lOrExp.m_nextOffset,
        .m_value = std::make_shared<Exp>(normalizedOffset, lOrExp.m_value),
    };
    m_expMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<LOrExp>> Parser::parseLOrExp(int32_t offset) {
    if (const auto memoIt = m_lOrExpMemo.find(offset); memoIt != m_lOrExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseLAndExp(normalizedOffset);
    if (!head.m_success) {
        const auto failure = ParseResult<std::shared_ptr<LOrExp>> {
            .m_success = false,
            .m_nextOffset = head.m_nextOffset,
            .m_value = nullptr,
        };
        m_lOrExpMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<LOrExp::Tail> tail;
    auto nextOffset = head.m_nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = matchSymbol(nextOffset, "||");
        if (!op.m_success) {
            break;
        }

        const auto rhs = parseLAndExp(op.m_nextOffset);
        if (!rhs.m_success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        tail.push_back({LOrOpKeyword::orOr, rhs.m_value});
        nextOffset = rhs.m_nextOffset;
    }

    const auto result = ParseResult<std::shared_ptr<LOrExp>> {
        .m_success = true,
        .m_nextOffset = nextOffset,
        .m_value = std::make_shared<LOrExp>(normalizedOffset, head.m_value, std::move(tail)),
    };
    m_lOrExpMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<LAndExp>> Parser::parseLAndExp(int32_t offset) {
    if (const auto memoIt = m_lAndExpMemo.find(offset); memoIt != m_lAndExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseEqExp(normalizedOffset);
    if (!head.m_success) {
        const auto failure = ParseResult<std::shared_ptr<LAndExp>> {
            .m_success = false,
            .m_nextOffset = head.m_nextOffset,
            .m_value = nullptr,
        };
        m_lAndExpMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<LAndExp::Tail> tail;
    auto nextOffset = head.m_nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = matchSymbol(nextOffset, "&&");
        if (!op.m_success) {
            break;
        }

        const auto rhs = parseEqExp(op.m_nextOffset);
        if (!rhs.m_success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        tail.push_back({LAndOpKeyword::andAnd, rhs.m_value});
        nextOffset = rhs.m_nextOffset;
    }

    const auto result = ParseResult<std::shared_ptr<LAndExp>> {
        .m_success = true,
        .m_nextOffset = nextOffset,
        .m_value = std::make_shared<LAndExp>(normalizedOffset, head.m_value, std::move(tail)),
    };
    m_lAndExpMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<EqExp>> Parser::parseEqExp(int32_t offset) {
    if (const auto memoIt = m_eqExpMemo.find(offset); memoIt != m_eqExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseRelExp(normalizedOffset);
    if (!head.m_success) {
        const auto failure = ParseResult<std::shared_ptr<EqExp>> {
            .m_success = false,
            .m_nextOffset = head.m_nextOffset,
            .m_value = nullptr,
        };
        m_eqExpMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<EqExp::Tail> tail;
    auto nextOffset = head.m_nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = parseEqOp(nextOffset);
        if (!op.m_success) {
            break;
        }

        const auto rhs = parseRelExp(op.m_nextOffset);
        if (!rhs.m_success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        tail.push_back({op.m_value, rhs.m_value});
        nextOffset = rhs.m_nextOffset;
    }

    const auto result = ParseResult<std::shared_ptr<EqExp>> {
        .m_success = true,
        .m_nextOffset = nextOffset,
        .m_value = std::make_shared<EqExp>(normalizedOffset, head.m_value, std::move(tail)),
    };
    m_eqExpMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<RelExp>> Parser::parseRelExp(int32_t offset) {
    if (const auto memoIt = m_relExpMemo.find(offset); memoIt != m_relExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseAddExp(normalizedOffset);
    if (!head.m_success) {
        const auto failure = ParseResult<std::shared_ptr<RelExp>> {
            .m_success = false,
            .m_nextOffset = head.m_nextOffset,
            .m_value = nullptr,
        };
        m_relExpMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<RelExp::Tail> tail;
    auto nextOffset = head.m_nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = parseRelOp(nextOffset);
        if (!op.m_success) {
            break;
        }

        const auto rhs = parseAddExp(op.m_nextOffset);
        if (!rhs.m_success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        tail.push_back({op.m_value, rhs.m_value});
        nextOffset = rhs.m_nextOffset;
    }

    const auto result = ParseResult<std::shared_ptr<RelExp>> {
        .m_success = true,
        .m_nextOffset = nextOffset,
        .m_value = std::make_shared<RelExp>(normalizedOffset, head.m_value, std::move(tail)),
    };
    m_relExpMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<AddExp>> Parser::parseAddExp(int32_t offset) {
    if (const auto memoIt = m_addExpMemo.find(offset); memoIt != m_addExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseMulExp(normalizedOffset);
    if (!head.m_success) {
        const auto failure = ParseResult<std::shared_ptr<AddExp>> {
            .m_success = false,
            .m_nextOffset = head.m_nextOffset,
            .m_value = nullptr,
        };
        m_addExpMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<AddExp::Tail> tail;
    auto nextOffset = head.m_nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = parseAddOp(nextOffset);
        if (!op.m_success) {
            break;
        }

        const auto rhs = parseMulExp(op.m_nextOffset);
        if (!rhs.m_success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        tail.push_back({op.m_value, rhs.m_value});
        nextOffset = rhs.m_nextOffset;
    }

    const auto result = ParseResult<std::shared_ptr<AddExp>> {
        .m_success = true,
        .m_nextOffset = nextOffset,
        .m_value = std::make_shared<AddExp>(normalizedOffset, head.m_value, std::move(tail)),
    };
    m_addExpMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<MulExp>> Parser::parseMulExp(int32_t offset) {
    if (const auto memoIt = m_mulExpMemo.find(offset); memoIt != m_mulExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseUnaryExp(normalizedOffset);
    if (!head.m_success) {
        const auto failure = ParseResult<std::shared_ptr<MulExp>> {
            .m_success = false,
            .m_nextOffset = head.m_nextOffset,
            .m_value = nullptr,
        };
        m_mulExpMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<MulExp::Tail> tail;
    auto nextOffset = head.m_nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = parseMulOp(nextOffset);
        if (!op.m_success) {
            break;
        }

        const auto rhs = parseUnaryExp(op.m_nextOffset);
        if (!rhs.m_success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        tail.push_back({op.m_value, rhs.m_value});
        nextOffset = rhs.m_nextOffset;
    }

    const auto result = ParseResult<std::shared_ptr<MulExp>> {
        .m_success = true,
        .m_nextOffset = nextOffset,
        .m_value = std::make_shared<MulExp>(normalizedOffset, head.m_value, std::move(tail)),
    };
    m_mulExpMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<PrimaryExp>> Parser::parsePrimaryExp(int32_t offset) {
    if (const auto memoIt = m_primaryExpMemo.find(offset); memoIt != m_primaryExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto openParen = matchSymbol(normalizedOffset, '(');
    if (openParen.m_success) {
        const auto exp = parseExp(openParen.m_nextOffset);
        if (!exp.m_success) {
            bool keepInnerDiagnostic = false;
            for (const auto& diagnostic : m_bestDiagnostics) {
                if (diagnostic.m_offset != m_bestFailureOffset) {
                    continue;
                }

                if (diagnostic.m_kind == DiagnosticKind::integerOutOfRange
                    || diagnostic.m_kind == DiagnosticKind::malformedPrimaryExp) {
                    keepInnerDiagnostic = true;
                    break;
                }
            }

            if (!keepInnerDiagnostic) {
                const auto recoveryDiagnosticOffset =
                    m_bestFailureOffset > skipTrivia(openParen.m_nextOffset)
                    ? m_bestFailureOffset
                    : skipTrivia(openParen.m_nextOffset);
                recordCommittedFailure(recoveryDiagnosticOffset, DiagnosticKind::malformedPrimaryExp, "malformed parenthesized expression");
            }

            const auto failure = ParseResult<std::shared_ptr<PrimaryExp>> {
                .m_success = false,
                .m_nextOffset = recoverToExprRParen(openParen.m_nextOffset),
                .m_value = nullptr,
            };
            m_primaryExpMemo.emplace(offset, failure);
            return failure;
        }

        const auto closeParen = matchSymbol(exp.m_nextOffset, ')');
        if (!closeParen.m_success) {
            recordCommittedFailure(skipTrivia(exp.m_nextOffset), DiagnosticKind::missingPrimaryRParen, "missing ')' after parenthesized expression");
            auto recoveredOffset = recoverToExprRParen(exp.m_nextOffset);
            const auto recoveredParen = matchSymbol(recoveredOffset, ')');
            if (recoveredParen.m_success) {
                recoveredOffset = recoveredParen.m_nextOffset;
            }

            const auto recoveredResult = ParseResult<std::shared_ptr<PrimaryExp>> {
                .m_success = true,
                .m_nextOffset = recoveredOffset,
                .m_value = std::make_shared<PrimaryExp>(openParen.m_startOffset, PrimaryExp::Kind {exp.m_value}),
            };
            m_primaryExpMemo.emplace(offset, recoveredResult);
            return recoveredResult;
        }

        const auto result = ParseResult<std::shared_ptr<PrimaryExp>> {
            .m_success = true,
            .m_nextOffset = closeParen.m_nextOffset,
            .m_value = std::make_shared<PrimaryExp>(openParen.m_startOffset, PrimaryExp::Kind {exp.m_value}),
        };
        m_primaryExpMemo.emplace(offset, result);
        return result;
    }

    if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] < '0' || m_source[normalizedOffset] > '9') {
        const auto failure = ParseResult<std::shared_ptr<PrimaryExp>> {
            .m_success = false,
            .m_nextOffset = normalizedOffset,
            .m_value = nullptr,
        };
        m_primaryExpMemo.emplace(offset, failure);
        return failure;
    }

    const auto number = parseNumber(normalizedOffset);
    if (!number.m_success) {
        const auto failure = ParseResult<std::shared_ptr<PrimaryExp>> {
            .m_success = false,
            .m_nextOffset = number.m_nextOffset,
            .m_value = nullptr,
        };
        m_primaryExpMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<std::shared_ptr<PrimaryExp>> {
        .m_success = true,
        .m_nextOffset = number.m_nextOffset,
        .m_value = std::make_shared<PrimaryExp>(number.m_value->m_startOffset, PrimaryExp::Kind {number.m_value}),
    };
    m_primaryExpMemo.emplace(offset, result);
    return result;
}

ParseResult<std::shared_ptr<UnaryExp>> Parser::parseUnaryExp(int32_t offset) {
    if (const auto memoIt = m_unaryExpMemo.find(offset); memoIt != m_unaryExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto primaryExp = parsePrimaryExp(normalizedOffset);
    if (primaryExp.m_success) {
        const auto result = ParseResult<std::shared_ptr<UnaryExp>> {
            .m_success = true,
            .m_nextOffset = primaryExp.m_nextOffset,
            .m_value = std::make_shared<UnaryExp>(primaryExp.m_value->m_startOffset, UnaryExp::Kind {primaryExp.m_value}),
        };
        m_unaryExpMemo.emplace(offset, result);
        return result;
    }

    const auto unaryOp = parseUnaryOp(normalizedOffset);
    if (!unaryOp.m_success) {
        const auto failure = ParseResult<std::shared_ptr<UnaryExp>> {
            .m_success = false,
            .m_nextOffset = primaryExp.m_nextOffset,
            .m_value = nullptr,
        };
        m_unaryExpMemo.emplace(offset, failure);
        return failure;
    }

    const auto nestedUnaryExp = parseUnaryExp(unaryOp.m_nextOffset);
    if (!nestedUnaryExp.m_success) {
        const auto failure = ParseResult<std::shared_ptr<UnaryExp>> {
            .m_success = false,
            .m_nextOffset = nestedUnaryExp.m_nextOffset,
            .m_value = nullptr,
        };
        m_unaryExpMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<std::shared_ptr<UnaryExp>> {
        .m_success = true,
        .m_nextOffset = nestedUnaryExp.m_nextOffset,
        .m_value = std::make_shared<UnaryExp>(normalizedOffset, UnaryExp::Kind {std::make_pair(unaryOp.m_value, nestedUnaryExp.m_value)}),
    };
    m_unaryExpMemo.emplace(offset, result);
    return result;
}

ParseResult<UnaryOpKeyword> Parser::parseUnaryOp(int32_t offset) {
    if (const auto memoIt = m_unaryOpMemo.find(offset); memoIt != m_unaryOpMemo.end()) {
        return memoIt->second;
    }

    const auto plus = matchSymbol(offset, '+');
    if (plus.m_success) {
        const auto result = ParseResult<UnaryOpKeyword> {
            .m_success = true,
            .m_nextOffset = plus.m_nextOffset,
            .m_value = UnaryOpKeyword::plus,
        };
        m_unaryOpMemo.emplace(offset, result);
        return result;
    }

    const auto minus = matchSymbol(offset, '-');
    if (minus.m_success) {
        const auto result = ParseResult<UnaryOpKeyword> {
            .m_success = true,
            .m_nextOffset = minus.m_nextOffset,
            .m_value = UnaryOpKeyword::minus,
        };
        m_unaryOpMemo.emplace(offset, result);
        return result;
    }

    const auto bang = matchSymbol(offset, '!');
    if (bang.m_success) {
        const auto result = ParseResult<UnaryOpKeyword> {
            .m_success = true,
            .m_nextOffset = bang.m_nextOffset,
            .m_value = UnaryOpKeyword::bang,
        };
        m_unaryOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<UnaryOpKeyword> {
        .m_success = false,
        .m_nextOffset = skipTrivia(offset),
        .m_value = UnaryOpKeyword::plus,
    };
    m_unaryOpMemo.emplace(offset, failure);
    return failure;
}

ParseResult<MulOpKeyword> Parser::parseMulOp(int32_t offset) {
    if (const auto memoIt = m_mulOpMemo.find(offset); memoIt != m_mulOpMemo.end()) {
        return memoIt->second;
    }

    const auto star = matchSymbol(offset, '*');
    if (star.m_success) {
        const auto result = ParseResult<MulOpKeyword> {
            .m_success = true,
            .m_nextOffset = star.m_nextOffset,
            .m_value = MulOpKeyword::star,
        };
        m_mulOpMemo.emplace(offset, result);
        return result;
    }

    const auto slash = matchSymbol(offset, '/');
    if (slash.m_success) {
        const auto result = ParseResult<MulOpKeyword> {
            .m_success = true,
            .m_nextOffset = slash.m_nextOffset,
            .m_value = MulOpKeyword::slash,
        };
        m_mulOpMemo.emplace(offset, result);
        return result;
    }

    const auto percent = matchSymbol(offset, '%');
    if (percent.m_success) {
        const auto result = ParseResult<MulOpKeyword> {
            .m_success = true,
            .m_nextOffset = percent.m_nextOffset,
            .m_value = MulOpKeyword::percent,
        };
        m_mulOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<MulOpKeyword> {
        .m_success = false,
        .m_nextOffset = skipTrivia(offset),
        .m_value = MulOpKeyword::star,
    };
    m_mulOpMemo.emplace(offset, failure);
    return failure;
}

ParseResult<AddOpKeyword> Parser::parseAddOp(int32_t offset) {
    if (const auto memoIt = m_addOpMemo.find(offset); memoIt != m_addOpMemo.end()) {
        return memoIt->second;
    }

    const auto plus = matchSymbol(offset, '+');
    if (plus.m_success) {
        const auto result = ParseResult<AddOpKeyword> {
            .m_success = true,
            .m_nextOffset = plus.m_nextOffset,
            .m_value = AddOpKeyword::plus,
        };
        m_addOpMemo.emplace(offset, result);
        return result;
    }

    const auto minus = matchSymbol(offset, '-');
    if (minus.m_success) {
        const auto result = ParseResult<AddOpKeyword> {
            .m_success = true,
            .m_nextOffset = minus.m_nextOffset,
            .m_value = AddOpKeyword::minus,
        };
        m_addOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<AddOpKeyword> {
        .m_success = false,
        .m_nextOffset = skipTrivia(offset),
        .m_value = AddOpKeyword::plus,
    };
    m_addOpMemo.emplace(offset, failure);
    return failure;
}

ParseResult<RelOpKeyword> Parser::parseRelOp(int32_t offset) {
    if (const auto memoIt = m_relOpMemo.find(offset); memoIt != m_relOpMemo.end()) {
        return memoIt->second;
    }

    const auto lessEqual = matchSymbol(offset, "<=");
    if (lessEqual.m_success) {
        const auto result = ParseResult<RelOpKeyword> {
            .m_success = true,
            .m_nextOffset = lessEqual.m_nextOffset,
            .m_value = RelOpKeyword::lessEqual,
        };
        m_relOpMemo.emplace(offset, result);
        return result;
    }

    const auto greaterEqual = matchSymbol(offset, ">=");
    if (greaterEqual.m_success) {
        const auto result = ParseResult<RelOpKeyword> {
            .m_success = true,
            .m_nextOffset = greaterEqual.m_nextOffset,
            .m_value = RelOpKeyword::greaterEqual,
        };
        m_relOpMemo.emplace(offset, result);
        return result;
    }

    const auto less = matchSymbol(offset, '<');
    if (less.m_success) {
        const auto result = ParseResult<RelOpKeyword> {
            .m_success = true,
            .m_nextOffset = less.m_nextOffset,
            .m_value = RelOpKeyword::less,
        };
        m_relOpMemo.emplace(offset, result);
        return result;
    }

    const auto greater = matchSymbol(offset, '>');
    if (greater.m_success) {
        const auto result = ParseResult<RelOpKeyword> {
            .m_success = true,
            .m_nextOffset = greater.m_nextOffset,
            .m_value = RelOpKeyword::greater,
        };
        m_relOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<RelOpKeyword> {
        .m_success = false,
        .m_nextOffset = skipTrivia(offset),
        .m_value = RelOpKeyword::less,
    };
    m_relOpMemo.emplace(offset, failure);
    return failure;
}

ParseResult<EqOpKeyword> Parser::parseEqOp(int32_t offset) {
    if (const auto memoIt = m_eqOpMemo.find(offset); memoIt != m_eqOpMemo.end()) {
        return memoIt->second;
    }

    const auto equal = matchSymbol(offset, "==");
    if (equal.m_success) {
        const auto result = ParseResult<EqOpKeyword> {
            .m_success = true,
            .m_nextOffset = equal.m_nextOffset,
            .m_value = EqOpKeyword::equal,
        };
        m_eqOpMemo.emplace(offset, result);
        return result;
    }

    const auto notEqual = matchSymbol(offset, "!=");
    if (notEqual.m_success) {
        const auto result = ParseResult<EqOpKeyword> {
            .m_success = true,
            .m_nextOffset = notEqual.m_nextOffset,
            .m_value = EqOpKeyword::notEqual,
        };
        m_eqOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<EqOpKeyword> {
        .m_success = false,
        .m_nextOffset = skipTrivia(offset),
        .m_value = EqOpKeyword::equal,
    };
    m_eqOpMemo.emplace(offset, failure);
    return failure;
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
        .m_value = std::make_shared<Identifier>(normalizedOffset, m_source.substr(normalizedOffset, nextOffset - normalizedOffset)),
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

int32_t Parser::recoverToFuncHeaderEnd(int32_t offset) const {
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

int32_t Parser::recoverToExprRParen(int32_t offset) const {
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset)
            || m_source[normalizedOffset] == ')'
            || m_source[normalizedOffset] == ';'
            || m_source[normalizedOffset] == '}') {
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

Parser::KeywordMatch Parser::matchSymbol(int32_t offset, std::string_view symbol) const {
    const auto normalizedOffset = skipTrivia(offset);
    const auto endOffset = normalizedOffset + static_cast<int32_t>(symbol.size());
    if (endOffset > static_cast<int32_t>(m_source.size())) {
        return KeywordMatch {
            .m_success = false,
            .m_startOffset = normalizedOffset,
            .m_nextOffset = normalizedOffset,
        };
    }

    if (m_source.compare(normalizedOffset, symbol.size(), symbol) != 0) {
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