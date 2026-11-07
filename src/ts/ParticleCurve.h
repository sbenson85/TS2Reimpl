#pragma once
// The curve and key-list readings of the particle code (lane WQ, rendering row E3). 0xbb5620 is the out-of-line curve
// reading (22 call sites); cTSParticlesGeometry's vertex body has the same reading inlined, with its colour and pivot lists
// beside it. All of it is scalar SSE single with the first-operand NaN rule of fx::Add / fx::Mul.
#include "ts/EffectsTransform.h"
#include "ts/cTSEffectCurve.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace fx {

// float to double, the double library call, back to float.
inline float SinF(float a) { return static_cast<float>(std::sin(static_cast<double>(a))); }
inline float CosF(float a) { return static_cast<float>(std::cos(static_cast<double>(a))); }
inline float SqrtF(float a) { return static_cast<float>(std::sqrt(static_cast<double>(a))); }

// An unsigned word as float through double (the original's cvtdq2pd + 2^32 table + cvtpd2ps).
inline float FromUnsigned(uint32_t value) { return static_cast<float>(static_cast<double>(value)); }

// The key-list position of `x`: the nearest integer by the 2^23 + 2^22 trick, moved up when i + 1 converts to exactly x.
// mIndex is what the list is read at, mFraction the rest (zero or negative: stay on the key).
struct KeyPosition {
    int32_t mIndex;
    float mFraction;
};
inline KeyPosition FindKey(float x)
{
    const float shifted = Add(Sub(x, 0.5f), 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &shifted, 4);
    int32_t index = static_cast<int32_t>(bits + 0xb4c00000u);
    if (static_cast<float>(index + 1) == x) index = index + 1;
    return {index, Sub(x, FromUnsigned(static_cast<uint32_t>(index)))};
}

// 0xbb5620: a curve at t. A wave is amplitude * sin((t - v[2]) * v[1]) + offset at any t; a list is read in [0, 1] (0 out
// of range, for NaN, and for any other kind), one value being a constant, else between the two nearest keys.
inline float EvaluateCurve(const cTSEffectCurve &curve, float t)
{
    const float *const v = curve.mValues.data();
    if (curve.mKind == cTSEffectCurve::kWave) return Add(Mul(SinF(Mul(Sub(t, v[2]), v[1])), v[0]), v[3]);
    if (curve.mKind != cTSEffectCurve::kList || !(t >= 0.0f) || !(1.0f >= t)) return 0.0f;
    const int32_t count = static_cast<int32_t>(curve.mValues.size());
    if (count == 1) return v[0];
    const KeyPosition key = FindKey(Mul(FromUnsigned(static_cast<uint32_t>(count - 1)), t));
    if (key.mFraction > 0.0f) return Add(Mul(Sub(v[key.mIndex + 1], v[key.mIndex]), key.mFraction), v[key.mIndex]);
    return v[key.mIndex];
}

} // namespace fx
