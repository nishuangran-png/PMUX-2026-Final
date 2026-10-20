// 类真实控制器的 FSM：状态转移用 case（时序），输出译码也用 case（组合）。
// 测什么：pass 在「时序 always 块里的 case」上是否也能正确识别与改写
//         （这种 case 同样会生成 $pmux，只是下游接的是 $dff）。
module fsm_ctrl (
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] op,
    input  wire [7:0] flags,
    output reg  [7:0] cmd,
    output reg        busy
);
    localparam S_IDLE  = 3'd0, S_FETCH = 3'd1, S_DEC   = 3'd2,
               S_EXEC  = 3'd3, S_MEM   = 3'd4, S_WB    = 3'd5,
               S_ERR   = 3'd6, S_HALT  = 3'd7;

    reg [2:0] state, next;

    always @* begin
        case (state)
            S_IDLE:  next = flags[0] ? S_FETCH : S_IDLE;
            S_FETCH: next = S_DEC;
            S_DEC:   next = op[7] ? S_ERR : S_EXEC;
            S_EXEC:  next = op[2:0] == 3'd0 ? S_WB : S_MEM;
            S_MEM:   next = flags[1] ? S_WB : S_HALT;
            S_WB:    next = S_FETCH;
            S_ERR:   next = S_HALT;
            default: next = S_HALT;
        endcase
    end

    always @(posedge clk) begin
        if (rst) state <= S_IDLE;
        else     state <= next;
    end

    always @* begin
        case (state)
            S_IDLE:  cmd = 8'h00;
            S_FETCH: cmd = 8'h01;
            S_DEC:   cmd = 8'h02;
            S_EXEC:  cmd = op;
            S_MEM:   cmd = 8'h04;
            S_WB:    cmd = 8'h05;
            S_ERR:   cmd = 8'hEE;
            default: cmd = 8'hFF;
        endcase
        busy = (state != S_IDLE) && (state != S_HALT);
    end
endmodule
