// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_056 #(parameter W = 8) (
    input  wire [3:0] sel,
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
            4'd0: y = d0;
            4'd1: y = d1;
            4'd2: y = d2;
            4'd3: y = d3;
            4'd4: y = d4;
            4'd5: y = d5;
            4'd6: y = d6;
            4'd7: y = d7;
            4'd8: y = d8;
            4'd9: y = d9;
            4'd10: y = d10;
            4'd11: y = d11;
            4'd12: y = d12;
            4'd13: y = d13;
            4'd14: y = d14;
            4'd15: y = d15;
            default: y = {W{1'b0}};
        endcase
    end
    reg [W-1:0] y2;
    always @* begin
        case (~sel)
                4'd0: y2 = d0 | a;
                4'd1: y2 = d1 | a;
                4'd2: y2 = d2 | a;
                4'd3: y2 = d3 | a;
                4'd4: y2 = d4 | a;
                4'd5: y2 = d5 | a;
                4'd6: y2 = d6 | a;
                4'd7: y2 = d7 | a;
                4'd8: y2 = d8 | a;
                4'd9: y2 = d9 | a;
                4'd10: y2 = d10 | a;
                4'd11: y2 = d11 | a;
                4'd12: y2 = d12 | a;
            default: y2 = {W{1'b0}};
        endcase
    end
endmodule
