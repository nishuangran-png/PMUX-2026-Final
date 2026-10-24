// Formal model for the Cyclone IV dffeas subset emitted by pmux_opt.
//
// The optimizer and the stock Intel FF map tie aload, asdata, and sload to
// zero.  This model intentionally covers only that subset so formal runs can
// interpret the storage element instead of leaving it as an undefined blackbox.
// Synchronous controls are gated by ena, matching Yosys $sdffce semantics used
// when pmux_opt lowers a gated synchronous reset to this primitive.
module dffeas(q, d, clk, clrn, prn, ena, asdata, aload, sclr, sload);
	parameter power_up = "dontcare";
	parameter is_wysiwyg = "false";

	output reg q;
	input d, clk, clrn, prn, ena, asdata, aload, sclr, sload;

	always @(posedge clk or negedge clrn or negedge prn) begin
		if (!clrn)
			q <= 1'b0;
		else if (!prn)
			q <= 1'b1;
		else if (ena) begin
			if (sclr)
				q <= 1'b0;
			else
				q <= d;
		end
	end
endmodule
