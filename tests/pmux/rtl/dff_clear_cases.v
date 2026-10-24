module dff_zero_on_a(
	input clk,
	input sel,
	input [3:0] d,
	output reg [3:0] q
);
	always @(posedge clk)
		q <= sel ? 4'b0000 : d;
endmodule

module dff_zero_on_b(
	input clk,
	input sel,
	input [3:0] d,
	output reg [3:0] q
);
	always @(posedge clk)
		q <= sel ? d : 4'b0000;
endmodule

module dffe_zero_on_a(
	input clk,
	input en,
	input sel,
	input [3:0] d,
	output reg [3:0] q
);
	always @(posedge clk)
		if (en)
			q <= sel ? 4'b0000 : d;
endmodule

module dffe_zero_on_b_active_low(
	input clk,
	input en_n,
	input sel,
	input [3:0] d,
	output reg [3:0] q
);
	always @(posedge clk)
		if (!en_n)
			q <= sel ? d : 4'b0000;
endmodule

module adffe_zero_on_a(
	input clk,
	input arst_n,
	input en,
	input sel,
	input [3:0] d,
	output reg [3:0] q
);
	always @(posedge clk or negedge arst_n)
		if (!arst_n)
			q <= 4'b0000;
		else if (en)
			q <= sel ? 4'b0000 : d;
endmodule

module dff_init_skip(
	input clk,
	input sel,
	input [3:0] d,
	output reg [3:0] q = 4'b0001
);
	always @(posedge clk)
		q <= sel ? 4'b0000 : d;
endmodule

module dffe_no_clear(
	input clk,
	input en,
	input [3:0] d,
	output reg [3:0] q
);
	always @(posedge clk)
		if (en)
			q <= d;
endmodule
