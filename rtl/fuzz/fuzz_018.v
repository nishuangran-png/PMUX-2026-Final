// 随机模糊测试用例（scripts/fuzz_equiv.py 生成，勿手改）
module fuzz_018 #(parameter W = 8) (
    input  wire [2:0] sel,
    input  wire [W-1:0] a,
    input  wire [W-1:0] d0,
    input  wire [W-1:0] d1,
    input  wire [W-1:0] d2,
    input  wire [W-1:0] d3,
    output reg [W-1:0] y2,
    output reg [W-1:0] y
);
    always @* begin
        case (sel)
            3'd0: y = d0;
            3'd2: y = d1;
            3'd4: y = d2;
            3'd7: y = d3;
            default: y = a;
        endcase
    end
    reg [W-1:0] y2;
    always @* begin
        case (~sel)
                3'd0: y2 = d0 | a;
                3'd2: y2 = d1 | a;
                3'd4: y2 = d2 | a;
                3'd7: y2 = d3 | a;
            default: y2 = {W{1'b0}};
        endcase
    end
endmodule
