// 理想实现参照 · pmux_in_alu
//
// 与 rtl/mixed/pmux_in_alu.v 功能完全等价（待 EQY 确认）。
// 原始 case 有 7 个分支（码 0..6）+ default（码 7 用 c），
// 正好凑满 3 位选择码的 8 个取值，所以 default 直接当第 7 片叶子。
//
// ⚠️ 实测这个用例几乎省不下东西：207 → 206。
// 因为算术部分占了 87 个单元，多路选择部分改写前后都是 120 左右——
// ABC 在算术为主的电路里会把这个 mux 树又翻回门级网络。
// 留着这份参照是为了让算法知道「这个方向改不动」，别白费力气。

module pmux_in_alu #(
    parameter W = 8
) (
    input  wire [2:0]   sel,
    input  wire [W-1:0] a, b, c,
    output wire [W-1:0] y
);
    wire [W-1:0] sum  = a + b;
    wire [W-1:0] diff = a - b;
    wire [W-1:0] andv = a & b;
    wire [W-1:0] orv  = a | b;
    wire [W-1:0] xorv = a ^ b;
    wire [W-1:0] cmp  = {W{(a > b)}};

    wire [W-1:0] l0 = sel[0] ? diff : sum;
    wire [W-1:0] l1 = sel[0] ? orv  : andv;
    wire [W-1:0] l2 = sel[0] ? cmp  : xorv;
    wire [W-1:0] l3 = sel[0] ? c    : a;

    wire [W-1:0] m0 = sel[1] ? l1 : l0;
    wire [W-1:0] m1 = sel[1] ? l3 : l2;

    assign y = sel[2] ? m1 : m0;
endmodule
