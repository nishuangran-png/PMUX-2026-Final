// 自动生成：类 ALU，4 位操作码选 8 种操作，运算结果再经输出 case
// 测什么：PMUX 与算术逻辑混排（选题要求「支持混合组合逻辑 PMUX」）。
module scale_alu #(parameter W = 16) (
    input  wire [3:0]  op,
    input  wire [W-1:0] a, b,
    output reg  [W-1:0] y
);
    wire [W-1:0] add_r = a + b;
    wire [W-1:0] sub_r = a - b;
    wire [W-1:0] and_r = a & b;
    wire [W-1:0] or_r  = a | b;
    wire [W-1:0] xor_r = a ^ b;
    wire [W-1:0] sll_r = a << b[3:0];
    wire [W-1:0] srl_r = a >> b[3:0];
    wire [W-1:0] slt_r = { {(W-1){1'b0}}, (a < b) };

    always @* begin
        case (op)
            4'd0: y = add_r;
            4'd1: y = sub_r;
            4'd2: y = and_r;
            4'd3: y = or_r;
            4'd4: y = xor_r;
            4'd5: y = sll_r;
            4'd6: y = srl_r;
            4'd7: y = slt_r;
            default: y = {W{1'b0}};
        endcase
    end
endmodule
