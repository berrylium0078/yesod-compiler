#ifndef _YESOD_FRONTEND_SEMANTIC_H_
#define _YESOD_FRONTEND_SEMANTIC_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontend/arena.h"
#include "frontend/ast.h"

namespace yesod::frontend {

enum class SemanticDiagnosticKind {
    useBeforeDefinition,
    doubleDefinition,
    nonConstantConstInitializer,
    nonConstantGlobalInitializer,
    excessInitializerElements,
    assignToConst,
    breakOutsideWhile,
    continueOutsideWhile,
    invalidCallTarget,
    callArityMismatch,
    typeMismatch,
    returnTypeMismatch,
};

enum class SemanticDiagnosticSeverity {
    error,
    warning,
};

struct SemanticDiagnostic {
    SemanticDiagnosticKind kind;
    int32_t m_offset;
    std::string m_message;
    SemanticDiagnosticSeverity m_severity = SemanticDiagnosticSeverity::error;
};

enum class ExpType {
    integer,
    boolean,
    voidType,
    array,
};

enum class SemanticTypeKind {
    integer,
    boolean,
    voidType,
    array,
};

struct SemanticType {
    SemanticTypeKind kind = SemanticTypeKind::integer;
    int32_t m_size = 4;
    int32_t m_arrayLength = 0;
    std::shared_ptr<SemanticType> m_elementType;

    [[nodiscard]] static SemanticType makeInteger()
    {
        return SemanticType {};
    }

    [[nodiscard]] static SemanticType makeBoolean()
    {
        return SemanticType {
            .kind = SemanticTypeKind::boolean,
            .m_size = 4,
            .m_arrayLength = 0,
            .m_elementType = nullptr,
        };
    }

    [[nodiscard]] static SemanticType makeVoid()
    {
        return SemanticType {
            .kind = SemanticTypeKind::voidType,
            .m_size = 0,
            .m_arrayLength = 0,
            .m_elementType = nullptr,
        };
    }

    [[nodiscard]] static SemanticType makeArray(
        const SemanticType& elementType, int32_t arrayLength)
    {
        return SemanticType {
            .kind = SemanticTypeKind::array,
            .m_size = arrayLength * elementType.m_size,
            .m_arrayLength = arrayLength,
            .m_elementType = std::make_shared<SemanticType>(elementType),
        };
    }

    [[nodiscard]] static SemanticType makeUnsizedArray(
        const SemanticType& elementType)
    {
        return SemanticType {
            .kind = SemanticTypeKind::array,
            .m_size = 0,
            .m_arrayLength = -1,
            .m_elementType = std::make_shared<SemanticType>(elementType),
        };
    }

    [[nodiscard]] bool isArray() const
    {
        return kind == SemanticTypeKind::array;
    }

    [[nodiscard]] bool isScalar() const
    {
        return kind == SemanticTypeKind::integer
            || kind == SemanticTypeKind::boolean;
    }

    [[nodiscard]] bool isVoid() const
    {
        return kind == SemanticTypeKind::voidType;
    }

    [[nodiscard]] ExpType valueKind() const
    {
        switch (kind) {
        case SemanticTypeKind::integer:
            return ExpType::integer;
        case SemanticTypeKind::boolean:
            return ExpType::boolean;
        case SemanticTypeKind::voidType:
            return ExpType::voidType;
        case SemanticTypeKind::array:
            return ExpType::array;
        }
        throw std::runtime_error("unsupported semantic type kind");
    }

    [[nodiscard]] bool operator==(const SemanticType& other) const
    {
        if (kind != other.kind || m_size != other.m_size
            || m_arrayLength != other.m_arrayLength) {
            return false;
        }
        if (m_elementType == nullptr || other.m_elementType == nullptr) {
            return m_elementType == nullptr && other.m_elementType == nullptr;
        }
        return *m_elementType == *other.m_elementType;
    }

    [[nodiscard]] bool operator!=(const SemanticType& other) const
    {
        return !(*this == other);
    }
};

enum class SemanticSymbolKind {
    object,
    function,
};

struct SemanticFunctionSignature {
    SemanticType m_returnType = SemanticType::makeInteger();
    std::vector<SemanticType> m_paramTypes;
};

struct SemanticSymbol {
    int32_t m_id;
    std::string name;
    SemanticSymbolKind kind = SemanticSymbolKind::object;
    bool m_isConst;
    bool m_hasConstantValue;
    int32_t m_constantValue;
    SemanticType m_type = SemanticType::makeInteger();
    SemanticFunctionSignature m_functionSignature;
};

struct SemanticExpInfo {
    ExpType m_type = ExpType::integer;
    SemanticType m_semanticType = SemanticType::makeInteger();
    bool m_hasConstantValue = false;
    int32_t m_constantValue = 0;
};

struct SemanticInfo {
    std::unordered_map<Ref<Identifier>, SemanticSymbol> m_symbolByIdentifier;
    std::unordered_map<Ref<Exp>, SemanticExpInfo> m_expInfoByExp;
    std::unordered_map<Ptr<BreakStmt>, Ptr<WhileStmt>> m_loopByBreakStmt;
    std::unordered_map<Ptr<ContinueStmt>, Ptr<WhileStmt>>
        m_loopByContinueStmt;

    [[nodiscard]] const SemanticSymbol* findSymbol(
        Ref<Identifier> identifier) const
    {
        const auto symbolIt = m_symbolByIdentifier.find(identifier);
        if (symbolIt == m_symbolByIdentifier.end()) {
            return nullptr;
        }
        return &symbolIt->second;
    }

    [[nodiscard]] std::optional<ExpType> findExpValueKind(
        Ref<Exp> node) const
    {
        const auto infoIt = m_expInfoByExp.find(node);
        if (infoIt == m_expInfoByExp.end()) {
            return std::nullopt;
        }
        return infoIt->second.m_type;
    }

    [[nodiscard]] std::optional<int32_t> findConstantValue(
        Ref<Exp> node) const
    {
        const auto infoIt = m_expInfoByExp.find(node);
        if (infoIt == m_expInfoByExp.end()
            || !infoIt->second.m_hasConstantValue) {
            return std::nullopt;
        }
        return infoIt->second.m_constantValue;
    }

    [[nodiscard]] std::optional<SemanticType> findExpType(
        Ref<Exp> node) const
    {
        const auto infoIt = m_expInfoByExp.find(node);
        if (infoIt == m_expInfoByExp.end()) {
            return std::nullopt;
        }
        return infoIt->second.m_semanticType;
    }

    template <typename T>
    [[nodiscard]] std::optional<Ptr<WhileStmt>> findLoop(
        Ptr<T> node) const
    {
        if constexpr (std::is_same_v<T, WhileStmt>) {
            return node;
        } else if constexpr (std::is_same_v<T, BreakStmt>) {
            const auto loopIt = m_loopByBreakStmt.find(node);
            if (loopIt == m_loopByBreakStmt.end()) {
                return std::nullopt;
            }
            return loopIt->second;
        } else if constexpr (std::is_same_v<T, ContinueStmt>) {
            const auto loopIt = m_loopByContinueStmt.find(node);
            if (loopIt == m_loopByContinueStmt.end()) {
                return std::nullopt;
            }
            return loopIt->second;
        } else {
            return std::nullopt;
        }
    }
};

struct SemanticOutput {
    AST m_ast;
    Ptr<CompUnit> m_root;
    SemanticInfo m_info;
    std::vector<SemanticDiagnostic> m_diagnostics;

    [[nodiscard]] bool success() const
    {
        if (!m_root) {
            return false;
        }
        for (const auto& diagnostic : m_diagnostics) {
            if (diagnostic.m_severity == SemanticDiagnosticSeverity::error) {
                return false;
            }
        }
        return true;
    }

    Ptr<CompUnit> root() { return m_root; }
};

class SemanticAnalyzer {
  public:
    [[nodiscard]] SemanticOutput analyze(AST ast, Ptr<CompUnit> compUnit_nn);

  private:
    struct AnalyzedExp {
        SemanticType m_type = SemanticType::makeInteger();
        ExpType m_valueKind = ExpType::integer;
        bool m_isConstant = false;
        int32_t m_constantValue = 0;
    };

    void analyzeCompUnit(Ptr<CompUnit> compUnit_nn);
    void declareFuncDef(Ptr<FuncDef> funcDef_nn);
    void analyzeFuncDef(Ptr<FuncDef> funcDef_nn);
    void analyzeBlock(Ptr<Block> block_nn);
    void analyzeBlockItemNode(BlockItem blockItemNode_nn);
    void analyzeDeclNode(Decl decl);
    void analyzeConstDecl(Ptr<ConstDecl> constDecl_nn);
    void declareVarDecl(Ptr<VarDecl> varDecl_nn);
    void analyzeVarDecl(Ptr<VarDecl> varDecl_nn);
    void analyzeStmtNode(Stmt stmt);
    void analyzeIfStmt(Ptr<IfStmt> ifStmt_nn);
    void analyzeWhileStmt(Ptr<WhileStmt> whileStmt_nn);
    void analyzeBreakStmt(Ptr<BreakStmt> breakStmt_nn);
    void analyzeContinueStmt(Ptr<ContinueStmt> continueStmt_nn);
    void analyzeAssignStmt(Ptr<AssignStmt> assignStmt_nn);
    void analyzeExpStmt(Ptr<ExpStmt> expStmt_nn);
    void analyzeReturnStmt(Ptr<ReturnStmt> returnStmt_nn);
    [[nodiscard]] AnalyzedExp analyzeExp(Ref<Exp> exp_nn);
    AnalyzedExp analyzeBinaryExp(const Exp &exp, const Exp::Binary &binary);
    AnalyzedExp analyzeUnaryExp(const Exp &exp, const Exp::Unary &unary);
    AnalyzedExp analyzeCallExp(const Exp &exp, const Exp::Call &call);
    AnalyzedExp analyzeLvalExp(const Exp &exp, const Exp::LVal &lval);
    [[nodiscard]] AnalyzedExp analyzeCondExp(Ref<Exp> exp_nn);
    [[nodiscard]] std::optional<Ref<Identifier>> lookupSymbol(
        const std::string& name) const;
    [[nodiscard]] int32_t resolveSymbol(Ref<Identifier> identifier_nn);
    [[nodiscard]] SemanticType analyzeObjectType(
        const std::vector<Ref<Exp>>& dimensions, int32_t offset,
        bool allowUnsizedFirstDimension = false);
    [[nodiscard]] AnalyzedExp analyzeConstInitVal(
        Ptr<ConstInitVal> constInitVal_nn, const SemanticType& expectedType,
        bool isOutermost, size_t& nextIndex, bool& hasRemainingWarning);
    [[nodiscard]] AnalyzedExp analyzeConstInitSequence(
        const std::vector<Ref<ConstInitVal>>& values, size_t& nextValueIndex,
        const SemanticType& expectedType, bool& hasRemainingWarning);
    [[nodiscard]] AnalyzedExp analyzeInitVal(Ptr<InitVal> initVal_nn,
        const SemanticType& expectedType, bool isGlobal, bool isOutermost,
        size_t& nextIndex, bool& hasRemainingWarning);
    [[nodiscard]] AnalyzedExp analyzeInitSequence(
        const std::vector<Ref<InitVal>>& values, size_t& nextValueIndex,
        const SemanticType& expectedType, bool isGlobal,
        bool& hasRemainingWarning);
    [[nodiscard]] bool typesMatchForCall(
        const SemanticType& paramType, const SemanticType& argType) const;
    [[nodiscard]] bool isScalarType(const SemanticType& type) const;
    [[nodiscard]] bool isArrayType(const SemanticType& type) const;
    [[nodiscard]] SemanticSymbol makePlaceholderSymbol(
        Ref<Identifier> identifier_nn);
    [[nodiscard]] SemanticSymbol makeObjectSymbol(
        Ref<Identifier> identifier_nn, bool isConst,
        bool hasConstantValue, int32_t constantValue,
        const SemanticType& type);
    [[nodiscard]] SemanticSymbol makeFunctionSymbol(
        Ref<Identifier> identifier_nn, const SemanticType& returnType,
        const std::vector<SemanticType>& paramTypes);
    template <typename T>
    [[nodiscard]] const T& node(Ptr<T> handle_nn, const char* message) const;
    [[nodiscard]] AnalyzedExp normalizeToArithmetic(AnalyzedExp analyzedExp);
    [[nodiscard]] AnalyzedExp normalizeToBoolean(AnalyzedExp analyzedExp);
    void bindSymbol(
        Ref<Identifier> identifier_nn, const SemanticSymbol& symbol);
    template <typename T>
    void bindLoop(Ptr<T> node_nn, Ptr<WhileStmt> whileStmt_nn);
    void recordExpFacts(Ref<Exp> exp_nn, const AnalyzedExp& analyzedExp);
    void pushScope();
    void popScope();
    [[nodiscard]] bool isGlobalScope() const;
    [[nodiscard]] bool defineSymbol(
        const std::string& name, Ref<Identifier> identifier_nn);
    [[nodiscard]] std::optional<Ptr<WhileStmt>> currentLoop() const;
    [[nodiscard]] SemanticType lowerFuncType(FuncTypeKeyword funcType) const;
    void recordDiagnostic(
        SemanticDiagnosticKind kind, int32_t offset, std::string message,
        SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::error);

    using Scope = std::unordered_map<std::string, Ref<Identifier>>;

    AST m_ast;
    Ptr<CompUnit> m_root_nn;
    SemanticInfo m_info;
    std::vector<Scope> m_scopeStack;
    std::vector<Ptr<WhileStmt>> m_loopStack;
    std::vector<SemanticDiagnostic> m_diagnostics;
    std::optional<SemanticType> m_currentFuncReturnType;
    std::unordered_set<int32_t> m_definedFunctionSymbolIds;
    int32_t m_nextSymbolId = 0;
};

template <typename T>
const T& SemanticAnalyzer::node(Ptr<T> handle_nn, const char* message) const
{
    if (!handle_nn) {
        throw std::runtime_error(message);
    }
    return handle_nn(m_ast);
}

inline SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::normalizeToArithmetic(
    AnalyzedExp analyzedExp)
{
    if (analyzedExp.m_valueKind == ExpType::voidType) {
        return analyzedExp;
    }

    if (analyzedExp.m_valueKind == ExpType::array) {
        return analyzedExp;
    }

    if (analyzedExp.m_valueKind == ExpType::integer) {
        return analyzedExp;
    }

    analyzedExp.m_type = SemanticType::makeInteger();
    analyzedExp.m_valueKind = ExpType::integer;
    if (analyzedExp.m_isConstant) {
        analyzedExp.m_constantValue = analyzedExp.m_constantValue != 0 ? 1 : 0;
    }
    return analyzedExp;
}

inline SemanticAnalyzer::AnalyzedExp SemanticAnalyzer::normalizeToBoolean(
    AnalyzedExp analyzedExp)
{
    if (analyzedExp.m_valueKind == ExpType::voidType) {
        return analyzedExp;
    }

    if (analyzedExp.m_valueKind == ExpType::array) {
        return analyzedExp;
    }

    if (analyzedExp.m_valueKind == ExpType::boolean) {
        return analyzedExp;
    }

    analyzedExp.m_type = SemanticType::makeBoolean();
    analyzedExp.m_valueKind = ExpType::boolean;
    if (analyzedExp.m_isConstant) {
        analyzedExp.m_constantValue = analyzedExp.m_constantValue != 0 ? 1 : 0;
    }
    return analyzedExp;
}

inline void SemanticAnalyzer::bindSymbol(
    Ref<Identifier> identifier_nn, const SemanticSymbol& symbol)
{
    m_info.m_symbolByIdentifier[identifier_nn] = symbol;
}

template <typename T>
void SemanticAnalyzer::bindLoop(
    Ptr<T> node_nn, Ptr<WhileStmt> whileStmt_nn)
{
    if constexpr (std::is_same_v<T, BreakStmt>) {
        m_info.m_loopByBreakStmt[node_nn] = whileStmt_nn;
    } else if constexpr (std::is_same_v<T, ContinueStmt>) {
        m_info.m_loopByContinueStmt[node_nn] = whileStmt_nn;
    }
}

inline void SemanticAnalyzer::recordExpFacts(
    Ref<Exp> node_nn, const AnalyzedExp& analyzedExp)
{
    m_info.m_expInfoByExp[node_nn] = SemanticExpInfo {
        .m_type = analyzedExp.m_valueKind,
        .m_semanticType = analyzedExp.m_type,
        .m_hasConstantValue = analyzedExp.m_isConstant,
        .m_constantValue = analyzedExp.m_constantValue,
    };
}

} // namespace yesod::frontend

#endif
