// 自动生成（scripts/gen_cases.py）：全码覆盖 case，8 路 × 8 位
// 测什么：最基本的常量解码型 PMUX，规模放大到接近千级节点。
module scale_case8 #(parameter W = 8) (
    input  wire [2:0] sel,
    input wire [7:0] d0, input wire [7:0] d1, input wire [7:0] d2, input wire [7:0] d3, input wire [7:0] d4, input wire [7:0] d5, input wire [7:0] d6, input wire [7:0] d7,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            3'd0: y = d0;
            3'd1: y = d1;
            3'd2: y = d2;
            3'd3: y = d3;
            3'd4: y = d4;
            3'd5: y = d5;
            3'd6: y = d6;
            3'd7: y = d7;
            default: y = {W{1'b0}};
        endcase
    end
endmodule
