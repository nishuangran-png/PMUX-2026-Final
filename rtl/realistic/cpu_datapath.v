// 类真实数据通路：ALU 操作选择 + 写回旁路 + 移位量译码，都是一路 case。
// 测什么：多个功能不同的 PMUX 混在一条数据通路上（含算术与比较），
//         算法能不能都吃掉、又不出错。
module cpu_datapath #(parameter W = 16) (
    input  wire [3:0]  alu_op,
    input  wire [2:0]  wb_sel,
    input  wire [3:0]  sh_amt,
    input  wire [W-1:0] rs1, rs2, imm, mem, pc,
    output reg  [W-1:0] result,
    output wire        zero
);
    wire [W-1:0] add = rs1 + rs2;
    wire [W-1:0] sub = rs1 - rs2;
    wire [W-1:0] and_ = rs1 & rs2;
    wire [W-1:0] or_  = rs1 | rs2;
    wire [W-1:0] xor_ = rs1 ^ rs2;
    wire [W-1:0] slt = {{(W-1){1'b0}}, (rs1 < rs2)};
    wire [W-1:0] sll = rs1 << sh_amt;
    wire [W-1:0] srl = rs1 >> sh_amt;
    wire [W-1:0] sra = $signed(rs1) >>> sh_amt;

    always @* begin
        case (alu_op)
            4'd0: result = add;
            4'd1: result = sub;
            4'd2: result = and_;
            4'd3: result = or_;
            4'd4: result = xor_;
            4'd5: result = slt;
            4'd6: result = sll;
            4'd7: result = srl;
            4'd8: result = sra;
            default: result = {W{1'b0}};
        endcase
    end

    reg [W-1:0] wb;
    always @* begin
        case (wb_sel)
            3'd0: wb = result;
            3'd1: wb = mem;
            3'd2: wb = imm;
            3'd3: wb = pc + 16'd4;
            default: wb = {W{1'b0}};
        endcase
    end

    assign zero = (wb == {W{1'b0}});
endmodule
