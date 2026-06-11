## Project Structure

Project name: the YESOD compiler.

- Build/config root: 
  - CMakeLists.txt: Top-level CMake build definition.
- Build directories:
  - build/
- Source code (src/):
  - main.cpp: Main program entry point.
  - utils.h: Utility functions and definitions used across the project. Currently the Arena and variant matching utilities are defined here.
  - koopa/
    - ir.*: Koopa IR generation helper library.
    - ast_to_koopa.*: AST (with semantic information) to Koopa IR translation.
  - frontend/ 
    - ast.*: AST node definitions for SysY language, as well as a base visitor that traverses the AST.
    - parser.*: Parser for SysY language, generates AST from tokens.
    - semantic.*: Semantic analysis for SysY language, annotates AST with semantic information.
  - backend/
    - riscv.*: Code generation from Koopa IR to target RISC-V assembly.
- CTest Tests (test/): Tests for the project, organized by component and SysY syntax.
- Documentation (doc/):
  - koopaIR.md: Documentation for Koopa IR text format.
  - sysy.md: Documentation for the SysY language.
  - sysy-peg.md: Documentation for the SysY PEG parser design.

## Workflow

### Planning

Before implementation, outline the affected files, the data structures to add or modify, and any interface changes. Present this plan as a numbered list and wait for confirmation before writing code. If the user rejects or modifies the plan, incorporate their feedback and present an updated plan for confirmation before proceeding.

### Implementation

#### Naming Style

- Namespace and header guard naming follow directory structure. For example, `namespace yesod::frontend` and `_YESOD_FRONTEND_FILENAME_H_` for files in `src/frontend/filename.h`.

- Use camelCase for variable and function names.
  - Use `m_` prefix for member variables in classes. But for structs that contain only public member variables and no virtual functions, keep member names simple without prefixes. For example:
    - `class MyClass { int m_value; void doSomething(); };`
    - `struct Point { int x; int y; };`
  - Other part of the variable name should be in camelCase, e.g. `m_astNode`.
- Use PascalCase for class and struct names, e.g. `class MyClass { ... };` and `struct Point { ... };`.
- Use ALL_CAPS with underscores for constants and macros, e.g. `const int MAX_SIZE = 100;` and `#define MAX_SIZE 100`.

#### Memory Management

- Prefer Arena based allocation, with typed handles `Ref<T>` and `Ptr<T>` for links between nodes. Avoid raw pointers and heap allocation unless explicitly required by the caller.
 - Prefer non-nullable handles `Ref<T>` to enforce static type checking. Before converting a `Ptr<T>` to `Ref<T>`, perform a null check using assert.
- Do not use `std::optional<Ref<T>>` for optional links. Instead, use `Ptr<T>` to indicate a nullable reference.

#### Variant Consumption

To perform case-analysis on a `std::variant`, there are two ways:
- Use `std::get_if` only when exactly one variant type needs distinct handling and ALL other types are handled by a single identical fallback block. For example:
  ```cpp
  if (auto* assignStmt = std::get_if<AssignStmt>(&stmt)) {
      // Handle the AssignStmt case directly.
  } else {
      // Handle the non-AssignStmt cases in a default case.
  }
  ```
- Otherwise, use the `MATCH(variant) WITH(lambda list...)` macro defined in `utils.h`, each case is written as a lambda taking a single parameter of the type of that case, e.g. `[](const IfStmt& ifStmt) -> void { ... }`.
- Every `MATCH`/`WITH` block must be exhaustive. If a catch-all is needed, add a final lambda `[](const auto&) { /* ... */ }` with an explicit comment explaining what types it covers and why they are handled uniformly.

#### General guidelines

- For none-primitive types, use const references when passing as parameters to avoid unnecessary copying.
- For base classes, declare a virtual destructor to ensure proper cleanup of derived classes.
- Always write the return type of lambda expressions, explicitly.
- For control flow statements (if, for, while), always use braces `{}` even for single-statement bodies to improve readability and reduce errors in future modifications.
- For member functions that do not modify the state of the object, declare them as `const` to indicate their immutability and allow them to be called on const instances.
- For classes that manage resources, implement the Rule of Three/Five as appropriate to ensure proper resource management and avoid memory leaks.

### Build & Test

Basic usage:

```sh
cmake --build build -j 18
ctest --test-dir build --output-on-failure
```

If a build or test command fails or emits warnings, analyze the error output and propose a concise fix.
- If the proposed fix is trivial (for example, a one-file or ≤5-line change), you may apply it and rebuild.
- For larger or invasive fixes, present an implementation plan and wait for user confirmation before making code changes.
- If you fail to fix a build or test error after 3 attempts across chat turns, stop attempting fixes, output a summary of the failure, and ask the user for guidance.

If you want to add more tests, see `test/` directory for examples and `.github/instructions/compiler-test.instructions.md` for instructions on building the project and running tests.

### Code Format & Review

Use the command `clang-format -i <file>` to format modified code. If `clang-format` fails, output the command output and wait for the user to fix the formatting issue.

Check if the coding style guidelines are followed, and if the code is well-structured and documented. Provide constructive feedback on how to improve the code quality and maintainability.

### Documentation & Memory

Update the documentation in `doc/` directory as needed to reflect any changes in the project structure, coding style, or workflow. Ensure that the documentation is clear and comprehensive for future reference.

Maintain the implementation plan `plan.md` and the instruction files in `.github/instructions/` directory to keep track of the project progress and guidelines. Update these files as necessary to reflect any changes in the project structure, coding style, or workflow.