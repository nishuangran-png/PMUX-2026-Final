// 对照用例：8 选 1 二叉树
//
// 用嵌套三目运算符显式写成 2:1 树。结构干净、条件互斥，
// 工具会推断成 $mux 树。放在这里是为了和 $pmux 用例对比单元数，
// 判断"同样的功能，两种结构差多少资源"。

module mux_tree_8to1 #(
    parameter W = 8
) (
    input  wire [2:0]   s,
    input  wire [W-1:0] d0, d1, d2, d3, d4, d5, d6, d7,
    output wire [W-1:0] y
);

    wire [W-1:0] t01   = s[0] ? d1   : d0;
    wire [W-1:0] t23   = s[0] ? d3   : d2;
    wire [W-1:0] t45   = s[0] ? d5   : d4;
    wire [W-1:0] t67   = s[0] ? d7   : d6;

    wire [W-1:0] t0123 = s[1] ? t23  : t01;
    wire [W-1:0] t4567 = s[1] ? t67  : t45;

    assign y = s[2] ? t4567 : t0123;

endmodule
