// 对照用例：case (1'b1) + 位选条件
//
// 硬件语义上这是"多条分支可能同时成立"的并行选择，工具必须按优先级实现，
// 因此退化成 $mux 树 + 大量优先编码逻辑。
//
// 实测：Yosys 0.69 下不生成 $pmux，而是一个 $mux 树；
// 完整 synth 后的门级单元数明显高于等功能的 pmux_basic。
// 这个对比正好说明"控制信号能不能被证明互斥"直接决定面积。

module pmux_priority #(
    parameter W = 8
) (
    input  wire [7:0]   sel,
    input  wire [W-1:0] d0, d1, d2, d3, d4, d5, d6, d7,
    input  wire [W-1:0] dflt,
    output reg  [W-1:0] y
);

    always @* begin
        case (1'b1)
            sel[0]:  y = d0;
            sel[1]:  y = d1;
            sel[2]:  y = d2;
            sel[3]:  y = d3;
            sel[4]:  y = d4;
            sel[5]:  y = d5;
            sel[6]:  y = d6;
            sel[7]:  y = d7;
            default: y = dflt;
        endcase
    end

endmodule
