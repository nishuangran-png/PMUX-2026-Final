#!/usr/bin/env python3
"""生成规模化测试用例（rtl/scale/*.v）。

目的是验证选题要求的「千级节点电路 ≤60s / ≤2GB」，以及在大电路上
优化收益是否仍然成立。生成的用例都是「常量解码型 PMUX」在不同写法下的
变体，模拟真实设计里常见的控制/数据选择结构。

所有用例都是纯组合逻辑、无复位，便于 EQY 直接验证。

用法：python3 scripts/gen_cases.py
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "rtl", "scale")


def write(name, text):
    path = os.path.join(OUT, name)
    with open(path, "w") as f:
        f.write(text)
    print("wrote", path)


def case_full(nbits, width, name):
    """全码覆盖的 case：2**nbits 路选择，数据位宽 width。"""
    n = 1 << nbits
    lines = []
    for i in range(n):
        lines.append(f"            {nbits}'d{i}: y = d{i};")
    ports = ", ".join(f"input wire [{width-1}:0] d{i}" for i in range(n))
    write(name, f"""// 自动生成（scripts/gen_cases.py）：全码覆盖 case，{n} 路 × {width} 位
// 测什么：最基本的常量解码型 PMUX，规模放大到接近千级节点。
module {name.split('.')[0]} #(parameter W = {width}) (
    input  wire [{nbits-1}:0] sel,
    {ports},
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
{chr(10).join(lines)}
            default: y = {{W{{1'b0}}}};
        endcase
    end
endmodule
""")


def case_sparse(nbits, width, covered, name):
    """稀疏 case：只列 covered 个码，其余走 default（多个缺码）。"""
    n = 1 << nbits
    lines = []
    for i in range(covered):
        lines.append(f"            {nbits}'d{i}: y = d{i};")
    ports = ", ".join(f"input wire [{width-1}:0] d{i}" for i in range(covered))
    write(name, f"""// 自动生成：稀疏 case，{nbits} 位选择里只列 {covered} 个码（{n-covered} 个缺码走 default）
// 测什么：多缺码场景（旧版插件只支持缺 1 个码，会跳过这类）。
module {name.split('.')[0]} #(parameter W = {width}) (
    input  wire [{nbits-1}:0] sel,
    {ports},
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
{chr(10).join(lines)}
            default: y = {{W{{1'b1}}}};
        endcase
    end
endmodule
""")


def multi_mux(nbits, width, count, name):
    """一组共享选择总线的并行 case mux（数据通路的常见写法）。"""
    n = 1 << nbits
    body = []
    for m in range(count):
        cs = "\n".join(f"                {nbits}'d{i}: y{m} = a{m}_{i};" for i in range(n))
        body.append(f"""        case (sel)
{cs}
            default: y{m} = {width}'h{(m * 17) & ((1 << width) - 1):x};
        endcase""")
    ports = []
    for m in range(count):
        ports.append(f"    output reg [{width-1}:0] y{m}")
        for i in range(n):
            ports.append(f"    input  wire [{width-1}:0] a{m}_{i}")
    write(name, f"""// 自动生成：{count} 个并行 case mux 共享 {nbits} 位选择总线，每个 {n} 路 × {width} 位
// 测什么：多个 PMUX 同时存在时，算法是否都能吃掉；总规模接近千级节点。
module {name.split('.')[0]} (
    input  wire [{nbits-1}:0] sel,
{",".join(ports)}
);
    always @* begin
{chr(10).join(body)}
    end
endmodule
""")


def alu_like(name):
    """类 ALU：操作码 case + 算术/移位逻辑混在一起。"""
    ops = ["ADD", "SUB", "AND", "OR", "XOR", "SLL", "SRL", "SLT"]
    cs = "\n".join(f"            4'd{i}: y = {op.lower()}_r;" for i, op in enumerate(ops))
    write(name, f"""// 自动生成：类 ALU，4 位操作码选 8 种操作，运算结果再经输出 case
// 测什么：PMUX 与算术逻辑混排（选题要求「支持混合组合逻辑 PMUX」）。
module {name.split('.')[0]} #(parameter W = 16) (
    input  wire [3:0]  op,
    input  wire [W-1:0] a, b,
    output reg  [W-1:0] y
);
    wire [W-1:0] add_r = a + b;
    wire [W-1:0] sub_r = a - b;
    wire [W-1:0] and_r = a & b;
    wire [W-1:0] or_r  = a | b;
    wire [W-1:0] xor_r = a ^ b;
    wire [W-1:0] sll_r = a << b[3:0];
    wire [W-1:0] srl_r = a >> b[3:0];
    wire [W-1:0] slt_r = {{ {{(W-1){{1'b0}}}}, (a < b) }};

    always @* begin
        case (op)
{cs}
            default: y = {{W{{1'b0}}}};
        endcase
    end
endmodule
""")


def nested(name):
    """两级嵌套 case：外层 3 位选 8 组，组内再按 3 位选 8 个。"""
    inner = []
    for g in range(8):
        cs = "\n".join(f"                3'd{k}: y = g{g}_{k};" for k in range(8))
        inner.append(f"""            3'd{g}: begin
                case (sub)
{cs}
                    default: y = 8'h00;
                endcase
            end""")
    ports = ", ".join(f"input wire [7:0] g{g}_{k}" for g in range(8) for k in range(8))
    write(name, f"""// 自动生成：两级嵌套 case（外层 8 组 × 组内 8 路，共 64 个数据输入）
// 测什么：层次化 PMUX；规模与 64 路单级相当，但结构更接近真实多级译码。
module {name.split('.')[0]} (
    input  wire [2:0] sel,
    input  wire [2:0] sub,
    {ports},
    output reg  [7:0] y
);
    always @* begin
        case (sel)
{chr(10).join(inner)}
            default: y = 8'h00;
        endcase
    end
endmodule
""")


def main():
    os.makedirs(OUT, exist_ok=True)
    case_full(3, 8, "scale_case8.v")        # 8 路 × 8 位
    case_full(4, 16, "scale_case16.v")      # 16 路 × 16 位
    case_full(6, 8, "scale_case64.v")       # 64 路 × 8 位
    case_sparse(5, 16, 12, "scale_sparse32.v")
    multi_mux(3, 8, 6, "scale_multi6.v")    # 6 个 8 路 mux 共享总线
    alu_like("scale_alu.v")
    nested("scale_nested.v")
    print("done.")


if __name__ == "__main__":
    main()
