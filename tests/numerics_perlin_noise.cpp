// Differential tests of nGZNumerics::cPerlinNoise (lane PN, scenegraph G0b step 6; src/numerics/PerlinNoise.h).
//
// Each side's object comes from its own factory: the game's 0x98c450 (new 0x1924, the constructor 0x98c2c0 with the
// most-derived flag) and ours, Create. Both are called through the faces the game's callers use: the cIPerlinNoise face
// (+0x191c, 15 slots) and the cIGZUnknown face (+0x1914, 3 slots).
//
//   PerlinNoise_Layout        the objects' shape: face offsets, vtordisps, both vbtables, the count and dimensions the
//                             constructor leaves, our class's size and member offsets.
//   PerlinNoise_Init          slot 0 over dimensions 0..4 and many seeds, the tables compared word for word (prefilled alike,
//                             so what Init leaves alone shows), IsValidDimension (slot 1) for each.
//   PerlinNoise_Noise         slots 2..7 on a seeded table over inputs of every kind (negative, huge, tiny, infinite, one
//                             canonical NaN, past the int range), results compared bit for bit at 53 and 24-bit x87.
//   PerlinNoise_Fractal       slots 8..13, the same.
//   PerlinNoise_OctaveCount   slot 14 and its helper 0x98d3f0 (the wrapper 0x98c400), with the 1/ln 2 global (0x10f4fbc)
//                             set on the original's side as its static initialiser does.
//   PerlinNoise_RefCount      QueryInterface (0x98d330) over iids, AddRef (0x98c3e0), Release (0x98d3ab) down to the
//                             destruction (deleting destructor 0x98c3b0, destructor 0x98c360).
//
// RVAs these tests reach (status entries cite them): the factory 0x98c450, the constructor 0x98c2c0, the destructor
// 0x98c360, the deleting destructor 0x98c3b0, QueryInterface 0x98d330, AddRef 0x98c3e0, Release 0x98d3ab, and the
// cIPerlinNoise face's bodies (slot order): 0x98c4a0 Init, 0x98c6b0 IsValidDimension, 0x98c6d0 Noise1Cell, 0x98c740 Noise1,
// 0x98c7a0 Noise2Cell, 0x98c8e0 Noise2, 0x98c980 Noise3Cell, 0x98cd30 Noise3, 0x98ce00 Fractal1Cell, 0x98cec0 Fractal1,
// 0x98cf40 Fractal2Cell, 0x98d050 Fractal2, 0x98d100 Fractal3Cell, 0x98d260 Fractal3, 0x98c400 OctaveCount (0x98d3f0);
// and the vtordisp thunks the game side calls through: 0x98c3ee 0x98c496 0x98c6a2 0x98c6c0 0x98c6c8 0x98c78e 0x98c796
// 0x98c962 0x98c96a 0x98cde2 0x98cdea 0x98cf2c 0x98cf34 0x98d0f0 0x98d0f8.
#include "code_patch.h"
#include "difftest.h"

#include "numerics/PerlinNoise.h"
#include "rz/cRZRandom.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace {

using namespace nGZNumerics;

constexpr uint32_t kFactory = 0x98c450;
constexpr uint32_t kHelper = 0x98d3f0;
constexpr uint32_t kReciprocalLog = 0x10f4fbc;
constexpr uint32_t kFaceOffset = 0x191c;
constexpr uint32_t kUnknownOffset = 0x1914;
using FactoryFn = cIGZUnknown *(__cdecl *)();

template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}

// The generator Init makes is constructed with -1 (a clock seed) and seeded at once: the original's constructor would reach the
// CRT's time functions, which the harness traps, so it is stood in for (seeded 1), and ours gets the same clock reading.
cRZRandom *__fastcall RandomStub(cRZRandom *self, void *, uint32_t seed)
{
    return self->Construct(seed == UINT32_MAX ? 1 : seed);
}
uint32_t RandomClock(int, uint32_t)
{
    return 1;
}
struct RandomScope {
    shape::CodePatch patch{0x1b8bc8u, reinterpret_cast<const void *>(&RandomStub)};
    uint32_t (*saved)(int, uint32_t) = gRZRandomClockSeedOverride;
    RandomScope() { gRZRandomClockSeedOverride = &RandomClock; }
    ~RandomScope() { gRZRandomClockSeedOverride = saved; }
};

// One side's object: its cIPerlinNoise face and the object's start.
struct Side {
    char *base;
    char *face() const { return base + kFaceOffset; }
    char *unknown() const { return base + kUnknownOffset; }
};

Side Make(const difftest::Context &ctx, bool original)
{
    cIGZUnknown *unknown = original ? ctx.Original<FactoryFn>(kFactory)() : cPerlinNoise::Create();
    return {reinterpret_cast<char *>(unknown) - kUnknownOffset};
}

// Releases the side's own reference... the object is made with a count of 0: one AddRef then one Release destroys it.
void Destroy(Side s)
{
    CallSlot<uint32_t>(s.unknown(), 1);
    CallSlot<uint32_t>(s.unknown(), 2);
}

bool SameBits(float a, float b)
{
    return std::memcmp(&a, &b, 4) == 0;
}

// Inputs of every kind. NaN is the one canonical quiet NaN: which of two different NaNs an operation keeps depends on the
// operand order the compiler picked (docs/lane/test.md, "NaN operand order").
float PickFloat(std::mt19937 &rng)
{
    switch (rng() % 16) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: return -1.0f;
    case 4: return 0.5f;
    case 5: return static_cast<float>(static_cast<int32_t>(rng() % 2000) - 1000);
    case 6: return std::ldexp(static_cast<float>(rng() & 0xffffff), -static_cast<int>(rng() % 40));
    case 7: return -std::ldexp(static_cast<float>(rng() & 0xffffff), -static_cast<int>(rng() % 40));
    case 8: return static_cast<float>((rng() % 6000000) / 1000.0 - 3000.0);
    case 9: return 3.0e9f;
    case 10: return -3.0e9f;
    case 11: return 2147483648.0f;
    case 12: return std::numeric_limits<float>::infinity() * ((rng() & 1) ? 1.0f : -1.0f);
    case 13: return std::numeric_limits<float>::quiet_NaN();
    case 14: return static_cast<float>((rng() % 2000) / 1000.0 - 1.0);
    default: return static_cast<float>((rng() % 20000) / 1000.0 - 10.0);
    }
}

// A fraction in [0, 1) for the cell entry points (and sometimes anything).
float PickFraction(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return PickFloat(rng);
    case 1: return 0.0f;
    case 2: return 0.5f;
    default: return static_cast<float>((rng() & 0xffffff)) / 16777216.0f;
    }
}

uint32_t PickInt(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return 0;
    case 1: return 0xffffffffu;
    case 2: return 0x7fffffffu;
    case 3: return 0x80000000u;
    case 4: return rng() % 600;
    default: return static_cast<uint32_t>(rng());
    }
}

float Amplitude(std::mt19937 &rng)
{
    return (rng() % 4 == 0) ? PickFloat(rng) : static_cast<float>((rng() % 2000) / 1000.0);
}

float Gain(std::mt19937 &rng)
{
    return (rng() % 4 == 0) ? PickFloat(rng) : static_cast<float>((rng() % 1500) / 1000.0);
}

int32_t Octaves(std::mt19937 &rng)
{
    return static_cast<int32_t>(rng() % 9);
}

void CheckFloat(difftest::Context &ctx, const char *what, float original, float ours)
{
    ++ctx.cases;
    if (!SameBits(original, ours)) {
        uint32_t a, b;
        std::memcpy(&a, &original, 4);
        std::memcpy(&b, &ours, 4);
        ctx.Fail("%s: original %08x ours %08x", what, a, b);
    }
}

// Both sides' objects given the same dimensions and seed.
struct Pair {
    Side o, n;
};

Pair MakeSeeded(difftest::Context &ctx, uint32_t dimensions, uint32_t seed)
{
    Pair p{Make(ctx, true), Make(ctx, false)};
    for (Side s : {p.o, p.n}) CallSlot<void>(s.face(), 0, dimensions, seed);
    return p;
}

} // namespace

DIFF_TEST(PerlinNoise_Layout, 0x98c2c0)
{
    Side o = Make(ctx, true), n = Make(ctx, false);
    // The count and dimensions the constructor sets.
    for (uint32_t off : {8u, 0xcu, 0x1910u, 0x1918u}) {
        uint32_t a, b;
        std::memcpy(&a, o.base + off, 4);
        std::memcpy(&b, n.base + off, 4);
        ++ctx.cases;
        if (a != 0 || b != 0) ctx.Fail("word at %x (the count, dimensions or a vtordisp): original %x ours %x", off, a, b);
    }
    // The vbtables: the main one (at +4) and the interface's own (at +0x1920).
    struct Bt {
        uint32_t at, count;
    };
    for (Bt bt : {Bt{4, 3}, Bt{0x1920, 2}}) {
        const int32_t *ot = *reinterpret_cast<int32_t **>(o.base + bt.at);
        const int32_t *nt = *reinterpret_cast<int32_t **>(n.base + bt.at);
        for (uint32_t i = 0; i < bt.count; ++i) {
            ++ctx.cases;
            if (ot[i] != nt[i]) ctx.Fail("vbtable at %x [%u]: original %d ours %d", bt.at, i, ot[i], nt[i]);
        }
    }
    // Our class on its own: the same offsets.
    cPerlinNoise c;
    char *cb = reinterpret_cast<char *>(&c);
    ++ctx.cases;
    if (sizeof(cPerlinNoise) != 0x1924) ctx.Fail("sizeof %zx", sizeof(cPerlinNoise));
    if (reinterpret_cast<char *>(static_cast<cIPerlinNoise *>(&c)) - cb != kFaceOffset) ctx.Fail("cIPerlinNoise face");
    if (reinterpret_cast<char *>(static_cast<cIGZUnknown *>(&c)) - cb != kUnknownOffset) ctx.Fail("cIGZUnknown face");
    if (reinterpret_cast<char *>(&c.mCount) - cb != 8) ctx.Fail("mCount");
    if (reinterpret_cast<char *>(&c.mDimensions) - cb != 0xc) ctx.Fail("mDimensions");
    if (reinterpret_cast<char *>(c.mGradient1) - cb != 0x10) ctx.Fail("mGradient1");
    if (reinterpret_cast<char *>(c.mGradient2) - cb != 0x410) ctx.Fail("mGradient2");
    if (reinterpret_cast<char *>(c.mGradient3) - cb != 0xc10) ctx.Fail("mGradient3");
    if (reinterpret_cast<char *>(c.mPermutation) - cb != 0x1810) ctx.Fail("mPermutation");
    Destroy(o);
    Destroy(n);
}

DIFF_TEST(PerlinNoise_Init, 0x98c4a0)
{
    RandomScope random;
    std::mt19937 rng(0x9e1a);
    const uint32_t fixedSeeds[] = {3, 1, 2, 12345, 0x7ffffffeu, 0x80000001u, 0xfffffffeu};
    for (uint32_t dims = 0; dims <= 4; ++dims) {
        for (uint32_t k = 0; k < 40; ++k) {
            uint32_t seed = k < 7 ? fixedSeeds[k] : static_cast<uint32_t>(rng());
            Pair p{Make(ctx, true), Make(ctx, false)};
            for (Side s : {p.o, p.n}) std::memset(s.base + 0x10, 0xa5, 0x1900);
            for (Side s : {p.o, p.n}) CallSlot<void>(s.face(), 0, dims, seed);
            ++ctx.cases;
            uint32_t dimO, dimN;
            std::memcpy(&dimO, p.o.base + 0xc, 4);
            std::memcpy(&dimN, p.n.base + 0xc, 4);
            if (dimO != dimN) ctx.Fail("dims %u seed %u: dimensions %u vs %u", dims, seed, dimO, dimN);
            if (std::memcmp(p.o.base + 0x10, p.n.base + 0x10, 0x1900) != 0) {
                uint32_t at = 0;
                while (p.o.base[0x10 + at] == p.n.base[0x10 + at]) ++at;
                ctx.Fail("dims %u seed %u: tables differ from +%x", dims, seed, 0x10 + at);
            }
            for (uint32_t q = 0; q < 6; ++q) {
                ++ctx.cases;
                bool a = CallSlot<bool>(p.o.face(), 1, q), b = CallSlot<bool>(p.n.face(), 1, q);
                if (a != b) ctx.Fail("IsValidDimension(%u) with %u: %d vs %d", q, dims, a, b);
            }
            Destroy(p.o);
            Destroy(p.n);
        }
    }
}

namespace {

void NoiseRound(difftest::Context &ctx, std::mt19937 &rng, const Pair &p, uint32_t cases)
{
    for (uint32_t k = 0; k < cases; ++k) {
        float x = PickFloat(rng), y = PickFloat(rng), z = PickFloat(rng);
        uint32_t ix = PickInt(rng);
        int32_t iy = static_cast<int32_t>(PickInt(rng)), iz = static_cast<int32_t>(PickInt(rng));
        float fx = PickFraction(rng), fy = PickFraction(rng), fz = PickFraction(rng);
        CheckFloat(ctx, "Noise1Cell", CallSlot<float>(p.o.face(), 2, ix, fx), CallSlot<float>(p.n.face(), 2, ix, fx));
        CheckFloat(ctx, "Noise1", CallSlot<float>(p.o.face(), 3, x), CallSlot<float>(p.n.face(), 3, x));
        CheckFloat(ctx, "Noise2Cell", CallSlot<float>(p.o.face(), 4, ix, fx, iy, fy),
                   CallSlot<float>(p.n.face(), 4, ix, fx, iy, fy));
        CheckFloat(ctx, "Noise2", CallSlot<float>(p.o.face(), 5, x, y), CallSlot<float>(p.n.face(), 5, x, y));
        CheckFloat(ctx, "Noise3Cell", CallSlot<float>(p.o.face(), 6, ix, fx, iy, fy, iz, fz),
                   CallSlot<float>(p.n.face(), 6, ix, fx, iy, fy, iz, fz));
        CheckFloat(ctx, "Noise3", CallSlot<float>(p.o.face(), 7, x, y, z), CallSlot<float>(p.n.face(), 7, x, y, z));
    }
}

void FractalRound(difftest::Context &ctx, std::mt19937 &rng, const Pair &p, uint32_t cases)
{
    for (uint32_t k = 0; k < cases; ++k) {
        float x = PickFloat(rng), y = PickFloat(rng), z = PickFloat(rng);
        int32_t ix = static_cast<int32_t>(PickInt(rng)), iy = static_cast<int32_t>(PickInt(rng));
        int32_t iz = static_cast<int32_t>(PickInt(rng));
        float fx = PickFraction(rng), fy = PickFraction(rng), fz = PickFraction(rng);
        float amp = Amplitude(rng), gain = Gain(rng);
        // A NaN amplitude with its own payload or sign meets the cell's canonical NaN in the accumulate multiply: the
        // x87 keeps the larger significand. Only with a finite gain, where the amplitude update has no second NaN.
        if (rng() % 6 == 0 && std::isfinite(gain)) {
            static const uint32_t kPayloads[] = {0x7fc12345u, 0x7fe00001u, 0xffc00000u, 0xffc00001u, 0x7fc00001u};
            const uint32_t bits = kPayloads[rng() % 5];
            std::memcpy(&amp, &bits, 4);
        }
        int32_t oct = Octaves(rng);
        CheckFloat(ctx, "Fractal1Cell", CallSlot<float>(p.o.face(), 8, ix, fx, amp, gain, oct),
                   CallSlot<float>(p.n.face(), 8, ix, fx, amp, gain, oct));
        CheckFloat(ctx, "Fractal1", CallSlot<float>(p.o.face(), 9, x, amp, gain, oct),
                   CallSlot<float>(p.n.face(), 9, x, amp, gain, oct));
        CheckFloat(ctx, "Fractal2Cell", CallSlot<float>(p.o.face(), 10, ix, fx, iy, fy, amp, gain, oct),
                   CallSlot<float>(p.n.face(), 10, ix, fx, iy, fy, amp, gain, oct));
        CheckFloat(ctx, "Fractal2", CallSlot<float>(p.o.face(), 11, x, y, amp, gain, oct),
                   CallSlot<float>(p.n.face(), 11, x, y, amp, gain, oct));
        CheckFloat(ctx, "Fractal3Cell", CallSlot<float>(p.o.face(), 12, ix, fx, iy, fy, iz, fz, amp, gain, oct),
                   CallSlot<float>(p.n.face(), 12, ix, fx, iy, fy, iz, fz, amp, gain, oct));
        CheckFloat(ctx, "Fractal3", CallSlot<float>(p.o.face(), 13, x, y, z, amp, gain, oct),
                   CallSlot<float>(p.n.face(), 13, x, y, z, amp, gain, oct));
    }
}

} // namespace

DIFF_TEST(PerlinNoise_Noise, 0x98c6d0)
{
    RandomScope random;
    std::mt19937 rng(0x70c1);
    for (uint32_t dims = 1; dims <= 3; ++dims) {
        Pair p = MakeSeeded(ctx, dims == 1 ? 3 : 3, 100 + dims);
        for (difftest::X87Pc pc : {difftest::X87Pc::Bits53, difftest::X87Pc::Bits24}) {
            difftest::X87Precision scope(pc);
            NoiseRound(ctx, rng, p, 1500);
        }
        Destroy(p.o);
        Destroy(p.n);
    }
}

DIFF_TEST(PerlinNoise_Fractal, 0x98ce00)
{
    RandomScope random;
    std::mt19937 rng(0x70c2);
    for (uint32_t seed : {7u, 4242u}) {
        Pair p = MakeSeeded(ctx, 3, seed);
        for (difftest::X87Pc pc : {difftest::X87Pc::Bits53, difftest::X87Pc::Bits24}) {
            difftest::X87Precision scope(pc);
            FractalRound(ctx, rng, p, 1200);
        }
        Destroy(p.o);
        Destroy(p.n);
    }
}

DIFF_TEST(PerlinNoise_OctaveCount, 0x98d3f0)
{
    // The static initialiser 0x13e3a0 sets the global to 1 / ln 2 in the game; the harness maps the image without it.
    *ctx.image.At<float>(kReciprocalLog) = 1.0f / static_cast<float>(std::log(2.0));
    std::mt19937 rng(0x70c3);
    using HelperFn = int32_t(__cdecl *)(float, float, float, float, float);
    HelperFn helper = ctx.Original<HelperFn>(kHelper);
    Side o = Make(ctx, true), n = Make(ctx, false);
    for (uint32_t k = 0; k < 6000; ++k) {
        float a[5];
        for (float &f : a) {
            switch (rng() % 5) {
            case 0: f = PickFloat(rng); break;
            case 1: f = 0.0f; break;
            default: f = static_cast<float>((rng() % 100000) / 1000.0 * ((rng() % 8) ? 1.0 : -1.0)); break;
            }
        }
        for (difftest::X87Pc pc : {difftest::X87Pc::Bits53, difftest::X87Pc::Bits24}) {
            difftest::X87Precision scope(pc);
            int32_t want = helper(a[0], a[1], a[2], a[3], a[4]);
            int32_t viaHelper = cPerlinNoise::OctaveCountOf(a[0], a[1], a[2], a[3], a[4]);
            int32_t viaOriginalSlot = CallSlot<int32_t>(o.face(), 14, a[0], a[1], a[2], a[3], a[4]);
            int32_t viaOurSlot = CallSlot<int32_t>(n.face(), 14, a[0], a[1], a[2], a[3], a[4]);
            ++ctx.cases;
            if (want != viaHelper || want != viaOriginalSlot || want != viaOurSlot)
                ctx.Fail("OctaveCount(%g %g %g %g %g): helper %d, ours %d, original slot %d, our slot %d", a[0], a[1],
                         a[2], a[3], a[4], want, viaHelper, viaOriginalSlot, viaOurSlot);
        }
    }
    Destroy(o);
    Destroy(n);
}

DIFF_TEST(PerlinNoise_RefCount, 0x98d330)
{
    std::mt19937 rng(0x70c4);
    const uint32_t iids[] = {1, 0xbd33210e, 0, 2, 3, 0xffffffffu, 0xbd33210f, 0x6a836d51};
    for (uint32_t round = 0; round < 40; ++round) {
        Side s[2] = {Make(ctx, true), Make(ctx, false)};
        std::vector<uint32_t> log[2];
        for (uint32_t step = 0; step < 60; ++step) {
            uint32_t op = rng() % 5;
            uint32_t iid = (rng() % 6 == 0) ? static_cast<uint32_t>(rng()) : iids[rng() % 8];
            bool nullOut = rng() % 9 == 0;
            for (int w = 0; w < 2; ++w) {
                void *out = reinterpret_cast<void *>(0x5ec0de);
                uint32_t count;
                std::memcpy(&count, s[w].base + 8, 4);
                if (op == 0 || count == 0) {
                    log[w].push_back(CallSlot<uint32_t>(s[w].unknown(), 1));
                } else if (op <= 2) {
                    bool ok = CallSlot<bool>(s[w].unknown(), 0, iid, nullOut ? nullptr : &out);
                    log[w].push_back(ok);
                    log[w].push_back(ok ? static_cast<uint32_t>(static_cast<char *>(out) - s[w].base)
                                        : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out)));
                } else if (count > 1) {
                    log[w].push_back(CallSlot<uint32_t>(s[w].unknown(), 2));
                } else {
                    log[w].push_back(CallSlot<uint32_t>(s[w].unknown(), 1));
                }
                std::memcpy(&count, s[w].base + 8, 4);
                log[w].push_back(count);
            }
        }
        ++ctx.cases;
        if (log[0] != log[1]) {
            size_t i = 0;
            while (i < log[0].size() && log[0][i] == log[1][i]) ++i;
            ctx.Fail("round %u: the logs differ at %zu (original %u ours %u)", round, i, log[0][i], log[1][i]);
        }
        // Release every reference: the last one destroys the object, the answers must agree.
        for (int w = 0; w < 2; ++w) {
            uint32_t count;
            std::memcpy(&count, s[w].base + 8, 4);
            uint32_t last = 99;
            for (uint32_t i = 0; i < count; ++i) last = CallSlot<uint32_t>(s[w].unknown(), 2);
            log[w].assign(1, last);
        }
        ++ctx.cases;
        if (log[0] != log[1]) ctx.Fail("round %u: the last Release answered %u vs %u", round, log[0][0], log[1][0]);
    }
}
