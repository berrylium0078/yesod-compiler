# YESOD Compiler Benchmark Report

n = 131072  |  Runs per contestant: 10

## Results (wall-clock time in ms)

| Test Case | Contestant | Avg (ms) | StdDev (ms) | Min (ms) | Max (ms) |
|-----------|------------|---------:|------------:|---------:|---------:|
| poly_div  | compiler (SysY→LLVM→-O2)     |      83.3 |       5.5 |      78.1 |      96.8 |
|           | std (reference C++)          |      57.2 |       1.5 |      56.0 |      60.9 |
|           | baseline (author C++)        |      88.2 |       1.3 |      87.1 |      91.3 |
| poly_exp_dc | compiler (SysY→LLVM→-O2)     |     133.6 |       2.6 |     130.2 |     139.4 |
|           | std (reference C++)          |     449.5 |       7.9 |     440.2 |     463.6 |
|           | baseline (author C++)        |      84.7 |       1.5 |      83.2 |      88.1 |
| poly_exp_newton | compiler (SysY→LLVM→-O2)     |     110.9 |       6.4 |     106.9 |     128.5 |
|           | std (reference C++)          |      80.9 |       1.9 |      78.8 |      84.4 |
|           | baseline (author C++)        |     109.6 |       2.0 |     107.2 |     113.1 |

---
*Benchmark executed on 2026-07-03 12:45:31*
