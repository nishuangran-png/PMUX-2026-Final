// 等价验证演示 · 优化后实现（gate，故意写错版）
//
// 和 gate_ok.v 唯一的区别：把 d3 和 d5 的位置调换了。
// 肉眼几乎看不出来，但功能已经错了——用来验证 EQY 能不能抓到。

module pmux_demo (
    input  wire [2:0] sel,
    input  wire [7:0] d0, d1, d2, d3, d4, d5, d6, d7,
    output wire [7:0] y
);

    wire [7:0] t01   = sel[0] ? d1   : d0;
    wire [7:0] t23   = sel[0] ? d5   : d2;   // ← 这里应该是 d3
    wire [7:0] t45   = sel[0] ? d3   : d4;   // ← 这里应该是 d5
    wire [7:0] t67   = sel[0] ? d7   : d6;
    wire [7:0] t0123 = sel[1] ? t23  : t01;
    wire [7:0] t4567 = sel[1] ? t67  : t45;

    assign y = sel[2] ? t4567 : t0123;

endmodule
