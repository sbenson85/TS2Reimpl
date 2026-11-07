// Differential tests for the thunks of nTSSG::cAnimatable's secondary subobjects (vtables at +424, +432 and +444).
// Each thunk computes this - vtordisp - N (vtordisp is the dword at this - 4) and then reads a field of the result
// or returns the address. Each case fills one random buffer, copies it, and runs the original thunk on one copy and
// our function on the other. The subobject sits at kThis in a kSize buffer, so every field the original reads lies
// inside the buffer, and the vtordisp is 0, 4 or 8 (written over the random bytes at kThis - 4).
// Checked: the result (the full 32 bits for dword and address results, the float bits for float results, which the
// original returns in st0, and only al for the byte result, since the thunk writes nothing else), and that neither
// side wrote to its buffer.
#include "difftest.h"
#include "sg/cAnimatable.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace {

using Sub = nTSSG::cAnimatableSubobject;

constexpr int kSize = 1024;
constexpr int kThis = 512; // the subobject's address inside the buffer; the vtordisp is at kThis - 4

// The result as bits, for comparison. Addresses are compared as offsets from the buffer they point into.
uint32_t Bits(uint32_t v, const uint8_t *)
{
    return v;
}
uint32_t Bits(uint8_t v, const uint8_t *)
{
    return v;
}
uint32_t Bits(float v, const uint8_t *)
{
    uint32_t b;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}
uint32_t Bits(void *p, const uint8_t *base)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(base));
}

// Fills buf with random bytes and writes the vtordisp at the subobject's - 4.
void Fill(uint8_t *buf, std::mt19937 &rng, uint32_t disp)
{
    for (int i = 0; i < kSize; ++i) buf[i] = static_cast<uint8_t>(rng());
    std::memcpy(buf + kThis - 4, &disp, sizeof(disp));
}

// A thunk whose result is returned in eax, al, st0 or as an address (R). Pmf is our member function.
template <typename R>
void CheckThunk(difftest::Context &ctx, const char *name, uint32_t rva, R (Sub::*pmf)(), std::mt19937 &rng, int trials)
{
    using Fn = R(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(rva);
    static const uint32_t kDisps[3] = {0, 4, 8};
    for (int i = 0; i < trials; ++i) {
        uint8_t init[kSize];
        Fill(init, rng, kDisps[rng() % 3]);
        uint8_t a[kSize], b[kSize];
        std::memcpy(a, init, kSize);
        std::memcpy(b, init, kSize);
        R ra = orig(a + kThis);
        R rb = (reinterpret_cast<Sub *>(b + kThis)->*pmf)();
        ++ctx.cases;
        uint32_t xa = Bits(ra, a);
        uint32_t xb = Bits(rb, b);
        if (xa != xb) ctx.Fail("%s: original 0x%08x, ours 0x%08x", name, xa, xb);
        if (std::memcmp(a, init, kSize) != 0) ctx.Fail("%s: the original wrote to its buffer", name);
        if (std::memcmp(b, init, kSize) != 0) ctx.Fail("%s: ours wrote to its buffer", name);
    }
}

} // namespace

// Slot 55 of +432, thunk 0x26e018: dword.
DIFF_TEST(cAnimatable_Vf432_55, 0x26e018)
{
    std::mt19937 rng(0x7a01);
    CheckThunk<uint32_t>(ctx, "Vf432_55", 0x26e018, &Sub::Vf432_55, rng, 20000);
}

// Slot 25 of +444, thunk 0x26fbdf: dword, no N.
DIFF_TEST(cAnimatable_Vf444_25, 0x26fbdf)
{
    std::mt19937 rng(0x7a02);
    CheckThunk<uint32_t>(ctx, "Vf444_25", 0x26fbdf, &Sub::Vf444_25, rng, 20000);
}

// Slot 20 of +432, thunk 0x26fc04: dword.
DIFF_TEST(cAnimatable_Vf432_20, 0x26fc04)
{
    std::mt19937 rng(0x7a03);
    CheckThunk<uint32_t>(ctx, "Vf432_20", 0x26fc04, &Sub::Vf432_20, rng, 20000);
}

// Slot 126 of +432, thunk 0x26fda5: float in st0.
DIFF_TEST(cAnimatable_Vf432_126, 0x26fda5)
{
    std::mt19937 rng(0x7a04);
    CheckThunk<float>(ctx, "Vf432_126", 0x26fda5, &Sub::Vf432_126, rng, 20000);
}

// Slot 26 of +444, thunk 0x26fdb0: float in st0, no N.
DIFF_TEST(cAnimatable_Vf444_26, 0x26fdb0)
{
    std::mt19937 rng(0x7a05);
    CheckThunk<float>(ctx, "Vf444_26", 0x26fdb0, &Sub::Vf444_26, rng, 20000);
}

// Slot 39 of +432, thunk 0x270558: dword.
DIFF_TEST(cAnimatable_Vf432_39, 0x270558)
{
    std::mt19937 rng(0x7a06);
    CheckThunk<uint32_t>(ctx, "Vf432_39", 0x270558, &Sub::Vf432_39, rng, 20000);
}

// Slot 134 of +432, thunk 0x271dac: byte in al (the rest of eax is not part of the result).
DIFF_TEST(cAnimatable_Vf432_134, 0x271dac)
{
    std::mt19937 rng(0x7a07);
    CheckThunk<uint8_t>(ctx, "Vf432_134", 0x271dac, &Sub::Vf432_134, rng, 20000);
}

// Slot 49 of +432, thunk 0x272197: dword.
DIFF_TEST(cAnimatable_Vf432_49, 0x272197)
{
    std::mt19937 rng(0x7a08);
    CheckThunk<uint32_t>(ctx, "Vf432_49", 0x272197, &Sub::Vf432_49, rng, 20000);
}

// Slot 37 of +432, thunk 0x2721a5: dword.
DIFF_TEST(cAnimatable_Vf432_37, 0x2721a5)
{
    std::mt19937 rng(0x7a09);
    CheckThunk<uint32_t>(ctx, "Vf432_37", 0x2721a5, &Sub::Vf432_37, rng, 20000);
}

// Slot 10 of +424, thunk 0x27253f: no result. The body (ret 0) never writes eax, so the return is not compared;
// both sides must leave their buffers unchanged.
DIFF_TEST(cAnimatable_Vf424_10, 0x27253f)
{
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x27253f);
    std::mt19937 rng(0x7a0a);
    static const uint32_t kDisps[3] = {0, 4, 8};
    for (int i = 0; i < 20000; ++i) {
        uint8_t init[kSize];
        Fill(init, rng, kDisps[rng() % 3]);
        uint8_t a[kSize], b[kSize];
        std::memcpy(a, init, kSize);
        std::memcpy(b, init, kSize);
        orig(a + kThis);
        reinterpret_cast<Sub *>(b + kThis)->Vf424_10();
        ++ctx.cases;
        if (std::memcmp(a, init, kSize) != 0) ctx.Fail("Vf424_10: the original wrote to its buffer");
        if (std::memcmp(b, init, kSize) != 0) ctx.Fail("Vf424_10: ours wrote to its buffer");
    }
}

// Slot 33 of +432, thunk 0x273339: dword.
DIFF_TEST(cAnimatable_Vf432_33, 0x273339)
{
    std::mt19937 rng(0x7a0b);
    CheckThunk<uint32_t>(ctx, "Vf432_33", 0x273339, &Sub::Vf432_33, rng, 20000);
}

// Slot 68 of +432, thunk 0x2737dc: dword.
DIFF_TEST(cAnimatable_Vf432_68, 0x2737dc)
{
    std::mt19937 rng(0x7a0c);
    CheckThunk<uint32_t>(ctx, "Vf432_68", 0x2737dc, &Sub::Vf432_68, rng, 20000);
}

// Slot 76 of +432, thunk 0x2737ea: dword.
DIFF_TEST(cAnimatable_Vf432_76, 0x2737ea)
{
    std::mt19937 rng(0x7a0d);
    CheckThunk<uint32_t>(ctx, "Vf432_76", 0x2737ea, &Sub::Vf432_76, rng, 20000);
}

// Slot 35 of +432, thunk 0x274062: dword.
DIFF_TEST(cAnimatable_Vf432_35, 0x274062)
{
    std::mt19937 rng(0x7a0e);
    CheckThunk<uint32_t>(ctx, "Vf432_35", 0x274062, &Sub::Vf432_35, rng, 20000);
}

// Slot 27 of +444, thunk 0x274070, no N: the address this - vtordisp - 0x6c.
DIFF_TEST(cAnimatable_Vf444_27, 0x274070)
{
    std::mt19937 rng(0x7a0f);
    CheckThunk<void *>(ctx, "Vf444_27", 0x274070, &Sub::Vf444_27, rng, 20000);
}
