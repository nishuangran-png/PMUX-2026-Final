// 理想实现参照 · pmux_nested
//
// 与 rtl/mixed/pmux_nested.v 功能完全等价（待 EQY 确认）。
// 内层三个 4 选 1 和外层 1 个 4 选 1 全部改成位选树。
// 实测 112 → 105（−6.2%），省下的是内层那三个比较器组。
//
// 注意 inner1 的原写法 `2'd2: inner1 = d & {W{1'b1}}` 就等于 d，
// 这里保持同样的语义（写成 d），不要"顺手修正"成别的。

module pmux_nested #(
    parameter W = 8
) (
    input  wire [1:0]   sel_hi,
    input  wire [1:0]   sel_lo0,
    input  wire [1:0]   sel_lo1,
    input  wire [W-1:0] d,
    output wire [W-1:0] y
);
    wire [W-1:0] notd  = ~d;
    wire [W-1:0] plus1 = d + 8'd1;
    wire [W-1:0] rot   = {d[W-2:0], d[W-1]};
    wire [W-1:0] all1  = {W{1'b1}};
    wire [W-1:0] xord  = d ^ {W{1'b1}};

    // 内层 0：码 0..2 = d, ~d, d+1；码 3（default）= 0
    wire [W-1:0] i0a = sel_lo0[0] ? notd      : d;
    wire [W-1:0] i0b = sel_lo0[0] ? {W{1'b0}} : plus1;
    wire [W-1:0] inner0 = sel_lo0[1] ? i0b : i0a;

    // 内层 1：码 0..2 = d, rot, d；码 3（default）= 全 1
    wire [W-1:0] i1a = sel_lo1[0] ? rot  : d;
    wire [W-1:0] i1b = sel_lo1[0] ? all1 : d;
    wire [W-1:0] inner1 = sel_lo1[1] ? i1b : i1a;

    wire [W-1:0] inner2 = xord;

    // 外层：码 0..2 = inner0, inner1, inner2；码 3（default）= d
    wire [W-1:0] oa = sel_hi[0] ? inner1 : inner0;
    wire [W-1:0] ob = sel_hi[0] ? d      : inner2;
    assign y = sel_hi[1] ? ob : oa;
endmodule
