// Minimal formal model for cells emitted by pmux_opt before technology mapping.
// pmux_opt ties aload/sload to zero and uses the same reset decode as the
// Cyclone IV cells_sim.v model, so an aload edge is not part of this boundary.
module dffeas(
    output reg q,
    input d, clk, clrn, prn, ena,
    input asdata, aload, sclr, sload
);
    parameter power_up = "dontcare";
    parameter is_wysiwyg = "false";

    wire reset = prn && sclr && ~clrn && ena;
    always @(posedge clk)
        if (reset)
            q <= 1'b0;
        else
            q <= d;
endmodule
