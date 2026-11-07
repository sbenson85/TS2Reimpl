// Differential tests for the geometry helpers of src/simantics/SimGeometry.* (lane AJ): the quaternion rotation 0x1a6c40, the
// object vector reader 0x7dd0d0, float sine and cosine 0x5a9de0 / 0x5a9a90, the tile and position conversions 0x7dcb60 /
// 0x7dcc50 / 0x7dd620 and the notch functions 0x7dcde0 / 0x7dcdb0 / 0x7dccd0. Floats are compared bit for bit (NaN payloads,
// signed zeros), on random bit patterns as well as ordinary values.
#include "difftest.h"

#include "simantics/SimGeometry.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

float FromBits(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

// A float: any bit pattern, a small ordinary value, a notable constant or an angle.
float RandomFloat(std::mt19937 &r)
{
    switch (r() % 8) {
    case 0: return FromBits(r());
    case 1: {
        const uint32_t edges[] = {0u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7f800000u, 0xff800000u, 0x7fc00000u, 0x00000001u, 0x007fffffu, 0x7f7fffffu, 0x3f000000u};
        return FromBits(edges[r() % 11]);
    }
    case 2: return static_cast<float>(static_cast<int32_t>(r() % 2001) - 1000) / 100.0f;
    default: return static_cast<float>(static_cast<int32_t>(r() % 20001) - 10000) / 1000.0f;
    }
}

float NoNaN(float f)
{
    return (Bits(f) & 0x7fffffffu) > 0x7f800000u ? 1.0f : f;
}

float RandomUnitish(std::mt19937 &r)
{
    return static_cast<float>(static_cast<int32_t>(r() % 2001) - 1000) / 1000.0f;
}

} // namespace

DIFF_TEST(SimGeometry_RotateByQuaternion, 0x1a6c40)
{
    auto original = ctx.Original<void(__cdecl *)(float *, const float *)>(0x1a6c40);
    std::mt19937 r(0x1a6c40);
    for (int i = 0; i < 30000; ++i) {
        float v[3], q[4];
        const bool wild = i % 4 == 0;
        // Infinities and zeros are in, NaN inputs are not: when two different NaNs meet in one SSE operation the result is the
        // first operand's, which depends on the operand order the compiler picked for each commutative operation; the
        // replacement does not reproduce that order (the game never passes NaNs here). NaNs the operations make themselves are
        // all the same default NaN, so those compare equal.
        for (float &f : v) f = wild ? NoNaN(RandomFloat(r)) : RandomUnitish(r) * 10.0f;
        for (float &f : q) f = wild ? NoNaN(RandomFloat(r)) : RandomUnitish(r);
        // Canaries around the vector: nothing past the third element is written.
        struct Guarded {
            float before, v[3], after;
        } a = {1.5f, {v[0], v[1], v[2]}, 2.5f}, b = a;
        const float qa[4] = {q[0], q[1], q[2], q[3]};
        original(a.v, q);
        simgeom::RotateByQuaternion(b.v, qa);
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("case %d: v (%08x %08x %08x) q (%08x %08x %08x %08x): original (%08x %08x %08x) ours (%08x %08x %08x)", i, Bits(v[0]), Bits(v[1]), Bits(v[2]), Bits(q[0]),
                     Bits(q[1]), Bits(q[2]), Bits(q[3]), Bits(a.v[0]), Bits(a.v[1]), Bits(a.v[2]), Bits(b.v[0]), Bits(b.v[1]), Bits(b.v[2]));
    }
}

namespace {

std::vector<uint32_t> gSlotLog;
float gSlotValues[3];

float __fastcall Slot4(void *, void *) { gSlotLog.push_back(4); return gSlotValues[0]; }
float __fastcall Slot5(void *, void *) { gSlotLog.push_back(5); return gSlotValues[1]; }
float __fastcall Slot6(void *, void *) { gSlotLog.push_back(6); return gSlotValues[2]; }
void *gVectorVt[8] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&Slot4), reinterpret_cast<void *>(&Slot5), reinterpret_cast<void *>(&Slot6)};

} // namespace

DIFF_TEST(SimGeometry_ReadObjectVector, 0x7dd0d0)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, void *)>(0x7dd0d0);
    std::mt19937 r(0x7dd0d0);
    struct Object {
        void **vt;
    } object = {gVectorVt};
    for (int i = 0; i < 4000; ++i) {
        for (float &f : gSlotValues) f = RandomFloat(r);
        const bool isNull = i % 7 == 0;
        void *const target = isNull ? nullptr : &object;
        float outA[5], outB[5];
        for (int k = 0; k < 5; ++k) outA[k] = outB[k] = FromBits(0x12345678u + k);
        gSlotLog.clear();
        float *ra = original(outA + 1, target);
        const std::vector<uint32_t> logA = gSlotLog;
        gSlotLog.clear();
        float *rb = simgeom::ReadObjectVector(outB + 1, target);
        ++ctx.cases;
        if (ra != outA + 1 || rb != outB + 1) ctx.Fail("case %d: return values", i);
        if (std::memcmp(outA, outB, sizeof(outA)) != 0) ctx.Fail("case %d: outputs differ", i);
        if (logA != gSlotLog) ctx.Fail("case %d: slot calls differ", i);
        if (!isNull && logA.size() != 3) ctx.Fail("case %d: expected three slot calls", i);
    }
}

DIFF_TEST(SimGeometry_SineOfFloat, 0x5a9de0)
{
    auto original = ctx.Original<float(__cdecl *)(float)>(0x5a9de0);
    std::mt19937 r(0x5a9de0);
    for (int i = 0; i < 30000; ++i) {
        const float x = RandomFloat(r);
        const float a = original(x), b = simgeom::SineOfFloat(x);
        ++ctx.cases;
        if (Bits(a) != Bits(b)) ctx.Fail("sin(%08x): original %08x ours %08x", Bits(x), Bits(a), Bits(b));
    }
}

DIFF_TEST(SimGeometry_CosineOfFloat, 0x5a9a90)
{
    auto original = ctx.Original<float(__cdecl *)(float)>(0x5a9a90);
    std::mt19937 r(0x5a9a90);
    for (int i = 0; i < 30000; ++i) {
        const float x = RandomFloat(r);
        const float a = original(x), b = simgeom::CosineOfFloat(x);
        ++ctx.cases;
        if (Bits(a) != Bits(b)) ctx.Fail("cos(%08x): original %08x ours %08x", Bits(x), Bits(a), Bits(b));
    }
}

DIFF_TEST(SimGeometry_TileFromBytes, 0x7dcb60)
{
    auto original = ctx.Original<int32_t *(__cdecl *)(int32_t *, const uint8_t *)>(0x7dcb60);
    std::mt19937 r(0x7dcb60);
    for (int i = 0; i < 4000; ++i) {
        uint8_t bytes[3] = {static_cast<uint8_t>(r()), static_cast<uint8_t>(r()), static_cast<uint8_t>(r())};
        if (i % 5 == 0) bytes[0] = 0x80, bytes[2] = 0x7f;
        int32_t a[5], b[5];
        for (int k = 0; k < 5; ++k) a[k] = b[k] = static_cast<int32_t>(0xabcd0000u + k);
        int32_t *ra = original(a + 1, bytes);
        int32_t *rb = simgeom::TileFromBytes(b + 1, bytes);
        ++ctx.cases;
        if (ra != a + 1 || rb != b + 1 || std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("bytes %02x %02x %02x", bytes[0], bytes[1], bytes[2]);
    }
}

DIFF_TEST(SimGeometry_PositionToFloats, 0x7dcc50)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const int32_t *, int32_t)>(0x7dcc50);
    std::mt19937 r(0x7dcc50);
    for (int i = 0; i < 6000; ++i) {
        const int32_t in[2] = {static_cast<int32_t>(r() % 3 ? r() : (r() % 5 ? static_cast<uint32_t>(i) - 3000 : 0x80000000u)), static_cast<int32_t>(r() % 3 ? r() : 0x7fffffff)};
        const int32_t level = static_cast<int32_t>(r() % 4 ? r() % 16 : r());
        float a[5], b[5];
        for (int k = 0; k < 5; ++k) a[k] = b[k] = FromBits(0xabcd0000u + k);
        float *ra = original(a + 1, in, level);
        float *rb = simgeom::PositionToFloats(b + 1, in, level);
        ++ctx.cases;
        if (ra != a + 1 || rb != b + 1 || std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("in %d %d level %d", in[0], in[1], level);
    }
}

DIFF_TEST(SimGeometry_TileCentre, 0x7dd620)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, const int32_t *)>(0x7dd620);
    std::mt19937 r(0x7dd620);
    for (int i = 0; i < 6000; ++i) {
        const int32_t in[3] = {static_cast<int32_t>(r() % 3 ? r() : r() % 100), static_cast<int32_t>(r() % 3 ? r() : 0x7fffffff), static_cast<int32_t>(r())};
        float a[5], b[5];
        for (int k = 0; k < 5; ++k) a[k] = b[k] = FromBits(0xabcd0000u + k);
        float *ra = original(a + 1, in);
        float *rb = simgeom::TileCentre(b + 1, in);
        ++ctx.cases;
        if (ra != a + 1 || rb != b + 1 || std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("in %d %d %d", in[0], in[1], in[2]);
    }
}

DIFF_TEST(SimGeometry_FacingToNotchOffset, 0x7dcde0)
{
    auto original = ctx.Original<int32_t(__cdecl *)(int32_t)>(0x7dcde0);
    for (int32_t facing = -40; facing <= 40; ++facing) {
        ++ctx.cases;
        if (original(facing) != simgeom::FacingToNotchOffset(facing)) ctx.Fail("facing %d", facing);
    }
    std::mt19937 r(0x7dcde0);
    for (int i = 0; i < 20000; ++i) {
        const int32_t facing = static_cast<int32_t>(r());
        ++ctx.cases;
        if (original(facing) != simgeom::FacingToNotchOffset(facing)) ctx.Fail("facing %08x: original %d ours %d", static_cast<uint32_t>(facing), original(facing), simgeom::FacingToNotchOffset(facing));
    }
}

DIFF_TEST(SimGeometry_NotchAngle, 0x7dcdb0)
{
    auto original = ctx.Original<float(__cdecl *)(int32_t)>(0x7dcdb0);
    std::mt19937 r(0x7dcdb0);
    for (int i = 0; i < 20000; ++i) {
        const int32_t notch = i < 200 ? i - 100 : static_cast<int32_t>(r());
        ++ctx.cases;
        if (Bits(original(notch)) != Bits(simgeom::NotchAngle(notch))) ctx.Fail("notch %d", notch);
    }
}

DIFF_TEST(SimGeometry_NotchQuaternion, 0x7dccd0)
{
    auto original = ctx.Original<float *(__cdecl *)(float *, int32_t)>(0x7dccd0);
    std::mt19937 r(0x7dccd0);
    // The second half runs at the game's 24-bit x87 precision (the angle sum is an x87 fadd; notes/plan.md "x87").
    for (int i = 0; i < 40000; ++i) {
        const int32_t notch = i % 20000 < 300 ? i % 20000 - 150 : (i % 2 ? static_cast<int32_t>(r() % 41) - 20 : static_cast<int32_t>(r()));
        difftest::X87Precision precision(i < 20000 ? difftest::X87Pc::Bits53 : difftest::X87Pc::Bits24);
        float a[6], b[6];
        for (int k = 0; k < 6; ++k) a[k] = b[k] = FromBits(0xabcd0000u + k);
        float *ra = original(a + 1, notch);
        float *rb = simgeom::NotchQuaternion(b + 1, notch);
        ++ctx.cases;
        if (ra != a + 1 || rb != b + 1 || std::memcmp(a, b, sizeof(a)) != 0)
            ctx.Fail("notch %d at %s-bit: original (%08x %08x %08x %08x) ours (%08x %08x %08x %08x)", notch, i < 20000 ? "53" : "24", Bits(a[1]), Bits(a[2]), Bits(a[3]), Bits(a[4]), Bits(b[1]), Bits(b[2]),
                     Bits(b[3]), Bits(b[4]));
    }
}
