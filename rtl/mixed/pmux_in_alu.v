// 混合组合逻辑 PMUX 用例：选择分支里是算术逻辑的输出
//
// 八个分支的数据不是裸输入，而是加法器、减法器、按位与、比较器的结果。
// 这类结构是官方指标里「兼容混合组合逻辑 PMUX 的复杂电路」所指的形态，
// 也是上游 pass 容易留白的地方：选择逻辑和数据通路缠在一起，
// 单纯做 mux 结构替换会波及旁边的算术逻辑。

module pmux_in_alu #(
    parameter W = 8
) (
    input  wire [2:0]   sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] b,
    input  wire [W-1:0] c,
    output reg  [W-1:0] y
);

    wire [W-1:0] sum  = a + b;
    wire [W-1:0] diff = a - b;
    wire [W-1:0] andv = a & b;
    wire [W-1:0] orv  = a | b;
    wire [W-1:0] xorv = a ^ b;
    wire         cmp  = (a > b);

    always @* begin
        case (sel)
            3'd0:    y = sum;
            3'd1:    y = diff;
            3'd2:    y = andv;
            3'd3:    y = orv;
            3'd4:    y = xorv;
            3'd5:    y = {W{cmp}};
            3'd6:    y = a;
            default: y = c;
        endcase
    end

endmodule
