// Self-test of the harness's x87 precision control (difftest::X87Precision, TS2_X87_PC; docs/lane/test.md, "x87
// precision"): the original's x87 code computes differently at 24-bit precision (the game's, D3D9 without
// D3DCREATE_FPU_PRESERVE) and at 53-bit (the harness's default), while our SSE2 code computes the same at both.
#include "difftest.h"

#include <float.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

uint64_t Bits(double d)
{
    uint64_t b;
    std::memcpy(&b, &d, 8);
    return b;
}

unsigned Pc()
{
    unsigned int x87 = 0;
    __control87_2(0, 0, &x87, nullptr);
    return x87 & _MCW_PC;
}

// SSE2 arithmetic of ours: a float product, a double product and quotient, a square root through the CRT.
__declspec(noinline) uint64_t OurArithmetic(volatile float *f, volatile double *d)
{
    const float p = f[0] * f[1];
    const double q = d[0] * d[1] / d[2];
    const double r = std::sqrt(d[0]);
    uint32_t pb;
    std::memcpy(&pb, &p, 4);
    return Bits(q) ^ Bits(r) * 3 ^ pb;
}

} // namespace

DIFF_TEST(X87Precision_SelfTest, 0)
{
    // The original's Dot87 (0x18fbf0: fld, fmul, fadd with the sum kept on the x87 stack). (1 + 2^-23)^2 is
    // 1 + 2^-22 + 2^-46: exact at 53-bit, 1 + 2^-22 at 24-bit.
    using Dot = double(__cdecl *)(const float *, const float *);
    const Dot dot = ctx.Original<Dot>(0x18fbf0);
    // Literals, not arithmetic: with TS2_X87_PC the whole test runs at another precision, and the CRT's ldexp returns on
    // the x87 stack.
    const float x = 0x1.000002p+0f; // 1 + 2^-23
    const float a[3] = {x, 0.0f, 0.0f};
    const double exact = 0x1.0000040000040p+0, rounded = 0x1.0000040000000p+0;

    volatile float f[2] = {x, x};
    volatile double d[3] = {0x1.0000000001p+0, 0x1.8000000000001p+1, 7.0};
    uint64_t ours[3] = {};
    double original[3] = {};
    const difftest::X87Pc modes[3] = {difftest::X87Pc::Bits53, difftest::X87Pc::Bits24, difftest::X87Pc::Bits64};
    const unsigned outside = Pc();
    // TS2_X87_PC: the runner has set that precision for the whole test.
    if (const char *pc = std::getenv("TS2_X87_PC")) {
        ++ctx.cases;
        const unsigned wanted = std::strcmp(pc, "24") == 0   ? static_cast<unsigned>(difftest::X87Pc::Bits24)
                                : std::strcmp(pc, "64") == 0 ? static_cast<unsigned>(difftest::X87Pc::Bits64)
                                                             : static_cast<unsigned>(difftest::X87Pc::Bits53);
        if (outside != wanted) ctx.Fail("TS2_X87_PC=%s: the test runs at precision control %x, wanted %x", pc, outside, wanted);
    }
    for (unsigned m = 0; m < 3; ++m) {
        difftest::X87Precision scope(modes[m]);
        ++ctx.cases;
        if (Pc() != static_cast<unsigned>(modes[m])) ctx.Fail("mode %u: precision control %x, wanted %x", m, Pc(), static_cast<unsigned>(modes[m]));
        original[m] = dot(a, a);
        ours[m] = OurArithmetic(f, d);
    }
    if (Pc() != outside) ctx.Fail("X87Precision left the precision control at %x (was %x)", Pc(), outside);

    ++ctx.cases;
    if (original[0] != exact) ctx.Fail("original at 53-bit: %a, expected %a", original[0], exact);
    if (original[1] != rounded) ctx.Fail("original at 24-bit: %a, expected %a", original[1], rounded);
    if (original[2] != exact) ctx.Fail("original at 64-bit: %a, expected %a", original[2], exact);
    if (ours[0] != ours[1] || ours[0] != ours[2])
        ctx.Fail("our SSE2 arithmetic depends on the x87 precision: %llx / %llx / %llx", ours[0], ours[1], ours[2]);
}
