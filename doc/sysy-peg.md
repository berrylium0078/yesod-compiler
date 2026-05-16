# SysY PEG Grammar

## 1. Source Grammar Notes

This document is the current PEG design for the implemented SysY subset. For this revision, [doc/sysy.md](doc/sysy.md) remains the source EBNF and this file is treated as an incremental extension of the previous PEG baseline, adding only `while`, `break`, and `continue` to the existing statement grammar.

Assumptions for the current subset:

- one compilation unit contains one function definition
- only `int` functions are supported
- declarations are limited to scalar `const int` and `int`
- `ConstDef ::= IDENT "=" ConstInitVal`
- `VarDef ::= IDENT | IDENT "=" InitVal`
- grammar-level `ConstInitVal ::= ConstExp` remains a useful way to describe the language, but the implemented AST stores the child directly as `Exp`
- `InitVal ::= Exp`
- `LVal ::= IDENT`
- `Stmt ::= LVal "=" Exp ";" | [Exp] ";" | Block | "return" Exp ";" | "if" "(" Exp ")" Stmt ["else" Stmt] | "while" "(" Exp ")" Stmt | "break" ";" | "continue" ";"`
- `Block` is both a function body and a statement form, so nested blocks introduce nested scopes and may be empty because `BlockItem*` admits zero items
- function calls, arrays, and brace initializers are out of scope
- `if-else` is supported, and dangling `else` binds to the innermost `if`
- `while`, `break`, and `continue` are supported as statement forms; `break` and `continue` remain subject to semantic loop-context checks rather than syntax restrictions
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

Implementation gap to keep in mind:

- The parser continues to use the usual precedence-layer grammar (`LOrExp`, `LAndExp`, `EqExp`, `RelExp`, `AddExp`, `MulExp`) to ensure correct precedence and associativity during parsing.
- The implemented AST intentionally flattens those precedence layers: the AST represents every expression as a single `Exp` node (allocated in `Arena<Exp>`). `Exp::Kind` is a discriminated variant with the concrete payloads used by the implementation:
  - `Exp::Binary`: holds a `BinaryOpKeyword`, a `Handle<Exp>` for the left operand, and a `Handle<Exp>` for the right operand (binary operators are encoded by the enum `BinaryOpKeyword`).
  - `Exp::Unary`: holds a `UnaryOp` and a `Handle<Exp>` for its operand.
  - `LVal`: a payload for identifier-access expressions (stores the identifier and any necessary symbol handle).
  - `Number`: integer literal payload.
- Parentheses are parsed for grouping but do not produce a dedicated parenthesized-`PrimaryExp` node in the final AST; a parenthesized expression yields the inner `Exp` directly. Tools and tests should not rely on parentheses being preserved as AST nodes.
- `ConstExp` and other grammar-level aliases remain useful in the PEG and for reader documentation, but `ConstExp` is a grammar-only alias: constant-ness is discovered during semantic analysis and recorded in typed side-tables (e.g. `SemanticExpInfo`) rather than by a special AST node type.
- Consequences: tests, analyzers, and lowering passes must operate over `Exp::Kind` variants and typed `Handle<Exp>` keys. They must not assume separate `PrimaryExp`, `UnaryExp`, or `BinaryExp` arena identities or node-key types.

## 2. Baseline Delta

Relative to the previous `doc/sysy-peg.md` baseline, the delta for this revision is:

- unchanged: the compilation-unit shell, declaration forms, `if` / `if-else`, assignment and expression statements, the parser's expression precedence ladder, identifier and integer-literal tokenization, and trivia skipping
- changed: `Stmt` grows three new keyword-led alternatives for `while`, `break`, and `continue`
- added: `WhileStmt` as the explicit helper for `"while" "(" Exp ")" Stmt`
- added: `BreakStmt` and `ContinueStmt` as keyword-led simple statements
- reordered: `Stmt` keeps `AssignStmt` before `ExpStmt` for the existing `IDENT` ambiguity, while inserting `WhileStmt`, `BreakStmt`, and `ContinueStmt` ahead of the expression fallback because their prefixes are keyword-distinct
- token layer: add `while`, `break`, and `continue` keywords; existing tokens stay unchanged
- recovery: existing function-header, declaration, block, `if`, return, and parenthesized-primary recovery remain valid; block-item and statement-boundary synchronization must now recognize `while`, `break`, and `continue` heads
-- implementation note: the grammar still names precedence layers and `ConstExp`, but the AST now flattens binary precedence layers into a single `Exp` variant (see Implementation gap above). Constantness is determined by semantic analysis (constant folding) and recorded in semantic side-tables rather than as distinct AST node kinds.

## 3. Left-Recursion Elimination

The `while` and loop-control additions introduce no new left recursion. The PEG-sensitive rewrites are:

| Original shape | PEG shape | Note |
|---|---|---|
| `Stmt ::= ... | "if" "(" Exp ")" Stmt ["else" Stmt]` | `IfStmt <- KW_IF LPAREN Exp RPAREN Stmt (KW_ELSE Stmt)?` and `Stmt <- IfStmt / ...` | the optional `else` stays greedy on the recursive `Stmt`, so the nearest nested `if` consumes it first |
| `Stmt ::= ... | "while" "(" Exp ")" Stmt` | `WhileStmt <- KW_WHILE LPAREN Exp RPAREN Stmt` and `Stmt <- ... / WhileStmt / ...` | the source shape is already PEG-friendly, so it only needs a keyword-led helper |
| `Stmt ::= ... | "break" ";" | "continue" ";"` | `BreakStmt <- KW_BREAK SEMI` and `ContinueStmt <- KW_CONTINUE SEMI` | each source form is already PEG-friendly and prefix-distinct from other statement heads |
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
| `ConstExp ::= Exp` | `ConstExp <- Exp` | direct grammar alias; the implemented AST stores `Exp` directly and const-ness remains semantic |

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

Stmt               <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt
IfStmt             <- KW_IF LPAREN Exp RPAREN Stmt (KW_ELSE Stmt)?
WhileStmt          <- KW_WHILE LPAREN Exp RPAREN Stmt
BreakStmt          <- KW_BREAK SEMI
ContinueStmt       <- KW_CONTINUE SEMI
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
KW_BREAK           <- "break" !IdentifierContinue Spacing
KW_CONTINUE        <- "continue" !IdentifierContinue Spacing
KW_ELSE            <- "else" !IdentifierContinue Spacing
KW_IF              <- "if" !IdentifierContinue Spacing
KW_INT             <- "int" !IdentifierContinue Spacing
KW_RETURN          <- "return" !IdentifierContinue Spacing
KW_WHILE           <- "while" !IdentifierContinue Spacing
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

- `Stmt <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt`
  `AssignStmt` must come before `ExpStmt` because both can begin with `IDENT`. `IfStmt`, `WhileStmt`, `BreakStmt`, `ContinueStmt`, `Block`, and `ReturnStmt` are prefix-disjoint keyword or delimiter-led forms, so they can safely precede the catch-all `ExpStmt`.
- `IfStmt <- KW_IF LPAREN Exp RPAREN Stmt (KW_ELSE Stmt)?`
  The optional `else` suffix is intentionally greedy. Because the then-branch is a recursive `Stmt`, an inner `IfStmt` gets the first chance to consume `KW_ELSE`, which implements the standard dangling-`else` rule.
- `WhileStmt <- KW_WHILE LPAREN Exp RPAREN Stmt`
  The `while` head is prefix-distinct, so it can sit before identifier-led and expression-led statements without changing their parse. Its body stays a full `Stmt`, which keeps nested `if`, nested `while`, and block bodies aligned with the source grammar.
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

Stmt                     <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt
IfStmt                   <- KW_IF LPAREN ^
                            (Exp / Throw<MalformedIfCond> RecoverToIfStmtHead)
                            (RPAREN / Throw<MissingIfRParen> RecoverToIfStmtHead)
                            (Stmt / Throw<MalformedIfThenStmt> RecoverToStmtBoundary)
                            (KW_ELSE ^
                              (Stmt / Throw<MalformedElseStmt> RecoverToStmtBoundary))?
WhileStmt                <- KW_WHILE LPAREN ^
                            (Exp / Throw<MalformedWhileCond> RecoverToWhileStmtHead)
                            (RPAREN / Throw<MissingWhileRParen> RecoverToWhileStmtHead)
                            (Stmt / Throw<MalformedWhileBody> RecoverToStmtBoundary)
BreakStmt                <- KW_BREAK ^
                            (SEMI / Throw<MissingBreakSemicolon> RecoverToStmtBoundary)
ContinueStmt             <- KW_CONTINUE ^
                            (SEMI / Throw<MissingContinueSemicolon> RecoverToStmtBoundary)
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
KW_BREAK                 <- "break" !IdentifierContinue Spacing
KW_CONTINUE              <- "continue" !IdentifierContinue Spacing
KW_ELSE                  <- "else" !IdentifierContinue Spacing
KW_IF                    <- "if" !IdentifierContinue Spacing
KW_INT                   <- "int" !IdentifierContinue Spacing
KW_RETURN                <- "return" !IdentifierContinue Spacing
KW_WHILE                 <- "while" !IdentifierContinue Spacing
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
RecoverToIfStmtHead        <- (!")" !LBRACE !RBRACE !KW_IF !KW_WHILE !KW_BREAK !KW_CONTINUE !KW_RETURN !KW_ELSE !SEMI !LPAREN !IDENT !INT_CONST !PLUS !MINUS !BANG !EOF .)*
                              (RPAREN / &LBRACE / &RBRACE / &KW_IF / &KW_WHILE / &KW_BREAK / &KW_CONTINUE / &KW_RETURN / &KW_ELSE / &SEMI / &LPAREN / &IDENT / &INT_CONST / &PLUS / &MINUS / &BANG / EOF)
RecoverToWhileStmtHead     <- (!")" !LBRACE !RBRACE !KW_IF !KW_WHILE !KW_BREAK !KW_CONTINUE !KW_RETURN !KW_ELSE !SEMI !LPAREN !IDENT !INT_CONST !PLUS !MINUS !BANG !EOF .)*
                              (RPAREN / &LBRACE / &RBRACE / &KW_IF / &KW_WHILE / &KW_BREAK / &KW_CONTINUE / &KW_RETURN / &KW_ELSE / &SEMI / &LPAREN / &IDENT / &INT_CONST / &PLUS / &MINUS / &BANG / EOF)
RecoverToStmtBoundary      <- (!";" !"}" !KW_ELSE !KW_IF !KW_WHILE !KW_BREAK !KW_CONTINUE !KW_RETURN !EOF .)*
                              (SEMI / &RBRACE / &KW_ELSE / &KW_IF / &KW_WHILE / &KW_BREAK / &KW_CONTINUE / &KW_RETURN / EOF)
RecoverToDeclBoundary      <- (!"," !";" !"}" !EOF .)* (COMMA / SEMI / &RBRACE / EOF)
RecoverToBlockItemBoundary <- (!KW_CONST !KW_INT !KW_IF !KW_WHILE !KW_BREAK !KW_CONTINUE !KW_RETURN !LBRACE !SEMI !LPAREN !IDENT !INT_CONST !PLUS !MINUS !BANG !RBRACE !EOF .)*
                              (&KW_CONST / &KW_INT / &KW_IF / &KW_WHILE / &KW_BREAK / &KW_CONTINUE / &KW_RETURN / &LBRACE / &SEMI / &LPAREN / &IDENT / &INT_CONST / &PLUS / &MINUS / &BANG / &RBRACE / EOF)
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
- `IfStmt`: commit after `if (` because the construct is already determined; recover the condition to `)` or the next statement head, and stop statement recovery at `else`
- `WhileStmt`: commit after `while (` because the construct is already determined; recover the condition to `)` or the next statement head, then recover the body like any other statement
- `BreakStmt` and `ContinueStmt`: commit after the keyword because no other statement shares that prefix, so a missing `;` should not fall through into another statement form
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
| `MalformedIfCond` | `IfStmt` after `if (` | `if` condition is malformed or missing | `RecoverToIfStmtHead` |
| `MissingIfRParen` | `IfStmt` after condition `Exp` | `if` condition is missing `)` | `RecoverToIfStmtHead` |
| `MalformedIfThenStmt` | `IfStmt` after `)` | then-branch statement is malformed or missing | `RecoverToStmtBoundary` |
| `MalformedElseStmt` | `IfStmt` after `else` | else-branch statement is malformed or missing | `RecoverToStmtBoundary` |
| `MalformedWhileCond` | `WhileStmt` after `while (` | `while` condition is malformed or missing | `RecoverToWhileStmtHead` |
| `MissingWhileRParen` | `WhileStmt` after condition `Exp` | `while` condition is missing `)` | `RecoverToWhileStmtHead` |
| `MalformedWhileBody` | `WhileStmt` after `)` | loop body statement is malformed or missing | `RecoverToStmtBoundary` |
| `MissingBreakSemicolon` | `BreakStmt` after `break` | `break` statement is missing `;` | `RecoverToStmtBoundary` |
| `MissingContinueSemicolon` | `ContinueStmt` after `continue` | `continue` statement is missing `;` | `RecoverToStmtBoundary` |
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
Stmt <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt
```

This is the main PEG-sensitive rule. `AssignStmt` must precede `ExpStmt` because both can begin with `IDENT`. `ExpStmt` must stay last because it is the fallback for empty statements and all remaining expression-headed statements. `IfStmt`, `WhileStmt`, `BreakStmt`, `ContinueStmt`, `Block`, and `ReturnStmt` can stay ahead of it because `if`, `while`, `break`, `continue`, `{`, and `return` are prefix-disjoint.

2. `IfStmt`

```peg
IfStmt <- KW_IF LPAREN Exp RPAREN Stmt (KW_ELSE Stmt)?
```

This rule is PEG-sensitive because of dangling `else`. The order inside the rule is fixed by the source grammar, and the optional `KW_ELSE Stmt` must remain on the same production as the recursive then-branch. If the grammar were split into separate `if-without-else` and `if-with-else` choices in the wrong order, PEG would let the shorter branch succeed too early and strand a later `else`. No extra cut is needed for the plain grammar; the greedy optional suffix plus recursive `Stmt` already gives the innermost `if` the first chance to consume `else`.

3. `WhileStmt`

```peg
WhileStmt <- KW_WHILE LPAREN Exp RPAREN Stmt
```

This rule is only mildly order-sensitive: it must remain ahead of `ExpStmt` because `while` should be recognized as a reserved statement head, not as an expression-starting identifier. No additional cut is needed in the plain grammar because `KW_WHILE LPAREN` already determines the construct.

4. `UnaryExp`

```peg
UnaryExp <- PrimaryExp / UnaryOp UnaryExp
```

This order preserves the source EBNF. In the current subset the prefixes are disjoint, so no cut is needed here.

5. `PrimaryExp`

```peg
PrimaryExp <- LPAREN Exp RPAREN / LVal / Number
```

The order is documented because `LVal` introduces an `IDENT`-headed primary form and because recovery commits after `(`. In the current subset the prefixes are still disjoint.

6. `RelOp`

```peg
RelOp <- LE / GE / LT / GT
```

Longer operators must come before their shorter prefixes.

7. `IntegerConst`

```peg
IntegerConst <- HexadecimalConst / OctalConst / DecimalConst
```

Hexadecimal must precede octal so `0x...` does not get consumed as plain octal `0`.

8. `Decl`

```peg
Decl <- ConstDecl / VarDecl
```

This order is safe because `const` and `int` are prefix-distinct.

9. `BlockItem`

```peg
BlockItem <- Decl / Stmt
```

This order is currently safe because declaration heads (`const`, `int`) are disjoint from statement heads (`if`, `{`, `return`, `;`, or an expression head), but it remains worth documenting because block-item dispatch is a common PEG growth point.

## 8. Short Design Rationale

The final grammar stays close to the source EBNF while making PEG-sensitive choices explicit only where needed. This revision is a minimal extension of the existing statement grammar: it adds one recursive loop form and two simple loop-control statements without disturbing declarations, expressions, or the established `if-else` shape. The parser still uses the standard precedence ladder, and the only token-layer delta is the addition of `while`, `break`, and `continue`.

The implementation intentionally diverges from the grammar in the shape of the expression AST. The grammar keeps named precedence layers because they remain the clearest way to describe parsing behavior and PEG rewrites, but the AST now records only generic binary operators and unary structure. Likewise, `ConstExp` remains useful in the grammar as a language-level alias, while the implementation treats constant-ness as a semantic property discovered by folding expressions rather than as a separate AST node category.

The recovery design is intentionally conservative. It attaches labels to outer user-visible constructs, not to low-level helpers, and uses only a few robust synchronization anchors: function-header end, statement boundary, declaration boundary, block-item boundary, `)`, `}`, and statement heads for `if` and `while`. Stopping statement recovery at `else` remains the key `if`-specific safeguard, while recognizing `while`, `break`, and `continue` as statement starts prevents loop-control syntax errors from cascading into the following statement.