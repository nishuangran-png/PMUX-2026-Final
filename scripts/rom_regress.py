#!/usr/bin/env python3
"""常量 ROM 转 LUT 的泛化回归。

默认生成 4～8 位地址、1～16 位输出的随机、仿射、低次、稀疏和混合真值表，
分别用官方 Yosys 0.69 与优化版综合，要求：

  1. 优化后的 `cycloneiv_lcell_comb + $not` 不得高于官方基线；
  2. 抽样用例在 ABC9 后的 `$lut` 网表上通过 EQY；
  3. 1 位地址、非 2 次幂、多读口、X 初值、常量地址和 enable ROM 不得回归。

用法：
  python3 scripts/rom_regress.py
  python3 scripts/rom_regress.py --full --eqy all
  python3 scripts/rom_regress.py --reps 5 --keep
"""
import argparse
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools/build/official-v069/build/yosys"
CANDIDATE = ROOT / "tools/build/official-v069-opt/build-current-20261025/yosys"
EQY = ROOT / "tools/oss-cad-suite/bin/eqy"
CELL_RE = re.compile(r"^\s+(\d+)\s+(\S+)\s*$")

SHAPES = [(4, 1), (4, 4), (5, 2), (5, 4), (5, 16),
          (6, 1), (6, 8), (6, 16), (7, 2), (7, 8), (7, 16),
          (8, 8), (8, 10), (8, 12), (8, 16)]
MODES = ["random", "affine", "degree2", "sparse10", "sparse25", "mixed"]

# 每种结构至少覆盖一次 EQY，避免只做面积比较。
EQY_SAMPLE = {
    (5, 4): {"random", "affine", "degree2"},
    (5, 16): {"sparse10", "mixed"},
    (6, 8): {"random", "mixed"},
    (6, 16): {"random", "affine"},
    (7, 8): {"sparse10", "mixed"},
    (7, 16): {"random"},
    (8, 10): {"random"},
}


def bit_parity(value):
    return bin(value).count("1") & 1


def random_table(nbits, rng, density=None):
    size = 1 << nbits
    if density is None:
        return [rng.getrandbits(1) for _ in range(size)]
    return [int(rng.random() < density) for _ in range(size)]


def affine_table(nbits, rng):
    size = 1 << nbits
    coeff = rng.randrange(1, size)
    const = rng.randrange(2)
    return [const ^ bit_parity(addr & coeff) for addr in range(size)]


def degree2_table(nbits, rng, terms=4):
    size = 1 << nbits
    pairs = [(rng.randrange(nbits), rng.randrange(nbits)) for _ in range(terms)]
    const = rng.randrange(2)
    table = []
    for addr in range(size):
        value = const
        for left, right in pairs:
            value ^= ((addr >> left) & 1) & ((addr >> right) & 1)
        table.append(value)
    return table


def make_tables(mode, nbits, width, rng):
    tables = []
    for bit in range(width):
        if mode == "random":
            table = random_table(nbits, rng)
        elif mode == "affine":
            table = affine_table(nbits, rng)
        elif mode == "degree2":
            table = degree2_table(nbits, rng)
        elif mode == "sparse10":
            table = random_table(nbits, rng, 0.10)
        elif mode == "sparse25":
            table = random_table(nbits, rng, 0.25)
        elif mode == "mixed":
            if bit % 3 == 0:
                table = random_table(nbits, rng)
            elif bit % 3 == 1:
                table = affine_table(nbits, rng)
            else:
                table = random_table(nbits, rng, 0.10)
        else:
            raise ValueError(mode)
        tables.append(table)
    return tables


def write_truth_table(path, module, nbits, width, tables):
    lines = [
        f"module {module}(input wire [{nbits-1}:0] a, output reg [{width-1}:0] y);",
        "always @* begin",
        "  case (a)",
    ]
    for addr in range(1 << nbits):
        value = sum((tables[bit][addr] & 1) << bit for bit in range(width))
        lines.append(f"    {nbits}'d{addr}: y = {width}'d{value};")
    lines += ["  endcase", "end", "endmodule"]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


SPECIAL_CASES = {
    "rom_addr1": """
module rom_addr1(input wire a, output wire y);
  reg rom [0:1];
  initial begin rom[0] = 1'b0; rom[1] = 1'b1; end
  assign y = rom[a];
endmodule
""",
    "rom_nonpow2": """
module rom_nonpow2(input wire [1:0] a, output wire [3:0] y);
  reg [3:0] rom [0:2];
  initial begin rom[0] = 4'h3; rom[1] = 4'h9; rom[2] = 4'h6; end
  assign y = rom[a];
endmodule
""",
    "rom_multiread": """
module rom_multiread(input wire [3:0] a, b, output wire [3:0] y0, y1);
  reg [3:0] rom [0:15];
  initial begin
    rom[0]=4'h1; rom[1]=4'h2; rom[2]=4'h3; rom[3]=4'h4;
    rom[4]=4'h5; rom[5]=4'h6; rom[6]=4'h7; rom[7]=4'h8;
    rom[8]=4'h9; rom[9]=4'ha; rom[10]=4'hb; rom[11]=4'hc;
    rom[12]=4'hd; rom[13]=4'he; rom[14]=4'hf; rom[15]=4'h0;
  end
  assign y0 = rom[a];
  assign y1 = rom[b];
endmodule
""",
    "rom_xinit": """
module rom_xinit(input wire [1:0] a, output wire [1:0] y);
  reg [1:0] rom [0:3];
  initial begin rom[0] = 2'b01; rom[1] = 2'b10; end
  assign y = rom[a];
endmodule
""",
    "rom_const_addr": """
module rom_const_addr(input wire [1:0] a, output wire [3:0] y);
  reg [3:0] rom [0:7];
  initial begin
    rom[0]=4'h1; rom[1]=4'h4; rom[2]=4'h2; rom[3]=4'h8;
    rom[4]=4'h3; rom[5]=4'hc; rom[6]=4'h5; rom[7]=4'ha;
  end
  assign y = rom[{a, 1'b0}];
endmodule
""",
    "rom_enable": """
module rom_enable(input wire en, input wire [3:0] a, output wire [5:0] y);
  reg [5:0] rom [0:15];
  initial begin
    rom[0]=6'h01; rom[1]=6'h03; rom[2]=6'h07; rom[3]=6'h0f;
    rom[4]=6'h1f; rom[5]=6'h3f; rom[6]=6'h3e; rom[7]=6'h3c;
    rom[8]=6'h38; rom[9]=6'h30; rom[10]=6'h20; rom[11]=6'h21;
    rom[12]=6'h23; rom[13]=6'h27; rom[14]=6'h2f; rom[15]=6'h1e;
  end
  assign y = en ? rom[a] : 6'b0;
endmodule
""",
}


def run_command(cmd, timeout=None):
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(map(str, cmd))}\n{proc.stderr[-2000:]}")
    return proc


def synth_stats(yosys, source, top):
    script = f"read_verilog {source}; synth_intel -family cycloneiv -top {top}; stat"
    proc = run_command([str(yosys), "-Q", "-p", script], timeout=180)
    marker = f"=== {top} ==="
    stat_at = proc.stdout.rfind(marker)
    if stat_at < 0:
        raise RuntimeError(f"{top}: final stat section not found")
    # synth_intel itself may print an intermediate stat containing wide $lut
    # cells.  Only the last module stat describes the delivered netlist.
    stat_text = proc.stdout[stat_at:]
    cells = {}
    for line in stat_text.splitlines():
        match = CELL_RE.match(line)
        if match:
            cells[match.group(2)] = int(match.group(1))
    if "$lut" in cells:
        raise RuntimeError(f"{top}: wide $lut left unmapped; area result is invalid")
    lcell = cells.get("cycloneiv_lcell_comb", 0)
    not_cells = cells.get("$not", 0)
    return lcell + not_cells, cells.get("dffeas", 0), cells


def keep_top_module(src, dst, name):
    lines = src.read_text(errors="replace").splitlines()
    out, pending, index = [], [], 0
    while index < len(lines):
        line = lines[index]
        if line.startswith("attribute "):
            pending.append(line)
            index += 1
            continue
        if line.startswith("module "):
            module = line.split()[1].lstrip("\\")
            block = [line]
            index += 1
            while index < len(lines) and lines[index] != "end":
                block.append(lines[index])
                index += 1
            if index < len(lines):
                block.append("end")
            index += 1
            if module == name:
                out.extend(pending)
                out.extend(block)
            pending = []
            continue
        if not pending:
            out.append(line)
        index += 1
    dst.write_text("\n".join(out) + "\n", encoding="utf-8")


def synth_lut(yosys, source, top, output, workdir, arm):
    raw = workdir / f"{top}_{arm}_raw.il"
    script = (
        f"read_verilog {source}; "
        f"synth_intel -family cycloneiv -top {top} -run begin:map_ffs; "
        f"abc9 -lut 4 -W 300; clean; opt -fast; clean; "
        f"write_rtlil {raw}"
    )
    run_command([str(yosys), "-q", "-p", script], timeout=180)
    keep_top_module(raw, output, top)
    if not output.exists() or output.stat().st_size == 0:
        raise RuntimeError(f"{top}: failed to extract top module for EQY")
    return sum(1 for line in output.read_text(errors="replace").splitlines()
               if line.startswith("  cell $lut"))


def run_eqy(source, top, workdir, timeout):
    gold = workdir / f"{top}_gold.il"
    gate = workdir / f"{top}_gate.il"
    gold_luts = synth_lut(BASELINE, source, top, gold, workdir, "gold")
    gate_luts = synth_lut(CANDIDATE, source, top, gate, workdir, "gate")
    config = workdir / f"{top}.eqy"
    config.write_text(f"""[gold]
read_rtlil {gold.name}
prep -top {top}

[gate]
read_rtlil {gate.name}
prep -top {top}

[strategy sat]
use sat
depth 10
""", encoding="utf-8")
    proc = subprocess.run([str(EQY), "-d", f"eqy_{top}", "-f", config.name],
                          cwd=workdir, capture_output=True, text=True, timeout=timeout)
    output = proc.stdout + proc.stderr
    if "DONE (PASS" in output:
        status = "PASS"
    elif "DONE (FAIL" in output:
        status = "FAIL"
    else:
        status = "ERROR"
    return status, gold_luts, gate_luts, output


def run_whole_miter(top, gold, gate, workdir, timeout):
    """用原语言层整体 miter 复核 EQY 的分区结果。

    EQY 默认按输出位切分区，并把两条网表中同名的内部信号都当作分区输入。
    常量 ROM 转 LUT 后，gold 与 gate 可能选择不同的内部节点作为输入，
    EQY 因而会丢失内部信号与原输入之间的函数关系，产生假失败。
    这里把整个模块作为一个 miter，保留全部内部逻辑后再做 SAT。
    """
    script = (
        f"read_rtlil {gold.name}; rename {top} gold; "
        f"read_rtlil {gate.name}; rename {top} gate; "
        "miter -equiv -flatten -make_outputs gold gate miter; "
        "hierarchy -top miter; "
        "sat -verify -prove trigger 0 miter"
    )
    proc = subprocess.run(
        [str(BASELINE), "-Q", "-p", script],
        cwd=workdir, capture_output=True, text=True, timeout=timeout)
    output = proc.stdout + proc.stderr
    if proc.returncode == 0 and "SAT proof finished - no model found: SUCCESS!" in output:
        return "PASS", output
    if "SAT proof finished" in output and "model found" in output:
        return "FAIL", output
    return "ERROR", output


def main():
    global BASELINE, CANDIDATE

    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", default=str(BASELINE))
    parser.add_argument("--candidate", default=str(CANDIDATE))
    parser.add_argument("--seed", type=int, default=20261023)
    parser.add_argument("--reps", type=int, default=2, help="每种结构与尺寸重复数")
    parser.add_argument("--full", action="store_true", help="每种结构 5 次（较慢）")
    parser.add_argument("--eqy", choices=["none", "sample", "all"], default="sample")
    parser.add_argument("--eqy-timeout", type=int, default=180)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    reps = 5 if args.full else args.reps
    BASELINE = Path(args.baseline)
    CANDIDATE = Path(args.candidate)

    for binary in (BASELINE, CANDIDATE):
        if not binary.exists():
            parser.error(f"缺少 Yosys：{binary}")
    if args.eqy != "none" and not EQY.exists():
        parser.error(f"缺少 EQY：{EQY}")

    workdir = Path(tempfile.mkdtemp(prefix="pmux_rom_regress_"))
    print(f"临时目录: {workdir}")
    rng = random.Random(args.seed)
    failures = []
    improved = equal = 0
    total = 0
    best = (0, "")
    eqy_results = []

    cases = []
    for name, text in SPECIAL_CASES.items():
        path = workdir / f"{name}.v"
        path.write_text(text.lstrip(), encoding="utf-8")
        cases.append((name, path, name, None))
    for nbits, width in SHAPES:
        for mode in MODES:
            for rep in range(reps):
                top = f"rom_{nbits}_{width}_{mode}_{rep}"
                path = workdir / f"{top}.v"
                tables = make_tables(mode, nbits, width, rng)
                write_truth_table(path, top, nbits, width, tables)
                cases.append((top, path, mode, (nbits, width, rep)))

    print(f"{'case':36} {'base':>6} {'cand':>6} {'delta':>7}  EQY")
    for top, source, mode, shape in cases:
        total += 1
        try:
            base, base_dff, _ = synth_stats(BASELINE, source, top)
            cand, cand_dff, _ = synth_stats(CANDIDATE, source, top)
        except Exception as exc:
            failures.append((top, str(exc)))
            print(f"{top:36} ERROR  {exc}")
            continue

        delta = cand - base
        if cand > base:
            failures.append((top, f"面积回归 {base} -> {cand}"))
        elif cand < base:
            improved += 1
        else:
            equal += 1
        if base - cand > best[0]:
            best = (base - cand, top)
        if cand_dff > base_dff:
            failures.append((top, f"DFF 回归 {base_dff} -> {cand_dff}"))

        need_eqy = args.eqy == "all"
        if args.eqy == "sample" and shape is not None:
            nbits, width, rep = shape
            need_eqy = rep == 0 and mode in EQY_SAMPLE.get((nbits, width), set())
        if need_eqy:
            try:
                status, gold_luts, gate_luts, eqy_log = run_eqy(
                    source, top, workdir, args.eqy_timeout)
            except Exception as exc:
                status, gold_luts, gate_luts, eqy_log = "ERROR", -1, -1, str(exc)
            partition_status = status
            whole_status = None
            whole_log = ""
            if status == "FAIL":
                gold = workdir / f"{top}_gold.il"
                gate = workdir / f"{top}_gate.il"
                whole_status, whole_log = run_whole_miter(
                    top, gold, gate, workdir, args.eqy_timeout)
                if whole_status == "PASS":
                    status = "PASS"
            eqy_results.append((top, status, gold_luts, gate_luts,
                                partition_status, whole_status))
            if status != "PASS":
                detail = eqy_log[-2000:]
                if whole_status is not None:
                    detail += "\n\n整体 miter 复核：\n" + whole_log[-2000:]
                failures.append((top, f"EQY {partition_status} / 整体 {whole_status or '未执行'}\n{detail}"))
            if partition_status == "PASS":
                eqy_text = f"PASS ({gold_luts}->{gate_luts} $lut)"
            elif status == "PASS":
                eqy_text = f"PASS* (EQY {partition_status}; 整体 SAT PASS)"
            else:
                eqy_text = f"FAIL (EQY {partition_status}; 整体 {whole_status or '未执行'})"
        else:
            eqy_text = "-"
        print(f"{top:36} {base:6d} {cand:6d} {delta:+7d}  {eqy_text}")

    print("\n汇总")
    print(f"  面积用例: {total}，改善 {improved}，持平 {equal}，回归 {total-improved-equal}")
    if best[1]:
        print(f"  最大改善: {best[1]}，减少 {best[0]} 个逻辑单元")
    if eqy_results:
        passed = sum(status == "PASS" for _, status, _, _, _, _ in eqy_results)
        print(f"  EQY: {passed}/{len(eqy_results)} PASS")
        partition_failures = sum(partition == "FAIL" for _, _, _, _, partition, _ in eqy_results)
        if partition_failures:
            print(f"  其中 EQY 分区失败但整体 miter 通过: {partition_failures}")
    if failures:
        print("\n失败项:")
        for name, reason in failures:
            print(f"  - {name}: {reason}")
    else:
        print("\nPASS：没有面积/DFF 回归，抽样的 EQY 全部通过。")

    if not args.keep:
        shutil.rmtree(workdir, ignore_errors=True)
    else:
        print(f"临时文件保留: {workdir}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
