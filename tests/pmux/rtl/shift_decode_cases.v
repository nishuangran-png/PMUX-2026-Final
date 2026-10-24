module shift_decode_b5_y32(
	input [4:0] idx,
	output [31:0] y
);
	assign y = 32'h1 << idx;
endmodule

module shift_decode_b5_y64_oob(
	input [4:0] idx,
	output [63:0] y
);
	assign y = 64'h1 << idx;
endmodule

module shift_decode_b8_y256(
	input [7:0] idx,
	output [255:0] y
);
	assign y = 256'h1 << idx;
endmodule

module shift_decode_a64_b8_y512_offset(
	input [7:0] idx,
	output [511:0] y
);
	assign y = 512'd64 << idx;
endmodule

module shift_decode_signed_a(
	input [4:0] idx,
	output [31:0] y
);
	assign y = 32'sd1 << idx;
endmodule

module shift_decode_b4_skip(
	input [3:0] idx,
	output [31:0] y
);
	assign y = 32'h1 << idx;
endmodule

module shift_decode_y16_skip(
	input [4:0] idx,
	output [15:0] y
);
	assign y = 16'h1 << idx;
endmodule

module shift_decode_a2(
	input [4:0] idx,
	output [31:0] y
);
	assign y = 32'd2 << idx;
endmodule

module shift_decode_a3_skip(
	input [4:0] idx,
	output [31:0] y
);
	assign y = 32'd3 << idx;
endmodule

module shift_decode_signed_b_normalized(
	input signed [4:0] idx,
	output [31:0] y
);
	assign y = 32'h1 << idx;
endmodule

module shift_decode_variable_a_skip(
	input [4:0] idx,
	input [31:0] a,
	output [31:0] y
);
	assign y = a << idx;
endmodule

module shift_decode_b17_skip(
	input [16:0] idx,
	output [63:0] y
);
	assign y = 64'h1 << idx;
endmodule
