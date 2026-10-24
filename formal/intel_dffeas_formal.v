// Formal model for the Cyclone IV dffeas subset emitted by pmux_opt.
//
// This models the control ports used by Yosys 0.69's Intel FF mapping,
// including synchronous load (sload/asdata), synchronous clear (sclr),
// asynchronous load (aload/asdata), clock enable and asynchronous clear/preset.
// It is a functional model, not a vendor timing model.
module dffeas(q, d, clk, clrn, prn, ena, asdata, aload, sclr, sload);
	parameter power_up = "dontcare";
	parameter is_wysiwyg = "false";

	output reg q;
	input d, clk, clrn, prn, ena, asdata, aload, sclr, sload;

	always @(posedge clk or negedge clrn or negedge prn or posedge aload) begin
		if (!clrn)
			q <= 1'b0;
		else if (!prn)
			q <= 1'b1;
		else if (aload)
			q <= asdata;
		else if (ena) begin
			if (sclr)
				q <= 1'b0;
			else if (sload)
				q <= asdata;
			else
				q <= d;
		end
	end
endmodule
