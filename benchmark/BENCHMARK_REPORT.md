# YESOD Compiler Benchmark Report

n = 131072  |  Runs per contestant: 10

## Results (wall-clock time in ms)

| Test Case | Contestant | Avg (ms) | StdDev (ms) | Min (ms) | Max (ms) |
|-----------|------------|---------:|------------:|---------:|---------:|
| poly_div  | compiler (SysY→LLVM→-O2)     |     119.5 |       3.3 |     115.8 |     126.9 |
|           | std (reference C++)          |      55.7 |       4.5 |      53.3 |      68.3 |
|           | baseline (author C++)        |     208.3 |       1.1 |     206.5 |     209.7 |
| poly_exp_dc | compiler (SysY→LLVM→-O2)     |     170.2 |       1.4 |     168.1 |     172.0 |
|           | std (reference C++)          |     435.4 |      11.3 |     424.2 |     453.1 |
|           | baseline (author C++)        |     222.2 |       5.2 |     212.3 |     230.8 |
| poly_exp_newton | compiler (SysY→LLVM→-O2)     |     183.9 |       3.0 |     180.1 |     188.3 |
|           | std (reference C++)          |      76.0 |       0.8 |      74.9 |      77.0 |
|           | baseline (author C++)        |     229.1 |       1.5 |     227.0 |     231.7 |

---
*Benchmark executed on 2026-07-03 00:04:24*
