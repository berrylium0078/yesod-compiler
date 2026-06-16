#ifndef _YESOD_TEST_POLY_POLY_TEST_DRIVER_H_
#define _YESOD_TEST_POLY_POLY_TEST_DRIVER_H_

#include <istream>
#include <ostream>
#include <stop_token>
#include <string>

#include "interpreter_runner.h"

namespace yesod::test_support::poly {

[[nodiscard]] yesod::test_support::interpreter_runner::ExecuteResult
executeSource(const std::string& source, std::istream& is, std::ostream& os,
    std::stop_token stopToken);

} // namespace yesod::test_support::poly

#endif
