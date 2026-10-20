// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_024 #(parameter W = 8) (
    input  wire [4:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    input  wire [W-1:0] d4,
    input  wire [W-1:0] d5,
    input  wire [W-1:0] d6,
    input  wire [W-1:0] d7,
    input  wire [W-1:0] d8,
    input  wire [W-1:0] d9,
    input  wire [W-1:0] d10,
    input  wire [W-1:0] d11,
    input  wire [W-1:0] d12,
    output reg [W-1:0] y2,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            5'd1: y = d0;
            5'd4: y = d1;
            5'd7: y = d2;
            5'd9: y = d3;
            5'd10: y = d4;
            5'd13: y = d5;
            5'd16: y = d6;
            5'd18: y = d7;
            5'd19: y = d8;
            5'd21: y = d9;
            5'd23: y = d10;
            5'd24: y = d11;
            5'd28: y = d12;
            default: y = a;
        endcase
    end
    reg [W-1:0] y2;
    always @* begin
        case (~sel)
                5'd1: y2 = d0 | a;
            default: y2 = {W{1'b0}};
        endcase
    end
endmodule
