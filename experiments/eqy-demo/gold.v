// 等价验证演示 · 基准实现（gold）
//
// 用 case (sel) 写法，会生成 $pmux 单元——这就是本选题的原始结构。
// 功能：8 选 1，选择信号 sel 覆盖 0~7。

module pmux_demo (
    input  wire [2:0] sel,
    input  wire [7:0] d0, d1, d2, d3, d4, d5, d6, d7,
    output reg  [7:0] y
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
            default: y = 8'h00;
        endcase
    end

endmodule
