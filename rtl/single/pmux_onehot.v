// 单级 PMUX 用例：选择信号由前面的逻辑算出来
//
// 和 pmux_basic 同样是 case (sel) 结构，但 sel 不是裸输入，而是由 addr 经
// 一小段组合逻辑产生。用来观察：当选择信号带着"上游逻辑的痕迹"时，
// 识别和重构会不会变难。

module pmux_onehot #(
    parameter W = 8
) (
    input  wire [2:0]   addr,
    input  wire [W-1:0] d0, d1, d2, d3, d4, d5, d6, d7,
    output reg  [W-1:0] y
);

    // 选择值 = addr 与常量的组合，故意不是裸输入
    wire [2:0] sel = addr ^ 3'b101;

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
            default: y = {W{1'b0}};
        endcase
    end

endmodule
