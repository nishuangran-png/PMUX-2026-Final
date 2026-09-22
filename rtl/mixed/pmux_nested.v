// 混合组合逻辑 PMUX 用例：嵌套 PMUX
//
// 内层两组 PMUX 的输出又作为外层 PMUX 的数据输入。
// 上游的 opt_muxtree 是逐层看的，看不到这种跨层结构；
// pmux2shiftx 也只在单层密度足够时才动手。
// 这个用例用来验证"整体识别 + 重构"相对"逐层处理"的增量。

module pmux_nested #(
    parameter W = 8
) (
    input  wire [1:0]   sel_hi,
    input  wire [1:0]   sel_lo0,
    input  wire [1:0]   sel_lo1,
    input  wire [W-1:0] d,
    output reg  [W-1:0] y
);

    reg [W-1:0] inner0;
    reg [W-1:0] inner1;
    reg [W-1:0] inner2;

    // ---- 内层：三个独立的 4 路 PMUX ----
    always @* begin
        case (sel_lo0)
            2'd0:    inner0 = d;
            2'd1:    inner0 = ~d;
            2'd2:    inner0 = d + 8'd1;
            default: inner0 = {W{1'b0}};
        endcase
    end

    always @* begin
        case (sel_lo1)
            2'd0:    inner1 = d;
            2'd1:    inner1 = {d[W-2:0], d[W-1]};
            2'd2:    inner1 = d & {W{1'b1}};
            default: inner1 = {W{1'b1}};
        endcase
    end

    always @* inner2 = d ^ {W{1'b1}};

    // ---- 外层：选择内层结果 ----
    always @* begin
        case (sel_hi)
            2'd0:    y = inner0;
            2'd1:    y = inner1;
            2'd2:    y = inner2;
            default: y = d;
        endcase
    end

endmodule
