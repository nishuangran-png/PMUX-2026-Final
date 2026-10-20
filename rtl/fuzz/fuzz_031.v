// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_031 #(parameter W = 8) (
    input  wire [3:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            4'd0: y = d0 ^ a;
            4'd3: y = d1;
            default: y = ~{W{1'b0}};
        endcase
    end
endmodule
