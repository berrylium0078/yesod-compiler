# SysY PEG Grammar

This document rewrites the given SysY EBNF snippet in Parsing Expression Grammar (PEG) form.

The main differences from the original EBNF are:

- PEG uses `<-` for rule definitions.
- PEG choice is ordered, so the order of alternatives matters.
- Left-recursive lexical rules are rewritten into repetition forms that are equivalent in meaning.

The start symbol is `CompUnit`.

```peg
CompUnit           <- FuncDef

FuncDef            <- FuncType IDENT "(" ")" Block
FuncType           <- "int"

Block              <- "{" Stmt "}"
Stmt               <- "return" Number ";"
Number             <- INT_CONST

IDENT              <- identifier
INT_CONST          <- integer_const

identifier         <- identifier_nondigit (identifier_nondigit / digit)*

integer_const      <- hexadecimal_const / octal_const / decimal_const
decimal_const      <- nonzero_digit digit*
octal_const        <- "0" octal_digit*
hexadecimal_const  <- hexadecimal_prefix hexadecimal_digit+
hexadecimal_prefix <- "0x" / "0X"

identifier_nondigit <- [_A-Za-z]
digit               <- [0-9]
nonzero_digit       <- [1-9]
octal_digit         <- [0-7]
hexadecimal_digit   <- [0-9A-Fa-f]
```

## Notes

- `identifier <- identifier_nondigit (identifier_nondigit / digit)*` is the PEG equivalent of the original left-recursive identifier definition.
- `hexadecimal_const` must appear before `octal_const` in `integer_const`, because PEG alternatives are matched from left to right. Otherwise, an input such as `0x2a` would be consumed as octal `0` before the hexadecimal rule is tried.
- This document only translates the grammar fragment provided above. It does not add whitespace, comments, or other lexical skip rules.