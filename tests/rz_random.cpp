#include "difftest.h"
#include "rz/cRZRandom.h"

#include <cstring>
#include <memory>
#include <random>

// Seeds of UINT32_MAX (seed from time/clock) and cRZRandom's mRealSeed == 0 (seeds from clock) are
// not exercised: the original reaches the CRT's time functions, which the harness traps.

namespace {

// Object buffer with canaries around it, so writes outside the object show up as differences.
struct Guarded {
    uint8_t bytes[64];
    void Fill(uint32_t seed)
    {
        std::memset(bytes, 0xA5, sizeof(bytes));
        std::memcpy(bytes + 16, &seed, 4);
    }
    void *Object() { return bytes + 16; }
};

bool SameBits(double a, double b)
{
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

} // namespace

DIFF_TEST(cRZRandom_RandomUint32Uniform, 0x1b8d75)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1b8d75);

    std::mt19937 gen(12345);
    const uint32_t edges[] = {0u, 1u, 0x3039u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    for (uint32_t i = 0; i < 100000; ++i) {
        uint32_t seed = i < std::size(edges) ? edges[i] : gen();
        Guarded a, b;
        a.Fill(seed);
        b.Fill(seed);

        // Several consecutive calls, so state carried between calls is also compared.
        for (int step = 0; step < 4; ++step) {
            uint32_t want = original(a.Object());
            uint32_t got = reinterpret_cast<cRZRandom *>(b.Object())->RandomUint32Uniform();
            ++ctx.cases;
            if (want != got) {
                ctx.Fail("seed 0x%08x step %d: return original 0x%08x, ours 0x%08x", seed, step, want, got);
            }
            if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) {
                ctx.Fail("seed 0x%08x step %d: object/canary memory differs", seed, step);
            }
        }
    }
}

DIFF_TEST(cRZRandom_Other, 0x1b8d5c)
{
    auto construct = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1b8bc8);
    auto seedFn = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x1b8ecb);
    auto scaled = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1b8d5c);
    auto range = ctx.Original<int32_t(__thiscall *)(void *, int32_t, int32_t)>(0x1b8d47);
    auto dbl = ctx.Original<double(__thiscall *)(void *)>(0x1b8c78);
    auto dblRange = ctx.Original<double(__thiscall *)(void *, double, double)>(0x1b8c48);

    std::mt19937 gen(777);
    std::uniform_real_distribution<double> real(-1e6, 1e6);
    for (int i = 0; i < 50000; ++i) {
        uint32_t seed = gen();
        if (seed == UINT32_MAX || seed == 0) seed = 1; // see the note at the top
        cRZRandom a{}, b{};
        a.mUnknown = b.mUnknown = gen();
        void *ret = construct(&a, seed);
        cRZRandom *got = b.Construct(seed);
        ++ctx.cases;
        if (ret != &a || got != &b || std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("Construct(0x%08x) differs", seed);

        uint32_t reseed = gen() | 1u;
        seedFn(&a, reseed);
        b.Seed(reseed);
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("Seed(0x%08x) differs", reseed);

        for (int step = 0; step < 4; ++step) {
            uint32_t scale = step == 0 ? 0 : gen();
            int32_t lo = static_cast<int32_t>(gen()), hi = static_cast<int32_t>(gen());
            double dlo = real(gen), dhi = real(gen);
            uint32_t w1 = scaled(&a, scale), g1 = b.RandomUint32Uniform(scale);
            int32_t w2 = range(&a, lo, hi), g2 = b.RandomSint32RangeUniform(lo, hi);
            if (a.mRealSeed == 0) a.mRealSeed = b.mRealSeed = 1; // see the note at the top
            double w3 = dbl(&a), g3 = b.RandomDoubleUniform();
            double w4 = dblRange(&a, dlo, dhi), g4 = b.RandomDoubleRangeUniform(dlo, dhi);
            ctx.cases += 4;
            if (w1 != g1) ctx.Fail("RandomUint32Uniform(%u): original %u, ours %u", scale, w1, g1);
            if (w2 != g2) ctx.Fail("RandomSint32RangeUniform(%d,%d): original %d, ours %d", lo, hi, w2, g2);
            if (!SameBits(w3, g3)) ctx.Fail("RandomDoubleUniform: original %.17g, ours %.17g", w3, g3);
            if (!SameBits(w4, g4)) ctx.Fail("RandomDoubleRangeUniform(%g,%g): original %.17g, ours %.17g", dlo, dhi, w4, g4);
            if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("state differs after step %d", step);
        }
    }
}

DIFF_TEST(cRZRandomSuper_All, 0x1b8d9b)
{
    auto construct = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1b8bde);
    auto seedFn = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x1b8efa);
    auto next = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1b8d9b);
    auto scaled = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1b8d8d);
    auto dbl = ctx.Original<double(__thiscall *)(void *)>(0x1b8cba);
    auto dblRange = ctx.Original<double(__thiscall *)(void *, double, double)>(0x1b8c60);
    auto hash = ctx.Original<uint32_t(__stdcall *)(uint32_t, uint32_t)>(0x1b8c01);

    auto same = [](const cRZRandomSuper &a, const cRZRandomSuper &b) {
        return std::memcmp(a.mState, b.mState, sizeof(a.mState)) == 0 && a.mLeft == b.mLeft
            && (a.mNext - a.mState) == (b.mNext - b.mState);
    };

    std::mt19937 gen(4242);
    std::uniform_real_distribution<double> real(-1e6, 1e6);
    auto a = std::make_unique<cRZRandomSuper>(), b = std::make_unique<cRZRandomSuper>();
    for (int i = 0; i < 300; ++i) {
        uint32_t seed = gen();
        if (seed == UINT32_MAX) seed = 0;
        void *ret = construct(a.get(), seed);
        cRZRandomSuper *got = b->Construct(seed);
        ++ctx.cases;
        if (ret != a.get() || got != b.get() || !same(*a, *b)) ctx.Fail("Construct(0x%08x) differs", seed);
        if (i % 3 == 0) {
            uint32_t reseed = gen() & 0x7fffffffu;
            seedFn(a.get(), reseed);
            b->Seed(reseed);
            ++ctx.cases;
            if (!same(*a, *b)) ctx.Fail("Seed(0x%08x) differs", reseed);
        }
        // Enough draws to cross several reloads.
        for (int step = 0; step < 2000; ++step) {
            ++ctx.cases;
            switch (gen() % 4) {
            case 0: { uint32_t w = next(a.get()), g = b->RandomUint32Uniform(); if (w != g) ctx.Fail("RandomUint32Uniform step %d: %u vs %u", step, w, g); break; }
            case 1: { uint32_t s = gen(); uint32_t w = scaled(a.get(), s), g = b->RandomUint32Uniform(s); if (w != g) ctx.Fail("RandomUint32Uniform(%u): %u vs %u", s, w, g); break; }
            case 2: { double w = dbl(a.get()), g = b->RandomDoubleUniform(); if (!SameBits(w, g)) ctx.Fail("RandomDoubleUniform: %.17g vs %.17g", w, g); break; }
            default: { double lo = real(gen), hi = real(gen); double w = dblRange(a.get(), lo, hi), g = b->RandomDoubleRangeUniform(lo, hi);
                       if (!SameBits(w, g)) ctx.Fail("RandomDoubleRangeUniform(%g,%g): %.17g vs %.17g", lo, hi, w, g); break; }
            }
        }
        if (!same(*a, *b)) ctx.Fail("state differs after draws (seed 0x%08x)", seed);
    }
    // Hash keeps a call counter (the original's in its image, ours in a static), so call in lockstep.
    for (int i = 0; i < 10000; ++i) {
        uint32_t x = gen(), y = gen();
        uint32_t w = hash(x, y), g = cRZRandomSuper::Hash(x, y);
        ++ctx.cases;
        if (w != g) ctx.Fail("Hash(0x%08x,0x%08x) call %d: original 0x%08x, ours 0x%08x", x, y, i, w, g);
    }
}
