# 多项式类型扩展 — 实现计划

## 已完成 ✅

### 规范文档

- [x] `doc/poly.md` 补完"语义规范细化"章节：
  - 类型定位：`poly` 是一等非标量类型，`!p` 表示长度，`p[k]`→`mint`、`p[n,m]`→`poly`，`poly(exp)` 显式构造
  - 对象身份与别名规则：`e` 模式必须独占底层对象，编译器无法证明则保守复制；`r` 模式允许别名（借用）
  - 首版排除项：无全局 `poly`、无元素为 `poly` 的数组、无隐式 `int→poly`

### AST（`src/frontend/ast.h` / `ast.cpp`）

- [x] `BTypeKeyword` 和 `FuncTypeKeyword` 各增加 `polyKeyword`
- [x] `Exp::Slice { base, start, end }` —— 片语法 `p[n,m]`
- [x] `Exp::Subscript { base, index }` —— 通用下标 `(expr)[k]`
- [x] `AstVisitor` 增加 `visitSliceExp` / `visitSubscriptExp`

### 解析器（`src/frontend/parser.cpp`）

- [x] `parseBType()` 支持 `"poly"` 关键字
- [x] `parseFuncType()` 支持 `"poly"` 关键字
- [x] `parseCompUnit()` 顶层 `poly` 声明/函数定义（复用现有 mint 模式）
- [x] `parsePrimaryExp()` 支持：
  - `identifier[exp, exp]` → slice（在 LVal 前检测，避免 LValIndices 的贪婪消耗）
  - 带括号表达式后循环解析尾部 `[exp,exp]`（slice）和 `[exp]`（subscript），可链式如 `(a+b)[1,5][2]`

### 语义类型系统（`src/frontend/semantic_type.h` / `.cpp`）

- [x] `SemanticTypeKind` 和 `ExpType` 各增加 `poly`
- [x] `SemanticType::makePoly()`、`isPoly()`
- [x] `lowerBType(polyKeyword)`、`lowerFuncType(polyKeyword)`
- [x] 类型检查规则：
  - `!poly` → `int`
  - `poly[k]`（LVal 下标）→ `mint`
  - `(expr)[k]`（Subscript）→ `mint`
  - `poly[n,m]`（slice）→ `poly`
  - `poly(exp)` 构造：exp 必须为 int 或 mint
  - `poly +-* poly` → `poly`
  - `poly << / >> int` → `poly`
  - `poly * mint` / `mint * poly` → `poly`

### 测试

- [x] `test/parser/parser_poly_test.cpp` —— 15 个测试，涵盖：
  - `poly` 局部变量声明 + `poly(exp)` 构造
  - `!poly` 长度提取
  - `p[k]` 系数提取
  - `p[n,m]` 片语法
  - `>>` / `<<` 移位
  - `+-*` 算术
  - `poly(x)` 变量参数构造
  - 变量作为 slice 起止
  - `(a+b)[k]` 括号表达式下标
  - `(a+b)[n,m]` 括号表达式 slice
  - `(a+b)[1,5][2]` 链式操作
  - 破损语法恢复诊断
- [x] `test/CMakeLists.txt` 注册 `parser_poly_test`
- [x] 测试支持文件：`requireSliceExp`、`requireSubscriptExp` 助手；`Exp::Kind` variant_size 更新；所有 `evaluateExp` MATCH 增加 Slice/Subscript 分支

### SSA / IR / 后端 占位

- [x] `semantic_ssa.cpp` `walkExp` 增加 Slice/Subscript 遍历
- [x] `ast_to_koopa.cpp` 增加 Slice/Subscript 占位桩（`throw runtime_error`）
- [x] `src/koopa/ast_to_koopa.cpp` 所有 `MATCH` 语句增加 Slice/Subscript case

### 构建状态

- [x] 全部已有测试（12 parser + 9 semantic/koopa + backend_riscv + compiler_llvm）通过
- [x] 修复 10 个预先存在的 koopa/backend 测试断言失败：
  - `pruneUnreachableBlocks` 保留 `%end` 块（即使不可达也保留为默认返回值守卫）
  - `backend_riscv_test` 位运算预期值修正（`-15`→`-23`，经 GCC 验证）
  - `koopa_if_test` 适配不可达 continuation 块被剪枝后的新 block 结构和计数
  - `koopa_array_test` 接受 `getelemptr` 作为数组到指针退化的合法形式
- [x] 全部 32 个测试 100% 通过

---

## 所有权模型细化（v2）

以下规则是对 Phase 3–5 中所有权与内存管理逻辑的精确补充或修订，所有实现必须遵守。

### 0. 局部 poly 数组离开作用域清理

当离开局部 poly 数组的作用域时，必须遍历数组中的每一个元素（每个元素均为 `poly` 类型），对每个元素依次调用 `__yesod_poly_free` 释放其堆内存。释放顺序为从后往前（数组末尾到开头），确保不存在 double-free 风险。数组本身的栈空间由正常的栈帧清理完成。

> 注意："首版排除项"中排除了元素类型为 `poly` 的数组在全局作用域的使用，但**局部** `poly` 数组是支持的。此规则即为该场景下的析构规范。

### 1. 存储空间独立规则

在函数内部视角，不同来源的 poly 值具有不同的存储空间独立性保证：

| 来源 | 存储独立性 | 说明 |
|------|-----------|------|
| 局部变量 / 表达式临时值 | 独立 | 每次分配均有独立堆内存 |
| `e` 模式参数 | 独立 | 调用方保证已复制或转移独占对象 |
| `r` 模式参数 | 可共享（别名） | 多个 `r` 参数可指向同一底层对象 |
| 全局变量 / 全局数组 | 独立 | 与局部/参数均不共享 |
| 函数数组参数（形参为数组） | 独立 | 与局部/参数均不共享 |

**别名边界规则：** `r` 模式参数之间可以共享存储，但 `r` 模式参数不得与以下任何来源共享存储：`e` 参数、全局变量、全局数组、函数数组参数。若调用方违背此规则，则必须在调用前插入复制以确保 callee 视角的合规性。

### 2. 函数返回值移动语义

所有 `poly` 类型的函数返回值均采用**移动语义**——被调用者返回一个 poly 值时，调用方获得该值的所有权。具体规则：

| 返回的表达式类型 | 行为 |
|----------------|------|
| 局部变量 / 表达式临时值 | 复制一次后返回（因返回后原值应释放，但返回值生命周期需延续到调用方） |
| `e` 模式参数 | 复制一次后返回（同理） |
| 局部数组 `arr` 中取出的一项 `arr[i]` | 将 `arr[i]` 的多项式数据**移动**出来（数组该位置变为零多项式），然后释放数组中每一个剩余 poly 元素的内存（从后往前遍历释放），再将移动出的值作为返回值。此优化避免了对被移出项的深复制 |
| `r` 模式参数 | 必须复制（因 `r` 为借用，仅读不释放） |

> 实现提示：对于局部数组的移动返回，在 `arr[i]` 位置用零多项式（`{nullptr, 0}`）覆盖原值，再执行标准的数组析构（从后往前 free 每个元素），最后将移动出的 `{ptr, len}` 对作为返回值。

### 3. 传参所有权特化规则（细化版）

以下规则替换 Phase 3 中的旧适配器表格。**指导原则：** 如果一个值可能通过其他方式被访问，必须复制并使用 `e` 模式；如果一个值不再活跃，优先使用 `e` 模式以减少复制。

| 实参来源 | 特化模式选择 | 复制策略 |
|----------|-------------|----------|
| 全局变量 / 全局数组 / 函数数组参数 | 必须 `e` | **复制一次**（因全局/数组值在调用后仍可能被其他路径访问） |
| `poly` 类型函数参数 / 局部变量 / 中间结果 | 根据活跃分析选 `e` 或 `r` | 若不活跃且该值**只被传参一次**（反例：`f(p+q, p+q)` 中 `p+q` 被传递给两个参数），选 `e` 且不复制；否则选 `r` 且不复制 |
| 局部数组 `arr` 的左值访问（仅下标提取 `arr[i]` 且结果为 `poly`，未参与其他运算） | 全部 `r` 或全部 `e + 复制` | 如果本次调用中**所有**以 `arr` 为基的访问在使用 `poly` 类型的位置，则全部使用 `r` 模式（零复制）；否则全部先复制再使用 `e` 模式 |
| 同一值传递给同一调用的多个参数 | 按最严格约束 | 若多个参数均为 `poly`，根据上述行逐参数判断。若任一参数需 `e` 模式且该值被多于一个参数引用，则需对该参数执行复制以满足互异约束 |

> 关键示例：`f(p, p)` 中，若 `p` 是局部变量且调用后仍活跃，则第一个参数可 `r`、第二个参数可 `r`，无需复制。若 callee 定义强制要求两个参数均为 `e`，则必须分别复制两个副本。

### 4. 活跃分析简化规则

为降低实现复杂度，采用以下简化方案替代完整的逆向数据流活跃分析：

1. **全 `e` 版本生成**：对每个函数，预先生成一个所有 poly 参数均为 `e` 模式的版本（mask = 全 1）。该版本用于保守推导参数在函数出口处的活跃性。
2. **可达 end 分析**：在 CFG 上标记哪些基本块存在可达函数出口（`end` / `return`）的路径。
3. **简化活跃判定**：
   - 对于可达 `end` 的基本块：其块参数列表中所有 `r` 模式 poly 参数**均视为在出口处活跃**（即不能释放）。
   - 对于不可达 `end` 的基本块（如无限循环、unreachable）：其 `r` 模式 poly 参数视为不活跃，可释放。
   - 对于 `e` 模式 poly 参数：在函数内与局部变量相同处理，**不**在出口处标记为活跃（因被调用者拥有所有权，需负责释放或返回）。
4. **无需迭代求解**：此规则为一次性标记，不依赖数据流迭代，因此实现简单、编译速度快。

> 此简化规则与 Phase 3 的调用点适配逻辑协同工作：调用方在决定是否将实参标记为 `e` 时，依据的是调用**后**该值是否仍被使用（调用方视角的活跃性），而非 callee 内部的活跃性。

---

## 待实现 🔜

### Phase 2 — SSA 基础设施推广 ✅

- [x] **推广 SSA 追踪范围**：`semantic_ssa.cpp` 中 `m_localScalarSymbols` 重命名为 `m_localTrackedSymbols`，现在同时追踪局部标量和 `poly` 类型的 symbol id。所有 SSA 构造（支配边界、use/def、活跃变量、块参数、别名版本）对 poly 符号同样适用
  - 注意：Phase 2 **不** 为 SSA alias 附加 poly 专属事实（如长度、非零区间、活跃区间等）。每类事实由后续对应语义分析模块（Phase 3 所有权分析 / Phase 4 长度 / Phase 7 区间分析）独立负责
- [x] **预 SSA CFG 化简与不可达块删除**：在 `semantic_cfg.cpp` 中实现 `simplify()` 方法：
  - **Phase 1**：遍历所有基本块的终止指令，若为条件分支且条件表达式经常量折叠后确定为常量，则将分支替换为无条件跳转。使用 `optional::emplace()` 就地构造新变体，避免 `std::bad_variant_access`
  - **Phase 1.5**：从入口块做可达性遍历，删除所有不可达基本块（保留合成的 `%end` 守卫块）。修复了 `while(0)` 等常量死分支导致 SSA / Koopa 对死块仍做 lowering 的崩溃
  - **Phase 2**：合并唯一前驱/后继的可达块（单次遍历）。排除 `%end` 块和入口块，正确更新所有剩余块的终结跳转目标。合并后再做一次不可达块清理
  - 化简 pass **已激活**：在 `semantic.cpp` 的 `SemanticAnalyzer::analyze()` 中，位于 CFG 构建之后、SSA 分析之前调用 `info.m_loopBinder->simplify(expInfo)`
  - 已在 `SemanticCFG` 和 `SemanticCFGBuilder` 上暴露公共方法

**涉及文件：**
- `src/frontend/semantic_ssa.h` —— 仅需支持 poly-typed symbol 的 SSA 构造，不附加 poly 事实字段（无改动）
- `src/frontend/semantic_ssa.cpp` —— 修改 `buildSsa()` 使其处理 poly-typed 符号；`isScalarObject` → `isTrackedObject`
- `src/frontend/semantic_cfg.h` —— 增加 `simplify()` 声明
- `src/frontend/semantic_cfg.cpp` —— 实现预 SSA CFG 化简 pass

### Phase 3 — 所有权分析与函数特化

- [ ] **函数 poly 参数位置元数据**：在语义符号表中为每个函数符号添加紧凑的 poly 参数序位映射。独立于 `r`/`e` 选择，仅在语义类型层面记录"第 i 个形参是 poly 类型"。将此元数据暴露给 SSA 和 lowering 阶段：
  - 在 `FuncSymbol`（或等价的函数符号结构体）中增加 `std::vector<int> m_polyParamOrdinals`，记录所有 poly 类型形参在参数列表中的索引
  - 所有所有权特化决策保留到后期（调用点适配时），不在解析和符号解析阶段决定
- [ ] **活跃分析简化**：详见"所有权模型细化（v2）"第 4 节。对每个函数生成全 `e` 版本，标记可达 `end` 的基本块。`r` 模式参数在可达 `end` 的块出口处视为活跃（不可释放），在不可达 `end` 的块中视为不活跃（可释放）。此标记为一次性标记，不依赖迭代求解
- [ ] **特化版本命名与惰性生成**：按 `__yesod_gen_{name}_{mask}` 命名，其中 `mask` 为 n 位二进制串（`r`=0, `e`=1），覆盖 n 个 poly 参数的所有可能的 `r`/`e` 组合。只生成被实际调用引用的特化版本，不做预生成：
  - 首次遇到对某特化版本的调用时，克隆原函数 AST/SSA 骨架，为 `e` 参数位置注入写权限语义（标记需要释放）
  - 未遇到的特化版本永不生成
- [ ] **调用点适配规则（核心逻辑）**：详见「所有权模型细化（v2）」第 3 节。此处列出实现要点：
  1. **局部最后使用 → `e`（零成本转移）**：当实参是局部已分配的 poly、且该 SSA value 在调用后不再被引用，且该值只被传递给**一个**参数位置（非 `f(p+q, p+q)`），直接转移所有权，无需复制
  2. **调用后仍活跃的局部对象 → `e` + 复制**：实参在调用后仍被引用，但调用方需要 `e` 模式：先 `__yesod_poly_clone`（深度复制新对象），再将复制品传递给 callee，原值继续存活
  3. **全局/全局数组/函数数组参数 → 必须 `e` + 复制**：因这些值在调用后可能通过其他路径被访问，保守复制一次，用 `e` 模式传递
  4. **局部数组的左值访问 → 统一模式**：若本次调用中所有以该数组为基的访问均为 `poly` 类型位置，全部使用 `r` 模式；否则全部复制用 `e` 模式
  5. **同一调用中多个 `e` 实参必须互异**：即使源自同一个 SSA value（如 `f_e_e(p, p)`），也必须复制其中之一或分别复制，确保 callee 视角下两个参数指向不同的底层对象

**涉及文件：**
- `src/frontend/semantic_type.cpp` —— 在函数符号中增加 `m_polyParamOrdinals`
- `src/frontend/semantic_type.h` —— 暴露 poly 参数元数据接口
- `src/frontend/semantic_ssa.cpp` —— 调用点遍历逻辑判断每次使用是否为"最后使用"
- `src/koopa/ast_to_koopa.cpp` —— 生成特化函数声明/定义/调用，插入复制/转移代码

### Phase 4 — Poly 分析 Pass（基于 SSA）

分析 Pass 按顺序执行，每个 Pass 依赖前一个 Pass 的输出。每次重写后立即进行常量折叠。

- [ ] **Pass 1：多项式长度代码生成**（`semantic_poly_length.h/.cpp`）
  - 正向数据流分析，为每个 poly SSA value 推导其长度表达式
  - 引入辅助 IR 运算（若 Koopa 层尚不支持）：
    - 三元条件选择：`cond ? a : b`
    - `min(a, b)` / `max(a, b)`
  - 来源规则：
    - `poly(exp)` 构造：长度为 `exp`（当 exp 类型为 int）或 `!exp`（当 exp 类型为 poly，即复制场景）
    - `p[n, m]` 切片：长度为 `clamp(m - n, 0, len(p))`（无效范围取 0）
    - 二元运算 `a + b`、`a - b`、`a * b`：长度为 `max(len(a), len(b))`
    - 移位 `p >> k`、`p << k`：长度分别为 `max(len(p) - k, 0)` 和 `len(p) + k`
    - 函数返回值的 poly：从返回值元数据字段读取
    - 全局/数组元素中的 poly：从对应内存字段读取
    - 块参数：从前驱块的对应实参表达式传播
  - 所有新合成的表达式立即常量折叠

- [ ] **Pass 2：非零区间分析（占位版本）**（`semantic_poly_range.h/.cpp`）
  - 当前实现正确但无用的占位版本：为每个 poly SSA value 直接返回 `[0, len)`，即假设所有系数均可能非零
  - 这保证后续 NTT/融合代码可正常编译运行，但不做任何优化
  - 真正的非零区间分析推迟到 Phase 7

- [ ] **Pass 3：活跃区间分析（占位版本）**（`semantic_poly_liveness.h/.cpp`）
  - 当前实现正确但无用的占位版本：为每个 poly SSA value 直接返回 `[0, len)`，即假设整个区间均在后续运算中被使用
  - 这保证后续 NTT/融合代码可正常编译运行，但不做任何优化
  - 真正的活跃区间分析推迟到 Phase 7

- [ ] **Pass 4：NTT 算子插入**（`semantic_poly_ntt.h/.cpp`）
  - 对每个 poly 乘法运算 `c = a * b`，在其左右插入 NTT 域转换：
    - `a' = ntt_forward(a)`（在插入处对 a 施加前向 NTT）
    - `b' = ntt_forward(b)`
    - `c_t = a' * b'`（逐点乘法，在 NTT 域中执行）
    - `c = ntt_backward(c_t)`（逆变换回系数域）
  - 后续可选的线性性质重写：`ntt(a + b) = ntt(a) + ntt(b)`（若 a, b 均需进入 NTT 域）
  - 后续可选的公共子表达式消除：若同一 poly 值参与了多次 NTT 变换，只计算一次
  - 避免对常量或已知为点值的 poly 插入不必要的 NTT（点值表示作为 SSA 值的分析属性，非用户可见类型）

- [ ] **Pass 5：乘法算子融合与代码生成**
  - 确定 NTT 的计算长度：取满足 `len ≥ max(len(a), len(b))` 的最小 2 的幂（`ceil2(max_len)`）
  - 该长度用于 `ntt_forward`、逐点乘法和 `ntt_backward` 的所有循环边界
  - 生成显式循环结构（或将 NTT 调用降低为对运行时 helper `__yesod_ntt` 的调用）

- [ ] **Pass 6：加法算子融合**
  - 将 poly 加法链化简为标准形式：`∑ (aᵢ[lᵢ, rᵢ] >> kᵢ) · cᵢ`
    - `aᵢ` 为基础 poly 值
    - `[lᵢ, rᵢ)` 为活跃区间
    - `kᵢ` 为右移量（来自 `>>` 运算）
    - `cᵢ` 为乘系数（来自 `* mint` 运算）
  - 扫描所有输出系数范围，将落在同一索引位置的多个项合并
  - 合并后生成最终的累加循环

**涉及文件：**
- 新增 `src/frontend/semantic_poly_length.h/.cpp` —— 长度代码生成 Pass
- 新增 `src/frontend/semantic_poly_range.h/.cpp` —— 非零区间分析（占位版本，Phase 7 替换为真实版本）
- 新增 `src/frontend/semantic_poly_liveness.h/.cpp` —— 活跃区间分析（占位版本，Phase 7 替换为真实版本）
- 新增 `src/frontend/semantic_poly_ntt.h/.cpp` —— NTT 插入/乘法融合/加法融合 Pass

### Phase 5 — Lowering、ABI、运行时

- [ ] **Koopa IR 增加 `PolyType`**：在 `src/koopa/ir.h` 中新增 `PolyType` 类型：
  - 允许函数参数、块参数（block params）和返回值携带 `PolyType`
  - `PolyType` 在 IR 文本格式中的表示：`@poly`
  - 序列化/反序列化支持：`dumpType()` / `parseType()` 增加 poly case
  - 注意：将 poly 算术和 NTT 运算降低为显式的 helper 调用、循环和内存操作，不在最终 Koopa IR 中保留通用 poly 算术指令。只有跨越 CFG 边界和函数边界的 poly 值保持一等公民状态

- [ ] **返回值移动语义实现**：详见"所有权模型细化（v2）"第 2 节。在生成函数返回代码时，对返回的 poly 表达式分类处理：
  - 局部变量 / 表达式临时值 / `e` 模式参数：先深度复制一份，再释放原值（若为局部或 `e` 参数），最后返回副本的 `{ptr, len}`
  - `r` 模式参数：直接深度复制后返回（不释放原值，因仅借用）
  - 局部数组 `arr[i]` 的移动返回：将 `arr[i]` 的 `{ptr, len}` 取出，在数组该位置写入零多项式 `{nullptr, 0}`，然后从后往前遍历数组释放每个元素的堆内存，最后将取出的值作为返回值
  - 返回值在 Koopa IR 层面表现为 `@poly` 类型，由后端 ABI 负责展开为 `a0/a1`

- [ ] **特化函数生成**：在 `ast_to_koopa.cpp` 中将所有权分析和 poly pass 的结果转换为 Koopa IR：
  - 为每个被引用的特化 mask 生成对应的 Koopa 函数声明和定义
  - 特化函数的名字直接映射为 `__yesod_gen_{name}_{mask}` 的 Koopa 符号
  - 特化函数的参数列表中，`e` 模式的 poly 参数标记为 `@poly`，`r` 模式的 poly 参数同样标记为 `@poly`（区别在于降级阶段插入的释放代码）
  - 调用点生成时，根据 Phase 3 选择的 mask 和适配规则，展开实参的传递逻辑（含可能的复制）

- [ ] **RISC-V 后端 ABI**：
  - **`ValueShape` 扩展**：新增 pair 模式 `Shape::Pair`，表示 poly 值占用两个机器字：`{ptr: i32*, len: i32}`。每个分量在寄存器/堆栈中作为独立的 4 字节值处理
  - **`FunctionSignature` 扩展**：支持多值返回，poly 返回值占据 `a0`（ptr）和 `a1`（len）两个寄存器
  - **`emitCall()` 传参**：
    - poly 实参展开为两个参数顺序压入（ptr 在前，len 在后）
    - 在寄存器分配中占两个寄存器槽位（优先使用 `a0-a7` 中的连续两个，或重叠到堆栈）
    - 超出寄存器容量（`a0-a7` 共 8 个寄存器，poly 占 2 个槽位）时，按调用顺序压栈，每个分量作为独立的 4 字节域
  - **`emitReturn()`**：
    - 返回 poly 值时，设 `a0 = ptr`、`a1 = len`
    - 不可使用 hidden sret（与双寄存器 ABI 一致，同时简化所有权转移）
  - **块参数跨边传递**：
    - 本块出口处的 poly 块参实参展开为两个值（ptr 和 len）分别存储到后继块的对应槽位
    - 涉及 `move` 指令时，展开为两条 `move`（ptr 和 len 分别移动）
  - **对齐保证**：无需额外的 8 字节栈对齐，沿用现有的 16 字节帧对齐规则即可

- [ ] **Poly 运行时注入**（`src/main.cpp`）：
  - 实现 Buddy 分配器：
    - `PAGE_SIZE ≈ 4KiB`（4096 字节），由 `PAGE_ORDER_BITS` 控制页阶数
    - 最小分配块 8 字节
    - 页阶表（free area 数组），每个阶位维护一个空闲块链表
    - 对小于页面的小对象类不做合并（coalescing），以减少实现复杂度
  - 导出的运行时函数：
    - `void* __yesod_poly_alloc(int len)` —— 分配 `len * sizeof(mint)` 字节，返回指针
    - `void __yesod_poly_free(void* ptr)` —— 释放指定指针指向的 poly 对象
    - `void* __yesod_poly_realloc(void* ptr, int oldLen, int newLen)` —— 重新分配，仅在 `oldCapacity ≥ 4 * newLen` 时才真正缩小（否则就地或扩大）
  - 注入条件检测：实现 `programUsesPolyRuntime()` 函数，扫描 Koopa IR 中的函数和类型定义，若存在 `PolyType` 使用或特化函数引用，则注入运行时。运行时声明以 Koopa IR 声明的方式注入到生成程序的开头

- [ ] **析构调度（destruction scheduling）**：
  - **基本块内部**：遍历语句序列，对每个 poly 值的"最后使用"点（由 Phase 2 的 `ownershipEligible` 和活跃区间共同判定）之后立即插入 `__yesod_poly_free` 调用
  - **基本块之间**：若某 poly 值在基本块出口处 live-out（活跃区间在出口后仍有值），但未被作为块参数传递给任何后继块：
    - 将该出口边分裂（edge splitting）：插入一个仅有 `free` 指令的 cleanup 基本块
    - 清理块无条件跳转到原后继块
  - **禁止释放的情况**：
    - `r` 模式的值（借用/只读）永不释放
    - 已通过 `e` 模式转移给 callee 的值视为死亡，caller 不再负责释放（除非调用前做了复制，则原值照常释放）
  - **局部 poly 数组离开作用域清理**（详见"所有权模型细化（v2）"第 0 节）：
    - 在作用域出口（return / 右花括号对应位置）处生成释放循环
    - 遍历方向：从数组末尾到开头，依次对每个元素调用 `__yesod_poly_free`
    - 仅释放数组元素存储的 poly 堆内存，数组本身的栈空间由栈帧清理自然完成

**涉及文件：**
- `src/koopa/ir.h` / `ir.cpp` —— 新增 `PolyType`，扩展类型系统
- `src/koopa/ast_to_koopa.cpp` —— 特化函数生成、调用适配、复制/释放代码生成
- `src/backend/riscv.h` —— `ValueShape` 增加 pair 模式
- `src/backend/riscv.cpp` —— 双寄存器传参、双寄存器返回、块参数展开、cleanup 块
- `src/main.cpp` —— Buddy 分配器实现 + 条件注入

### Phase 6 — 纵深测试

按垂直切片（vertical slice）顺序构建测试。每个切片提交后，相关测试应能独立通过。外部命令测试保持现有的 timeout 纪律。

- [ ] **1. 解析器 + 语义 poly 测试**（首批）：
  - `test/semantic/semantic_poly_test.cpp`（新建）：
    - `poly(int)` 和 `poly(mint)` 显式构造的类型检查
    - `!poly` 返回 `int` 类型
    - `p[k]` 返回 `mint` 类型
    - `p[n,m]` 返回 `poly` 类型
    - poly 二元算术 (`+`, `-`, `*`) 返回 `poly`
    - poly 移位 (`<<`, `>>`) 返回 `poly`
    - `poly * mint` 和 `mint * poly` 类型规则
    - 在 scalar-only 上下文中使用 poly 产生诊断错误（如 `int x = p`、全局 poly 声明等）
    - 越界诊断（负下标、起止顺序错误等）
  - 参考已有文件：`test/semantic/semantic_mint_test.cpp`

- [ ] **2. SSA / 所有权测试**（第二批）：
  - 将测试逻辑加入 `test/backend_riscv_test.cpp` 或新建 `test/semantic/semantic_ssa_poly_test.cpp`：
    - 验证简单调用 `f(p)` 选择的特化 mask（无 `e` 参数引用时选 `r`）
    - 验证 `f(g())` 中 g 返回的 poly 传递给 f 时选择 `e`（返回值最后使用）
    - 验证 `f(p, p)` 同一 poly 值传给两参数时选择（或插入复制）使 `e` 参数互异
    - 验证局部最后使用转为 `e`（转移，无复制）
    - 验证 live-after-call 转为 `e` 时插入复制
    - 验证 distinctness 不可证明时保守选择 `r`（或保守复制到 `e`）
  - 参考已有文件：`test/semantic/semantic_ssa_test.cpp`

- [ ] **3. Koopa IR 测试**（第三批）：
  - `test/koopa/koopa_poly_test.cpp`（新建）：
    - 验证特化函数名称：`__yesod_gen_{name}_{mask}`
    - 验证函数签名包含 `@poly` 类型参数和返回值
    - 验证基本块参数携带 poly 值（展开为 ptr + len 两块参数）
    - 验证函数内部引用了 `__yesod_poly_alloc`、`__yesod_poly_free` 等运行时辅助函数
    - 验证复制/转移在调用点正确生成
  - 参考已有文件：`test/koopa/koopa_mint_test.cpp`

- [ ] **4. 后端 ABI 测试**（第四批）：
  - 扩展 `test/backend_riscv_test.cpp` 或新建：
    - **寄存器配对**：poly 参数和返回值占用 `a0/a1`（返回）/ `a0-a7` 中连续两寄存器（传参）
    - **溢出**：超过 8 个寄存器槽位时，poly 的分量按 4 字节域在调用栈上顺序排列
    - **块参数跨边传递**：poly 作为块参数时，ptr 和 len 分量分别 move/存储
    - **last-use free**：基本块内最后使用后插入 free；跨边界时插入 cleanup 块
    - **r 模式不释放**：验证 `r` 参数对应的 poly 值没有被插入 free

- [ ] **5. 端到端测试**（第五批）：
  - `test/llvm/poly_*.sy`（新建，每个文件一个测试用例）：
    - `poly_construct.sy`：poly 构造 + 长度提取 + 系数访问
    - `poly_arith.sy`：poly 加减法 + 移位 + 输出验证
    - `poly_mul.sy`：poly 乘法（通过 ntt 实现）
    - `poly_slice.sy`：切片操作
    - `poly_call.sy`：函数间 poly 传递，含 `r`/`e` 模式
    - `poly_ntt.sy`：完整的 NTT 参考实现验证（对比 `test/llvm/ntt.sy`）
  - 扩展 `test/compiler_llvm_test.cpp`：注册上述 `poly_*.sy` 测试用例
  - 运行方式：`ctest --test-dir build --output-on-failure -R "(parser_.*poly|semantic_.*poly|koopa_.*poly|backend_riscv_test|compiler_llvm_test)"`

- [ ] **6. 文档同步**（贯穿整个 Phase 6）：
  - 每个切片落地后更新 `doc/poly.md`：
    - 在文档中明确区分"强制性语义"（必须实现的类型规则、所有权约束、ABI）与"可选优化"（NTT 融合的重写策略、延迟合并等）
    - 明确记录 `e` 参数的 no-alias 规则
    - 列出首版实现的 deliberately deferred items（全局 poly、poly 数组、隐式 int→poly 转换）
    - 确保实现者和测试者共享同一份契约

**涉及文件：**
- `test/semantic/semantic_poly_test.cpp`（新建）—— 语义类型检查测试
- `test/koopa/koopa_poly_test.cpp`（新建）—— Koopa IR poly 相关测试
- `test/backend_riscv_test.cpp` —— 后端 ABI 测试扩展
- `test/compiler_llvm_test.cpp` —— 注册 end-to-end 测试
- `test/llvm/poly_*.sy`（新建）—— SysY poly 端到端测试用例
- `test/CMakeLists.txt` —— 注册新的测试目标
- `doc/poly.md` —— 随实现进展更新规范文档

### Phase 7 — Poly 分析优化 Pass（真实区间分析 + 测试）

本阶段将 Phase 4 中的占位 Pass 替换为真实分析实现，并新增对应测试。运行时正确性不依赖此阶段（占位版本已保证正确性），但代码生成质量依赖此阶段。

- [ ] **Pass 2（真实）：非零区间分析**（`semantic_poly_range.h/.cpp`，替换占位版本）
  - 正向数据流 Pass，为每个 poly SSA value 推导其非零系数的索引范围 `[l, r)`，其中 `0 ≤ l ≤ r ≤ len`
  - 格（lattice）定义：`{⊥, [l,r) finite, ⊤(unknown)}`，`⊥` 表示无信息（空前缀）
  - 传递函数：
    - 加减法：`range(a ± b) = range(a) ∪ range(b)`（并集）
    - 乘法：`range(a * b) = convolve(range(a), range(b))` —— 区间卷积
    - 移位：`range(p >> k)` = `[l+k, r+k)` 平移；`range(p << k)` = `[l-k, r-k)` 截断
    - 切片 `p[n,m]`：`range = [l', r')` 其中 `l' = max(l, n)`, `r' = min(r, m)`，若 `l' ≥ r'` 则为空区间
    - 构造 `poly(exp)`：`[0, len)` 初始化为全量
  - 跨基本块传递：随块参数传播到后继块的对应块参数

- [ ] **Pass 3（真实）：活跃区间分析**（`semantic_poly_liveness.h/.cpp`，替换占位版本）
  - 逆向数据流 Pass，推导后续运算中实际使用的系数范围（是 `nonZeroRange` 的子集）
  - 格扩展：引入 `槑`（未知端点）—— 表示"具体边界未知，但已知非空"的区间
    - 格结构：`{⊥, [l,r) finite, 槑, ⊤(unknown)}`
    - `槑` ⊑ `⊤`，`[l,r)` ⊑ `槑`
  - 语义规则：
    - 某 poly 值的切片 `p[n,m]` 对 p 的活跃区间贡献 `[n, m)`（若 n,m 为常量）或 `槑`（若 n,m 为变量）
    - 某 poly 值的下标 `p[k]` 对 p 的活跃区间贡献 `[k, k+1)` 或 `槑`
    - 算术二元运算对 operands 的活跃区间贡献为 `union(lhs_active, rhs_active)` 的适当子集
    - 跨函数调用：传递给 `e` 实参时，callee 视为会使用整个 poly；传递给 `r` 实参时，callee 视为仅使用 `nonZeroRange` 的子集（若 callee 未暴露更窄的区间则保守为全量）
    - 跨基本块边界：任何 poly 值若作为块参数传递给后继，在该基本块出口处的活跃区间取该块参数在继块中活跃区间与当前块非零区间的交

- [ ] **Phase 7 测试**：
  - 新建 `test/semantic/semantic_poly_range_test.cpp`：
    - 验证非零区间的基本运算（加减并集、乘法卷积、移位平移、切片截断）
    - 验证空区间传播（零多项式参与运算）
    - 验证跨基本块传递时区间正确传播
    - 验证格不动点收敛（循环 CFG 中区间稳定）
  - 新建 `test/semantic/semantic_poly_liveness_test.cpp`：
    - 验证活跃区间逆向传播的正确性
    - 验证 `槑` 值在各种运算中的传播规则
    - 验证跨函数调用时 `e`/`r` 模式的区间差异
    - 验证跨基本块边界时活跃区间收缩
  - 参考已有文件：`test/semantic/semantic_ssa_test.cpp`
  - 在 `test/CMakeLists.txt` 中注册新测试目标

**涉及文件：**
- `src/frontend/semantic_poly_range.h/.cpp` —— 替换占位版本为真实实现
- `src/frontend/semantic_poly_liveness.h/.cpp` —— 替换占位版本为真实实现
- `test/semantic/semantic_poly_range_test.cpp`（新建）
- `test/semantic/semantic_poly_liveness_test.cpp`（新建）
- `test/CMakeLists.txt` —— 注册新测试目标

---

## 关键设计决定

| 决定 | 理由 |
|------|------|
| Koopa 层保持 `PolyType` 而非降低到 hidden sret | 与双寄存器 ABI 一致，便于表达块参数和返回值 |
| `e` 模式的唯一性落在调用点适配，而非靠被调用者兜底 | 编译器可见时做复制最安全，运行时检查成本高 |
| 所有权是"函数特化后的参数位置属性"，非源语言类型修饰 | 保持源语言简洁，特化在内部生成 |
| 释放规则与转移规则联动：直接传给 `e` callee 后原值失活；传复制品则原值照常释放 | 避免 double-free，与唯一性原则一致 |
| 文档中用"对象身份/backing allocation"避免与 SSA `Alias` 混淆 | `semantic_ssa.h` 已有 `SemanticSsaAlias`，含义不同 |
| 局部 poly 数组离开作用域时全部释放（从后往前） | 确保所有元素堆内存被回收，避免泄漏；从后往前防止 double-free |
| 返回值移动语义：局部数组 `arr[i]` 移出时用零多项式占位 | 避免对被移出项的不必要深复制，同时保证数组其余元素正常析构 |
| 全局/全局数组/函数数组参数传参时强制 `e` + 复制 | 这些值可能在调用后通过其他路径被读取，保守复制是最安全的策略 |
| 局部数组左值访问传参时统一模式（全 `r` 或全 `e`+复制） | 避免同一数组的同一元素被多次复制，保持行为一致 |
| 活跃分析简化：全 `e` 版本 + 可达 end 标记，不做迭代求解 | 大幅降低实现复杂度，编译速度快，保守性损失可接受 |
