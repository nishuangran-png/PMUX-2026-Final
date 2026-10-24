autoidx 1
module \shift_decode_a4_signed_safe
  wire width 5 input 1 \idx
  wire width 32 output 2 \y
  cell $shl $shl_a4_signed_safe
    parameter \A_SIGNED 1
    parameter \B_SIGNED 0
    parameter \A_WIDTH 4
    parameter \B_WIDTH 5
    parameter \Y_WIDTH 32
    connect \A 4'0100
    connect \B \idx
    connect \Y \y
  end
end
