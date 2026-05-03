# SysY PEG Grammar

## 1. Source Grammar Notes

This document is an incremental extension of the existing PEG design in this repository, not a fresh grammar from scratch. The baseline language fragment still has:

- one compilation unit containing one function definition
- one function type, `int`
- one numeric token class, `INT_CONST`
- whitespace and comments handled entirely by the token layer

The source EBNF assumptions still come from [doc/sysy.md](/root/compiler/doc/sysy.md), but this update follows the simplified feature delta from the task prompt rather than the full SysY grammar in that file.

Assumptions for this delta:

- declarations are limited to scalar `const int` and `int` declarations
- `ConstDef` is `IDENT = ConstInitVal`, with no array suffixes
- `VarDef` is `IDENT` or `IDENT = InitVal`, with no array suffixes
- `ConstInitVal ::= ConstExp` and `InitVal ::= Exp`; brace initializers are out of scope
- `LVal ::= IDENT`
- `Stmt ::= LVal "=" Exp ";" | [Exp] ";" | Block | "return" Exp ";"`
- `Block` can now appear as a statement form, so nested blocks introduce nested statement scopes
- control-flow statements and function calls are still out of scope

The existing expression grammar remains the usual SysY operator-precedence ladder:

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

## 2. Baseline Delta

Compared with the previous version of this document:

- unchanged rules: `CompUnit`, `FuncDef`, `FuncType`, `Decl`, `ConstDecl`, `VarDecl`, `BType`, `ConstDef`, `ConstInitVal`, `VarDef`, `InitVal`, `ConstExp`, `Exp`, `LOrExp`, `LAndExp`, `EqExp`, `RelExp`, `AddExp`, `MulExp`, `UnaryExp`, `UnaryOp`, `PrimaryExp`, `LVal`, `Number`, the identifier and integer-literal rules, and whitespace/comment handling
- changed rules: `Stmt` now covers assignment, expression statements, block statements, and `return`; `Block` remains a sequence of `BlockItem`s but is now reachable recursively through `Stmt`, which is what gives nested scopes their syntax
- added rules: the helper `ExpStmt`; no declaration or token rules are added by this delta
- reordered or refactored rules: `Stmt` must now make the `AssignStmt` versus `ExpStmt` choice explicit because both can start with `IDENT`; `Block` is promoted from top-level function-body structure to a general statement form
- token-layer delta: none; the existing `{`, `}`, and `;` tokens already cover statement blocks and empty or non-empty expression statements
- recovery impact: the existing function-header, block, declaration, assignment, return-statement, and parenthesized-primary recovery points remain valid; `RecoverToBlockItemBoundary` must be widened because block items can now begin with block heads, empty statements, or general expression-statement heads

This is the minimal coherent extension of the current PEG design. The existing compilation-unit, declaration, and expression structure stays intact, and only the statement surface is widened enough to admit nested blocks and scoped statement bodies.

## 3. Left-Recursion Elimination

The declaration and statement additions introduce no new left recursion. The only PEG-incompatible patterns that still need rewriting are the existing expression and token-layer accumulator rules.

1. Multiplicative expressions

Original shape:

```ebnf
MulExp ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp;
```

Rewritten PEG shape:

```peg
MulExp <- UnaryExp (MulOp UnaryExp)*
```

This preserves the intended parse because each successive multiplicative tail is consumed in source order, which corresponds to left-associative folding.

2. Additive expressions

Original shape:

```ebnf
AddExp ::= MulExp | AddExp ("+" | "-") MulExp;
```

Rewritten PEG shape:

```peg
AddExp <- MulExp (AddOp MulExp)*
```

This preserves the original meaning by parsing one multiplicative head followed by zero or more additive tails in left-to-right order.

3. Relational expressions

Original shape:

```ebnf
RelExp ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp;
```

Rewritten PEG shape:

```peg
RelExp <- AddExp (RelOp AddExp)*
```

This keeps the same precedence boundary and chaining behavior while removing left recursion.

4. Equality expressions

Original shape:

```ebnf
EqExp ::= RelExp | EqExp ("==" | "!=") RelExp;
```

Rewritten PEG shape:

```peg
EqExp <- RelExp (EqOp RelExp)*
```

This preserves the intended parse by encoding equality chains as a leading relational expression plus repeated equality tails.

5. Logical-and expressions

Original shape:

```ebnf
LAndExp ::= EqExp | LAndExp "&&" EqExp;
```

Rewritten PEG shape:

```peg
LAndExp <- EqExp (ANDAND EqExp)*
```

This preserves `&&` precedence and associativity while removing left recursion.

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

11. Constant expressions

Original shape:

```ebnf
ConstExp ::= Exp;
```

Rewritten PEG shape:

```peg
ConstExp <- Exp
```

This is a direct alias rather than a left-recursive rewrite. The PEG rule preserves the syntax exactly, while constant-only restrictions remain semantic rather than syntactic.

## 4. Plain PEG Grammar

```peg
CompUnit           <- Spacing FuncDef EOF

FuncDef            <- FuncType IDENT LPAREN RPAREN Block
FuncType           <- KW_INT

Block              <- LBRACE BlockItem* RBRACE
BlockItem          <- Decl / Stmt

Decl               <- ConstDecl / VarDecl
ConstDecl          <- KW_CONST BType ConstDef (COMMA ConstDef)* SEMI
VarDecl            <- BType VarDef (COMMA VarDef)* SEMI
BType              <- KW_INT
ConstDef           <- IDENT ASSIGN ConstInitVal
ConstInitVal       <- ConstExp
VarDef             <- IDENT (ASSIGN InitVal)?
InitVal            <- Exp
ConstExp           <- Exp

Stmt               <- AssignStmt / Block / ReturnStmt / ExpStmt
AssignStmt         <- LVal ASSIGN Exp SEMI
ExpStmt            <- Exp? SEMI
ReturnStmt         <- KW_RETURN Exp SEMI

Exp                <- LOrExp
LOrExp             <- LAndExp (OROR LAndExp)*
LAndExp            <- EqExp (ANDAND EqExp)*
EqExp              <- RelExp (EqOp RelExp)*
RelExp             <- AddExp (RelOp AddExp)*
AddExp             <- MulExp (AddOp MulExp)*
MulExp             <- UnaryExp (MulOp UnaryExp)*
UnaryExp           <- PrimaryExp / UnaryOp UnaryExp
PrimaryExp         <- LPAREN Exp RPAREN / LVal / Number
LVal               <- IDENT
Number             <- INT_CONST
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

KW_CONST           <- "const" !IdentifierContinue Spacing
KW_INT             <- "int" !IdentifierContinue Spacing
KW_RETURN          <- "return" !IdentifierContinue Spacing
LPAREN             <- "(" Spacing
RPAREN             <- ")" Spacing
LBRACE             <- "{" Spacing
RBRACE             <- "}" Spacing
SEMI               <- ";" Spacing
COMMA              <- "," Spacing
ASSIGN             <- "=" Spacing
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

- `Stmt <- AssignStmt / Block / ReturnStmt / ExpStmt`
  `AssignStmt` must come first because assignment and expression statements can both start with `IDENT`. `ExpStmt` must stay last because it is the catch-all statement form for empty statements and general expression-headed statements; putting it earlier would make PEG diagnose `IDENT = ... ;` through the wrong branch.
- `UnaryExp <- PrimaryExp / UnaryOp UnaryExp`
  The chosen order preserves the source EBNF. The prefixes remain disjoint: `PrimaryExp` starts with `(`, `IDENT`, or an integer literal, while `UnaryOp UnaryExp` starts with `+`, `-`, or `!`.
- `PrimaryExp <- LPAREN Exp RPAREN / LVal / Number`
  The branch order is intentional even though the prefixes are still disjoint in this subset. `LVal` must stay ahead of any future `IDENT`-based primary forms if the grammar grows further, and the current order keeps the document aligned with that likely extension path.
- `RelOp <- LE / GE / LT / GT`
  `<=` must appear before `<`, and `>=` must appear before `>`. In PEG, putting `LT` before `LE` would make `<=` commit to `<` and leave `=` behind; the same issue applies to `>` versus `>=`.
- `IntegerConst <- HexadecimalConst / OctalConst / DecimalConst`
  `HexadecimalConst` must come first. In PEG, `0x2a` would otherwise match `OctalConst` as just `0`, leaving `x2a` behind.

Rules whose choice order is intentionally documented but not currently hazardous:

- `Decl <- ConstDecl / VarDecl`
  `ConstDecl` and `VarDecl` are prefix-distinct because only `ConstDecl` starts with `const`.
- `BlockItem <- Decl / Stmt`
  `Decl` starts with `const` or `int`, while `Stmt` starts with `{`, `return`, `;`, or an expression head in this subset. The order is therefore still safe today, but it is worth documenting because block-item dispatch is a common growth point in PEG grammars.

## 5. Recovery-Annotated PEG Grammar

```peg
CompUnit                 <- Spacing FuncDef EOF

FuncDef                  <- FuncType IDENT LPAREN ^
                            (RPAREN / Throw<MissingFuncRParen> RecoverToFuncHeaderEnd)
                            Block
FuncType                 <- KW_INT

Block                    <- LBRACE ^
                            (BlockItem / Throw<MalformedBlockItem> RecoverToBlockItemBoundary)*
                            (RBRACE / Throw<MissingRBrace> RecoverToBlockEnd)
BlockItem                <- Decl / Stmt

Decl                     <- ConstDecl / VarDecl
ConstDecl                <- KW_CONST ^
                            BType
                            (ConstDef / Throw<MalformedDeclItem> RecoverToDeclBoundary)
                            (COMMA (ConstDef / Throw<MalformedDeclItem> RecoverToDeclBoundary))*
                            (SEMI / Throw<MissingDeclSemicolon> RecoverToDeclBoundary)
VarDecl                  <- BType ^
                            (VarDef / Throw<MalformedDeclItem> RecoverToDeclBoundary)
                            (COMMA (VarDef / Throw<MalformedDeclItem> RecoverToDeclBoundary))*
                            (SEMI / Throw<MissingDeclSemicolon> RecoverToDeclBoundary)
BType                    <- KW_INT
ConstDef                 <- IDENT ASSIGN ConstInitVal
ConstInitVal             <- ConstExp
VarDef                   <- IDENT (ASSIGN InitVal)?
InitVal                  <- Exp
ConstExp                 <- Exp

Stmt                     <- AssignStmt / Block / ReturnStmt / ExpStmt
AssignStmt               <- LVal ASSIGN ^
                            (Exp / Throw<MalformedAssignValue> RecoverToStmtBoundary)
                            (SEMI / Throw<MissingAssignSemicolon> RecoverToStmtBoundary)
ExpStmt                  <- SEMI
                          / Exp ^
                            (SEMI / Throw<MissingSemicolon> RecoverToStmtBoundary)
ReturnStmt               <- KW_RETURN ^
                            (Exp / Throw<MalformedReturnValue> RecoverToStmtBoundary)
                            (SEMI / Throw<MissingSemicolon> RecoverToStmtBoundary)

Exp                      <- LOrExp
LOrExp                   <- LAndExp (OROR LAndExp)*
LAndExp                  <- EqExp (ANDAND EqExp)*
EqExp                    <- RelExp (EqOp RelExp)*
RelExp                   <- AddExp (RelOp AddExp)*
AddExp                   <- MulExp (AddOp MulExp)*
MulExp                   <- UnaryExp (MulOp UnaryExp)*
UnaryExp                 <- PrimaryExp / UnaryOp UnaryExp
PrimaryExp               <- LPAREN ^
                            (Exp / Throw<MalformedPrimaryExp> RecoverToExprRParen)
                            (RPAREN / Throw<MissingPrimaryRParen> RecoverToExprRParen)
                          / LVal
                          / Number
LVal                     <- IDENT
Number                   <- INT_CONST
UnaryOp                  <- PLUS / MINUS / BANG
MulOp                    <- STAR / SLASH / PERCENT
AddOp                    <- PLUS / MINUS
RelOp                    <- LE / GE / LT / GT
EqOp                     <- EQEQ / NE

IDENT                    <- Identifier Spacing
INT_CONST                <- IntegerConst Spacing

Identifier               <- IdentifierStart IdentifierContinue*
IdentifierStart          <- [_A-Za-z]
IdentifierContinue       <- IdentifierStart / Digit

IntegerConst             <- HexadecimalConst / OctalConst / DecimalConst
DecimalConst             <- NonZeroDigit Digit*
OctalConst               <- "0" OctalDigit*
HexadecimalConst         <- HexadecimalPrefix HexadecimalDigit+
HexadecimalPrefix        <- "0x" / "0X"

KW_CONST                 <- "const" !IdentifierContinue Spacing
KW_INT                   <- "int" !IdentifierContinue Spacing
KW_RETURN                <- "return" !IdentifierContinue Spacing
LPAREN                   <- "(" Spacing
RPAREN                   <- ")" Spacing
LBRACE                   <- "{" Spacing
RBRACE                   <- "}" Spacing
SEMI                     <- ";" Spacing
COMMA                    <- "," Spacing
ASSIGN                   <- "=" Spacing
PLUS                     <- "+" Spacing
MINUS                    <- "-" Spacing
BANG                     <- "!" Spacing
STAR                     <- "*" Spacing
SLASH                    <- "/" Spacing
PERCENT                  <- "%" Spacing
LE                       <- "<=" Spacing
GE                       <- ">=" Spacing
LT                       <- "<" Spacing
GT                       <- ">" Spacing
EQEQ                     <- "==" Spacing
NE                       <- "!=" Spacing
ANDAND                   <- "&&" Spacing
OROR                     <- "||" Spacing

Spacing                  <- (WhiteSpace / Comment)*
WhiteSpace               <- [ \t\r\n]+
Comment                  <- LineComment / BlockComment
LineComment              <- "//" (!EndOfLine .)* EndOfLine?
BlockComment             <- "/*" (!"*/" .)* "*/"
EndOfLine                <- "\r\n" / "\n" / "\r"

RecoverToFuncHeaderEnd   <- (!")" !"{" !EOF .)* (RPAREN / &LBRACE / EOF)
RecoverToExprRParen      <- (!")" !";" !"}" !EOF .)* (RPAREN / &SEMI / &RBRACE / EOF)
RecoverToStmtBoundary    <- (!";" !"}" !EOF .)* (SEMI / &RBRACE / EOF)
RecoverToDeclBoundary    <- (!"," !";" !"}" !EOF .)* (COMMA / SEMI / &RBRACE / EOF)
RecoverToBlockItemBoundary <- (!KW_CONST !KW_INT !KW_RETURN !LBRACE !SEMI !LPAREN !IDENT !INT_CONST !PLUS !MINUS !BANG !RBRACE !EOF .)*
                              (&KW_CONST / &KW_INT / &KW_RETURN / &LBRACE / &SEMI / &LPAREN / &IDENT / &INT_CONST / &PLUS / &MINUS / &BANG / &RBRACE / EOF)
RecoverToBlockEnd        <- (!"}" !EOF .)* (RBRACE / EOF)

Digit                    <- [0-9]
NonZeroDigit             <- [1-9]
OctalDigit               <- [0-7]
HexadecimalDigit         <- [0-9A-Fa-f]
EOF                      <- !.
```

Recovery placement rationale:

- `FuncDef` keeps the existing cut after `LPAREN` because `FuncType IDENT (` decisively commits to a function definition in this subset.
- `Block` keeps the existing cut after `{` because once the opening delimiter is consumed, the parser is structurally inside a block. That same block rule now serves both function bodies and nested statement blocks, so block-item recovery becomes the main scope-level synchronization point.
- `ConstDecl` and `VarDecl` gain recovery because declaration lists are now comma-separated structural regions with clear synchronization tokens: `,`, `;`, and `}`.
- `AssignStmt` gains a cut after `=` because once `LVal =` is consumed, the parser should report a malformed right-hand side or missing semicolon rather than backtracking into some other statement kind.
- `ExpStmt` uses a dedicated empty-statement branch for bare `;` and otherwise commits only after a full expression has been recognized. This keeps empty statements cheap while still reporting a missing semicolon after a non-empty expression statement.
- `ReturnStmt` keeps the existing cut after `return` because no other statement form shares that prefix.
- `PrimaryExp` keeps the existing cut after `(` because once a parenthesized primary starts, the parser should not backtrack into `LVal` or `Number`.
- No labeled recovery is added to `ConstDef`, `VarDef`, `InitVal`, `ConstExp`, `LVal`, or the precedence helpers. Those are lower-value structural helpers whose failures are better surfaced through the enclosing declaration, statement, or parenthesized-expression rules.

## 6. Recovery Inventory

| Label | Where it is thrown | Meaning | Recovery |
|---|---|---|---|
| `MissingFuncRParen` | `FuncDef` after `LPAREN` | function declarator is missing `)` | `RecoverToFuncHeaderEnd` |
| `MalformedBlockItem` | `Block` where a declaration or statement should begin | block body does not start with a valid `Decl` or `Stmt` | `RecoverToBlockItemBoundary` |
| `MalformedDeclItem` | `ConstDecl` or `VarDecl` where a declarator should begin | a comma-separated declarator is malformed or missing | `RecoverToDeclBoundary` |
| `MissingDeclSemicolon` | `ConstDecl` or `VarDecl` after the declarator list | declaration is missing `;` | `RecoverToDeclBoundary` |
| `MalformedAssignValue` | `AssignStmt` after `=` | assignment statement is missing or starts with a malformed right-hand-side expression | `RecoverToStmtBoundary` |
| `MissingSemicolon` | `ExpStmt` or `ReturnStmt` after a parsed expression | statement is missing `;` | `RecoverToStmtBoundary` |
| `MissingAssignSemicolon` | `AssignStmt` after `Exp` | assignment statement is missing `;` | `RecoverToStmtBoundary` |
| `MalformedReturnValue` | `ReturnStmt` after `return` | return statement is missing or starts with a malformed expression | `RecoverToStmtBoundary` |
| `MalformedPrimaryExp` | `PrimaryExp` after `(` | parenthesized primary expression has no valid inner expression | `RecoverToExprRParen` |
| `MissingPrimaryRParen` | `PrimaryExp` after inner `Exp` | parenthesized primary expression is missing `)` | `RecoverToExprRParen` |
| `MissingRBrace` | `Block` after the block items | block is missing `}` | `RecoverToBlockEnd` |

## 7. Ordered-Choice Notes

1. `Stmt`

```peg
Stmt <- AssignStmt / Block / ReturnStmt / ExpStmt
```

This order is required in PEG because `Stmt` is now genuinely branch-order-sensitive. `AssignStmt` and `ExpStmt` can both begin with `IDENT`, so assignment must run first. `ExpStmt` is intentionally last because it is the fallback for both empty statements and every remaining expression-headed statement; moving it earlier would make PEG consume statement heads too generically and would turn `IDENT = ... ;` into the wrong error shape. `Block` and `ReturnStmt` sit ahead of `ExpStmt` because their prefixes, `{` and `return`, are disjoint and therefore cheap to dispatch. A cut is not needed at `Stmt` itself; the decisive cuts belong inside `AssignStmt`, `Block`, `ReturnStmt`, and parenthesized primaries.

2. `UnaryExp`

```peg
UnaryExp <- PrimaryExp / UnaryOp UnaryExp
```

The chosen order preserves the source EBNF. In the current fragment, changing the order would not change the accepted language because the prefixes are disjoint: `PrimaryExp` starts with `(`, `IDENT`, or `INT_CONST`, while `UnaryOp UnaryExp` starts with `+`, `-`, or `!`. No cut is needed at `UnaryExp` itself.

3. `PrimaryExp`

```peg
PrimaryExp <- LPAREN Exp RPAREN / LVal / Number
```

The alternatives remain prefix-disjoint in this subset, so the order is not yet correctness-critical. It is still documented because `PrimaryExp` has become PEG-sensitive in the broader design sense: `LVal` now introduces an `IDENT`-based primary form, and the recovery grammar commits after `(`. If the order were changed carelessly in a future extension that added calls or other `IDENT`-led primary forms, PEG backtracking behavior would matter. A cut is needed only after `(`, where the parenthesized form is already determined.

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

6. `Decl`

```peg
Decl <- ConstDecl / VarDecl
```

The chosen order is safe because `ConstDecl` starts with `const`, while `VarDecl` starts with `int`. In other words, this rule is intentionally ordered but not currently branch-order-sensitive. No cut is needed at `Decl`; the cuts belong inside `ConstDecl` and `VarDecl`, after their distinctive prefixes commit to a declaration shape.

7. `BlockItem`

```peg
BlockItem <- Decl / Stmt
```

This order is also safe in the current subset. `Decl` starts with `const` or `int`, while `Stmt` starts with `{`, `return`, `;`, or an expression head. If the order were changed today, the accepted language would not change. It is still worth documenting because block-item dispatch is a common place where later grammar growth creates PEG sensitivity, especially once nested scopes exist. No cut is needed at `BlockItem`; malformed block items are recovered at the enclosing `Block` level.

## 8. Short Design Rationale

The final grammar keeps the previous PEG design intact and extends only the statement surface needed for the task: nested block statements and optional expression statements. The compilation-unit shape, declaration forms, precedence ladder, token-layer trivia rules, and operator ordering all remain unchanged. This makes the delta reviewable and keeps the grammar close to the original EBNF while still being explicit about PEG-specific branch order.

The recovery design also stays narrow. The existing outer-structure recovery for function headers, blocks, return statements, and parenthesized primaries remains valid. This delta does not add new delimiter classes, so the main recovery change is broadening block-item synchronization to recognize nested-block heads, empty statements, and expression-statement heads in addition to declaration and return heads. Low-level helpers such as `LVal`, `VarDef`, and the expression-precedence rules are intentionally left unannotated so diagnostics stay tied to higher-value user-visible constructs rather than speculative helper failures.