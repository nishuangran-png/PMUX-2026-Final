// 自动生成：两级嵌套 case（外层 8 组 × 组内 8 路，共 64 个数据输入）
// 测什么：层次化 PMUX；规模与 64 路单级相当，但结构更接近真实多级译码。
module scale_nested (
    input  wire [2:0] sel,
    input  wire [2:0] sub,
    input wire [7:0] g0_0, input wire [7:0] g0_1, input wire [7:0] g0_2, input wire [7:0] g0_3, input wire [7:0] g0_4, input wire [7:0] g0_5, input wire [7:0] g0_6, input wire [7:0] g0_7, input wire [7:0] g1_0, input wire [7:0] g1_1, input wire [7:0] g1_2, input wire [7:0] g1_3, input wire [7:0] g1_4, input wire [7:0] g1_5, input wire [7:0] g1_6, input wire [7:0] g1_7, input wire [7:0] g2_0, input wire [7:0] g2_1, input wire [7:0] g2_2, input wire [7:0] g2_3, input wire [7:0] g2_4, input wire [7:0] g2_5, input wire [7:0] g2_6, input wire [7:0] g2_7, input wire [7:0] g3_0, input wire [7:0] g3_1, input wire [7:0] g3_2, input wire [7:0] g3_3, input wire [7:0] g3_4, input wire [7:0] g3_5, input wire [7:0] g3_6, input wire [7:0] g3_7, input wire [7:0] g4_0, input wire [7:0] g4_1, input wire [7:0] g4_2, input wire [7:0] g4_3, input wire [7:0] g4_4, input wire [7:0] g4_5, input wire [7:0] g4_6, input wire [7:0] g4_7, input wire [7:0] g5_0, input wire [7:0] g5_1, input wire [7:0] g5_2, input wire [7:0] g5_3, input wire [7:0] g5_4, input wire [7:0] g5_5, input wire [7:0] g5_6, input wire [7:0] g5_7, input wire [7:0] g6_0, input wire [7:0] g6_1, input wire [7:0] g6_2, input wire [7:0] g6_3, input wire [7:0] g6_4, input wire [7:0] g6_5, input wire [7:0] g6_6, input wire [7:0] g6_7, input wire [7:0] g7_0, input wire [7:0] g7_1, input wire [7:0] g7_2, input wire [7:0] g7_3, input wire [7:0] g7_4, input wire [7:0] g7_5, input wire [7:0] g7_6, input wire [7:0] g7_7,
    output reg  [7:0] y
);
    always @* begin
        case (sel)
            3'd0: begin
                case (sub)
                3'd0: y = g0_0;
                3'd1: y = g0_1;
                3'd2: y = g0_2;
                3'd3: y = g0_3;
                3'd4: y = g0_4;
                3'd5: y = g0_5;
                3'd6: y = g0_6;
                3'd7: y = g0_7;
                    default: y = 8'h00;
                endcase
            end
            3'd1: begin
                case (sub)
                3'd0: y = g1_0;
                3'd1: y = g1_1;
                3'd2: y = g1_2;
                3'd3: y = g1_3;
                3'd4: y = g1_4;
                3'd5: y = g1_5;
                3'd6: y = g1_6;
                3'd7: y = g1_7;
                    default: y = 8'h00;
                endcase
            end
            3'd2: begin
                case (sub)
                3'd0: y = g2_0;
                3'd1: y = g2_1;
                3'd2: y = g2_2;
                3'd3: y = g2_3;
                3'd4: y = g2_4;
                3'd5: y = g2_5;
                3'd6: y = g2_6;
                3'd7: y = g2_7;
                    default: y = 8'h00;
                endcase
            end
            3'd3: begin
                case (sub)
                3'd0: y = g3_0;
                3'd1: y = g3_1;
                3'd2: y = g3_2;
                3'd3: y = g3_3;
                3'd4: y = g3_4;
                3'd5: y = g3_5;
                3'd6: y = g3_6;
                3'd7: y = g3_7;
                    default: y = 8'h00;
                endcase
            end
            3'd4: begin
                case (sub)
                3'd0: y = g4_0;
                3'd1: y = g4_1;
                3'd2: y = g4_2;
                3'd3: y = g4_3;
                3'd4: y = g4_4;
                3'd5: y = g4_5;
                3'd6: y = g4_6;
                3'd7: y = g4_7;
                    default: y = 8'h00;
                endcase
            end
            3'd5: begin
                case (sub)
                3'd0: y = g5_0;
                3'd1: y = g5_1;
                3'd2: y = g5_2;
                3'd3: y = g5_3;
                3'd4: y = g5_4;
                3'd5: y = g5_5;
                3'd6: y = g5_6;
                3'd7: y = g5_7;
                    default: y = 8'h00;
                endcase
            end
            3'd6: begin
                case (sub)
                3'd0: y = g6_0;
                3'd1: y = g6_1;
                3'd2: y = g6_2;
                3'd3: y = g6_3;
                3'd4: y = g6_4;
                3'd5: y = g6_5;
                3'd6: y = g6_6;
                3'd7: y = g6_7;
                    default: y = 8'h00;
                endcase
            end
            3'd7: begin
                case (sub)
                3'd0: y = g7_0;
                3'd1: y = g7_1;
                3'd2: y = g7_2;
                3'd3: y = g7_3;
                3'd4: y = g7_4;
                3'd5: y = g7_5;
                3'd6: y = g7_6;
                3'd7: y = g7_7;
                    default: y = 8'h00;
                endcase
            end
            default: y = 8'h00;
        endcase
    end
endmodule
