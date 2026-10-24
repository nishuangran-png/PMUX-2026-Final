// 自动生成（scripts/gen_priority_chains.py）：自行构造的 5 级优先级链
// 不是官方用例，也不是真实设计；只用于长链专项回归。
module priority_chain_05 #(
    parameter W = 16
) (
    input  wire [4:0] sel,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    input  wire [W-1:0] d4,
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
            default: y = dflt;
        endcase
    end
endmodule
