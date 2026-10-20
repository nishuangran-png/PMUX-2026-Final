// 自动生成（scripts/gen_cases.py）：全码覆盖 case，16 路 × 16 位
// 测什么：最基本的常量解码型 PMUX，规模放大到接近千级节点。
module scale_case16 #(parameter W = 16) (
    input  wire [3:0] sel,
    input wire [15:0] d0, input wire [15:0] d1, input wire [15:0] d2, input wire [15:0] d3, input wire [15:0] d4, input wire [15:0] d5, input wire [15:0] d6, input wire [15:0] d7, input wire [15:0] d8, input wire [15:0] d9, input wire [15:0] d10, input wire [15:0] d11, input wire [15:0] d12, input wire [15:0] d13, input wire [15:0] d14, input wire [15:0] d15,
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
endmodule
