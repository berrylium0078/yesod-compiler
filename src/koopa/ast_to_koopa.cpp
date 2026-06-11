#include "koopa/ast_to_koopa.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace yesod::koopa {

using namespace frontend;
namespace koopa_ir = yesod::koopa::ir;

namespace {

    std::string makeFunctionName(const std::string& identifier);
    std::unique_ptr<koopa_ir::Program> generateIrProgram(const AST& ast,
        Ptr<CompUnit> compUnit, const SemanticInfo& semanticInfo);

    [[nodiscard]] bool isMintType(const SemanticType& type)
    {
        return type.kind == SemanticTypeKind::mint;
    }

    size_t countScalarSlots(const SemanticType& type)
    {
        if (!type.isArray()) {
            return 1;
        }

        if (type.m_elementType == nullptr) {
            throw std::runtime_error("array type is missing element type");
        }

        return static_cast<size_t>(type.m_arrayLength)
            * countScalarSlots(*type.m_elementType);
    }

    template <typename InitNode>
    void fillObjectFromNode(const AST& ast, Ref<InitNode> init_nn,
        const SemanticType& type, size_t baseOffset,
        std::vector<Ptr<Exp>>& scalarExprs);

    template <typename InitNode>
    void assignScalarInitializerFromNode(const AST& ast, Ref<InitNode> init_nn,
        size_t baseOffset, std::vector<Ptr<Exp>>& scalarExprs);

    template <typename InitNode>
    void fillObjectFromSequence(const AST& ast,
        const std::vector<Ref<InitNode>>& values, size_t& nextValueIndex,
        const SemanticType& type, size_t baseOffset,
        std::vector<Ptr<Exp>>& scalarExprs)
    {
        if (nextValueIndex >= values.size()) {
            return;
        }

        if (!type.isArray()) {
            assignScalarInitializerFromNode(
                ast, values[nextValueIndex], baseOffset, scalarExprs);
            ++nextValueIndex;
            return;
        }

        if (type.m_elementType == nullptr) {
            throw std::runtime_error("array type is missing element type");
        }

        const size_t elementSlots = countScalarSlots(*type.m_elementType);
        for (int32_t i = 0;
             i < type.m_arrayLength && nextValueIndex < values.size(); ++i) {
            const auto& child = values[nextValueIndex](ast);
            MATCH(child.kind)
            WITH(
                [&](Ref<Exp>) {
                    fillObjectFromSequence(ast, values, nextValueIndex,
                        *type.m_elementType,
                        baseOffset + static_cast<size_t>(i) * elementSlots,
                        scalarExprs);
                },
                [&](const typename InitNode::List&) {
                    fillObjectFromNode(ast, values[nextValueIndex],
                        *type.m_elementType,
                        baseOffset + static_cast<size_t>(i) * elementSlots,
                        scalarExprs);
                    ++nextValueIndex;
                });
        }
    }

    template <typename InitNode>
    void fillObjectFromNode(const AST& ast, Ref<InitNode> init_nn,
        const SemanticType& type, size_t baseOffset,
        std::vector<Ptr<Exp>>& scalarExprs)
    {
        const auto& init = init_nn(ast);
        MATCH(init.kind)
        WITH(
            [&](Ref<Exp>) {
                size_t nextValueIndex = 0;
                const std::vector<Ref<InitNode>> singleton { init_nn };
                fillObjectFromSequence(ast, singleton, nextValueIndex, type,
                    baseOffset, scalarExprs);
            },
            [&](const typename InitNode::List& initAlt) {
                size_t nextValueIndex = 0;
                fillObjectFromSequence(ast, initAlt, nextValueIndex, type,
                    baseOffset, scalarExprs);
            }, );
    }

    template <typename InitNode>
    void assignScalarInitializerFromNode(const AST& ast, Ref<InitNode> init_nn,
        size_t baseOffset, std::vector<Ptr<Exp>>& scalarExprs)
    {
        const auto& init = init_nn(ast);
        MATCH(init.kind)
        WITH(
            [&](Ptr<Exp> initAlt) {
                if (baseOffset < scalarExprs.size()) {
                    scalarExprs[baseOffset] = initAlt;
                }
            },
            [&](const typename InitNode::List& initAlt) {
                if (!initAlt.empty()) {
                    assignScalarInitializerFromNode(
                        ast, initAlt.front(), baseOffset, scalarExprs);
                }
            }, );
    }

    template <typename InitNode>
    std::vector<Ptr<Exp>> flattenArrayInitializer(
        const AST& ast, Ref<InitNode> init_nn, const SemanticType& type)
    {
        std::vector<Ptr<Exp>> scalarExprs(countScalarSlots(type));
        fillObjectFromNode(ast, init_nn, type, 0, scalarExprs);
        return scalarExprs;
    }

    bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

    bool isAllDigits(const std::string& text)
    {
        if (text.empty()) {
            return false;
        }
        for (const char ch : text) {
            if (!isDigit(ch)) {
                return false;
            }
        }
        return true;
    }

    std::string normalizeIdentifierStem(std::string stem)
    {
        if (!stem.empty() && isDigit(stem.front()) && !isAllDigits(stem)) {
            stem.insert(stem.begin(), '_');
        }
        return stem;
    }

    std::string makeFunctionName(const std::string& identifier)
    {
        return "@" + identifier;
    }

    std::string makeGlobalName(const std::string& identifier)
    {
        return "@" + identifier;
    }

    std::string makeTempName(int32_t& nextTempId)
    {
        return "%t" + std::to_string(nextTempId++);
    }

    const SemanticSymbol& requireSymbolForIdentifier(Ref<Identifier> identifier,
        const SemanticInfo& semanticInfo, const char* message)
    {
        const auto* symbol = semanticInfo.findSymbol(identifier);
        if (symbol == nullptr) {
            throw std::runtime_error(message);
        }
        return *symbol;
    }

} // namespace

std::unique_ptr<koopa_ir::Program> Generator::generateIr(const AST& ast,
    Ptr<CompUnit> compUnit, const SemanticInfo& semanticInfo) const
{
    return generateIrProgram(ast, compUnit, semanticInfo);
}
namespace {

    [[nodiscard]] koopa_ir::Symbol makeIrSymbol(const std::string& spelling)
    {
        return koopa_ir::Symbol { .sourcePos = {}, .spelling = spelling };
    }

    [[nodiscard]] koopa_ir::Type lowerSemanticTypeToIr(
        koopa_ir::Program& program, const SemanticType& semanticType,
        bool decayUnsizedArrayToPointer = true)
    {
        switch (semanticType.kind) {
        case SemanticTypeKind::integer:
        case SemanticTypeKind::boolean:
            return koopa_ir::I32Type {};
        case SemanticTypeKind::mint:
            return koopa_ir::MintType {};
        case SemanticTypeKind::poly:
            return koopa_ir::PolyType {};
        case SemanticTypeKind::pv:
            return koopa_ir::PvType {};
        case SemanticTypeKind::voidType:
            throw std::runtime_error(
                "void type should only appear as an omitted IR return type");
        case SemanticTypeKind::array:
            if (semanticType.m_elementType == nullptr) {
                throw std::runtime_error("array type is missing element type");
            }
            if (semanticType.m_arrayLength == -1
                && decayUnsizedArrayToPointer) {
                return program.alloc<koopa_ir::PointerType>(
                    koopa_ir::PointerType {
                        .sourcePos = {},
                        .pointeeType = lowerSemanticTypeToIr(
                            program, *semanticType.m_elementType, false),
                    });
            }
            return program.alloc<koopa_ir::ArrayType>(koopa_ir::ArrayType {
                .sourcePos = {},
                .elementType = lowerSemanticTypeToIr(
                    program, *semanticType.m_elementType, false),
                .length = semanticType.m_arrayLength,
            });
        }

        throw std::runtime_error("unsupported semantic type for IR lowering");
    }

    [[nodiscard]] bool isZeroInitializer(
        const koopa_ir::Initializer& initializer,
        const koopa_ir::Program& program)
    {
        return MATCH(initializer) WITH(
            [&](const koopa_ir::IntegerLiteral& literal) {
                return literal.value == 0;
            },
            [&](const koopa_ir::UndefValue&) { return false; },
            [&](const koopa_ir::ZeroInit&) { return true; },
            [&](Ref<koopa_ir::AggregateInitializer> aggregateRef) {
                const auto& aggregate = program[aggregateRef];
                return std::all_of(aggregate.elements.begin(),
                    aggregate.elements.end(), [&](const auto& element) {
                        return isZeroInitializer(element, program);
                    });
            });
    }

    [[nodiscard]] koopa_ir::Initializer generateGlobalInitializerToIr(
        koopa_ir::Program& program, const SemanticType& type,
        const std::vector<Ptr<Exp>>& scalarExprs, size_t& nextScalarIndex,
        const SemanticInfo& semanticInfo)
    {
        if (!type.isArray()) {
            if (nextScalarIndex >= scalarExprs.size()) {
                return koopa_ir::IntegerLiteral { .sourcePos = {}, .value = 0 };
            }

            const auto exp_nn = scalarExprs[nextScalarIndex++];
            if (!exp_nn) {
                return koopa_ir::IntegerLiteral { .sourcePos = {}, .value = 0 };
            }

            const auto constantValue
                = semanticInfo.findConstantValue(exp_nn.ref());
            if (!constantValue.has_value()) {
                throw std::runtime_error(
                    "global array initializer element must be constant");
            }
            return koopa_ir::IntegerLiteral {
                .sourcePos = {},
                .value = *constantValue,
            };
        }

        if (type.m_elementType == nullptr) {
            throw std::runtime_error("array type is missing element type");
        }

        std::vector<koopa_ir::Initializer> elements;
        elements.reserve(static_cast<size_t>(type.m_arrayLength));
        for (int32_t i = 0; i < type.m_arrayLength; ++i) {
            elements.push_back(
                generateGlobalInitializerToIr(program, *type.m_elementType,
                    scalarExprs, nextScalarIndex, semanticInfo));
        }

        if (elements.empty()) {
            return koopa_ir::ZeroInit {};
        }
        if (std::all_of(
                elements.begin(), elements.end(), [&](const auto& element) {
                    return isZeroInitializer(element, program);
                })) {
            return koopa_ir::ZeroInit {};
        }
        return program.alloc<koopa_ir::AggregateInitializer>(
            koopa_ir::AggregateInitializer {
                .sourcePos = {},
                .elements = std::move(elements),
            });
    }

    [[nodiscard]] std::optional<koopa_ir::Type> lowerOptionalReturnTypeToIr(
        koopa_ir::Program& program, const SemanticType& type)
    {
        if (type.kind == SemanticTypeKind::voidType) {
            return std::nullopt;
        }
        return lowerSemanticTypeToIr(program, type);
    }

    [[nodiscard]] koopa_ir::StoreValue toStoreValue(
        const koopa_ir::Value& value)
    {
        return MATCH(value) WITH(
            [&](const koopa_ir::Symbol& symbol) {
                return koopa_ir::StoreValue { symbol };
            },
            [&](const koopa_ir::IntegerLiteral& literal) {
                return koopa_ir::StoreValue { literal };
            },
            [&](const koopa_ir::UndefValue& undef) {
                return koopa_ir::StoreValue { undef };
            });
    }

    [[nodiscard]] koopa_ir::BinaryOp lowerBinaryOpToIr(BinaryOpKeyword op)
    {
        switch (op) {
        case BinaryOpKeyword::star:
            return koopa_ir::BinaryOp::mul;
        case BinaryOpKeyword::slash:
            return koopa_ir::BinaryOp::div;
        case BinaryOpKeyword::percent:
            return koopa_ir::BinaryOp::mod;
        case BinaryOpKeyword::plus:
            return koopa_ir::BinaryOp::add;
        case BinaryOpKeyword::minus:
            return koopa_ir::BinaryOp::sub;
        case BinaryOpKeyword::shl:
            return koopa_ir::BinaryOp::shl;
        case BinaryOpKeyword::sar:
            return koopa_ir::BinaryOp::sar;
        case BinaryOpKeyword::less:
            return koopa_ir::BinaryOp::lt;
        case BinaryOpKeyword::greater:
            return koopa_ir::BinaryOp::gt;
        case BinaryOpKeyword::lessEqual:
            return koopa_ir::BinaryOp::le;
        case BinaryOpKeyword::greaterEqual:
            return koopa_ir::BinaryOp::ge;
        case BinaryOpKeyword::equal:
            return koopa_ir::BinaryOp::eq;
        case BinaryOpKeyword::notEqual:
            return koopa_ir::BinaryOp::ne;
        case BinaryOpKeyword::bitAnd:
            return koopa_ir::BinaryOp::bitAnd;
        case BinaryOpKeyword::bitXor:
            return koopa_ir::BinaryOp::bitXor;
        case BinaryOpKeyword::bitOr:
            return koopa_ir::BinaryOp::bitOr;
        case BinaryOpKeyword::andAnd:
        case BinaryOpKeyword::orOr:
            break;
        }

        throw std::runtime_error("short-circuit binary expression should lower "
                                 "through boolean branching");
    }

    struct IrAddress {
        koopa_ir::Symbol symbol;
        SemanticType pointeeType;
    };

    struct IrFunctionGenerator {
        const frontend::AST& m_ast;
        const frontend::SemanticInfo* m_semanticInfo;
        koopa_ir::Program* m_program;
        Ref<koopa_ir::FunctionDef> m_function;
        Ref<koopa_ir::BasicBlock> m_currentBasicBlock;
        int32_t m_nextTempId = 1;
        int32_t m_nextBlockId = 1;
        std::unordered_map<int32_t, std::string> m_storageBySymbolId;
        const frontend::SemanticFunctionSSA* m_ssa = nullptr;
        std::unordered_map<int64_t, koopa_ir::Value> m_valueByAliasKey;
        std::unordered_map<int32_t, std::string> m_functionBySymbolId;
        std::unordered_map<Ref<frontend::SemanticBasicBlock>,
            Ref<koopa_ir::BasicBlock>>
            m_basicBlockBySemanticBlock;
        std::unordered_set<Ref<koopa_ir::BasicBlock>> m_terminatedBlocks;
        std::unordered_set<std::string> m_usedSymbolNames;
        SemanticType m_functionReturnType = SemanticType::makeVoid();

        [[nodiscard]] koopa_ir::BasicBlock& currentBlock() const
        {
            return (*m_program)[m_currentBasicBlock];
        }

        [[nodiscard]] koopa_ir::FunctionDef& function() const
        {
            return (*m_program)[m_function];
        }

        [[nodiscard]] std::string makeUniqueLocalName(
            const frontend::SemanticSymbol& symbol)
        {
            const std::string baseName
                = "%" + normalizeIdentifierStem(symbol.name);
            if (m_usedSymbolNames.insert(baseName).second) {
                return baseName;
            }

            int32_t suffix = 1;
            while (true) {
                const std::string candidate
                    = baseName + "_" + std::to_string(suffix++);
                if (m_usedSymbolNames.insert(candidate).second) {
                    return candidate;
                }
            }
        }

        [[nodiscard]] static int64_t aliasKey(
            const frontend::SemanticSsaAlias& alias)
        {
            return (static_cast<int64_t>(alias.m_symbolId) << 32)
                ^ static_cast<uint32_t>(alias.m_version);
        }

        void bindAlias(
            Ref<frontend::Identifier> identifier, koopa_ir::Value value)
        {
            const auto alias = m_semanticInfo->findAlias(identifier);
            if (!alias.has_value()) {
                throw std::runtime_error(
                    "identifier is missing semantic SSA alias");
            }
            m_valueByAliasKey[aliasKey(*alias)] = std::move(value);
        }

        [[nodiscard]] koopa_ir::Value requireAliasValue(
            Ref<frontend::Identifier> identifier, const char* message) const
        {
            const auto alias = m_semanticInfo->findAlias(identifier);
            if (!alias.has_value()) {
                throw std::runtime_error(message);
            }
            const auto valueIt = m_valueByAliasKey.find(aliasKey(*alias));
            if (valueIt == m_valueByAliasKey.end()) {
                throw std::runtime_error(message);
            }
            return valueIt->second;
        }

        void pushStatement(koopa_ir::Statement statement)
        {
            currentBlock().statements.push_back(std::move(statement));
        }

        void setTerminator(koopa_ir::Terminator terminator)
        {
            currentBlock().terminator = std::move(terminator);
            m_terminatedBlocks.insert(m_currentBasicBlock);
        }

        [[nodiscard]] bool blockHasTerminator(
            Ref<koopa_ir::BasicBlock> basicBlock) const
        {
            return m_terminatedBlocks.contains(basicBlock);
        }

        [[nodiscard]] koopa_ir::Value emitNamedRhs(
            koopa_ir::SymbolRhs rhs, const std::string& name)
        {
            auto defRef
                = m_program->alloc<koopa_ir::SymbolDef>(koopa_ir::SymbolDef {
                    .sourcePos = {},
                    .symbol = makeIrSymbol(name),
                    .rhs = std::move(rhs),
                    .annotations = {},
                });
            pushStatement(defRef);
            return makeIrSymbol(name);
        }

        [[nodiscard]] koopa_ir::Value emitAlloc(
            const SemanticType& type, const std::string& name)
        {
            auto rhsRef = m_program->alloc<koopa_ir::MemoryDeclaration>(
                koopa_ir::MemoryDeclaration {
                    .sourcePos = {},
                    .allocType = lowerSemanticTypeToIr(*m_program, type),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, name);
        }

        [[nodiscard]] koopa_ir::Value emitLoad(const koopa_ir::Symbol& source)
        {
            auto rhsRef
                = m_program->alloc<koopa_ir::LoadExpr>(koopa_ir::LoadExpr {
                    .sourcePos = {},
                    .source = source,
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitBinary(
            koopa_ir::BinaryOp op, koopa_ir::Value lhs, koopa_ir::Value rhs)
        {
            auto rhsRef
                = m_program->alloc<koopa_ir::BinaryExpr>(koopa_ir::BinaryExpr {
                    .sourcePos = {},
                    .op = op,
                    .lhs = std::move(lhs),
                    .rhs = std::move(rhs),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitGetPointer(
            const koopa_ir::Symbol& source, koopa_ir::Value index)
        {
            auto rhsRef = m_program->alloc<koopa_ir::GetPointerExpr>(
                koopa_ir::GetPointerExpr {
                    .sourcePos = {},
                    .source = source,
                    .index = std::move(index),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitGetElementPointer(
            const koopa_ir::Symbol& source, koopa_ir::Value index)
        {
            auto rhsRef = m_program->alloc<koopa_ir::GetElementPointerExpr>(
                koopa_ir::GetElementPointerExpr {
                    .sourcePos = {},
                    .source = source,
                    .index = std::move(index),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitCall(const std::string& callee,
            std::vector<koopa_ir::Value> args, bool hasReturnValue)
        {
            auto callRef
                = m_program->alloc<koopa_ir::CallExpr>(koopa_ir::CallExpr {
                    .sourcePos = {},
                    .callee = makeIrSymbol(callee),
                    .args = std::move(args),
                    .annotations = {},
                });
            if (!hasReturnValue) {
                pushStatement(callRef);
                return koopa_ir::UndefValue {};
            }
            return emitNamedRhs(callRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitUnaryPoly(
            koopa_ir::UnaryPolyOp op, koopa_ir::Value value)
        {
            auto rhsRef = m_program->alloc<koopa_ir::UnaryPolyExpr>(
                koopa_ir::UnaryPolyExpr {
                    .sourcePos = {},
                    .op = op,
                    .value = std::move(value),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitPvBinary(
            koopa_ir::PvBinaryOp op, koopa_ir::Value lhs, koopa_ir::Value rhs)
        {
            auto rhsRef = m_program->alloc<koopa_ir::PvBinaryExpr>(
                koopa_ir::PvBinaryExpr {
                    .sourcePos = {},
                    .op = op,
                    .lhs = std::move(lhs),
                    .rhs = std::move(rhs),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitCombine(
            std::vector<koopa_ir::CombineTerm> terms)
        {
            auto rhsRef = m_program->alloc<koopa_ir::CombineExpr>(
                koopa_ir::CombineExpr {
                    .sourcePos = {},
                    .terms = std::move(terms),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitGetCoeff(
            koopa_ir::Value value, koopa_ir::Value index)
        {
            auto rhsRef = m_program->alloc<koopa_ir::GetCoeffExpr>(
                koopa_ir::GetCoeffExpr {
                    .sourcePos = {},
                    .value = std::move(value),
                    .index = std::move(index),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitPolyConstruct(
            std::vector<koopa_ir::Value> elements)
        {
            auto rhsRef = m_program->alloc<koopa_ir::PolyConstructExpr>(
                koopa_ir::PolyConstructExpr {
                    .sourcePos = {},
                    .elements = std::move(elements),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitConversion(
            koopa_ir::ConversionOp op, koopa_ir::Value value)
        {
            auto rhsRef = m_program->alloc<koopa_ir::ConversionExpr>(
                koopa_ir::ConversionExpr {
                    .sourcePos = {},
                    .op = op,
                    .value = std::move(value),
                    .annotations = {},
                });
            return emitNamedRhs(rhsRef, makeTempName(m_nextTempId));
        }

        [[nodiscard]] koopa_ir::Value emitIntAdd(
            koopa_ir::Value lhs, koopa_ir::Value rhs)
        {
            return emitBinary(
                koopa_ir::BinaryOp::add, std::move(lhs), std::move(rhs));
        }

        [[nodiscard]] koopa_ir::Value emitMintMul(
            koopa_ir::Value lhs, koopa_ir::Value rhs)
        {
            return emitBinary(
                koopa_ir::BinaryOp::mul, std::move(lhs), std::move(rhs));
        }

        [[nodiscard]] koopa_ir::Symbol requireSymbolValue(
            const koopa_ir::Value& value, const char* message) const
        {
            if (const auto* symbol = std::get_if<koopa_ir::Symbol>(&value)) {
                return *symbol;
            }
            throw std::runtime_error(message);
        }

        [[nodiscard]] Ref<koopa_ir::BasicBlock> createBasicBlock(
            const std::string& stem)
        {
            auto placeholderRef = m_program->alloc<koopa_ir::ReturnTerminator>(
                koopa_ir::ReturnTerminator {
                    .sourcePos = {},
                    .value = std::nullopt,
                    .annotations = {},
                });
            auto blockRef
                = m_program->alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                    .sourcePos = {},
                    .label = makeIrSymbol(
                        "%" + stem + "_" + std::to_string(m_nextBlockId++)),
                    .params = {},
                    .statements = {},
                    .terminator = placeholderRef,
                    .annotations = {},
                });
            function().blocks.push_back(blockRef);
            return blockRef;
        }

        [[nodiscard]] std::vector<koopa_ir::Value> edgeArgs(
            Ref<frontend::SemanticBasicBlock> source,
            Ref<frontend::SemanticBasicBlock> target) const
        {
            const auto sourceIt = m_ssa->m_blockInfoByBlock.find(source);
            if (sourceIt == m_ssa->m_blockInfoByBlock.end()) {
                throw std::runtime_error("source block is missing SSA info");
            }
            const auto argsIt
                = sourceIt->second.m_outgoingArgsByTarget.find(target);
            if (argsIt == sourceIt->second.m_outgoingArgsByTarget.end()) {
                return {};
            }
            std::vector<koopa_ir::Value> values;
            values.reserve(argsIt->second.size());
            for (const auto& alias : argsIt->second) {
                const auto valueIt = m_valueByAliasKey.find(aliasKey(alias));
                if (valueIt == m_valueByAliasKey.end()) {
                    throw std::runtime_error(
                        "SSA edge argument is missing a value binding");
                }
                values.push_back(valueIt->second);
            }
            return values;
        }

        [[nodiscard]] koopa_ir::Value generateSemanticValue(
            const frontend::SemanticValue& value)
        {
            return MATCH(value.kind) WITH(
                [&](int32_t constantValue) -> koopa_ir::Value {
                    return koopa_ir::IntegerLiteral {
                        .sourcePos = {},
                        .value = constantValue,
                    };
                },
                [&](Ref<Exp> exp) -> koopa_ir::Value {
                    return generateExp(exp);
                });
        }

        void generateSemanticBlock(
            Ref<frontend::SemanticBasicBlock> semanticBlockRef)
        {
            const auto& semanticBlock
                = semanticBlockRef(m_semanticInfo->controlFlowArena());
            const auto ssaBlockIt
                = m_ssa->m_blockInfoByBlock.find(semanticBlockRef);
            if (ssaBlockIt == m_ssa->m_blockInfoByBlock.end()) {
                throw std::runtime_error(
                    "semantic basic block is missing SSA info");
            }
            auto& block = currentBlock();
            if (ssaBlockIt->second.m_params.size() != block.params.size()) {
                throw std::runtime_error(
                    "basic block parameter count should match SSA parameters");
            }
            for (size_t i = 0; i < ssaBlockIt->second.m_params.size(); ++i) {
                const auto& blockParam = (*m_program)[block.params[i]];
                m_valueByAliasKey[aliasKey(
                    ssaBlockIt->second.m_params[i].m_alias)]
                    = blockParam.symbol;
            }
            for (const auto& item : semanticBlock.items) {
                generateSemanticBlockItem(item);
            }
            if (!semanticBlock.terminator.has_value()) {
                throw std::runtime_error(
                    "semantic basic block is missing terminator");
            }
            generateSemanticTerminator(
                semanticBlockRef, *semanticBlock.terminator);
        }

        void generateSemanticBlockItem(
            const frontend::SemanticBlockItem& semanticBlockItem)
        {
            MATCH(semanticBlockItem)
            WITH([&](frontend::Decl decl) { generateDecl(decl); },
                [&](Ref<AssignStmt> assignStmt) {
                    generateAssignStmt(assignStmt.ptr());
                },
                [&](Ref<ExpStmt> expStmt) { generateExpStmt(expStmt.ptr()); });
        }

        void generateSemanticTerminator(
            Ref<frontend::SemanticBasicBlock> source,
            const frontend::SemanticBlockTerminator& terminator)
        {
            auto& state = *this;
            MATCH(terminator)
            WITH(
                [&](const frontend::SemanticJumpTerminator& jump) {
                    state.setTerminator(
                        state.m_program->alloc<koopa_ir::JumpTerminator>(
                            koopa_ir::JumpTerminator {
                                .sourcePos = {},
                                .target
                                = (*state.m_program)
                                      [state.m_basicBlockBySemanticBlock.at(
                                           jump.target)]
                                          .label,
                                .args = state.edgeArgs(source, jump.target),
                                .annotations = {},
                            }));
                },
                [&](const frontend::SemanticBranchTerminator& branch) {
                    auto trueTarget = state.m_basicBlockBySemanticBlock.at(
                        branch.trueTarget);
                    auto falseTarget = state.m_basicBlockBySemanticBlock.at(
                        branch.falseTarget);
                    koopa_ir::Value condition;
                    if (const auto constantValue
                        = state.m_semanticInfo->findConstantValue(
                            branch.condition);
                        constantValue.has_value()) {
                        condition = koopa_ir::IntegerLiteral {
                            .sourcePos = {},
                            .value = *constantValue,
                        };
                    } else {
                        condition = state.generateExp(branch.condition);
                    }
                    state.setTerminator(
                        state.m_program->alloc<koopa_ir::BranchTerminator>(
                            koopa_ir::BranchTerminator {
                                .sourcePos = {},
                                .condition = std::move(condition),
                                .trueTarget
                                = (*state.m_program)[trueTarget].label,
                                .trueArgs
                                = state.edgeArgs(source, branch.trueTarget),
                                .falseTarget
                                = (*state.m_program)[falseTarget].label,
                                .falseArgs
                                = state.edgeArgs(source, branch.falseTarget),
                                .annotations = {},
                            }));
                },
                [&](const frontend::SemanticReturnTerminator&
                        returnTerminator) {
                    std::optional<koopa_ir::Value> value;
                    if (returnTerminator.value.has_value()) {
                        value = state.generateSemanticValue(
                            *returnTerminator.value);
                    }
                    state.setTerminator(
                        state.m_program->alloc<koopa_ir::ReturnTerminator>(
                            koopa_ir::ReturnTerminator {
                                .sourcePos = {},
                                .value = std::move(value),
                                .annotations = {},
                            }));
                });
        }

        void generateDecl(frontend::Decl decl)
        {
            auto& state = *this;
            MATCH(decl)
            WITH(
                [&](Ptr<ConstDecl> declAlt) {
                    const auto& constDecl = declAlt(state.m_ast);
                    for (const auto constDef : constDecl.constDef) {
                        const auto& parsedConstDef = constDef(state.m_ast);
                        const auto& symbol = requireSymbolForIdentifier(
                            parsedConstDef.identifier, *state.m_semanticInfo,
                            "const declarator is missing its symbol binding");
                        const auto& type = symbol.object().m_type;
                        if (state.m_semanticInfo
                                ->findAlias(parsedConstDef.identifier)
                                .has_value()
                            && type.isScalar()) {
                            const auto& constInitVal
                                = parsedConstDef.constInitVal(state.m_ast);
                            MATCH(constInitVal.kind)
                            WITH(
                                [&](Ref<Exp> initAlt) {
                                    state.bindAlias(parsedConstDef.identifier,
                                        state.generateExp(initAlt));
                                },
                                [&](const auto&) {
                                    throw std::runtime_error(
                                        "scalar const initializer should be a "
                                        "scalar expression");
                                });
                            continue;
                        }

                        const std::string allocName
                            = state.makeUniqueLocalName(symbol);
                        (void)state.emitAlloc(type, allocName);
                        state.m_storageBySymbolId[symbol.m_id] = allocName;

                        if (type.isArray()) {
                            auto scalarExprs
                                = flattenArrayInitializer(state.m_ast,
                                    parsedConstDef.constInitVal.ref(), type);
                            size_t nextScalarIndex = 0;
                            state.generateLocalArrayInitializer(
                                makeIrSymbol(allocName), type, scalarExprs,
                                nextScalarIndex);
                            continue;
                        }

                        const auto& constInitVal
                            = parsedConstDef.constInitVal(state.m_ast);
                        MATCH(constInitVal.kind)
                        WITH(
                            [&](Ref<Exp> initAlt) {
                                auto storeRef = state.m_program->alloc<
                                    koopa_ir::StoreStmt>(koopa_ir::StoreStmt {
                                    .sourcePos = {},
                                    .value
                                    = toStoreValue(state.generateExp(initAlt)),
                                    .destination = makeIrSymbol(allocName),
                                    .annotations = {},
                                });
                                state.pushStatement(storeRef);
                            },
                            [&](const auto&) {});
                    }
                },
                [&](Ptr<VarDecl> declAlt) {
                    const auto& varDecl = declAlt(state.m_ast);
                    for (const auto varDef : varDecl.varDef) {
                        const auto& resolvedVarDef = varDef(state.m_ast);
                        const auto& symbol = requireSymbolForIdentifier(
                            resolvedVarDef.identifier, *state.m_semanticInfo,
                            "var declarator is missing its symbol binding");
                        const auto& type = symbol.object().m_type;
                        if (state.m_semanticInfo
                                ->findAlias(resolvedVarDef.identifier)
                                .has_value()
                            && type.isScalar()) {
                            if (resolvedVarDef.initVal) {
                                const auto& initVal
                                    = resolvedVarDef.initVal(state.m_ast);
                                MATCH(initVal.kind)
                                WITH(
                                    [&](Ref<Exp> initAlt) {
                                        state.bindAlias(
                                            resolvedVarDef.identifier,
                                            state.generateExp(initAlt));
                                    },
                                    [&](const auto&) {
                                        throw std::runtime_error(
                                            "scalar var initializer should be "
                                            "a scalar expression");
                                    });
                            } else {
                                state.bindAlias(resolvedVarDef.identifier,
                                    koopa_ir::UndefValue {});
                            }
                            continue;
                        }

                        const std::string allocName
                            = state.makeUniqueLocalName(symbol);
                        (void)state.emitAlloc(type, allocName);
                        state.m_storageBySymbolId[symbol.m_id] = allocName;
                        if (!resolvedVarDef.initVal) {
                            if (type.isPoly()) {
                                auto storeRef = state.m_program->alloc<
                                    koopa_ir::StoreStmt>(koopa_ir::StoreStmt {
                                    .sourcePos = {},
                                    .value
                                    = toStoreValue(state.emitPolyConstruct({})),
                                    .destination = makeIrSymbol(allocName),
                                    .annotations = {},
                                });
                                state.pushStatement(storeRef);
                            }
                            continue;
                        }
                        if (type.isArray()) {
                            auto scalarExprs
                                = flattenArrayInitializer(state.m_ast,
                                    resolvedVarDef.initVal.ref(), type);
                            size_t nextScalarIndex = 0;
                            state.generateLocalArrayInitializer(
                                makeIrSymbol(allocName), type, scalarExprs,
                                nextScalarIndex);
                            continue;
                        }

                        const auto& initVal
                            = resolvedVarDef.initVal(state.m_ast);
                        MATCH(initVal.kind)
                        WITH(
                            [&](Ref<Exp> initAlt) {
                                auto storeRef = state.m_program->alloc<
                                    koopa_ir::StoreStmt>(koopa_ir::StoreStmt {
                                    .sourcePos = {},
                                    .value
                                    = toStoreValue(state.generateExp(initAlt)),
                                    .destination = makeIrSymbol(allocName),
                                    .annotations = {},
                                });
                                state.pushStatement(storeRef);
                            },
                            [&](const auto&) {});
                    }
                });
        }

        void generateAssignStmt(Ptr<AssignStmt> assignStmt)
        {
            const auto& parsedAssignStmt = assignStmt(m_ast);
            const auto& lValExp = parsedAssignStmt.lval(m_ast);
            MATCH(lValExp.kind)
            WITH(
                [&](Exp::LVal expAlt) {
                    if (m_semanticInfo->findAlias(expAlt.identifier).has_value()
                        && expAlt.indices.empty()) {
                        bindAlias(expAlt.identifier,
                            generateExp(parsedAssignStmt.exp));
                        return;
                    }
                    const auto address = generateLValueAddress(expAlt);
                    auto storeRef = m_program->alloc<koopa_ir::StoreStmt>(
                        koopa_ir::StoreStmt {
                            .sourcePos = {},
                            .value
                            = toStoreValue(generateExp(parsedAssignStmt.exp)),
                            .destination = address.symbol,
                            .annotations = {},
                        });
                    pushStatement(storeRef);
                },
                [&](const auto&) {
                    throw std::runtime_error(
                        "assignment lhs is not an lvalue expression");
                });
        }

        void generateExpStmt(Ptr<ExpStmt> expStmt)
        {
            const auto& parsedExpStmt = expStmt(m_ast);
            if (parsedExpStmt.exp) {
                (void)generateExp(parsedExpStmt.exp.ref());
            }
        }

        [[nodiscard]] bool expHasType(Ref<Exp> exp, SemanticTypeKind kind) const
        {
            const auto type = m_semanticInfo->findExpType(exp);
            return type.has_value() && type->kind == kind;
        }

        [[nodiscard]] koopa_ir::Value mintValueForExp(Ref<Exp> exp)
        {
            auto value = generateExp(exp);
            const auto type = m_semanticInfo->findExpType(exp);
            if (type.has_value() && type->kind == SemanticTypeKind::integer) {
                return emitConversion(
                    koopa_ir::ConversionOp::int2mint, std::move(value));
            }
            return value;
        }

        [[nodiscard]] koopa_ir::Value generatePolyConstructFromCast(
            const Exp::Cast& cast)
        {
            std::vector<koopa_ir::Value> elements;
            elements.push_back(mintValueForExp(cast.value));
            return emitPolyConstruct(std::move(elements));
        }

        [[nodiscard]] koopa_ir::Value generatePolyConvolution(
            const Exp::Binary& binaryExp)
        {
            auto lhsPv = emitUnaryPoly(
                koopa_ir::UnaryPolyOp::ntt, generateExp(binaryExp.lhs));
            auto rhsPv = emitUnaryPoly(
                koopa_ir::UnaryPolyOp::ntt, generateExp(binaryExp.rhs));
            auto product = emitPvBinary(
                koopa_ir::PvBinaryOp::mul, std::move(lhsPv), std::move(rhsPv));
            return emitUnaryPoly(
                koopa_ir::UnaryPolyOp::intt, std::move(product));
        }

        [[nodiscard]] koopa_ir::Value generatePolyBaseValue(Ref<Exp> exp)
        {
            const auto& parsedExp = exp(m_ast);
            return MATCH(parsedExp.kind) WITH(
                [&](Exp::Binary expAlt) -> koopa_ir::Value {
                    if (expAlt.op == BinaryOpKeyword::star
                        && expHasType(expAlt.lhs, SemanticTypeKind::poly)
                        && expHasType(expAlt.rhs, SemanticTypeKind::poly)) {
                        return generatePolyConvolution(expAlt);
                    }
                    return emitCombine(generatePolyLinearTerms(exp));
                },
                [&](Exp::Cast expAlt) -> koopa_ir::Value {
                    if (expAlt.targetType == BTypeKeyword::polyKeyword) {
                        return generatePolyConstructFromCast(expAlt);
                    }
                    return generateExp(expAlt.value);
                },
                [&](Exp::Call expAlt) -> koopa_ir::Value {
                    const auto& symbol = requireSymbolForIdentifier(
                        expAlt.funcName, *m_semanticInfo,
                        "call target is missing a symbol binding");
                    const auto functionIt
                        = m_functionBySymbolId.find(symbol.m_id);
                    if (functionIt == m_functionBySymbolId.end()) {
                        throw std::runtime_error("call target is missing a "
                                                 "lowered function binding");
                    }
                    std::vector<koopa_ir::Value> args;
                    args.reserve(expAlt.params.size());
                    for (const auto arg_nn : expAlt.params) {
                        args.push_back(generateExp(arg_nn));
                    }
                    return emitCall(functionIt->second, std::move(args), true);
                },
                [&](Exp::LVal expAlt) -> koopa_ir::Value {
                    auto address = generateLValueAddress(expAlt);
                    return emitLoad(address.symbol);
                },
                [&](Exp::PolyConstruct expAlt) -> koopa_ir::Value {
                    std::vector<koopa_ir::Value> elements;
                    elements.reserve(expAlt.elements.size());
                    for (const auto element : expAlt.elements) {
                        elements.push_back(mintValueForExp(element));
                    }
                    return emitPolyConstruct(std::move(elements));
                },
                [&](const auto&) -> koopa_ir::Value {
                    return emitCombine(generatePolyLinearTerms(exp));
                });
        }

        [[nodiscard]] koopa_ir::CombineTerm makeIdentityPolyTerm(
            koopa_ir::Value value)
        {
            return koopa_ir::CombineTerm {
                .value = std::move(value),
                .start
                = koopa_ir::IntegerLiteral { .sourcePos = {}, .value = 0 },
                .end = std::nullopt,
                .shift
                = koopa_ir::IntegerLiteral { .sourcePos = {}, .value = 0 },
                .scale
                = koopa_ir::IntegerLiteral { .sourcePos = {}, .value = 1 },
            };
        }

        void scaleTerms(
            std::vector<koopa_ir::CombineTerm>& terms, koopa_ir::Value scale)
        {
            for (auto& term : terms) {
                term.scale = emitMintMul(std::move(term.scale), scale);
            }
        }

        void shiftTerms(
            std::vector<koopa_ir::CombineTerm>& terms, koopa_ir::Value shift)
        {
            for (auto& term : terms) {
                term.start = emitIntAdd(std::move(term.start), shift);
                if (term.end.has_value()) {
                    term.end = emitIntAdd(std::move(*term.end), shift);
                }
                term.shift = emitIntAdd(std::move(term.shift), shift);
            }
        }

        void sliceTerms(std::vector<koopa_ir::CombineTerm>& terms,
            koopa_ir::Value start, koopa_ir::Value end)
        {
            for (auto& term : terms) {
                term.start = emitIntAdd(start, term.shift);
                term.end = emitIntAdd(end, term.shift);
            }
        }

        [[nodiscard]] std::vector<koopa_ir::CombineTerm>
        generatePolyLinearTerms(Ref<Exp> exp)
        {
            const auto& parsedExp = exp(m_ast);
            return MATCH(parsedExp.kind) WITH(
                [&](Exp::Binary expAlt) -> std::vector<koopa_ir::CombineTerm> {
                    if ((expAlt.op == BinaryOpKeyword::plus
                            || expAlt.op == BinaryOpKeyword::minus)
                        && expHasType(expAlt.lhs, SemanticTypeKind::poly)
                        && expHasType(expAlt.rhs, SemanticTypeKind::poly)) {
                        auto terms = generatePolyLinearTerms(expAlt.lhs);
                        auto rhsTerms = generatePolyLinearTerms(expAlt.rhs);
                        if (expAlt.op == BinaryOpKeyword::minus) {
                            scaleTerms(rhsTerms,
                                koopa_ir::IntegerLiteral {
                                    .sourcePos = {}, .value = -1 });
                        }
                        terms.insert(terms.end(),
                            std::make_move_iterator(rhsTerms.begin()),
                            std::make_move_iterator(rhsTerms.end()));
                        return terms;
                    }
                    if (expAlt.op == BinaryOpKeyword::star) {
                        if (expHasType(expAlt.lhs, SemanticTypeKind::poly)
                            && !expHasType(
                                expAlt.rhs, SemanticTypeKind::poly)) {
                            auto terms = generatePolyLinearTerms(expAlt.lhs);
                            scaleTerms(terms, mintValueForExp(expAlt.rhs));
                            return terms;
                        }
                        if (!expHasType(expAlt.lhs, SemanticTypeKind::poly)
                            && expHasType(expAlt.rhs, SemanticTypeKind::poly)) {
                            auto terms = generatePolyLinearTerms(expAlt.rhs);
                            scaleTerms(terms, mintValueForExp(expAlt.lhs));
                            return terms;
                        }
                    }
                    if ((expAlt.op == BinaryOpKeyword::sar
                            || expAlt.op == BinaryOpKeyword::shl)
                        && expHasType(expAlt.lhs, SemanticTypeKind::poly)) {
                        auto terms = generatePolyLinearTerms(expAlt.lhs);
                        auto shift = generateExp(expAlt.rhs);
                        if (expAlt.op == BinaryOpKeyword::shl) {
                            shift = emitBinary(koopa_ir::BinaryOp::sub,
                                koopa_ir::IntegerLiteral {
                                    .sourcePos = {}, .value = 0 },
                                std::move(shift));
                        }
                        shiftTerms(terms, std::move(shift));
                        return terms;
                    }
                    return { makeIdentityPolyTerm(generatePolyBaseValue(exp)) };
                },
                [&](Exp::Slice expAlt) -> std::vector<koopa_ir::CombineTerm> {
                    auto terms = generatePolyLinearTerms(expAlt.base);
                    sliceTerms(terms, generateExp(expAlt.start),
                        generateExp(expAlt.end));
                    return terms;
                },
                [&](const auto&) -> std::vector<koopa_ir::CombineTerm> {
                    return { makeIdentityPolyTerm(generatePolyBaseValue(exp)) };
                });
        }

        [[nodiscard]] koopa_ir::Value generatePolyExpression(Ref<Exp> exp)
        {
            const auto& parsedExp = exp(m_ast);
            if (const auto* binary = std::get_if<Exp::Binary>(&parsedExp.kind);
                binary != nullptr && binary->op == BinaryOpKeyword::star
                && expHasType(binary->lhs, SemanticTypeKind::poly)
                && expHasType(binary->rhs, SemanticTypeKind::poly)) {
                return generatePolyConvolution(*binary);
            }
            if (std::holds_alternative<Exp::Cast>(parsedExp.kind)
                || std::holds_alternative<Exp::Call>(parsedExp.kind)
                || std::holds_alternative<Exp::LVal>(parsedExp.kind)
                || std::holds_alternative<Exp::PolyConstruct>(parsedExp.kind)) {
                return generatePolyBaseValue(exp);
            }
            return emitCombine(generatePolyLinearTerms(exp));
        }

        [[nodiscard]] koopa_ir::Value generateExp(Ref<Exp> exp)
        {
            const auto semanticType = m_semanticInfo->findExpType(exp);
            if (semanticType.has_value()
                && semanticType->kind == SemanticTypeKind::poly) {
                return generatePolyExpression(exp);
            }
            if (const auto constantValue
                = m_semanticInfo->findConstantValue(exp);
                constantValue.has_value()) {
                return koopa_ir::IntegerLiteral {
                    .sourcePos = {},
                    .value = *constantValue,
                };
            }

            const auto& parsedExp = exp(m_ast);
            return MATCH(parsedExp.kind) WITH(
                [&](Exp::Binary expAlt) -> koopa_ir::Value {
                    if (expAlt.op == BinaryOpKeyword::orOr
                        || expAlt.op == BinaryOpKeyword::andAnd) {
                        return generateBooleanAsInt(exp);
                    }
                    return generateBinaryExpValue(expAlt);
                },
                [&](Exp::Unary expAlt) -> koopa_ir::Value {
                    return generateUnaryExpValue(expAlt);
                },
                [&](Exp::Cast expAlt) -> koopa_ir::Value {
                    auto value = generateExp(expAlt.value);
                    const auto operandType
                        = m_semanticInfo->findExpType(expAlt.value);
                    if (!operandType.has_value()
                        || operandType->kind == SemanticTypeKind::array
                        || operandType->kind == SemanticTypeKind::voidType) {
                        return value;
                    }
                    const bool targetIsMint
                        = expAlt.targetType == BTypeKeyword::mintKeyword;
                    const bool targetIsInt
                        = expAlt.targetType == BTypeKeyword::intKeyword;
                    if (targetIsMint
                        && operandType->kind == SemanticTypeKind::integer) {
                        return emitConversion(
                            koopa_ir::ConversionOp::int2mint, std::move(value));
                    }
                    if (targetIsInt
                        && operandType->kind == SemanticTypeKind::mint) {
                        return emitConversion(
                            koopa_ir::ConversionOp::mint2int, std::move(value));
                    }
                    return value;
                },
                [&](Exp::Call expAlt) -> koopa_ir::Value {
                    const auto& symbol = requireSymbolForIdentifier(
                        expAlt.funcName, *m_semanticInfo,
                        "call target is missing a symbol binding");
                    const auto functionIt
                        = m_functionBySymbolId.find(symbol.m_id);
                    if (functionIt == m_functionBySymbolId.end()) {
                        throw std::runtime_error("call target is missing a "
                                                 "lowered function binding");
                    }
                    std::vector<koopa_ir::Value> args;
                    args.reserve(expAlt.params.size());
                    for (const auto arg_nn : expAlt.params) {
                        args.push_back(generateExp(arg_nn));
                    }
                    return emitCall(functionIt->second, std::move(args),
                        symbol.function().m_returnType.kind
                            != SemanticTypeKind::voidType);
                },
                [&](Exp::Slice expAlt) -> koopa_ir::Value {
                    (void)expAlt;
                    return generatePolyExpression(exp);
                },
                [&](Exp::Subscript expAlt) -> koopa_ir::Value {
                    return emitGetCoeff(
                        generateExp(expAlt.base), generateExp(expAlt.index));
                },
                [&](Exp::Ntt expAlt) -> koopa_ir::Value {
                    return emitUnaryPoly(
                        koopa_ir::UnaryPolyOp::ntt, generateExp(expAlt.value));
                },
                [&](Exp::Intt expAlt) -> koopa_ir::Value {
                    return emitUnaryPoly(
                        koopa_ir::UnaryPolyOp::intt, generateExp(expAlt.value));
                },
                [&](Exp::PvBinary expAlt) -> koopa_ir::Value {
                    koopa_ir::PvBinaryOp op = koopa_ir::PvBinaryOp::add;
                    switch (expAlt.op) {
                    case PvBinaryOpKeyword::add:
                        op = koopa_ir::PvBinaryOp::add;
                        break;
                    case PvBinaryOpKeyword::sub:
                        op = koopa_ir::PvBinaryOp::sub;
                        break;
                    case PvBinaryOpKeyword::mul:
                        op = koopa_ir::PvBinaryOp::mul;
                        break;
                    }
                    return emitPvBinary(
                        op, generateExp(expAlt.lhs), generateExp(expAlt.rhs));
                },
                [&](Exp::Combine expAlt) -> koopa_ir::Value {
                    std::vector<koopa_ir::CombineTerm> terms;
                    terms.reserve(expAlt.terms.size());
                    for (const auto& term : expAlt.terms) {
                        terms.push_back(koopa_ir::CombineTerm {
                            .value = generateExp(term.value),
                            .start = generateExp(term.start),
                            .end = term.end == nullptr
                                ? std::nullopt
                                : std::make_optional(
                                      generateExp(term.end.ref())),
                            .shift = generateExp(term.shift),
                            .scale = mintValueForExp(term.scale),
                        });
                    }
                    return emitCombine(std::move(terms));
                },
                [&](Exp::GetCoeff expAlt) -> koopa_ir::Value {
                    return emitGetCoeff(
                        generateExp(expAlt.value), generateExp(expAlt.index));
                },
                [&](Exp::PolyConstruct expAlt) -> koopa_ir::Value {
                    std::vector<koopa_ir::Value> elements;
                    elements.reserve(expAlt.elements.size());
                    for (const auto element : expAlt.elements) {
                        elements.push_back(mintValueForExp(element));
                    }
                    return emitPolyConstruct(std::move(elements));
                },
                [&](Exp::IntToMint expAlt) -> koopa_ir::Value {
                    return emitConversion(koopa_ir::ConversionOp::int2mint,
                        generateExp(expAlt.value));
                },
                [&](Exp::MintToInt expAlt) -> koopa_ir::Value {
                    return emitConversion(koopa_ir::ConversionOp::mint2int,
                        generateExp(expAlt.value));
                },
                [&](Exp::LVal expAlt) -> koopa_ir::Value {
                    const auto& lvalSymbol = requireSymbolForIdentifier(
                        expAlt.identifier, *m_semanticInfo,
                        "lvalue is missing a symbol binding");
                    if (lvalSymbol.isObject()
                        && lvalSymbol.object().m_type.isPoly()
                        && expAlt.indices.size() == 1) {
                        Exp::LVal baseLVal {
                            .identifier = expAlt.identifier,
                            .indices = {},
                        };
                        auto baseAddress = generateLValueAddress(baseLVal);
                        return emitGetCoeff(emitLoad(baseAddress.symbol),
                            generateExp(expAlt.indices.front()));
                    }
                    if (m_semanticInfo->findAlias(expAlt.identifier).has_value()
                        && expAlt.indices.empty()) {
                        return requireAliasValue(expAlt.identifier,
                            "scalar lvalue is missing an SSA alias value");
                    }
                    auto address = generateLValueAddress(expAlt);
                    const auto expType = m_semanticInfo->findExpType(exp);
                    if (expType.has_value() && expType->isArray()) {
                        if (address.pointeeType.isArray()) {
                            return emitGetElementPointer(address.symbol,
                                koopa_ir::IntegerLiteral {
                                    .sourcePos = {},
                                    .value = 0,
                                });
                        }
                        return address.symbol;
                    }
                    return emitLoad(address.symbol);
                },
                [&](Exp::Number expAlt) -> koopa_ir::Value {
                    return koopa_ir::IntegerLiteral {
                        .sourcePos = {},
                        .value = expAlt.value,
                    };
                });
        }

        [[nodiscard]] koopa_ir::Value generateBinaryExpValue(
            const Exp::Binary& binaryExp)
        {
            const auto lhsType = m_semanticInfo->findExpType(binaryExp.lhs);
            const auto rhsType = m_semanticInfo->findExpType(binaryExp.rhs);
            if ((lhsType.has_value() && lhsType->kind == SemanticTypeKind::poly)
                || (rhsType.has_value()
                    && rhsType->kind == SemanticTypeKind::poly)) {
                if (binaryExp.op == BinaryOpKeyword::star && lhsType.has_value()
                    && rhsType.has_value()
                    && lhsType->kind == SemanticTypeKind::poly
                    && rhsType->kind == SemanticTypeKind::poly) {
                    return generatePolyConvolution(binaryExp);
                }
                throw std::runtime_error("poly binary expression should lower "
                                         "through generatePolyExpression");
            }

            auto lhs = generateExp(binaryExp.lhs);
            auto rhs = generateExp(binaryExp.rhs);
            if (lhsType.has_value() && isMintType(*lhsType)) {
                switch (binaryExp.op) {
                case BinaryOpKeyword::plus:
                    return emitBinary(koopa_ir::BinaryOp::add, std::move(lhs),
                        std::move(rhs));
                case BinaryOpKeyword::minus:
                    return emitBinary(koopa_ir::BinaryOp::sub, std::move(lhs),
                        std::move(rhs));
                case BinaryOpKeyword::star:
                    return emitBinary(koopa_ir::BinaryOp::mul, std::move(lhs),
                        std::move(rhs));
                case BinaryOpKeyword::slash:
                    return emitBinary(koopa_ir::BinaryOp::div, std::move(lhs),
                        std::move(rhs));
                case BinaryOpKeyword::less:
                case BinaryOpKeyword::greater:
                case BinaryOpKeyword::lessEqual:
                case BinaryOpKeyword::greaterEqual:
                case BinaryOpKeyword::equal:
                case BinaryOpKeyword::notEqual:
                    lhs = emitConversion(
                        koopa_ir::ConversionOp::mint2int, std::move(lhs));
                    rhs = emitConversion(
                        koopa_ir::ConversionOp::mint2int, std::move(rhs));
                    break;
                case BinaryOpKeyword::percent:
                case BinaryOpKeyword::shl:
                case BinaryOpKeyword::sar:
                case BinaryOpKeyword::bitAnd:
                case BinaryOpKeyword::bitXor:
                case BinaryOpKeyword::bitOr:
                    throw std::runtime_error(
                        "mint bitwise and shift expressions should be rejected "
                        "semantically");
                case BinaryOpKeyword::andAnd:
                case BinaryOpKeyword::orOr:
                    throw std::runtime_error(
                        "short-circuit binary expression should lower through "
                        "boolean branching");
                }
            }
            return emitBinary(lowerBinaryOpToIr(binaryExp.op), std::move(lhs),
                std::move(rhs));
        }

        [[nodiscard]] koopa_ir::Value generateUnaryExpValue(
            const Exp::Unary& unaryExp)
        {
            auto operand = generateExp(unaryExp.lhs);
            const auto operandType = m_semanticInfo->findExpType(unaryExp.lhs);
            if (operandType.has_value() && isMintType(*operandType)) {
                switch (unaryExp.op) {
                case UnaryOpKeyword::plus:
                    return operand;
                case UnaryOpKeyword::minus:
                    return emitBinary(koopa_ir::BinaryOp::sub,
                        koopa_ir::IntegerLiteral {
                            .sourcePos = {}, .value = 0 },
                        std::move(operand));
                case UnaryOpKeyword::bang:
                    return emitBinary(koopa_ir::BinaryOp::eq,
                        koopa_ir::IntegerLiteral {
                            .sourcePos = {}, .value = 0 },
                        std::move(operand));
                case UnaryOpKeyword::tilde:
                    throw std::runtime_error("mint bitwise-not expressions "
                                             "should be rejected semantically");
                }
            }

            switch (unaryExp.op) {
            case UnaryOpKeyword::plus:
                return emitBinary(koopa_ir::BinaryOp::add,
                    koopa_ir::IntegerLiteral { .sourcePos = {}, .value = 0 },
                    std::move(operand));
            case UnaryOpKeyword::minus:
                return emitBinary(koopa_ir::BinaryOp::sub,
                    koopa_ir::IntegerLiteral { .sourcePos = {}, .value = 0 },
                    std::move(operand));
            case UnaryOpKeyword::bang:
                return emitBinary(koopa_ir::BinaryOp::eq,
                    koopa_ir::IntegerLiteral { .sourcePos = {}, .value = 0 },
                    std::move(operand));
            case UnaryOpKeyword::tilde:
                return emitBinary(koopa_ir::BinaryOp::bitXor,
                    std::move(operand),
                    koopa_ir::IntegerLiteral { .sourcePos = {}, .value = -1 });
            }
            throw std::runtime_error("unsupported unary operator");
        }

        [[nodiscard]] koopa_ir::Value generateBooleanAsInt(Ref<Exp> exp)
        {
            const std::string resultName = makeTempName(m_nextTempId);
            (void)emitAlloc(SemanticType::makeInteger(), resultName);
            auto zeroStoreRef = m_program->alloc<koopa_ir::StoreStmt>(
                koopa_ir::StoreStmt {
                    .sourcePos = {},
                    .value = koopa_ir::IntegerLiteral {
                        .sourcePos = {},
                        .value = 0,
                    },
                    .destination = makeIrSymbol(resultName),
                    .annotations = {},
                });
            pushStatement(zeroStoreRef);

            auto trueBlock = createBasicBlock("bool_true");
            auto falseBlock = createBasicBlock("bool_false");
            auto contBlock = createBasicBlock("bool_end");

            generateBooleanBranch(exp, trueBlock, falseBlock);

            m_currentBasicBlock = trueBlock;
            auto trueStoreRef = m_program->alloc<koopa_ir::StoreStmt>(
                koopa_ir::StoreStmt {
                    .sourcePos = {},
                    .value = koopa_ir::IntegerLiteral {
                        .sourcePos = {},
                        .value = 1,
                    },
                    .destination = makeIrSymbol(resultName),
                    .annotations = {},
                });
            pushStatement(trueStoreRef);
            setTerminator(m_program->alloc<koopa_ir::JumpTerminator>(
                koopa_ir::JumpTerminator {
                    .sourcePos = {},
                    .target = (*m_program)[contBlock].label,
                    .args = {},
                    .annotations = {},
                }));

            m_currentBasicBlock = falseBlock;
            auto falseStoreRef = m_program->alloc<koopa_ir::StoreStmt>(
                koopa_ir::StoreStmt {
                    .sourcePos = {},
                    .value = koopa_ir::IntegerLiteral {
                        .sourcePos = {},
                        .value = 0,
                    },
                    .destination = makeIrSymbol(resultName),
                    .annotations = {},
                });
            pushStatement(falseStoreRef);
            setTerminator(m_program->alloc<koopa_ir::JumpTerminator>(
                koopa_ir::JumpTerminator {
                    .sourcePos = {},
                    .target = (*m_program)[contBlock].label,
                    .args = {},
                    .annotations = {},
                }));

            m_currentBasicBlock = contBlock;
            return emitLoad(makeIrSymbol(resultName));
        }

        void generateBooleanBranch(Ref<Exp> exp,
            Ref<koopa_ir::BasicBlock> trueBlock,
            Ref<koopa_ir::BasicBlock> falseBlock)
        {
            if (const auto constantValue
                = m_semanticInfo->findConstantValue(exp);
                constantValue.has_value()) {
                setTerminator(m_program->alloc<koopa_ir::BranchTerminator>(
                    koopa_ir::BranchTerminator {
                        .sourcePos = {},
                        .condition = koopa_ir::IntegerLiteral {
                            .sourcePos = {},
                            .value = *constantValue,
                        },
                        .trueTarget = (*m_program)[trueBlock].label,
                        .trueArgs = {},
                        .falseTarget = (*m_program)[falseBlock].label,
                        .falseArgs = {},
                        .annotations = {},
                    }));
                return;
            }

            const auto& parsedExp = exp(m_ast);
            MATCH(parsedExp.kind)
            WITH(
                [&](Exp::Binary expAlt) {
                    if (expAlt.op == BinaryOpKeyword::orOr) {
                        generateLogicalOrBranch(expAlt, trueBlock, falseBlock);
                        return;
                    }
                    if (expAlt.op == BinaryOpKeyword::andAnd) {
                        generateLogicalAndBranch(expAlt, trueBlock, falseBlock);
                        return;
                    }

                    setTerminator(m_program->alloc<koopa_ir::BranchTerminator>(
                        koopa_ir::BranchTerminator {
                            .sourcePos = {},
                            .condition = generateBinaryExpValue(expAlt),
                            .trueTarget = (*m_program)[trueBlock].label,
                            .trueArgs = {},
                            .falseTarget = (*m_program)[falseBlock].label,
                            .falseArgs = {},
                            .annotations = {},
                        }));
                },
                [&](const auto&) {
                    setTerminator(m_program->alloc<koopa_ir::BranchTerminator>(
                        koopa_ir::BranchTerminator {
                            .sourcePos = {},
                            .condition = generateExp(exp),
                            .trueTarget = (*m_program)[trueBlock].label,
                            .trueArgs = {},
                            .falseTarget = (*m_program)[falseBlock].label,
                            .falseArgs = {},
                            .annotations = {},
                        }));
                });
        }

        void generateLogicalOrBranch(const Exp::Binary& binaryExp,
            Ref<koopa_ir::BasicBlock> trueBlock,
            Ref<koopa_ir::BasicBlock> falseBlock)
        {
            const auto nextOperandBlock = createBasicBlock("lor_rhs");
            generateBooleanBranch(binaryExp.lhs, trueBlock, nextOperandBlock);
            m_currentBasicBlock = nextOperandBlock;
            generateBooleanBranch(binaryExp.rhs, trueBlock, falseBlock);
        }

        void generateLogicalAndBranch(const Exp::Binary& binaryExp,
            Ref<koopa_ir::BasicBlock> trueBlock,
            Ref<koopa_ir::BasicBlock> falseBlock)
        {
            const auto nextOperandBlock = createBasicBlock("land_rhs");
            generateBooleanBranch(binaryExp.lhs, nextOperandBlock, falseBlock);
            m_currentBasicBlock = nextOperandBlock;
            generateBooleanBranch(binaryExp.rhs, trueBlock, falseBlock);
        }

        void generateLocalArrayInitializer(const koopa_ir::Symbol& address,
            const SemanticType& type, const std::vector<Ptr<Exp>>& scalarExprs,
            size_t& nextScalarIndex)
        {
            if (!type.isArray()) {
                koopa_ir::Value initValue = koopa_ir::IntegerLiteral {
                    .sourcePos = {},
                    .value = 0,
                };
                if (nextScalarIndex < scalarExprs.size()) {
                    const auto exp_nn = scalarExprs[nextScalarIndex++];
                    if (exp_nn) {
                        initValue = generateExp(exp_nn.ref());
                    }
                }
                auto storeRef = m_program->alloc<koopa_ir::StoreStmt>(
                    koopa_ir::StoreStmt {
                        .sourcePos = {},
                        .value = toStoreValue(initValue),
                        .destination = address,
                        .annotations = {},
                    });
                pushStatement(storeRef);
                return;
            }

            if (type.m_elementType == nullptr) {
                throw std::runtime_error("array type is missing element type");
            }

            for (int32_t i = 0; i < type.m_arrayLength; ++i) {
                const auto elementAddress
                    = requireSymbolValue(emitGetElementPointer(address,
                                             koopa_ir::IntegerLiteral {
                                                 .sourcePos = {},
                                                 .value = i,
                                             }),
                        "array element address should lower to a symbol");
                generateLocalArrayInitializer(elementAddress,
                    *type.m_elementType, scalarExprs, nextScalarIndex);
            }
        }

        [[nodiscard]] IrAddress generateLValueAddress(const Exp::LVal& lVal)
        {
            const auto& symbol = requireSymbolForIdentifier(lVal.identifier,
                *m_semanticInfo, "lvalue is missing a symbol binding");
            const auto storageIt = m_storageBySymbolId.find(symbol.m_id);
            if (storageIt == m_storageBySymbolId.end()) {
                throw std::runtime_error("lvalue references undefined storage");
            }

            auto address = makeIrSymbol(storageIt->second);
            auto currentType = symbol.object().m_type;
            auto pointeeType = currentType;
            bool indexesDecayedArrayParameter = false;
            if (currentType.isArray() && currentType.m_arrayLength == -1) {
                address = requireSymbolValue(emitLoad(address),
                    "decayed array parameter load should produce a symbol");
                if (currentType.m_elementType == nullptr) {
                    throw std::runtime_error(
                        "array parameter type is missing element type");
                }
                pointeeType = *currentType.m_elementType;
                indexesDecayedArrayParameter = true;
            }

            for (const auto index_nn : lVal.indices) {
                auto indexValue = generateExp(index_nn);
                if (indexesDecayedArrayParameter) {
                    address = requireSymbolValue(
                        emitGetPointer(address, std::move(indexValue)),
                        "pointer arithmetic should produce a symbol");
                    indexesDecayedArrayParameter = false;
                } else if (pointeeType.isArray()) {
                    address = requireSymbolValue(
                        emitGetElementPointer(address, std::move(indexValue)),
                        "array element address should produce a symbol");
                    if (pointeeType.m_elementType == nullptr) {
                        throw std::runtime_error(
                            "array pointee type is missing element type");
                    }
                    pointeeType = *pointeeType.m_elementType;
                } else {
                    address = requireSymbolValue(
                        emitGetPointer(address, std::move(indexValue)),
                        "pointer arithmetic should produce a symbol");
                }

                if (currentType.isArray()
                    && currentType.m_elementType != nullptr) {
                    currentType = *currentType.m_elementType;
                }
            }

            return IrAddress {
                .symbol = address,
                .pointeeType = pointeeType,
            };
        }
    };

    [[nodiscard]] Ref<koopa_ir::FunctionDecl> createExternalFunctionDeclIr(
        koopa_ir::Program& program, const SemanticSymbol& symbol)
    {
        const auto& funcInfo = symbol.function();
        std::vector<koopa_ir::Type> paramTypes;
        paramTypes.reserve(funcInfo.m_paramTypes.size());
        for (const auto& paramType : funcInfo.m_paramTypes) {
            paramTypes.push_back(lowerSemanticTypeToIr(program, paramType));
        }
        return program.alloc<koopa_ir::FunctionDecl>(koopa_ir::FunctionDecl {
            .sourcePos = {},
            .name = makeIrSymbol(makeFunctionName(symbol.name)),
            .paramTypes = std::move(paramTypes),
            .returnType
            = lowerOptionalReturnTypeToIr(program, funcInfo.m_returnType),
            .annotations = {},
        });
    }

    void pruneUnreachableBlocks(koopa_ir::Program& program,
        Ref<koopa_ir::FunctionDef> functionRef,
        Ref<koopa_ir::BasicBlock> entryBlockRef)
    {
        auto& function = program[functionRef];
        std::unordered_map<std::string, Ref<koopa_ir::BasicBlock>> blockByLabel;
        blockByLabel.reserve(function.blocks.size());
        for (const auto blockRef : function.blocks) {
            blockByLabel.insert_or_assign(
                program[blockRef].label.spelling, blockRef);
        }

        std::vector<Ref<koopa_ir::BasicBlock>> worklist { entryBlockRef };
        std::unordered_set<Ref<koopa_ir::BasicBlock>> reachableBlocks;
        reachableBlocks.reserve(function.blocks.size());
        while (!worklist.empty()) {
            const auto blockRef = worklist.back();
            worklist.pop_back();
            if (!reachableBlocks.insert(blockRef).second) {
                continue;
            }

            MATCH(program[blockRef].terminator)
            WITH(
                [&](Ref<koopa_ir::BranchTerminator> terminatorRef) {
                    const auto& terminator = program[terminatorRef];
                    if (const auto trueIt
                        = blockByLabel.find(terminator.trueTarget.spelling);
                        trueIt != blockByLabel.end()) {
                        worklist.push_back(trueIt->second);
                    }
                    if (const auto falseIt
                        = blockByLabel.find(terminator.falseTarget.spelling);
                        falseIt != blockByLabel.end()) {
                        worklist.push_back(falseIt->second);
                    }
                },
                [&](Ref<koopa_ir::JumpTerminator> terminatorRef) {
                    const auto& terminator = program[terminatorRef];
                    if (const auto targetIt
                        = blockByLabel.find(terminator.target.spelling);
                        targetIt != blockByLabel.end()) {
                        worklist.push_back(targetIt->second);
                    }
                },
                [&](Ref<koopa_ir::ReturnTerminator>) {});
        }

        std::erase_if(function.blocks, [&](Ref<koopa_ir::BasicBlock> blockRef) {
            // Always keep the synthesized %end block (default return guard),
            // even if it's unreachable from entry (e.g., function returns
            // early).
            if (program[blockRef].label.spelling == "%end") {
                return false;
            }
            return !reachableBlocks.contains(blockRef);
        });
    }

    [[nodiscard]] Ref<koopa_ir::FunctionDef> createFunctionDefIr(
        koopa_ir::Program& program, const AST& ast, Ptr<FuncDef> funcDef,
        const SemanticInfo& semanticInfo)
    {
        const auto& parsedFuncDef = funcDef(ast);
        const auto& identifier = parsedFuncDef.identifier(ast);
        const auto& symbol
            = requireSymbolForIdentifier(parsedFuncDef.identifier, semanticInfo,
                "function definition is missing a symbol binding");
        const auto& funcInfo = symbol.function();
        std::vector<Ref<koopa_ir::FunctionParameter>> params;
        params.reserve(funcInfo.m_paramTypes.size());
        for (size_t i = 0; i < funcInfo.m_paramTypes.size(); ++i) {
            params.push_back(program.alloc<koopa_ir::FunctionParameter>(
                koopa_ir::FunctionParameter {
                    .sourcePos = {},
                    .symbol = makeIrSymbol("%arg" + std::to_string(i)),
                    .type
                    = lowerSemanticTypeToIr(program, funcInfo.m_paramTypes[i]),
                    .annotations = {},
                }));
        }
        return program.alloc<koopa_ir::FunctionDef>(koopa_ir::FunctionDef {
            .sourcePos = {},
            .name = makeIrSymbol(makeFunctionName(identifier.name)),
            .params = std::move(params),
            .returnType
            = lowerOptionalReturnTypeToIr(program, funcInfo.m_returnType),
            .blocks = {},
            .annotations = {},
        });
    }

    void generateGlobalDeclIr(frontend::Decl decl, koopa_ir::Program& program,
        const AST& ast, const SemanticInfo& semanticInfo,
        std::unordered_map<int32_t, std::string>& globalStorageBySymbolId)
    {
        MATCH(decl)
        WITH(
            [&](Ptr<ConstDecl> declAlt) {
                const auto& constDecl = declAlt(ast);
                for (const auto constDef_nn : constDecl.constDef) {
                    const auto& constDef = constDef_nn(ast);
                    const auto& symbol = requireSymbolForIdentifier(
                        constDef.identifier, semanticInfo,
                        "global const is missing its symbol binding");
                    if (!symbol.isObject()) {
                        continue;
                    }
                    const auto& type = symbol.object().m_type;
                    if (!type.isArray()) {
                        continue;
                    }
                    auto scalarExprs = flattenArrayInitializer(
                        ast, constDef.constInitVal.ref(), type);
                    size_t nextScalarIndex = 0;
                    auto globalRef = program.alloc<koopa_ir::GlobalMemoryDef>(
                        koopa_ir::GlobalMemoryDef {
                            .sourcePos = {},
                            .name = makeIrSymbol(makeGlobalName(symbol.name)),
                            .allocType
                            = lowerSemanticTypeToIr(program, type, false),
                            .initializer
                            = generateGlobalInitializerToIr(program, type,
                                scalarExprs, nextScalarIndex, semanticInfo),
                            .annotations = {},
                        });
                    program.items.push_back(globalRef);
                    globalStorageBySymbolId[symbol.m_id]
                        = makeGlobalName(symbol.name);
                }
            },
            [&](Ptr<VarDecl> declAlt) {
                const auto& varDecl = declAlt(ast);
                for (const auto varDef_nn : varDecl.varDef) {
                    const auto& varDef = varDef_nn(ast);
                    const auto& symbol = requireSymbolForIdentifier(
                        varDef.identifier, semanticInfo,
                        "global variable is missing its symbol binding");
                    const auto& type = symbol.object().m_type;
                    koopa_ir::Initializer initValue = koopa_ir::ZeroInit {};
                    if (type.isArray() && varDef.initVal) {
                        auto scalarExprs = flattenArrayInitializer(
                            ast, varDef.initVal.ref(), type);
                        size_t nextScalarIndex = 0;
                        initValue = generateGlobalInitializerToIr(program, type,
                            scalarExprs, nextScalarIndex, semanticInfo);
                    } else if (varDef.initVal) {
                        const auto& initVal = varDef.initVal(ast);
                        MATCH(initVal.kind)
                        WITH(
                            [&](Ref<Exp> initAlt) {
                                const auto constantValue
                                    = semanticInfo.findConstantValue(initAlt);
                                if (!constantValue.has_value()) {
                                    throw std::runtime_error(
                                        "global variable initializer must be "
                                        "constant");
                                }
                                initValue = koopa_ir::IntegerLiteral {
                                    .sourcePos = {},
                                    .value = *constantValue,
                                };
                            },
                            [&](const auto&) {});
                    }
                    auto globalRef = program.alloc<koopa_ir::GlobalMemoryDef>(
                        koopa_ir::GlobalMemoryDef {
                            .sourcePos = {},
                            .name = makeIrSymbol(makeGlobalName(symbol.name)),
                            .allocType
                            = lowerSemanticTypeToIr(program, type, false),
                            .initializer = std::move(initValue),
                            .annotations = {},
                        });
                    program.items.push_back(globalRef);
                    globalStorageBySymbolId[symbol.m_id]
                        = makeGlobalName(symbol.name);
                }
            });
    }

    void generateFuncDefIr(koopa_ir::Program& program, const AST& ast,
        Ptr<FuncDef> funcDef, const SemanticInfo& semanticInfo,
        const std::unordered_map<int32_t, std::string>& globalStorageBySymbolId,
        const std::unordered_map<int32_t, std::string>& functionBySymbolId,
        Ref<koopa_ir::FunctionDef> functionRef)
    {
        const auto& parsedFuncDef = funcDef(ast);
        const auto* controlFlow = semanticInfo.findControlFlow(funcDef.ref());
        if (controlFlow == nullptr) {
            throw std::runtime_error(
                "function definition is missing semantic control flow");
        }

        const auto& symbol
            = requireSymbolForIdentifier(parsedFuncDef.identifier, semanticInfo,
                "function definition is missing a symbol binding");
        const auto* ssa = semanticInfo.findSSA(funcDef.ref());
        if (ssa == nullptr) {
            throw std::runtime_error(
                "function definition is missing semantic SSA");
        }

        auto placeholderRef = program.alloc<koopa_ir::ReturnTerminator>(
            koopa_ir::ReturnTerminator {
                .sourcePos = {},
                .value = std::nullopt,
                .annotations = {},
            });
        IrFunctionGenerator state {
            .m_ast = ast,
            .m_semanticInfo = &semanticInfo,
            .m_program = &program,
            .m_function = functionRef,
            .m_currentBasicBlock
            = program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                .sourcePos = {},
                .label = makeIrSymbol("%placeholder"),
                .params = {},
                .statements = {},
                .terminator = placeholderRef,
                .annotations = {},
            }),
            .m_storageBySymbolId = globalStorageBySymbolId,
            .m_ssa = ssa,
            .m_functionBySymbolId = functionBySymbolId,
            .m_functionReturnType = symbol.function().m_returnType,
        };

        auto& function = program[functionRef];
        function.blocks.clear();
        const auto& controlFlowArena = semanticInfo.controlFlowArena();
        for (const auto semanticBlockRef : controlFlow->blocks) {
            const auto& semanticBlock = semanticBlockRef(controlFlowArena);
            auto blockRef
                = program.alloc<koopa_ir::BasicBlock>(koopa_ir::BasicBlock {
                    .sourcePos = {},
                    .label = makeIrSymbol("%" + semanticBlock.nameHint),
                    .params = {},
                    .statements = {},
                    .terminator = placeholderRef,
                    .annotations = {},
                });
            const auto ssaBlockIt
                = ssa->m_blockInfoByBlock.find(semanticBlockRef);
            if (ssaBlockIt == ssa->m_blockInfoByBlock.end()) {
                throw std::runtime_error(
                    "semantic basic block is missing SSA info");
            }
            auto& block = program[blockRef];
            for (size_t i = 0; i < ssaBlockIt->second.m_params.size(); ++i) {
                const auto& param = ssaBlockIt->second.m_params[i];
                const auto* blockParamSymbol
                    = semanticInfo.findSymbolById(param.m_symbolId);
                if (blockParamSymbol == nullptr
                    || !blockParamSymbol->isObject()) {
                    throw std::runtime_error(
                        "basic block parameter is missing object type");
                }
                block.params.push_back(program.alloc<koopa_ir::BlockParameter>(
                    koopa_ir::BlockParameter {
                        .sourcePos = {},
                        .symbol = makeIrSymbol(
                            block.label.spelling + "_arg" + std::to_string(i)),
                        .type = lowerSemanticTypeToIr(
                            program, blockParamSymbol->object().m_type, false),
                        .annotations = {},
                    }));
            }
            state.m_basicBlockBySemanticBlock.insert_or_assign(
                semanticBlockRef, blockRef);
            function.blocks.push_back(blockRef);
        }

        const auto entryBlockRef
            = state.m_basicBlockBySemanticBlock.at(controlFlow->entryBlock);
        state.m_currentBasicBlock = entryBlockRef;
        for (size_t i = 0; i < parsedFuncDef.funcFParams.size(); ++i) {
            const auto& funcFParam = parsedFuncDef.funcFParams[i];
            const auto& paramSymbol = requireSymbolForIdentifier(
                funcFParam.identifier, semanticInfo,
                "function parameter is missing a symbol binding");
            if (semanticInfo.findAlias(funcFParam.identifier).has_value()) {
                state.bindAlias(
                    funcFParam.identifier, program[function.params[i]].symbol);
                continue;
            }
            const std::string allocName
                = state.makeUniqueLocalName(paramSymbol);
            (void)state.emitAlloc(paramSymbol.object().m_type, allocName);
            auto storeRef
                = program.alloc<koopa_ir::StoreStmt>(koopa_ir::StoreStmt {
                    .sourcePos = {},
                    .value = program[function.params[i]].symbol,
                    .destination = makeIrSymbol(allocName),
                    .annotations = {},
                });
            state.pushStatement(storeRef);
            state.m_storageBySymbolId[paramSymbol.m_id] = allocName;
        }

        for (const auto semanticBlockRef : controlFlow->blocks) {
            state.m_currentBasicBlock
                = state.m_basicBlockBySemanticBlock.at(semanticBlockRef);
            state.generateSemanticBlock(semanticBlockRef);
        }

        pruneUnreachableBlocks(program, functionRef, entryBlockRef);
    }

    std::unique_ptr<koopa_ir::Program> generateIrProgram(const AST& ast,
        Ptr<CompUnit> compUnit, const SemanticInfo& semanticInfo)
    {
        auto program = std::make_unique<koopa_ir::Program>();
        const auto& parsedCompUnit = compUnit(ast);
        std::unordered_map<int32_t, std::string> globalStorageBySymbolId;
        std::unordered_map<int32_t, std::string> functionBySymbolId;
        std::unordered_map<int32_t, size_t> symbolUseCount;
        std::unordered_set<int32_t> definedFunctionSymbolIds;

        for (const auto& [identifier_nn, symbolId] :
            semanticInfo.symbolIdByIdentifier()) {
            (void)identifier_nn;
            ++symbolUseCount[symbolId];
        }

        for (const auto topLevelItem : parsedCompUnit.topLevelItems) {
            MATCH(topLevelItem)
            WITH(
                [&](Ref<FuncDef> topLevelAlt) {
                    const auto& funcDef = topLevelAlt(ast);
                    if (funcDef.body == nullptr) {
                        return;
                    }
                    const auto* functionSymbol
                        = semanticInfo.findSymbol(funcDef.identifier);
                    if (functionSymbol == nullptr) {
                        throw std::runtime_error(
                            "function declaration missing semantic symbol "
                            "during lowering");
                    }
                    definedFunctionSymbolIds.insert(functionSymbol->m_id);
                },
                [&](const auto&) {});
        }

        for (const auto& [symbolId, symbol] : semanticInfo.symbolById()) {
            if (!symbol.isFunction()) {
                continue;
            }
            if (definedFunctionSymbolIds.contains(symbolId)) {
                continue;
            }
            if (symbolUseCount[symbolId] <= 1) {
                continue;
            }

            auto [it, inserted] = functionBySymbolId.try_emplace(
                symbolId, makeFunctionName(symbol.name));
            if (!inserted) {
                continue;
            }
            program->items.push_back(
                createExternalFunctionDeclIr(*program, symbol));
        }

        for (const auto topLevelItem : parsedCompUnit.topLevelItems) {
            MATCH(topLevelItem)
            WITH(
                [&](Decl topLevelAlt) {
                    generateGlobalDeclIr(topLevelAlt, *program, ast,
                        semanticInfo, globalStorageBySymbolId);
                },
                [&](Ptr<FuncDef> topLevelAlt) {
                    const auto& symbol = requireSymbolForIdentifier(
                        topLevelAlt(ast).identifier, semanticInfo,
                        "function declaration is missing a symbol binding");
                    auto [functionIt, inserted]
                        = functionBySymbolId.try_emplace(
                            symbol.m_id, makeFunctionName(symbol.name));
                    if (!inserted) {
                        return;
                    }
                    if (topLevelAlt(ast).body == nullptr) {
                        program->items.push_back(
                            createExternalFunctionDeclIr(*program, symbol));
                    }
                });
        }

        for (const auto topLevelItem : parsedCompUnit.topLevelItems) {
            MATCH(topLevelItem)
            WITH(
                [&](Ref<FuncDef> topLevelAlt) {
                    if (topLevelAlt(ast).body == nullptr) {
                        return;
                    }
                    auto functionRef = createFunctionDefIr(
                        *program, ast, topLevelAlt.ptr(), semanticInfo);
                    program->items.push_back(functionRef);
                    generateFuncDefIr(*program, ast, topLevelAlt.ptr(),
                        semanticInfo, globalStorageBySymbolId,
                        functionBySymbolId, functionRef);
                },
                [&](const auto&) {});
        }

        return program;
    }

} // namespace

} // namespace yesod::koopa
