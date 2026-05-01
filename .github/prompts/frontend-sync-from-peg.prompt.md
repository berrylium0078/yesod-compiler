# Synchronize Frontend With PEG Design

Use this prompt when you need to bring the frontend implementation and parser-oriented tests back into sync with `doc/sysy-peg.md`.

## Goal

Given the PEG grammar and recovery design documented in `doc/sysy-peg.md`, update the AST, the integrated scanner-plus-packrat-parser implementation, and the parser tests so the code matches the document and the repository conventions.

## Input

Additional task-specific scope, deltas, or constraints are:

```text
{{input}}
```

The authoritative frontend design inputs are:

- `doc/sysy-peg.md` for grammar, ordered choice, cuts, labels, and synchronization behavior
- `.github/copilot-instructions.md` for coding conventions
- `src/frontend/ast.*` for AST definitions
- `src/frontend/parser.*` for the integrated scanner and parser implementation

## Requirements

1. Treat `doc/sysy-peg.md` as the source of truth. Do not redesign the grammar or recovery strategy unless the document itself is internally inconsistent.
2. Assume `doc/sysy-peg.md` already exists. If it is missing or too incomplete to implement faithfully, stop and report the blocker instead of inventing the missing design.
3. Follow `.github/copilot-instructions.md` exactly for namespace, naming, header guards, ownership, and `std::variant` usage. Use `yesod::frontend` for frontend code.
4. Keep lexical scanning integrated inside the parser implementation. Do not create a separate lexer module unless an existing code boundary forces a minimal compatibility wrapper.
5. Implement or update one parse function for each PEG non-terminal that must be recognized by the frontend.
6. Use packrat parsing. Memoize by non-terminal and `int32_t` byte offset.
7. Each memoized parse result must record success or failure, the next `int32_t` byte offset, and the produced AST node or other typed parse result.
8. Use byte offsets as `int32_t` source positions throughout the parser and AST.
9. Each AST node stores only its starting byte offset for diagnostics. Do not store line or column on nodes.
10. Represent AST child substructures only with `std::shared_ptr`. Do not embed child AST substructures by value inside parent nodes.
11. Keep leaf-node payloads minimal:
    - identifiers store only `std::string`
    - number literals store only parsed `int32_t`
    - comment nodes and whitespace nodes, if represented at all, store no additional payload
    - operators and keywords are represented as `enum class`
12. When a production has multiple semantic alternatives, model it with `std::variant` instead of flattening into structs with unrelated optional fields.
13. Consume `std::variant` values with `std::visit`. Do not use `std::get` or `std::get_if`.
14. Preserve PEG ordered-choice behavior exactly, especially for branch-order-sensitive productions documented in `doc/sysy-peg.md`.
15. Implement recovery from the annotations in `doc/sysy-peg.md`. Cuts, labeled failures, and synchronization helpers must match the document rather than ad hoc parser behavior.
16. Before editing, compare `doc/sysy-peg.md` against the current AST, parser, and parser tests. Identify missing or stale non-terminals, node variants, token categories, recovery paths, and tests.
17. Keep the change set minimal but complete. Update AST definitions, parser code, parser-internal tokenization, tests, and build wiring only where synchronization requires it.
18. Add or update parser-oriented tests that cover:
    - representative valid parses
    - precedence and associativity-sensitive expressions
    - ordered-choice-sensitive productions
    - AST payload constraints for identifiers, number literals, keywords, and operators
    - integrated lexical handling at parser boundaries
    - malformed inputs and recovery behavior
19. Where recovery is observable, assert both the intended diagnostic category and that parsing makes forward progress to the documented synchronization point.
20. If comments or whitespace are skipped instead of emitted as nodes, document that choice and keep it consistent with `doc/sysy-peg.md` and the AST constraints above.
21. Update CMake or test registration only as needed to build and run the parser tests.
22. Fix synchronization at the root cause. Do not weaken tests to preserve parser behavior that contradicts `doc/sysy-peg.md`.
23. Validate the result with the narrowest available build and test steps for the touched frontend slice.

## Output Format

Produce the answer in this order.

### 1. Source Of Truth Check

Summarize the relevant assumptions taken from `doc/sysy-peg.md` and call out any ambiguity or incompleteness that affects implementation.

### 2. Synchronization Gap Summary

List the concrete mismatches between the documented PEG design and the current frontend implementation or tests.

Include:

- missing or stale non-terminals
- missing or stale AST node types or variants
- missing parser recovery behavior
- missing or stale parser tests
- any required build or test wiring changes

### 3. AST Mapping Decisions

Describe the AST representation decisions needed to match the PEG design.

Include:

- which productions map to `std::variant`
- where `std::shared_ptr` is required for child nodes
- how node start offsets are stored
- how identifier, literal, keyword, operator, comment, and whitespace payload rules are enforced

### 4. Implementation Changes

Describe the code changes you made to synchronize the frontend.

Include:

- AST changes in `src/frontend/ast.*`
- parser changes in `src/frontend/parser.*`
- integrated lexical scanning changes inside the parser, if any
- recovery and memoization changes
- any build or test registration changes

### 5. Test Changes

Summarize the parser tests you added or updated and what behavior each group covers.

### 6. Validation

Report the build and test steps you ran and the result of each step.

### 7. Remaining Issues

List only unresolved blockers, documented ambiguities, or intentionally deferred follow-up work.

## Additional Guidance

- Start from the controlling grammar and recovery rules in `doc/sysy-peg.md`, then step outward to the nearest AST and parser code that implements them.
- Preserve unaffected productions, AST shapes, and tests when they already match the document.
- Prefer parser-internal helpers that make memoization, ordered choice, and recovery explicit over generic control flow that hides PEG behavior.
- Keep diagnostic categories aligned with the document's labels or recovery intent. If the document names a missing delimiter or malformed construct, reuse that distinction in code and tests.
- If a production admits trivia but the implementation discards it, make that choice explicit in the AST and test expectations rather than leaving it implicit.
- When synchronization requires adding new parser tests from scratch, keep them focused on behavior implied directly by `doc/sysy-peg.md`.
- If the document and existing tests disagree, reconcile tests to the document after confirming the document is internally coherent.