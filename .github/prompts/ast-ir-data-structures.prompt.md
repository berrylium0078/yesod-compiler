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
3. State the ownership model for each linked field. Use `std::shared_ptr` or array indices when shared ownership is needed, and `std::unique_ptr` when ownership is exclusive.
4. Name alternatives and grouped inner types by meaning rather than by position. For example, `A ::= B C | D E` should yield meaning-based groups such as `Stmt ::= IfStmt | WhileStmt`.

### Mapping

1. Keep the design idiomatic modern C++ and favor clear ownership and value semantics.
2. Grouped inner types become nested `struct` definitions inside the parent type. For example:
   ```cpp
   struct Stmt {
       struct IfStmt { /* ... */ };
       struct WhileStmt { /* ... */ }; 
       /* ... */
   };
   ```
3. If a type has more than one constructor or production alternative, model it with `std::variant` instead of inheritance. For example, `Stmt` would have a field of type `std::variant<IfStmt, WhileStmt>` to represent the alternatives.
4. Map optional fields to `std::optional<T>`, or links that might be null.
5. Map fields that may appear arbitrary times to `std::vector<T>`.

## Output Format

Produce the answer in this order.

### 1. EBNF Type Definition

Rewrite the input into explicit EBNF-style type definitions.

### 2. Representation Decisions

List the key representation choices.

- Which fields are embeds
- Which fields are links, as well as the chosen form.
- Why each link is necessary, if any
- Where `std::optional`, `std::vector`, and `std::variant` are used

### 3. C++ Data Structure Definitions

Write the modern C++ type definitions, but prioritizes the following guidelines:
- Keep fields public unless encapsulation is needed.
- Use `struct` by default unless a `class` is justified.
- Use meaningful names for alternatives and grouped inner types.
- Preserve the grammar structure as provided in the input, and avoid transforming it into a simplified or alternative form unless explicitly required.

### 4. Short Rationale

Briefly explain how the C++ mapping corresponds to the EBNF.