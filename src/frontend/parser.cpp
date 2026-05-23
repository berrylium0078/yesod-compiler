#include "frontend/parser.h"

#include <cctype>
#include <charconv>
#include <string>
#include <utility>
#include <vector>

namespace yesod::frontend {

namespace {

    bool isOctalDigit(char ch) { return ch >= '0' && ch <= '7'; }

    bool isDecimalDigit(char ch) { return ch >= '0' && ch <= '9'; }

    bool isHexadecimalDigit(char ch)
    {
        return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
    }

    Ref<Exp> makeUnaryExp(
        AST& ast, int32_t startOffset, UnaryOpKeyword op, Ref<Exp> lhs_nn)
    {
        return ast.alloc<Exp>(startOffset,
            Exp::Kind { Exp::Unary {
                .lhs = lhs_nn,
                .op = op,
            } });
    }

    Ref<Exp> makeBinaryRoot(AST& ast, int32_t startOffset, BinaryOpKeyword op,
        Ref<Exp> lhs_nn, Ref<Exp> rhs_nn)
    {
        return ast.alloc<Exp>(startOffset,
            Exp::Kind { Exp::Binary {
                .lhs = lhs_nn,
                .rhs = rhs_nn,
                .op = op,
            } });
    }

} // namespace

Parser::Parser(std::string source)
    : m_source(std::move(source))
{
}

ParseOutput Parser::parse()
{
    m_ast.clear();
    m_bestFailureOffset = -1;
    m_bestDiagnostics.clear();
    m_compUnitMemo.clear();
    m_funcDefMemo.clear();
    m_funcTypeMemo.clear();
    m_blockMemo.clear();
    m_blockItemMemo.clear();
    m_declMemo.clear();
    m_constDeclMemo.clear();
    m_varDeclMemo.clear();
    m_bTypeMemo.clear();
    m_constDefMemo.clear();
    m_arrayConstDimsMemo.clear();
    m_constInitValMemo.clear();
    m_constInitValListMemo.clear();
    m_varDefMemo.clear();
    m_initValMemo.clear();
    m_initValListMemo.clear();
    m_stmtMemo.clear();
    m_ifStmtMemo.clear();
    m_whileStmtMemo.clear();
    m_breakStmtMemo.clear();
    m_continueStmtMemo.clear();
    m_assignStmtMemo.clear();
    m_expStmtMemo.clear();
    m_returnStmtMemo.clear();
    m_expMemo.clear();
    m_lOrExpMemo.clear();
    m_lAndExpMemo.clear();
    m_eqExpMemo.clear();
    m_relExpMemo.clear();
    m_addExpMemo.clear();
    m_mulExpMemo.clear();
    m_primaryExpMemo.clear();
    m_unaryExpMemo.clear();
    m_lValMemo.clear();
    m_lValIndicesMemo.clear();
    m_unaryOpMemo.clear();
    m_mulOpMemo.clear();
    m_addOpMemo.clear();
    m_relOpMemo.clear();
    m_eqOpMemo.clear();
    m_numberMemo.clear();
    m_identMemo.clear();
    m_paramArraySuffixMemo.clear();

    const auto compUnit = parseCompUnit(0);
    if (!compUnit.success) {
        return failureOutput();
    }

    const auto trailingOffset = skipTrivia(compUnit.nextOffset);
    if (!isAtEnd(trailingOffset)) {
        return ParseOutput {
            .m_ast = std::move(m_ast),
            .m_root = { },
            .m_diagnostics = { Diagnostic {
                .kind = DiagnosticKind::trailingInput,
                .m_offset = trailingOffset,
                .m_message = "unexpected trailing input",
            } },
        };
    }

    return ParseOutput {
        .m_ast = std::move(m_ast),
        .m_root = compUnit.value,
        .m_diagnostics = m_bestDiagnostics,
    };
}

ParseResult<Ptr<CompUnit>> Parser::parseCompUnit(int32_t offset)
{
    if (const auto memoIt = m_compUnitMemo.find(offset);
        memoIt != m_compUnitMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    std::vector<CompUnit::Item> topLevelItems;
    auto nextOffset = normalizedOffset;

    while (true) {
        const auto currentOffset = skipTrivia(nextOffset);
        if (isAtEnd(currentOffset)) {
            break;
        }

        const auto constKeyword = matchKeyword(currentOffset, "const");
        if (constKeyword.success) {
            const auto decl = parseDecl(currentOffset);
            if (!decl.success) {
                const auto failure = ParseResult<Ptr<CompUnit>> {
                    .success = false,
                    .nextOffset = decl.nextOffset,
                    .value = { },
                };
                m_compUnitMemo.emplace(offset, failure);
                return failure;
            }

            topLevelItems.emplace_back(decl.value.value());
            nextOffset = decl.nextOffset;
            continue;
        }

        const auto voidKeyword = matchKeyword(currentOffset, "void");
        if (voidKeyword.success) {
            const auto funcDef = parseFuncDef(currentOffset);
            if (!funcDef.success) {
                const auto failure = ParseResult<Ptr<CompUnit>> {
                    .success = false,
                    .nextOffset = funcDef.nextOffset,
                    .value = { },
                };
                m_compUnitMemo.emplace(offset, failure);
                return failure;
            }

            topLevelItems.emplace_back(funcDef.value.ref());
            nextOffset = funcDef.nextOffset;
            continue;
        }

        const auto intKeyword = matchKeyword(currentOffset, "int");
        if (!intKeyword.success) {
            break;
        }

        const auto identifier = parseIdent(intKeyword.nextOffset);
        if (!identifier.success) {
            break;
        }

        const auto openParen = matchSymbol(identifier.nextOffset, '(');
        if (openParen.success) {
            const auto funcDef = parseFuncDef(currentOffset);
            if (!funcDef.success) {
                const auto failure = ParseResult<Ptr<CompUnit>> {
                    .success = false,
                    .nextOffset = funcDef.nextOffset,
                    .value = { },
                };
                m_compUnitMemo.emplace(offset, failure);
                return failure;
            }
            topLevelItems.emplace_back(funcDef.value.ref());
            nextOffset = funcDef.nextOffset;
            continue;
        }

        const auto decl = parseDecl(currentOffset);
        if (!decl.success) {
            const auto failure = ParseResult<Ptr<CompUnit>> {
                .success = false,
                .nextOffset = decl.nextOffset,
                .value = { },
            };
            m_compUnitMemo.emplace(offset, failure);
            return failure;
        }
        topLevelItems.emplace_back(decl.value.value());
        nextOffset = decl.nextOffset;
    }

    if (topLevelItems.empty()) {
        const auto failure = ParseResult<Ptr<CompUnit>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_compUnitMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<CompUnit>> {
        .success = true,
        .nextOffset = nextOffset,
        .value
        = m_ast.alloc<CompUnit>(normalizedOffset, std::move(topLevelItems)),
    };
    m_compUnitMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<FuncDef>> Parser::parseFuncDef(int32_t offset)
{
    if (const auto memoIt = m_funcDefMemo.find(offset);
        memoIt != m_funcDefMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto funcType = parseFuncType(normalizedOffset);
    if (!funcType.success) {
        const auto failure = ParseResult<Ptr<FuncDef>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_funcDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto identifier = parseIdent(funcType.nextOffset);
    if (!identifier.success) {
        const auto failure = ParseResult<Ptr<FuncDef>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_funcDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto openParen = matchSymbol(identifier.nextOffset, '(');
    if (!openParen.success) {
        recordFailure(skipTrivia(identifier.nextOffset),
            DiagnosticKind::expectedSymbol, "expected '('");
        const auto failure = ParseResult<Ptr<FuncDef>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_funcDefMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<FuncFParam> funcFParams;
    auto nextOffset = openParen.nextOffset;
    const auto directCloseParen = matchSymbol(nextOffset, ')');
    if (!directCloseParen.success) {
        while (true) {
            const auto bType = parseBType(nextOffset);
            if (!bType.success) {
                recordCommittedFailure(skipTrivia(nextOffset),
                    DiagnosticKind::missingFuncRParen,
                    "malformed function parameter list");
                nextOffset = recoverToFuncHeaderEnd(nextOffset);
                break;
            }

            const auto paramIdentifier = parseIdent(bType.nextOffset);
            if (!paramIdentifier.success) {
                recordCommittedFailure(skipTrivia(bType.nextOffset),
                    DiagnosticKind::expectedIdentifier,
                    "expected parameter identifier");
                nextOffset = recoverToFuncHeaderEnd(bType.nextOffset);
                break;
            }

            auto paramNextOffset = paramIdentifier.nextOffset;
            std::vector<Ref<Exp>> trailingDimensions;
            const auto paramArraySuffix
                = parseParamArraySuffix(paramIdentifier.nextOffset);
            if (paramArraySuffix.success) {
                trailingDimensions
                    = std::move(paramArraySuffix.value.m_trailingDimensions);
                paramNextOffset = paramArraySuffix.nextOffset;
            }

            funcFParams.emplace_back(nextOffset, bType.value,
                paramIdentifier.value.ref(), paramArraySuffix.success,
                std::move(trailingDimensions));
            nextOffset = paramNextOffset;

            const auto comma = matchSymbol(nextOffset, ',');
            if (!comma.success) {
                break;
            }
            nextOffset = comma.nextOffset;
        }
    }

    const auto closeParen = matchSymbol(nextOffset, ')');
    if (!closeParen.success) {
        const auto recoveryOffset = recoverToFuncHeaderEnd(nextOffset);
        recordCommittedFailure(skipTrivia(nextOffset),
            DiagnosticKind::missingFuncRParen,
            "missing ')' in function declarator");

        auto blockOffset = recoveryOffset;
        const auto recoveredParen = matchSymbol(recoveryOffset, ')');
        if (recoveredParen.success) {
            blockOffset = recoveredParen.nextOffset;
        }

        const auto block = parseBlock(blockOffset);
        if (!block.success) {
            const auto failure = ParseResult<Ptr<FuncDef>> {
                .success = false,
                .nextOffset = recoveryOffset,
                .value = { },
            };
            m_funcDefMemo.emplace(offset, failure);
            return failure;
        }

        const auto recoveredResult = ParseResult<Ptr<FuncDef>> {
            .success = true,
            .nextOffset = block.nextOffset,
            .value = m_ast.alloc<FuncDef>(normalizedOffset, funcType.value,
                identifier.value.ref(), std::move(funcFParams),
                block.value.ref()),
        };
        m_funcDefMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto block = parseBlock(closeParen.nextOffset);
    if (!block.success) {
        const auto failure = ParseResult<Ptr<FuncDef>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_funcDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<FuncDef>> {
        .success = true,
        .nextOffset = block.nextOffset,
        .value = m_ast.alloc<FuncDef>(normalizedOffset, funcType.value,
            identifier.value.ref(), std::move(funcFParams), block.value.ref()),
    };
    m_funcDefMemo.emplace(offset, result);
    return result;
}

ParseResult<FuncTypeKeyword> Parser::parseFuncType(int32_t offset)
{
    if (const auto memoIt = m_funcTypeMemo.find(offset);
        memoIt != m_funcTypeMemo.end()) {
        return memoIt->second;
    }

    const auto voidKeyword = matchKeyword(offset, "void");
    if (voidKeyword.success) {
        const auto result = ParseResult<FuncTypeKeyword> {
            .success = true,
            .nextOffset = voidKeyword.nextOffset,
            .value = FuncTypeKeyword::voidKeyword,
        };
        m_funcTypeMemo.emplace(offset, result);
        return result;
    }

    const auto intKeyword = matchKeyword(offset, "int");
    if (!intKeyword.success) {
        recordFailure(skipTrivia(offset), DiagnosticKind::expectedKeyword,
            "expected 'void' or 'int'");
        const auto failure = ParseResult<FuncTypeKeyword> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = FuncTypeKeyword::intKeyword,
        };
        m_funcTypeMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<FuncTypeKeyword> {
        .success = true,
        .nextOffset = intKeyword.nextOffset,
        .value = FuncTypeKeyword::intKeyword,
    };
    m_funcTypeMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Block>> Parser::parseBlock(int32_t offset)
{
    if (const auto memoIt = m_blockMemo.find(offset);
        memoIt != m_blockMemo.end()) {
        return memoIt->second;
    }

    const auto openBrace = matchSymbol(offset, '{');
    if (!openBrace.success) {
        recordFailure(
            skipTrivia(offset), DiagnosticKind::expectedSymbol, "expected '{'");
        const auto failure = ParseResult<Ptr<Block>> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = { },
        };
        m_blockMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<BlockItem> blockItems;
    auto nextOffset = openBrace.nextOffset;

    while (true) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        const auto closeBrace = matchSymbol(normalizedOffset, '}');
        if (closeBrace.success) {
            const auto result = ParseResult<Ptr<Block>> {
                .success = true,
                .nextOffset = closeBrace.nextOffset,
                .value = m_ast.alloc<Block>(
                    openBrace.m_startOffset, std::move(blockItems)),
            };
            m_blockMemo.emplace(offset, result);
            return result;
        }

        if (isAtEnd(normalizedOffset)) {
            recordCommittedFailure(normalizedOffset,
                DiagnosticKind::missingRBrace, "missing '}' at end of block");
            const auto recoveredResult = ParseResult<Ptr<Block>> {
                .success = true,
                .nextOffset = normalizedOffset,
                .value = m_ast.alloc<Block>(
                    openBrace.m_startOffset, std::move(blockItems)),
            };
            m_blockMemo.emplace(offset, recoveredResult);
            return recoveredResult;
        }

        const auto blockItem = parseBlockItem(normalizedOffset);
        if (blockItem.success) {
            blockItems.push_back(blockItem.value.value());
            nextOffset = blockItem.nextOffset;
            continue;
        }

        recordCommittedFailure(normalizedOffset,
            DiagnosticKind::malformedBlockItem, "malformed block item");
        nextOffset = blockItem.nextOffset > normalizedOffset
            ? blockItem.nextOffset
            : recoverToBlockItemBoundary(normalizedOffset + 1);
    }
}

ParseResult<std::optional<BlockItem>> Parser::parseBlockItem(int32_t offset)
{
    if (const auto memoIt = m_blockItemMemo.find(offset);
        memoIt != m_blockItemMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto decl = parseDecl(normalizedOffset);
    if (decl.success) {
        const auto result = ParseResult<std::optional<BlockItem>> {
            .success = true,
            .nextOffset = decl.nextOffset,
            .value = decl.value.value(),
        };
        m_blockItemMemo.emplace(offset, result);
        return result;
    }

    const auto stmt = parseStmt(normalizedOffset);
    if (stmt.success) {
        const auto result = ParseResult<std::optional<BlockItem>> {
            .success = true,
            .nextOffset = stmt.nextOffset,
            .value = stmt.value.value(),
        };
        m_blockItemMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<std::optional<BlockItem>> {
        .success = false,
        .nextOffset = stmt.nextOffset > normalizedOffset ? stmt.nextOffset
                                                         : decl.nextOffset,
        .value = { },
    };
    m_blockItemMemo.emplace(offset, failure);
    return failure;
}

ParseResult<std::optional<Decl>> Parser::parseDecl(int32_t offset)
{
    if (const auto memoIt = m_declMemo.find(offset);
        memoIt != m_declMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto constDecl = parseConstDecl(normalizedOffset);
    if (constDecl.success) {
        const auto result = ParseResult<std::optional<Decl>> {
            .success = true,
            .nextOffset = constDecl.nextOffset,
            .value = Decl { constDecl.value.ref() },
        };
        m_declMemo.emplace(offset, result);
        return result;
    }

    const auto varDecl = parseVarDecl(normalizedOffset);
    if (varDecl.success) {
        const auto result = ParseResult<std::optional<Decl>> {
            .success = true,
            .nextOffset = varDecl.nextOffset,
            .value = Decl { varDecl.value.ref() },
        };
        m_declMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<std::optional<Decl>> {
        .success = false,
        .nextOffset = normalizedOffset,
        .value = { },
    };
    m_declMemo.emplace(offset, failure);
    return failure;
}

ParseResult<Ptr<ConstDecl>> Parser::parseConstDecl(int32_t offset)
{
    if (const auto memoIt = m_constDeclMemo.find(offset);
        memoIt != m_constDeclMemo.end()) {
        return memoIt->second;
    }

    const auto keyword = matchKeyword(offset, "const");
    if (!keyword.success) {
        const auto failure = ParseResult<Ptr<ConstDecl>> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = { },
        };
        m_constDeclMemo.emplace(offset, failure);
        return failure;
    }

    const auto bType = parseBType(keyword.nextOffset);
    if (!bType.success) {
        const auto failure = ParseResult<Ptr<ConstDecl>> {
            .success = false,
            .nextOffset = bType.nextOffset,
            .value = { },
        };
        m_constDeclMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<Ref<ConstDef>> constDefs;
    auto nextOffset = bType.nextOffset;

    const auto firstConstDef = parseConstDef(nextOffset);
    if (firstConstDef.success) {
        constDefs.push_back(firstConstDef.value.ref());
        nextOffset = firstConstDef.nextOffset;
    } else {
        recordCommittedFailure(skipTrivia(nextOffset),
            DiagnosticKind::malformedDeclItem, "malformed declaration item");
        nextOffset = recoverToDeclBoundary(nextOffset);
    }

    while (true) {
        const auto comma = matchSymbol(nextOffset, ',');
        if (!comma.success) {
            break;
        }

        const auto constDef = parseConstDef(comma.nextOffset);
        if (constDef.success) {
            constDefs.push_back(constDef.value.ref());
            nextOffset = constDef.nextOffset;
            continue;
        }

        recordCommittedFailure(skipTrivia(comma.nextOffset),
            DiagnosticKind::malformedDeclItem, "malformed declaration item");
        nextOffset = recoverToDeclBoundary(comma.nextOffset);
        if (!matchSymbol(nextOffset, ',').success) {
            break;
        }
    }

    const auto semicolon = matchSymbol(nextOffset, ';');
    if (!semicolon.success) {
        recordCommittedFailure(skipTrivia(nextOffset),
            DiagnosticKind::missingDeclSemicolon,
            "missing ';' after declaration");
        auto recoveredOffset = recoverToDeclBoundary(nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.success) {
            recoveredOffset = recoveredSemicolon.nextOffset;
        }

        const auto recoveredResult = ParseResult<Ptr<ConstDecl>> {
            .success = true,
            .nextOffset = recoveredOffset,
            .value = m_ast.alloc<ConstDecl>(
                keyword.m_startOffset, bType.value, std::move(constDefs)),
        };
        m_constDeclMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto result = ParseResult<Ptr<ConstDecl>> {
        .success = true,
        .nextOffset = semicolon.nextOffset,
        .value = m_ast.alloc<ConstDecl>(
            keyword.m_startOffset, bType.value, std::move(constDefs)),
    };
    m_constDeclMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<VarDecl>> Parser::parseVarDecl(int32_t offset)
{
    if (const auto memoIt = m_varDeclMemo.find(offset);
        memoIt != m_varDeclMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto bType = parseBType(normalizedOffset);
    if (!bType.success) {
        const auto failure = ParseResult<Ptr<VarDecl>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_varDeclMemo.emplace(offset, failure);
        return failure;
    }

    std::vector<Ref<VarDef>> varDefs;
    auto nextOffset = bType.nextOffset;

    const auto firstVarDef = parseVarDef(nextOffset);
    if (firstVarDef.success) {
        varDefs.push_back(firstVarDef.value.ref());
        nextOffset = firstVarDef.nextOffset;
    } else {
        recordCommittedFailure(skipTrivia(nextOffset),
            DiagnosticKind::malformedDeclItem, "malformed declaration item");
        nextOffset = recoverToDeclBoundary(nextOffset);
    }

    while (true) {
        const auto comma = matchSymbol(nextOffset, ',');
        if (!comma.success) {
            break;
        }

        const auto varDef = parseVarDef(comma.nextOffset);
        if (varDef.success) {
            varDefs.push_back(varDef.value.ref());
            nextOffset = varDef.nextOffset;
            continue;
        }

        recordCommittedFailure(skipTrivia(comma.nextOffset),
            DiagnosticKind::malformedDeclItem, "malformed declaration item");
        nextOffset = recoverToDeclBoundary(comma.nextOffset);
        if (!matchSymbol(nextOffset, ',').success) {
            break;
        }
    }

    const auto semicolon = matchSymbol(nextOffset, ';');
    if (!semicolon.success) {
        recordCommittedFailure(skipTrivia(nextOffset),
            DiagnosticKind::missingDeclSemicolon,
            "missing ';' after declaration");
        auto recoveredOffset = recoverToDeclBoundary(nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.success) {
            recoveredOffset = recoveredSemicolon.nextOffset;
        }

        const auto recoveredResult = ParseResult<Ptr<VarDecl>> {
            .success = true,
            .nextOffset = recoveredOffset,
            .value = m_ast.alloc<VarDecl>(
                normalizedOffset, bType.value, std::move(varDefs)),
        };
        m_varDeclMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto result = ParseResult<Ptr<VarDecl>> {
        .success = true,
        .nextOffset = semicolon.nextOffset,
        .value = m_ast.alloc<VarDecl>(
            normalizedOffset, bType.value, std::move(varDefs)),
    };
    m_varDeclMemo.emplace(offset, result);
    return result;
}

ParseResult<BTypeKeyword> Parser::parseBType(int32_t offset)
{
    if (const auto memoIt = m_bTypeMemo.find(offset);
        memoIt != m_bTypeMemo.end()) {
        return memoIt->second;
    }

    const auto keyword = matchKeyword(offset, "int");
    if (!keyword.success) {
        const auto failure = ParseResult<BTypeKeyword> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = BTypeKeyword::intKeyword,
        };
        m_bTypeMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<BTypeKeyword> {
        .success = true,
        .nextOffset = keyword.nextOffset,
        .value = BTypeKeyword::intKeyword,
    };
    m_bTypeMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<ConstDef>> Parser::parseConstDef(int32_t offset)
{
    if (const auto memoIt = m_constDefMemo.find(offset);
        memoIt != m_constDefMemo.end()) {
        return memoIt->second;
    }

    const auto identifier = parseIdent(offset);
    if (!identifier.success) {
        const auto failure = ParseResult<Ptr<ConstDef>> {
            .success = false,
            .nextOffset = identifier.nextOffset,
            .value = { },
        };
        m_constDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto dimensions = parseArrayConstDims(identifier.nextOffset);
    const auto assign = matchSymbol(dimensions.nextOffset, '=');
    if (!assign.success) {
        const auto failure = ParseResult<Ptr<ConstDef>> {
            .success = false,
            .nextOffset = skipTrivia(dimensions.nextOffset),
            .value = { },
        };
        m_constDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto constInitVal = parseConstInitVal(assign.nextOffset);
    if (!constInitVal.success) {
        const auto failure = ParseResult<Ptr<ConstDef>> {
            .success = false,
            .nextOffset = constInitVal.nextOffset,
            .value = { },
        };
        m_constDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<ConstDef>> {
        .success = true,
        .nextOffset = constInitVal.nextOffset,
        .value
        = m_ast.alloc<ConstDef>(identifier.value(m_ast).sourcePos.m_offset,
            identifier.value.ref(), dimensions.value, constInitVal.value.ref()),
    };
    m_constDefMemo.emplace(offset, result);
    return result;
}

ParseResult<std::vector<Ref<Exp>>> Parser::parseArrayConstDims(int32_t offset)
{
    if (const auto memoIt = m_arrayConstDimsMemo.find(offset);
        memoIt != m_arrayConstDimsMemo.end()) {
        return memoIt->second;
    }

    auto nextOffset = skipTrivia(offset);
    std::vector<Ref<Exp>> dimensions;
    while (true) {
        const auto openBracket = matchSymbol(nextOffset, '[');
        if (!openBracket.success) {
            break;
        }

        const auto boundExp = parseExp(openBracket.nextOffset);
        if (!boundExp.success) {
            auto recoveryOffset = recoverToRBracket(openBracket.nextOffset);
            recordCommittedFailure(skipTrivia(openBracket.nextOffset),
                DiagnosticKind::malformedArrayBound, "malformed array bound");
            const auto recoveredBracket = matchSymbol(recoveryOffset, ']');
            nextOffset = recoveredBracket.success ? recoveredBracket.nextOffset
                                                  : recoveryOffset;
            continue;
        }

        dimensions.push_back(boundExp.value.ref());
        const auto closeBracket = matchSymbol(boundExp.nextOffset, ']');
        if (!closeBracket.success) {
            auto recoveryOffset = recoverToRBracket(boundExp.nextOffset);
            recordCommittedFailure(skipTrivia(boundExp.nextOffset),
                DiagnosticKind::missingArrayRBracket,
                "missing ']' after array bound");
            const auto recoveredBracket = matchSymbol(recoveryOffset, ']');
            nextOffset = recoveredBracket.success ? recoveredBracket.nextOffset
                                                  : recoveryOffset;
            continue;
        }

        nextOffset = closeBracket.nextOffset;
    }

    const auto result = ParseResult<std::vector<Ref<Exp>>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = std::move(dimensions),
    };
    m_arrayConstDimsMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<ConstInitVal>> Parser::parseConstInitVal(int32_t offset)
{
    if (const auto memoIt = m_constInitValMemo.find(offset);
        memoIt != m_constInitValMemo.end()) {
        return memoIt->second;
    }

    const auto openBrace = matchSymbol(offset, '{');
    if (openBrace.success) {
        const auto closeBrace = matchSymbol(openBrace.nextOffset, '}');
        if (closeBrace.success) {
            const auto result = ParseResult<Ptr<ConstInitVal>> {
                .success = true,
                .nextOffset = closeBrace.nextOffset,
                .value = m_ast.alloc<ConstInitVal>(
                    openBrace.m_startOffset, ConstInitVal::List { }),
            };
            m_constInitValMemo.emplace(offset, result);
            return result;
        }

        auto initializerValues = ConstInitVal::List { };
        auto nextOffset = openBrace.nextOffset;
        const auto initValList = parseConstInitValList(openBrace.nextOffset);
        if (initValList.success) {
            initializerValues = initValList.value;
            nextOffset = initValList.nextOffset;
        } else {
            recordCommittedFailure(skipTrivia(openBrace.nextOffset),
                DiagnosticKind::malformedConstInitializer,
                "malformed constant initializer");
            nextOffset = recoverToInitBoundary(openBrace.nextOffset);
        }

        const auto recoveredCloseBrace = matchSymbol(nextOffset, '}');
        if (!recoveredCloseBrace.success) {
            auto recoveryOffset = recoverToInitBoundary(nextOffset);
            recordCommittedFailure(skipTrivia(nextOffset),
                DiagnosticKind::missingConstInitRBrace,
                "missing '}' after constant initializer");
            const auto syncedCloseBrace = matchSymbol(recoveryOffset, '}');
            nextOffset = syncedCloseBrace.success ? syncedCloseBrace.nextOffset
                                                  : recoveryOffset;
        } else {
            nextOffset = recoveredCloseBrace.nextOffset;
        }

        const auto result = ParseResult<Ptr<ConstInitVal>> {
            .success = true,
            .nextOffset = nextOffset,
            .value = m_ast.alloc<ConstInitVal>(
                openBrace.m_startOffset, std::move(initializerValues)),
        };
        m_constInitValMemo.emplace(offset, result);
        return result;
    }

    const auto exp = parseExp(offset);
    if (!exp.success) {
        const auto failure = ParseResult<Ptr<ConstInitVal>> {
            .success = false,
            .nextOffset = exp.nextOffset,
            .value = { },
        };
        m_constInitValMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<ConstInitVal>> {
        .success = true,
        .nextOffset = exp.nextOffset,
        .value = m_ast.alloc<ConstInitVal>(
            exp.value(m_ast).sourcePos.m_offset, exp.value.ref()),
    };
    m_constInitValMemo.emplace(offset, result);
    return result;
}

ParseResult<std::vector<Ref<ConstInitVal>>> Parser::parseConstInitValList(
    int32_t offset)
{
    if (const auto memoIt = m_constInitValListMemo.find(offset);
        memoIt != m_constInitValListMemo.end()) {
        return memoIt->second;
    }

    const auto firstInitVal = parseConstInitVal(offset);
    if (!firstInitVal.success) {
        const auto failure = ParseResult<std::vector<Ref<ConstInitVal>>> {
            .success = false,
            .nextOffset = firstInitVal.nextOffset,
            .value = { },
        };
        m_constInitValListMemo.emplace(offset, failure);
        return failure;
    }

    auto nextOffset = firstInitVal.nextOffset;
    std::vector<Ref<ConstInitVal>> values { firstInitVal.value.ref() };
    while (true) {
        const auto comma = matchSymbol(nextOffset, ',');
        if (!comma.success) {
            break;
        }

        const auto nextInitVal = parseConstInitVal(comma.nextOffset);
        if (!nextInitVal.success) {
            recordCommittedFailure(skipTrivia(comma.nextOffset),
                DiagnosticKind::malformedConstInitializer,
                "malformed constant initializer");
            auto recoveryOffset = recoverToInitBoundary(comma.nextOffset);
            if (matchSymbol(recoveryOffset, ',').success) {
                nextOffset = matchSymbol(recoveryOffset, ',').nextOffset;
                continue;
            }
            nextOffset = recoveryOffset;
            break;
        }

        values.push_back(nextInitVal.value.ref());
        nextOffset = nextInitVal.nextOffset;
    }

    const auto result = ParseResult<std::vector<Ref<ConstInitVal>>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = std::move(values),
    };
    m_constInitValListMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<VarDef>> Parser::parseVarDef(int32_t offset)
{
    if (const auto memoIt = m_varDefMemo.find(offset);
        memoIt != m_varDefMemo.end()) {
        return memoIt->second;
    }

    const auto identifier = parseIdent(offset);
    if (!identifier.success) {
        const auto failure = ParseResult<Ptr<VarDef>> {
            .success = false,
            .nextOffset = identifier.nextOffset,
            .value = { },
        };
        m_varDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto dimensions = parseArrayConstDims(identifier.nextOffset);
    const auto assign = matchSymbol(dimensions.nextOffset, '=');
    if (!assign.success) {
        const auto offset = identifier.value(m_ast).sourcePos.m_offset;
        const auto result = ParseResult<Ptr<VarDef>> {
            .success = true,
            .nextOffset = dimensions.nextOffset,
            .value
            = m_ast.alloc<VarDef>(offset, identifier.value.ref(), dimensions.value,
                m_ast.alloc<InitVal>(offset, InitVal::List { })),
        };
        m_varDefMemo.emplace(offset, result);
        return result;
    }

    const auto initVal = parseInitVal(assign.nextOffset);
    if (!initVal.success) {
        const auto failure = ParseResult<Ptr<VarDef>> {
            .success = false,
            .nextOffset = initVal.nextOffset,
            .value = { },
        };
        m_varDefMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<VarDef>> {
        .success = true,
        .nextOffset = initVal.nextOffset,
        .value = m_ast.alloc<VarDef>(identifier.value(m_ast).sourcePos.m_offset,
            identifier.value.ref(), dimensions.value, initVal.value.ref()),
    };
    m_varDefMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<InitVal>> Parser::parseInitVal(int32_t offset)
{
    if (const auto memoIt = m_initValMemo.find(offset);
        memoIt != m_initValMemo.end()) {
        return memoIt->second;
    }

    const auto openBrace = matchSymbol(offset, '{');
    if (openBrace.success) {
        const auto closeBrace = matchSymbol(openBrace.nextOffset, '}');
        if (closeBrace.success) {
            const auto result = ParseResult<Ptr<InitVal>> {
                .success = true,
                .nextOffset = closeBrace.nextOffset,
                .value = m_ast.alloc<InitVal>(
                    openBrace.m_startOffset, InitVal::List { }),
            };
            m_initValMemo.emplace(offset, result);
            return result;
        }

        auto initializerValues = InitVal::List { };
        auto nextOffset = openBrace.nextOffset;
        const auto initValList = parseInitValList(openBrace.nextOffset);
        if (initValList.success) {
            initializerValues = initValList.value;
            nextOffset = initValList.nextOffset;
        } else {
            recordCommittedFailure(skipTrivia(openBrace.nextOffset),
                DiagnosticKind::malformedInitializer, "malformed initializer");
            nextOffset = recoverToInitBoundary(openBrace.nextOffset);
        }

        const auto recoveredCloseBrace = matchSymbol(nextOffset, '}');
        if (!recoveredCloseBrace.success) {
            auto recoveryOffset = recoverToInitBoundary(nextOffset);
            recordCommittedFailure(skipTrivia(nextOffset),
                DiagnosticKind::missingInitRBrace,
                "missing '}' after initializer");
            const auto syncedCloseBrace = matchSymbol(recoveryOffset, '}');
            nextOffset = syncedCloseBrace.success ? syncedCloseBrace.nextOffset
                                                  : recoveryOffset;
        } else {
            nextOffset = recoveredCloseBrace.nextOffset;
        }

        const auto result = ParseResult<Ptr<InitVal>> {
            .success = true,
            .nextOffset = nextOffset,
            .value = m_ast.alloc<InitVal>(
                openBrace.m_startOffset, std::move(initializerValues)),
        };
        m_initValMemo.emplace(offset, result);
        return result;
    }

    const auto exp = parseExp(offset);
    if (!exp.success) {
        const auto failure = ParseResult<Ptr<InitVal>> {
            .success = false,
            .nextOffset = exp.nextOffset,
            .value = { },
        };
        m_initValMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<InitVal>> {
        .success = true,
        .nextOffset = exp.nextOffset,
        .value = m_ast.alloc<InitVal>(
            exp.value(m_ast).sourcePos.m_offset, exp.value.ref()),
    };
    m_initValMemo.emplace(offset, result);
    return result;
}

ParseResult<std::vector<Ref<InitVal>>> Parser::parseInitValList(int32_t offset)
{
    if (const auto memoIt = m_initValListMemo.find(offset);
        memoIt != m_initValListMemo.end()) {
        return memoIt->second;
    }

    const auto firstInitVal = parseInitVal(offset);
    if (!firstInitVal.success) {
        const auto failure = ParseResult<std::vector<Ref<InitVal>>> {
            .success = false,
            .nextOffset = firstInitVal.nextOffset,
            .value = { },
        };
        m_initValListMemo.emplace(offset, failure);
        return failure;
    }

    auto nextOffset = firstInitVal.nextOffset;
    std::vector<Ref<InitVal>> values { firstInitVal.value.ref() };
    while (true) {
        const auto comma = matchSymbol(nextOffset, ',');
        if (!comma.success) {
            break;
        }

        const auto nextInitVal = parseInitVal(comma.nextOffset);
        if (!nextInitVal.success) {
            recordCommittedFailure(skipTrivia(comma.nextOffset),
                DiagnosticKind::malformedInitializer, "malformed initializer");
            auto recoveryOffset = recoverToInitBoundary(comma.nextOffset);
            if (matchSymbol(recoveryOffset, ',').success) {
                nextOffset = matchSymbol(recoveryOffset, ',').nextOffset;
                continue;
            }
            nextOffset = recoveryOffset;
            break;
        }

        values.push_back(nextInitVal.value.ref());
        nextOffset = nextInitVal.nextOffset;
    }

    const auto result = ParseResult<std::vector<Ref<InitVal>>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = std::move(values),
    };
    m_initValListMemo.emplace(offset, result);
    return result;
}

ParseResult<std::optional<Stmt>> Parser::parseStmt(int32_t offset)
{
    if (const auto memoIt = m_stmtMemo.find(offset);
        memoIt != m_stmtMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto ifKeyword = matchKeyword(normalizedOffset, "if");
    if (ifKeyword.success) {
        const auto ifStmt = parseIfStmt(normalizedOffset);
        if (ifStmt.success) {
            const auto result = ParseResult<std::optional<Stmt>> {
                .success = true,
                .nextOffset = ifStmt.nextOffset,
                .value = Stmt { ifStmt.value.ref() },
            };
            m_stmtMemo.emplace(offset, result);
            return result;
        }

        const auto failure = ParseResult<std::optional<Stmt>> {
            .success = false,
            .nextOffset = ifStmt.nextOffset,
            .value = { },
        };
        m_stmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto whileKeyword = matchKeyword(normalizedOffset, "while");
    if (whileKeyword.success) {
        const auto whileStmt = parseWhileStmt(normalizedOffset);
        if (whileStmt.success) {
            const auto result = ParseResult<std::optional<Stmt>> {
                .success = true,
                .nextOffset = whileStmt.nextOffset,
                .value = Stmt { whileStmt.value.ref() },
            };
            m_stmtMemo.emplace(offset, result);
            return result;
        }

        const auto failure = ParseResult<std::optional<Stmt>> {
            .success = false,
            .nextOffset = whileStmt.nextOffset,
            .value = { },
        };
        m_stmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto breakKeyword = matchKeyword(normalizedOffset, "break");
    if (breakKeyword.success) {
        const auto breakStmt = parseBreakStmt(normalizedOffset);
        if (breakStmt.success) {
            const auto result = ParseResult<std::optional<Stmt>> {
                .success = true,
                .nextOffset = breakStmt.nextOffset,
                .value = Stmt { breakStmt.value.ref() },
            };
            m_stmtMemo.emplace(offset, result);
            return result;
        }

        const auto failure = ParseResult<std::optional<Stmt>> {
            .success = false,
            .nextOffset = breakStmt.nextOffset,
            .value = { },
        };
        m_stmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto continueKeyword = matchKeyword(normalizedOffset, "continue");
    if (continueKeyword.success) {
        const auto continueStmt = parseContinueStmt(normalizedOffset);
        if (continueStmt.success) {
            const auto result = ParseResult<std::optional<Stmt>> {
                .success = true,
                .nextOffset = continueStmt.nextOffset,
                .value = Stmt { continueStmt.value.ref() },
            };
            m_stmtMemo.emplace(offset, result);
            return result;
        }

        const auto failure = ParseResult<std::optional<Stmt>> {
            .success = false,
            .nextOffset = continueStmt.nextOffset,
            .value = { },
        };
        m_stmtMemo.emplace(offset, failure);
        return failure;
    }

    if (!isAtEnd(normalizedOffset)
        && isIdentifierStart(m_source[normalizedOffset])) {
        const auto assignStmt = parseAssignStmt(normalizedOffset);
        if (assignStmt.success) {
            const auto result = ParseResult<std::optional<Stmt>> {
                .success = true,
                .nextOffset = assignStmt.nextOffset,
                .value = Stmt { assignStmt.value.ref() },
            };
            m_stmtMemo.emplace(offset, result);
            return result;
        }
    }

    if (!isAtEnd(normalizedOffset) && m_source[normalizedOffset] == '{') {
        const auto block = parseBlock(normalizedOffset);
        if (block.success) {
            const auto result = ParseResult<std::optional<Stmt>> {
                .success = true,
                .nextOffset = block.nextOffset,
                .value = Stmt { block.value.ref() },
            };
            m_stmtMemo.emplace(offset, result);
            return result;
        }
    }

    const auto returnKeyword = matchKeyword(normalizedOffset, "return");
    if (returnKeyword.success) {
        const auto returnStmt = parseReturnStmt(normalizedOffset);
        if (returnStmt.success) {
            const auto result = ParseResult<std::optional<Stmt>> {
                .success = true,
                .nextOffset = returnStmt.nextOffset,
                .value = Stmt { returnStmt.value.ref() },
            };
            m_stmtMemo.emplace(offset, result);
            return result;
        }

        const auto failure = ParseResult<std::optional<Stmt>> {
            .success = false,
            .nextOffset = returnStmt.nextOffset,
            .value = { },
        };
        m_stmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto expStmt = parseExpStmt(normalizedOffset);
    if (expStmt.success) {
        const auto result = ParseResult<std::optional<Stmt>> {
            .success = true,
            .nextOffset = expStmt.nextOffset,
            .value = Stmt { expStmt.value.ref() },
        };
        m_stmtMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<std::optional<Stmt>> {
        .success = false,
        .nextOffset = normalizedOffset,
        .value = { },
    };
    m_stmtMemo.emplace(offset, failure);
    return failure;
}

ParseResult<Ptr<IfStmt>> Parser::parseIfStmt(int32_t offset)
{
    if (const auto memoIt = m_ifStmtMemo.find(offset);
        memoIt != m_ifStmtMemo.end()) {
        return memoIt->second;
    }

    const auto keyword = matchKeyword(offset, "if");
    if (!keyword.success) {
        const auto failure = ParseResult<Ptr<IfStmt>> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = { },
        };
        m_ifStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto openParen = matchSymbol(keyword.nextOffset, '(');
    if (!openParen.success) {
        recordFailure(skipTrivia(keyword.nextOffset),
            DiagnosticKind::expectedSymbol, "expected '(' after 'if'");
        const auto failure = ParseResult<Ptr<IfStmt>> {
            .success = false,
            .nextOffset = skipTrivia(keyword.nextOffset),
            .value = { },
        };
        m_ifStmtMemo.emplace(offset, failure);
        return failure;
    }

    Ptr<Exp> condExp_p;
    auto thenStmtOffset = openParen.nextOffset;
    const auto condExp = parseExp(openParen.nextOffset);
    if (!condExp.success) {
        bool keepInnerDiagnostic = false;
        for (const auto& diagnostic : m_bestDiagnostics) {
            if (diagnostic.m_offset != m_bestFailureOffset) {
                continue;
            }

            if (diagnostic.kind == DiagnosticKind::integerOutOfRange
                || diagnostic.kind == DiagnosticKind::malformedPrimaryExp) {
                keepInnerDiagnostic = true;
                break;
            }
        }
        if (!keepInnerDiagnostic) {
            const auto recoveryDiagnosticOffset
                = m_bestFailureOffset > skipTrivia(openParen.nextOffset)
                ? m_bestFailureOffset
                : skipTrivia(openParen.nextOffset);
            recordCommittedFailure(recoveryDiagnosticOffset,
                DiagnosticKind::malformedIfCond, "malformed if condition");
        }

        condExp_p = m_ast.alloc<Exp>(
            openParen.m_startOffset, Exp::Kind { Exp::Number { 0 } });
        thenStmtOffset = recoverToIfStmtHead(openParen.nextOffset);
    } else {
        condExp_p = condExp.value;
        thenStmtOffset = condExp.nextOffset;
    }

    const auto closeParen = matchSymbol(thenStmtOffset, ')');
    if (!closeParen.success) {
        recordCommittedFailure(skipTrivia(thenStmtOffset),
            DiagnosticKind::missingIfRParen, "missing ')' after if condition");
        thenStmtOffset = recoverToIfStmtHead(thenStmtOffset);
        const auto recoveredParen = matchSymbol(thenStmtOffset, ')');
        if (recoveredParen.success) {
            thenStmtOffset = recoveredParen.nextOffset;
        }
    } else {
        thenStmtOffset = closeParen.nextOffset;
    }

    const auto thenStmt = parseStmt(thenStmtOffset);
    if (!thenStmt.success) {
        recordCommittedFailure(skipTrivia(thenStmtOffset),
            DiagnosticKind::malformedIfThenStmt,
            "malformed then-branch statement");
        const auto failure = ParseResult<Ptr<IfStmt>> {
            .success = false,
            .nextOffset = recoverToStmtBoundary(thenStmtOffset),
            .value = { },
        };
        m_ifStmtMemo.emplace(offset, failure);
        return failure;
    }

    std::optional<Stmt> elseStmt;
    auto nextOffset = thenStmt.nextOffset;
    const auto elseKeyword = matchKeyword(nextOffset, "else");
    if (elseKeyword.success) {
        const auto elseStmt_ = parseStmt(elseKeyword.nextOffset);
        if (!elseStmt_.success) {
            recordCommittedFailure(skipTrivia(elseKeyword.nextOffset),
                DiagnosticKind::malformedElseStmt,
                "malformed else-branch statement");
            const auto failure = ParseResult<Ptr<IfStmt>> {
                .success = false,
                .nextOffset = recoverToStmtBoundary(elseKeyword.nextOffset),
                .value = { },
            };
            m_ifStmtMemo.emplace(offset, failure);
            return failure;
        }
        elseStmt = elseStmt_.value;
        nextOffset = elseStmt_.nextOffset;
    }
    if (!elseStmt.has_value())
        elseStmt = m_ast.alloc<Block>(keyword.m_startOffset);

    const auto result = ParseResult<Ptr<IfStmt>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = m_ast.alloc<IfStmt>(keyword.m_startOffset, condExp_p.ref(),
            thenStmt.value.value(), elseKeyword.success, elseStmt.value()),
    };
    m_ifStmtMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<WhileStmt>> Parser::parseWhileStmt(int32_t offset)
{
    if (const auto memoIt = m_whileStmtMemo.find(offset);
        memoIt != m_whileStmtMemo.end()) {
        return memoIt->second;
    }

    const auto keyword = matchKeyword(offset, "while");
    if (!keyword.success) {
        const auto failure = ParseResult<Ptr<WhileStmt>> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = { },
        };
        m_whileStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto openParen = matchSymbol(keyword.nextOffset, '(');
    if (!openParen.success) {
        recordFailure(skipTrivia(keyword.nextOffset),
            DiagnosticKind::expectedSymbol, "expected '(' after 'while'");
        const auto failure = ParseResult<Ptr<WhileStmt>> {
            .success = false,
            .nextOffset = skipTrivia(keyword.nextOffset),
            .value = { },
        };
        m_whileStmtMemo.emplace(offset, failure);
        return failure;
    }

    Ptr<Exp> condExp_p;
    auto bodyStmtOffset = openParen.nextOffset;
    const auto condExp = parseExp(openParen.nextOffset);
    if (!condExp.success) {
        bool keepInnerDiagnostic = false;
        for (const auto& diagnostic : m_bestDiagnostics) {
            if (diagnostic.m_offset != m_bestFailureOffset) {
                continue;
            }

            if (diagnostic.kind == DiagnosticKind::integerOutOfRange
                || diagnostic.kind == DiagnosticKind::malformedPrimaryExp) {
                keepInnerDiagnostic = true;
                break;
            }
        }
        if (!keepInnerDiagnostic) {
            const auto recoveryDiagnosticOffset
                = m_bestFailureOffset > skipTrivia(openParen.nextOffset)
                ? m_bestFailureOffset
                : skipTrivia(openParen.nextOffset);
            recordCommittedFailure(recoveryDiagnosticOffset,
                DiagnosticKind::malformedWhileCond,
                "malformed while condition");
        }

        condExp_p = m_ast.alloc<Exp>(
            openParen.m_startOffset, Exp::Kind { Exp::Number { 0 } });
        bodyStmtOffset = recoverToWhileStmtHead(openParen.nextOffset);
    } else {
        condExp_p = condExp.value;
        bodyStmtOffset = condExp.nextOffset;
    }

    const auto closeParen = matchSymbol(bodyStmtOffset, ')');
    if (!closeParen.success) {
        recordCommittedFailure(skipTrivia(bodyStmtOffset),
            DiagnosticKind::missingWhileRParen,
            "missing ')' after while condition");
        bodyStmtOffset = recoverToWhileStmtHead(bodyStmtOffset);
        const auto recoveredParen = matchSymbol(bodyStmtOffset, ')');
        if (recoveredParen.success) {
            bodyStmtOffset = recoveredParen.nextOffset;
        }
    } else {
        bodyStmtOffset = closeParen.nextOffset;
    }

    const auto bodyStmt = parseStmt(bodyStmtOffset);
    if (!bodyStmt.success) {
        recordCommittedFailure(skipTrivia(bodyStmtOffset),
            DiagnosticKind::malformedWhileBody,
            "malformed while body statement");
        const auto failure = ParseResult<Ptr<WhileStmt>> {
            .success = false,
            .nextOffset = recoverToStmtBoundary(bodyStmtOffset),
            .value = { },
        };
        m_whileStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<WhileStmt>> {
        .success = true,
        .nextOffset = bodyStmt.nextOffset,
        .value = m_ast.alloc<WhileStmt>(
            keyword.m_startOffset, condExp_p.ref(), bodyStmt.value.value()),
    };
    m_whileStmtMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<BreakStmt>> Parser::parseBreakStmt(int32_t offset)
{
    if (const auto memoIt = m_breakStmtMemo.find(offset);
        memoIt != m_breakStmtMemo.end()) {
        return memoIt->second;
    }

    const auto keyword = matchKeyword(offset, "break");
    if (!keyword.success) {
        const auto failure = ParseResult<Ptr<BreakStmt>> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = { },
        };
        m_breakStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto semicolon = matchSymbol(keyword.nextOffset, ';');
    if (!semicolon.success) {
        recordCommittedFailure(skipTrivia(keyword.nextOffset),
            DiagnosticKind::missingBreakSemicolon,
            "missing ';' after break statement");
        auto recoveredOffset = recoverToStmtBoundary(keyword.nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.success) {
            recoveredOffset = recoveredSemicolon.nextOffset;
        }

        const auto recoveredResult = ParseResult<Ptr<BreakStmt>> {
            .success = true,
            .nextOffset = recoveredOffset,
            .value = m_ast.alloc<BreakStmt>(keyword.m_startOffset),
        };
        m_breakStmtMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto result = ParseResult<Ptr<BreakStmt>> {
        .success = true,
        .nextOffset = semicolon.nextOffset,
        .value = m_ast.alloc<BreakStmt>(keyword.m_startOffset),
    };
    m_breakStmtMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<ContinueStmt>> Parser::parseContinueStmt(int32_t offset)
{
    if (const auto memoIt = m_continueStmtMemo.find(offset);
        memoIt != m_continueStmtMemo.end()) {
        return memoIt->second;
    }

    const auto keyword = matchKeyword(offset, "continue");
    if (!keyword.success) {
        const auto failure = ParseResult<Ptr<ContinueStmt>> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = { },
        };
        m_continueStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto semicolon = matchSymbol(keyword.nextOffset, ';');
    if (!semicolon.success) {
        recordCommittedFailure(skipTrivia(keyword.nextOffset),
            DiagnosticKind::missingContinueSemicolon,
            "missing ';' after continue statement");
        auto recoveredOffset = recoverToStmtBoundary(keyword.nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.success) {
            recoveredOffset = recoveredSemicolon.nextOffset;
        }

        const auto recoveredResult = ParseResult<Ptr<ContinueStmt>> {
            .success = true,
            .nextOffset = recoveredOffset,
            .value = m_ast.alloc<ContinueStmt>(keyword.m_startOffset),
        };
        m_continueStmtMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto result = ParseResult<Ptr<ContinueStmt>> {
        .success = true,
        .nextOffset = semicolon.nextOffset,
        .value = m_ast.alloc<ContinueStmt>(keyword.m_startOffset),
    };
    m_continueStmtMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<AssignStmt>> Parser::parseAssignStmt(int32_t offset)
{
    if (const auto memoIt = m_assignStmtMemo.find(offset);
        memoIt != m_assignStmtMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto lVal = parseLVal(normalizedOffset);
    if (!lVal.success) {
        const auto failure = ParseResult<Ptr<AssignStmt>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_assignStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto assign = matchSymbol(lVal.nextOffset, '=');
    if (!assign.success) {
        const auto failure = ParseResult<Ptr<AssignStmt>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_assignStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto exp = parseExp(assign.nextOffset);
    if (!exp.success) {
        bool keepInnerDiagnostic = false;
        for (const auto& diagnostic : m_bestDiagnostics) {
            if (diagnostic.m_offset != m_bestFailureOffset) {
                continue;
            }

            if (diagnostic.kind == DiagnosticKind::integerOutOfRange
                || diagnostic.kind == DiagnosticKind::malformedPrimaryExp) {
                keepInnerDiagnostic = true;
                break;
            }
        }
        if (!keepInnerDiagnostic) {
            const auto recoveryDiagnosticOffset
                = m_bestFailureOffset > skipTrivia(assign.nextOffset)
                ? m_bestFailureOffset
                : skipTrivia(assign.nextOffset);
            recordCommittedFailure(recoveryDiagnosticOffset,
                DiagnosticKind::malformedAssignValue,
                "malformed assignment value");
        }

        const auto failure = ParseResult<Ptr<AssignStmt>> {
            .success = false,
            .nextOffset = recoverToStmtBoundary(assign.nextOffset),
            .value = { },
        };
        m_assignStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto semicolon = matchSymbol(exp.nextOffset, ';');
    if (!semicolon.success) {
        recordCommittedFailure(skipTrivia(exp.nextOffset),
            DiagnosticKind::missingAssignSemicolon,
            "missing ';' after assignment statement");
        auto recoveredOffset = recoverToStmtBoundary(exp.nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.success) {
            recoveredOffset = recoveredSemicolon.nextOffset;
        }

        const auto recoveredResult = ParseResult<Ptr<AssignStmt>> {
            .success = true,
            .nextOffset = recoveredOffset,
            .value
            = m_ast.alloc<AssignStmt>(lVal.value(m_ast).sourcePos.m_offset,
                lVal.value.ref(), exp.value.ref()),
        };
        m_assignStmtMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto result = ParseResult<Ptr<AssignStmt>> {
        .success = true,
        .nextOffset = semicolon.nextOffset,
        .value = m_ast.alloc<AssignStmt>(lVal.value(m_ast).sourcePos.m_offset,
            lVal.value.ref(), exp.value.ref()),
    };
    m_assignStmtMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<ExpStmt>> Parser::parseExpStmt(int32_t offset)
{
    if (const auto memoIt = m_expStmtMemo.find(offset);
        memoIt != m_expStmtMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto semicolonOnly = matchSymbol(normalizedOffset, ';');
    if (semicolonOnly.success) {
        const auto result = ParseResult<Ptr<ExpStmt>> { .success = true,
            .nextOffset = semicolonOnly.nextOffset,
            .value = m_ast.alloc<ExpStmt>(semicolonOnly.m_startOffset) };
        m_expStmtMemo.emplace(offset, result);
        return result;
    }

    const auto exp = parseExp(normalizedOffset);
    if (!exp.success) {
        const auto failure = ParseResult<Ptr<ExpStmt>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_expStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto semicolon = matchSymbol(exp.nextOffset, ';');
    if (!semicolon.success) {
        recordCommittedFailure(skipTrivia(exp.nextOffset),
            DiagnosticKind::missingSemicolon,
            "missing ';' after expression statement");
        auto recoveredOffset = recoverToStmtBoundary(exp.nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.success) {
            recoveredOffset = recoveredSemicolon.nextOffset;
        }

        const auto recoveredResult = ParseResult<Ptr<ExpStmt>> {
            .success = true,
            .nextOffset = recoveredOffset,
            .value = m_ast.alloc<ExpStmt>(normalizedOffset, exp.value),
        };
        m_expStmtMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto result = ParseResult<Ptr<ExpStmt>> {
        .success = true,
        .nextOffset = semicolon.nextOffset,
        .value = m_ast.alloc<ExpStmt>(normalizedOffset, exp.value),
    };
    m_expStmtMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<ReturnStmt>> Parser::parseReturnStmt(int32_t offset)
{
    if (const auto memoIt = m_returnStmtMemo.find(offset);
        memoIt != m_returnStmtMemo.end()) {
        return memoIt->second;
    }

    const auto keyword = matchKeyword(offset, "return");
    if (!keyword.success) {
        const auto failure = ParseResult<Ptr<ReturnStmt>> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = { },
        };
        m_returnStmtMemo.emplace(offset, failure);
        return failure;
    }

    const auto immediateSemicolon = matchSymbol(keyword.nextOffset, ';');
    if (immediateSemicolon.success) {
        const auto result = ParseResult<Ptr<ReturnStmt>> {
            .success = true,
            .nextOffset = immediateSemicolon.nextOffset,
            .value = m_ast.alloc<ReturnStmt>(keyword.m_startOffset),
        };
        m_returnStmtMemo.emplace(offset, result);
        return result;
    }

    const auto exp = parseExp(keyword.nextOffset);
    if (!exp.success) {
        bool keepInnerDiagnostic = false;
        for (const auto& diagnostic : m_bestDiagnostics) {
            if (diagnostic.m_offset != m_bestFailureOffset) {
                continue;
            }

            if (diagnostic.kind == DiagnosticKind::integerOutOfRange
                || diagnostic.kind == DiagnosticKind::malformedPrimaryExp) {
                keepInnerDiagnostic = true;
                break;
            }
        }
        if (!keepInnerDiagnostic) {
            const auto recoveryDiagnosticOffset
                = m_bestFailureOffset > skipTrivia(keyword.nextOffset)
                ? m_bestFailureOffset
                : skipTrivia(keyword.nextOffset);
            recordCommittedFailure(recoveryDiagnosticOffset,
                DiagnosticKind::malformedReturnValue, "malformed return value");
        }

        auto recoveredOffset = recoverToStmtBoundary(keyword.nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.success) {
            recoveredOffset = recoveredSemicolon.nextOffset;
        }

        const auto recoveredResult
            = ParseResult<Ptr<ReturnStmt>> { .success = true,
                  .nextOffset = recoveredOffset,
                  .value = m_ast.alloc<ReturnStmt>(keyword.m_startOffset) };
        m_returnStmtMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto semicolon = matchSymbol(exp.nextOffset, ';');
    if (!semicolon.success) {
        recordCommittedFailure(skipTrivia(exp.nextOffset),
            DiagnosticKind::missingSemicolon,
            "missing ';' after return statement");
        auto recoveredOffset = recoverToStmtBoundary(exp.nextOffset);
        const auto recoveredSemicolon = matchSymbol(recoveredOffset, ';');
        if (recoveredSemicolon.success) {
            recoveredOffset = recoveredSemicolon.nextOffset;
        }

        const auto recoveredResult = ParseResult<Ptr<ReturnStmt>> {
            .success = true,
            .nextOffset = recoveredOffset,
            .value = m_ast.alloc<ReturnStmt>(keyword.m_startOffset, exp.value),
        };
        m_returnStmtMemo.emplace(offset, recoveredResult);
        return recoveredResult;
    }

    const auto result = ParseResult<Ptr<ReturnStmt>> {
        .success = true,
        .nextOffset = semicolon.nextOffset,
        .value = m_ast.alloc<ReturnStmt>(keyword.m_startOffset, exp.value),
    };
    m_returnStmtMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parseExp(int32_t offset)
{
    if (const auto memoIt = m_expMemo.find(offset); memoIt != m_expMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto lOrExp = parseLOrExp(normalizedOffset);
    if (!lOrExp.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = lOrExp.nextOffset,
            .value = { },
        };
        m_expMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = lOrExp.nextOffset,
        .value = lOrExp.value,
    };
    m_expMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parseLOrExp(int32_t offset)
{
    if (const auto memoIt = m_lOrExpMemo.find(offset);
        memoIt != m_lOrExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseLAndExp(normalizedOffset);
    if (!head.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = head.nextOffset,
            .value = { },
        };
        m_lOrExpMemo.emplace(offset, failure);
        return failure;
    }

    auto current = head.value;
    auto nextOffset = head.nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = matchSymbol(nextOffset, "||");
        if (!op.success) {
            break;
        }

        const auto rhs = parseLAndExp(op.nextOffset);
        if (!rhs.success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        current = makeBinaryRoot(m_ast, normalizedOffset, BinaryOpKeyword::orOr,
            current.ref(), rhs.value.ref());
        nextOffset = rhs.nextOffset;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = current,
    };
    m_lOrExpMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parseLAndExp(int32_t offset)
{
    if (const auto memoIt = m_lAndExpMemo.find(offset);
        memoIt != m_lAndExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseEqExp(normalizedOffset);
    if (!head.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = head.nextOffset,
            .value = { },
        };
        m_lAndExpMemo.emplace(offset, failure);
        return failure;
    }

    auto current = head.value;
    auto nextOffset = head.nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = matchSymbol(nextOffset, "&&");
        if (!op.success) {
            break;
        }

        const auto rhs = parseEqExp(op.nextOffset);
        if (!rhs.success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        current = makeBinaryRoot(m_ast, normalizedOffset,
            BinaryOpKeyword::andAnd, current.ref(), rhs.value.ref());
        nextOffset = rhs.nextOffset;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = current,
    };
    m_lAndExpMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parseEqExp(int32_t offset)
{
    if (const auto memoIt = m_eqExpMemo.find(offset);
        memoIt != m_eqExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseRelExp(normalizedOffset);
    if (!head.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = head.nextOffset,
            .value = { },
        };
        m_eqExpMemo.emplace(offset, failure);
        return failure;
    }

    auto current = head.value;
    auto nextOffset = head.nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = parseEqOp(nextOffset);
        if (!op.success) {
            break;
        }

        const auto rhs = parseRelExp(op.nextOffset);
        if (!rhs.success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        current = makeBinaryRoot(
            m_ast, normalizedOffset, op.value, current.ref(), rhs.value.ref());
        nextOffset = rhs.nextOffset;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = current,
    };
    m_eqExpMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parseRelExp(int32_t offset)
{
    if (const auto memoIt = m_relExpMemo.find(offset);
        memoIt != m_relExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseAddExp(normalizedOffset);
    if (!head.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = head.nextOffset,
            .value = { },
        };
        m_relExpMemo.emplace(offset, failure);
        return failure;
    }

    auto current = head.value;
    auto nextOffset = head.nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = parseRelOp(nextOffset);
        if (!op.success) {
            break;
        }

        const auto rhs = parseAddExp(op.nextOffset);
        if (!rhs.success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        current = makeBinaryRoot(
            m_ast, normalizedOffset, op.value, current.ref(), rhs.value.ref());
        nextOffset = rhs.nextOffset;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = current,
    };
    m_relExpMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parseAddExp(int32_t offset)
{
    if (const auto memoIt = m_addExpMemo.find(offset);
        memoIt != m_addExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseMulExp(normalizedOffset);
    if (!head.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = head.nextOffset,
            .value = { },
        };
        m_addExpMemo.emplace(offset, failure);
        return failure;
    }

    auto current = head.value;
    auto nextOffset = head.nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = parseAddOp(nextOffset);
        if (!op.success) {
            break;
        }

        const auto rhs = parseMulExp(op.nextOffset);
        if (!rhs.success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        current = makeBinaryRoot(
            m_ast, normalizedOffset, op.value, current.ref(), rhs.value.ref());
        nextOffset = rhs.nextOffset;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = current,
    };
    m_addExpMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parseMulExp(int32_t offset)
{
    if (const auto memoIt = m_mulExpMemo.find(offset);
        memoIt != m_mulExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto head = parseUnaryExp(normalizedOffset);
    if (!head.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = head.nextOffset,
            .value = { },
        };
        m_mulExpMemo.emplace(offset, failure);
        return failure;
    }

    auto current = head.value;
    auto nextOffset = head.nextOffset;
    while (true) {
        const auto savedFailureOffset = m_bestFailureOffset;
        const auto savedDiagnostics = m_bestDiagnostics;
        const auto op = parseMulOp(nextOffset);
        if (!op.success) {
            break;
        }

        const auto rhs = parseUnaryExp(op.nextOffset);
        if (!rhs.success) {
            m_bestFailureOffset = savedFailureOffset;
            m_bestDiagnostics = savedDiagnostics;
            break;
        }

        current = makeBinaryRoot(
            m_ast, normalizedOffset, op.value, current.ref(), rhs.value.ref());
        nextOffset = rhs.nextOffset;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = current,
    };
    m_mulExpMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parsePrimaryExp(int32_t offset)
{
    if (const auto memoIt = m_primaryExpMemo.find(offset);
        memoIt != m_primaryExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto openParen = matchSymbol(normalizedOffset, '(');
    if (openParen.success) {
        const auto exp = parseExp(openParen.nextOffset);
        if (!exp.success) {
            bool keepInnerDiagnostic = false;
            for (const auto& diagnostic : m_bestDiagnostics) {
                if (diagnostic.m_offset != m_bestFailureOffset) {
                    continue;
                }

                if (diagnostic.kind == DiagnosticKind::integerOutOfRange
                    || diagnostic.kind == DiagnosticKind::malformedPrimaryExp) {
                    keepInnerDiagnostic = true;
                    break;
                }
            }

            if (!keepInnerDiagnostic) {
                const auto recoveryDiagnosticOffset
                    = m_bestFailureOffset > skipTrivia(openParen.nextOffset)
                    ? m_bestFailureOffset
                    : skipTrivia(openParen.nextOffset);
                recordCommittedFailure(recoveryDiagnosticOffset,
                    DiagnosticKind::malformedPrimaryExp,
                    "malformed parenthesized expression");
            }

            const auto failure = ParseResult<Ptr<Exp>> {
                .success = false,
                .nextOffset = recoverToExprRParen(openParen.nextOffset),
                .value = { },
            };
            m_primaryExpMemo.emplace(offset, failure);
            return failure;
        }

        const auto closeParen = matchSymbol(exp.nextOffset, ')');
        if (!closeParen.success) {
            recordCommittedFailure(skipTrivia(exp.nextOffset),
                DiagnosticKind::missingPrimaryRParen,
                "missing ')' after parenthesized expression");
            auto recoveredOffset = recoverToExprRParen(exp.nextOffset);
            const auto recoveredParen = matchSymbol(recoveredOffset, ')');
            if (recoveredParen.success) {
                recoveredOffset = recoveredParen.nextOffset;
            }

            const auto recoveredResult = ParseResult<Ptr<Exp>> {
                .success = true,
                .nextOffset = recoveredOffset,
                .value = exp.value,
            };
            m_primaryExpMemo.emplace(offset, recoveredResult);
            return recoveredResult;
        }

        const auto result = ParseResult<Ptr<Exp>> {
            .success = true,
            .nextOffset = closeParen.nextOffset,
            .value = exp.value,
        };
        m_primaryExpMemo.emplace(offset, result);
        return result;
    }

    const auto lVal = parseLVal(normalizedOffset);
    if (lVal.success) {
        const auto result = ParseResult<Ptr<Exp>> {
            .success = true,
            .nextOffset = lVal.nextOffset,
            .value = lVal.value,
        };
        m_primaryExpMemo.emplace(offset, result);
        return result;
    }

    if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] < '0'
        || m_source[normalizedOffset] > '9') {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_primaryExpMemo.emplace(offset, failure);
        return failure;
    }

    const auto number = parseNumber(normalizedOffset);
    if (!number.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = number.nextOffset,
            .value = { },
        };
        m_primaryExpMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = number.nextOffset,
        .value = number.value,
    };
    m_primaryExpMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parseUnaryExp(int32_t offset)
{
    if (const auto memoIt = m_unaryExpMemo.find(offset);
        memoIt != m_unaryExpMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    if (!isAtEnd(normalizedOffset)
        && isIdentifierStart(m_source[normalizedOffset])) {
        const auto identifier = parseIdent(normalizedOffset);
        if (identifier.success) {
            const auto openParen = matchSymbol(identifier.nextOffset, '(');
            if (openParen.success) {
                std::vector<Ref<Exp>> params;
                auto nextOffset = openParen.nextOffset;
                const auto directCloseParen = matchSymbol(nextOffset, ')');
                if (!directCloseParen.success) {
                    while (true) {
                        const auto arg = parseExp(nextOffset);
                        if (!arg.success) {
                            const auto failure = ParseResult<Ptr<Exp>> {
                                .success = false,
                                .nextOffset = arg.nextOffset,
                                .value = { },
                            };
                            m_unaryExpMemo.emplace(offset, failure);
                            return failure;
                        }

                        params.push_back(arg.value.ref());
                        nextOffset = arg.nextOffset;
                        const auto comma = matchSymbol(nextOffset, ',');
                        if (!comma.success) {
                            break;
                        }
                        nextOffset = comma.nextOffset;
                    }
                }

                const auto closeParen = matchSymbol(nextOffset, ')');
                if (!closeParen.success) {
                    recordCommittedFailure(skipTrivia(nextOffset),
                        DiagnosticKind::missingPrimaryRParen,
                        "missing ')' after function call");
                    auto recoveredOffset = recoverToExprRParen(nextOffset);
                    const auto recoveredParen
                        = matchSymbol(recoveredOffset, ')');
                    if (recoveredParen.success) {
                        recoveredOffset = recoveredParen.nextOffset;
                    }

                    const auto recoveredResult = ParseResult<Ptr<Exp>> {
                        .success = true,
                        .nextOffset = recoveredOffset,
                        .value = m_ast.alloc<Exp>(normalizedOffset,
                            Exp::Kind { Exp::Call {
                                .funcName = identifier.value.ref(),
                                .params = std::move(params),
                            } }),
                    };
                    m_unaryExpMemo.emplace(offset, recoveredResult);
                    return recoveredResult;
                }

                const auto result = ParseResult<Ptr<Exp>> {
                    .success = true,
                    .nextOffset = closeParen.nextOffset,
                    .value = m_ast.alloc<Exp>(normalizedOffset,
                        Exp::Kind { Exp::Call {
                            .funcName = identifier.value.ref(),
                            .params = std::move(params),
                        } }),
                };
                m_unaryExpMemo.emplace(offset, result);
                return result;
            }
        }
    }

    const auto primaryExp = parsePrimaryExp(normalizedOffset);
    if (primaryExp.success) {
        const auto result = ParseResult<Ptr<Exp>> {
            .success = true,
            .nextOffset = primaryExp.nextOffset,
            .value = primaryExp.value,
        };
        m_unaryExpMemo.emplace(offset, result);
        return result;
    }

    const auto unaryOp = parseUnaryOp(normalizedOffset);
    if (!unaryOp.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = primaryExp.nextOffset,
            .value = { },
        };
        m_unaryExpMemo.emplace(offset, failure);
        return failure;
    }

    const auto nestedUnaryExp = parseUnaryExp(unaryOp.nextOffset);
    if (!nestedUnaryExp.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = nestedUnaryExp.nextOffset,
            .value = { },
        };
        m_unaryExpMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = nestedUnaryExp.nextOffset,
        .value = makeUnaryExp(
            m_ast, normalizedOffset, unaryOp.value, nestedUnaryExp.value.ref()),
    };
    m_unaryExpMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Exp>> Parser::parseLVal(int32_t offset)
{
    if (const auto memoIt = m_lValMemo.find(offset);
        memoIt != m_lValMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    if (isAtEnd(normalizedOffset)
        || !isIdentifierStart(m_source[normalizedOffset])) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_lValMemo.emplace(offset, failure);
        return failure;
    }

    const auto identifier = parseIdent(normalizedOffset);
    if (!identifier.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = identifier.nextOffset,
            .value = { },
        };
        m_lValMemo.emplace(offset, failure);
        return failure;
    }

    const auto indices = parseLValIndices(identifier.nextOffset);

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = indices.nextOffset,
        .value = m_ast.alloc<Exp>(identifier.value(m_ast).sourcePos.m_offset,
            Exp::Kind { Exp::LVal { identifier.value.ref(), indices.value } }),
    };
    m_lValMemo.emplace(offset, result);
    return result;
}

ParseResult<std::vector<Ref<Exp>>> Parser::parseLValIndices(int32_t offset)
{
    if (const auto memoIt = m_lValIndicesMemo.find(offset);
        memoIt != m_lValIndicesMemo.end()) {
        return memoIt->second;
    }

    auto nextOffset = skipTrivia(offset);
    std::vector<Ref<Exp>> indices;
    while (true) {
        const auto openBracket = matchSymbol(nextOffset, '[');
        if (!openBracket.success) {
            break;
        }

        const auto indexExp = parseExp(openBracket.nextOffset);
        if (!indexExp.success) {
            auto recoveryOffset = recoverToRBracket(openBracket.nextOffset);
            recordCommittedFailure(skipTrivia(openBracket.nextOffset),
                DiagnosticKind::malformedSubscript,
                "malformed array subscript");
            const auto recoveredBracket = matchSymbol(recoveryOffset, ']');
            nextOffset = recoveredBracket.success ? recoveredBracket.nextOffset
                                                  : recoveryOffset;
            continue;
        }

        indices.push_back(indexExp.value.ref());
        const auto closeBracket = matchSymbol(indexExp.nextOffset, ']');
        if (!closeBracket.success) {
            auto recoveryOffset = recoverToRBracket(indexExp.nextOffset);
            recordCommittedFailure(skipTrivia(indexExp.nextOffset),
                DiagnosticKind::missingSubscriptRBracket,
                "missing ']' after array subscript");
            const auto recoveredBracket = matchSymbol(recoveryOffset, ']');
            nextOffset = recoveredBracket.success ? recoveredBracket.nextOffset
                                                  : recoveryOffset;
            continue;
        }

        nextOffset = closeBracket.nextOffset;
    }

    const auto result = ParseResult<std::vector<Ref<Exp>>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = std::move(indices),
    };
    m_lValIndicesMemo.emplace(offset, result);
    return result;
}

ParseResult<Parser::ParamArraySuffixParse> Parser::parseParamArraySuffix(
    int32_t offset)
{
    if (const auto memoIt = m_paramArraySuffixMemo.find(offset);
        memoIt != m_paramArraySuffixMemo.end()) {
        return memoIt->second;
    }

    const auto openBracket = matchSymbol(offset, '[');
    if (!openBracket.success) {
        const auto failure = ParseResult<ParamArraySuffixParse> {
            .success = false,
            .nextOffset = skipTrivia(offset),
            .value = { },
        };
        m_paramArraySuffixMemo.emplace(offset, failure);
        return failure;
    }

    auto nextOffset = openBracket.nextOffset;
    const auto closeBracket = matchSymbol(nextOffset, ']');
    if (!closeBracket.success) {
        auto recoveryOffset = recoverToRBracket(nextOffset);
        recordCommittedFailure(skipTrivia(nextOffset),
            DiagnosticKind::missingParamArrayRBracket,
            "missing ']' in array parameter declarator");
        const auto recoveredBracket = matchSymbol(recoveryOffset, ']');
        nextOffset = recoveredBracket.success ? recoveredBracket.nextOffset
                                              : recoveryOffset;
    } else {
        nextOffset = closeBracket.nextOffset;
    }

    const auto trailingDimensions = parseArrayConstDims(nextOffset);
    const auto result = ParseResult<ParamArraySuffixParse> {
        .success = true,
        .nextOffset = trailingDimensions.nextOffset,
        .value = ParamArraySuffixParse {
            .m_isArray = true,
            .m_trailingDimensions = trailingDimensions.value,
        },
    };
    m_paramArraySuffixMemo.emplace(offset, result);
    return result;
}

ParseResult<UnaryOpKeyword> Parser::parseUnaryOp(int32_t offset)
{
    if (const auto memoIt = m_unaryOpMemo.find(offset);
        memoIt != m_unaryOpMemo.end()) {
        return memoIt->second;
    }

    const auto plus = matchSymbol(offset, '+');
    if (plus.success) {
        const auto result = ParseResult<UnaryOpKeyword> {
            .success = true,
            .nextOffset = plus.nextOffset,
            .value = UnaryOpKeyword::plus,
        };
        m_unaryOpMemo.emplace(offset, result);
        return result;
    }

    const auto minus = matchSymbol(offset, '-');
    if (minus.success) {
        const auto result = ParseResult<UnaryOpKeyword> {
            .success = true,
            .nextOffset = minus.nextOffset,
            .value = UnaryOpKeyword::minus,
        };
        m_unaryOpMemo.emplace(offset, result);
        return result;
    }

    const auto bang = matchSymbol(offset, '!');
    if (bang.success) {
        const auto result = ParseResult<UnaryOpKeyword> {
            .success = true,
            .nextOffset = bang.nextOffset,
            .value = UnaryOpKeyword::bang,
        };
        m_unaryOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<UnaryOpKeyword> {
        .success = false,
        .nextOffset = skipTrivia(offset),
        .value = UnaryOpKeyword::plus,
    };
    m_unaryOpMemo.emplace(offset, failure);
    return failure;
}

ParseResult<BinaryOpKeyword> Parser::parseMulOp(int32_t offset)
{
    if (const auto memoIt = m_mulOpMemo.find(offset);
        memoIt != m_mulOpMemo.end()) {
        return memoIt->second;
    }

    const auto star = matchSymbol(offset, '*');
    if (star.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = star.nextOffset,
            .value = BinaryOpKeyword::star,
        };
        m_mulOpMemo.emplace(offset, result);
        return result;
    }

    const auto slash = matchSymbol(offset, '/');
    if (slash.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = slash.nextOffset,
            .value = BinaryOpKeyword::slash,
        };
        m_mulOpMemo.emplace(offset, result);
        return result;
    }

    const auto percent = matchSymbol(offset, '%');
    if (percent.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = percent.nextOffset,
            .value = BinaryOpKeyword::percent,
        };
        m_mulOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<BinaryOpKeyword> {
        .success = false,
        .nextOffset = skipTrivia(offset),
        .value = BinaryOpKeyword::star,
    };
    m_mulOpMemo.emplace(offset, failure);
    return failure;
}

ParseResult<BinaryOpKeyword> Parser::parseAddOp(int32_t offset)
{
    if (const auto memoIt = m_addOpMemo.find(offset);
        memoIt != m_addOpMemo.end()) {
        return memoIt->second;
    }

    const auto plus = matchSymbol(offset, '+');
    if (plus.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = plus.nextOffset,
            .value = BinaryOpKeyword::plus,
        };
        m_addOpMemo.emplace(offset, result);
        return result;
    }

    const auto minus = matchSymbol(offset, '-');
    if (minus.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = minus.nextOffset,
            .value = BinaryOpKeyword::minus,
        };
        m_addOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<BinaryOpKeyword> {
        .success = false,
        .nextOffset = skipTrivia(offset),
        .value = BinaryOpKeyword::plus,
    };
    m_addOpMemo.emplace(offset, failure);
    return failure;
}

ParseResult<BinaryOpKeyword> Parser::parseRelOp(int32_t offset)
{
    if (const auto memoIt = m_relOpMemo.find(offset);
        memoIt != m_relOpMemo.end()) {
        return memoIt->second;
    }

    const auto lessEqual = matchSymbol(offset, "<=");
    if (lessEqual.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = lessEqual.nextOffset,
            .value = BinaryOpKeyword::lessEqual,
        };
        m_relOpMemo.emplace(offset, result);
        return result;
    }

    const auto greaterEqual = matchSymbol(offset, ">=");
    if (greaterEqual.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = greaterEqual.nextOffset,
            .value = BinaryOpKeyword::greaterEqual,
        };
        m_relOpMemo.emplace(offset, result);
        return result;
    }

    const auto less = matchSymbol(offset, '<');
    if (less.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = less.nextOffset,
            .value = BinaryOpKeyword::less,
        };
        m_relOpMemo.emplace(offset, result);
        return result;
    }

    const auto greater = matchSymbol(offset, '>');
    if (greater.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = greater.nextOffset,
            .value = BinaryOpKeyword::greater,
        };
        m_relOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<BinaryOpKeyword> {
        .success = false,
        .nextOffset = skipTrivia(offset),
        .value = BinaryOpKeyword::less,
    };
    m_relOpMemo.emplace(offset, failure);
    return failure;
}

ParseResult<BinaryOpKeyword> Parser::parseEqOp(int32_t offset)
{
    if (const auto memoIt = m_eqOpMemo.find(offset);
        memoIt != m_eqOpMemo.end()) {
        return memoIt->second;
    }

    const auto equal = matchSymbol(offset, "==");
    if (equal.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = equal.nextOffset,
            .value = BinaryOpKeyword::equal,
        };
        m_eqOpMemo.emplace(offset, result);
        return result;
    }

    const auto notEqual = matchSymbol(offset, "!=");
    if (notEqual.success) {
        const auto result = ParseResult<BinaryOpKeyword> {
            .success = true,
            .nextOffset = notEqual.nextOffset,
            .value = BinaryOpKeyword::notEqual,
        };
        m_eqOpMemo.emplace(offset, result);
        return result;
    }

    const auto failure = ParseResult<BinaryOpKeyword> {
        .success = false,
        .nextOffset = skipTrivia(offset),
        .value = BinaryOpKeyword::equal,
    };
    m_eqOpMemo.emplace(offset, failure);
    return failure;
}

ParseResult<Ptr<Exp>> Parser::parseNumber(int32_t offset)
{
    if (const auto memoIt = m_numberMemo.find(offset);
        memoIt != m_numberMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    const auto intConst = parseIntConst(normalizedOffset);
    if (!intConst.success) {
        const auto failure = ParseResult<Ptr<Exp>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_numberMemo.emplace(offset, failure);
        return failure;
    }

    const auto result = ParseResult<Ptr<Exp>> {
        .success = true,
        .nextOffset = intConst.nextOffset,
        .value = m_ast.alloc<Exp>(
            normalizedOffset, Exp::Kind { Exp::Number { intConst.value } }),
    };
    m_numberMemo.emplace(offset, result);
    return result;
}

ParseResult<Ptr<Identifier>> Parser::parseIdent(int32_t offset)
{
    if (const auto memoIt = m_identMemo.find(offset);
        memoIt != m_identMemo.end()) {
        return memoIt->second;
    }

    const auto normalizedOffset = skipTrivia(offset);
    if (isAtEnd(normalizedOffset)
        || !isIdentifierStart(m_source[normalizedOffset])) {
        recordFailure(normalizedOffset, DiagnosticKind::expectedIdentifier,
            "expected identifier");
        const auto failure = ParseResult<Ptr<Identifier>> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = { },
        };
        m_identMemo.emplace(offset, failure);
        return failure;
    }

    auto nextOffset = normalizedOffset + 1;
    while (!isAtEnd(nextOffset) && isIdentifierContinue(m_source[nextOffset])) {
        ++nextOffset;
    }

    const auto result = ParseResult<Ptr<Identifier>> {
        .success = true,
        .nextOffset = nextOffset,
        .value = m_ast.alloc<Identifier>(normalizedOffset,
            m_source.substr(normalizedOffset, nextOffset - normalizedOffset)),
    };
    m_identMemo.emplace(offset, result);
    return result;
}

ParseResult<int32_t> Parser::parseIntConst(int32_t offset)
{
    const auto normalizedOffset = skipTrivia(offset);

    if (normalizedOffset + 1 < static_cast<int32_t>(m_source.size())
        && m_source[normalizedOffset] == '0'
        && (m_source[normalizedOffset + 1] == 'x'
            || m_source[normalizedOffset + 1] == 'X')) {
        return parseHexadecimalConst(normalizedOffset);
    }

    const auto octal = parseOctalConst(normalizedOffset);
    if (octal.success) {
        return octal;
    }

    const auto decimal = parseDecimalConst(normalizedOffset);
    if (decimal.success) {
        return decimal;
    }

    recordFailure(normalizedOffset, DiagnosticKind::expectedInteger,
        "expected integer constant");
    return ParseResult<int32_t> {
        .success = false,
        .nextOffset = normalizedOffset,
        .value = 0,
    };
}

ParseResult<int32_t> Parser::parseHexadecimalConst(int32_t offset)
{
    const auto normalizedOffset = skipTrivia(offset);
    if (normalizedOffset + 1 >= static_cast<int32_t>(m_source.size())
        || m_source[normalizedOffset] != '0'
        || (m_source[normalizedOffset + 1] != 'x'
            && m_source[normalizedOffset + 1] != 'X')) {
        return ParseResult<int32_t> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = 0,
        };
    }

    return parseBaseInteger(normalizedOffset, 16, 2, isHexadecimalDigit);
}

ParseResult<int32_t> Parser::parseOctalConst(int32_t offset)
{
    const auto normalizedOffset = skipTrivia(offset);
    if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] != '0') {
        return ParseResult<int32_t> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = 0,
        };
    }

    return parseBaseInteger(normalizedOffset, 8, 0, isOctalDigit);
}

ParseResult<int32_t> Parser::parseDecimalConst(int32_t offset)
{
    const auto normalizedOffset = skipTrivia(offset);
    if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] < '1'
        || m_source[normalizedOffset] > '9') {
        return ParseResult<int32_t> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = 0,
        };
    }

    return parseBaseInteger(normalizedOffset, 10, 0, isDecimalDigit);
}

int32_t Parser::skipTrivia(int32_t offset) const
{
    auto nextOffset = offset;
    while (!isAtEnd(nextOffset)) {
        if (std::isspace(static_cast<unsigned char>(m_source[nextOffset]))
            != 0) {
            ++nextOffset;
            continue;
        }

        if (nextOffset + 1 >= static_cast<int32_t>(m_source.size())
            || m_source[nextOffset] != '/') {
            break;
        }

        if (m_source[nextOffset + 1] == '/') {
            nextOffset += 2;
            while (!isAtEnd(nextOffset) && m_source[nextOffset] != '\n'
                && m_source[nextOffset] != '\r') {
                ++nextOffset;
            }
            continue;
        }

        if (m_source[nextOffset + 1] == '*') {
            auto commentOffset = nextOffset + 2;
            bool terminated = false;
            while (commentOffset + 1 < static_cast<int32_t>(m_source.size())) {
                if (m_source[commentOffset] == '*'
                    && m_source[commentOffset + 1] == '/') {
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

int32_t Parser::recoverToFuncHeaderEnd(int32_t offset) const
{
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == ')'
            || m_source[normalizedOffset] == '{') {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToExprRParen(int32_t offset) const
{
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == ')'
            || m_source[normalizedOffset] == ';'
            || m_source[normalizedOffset] == '}') {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToRBracket(int32_t offset) const
{
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == ']'
            || m_source[normalizedOffset] == ','
            || m_source[normalizedOffset] == ')'
            || m_source[normalizedOffset] == '}'
            || m_source[normalizedOffset] == ';') {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToInitBoundary(int32_t offset) const
{
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == ','
            || m_source[normalizedOffset] == '}'
            || m_source[normalizedOffset] == ';') {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToIfStmtHead(int32_t offset) const
{
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == ')'
            || m_source[normalizedOffset] == '{'
            || m_source[normalizedOffset] == '}'
            || m_source[normalizedOffset] == ';'
            || m_source[normalizedOffset] == '('
            || m_source[normalizedOffset] == '+'
            || m_source[normalizedOffset] == '-'
            || m_source[normalizedOffset] == '!'
            || std::isdigit(
                   static_cast<unsigned char>(m_source[normalizedOffset]))
                != 0
            || isIdentifierStart(m_source[normalizedOffset])
            || matchKeyword(normalizedOffset, "if").success
            || matchKeyword(normalizedOffset, "while").success
            || matchKeyword(normalizedOffset, "break").success
            || matchKeyword(normalizedOffset, "continue").success
            || matchKeyword(normalizedOffset, "else").success
            || matchKeyword(normalizedOffset, "return").success) {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToWhileStmtHead(int32_t offset) const
{
    return recoverToIfStmtHead(offset);
}

int32_t Parser::recoverToDeclBoundary(int32_t offset) const
{
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == ','
            || m_source[normalizedOffset] == ';'
            || m_source[normalizedOffset] == '}') {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToStmtBoundary(int32_t offset) const
{
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == ';'
            || m_source[normalizedOffset] == '}'
            || matchKeyword(normalizedOffset, "if").success
            || matchKeyword(normalizedOffset, "while").success
            || matchKeyword(normalizedOffset, "break").success
            || matchKeyword(normalizedOffset, "continue").success
            || matchKeyword(normalizedOffset, "return").success
            || matchKeyword(normalizedOffset, "else").success) {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToBlockItemBoundary(int32_t offset) const
{
    auto nextOffset = skipTrivia(offset);
    while (!isAtEnd(nextOffset)) {
        const auto normalizedOffset = skipTrivia(nextOffset);
        if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] == '}'
            || m_source[normalizedOffset] == '{'
            || m_source[normalizedOffset] == ';'
            || m_source[normalizedOffset] == '('
            || m_source[normalizedOffset] == '+'
            || m_source[normalizedOffset] == '-'
            || m_source[normalizedOffset] == '!'
            || std::isdigit(
                   static_cast<unsigned char>(m_source[normalizedOffset]))
                != 0
            || isIdentifierStart(m_source[normalizedOffset])
            || matchKeyword(normalizedOffset, "const").success
            || matchKeyword(normalizedOffset, "int").success
            || matchKeyword(normalizedOffset, "if").success
            || matchKeyword(normalizedOffset, "while").success
            || matchKeyword(normalizedOffset, "break").success
            || matchKeyword(normalizedOffset, "continue").success
            || matchKeyword(normalizedOffset, "return").success) {
            return normalizedOffset;
        }
        nextOffset = normalizedOffset + 1;
    }
    return skipTrivia(nextOffset);
}

int32_t Parser::recoverToBlockEnd(int32_t offset) const
{
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

bool Parser::isAtEnd(int32_t offset) const
{
    return offset >= static_cast<int32_t>(m_source.size());
}

bool Parser::isIdentifierStart(char ch) const
{
    return ch == '_' || std::isalpha(static_cast<unsigned char>(ch)) != 0;
}

bool Parser::isIdentifierContinue(char ch) const
{
    return isIdentifierStart(ch)
        || std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

bool Parser::hasKeywordBoundary(int32_t offset) const
{
    return isAtEnd(offset) || !isIdentifierContinue(m_source[offset]);
}

Parser::KeywordMatch Parser::matchKeyword(
    int32_t offset, std::string_view keyword) const
{
    const auto normalizedOffset = skipTrivia(offset);
    const auto endOffset
        = normalizedOffset + static_cast<int32_t>(keyword.size());
    if (endOffset > static_cast<int32_t>(m_source.size())) {
        return KeywordMatch {
            .success = false,
            .m_startOffset = normalizedOffset,
            .nextOffset = normalizedOffset,
        };
    }

    if (m_source.compare(normalizedOffset, keyword.size(), keyword) != 0
        || !hasKeywordBoundary(endOffset)) {
        return KeywordMatch {
            .success = false,
            .m_startOffset = normalizedOffset,
            .nextOffset = normalizedOffset,
        };
    }

    return KeywordMatch {
        .success = true,
        .m_startOffset = normalizedOffset,
        .nextOffset = endOffset,
    };
}

Parser::KeywordMatch Parser::matchSymbol(int32_t offset, char symbol) const
{
    const auto normalizedOffset = skipTrivia(offset);
    if (isAtEnd(normalizedOffset) || m_source[normalizedOffset] != symbol) {
        return KeywordMatch {
            .success = false,
            .m_startOffset = normalizedOffset,
            .nextOffset = normalizedOffset,
        };
    }

    return KeywordMatch {
        .success = true,
        .m_startOffset = normalizedOffset,
        .nextOffset = normalizedOffset + 1,
    };
}

Parser::KeywordMatch Parser::matchSymbol(
    int32_t offset, std::string_view symbol) const
{
    const auto normalizedOffset = skipTrivia(offset);
    const auto endOffset
        = normalizedOffset + static_cast<int32_t>(symbol.size());
    if (endOffset > static_cast<int32_t>(m_source.size())) {
        return KeywordMatch {
            .success = false,
            .m_startOffset = normalizedOffset,
            .nextOffset = normalizedOffset,
        };
    }

    if (m_source.compare(normalizedOffset, symbol.size(), symbol) != 0) {
        return KeywordMatch {
            .success = false,
            .m_startOffset = normalizedOffset,
            .nextOffset = normalizedOffset,
        };
    }

    return KeywordMatch {
        .success = true,
        .m_startOffset = normalizedOffset,
        .nextOffset = endOffset,
    };
}

ParseResult<int32_t> Parser::parseBaseInteger(int32_t offset, int base,
    int32_t prefixLength, bool (*digitPredicate)(char))
{
    const auto normalizedOffset = skipTrivia(offset);
    auto digitOffset = normalizedOffset + prefixLength;

    if (isAtEnd(digitOffset) || !digitPredicate(m_source[digitOffset])) {
        recordFailure(digitOffset, DiagnosticKind::expectedInteger,
            "expected integer digits");
        return ParseResult<int32_t> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = 0,
        };
    }

    auto nextOffset = digitOffset + 1;
    while (!isAtEnd(nextOffset) && digitPredicate(m_source[nextOffset])) {
        ++nextOffset;
    }

    int32_t parsedValue = 0;
    const auto digits
        = std::string_view(m_source).substr(normalizedOffset + prefixLength,
            nextOffset - normalizedOffset - prefixLength);
    const auto parseStart = digits.data();
    const auto parseEnd = digits.data() + digits.size();
    const auto parseResult
        = std::from_chars(parseStart, parseEnd, parsedValue, base);
    if (parseResult.ec == std::errc::result_out_of_range) {
        recordFailure(normalizedOffset, DiagnosticKind::integerOutOfRange,
            "integer literal is out of int32_t range");
        return ParseResult<int32_t> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = 0,
        };
    }

    if (parseResult.ec != std::errc() || parseResult.ptr != parseEnd) {
        recordFailure(normalizedOffset, DiagnosticKind::expectedInteger,
            "invalid integer constant");
        return ParseResult<int32_t> {
            .success = false,
            .nextOffset = normalizedOffset,
            .value = 0,
        };
    }

    return ParseResult<int32_t> {
        .success = true,
        .nextOffset = nextOffset,
        .value = parsedValue,
    };
}

void Parser::recordFailure(
    int32_t offset, DiagnosticKind kind, std::string message)
{
    if (offset < m_bestFailureOffset) {
        return;
    }

    if (offset > m_bestFailureOffset) {
        m_bestFailureOffset = offset;
        m_bestDiagnostics.clear();
    }

    m_bestDiagnostics.push_back(Diagnostic {
        .kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
    });
}

void Parser::recordCommittedFailure(
    int32_t offset, DiagnosticKind kind, std::string message)
{
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
        .kind = kind,
        .m_offset = offset,
        .m_message = std::move(message),
    });
}

ParseOutput Parser::failureOutput()
{
    if (!m_bestDiagnostics.empty()) {
        return ParseOutput {
            .m_ast = std::move(m_ast),
            .m_root = { },
            .m_diagnostics = m_bestDiagnostics,
        };
    }

    return ParseOutput {
        .m_ast = std::move(m_ast),
        .m_root = { },
        .m_diagnostics = { Diagnostic {
            .kind = DiagnosticKind::unexpectedToken,
            .m_offset = 0,
            .m_message = "parse failed",
        } },
    };
}

} // namespace yesod::frontend