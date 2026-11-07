// Differential test for the rotation between two directions 0x1a6db0 (lane XB, math::Quaternion::SetRotationBetween,
// src/misc/Quaternion.cpp): our replacement against the original at its RVA, outputs (four floats and the guard words
// around them) and the returned pointer compared bit for bit, at both x87 precisions (the dot product is the x87 Dot87).
// Cases: unit directions, lengths the function does not expect, the half vector's square just around 2^-23 on both sides
// and on it, opposite directions on either side of the tie between the two pairs of components, zeros, infinities and NaNs.
// RVAs these tests reach: 0x1a6db0, 0x18fbf0 (Dot87, ours), the CRT root 0x82fc60.

#include "difftest.h"
#include "misc/Quaternion.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>

namespace {

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float Float(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

constexpr uint32_t kEdges[] = {0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x00000001, 0x80000001, 0x00800000,
                               0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001, 0x7fa00000,
                               0x3f000000, 0x34000000, 0x39b504f3, 0x39b504f4};

struct Directions {
    float from[3], to[3];
};

void Unit(float *v, std::mt19937 &rng)
{
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    float l = 0.0f;
    do {
        for (int i = 0; i < 3; ++i) v[i] = d(rng);
        l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    } while (l < 0.01f);
    for (int i = 0; i < 3; ++i) v[i] /= l;
}

// The six kinds of case.
Directions Make(unsigned kind, std::mt19937 &rng)
{
    Directions c;
    switch (kind) {
    case 0: // two directions
        Unit(c.from, rng);
        Unit(c.to, rng);
        break;
    case 1: { // opposite, up to a tilt around the threshold
        Unit(c.from, rng);
        const float tilt = Float(0x39000000u + rng() % 0x01800000u) * (rng() % 2 ? 1.0f : -1.0f);
        for (int i = 0; i < 3; ++i) c.to[i] = -c.from[i];
        if (rng() % 2) c.to[rng() % 3] += tilt;
        break;
    }
    case 2: { // exactly on the threshold: the half vector along an axis at 2^-11.5, or a value whose square rounds to it
        for (float &f : c.from) f = 0.0f;
        const unsigned axis = rng() % 3;
        c.from[axis] = 1.0f;
        for (int i = 0; i < 3; ++i) c.to[i] = -c.from[i];
        const float pick[] = {Float(0x39b504f3), Float(0x39b504f4), Float(0x39b504f2), Float(0x39b504f5)};
        c.to[axis] += pick[rng() % 4] * (rng() % 2 ? 1.0f : -1.0f);
        break;
    }
    case 3: { // opposite, with x^2 + y^2 and x^2 + z^2 tied or nearly so
        const float x = float(int(rng() % 7) - 3) / 4.0f, y = float(int(rng() % 7) - 3) / 4.0f;
        const float z = rng() % 3 ? y : (rng() % 2 ? -y : float(int(rng() % 7) - 3) / 4.0f);
        c.from[0] = x;
        c.from[1] = rng() % 2 ? y : std::nextafter(y, 2.0f);
        c.from[2] = z;
        for (int i = 0; i < 3; ++i) c.to[i] = -c.from[i];
        break;
    }
    case 4: // lengths and edges
        for (float *v : {c.from, c.to})
            for (int i = 0; i < 3; ++i) v[i] = rng() % 3 ? Float(kEdges[rng() % std::size(kEdges)]) : float(int(rng() % 9) - 4);
        break;
    default: { // raw bits, and two NaNs of different payloads
        for (float *v : {c.from, c.to})
            for (int i = 0; i < 3; ++i) v[i] = Float(rng());
        float *all = c.from;
        for (int n = 0; n < 2; ++n) {
            const unsigned at = rng() % 6;
            all[at] = Float((rng() & 0x80000000u) | 0x7f800000u | (rng() % 4 ? 0x00400000u : 0) | (1 + rng() % 0x3ffffe));
        }
        break;
    }
    }
    return c;
}

} // namespace

DIFF_TEST(Quaternion_SetRotationBetween, 0x1a6db0)
{
    using math::Quaternion;
    auto orig = ctx.Original<float *(__thiscall *)(void *, const float *, const float *)>(0x1a6db0);
    std::mt19937 rng(0x1a6db0);
    uint32_t halfTurnXY = 0, halfTurnXZ = 0, normal = 0;
    for (uint32_t i = 0; i < 300000; ++i) {
        const unsigned kind = i % 6;
        const Directions c = Make(kind, rng);
        std::unique_ptr<difftest::X87Precision> precision;
        if (i & 8) precision = std::make_unique<difftest::X87Precision>(difftest::X87Pc::Bits24);
        struct {
            float guard0;
            float q[4];
            float guard1;
        } out[2];
        for (auto &o : out) {
            for (float *p = &o.guard0; p <= &o.guard1; ++p) *p = Float(0x7fc0dead);
            *reinterpret_cast<uint32_t *>(&o.guard0) = 0x5c5c5c5c;
            *reinterpret_cast<uint32_t *>(&o.guard1) = 0x5c5c5c5c;
        }
        float *const a = orig(out[0].q, c.from, c.to);
        Quaternion *const b = reinterpret_cast<Quaternion *>(out[1].q)->SetRotationBetween(c.from, c.to);
        ++ctx.cases;
        if (a != out[0].q || reinterpret_cast<float *>(b) != out[1].q) ctx.Fail("case %u: returned %p / %p", i, a, b);
        if (std::memcmp(&out[0], &out[1], sizeof(out[0])) != 0)
            ctx.Fail("case %u kind %u: %08x %08x %08x %08x / %08x %08x %08x %08x", i, kind, Bits(out[0].q[0]), Bits(out[0].q[1]),
                     Bits(out[0].q[2]), Bits(out[0].q[3]), Bits(out[1].q[0]), Bits(out[1].q[1]), Bits(out[1].q[2]), Bits(out[1].q[3]));
        if (Bits(out[1].q[3]) == 0) {
            if (out[1].q[2] == 0.0f && Bits(out[1].q[2]) == 0) ++halfTurnXY;
            else if (Bits(out[1].q[1]) == 0) ++halfTurnXZ;
        } else {
            ++normal;
        }
    }
    if (normal < 50000 || halfTurnXY < 5000 || halfTurnXZ < 5000) ctx.Fail("cases ran dry: %u %u %u", normal, halfTurnXY, halfTurnXZ);
}
