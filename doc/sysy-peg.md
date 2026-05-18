# SysY PEG Grammar

## 1. Source Grammar Notes

This document is the PEG-oriented design for the next SysY extension. The source EBNF remains [doc/sysy.md](doc/sysy.md), and this revision is an incremental extension of the current PEG baseline in order to support arrays in declarations, lvalues, function parameters, and brace initializers.

Source-language delta for this revision:

```text
ConstDef      ::= IDENT {"[" ConstExp "]"} "=" ConstInitVal;
ConstInitVal  ::= ConstExp | "{" [ConstInitVal {"," ConstInitVal}] "}";
VarDef        ::= IDENT {"[" ConstExp "]"}
                | IDENT {"[" ConstExp "]"} "=" InitVal;
InitVal       ::= Exp | "{" [InitVal {"," InitVal}] "}";

LVal          ::= IDENT {"[" Exp "]"};

FuncFParam    ::= BType IDENT ["[" "]" {"[" ConstExp "]"}];
```

Assumptions carried forward from the existing token layer and baseline design unless noted below:

- top-level sequencing remains `CompUnit ::= [CompUnit] (Decl | FuncDef)` and is still represented in PEG as a non-empty ordered list of top-level items
- function definitions, optional `return Exp`, and call expressions stay as in the current baseline
- the expression-precedence ladder is unchanged: `Exp ::= LOrExp`, down through `UnaryExp`, `PrimaryExp`, `LVal`, and `Number`
- whitespace and comments are still handled entirely by the token layer
- `break` and `continue` remain syntactically unrestricted and are still rejected semantically outside loops

Token-layer delta:

- identifiers, integer literals, keywords, parentheses, braces, separators, and operators are unchanged
- `[` and `]` must now be exposed explicitly as tokens because the source grammar now uses array declarators and subscripts

Implementation note:

- The grammar continues to use named precedence layers (`LOrExp`, `AddExp`, `MulExp`, `UnaryExp`, and so on) because they are the clearest way to explain precedence and PEG rewrites.
- The runtime AST is flatter than the grammar. [src/frontend/ast.h](src/frontend/ast.h) already models expressions as one `Exp` node with payload variants such as binary, unary, call, lvalue, and number, so the grammar-level nonterminals remain explanatory rather than one-to-one runtime node types.
- This array extension implies AST growth outside the current expression payload split. The scalar-only shapes in [src/frontend/ast.h](src/frontend/ast.h) for `LVal`, `ConstInitVal`, `InitVal`, `ConstDef`, `VarDef`, and `FuncFParam` will need array-dimension vectors and recursive initializer forms even though the grammar still presents them with separate nonterminals.
- `ConstExp` remains a grammar alias, not a distinct runtime AST node. Whether an expression is constant is still a semantic property discovered during analysis and constant folding.

## 2. Baseline Delta

Relative to the previous baseline in this file:

- unchanged: `CompUnit`, `TopLevelItem`, `FuncDef`, `FuncType`, `FuncFParams`, block structure, statement forms, return statements, function calls, the full expression-precedence ladder, and the existing keyword and literal tokenization apart from brackets
- changed: `ConstDef` now accepts zero or more constant-dimension suffixes before `=`
- changed: `VarDef` now accepts zero or more constant-dimension suffixes, with the optional initializer preserved after the full declarator
- changed: `ConstInitVal` and `InitVal` now accept recursive brace initializers in addition to scalar expression forms
- changed: `LVal` now accepts zero or more subscript suffixes
- changed: `FuncFParam` now accepts the SysY array-parameter suffix `[]` followed by zero or more constant trailing dimensions
- added: `ArrayConstDims`, `LValIndices`, `ParamArraySuffix`, `ConstInitValList`, and `InitValList`
- reordered: no new ordered-choice reordering is required beyond preserving `CallExp` before `PrimaryExp` and `AssignStmt` before `ExpStmt`
- token layer changes: add `LBRACK` and `RBRACK`; no keyword changes are needed
- recovery updates: existing block, declaration, statement, and parenthesized-expression recovery remain valid; new recovery is only needed around `[` `]` array suffixes and `{` `}` brace initializers

## 3. Left-Recursion Elimination

The existing expression-ladder rewrites from the current baseline remain unchanged. This revision adds PEG-incompatible repetition and optional-list patterns, but it does not introduce any new left-recursive expression rule.

| Original shape | Rewritten PEG shape | Why it preserves the parse |
|---|---|---|
| `ConstDef ::= IDENT {"[" ConstExp "]"} "=" ConstInitVal` | `ConstDef <- IDENT ArrayConstDims ASSIGN ConstInitVal` with `ArrayConstDims <- (LBRACK ConstExp RBRACK)*` | preserves an identifier followed by zero or more constant dimensions, then the required initializer |
| `VarDef ::= IDENT {"[" ConstExp "]"} | IDENT {"[" ConstExp "]"} "=" InitVal` | `VarDef <- IDENT ArrayConstDims (ASSIGN InitVal)?` | factors the shared prefix and keeps the initializer optional after the whole declarator |
| `ConstInitVal ::= ConstExp | "{" [ConstInitVal {"," ConstInitVal}] "}"` | `ConstInitVal <- ConstExp / LBRACE ConstInitValList? RBRACE` and `ConstInitValList <- ConstInitVal (COMMA ConstInitVal)*` | preserves either a scalar constant expression or a brace-enclosed comma-separated recursive list |
| `InitVal ::= Exp | "{" [InitVal {"," InitVal}] "}"` | `InitVal <- Exp / LBRACE InitValList? RBRACE` and `InitValList <- InitVal (COMMA InitVal)*` | same factoring as `ConstInitVal`, preserving scalar or recursive brace forms |
| `LVal ::= IDENT {"[" Exp "]"}` | `LVal <- IDENT LValIndices` with `LValIndices <- (LBRACK Exp RBRACK)*` | preserves a base identifier followed by zero or more subscripts |
| `FuncFParam ::= BType IDENT ["[" "]" {"[" ConstExp "]"}]` | `FuncFParam <- BType IDENT ParamArraySuffix?` with `ParamArraySuffix <- LBRACK RBRACK (LBRACK ConstExp RBRACK)*` | preserves SysY array-parameter syntax: one unsized first dimension, then zero or more constant trailing dimensions |

The earlier rewrites for `CompUnit`, `FuncFParams`, `FuncRParams`, `MulExp`, `AddExp`, `RelExp`, `EqExp`, `LAndExp`, `LOrExp`, and `ConstExp <- Exp` are unchanged from the current baseline and remain valid.

## 4. Plain PEG Grammar

```peg
CompUnit           <- Spacing TopLevelItem+ EOF
TopLevelItem       <- ConstDecl / FuncDef / VarDecl

FuncDef            <- FuncType IDENT LPAREN FuncFParams? RPAREN Block
FuncType           <- KW_VOID / KW_INT
FuncFParams        <- FuncFParam (COMMA FuncFParam)*
FuncFParam         <- BType IDENT ParamArraySuffix?
ParamArraySuffix   <- LBRACK RBRACK (LBRACK ConstExp RBRACK)*

Block              <- LBRACE BlockItem* RBRACE
BlockItem          <- Decl / Stmt

Decl               <- ConstDecl / VarDecl
ConstDecl          <- KW_CONST BType ConstDef (COMMA ConstDef)* SEMI
VarDecl            <- BType VarDef (COMMA VarDef)* SEMI
BType              <- KW_INT
ConstDef           <- IDENT ArrayConstDims ASSIGN ConstInitVal
VarDef             <- IDENT ArrayConstDims (ASSIGN InitVal)?
ArrayConstDims     <- (LBRACK ConstExp RBRACK)*
ConstInitVal       <- ConstExp / LBRACE ConstInitValList? RBRACE
ConstInitValList   <- ConstInitVal (COMMA ConstInitVal)*
InitVal            <- Exp / LBRACE InitValList? RBRACE
InitValList        <- InitVal (COMMA InitVal)*
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
LVal               <- IDENT LValIndices
LValIndices        <- (LBRACK Exp RBRACK)*
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
LBRACK             <- "[" Spacing
RBRACK             <- "]" Spacing
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
  `ConstDecl` is prefix-distinct because of `const`. `FuncDef` must still precede `VarDecl` so an `int`-headed function definition is not misread as a variable declaration prefix.
- `Stmt <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt`
  `AssignStmt` must remain before `ExpStmt` because both may begin with `IDENT`, and with arrays the overlap now includes subscripted heads such as `a[i] = 1;`.
- `UnaryExp <- CallExp / PrimaryExp / UnaryOp UnaryExp`
  `CallExp` must remain before `PrimaryExp` because both start with `IDENT`; otherwise `foo(1)` would be consumed as an `LVal` and the following `(` would be stranded.
- `PrimaryExp <- LPAREN Exp RPAREN / LVal / Number`
  The `LVal` branch now includes postfix subscripts, but it is still safe here because the call form has already been checked at `UnaryExp`.
- `RelOp <- LE / GE / LT / GT`
  Longer operators must still precede shorter prefix-sharing ones.
- `IntegerConst <- HexadecimalConst / OctalConst / DecimalConst`
  Hexadecimal must still precede octal so `0x...` is not consumed as octal `0`.

The new initializer rules are not branch-order-sensitive because `ConstExp` and `Exp` do not start with `{`, while brace initializers do.

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
FuncFParam                 <- BType IDENT ParamArraySuffix?
ParamArraySuffix           <- LBRACK ^
                              (RBRACK / Throw<MissingParamArrayRBracket> RecoverToRBracket)
                              (LBRACK ^
                                (ConstExp / Throw<MalformedArrayBound> RecoverToRBracket)
                                (RBRACK / Throw<MissingArrayRBracket> RecoverToRBracket))*

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
ConstDef                   <- IDENT ArrayConstDims ASSIGN ^
                              (ConstInitVal / Throw<MalformedConstInitializer> RecoverToDeclBoundary)
VarDef                     <- IDENT ArrayConstDims
                              (ASSIGN ^
                                (InitVal / Throw<MalformedInitializer> RecoverToDeclBoundary))?
ArrayConstDims             <- (LBRACK ^
                                (ConstExp / Throw<MalformedArrayBound> RecoverToRBracket)
                                (RBRACK / Throw<MissingArrayRBracket> RecoverToRBracket))*
ConstInitVal               <- ConstExp
                            / LBRACE ^
                              (RBRACE
                              / ConstInitValList
                                (RBRACE / Throw<MissingConstInitRBrace> RecoverToInitBoundary)
                              / Throw<MalformedConstInitializer> RecoverToInitBoundary)
ConstInitValList           <- ConstInitVal
                              (COMMA (ConstInitVal / Throw<MalformedConstInitializer> RecoverToInitBoundary))*
InitVal                    <- Exp
                            / LBRACE ^
                              (RBRACE
                              / InitValList
                                (RBRACE / Throw<MissingInitRBrace> RecoverToInitBoundary)
                              / Throw<MalformedInitializer> RecoverToInitBoundary)
InitValList                <- InitVal
                              (COMMA (InitVal / Throw<MalformedInitializer> RecoverToInitBoundary))*
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
LVal                       <- IDENT LValIndices
LValIndices                <- (LBRACK ^
                                (Exp / Throw<MalformedSubscript> RecoverToRBracket)
                                (RBRACK / Throw<MissingSubscriptRBracket> RecoverToRBracket))*
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
LBRACK                     <- "[" Spacing
RBRACK                     <- "]" Spacing
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

RecoverToTopLevelBoundary  <- (!KW_CONST !KW_INT !KW_VOID !EOF .)*
                              (&KW_CONST / &KW_INT / &KW_VOID / EOF)
RecoverToFuncHeaderEnd     <- (!")" !"{" !EOF .)* (RPAREN / &LBRACE / EOF)
RecoverToParamBoundary     <- (!"," !")" !EOF .)* (COMMA / RPAREN / EOF)
RecoverToCallEnd           <- (!")" !";" !"}" !"," !EOF .)* (RPAREN / &SEMI / &RBRACE / &COMMA / EOF)
RecoverToArgBoundary       <- (!"," !")" !";" !"}" !EOF .)* (COMMA / RPAREN / &SEMI / &RBRACE / EOF)
RecoverToExprRParen        <- (!")" !";" !"}" !EOF .)* (RPAREN / &SEMI / &RBRACE / EOF)
RecoverToRBracket          <- (!"]" !"," !")" !"}" !";" !EOF .)* (RBRACK / &COMMA / &RPAREN / &RBRACE / &SEMI / EOF)
RecoverToInitBoundary      <- (!"," !"}" !";" !EOF .)* (&COMMA / &RBRACE / &SEMI / EOF)
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

Digit                      <- [0-9]
NonZeroDigit               <- [1-9]
OctalDigit                 <- [0-7]
HexadecimalDigit           <- [0-9A-Fa-f]
EOF                        <- !.
```

Recovery placement stays deliberately narrow:

- `ArrayConstDims`, `LValIndices`, and `ParamArraySuffix` cut immediately after `[` because that prefix already determines an array suffix, and a missing `]` would otherwise cascade into declaration, parameter, or statement recovery
- `ConstInitVal` and `InitVal` cut after `{` because the parser has already committed to a brace initializer, making missing `}` or malformed elements the only meaningful local failures
- existing function-header, call, block, declaration, `if`, `while`, assignment, and parenthesized-expression recovery remain valid without broader restructuring

## 6. Recovery Inventory

| Label | Where it is thrown | Meaning | Recovery |
|---|---|---|---|
| `MalformedTopLevelItem` | `CompUnit` at a top-level item start | top-level text is neither a valid declaration nor function definition | `RecoverToTopLevelBoundary` |
| `MissingFuncRParen` | `FuncDef` after the optional formal-parameter list | function header is missing `)` | `RecoverToFuncHeaderEnd` |
| `MalformedFuncParam` | `FuncFParams` after `,` or in the header body | a formal parameter is malformed or missing | `RecoverToParamBoundary` |
| `MissingParamArrayRBracket` | `ParamArraySuffix` after the first `[` | function parameter array marker `[]` is missing `]` | `RecoverToRBracket` |
| `MalformedArrayBound` | `ArrayConstDims` or trailing parameter dimensions after `[` | array bound expression is malformed or missing | `RecoverToRBracket` |
| `MissingArrayRBracket` | `ArrayConstDims` or trailing parameter dimensions after a bound | array declarator dimension is missing `]` | `RecoverToRBracket` |
| `MalformedBlockItem` | `Block` at a block-item start | block item is neither a valid declaration nor statement | `RecoverToBlockItemBoundary` |
| `MissingRBrace` | `Block` after block items | block is missing `}` | `RecoverToBlockEnd` |
| `MalformedDeclItem` | `ConstDecl` or `VarDecl` at a declarator position | declarator is malformed or missing | `RecoverToDeclBoundary` |
| `MissingDeclSemicolon` | `ConstDecl` or `VarDecl` after the declarator list | declaration is missing `;` | `RecoverToDeclBoundary` |
| `MalformedConstInitializer` | `ConstDef` after `=` or inside `ConstInitValList` | constant initializer is malformed or missing | `RecoverToDeclBoundary` or `RecoverToInitBoundary` |
| `MissingConstInitRBrace` | `ConstInitVal` after a brace initializer body | constant brace initializer is missing `}` | `RecoverToInitBoundary` |
| `MalformedInitializer` | `VarDef` after `=` or inside `InitValList` | initializer is malformed or missing | `RecoverToDeclBoundary` or `RecoverToInitBoundary` |
| `MissingInitRBrace` | `InitVal` after a brace initializer body | brace initializer is missing `}` | `RecoverToInitBoundary` |
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
| `MalformedSubscript` | `LValIndices` after `[` | array subscript expression is malformed or missing | `RecoverToRBracket` |
| `MissingSubscriptRBracket` | `LValIndices` after a subscript expression | array subscript is missing `]` | `RecoverToRBracket` |

## 7. Ordered-Choice Notes

1. `TopLevelItem`

```peg
TopLevelItem <- ConstDecl / FuncDef / VarDecl
```

This rule remains PEG-sensitive for the same reason as the previous baseline. `ConstDecl` is safe first because `const` is unique. `FuncDef` must stay before `VarDecl` so `int f(...) { ... }` is recognized as a function definition before declaration parsing can misclassify it. Arrays do not change that prefix conflict.

2. `Stmt`

```peg
Stmt <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt
```

This ordering is still correct, and arrays make the `AssignStmt` versus `ExpStmt` overlap slightly larger. Inputs like `a[i] = x;` and `a[i];` share the same prefix for longer, so `AssignStmt` must stay before `ExpStmt`. The cut after `=` is still the right commitment point because only then is the statement definitely an assignment.

3. `UnaryExp`

```peg
UnaryExp <- CallExp / PrimaryExp / UnaryOp UnaryExp
```

This remains the most important identifier-headed branch order. Arrays enlarge `LVal`, but they do not change the core conflict: `foo(1)` and `foo[1]` both start with `IDENT`, and the call form must still be recognized before the plain primary-expression route. If `PrimaryExp` came first, `foo(1)` would still be consumed as an `LVal` prefix and the call would fail later.

4. `PrimaryExp`

```peg
PrimaryExp <- LPAREN Exp RPAREN / LVal / Number
```

This order is still correct. The `(` branch is distinct and already cut in the recovery grammar. `LVal` now covers `IDENT` plus zero or more bracketed subscripts, which is exactly why the call form must already have been handled one level above.

5. `ConstInitVal` and `InitVal`

```peg
ConstInitVal <- ConstExp / LBRACE ConstInitValList? RBRACE
InitVal      <- Exp / LBRACE InitValList? RBRACE
```

These rules are not truly branch-order-sensitive because the brace initializer starts with `{`, while expression forms do not. No additional cut is needed in the plain grammar. In the recovery grammar, the cut after `{` is useful only because the parser has already committed to the brace-initializer shape.

6. `RelOp`

```peg
RelOp <- LE / GE / LT / GT
```

Longer operators must still precede shorter prefixes.

7. `IntegerConst`

```peg
IntegerConst <- HexadecimalConst / OctalConst / DecimalConst
```

Hexadecimal must still precede octal so `0x...` is not consumed as octal `0`.

## 8. Short Design Rationale

This update is the minimal coherent extension of the existing PEG baseline. The function, statement, and expression design stays intact, while array support is introduced only where the source grammar requires it: declaration suffixes, lvalue subscripts, function-parameter array markers, and recursive brace initializers. The grammar still uses straightforward PEG rewrites such as `Head Tail*`, shared-prefix factoring, and explicit list helpers instead of redesigning unrelated rules.

Recovery scope also stays narrow. The new labels focus on high-value structural failures introduced by arrays: missing `]`, malformed bounds or subscripts, malformed initializer elements, and missing `}` in brace initializers. Synchronization still relies on existing delimiters and boundaries such as `]`, `}`, `,`, `;`, and enclosing statement or declaration ends, which keeps the recovery strategy small and readable.

At the implementation boundary, the grammar-level additions are larger than the parser surface alone because the current AST remains scalar-oriented in several declaration and initializer nodes. That distinction is intentional: grammar nonterminals explain precedence and syntax, while runtime representation can continue to flatten expressions and attach array-specific metadata only where the AST actually needs it. `ConstExp` remains only a grammar alias, with constantness still established during semantic analysis rather than syntactically.
