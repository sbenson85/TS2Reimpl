// Opcodes 0x7e and 0x00 (framework and notes: tests/simantics_prims_mid.h).
#include "simantics_prims_mid.h"

// ============================================================================================================
// 0x5a1260 (opcode 0x7e)
// ============================================================================================================

namespace {

void Build7E(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 7);
    Base(w, seed, r);
    SetParam(w, r);
    // owner (fk[1]) slot 67 returns the handler object fk[2] or null; fk[2] slot 7 takes (this, frame, param).
    Use(w.vtFk[1], 67, 0);
    Use(w.vtFk[2], 7, 3);
    RetObject(&w.fk[1], 67, (r() % 3 == 0) ? nullptr : &w.fk[2], w);
    RetValue(&w.fk[2], 7, (r() % 2) ? static_cast<uint32_t>(r()) : r() % 4, w);
}

} // namespace

DIFF_TEST(PrimMid_7E, 0x5a1260)
{
    Stats st = RunHandler(ctx, "7E", 0x5a1260, 4000, Build7E,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->Prim7E(f, p); });
    if (st.returns < 3000) ctx.Fail("coverage");
}

// ============================================================================================================
// 0x5988a0 (opcode 0x00, Idle)
// ============================================================================================================

namespace {

void BuildIdle(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 11);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &ticker = w.fk[2], &table = w.fk[3];
    const uint32_t count = Pick(r, {0, 1, 2, 3, 5, 12, 255});
    Use(frame.vt, 26, 0);
    frame.vt[35] = reinterpret_cast<void *>(&Frame35);
    RetValue(&frame, 26, (r() & 0xffffff00u) | count, w);
    int idx;
    switch (r() % 6) {
    case 0: idx = -1 - static_cast<int>(r() % 3); break;
    case 1: idx = static_cast<int>(count); break;
    case 2: idx = static_cast<int>(count) + 1; break;
    case 3: idx = static_cast<int>(r() % 400) - 100; break;
    default: idx = count ? static_cast<int>(r() % count) : 0; break;
    }
    PutWord(w, 4, static_cast<uint16_t>(idx));
    const int16_t values[] = {-3, -1, 0, 0, 1, 2, 5, 100, 32767, -32768};
    for (int16_t &c : w.counters) c = values[r() % 10];

    Use(ctx.vt, 3, 0);
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 3, &ticker, w);
    RetObject(&ctx, 26, &table, w);
    Use(ticker.vt, 14, 0);
    RetValue(&ticker, 14, Pick(r, {0, 1, 2, 4, 6, 8, 0x100, 0x1ff, static_cast<uint32_t>(r())}), w);
    Use(w.vtThis, 47, 0);
    Use(w.vtThis, 196, 0);
    RetValue(&w.obj, 47, static_cast<uint32_t>(r()), w);
    RetValue(&w.obj, 196, (r() % 2) ? (static_cast<uint32_t>(r()) | 1u) : (static_cast<uint32_t>(r()) & ~0xffu), w);
    Use(table.vt, 73, 1);
    Use(table.vt, 71, 2);
    RetValue(&table, 73, (r() % 2) ? 0 : static_cast<uint32_t>(r()) | 1u, w);
}

} // namespace

DIFF_TEST(PrimMid_Idle, 0x5988a0)
{
    Stats st = RunHandler(ctx, "Idle", 0x5988a0, 20000, BuildIdle,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->Idle(f, p); });
    if (st.throws < 1000 || st.byRet[1] < 1000 || st.byRet[2] < 1000)
        ctx.Fail("coverage: %u throws, %u/%u", st.throws, st.byRet[1], st.byRet[2]);
}
