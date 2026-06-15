#ifndef _YESOD_TEST_KOOPA_KOOPA_INTERPRETER_H_
#define _YESOD_TEST_KOOPA_KOOPA_INTERPRETER_H_

#include <iosfwd>
#include <stop_token>
#include <string>

#include "koopa/ir.h"

namespace yesod::test_support::koopa::interpreter {

namespace koopa_ir = yesod::koopa::ir;

enum class ExecuteStatus {
    normal,
    stopped,
    arrayOutOfBounds,
    divisionByZero,
    unsupported,
    runtimeError,
};

struct ExecuteResult {
    ExecuteStatus status = ExecuteStatus::normal;
    int32_t returnValue = 0;
    std::string message;
};

[[nodiscard]] const char* toString(ExecuteStatus status);

[[nodiscard]] ExecuteResult execute(const koopa_ir::Program& program,
    std::istream& is, std::ostream& os, std::stop_token stopToken);

} // namespace yesod::test_support::koopa::interpreter

#endif // _YESOD_TEST_KOOPA_KOOPA_INTERPRETER_H_
