// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_022 #(parameter W = 8) (
    input  wire [4:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    input  wire [W-1:0] d4,
    input  wire [W-1:0] d5,
    input  wire [W-1:0] d6,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            5'd2: y = d0;
            5'd10: y = d1 ^ a;
            5'd11: y = d2 ^ a;
            5'd14: y = d3;
            5'd18: y = d4 ^ a;
            5'd20: y = d5;
            5'd28: y = d6 ^ a;
            default: y = ~{W{1'b0}};
        endcase
    end
endmodule
