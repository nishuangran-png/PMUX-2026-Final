// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_059 #(parameter W = 8) (
    input  wire [2:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            3'd1: y = d0;
            3'd4: y = d1;
            default: y = ~{W{1'b0}};
        endcase
    end
endmodule
