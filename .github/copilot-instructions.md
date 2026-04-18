## Project Structure

- Build/config root:
  - CMakeLists.txt: Top-level CMake build definition.
- Build directories:
  - build/
- Source code (src/):
  - main.cpp: Main program entry point.
  - koopa/
    - mykoopa.*: Koopa IR generation.
  - parser/
    - lexeme.*: Lexeme parser for SysY language.
- Tests (test/):
  - Contains CTest test cases for the compiler.
- Documentation (doc/):
  - koopaIR.md: Documentation for Koopa IR text format.
  - sysy.md: Documentation for the SysY language.

## Coding Style

- Use 4 spaces for indentation.
- Use camelCase for variable and function names.
- Use `m_` prefix for member variables in classes.
- Use `clang-format` for code formatting, the configuration is defined in `.clang-format`.

## Build

The default build directory is `build/`.

If you have created new files, reconfigure CMake: `cmake -B ${buildDir}`

Build the project: `cmake --build ${buildDir} -j $(nproc)`

