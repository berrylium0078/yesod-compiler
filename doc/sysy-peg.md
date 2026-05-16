# SysY PEG Grammar

## 1. Source Grammar Notes

This document is the PEG-oriented design for the next SysY extension. The source EBNF remains [doc/sysy.md](doc/sysy.md), and this revision is an incremental extension of the existing PEG baseline in order to support:

- compilation units with multiple top-level items
- global `const` and `int` declarations
- function definitions with `int` or `void` return type
- function parameter lists
- `return;` as well as `return Exp;`
- function calls in expressions

Assumptions carried forward from the existing token layer unless noted below:

- declarations remain scalar only: `ConstDef ::= IDENT "=" ConstInitVal` and `VarDef ::= IDENT | IDENT "=" InitVal`
- `ConstInitVal ::= ConstExp` and `InitVal ::= Exp`
- `LVal ::= IDENT`
- blocks still contain `Decl` or `Stmt`, and nested blocks introduce nested scopes
- arrays and brace initializers remain out of scope
- whitespace and comments are handled entirely by the token layer
- `break` and `continue` remain syntactically unrestricted and are rejected semantically outside loops

The source-language delta to incorporate is:

```text
CompUnit    ::= [CompUnit] (Decl | FuncDef);

FuncDef     ::= FuncType IDENT "(" [FuncFParams] ")" Block;
FuncType    ::= "void" | "int";
FuncFParams ::= FuncFParam {"," FuncFParam};
FuncFParam  ::= BType IDENT;

Stmt        ::= ...
              | "return" [Exp] ";";

UnaryExp    ::= ...
              | IDENT "(" [FuncRParams] ")"
              | ...;
FuncRParams ::= Exp {"," Exp};
```

The expression-precedence ladder is still the usual SysY one:

- `Exp ::= LOrExp`
- `LOrExp ::= LAndExp | LOrExp "||" LAndExp`
- `LAndExp ::= EqExp | LAndExp "&&" EqExp`
- `EqExp ::= RelExp | EqExp ("==" | "!=") RelExp`
- `RelExp ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp`
- `AddExp ::= MulExp | AddExp ("+" | "-") MulExp`
- `MulExp ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp`
- `UnaryExp ::= PrimaryExp | IDENT "(" [FuncRParams] ")" | UnaryOp UnaryExp`
- `PrimaryExp ::= "(" Exp ")" | LVal | Number`

Implementation note:

- The grammar still uses named precedence layers (`LOrExp`, `AddExp`, `MulExp`, `UnaryExp`, and so on) because they are the clearest way to explain precedence and PEG rewrites.
- The runtime AST is intentionally flatter. Expressions are still represented by one arena-backed `Exp` node shape plus payload variants such as binary, unary, lvalue, and number.
- To represent the new call syntax cleanly, the current `Exp` payload set should grow a call payload, for example `struct Exp::Call { Handle<Identifier> m_func_nn; std::vector<Handle<Exp>> m_params; };`.
- `ConstExp` remains a grammar alias, not a distinct runtime AST node. Whether an expression is constant is still a semantic property discovered during analysis and constant folding.
- This grammar extension also implies non-expression AST growth outside `Exp`: `FuncTypeKeyword` must add `void`, and `CompUnit` must stop assuming a single `FuncDef` so it can preserve the ordered list of top-level declarations and function definitions.

## 2. Baseline Delta

Relative to the previous [doc/sysy-peg.md](doc/sysy-peg.md) baseline:

- unchanged: local declaration syntax, scalar declarators, block structure, `if` / `while` / `break` / `continue`, the precedence ladder above `UnaryExp`, tokenization of identifiers and integer literals, and the general synchronization strategy based on delimiters and statement or declaration boundaries
- changed: `CompUnit` no longer means exactly one function definition; it now accepts a non-empty sequence of top-level `Decl` or `FuncDef`
- changed: `FuncDef` now accepts `void` as well as `int`, and allows an optional parameter list
- changed: `Stmt` keeps the existing statement alternatives but changes `return` from mandatory-expression form to optional-expression form
- changed: `UnaryExp` grows the call form `IDENT "(" [FuncRParams] ")"`, which must bind tighter than binary operators and must be recognized before the `LVal`-headed primary-expression branch
- added: `FuncFParams`, `FuncFParam`, `FuncRParams`, `CallExp`, and a top-level helper to describe `Decl | FuncDef` sequencing cleanly in PEG
- reordered: ordered choice now matters for `TopLevelItem`, `Stmt`, and especially `UnaryExp`
- token layer changes: add `void`; all other lexical classes stay the same
- recovery updates: the existing block, statement-boundary, declaration-boundary, and parenthesized-expression recovery remain valid, but top-level synchronization must now recognize `void` as a function head and call parsing adds one more high-value missing-`)` site

## 3. Left-Recursion Elimination

The new extension adds one PEG-incompatible repetition at the compilation-unit level and preserves the existing precedence rewrites.

| Original shape | Rewritten PEG shape | Why it preserves the parse |
|---|---|---|
| `CompUnit ::= [CompUnit] (Decl | FuncDef)` | `CompUnit <- TopLevelItem+` with `TopLevelItem <- ConstDecl / FuncDef / VarDecl` | the source rule denotes a non-empty left-recursive list of top-level items; `TopLevelItem+` preserves the same ordered sequence without left recursion |
| `FuncFParams ::= FuncFParam {"," FuncFParam}` | `FuncFParams <- FuncFParam (COMMA FuncFParam)*` | preserves a non-empty comma-separated list of formal parameters |
| `FuncRParams ::= Exp {"," Exp}` | `FuncRParams <- Exp (COMMA Exp)*` | preserves a non-empty comma-separated list of actual arguments |
| `MulExp ::= UnaryExp | MulExp MulOp UnaryExp` | `MulExp <- UnaryExp (MulOp UnaryExp)*` | preserves left-associative multiplicative chains |
| `AddExp ::= MulExp | AddExp AddOp MulExp` | `AddExp <- MulExp (AddOp MulExp)*` | preserves additive precedence and left-to-right folding |
| `RelExp ::= AddExp | RelExp RelOp AddExp` | `RelExp <- AddExp (RelOp AddExp)*` | removes left recursion without changing precedence |
| `EqExp ::= RelExp | EqExp EqOp RelExp` | `EqExp <- RelExp (EqOp RelExp)*` | preserves equality precedence over logical operators |
| `LAndExp ::= EqExp | LAndExp "&&" EqExp` | `LAndExp <- EqExp (ANDAND EqExp)*` | preserves logical-and chaining |
| `LOrExp ::= LAndExp | LOrExp "||" LAndExp` | `LOrExp <- LAndExp (OROR LAndExp)*` | preserves logical-or chaining |
| `ConstExp ::= Exp` | `ConstExp <- Exp` | remains a grammar alias; const-ness is semantic, not syntactic |

The call extension does not introduce left recursion, but it does introduce a PEG-sensitive overlap: `IDENT "(" ... ")"` and `LVal ::= IDENT` share the same prefix, so the call form must be checked before the plain identifier primary form.

## 4. Plain PEG Grammar

```peg
CompUnit           <- Spacing TopLevelItem+ EOF
TopLevelItem       <- ConstDecl / FuncDef / VarDecl

FuncDef            <- FuncType IDENT LPAREN FuncFParams? RPAREN Block
FuncType           <- KW_VOID / KW_INT
FuncFParams        <- FuncFParam (COMMA FuncFParam)*
FuncFParam         <- BType IDENT

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
ReturnStmt         <- KW_RETURN Exp? SEMI
ExpStmt            <- Exp? SEMI

Exp                <- LOrExp
LOrExp             <- LAndExp (OROR LAndExp)*
LAndExp            <- EqExp (ANDAND EqExp)*
EqExp              <- RelExp (EqOp RelExp)*
RelExp             <- AddExp (RelOp AddExp)*
AddExp             <- MulExp (AddOp MulExp)*
MulExp             <- UnaryExp (MulOp UnaryExp)*
UnaryExp           <- CallExp / PrimaryExp / UnaryOp UnaryExp
CallExp            <- IDENT LPAREN FuncRParams? RPAREN
FuncRParams        <- Exp (COMMA Exp)*
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
KW_VOID            <- "void" !IdentifierContinue Spacing
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

Ordered-choice-sensitive rules in the plain grammar:

- `TopLevelItem <- ConstDecl / FuncDef / VarDecl`
  `ConstDecl` is prefix-distinct because of `const`. `FuncDef` must precede `VarDecl` so an `int`-headed function definition is recognized as a function before the declaration branch can commit in the recovery grammar.
- `Stmt <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt`
  `AssignStmt` must stay before `ExpStmt` because both may start with `IDENT`. Keyword-led and brace-led forms can safely precede the fallback expression statement.
- `UnaryExp <- CallExp / PrimaryExp / UnaryOp UnaryExp`
  `CallExp` must precede `PrimaryExp` because both can start with `IDENT`; otherwise an identifier-headed call would be consumed as `LVal` and the following `(` would be stranded.
- `PrimaryExp <- LPAREN Exp RPAREN / LVal / Number`
  The prefixes are currently disjoint, but this rule remains a documented hotspot because `LVal` is the plain identifier branch that overlaps with potential future postfix forms.
- `RelOp <- LE / GE / LT / GT`
  Longer operators must precede shorter prefix-sharing ones.
- `IntegerConst <- HexadecimalConst / OctalConst / DecimalConst`
  Hexadecimal must precede octal so `0x...` is not consumed as octal `0`.

## 5. Recovery-Annotated PEG Grammar

```peg
CompUnit                   <- Spacing
                              (TopLevelItem / Throw<MalformedTopLevelItem> RecoverToTopLevelBoundary)+
                              EOF
TopLevelItem               <- ConstDecl / FuncDef / VarDecl

FuncDef                    <- FuncType IDENT LPAREN ^
                              (RPAREN
                              / FuncFParams
                                (RPAREN / Throw<MissingFuncRParen> RecoverToFuncHeaderEnd)
                              / Throw<MalformedFuncParam> RecoverToFuncHeaderEnd)
                              Block
FuncType                   <- KW_VOID / KW_INT
FuncFParams                <- FuncFParam
                              (COMMA (FuncFParam / Throw<MalformedFuncParam> RecoverToParamBoundary))*
FuncFParam                 <- BType IDENT

Block                      <- LBRACE ^
                              (BlockItem / Throw<MalformedBlockItem> RecoverToBlockItemBoundary)*
                              (RBRACE / Throw<MissingRBrace> RecoverToBlockEnd)
BlockItem                  <- Decl / Stmt

Decl                       <- ConstDecl / VarDecl
ConstDecl                  <- KW_CONST ^
                              BType
                              (ConstDef / Throw<MalformedDeclItem> RecoverToDeclBoundary)
                              (COMMA (ConstDef / Throw<MalformedDeclItem> RecoverToDeclBoundary))*
                              (SEMI / Throw<MissingDeclSemicolon> RecoverToDeclBoundary)
VarDecl                    <- BType ^
                              (VarDef / Throw<MalformedDeclItem> RecoverToDeclBoundary)
                              (COMMA (VarDef / Throw<MalformedDeclItem> RecoverToDeclBoundary))*
                              (SEMI / Throw<MissingDeclSemicolon> RecoverToDeclBoundary)
BType                      <- KW_INT
ConstDef                   <- IDENT ASSIGN ConstInitVal
ConstInitVal               <- ConstExp
VarDef                     <- IDENT (ASSIGN InitVal)?
InitVal                    <- Exp
ConstExp                   <- Exp

Stmt                       <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt
IfStmt                     <- KW_IF LPAREN ^
                              (Exp / Throw<MalformedIfCond> RecoverToIfStmtHead)
                              (RPAREN / Throw<MissingIfRParen> RecoverToIfStmtHead)
                              (Stmt / Throw<MalformedIfThenStmt> RecoverToStmtBoundary)
                              (KW_ELSE ^
                                (Stmt / Throw<MalformedElseStmt> RecoverToStmtBoundary))?
WhileStmt                  <- KW_WHILE LPAREN ^
                              (Exp / Throw<MalformedWhileCond> RecoverToWhileStmtHead)
                              (RPAREN / Throw<MissingWhileRParen> RecoverToWhileStmtHead)
                              (Stmt / Throw<MalformedWhileBody> RecoverToStmtBoundary)
BreakStmt                  <- KW_BREAK ^
                              (SEMI / Throw<MissingBreakSemicolon> RecoverToStmtBoundary)
ContinueStmt               <- KW_CONTINUE ^
                              (SEMI / Throw<MissingContinueSemicolon> RecoverToStmtBoundary)
AssignStmt                 <- LVal ASSIGN ^
                              (Exp / Throw<MalformedAssignValue> RecoverToStmtBoundary)
                              (SEMI / Throw<MissingAssignSemicolon> RecoverToStmtBoundary)
ReturnStmt                 <- KW_RETURN ^
                              (SEMI
                              / (Exp / Throw<MalformedReturnValue> RecoverToStmtBoundary)
                                (SEMI / Throw<MissingReturnSemicolon> RecoverToStmtBoundary))
ExpStmt                    <- SEMI
                            / Exp ^
                              (SEMI / Throw<MissingSemicolon> RecoverToStmtBoundary)

Exp                        <- LOrExp
LOrExp                     <- LAndExp (OROR LAndExp)*
LAndExp                    <- EqExp (ANDAND EqExp)*
EqExp                      <- RelExp (EqOp RelExp)*
RelExp                     <- AddExp (RelOp AddExp)*
AddExp                     <- MulExp (AddOp MulExp)*
MulExp                     <- UnaryExp (MulOp UnaryExp)*
UnaryExp                   <- CallExp / PrimaryExp / UnaryOp UnaryExp
CallExp                    <- IDENT LPAREN ^
                              (RPAREN
                              / FuncRParams
                                (RPAREN / Throw<MissingCallRParen> RecoverToCallEnd)
                              / Throw<MalformedCallArg> RecoverToCallEnd)
FuncRParams                <- Exp
                              (COMMA (Exp / Throw<MalformedCallArg> RecoverToArgBoundary))*
PrimaryExp                 <- LPAREN ^
                              (Exp / Throw<MalformedPrimaryExp> RecoverToExprRParen)
                              (RPAREN / Throw<MissingPrimaryRParen> RecoverToExprRParen)
                            / LVal
                            / Number
LVal                       <- IDENT
Number                     <- INT_CONST
UnaryOp                    <- PLUS / MINUS / BANG
MulOp                      <- STAR / SLASH / PERCENT
AddOp                      <- PLUS / MINUS
RelOp                      <- LE / GE / LT / GT
EqOp                       <- EQEQ / NE

IDENT                      <- Identifier Spacing
INT_CONST                  <- IntegerConst Spacing

Identifier                 <- IdentifierStart IdentifierContinue*
IdentifierStart            <- [_A-Za-z]
IdentifierContinue         <- IdentifierStart / Digit

IntegerConst               <- HexadecimalConst / OctalConst / DecimalConst
DecimalConst               <- NonZeroDigit Digit*
OctalConst                 <- "0" OctalDigit*
HexadecimalConst           <- HexadecimalPrefix HexadecimalDigit+
HexadecimalPrefix          <- "0x" / "0X"

KW_CONST                   <- "const" !IdentifierContinue Spacing
KW_BREAK                   <- "break" !IdentifierContinue Spacing
KW_CONTINUE                <- "continue" !IdentifierContinue Spacing
KW_ELSE                    <- "else" !IdentifierContinue Spacing
KW_IF                      <- "if" !IdentifierContinue Spacing
KW_INT                     <- "int" !IdentifierContinue Spacing
KW_RETURN                  <- "return" !IdentifierContinue Spacing
KW_VOID                    <- "void" !IdentifierContinue Spacing
KW_WHILE                   <- "while" !IdentifierContinue Spacing
LPAREN                     <- "(" Spacing
RPAREN                     <- ")" Spacing
LBRACE                     <- "{" Spacing
RBRACE                     <- "}" Spacing
SEMI                       <- ";" Spacing
COMMA                      <- "," Spacing
ASSIGN                     <- "=" Spacing
PLUS                       <- "+" Spacing
MINUS                      <- "-" Spacing
BANG                       <- "!" Spacing
STAR                       <- "*" Spacing
SLASH                      <- "/" Spacing
PERCENT                    <- "%" Spacing
LE                         <- "<=" Spacing
GE                         <- ">=" Spacing
LT                         <- "<" Spacing
GT                         <- ">" Spacing
EQEQ                       <- "==" Spacing
NE                         <- "!=" Spacing
ANDAND                     <- "&&" Spacing
OROR                       <- "||" Spacing

Spacing                    <- (WhiteSpace / Comment)*
WhiteSpace                 <- [ \t\r\n]+
Comment                    <- LineComment / BlockComment
LineComment                <- "//" (!EndOfLine .)* EndOfLine?
BlockComment               <- "/*" (!"*/" .)* "*/"
EndOfLine                  <- "\r\n" / "\n" / "\r"

RecoverToTopLevelBoundary    <- (!KW_CONST !KW_INT !KW_VOID !EOF .)*
                                (&KW_CONST / &KW_INT / &KW_VOID / EOF)
RecoverToFuncHeaderEnd       <- (!")" !"{" !EOF .)* (RPAREN / &LBRACE / EOF)
RecoverToParamBoundary       <- (!"," !")" !EOF .)* (COMMA / RPAREN / EOF)
RecoverToCallEnd             <- (!")" !";" !"}" !"," !EOF .)* (RPAREN / &SEMI / &RBRACE / &COMMA / EOF)
RecoverToArgBoundary         <- (!"," !")" !";" !"}" !EOF .)* (COMMA / RPAREN / &SEMI / &RBRACE / EOF)
RecoverToExprRParen          <- (!")" !";" !"}" !EOF .)* (RPAREN / &SEMI / &RBRACE / EOF)
RecoverToIfStmtHead          <- (!")" !LBRACE !RBRACE !KW_IF !KW_WHILE !KW_BREAK !KW_CONTINUE !KW_RETURN !KW_ELSE !SEMI !LPAREN !IDENT !INT_CONST !PLUS !MINUS !BANG !EOF .)*
                                (RPAREN / &LBRACE / &RBRACE / &KW_IF / &KW_WHILE / &KW_BREAK / &KW_CONTINUE / &KW_RETURN / &KW_ELSE / &SEMI / &LPAREN / &IDENT / &INT_CONST / &PLUS / &MINUS / &BANG / EOF)
RecoverToWhileStmtHead       <- (!")" !LBRACE !RBRACE !KW_IF !KW_WHILE !KW_BREAK !KW_CONTINUE !KW_RETURN !KW_ELSE !SEMI !LPAREN !IDENT !INT_CONST !PLUS !MINUS !BANG !EOF .)*
                                (RPAREN / &LBRACE / &RBRACE / &KW_IF / &KW_WHILE / &KW_BREAK / &KW_CONTINUE / &KW_RETURN / &KW_ELSE / &SEMI / &LPAREN / &IDENT / &INT_CONST / &PLUS / &MINUS / &BANG / EOF)
RecoverToStmtBoundary        <- (!";" !"}" !KW_ELSE !KW_IF !KW_WHILE !KW_BREAK !KW_CONTINUE !KW_RETURN !EOF .)*
                                (SEMI / &RBRACE / &KW_ELSE / &KW_IF / &KW_WHILE / &KW_BREAK / &KW_CONTINUE / &KW_RETURN / EOF)
RecoverToDeclBoundary        <- (!"," !";" !"}" !EOF .)* (COMMA / SEMI / &RBRACE / EOF)
RecoverToBlockItemBoundary   <- (!KW_CONST !KW_INT !KW_IF !KW_WHILE !KW_BREAK !KW_CONTINUE !KW_RETURN !LBRACE !SEMI !LPAREN !IDENT !INT_CONST !PLUS !MINUS !BANG !RBRACE !EOF .)*
                                (&KW_CONST / &KW_INT / &KW_IF / &KW_WHILE / &KW_BREAK / &KW_CONTINUE / &KW_RETURN / &LBRACE / &SEMI / &LPAREN / &IDENT / &INT_CONST / &PLUS / &MINUS / &BANG / &RBRACE / EOF)
RecoverToBlockEnd            <- (!"}" !EOF .)* (RBRACE / EOF)

Digit                      <- [0-9]
NonZeroDigit               <- [1-9]
OctalDigit                 <- [0-7]
HexadecimalDigit           <- [0-9A-Fa-f]
EOF                        <- !.
```

Recovery placement is still deliberately small and outer-construct focused:

- `FuncDef`: commit after `(` because the declaration has already been identified as a function header; the only high-value diagnostic inside is malformed parameters or a missing `)`
- `ReturnStmt`: keep `return;` cheap, but once a return expression starts, treat a missing trailing `;` as a return-shaped error rather than falling through to another statement form
- `CallExp`: commit after `(` because `IDENT (` already determines a call, and a missing `)` is a high-value diagnostic that would otherwise cascade badly
- existing block, declaration, `if`, `while`, assignment, and parenthesized-expression recovery remain valid with only the synchronization sets widened where the new grammar introduces `void` or call argument separators

## 6. Recovery Inventory

| Label | Where it is thrown | Meaning | Recovery |
|---|---|---|---|
| `MalformedTopLevelItem` | `CompUnit` at a top-level item start | top-level text is neither a valid declaration nor function definition | `RecoverToTopLevelBoundary` |
| `MissingFuncRParen` | `FuncDef` after the optional formal-parameter list | function header is missing `)` | `RecoverToFuncHeaderEnd` |
| `MalformedFuncParam` | `FuncFParams` after `,` | a formal parameter is malformed or missing | `RecoverToParamBoundary` |
| `MalformedBlockItem` | `Block` at a block-item start | block item is neither a valid declaration nor statement | `RecoverToBlockItemBoundary` |
| `MalformedDeclItem` | `ConstDecl` or `VarDecl` at a declarator position | declarator is malformed or missing | `RecoverToDeclBoundary` |
| `MissingDeclSemicolon` | `ConstDecl` or `VarDecl` after the declarator list | declaration is missing `;` | `RecoverToDeclBoundary` |
| `MalformedIfCond` | `IfStmt` after `if (` | `if` condition is malformed or missing | `RecoverToIfStmtHead` |
| `MissingIfRParen` | `IfStmt` after the condition expression | `if` condition is missing `)` | `RecoverToIfStmtHead` |
| `MalformedIfThenStmt` | `IfStmt` after `)` | then-branch statement is malformed or missing | `RecoverToStmtBoundary` |
| `MalformedElseStmt` | `IfStmt` after `else` | else-branch statement is malformed or missing | `RecoverToStmtBoundary` |
| `MalformedWhileCond` | `WhileStmt` after `while (` | `while` condition is malformed or missing | `RecoverToWhileStmtHead` |
| `MissingWhileRParen` | `WhileStmt` after the condition expression | `while` condition is missing `)` | `RecoverToWhileStmtHead` |
| `MalformedWhileBody` | `WhileStmt` after `)` | loop body statement is malformed or missing | `RecoverToStmtBoundary` |
| `MissingBreakSemicolon` | `BreakStmt` after `break` | `break` statement is missing `;` | `RecoverToStmtBoundary` |
| `MissingContinueSemicolon` | `ContinueStmt` after `continue` | `continue` statement is missing `;` | `RecoverToStmtBoundary` |
| `MalformedAssignValue` | `AssignStmt` after `=` | assignment rhs is malformed or missing | `RecoverToStmtBoundary` |
| `MissingAssignSemicolon` | `AssignStmt` after the rhs expression | assignment is missing `;` | `RecoverToStmtBoundary` |
| `MalformedReturnValue` | `ReturnStmt` after `return` when `;` is not next | return expression is malformed or missing | `RecoverToStmtBoundary` |
| `MissingReturnSemicolon` | `ReturnStmt` after a parsed return expression | return statement is missing `;` | `RecoverToStmtBoundary` |
| `MissingSemicolon` | `ExpStmt` after an expression | expression statement is missing `;` | `RecoverToStmtBoundary` |
| `MissingCallRParen` | `CallExp` after the optional argument list | function call is missing `)` | `RecoverToCallEnd` |
| `MalformedCallArg` | `FuncRParams` after `,` | call argument is malformed or missing | `RecoverToArgBoundary` |
| `MalformedPrimaryExp` | `PrimaryExp` after `(` | parenthesized expression has no valid inner expression | `RecoverToExprRParen` |
| `MissingPrimaryRParen` | `PrimaryExp` after the inner expression | parenthesized expression is missing `)` | `RecoverToExprRParen` |
| `MissingRBrace` | `Block` after block items | block is missing `}` | `RecoverToBlockEnd` |

## 7. Ordered-Choice Notes

1. `TopLevelItem`

```peg
TopLevelItem <- ConstDecl / FuncDef / VarDecl
```

This rule becomes PEG-sensitive once global declarations and multiple functions are allowed. `ConstDecl` is safely first because `const` is unique. The subtle case is `FuncDef` versus `VarDecl`: both can start with `int IDENT`. Keeping `FuncDef` before `VarDecl` means `int f(...) { ... }` is recognized as a function definition before declaration recovery can commit to a variable declaration shape. If the order were reversed and declaration parsing cut too early, malformed or even valid function headers could be misreported as declaration errors.

2. `Stmt`

```peg
Stmt <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt
```

`AssignStmt` must remain before `ExpStmt` because both may begin with `IDENT`. `ReturnStmt` can stay before `ExpStmt` because `return` is reserved and prefix-distinct. No new cut is needed beyond the existing ones on keyword-led statements and after `=` in assignments.

3. `UnaryExp`

```peg
UnaryExp <- CallExp / PrimaryExp / UnaryOp UnaryExp
```

This is the most important new branch-order decision. `CallExp` must appear before `PrimaryExp` because both can begin with `IDENT`. If `PrimaryExp` came first, `foo(1, 2)` would be consumed as `LVal` with the `(` left behind, producing either a later syntax error or a misleading recovery path. `UnaryOp UnaryExp` remains safe as a separate branch because `+`, `-`, and `!` are prefix-distinct from identifier and parenthesis starts.

4. `PrimaryExp`

```peg
PrimaryExp <- LPAREN Exp RPAREN / LVal / Number
```

The order is still correct. The `(`-headed branch is distinct and recovery commits after `(`. `LVal` remains the plain identifier leaf once the call form has already been checked at the `UnaryExp` level.

5. `FuncType`

```peg
FuncType <- KW_VOID / KW_INT
```

This rule is not branch-order-sensitive today because `void` and `int` are disjoint keywords. No cut is needed.

6. `RelOp`

```peg
RelOp <- LE / GE / LT / GT
```

Longer operators must precede their shorter prefixes.

7. `IntegerConst`

```peg
IntegerConst <- HexadecimalConst / OctalConst / DecimalConst
```

Hexadecimal must precede octal so `0x...` is not consumed as octal `0`.

## 8. Short Design Rationale

This update keeps the previous PEG design intact wherever the language extension does not force change. The only structural expansion is at the compilation-unit boundary, where the source grammar introduces a left-recursive list of top-level declarations and function definitions. Everything else is a local delta: `void` in function type, optional formal and actual parameter lists, optional return expressions, and one new identifier-headed unary-expression form for calls.

The recovery strategy stays intentionally conservative. It continues to spend labels on outer, user-visible structure rather than on low-level helpers, and it reuses the existing synchronization anchors: `)`, `}`, `;`, `,`, block-item starts, and statement starts. The only genuinely new high-value recovery sites are call parsing, where `IDENT (` has already determined the construct and a missing `)` otherwise cascades through the rest of the containing expression or statement, and compilation-unit item recovery, which now syncs on `const`, `int`, `void`, or end of file.

The implementation note remains important. Grammar-level nonterminals still describe precedence and reviewer intent, but the runtime AST need not mirror them one-for-one. `ConstExp` stays a grammar alias whose meaning is established semantically, and the call extension fits naturally into the existing flattened expression AST by adding one `Exp::Call` payload rather than introducing a separate tower of runtime expression node types.
