#!/usr/bin/env python3

import argparse
import random
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


TIMEOUT_SECONDS = 10
MAX_TESTS = 1000
LOGICAL_OPERATORS = ("||", "&&")
UNARY_OPERATORS = ("+", "-", "!")
BASE_NAMES = (
    "value",
    "state",
    "flag",
    "count",
    "index",
    "limit",
    "buffer",
    "cursor",
    "node",
    "shadow",
    "path",
    "depth",
    "sum",
    "step",
    "gate",
    "mask",
)


@dataclass
class CommandResult:
    argv: list[str]
    returncode: int
    stdout: str
    stderr: str
    timed_out: bool = False


@dataclass(frozen=True)
class SysYType:
    dimensions: tuple[int, ...] = ()

    def is_scalar(self) -> bool:
        return not self.dimensions

    def render_suffix(self) -> str:
        return "".join(f"[{dimension}]" for dimension in self.dimensions)

    def render_indices(self, rng: random.Random) -> str:
        return "".join(f"[{rng.randrange(dimension)}]" for dimension in self.dimensions)


@dataclass(frozen=True)
class FunctionSignature:
    name: str
    parameter_names: tuple[str, ...]
    parameter_types: tuple[SysYType, ...]


@dataclass
class ObjectSymbol:
    symbol_id: int
    name: str
    type: SysYType
    is_const: bool
    scope_depth: int


@dataclass
class GeneratorConfig:
    max_functions: int = 5
    max_params: int = 4
    max_globals: int = 5
    max_block_depth: int = 4
    max_block_items: int = 6
    max_array_rank: int = 3
    max_array_dim: int = 5


@dataclass
class ProgramGenerator:
    rng: random.Random
    config: GeneratorConfig
    next_symbol_id: int = 0
    next_fresh_name_id: int = 0
    global_symbols: dict[str, ObjectSymbol] = field(default_factory=dict)
    function_signatures: list[FunctionSignature] = field(default_factory=list)
    scopes: list[dict[str, ObjectSymbol]] = field(default_factory=list)

    def generate_program(self) -> str:
        self.scopes = [{}]
        lines: list[str] = []
        lines.extend(self.generate_global_declarations())

        helper_count = self.rng.randint(1, self.config.max_functions)
        for function_index in range(helper_count):
            signature = self.make_function_signature(f"helper_{function_index}")
            self.function_signatures.append(signature)
            lines.extend(self.generate_function(signature, include_calls=function_index > 0))
            lines.append("")

        main_signature = FunctionSignature("main", (), ())
        lines.extend(self.generate_function(main_signature, include_calls=True))
        lines.append("")
        return "\n".join(lines)

    def generate_global_declarations(self) -> list[str]:
        global_count = self.rng.randint(1, self.config.max_globals)
        lines: list[str] = []
        anchor_name = self.make_fresh_name(set())
        anchor_symbol = self.define_symbol(anchor_name, self.make_array_type(), is_const=False)
        self.global_symbols[anchor_symbol.name] = anchor_symbol
        lines.append(f"int {anchor_symbol.name}{anchor_symbol.type.render_suffix()};")

        for _ in range(global_count - 1):
            declaration, symbol = self.make_object_declaration(scope_depth=0, prefer_shadowing=False)
            self.global_symbols[symbol.name] = symbol
            lines.append(declaration)
        if lines:
            lines.append("")
        return lines

    def generate_function(
        self, signature: FunctionSignature, include_calls: bool
    ) -> list[str]:
        lines = [
            f"int {signature.name}({self.render_parameters(signature)}) {{",
        ]
        self.scopes = [dict(self.global_symbols)]
        self.push_scope()

        for parameter_name, parameter_type in zip(
            signature.parameter_names, signature.parameter_types
        ):
            self.define_symbol(parameter_name, parameter_type, is_const=False)

        item_count = self.rng.randint(2, self.config.max_block_items)
        lines.extend(
            self.generate_block_items(
                indent_level=1,
                block_depth=0,
                remaining_items=item_count,
                include_calls=include_calls,
            )
        )
        lines.append(
            f"    return {self.generate_int_expression(depth=3, include_calls=include_calls)};"
        )
        self.pop_scope()
        lines.append("}")
        return lines

    def render_parameters(self, signature: FunctionSignature) -> str:
        parts: list[str] = []
        for parameter_name, parameter_type in zip(
            signature.parameter_names, signature.parameter_types
        ):
            parts.append(f"int {parameter_name}{parameter_type.render_suffix()}")
        return ", ".join(parts)

    def make_function_signature(self, name: str) -> FunctionSignature:
        parameter_count = self.rng.randint(0, self.config.max_params)
        parameter_names: list[str] = []
        parameter_types: list[SysYType] = []
        current_scope_names: set[str] = set()
        for _ in range(parameter_count):
            parameter_name = self.make_fresh_name(current_scope_names)
            current_scope_names.add(parameter_name)
            parameter_names.append(parameter_name)
            parameter_types.append(SysYType(()))
        return FunctionSignature(name, tuple(parameter_names), tuple(parameter_types))

    def generate_block_items(
        self,
        indent_level: int,
        block_depth: int,
        remaining_items: int,
        include_calls: bool,
    ) -> list[str]:
        lines: list[str] = []
        for _ in range(remaining_items):
            lines.extend(
                self.generate_statement_or_decl(
                    indent_level=indent_level,
                    block_depth=block_depth,
                    include_calls=include_calls,
                )
            )
        return lines

    def generate_statement_or_decl(
        self, indent_level: int, block_depth: int, include_calls: bool
    ) -> list[str]:
        indent = "    " * indent_level
        choices = ["decl", "assign", "if", "while", "block"]
        if not self.visible_assignable_symbols():
            choices = ["decl", "if", "while", "block"]
        if block_depth >= self.config.max_block_depth:
            choices = ["decl", "assign"]
        choice = self.rng.choice(choices)

        if choice == "decl":
            declaration, _ = self.make_object_declaration(
                scope_depth=len(self.scopes) - 1,
                prefer_shadowing=block_depth > 0,
            )
            return [f"{indent}{declaration}"]

        if choice == "assign":
            lvalue = self.generate_lvalue()
            expression = self.generate_int_expression(depth=3, include_calls=include_calls)
            return [f"{indent}{lvalue} = {expression};"]

        if choice == "if":
            lines = [
                f"{indent}if ({self.generate_condition_expression(include_calls=include_calls)}) {{"
            ]
            self.push_scope()
            then_items = self.rng.randint(1, self.config.max_block_items)
            lines.extend(
                self.generate_block_items(
                    indent_level + 1,
                    block_depth + 1,
                    then_items,
                    include_calls,
                )
            )
            self.pop_scope()
            lines.append(f"{indent}}}")

            if self.rng.random() < 0.7:
                lines.append(f"{indent}else {{")
                self.push_scope()
                else_items = self.rng.randint(1, self.config.max_block_items)
                lines.extend(
                    self.generate_block_items(
                        indent_level + 1,
                        block_depth + 1,
                        else_items,
                        include_calls,
                    )
                )
                self.pop_scope()
                lines.append(f"{indent}}}")
            return lines

        if choice == "while":
            lines = [
                f"{indent}while ({self.generate_condition_expression(include_calls=include_calls)}) {{"
            ]
            self.push_scope()
            body_items = self.rng.randint(1, self.config.max_block_items)
            lines.extend(
                self.generate_block_items(
                    indent_level + 1,
                    block_depth + 1,
                    body_items,
                    include_calls,
                )
            )
            self.pop_scope()
            lines.append(f"{indent}}}")
            return lines

        lines = [f"{indent}{{"]
        self.push_scope()
        nested_items = self.rng.randint(1, self.config.max_block_items)
        lines.extend(
            self.generate_block_items(
                indent_level + 1,
                block_depth + 1,
                nested_items,
                include_calls,
            )
        )
        self.pop_scope()
        lines.append(f"{indent}}}")
        return lines

    def make_object_declaration(
        self, scope_depth: int, prefer_shadowing: bool
    ) -> tuple[str, ObjectSymbol]:
        current_scope = self.scopes[-1] if self.scopes else {}
        name = self.choose_declaration_name(current_scope, prefer_shadowing)
        is_global_scope = scope_depth == 0

        if self.rng.random() < 0.35:
            symbol = self.define_symbol(name, self.make_array_type(), is_const=False)
            return f"int {symbol.name}{symbol.type.render_suffix()};", symbol

        if self.rng.random() < 0.75:
            initializer = (
                self.generate_constant_number()
                if is_global_scope
                else self.generate_int_expression(depth=2, include_calls=True)
            )
            symbol = self.define_symbol(name, SysYType(()), is_const=False)
            return f"int {symbol.name} = {initializer};", symbol
        symbol = self.define_symbol(name, SysYType(()), is_const=False)
        return f"int {symbol.name};", symbol

    def choose_declaration_name(
        self, current_scope: dict[str, ObjectSymbol], prefer_shadowing: bool
    ) -> str:
        outer_names = self.visible_outer_names()
        if prefer_shadowing and outer_names and self.rng.random() < 0.8:
            return self.rng.choice(outer_names)
        return self.make_fresh_name(set(current_scope))

    def visible_outer_names(self) -> list[str]:
        if len(self.scopes) <= 1:
            return []
        names: list[str] = []
        seen: set[str] = set(self.scopes[-1])
        for scope in reversed(self.scopes[:-1]):
            for name in scope:
                if name in seen:
                    continue
                names.append(name)
                seen.add(name)
        return names

    def make_fresh_name(self, current_scope_names: set[str]) -> str:
        for _ in range(len(BASE_NAMES) * 2):
            base_name = self.rng.choice(BASE_NAMES)
            candidate = f"{base_name}_{self.next_fresh_name_id}"
            self.next_fresh_name_id += 1
            if candidate not in current_scope_names:
                return candidate
        while True:
            candidate = f"value_{self.next_fresh_name_id}"
            self.next_fresh_name_id += 1
            if candidate not in current_scope_names:
                return candidate

    def make_array_type(self) -> SysYType:
        rank = self.rng.randint(1, self.config.max_array_rank)
        dimensions = tuple(
            self.rng.randint(1, self.config.max_array_dim) for _ in range(rank)
        )
        return SysYType(dimensions)

    def define_symbol(self, name: str, type_: SysYType, is_const: bool) -> ObjectSymbol:
        symbol = ObjectSymbol(
            symbol_id=self.next_symbol_id,
            name=name,
            type=type_,
            is_const=is_const,
            scope_depth=len(self.scopes) - 1,
        )
        self.next_symbol_id += 1
        self.scopes[-1][name] = symbol
        return symbol

    def push_scope(self) -> None:
        self.scopes.append({})

    def pop_scope(self) -> None:
        self.scopes.pop()

    def visible_symbols(self) -> list[ObjectSymbol]:
        visible: list[ObjectSymbol] = []
        seen: set[str] = set()
        for scope in reversed(self.scopes):
            for name, symbol in scope.items():
                if name in seen:
                    continue
                visible.append(symbol)
                seen.add(name)
        return visible

    def visible_assignable_symbols(self) -> list[ObjectSymbol]:
        return [symbol for symbol in self.visible_symbols() if not symbol.is_const]

    def generate_lvalue(self) -> str:
        candidates = self.visible_assignable_symbols()
        if not candidates:
            raise RuntimeError("no visible assignable symbols available")
        symbol = self.rng.choice(candidates)
        return symbol.name + symbol.type.render_indices(self.rng)

    def generate_rvalue(self) -> str:
        candidates = self.visible_symbols()
        if not candidates:
            return self.generate_constant_number()
        symbol = self.rng.choice(candidates)
        return symbol.name + symbol.type.render_indices(self.rng)

    def generate_condition_expression(self, include_calls: bool) -> str:
        return self.generate_int_expression(depth=2, include_calls=include_calls)

    def generate_int_expression(self, depth: int, include_calls: bool) -> str:
        if depth <= 0:
            return self.generate_expression_leaf(include_calls)

        choices = ["leaf", "unary", "binary"]
        if include_calls and self.function_signatures:
            choices.append("call")
        choice = self.rng.choice(choices)

        if choice == "leaf":
            return self.generate_expression_leaf(include_calls)

        if choice == "call":
            return self.generate_call_expression(depth - 1)

        if choice == "unary":
            operator = self.rng.choice(UNARY_OPERATORS)
            operand = self.generate_int_expression(depth - 1, include_calls=include_calls)
            return f"({operator} {operand})"

        left = self.generate_int_expression(depth - 1, include_calls=include_calls)
        right = self.generate_int_expression(depth - 1, include_calls=include_calls)
        operator = self.rng.choice(LOGICAL_OPERATORS)
        return f"({left} {operator} {right})"

    def generate_expression_leaf(self, include_calls: bool) -> str:
        choices = ["number"]
        if self.visible_symbols():
            choices.append("var")
        if include_calls and self.function_signatures:
            choices.append("call")
        choice = self.rng.choice(choices)
        if choice == "number":
            return self.generate_constant_number()
        if choice == "call":
            return self.generate_call_expression(depth=1)
        return self.generate_rvalue()

    def generate_call_expression(self, depth: int) -> str:
        signature = self.rng.choice(self.function_signatures)
        arguments = [
            self.generate_int_expression(max(depth - 1, 0), include_calls=False)
            for _ in signature.parameter_types
        ]
        return f"{signature.name}({', '.join(arguments)})"

    def generate_constant_number(self) -> str:
        return str(self.rng.randint(-9, 9))


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


def save_failure_source(source_code: str) -> Path:
    with tempfile.NamedTemporaryFile(
        mode="w", prefix="yesod-function-stress-", suffix=".sy", dir="/tmp", delete=False
    ) as temp_file:
        temp_file.write(source_code)
        return Path(temp_file.name)


def compile_and_assemble(
    compiler: Path, source_path: Path, workdir: Path, timeout: int
) -> tuple[str | None, CommandResult]:
    asm_path = workdir / "program.S"
    compile_result = run_command(
        [str(compiler), "-riscv", str(source_path), "-o", str(asm_path)], timeout
    )
    if compile_result.returncode != 0:
        return "compile", compile_result

    clang = require_tool("clang")
    assemble_result = run_command(
        [
            clang,
            str(asm_path),
            "-c",
            "-o",
            str(workdir / "program.o"),
            "-target",
            "riscv32-unknown-linux-elf",
            "-march=rv32im",
            "-mabi=ilp32",
        ],
        timeout,
    )
    if assemble_result.returncode != 0:
        return "assembly", assemble_result

    return None, assemble_result


def run_one_test(
    compiler: Path, generator: ProgramGenerator, timeout: int
) -> tuple[str | None, Path | None, CommandResult | None]:
    source_code = generator.generate_program()
    with tempfile.TemporaryDirectory(prefix="yesod-function-stress-run-", dir="/tmp") as temp_dir:
        workdir = Path(temp_dir)
        source_path = workdir / "program.sy"
        source_path.write_text(source_code)

        failure_kind, command_result = compile_and_assemble(
            compiler=compiler,
            source_path=source_path,
            workdir=workdir,
            timeout=timeout,
        )
        if failure_kind is None:
            return None, None, None

        failure_path = save_failure_source(source_code)
        return failure_kind, failure_path, command_result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stress-test the compiler with randomly generated function-heavy SysY programs"
    )
    parser.add_argument("--compiler", default="/root/compiler/build/compiler")
    parser.add_argument("--tests", type=int, default=MAX_TESTS)
    parser.add_argument("--timeout", type=int, default=TIMEOUT_SECONDS)
    parser.add_argument("--seed", type=int, default=random.randrange(1 << 30))
    parser.add_argument("--max-functions", type=int, default=5)
    parser.add_argument("--max-params", type=int, default=4)
    parser.add_argument("--max-globals", type=int, default=5)
    parser.add_argument("--max-block-depth", type=int, default=4)
    parser.add_argument("--max-block-items", type=int, default=6)
    parser.add_argument("--max-array-rank", type=int, default=3)
    parser.add_argument("--max-array-dim", type=int, default=5)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    compiler = Path(args.compiler)
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    config = GeneratorConfig(
        max_functions=args.max_functions,
        max_params=args.max_params,
        max_globals=args.max_globals,
        max_block_depth=args.max_block_depth,
        max_block_items=args.max_block_items,
        max_array_rank=args.max_array_rank,
        max_array_dim=args.max_array_dim,
    )

    print(f"seed={args.seed}")
    for test_index in range(1, args.tests + 1):
        generator = ProgramGenerator(rng=rng, config=config)
        failure_kind, failure_path, command_result = run_one_test(
            compiler=compiler,
            generator=generator,
            timeout=args.timeout,
        )
        if failure_kind is not None and failure_path is not None:
            if command_result is not None and command_result.stderr:
                print(command_result.stderr, file=sys.stderr, end="")
            print(f"{failure_kind} error {failure_path}")
            return 1
        if test_index % 50 == 0:
            print(f"passed {test_index} tests")

    print(f"passed {args.tests} tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())