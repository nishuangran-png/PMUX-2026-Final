// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_073 #(parameter W = 16) (
    input  wire [4:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    input  wire [W-1:0] d4,
    input  wire [W-1:0] d5,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            5'd6: y = d0;
            5'd14: y = d1;
            5'd16: y = d2;
            5'd18: y = d3;
            5'd23: y = d4;
            5'd29: y = d5;
            default: y = {W{1'b0}};
        endcase
    end
endmodule
