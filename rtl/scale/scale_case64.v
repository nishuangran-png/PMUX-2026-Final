// 自动生成（scripts/gen_cases.py）：全码覆盖 case，64 路 × 8 位
// 测什么：最基本的常量解码型 PMUX，规模放大到接近千级节点。
module scale_case64 #(parameter W = 8) (
    input  wire [5:0] sel,
    input wire [7:0] d0, input wire [7:0] d1, input wire [7:0] d2, input wire [7:0] d3, input wire [7:0] d4, input wire [7:0] d5, input wire [7:0] d6, input wire [7:0] d7, input wire [7:0] d8, input wire [7:0] d9, input wire [7:0] d10, input wire [7:0] d11, input wire [7:0] d12, input wire [7:0] d13, input wire [7:0] d14, input wire [7:0] d15, input wire [7:0] d16, input wire [7:0] d17, input wire [7:0] d18, input wire [7:0] d19, input wire [7:0] d20, input wire [7:0] d21, input wire [7:0] d22, input wire [7:0] d23, input wire [7:0] d24, input wire [7:0] d25, input wire [7:0] d26, input wire [7:0] d27, input wire [7:0] d28, input wire [7:0] d29, input wire [7:0] d30, input wire [7:0] d31, input wire [7:0] d32, input wire [7:0] d33, input wire [7:0] d34, input wire [7:0] d35, input wire [7:0] d36, input wire [7:0] d37, input wire [7:0] d38, input wire [7:0] d39, input wire [7:0] d40, input wire [7:0] d41, input wire [7:0] d42, input wire [7:0] d43, input wire [7:0] d44, input wire [7:0] d45, input wire [7:0] d46, input wire [7:0] d47, input wire [7:0] d48, input wire [7:0] d49, input wire [7:0] d50, input wire [7:0] d51, input wire [7:0] d52, input wire [7:0] d53, input wire [7:0] d54, input wire [7:0] d55, input wire [7:0] d56, input wire [7:0] d57, input wire [7:0] d58, input wire [7:0] d59, input wire [7:0] d60, input wire [7:0] d61, input wire [7:0] d62, input wire [7:0] d63,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            6'd0: y = d0;
            6'd1: y = d1;
            6'd2: y = d2;
            6'd3: y = d3;
            6'd4: y = d4;
            6'd5: y = d5;
            6'd6: y = d6;
            6'd7: y = d7;
            6'd8: y = d8;
            6'd9: y = d9;
            6'd10: y = d10;
            6'd11: y = d11;
            6'd12: y = d12;
            6'd13: y = d13;
            6'd14: y = d14;
            6'd15: y = d15;
            6'd16: y = d16;
            6'd17: y = d17;
            6'd18: y = d18;
            6'd19: y = d19;
            6'd20: y = d20;
            6'd21: y = d21;
            6'd22: y = d22;
            6'd23: y = d23;
            6'd24: y = d24;
            6'd25: y = d25;
            6'd26: y = d26;
            6'd27: y = d27;
            6'd28: y = d28;
            6'd29: y = d29;
            6'd30: y = d30;
            6'd31: y = d31;
            6'd32: y = d32;
            6'd33: y = d33;
            6'd34: y = d34;
            6'd35: y = d35;
            6'd36: y = d36;
            6'd37: y = d37;
            6'd38: y = d38;
            6'd39: y = d39;
            6'd40: y = d40;
            6'd41: y = d41;
            6'd42: y = d42;
            6'd43: y = d43;
            6'd44: y = d44;
            6'd45: y = d45;
            6'd46: y = d46;
            6'd47: y = d47;
            6'd48: y = d48;
            6'd49: y = d49;
            6'd50: y = d50;
            6'd51: y = d51;
            6'd52: y = d52;
            6'd53: y = d53;
            6'd54: y = d54;
            6'd55: y = d55;
            6'd56: y = d56;
            6'd57: y = d57;
            6'd58: y = d58;
            6'd59: y = d59;
            6'd60: y = d60;
            6'd61: y = d61;
            6'd62: y = d62;
            6'd63: y = d63;
            default: y = {W{1'b0}};
        endcase
    end
endmodule
