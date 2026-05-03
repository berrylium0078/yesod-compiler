# SysY PEG Grammar

## 1. Source Grammar Notes

This document is the current integrated PEG design for the implemented SysY subset. It is still derived from the source EBNF in [doc/sysy.md](doc/sysy.md), but it is presented here as one final design rather than as a running sequence of deltas.

Assumptions for the current subset:

- one compilation unit contains one function definition
- only `int` functions are supported
- declarations are limited to scalar `const int` and `int`
- `ConstDef ::= IDENT "=" ConstInitVal`
- `VarDef ::= IDENT | IDENT "=" InitVal`
- `ConstInitVal ::= ConstExp`
- `InitVal ::= Exp`
- `LVal ::= IDENT`
- `Stmt ::= LVal "=" Exp ";" | [Exp] ";" | Block | "return" Exp ";"`
- `Block` is both a function body and a statement form, so nested blocks introduce nested scopes and may be empty because `BlockItem*` admits zero items
- function calls, arrays, brace initializers, and control-flow statements are out of scope
- whitespace and comments are handled entirely by the token layer and are not represented in the AST

The operator-precedence ladder is the usual SysY one:

- `Exp ::= LOrExp`
- `LOrExp ::= LAndExp | LOrExp "||" LAndExp`
- `LAndExp ::= EqExp | LAndExp "&&" EqExp`
- `EqExp ::= RelExp | EqExp ("==" | "!=") RelExp`
- `RelExp ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp`
- `AddExp ::= MulExp | AddExp ("+" | "-") MulExp`
- `MulExp ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp`
- `UnaryExp ::= PrimaryExp | UnaryOp UnaryExp`
- `PrimaryExp ::= "(" Exp ")" | LVal | Number`

## 2. Baseline Delta

This file now presents the current design as one unified grammar, but relative to the earlier baseline the meaningful changes are still:

- unchanged: the compilation-unit shell, declaration forms, expression precedence ladder, identifier and integer-literal tokenization, and trivia skipping
- changed: `Stmt` now includes assignment, expression statements, block statements, and return
- changed: `Block` is reachable recursively through `Stmt`, not only from `FuncDef`
- added: `ExpStmt` as the explicit helper for `[Exp] ";"`
- reordered: `Stmt` must distinguish `AssignStmt` from `ExpStmt`, because both may start with `IDENT`
- token layer: unchanged; existing `{`, `}`, `;`, and expression-head tokens are sufficient
- recovery: existing function-header, declaration, block, return, and parenthesized-primary recovery stay valid; block-item synchronization is widened to include block heads and expression-statement heads

## 3. Left-Recursion Elimination

The statement and declaration additions introduce no new left recursion. The PEG-sensitive rewrites are:

| Original shape | PEG shape | Note |
|---|---|---|
| `MulExp ::= UnaryExp | MulExp MulOp UnaryExp` | `MulExp <- UnaryExp (MulOp UnaryExp)*` | preserves left-associative multiplicative chains |
| `AddExp ::= MulExp | AddExp AddOp MulExp` | `AddExp <- MulExp (AddOp MulExp)*` | preserves additive precedence and left-to-right folding |
| `RelExp ::= AddExp | RelExp RelOp AddExp` | `RelExp <- AddExp (RelOp AddExp)*` | removes left recursion without changing precedence |
| `EqExp ::= RelExp | EqExp EqOp RelExp` | `EqExp <- RelExp (EqOp RelExp)*` | preserves equality chaining over relational expressions |
| `LAndExp ::= EqExp | LAndExp "&&" EqExp` | `LAndExp <- EqExp (ANDAND EqExp)*` | preserves logical-and precedence |
| `LOrExp ::= LAndExp | LOrExp "\|\|" LAndExp` | `LOrExp <- LAndExp (OROR LAndExp)*` | preserves logical-or precedence |
| `identifier ::= identifier-nondigit | identifier identifier-nondigit | identifier digit` | `Identifier <- IdentifierStart IdentifierContinue*` | same token language, no left recursion |
| `decimal-const ::= nonzero-digit | decimal-const digit` | `DecimalConst <- NonZeroDigit Digit*` | same decimal token language |
| `octal-const ::= "0" | octal-const octal-digit` | `OctalConst <- "0" OctalDigit*` | same octal token language |
| `hexadecimal-const ::= hexadecimal-prefix hexadecimal-digit | hexadecimal-const hexadecimal-digit` | `HexadecimalConst <- HexadecimalPrefix HexadecimalDigit+` | same hexadecimal token language |
| `ConstExp ::= Exp` | `ConstExp <- Exp` | direct alias; const-ness remains semantic |

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
ReturnStmt         <- KW_RETURN Exp SEMI
ExpStmt            <- Exp? SEMI

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

Ordered-choice-sensitive plain-grammar rules:

- `Stmt <- AssignStmt / Block / ReturnStmt / ExpStmt`
  `AssignStmt` must come before `ExpStmt` because both can begin with `IDENT`. `ExpStmt` stays last because it is the catch-all statement form.
- `UnaryExp <- PrimaryExp / UnaryOp UnaryExp`
  The order matches the source EBNF. Prefixes are disjoint in the current subset.
- `PrimaryExp <- LPAREN Exp RPAREN / LVal / Number`
  Prefixes are currently disjoint, but the `LVal` branch is intentionally documented because `IDENT`-led primary forms are a common future growth point.
- `RelOp <- LE / GE / LT / GT`
  Longer operators must precede shorter prefix-sharing ones.
- `IntegerConst <- HexadecimalConst / OctalConst / DecimalConst`
  Hexadecimal must precede octal because both can start with `0`.

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
ReturnStmt               <- KW_RETURN ^
                            (Exp / Throw<MalformedReturnValue> RecoverToStmtBoundary)
                            (SEMI / Throw<MissingSemicolon> RecoverToStmtBoundary)
ExpStmt                  <- SEMI
                          / Exp ^
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

RecoverToFuncHeaderEnd     <- (!")" !"{" !EOF .)* (RPAREN / &LBRACE / EOF)
RecoverToExprRParen        <- (!")" !";" !"}" !EOF .)* (RPAREN / &SEMI / &RBRACE / EOF)
RecoverToStmtBoundary      <- (!";" !"}" !EOF .)* (SEMI / &RBRACE / EOF)
RecoverToDeclBoundary      <- (!"," !";" !"}" !EOF .)* (COMMA / SEMI / &RBRACE / EOF)
RecoverToBlockItemBoundary <- (!KW_CONST !KW_INT !KW_RETURN !LBRACE !SEMI !LPAREN !IDENT !INT_CONST !PLUS !MINUS !BANG !RBRACE !EOF .)*
                              (&KW_CONST / &KW_INT / &KW_RETURN / &LBRACE / &SEMI / &LPAREN / &IDENT / &INT_CONST / &PLUS / &MINUS / &BANG / &RBRACE / EOF)
RecoverToBlockEnd          <- (!"}" !EOF .)* (RBRACE / EOF)

Digit                    <- [0-9]
NonZeroDigit             <- [1-9]
OctalDigit               <- [0-7]
HexadecimalDigit         <- [0-9A-Fa-f]
EOF                      <- !.
```

Recovery placement, intentionally kept small:

- `FuncDef`: commit after `(` because the construct is already determined
- `Block`: commit after `{` and recover at block-item boundaries or `}`
- `ConstDecl` and `VarDecl`: recover on declarator boundaries `,`, `;`, `}`
- `AssignStmt`: commit after `=` so failures stay assignment-shaped
- `ReturnStmt`: commit after `return` because no other statement shares that prefix
- `ExpStmt`: keep bare `;` cheap, but treat a parsed expression as committed when its trailing `;` is missing
- `PrimaryExp`: commit after `(` so malformed parenthesized primaries do not backtrack into `LVal` or `Number`
- helpers such as `LVal`, `VarDef`, `InitVal`, and precedence rules intentionally stay unlabeled

## 6. Recovery Inventory

| Label | Where thrown | Meaning | Recovery |
|---|---|---|---|
| `MissingFuncRParen` | `FuncDef` after `LPAREN` | function declarator is missing `)` | `RecoverToFuncHeaderEnd` |
| `MalformedBlockItem` | `Block` at a block-item start | block item is neither a valid declaration nor statement | `RecoverToBlockItemBoundary` |
| `MalformedDeclItem` | `ConstDecl` or `VarDecl` at a declarator position | declarator is malformed or missing | `RecoverToDeclBoundary` |
| `MissingDeclSemicolon` | `ConstDecl` or `VarDecl` after the declarator list | declaration is missing `;` | `RecoverToDeclBoundary` |
| `MalformedAssignValue` | `AssignStmt` after `=` | assignment rhs is malformed or missing | `RecoverToStmtBoundary` |
| `MissingAssignSemicolon` | `AssignStmt` after `Exp` | assignment is missing `;` | `RecoverToStmtBoundary` |
| `MalformedReturnValue` | `ReturnStmt` after `return` | return expression is malformed or missing | `RecoverToStmtBoundary` |
| `MissingSemicolon` | `ReturnStmt` or `ExpStmt` after `Exp` | statement is missing `;` | `RecoverToStmtBoundary` |
| `MalformedPrimaryExp` | `PrimaryExp` after `(` | parenthesized primary has no valid inner expression | `RecoverToExprRParen` |
| `MissingPrimaryRParen` | `PrimaryExp` after inner `Exp` | parenthesized primary is missing `)` | `RecoverToExprRParen` |
| `MissingRBrace` | `Block` after block items | block is missing `}` | `RecoverToBlockEnd` |

## 7. Ordered-Choice Notes

1. `Stmt`

```peg
Stmt <- AssignStmt / Block / ReturnStmt / ExpStmt
```

This is the main PEG-sensitive rule. `AssignStmt` must precede `ExpStmt` because both can begin with `IDENT`. `ExpStmt` must stay last because it is the fallback for empty statements and all remaining expression-headed statements. `Block` and `ReturnStmt` can stay ahead of it because `{` and `return` are prefix-disjoint.

2. `UnaryExp`

```peg
UnaryExp <- PrimaryExp / UnaryOp UnaryExp
```

This order preserves the source EBNF. In the current subset the prefixes are disjoint, so no cut is needed here.

3. `PrimaryExp`

```peg
PrimaryExp <- LPAREN Exp RPAREN / LVal / Number
```

The order is documented because `LVal` introduces an `IDENT`-headed primary form and because recovery commits after `(`. In the current subset the prefixes are still disjoint.

4. `RelOp`

```peg
RelOp <- LE / GE / LT / GT
```

Longer operators must come before their shorter prefixes.

5. `IntegerConst`

```peg
IntegerConst <- HexadecimalConst / OctalConst / DecimalConst
```

Hexadecimal must precede octal so `0x...` does not get consumed as plain octal `0`.

6. `Decl`

```peg
Decl <- ConstDecl / VarDecl
```

This order is safe because `const` and `int` are prefix-distinct.

7. `BlockItem`

```peg
BlockItem <- Decl / Stmt
```

This order is currently safe because declaration heads (`const`, `int`) are disjoint from statement heads (`{`, `return`, `;`, or an expression head), but it remains worth documenting because block-item dispatch is a common PEG growth point.

## 8. Short Design Rationale

The final grammar stays close to the source EBNF while making PEG-sensitive choices explicit only where needed. The main extension points are declarations, identifier-valued primaries, assignment statements, expression statements, and nested block statements. Everything else, especially the precedence ladder and token layer, remains structurally unchanged.

The recovery design is intentionally conservative. It attaches labels to outer user-visible constructs, not to low-level helpers, and uses only a few robust synchronization anchors: function-header end, statement boundary, declaration boundary, block-item boundary, `)`, and `}`. That keeps diagnostics concrete and implementation-friendly while avoiding speculative fine-grained recovery logic.