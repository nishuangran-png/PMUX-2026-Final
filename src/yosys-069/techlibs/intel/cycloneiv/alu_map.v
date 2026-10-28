// Correct Cyclone IV arithmetic-mode mapping for Yosys $alu cells.
//
// The vendor file contains a WIP mapping that leaves some outputs undriven.
// This mapping uses an arithmetic-mode LCELL full adder:
//   combout = A ^ B ^ CI
//   cout    = (A & B) | (CI & (A ^ B))
// BI inverts B for subtraction, preserving the $alu contract.
(* techmap_celltype = "$alu" *)
module _80_pmux_cycloneiv_alu (A, B, CI, BI, X, Y, CO);
	parameter A_SIGNED = 0;
	parameter B_SIGNED = 0;
	parameter A_WIDTH = 1;
	parameter B_WIDTH = 1;
	parameter Y_WIDTH = 1;

	(* force_downto *)
	input [A_WIDTH-1:0] A;
	(* force_downto *)
	input [B_WIDTH-1:0] B;
	(* force_downto *)
	output [Y_WIDTH-1:0] X, Y;

	input CI;
	input BI;
	output [Y_WIDTH-1:0] CO;

	wire _TECHMAP_FAIL_ = Y_WIDTH < 1;

	(* force_downto *)
	wire [Y_WIDTH-1:0] A_buf, B_buf;
	\$pos #(.A_SIGNED(A_SIGNED), .A_WIDTH(A_WIDTH), .Y_WIDTH(Y_WIDTH)) A_conv (.A(A), .Y(A_buf));
	\$pos #(.A_SIGNED(B_SIGNED), .A_WIDTH(B_WIDTH), .Y_WIDTH(Y_WIDTH)) B_conv (.A(B), .Y(B_buf));

	(* force_downto *)
	wire [Y_WIDTH-1:0] AA = A_buf;
	(* force_downto *)
	wire [Y_WIDTH-1:0] BB = BI ? ~B_buf : B_buf;
	(* force_downto *)
	wire [Y_WIDTH:0] C;
	assign C[0] = CI;

	genvar i;
	generate for (i = 0; i < Y_WIDTH; i = i + 1) begin: slice
		cycloneiv_lcell_comb #(
			.lut_mask(16'h96e8),
			.sum_lutc_input("cin")
		) full_adder (
			.combout(Y[i]),
			.cout(C[i+1]),
			.dataa(AA[i]),
			.datab(BB[i]),
			.datac(1'b1),
			.datad(1'b1),
			.cin(C[i])
		);
	end endgenerate

	assign CO = C[Y_WIDTH:1];
	assign X = AA ^ BB;
endmodule
