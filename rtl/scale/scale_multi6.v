// 自动生成：6 个并行 case mux 共享 3 位选择总线，每个 8 路 × 8 位
// 测什么：多个 PMUX 同时存在时，算法是否都能吃掉；总规模接近千级节点。
module scale_multi6 (
    input  wire [2:0] sel,
    output reg [7:0] y0,    input  wire [7:0] a0_0,    input  wire [7:0] a0_1,    input  wire [7:0] a0_2,    input  wire [7:0] a0_3,    input  wire [7:0] a0_4,    input  wire [7:0] a0_5,    input  wire [7:0] a0_6,    input  wire [7:0] a0_7,    output reg [7:0] y1,    input  wire [7:0] a1_0,    input  wire [7:0] a1_1,    input  wire [7:0] a1_2,    input  wire [7:0] a1_3,    input  wire [7:0] a1_4,    input  wire [7:0] a1_5,    input  wire [7:0] a1_6,    input  wire [7:0] a1_7,    output reg [7:0] y2,    input  wire [7:0] a2_0,    input  wire [7:0] a2_1,    input  wire [7:0] a2_2,    input  wire [7:0] a2_3,    input  wire [7:0] a2_4,    input  wire [7:0] a2_5,    input  wire [7:0] a2_6,    input  wire [7:0] a2_7,    output reg [7:0] y3,    input  wire [7:0] a3_0,    input  wire [7:0] a3_1,    input  wire [7:0] a3_2,    input  wire [7:0] a3_3,    input  wire [7:0] a3_4,    input  wire [7:0] a3_5,    input  wire [7:0] a3_6,    input  wire [7:0] a3_7,    output reg [7:0] y4,    input  wire [7:0] a4_0,    input  wire [7:0] a4_1,    input  wire [7:0] a4_2,    input  wire [7:0] a4_3,    input  wire [7:0] a4_4,    input  wire [7:0] a4_5,    input  wire [7:0] a4_6,    input  wire [7:0] a4_7,    output reg [7:0] y5,    input  wire [7:0] a5_0,    input  wire [7:0] a5_1,    input  wire [7:0] a5_2,    input  wire [7:0] a5_3,    input  wire [7:0] a5_4,    input  wire [7:0] a5_5,    input  wire [7:0] a5_6,    input  wire [7:0] a5_7
);
    always @* begin
        case (sel)
                3'd0: y0 = a0_0;
                3'd1: y0 = a0_1;
                3'd2: y0 = a0_2;
                3'd3: y0 = a0_3;
                3'd4: y0 = a0_4;
                3'd5: y0 = a0_5;
                3'd6: y0 = a0_6;
                3'd7: y0 = a0_7;
            default: y0 = 8'h0;
        endcase
        case (sel)
                3'd0: y1 = a1_0;
                3'd1: y1 = a1_1;
                3'd2: y1 = a1_2;
                3'd3: y1 = a1_3;
                3'd4: y1 = a1_4;
                3'd5: y1 = a1_5;
                3'd6: y1 = a1_6;
                3'd7: y1 = a1_7;
            default: y1 = 8'h11;
        endcase
        case (sel)
                3'd0: y2 = a2_0;
                3'd1: y2 = a2_1;
                3'd2: y2 = a2_2;
                3'd3: y2 = a2_3;
                3'd4: y2 = a2_4;
                3'd5: y2 = a2_5;
                3'd6: y2 = a2_6;
                3'd7: y2 = a2_7;
            default: y2 = 8'h22;
        endcase
        case (sel)
                3'd0: y3 = a3_0;
                3'd1: y3 = a3_1;
                3'd2: y3 = a3_2;
                3'd3: y3 = a3_3;
                3'd4: y3 = a3_4;
                3'd5: y3 = a3_5;
                3'd6: y3 = a3_6;
                3'd7: y3 = a3_7;
            default: y3 = 8'h33;
        endcase
        case (sel)
                3'd0: y4 = a4_0;
                3'd1: y4 = a4_1;
                3'd2: y4 = a4_2;
                3'd3: y4 = a4_3;
                3'd4: y4 = a4_4;
                3'd5: y4 = a4_5;
                3'd6: y4 = a4_6;
                3'd7: y4 = a4_7;
            default: y4 = 8'h44;
        endcase
        case (sel)
                3'd0: y5 = a5_0;
                3'd1: y5 = a5_1;
                3'd2: y5 = a5_2;
                3'd3: y5 = a5_3;
                3'd4: y5 = a5_4;
                3'd5: y5 = a5_5;
                3'd6: y5 = a5_6;
                3'd7: y5 = a5_7;
            default: y5 = 8'h55;
        endcase
    end
endmodule
