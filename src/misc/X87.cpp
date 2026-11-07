#include "misc/X87.h"

#include <cstring>

// x87 precision helpers (src/misc/X87.h).
namespace x87 {

uint16_t ControlWord()
{
    uint16_t cw;
    __asm fnstcw cw
    return cw;
}

// On the bits: the 29 low significand bits dropped, rounding to nearest with ties to even (a carry runs into the
// exponent as it should). Exact for normal doubles; double denormals (below 2^-1022) never come from float operands.
double Round24(double v)
{
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    if ((bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) return v; // NaN or infinity
    bits += 0x0fffffffull + ((bits >> 29) & 1);
    bits &= ~0x1fffffffull;
    std::memcpy(&v, &bits, 8);
    return v;
}

} // namespace x87
