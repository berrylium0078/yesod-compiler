#ifndef _YESOD_FRONTEND_SEMANTIC_H_
#define _YESOD_FRONTEND_SEMANTIC_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "utils.h"
#include "frontend/ast.h"
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
        return SemanticType { };
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
    std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>> m_loopByBreakStmt;
    std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>>
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
    [[nodiscard]] std::optional<Ref<WhileStmt>> findLoop(
        Ref<T> node) const
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

    Ref<CompUnit> root() { return m_root.ref(); }
};

class SemanticSymbolResolver : public AstVisitor {
  public:
    explicit SemanticSymbolResolver(const AST& ast);

    void analyze(Ref<CompUnit> compUnit);

    [[nodiscard]] const std::unordered_map<Ref<Identifier>, SemanticSymbol>&
    symbolsByIdentifier() const;
    [[nodiscard]] const std::vector<SemanticDiagnostic>& diagnostics() const;
    [[nodiscard]] const SemanticSymbol* findSymbol(
        Ref<Identifier> identifier) const;
    [[nodiscard]] bool hasDeclaration(int32_t symbolId) const;

  protected:
    void visitCompUnit(Ref<CompUnit> compUnit) override;
    void visitFuncDef(Ref<FuncDef> funcDef) override;
    void visitBlock(Ref<Block> block) override;
    void visitConstDecl(Ref<ConstDecl> constDecl) override;
    void visitVarDecl(Ref<VarDecl> varDecl) override;
    void visitCallExp(const Exp& exp, const Exp::Call& call) override;
    void visitLValExp(const Exp& exp, const Exp::LVal& lVal) override;

  private:
    using Scope = std::unordered_map<std::string, Ref<Identifier>>;

    void declareFuncDef(Ref<FuncDef> funcDef);
    void pushScope();
    void popScope();
    [[nodiscard]] bool defineSymbol(
        const std::string& name, Ref<Identifier> identifier);
    [[nodiscard]] std::optional<Ref<Identifier>> lookupSymbol(
        const std::string& name) const;
    [[nodiscard]] int32_t resolveIdentifier(Ref<Identifier> identifier);
    void bindSymbol(
        Ref<Identifier> identifier, const SemanticSymbol& symbol);
    [[nodiscard]] SemanticSymbol makePlaceholderSymbol(
        Ref<Identifier> identifier);
    [[nodiscard]] SemanticSymbol makeObjectSymbol(
        Ref<Identifier> identifier, bool isConst);
    [[nodiscard]] SemanticSymbol makeFunctionSymbol(
        Ref<Identifier> identifier);
    void recordDiagnostic(
        SemanticDiagnosticKind kind, int32_t offset, std::string message,
        SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::error);

    std::unordered_map<Ref<Identifier>, SemanticSymbol> m_symbolByIdentifier;
    std::vector<Scope> m_scopeStack;
    std::vector<SemanticDiagnostic> m_diagnostics;
    std::unordered_set<int32_t> m_declaredSymbolIds;
    std::unordered_set<int32_t> m_definedFunctionSymbolIds;
    int32_t m_nextSymbolId = 0;
};

class SemanticTypeAnalyzer : public AstVisitor {
  public:
    explicit SemanticTypeAnalyzer(
        const AST& ast, const SemanticSymbolResolver& symbolResolver);

    void analyze(Ref<CompUnit> compUnit);

    [[nodiscard]] const std::unordered_map<Ref<Exp>, SemanticExpInfo>&
    expInfoByExp() const;
    [[nodiscard]] const std::unordered_map<int32_t, SemanticSymbol>&
    symbolsById() const;
    [[nodiscard]] const std::vector<SemanticDiagnostic>& diagnostics() const;
    [[nodiscard]] const SemanticSymbol* findSymbolById(int32_t symbolId) const;

  protected:
    void visitCompUnit(Ref<CompUnit> compUnit) override;
    void visitFuncDef(Ref<FuncDef> funcDef) override;
    void visitConstDecl(Ref<ConstDecl> constDecl) override;
    void visitVarDecl(Ref<VarDecl> varDecl) override;
    void visitIfStmt(Ref<IfStmt> ifStmt) override;
    void visitWhileStmt(Ref<WhileStmt> whileStmt) override;
    void visitAssignStmt(Ref<AssignStmt> assignStmt) override;
    void visitExpStmt(Ref<ExpStmt> expStmt) override;
    void visitReturnStmt(Ref<ReturnStmt> returnStmt) override;
    void visitExp(Ref<Exp> exp) override;

  private:
    struct AnalyzedExp {
        SemanticType m_type = SemanticType::makeInteger();
        ExpType m_valueKind = ExpType::integer;
        bool m_isConstant = false;
        int32_t m_constantValue = 0;
    };

    void declareFuncDef(Ref<FuncDef> funcDef);
    [[nodiscard]] AnalyzedExp analyzeExp(Ref<Exp> exp);
    [[nodiscard]] AnalyzedExp analyzeBinaryExp(
        const Exp& exp, const Exp::Binary& binary);
    [[nodiscard]] AnalyzedExp analyzeUnaryExp(
        const Exp& exp, const Exp::Unary& unary);
    [[nodiscard]] AnalyzedExp analyzeCallExp(
        const Exp& exp, const Exp::Call& call);
    [[nodiscard]] AnalyzedExp analyzeLValExp(
        const Exp& exp, const Exp::LVal& lVal);
    [[nodiscard]] AnalyzedExp analyzeCondExp(Ref<Exp> exp);
    [[nodiscard]] SemanticType analyzeObjectType(
        const std::vector<Ref<Exp>>& dimensions, int32_t offset,
        bool allowUnsizedFirstDimension = false);
    [[nodiscard]] AnalyzedExp analyzeConstInitVal(
        Ref<ConstInitVal> constInitVal, const SemanticType& expectedType,
        bool isOutermost, size_t& nextIndex, bool& hasRemainingWarning);
    [[nodiscard]] AnalyzedExp analyzeConstInitSequence(
        const std::vector<Ref<ConstInitVal>>& values, size_t& nextValueIndex,
        const SemanticType& expectedType, bool& hasRemainingWarning);
    [[nodiscard]] AnalyzedExp analyzeInitVal(Ref<InitVal> initVal,
        const SemanticType& expectedType, bool isGlobal, bool isOutermost,
        size_t& nextIndex, bool& hasRemainingWarning);
    [[nodiscard]] AnalyzedExp analyzeInitSequence(
        const std::vector<Ref<InitVal>>& values, size_t& nextValueIndex,
        const SemanticType& expectedType, bool isGlobal,
        bool& hasRemainingWarning);
    [[nodiscard]] bool typesMatchForCall(
        const SemanticType& paramType, const SemanticType& argType) const;
    [[nodiscard]] const SemanticSymbol* resolvedSymbol(
        Ref<Identifier> identifier) const;
    [[nodiscard]] SemanticSymbol makeObjectSymbol(Ref<Identifier> identifier,
        bool isConst, bool hasConstantValue, int32_t constantValue,
        const SemanticType& type) const;
    [[nodiscard]] SemanticSymbol makeFunctionSymbol(
        Ref<Identifier> identifier, int32_t symbolId,
        const SemanticType& returnType,
        const std::vector<SemanticType>& paramTypes) const;
    [[nodiscard]] AnalyzedExp normalizeToArithmetic(
        AnalyzedExp analyzedExp) const;
    [[nodiscard]] AnalyzedExp normalizeToBoolean(AnalyzedExp analyzedExp) const;
    void recordExpFacts(Ref<Exp> exp, const AnalyzedExp& analyzedExp);
    void recordDiagnostic(
        SemanticDiagnosticKind kind, int32_t offset, std::string message,
        SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::error);
    [[nodiscard]] bool isGlobalSymbol(int32_t symbolId) const;

    const SemanticSymbolResolver& m_symbolResolver;
    std::unordered_map<Ref<Exp>, SemanticExpInfo> m_expInfoByExp;
    std::unordered_map<int32_t, SemanticSymbol> m_symbolById;
    std::vector<SemanticDiagnostic> m_diagnostics;
    std::optional<SemanticType> m_currentFuncReturnType;
    std::unordered_set<int32_t> m_definedFunctionSymbolIds;
    std::unordered_set<int32_t> m_globalSymbolIds;
};

class SemanticLoopBinder : public AstVisitor {
  public:
    explicit SemanticLoopBinder(const AST& ast);

    void analyze(Ref<CompUnit> compUnit);

    [[nodiscard]] const std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>>&
    loopByBreakStmt() const;
    [[nodiscard]] const std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>>&
    loopByContinueStmt() const;
    [[nodiscard]] const std::vector<SemanticDiagnostic>& diagnostics() const;

  protected:
    void visitWhileStmt(Ref<WhileStmt> whileStmt) override;
    void visitBreakStmt(Ref<BreakStmt> breakStmt) override;
    void visitContinueStmt(Ref<ContinueStmt> continueStmt) override;

  private:
    [[nodiscard]] std::optional<Ref<WhileStmt>> currentLoop() const;
    void recordDiagnostic(
        SemanticDiagnosticKind kind, int32_t offset, std::string message,
        SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::error);

    std::unordered_map<Ref<BreakStmt>, Ref<WhileStmt>> m_loopByBreakStmt;
    std::unordered_map<Ref<ContinueStmt>, Ref<WhileStmt>>
        m_loopByContinueStmt;
    std::vector<Ref<WhileStmt>> m_loopStack;
    std::vector<SemanticDiagnostic> m_diagnostics;
};

class SemanticAnalyzer {
  public:
    [[nodiscard]] SemanticOutput analyze(const AST &ast, Ref<CompUnit> compUnit);
};

} // namespace yesod::frontend

#endif
