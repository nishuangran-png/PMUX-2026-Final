#!/usr/bin/env python3
"""多流程对照测量：对每个用例，分别在若干「映射口径」下量基准与优化后的单元数。

为什么要分口径：
  Yosys 默认 synth 里的 abc 是**延迟导向**映射，它会把规则的多路选择器
  重新拆成零散门电路；而本选题的目标是面积。两者必须分开量、分开比，
  不能把门级 cell 数和 $lut 数混在一张表里。

口径：
  default    官方默认流程 synth（门级 cell 数；这就是赛题的 baseline）
  gate_area  门级映射，但 ABC 换成面积导向（&nf -a）
  lut_area   LUT 映射（K=6），对应 FPGA 实现在 LUT 口径下的结果

用法：
  python3 scripts/bench_flows.py [用例清单文件]
  # 清单格式同 scripts/regress.sh：rtl 下相对路径(不含 .v):顶层模块名

输出：每个用例一行，列出 3 个口径下 基准/优化后/降幅。
"""
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN = os.path.join(ROOT, "build", "pmux_opt.so")

DEFAULT_CASES = """\
single/pmux_basic:pmux_basic
single/pmux_onehot:pmux_onehot
single/pmux_priority:pmux_priority
single/mux_tree_8to1:mux_tree_8to1
mixed/pmux_in_alu:pmux_in_alu
mixed/pmux_nested:pmux_nested
realistic/fsm_ctrl:fsm_ctrl
realistic/cpu_datapath:cpu_datapath
scale/scale_case8:scale_case8
scale/scale_case16:scale_case16
scale/scale_sparse32:scale_sparse32
scale/scale_multi6:scale_multi6
scale/scale_alu:scale_alu
scale/scale_nested:scale_nested
scale/scale_case64:scale_case64
"""

FLOWS = {
    "default":   "synth -top {mod}",
    "mux_keep":  "synth -noabc -top {mod}; abc -script " + ROOT + "/scripts/abc_mux_keep.abc",
    "gate_area": "synth -noabc -top {mod}; abc -script " + ROOT + "/scripts/abc_gate_area.abc",
    "lut_area":  "synth -noabc -top {mod}; abc -script " + ROOT + "/scripts/abc_lut_area.abc",
}
FLOW_ORDER = ["default", "mux_keep", "gate_area", "lut_area"]


def measure(src, mod, flow_tail, use_plugin):
    pre = "proc; opt; pmux_opt;" if use_plugin else ""
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
        jf = tf.name
    script = (f"read_verilog {os.path.join(ROOT, 'rtl', src + '.v')}; {pre} "
              f"{flow_tail.format(mod=mod)}; opt_clean -purge; "
              f"tee -o {jf} stat -json")
    args = ["yosys", "-Q"]
    if use_plugin:
        args += ["-m", PLUGIN]
    args += ["-p", script]
    res = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    log = res.stdout + res.stderr
    try:
        data = json.load(open(jf))
    except Exception as e:
        os.unlink(jf)
        return None, f"JSON 解析失败：{e}"
    finally:
        if os.path.exists(jf):
            os.unlink(jf)
    if "cmd error" in log:
        return None, "ABC 脚本报错（结果不可用）"
    key = [k for k in data.get("modules", {}) if k.lstrip("\\") == mod]
    if not key:
        return None, "没找到模块 " + mod
    m = data["modules"][key[0]]
    types = m.get("num_cells_by_type", {})
    return {"total": m.get("num_cells", 0), "types": types}, None


def main():
    cases = DEFAULT_CASES
    if len(sys.argv) > 1 and os.path.exists(sys.argv[1]):
        cases = open(sys.argv[1]).read()
    rows = [ln.strip() for ln in cases.splitlines()
            if ln.strip() and not ln.strip().startswith("#")]

    header = f"{'用例':<14}" + "".join(f"{f:>21}" for f in FLOW_ORDER)
    print(header)
    print(f"{'':<14}" + "".join(f"{'基准':>8}{'优化':>7}{'降幅':>6}" for _ in FLOW_ORDER))
    print("-" * len(header))
    totals = {f: [0, 0] for f in FLOW_ORDER}
    problems = []
    for row in rows:
        src, mod = row.split(":")
        line = f"{mod:<14}"
        for flow in FLOW_ORDER:
            tail = FLOWS[flow]
            base, err1 = measure(src, mod, tail, False)
            opt, err2 = measure(src, mod, tail, True)
            if err1 or err2:
                line += f"{'ERR':>21}"
                problems.append(f"{mod}/{flow}: {err1 or err2}")
                continue
            b, o = base["total"], opt["total"]
            totals[flow][0] += b
            totals[flow][1] += o
            line += f"{b:>8}{o:>7}{(o - b) / b * 100:>5.1f}%"
        print(line)
    print("-" * len(header))
    line = f"{'合计':<14}"
    for flow in FLOW_ORDER:
        b, o = totals[flow]
        line += f"{b:>8}{o:>7}{(o - b) / b * 100:>5.1f}%" if b else f"{'-':>21}"
    print(line)
    if problems:
        print("\n⚠️  异常：")
        for p in problems:
            print("   " + p)
        sys.exit(1)


if __name__ == "__main__":
    main()
