module test2_orig (
    input a, b, c, d,
    input [4:0] dataa, datab, datac, datad,
    output reg [4:0] out
);
    always @* begin
        case (1'b1)
            a: out = dataa;
            b: out = datab;
            c: out = datac;
            d: out = datad;
            default: out = 5'b0;
        endcase
    end
endmodule

module test2_balanced (
    input a, b, c, d,
    input [4:0] dataa, datab, datac, datad,
    output [4:0] out
);
    wire ab = a | b;
    wire cd = c | d;
    wire [4:0] mab = a ? dataa : datab;
    wire [4:0] mcd = c ? datac : datad;
    assign out = ab ? mab : (cd ? mcd : 5'b0);
endmodule

module test2_casez (
    input a, b, c, d,
    input [4:0] dataa, datab, datac, datad,
    output reg [4:0] out
);
    always @* begin
        casez ({a, b, c, d})
            4'b1???: out = dataa;
            4'b01??: out = datab;
            4'b001?: out = datac;
            4'b0001: out = datad;
            default: out = 5'b0;
        endcase
    end
endmodule

module test2_sop (
    input a, b, c, d,
    input [4:0] dataa, datab, datac, datad,
    output [4:0] out
);
    wire s0 = a;
    wire s1 = ~a & b;
    wire s2 = ~(a | b) & c;
    wire s3 = ~(a | b | c) & d;
    assign out = ({5{s0}} & dataa) | ({5{s1}} & datab) |
                 ({5{s2}} & datac) | ({5{s3}} & datad);
endmodule

module test2_sop_chain (
    input a, b, c, d,
    input [4:0] dataa, datab, datac, datad,
    output [4:0] out
);
    wire na = ~a;
    wire nb = ~b;
    wire nc = ~c;
    wire s0 = a;
    wire s1 = na & b;
    wire s2 = na & nb & c;
    wire s3 = na & nb & nc & d;
    assign out = ({5{s0}} & dataa) | ({5{s1}} & datab) |
                 ({5{s2}} & datac) | ({5{s3}} & datad);
endmodule

module test2_index (
    input a, b, c, d,
    input [4:0] dataa, datab, datac, datad,
    output reg [4:0] out
);
    reg [2:0] idx;
    always @* begin
        if (a) idx = 0;
        else if (b) idx = 1;
        else if (c) idx = 2;
        else if (d) idx = 3;
        else idx = 4;
    end
    always @* begin
        case (idx)
            0: out = dataa;
            1: out = datab;
            2: out = datac;
            3: out = datad;
            default: out = 5'b0;
        endcase
    end
endmodule

module test2_nested_muxes (
    input a, b, c, d,
    input [4:0] dataa, datab, datac, datad,
    output [4:0] out
);
    wire [4:0] t1 = b ? datab : (c ? datac : (d ? datad : 5'b0));
    assign out = a ? dataa : t1;
endmodule
