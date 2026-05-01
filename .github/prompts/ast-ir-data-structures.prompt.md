# AST/IR Data Structures In Modern C++

Use this prompt when you need to turn a language or IR type specification into modern C++ data structures.

## Goal

Given a set of type definitions, produce a modern C++ representation for the AST or IR data structures.

## Input

The input grammar or type sketch is:

```text
{{input}}
```

## Requirements

1. Draft the type definition explicitly in EBNF style before writing any C++.
2. Prefer embedded/value types over links. Use embedded fields such as `struct A { B b; };` unless a link is required.
3. For every field and nested type, state whether it is an embed or a link.
4. Clarify links explicitly. A link may be a raw pointer, smart pointer, handle, index, or another indirection mechanism, but only use one when it is actually necessary.
5. If a type has more than one constructor or production alternative, model it with `std::variant`.
6. For an alternative such as `A ::= B C | D E`, do not flatten it into a single struct with many optional fields. Instead, map it like this pattern:

   ```cpp
   using A = std::variant<MeaningfulAlternativeName1, MeaningfulAlternativeName2>;

   struct MeaningfulAlternativeName1 {
     B b;
     C c;
   };

   struct MeaningfulAlternativeName2 {
     D d;
     E e;
   };
   ```

   Choose alternative names based on semantics, not ordinal names such as `A1` or `A2`.
7. Translate groups into inner type definitions when that keeps the structure faithful and readable. For example, `A ::= B (C D)?` should become a nested struct with a meaningful name, then use `std::optional` for the grouped field.
8. Map optional fields to `std::optional<T>`.
9. Map fields that may appear arbitrary times to `std::vector<T>`.
10. Keep the design idiomatic modern C++ and favor clear ownership and value semantics.
11. If recursion forces indirection, explain why the link is necessary.
12. If naming is ambiguous, choose descriptive names and briefly justify them.

## Output Format

Produce the answer in this order.

### 1. EBNF Type Definition

Rewrite the input into explicit EBNF-style type definitions.

### 2. Representation Decisions

List the key representation choices.

- Which fields are embeds
- Which fields are links
- Why each link is necessary, if any
- Where `std::optional`, `std::vector`, and `std::variant` are used

### 3. C++ Data Structure Definitions

Write the modern C++ type definitions.

Guidelines:

- Keep fields public unless encapsulation is needed.
- Use `struct` by default unless a `class` is justified.
- Use meaningful names for alternatives and grouped inner types.
- Preserve the grammar structure instead of prematurely normalizing it away.
- Prefer direct members over heap allocation when possible.

### 4. Short Rationale

Briefly explain how the C++ mapping corresponds to the EBNF.

## Additional Guidance

- If the grammar is recursive, identify the exact recursion point and use links only there.
- If the grammar mixes sum types and product types, make that distinction explicit.
- Avoid unnecessary inheritance when `std::variant` models the sum type more directly.
- Do not silently invent semantics that are not implied by the input.
- If the input is incomplete, state the assumption before emitting code.