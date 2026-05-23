# SysY PEG Grammar

This document describes the current PEG-oriented grammar for the SysY frontend in this repository. The source-language reference remains [doc/sysy.md](doc/sysy.md). This file presents the grammar in its final integrated form, including arrays in declarations, lvalues, function parameters, brace initializers, and top-level function declarations.

## 1. Source Grammar Notes

The source-language shapes relevant to this frontend are:

```text
CompUnit      ::= {Decl | FuncItem};

ConstDecl     ::= "const" BType ConstDef {"," ConstDef} ";";
VarDecl       ::= BType VarDef {"," VarDef} ";";
BType         ::= "int";

ConstDef      ::= IDENT {"[" ConstExp "]"} "=" ConstInitVal;
VarDef        ::= IDENT {"[" ConstExp "]"}
                | IDENT {"[" ConstExp "]"} "=" InitVal;

ConstInitVal  ::= ConstExp | "{" [ConstInitVal {"," ConstInitVal}] "}";
InitVal       ::= Exp | "{" [InitVal {"," InitVal}] "}";

FuncItem      ::= FuncDef | FuncDecl;
FuncDef       ::= FuncType IDENT "(" [FuncFParams] ")" Block;
FuncDecl      ::= FuncType IDENT "(" [FuncFParams] ")" ";";
FuncType      ::= "void" | "int";
FuncFParams   ::= FuncFParam {"," FuncFParam};
FuncFParam    ::= BType IDENT ["[" "]" {"[" ConstExp "]"}];

Block         ::= "{" {BlockItem} "}";
BlockItem     ::= Decl | Stmt;

Stmt          ::= IfStmt | WhileStmt | BreakStmt | ContinueStmt
                | AssignStmt | Block | ReturnStmt | ExpStmt;
IfStmt        ::= "if" "(" Exp ")" Stmt ["else" Stmt];
WhileStmt     ::= "while" "(" Exp ")" Stmt;
BreakStmt     ::= "break" ";";
ContinueStmt  ::= "continue" ";";
AssignStmt    ::= LVal "=" Exp ";";
ReturnStmt    ::= "return" [Exp] ";";
ExpStmt       ::= [Exp] ";";

Exp           ::= LOrExp;
ConstExp      ::= Exp;
LVal          ::= IDENT {"[" Exp "]"};
```

Assumptions carried by the PEG form:

- top-level parsing is represented as a non-empty ordered list of top-level items
- expression precedence remains the standard SysY ladder from `LOrExp` down to `UnaryExp`, `PrimaryExp`, `LVal`, and `Number`
- whitespace and comments are handled by the token layer through `Spacing`
- `[` and `]` are first-class tokens because array declarators and subscripts are part of the grammar
- the compilation pipeline prepends builtin library declarations to the translation unit source before parsing; builtin names are not synthesized later during semantic analysis

## 2. Baseline Delta

Compared with the previous baseline, arrays, statements, expressions, and initializer rules stay unchanged.

- top-level function syntax now admits both a definition form and a declaration form with the same header
- the `FuncDef` PEG rule is refactored into a shared header plus a body-or-semicolon tail
- the token layer is unchanged
- existing recovery around function headers remains valid, but the post-header recovery target now has to accept either `;` or a block start

## 3. Runtime AST Mapping

The PEG grammar is not a one-to-one dump of runtime node types. The current AST in [src/frontend/ast.h](src/frontend/ast.h) has these important design choices:

- Expressions are flattened into one `Exp` node with payload variants `Binary`, `Unary`, `Call`, `LVal`, and `Number`.
- `ConstExp` is only a grammar alias. There is no dedicated runtime `ConstExp` node; constantness is determined during semantic analysis.
- `Exp::LVal` stores a base identifier plus a vector of subscript expressions.
- `ConstInitVal` and `InitVal` are recursive nodes whose payload is either a scalar expression or a list of nested initializer nodes.
- `ConstDef` and `VarDef` store array dimensions in `shape` as `std::vector<Ref<Exp>>`.
- `FuncFParam` stores the base identifier, a boolean `m_isArray` for the unsized first `[]` in array parameters, and trailing dimensions in `shape`.
- Top-level function items continue to use one runtime node type, `FuncDef`. The `body` field is `Ptr<Block>` and is null for declarations.
- `Decl`, `Stmt`, and `CompUnit::Item` are `std::variant` values whose alternatives are `Ref<...>` handles, not `Ptr<...>` handles.
- `IfStmt` stores both `thenBody` and `elseBody` as `Stmt`. The parser always synthesizes an `elseBody`; when the source omits `else`, the parser inserts an empty `Block`.

Those choices drive a few grammar-to-AST mismatches intentionally:

- grammar precedence layers such as `AddExp`, `MulExp`, and `UnaryExp` explain parse structure, but all lower to the single `Exp` runtime node type
- brace initializers are grammar rules with their own list helpers, but runtime storage is recursive through `ConstInitVal::List` and `InitVal::List`
- function array parameters encode the unsized first dimension separately from the trailing constant dimensions because the AST needs to preserve `int a[]` distinctly from `int a`
- `ConstExp` remains a grammar alias only. Constant-expression validity is discovered later by semantic analysis and constant folding, not by introducing a separate AST node.

## 4. Left-Recursion Elimination

The expression ladder uses standard PEG-friendly repetition instead of left recursion. Arrays and recursive initializers add list and suffix forms but do not introduce any new left-recursive rule.

| Original shape | Rewritten PEG shape | Why it preserves the parse |
|---|---|---|
| `ConstDef ::= IDENT {"[" ConstExp "]"} "=" ConstInitVal` | `ConstDef <- IDENT ArrayConstDims ASSIGN ConstInitVal` with `ArrayConstDims <- (LBRACK ConstExp RBRACK)*` | preserves zero or more constant declarator dimensions before the required initializer |
| `VarDef ::= IDENT {"[" ConstExp "]"} | IDENT {"[" ConstExp "]"} "=" InitVal` | `VarDef <- IDENT ArrayConstDims (ASSIGN InitVal)?` | factors the shared declarator prefix and keeps the initializer optional |
| `ConstInitVal ::= ConstExp | "{" [ConstInitVal {"," ConstInitVal}] "}"` | `ConstInitVal <- ConstExp / LBRACE ConstInitValList? RBRACE` and `ConstInitValList <- ConstInitVal (COMMA ConstInitVal)*` | preserves scalar constant initializers and recursive brace forms |
| `InitVal ::= Exp | "{" [InitVal {"," InitVal}] "}"` | `InitVal <- Exp / LBRACE InitValList? RBRACE` and `InitValList <- InitVal (COMMA InitVal)*` | preserves scalar initializers and recursive brace forms |
| `LVal ::= IDENT {"[" Exp "]"}` | `LVal <- IDENT LValIndices` with `LValIndices <- (LBRACK Exp RBRACK)*` | preserves a base identifier followed by zero or more subscripts |
| `FuncFParam ::= BType IDENT ["[" "]" {"[" ConstExp "]"}]` | `FuncFParam <- BType IDENT ParamArraySuffix?` with `ParamArraySuffix <- LBRACK RBRACK (LBRACK ConstExp RBRACK)*` | preserves the SysY rule that only the first parameter dimension may be unsized |
| `FuncDef ::= FuncType IDENT "(" [FuncFParams] ")" Block` and `FuncDecl ::= FuncType IDENT "(" [FuncFParams] ")" ";"` | `FuncItem <- FuncType IDENT LPAREN FuncFParams? RPAREN FuncTail` with `FuncTail <- Block / SEMI` | factors the shared function header while preserving the distinction between a declaration tail and a definition tail |

The existing rewrites for `FuncRParams`, `MulExp`, `AddExp`, `RelExp`, `EqExp`, `LAndExp`, and `LOrExp` are unchanged.

## 5. Plain PEG Grammar

```peg
CompUnit           <- Spacing TopLevelItem+ EOF
TopLevelItem       <- ConstDecl / FuncItem / VarDecl

FuncItem           <- FuncType IDENT LPAREN FuncFParams? RPAREN FuncTail
FuncTail           <- Block / SEMI
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

## 6. Recovery-Annotated PEG Grammar

```peg
CompUnit                   <- Spacing
                              (TopLevelItem / Throw<MalformedTopLevelItem> RecoverToTopLevelBoundary)+
                              EOF
TopLevelItem               <- ConstDecl / FuncItem / VarDecl

FuncItem                   <- FuncType IDENT LPAREN ^
                              (RPAREN
                              / FuncFParams
                                (RPAREN / Throw<MissingFuncRParen> RecoverToFuncHeaderEnd)
                              / Throw<MalformedFuncParam> RecoverToFuncHeaderEnd)
                              FuncTail
FuncTail                   <- Block / SEMI
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

## 7. Recovery Inventory

| Label | Where it is thrown | Meaning | Recovery |
|---|---|---|---|
| `MalformedTopLevelItem` | `CompUnit` at a top-level item start | top-level text is neither a valid declaration nor function item | `RecoverToTopLevelBoundary` |
| `MissingFuncRParen` | `FuncItem` after the optional formal-parameter list | function header is missing `)` before either `;` or a block | `RecoverToFuncHeaderEnd` |
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

## 8. Ordered-Choice Notes

1. `TopLevelItem`

```peg
TopLevelItem <- ConstDecl / FuncItem / VarDecl
```

`ConstDecl` is safe first because `const` is prefix-distinct. `FuncItem` must stay before `VarDecl` so both `int f(...);` and `int f(...) { ... }` are recognized as function headers rather than being consumed as the start of a variable declaration.

No additional cut is needed after the header beyond the existing cut after `(`. Once the parser has consumed `FuncType IDENT (`, it is committed to a function item; the only remaining choice is whether the tail is `;` or a block.

2. `Stmt`

```peg
Stmt <- IfStmt / WhileStmt / BreakStmt / ContinueStmt / AssignStmt / Block / ReturnStmt / ExpStmt
```

`AssignStmt` must remain before `ExpStmt` because both may start with an identifier. With arrays, the ambiguous prefix is longer: both `a[i] = 1;` and `a[i];` begin with the same lvalue head.

3. `UnaryExp`

```peg
UnaryExp <- CallExp / PrimaryExp / UnaryOp UnaryExp
```

`CallExp` must remain before `PrimaryExp` because both start with `IDENT`. Otherwise `foo(1)` would be consumed as an `LVal` and leave the call suffix stranded.

4. `PrimaryExp`

```peg
PrimaryExp <- LPAREN Exp RPAREN / LVal / Number
```

This order is safe because call syntax is already handled at `UnaryExp`, and the parenthesized-expression form is prefix-distinct.

5. `RelOp`

```peg
RelOp <- LE / GE / LT / GT
```

Longer operators must precede shorter prefix-sharing operators.

6. `IntegerConst`

```peg
IntegerConst <- HexadecimalConst / OctalConst / DecimalConst
```

Hexadecimal must precede octal so `0x...` is not consumed as octal `0`.

The initializer rules are not branch-order-sensitive because brace initializers start with `{`, while expression forms do not.

## 9. Short Design Rationale

This grammar stays close to the SysY source rules while keeping the parser implementation local and predictable. Function declarations are added as the smallest coherent delta: a shared function header with a `Block / SEMI` tail, no token-layer change, and no rework of the existing expression or declaration subgrammars.

The recovery grammar stays intentionally narrow. The only function-specific adjustment is that recovery after a malformed function header now synchronizes to a point where either `)` plus `;` or `)` plus `{` can resume parsing.

The runtime AST is flatter and more normalized than the grammar:

- `ConstExp` is semantic, not syntactic, at runtime
- expression precedence nodes collapse into `Exp`
- absent `else` branches are normalized to an empty block in `IfStmt::elseBody`
- top-level function declarations and definitions share one node type, with a null `FuncDef::body` marking declarations
- array parameter shape is split between `m_isArray` and trailing `shape`

That split is deliberate: the grammar explains how the source parses, while the AST preserves the information the later semantic and lowering phases actually need. Builtin functions are now supplied by the compilation pipeline as ordinary declarations before parsing, so semantic analysis and lowering can treat them exactly like user-written declarations.
