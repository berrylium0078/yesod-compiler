# SysY 语言规范

> SysY 官方的语言定义见 [这里](https://gitlab.eduxiji.net/nscscc/compiler2021/-/blob/master/SysY%E8%AF%AD%E8%A8%80%E5%AE%9A%E4%B9%89.pdf).
>
> 编译实践课所使用的 SysY 语言和官方定义略有不同: 实践课的 SysY 向下兼容官方定义.

## 文法定义

SysY 语言的文法采用扩展的 Backus 范式 (EBNF, Extended Backus-Naur Form) 表示, 其中:

- 符号 `[...]` 表示方括号内包含的项可被重复 0 次或 1 次.
- 符号 `{...}` 表示花括号内包含的项可被重复 0 次或多次.
- 终结符是由双引号括起的串, 或者是 `IDENT`, `INT_CONST` 这样的大写记号. 其余均为非终结符.

SysY 语言的文法表示如下, `CompUnit` 为开始符号:

```ebnf
CompUnit      ::= [CompUnit] (Decl | FuncDef);

Decl          ::= ConstDecl | VarDecl;
ConstDecl     ::= "const" BType ConstDef {"," ConstDef} ";";
BType         ::= "int";
ConstDef      ::= IDENT {"[" ConstExp "]"} "=" ConstInitVal;
ConstInitVal  ::= ConstExp | "{" [ConstInitVal {"," ConstInitVal}] "}";
VarDecl       ::= BType VarDef {"," VarDef} ";";
VarDef        ::= IDENT {"[" ConstExp "]"}
                | IDENT {"[" ConstExp "]"} "=" InitVal;
InitVal       ::= Exp | "{" [InitVal {"," InitVal}] "}";

FuncDef       ::= FuncType IDENT "(" [FuncFParams] ")" Block;
FuncType      ::= "void" | "int";
FuncFParams   ::= FuncFParam {"," FuncFParam};
FuncFParam    ::= BType IDENT ["[" "]" {"[" ConstExp "]"}];

Block         ::= "{" {BlockItem} "}";
BlockItem     ::= Decl | Stmt;
Stmt          ::= LVal "=" Exp ";"
                | [Exp] ";"
                | Block
                | "if" "(" Exp ")" Stmt ["else" Stmt]
                | "while" "(" Exp ")" Stmt
                | "break" ";"
                | "continue" ";"
                | "return" [Exp] ";";

Exp           ::= LOrExp;
LVal          ::= IDENT {"[" Exp "]"};
PrimaryExp    ::= "(" Exp ")" | LVal | Number;
Number        ::= INT_CONST;
UnaryExp      ::= PrimaryExp | IDENT "(" [FuncRParams] ")" | UnaryOp UnaryExp;
UnaryOp       ::= "+" | "-" | "!";
FuncRParams   ::= Exp {"," Exp};
MulExp        ::= UnaryExp | MulExp ("*" | "/" | "%") UnaryExp;
AddExp        ::= MulExp | AddExp ("+" | "-") MulExp;
RelExp        ::= AddExp | RelExp ("<" | ">" | "<=" | ">=") AddExp;
EqExp         ::= RelExp | EqExp ("==" | "!=") RelExp;
LAndExp       ::= EqExp | LAndExp "&&" EqExp;
LOrExp        ::= LAndExp | LOrExp "||" LAndExp;
ConstExp      ::= Exp;
```

其中, 各符号的含义如下:

| 符号 | 含义 | 符号 | 含义 |
|---|---|---|---|
| CompUnit | 编译单元 | Decl | 声明 |
| ConstDecl | 常量声明 | BType | 基本类型 |
| ConstDef | 常数定义 | ConstInitVal | 常量初值 |
| VarDecl | 变量声明 | VarDef | 变量定义 |
| InitVal | 变量初值 | FuncDef | 函数定义 |
| FuncType | 函数类型 | FuncFParams | 函数形参表 |
| FuncFParam | 函数形参 | Block | 语句块 |
| BlockItem | 语句块项 | Stmt | 语句 |
| Exp | 表达式 | LVal | 左值表达式 |
| PrimaryExp | 基本表达式 | Number | 数值 |
| UnaryExp | 一元表达式 | UnaryOp | 单目运算符 |
| FuncRParams | 函数实参表 | MulExp | 乘除模表达式 |
| AddExp | 加减表达式 | RelExp | 关系表达式 |
| EqExp | 相等性表达式 | LAndExp | 逻辑与表达式 |
| LOrExp | 逻辑或表达式 | ConstExp | 常量表达式 |

需要注意的是:

- `Exp`: SysY 中表达式的类型均为 `int` 型. 当 `Exp` 出现在表示条件判断的位置时 (例如 `if` 和 `while`), 表达式值为 0 时为假, 非 0 时为真.
- `ConstExp`: 其中使用的 `IDENT` 必须是常量.

## SysY 语言的终结符特征

### 标识符

SysY 语言中标识符 `IDENT` (identifier) 的规范如下:

```ebnf
identifier ::= identifier-nondigit
             | identifier identifier-nondigit
             | identifier digit;
```

其中, `identifier-nondigit` 为下划线, 小写英文字母或大写英文字母; `digit` 为数字 0 到 9.

关于其他信息, 请参考 [ISO/IEC 9899](http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf) 第 51 页关于标识符的定义.

对于同名**标识符**, SysY 中有以下约定:

- 全局变量和局部变量的作用域可以重叠, 重叠部分局部变量优先.
- 同名局部变量的作用域不能重叠.
- 变量名可以和函数名相同.

### 数值常量

SysY 语言中数值常量可以是整型数 `INT_CONST` (integer-const), 其规范如下:

```ebnf
integer-const ::= decimal-const | octal-const | hexadecimal-const;
decimal-const ::= nonzero-digit | decimal-const digit;
octal-const   ::= "0" | octal-const octal-digit;
hexadecimal-const ::= hexadecimal-prefix hexadecimal-digit | hexadecimal-const hexadecimal-digit;
hexadecimal-prefix ::= "0x" | "0X";
```

其中, `nonzero-digit` 为数字 1 到 9; `octal-digit` 为数字 0 到 7; `hexadecimal-digit` 为数字 0 到 9, 或字母 a 到 f/A 到 F.

数值常量的范围为 $[0, 2^{31} - 1]$, 不包含负号.

关于其他信息, 请参考 [ISO/IEC 9899](http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf) 第 54 页关于整数型常量的定义, 在此基础上忽略所有后缀.

### 注释

SysY 语言中注释的规范与 C 语言一致, 如下:

- 单行注释: 以序列 `//` 开始, 直到换行符结束, 不包括换行符.
- 多行注释: 以序列 `/*` 开始, 直到第一次出现 `*/` 时结束, 包括结束处 `*/`.

关于其他信息, 请参考 [ISO/IEC 9899](http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf) 第 66 页关于注释的定义.

## 语义约束

符合文法定义的程序集合是合法的 SysY 语言程序集合的超集。下面给出 SysY 语言的语义约束。

### 编译单元

```ebnf
CompUnit ::= [CompUnit] (Decl | FuncDef);
Decl     ::= ConstDecl | VarDecl;
```

1. 一个 SysY 程序由单个文件组成, 文件内容对应 EBNF 表示中的 `CompUnit`. 在该 `CompUnit` 中, 必须存在且仅存在一个标识为 `main`, 无参数, 返回类型为 `int` 的 `FuncDef` (函数定义). `main` 函数是程序的入口点.
2. `CompUnit` 的顶层变量/常量声明语句 (对应 `Decl`), 函数定义 (对应 `FuncDef`) 都不可以重复定义同名标识符 (`IDENT`), 即便标识符的类型不同也不允许.
3. `CompUnit` 的变量/常量/函数声明的作用域从该声明处开始, 直到文件结尾.

### 常量定义

```ebnf
ConstDef ::= IDENT {"[" ConstExp "]"} "=" ConstInitVal;
```

1. `ConstDef` 用于定义符号常量。`IDENT` 为常量的标识符, 在 `IDENT` 后, `=` 之前是可选的数组维度和各维长度的定义部分, 在 `=` 之后是初始值。
2. 数组维度部分不存在时, 表示定义单个常量, 此时 `=` 右边必须是单个初始数值。
3. 数组维度存在时, 表示定义数组。各维长度必须能在编译时被求值到非负整数, 且声明数组时各维长度都需要显式给出。
4. 当定义数组时, `ConstInitVal` 表示常量初始化器。`ConstInitVal` 中的 `ConstExp` 必须能在编译时被求值, 可以引用已定义的符号常量。
5. `ConstInitVal` 初始化器必须是下列三种情况之一：

   - 一对花括号 `{}`，表示所有元素初始为 0。
   - 与多维数组的维数与各维长度对应的初始值列表，如 `{{1,2},{3,4},{5,6}}` 等。
   - 当花括号内初始值少于数组元素个数时, 剩余部分会被隐式初始化为 0。

示例：

```c
const int a[4][2] = {};
const int b[4][2] = {1,2,3,4,5,6,7,8};
const int c[4][2] = {{1,2},{3,4},5,6,7,8};
const int d[4][2] = {1,2,{3}, {5},7,8};
const int e[4][2] = {{1,2},{3,4},{5,6},{7,8}};
```

> Tip: SysY 中“常量”的定义要求所有常量必须能在编译时被计算出来，类似于 C++ 的 `consteval` 或 Rust 的 `const`。

### 变量定义

```ebnf
VarDef ::= IDENT {"[" ConstExp "]"} | IDENT {"[" ConstExp "]"} "=" InitVal;
```

1. `VarDef` 用于定义变量。未带 `=` 的局部变量运行时初值未定义；全局未显式初始化的变量元素初值为 0。
2. 数组维度和常量定义类似, 各维长度需能被求值为非负整数。
3. 当含有 `=` 时, `InitVal` 的结构与 `ConstInitVal` 类似，但 `InitVal` 中的表达式为一般 `Exp`，可以引用变量。

示例：

```c
int a[4][2] = {};
int b[4][2] = {1,2,3,4,5,6,7,8};
int c[4][2] = {{1,2},{3,4},5,6,7,8};
int d[4][2] = {1,2,{3},{5},7,8};
int e[4][2] = {{d[2][1], c[2][1]}, {3,4}, {5,6}, {7,8}};
```

### 初值

```ebnf
ConstInitVal ::= ConstExp | "{" [ConstInitVal {"," ConstInitVal}] "}";
InitVal      ::= Exp | "{" [InitVal {"," InitVal}] "}";
```

1. 全局变量声明中指定的初值表达式必须是常量表达式.
2. 未显式初始化的局部变量值不确定; 未显式初始化的全局变量(及元素)值初始化为 0.
3. 初值类型必须与变量/常量类型一致。

下列形式不满足 SysY 语义约束：

```c
// 以下示例均非法
// a[4] = 4;        // 错误：数组赋单值
// a[2] = {{1,2},3};
// a = {1,2,3};
```

### 函数形参与实参

```ebnf
FuncFParam ::= BType IDENT ["[" "]" {"[" ConstExp "]"}];
FuncRParams ::= Exp {"," Exp};
```

1. `FuncFParam` 定义函数形参；数组形参第一维可省略，后续维需为常量表达式。
2. 对于 `int` 参数按值传递；对于数组形参，形参接收实参数组的地址。
3. 多维数组可以传递其部分（如 `a[1]` 作为 `int[]`）。

### 函数定义

```ebnf
FuncDef ::= FuncType IDENT "(" [FuncFParams] ")" Block;
```

1. `FuncDef` 指明函数返回类型：
   - 返回 `int` 时，函数内所有分支应含有带表达式的 `return`；缺失返回值的分支返回值未定义。
   - 返回 `void` 时，仅允许不带返回值的 `return` 语句。
2. 形参声明用于声明 `int` 或元素类型为 `int` 的数组。

### 语句块

```ebnf
Block ::= "{" {BlockItem} "}";
BlockItem ::= Decl | Stmt;
```

1. 语句块创建作用域，块内声明变量的生存期在该块内。
2. 块内可定义与外部同名的变量或常量，覆盖外部同名声明。

### 语句

```ebnf
Stmt ::= LVal "=" Exp ";"
       | [Exp] ";"
       | Block
       | "if" "(" Exp ")" Stmt ["else" Stmt]
       | "while" "(" Exp ")" Stmt
       | "break" ";"
       | "continue" ";"
       | "return" [Exp] ";";
```

1. `if` 语句遵循就近匹配原则。
2. `Exp` 作为独立 `Stmt` 时，其值被计算并丢弃。

### 左值表达式

```ebnf
LVal ::= IDENT {"[" Exp "]"};
```

1. `LVal` 可以是变量或数组元素。
2. 当表示数组时，方括号个数必须与数组维数相同以定位到元素；作为数组参数时可不完全匹配。
3. 单个变量不能带方括号。

### 表达式

```ebnf
Exp ::= LOrExp; ...
```

1. `Exp` 表示 `int` 型表达式; 在条件位置 0 为假，非 0 为真。
2. `LOrExp`、`LAndExp` 遵循短路求值规则。
3. `LVal` 必须在当前作用域且在该 `Exp` 之前定义。赋值左侧必须是变量。
4. 函数调用 `IDENT("FuncRParams")` 的实参类型和个数必须与定义相匹配。
5. 运算符优先级与结合性与 C 语言一致。

### 求值顺序

在 SysY 中，以下三类表达式求值顺序可能影响结果，且均被定义为未定义行为（与 C 保持一致）：

1. 操作数/函数参数的求值顺序影响结果（例如 `g(f(1), f(2))` 输出不确定）。
2. 数组下标的求值顺序影响结果（例如 `a[i][g()] = 2`）。
3. 赋值运算符两侧求值顺序影响结果（例如 `a[i] = g()`）。

参考 C/C++ 关于求值顺序的文档，SysY 采用与 C 相同的约定，以上三种类型均为 UB。

---

文档 v2.0.0 由 [MaxXing](https://github.com/MaxXSoft) 撰写，采用 [CC BY-NC-SA 4.0](http://creativecommons.org/licenses/by-nc-sa/4.0/) 发布。
