// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_054 #(parameter W = 4) (
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
    input  wire [W-1:0] d16,
    input  wire [W-1:0] d17,
    input  wire [W-1:0] d18,
    input  wire [W-1:0] d19,
    input  wire [W-1:0] d20,
    input  wire [W-1:0] d21,
    input  wire [W-1:0] d22,
    input  wire [W-1:0] d23,
    input  wire [W-1:0] d24,
    input  wire [W-1:0] d25,
    input  wire [W-1:0] d26,
    input  wire [W-1:0] d27,
    input  wire [W-1:0] d28,
    input  wire [W-1:0] d29,
    input  wire [W-1:0] d30,
    input  wire [W-1:0] d31,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            5'd0: y = d0 ^ a;
            5'd1: y = d1 ^ a;
            5'd2: y = d2 ^ a;
            5'd3: y = d3;
            5'd4: y = d4;
            5'd5: y = d5;
            5'd6: y = d6 ^ a;
            5'd7: y = d7 ^ a;
            5'd8: y = d8;
            5'd9: y = d9;
            5'd10: y = d10 ^ a;
            5'd11: y = d11 ^ a;
            5'd12: y = d12 ^ a;
            5'd13: y = d13 ^ a;
            5'd14: y = d14 ^ a;
            5'd15: y = d15;
            5'd16: y = d16 ^ a;
            5'd17: y = d17;
            5'd18: y = d18 ^ a;
            5'd19: y = d19 ^ a;
            5'd20: y = d20;
            5'd21: y = d21;
            5'd22: y = d22 ^ a;
            5'd23: y = d23;
            5'd24: y = d24 ^ a;
            5'd25: y = d25;
            5'd26: y = d26;
            5'd27: y = d27;
            5'd28: y = d28;
            5'd29: y = d29;
            5'd30: y = d30;
            5'd31: y = d31;
            default: y = {W{1'b0}};
        endcase
    end
endmodule
