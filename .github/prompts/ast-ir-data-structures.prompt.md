# AST/IR Data Structures In Modern C++

Use this prompt when you need to turn a language or IR type specification into modern C++ data structures.

## Goal

Given a set of type definitions, produce a modern C++ representation for the AST or IR data structures.

## Input

The input grammar or type sketch is:

```text
{{input}}
```

If the input is incomplete or has multiple plausible interpretations because key grammar, type, or relationship details are missing, respond with: "The input is incomplete or ambiguous. Please provide a complete and unambiguous grammar."

## Steps & Requirements

### Design

1. Draft the type definition explicitly in EBNF style.
2. Decide which fields are embedded directly and which require links. Use a link only when recursion forces it, or when a field must be shared or mutated independently of its parent.
3. Name alternatives and grouped inner types by meaning rather than by position. For example, `A ::= B C | D E` should yield meaning-based groups such as `Stmt ::= IfStmt | WhileStmt`.
4. Preserve the grammar structure as provided in the input unless a normalization choice is needed to match the target AST or IR design.

### Mapping

1. Keep the design idiomatic modern C++ and favor clear ownership and value semantics.
2. For ASTs in this project, use arena-backed storage with typed handles. `Ref<T>` is the non-null handle for required links and variant alternatives. `Ptr<T>` is the nullable handle for optional links. Model recursive links as `Ref<T>` or `Ptr<T>` values into the owning top-level `AST` container that stores the node arenas.
3. If a type has more than one constructor or production alternative, model it with `std::variant` instead of inheritance. For example, `Stmt` should use `std::variant<Ref<IfStmt>, Ref<WhileStmt>>`-style alternatives rather than a shared base class.
4. Use `std::vector<T>` for fields that may appear arbitrary times.
5. Use `std::optional<T>` only for optional non-node values or parser state. For optional AST links, prefer `Ptr<T>` over `std::optional<Ref<T>>`.
6. Do not introduce a shared AST base class just to carry ids or virtual dispatch when `std::variant` plus typed handles already encode node identity and alternatives.
7. Add `SourcePos` to each AST node type and keep ownership in the top-level `AST` object rather than inside individual nodes.
8. If the parser normalizes an optional source construct into a default child node, model that normalization explicitly with a flag plus a concrete child rather than with a nullable link.
9. Use `std::shared_ptr` or `std::unique_ptr` only when the caller explicitly asks for pointer-based ownership instead of arena handles.
10. A compact example of the intended pattern:

   ```cpp
   using Stmt = std::variant<Ref<IfStmt>, Ref<WhileStmt>, Ref<BreakStmt>,
       Ref<ContinueStmt>, Ref<AssignStmt>, Ref<Block>, Ref<ReturnStmt>,
       Ref<ExpStmt>>;

   struct IfStmt {
       SourcePos sourcePos;
       Ref<Exp> condition;
       Stmt thenBody;
       Stmt elseBody;
   };
   ```

### 1. EBNF Type Definition

Rewrite the input into explicit EBNF-style type definitions.

### 2. Representation Decisions

List the key representation choices.

- Which fields are embedded directly.
- Which fields are links, and whether they use `Ref<T>` or `Ptr<T>`.
- Why each link is necessary, if any.
- Where `std::optional`, `std::vector`, and `std::variant` are used.

### 3. C++ Data Structure Definitions

Write the modern C++ type definitions, but prioritize the following guidelines:
- Keep fields public unless encapsulation is needed.
- Use `struct` by default unless a `class` is justified.
- Use meaningful names for alternatives and grouped inner types.
- Preserve the grammar structure as provided in the input, and avoid transforming it into a simplified or alternative form unless explicitly required.
- For this project, add `SourcePos` to each node type and keep ownership in the top-level `AST` object rather than inside individual nodes.
- Use `Ref<T>` for required AST links and `Ptr<T>` for nullable AST links.

### 4. Short Rationale

Briefly explain how the C++ mapping corresponds to the EBNF.