// 自动生成：稀疏 case，5 位选择里只列 12 个码（20 个缺码走 default）
// 测什么：多缺码场景（旧版插件只支持缺 1 个码，会跳过这类）。
module scale_sparse32 #(parameter W = 16) (
    input  wire [4:0] sel,
    input wire [15:0] d0, input wire [15:0] d1, input wire [15:0] d2, input wire [15:0] d3, input wire [15:0] d4, input wire [15:0] d5, input wire [15:0] d6, input wire [15:0] d7, input wire [15:0] d8, input wire [15:0] d9, input wire [15:0] d10, input wire [15:0] d11,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            5'd0: y = d0;
            5'd1: y = d1;
            5'd2: y = d2;
            5'd3: y = d3;
            5'd4: y = d4;
            5'd5: y = d5;
            5'd6: y = d6;
            5'd7: y = d7;
            5'd8: y = d8;
            5'd9: y = d9;
            5'd10: y = d10;
            5'd11: y = d11;
            default: y = {W{1'b1}};
        endcase
    end
endmodule
