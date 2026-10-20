// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_066 #(parameter W = 16) (
    input  wire [2:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            3'd2: y = d0;
            3'd3: y = d1;
            3'd4: y = d2;
            default: y = ~{W{1'b0}};
        endcase
    end
endmodule
