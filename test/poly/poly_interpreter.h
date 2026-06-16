#ifndef _YESOD_TEST_POLY_POLY_INTERPRETER_H_
#define _YESOD_TEST_POLY_POLY_INTERPRETER_H_

#include <iosfwd>
#include <stop_token>

#include "frontend/ast.h"
#include "frontend/semantic.h"
#include "interpreter_runner.h"

namespace yesod::test_support::poly::interpreter {

using ExecuteResult = yesod::test_support::interpreter_runner::ExecuteResult;
using ExecuteStatus = yesod::test_support::interpreter_runner::ExecuteStatus;

[[nodiscard]] ExecuteResult execute(const frontend::AST& ast,
    frontend::Ref<frontend::CompUnit> root,
    const frontend::SemanticInfo& semanticInfo, std::istream& is,
    std::ostream& os, std::stop_token stopToken);

} // namespace yesod::test_support::poly::interpreter

#endif
