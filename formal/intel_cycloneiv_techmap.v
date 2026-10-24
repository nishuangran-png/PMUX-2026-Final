// Functional models for the Cyclone IV cells that survive synth_intel.
//
// This file is used by verify_final_mapping.py after full synthesis.  It
// deliberately models the delivered lcell_comb primitive rather than the
// pre-mapping $lut cell, so errors in cells_map.v remain visible.

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
