// Formal-only model for the Cyclone IV dffeas instances emitted by pmux_opt.
// The generated cells use aload/sload/asdata=0; reset, preset, clock enable,
// and synchronous clear remain fully modeled.
module dffeas (
    input d,
    input clk,
    input clrn,
    input prn,
    input ena,
    input asdata,
    input aload,
    input sclr,
    input sload,
    output q
);
    parameter is_wysiwyg = "false";
    parameter power_up = "dontcare";
    reg q_int;
    always @(posedge clk or negedge clrn) begin
        if (!clrn)
            q_int <= 1'b0;
        else if (sclr)
            q_int <= 1'b0;
        else if (sload)
            q_int <= asdata;
        else if (ena)
            q_int <= d;
    end
    assign q = !prn ? 1'b1 : q_int;
endmodule
