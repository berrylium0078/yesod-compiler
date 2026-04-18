---
description: This file describes how to test the compiler constructed in this project.
---

## Testing the Compiler

The compiler constructed in this project supports the following command-line format:

```sh
compiler [mode] [input-file] -o [output-file]
```

Where `mode` is one of the following:

- `-koopa`: output Koopa IR
- `-llvm`: output LLVM IR
- `-riscv`: output RISC-V assembly

Examples:

```sh
build/compiler -koopa tests/hello.c -o tests/hello.koopa
build/compiler -riscv tests/foo.c -o tests/foo.S
```

Note: The input and output files should be placed in the `tests/` directory.

## Running the Output

If you have saved a Koopa IR program in `hello.koopa`, you can run it in the lab environment with:

```sh
koopac hello.koopa | llc --filetype=obj -o hello.o
clang hello.o -L$CDE_LIBRARY_PATH/native -lsysy -o hello
./hello
```

If you have saved a RISC-V assembly program in `hello.S`, you can assemble and link it into an executable, then run it with:

```sh
clang hello.S -c -o hello.o -target riscv32-unknown-linux-elf -march=rv32im -mabi=ilp32
ld.lld hello.o -L$CDE_LIBRARY_PATH/riscv32 -lsysy -o hello
qemu-riscv32-static hello
```