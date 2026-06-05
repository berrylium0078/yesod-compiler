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

- [x] 全部已有测试（12 parser + 9 semantic/koopa）通过

---

## 待实现 🔜

### Phase 2 — SSA 基础设施推广

- [ ] **SSA 推广到 poly 值**：`semantic_ssa.h/cpp` 当前 `m_localScalarSymbols` 只追踪局部标量 symbol id，需要包含 `poly` 类型的局部变量、参数和块参数
- [ ] **poly 相关的事实附着**：每个 SSA alias 附加长度表达式、非零区间、活跃区间、对象来源、是否可转移等信息
- [ ] **预 SSA CFG 化简 pass**：常量传播发现分支跳转固定时，合并基本块与其唯一后继

**涉及文件：**
- `src/frontend/semantic_ssa.h`
- `src/frontend/semantic_ssa.cpp`
- `src/frontend/semantic_cfg.cpp`

### Phase 3 — 所有权分析与函数特化

- [ ] **函数 poly 参数位置元数据**：语义符号允许记录哪些形参是 `poly` 类型（独立于 `r`/`e` 选择）
- [ ] **特化生成**：按 `__yesod_gen_{name}_{mask}` 命名，`mask ∈ {r,e}^n` 覆盖 n 个 poly 参数的所有排列
- [ ] **调用点适配**：
  - 最后一次使用的局部唯一对象 → `e` 模式（转移所有权，无需复制）
  - 调用后仍活跃 → `e` 模式 + 复制
  - 借用参数 / 无法证明唯一 → 复制到 `e`，或降级为 `r`
  - 同调用多个 `e` 实参必须互异
- [ ] **只生成被使用的特化版本**（惰性生成，不重复）

**涉及文件：**
- `src/frontend/semantic_type.cpp`（增加特化元数据）
- `src/koopa/ast_to_koopa.cpp`（生成特化函数声明/定义/调用）

### Phase 4 — Poly 分析 Pass（基于 SSA）

- [ ] **多项式长度代码生成**：正向数据流分析，为每个 poly 值推导长度表达式
  - 引入辅助 IR 运算：三元运算符（条件选择）、`min`/`max`
  - 全局/数组/函数返回值从对应字段读取
  - 所有新产生的表达式立即常量折叠
- [ ] **非零区间分析**：正向数据流，推导 poly 值的非零系数范围 `[l, r)`
  - 加减法：并集；乘法：区间卷积；移位/切片：平移/截断
  - 随基本块参数传递给后继
- [ ] **活跃区间分析**：逆向数据流，推导后续运算中实际使用的系数范围（`range` 的子集）
  - 引入 `槑`（未知端点）格
- [ ] **NTT 算子插入**：表达式选择
  - 乘法前插入 `ntt_forward`，乘法后插入 `ntt_backward`
  - 后续优化：线性性质重写、公共子表达式消除
- [ ] **乘法算子融合与代码生成**：计算最小 2-幂 NTT 长度
- [ ] **加法算子融合**：化简为 `∑ (aᵢ[lᵢ,rᵢ] >> kᵢ) · cᵢ` 的标准形式，扫描输出

**涉及文件：**
- 新增 `src/frontend/semantic_poly_length.h/.cpp`
- 新增 `src/frontend/semantic_poly_range.h/.cpp`
- 新增 `src/frontend/semantic_poly_liveness.h/.cpp`
- 新增 `src/frontend/semantic_poly_ntt.h/.cpp`

### Phase 5 — Lowering、ABI、运行时

- [ ] **Koopa IR 增加 `PolyType`**：`src/koopa/ir.h` 加入 poly 类型，函数参数/块参数/返回值可携带
- [ ] **Koopa IR 特化函数生成**：将所有权分析和 poly pass 后的结果生成特化函数
- [ ] **RISC-V 后端 ABI**：
  - `ValueShape` 支持 pair 模式：poly = `{ptr: i32*, len: i32}`
  - `FunctionSignature` 支持多值返回 (a0 + a1)
  - `emitCall()` 传参时 poly 占两寄存器 / 堆栈槽位
  - `emitReturn()` 返回时设 a0=ptr, a1=len
  - 块参数跨边传递展开为分别传 ptr 和 len
- [ ] **Poly 运行时**：
  - `src/main.cpp` 注入 buddy allocator（PAGE_SIZE ≈ 4KiB，最小 8B，伙伴算法）
  - 函数：`__yesod_poly_alloc`、`__yesod_poly_free`、`__yesod_poly_realloc`
  - `programUsesPolyRuntime()` 检测 + 注入
- [ ] **析构调度**：
  - 语句内最后使用后立刻插入 `free`
  - 基本块间：值 live-out 但未作为块参数传给后继 → 边分裂 + 清理块
  - `r` 模式值永不释放；转移到 `e` callee 后原始值视为死亡

**涉及文件：**
- `src/koopa/ir.h` / `ir.cpp`
- `src/koopa/ast_to_koopa.cpp`
- `src/backend/riscv.h` / `riscv.cpp`
- `src/main.cpp`

### Phase 6 — 纵深测试

- [ ] **语义 poly 测试**：`poly(int/mint)` 转换、`!poly` 类型、越界/错误诊断
- [ ] **SSA / 所有权测试**：验证特化 mask 选择、最后使用转移、live-after-call 复制、互异复制
- [ ] **Koopa IR 测试**：`PolyType` 签名、块参数携带 poly 值、运行时辅助函数引用
- [ ] **后端测试**：a0/a1 寄存器配对、超 8 参溢出、块参数跨边传递、last-use free
- [ ] **端到端测试**：`test/llvm/poly_*.sy` → LLVM → RISC-V → QEMU，含 `ntt` 参考实现验证
- [ ] **文档同步**：`doc/poly.md` 随实现进展更新，区分强制性语义与可选优化

**涉及文件：**
- `test/semantic/semantic_poly_test.cpp`（新建）
- `test/koopa/koopa_poly_test.cpp`（新建）
- `test/backend_riscv_test.cpp`
- `test/compiler_llvm_test.cpp`
- `test/llvm/poly_*.sy`（新建）
- `test/CMakeLists.txt`

---

## 关键设计决定

| 决定 | 理由 |
|------|------|
| Koopa 层保持 `PolyType` 而非降低到 hidden sret | 与双寄存器 ABI 一致，便于表达块参数和返回值 |
| `e` 模式的唯一性落在调用点适配，而非靠被调用者兜底 | 编译器可见时做复制最安全，运行时检查成本高 |
| 所有权是"函数特化后的参数位置属性"，非源语言类型修饰 | 保持源语言简洁，特化在内部生成 |
| 释放规则与转移规则联动：直接传给 `e` callee 后原值失活；传复制品则原值照常释放 | 避免 double-free，与唯一性原则一致 |
| 文档中用"对象身份/backing allocation"避免与 SSA `Alias` 混淆 | `semantic_ssa.h` 已有 `SemanticSsaAlias`，含义不同 |
