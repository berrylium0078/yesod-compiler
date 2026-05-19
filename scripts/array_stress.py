#!/usr/bin/env python3

import argparse
import math
import os
import random
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


TIMEOUT_SECONDS = 10
MAX_TESTS = 1000
MAX_TOTAL_ELEMENTS = 1024


@dataclass
class CommandResult:
    argv: list[str]
    returncode: int
    stdout: str
    stderr: str
    timed_out: bool = False


@dataclass
class RunResult:
    accepted: bool
    stdout: str
    stderr: str
    phase: str
    command: Optional[CommandResult]


class InitializerGenerator:
    def __init__(self, rng: random.Random, rank: int, total_elements: int) -> None:
        self.rng = rng
        self.rank = rank
        self.total_elements = total_elements

    def generate(self, gentle_mode: bool) -> str:
        scalar_target = self._choose_scalar_target(gentle_mode)
        parts = ["{"]
        depth = 1
        scalar_count = 0
        need_value = True

        while depth > 0:
            if need_value:
                choices = ["number"]
                if depth < self.rank and self.rng.random() < (0.35 if gentle_mode else 0.5):
                    choices.append("lbrace")
                if self.rng.random() < 0.1:
                    choices.append("rbrace")

                if scalar_count >= scalar_target and "number" in choices and len(choices) > 1:
                    choices.remove("number")
                choice = self.rng.choice(choices)

                if choice == "number":
                    parts.append(str(self.rng.randrange(64)))
                    scalar_count += 1
                    need_value = False
                elif choice == "lbrace":
                    parts.append("{")
                    depth += 1
                    need_value = True
                else:
                    parts.append("}")
                    depth -= 1
                    need_value = False
            else:
                if depth == 0:
                    break
                must_close = scalar_count >= scalar_target and depth == 1
                if must_close or self.rng.random() < 0.3:
                    parts.append("}")
                    depth -= 1
                else:
                    parts.append(",")
                    need_value = True

        # Remove any commas that appear immediately before a closing brace
        s = " ".join(parts)
        # replace repeatedly in case of nested occurrences
        while ", }" in s:
            s = s.replace(", }", " }")
        return s

    def _choose_scalar_target(self, gentle_mode: bool) -> int:
        if gentle_mode:
            return self.rng.randint(1, max(1, min(self.total_elements, 32)))
        upper = max(1, min(self.total_elements * 2, 64))
        return self.rng.randint(1, upper)


def run_command(argv: list[str], timeout: int) -> CommandResult:
    try:
        completed = subprocess.run(
            argv,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return CommandResult(
            argv=argv,
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=completed.stderr,
            timed_out=False,
        )
    except subprocess.TimeoutExpired as exc:
        return CommandResult(
            argv=argv,
            returncode=124,
            stdout=exc.stdout or "",
            stderr=exc.stderr or "",
            timed_out=True,
        )


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise SystemExit(f"missing required tool: {name}")
    return path


def choose_shape(rng: random.Random) -> list[int]:
    while True:
        rank = rng.randint(1, 5)
        dims: list[int] = []
        product = 1
        for _ in range(rank):
            limit = min(8, MAX_TOTAL_ELEMENTS // product)
            if limit <= 0:
                break
            dim = rng.randint(1, max(1, limit))
            dims.append(dim)
            product *= dim
        if dims and product <= MAX_TOTAL_ELEMENTS:
            return dims


def render_source(shape: list[int], initializer: str) -> str:
    lines: list[str] = []
    for index, dim in enumerate(shape, start=1):
        lines.append(f"const int N{index} = {dim};")

    suffix = "".join(f"[N{index}]" for index in range(1, len(shape) + 1))
    lines.append(f"int a{suffix} = {initializer};")
    lines.append("")
    lines.append("int main() {")

    indent = "    "
    index_names = [f"i{index}" for index in range(1, len(shape) + 1)]
    for depth, name in enumerate(index_names, start=1):
        lines.append(f"{indent * depth}int {name} = 0;")
        lines.append(f"{indent * depth}while ({name} < N{depth}) {{")

    access = "".join(f"[{name}]" for name in index_names)
    body_indent = indent * (len(shape) + 1)
    lines.append(f"{body_indent}putch(a{access} + 48);")

    for depth, name in reversed(list(enumerate(index_names, start=1))):
        lines.append(f"{indent * (depth + 1)}{name} = {name} + 1;")
        lines.append(f"{indent * depth}}}")

    lines.append(f"{indent}return 0;")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def render_reference_source(sysy_source: str) -> str:
    return (
        "#include <stdio.h>\n"
        "#define putch putchar\n\n"
        f"{sysy_source}"
    )


def compile_and_run_reference(
    source_path: Path, workdir: Path, timeout: int
) -> RunResult:
    exe_path = workdir / "reference.out"
    clangxx = require_tool("clang++")
    compile_result = run_command(
        [clangxx, "-x", "c", "-std=c99", str(source_path), "-O0", "-o", str(exe_path)],
        timeout,
    )
    if compile_result.returncode != 0:
        return RunResult(False, compile_result.stdout, compile_result.stderr, "reference compile", compile_result)

    run_result = run_command([str(exe_path)], timeout)
    if run_result.returncode != 0:
        return RunResult(False, run_result.stdout, run_result.stderr, "reference run", run_result)
    return RunResult(True, run_result.stdout, run_result.stderr, "reference run", run_result)


def compile_and_run_yesod(
    compiler: Path, source_path: Path, workdir: Path, timeout: int
) -> RunResult:
    asm_path = workdir / "program.S"
    obj_path = workdir / "program.o"
    exe_path = workdir / "program.riscv"

    compile_result = run_command(
        [str(compiler), "-riscv", str(source_path), "-o", str(asm_path)], timeout
    )
    if compile_result.returncode != 0:
        return RunResult(False, compile_result.stdout, compile_result.stderr, "yesod compile", compile_result)

    clang = require_tool("clang")
    assemble_result = run_command(
        [
            clang,
            str(asm_path),
            "-c",
            "-o",
            str(obj_path),
            "-target",
            "riscv32-unknown-linux-elf",
            "-march=rv32im",
            "-mabi=ilp32",
        ],
        timeout,
    )
    if assemble_result.returncode != 0:
        return RunResult(False, assemble_result.stdout, assemble_result.stderr, "assemble", assemble_result)

    ld_lld = require_tool("ld.lld")
    cde_library_path = os.environ.get("CDE_LIBRARY_PATH")
    if not cde_library_path:
        raise SystemExit("CDE_LIBRARY_PATH is not set")
    link_result = run_command(
        [
            ld_lld,
            str(obj_path),
            f"-L{Path(cde_library_path) / 'riscv32'}",
            "-lsysy",
            "-o",
            str(exe_path),
        ],
        timeout,
    )
    if link_result.returncode != 0:
        return RunResult(False, link_result.stdout, link_result.stderr, "link", link_result)

    qemu = require_tool("qemu-riscv32-static")
    run_result = run_command([qemu, str(exe_path)], timeout)
    if run_result.returncode != 0:
        return RunResult(False, run_result.stdout, run_result.stderr, "yesod run", run_result)
    return RunResult(True, run_result.stdout, run_result.stderr, "yesod run", run_result)


def report_failure(
    test_index: int,
    seed: int,
    source_code: str,
    reference_result: RunResult,
    yesod_result: RunResult,
) -> int:
    print(f"FAIL on test {test_index} with seed {seed}")
    print("==== Generated Source ====")
    print(source_code)
    print("==== Reference Result ====")
    print(reference_result.phase)
    if reference_result.command is not None:
        print("command:", " ".join(reference_result.command.argv))
        print("returncode:", reference_result.command.returncode)
        if reference_result.command.stdout:
            print("stdout:")
            print(reference_result.command.stdout)
        if reference_result.command.stderr:
            print("stderr:")
            print(reference_result.command.stderr)
    print("==== Yesod Result ====")
    print(yesod_result.phase)
    if yesod_result.command is not None:
        print("command:", " ".join(yesod_result.command.argv))
        print("returncode:", yesod_result.command.returncode)
        if yesod_result.command.stdout:
            print("stdout:")
            print(yesod_result.command.stdout)
        if yesod_result.command.stderr:
            print("stderr:")
            print(yesod_result.command.stderr)
    return 1


def run_one_test(
    compiler: Path,
    rng: random.Random,
    seed: int,
    test_index: int,
    timeout: int,
    gentle_mode: bool,
) -> tuple[bool, bool]:
    shape = choose_shape(rng)
    initializer = InitializerGenerator(rng, len(shape), math.prod(shape)).generate(
        gentle_mode
    )
    sysy_source = render_source(shape, initializer)
    reference_source = render_reference_source(sysy_source)

    with tempfile.TemporaryDirectory(prefix="yesod-array-stress-", dir="/tmp") as temp_dir:
        workdir = Path(temp_dir)
        sysy_path = workdir / "program.sy"
        ref_path = workdir / "program_ref.cpp"
        sysy_path.write_text(sysy_source)
        ref_path.write_text(reference_source)

        reference_result = compile_and_run_reference(ref_path, workdir, timeout)
        yesod_result = compile_and_run_yesod(compiler, sysy_path, workdir, timeout)

        same_acceptance = reference_result.accepted == yesod_result.accepted
        if not same_acceptance:
            report_failure(
                test_index, seed, sysy_source, reference_result, yesod_result
            )
            return False, False

        if reference_result.accepted and reference_result.stdout != yesod_result.stdout:
            report_failure(
                test_index, seed, sysy_source, reference_result, yesod_result
            )
            return False, False

        return True, reference_result.accepted or yesod_result.accepted


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Stress-test array initializer generation")
    parser.add_argument("--compiler", default="/root/compiler/build/compiler")
    parser.add_argument("--tests", type=int, default=MAX_TESTS)
    parser.add_argument("--timeout", type=int, default=TIMEOUT_SECONDS)
    parser.add_argument("--seed", type=int, default=random.randrange(1 << 30))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    compiler = Path(args.compiler)
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    consecutive_rejections = 0
    gentle_mode = False

    print(f"seed={args.seed}")
    for test_index in range(1, args.tests + 1):
        passed, accepted = run_one_test(
            compiler, rng, args.seed, test_index, args.timeout, gentle_mode
        )
        if not passed:
            return 1

        if accepted:
            consecutive_rejections = 0
            gentle_mode = False
        else:
            consecutive_rejections += 1
            if consecutive_rejections >= 20:
                gentle_mode = True

        if test_index % 50 == 0:
            print(f"passed {test_index} tests")

    print(f"passed {args.tests} tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())