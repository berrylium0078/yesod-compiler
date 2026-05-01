## Project Structure

Project name: the YESOD compiler.

- Build/config root:
  - CMakeLists.txt: Top-level CMake build definition.
- Build directories:
  - build/
- Source code (src/):
  - main.cpp: Main program entry point.
  - koopa/
    - mykoopa.*: Koopa IR generation helper library.
  - frontend/
    - lexer.*: Lexeme parser for SysY language.
    - ast.*: AST node definitions for SysY language. (Incomplete)
    - parser.*: Parser for SysY language, generates AST from tokens. (Incomplete)
- CTest Tests (test/):
  - currently empty.
- Documentation (doc/):
  - koopaIR.md: Documentation for Koopa IR text format.
  - sysy.md: Documentation for the SysY language.

## Coding Style

- Namespace and header guard naming follow directory structure. For example, `namespace yesod::frontend` and `_YESOD_FRONTEND_FILENAME_H_` for files in `src/frontend/filename.h`.
- Use camelCase for variable and function names.
  - Use `m_` prefix for member variables in classes.
  - Use `_nn` suffix to specify that a pointer-like variable will never be null during its lifespan.
  - Other part of the variable name should be in camelCase, e.g. `m_astNode_nn`.
  - Never wrap a pointer-like (including STL smart pointers -- they are all nullable) in `std::optional`.
- `std::variant` should always be consumed with `std::visit` and never with `std::get` or `std::get_if`.
- For none-primitive types, use const references when passing as parameters to avoid unnecessary copying.
- For base classes, declare a virtual destructor to ensure proper cleanup of derived classes.

## Workflow

### Planning

### Implementation

DO follow the coding style guidelines outlined above to maintain code consistency and readability.

### Build & Test

See `.github/prompts/build-and-test.prompt.md` for instructions on building the project and running tests.

### Code Review

### Documentation

