// Opcodes 0x0b, 0x0c, 0x19 and the helpers of DirectionTo (framework and notes: tests/simantics_prims_mid.h).
#include "simantics_prims_mid.h"

// ============================================================================================================
// 0x588eb0 (opcode 0x0b, DistanceTo)
// ============================================================================================================

namespace {

void BuildDistance(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 13);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[3], &a = w.fk[4], &b = w.fk[5];
    const int idx = (r() % 4 == 0) ? static_cast<int>(r() % 20) - 6 : static_cast<int>(r() % 8);
    PutWord(w, 4, static_cast<uint16_t>(idx));
    w.vtSec[7] = reinterpret_cast<void *>(&Read7);
    RetValue(reinterpret_cast<uint8_t *>(&w.obj) + 4, 7, static_cast<uint32_t>(r()), w);
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 6 == 0) ? nullptr : &a, w);
    LookupObj((r() % 6 == 0) ? nullptr : &b, w);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    a.vt[128] = reinterpret_cast<void *>(&Dist128);
    RetValue(&a, 128, FloatBits(RandomFloat(r)), w);
    Use(w.vtThis, 20, 2);
}

} // namespace

DIFF_TEST(PrimMid_DistanceTo, 0x588eb0)
{
    Stats st = RunHandler(ctx, "DistanceTo", 0x588eb0, 30000, BuildDistance,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->DistanceTo(f, p); });
    if (st.throws < 2000 || st.byRet[1] < 5000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.byRet[1]);
}

// ============================================================================================================
// 0x588bb0 (opcode 0x0c, DirectionTo) and its helpers
// ============================================================================================================

namespace {

int32_t RandomFixed(std::mt19937 &r)
{
    switch (r() % 6) {
    case 0: return 0;
    case 1: return static_cast<int32_t>(r() % 9) - 4;
    case 2: return (static_cast<int32_t>(r() % 41) - 20) << 16;
    case 3: return (static_cast<int32_t>(r() % 2001) - 1000) << 12;
    case 4: return static_cast<int32_t>(r());
    default: return (static_cast<int32_t>(r() % 21) - 10) * 65536 + static_cast<int32_t>(r() % 3) - 1;
    }
}

void BuildDirection(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 17);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[3], &a = w.fk[4], &b = w.fk[5];
    if (r() % 2) w.param[0xc] |= 2;
    else w.param[0xc] &= ~2;
    w.vtSec[7] = reinterpret_cast<void *>(&Read7);
    RetValue(reinterpret_cast<uint8_t *>(&w.obj) + 4, 7, static_cast<uint32_t>(r()), w);
    gRead.hasTarget = r() % 8 != 0;
    gRead.cell = static_cast<int>(r() % 16);
    switch (r() % 4) {
    case 0: gRead.min = -32768; gRead.max = 32767; break;
    case 1: gRead.min = 0; gRead.max = 359; break;
    case 2: gRead.min = static_cast<int16_t>(r() % 8); gRead.max = static_cast<int16_t>(r() % 400); break;
    default: gRead.min = static_cast<int16_t>(r()); gRead.max = static_cast<int16_t>(r()); break;
    }
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 8 == 0) ? nullptr : &a, w);
    LookupObj((r() % 8 == 0) ? nullptr : &b, w);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    a.vt[33] = reinterpret_cast<void *>(&Pos33);
    b.vt[33] = reinterpret_cast<void *>(&Pos33);
    for (SimFixedPoint &p : w.pos) {
        p.mX = RandomFixed(r);
        p.mY = RandomFixed(r);
    }
    if (r() % 5 == 0) w.pos[1] = w.pos[0];
    if (r() % 5 == 0) w.pos[1].mX = w.pos[0].mX;
    if (r() % 5 == 0) w.pos[1].mY = w.pos[0].mY;
    RetObject(&a, 33, &w.pos[0], w);
    RetObject(&b, 33, &w.pos[1], w);
}

} // namespace

DIFF_TEST(PrimMid_DirectionTo, 0x588bb0)
{
    Stats st = RunHandler(ctx, "DirectionTo", 0x588bb0, 40000, BuildDirection,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->DirectionTo(f, p); });
    if (st.throws < 2000 || st.byRet[1] < 10000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.byRet[1]);
}

DIFF_TEST(SimFixed_ToFloat, 0x57f290)
{
    auto original = ctx.Original<float(__thiscall *)(const int32_t *)>(0x57f290);
    std::mt19937 r(3);
    for (int i = 0; i < 100000; ++i) {
        SimFixed v{RandomFixed(r)};
        if (i < 8) v.mRaw = i == 0 ? 0 : i == 1 ? 1 : i == 2 ? -1 : i == 3 ? INT32_MIN : i == 4 ? INT32_MAX : i == 5 ? 65536 : i == 6 ? 0x1000001 : -0x1000001;
        const float a = original(&v.mRaw);
        const float b = v.ToFloat();
        ++ctx.cases;
        if (FloatBits(a) != FloatBits(b)) ctx.Fail("ToFloat(%d): 0x%x / 0x%x", v.mRaw, FloatBits(a), FloatBits(b));
    }
}

DIFF_TEST(SimFixedPoint_Octant, 0x460400)
{
    auto original = ctx.Original<int32_t(__thiscall *)(const SimFixedPoint *, const SimFixedPoint *)>(0x460400);
    std::mt19937 r(5);
    uint32_t seen[8] = {};
    for (int i = 0; i < 400000; ++i) {
        SimFixedPoint from{RandomFixed(r), RandomFixed(r)}, to{RandomFixed(r), RandomFixed(r)};
        if (i % 4 == 0) to = {from.mX + (static_cast<int32_t>(r() % 21) - 10) * 4096, from.mY + (static_cast<int32_t>(r() % 21) - 10) * 4096};
        if (i % 7 == 0) to.mX = from.mX;
        if (i % 11 == 0) to.mY = from.mY;
        if (i % 13 == 0) { // near the 22.5 degree boundaries: |dy| = 0.41421357 |dx| and its neighbours
            const int32_t dx = static_cast<int32_t>(r() % 100000) + 1000;
            const int32_t dy = static_cast<int32_t>(dx * 0.41421357) + static_cast<int32_t>(r() % 5) - 2;
            to = {from.mX + ((r() % 2) ? dx : -dx), from.mY + ((r() % 2) ? dy : -dy)};
        }
        const int32_t a = original(&from, &to);
        const int32_t b = from.Octant(&to);
        ++ctx.cases;
        if (a >= 0 && a < 8) ++seen[a];
        if (a != b) ctx.Fail("Octant(%d,%d -> %d,%d): %d / %d", from.mX, from.mY, to.mX, to.mY, a, b);
    }
    for (int i = 0; i < 8; ++i)
        if (seen[i] < 1000) ctx.Fail("coverage: octant %d seen %u times", i, seen[i]);
}

DIFF_TEST(Atan2Float, 0x275390)
{
    auto original = ctx.Original<float(__cdecl *)(float, float)>(0x275390);
    std::mt19937 r(9);
    uint32_t mismatches = 0;
    for (int i = 0; i < 400000; ++i) {
        float y = static_cast<float>(static_cast<int>(r() % 200001) - 100000) / 64.0f;
        float x = static_cast<float>(static_cast<int>(r() % 200001) - 100000) / 64.0f;
        if (i % 5 == 0) x = 0;
        if (i % 7 == 0) y = 0;
        if (i % 11 == 0) { x = RandomFloat(r); y = RandomFloat(r); }
        const float a = original(y, x);
        const float b = Atan2Float(y, x);
        ++ctx.cases;
        if (FloatBits(a) != FloatBits(b) && !(std::isnan(a) && std::isnan(b))) {
            if (++mismatches < 5) ctx.Fail("atan2(%g, %g): 0x%x / 0x%x", y, x, FloatBits(a), FloatBits(b));
        }
    }
    if (mismatches) ctx.Fail("%u mismatches", mismatches);
}

DIFF_TEST(RankCompare, 0x5a9330)
{
    auto original = ctx.Original<int(__cdecl *)(const int32_t *, const int32_t *)>(0x5a9330);
    std::mt19937 r(21);
    float table[16];
    float **global = GameAddress<float *>(0x10dbf50);
    float *saved = *global;
    *global = table;
    for (int i = 0; i < 200000; ++i) {
        for (float &t : table) t = RandomFloat(r);
        if (i % 3 == 0) {
            const float base = RandomFloat(r);
            for (float &t : table) t = base + static_cast<float>(static_cast<int>(r() % 41) - 20) / 1000.0f;
        }
        const int32_t x = static_cast<int32_t>(r() % 16), y = static_cast<int32_t>(r() % 16);
        const int a = original(&x, &y);
        const int b = RankCompare(&x, &y);
        ++ctx.cases;
        if (a != b) ctx.Fail("RankCompare: %d / %d (%g, %g)", a, b, table[x], table[y]);
    }
    *global = saved;
}

// ============================================================================================================
// 0x5801b0 (opcode 0x19, Budget)
// ============================================================================================================

namespace {

void BuildBudget(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 19);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &funds = w.fk[3];
    if (r() % 3 == 0) w.param[4] = static_cast<uint8_t>(r() % 4);
    w.param[0xa] = (r() % 10 == 0) ? static_cast<uint8_t>(r()) : static_cast<uint8_t>(r() % 12);
    // flags word: bits 0..3 matter (bit 0 skips the charge, bit 1 credit, bit 2 multiplier from data, bit 3 two cells)
    PutWord(w, 8, static_cast<uint16_t>((r() & 0xfff0u) | (r() % 16)));
    w.vtSec[7] = reinterpret_cast<void *>(&Read7);
    RetValue(reinterpret_cast<uint8_t *>(&w.obj) + 4, 7, static_cast<uint32_t>(Edge16(r)) | (r() & 0xffff0000u), w);
    Use(w.vtThis, 19, 1);
    RetValue(&w.obj, 19, static_cast<uint32_t>(Edge16(r)) | (r() & 0xffff0000u), w);
    Use(ctx.vt, 3, 0);
    RetObject(&ctx, 3, &funds, w);
    Use(funds.vt, 29, 0);
    Use(funds.vt, 44, 2);
    const int32_t balances[] = {0, 1, -1, 99, 100, 1000, 10000, 123456789, -5000, INT32_MAX, INT32_MIN};
    RetValue(&funds, 29, (r() % 3 == 0) ? static_cast<uint32_t>(r()) : static_cast<uint32_t>(balances[r() % 11]), w);
}

} // namespace

DIFF_TEST(PrimMid_Budget, 0x5801b0)
{
    Stats st = RunHandler(ctx, "Budget", 0x5801b0, 40000, BuildBudget,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->Budget(f, p); });
    if (st.throws < 500 || st.byRet[0] < 3000 || st.byRet[1] < 3000)
        ctx.Fail("coverage: %u throws, %u/%u", st.throws, st.byRet[0], st.byRet[1]);
}
