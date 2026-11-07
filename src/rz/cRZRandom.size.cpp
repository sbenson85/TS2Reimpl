#include "rz/cRZRandom.h"

#include <ctime>

namespace {

// 2^-32. cRZRandom keeps it as a float and multiplies in single precision; cRZRandomSuper uses double.
constexpr float kScaleF = 2.3283064365386963e-10f;
constexpr double kScaleD = 2.3283064365386963e-10;

// Seed used for UINT32_MAX: time mixed with clock.
uint32_t TimeSeed()
{
    if (gRZRandomClockSeedOverride) return gRZRandomClockSeedOverride(0, 0);
    uint32_t t = static_cast<uint32_t>(std::time(nullptr));
    uint32_t c = static_cast<uint32_t>(std::clock());
    return ((c << 16) | c) ^ t;
}

uint32_t gHashCalls = 0;

} // namespace

uint32_t (*gRZRandomClockSeedOverride)(int kind, uint32_t site) = nullptr;

cRZRandom *cRZRandom::Construct(uint32_t seed)
{
    Seed(seed);
    mUnknown = 0;
    return this;
}

cRZRandom *cRZRandom::ConstructAt(uint32_t seed, uint32_t originalSite)
{
    if (seed == UINT32_MAX && gRZRandomClockSeedOverride) seed = gRZRandomClockSeedOverride(0, originalSite);
    return Construct(seed);
}

void cRZRandom::Seed(uint32_t seed)
{
    if (seed == UINT32_MAX) seed = TimeSeed();
    mIntegerSeed = seed;
    mRealSeed = static_cast<int32_t>(seed);
}

uint32_t cRZRandom::RandomUint32Uniform()
{
    // seed' = seed * 1103515245 + 12345 (mod 2^32); the result is bits 16..47 of the full 64-bit value.
    uint64_t next = uint64_t(mIntegerSeed) * 1103515245u + 12345u;
    mIntegerSeed = uint32_t(next);
    return uint32_t(next >> 16);
}

uint32_t cRZRandom::RandomUint32Uniform(uint32_t scale)
{
    if (scale == 0) return 0;
    return static_cast<uint32_t>((uint64_t(RandomUint32Uniform()) * scale) >> 32);
}

int32_t cRZRandom::RandomSint32RangeUniform(int32_t min, int32_t max)
{
    return static_cast<int32_t>(RandomUint32Uniform(static_cast<uint32_t>(max - min)) + static_cast<uint32_t>(min));
}

double cRZRandom::RandomDoubleUniform()
{
    if (mRealSeed == 0) mRealSeed = static_cast<int32_t>(std::clock());
    mRealSeed = static_cast<int32_t>(static_cast<uint32_t>(mRealSeed) * 663608941u);
    float scaled = static_cast<float>(mRealSeed) * kScaleF;
    return static_cast<double>(scaled) + 0.5;
}

double cRZRandom::RandomDoubleRangeUniform(double min, double max)
{
    return RandomDoubleUniform() * (max - min) + min;
}

cRZRandomSuper *cRZRandomSuper::Construct(uint32_t seed)
{
    mNext = nullptr;
    mLeft = kN;
    Seed(seed);
    return this;
}

uint32_t __stdcall cRZRandomSuper::Hash(uint32_t a, uint32_t b)
{
    uint32_t ha = 0, hb = 0;
    for (int i = 0; i < 4; ++i) ha = ha * 257 + reinterpret_cast<const uint8_t *>(&a)[i];
    for (int i = 0; i < 4; ++i) hb = hb * 257 + reinterpret_cast<const uint8_t *>(&b)[i];
    return (gHashCalls++ + ha) ^ hb;
}

void cRZRandomSuper::Seed(uint32_t seed)
{
    if (seed == UINT32_MAX && gRZRandomClockSeedOverride) {
        seed = gRZRandomClockSeedOverride(1, 0);
    } else if (seed == UINT32_MAX) {
        uint32_t t = static_cast<uint32_t>(std::time(nullptr));
        seed = Hash(t, static_cast<uint32_t>(std::clock()));
    }
    // Knuth's 69069 generator fills the state: each word takes the high halves of two successive
    // values. The seed is forced odd first.
    uint32_t x = seed | 1u;
    for (int i = 0; i < kN; ++i) {
        uint32_t y = x * 69069u;
        mState[i] = (x & 0xFFFF0000u) | (y >> 16);
        x = y * 69069u + 69070u;
    }
    Reload();
}

void cRZRandomSuper::Reload()
{
    constexpr int kM = 397;
    auto twist = [](uint32_t u, uint32_t v) {
        uint32_t mixed = ((u & 0x80000000u) | (v & 0x7FFFFFFFu)) >> 1;
        return mixed ^ ((v & 1u) ? 0x9908B0DFu : 0u);
    };
    int i = 0;
    for (; i < kN - kM; ++i) mState[i] = mState[i + kM] ^ twist(mState[i], mState[i + 1]);
    for (; i < kN - 1; ++i) mState[i] = mState[i + kM - kN] ^ twist(mState[i], mState[i + 1]);
    mState[kN - 1] = mState[kM - 1] ^ twist(mState[kN - 1], mState[0]);
    mLeft = kN;
    mNext = mState;
}

uint32_t cRZRandomSuper::RandomUint32Uniform()
{
    if (--mLeft < 0) {
        Reload();
        --mLeft;
    }
    uint32_t y = *mNext++;
    y ^= y >> 11;
    y ^= (y << 7) & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    return y ^ (y >> 18);
}

uint32_t cRZRandomSuper::RandomUint32Uniform(uint32_t scale)
{
    return static_cast<uint32_t>((uint64_t(RandomUint32Uniform()) * scale) >> 32);
}

double cRZRandomSuper::RandomDoubleUniform()
{
    return static_cast<double>(static_cast<int32_t>(RandomUint32Uniform())) * kScaleD + 0.5;
}

double cRZRandomSuper::RandomDoubleRangeUniform(double min, double max)
{
    return RandomDoubleUniform() * (max - min) + min;
}
