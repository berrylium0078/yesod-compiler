# SysY PEG Grammar

## 1. Source Grammar Notes

This document is an incremental extension of the existing PEG design in this repository, not a fresh grammar from scratch. The baseline language fragment still has:

- one compilation unit containing one function definition
- one function type, `int`
- one block containing one statement
- one numeric token class, `INT_CONST`

The source EBNF assumptions still come from [doc/sysy.md](/root/compiler/doc/sysy.md):

- identifiers follow the SysY `IDENT` token shape
- integer constants follow the SysY decimal, octal, and hexadecimal forms
- comments and whitespace remain ignorable trivia handled by the token layer

This delta extends the expression grammar from unary-only expressions to the usual SysY operator-precedence ladder:

- `Exp ::= LOrExp`
- `PrimaryExp ::= ...`
- `Number ::= ...`
- `UnaryExp ::= ...`
- `UnaryOp ::= ...`
- `MulExp ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp`
- `AddExp ::= MulExp | AddExp ("+" | "-") MulExp`
- `RelExp ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp`
- `EqExp ::= RelExp | EqExp ("==" | "!=") RelExp`
- `LAndExp ::= EqExp | LAndExp "&&" EqExp`
- `LOrExp ::= LAndExp | LOrExp "||" LAndExp`

Assumption: the input is intentionally limited to these expression forms plus the already documented `return` statement subset. No assignment, declaration, call, or identifier-valued expressions are implied by this delta.

## 2. Baseline Delta

Compared with the previous version of this document:

- unchanged rules: `CompUnit`, `FuncDef`, `FuncType`, `Block`, `Stmt`, `ReturnStmt`, `PrimaryExp`, `Number`, `UnaryExp`, `UnaryOp`, identifier and integer-literal rules, trivia rules, and the existing structural recovery points
- refactored rules: `Exp` now aliases `LOrExp` instead of `UnaryExp`
- added rules: `MulExp`, `MulOp`, `AddExp`, `AddOp`, `RelExp`, `RelOp`, `EqExp`, `EqOp`, `LAndExp`, `LOrExp`, and fixed token rules for `*`, `/`, `%`, `<`, `>`, `<=`, `>=`, `==`, `!=`, `&&`, and `||`
- reordered or split rules: the left-recursive binary-expression rules are rewritten into precedence-layer heads with repeated operator tails
- token-layer change: fixed operator tokens are extended to cover multiplicative, additive, relational, equality, and logical operators; whitespace/comment handling stays unchanged
- recovery impact: the existing function-header, block, return-statement, and parenthesized-primary recovery points remain valid; no new recovery labels are required for the precedence helpers because they are better reported through `ReturnStmt` and `PrimaryExp`

This is the minimal coherent extension of the current PEG design: unaffected statement and declaration rules stay intact, while the expression layer grows only enough to cover the new operator families and their precedence.

## 3. Left-Recursion Elimination

The new operator-precedence EBNF introduces several left-recursive expression rules. In PEG they are rewritten into head-plus-repetition form so the resulting parse stays left-associative without left recursion.

1. Multiplicative expressions

Original shape:

```ebnf
MulExp ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp;
```

Rewritten PEG shape:

```peg
MulExp <- UnaryExp (MulOp UnaryExp)*
```

This preserves the intended parse because the repeated tail consumes each successive multiplicative operator and operand in source order, which corresponds to left-associative folding.

2. Additive expressions

Original shape:

```ebnf
AddExp ::= MulExp | AddExp ("+" | "-") MulExp;
```

Rewritten PEG shape:

```peg
AddExp <- MulExp (AddOp MulExp)*
```

This preserves the original meaning for the same reason: the PEG rule parses a leading multiplicative expression followed by zero or more additive tails, which is the standard left-associative encoding.

3. Relational expressions

Original shape:

```ebnf
RelExp ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp;
```

Rewritten PEG shape:

```peg
RelExp <- AddExp (RelOp AddExp)*
```

This keeps the same precedence boundary and left-to-right chaining structure while removing left recursion.

4. Equality expressions

Original shape:

```ebnf
EqExp ::= RelExp | EqExp ("==" | "!=") RelExp;
```

Rewritten PEG shape:

```peg
EqExp <- RelExp (EqOp RelExp)*
```

This preserves the intended parse by encoding zero or more equality tails after the leading relational expression.

5. Logical-and expressions

Original shape:

```ebnf
LAndExp ::= EqExp | LAndExp "&&" EqExp;
```

Rewritten PEG shape:

```peg
LAndExp <- EqExp (ANDAND EqExp)*
```

This keeps `&&` at its own precedence level and eliminates left recursion with the same left-associative repetition strategy.

6. Logical-or expressions

Original shape:

```ebnf
LOrExp ::= LAndExp | LOrExp "||" LAndExp;
```

Rewritten PEG shape:

```peg
LOrExp <- LAndExp (OROR LAndExp)*
```

This preserves the intended precedence and associativity while removing the left-recursive accumulator form.

7. Identifier

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

8. Decimal integer constant

Original shape:

```ebnf
decimal-const ::= nonzero-digit | decimal-const digit;
```

Rewritten PEG shape:

```peg
DecimalConst <- NonZeroDigit Digit*
```

This keeps the same set of decimal strings while removing the left-recursive accumulator form.

9. Octal integer constant

Original shape:

```ebnf
octal-const ::= "0" | octal-const octal-digit;
```

Rewritten PEG shape:

```peg
OctalConst <- "0" OctalDigit*
```

This preserves the original meaning: one leading `0`, followed by zero or more octal digits.

10. Hexadecimal integer constant

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

## 4. Plain PEG Grammar

```peg
CompUnit           <- Spacing FuncDef EOF

FuncDef            <- FuncType IDENT LPAREN RPAREN Block
FuncType           <- KW_INT

Block              <- LBRACE Stmt RBRACE
Stmt               <- ReturnStmt
ReturnStmt         <- KW_RETURN Exp SEMI

Exp                <- LOrExp
LOrExp             <- LAndExp (OROR LAndExp)*
LAndExp            <- EqExp (ANDAND EqExp)*
EqExp              <- RelExp (EqOp RelExp)*
RelExp             <- AddExp (RelOp AddExp)*
AddExp             <- MulExp (AddOp MulExp)*
MulExp             <- UnaryExp (MulOp UnaryExp)*
PrimaryExp         <- LPAREN Exp RPAREN / Number
Number             <- INT_CONST
UnaryExp           <- PrimaryExp / UnaryOp UnaryExp
UnaryOp            <- PLUS / MINUS / BANG
MulOp              <- STAR / SLASH / PERCENT
AddOp              <- PLUS / MINUS
RelOp              <- LE / GE / LT / GT
EqOp               <- EQEQ / NE

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
PLUS               <- "+" Spacing
MINUS              <- "-" Spacing
BANG               <- "!" Spacing
STAR               <- "*" Spacing
SLASH              <- "/" Spacing
PERCENT            <- "%" Spacing
LE                 <- "<=" Spacing
GE                 <- ">=" Spacing
LT                 <- "<" Spacing
GT                 <- ">" Spacing
EQEQ               <- "==" Spacing
NE                 <- "!=" Spacing
ANDAND             <- "&&" Spacing
OROR               <- "||" Spacing

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
- `Stmt <- ReturnStmt`
  The current subset still has only one statement form, so there is no active branch-order hazard yet. This rule is still called out because statement parsing becomes choice-sensitive as more statement forms are added.
- `UnaryExp <- PrimaryExp / UnaryOp UnaryExp`
  The chosen order preserves the source EBNF. In the current fragment the prefixes are still disjoint: `PrimaryExp` starts with `(` or an integer literal, while `UnaryOp UnaryExp` starts with `+`, `-`, or `!`.
- `PrimaryExp <- LPAREN Exp RPAREN / Number`
  The alternatives are prefix-disjoint in this subset, so the order is currently safe either way. It remains documented because the recovery grammar commits after `(`.
- `RelOp <- LE / GE / LT / GT`
  `<=` must appear before `<`, and `>=` must appear before `>`. In PEG, putting `LT` before `LE` would make `<=` commit to `<` and leave `=` behind; the same issue applies to `>` versus `>=`.

## 5. Recovery-Annotated PEG Grammar

```peg
CompUnit                <- Spacing FuncDef EOF

FuncDef                 <- FuncType IDENT LPAREN ^
                           (RPAREN / Throw<MissingFuncRParen> RecoverToFuncHeaderEnd)
                           Block
FuncType                <- KW_INT

Block                   <- LBRACE ^
                           (Stmt / Throw<MalformedStmtHead> RecoverToStmtBoundary)
                           (RBRACE / Throw<MissingRBrace> RecoverToBlockEnd)
Stmt                    <- ReturnStmt
ReturnStmt              <- KW_RETURN ^
                           (Exp / Throw<MalformedReturnValue> RecoverToStmtBoundary)
                           (SEMI / Throw<MissingSemicolon> RecoverToStmtBoundary)

Exp                     <- LOrExp
LOrExp                  <- LAndExp (OROR LAndExp)*
LAndExp                 <- EqExp (ANDAND EqExp)*
EqExp                   <- RelExp (EqOp RelExp)*
RelExp                  <- AddExp (RelOp AddExp)*
AddExp                  <- MulExp (AddOp MulExp)*
MulExp                  <- UnaryExp (MulOp UnaryExp)*
PrimaryExp              <- LPAREN ^
                           (Exp / Throw<MalformedPrimaryExp> RecoverToExprRParen)
                           (RPAREN / Throw<MissingPrimaryRParen> RecoverToExprRParen)
                         / Number
Number                  <- INT_CONST
UnaryExp                <- PrimaryExp / UnaryOp UnaryExp
UnaryOp                 <- PLUS / MINUS / BANG
MulOp                   <- STAR / SLASH / PERCENT
AddOp                   <- PLUS / MINUS
RelOp                   <- LE / GE / LT / GT
EqOp                    <- EQEQ / NE

IDENT                   <- Identifier Spacing
INT_CONST               <- IntegerConst Spacing

Identifier              <- IdentifierStart IdentifierContinue*
IdentifierStart         <- [_A-Za-z]
IdentifierContinue      <- IdentifierStart / Digit

IntegerConst            <- HexadecimalConst / OctalConst / DecimalConst
DecimalConst            <- NonZeroDigit Digit*
OctalConst              <- "0" OctalDigit*
HexadecimalConst        <- HexadecimalPrefix HexadecimalDigit+
HexadecimalPrefix       <- "0x" / "0X"

KW_INT                  <- "int" !IdentifierContinue Spacing
KW_RETURN               <- "return" !IdentifierContinue Spacing
LPAREN                  <- "(" Spacing
RPAREN                  <- ")" Spacing
LBRACE                  <- "{" Spacing
RBRACE                  <- "}" Spacing
SEMI                    <- ";" Spacing
PLUS                    <- "+" Spacing
MINUS                   <- "-" Spacing
BANG                    <- "!" Spacing
STAR                    <- "*" Spacing
SLASH                   <- "/" Spacing
PERCENT                 <- "%" Spacing
LE                      <- "<=" Spacing
GE                      <- ">=" Spacing
LT                      <- "<" Spacing
GT                      <- ">" Spacing
EQEQ                    <- "==" Spacing
NE                      <- "!=" Spacing
ANDAND                  <- "&&" Spacing
OROR                    <- "||" Spacing

Spacing                 <- (WhiteSpace / Comment)*
WhiteSpace              <- [ \t\r\n]+
Comment                 <- LineComment / BlockComment
LineComment             <- "//" (!EndOfLine .)* EndOfLine?
BlockComment            <- "/*" (!"*/" .)* "*/"
EndOfLine               <- "\r\n" / "\n" / "\r"

RecoverToFuncHeaderEnd  <- (!")" !"{" !EOF .)* (RPAREN / &LBRACE / EOF)
RecoverToExprRParen     <- (!")" !";" !"}" !EOF .)* (RPAREN / &SEMI / &RBRACE / EOF)
RecoverToStmtBoundary   <- (!";" !"}" !EOF .)* (SEMI / &RBRACE / EOF)
RecoverToBlockEnd       <- (!"}" !EOF .)* (RBRACE / EOF)

Digit                   <- [0-9]
NonZeroDigit            <- [1-9]
OctalDigit              <- [0-7]
HexadecimalDigit        <- [0-9A-Fa-f]
EOF                     <- !.
```

Recovery placement rationale:

- `FuncDef` keeps the existing cut after `LPAREN` because `FuncType IDENT (` decisively commits to a function definition in this subset.
- `Block` keeps the existing cut after `{` because block parsing is already structurally determined once the opening delimiter is consumed.
- `ReturnStmt` keeps the existing cut after `return` because no other statement form shares that prefix.
- `PrimaryExp` keeps the existing cut after `(` because once a parenthesized primary starts, the parser should not backtrack into `Number`; this remains the highest-value expression-level recovery point.
- No labeled recovery is added to `MulExp`, `AddExp`, `RelExp`, `EqExp`, `LAndExp`, `LOrExp`, `MulOp`, `AddOp`, `RelOp`, or `EqOp`. These precedence helpers are low-level shape rules; their failures are better reported through `ReturnStmt` or `PrimaryExp`, which provide clearer diagnostics and more reliable synchronization anchors.

## 6. Recovery Inventory

| Label | Where it is thrown | Meaning | Recovery |
|---|---|---|---|
| `MissingFuncRParen` | `FuncDef` after `LPAREN` | function declarator is missing `)` | `RecoverToFuncHeaderEnd` |
| `MalformedStmtHead` | `Block` where a statement should begin | block body does not start with a valid statement form | `RecoverToStmtBoundary` |
| `MalformedReturnValue` | `ReturnStmt` after `return` | return statement is missing or starts with a malformed expression | `RecoverToStmtBoundary` |
| `MissingSemicolon` | `ReturnStmt` after `Exp` | return statement is missing `;` | `RecoverToStmtBoundary` |
| `MalformedPrimaryExp` | `PrimaryExp` after `(` | parenthesized primary expression has no valid inner expression | `RecoverToExprRParen` |
| `MissingPrimaryRParen` | `PrimaryExp` after inner `Exp` | parenthesized primary expression is missing `)` | `RecoverToExprRParen` |
| `MissingRBrace` | `Block` after the statement | block is missing `}` | `RecoverToBlockEnd` |

## 7. Ordered-Choice Notes

1. `Stmt`

```peg
Stmt <- ReturnStmt
```

There is still only one statement form, so PEG branch ordering is not yet a correctness issue. The rule is still called out explicitly because statement parsing becomes highly order-sensitive once assignment, block, empty, or expression statements are added. The recovery grammar still needs a cut in `ReturnStmt` after `return`, because that prefix fully determines the intended statement kind in the current subset.

2. `UnaryExp`

```peg
UnaryExp <- PrimaryExp / UnaryOp UnaryExp
```

The chosen order preserves the source EBNF. In the current fragment, changing the order would not change the accepted language because the prefixes are disjoint: `PrimaryExp` starts with `(` or `INT_CONST`, while `UnaryOp UnaryExp` starts with `+`, `-`, or `!`. No cut is needed at `UnaryExp` itself.

3. `PrimaryExp`

```peg
PrimaryExp <- LPAREN Exp RPAREN / Number
```

The alternatives are prefix-disjoint in this subset, so the order is currently safe either way. The recovery grammar still adds a cut after `LPAREN`, because once that delimiter is consumed, the parser should commit to finishing the parenthesized form rather than silently falling through to other primary-expression alternatives.

4. `RelOp`

```peg
RelOp <- LE / GE / LT / GT
```

This order is required in PEG. `LE` must appear before `LT`, and `GE` must appear before `GT`, because the longer operators share prefixes with the shorter ones. If `LT` or `GT` came first, inputs like `<=` or `>=` would be split incorrectly.

5. `IntegerConst`

```peg
IntegerConst <- HexadecimalConst / OctalConst / DecimalConst
```

This order is required in PEG. `HexadecimalConst` must appear before `OctalConst`, because both can start with `0`. If `OctalConst` came first, `0x2a` would commit to octal `0` and the remaining input would fail later.

## 8. Short Design Rationale

This update keeps the existing PEG design intact and extends only the expression layer needed to support the remaining arithmetic, relational, equality, and logical operators. The final shape follows the standard precedence ladder from `LOrExp` down to `UnaryExp`, with each left-recursive EBNF rule converted to a PEG-friendly `Head Tail*` form so precedence and associativity stay clear and implementation-oriented.

The recovery design stays deliberately narrow. The existing outer-structure recovery for function headers, blocks, and return statements remains valid, and the only expression-level recovery point is still the parenthesized primary form, where malformed inner expressions and missing `)` are both common and structurally recoverable. This keeps the label set compact and relies on robust synchronization tokens already present in the grammar, namely `)`, `;`, `{`, and `}`.