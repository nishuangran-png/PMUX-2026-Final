// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_030 #(parameter W = 16) (
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
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            5'd2: y = d0;
            5'd3: y = d1;
            5'd4: y = d2;
            5'd8: y = d3;
            5'd14: y = d4;
            5'd16: y = d5;
            5'd19: y = d6;
            5'd24: y = d7;
            5'd28: y = d8;
            5'd29: y = d9;
            default: y = a;
        endcase
    end
endmodule
