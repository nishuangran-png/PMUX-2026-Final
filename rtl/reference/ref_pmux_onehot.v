// 理想实现参照 · pmux_onehot
//
// 与 rtl/single/pmux_onehot.v 功能完全等价（待 EQY 确认）。
// 原始写法：sel = addr ^ 3'b101，再对 sel 做 case。
// 这里把那个异或**折叠进叶子的排列顺序**：直接按 addr 建树，
// 只把 8 个数据输入的位置换一下。这样不仅 8 个 $eq 比较器没了，
// 连产生 sel 的 3 个 XOR 也一并省掉。
//
// 叶子顺序推导：addr ^ 5 = 选择码，故 addr 的 0..7 依次对应
//   码 5,4,7,6,1,0,3,2  →  叶子 [d5, d4, d7, d6, d1, d0, d3, d2]
//
// 实测：默认流程 132 → 56（−57.6%）

module pmux_onehot #(
    parameter W = 8
) (
    input  wire [2:0]   addr,
    input  wire [W-1:0] d0, d1, d2, d3, d4, d5, d6, d7,
    output wire [W-1:0] y
);
    wire [W-1:0] l0 = addr[0] ? d4 : d5;
    wire [W-1:0] l1 = addr[0] ? d6 : d7;
    wire [W-1:0] l2 = addr[0] ? d0 : d1;
    wire [W-1:0] l3 = addr[0] ? d2 : d3;

    wire [W-1:0] m0 = addr[1] ? l1 : l0;
    wire [W-1:0] m1 = addr[1] ? l3 : l2;

    assign y = addr[2] ? m1 : m0;
endmodule
