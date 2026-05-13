# Design And Document PEG Grammar

Use this prompt when you need to turn an EBNF-style language grammar into a PEG grammar, or extend an existing PEG design as the language gains new features.

## Goal

Given a source grammar, produce or update a PEG-oriented grammar design that is suitable for implementation and review.

## Input

The source grammar and token-layer assumptions are:

```text
{{input}}
```

The current PEG design baseline is documented in `doc/sysy-peg.md` and should be treated as the existing design when the task is an incremental language extension.

## Requirements

1. Preserve the original EBNF structure unless a PEG constraint explicitly requires a redesign. Keep the PEG extension minimal, and preserve the existing token layer unless the input explicitly requires lexical changes.
2. Eliminate left recursion explicitly, and document every ordered-choice-sensitive rule. For transformed rules, show the rewritten PEG-friendly form and briefly explain why it preserves the intended parse. For rules like `Stmt` and `UnaryExp`, explain the chosen branch order and what each earlier alternative must consume or exclude.
3. Add recovery annotations only when they significantly improve parser diagnostics or structural recovery. Use only `^` for commit points, `Throw<Label>` for labeled failures, and named `Recover...` rules for panic-mode resynchronization. Keep labels compact and name them precisely, such as missing `)`, missing `]`, missing `}`, malformed statement head, or malformed declaration head.
4. Use explicit synchronization helpers that rely on the existing token layer, and prefer a small number of delimiters, separators, and statement or declaration boundaries instead of fine-grained recovery everywhere. Distinguish clearly between the plain PEG grammar, the recovery-annotated PEG grammar, and the rationale for recovery placement. If the input grammar is ambiguous or incomplete, suggest possible corrections and ask the user to choose one before proceeding.
5. For incremental feature additions, use `doc/sysy-peg.md` as the baseline. Preserve unaffected rules, describe only the necessary deltas and reordered choices, and explain how the new construct fits into precedence, statement forms, declaration forms, and synchronization strategy.

## Output Format

Produce the answer in this order.

### 1. Source Grammar Notes

Summarize the important assumptions about the source EBNF, token layer, and whether this is a fresh PEG design or an extension of the current design in `doc/sysy-peg.md`.

### 2. Baseline Delta

If this task adds or changes language features, summarize the delta against `doc/sysy-peg.md` before presenting the revised grammar.

Include:

- which existing rules stay unchanged
- which rules must be added, split, reordered, or refactored
- whether the token layer changes
- which existing recovery points remain valid and which need adjustment

If this task is a fresh design rather than an extension, state that no baseline delta applies.

### 3. Left-Recursion Elimination

List each left-recursive or PEG-incompatible pattern and rewrite it into PEG-friendly form.

For each rewritten rule, include:

- the original shape
- the rewritten PEG shape
- one short note on why the rewrite preserves the intended parse

### 4. Plain PEG Grammar

Write the PEG grammar without recovery annotations first.

Guidelines:

- Use PEG operators consistently.
- Keep token references explicit.
- Make ordered choices intentional and document-sensitive rules inline or immediately below the rule.
- For rules such as `Stmt` and `UnaryExp`, explain the chosen branch ordering.

### 5. Recovery-Annotated PEG Grammar

Extend the plain PEG grammar with the minimal recovery annotations.

Requirements for this section:

- Use `^` only where committing avoids cascading misclassification after a decisive prefix.
- Use `Throw<Label>` only for meaningful structural failures.
- Introduce named `Recover...` rules only where panic-mode synchronization has a clear token anchor.
- Add labeled recovery branches only to the productions that provide high-value diagnostics or structural recovery.

### 6. Recovery Inventory

Provide a compact table with:

- label name
- where it is thrown
- what it means
- which `Recover...` rule or synchronization tokens handle it, if any

### 7. Ordered-Choice Notes

Call out every branch-order-sensitive rule, especially `Stmt` and `UnaryExp`.

For each such rule, explain:

- why the chosen order is correct in PEG
- what would go wrong if the order were changed
- whether a cut is needed after any distinguishing prefix

### 8. Short Design Rationale

Briefly justify the final grammar shape, recovery scope, synchronization strategy, and, for feature additions, why the chosen delta is the minimal coherent update to the existing PEG design.

## Additional Guidance

- Prefer PEG rewrites of the form `Head Tail*` when removing expression-style left recursion.
- If multiple productions share a long prefix, factor or document them carefully so ordered choice remains readable.
- Keep recovery helpers named by their synchronization purpose, such as `RecoverToRParen`, `RecoverToStmtBoundary`, or `RecoverToDeclBoundary`.
- Reuse existing separators and delimiters from the token layer instead of inventing synthetic recovery tokens.
- Do not overuse cuts. A cut should follow a prefix that makes the intended construct effectively determined.
- Do not attach recovery to low-value helper rules unless doing so materially improves outer-structure recovery.
- Prefer concrete diagnostics over generic failure labels.
- For incremental changes, prefer patching the existing PEG structure over rewriting unrelated sections.