# [RISC-V 指令速查](#/misc-app-ref/riscv-insts?id=risc-v-%e6%8c%87%e4%bb%a4%e9%80%9f%e6%9f%a5)

本节只提供编译实践中你可能会用到的 RISC-V 指令的简略定义.
如果你需要详细了解 RISC-V ISA 的各类细节, 请参考 [RISC-V
规范](https://github.com/riscv/riscv-isa-manual/releases/download/Ratified-IMAFDQC/riscv-spec-20191213.pdf).

## [寄存器一览](#/misc-app-ref/riscv-insts?id=%e5%af%84%e5%ad%98%e5%99%a8%e4%b8%80%e8%a7%88)

| 寄存器   | ABI 名称  | 描述                | 保存者   |
|----------|-----------|---------------------|----------|
| `x0`     | `zero`    | 恒为 0              | N/A      |
| `x1`     | `ra`      | 返回地址            | 调用者   |
| `x2`     | `sp`      | 栈指针              | 被调用者 |
| `x3`     | `gp`      | 全局指针            | N/A      |
| `x4`     | `tp`      | 线程指针            | N/A      |
| `x5`     | `t0`      | 临时/备用链接寄存器 | 调用者   |
| `x6-7`   | `t1-2`    | 临时寄存器          | 调用者   |
| `x8`     | `s0`/`fp` | 保存寄存器/帧指针   | 被调用者 |
| `x9`     | `s1`      | 保存寄存器          | 被调用者 |
| `x10-11` | `a0-1`    | 函数参数/返回值     | 调用者   |
| `x12-17` | `a2-7`    | 函数参数            | 调用者   |
| `x18-27` | `s2-11`   | 保存寄存器          | 被调用者 |
| `x28-31` | `t3-6`    | 临时寄存器          | 调用者   |

## [指令记法](#/misc-app-ref/riscv-insts?id=%e6%8c%87%e4%bb%a4%e8%ae%b0%e6%b3%95)

在之后的指令描述中, 你可能会看到如下记法:

- `imm12`: 12-bit 有符号立即数, 范围为 $`[-2048, 2047]`$. 与 32-bit
  数据运算时, `imm12` 会被符号扩展到 32-bit.
- `imm`: 32-bit 有符号立即数.
- `rs`: 源寄存器.
- `rd`: 目的寄存器.
- `label`: 汇编中的标号, 例如 `main:`.

## [指令一览](#/misc-app-ref/riscv-insts?id=%e6%8c%87%e4%bb%a4%e4%b8%80%e8%a7%88)

### [控制转移类](#/misc-app-ref/riscv-insts?id=%e6%8e%a7%e5%88%b6%e8%bd%ac%e7%a7%bb%e7%b1%bb)

#### [beqz/bnez](#/misc-app-ref/riscv-insts?id=beqzbnez)

- **类别:** 伪指令.
- **汇编格式:** `beqz/bnez rs, label`.
- **行为:** 如果 `rs` 寄存器的值等于 (`beqz`) 或不等于 (`bnez`) 0,
  则转移到目标 `label`.

#### [j](#/misc-app-ref/riscv-insts?id=j)

- **类别:** 伪指令.
- **汇编格式:** `j label`.
- **行为:** 无条件转移到目标 `label`.

#### [call/ret](#/misc-app-ref/riscv-insts?id=callret)

- **类别:** 伪指令.
- **汇编格式:** `call label`, `ret`.
- **行为:** `call` 指令会将后一条指令的地址存入 `ra` 寄存器,
  并无条件转移到目标 `label`. `ret` 指令会无条件转移到 `ra`
  寄存器中保存的地址处.

### [访存类](#/misc-app-ref/riscv-insts?id=%e8%ae%bf%e5%ad%98%e7%b1%bb)

#### [lw](#/misc-app-ref/riscv-insts?id=lw)

- **类别:** 指令.
- **汇编格式:** `lw rs, imm12(rd)`.
- **行为:** 计算 `rd` 寄存器的值与 `imm12` 相加的结果作为访存地址,
  从内存中读取 32-bit 的数据, 存入 `rs` 寄存器.

#### [sw](#/misc-app-ref/riscv-insts?id=sw)

- **类别:** 指令.
- **汇编格式:** `sw rs2, imm12(rs1)`.
- **行为:** 计算 `rs1` 寄存器的值与 `imm12` 相加的结果作为访存地址, 将
  `rs2` 寄存器的值 (32-bit) 存入内存.

### [运算类](#/misc-app-ref/riscv-insts?id=%e8%bf%90%e7%ae%97%e7%b1%bb)

#### [add/addi](#/misc-app-ref/riscv-insts?id=addaddi)

- **类别:** 指令.
- **汇编格式:** `add rd, rs1, rs2`, `addi rd, rs1, imm12`.
- **行为:** 计算 `rs1` 寄存器和 `rs2` 寄存器 (`add`) 或 `imm12` (`addi`)
  相加的值, 存入 `rd` 寄存器.

#### [sub](#/misc-app-ref/riscv-insts?id=sub)

- **类别:** 指令.
- **汇编格式:** `sub rd, rs1, rs2`.
- **行为:** 计算 `rs1` 寄存器和 `rs2` 寄存器相减的值, 存入 `rd` 寄存器.

#### [slt/sgt](#/misc-app-ref/riscv-insts?id=sltsgt)

- **类别:** `slt` 为指令, `sgt` 为伪指令.
- **汇编格式:** `slt/sgt rd, rs1, rs2`.
- **行为:** 判断 `rs1` 寄存器是否小于 (`slt`) 或大于 (`sgt`) `rs2`
  寄存器, 如果判断条件成立, 则将 1 写入 `rd` 寄存器, 否则写入 0.

#### [seqz/snez](#/misc-app-ref/riscv-insts?id=seqzsnez)

- **类别:** 伪指令.
- **汇编格式:** `seqz/snez rd, rs`.
- **行为:** 判断 `rs` 寄存器是否等于 (`seqz`) 或不等于 (`snez`) 0,
  如果判断条件成立, 则将 1 写入 `rd` 寄存器, 否则写入 0.

#### [xor/xori](#/misc-app-ref/riscv-insts?id=xorxori)

- **类别:** 指令.
- **汇编格式:** `xor rd, rs1, rs2`, `xori rd, rs1, imm12`.
- **行为:** 计算 `rs1` 寄存器和 `rs2` 寄存器 (`xor`) 或 `imm12` (`xori`)
  按位异或的值, 存入 `rd` 寄存器.

#### [or/ori](#/misc-app-ref/riscv-insts?id=orori)

- **类别:** 指令.
- **汇编格式:** `or rd, rs1, rs2`, `ori rd, rs1, imm12`.
- **行为:** 计算 `rs1` 寄存器和 `rs2` 寄存器 (`or`) 或 `imm12` (`ori`)
  按位或的值, 存入 `rd` 寄存器.

#### [and/andi](#/misc-app-ref/riscv-insts?id=andandi)

- **类别:** 指令.
- **汇编格式:** `and rd, rs1, rs2`, `andi rd, rs1, imm12`.
- **行为:** 计算 `rs1` 寄存器和 `rs2` 寄存器 (`and`) 或 `imm12` (`andi`)
  按位与的值, 存入 `rd` 寄存器.

#### [sll/srl/sra](#/misc-app-ref/riscv-insts?id=sllsrlsra)

- **类别:** 指令.
- **汇编格式:** `sll/srl/sra rd, rs1, rs2`.
- **行为:** 对寄存器 `rs1` 进行逻辑左移 (`sll`), 逻辑右移 (`srl`)
  或算术右移 (`sra`) 运算, 移位的位数为 `rs2` 寄存器的值, 结果存入 `rd`
  寄存器.

#### [mul/div/rem](#/misc-app-ref/riscv-insts?id=muldivrem)

- **类别:** 指令.
- **汇编格式:** `mul/div/rem rd, rs1, rs2`.
- **行为:** 计算寄存器 `rs1` 和寄存器 `rs2` 相乘 (`mul`), 除以 (`div`)
  或取余 (`rem`) 的值, 存入 `rd` 寄存器.

### [加载和移动类](#/misc-app-ref/riscv-insts?id=%e5%8a%a0%e8%bd%bd%e5%92%8c%e7%a7%bb%e5%8a%a8%e7%b1%bb)

#### [li](#/misc-app-ref/riscv-insts?id=li)

- **类别:** 伪指令.
- **汇编格式:** `li rd, imm`.
- **行为:** 将立即数 `imm` 加载到寄存器 `rd` 中.

#### [la](#/misc-app-ref/riscv-insts?id=la)

- **类别:** 伪指令.
- **汇编格式:** `la rd, label`.
- **行为:** 将标号 `label` 的绝对地址加载到寄存器 `rd` 中.

#### [mv](#/misc-app-ref/riscv-insts?id=mv)

- **类别:** 伪指令.
- **汇编格式:** `mv rd, rs`.
- **行为:** 将寄存器 `rs` 的值复制到寄存器 `rd`.

文档 v2.0.0 由 [MaxXing](https://github.com/MaxXSoft) 撰写, 采用 [CC BY-NC-SA 4.0 协议](http://creativecommons.org/licenses/by-nc-sa/4.0/)发布.
