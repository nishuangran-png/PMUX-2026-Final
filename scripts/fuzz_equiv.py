#!/usr/bin/env python3
"""随机用例模糊测试：自动生成随机的常量解码型 PMUX 电路，逐个跑
「优化流程 → EQY 形式等价验证」，全部通过才算过。

为什么要做：赛题规则里「任意一个用例不等价 = 整个作品作废」，
所以除了手写的 13 个用例，还需要一批随机结构来撞算法边界。

用法：
  python3 scripts/fuzz_equiv.py [数量] [随机种子]
  # 默认 20 个用例、种子 20261014（固定种子保证可复现）

产物：rtl/fuzz/fuzz_*.v（可复现的实验记录），验证日志在 /tmp。
"""
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUZZ_DIR = os.path.join(ROOT, "rtl", "fuzz")
PLUGIN = os.path.join(ROOT, "build", "pmux_opt.so")


def gen(rng, idx):
    """生成一个随机电路，返回 (verilog 文本, 模块名)。"""
    nbits = rng.randint(2, 5)               # 选择总线宽度
    width = rng.choice([4, 8, 8, 16])       # 数据位宽
    ncodes = 1 << nbits
    style = rng.choice(["full", "sparse", "sparse", "paired"])
    if style == "full":
        covered = list(range(ncodes))
    elif style == "sparse":
        covered = sorted(rng.sample(range(ncodes), rng.randint(1, max(1, ncodes // 2))))
    else:  # paired：成对出现，模拟一组码共用数据
        covered = sorted(rng.sample(range(ncodes), max(1, ncodes // 2)))

    mod = f"fuzz_{idx:03d}"
    add_logic = rng.random() < 0.35
    extra_mux = rng.random() < 0.35

    cases = []
    for i, c in enumerate(covered):
        d = f"d{i}"
        if add_logic and rng.random() < 0.4:
            cases.append(f"            {nbits}'d{c}: y = {d} ^ a;")
        else:
            cases.append(f"            {nbits}'d{c}: y = {d};")
    default_expr = rng.choice([f"{{W{{1'b0}}}}", "a", f"~{{W{{1'b0}}}}"])
    cases.append(f"            default: y = {default_expr};")

    ports = [f"    input  wire [{nbits-1}:0] sel",
             f"    input  wire [W-1:0] a"]
    ports += [f"    input  wire [W-1:0] d{i}" for i in range(len(covered))]

    body2 = ""
    if extra_mux:
        # 第二个 mux：选择信号是 sel 的反码，考察一bus 被反相后的处理
        k = rng.randint(1, max(1, len(covered)))
        cs = "\n".join(f"                {nbits}'d{c}: y2 = d{i} | a;" for i, c in enumerate(covered[:k]))
        body2 = f"""
    reg [W-1:0] y2;
    always @* begin
        case (~sel)
{cs}
            default: y2 = {{W{{1'b0}}}};
        endcase
    end"""
        ports.append(f"    output reg [W-1:0] y2")

    outs = "output reg [W-1:0] y"
    ports_joined = ",\n".join(ports)
    cases_joined = "\n".join(cases)
    src = f"""// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module {mod} #(parameter W = {width}) (
{ports_joined},
    {outs}
);
    always @* begin
        case (sel)
{cases_joined}
        endcase
    end{body2}
endmodule
"""
    return src, mod


def run(cmd, cwd=ROOT):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)


def last_cells(text):
    """日志里最后一个 "N cells" —— synth 内部也会打一次，必须取最后一个。"""
    import re
    m = re.findall(r"^\s+(\d+)\s+cells\s*$", text, re.M)
    return int(m[-1]) if m else None


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20261014
    rng = random.Random(seed)
    os.makedirs(FUZZ_DIR, exist_ok=True)

    n_pass = n_fail = 0
    for i in range(count):
        src, mod = gen(rng, i)
        path = os.path.join(FUZZ_DIR, mod + ".v")
        open(path, "w").write(src)

        work = tempfile.mkdtemp(prefix="fuzz_")
        il = os.path.join(work, mod + ".il")
        flow = (f"read_verilog {path}; proc; opt; pmux_opt; synth -noabc -top {mod}; "
                f"abc -script {ROOT}/scripts/abc_mux_keep.abc; opt_clean -purge; write_rtlil {il}")
        r = run(["yosys", "-Q", "-m", PLUGIN, "-p", flow])
        if r.returncode != 0:
            print(f"  [FAIL] {mod}: 优化流程报错")
            n_fail += 1
            continue

        eqy = os.path.join(work, "eq.eqy")
        open(eqy, "w").write(f"""[gold]
read_verilog {path}
prep -top {mod}

[gate]
read_rtlil {il}
prep -top {mod}

[strategy sat]
use sat
""")
        r = run(["eqy", "eq.eqy"], cwd=work)
        ok = r.returncode == 0 and "DONE (PASS" in r.stdout
        # 基准与优化必须走同一个映射口径（mux_keep），只差加不加 pass
        b = last_cells(run(["yosys", "-Q", "-p",
                            f"read_verilog {path}; synth -noabc -top {mod}; "
                            f"abc -script {ROOT}/scripts/abc_mux_keep.abc; opt_clean -purge; stat"]).stdout)
        o = last_cells(run(["yosys", "-Q", "-m", PLUGIN, "-p",
                            f"read_verilog {path}; proc; opt; pmux_opt; synth -noabc -top {mod}; "
                            f"abc -script {ROOT}/scripts/abc_mux_keep.abc; opt_clean -purge; stat"]).stdout)
        delta = f"  {b} → {o}（{(o-b)/b*100:+.1f}%）" if b and o else ""
        if ok:
            n_pass += 1
            print(f"  [PASS] {mod}{delta}")
        else:
            n_fail += 1
            print(f"  [FAIL] {mod}: 不等价！中间文件在 {work}")

    print(f"\n随机用例 {count} 个：{n_pass} 通过，{n_fail} 失败（种子 {seed}）")
    if n_fail:
        print("⚠️  不等价 = 作品作废级别的问题，必须定位。")
        sys.exit(1)


if __name__ == "__main__":
    main()
