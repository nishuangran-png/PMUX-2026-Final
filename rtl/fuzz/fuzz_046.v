// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_046 #(parameter W = 4) (
    input  wire [3:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            4'd1: y = d0;
            4'd2: y = d1;
            default: y = a;
        endcase
    end
endmodule
