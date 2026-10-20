// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_017 #(parameter W = 16) (
    input  wire [3:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    input  wire [W-1:0] d4,
    input  wire [W-1:0] d5,
    input  wire [W-1:0] d6,
    output reg [W-1:0] y2,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            4'd1: y = d0;
            4'd2: y = d1;
            4'd5: y = d2;
            4'd8: y = d3;
            4'd12: y = d4;
            4'd13: y = d5;
            4'd14: y = d6;
            default: y = a;
        endcase
    end
    reg [W-1:0] y2;
    always @* begin
        case (~sel)
                4'd1: y2 = d0 | a;
                4'd2: y2 = d1 | a;
            default: y2 = {W{1'b0}};
        endcase
    end
endmodule
