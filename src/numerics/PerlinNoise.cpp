#include "numerics/PerlinNoise.h"

#include "misc/X87.h"
#include "rz/cRZRandom.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>

namespace nGZNumerics {

namespace {

constexpr float kTwoPi = 6.2831855f; // 0xda20f0

// The cubic ease 3t^2 - 2t^3 as the original orders it: (3 - 2t) * (t * t).
inline float Smooth(float t) { return (3.0f - (t + t)) * (t * t); }

// The whole and fractional parts, truncated and then moved down for a negative input (the original's floor).
inline void Split(float x, int32_t &i, float &f)
{
    i = static_cast<int32_t>(x);
    f = x - static_cast<float>(i);
    if (x < 0.0f && f < 0.0f) {
        f = f + 1.0f;
        i = static_cast<int32_t>(static_cast<uint32_t>(i) - 1u);
    }
}

// A fractal slot's running sum: the cell's answer arrives in st0, the x87 multiplies by the amplitude, adds the sum and
// stores a float.
// x87 arithmetic on two NaNs keeps the one with the larger significand (the positive one when they are equal), where SSE
// keeps the first operand; with one NaN, that one.
inline double X87Binary(double reg, double mem, bool multiply)
{
    if (reg != reg && mem != mem) {
        uint64_t br, bm;
        std::memcpy(&br, &reg, 8);
        std::memcpy(&bm, &mem, 8);
        const uint64_t sr = br & 0x000fffffffffffffull, sm = bm & 0x000fffffffffffffull;
        if (sr != sm)
            return sm > sr ? mem : reg;
        return (br >> 63) ? mem : reg;
    }
    return x87::Rounded(multiply ? reg * mem : reg + mem);
}

inline float Accumulate(float cell, float amplitude, float sum)
{
    const double product = X87Binary(static_cast<double>(cell), static_cast<double>(amplitude), true);
    return static_cast<float>(X87Binary(product, static_cast<double>(sum), false));
}

// Doubles a cell coordinate for the next octave: the integer part gains the bit that crosses 1.
inline void Double(int32_t &i, float &f)
{
    float d = f + f;
    i = static_cast<int32_t>(static_cast<uint32_t>(i) * 2u + (d < 1.0f ? 0u : 1u));
    f = (1.0f <= d) ? d - 1.0f : d;
}

// 1 / ln 2: the static initialiser 0x13e3a0's float at 0x10f4fbc.
inline float ReciprocalLn2() { return 1.0f / static_cast<float>(std::log(2.0)); }

} // namespace

cIGZUnknown *__cdecl cPerlinNoise::Create()
{
    return static_cast<cIGZUnknown *>(new cPerlinNoise);
}

cPerlinNoise::~cPerlinNoise() {}

bool cPerlinNoise::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == 1) *out = static_cast<cIGZUnknown *>(this);
    else if (iid == cIPerlinNoise::kIid) *out = static_cast<cIPerlinNoise *>(this);
    else return false;
    AddRef();
    return true;
}

uint32_t cPerlinNoise::AddRef()
{
    return ++mCount;
}

uint32_t cPerlinNoise::Release()
{
    uint32_t n = --mCount;
    if (n == 0) {
        delete this;
        return 0;
    }
    return n;
}

void cPerlinNoise::Init(uint32_t dimensions, uint32_t seed)
{
    cRZRandom rng;
    rng.ConstructAt(0xffffffffu, 0x98c4b9);
    rng.Seed(seed);
    mDimensions = dimensions;
    for (uint32_t i = 0; i < 256; ++i) {
        mPermutation[i] = static_cast<uint8_t>(i);
        float u = static_cast<float>(rng.RandomDoubleUniform());
        mGradient1[i] = 1.0f - (u + u);
        if (mDimensions > 1) {
            float angle = static_cast<float>(rng.RandomDoubleUniform());
            double a = static_cast<double>(angle * kTwoPi);
            mGradient2[i][0] = static_cast<float>(std::cos(a));
            mGradient2[i][1] = static_cast<float>(std::sin(a));
            if (mDimensions > 2) {
                float x, y, z, d;
                do {
                    do {
                        x = static_cast<float>(rng.RandomDoubleUniform());
                        y = static_cast<float>(rng.RandomDoubleUniform());
                        z = static_cast<float>(rng.RandomDoubleUniform());
                        d = y * y + x * x + z * z;
                    } while (1.0f < d);
                } while (d <= 0.0f);
                float s = 1.0f / static_cast<float>(std::sqrt(static_cast<double>(d)));
                mGradient3[i][0] = s * x;
                mGradient3[i][1] = s * y;
                mGradient3[i][2] = s * z;
            }
        }
    }
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t j = rng.RandomUint32Uniform(0x100);
        uint8_t t = mPermutation[i];
        mPermutation[i] = mPermutation[j];
        mPermutation[j] = t;
    }
}

bool cPerlinNoise::IsValidDimension(uint32_t dimension)
{
    return dimension <= mDimensions;
}

float cPerlinNoise::Noise1Cell(uint32_t ix, float fx)
{
    float a = fx * mGradient1[ix & 0xff];
    float b = (fx - 1.0f) * mGradient1[(ix + 1) & 0xff];
    return (b - a) * Smooth(fx) + a;
}

float cPerlinNoise::Noise1(float x)
{
    int32_t i;
    float f;
    Split(x, i, f);
    return Noise1Cell(static_cast<uint32_t>(i), f);
}

float cPerlinNoise::Noise2Cell(uint32_t ix, float fx, int32_t iy, float fy)
{
    uint32_t p0 = mPermutation[ix & 0xff];
    uint32_t p1 = mPermutation[(ix + 1) & 0xff];
    uint32_t uy = static_cast<uint32_t>(iy);
    uint32_t h00 = (p0 + uy) & 0xff;
    uint32_t h10 = (p1 + uy) & 0xff;
    uint32_t h01 = (uy + 1 + p0) & 0xff;
    uint32_t h11 = (uy + 1 + p1) & 0xff;
    float fx1 = fx - 1.0f;
    float fy1 = fy - 1.0f;
    float d00 = fx * mGradient2[h00][0] + fy * mGradient2[h00][1];
    float u = Smooth(fx);
    float d10 = fx1 * mGradient2[h10][0] + fy * mGradient2[h10][1];
    float d01 = fx * mGradient2[h01][0] + fy1 * mGradient2[h01][1];
    float d11 = fx1 * mGradient2[h11][0] + fy1 * mGradient2[h11][1];
    float l0 = (d10 - d00) * u + d00;
    float l1 = (d11 - d01) * u + d01;
    return (l1 - l0) * Smooth(fy) + l0;
}

float cPerlinNoise::Noise2(float x, float y)
{
    int32_t ix, iy;
    float fx, fy;
    Split(x, ix, fx);
    Split(y, iy, fy);
    return Noise2Cell(static_cast<uint32_t>(ix), fx, iy, fy);
}

float cPerlinNoise::Noise3Cell(uint32_t ix, float fx, int32_t iy, float fy, int32_t iz, float fz)
{
    uint32_t p0 = mPermutation[ix & 0xff];
    uint32_t p1 = mPermutation[(ix + 1) & 0xff];
    uint32_t uy = static_cast<uint32_t>(iy);
    uint32_t uz = static_cast<uint32_t>(iz);
    // The permuted row of each (x, y) corner, then the z corners' gradients.
    uint32_t r00 = mPermutation[(p0 + uy) & 0xff];
    uint32_t r10 = mPermutation[(p1 + uy) & 0xff];
    uint32_t r01 = mPermutation[(uy + 1 + p0) & 0xff];
    uint32_t r11 = mPermutation[(uy + 1 + p1) & 0xff];
    float fx1 = fx - 1.0f;
    float fy1 = fy - 1.0f;
    float fz1 = fz - 1.0f;
    auto dot = [this](uint32_t h, float x, float y, float z) {
        const float *g = mGradient3[h & 0xff];
        return y * g[1] + x * g[0] + z * g[2];
    };
    float ux = Smooth(fx);
    float uy_ = Smooth(fy);
    // z = 0 layer
    float c000 = dot(r00 + uz, fx, fy, fz);
    float c100 = dot(r10 + uz, fx1, fy, fz);
    float x0 = (c100 - c000) * ux + c000;
    float c010 = dot(r01 + uz, fx, fy1, fz);
    float c110 = dot(r11 + uz, fx1, fy1, fz);
    float x1 = (c110 - c010) * ux + c010;
    float layer0 = (x1 - x0) * uy_ + x0;
    // z = 1 layer
    uint32_t z1 = uz + 1;
    float c001 = dot(z1 + r00, fx, fy, fz1);
    float c101 = dot(z1 + r10, fx1, fy, fz1);
    float x2 = (c101 - c001) * ux + c001;
    float c011 = dot(r01 + z1, fx, fy1, fz1);
    float c111 = dot(z1 + r11, fx1, fy1, fz1);
    float x3 = (c111 - c011) * ux + c011;
    float layer1 = (x3 - x2) * uy_ + x2;
    return (layer1 - layer0) * Smooth(fz) + layer0;
}

float cPerlinNoise::Noise3(float x, float y, float z)
{
    int32_t ix, iy, iz;
    float fx, fy, fz;
    Split(x, ix, fx);
    Split(y, iy, fy);
    Split(z, iz, fz);
    return Noise3Cell(static_cast<uint32_t>(ix), fx, iy, fy, iz, fz);
}

float cPerlinNoise::Fractal1Cell(int32_t ix, float fx, float amplitude, float gain, int32_t octaves)
{
    float sum = 0.0f;
    for (; octaves != 0; --octaves) {
        float r = Noise1Cell(static_cast<uint32_t>(ix), fx);
        sum = Accumulate(r, amplitude, sum);
        Double(ix, fx);
        amplitude = amplitude * gain;
    }
    return sum;
}

float cPerlinNoise::Fractal1(float x, float amplitude, float gain, int32_t octaves)
{
    int32_t ix;
    float fx;
    Split(x, ix, fx);
    return Fractal1Cell(ix, fx, amplitude, gain, octaves);
}

float cPerlinNoise::Fractal2Cell(int32_t ix, float fx, int32_t iy, float fy, float amplitude, float gain,
                                 int32_t octaves)
{
    float sum = 0.0f;
    for (; octaves != 0; --octaves) {
        float r = Noise2Cell(static_cast<uint32_t>(ix), fx, iy, fy);
        sum = Accumulate(r, amplitude, sum);
        Double(ix, fx);
        Double(iy, fy);
        amplitude = amplitude * gain;
    }
    return sum;
}

float cPerlinNoise::Fractal2(float x, float y, float amplitude, float gain, int32_t octaves)
{
    int32_t ix, iy;
    float fx, fy;
    Split(x, ix, fx);
    Split(y, iy, fy);
    return Fractal2Cell(ix, fx, iy, fy, amplitude, gain, octaves);
}

float cPerlinNoise::Fractal3Cell(int32_t ix, float fx, int32_t iy, float fy, int32_t iz, float fz, float amplitude,
                                 float gain, int32_t octaves)
{
    float sum = 0.0f;
    for (; octaves != 0; --octaves) {
        float r = Noise3Cell(static_cast<uint32_t>(ix), fx, iy, fy, iz, fz);
        sum = Accumulate(r, amplitude, sum);
        Double(ix, fx);
        Double(iy, fy);
        Double(iz, fz);
        amplitude = amplitude * gain;
    }
    return sum;
}

float cPerlinNoise::Fractal3(float x, float y, float z, float amplitude, float gain, int32_t octaves)
{
    int32_t ix, iy, iz;
    float fx, fy, fz;
    Split(x, ix, fx);
    Split(y, iy, fy);
    Split(z, iz, fz);
    return Fractal3Cell(ix, fx, iy, fy, iz, fz, amplitude, gain, octaves);
}

int32_t cPerlinNoise::OctaveCount(float limit, float amplitude, float gain, float scale, float minAmplitude)
{
    return OctaveCountOf(limit, amplitude, gain, scale, minAmplitude);
}

int32_t cPerlinNoise::OctaveCountOf(float limit, float amplitude, float gain, float scale, float minAmplitude)
{
    if (gain == 0.0f) return 1;
    if (minAmplitude == 0.0f && scale == 0.0f) return -1;
    float t = FLT_MAX; // 0xda7660
    if (minAmplitude != 0.0f) {
        float num = static_cast<float>(std::log(static_cast<double>(amplitude / minAmplitude)));
        float den = static_cast<float>(std::log(static_cast<double>(1.0f / gain)));
        t = num / den;
    }
    if (0.0f < scale) {
        float v = static_cast<float>(std::log(static_cast<double>(limit / scale))) * ReciprocalLn2();
        t = (v < t) ? v : t;
    }
    float r = (1.0f > t) ? 1.0f : t;
    r = r + 0.5f;
    return static_cast<int32_t>(static_cast<int64_t>(r));
}

} // namespace nGZNumerics
