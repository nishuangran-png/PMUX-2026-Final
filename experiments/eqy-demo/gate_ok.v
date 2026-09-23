// 等价验证演示 · 优化后实现（gate，正确版）
//
// 同一个 8 选 1，但用嵌套三目运算符显式写成 mux 树。
// 结构和 gold 完全不同，功能应当完全一致。

module pmux_demo (
    input  wire [2:0] sel,
    input  wire [7:0] d0, d1, d2, d3, d4, d5, d6, d7,
    output wire [7:0] y
);

    wire [7:0] t01   = sel[0] ? d1   : d0;
    wire [7:0] t23   = sel[0] ? d3   : d2;
    wire [7:0] t45   = sel[0] ? d5   : d4;
    wire [7:0] t67   = sel[0] ? d7   : d6;
    wire [7:0] t0123 = sel[1] ? t23  : t01;
    wire [7:0] t4567 = sel[1] ? t67  : t45;

    assign y = sel[2] ? t4567 : t0123;

endmodule
