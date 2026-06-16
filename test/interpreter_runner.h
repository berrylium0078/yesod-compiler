#ifndef _YESOD_TEST_INTERPRETER_RUNNER_H_
#define _YESOD_TEST_INTERPRETER_RUNNER_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <stop_token>
#include <string>

namespace yesod::test_support::interpreter_runner {

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

struct RunnerConfig {
    std::filesystem::path defaultTestDirectory;
    std::string suiteName;
    int32_t defaultTimeoutMs = 2000;
};

using ExecuteOne = std::function<ExecuteResult(const std::string& source,
    std::istream& input, std::ostream& output, std::stop_token stopToken)>;

[[nodiscard]] const char* toString(ExecuteStatus status);

[[nodiscard]] int runTestMain(
    int argc, char** argv, const RunnerConfig& config, ExecuteOne executeOne);

} // namespace yesod::test_support::interpreter_runner

#endif // _YESOD_TEST_INTERPRETER_RUNNER_H_
