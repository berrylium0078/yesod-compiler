# SysY PEG Grammar

## 1. Source Grammar Notes

This document is an incremental update to the existing minimal SysY PEG design, not a fresh grammar definition. The baseline remains the same language fragment already documented here:

- one compilation unit containing one function definition
- one function type, `int`
- one statement form, `return Number;`
- one numeric token class, `INT_CONST`

The source EBNF assumptions still come from [doc/sysy.md](/root/compiler/doc/sysy.md):

- identifiers use the SysY `IDENT` token shape
- integer constants use SysY decimal, octal, and hexadecimal forms
- comments follow C style: `//` for single-line comments and `/* ... */` for block comments

This delta only adds the trivia layer that the previous note explicitly omitted:

- whitespace skipping
- single-line comments
- multi-line comments

No new syntactic constructs are introduced beyond that lexical/trivia extension.

## 2. Baseline Delta

Compared with the previous version of this document:

- unchanged rules: `CompUnit`, `FuncDef`, `FuncType`, `Block`, `Stmt`, `Number`, `IDENT`, `INT_CONST`
- refactored rules: punctuation and keywords are now surfaced as explicit token rules so trailing trivia can be consumed uniformly
- added rules: `Spacing`, `WhiteSpace`, `Comment`, `LineComment`, `BlockComment`, `EndOfLine`, and explicit delimiter token rules such as `LPAREN` and `RBRACE`
- token-layer change: the grammar now treats whitespace and C-style comments as ignorable trivia after every token
- recovery update: the earlier document had no recovery layer; this revision adds a compact recovery-annotated PEG for missing `)`, missing `}`, malformed statement head, malformed return value, and missing `;`

Because this is only a trivia-layer extension, the core parse tree shape stays unchanged.

## 3. Left-Recursion Elimination

The syntactic core of this subset is already PEG-friendly. The only left-recursive or PEG-incompatible patterns are in the lexical layer inherited from SysY.

1. Identifier

Original shape:

```ebnf
identifier ::= identifier-nondigit
             | identifier identifier-nondigit
             | identifier digit;
```

Rewritten PEG shape:

```peg
Identifier <- IdentifierStart IdentifierContinue*
```

This preserves the intended language because the original rule describes one leading nondigit followed by zero or more nondigits or digits.

2. Decimal integer constant

Original shape:

```ebnf
decimal-const ::= nonzero-digit | decimal-const digit;
```

Rewritten PEG shape:

```peg
DecimalConst <- NonZeroDigit Digit*
```

This keeps the same set of decimal strings while removing the left-recursive accumulator form.

3. Octal integer constant

Original shape:

```ebnf
octal-const ::= "0" | octal-const octal-digit;
```

Rewritten PEG shape:

```peg
OctalConst <- "0" OctalDigit*
```

This preserves the original meaning: one leading `0`, followed by zero or more octal digits.

4. Hexadecimal integer constant

Original shape:

```ebnf
hexadecimal-const ::= hexadecimal-prefix hexadecimal-digit
                    | hexadecimal-const hexadecimal-digit;
```

Rewritten PEG shape:

```peg
HexadecimalConst <- HexadecimalPrefix HexadecimalDigit+
```

This keeps the same accepted strings while replacing left recursion with one required hexadecimal digit after the prefix and then repetition.

5. Trivia skipping

Original shape:

- not present in the earlier PEG note

Rewritten PEG shape:

```peg
Spacing <- (WhiteSpace / Comment)*
```

This is PEG-friendly as written. It adds an ignorable token layer without changing the concrete syntax of the language fragment.

## 4. Plain PEG Grammar

```peg
CompUnit           <- Spacing FuncDef EOF

FuncDef            <- FuncType IDENT LPAREN RPAREN Block
FuncType           <- KW_INT

Block              <- LBRACE Stmt RBRACE
Stmt               <- ReturnStmt
ReturnStmt         <- KW_RETURN Number SEMI
Number             <- INT_CONST

IDENT              <- Identifier Spacing
INT_CONST          <- IntegerConst Spacing

Identifier         <- IdentifierStart IdentifierContinue*
IdentifierStart    <- [_A-Za-z]
IdentifierContinue <- IdentifierStart / Digit

IntegerConst       <- HexadecimalConst / OctalConst / DecimalConst
DecimalConst       <- NonZeroDigit Digit*
OctalConst         <- "0" OctalDigit*
HexadecimalConst   <- HexadecimalPrefix HexadecimalDigit+
HexadecimalPrefix  <- "0x" / "0X"

KW_INT             <- "int" !IdentifierContinue Spacing
KW_RETURN          <- "return" !IdentifierContinue Spacing
LPAREN             <- "(" Spacing
RPAREN             <- ")" Spacing
LBRACE             <- "{" Spacing
RBRACE             <- "}" Spacing
SEMI               <- ";" Spacing

Spacing            <- (WhiteSpace / Comment)*
WhiteSpace         <- [ \t\r\n]+
Comment            <- LineComment / BlockComment
LineComment        <- "//" (!EndOfLine .)* EndOfLine?
BlockComment       <- "/*" (!"*/" .)* "*/"
EndOfLine          <- "\r\n" / "\n" / "\r"

Digit              <- [0-9]
NonZeroDigit       <- [1-9]
OctalDigit         <- [0-7]
HexadecimalDigit   <- [0-9A-Fa-f]
EOF                <- !.
```

Ordered-choice-sensitive rules in the plain grammar:

- `IntegerConst <- HexadecimalConst / OctalConst / DecimalConst`
  `HexadecimalConst` must come first. In PEG, `0x2a` would otherwise match `OctalConst` as just `0`, leaving `x2a` behind.
- `Comment <- LineComment / BlockComment`
  This rule is not practically order-sensitive because the prefixes diverge at the second character: `//` versus `/*`.
- `Stmt <- ReturnStmt`
  In this restricted fragment, `Stmt` has only one branch, so branch ordering is not yet a correctness issue.

## 5. Recovery-Annotated PEG Grammar

The recovery layer stays small and only annotates high-value structural productions.

```peg
CompUnit              <- Spacing FuncDef EOF

FuncDef               <- FuncType IDENT LPAREN ^ (RPAREN / Throw<MissingRParen> RecoverToRParen) Block
FuncType              <- KW_INT

Block                 <- LBRACE ^ (Stmt / Throw<MalformedStmtHead> RecoverToStmtBoundary)
                         (RBRACE / Throw<MissingRBrace> RecoverToBlockEnd)
Stmt                  <- ReturnStmt
ReturnStmt            <- KW_RETURN ^ (Number / Throw<MalformedReturnValue> RecoverToStmtBoundary)
                         (SEMI / Throw<MissingSemicolon> RecoverToStmtBoundary)
Number                <- INT_CONST

IDENT                 <- Identifier Spacing
INT_CONST             <- IntegerConst Spacing

Identifier            <- IdentifierStart IdentifierContinue*
IdentifierStart       <- [_A-Za-z]
IdentifierContinue    <- IdentifierStart / Digit

IntegerConst          <- HexadecimalConst / OctalConst / DecimalConst
DecimalConst          <- NonZeroDigit Digit*
OctalConst            <- "0" OctalDigit*
HexadecimalConst      <- HexadecimalPrefix HexadecimalDigit+
HexadecimalPrefix     <- "0x" / "0X"

KW_INT                <- "int" !IdentifierContinue Spacing
KW_RETURN             <- "return" !IdentifierContinue Spacing
LPAREN                <- "(" Spacing
RPAREN                <- ")" Spacing
LBRACE                <- "{" Spacing
RBRACE                <- "}" Spacing
SEMI                  <- ";" Spacing

Spacing               <- (WhiteSpace / Comment)*
WhiteSpace            <- [ \t\r\n]+
Comment               <- LineComment / BlockComment
LineComment           <- "//" (!EndOfLine .)* EndOfLine?
BlockComment          <- "/*" (!"*/" .)* "*/"
EndOfLine             <- "\r\n" / "\n" / "\r"

RecoverToRParen       <- (!")" !"{" !EOF .)* (RPAREN / &LBRACE / EOF)
RecoverToStmtBoundary <- (!";" !"}" !EOF .)* (SEMI / &RBRACE / EOF)
RecoverToBlockEnd     <- (!"}" !EOF .)* (RBRACE / EOF)

Digit                 <- [0-9]
NonZeroDigit          <- [1-9]
OctalDigit            <- [0-7]
HexadecimalDigit      <- [0-9A-Fa-f]
EOF                   <- !.
```

Recovery placement rationale:

- `FuncDef` gets a cut after `LPAREN` because once `FuncType IDENT (` is seen, the parser is decisively in a function definition.
- `Block` gets a cut after `{` because that delimiter commits to block parsing and makes statement-level recovery the right next action.
- `ReturnStmt` gets a cut after `return` because no other statement form in this subset shares that prefix.
- No labeled recovery is added to `Identifier`, `IntegerConst`, `Comment`, or digit helper rules. Those are low-level lexical rules; annotating them would create noisy failures without improving outer-structure recovery.

Unterminated block comments are intentionally left as plain lexical failure at EOF rather than being given a separate recovery label. That keeps the label set compact and avoids speculative resynchronization inside comments.

## 6. Recovery Inventory

| Label | Thrown from | Meaning | Recovery |
|---|---|---|---|
| `MissingRParen` | `FuncDef` after `LPAREN` | function declarator is missing `)` | `RecoverToRParen` or sync before `{`/EOF |
| `MalformedStmtHead` | `Block` where a statement should begin | block body does not start with a valid statement form | `RecoverToStmtBoundary` |
| `MalformedReturnValue` | `ReturnStmt` after `return` | return statement is missing or malformed value | `RecoverToStmtBoundary` |
| `MissingSemicolon` | `ReturnStmt` after `Number` | return statement is missing `;` | `RecoverToStmtBoundary` |
| `MissingRBrace` | `Block` after the statement | block is missing `}` | `RecoverToBlockEnd` |

## 7. Ordered-Choice Notes

1. `IntegerConst`

```peg
IntegerConst <- HexadecimalConst / OctalConst / DecimalConst
```

This order is required in PEG. `HexadecimalConst` must appear before `OctalConst`, because both can start with `0`. If `OctalConst` came first, `0x2a` would commit to octal `0` and the remaining input would fail later.

2. `Stmt`

```peg
Stmt <- ReturnStmt
```

The current subset has only one statement form, so no branch-order ambiguity exists yet. The recovery layer still uses a cut in `ReturnStmt` after `return`, because that prefix fully determines the intended construct.

3. `Comment`

```peg
Comment <- LineComment / BlockComment
```

This order is harmless because the two alternatives are distinguished by the second character. No cut is needed.

4. `FuncDef`

```peg
FuncDef <- FuncType IDENT LPAREN RPAREN Block
```

This rule has no internal ordered choice in the plain grammar, but the recovery grammar adds a cut after `LPAREN`. That cut is appropriate because once the parser has consumed `FuncType IDENT (`, the only meaningful continuation in this subset is the remainder of a function definition.

5. `UnaryExp`

There is no `UnaryExp` rule in this minimal fragment yet, so no PEG branch-order decision is required there. When the language grows toward the full SysY expression grammar, `UnaryExp` will become one of the primary order-sensitive rules and should be documented separately at that time.

## 8. Short Design Rationale

This update keeps the previous PEG design intact and only adds the missing trivia layer needed for a practical parser specification. The grammar now explicitly models whitespace and both C-style comment forms from [doc/sysy.md](/root/compiler/doc/sysy.md), while preserving the same syntactic core and the same parse-tree intent.

The recovery design stays deliberately small. It annotates only the productions that define the outer structure of this subset: function headers, blocks, and return statements. Synchronization points use existing delimiters already present in the token layer, namely `)`, `;`, and `}`. That is enough to provide useful diagnostics and avoid cascading failures without turning the grammar into a recovery-heavy design.