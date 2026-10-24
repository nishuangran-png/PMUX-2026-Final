// Async active-low reset DFF with reset/init value 1.
module  \$_DFFE_PN1P_ (input D, C, R, E, output Q);
   parameter _TECHMAP_WIREINIT_Q_ = 1'bx;
   generate if (_TECHMAP_WIREINIT_Q_ === 1'b1) begin
     dffeas #(.is_wysiwyg("TRUE"), .power_up("high")) _TECHMAP_REPLACE_ (.d(D), .q(Q), .clk(C), .clrn(1'b1), .prn(R), .ena(E), .asdata(1'b0), .aload(1'b0), .sclr(1'b0), .sload(1'b0));
   end else begin
     dffeas #(.is_wysiwyg("TRUE"), .power_up("low")) _TECHMAP_REPLACE_ (.d(D), .q(Q), .clk(C), .clrn(1'b1), .prn(R), .ena(E), .asdata(1'b0), .aload(1'b0), .sclr(1'b0), .sload(1'b0));
   end
   endgenerate
   wire _TECHMAP_REMOVEINIT_Q_ = 1;
endmodule
