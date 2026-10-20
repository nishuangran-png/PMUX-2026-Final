// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_009 #(parameter W = 4) (
    input  wire [3:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    input  wire [W-1:0] d4,
    input  wire [W-1:0] d5,
    input  wire [W-1:0] d6,
    input  wire [W-1:0] d7,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            4'd0: y = d0;
            4'd1: y = d1 ^ a;
            4'd3: y = d2 ^ a;
            4'd5: y = d3 ^ a;
            4'd7: y = d4 ^ a;
            4'd8: y = d5;
            4'd10: y = d6;
            4'd11: y = d7 ^ a;
            default: y = a;
        endcase
    end
endmodule
