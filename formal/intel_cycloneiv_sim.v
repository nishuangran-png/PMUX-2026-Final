// Simulation models for the subset of Cyclone IV primitives emitted by
// Yosys 0.69 synth_intel for the PMUX benchmark.  These are intentionally
// small functional models, not vendor timing models.

module cycloneiv_lcell_comb(
	output combout,
	output cout,
	input dataa,
	input datab,
	input datac,
	input datad,
	input cin
);
	parameter [15:0] lut_mask = 16'hffff;
	parameter sum_lutc_input = "datac";

	wire [3:0] datac_index = {datad, datac, datab, dataa};
	wire [3:0] cin_index = {datad, cin, datab, dataa};

	assign combout = sum_lutc_input == "cin"
		? lut_mask[cin_index]
		: lut_mask[datac_index];
	assign cout = lut_mask[{1'b0, cin, datab, dataa}];
endmodule

module dffeas(
	output reg q,
	input d,
	input clk,
	input clrn,
	input prn,
	input ena,
	input asdata,
	input aload,
	input sclr,
	input sload
);
	parameter power_up = "dontcare";
	parameter is_wysiwyg = "false";

	initial begin
		if (power_up == "high")
			q = 1'b1;
		else
			q = 1'b0;
	end

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

module cycloneiv_io_ibuf(output o, input i, input ibar);
	assign o = i;
endmodule

module cycloneiv_io_obuf(output o, input i, input oe);
	assign o = i;
endmodule

module VCC(output V);
	assign V = 1'b1;
endmodule

module GND(output G);
	assign G = 1'b0;
endmodule
