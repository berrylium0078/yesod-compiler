# YESOD 编译器课程项目报告

## 项目背景

在进行模 `998244353` 质数域上的多项式环的计算过程中，手动管理数组长度、清零十分繁琐，所以尝试写一个编译器自动生成代码。

### 多项式计算 DSL 语言设计

扩展自 SysY，添加了位运算、有限域类型 `mint`（模数 998244353）、多项式环类型 `poly`。

有限域支持和 `int` 的双向显示类型转换，支持加减乘除四则运算。底层实现是 montgomery reduction，其中除法基于快速幂，除零相当于乘零（特性）。

多项式类型 `poly` 是一等类型，底层存储为堆分配的动态数组，支持操作：
- **算术运算**：多项式加法、减法、乘法（卷积）
- **数乘**：`mint` 与 `poly` 的标量乘法
- **提取系数**：`p[k]` 提取第 $k$ 次项系数，越界返回 `0`
- **切片与移位**：`p[n, m]` 切片操作，`p << k` / `p >> k` 移位操作，本质是视图
- **显式构造**：`poly(int)` / `poly(mint)` 构造仅含常数项的多项式

多项式类型会利用活跃分析，自动在不活跃处释放内存或采用移动语义优化。

## 2 项目设计

### 2.1 总体架构

编译器的整体输入输出和 passes 流程如下：

```
SysY 源代码
    │
    ▼
┌────────────────┐
│  词法/语法分析  │  ← PEG 手写递归下降解析器
│  (Parser)      │
└────────┬───────┘
         │ AST
         ▼
┌────────────────┐
│  语义分析       │  ← 符号解析 + 类型分析 + CFG构建 + SSA构建
│  (Semantic)    │
└────────┬───────┘
         │ 带语义信息的 AST
         ▼
┌────────────────┐
│  AST → Koopa   │  ← AST 到自定义中间表示的翻译
│  IR 生成        │
└────────┬───────┘
         │ Koopa IR
         ▼
┌────────────────┐
│  优化 Passes   │  ← CSE、常量折叠、数据流简化等
│  (Optimizer)   │
└────────┬───────┘
         │ 优化后的 Koopa IR
         ▼
┌────────────────┐
│  LLVM IR 生成  │  ← Koopa IR 到 LLVM IR 的翻译
│  (LlvmGenerator)│
└────────┬───────┘
         │ LLVM IR
         ▼
┌────────────────┐
│  clang -O2     │  ← 利用 LLVM 的优化能力
│  (外部工具)     │
└────────┬───────┘
         │ 可执行文件
```

### 2.2 输入输出

- **输入**：一个 SysY 源文件（`.c` 或 `.sy` 扩展名），包含完整的程序定义（必须包含 `int main()` 入口函数）
- **输出**：取决于命令行参数：
  - `-koopa`：Koopa IR 文本文件
  - `-llvm`：LLVM IR 文件
  - `-c`：C 源文件（通过 llvm-cbe 反编译）

### 2.3 各 Pass 详解

#### Pass 1: 词法分析与语法分析（Parser）

**输入**：SysY 源代码字符串  
**输出**：抽象语法树（AST），以 `Arena` 分配器管理节点内存

解析器采用 **PEG（Parsing Expression Grammar）** 风格的手写递归下降解析器。该阶段负责把源程序转换成 AST，并尽量在语法错误后继续向后解析，从而一次性报告更多诊断。

主要设计特点：
- **错误恢复**：每个解析函数在遇到语法错误时，会尝试错误恢复（panic mode recovery），收集诊断信息而非立即终止
- **左递归消除**：表达式解析使用 PEG 友好的 `Head (Op Tail)*` 模式替代左递归
- **AST 扁平化**：虽然语法有 `AddExp`、`MulExp` 等多层优先级，运行时 AST 使用单一的 `Exp` 节点，通过 `Binary`、`Unary` 等变体 payload 区分运算类型
- **内置函数预声明**：在解析前自动在源代码前追加 SysY 运行时库函数（`getint`、`putint` 等）的声明

诊断类型示例：`UnexpectedTokenDiagnostic`、`ExpectedIdentifierDiagnostic`、`MissingSemicolonDiagnostic` 等 **30 余种**错误诊断类型。

#### Pass 2: 语义分析（Semantic Analysis）

**输入**：AST  
**输出**：带语义信息的 AST（`SemanticInfo`），包含符号表、类型信息、CFG 和 SSA 信息

语义分析由 `SemanticAnalyzer::analyze` 串联多个子 pass 完成。其顺序是符号解析、类型分析、CFG 构建、Pre-SSA CFG 简化、SSA 构建。

##### 2a: 符号解析（Symbol Resolution）

遍历 AST，构建**符号表**。主要工作：
- 维护作用域栈（scope stack），处理嵌套作用域中的变量声明
- 检测重复定义（`DoubleDefinitionDiagnostic`）和未定义使用（`UseBeforeDefinitionDiagnostic`）
- 区分函数符号和变量符号，支持全局作用域和局部作用域
- 为每个标识符分配唯一的符号 ID

##### 2b: 类型分析（Type Analysis）

对每个表达式进行类型推导和类型检查：
- 支持 `int`、`mint`、`boolean`、`void`、`array`、`poly`、`pv`（点值）等多种类型
- 常量表达式求值：在编译时对 `ConstExp` 进行常量折叠，记录每个表达式的常量值，特别地除零认为不是常量表达式
- 检查类型一致性（如赋值类型匹配、函数返回类型匹配等）
- 诊断类型：`TypeMismatchDiagnostic`、`ReturnTypeMismatchDiagnostic`、`AssignToConstDiagnostic`、`CallArityMismatchDiagnostic` 等
- 处理 `mint` 和 `poly` 类型的类型约束（如 `poly` 不能用于 `if`/`while` 条件）

##### 2c: 控制流分析（CFG Construction）

将函数体的 AST 转换为**控制流图（CFG）**：
- 将顺序语句块划分为基本块（basic blocks），以跳转（`jump`）和条件分支（`branch`）为边界
- 使用 `SemanticBasicBlock` 表示基本块，包含有序的语句列表和终止符
- 记录 `break`/`continue` 与所属的 `while` 循环的绑定关系
- **Pre-SSA CFG 简化**：在 SSA 构建前根据类型分析得到的常量表达式信息，将条件恒定的分支改写为无条件跳转，并删除不可达块、合并简单跳板块

##### 2d: SSA 构建（SSA Construction）

在 CFG 的基础上构建**静态单赋值（SSA）信息**。当前实现采用 block parameter 风格，而不是在前端直接生成传统 $\phi$ 函数：
- 只跟踪局部 scalar 和 `poly` 对象；数组、全局对象和需要地址语义的对象仍然走内存模型
- 计算每个基本块的前驱、后继、支配者、直接支配者、支配树和支配边界
- 扫描块内声明、赋值、表达式和 terminator，构建 `useSet`、`defSet`
- 通过迭代数据流分析得到 `liveIn` 和 `liveOut`
- 对每个在支配边界处需要合流且入口活跃的变量插入 `SemanticSsaBlockParam`
- 沿支配树给每个定义分配 `SemanticSsaAlias { symbolId, version }`
- 为 CFG 边记录 `outgoingArgsByTarget`，即源块跳向目标块时传给目标 block parameters 的 SSA 实参

这个阶段的结果不直接改写 AST，而是作为后续 Koopa IR 生成的指导信息。

#### Pass 3: SSA 驱动的 AST 到 Koopa IR 翻译（AST → Koopa IR Generation）

**输入**：AST + `SemanticInfo`  
**输出**：Koopa IR 程序

IR 设计为 Koopa IR 加上自定义扩展。该阶段把前端 SSA 信息落到 Koopa IR 的基本块参数上：
- 每个语义基本块生成一个 Koopa `BasicBlock`
- 每个 `SemanticSsaBlockParam` 生成一个 Koopa `BlockParameter`
- 进入基本块时，将 block parameter 绑定到对应的 SSA alias
- 生成跳转和分支时，根据 SSA 边信息填写目标块参数实参
- 对 SSA 跟踪的 scalar 和 `poly` 局部变量，声明和赋值通常不再生成 `alloc/store/load`，而是直接绑定到新的 SSA 值
- 对数组、全局变量和非 SSA 跟踪对象，仍然生成显式内存访问

Koopa IR 指令包括：
- 辅助指令：`select`（三目运算符）、`next_pow2`（对数上取整；0 映射为 0）
- 多项式专用：`poly_construct`、`combine`、`pointwise`、`ntt`、`intt`、`get_coeff`、`get_attr`、`set_attr`、`copy`
- 类型转换：`int2mint`、`mint2int`

#### Pass 4: 多项式表达式选择与区间处理（Poly Expression Lowering）

该 pass 当前嵌在 AST 到 Koopa IR 生成阶段中完成，而不是独立遍历完整 IR。它依赖 SSA 形式，因为 `poly` 值的身份和生命周期通过 SSA 值追踪。区间计算发生在单个多项式表达式树内：先正向计算可能非零区间，再从结果需求反向传播活跃区间。

核心工作：
- **区间表示**：使用 `PolyInterval { l, r }` 表示多项式可能非零或需要计算的项范围，语义为左闭右开区间
- **正向非零区间计算**：`combine` 通过 `emitCombineInterval` 汇总各个 `CombineTerm` 的区间；`pointwise` 通过 `emitPointwiseInterval` 自底向上计算表达式树的 full/nonzero interval
- **表达式内反向活跃区间传播**：`generatePolyExpression` 可携带 `requestedInterval`；`emitPointwiseEffectiveInterval` 从结果所需区间出发，向子表达式传播 `requiredInterval`
- **Combine 生成**：线性多项式表达式生成融合算子，将加减、数乘、移位、切片整理为“先切片再移位的线性组合”
- **Pointwise 生成**：可点值化的表达式和多项式乘法生成 `pointwise`，在 NTT 域中逐点计算表达式树
- **NTT 插入**：`emitNttPointwiseNode` 再次沿表达式树向下传递反向得到的 `requiredInterval`，多项式叶子节点在需要时先切片，再生成 `ntt`
- **长度计算**：根据反向裁剪后的有效非零区间和最终 active interval 生成 `next_pow2`，作为 NTT 长度
- **属性附加**：`combine` 和 `pointwise` 的结果通过 `set_attr l/r` 记录当前 active interval，后续 `get_attr` 可以被化简

区间规则包括：
- 正向加减法取区间并集
- 正向多项式乘法使用卷积区间
- 切片取交集，空区间特殊处理
- 反向传播时，加减法通常把结果需求原样传给两侧，再与各自 full interval 相交
- 反向传播到乘法时，根据结果 active interval 和另一侧 full interval 反推出本侧 required interval。例如结果只需要 $[l, r)$，右侧可能非零区间为 $[a, b)$，则左侧只需要大致 $[l + 1 - b, r - a)$，再与左侧 full interval 相交
- 反向传播遇到函数调用时停下
- 对乘法中只会影响目标 active interval 的部分输入进行裁剪，减少不必要的 NTT 长度

由于该处理以单个表达式为单位贪心生成，当前不会全局缓存“某个多项式的点值表示”。跨语句的复用主要依赖后续 CSE。

#### Pass 5: Koopa 局部值化简（Local Value Simplification）

**输入**：单个 Koopa 基本块  
**输出**：局部化简后的 Koopa 基本块

`simplifyLocalValues` 在生成每个多项式表达式后和生成每个基本块后调用，主要用于清理 poly lowering 产生的大量临时值。

主要工作：
- 展开 `get_attr(set_attr(x, attr, v), attr)` 为 `v`
- 查询不同属性时穿透 `set_attr` 链，尽量回到更早的 base value
- 传播 `copy`，把使用点替换为源值
- 在允许时删除未使用且无副作用的临时定义

这个 pass 是局部的，只在一个基本块内部保证依赖顺序合法时替换。

#### Pass 6: 函数级 CFG/SSA 清理（IR Cleanup）

**输入**：单个函数的 Koopa IR  
**输出**：更紧凑的 Koopa IR

函数生成结束后依次执行三个清理 pass：

1. **不可达块裁剪（Prune Unreachable Blocks）**
   - 从入口块沿 terminator 遍历 CFG
   - 删除入口不可达的基本块
   - 避免后续验证、CSE、LLVM phi 生成处理无效块

2. **死值删除（Dead Value Elimination）**
   - 统计函数中所有 SSA value 的使用次数
   - 删除未使用且可安全删除的 `SymbolDef`
   - 删除未使用的 block parameter
   - 删除 block parameter 时，同步删除所有跳转和分支边上的对应实参

3. **空基本块消除（Empty Basic Block Elimination）**
   - 识别非入口的空转发块：无普通语句，terminator 是 jump，参数只用于继续转发
   - 将前驱边直接重定向到最终目标块
   - 按 block parameter 到 edge argument 的映射替换参数
   - 重新计算可达性并删除被绕开的块

#### Pass 7: 公共子表达式消除与常量化简（CSE）

**输入**：Koopa IR 程序
**输出**：优化后的 Koopa IR 程序

`eliminateCommonSubexpressions` 在每个函数上执行。它先计算 Koopa CFG 的 dominator 集合，然后在支配关系允许的范围内复用已有定义。

主要工作：
- 对 RHS 做常量和代数化简：
  - 整数二元表达式常量折叠
  - `x + 0 → x`、`x - 0 → x`、`x * 0 → 0`、`x * 1 → x`
  - `select c x y` 在 `c` 为常量时化简
  - `select c x x → x`
  - `next_pow2` 的常量折叠
- 对 poly 属性访问做化简：
  - `get_attr` 穿透 `set_attr`
  - 对 `poly_construct` 的 `l/r` 直接推出 `0/元素个数`
- 为纯表达式构造结构化 key：
  - 纯整数 binary，排除 `div` 和 `mod`
  - `get_attr`、`set_attr`、`select`、`next_pow2`
  - `ntt`、`pointwise`、`combine`、`get_coeff`、`poly_construct`
  - `int2mint`、`mint2int`
- 对可交换运算规范化操作数顺序，便于发现重复表达式
- 重复表达式消除：若旧定义支配当前定义，则把当前定义替换为对旧定义的复制，并重写后续使用
- 最后再次执行死值删除，清理被替换后不再使用的定义

有副作用或内存相关的操作不会参与 CSE，例如 `alloc`、`load`、函数调用和 `store`。

#### Pass 8: LLVM IR 代码生成与后端数据流处理（LLVM IR Generation）

**输入**：优化后的 Koopa IR 程序
**输出**：LLVM IR 文本

`LlvmGenerator` 类将 Koopa IR 逐指令翻译为 LLVM IR：
- `i32` 类型直接映射到 LLVM 的 `i32`
- `mint` 类型在 LLVM 中也使用 `i32` 表示（Montgomery 形式）
- `poly` 类型翻译为 LLVM 的 `%YesodPoly` 结构体 `{ i32* %coeffs, i32* %addr, i32 %n, i32 %l, i32 %r }`
- 运行时支持：需要时自动编译并链接 `mint` 运行时库（Montgomery 算术）和 `poly` 运行时库（NTT/INTT、多项式内存管理）

LLVM 后端还包含几个与 SSA 和 `poly` 所有权相关的处理：

1. **Block parameter 到 LLVM phi**
   - 收集所有跳转/分支边传给目标 block parameter 的 incoming values
   - Koopa IR 的基本块参数在 LLVM block 开头输出为 `phi`
   - 如果同一个 branch 的 true/false 目标相同但携带不同参数，则构造中间 edge label，保证 phi incoming 的前驱块合法

2. **函数类型与 owned value 识别**
   - 先扫描函数参数、block parameters、局部定义和全局对象，记录每个值的 LLVM 类型和逻辑类型
   - `poly` 这类拥有堆内存的值被视为 owned value，需要后续生命周期管理

3. **所有权活跃分析（Ownership Analysis）**
   - 对 owned value 计算 `liveIn`、`liveOut` 和每条语句后的活跃集合
   - terminator 的边参数按目标 block parameter 类型计入活跃性
   - return、call、store、普通表达式都会记录对 owned value 的使用

4. **边参数 move/clone/release 计划**
   - 对每条 CFG 边构造 edge emission plan
   - 如果一个 `poly` 值传给后继后当前路径不再使用，可以 move
   - 如果后续仍需要该值，则 clone
   - 对离开边后不再活跃的 owned value 插入释放
   - 如果 edge plan 改写了 poly 实参，phi incoming 也同步改写

5. **多项式运行时调用生成**
   - `load` / `store` 进行复制并 drop 旧值
   - `poly_construct` 生成多项式对象
   - `combine` 用扫描线算法分段处理，最小化内存读写次数
   - `pointwise` 生成循环，在 NTT 域内逐点计算表达式树
   - `ntt` / `intt` 调用运行时转换表示
   - `get_coeff` 读取指定项，越界返回零
   - `get_attr` / `set_attr` 直接读写 `%YesodPoly` 结构体字段

因此，Koopa 层的 block-argument SSA 在 LLVM 层才最终落成传统 `phi` 指令；`poly` 的 move、clone 和释放也在这一阶段根据后端活跃分析插入。

## 3 实现情况

### 3.1 工作量统计

| 模块 | 源文件 | 主要功能 |
|------|--------|---------|
| **前端：解析器** | `parser.h`, `parser.cpp` | PEG 手写递归下降解析，30+ 种错误诊断 |
| **前端：AST** | `ast.h`, `ast.cpp` | AST 节点定义，基础访问者模式 |
| **前端：语义分析** | `semantic.h`, `semantic.cpp` | 语义分析入口，聚合各子分析器 |
| **前端：符号解析** | `semantic_symbol.h`, `semantic_symbol.cpp`, `semantic_symbol_impl.h` | 符号表、作用域管理 |
| **前端：类型分析** | `semantic_type.h`, `semantic_type.cpp`, `semantic_type_impl.h` | 类型推导、常量折叠、类型检查 |
| **前端：CFG构建** | `semantic_cfg.h`, `semantic_cfg.cpp`, `semantic_cfg_impl.h` | 控制流图构建与简化 |
| **前端：SSA构建** | `semantic_ssa.h`, `semantic_ssa.cpp`, `semantic_ssa.h` | SSA 形式构建、支配树、活跃分析 |
| **前端：诊断** | `diagnostic.h` | 错误诊断基础设施 |
| **中端：Koopa IR** | `ir.h`, `ir.cpp` | IR 定义、序列化、验证 |
| **中端：AST→Koopa** | `ast_to_koopa.h`, `ast_to_koopa.cpp` | AST 到 Koopa IR 翻译 |
| **中端：CSE** | `cse.h`, `cse.cpp` | 公共子表达式消除 |
| **后端：LLVM 生成** | `llvm.h`, `llvm.cpp` | Koopa IR 到 LLVM IR 翻译 |
| **后端：Poly 运行时** | `llvm_poly_runtime.h` | 多项式运行时库（NTT、内存分配器） |
| **入口** | `main.cpp` | 编译器主入口，多模式支持 |

### 3.2 代码结构设计

源代码组织遵循三层架构：

```
src/
├── main.cpp                    # 编译器入口、参数解析、流水线编排
├── utils.h                     # Arena 分配器、Ref/Ptr 句柄、MATCH/WITH 宏
├── frontend/                   # 前端：词法/语法/语义分析
│   ├── ast.h / ast.cpp        # AST 节点定义、基础访问者
│   ├── parser.h / parser.cpp  # PEG 解析器
│   ├── semantic.h / ...       # 语义分析各子阶段
│   └── diagnostic.h           # 诊断类型
├── koopa/                      # 中端：Koopa IR
│   ├── ir.h / ir.cpp          # IR 定义与序列化
│   ├── ast_to_koopa.h / ...   # AST 到 IR 翻译
│   └── cse.h / cse.cpp        # 优化 Pass
└── backend/                    # 后端：LLVM IR 生成
    ├── llvm.h / llvm.cpp      # LLVM IR 生成器
    └── llvm_poly_runtime.h    # 多项式运行时
```

**关键设计决策**：

- **Arena 分配器**：所有 AST 和 Koopa IR 节点均通过 `Arena` 分配器统一管理，使用类型安全的 `Ref<T>`（非空引用）和 `Ptr<T>`（可为空指针）句柄。这消除了手动内存管理的负担，确保确定性释放且无内存泄漏。
- **变体匹配宏**：`MATCH(variant) WITH(lambda1, lambda2, ...)` 宏提供类型安全、穷尽检查的变体匹配，是 `std::visit` 的用户友好封装。
- **PEG 解析器**：采用 PEG 文法（比传统的上下文无关文法更易于处理运算符优先级和左递归），解析器本身为手写递归下降实现，具有精确的错误定位和恢复能力。
- **多阶段语义分析**：语义分析分为符号解析、类型分析、CFG 构建、SSA 构建四个独立子阶段，每个阶段可以单独测试，降低了整体复杂度。
- **Koopa IR 作为桥梁**：自定义的 Koopa IR 起到了前端与后端之间的桥梁作用，它同时具备高层语言语义（如 `mint`、`poly` 类型）和底层控制流表示（基本块、SSA），使得优化 Pass 可以在一个统一的 IR 上工作；此外每个算子的代码生成逻辑可以独立实现，便于维护和扩展。

### 3.3 项目实现过程中的经验

1. **PEG 解析器的错误恢复**：早期版本的解析器在遇到语法错误时会直接失败，无法报告多个错误。后期引入了带有错误token同步的错误恢复机制，大幅提升了用户体验。

2. **SSA 构建的复杂性**：支配树计算和 $\phi$ 节点插入是编译器实现中最复杂的部分之一。通过先将 CFG 简化为 DAG（消除回边），再处理循环，逐步构建完整的 SSA 形式。

3. **多项式类型的内存管理**：`poly` 类型在堆上分配系数数组，其生命周期管理是巨大的挑战。采用 SSA 形式的活跃性分析来确定释放点，避免了引用计数和 GC 的开销，而且可以自动 move 或复用数组（例子可参考示例代码的读入和 `set_coeff` 函数），但是可能释放点数量很多（例如一个巨大的 if-else 结构），总的来说是一次有趣的尝试。

4. **乘法的精度处理**：实现有限域类型需要处理 Montgomery 约简，且中间结果需要 64 位精度。将此运行时逻辑提取为独立的 C 运行时库（通过 clang 编译为 LLVM IR 后注入），避免了直接手写 LLVM IR 的繁琐和易错。

5. **测试驱动开发**：项目始终坚持先写测试再实现的策略，每个特性都有对应的单元测试，此外还实现了解释器（`interpreter.cpp`）作为参考实现，确保编译器生成的代码与解释器行为一致。

### 3.4 AI 使用说明

在本项目的开发过程中，AI 辅助编程在以下环节发挥了重要作用：

#### 3.4.1 使用方式

人工设计编译器框架和算法思路，让 Agent 生成代码和测试直到测试通过，然后人工审查和补充测试。

测试用例包括：
1. 单元测试
2. IR 合法性断言
3. 程序行为断言（与解释器结果一致）

IR 化简相关 pass 的工作流：人工设计输入程序，让 Agent 生成 Koopa IR，人工审查 IR 形式，让 AI 修复相关 pass 的 bug 并创建新的 IR 合法性断言。

此外，一些示例程序的编写、代码重构工作也交给 AI 生成。

#### 3.4.2 使用的 Agent 和 Model，费用估算

6 月前主要使用 **GitHub Copilot**（GPT-5.4），Copilot Plus 订阅 10$，约消耗月限额的 70%（210 次请求）。

6 月后主要使用 **Codex**（GPT-5.5），Plus 订阅 20$，没有购买额外 token，大约也是用了 70% 左右。

一些简单任务交给 Deepseek，例如代码重构和 benchmark 脚本编写，感觉应该不超过 10￥

#### 3.4.4 使用经验与教训

**经验**：
1. **AI 擅长生成样板代码**：类型转换、序列化、访问者模式的各个 visit 方法等重复性代码，AI 的生成效率远高于手写
2. **与 AI 进行算法讨论有启发**：在多项式优化 Pass 的设计中，与 AI 讨论不同方案有助于开拓思路
3. 我试着手搓了一套可复用的提示词，让 AI 在实现解析器前先生成一个完整的 PEG 文法描述，之后再生成解析器代码。这个方法在实现复杂语法时有帮助。
4. 在编译器测试中，程序行为断言很方便实现和迁移。

**教训**：
1. **AI 生成的代码必须经过完整测试**
2. **AI 对领域知识理解有限**：对于 NTT、Montgomery 约简、SSA 构建等领域知识，AI 的理解可能不准确或过于简化，需要人工严格把关
3. 记忆管理：AI 不会主动更新文档，导致后续记忆混乱，需要人工维护。

## 4 达成效果

### 4.1 标准 SysY 功能覆盖

编译器完整支持标准 SysY 语言的全部特性，可以通过 **autotest** 的全部测试和 [**lvX**](https://github.com/jokerwyt/sysy-testsuit-collection) 的大部分测试。

我删除了 lvX 中的部分性能测试，因为此项目不涉及后端性能优化。

此外原始 lvX 中 long_func 系列测试暴露出编译器的一个问题：递归解析长表达式会爆栈。但是我觉得人类不会写出这样的代码，所以不予处理。

手动测试除零不会常量折叠。

### 4.2 扩展功能

#### 4.2.1 `mint` 有限域类型

- 完整的四则运算支持（加、减、乘、除）
- Montgomery 约简优化，消除除法指令
- 与 `int` 类型的双向显式转换
- 在 LLVM 后端的运行时支持

#### 4.2.2 `poly` 多项式类型

- 多项式算术（加、减、乘/卷积）
- NTT/INTT 硬件加速的多项式乘法（$O(n \log n)$ 复杂度）
- 切片、移位、系数提取
- 点值融合算子（`pointwise`）避免中间 NTT/INTT 转换
- 合并折叠算子（`combine`）优化多项式视图运算的线性组合
- 基于 SSA 的活跃性驱动的内存管理，自动移动优化，且确保无泄漏
- 所有权分析驱动的参数传递优化（减少堆复制）

示例代码可以在 `test/testsuit-collection/poly` 目录下找到。

### 4.3 Benchmark 性能表现

收集了本人之前写的代码（但是为了公平替换 NTT 实现为效率更高的版本），以及洛谷在线评测平台的无指令集优化、常数优秀实现（多项式 exp 的分治法没有在排行榜上找到，所以抄的一篇题解）。

代码和测试脚本见 benchmark 目录，统一 `clang` 编译器，开启 `-O2` 优化。

测试了三个题目的表现，统一数据规模 $n = 2^17$：
1. 多项式除法（$m=n/2$）
2. 多项式 exp （半在线卷积，分治实现）
3. 多项式 exp （牛顿迭代实现）

| 测试用例 | 参赛者 | 平均耗时 (ms) | 标准差 (ms) |
|---------|--------|:-----------:|:-----------:|
| **多项式除法** | YESOD 编译器 (SysY→LLVM→-O2) | 83.3 | 5.5 |
| | 参考 C++ 实现 | 57.2 |       1.5 |
| | 作者 C++ 实现 | 88.2 |       1.3 |
| **指数（分治）** | YESOD 编译器 (SysY→LLVM→-O2) | 133.6 |       2.6 |
| | 参考 C++ 实现 | 449.5 |       7.9 |
| | 作者 C++ 实现 | 84.7 |       1.5 |
| **指数（牛顿）** | YESOD 编译器 (SysY→LLVM→-O2) | 110.9 |       6.4 |
| | 参考 C++ 实现 | 80.9 |       1.9 |
| | 作者 C++ 实现 | 109.6 |       2.0 |

总结：和人工编写的最优实现仍有一定效率差距，但和不那么注重效率的版本在多项式较少时表现基本持平。推测分治法效率差距主要在于动态分配数组、维护多项式长度等额外信息的开销。

### 4.4 测试覆盖

项目拥有多层次的测试体系：

| 测试层级 | 数量 | 用途 |
|---------|:---:|------|
| 解析器单元测试 | 11 个测试集 | 测试每个语法构造的解析正确性 |
| 语义分析测试 | 7 个测试集 | 测试符号解析、类型检查、常量折叠、SSA 构建 |
| Koopa IR 验证测试 | 1 个测试集 | 验证 IR 结构的合法性 |
| Koopa IR 解释执行测试 | 7 个等级 | 使用 Koopa 解释器运行标准测试集 |
| LLVM IR 端到端测试 | 7 个等级 + poly | 完整编译链测试 |
| 多项式基础测试 | 1 个测试集 | 多项式运行时功能验证 |
| 多项式扩展测试 | 1 个测试集 | 多项式高级特性测试 |
| 多项式压力测试 | 1 个（手动） | $n=10^5$ 规模的大数据测试有无复杂度退化 |

### 4.5 代码质量

- **内存安全**：通过 Arena 分配器和 RAII 管理所有资源，无手动 `new`/`delete`
- **类型安全**：`Ref<T>` / `Ptr<T>` 区分非空和可空引用，`std::variant` + `MATCH`/`WITH` 穷尽检查
- **错误诊断**：精确的行列号定位 + 源码行 + 箭头指示，友好的错误信息（没有充分测试过，可能还有很多 BUG）
- **测试覆盖**：多层次测试体系，持续集成验证

## 5 参考文献

1. **SysY 语言定义** - 北大编译实践在线文档
2. **Koopa IR 规范** - 北大编译实践在线文档
3. **PEG (Parsing Expression Grammar)** - Ford, Bryan. "Parsing expression grammars: a recognition-based syntactic foundation." POPL 2004
4. **SSA (Static Single Assignment)** - Cytron, Ron, et al. "Efficiently computing static single assignment form and the control dependence graph." ACM TOPLAS 1991
5. **Montgomery Modular Multiplication** - Montgomery, Peter L. "Modular multiplication without trial division." Mathematics of Computation 1985
6. **LLVM Language Reference Manual** - https://llvm.org/docs/LangRef.html
7. **NTT (Number Theoretic Transform)** - 用于快速多项式乘法的数论变换，基于模 $998244353$ 质数域
8. **循环卷积与 NTT 加速** - 多项式卷积的 NTT 加速原理，$O(n \log n)$ 时间复杂度