// 单级 PMUX 基准用例：8 路并行选择
//
// 关键结论（0.68 / 0.69 实测一致）：只有 case (sel) 这种"选择信号对常量分支"的
// 写法才会生成 $pmux 单元。写成 case (1'b1) + sel[i] 反而会退化成带优先级的
// $mux 树（见 pmux_priority.v），单元数更多。
//
// 这个用例就是本选题的基准结构：一个 $pmux，8 个数据分支 + 默认值。

module pmux_basic #(
    parameter W = 8
) (
    input  wire [2:0]   sel,
    input  wire [W-1:0] d0, d1, d2, d3, d4, d5, d6, d7,
    input  wire [W-1:0] dflt,
    output reg  [W-1:0] y
);

    always @* begin
        case (sel)
            3'd0:    y = d0;
            3'd1:    y = d1;
            3'd2:    y = d2;
            3'd3:    y = d3;
            3'd4:    y = d4;
            3'd5:    y = d5;
            3'd6:    y = d6;
            3'd7:    y = d7;
            default: y = dflt;
        endcase
    end

endmodule
