#include "interpreter_runner.h"
#include "poly/poly_test_driver.h"

#include <filesystem>

namespace {

namespace fs = std::filesystem;
namespace runner = yesod::test_support::interpreter_runner;

#ifndef YESOD_TEST_SOURCE_DIR
#define YESOD_TEST_SOURCE_DIR "."
#endif

} // namespace

int main(int argc, char** argv)
{
    const runner::RunnerConfig config {
        .defaultTestDirectory
        = fs::path(YESOD_TEST_SOURCE_DIR) / "testsuit-collection" / "lvX",
        .suiteName = "poly base extra",
        .defaultTimeoutMs = 2000,
    };
    return runner::runTestMain(
        argc, argv, config, yesod::test_support::poly::executeSource);
}
