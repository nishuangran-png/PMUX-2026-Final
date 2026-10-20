// 理想实现参照 · pmux_basic
//
// 与 rtl/single/pmux_basic.v 功能完全等价（待 EQY 确认）。
// 差别只有一处：不用 8 个 $eq 比较器解出选择码，而是用 sel 的三位
// 直接驱动一棵 mux 树。比较器全部消失，所以默认流程里那 132 个门
// 变成 56 个 $_MUX_。
//
// 注意端口必须与原始模块完全一致（EQY 要求），故 dflt 保留但不使用：
// 原 case 覆盖了 3 位 sel 的全部 8 种取值，default 分支不可达。
//
// 实测：默认流程 132 → 56（−57.6%）

module pmux_basic #(
    parameter W = 8
) (
    input  wire [2:0]   sel,
    input  wire [W-1:0] d0, d1, d2, d3, d4, d5, d6, d7,
    input  wire [W-1:0] dflt,
    output wire [W-1:0] y
);
    wire [W-1:0] l0 = sel[0] ? d1 : d0;
    wire [W-1:0] l1 = sel[0] ? d3 : d2;
    wire [W-1:0] l2 = sel[0] ? d5 : d4;
    wire [W-1:0] l3 = sel[0] ? d7 : d6;

    wire [W-1:0] m0 = sel[1] ? l1 : l0;
    wire [W-1:0] m1 = sel[1] ? l3 : l2;

    assign y = sel[2] ? m1 : m0;
endmodule
