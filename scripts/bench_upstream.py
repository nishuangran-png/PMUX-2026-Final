#!/usr/bin/env python3
"""上游「插一步 pmuxtree」与我们的算法在同一批用例上的对比。

背景：汇报材料里「插一步上游 pmuxtree，132 → 103，22%」曾让 22% 看起来
不可思议。本脚本把三种做法放在一张表上，说明各自吃掉了什么：

  base      read_verilog; synth                  官方默认流程
  upstream  read_verilog; proc; opt; pmuxtree; synth   上游现成的展平步骤
  ours      read_verilog; proc; opt; pmux_opt;  synth   本选题的算法

注意：pmuxtree 不在 synth 默认脚本里（reports/005 已验证），必须显式插入，
所以它属于「任何人插一步都能拿到的」收益，不是算法贡献。

用法：python3 scripts/bench_upstream.py
"""
import json
import os
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN = os.path.join(ROOT, "build", "pmux_opt.so")
CASES = [ln.split(":") for ln in """\
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
scale/scale_case64:scale_case64""".splitlines() if ln.strip()]

FLOWS = {
    "base":     ("synth -top {mod}", False),
    "upstream": ("proc; opt; pmuxtree; synth -top {mod}", False),
    "ours":     ("proc; opt; pmux_opt; synth -top {mod}", True),
}


def cells(src, mod, flow, need_plugin):
    jf = tempfile.NamedTemporaryFile(suffix=".json", delete=False).name
    script = (f"read_verilog {os.path.join(ROOT, 'rtl', src + '.v')}; "
              f"{flow.format(mod=mod)}; opt_clean -purge; tee -o {jf} stat -json")
    args = ["yosys", "-Q"] + (["-m", PLUGIN] if need_plugin else []) + ["-p", script]
    subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    try:
        data = json.load(open(jf))
    finally:
        os.unlink(jf)
    key = [k for k in data["modules"] if k.lstrip("\\") == mod][0]
    return data["modules"][key]["num_cells"]


names = list(FLOWS)
print(f"{'用例':<15}" + "".join(f"{n:>10}" for n in names) + f"{'ours vs base':>14}{'ours vs up':>12}")
print("-" * 75)
tot = {n: 0 for n in names}
for src, mod in CASES:
    vals = {n: cells(src, mod, *FLOWS[n]) for n in names}
    for n in names:
        tot[n] += vals[n]
    b, u, o = vals["base"], vals["upstream"], vals["ours"]
    print(f"{mod:<15}{b:>10}{u:>10}{o:>10}{(o-b)/b*100:>13.1f}%{(o-u)/u*100:>11.1f}%")
print("-" * 75)
b, u, o = tot["base"], tot["upstream"], tot["ours"]
print(f"{'合计':<15}{b:>10}{u:>10}{o:>10}{(o-b)/b*100:>13.1f}%{(o-u)/u*100:>11.1f}%")
print(f"\n上游 pmuxtree 相对默认流程: {(u-b)/b*100:+.1f}%")
