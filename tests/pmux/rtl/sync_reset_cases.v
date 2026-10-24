module sdffce_positive(
	input clk,
	input en,
	input rst,
	input d,
	output reg q
);
	always @(posedge clk)
		if (en)
			if (rst)
				q <= 1'b0;
			else
				q <= d;
endmodule

module sdff_positive(
	input clk,
	input rst,
	input d,
	output reg q
);
	always @(posedge clk)
		if (rst)
			q <= 1'b0;
		else
			q <= d;
endmodule

module sdffce_negative_polarities(
	input clk_n,
	input en_n,
	input rst_n,
	input d,
	output reg q
);
	always @(negedge clk_n)
		if (!en_n)
			if (!rst_n)
				q <= 1'b0;
			else
				q <= d;
endmodule

module sdff_reset_value_one_skip(
	input clk,
	input rst,
	input d,
	output reg q
);
	always @(posedge clk)
		if (rst)
			q <= 1'b1;
		else
			q <= d;
endmodule

module sdffe_skip(
	input clk,
	input en,
	input rst,
	input d,
	output reg q
);
	always @(posedge clk)
		if (rst)
			q <= 1'b0;
		else if (en)
			q <= d;
endmodule

module sdff_mixed_reset_skip(
	input clk,
	input rst,
	input [1:0] d,
	output reg [1:0] q
);
	always @(posedge clk)
		if (rst)
			q <= 2'b10;
		else
			q <= d;
endmodule

module sdff_init_skip(
	input clk,
	input rst,
	input d,
	output reg q = 1'b0
);
	always @(posedge clk)
		if (rst)
			q <= 1'b0;
		else
			q <= d;
endmodule
