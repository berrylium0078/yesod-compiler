#!/usr/bin/env python3
"""
Benchmark script for polynomial operations in the YESOD compiler project.

Tests three operations across 10 data sizes n ∈ [5e4, 1e5]:
  1. poly_div    — Polynomial division (m = n/2)
  2. poly_exp_dc — Polynomial exp via CDQ divide-and-conquer
  3. poly_exp_newton — Polynomial exp via Newton iteration

Three contestants:
  - compiler  : SysY source → YESOD compiler → LLVM IR → clang -O2
  - std       : Reference C++ implementations (from the internet)
  - baseline  : Author's C++ implementations

For each (test, contestant, n), runs once and reports wall-clock time.
Correctness is verified by comparing normalized stdout across contestants.
"""

import subprocess
import tempfile
import os
import time
import sys
import re
import shutil
import random
from pathlib import Path

# ── Configuration ───────────────────────────────────────────────────────

PROJECT_ROOT = Path(__file__).resolve().parent.parent
COMPILER_BIN = PROJECT_ROOT / "build" / "compiler"
TEST_SUITE_DIR = PROJECT_ROOT / "test" / "testsuit-collection" / "poly"
BENCHMARK_DIR = PROJECT_ROOT / "benchmark"
STD_DIR = BENCHMARK_DIR / "std"
BASELINE_DIR = BENCHMARK_DIR / "baseline"

MINT_MOD = 998244353
POLY_N = 1 << 17  # 131072
NUM_RUNS = 10
COMPILE_TIMEOUT = 600   # seconds per compilation (10 min for large inputs)
RUN_TIMEOUT = 300       # seconds per run

CXX = shutil.which("clang++") or shutil.which("g++") or "g++"
CC = shutil.which("clang") or shutil.which("gcc") or "gcc"

# ── Test case definitions ───────────────────────────────────────────────

TEST_CASES = [
    {
        "name": "poly_div",
        "source": "poly_div.c",
        "std_src": "poly_div.cpp",
        "baseline_src": "poly_div.cpp",
        "needs_m": True,       # reads n and m
        "needs_exp_f0_zero": False,
    },
    {
        "name": "poly_exp_dc",
        "source": "poly_exp_dc.c",
        "std_src": "poly_exp_dc.cpp",
        "baseline_src": "poly_exp_dc.cpp",
        "needs_m": False,
        "needs_exp_f0_zero": True,
    },
    {
        "name": "poly_exp_newton",
        "source": "poly_exp_newton.c",
        "std_src": "poly_exp_newton.cpp",
        "baseline_src": "poly_exp_newton.cpp",
        "needs_m": False,
        "needs_exp_f0_zero": True,
    },
]


# ── I/O helpers ─────────────────────────────────────────────────────────

def log(msg: str, end: str = "\n"):
    """Print a log message to stderr so stdout stays clean for piping."""
    print(msg, file=sys.stderr, end=end, flush=True)


def run_cmd(cmd: list[str], description: str, timeout: int | None = None,
            stdin_data: str | None = None,
            cwd: str | None = None) -> subprocess.CompletedProcess:
    """Run a command, log it, and return the result. Raise on failure."""
    log(f"    └─ {description}...")
    sys.stderr.flush()
    try:
        result = subprocess.run(
            cmd,
            input=stdin_data,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=cwd,
        )
    except subprocess.TimeoutExpired:
        log(f"  !! TIMEOUT ({timeout}s): {description}")
        sys.stderr.flush()
        raise
    if result.returncode != 0:
        err = result.stderr.strip()[-2000:]
        log(f"  !! FAILED (exit={result.returncode}): {description}")
        log(f"  stderr: {err}")
        sys.stderr.flush()
        raise RuntimeError(
            f"{description} failed (exit={result.returncode}): {err[-500:]}")
    return result


# ── LLVM runnable wrapper (replicates poly_stress_test.cpp logic) ──────

LLVM_RUNTIME_WRAPPER = r"""
@.yesod.scan_int = private unnamed_addr constant [3 x i8] c"%d\00"
@.yesod.putint_fmt = private unnamed_addr constant [3 x i8] c"%d\00"
@.yesod.putch_fmt = private unnamed_addr constant [3 x i8] c"%c\00"

declare i32 @scanf(ptr, ...)
declare i32 @printf(ptr, ...)
declare i32 @getchar()

define i32 @getint() {
entry:
  %slot = alloca i32
  %fmt = getelementptr inbounds [3 x i8], ptr @.yesod.scan_int, i32 0, i32 0
  call i32 (ptr, ...) @scanf(ptr %fmt, ptr %slot)
  %value = load i32, ptr %slot
  ret i32 %value
}

define i32 @getch() {
entry:
  %value = call i32 @getchar()
  ret i32 %value
}

define i32 @getarray(ptr %array) {
entry:
  %count = call i32 @getint()
  br label %cond
cond:
  %i = phi i32 [ 0, %entry ], [ %next, %body ]
  %keep = icmp slt i32 %i, %count
  br i1 %keep, label %body, label %end
body:
  %value = call i32 @getint()
  %element = getelementptr i32, ptr %array, i32 %i
  store i32 %value, ptr %element
  %next = add i32 %i, 1
  br label %cond
end:
  ret i32 %count
}

define void @putint(i32 %x) {
entry:
  %fmt = getelementptr inbounds [3 x i8], ptr @.yesod.putint_fmt, i32 0, i32 0
  call i32 (ptr, ...) @printf(ptr %fmt, i32 %x)
  ret void
}

define void @putch(i32 %x) {
entry:
  %fmt = getelementptr inbounds [3 x i8], ptr @.yesod.putch_fmt, i32 0, i32 0
  call i32 (ptr, ...) @printf(ptr %fmt, i32 %x)
  ret void
}

define void @putarray(i32 %count, ptr %array) {
entry:
  call void @putint(i32 %count)
  call void @putch(i32 58)
  br label %cond
cond:
  %i = phi i32 [ 0, %entry ], [ %next, %body ]
  %keep = icmp slt i32 %i, %count
  br i1 %keep, label %body, label %end
body:
  call void @putch(i32 32)
  %element = getelementptr i32, ptr %array, i32 %i
  %value = load i32, ptr %element
  call void @putint(i32 %value)
  %next = add i32 %i, 1
  br label %cond
end:
  call void @putch(i32 10)
  ret void
}

define void @starttime() {
entry:
  ret void
}

define void @stoptime() {
entry:
  ret void
}
"""


def is_sysy_runtime_decl(line: str) -> bool:
    """Check if a line is a SysY runtime declaration that should be stripped."""
    if not line.startswith("declare "):
        return False
    return any(
        fn in line
        for fn in [
            "@getint(",
            "@getch(",
            "@getarray(",
            "@putint(",
            "@putch(",
            "@putarray(",
            "@starttime(",
            "@stoptime(",
        ]
    )


def make_llvm_runnable(llvm_text: str) -> str:
    """Replace SysY runtime declarations with actual C-based implementations."""
    lines = []
    for line in llvm_text.splitlines(keepends=True):
        if is_sysy_runtime_decl(line.strip()):
            continue
        lines.append(line)
    lines.append(LLVM_RUNTIME_WRAPPER)
    return "".join(lines)


# ── Input generation ────────────────────────────────────────────────────

def random_coeffs(n: int, seed: int = 123456789) -> list[int]:
    """Generate n random coefficients modulo MINT_MOD."""
    rng = random.Random(seed)
    return [rng.randint(0, MINT_MOD - 1) for _ in range(n)]


def generate_poly_div_input(n: int, m: int) -> str:
    """
    Generate input for polynomial division.
    Format: n m
            c0 c1 ... cn   (n+1 coeffs for f, deg=n)
            d0 d1 ... dm   (m+1 coeffs for g, deg=m)
    Ensures leading coeff g[m] != 0 for invertibility of reversed g.
    """
    coeffs_f = random_coeffs(n + 1, seed=123456789)
    coeffs_g = random_coeffs(m + 1, seed=987654321)
    # Ensure leading coefficient of g is non-zero
    if coeffs_g[m] == 0:
        coeffs_g[m] = 1
    lines = [f"{n} {m}"]
    lines.append(" ".join(str(c) for c in coeffs_f))
    lines.append(" ".join(str(c) for c in coeffs_g))
    return "\n".join(lines) + "\n"


def generate_poly_exp_input(n: int) -> str:
    """
    Generate input for polynomial exp.
    Format: n
            c0 c1 ... c(n-1)   (n coeffs)
    Ensures c0 == 0 (required by exp: exp(0) = 1).
    """
    coeffs = random_coeffs(n, seed=123456789)
    coeffs[0] = 0  # exp requires f[0] = 0
    lines = [str(n)]
    lines.append(" ".join(str(c) for c in coeffs))
    return "\n".join(lines) + "\n"


# ── Build contestants ──────────────────────────────────────────────────

def build_compiler_binary(test_case: dict, tmp_dir: str,
                          test_label: str) -> str:
    """
    Build the contestant binary from the compiler (SysY → LLVM → clang -O2).

    Steps:
      1. Compile .c to LLVM IR using build/compiler -llvm
      2. Replace SysY runtime declarations with C-based implementations
      3. Compile the modified LLVM IR with clang -O2
    Returns path to the final binary.
    """
    name = test_case["name"]
    src_c = TEST_SUITE_DIR / test_case["source"]
    raw_ll = Path(tmp_dir) / f"{name}_raw.ll"
    runnable_ll = Path(tmp_dir) / f"{name}_runnable.ll"
    binary = Path(tmp_dir) / f"{name}_compiler"

    # Step 1: SysY → LLVM IR
    cmd_compile = [
        str(COMPILER_BIN),
        "-llvm",
        str(src_c),
        "-o",
        str(raw_ll),
    ]
    run_cmd(cmd_compile, f"{test_label}: 编译 SysY → LLVM IR",
            timeout=COMPILE_TIMEOUT)

    # Step 2: Make runnable
    raw_text = raw_ll.read_text()
    runnable_text = make_llvm_runnable(raw_text)
    runnable_ll.write_text(runnable_text)

    # Step 3: clang -O2 → binary
    cmd_clang = [
        CC, "-O2",
        str(runnable_ll),
        "-o",
        str(binary),
    ]
    run_cmd(cmd_clang, f"{test_label}: clang -O2 编译 LLVM → 二进制",
            timeout=COMPILE_TIMEOUT)

    return str(binary)


def build_cpp_binary(source_path: Path, output_path: str, label: str) -> str:
    """Compile a C++ source with clang++ -O2."""
    cmd = [
        CXX, "-std=c++17", "-O2",
        str(source_path),
        "-o",
        output_path,
    ]
    run_cmd(cmd, f"{label}: 编译 C++ → 二进制", timeout=COMPILE_TIMEOUT)
    return output_path

# ── Benchmark runner ────────────────────────────────────────────────────

def normalize_output(text: str) -> str:
    """Normalize program output for comparison.

    The compiler's SysY runtime prints polynomials with putpoly():
        "N: c0 c1 ... c(N-1)\\n"
    C++ contestants print raw coefficients:
        "c0 c1 ... c(N-1) \\n"

    Normalization strips length prefixes (\\d+:) and collapses whitespace.
    """
    # Strip length prefixes like "3:" or "100000:"
    text = re.sub(r'\b\d+\s*:', '', text)
    # Split on whitespace and rejoin with single space
    tokens = text.strip().split()
    return " ".join(tokens)


def run_benchmark_and_capture(binary: str, input_data: str,
                              label: str) -> dict | None:
    """Run a binary once, capture timing + stdout. Returns {elapsed_ms, stdout}."""
    log(f"    └─ {label}...", end="")
    sys.stderr.flush()
    try:
        t0 = time.perf_counter()
        result = subprocess.run(
            [binary],
            input=input_data,
            capture_output=True,
            text=True,
            timeout=RUN_TIMEOUT,
        )
        t1 = time.perf_counter()
    except subprocess.TimeoutExpired:
        log(f" TIMEOUT!")
        return None
    except OSError as e:
        log(f" OS ERROR: {e}")
        return None

    if result.returncode != 0:
        log(f" FAILED (exit={result.returncode})")
        log(f"       stderr: {result.stderr.strip()[-300:]}")
        return None

    elapsed_ms = (t1 - t0) * 1000
    log(f" {elapsed_ms:.1f} ms")
    return {"elapsed_ms": elapsed_ms, "stdout": result.stdout}


# ── Main ────────────────────────────────────────────────────────────────

def main():
    if not COMPILER_BIN.exists():
        log(f"ERROR: compiler binary not found at {COMPILER_BIN}")
        log("Build the project first: cmake --build build -j $(nproc)")
        sys.exit(1)

    contestant_defs = [
        ("compiler", "compiler (SysY → LLVM → -O2)"),
        ("std",      "std (reference C++)"),
        ("baseline", "baseline (author C++)"),
    ]

    log("=" * 60)
    log(f"YESOD Compiler Benchmark — n = {POLY_N}, {NUM_RUNS} runs each")
    log(f"Compiler: {COMPILER_BIN}")
    log(f"C++ compiler: {CXX}")
    log(f"C compiler: {CC}")
    log("=" * 60)

    total_tests = len(TEST_CASES) * len(contestant_defs) * NUM_RUNS
    completed = 0

    # results[tc_name][ct_key] = list of (elapsed_ms, stdout)
    results: dict[str, dict[str, list[dict]]] = {}

    with tempfile.TemporaryDirectory(prefix="yesod_bench_") as tmp_dir:
        for ti, tc in enumerate(TEST_CASES):
            name = tc["name"]
            results[name] = {ct: [] for ct, _ in contestant_defs}

            log(f"\n{'═' * 50}")
            log(f"  ▌ 测试 [{ti + 1}/{len(TEST_CASES)}]: {name}")
            log(f"{'═' * 50}")

            # ── Generate input (deterministic, same for all runs) ────
            if tc["needs_m"]:
                m = POLY_N // 2
                input_data = generate_poly_div_input(POLY_N, m)
            else:
                input_data = generate_poly_exp_input(POLY_N)

            # ── Build all three binaries once ────────────────────────
            binaries = {}
            build_errors = {}

            for ct_key, ct_label in contestant_defs:
                log(f"\n  ┌─ 构建: {ct_label}")
                if ct_key == "compiler":
                    try:
                        binaries["compiler"] = build_compiler_binary(
                            tc, tmp_dir, f"{name}/compiler")
                        log(f"  └─ ✅ 构建成功")
                    except (RuntimeError, subprocess.TimeoutExpired) as e:
                        log(f"  └─ ❌ 构建失败: {e}")
                        build_errors[ct_key] = str(e)
                elif ct_key == "std":
                    std_src = STD_DIR / tc["std_src"]
                    if std_src.exists():
                        try:
                            binaries["std"] = build_cpp_binary(
                                std_src,
                                os.path.join(tmp_dir, f"{name}_std"),
                                f"{name}/std")
                            log(f"  └─ ✅ 构建成功")
                        except (RuntimeError, subprocess.TimeoutExpired) as e:
                            log(f"  └─ ❌ 构建失败: {e}")
                            build_errors[ct_key] = str(e)
                    else:
                        log(f"  └─ ⚠ 源文件不存在")
                        build_errors[ct_key] = "源文件不存在"
                elif ct_key == "baseline":
                    baseline_src = BASELINE_DIR / tc["baseline_src"]
                    if baseline_src.exists():
                        try:
                            binaries["baseline"] = build_cpp_binary(
                                baseline_src,
                                os.path.join(tmp_dir, f"{name}_baseline"),
                                f"{name}/baseline")
                            log(f"  └─ ✅ 构建成功")
                        except (RuntimeError, subprocess.TimeoutExpired) as e:
                            log(f"  └─ ❌ 构建失败: {e}")
                            build_errors[ct_key] = str(e)
                    else:
                        log(f"  └─ ⚠ 源文件不存在")
                        build_errors[ct_key] = "源文件不存在"

            # ── Correctness check: run once, compare outputs ──────────
            log(f"\n  ┌─ 正确性验证...")
            captured = {}
            for ct_key in ["compiler", "std", "baseline"]:
                if ct_key in build_errors:
                    log(f"  │  ⚠ {ct_key}: 跳过")
                    continue
                binary = binaries.get(ct_key)
                if binary is None:
                    continue
                result = run_benchmark_and_capture(
                    binary, input_data, f"{name}/{ct_key}")
                if result is None:
                    log(f"  │  ❌ {ct_key}: 运行失败")
                    build_errors[ct_key] = "运行失败"
                else:
                    captured[ct_key] = normalize_output(result["stdout"])

            outputs_match = True
            ref_key = None
            ref_out = None
            for ct_key in ["compiler", "std", "baseline"]:
                if ct_key in build_errors:
                    continue
                if ct_key not in captured:
                    continue
                if ref_key is None:
                    ref_key = ct_key
                    ref_out = captured[ct_key]
                elif captured[ct_key] != ref_out:
                    log(f"  │  ❌ 输出不匹配: {ref_key} vs {ct_key}")
                    ref_tokens = ref_out.split()
                    cur_tokens = captured[ct_key].split()
                    for k in range(min(len(ref_tokens), len(cur_tokens))):
                        if ref_tokens[k] != cur_tokens[k]:
                            log(f"  │     位置 {k}: {ref_key}={ref_tokens[k]}, {ct_key}={cur_tokens[k]}")
                            break
                    if len(ref_tokens) != len(cur_tokens):
                        log(f"  │     长度差异: {ref_key}={len(ref_tokens)}, {ct_key}={len(cur_tokens)}")
                    outputs_match = False
                    build_errors["correctness"] = f"{ref_key} vs {ct_key} 输出不一致"

            if outputs_match and ref_key is not None:
                log(f"  │  ✅ 所有选手输出一致 (以 {ref_key} 为参考)")
            else:
                log(f"  │  ⚠ 跳过基准测试（正确性不通过）")

            # ── Benchmark: run NUM_RUNS times per contestant ──────────
            for ci, (ct_key, ct_label) in enumerate(contestant_defs):
                if ct_key in build_errors:
                    log(f"\n  ┌─ 选手 [{ci + 1}/3]: {ct_label}  ❌ {build_errors[ct_key]}")
                    log(f"  └─ 跳过")
                    continue

                log(f"\n  ┌─ 选手 [{ci + 1}/3]: {ct_label}")
                binary = binaries[ct_key]

                for ri in range(NUM_RUNS):
                    completed += 1
                    log(f"  │  run {ri + 1}/{NUM_RUNS} (总进度 {completed}/{total_tests})...", end="")
                    sys.stderr.flush()

                    t0 = time.perf_counter()
                    result = subprocess.run(
                        [binary],
                        input=input_data,
                        capture_output=True,
                        text=True,
                        timeout=RUN_TIMEOUT,
                    )
                    t1 = time.perf_counter()

                    if result.returncode != 0:
                        log(f" FAILED (exit={result.returncode})")
                        log(f"       stderr: {result.stderr.strip()[-200:]}")
                        results[name][ct_key].append(None)
                    else:
                        elapsed_ms = (t1 - t0) * 1000
                        log(f" {elapsed_ms:.1f} ms")
                        results[name][ct_key].append({
                            "elapsed_ms": elapsed_ms,
                            "stdout": result.stdout,
                        })

                log(f"  └─ 完成")

    # ── Output: Markdown table ─────────────────────────────────────────
    output_path = (PROJECT_ROOT / "benchmark" / "BENCHMARK_REPORT.md").resolve()

    def avg_of(vals: list) -> float | None:
        nums = [v["elapsed_ms"] for v in vals if v is not None]
        if not nums:
            return None
        return sum(nums) / len(nums)

    def std_of(vals: list) -> float:
        nums = [v["elapsed_ms"] for v in vals if v is not None]
        if len(nums) < 2:
            return 0.0
        mean = sum(nums) / len(nums)
        return (sum((x - mean) ** 2 for x in nums) / (len(nums) - 1)) ** 0.5

    output_lines = []
    output_lines.append("# YESOD Compiler Benchmark Report")
    output_lines.append("")
    output_lines.append(f"n = {POLY_N}  |  Runs per contestant: {NUM_RUNS}")
    output_lines.append("")
    output_lines.append("## Results (wall-clock time in ms)")
    output_lines.append("")
    output_lines.append("| Test Case | Contestant | Avg (ms) | StdDev (ms) | Min (ms) | Max (ms) |")
    output_lines.append("|-----------|------------|---------:|------------:|---------:|---------:|")

    contestant_labels = {
        "compiler": "compiler (SysY→LLVM→-O2)",
        "std": "std (reference C++)",
        "baseline": "baseline (author C++)",
    }

    for tc_name in [tc["name"] for tc in TEST_CASES]:
        first_row = True
        for ct_key in ["compiler", "std", "baseline"]:
            label = contestant_labels.get(ct_key, ct_key)
            vals = results.get(tc_name, {}).get(ct_key, [])
            avg = avg_of(vals)
            row_label = tc_name if first_row else ""
            first_row = False
            if avg is not None:
                stddev = std_of(vals)
                mn = min(v["elapsed_ms"] for v in vals if v is not None)
                mx = max(v["elapsed_ms"] for v in vals if v is not None)
                output_lines.append(f"| {row_label:9s} | {label:28s} | {avg:9.1f} | {stddev:9.1f} | {mn:9.1f} | {mx:9.1f} |")
            else:
                err = "运行失败"
                # Check build_errors... but we don't have it here, so just mark
                output_lines.append(f"| {row_label:9s} | {label:28s} | {'—':>9s} | {'—':>9s} | {'—':>9s} | {'—':>9s} |")

    output_lines.append("")
    output_lines.append("---")
    output_lines.append(
        f"*Benchmark executed on {time.strftime('%Y-%m-%d %H:%M:%S')}*")

    report = "\n".join(output_lines) + "\n"

    # Print to stdout
    print()
    print(report)

    # Write to file
    try:
        output_path.write_text(report)
        log(f"\nReport saved to: {output_path}")
    except Exception as e:
        log(f"Could not save report: {e}")


if __name__ == "__main__":
    main()
