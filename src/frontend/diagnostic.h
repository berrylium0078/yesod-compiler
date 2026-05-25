#ifndef _YESOD_FRONTEND_DIAGNOSTIC_H_
#define _YESOD_FRONTEND_DIAGNOSTIC_H_

#include <string>
#include <cstdint>
#include <memory>

namespace yesod::frontend {
    
enum class DiagnosticSeverity {
    error,
    warning,
};

struct Diagnostic {
    int32_t offset = 0;
    std::string message;
    DiagnosticSeverity severity = DiagnosticSeverity::error;

    Diagnostic() = default;
    Diagnostic(int32_t offset, std::string message,
        DiagnosticSeverity severity = DiagnosticSeverity::error)
        : offset(offset)
        , message(std::move(message))
        , severity(severity)
    {
    }

    virtual ~Diagnostic() = default;
    [[nodiscard]] virtual std::unique_ptr<Diagnostic> clone() const = 0;
};
#define YESOD_DECLARE_DIAGNOSTIC(typeName)                                     \
    struct typeName final : Diagnostic {                                       \
        using Diagnostic::Diagnostic;                                          \
        [[nodiscard]] std::unique_ptr<Diagnostic> clone() const override       \
        {                                                                      \
            return std::make_unique<typeName>(*this);                          \
        }                                                                      \
    };

template <typename T>
[[nodiscard]] bool isDiagnostic(const Diagnostic& diagnostic)
{
    return dynamic_cast<const T*>(&diagnostic) != nullptr;
}
}

#endif // _YESOD_FRONTEND_DIAGNOSTIC_H_