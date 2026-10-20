// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_075 #(parameter W = 8) (
    input  wire [4:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            5'd4: y = d0;
            5'd7: y = d1;
            5'd13: y = d2;
            5'd22: y = d3;
            default: y = ~{W{1'b0}};
        endcase
    end
endmodule
