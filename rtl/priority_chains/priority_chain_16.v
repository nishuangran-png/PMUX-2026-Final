// 自动生成（scripts/gen_priority_chains.py）：自行构造的 16 级优先级链
// 不是官方用例，也不是真实设计；只用于长链专项回归。
module priority_chain_16 #(
    parameter W = 16
) (
    input  wire [15:0] sel,
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
    input  wire [W-1:0] dflt,
    output reg  [W-1:0] y
);
    always @* begin
        case (1'b1)
            sel[0]: y = d0;
            sel[1]: y = d1;
            sel[2]: y = d2;
            sel[3]: y = d3;
            sel[4]: y = d4;
            sel[5]: y = d5;
            sel[6]: y = d6;
            sel[7]: y = d7;
            sel[8]: y = d8;
            sel[9]: y = d9;
            sel[10]: y = d10;
            sel[11]: y = d11;
            sel[12]: y = d12;
            sel[13]: y = d13;
            sel[14]: y = d14;
            sel[15]: y = d15;
            default: y = dflt;
        endcase
    end
endmodule
