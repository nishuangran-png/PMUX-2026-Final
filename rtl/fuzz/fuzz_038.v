// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_038 #(parameter W = 16) (
    input  wire [3:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            4'd1: y = d0;
            4'd9: y = d1;
            4'd10: y = d2;
            4'd14: y = d3;
            default: y = {W{1'b0}};
        endcase
    end
endmodule
