---
description: This file describes how to test the compiler constructed in this project.
---

## expected behavior of the compiler

1. The compiler must not crash or hang indefinitely for any input. When you run the compiler in tests, use a timeout mechanism such as a 10-second limit so hangs fail quickly.
2. For inputs with grammar or semantic errors, compiler should not produce or overwrite the expected output file, exit with a non-zero status, and print an informative error message to stderr (including error type, location, and description).
3. For syntactically and semantically correct inputs, the compiler should exit with status 0 and produce the requested output file (Koopa IR or RISC-V assembly) that preserves the program's semantics. This can be checked by inspecting the output file directly, comparing it to a reference output, or compiling & running the output and verifying the runtime behavior matches expectations.

The syntax and semantics of the input language (SysY) are defined in the file `doc/sysy.md`.

## Manually Testing the Compiler

While manual testing can be helpful during debugging, automated unit tests with CTest are strongly recommended for systematic testing. See the "Unit test with CTest" section below for instructions on writing and running unit tests.

Firstly, create a test C file, for example `/tmp/hello.c`. For manual testing, ensure all test inputs, generated outputs, and other temporary artifacts created during testing are located in the `/tmp` directory.

In this devcontainer environment provided with this project, you can safely assume that all tools and environment variables mentioned below exist.

### Testing KoopaIR generation

Run the compiler with the `-koopa` option to generate Koopa IR.

1. Compile the SysY program to Koopa IR. If you have saved a SysY program in `hello.c`, run:

```sh
compiler -koopa hello.c -o hello.koopa
```

2. Inspect or compare the generated Koopa IR. You can review `hello.koopa` directly, or compare it to a reference output with `diff` or a similar tool.

3. Optionally validate the IR by compiling, linking, and running it:

```sh
koopac hello.koopa | llc --filetype=obj -o hello.o
clang hello.o -L$CDE_LIBRARY_PATH/native -lsysy -o hello
./hello
```

### Troubleshooting common issues during manual testing

- **Incorrect file paths:** Ensure input files exist and use absolute paths if unsure. Example: `ls -l /tmp/hello.c`.
- **Linker / library issues:** When linking, confirm `$CDE_LIBRARY_PATH` is set correctly and points to the expected library directories used by `clang`/`ld.lld`.
- **Compiler run failures:** If `compiler -koopa` or `compiler -riscv` exits non-zero, inspect stderr for error messages and do not assume an output file was produced. Check the exit status (`echo $?`) and the presence of the output file (for example `test -f hello.koopa && echo exists || echo missing`).

### Testing RISC-V Assembly generation

Run the compiler with the `-riscv` option to generate RISC-V assembly.

1. Compile the SysY program to RISC-V assembly. If you have saved a SysY program in `hello.c`, run:

```sh
compiler -riscv hello.c -o hello.S
```

2. Inspect or compare the generated assembly. You can review `hello.S` directly, or compare it to a reference output with `diff` or a similar tool.

3. Optionally validate the assembly by compiling, linking, and running it:

```sh
clang hello.S -c -o hello.o -target riscv32-unknown-linux-elf -march=rv32im -mabi=ilp32
ld.lld hello.o -L$CDE_LIBRARY_PATH/riscv32 -lsysy -o hello
qemu-riscv32-static hello
```

## Unit test with CTest

It is recommended to write unit tests for the compiler using CTest.

It is assumed that the current working directory is the project root, and the build directory is `build/`.

### Basic workflow

Configure the build directory and generate build files:
```sh
cmake -S . -B build
```
Build the project (and the test binaries):
```sh
cmake --build build -j $(nproc)
```
Run the tests:
```sh
cd build
ctest --test-dir build
```

Common ctest options
- -V            : verbose output for all tests
- --output-on-failure : show stdout/stderr for failing tests
- -j N          : run up to N tests in parallel
- -R <regex>    : run only tests matching regex
- -E <regex>    : exclude tests matching regex

Example: run tests in parallel, show failures:

```sh
ctest -j4 --output-on-failure
```

### Declaring tests in CMake

All test case source files should be placed under `test/` directory, and declared in `test/CMakeLists.txt` so that CTest can discover and run them.

Minimal example:

```cmake
enable_testing()

add_executable(test_compiler tests/test_hello.c)
target_link_libraries(test_compiler PRIVATE compiler_lib)

add_test(NAME compiler_hello COMMAND test_compiler)
set_tests_properties(compiler_hello PROPERTIES TIMEOUT 10)
```

If your tests invoke the built compiler binary directly, prefer using the target file path or the test executable target:

```cmake
add_executable(run_compiler_test tests/run_compiler_test.c)
add_test(NAME run_compiler COMMAND $<TARGET_FILE:run_compiler_test>
		 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests)
```

### Debugging failing unit tests
- Re-run a single failing test with verbose output:

```sh
ctest -R <test-name-regex> -V
```

- Re-run only the tests that failed in the last run:

```sh
ctest --rerun-failed
```

Tips
- Keep test inputs in the `test/` directory as the project expects.
- Use labels (set_tests_properties(... PROPERTIES LABELS "fast") ) to group and select tests.
- Set reasonable TIMEOUT and ENVIRONMENT properties for tests that run external tools (qemu, clang, llc).