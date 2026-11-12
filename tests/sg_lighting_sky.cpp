// Differential tests for nTSSG::cLightingManager's sky light (lane 261108b, scenegraph LG2; src/sg/cLightingManager.cpp).
// Hook shape: the original runs on the same object image as ours, each case at the x87's 53-bit and 24-bit precision.
//
// The world is tests/sg_lighting_world.h's, with the object's sky samples (+0x1f4..+0x1f8) pointed at this file's own
// buffer: 0 to 40 samples, now and then a byte count that isn't a multiple of 0x8c (the count rounds down; 0 samples
// with a non-empty buffer), and a few cases of 65536 to 65538 samples (the 16-bit sample index test). The current set's
// sky colour and reach (+0x158, +0x164) are drawn per case: reach above 0 (small, so most samples get nothing, or
// large), 0, -0, negative, NaN; colours near the game's zero vector (0x10d9424, bss: 0 unless a case sets it) by 0, 31,
// 32 or 33 ulps either way, signed zeros, denormals, and the zero vector itself moved next to the colour. The light
// records (0x6c bytes, an int16 sample index at +0x68) name samples in range, negative, at and past the count; counts
// 0 to 13 and negative. NaNs: one or two planted in the samples' first 18 floats (the average's components, the max
// form's choice), the colour and the samples' first 15 floats (the add's operand order), the reach.
//
// Compared: the fakes' event log, the object's and light sets' bytes, the whole sample buffer, and per test what it
// returns (below).
#include "sg_lighting_world.h"

#include <memory>
#include <vector>

namespace {

constexpr uint32_t kSampleFloats = 0x8c / 4;
constexpr uint32_t kSmallSamples = 41;
constexpr uint32_t kBigSamples = 65540;
constexpr uint32_t kZeroVector = 0x10d9424;
alignas(16) float gSamples[kSmallSamples * kSampleFloats];
alignas(16) uint8_t gLights[16 * 0x6c];

float Ulps(float f, int32_t k)
{
    // k ulps away from f on the ordered integer line (through the signed zeros)
    const uint32_t b = U(f);
    const int32_t ordered = (b >> 31) ? -int32_t(b & 0x7fffffffu) : int32_t(b);
    const int32_t moved = ordered + k;
    return Fl(moved < 0 ? (0x80000000u | uint32_t(-moved)) : uint32_t(moved));
}

float ColourComponent(std::mt19937 &gen)
{
    static const int32_t kSteps[] = {0, 31, 32, 33, -31, -32, -33, 1, -1, 30};
    switch (gen() % 6) {
    case 0: return Ulps(0.0f, kSteps[gen() % 10]);
    case 1: return gen() % 2 ? 0.0f : -0.0f;
    case 2: return Fl(gen() % 0x7fffff | (gen() % 2 ? 0x80000000u : 0u)); // a denormal
    default: return Nice(gen, 2.0f);
    }
}

// One case's draw (both sides set up from it alike).
struct SkyCase {
    uint32_t seed, world, samples, extra, sampleSeed, lightsSeed, nanPick;
    bool isBig;
    uint8_t flag;
    float reach, colour[3], zeroWords[3];
    int32_t count;
    int16_t indexes[16];
    unsigned mode;

    SkyCase(uint32_t s, uint32_t salt, bool bigAllowed) : seed(s)
    {
        std::mt19937 gen(seed * 7919u + salt);
        world = gen();
        const uint32_t r = seed % 1000;
        isBig = bigAllowed && (r == 17 || r == 517 || r == 917);
        samples = isBig ? 65536 + gen() % 3 : gen() % 8 ? 1 + gen() % 40 : gen() % 3;
        extra = !isBig && gen() % 5 == 0 ? 1 + gen() % 0x8b : 0;
        sampleSeed = gen();
        flag = uint8_t(gen() % 8 ? 1 + gen() % 255 : 0);
        switch (gen() % 10) {
        case 0: reach = 0.0f; break;
        case 1: reach = -0.0f; break;
        case 2: reach = -std::fabs(Nice(gen, 2.0f)); break;
        case 3: reach = NaN(gen); break;
        case 4: reach = float(1 + gen() % 100) / 1000.0f; break; // most samples above it
        case 5: reach = 100.0f + float(gen() % 1000); break;        // every sample below it
        default: reach = std::fabs(Nice(gen, 4.0f)); break;
        }
        const unsigned zeroKind = gen() % 4; // 0: the zero vector set near the colour
        zeroWords[0] = zeroWords[1] = zeroWords[2] = 0.0f;
        for (auto &c : colour) c = ColourComponent(gen);
        if (zeroKind == 0) {
            static const int32_t kSteps[] = {0, 31, 32, 33, -31, -32, -33, 5};
            for (unsigned c = 0; c < 3; ++c) zeroWords[c] = gen() % 4 ? Ulps(colour[c], kSteps[gen() % 8]) : Nice(gen, 2.0f);
        }
        if (gen() % 5 == 0) for (unsigned c = 0; c < 3; ++c) colour[c] = Ulps(zeroWords[c], int32_t(gen() % 70) - 35); // all near
        count = gen() % 7 ? int32_t(gen() % 14) : -int32_t(1 + gen() % 5);
        for (auto &i : indexes) {
            const int32_t n = int32_t(samples);
            switch (gen() % 8) {
            case 0: i = -1; break;
            case 1: i = int16_t(-int32_t(gen() % 32768) - 1); break;
            case 2: i = int16_t(n > 32767 ? 32767 : n); break;
            case 3: i = int16_t(n > 32768 ? 32767 : n - 1); break;
            case 4: i = 0; break;
            default: i = int16_t(n ? gen() % uint32_t(n > 32767 ? 32767 : n + 2) : gen() % 3); break;
            }
        }
        nanPick = gen();
        mode = gen() % 12; // 0..3 a planted NaN pair, else none or one
        lightsSeed = gen();
    }

    // The world, the sample buffer, the light records, the set's colour and reach, the zero vector and the planted NaNs.
    // Returns the sample buffer; `total` its floats.
    float *SetUp(float *big, uint32_t &total) const
    {
        Build(world);
        gLog.clear();
        gTexelNaN = ~0u;
        float *base = isBig ? big : gSamples;
        total = isBig ? kBigSamples * kSampleFloats : uint32_t(sizeof(gSamples) / 4);
        {
            std::mt19937 fill(sampleSeed);
            for (uint32_t i = 0; i < total; ++i) base[i] = isBig ? float(int32_t(fill() % 4001) - 2000) / 500.0f : Nice(fill, 4.0f);
        }
        {
            std::mt19937 fill(lightsSeed);
            for (auto &b : gLights) b = uint8_t(fill());
            for (unsigned k = 0; k < 16; ++k) std::memcpy(gLights + k * 0x6c + 0x68, &indexes[k], 2);
        }
        Put(0x1f4, P(base)), Put(0x1f8, P(base) + samples * 0x8c + extra), Put(0x1fc, P(base) + total * 4);
        gObject[0x44] = flag;
        auto &set = const_cast<cLightSet &>(Current());
        set.mSkyColour.x = colour[0], set.mSkyColour.y = colour[1], set.mSkyColour.z = colour[2];
        set.mSkyReach = reach;
        float *zero = GameAddress<float>(kZeroVector);
        zero[0] = zeroWords[0], zero[1] = zeroWords[1], zero[2] = zeroWords[2];
        std::mt19937 pick(nanPick);
        const uint32_t lastSample = samples ? samples - 1 : 0;
        const uint32_t s = pick() % 4 ? 1 + pick() % (lastSample ? lastSample : 1) : pick() % (lastSample + 1);
        float *sample = base + s * kSampleFloats;
        float *col = &set.mSkyColour.x;
        switch (mode) {
        case 0: { const unsigned j = pick() % 15; col[j % 3] = NaN(pick), sample[j] = NaN(pick); break; } // add order
        case 1: sample[pick() % 18] = NaN(pick), sample[pick() % 18] = NaN(pick); break; // the average, max form
        case 2: sample[2 + 3 * (pick() % 6)] = NaN(pick); break;                         // a z: the max form's pick
        case 3: col[pick() % 3] = NaN(pick), set.mSkyReach = pick() % 2 ? NaN(pick) : set.mSkyReach; break;
        default: {
            float *places[] = {&sample[pick() % 18], &sample[pick() % 15], &col[0], &col[1], &col[2], &set.mSkyReach};
            MaybeNaN(pick, places, 6);
        }
        }
        return base;
    }
};

// The zero vector as the harness has it (bss: zeros), put back after every side.
struct ZeroVectorGuard {
    float saved[3];
    ZeroVectorGuard() { std::memcpy(saved, GameAddress<float>(kZeroVector), sizeof(saved)); }
    ~ZeroVectorGuard() { std::memcpy(GameAddress<float>(kZeroVector), saved, sizeof(saved)); }
};

} // namespace

// 0x2d7ad0 AddSkyLight: the sky colour weighted into the sky samples, the weights vector resized to their count. The
// weights vector comes in empty, or with a size and capacity of its own (resize shrinks, grows within the capacity,
// reallocates). Also compared: the return value, the vector's size, capacity and contents (the whole capacity when
// the buffer wasn't reallocated).
DIFF_TEST(sg_lighting_sky, 0x2d7ad0)
{
    FillFakes();
    using SkyFn = uint8_t(__thiscall *)(void *, const uint8_t *, int32_t, std::vector<float> *);
    auto sky = ctx.Original<SkyFn>(0x2d7ad0);
    std::unique_ptr<float[]> big(new float[kBigSamples * kSampleFloats]);
    AtBothPrecisions(ctx, "sky", 3000, [&](uint32_t seed, const char *label) {
        const SkyCase c(seed, 0x2d7ad0u, true);
        std::mt19937 gen(seed ^ 0x5eedu);
        const unsigned vectorKind = gen() % 4; // 0: empty; else a size and capacity of its own
        const uint32_t capacity = gen() % 3 ? gen() % 48 : gen() % 4, size = capacity ? gen() % (capacity + 1) : 0;
        const uint32_t fillSeed = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ZeroVectorGuard guard;
            uint32_t total;
            float *base = c.SetUp(big.get(), total);
            std::vector<float> weights;
            if (vectorKind) {
                weights.reserve(capacity);
                std::mt19937 fill(fillSeed);
                for (uint32_t i = 0; i < capacity; ++i) weights.push_back(Nice(fill, 3.0f));
                weights.resize(size);
            }
            const size_t capacityBefore = weights.capacity();
            ctx.Mark(side ? "ours" : "original");
            uint32_t result;
            if (side == 0) result = sky(gObject, gLights, c.count, &weights);
            else result = Ours()->AddSkyLight(gLights, c.count, &weights) ? 1 : 0;
            Blob o = End(nullptr, 0);
            o.push_back(0xbbbb), o.push_back(result & 0xffu);
            o.push_back(uint32_t(weights.size())), o.push_back(uint32_t(weights.capacity()));
            const bool same = weights.capacity() == capacityBefore; // not reallocated
            o.push_back(same ? 1u : 0u);
            const uint32_t shown = uint32_t(same ? weights.capacity() : weights.size());
            for (uint32_t i = 0; i < shown; ++i) o.push_back(U(weights.data()[i]));
            o.push_back(0xcccc);
            for (uint32_t i = 0; i < total; ++i) o.push_back(U(base[i]));
            outcome[side] = std::move(o);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

namespace {

// Slot 45's fakes: the object's own vtable (slot 36, a percentage per sample index: an int16 in a word whose upper half
// is noise, so the sign extension shows) and the sky target at +0x18 (slot 22 logs the index and the colour's bits).
void *gSelfVT[46];
void *gTargetVT[23];
struct FakeTarget {
    void *vtable;
    uint32_t salt;
} gTarget;
uint32_t gPercentSalt;

uint32_t __fastcall PercentSlot(void *, void *, uint32_t index)
{
    gLog.push_back(0x36), gLog.push_back(index);
    const uint32_t m = Mix(index, gPercentSalt);
    static const uint16_t kPercents[] = {100, 0, 50, 0xffff, 0x8000, 0x7fff, 0xff9c, 1, 200, 33};
    const uint16_t p = m % 3 ? kPercents[(m >> 4) % 10] : uint16_t(m >> 8);
    return (m & 0xffff0000u) | p;
}

uint32_t __fastcall TargetSlot(FakeTarget *self, void *, uint32_t index, const float *colour)
{
    gLog.push_back(0x22), gLog.push_back(self->salt), gLog.push_back(index);
    gLog.push_back(U(colour[0])), gLog.push_back(U(colour[1])), gLog.push_back(U(colour[2]));
    return 0xdead0000u | index;
}

} // namespace

// 0x2d2420 slot 45 SendSkyLight: AddSkyLight into a weights vector of its own, then each sky-lit sample's colour
// (sky colour * weight * percent / 100) to the target at +0x18. Fewer big cases than the AddSkyLight test (each one
// calls the fakes 65535 times).
DIFF_TEST(sg_lighting_sky_send, 0x2d2420)
{
    FillFakes();
    gSelfVT[36] = reinterpret_cast<void *>(&PercentSlot);
    gTargetVT[22] = reinterpret_cast<void *>(&TargetSlot);
    using SendFn = void(__thiscall *)(void *, const uint8_t *, int32_t);
    auto send = ctx.Original<SendFn>(0x2d2420);
    std::unique_ptr<float[]> big(new float[kBigSamples * kSampleFloats]);
    AtBothPrecisions(ctx, "send", 3000, [&](uint32_t seed, const char *label) {
        const SkyCase c(seed, 0x2d2420u, seed % 1500 == 517);
        std::mt19937 gen(seed ^ 0x5e2du);
        const uint32_t percentSalt = gen(), targetSalt = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ZeroVectorGuard guard;
            uint32_t total;
            float *base = c.SetUp(big.get(), total);
            gPercentSalt = percentSalt;
            gTarget = {gTargetVT, targetSalt};
            Put(0, P(gSelfVT)), Put(0x18, P(&gTarget));
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) send(gObject, gLights, c.count);
            else Ours()->SendSkyLight(gLights, c.count);
            Blob o = End(nullptr, 0);
            o.push_back(0xcccc);
            for (uint32_t i = 0; i < total; ++i) o.push_back(U(base[i]));
            outcome[side] = std::move(o);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}
