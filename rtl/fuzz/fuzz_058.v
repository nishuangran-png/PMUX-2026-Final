// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_058 #(parameter W = 8) (
    input  wire [4:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    input  wire [W-1:0] d4,
    input  wire [W-1:0] d5,
    input  wire [W-1:0] d6,
    input  wire [W-1:0] d7,
    input  wire [W-1:0] d8,
    input  wire [W-1:0] d9,
    input  wire [W-1:0] d10,
    input  wire [W-1:0] d11,
    input  wire [W-1:0] d12,
    input  wire [W-1:0] d13,
    input  wire [W-1:0] d14,
    input  wire [W-1:0] d15,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            5'd0: y = d0;
            5'd1: y = d1;
            5'd2: y = d2 ^ a;
            5'd3: y = d3;
            5'd5: y = d4;
            5'd6: y = d5;
            5'd8: y = d6 ^ a;
            5'd10: y = d7;
            5'd11: y = d8 ^ a;
            5'd16: y = d9 ^ a;
            5'd19: y = d10;
            5'd21: y = d11 ^ a;
            5'd24: y = d12;
            5'd27: y = d13;
            5'd28: y = d14 ^ a;
            5'd29: y = d15 ^ a;
            default: y = {W{1'b0}};
        endcase
    end
endmodule
