// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_001 #(parameter W = 16) (
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
            4'd1: y = d1;
            4'd3: y = d2;
            4'd6: y = d3;
            4'd9: y = d4;
            4'd10: y = d5;
            4'd12: y = d6;
            4'd15: y = d7;
            default: y = ~{W{1'b0}};
        endcase
    end
endmodule
