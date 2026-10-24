`default_nettype none

module priority_encoder #(
    parameter WIDTH = 16
) (
    input  wire [WIDTH-1:0] input_unencoded,
    output wire             output_valid,
    output wire [$clog2(WIDTH)-1:0] output_encoded,
    output wire [WIDTH-1:0] output_unencoded
);
    localparam OUTW = $clog2(WIDTH);

    wire [WIDTH-1:0] seen_higher;
    assign seen_higher[WIDTH-1] = 1'b0;

    genvar i;
    generate
        for (i = WIDTH-2; i >= 0; i = i - 1) begin : gen_priority
            wire higher;
            assign higher = input_unencoded[i+1] | seen_higher[i+1];
            assign seen_higher[i] = higher;
            assign output_unencoded[i] = input_unencoded[i] & ~higher;
        end
    endgenerate

    assign output_unencoded[WIDTH-1] = input_unencoded[WIDTH-1];
    assign output_valid = |input_unencoded;

    function automatic [OUTW-1:0] encode_onehot(input [WIDTH-1:0] onehot);
        integer k;
        begin
            encode_onehot = {OUTW{1'b0}};
            for (k = 0; k < WIDTH; k = k + 1)
                if (onehot[k])
                    encode_onehot = encode_onehot | k[OUTW-1:0];
        end
    endfunction

    assign output_encoded = encode_onehot(output_unencoded);
endmodule

`default_nettype wire
