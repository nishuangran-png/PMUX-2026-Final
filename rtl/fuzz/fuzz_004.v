// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_004 #(parameter W = 4) (
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
    input  wire [W-1:0] d13,
    input  wire [W-1:0] d14,
    input  wire [W-1:0] d15,
    output reg [W-1:0] y2,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            5'd0: y = d0;
            5'd1: y = d1;
            5'd3: y = d2;
            5'd6: y = d3;
            5'd9: y = d4;
            5'd11: y = d5;
            5'd12: y = d6;
            5'd16: y = d7;
            5'd17: y = d8;
            5'd18: y = d9;
            5'd20: y = d10;
            5'd21: y = d11;
            5'd22: y = d12;
            5'd28: y = d13;
            5'd29: y = d14;
            5'd30: y = d15;
            default: y = {W{1'b0}};
        endcase
    end
    reg [W-1:0] y2;
    always @* begin
        case (~sel)
                5'd0: y2 = d0 | a;
                5'd1: y2 = d1 | a;
                5'd3: y2 = d2 | a;
                5'd6: y2 = d3 | a;
                5'd9: y2 = d4 | a;
                5'd11: y2 = d5 | a;
                5'd12: y2 = d6 | a;
                5'd16: y2 = d7 | a;
                5'd17: y2 = d8 | a;
                5'd18: y2 = d9 | a;
                5'd20: y2 = d10 | a;
            default: y2 = {W{1'b0}};
        endcase
    end
endmodule
