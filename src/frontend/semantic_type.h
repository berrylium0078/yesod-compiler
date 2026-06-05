#ifndef _YESOD_FRONTEND_SEMANTIC_TYPE_H_
#define _YESOD_FRONTEND_SEMANTIC_TYPE_H_

#include <memory>
#include <optional>
#include <unordered_set>

#include "frontend/ast.h"
#include "frontend/diagnostic.h"
#include "frontend/semantic_symbol.h"

namespace yesod::frontend {

YESOD_DECLARE_DIAGNOSTIC(NonConstantConstInitializerDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(NonConstantGlobalInitializerDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(ExcessInitializerElementsDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(AssignToConstDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(InvalidCallTargetDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(CallArityMismatchDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(TypeMismatchDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(ReturnTypeMismatchDiagnostic)
YESOD_DECLARE_DIAGNOSTIC(ShiftOperandOutOfRangeDiagnostic)


enum class ExpType {
    integer,
    mint,
    boolean,
    voidType,
    array,
    poly,
};

enum class SemanticTypeKind {
    integer,
    mint,
    boolean,
    voidType,
    array,
    poly,
};

struct SemanticType {
    SemanticTypeKind kind = SemanticTypeKind::integer;
    int32_t m_size = 4;
    int32_t m_arrayLength = 0;
    std::shared_ptr<SemanticType> m_elementType;

    [[nodiscard]] static SemanticType makeInteger() { return SemanticType { }; }

    [[nodiscard]] static SemanticType makeMint()
    {
        return SemanticType {
            .kind = SemanticTypeKind::mint,
            .m_size = 4,
            .m_arrayLength = 0,
            .m_elementType = nullptr,
        };
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

    [[nodiscard]] static SemanticType makePoly()
    {
        return SemanticType {
            .kind = SemanticTypeKind::poly,
            .m_size = 8,
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
            || kind == SemanticTypeKind::mint
            || kind == SemanticTypeKind::boolean;
    }

    [[nodiscard]] bool isPoly() const
    {
        return kind == SemanticTypeKind::poly;
    }

    [[nodiscard]] bool isNumeric() const
    {
        return kind == SemanticTypeKind::integer
            || kind == SemanticTypeKind::mint;
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
        case SemanticTypeKind::mint:
            return ExpType::mint;
        case SemanticTypeKind::boolean:
            return ExpType::boolean;
        case SemanticTypeKind::voidType:
            return ExpType::voidType;
        case SemanticTypeKind::array:
            return ExpType::array;
        case SemanticTypeKind::poly:
            return ExpType::poly;
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

struct SemanticSymbol {
    int32_t m_id;
    std::string name;
    struct ObjectInfo {
        bool m_isConst = false;
        std::optional<int32_t> constantValue;
        SemanticType m_type = SemanticType::makeInteger();
    };
    struct FunctionInfo {
        SemanticType m_returnType;
        std::vector<SemanticType> m_paramTypes;
    };
    std::variant<ObjectInfo, FunctionInfo> info;

    bool isObject() const { return std::holds_alternative<ObjectInfo>(info); }
    bool isFunction() const
    {
        return std::holds_alternative<FunctionInfo>(info);
    }
    template <class Self> auto&& function(this Self&& self)
    {
        return std::get<FunctionInfo>(self.info);
    }
    template <class Self> auto&& object(this Self&& self)
    {
        return std::get<ObjectInfo>(self.info);
    }
};

struct SemanticExpInfo {
    ExpType m_type = ExpType::integer;
    SemanticType m_semanticType = SemanticType::makeInteger();
    std::optional<int32_t> m_constantValue;
};

class SemanticTypeAnalysisResult {
public:
    explicit SemanticTypeAnalysisResult(const AST& ast);
    virtual ~SemanticTypeAnalysisResult() = default;
    [[nodiscard]] const std::unordered_map<Ref<Exp>, SemanticExpInfo>&
    expInfoByExp() const;
    [[nodiscard]] const std::unordered_map<int32_t, SemanticSymbol>&
    symbolsById() const;
    [[nodiscard]] const std::vector<std::unique_ptr<Diagnostic>>&
    diagnostics() const;
    [[nodiscard]] const SemanticSymbol* findSymbolById(int32_t symbolId) const;

protected:
    const AST& ast;
    std::unordered_map<Ref<Exp>, SemanticExpInfo> m_expInfoByExp;
    std::unordered_map<int32_t, SemanticSymbol> m_symbolById;
    std::vector<std::unique_ptr<Diagnostic>> m_diagnostics;
};

namespace detail {
    class SemanticTypeAnalyzerImpl;
}

class SemanticTypeAnalyzer {
    friend class SemanticAnalyzer;
public:
    explicit SemanticTypeAnalyzer(
        const AST& ast, const SemanticSymbolResolver& symbolResult);
    ~SemanticTypeAnalyzer();

    void analyze(Ref<CompUnit> compUnit);
    [[nodiscard]] const SemanticTypeAnalysisResult* operator->() const;

private:
    std::unique_ptr<detail::SemanticTypeAnalyzerImpl> m_impl;
};

} // namespace yesod::frontend 
#endif // _YESOD_FRONTEND_SEMANTIC_TYPE_H_