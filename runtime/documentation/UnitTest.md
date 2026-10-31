# RTX Remix Unit Test

Remix Unit Tests serve the purpose of evaluating the smallest functional components of operators or algorithms within Remix.

## Guidelines for Introducing Unit Tests

1. Add your test code to `tests/rtx/unit` and register it in [Meson](../tests/rtx/unit/meson.build). Shared buffer-table tests also need registration in the standalone [CMake build](../tests/rtx/unit/CMakeLists.txt).
2. Write straightforward code that directly invokes the functions being tested. Check result for each test, and if all tests yield the intended outcomes, the main function should return 0. In cases where any test fails, return a nonzero exit code.

## Requirements

- The new unit tests should have minimal number of dependencies. If there are too many dependencies, do refactoring on the code that is tested, and make sure it's compatible to the unit test.
- It is essential that the unit tests offer complete coverage. This requires a carefully design of the test data to cover every possible branch and scenario. Make sure the expected result are correct. Moreover, for ease of debugging, put comments with details about each test.
- In cases where a test fails to deliver the expected outcome, it is important to do error handling and descriptive logging.

## Running Locally

For the standalone buffer-table tests on Linux and Windows, including portable
Windows executables from CI, follow the [native test guide](../native/README.md#portable-buffer-table-tests).

The full Windows unit suite uses Meson:

1. Open a PowerShell window in `runtime/` and run `.\build_dxvk.ps1 -BuildFlavour release -BuildSubDir _CompUnitTest_x64 -Backend ninja -EnableTracy false -BuildTarget unit_tests -InstallTags unit`
2. Then change directory into `_CompUnitTest_x64/` and run `meson test --verbose <test_name>`.  Leave off the test name to run all tests.
