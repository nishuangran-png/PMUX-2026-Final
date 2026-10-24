#!/usr/bin/env python3
"""生成 5/6/8/16 级优先级选择链。

这些文件是项目自行构造的专项回归，不是官方用例，也不是真实芯片设计。
用途只有一个：检查 pmux_opt 在超过官方 test2 的四级链之后是否仍能正确处理
长链，并观察面积、DFF 与运行代价。
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "rtl" / "priority_chains"
LEVELS = (5, 6, 8, 16)


def render(levels: int) -> str:
    top = f"priority_chain_{levels:02d}"
    select_msb = levels - 1
    inputs = [f"    input  wire [W-1:0] d{i}" for i in range(levels)]
    inputs.append("    input  wire [W-1:0] dflt")
    input_block = ",\n".join(inputs)
    arms = "\n".join(
        f"            sel[{i}]: y = d{i};" for i in range(levels)
    )
    return f"""// 自动生成（scripts/gen_priority_chains.py）：自行构造的 {levels} 级优先级链
// 不是官方用例，也不是真实设计；只用于长链专项回归。
module {top} #(
    parameter W = 16
) (
    input  wire [{select_msb}:0] sel,
{input_block},
    output reg  [W-1:0] y
);
    always @* begin
        case (1'b1)
{arms}
            default: y = dflt;
        endcase
    end
endmodule
"""


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for levels in LEVELS:
        path = OUT / f"priority_chain_{levels:02d}.v"
        path.write_text(render(levels), encoding="utf-8")
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
