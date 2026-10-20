// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_005 #(parameter W = 16) (
    input  wire [2:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            3'd7: y = d0;
            default: y = a;
        endcase
    end
endmodule
