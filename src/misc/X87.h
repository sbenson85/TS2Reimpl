#pragma once
#include <cstdint>

// x87 precision for code of ours that models the original's x87 arithmetic in SSE2 doubles (lane PJ, notes/plan.md "x87").
//
// The original computes float maths on the x87, whose precision control rounds the significand of every fadd, fsub,
// fmul, fdiv and fsqrt (not loads, stores, the exponent range or transcendentals). The CRT starts threads at 53-bit, and
// the difftest harness runs there; the game creates its Direct3D 9 device without D3DCREATE_FPU_PRESERVE, so D3D9 sets
// 24-bit on that thread (check with the host's TS2HOST_X87PROBE). A double in SSE2 is what the x87 leaves at 53-bit
// for operands of 24 or 53 bits; at 24-bit, Round24 of each operation's double result is what the x87 leaves, as long
// as both operands have 24-bit significands (values loaded from floats, or earlier results at 24-bit): their products are
// exact in a double, and sums, quotients and square roots are rounded twice harmlessly (53 >= 2 * 24 + 2). An operand
// loaded from a double constant (fmul qword) breaks that: say so where it happens.
//
// So: compute each x87 operation in double, then x87::Rounded(result), which reads the calling thread's control word.
// 64-bit precision is not modelled (Rounded leaves the double, as at 53).
namespace x87 {

// The calling thread's x87 control word (fnstcw).
uint16_t ControlWord();

// True when the calling thread's x87 runs at 24-bit precision (precision control 00).
inline bool SinglePrecision() { return (ControlWord() & 0x300) == 0; }

// The significand rounded to 24 bits (nearest, ties to even), the exponent kept: no overflow or denormal at the float
// range, as on the x87. NaNs, infinities and zeros are returned as they are.
double Round24(double v);

// One x87 arithmetic result at the calling thread's precision: Round24(v) at 24-bit, v otherwise.
inline double Rounded(double v) { return SinglePrecision() ? Round24(v) : v; }

} // namespace x87
