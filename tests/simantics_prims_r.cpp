// Differential tests for the SimAntics primitive handlers of src/simantics/PrimR.h: opcode 0x1a (0x59e6e0).
// Framework and notes: tests/simantics_prims_mid.h (fake objects with recording vtable slots), tests/simantics_prims_r.h.
#include "simantics_prims_r.h"

namespace {

// Slot 5 of the target: the count at an index (one argument), slot 6 raises it (three arguments); the count lives in
// the world's scratch word 1 so both sides see the same changes.
uint32_t __fastcall Tgt5(void *self, void *, uint32_t index)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(5u << 8 | 1u);
    gTrace.push_back(index);
    return gW->scratch[1];
}

uint32_t __fastcall Tgt6(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(6u << 8 | 3u);
    gTrace.insert(gTrace.end(), {a1, a2, a3});
    if (gW->scratch[2]) gW->scratch[1] = a3;
    return 0;
}

void Build1A(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 101);
    Base(w, seed, r);
    SetParam(w, r);
    ClearQueues();
    Fake &ctx = w.fk[0], &table = w.fk[2], &mgr = w.fk[3], &a = w.fk[4], &b = w.fk[5], &p1 = w.fk[6], &p2 = w.fk[7], &o1 = w.fk[8],
         &o2 = w.fk[9], &tgt = w.fk[10], &tgt2 = w.fk[11];

    // The flags: bit 0 (no raise), bit 1 (manager lookup), bit 2 (slot 10 store), bit 3 (single lookup).
    w.param[5] = static_cast<uint8_t>((r() % 3 == 0) ? r() : (r() % 16));
    w.param[4] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 5, 100, 254, 255, static_cast<uint32_t>(r())}));

    w.vtSec[7] = reinterpret_cast<void *>(&ReadQ);
    gReadQ = {Wide16(r), Wide16(r), Wide16(r)};
    gRead.hasTarget = r() % 8 != 0;
    gRead.cell = static_cast<int>(r() % 16);
    switch (r() % 4) {
    case 0: gRead.min = -32768; gRead.max = 32767; break;
    case 1: gRead.min = 0; gRead.max = 100; break;
    case 2: gRead.min = static_cast<int16_t>(r() % 50); gRead.max = static_cast<int16_t>(r() % 400); break;
    default: gRead.min = static_cast<int16_t>(r()); gRead.max = static_cast<int16_t>(r()); break;
    }

    // Context -> manager (slot 10); this -> table (slot 63).
    Use(ctx.vt, 10, 0);
    RetObject(&ctx, 10, &mgr, w);
    Use(w.vtThis, 63, 0);
    RetObject(&w.obj, 63, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Q1<29>);
    mgr.vt[16] = reinterpret_cast<void *>(&Q2<16>);
    mgr.vt[17] = reinterpret_cast<void *>(&Q1<17>);
    Use(mgr.vt, 19, 1);

    Fake *la = (r() % 7 == 0) ? nullptr : &a;
    Fake *lb = (r() % 7 == 0) ? nullptr : (r() % 6 == 0 ? &a : &b);
    QObj(16, la, w);
    QObj(16, lb, w);
    QObj(29, la, w);
    QObj(29, lb, w);
    QObj(17, &o1, w);
    QObj(17, (r() % 5 == 0) ? &o1 : &o2, w);

    for (Fake *f : {&a, &b}) {
        Use(f->vt, 1, 0);
        Use(f->vt, 2, 0);
        Use(f->vt, 12, 0);
        Use(f->vt, 98, 0);
        Use(f->vt, 61, 0);
        Use(f->vt, 109, 0);
        RetObject(f, 12, (r() % 8 == 0) ? nullptr : &tgt, w);
        RetValue(f, 98, (r() % 4 == 0) ? static_cast<uint32_t>(r() % 4) : 2u, w);
        RetObject(f, 61, f == &a ? &p1 : &p2, w);
        RetObject(f, 109, (r() % 8 == 0) ? nullptr : &tgt2, w);
    }
    Use(p1.vt, 28, 0);
    Use(p2.vt, 28, 0);
    RetValue(&p1, 28, static_cast<uint32_t>(r()), w);
    RetValue(&p2, 28, static_cast<uint32_t>(r()), w);
    for (Fake *f : {&o1, &o2}) {
        Use(f->vt, 8, 0);
        Use(f->vt, 12, 0);
        RetValue(f, 8, Wide16(r), w);
        RetObject(f, 12, (r() % 8 == 0) ? nullptr : &tgt, w);
    }
    for (Fake *f : {&tgt, &tgt2}) {
        f->vt[5] = reinterpret_cast<void *>(&Tgt5);
        f->vt[6] = reinterpret_cast<void *>(&Tgt6);
        Use(f->vt, 9, 2);
        Use(f->vt, 10, 3);
        RetValue(f, 9, Wide16(r), w);
        RetValue(f, 10, (r() % 3 == 0) ? 0u : (r() % 2 ? 1u : (r() | 1u)), w);
        if (r() % 4 == 0) RetValue(f, 10, r() & ~0xffu, w);
    }
    // The count: around the limit (param byte +4), sometimes huge (unsigned compare).
    const uint32_t limit = w.param[4];
    switch (r() % 6) {
    case 0: w.scratch[1] = limit; break;
    case 1: w.scratch[1] = limit + 1; break;
    case 2: w.scratch[1] = limit - 1; break;
    case 3: w.scratch[1] = r(); break;
    case 4: w.scratch[1] = 0xffffffffu; break;
    default: w.scratch[1] = r() % 300; break;
    }
    w.scratch[2] = r() % 2;
}

} // namespace

DIFF_TEST(PrimR_Prim1A, 0x59e6e0)
{
    gBeforeRun = [](World &) { ResetQueues(); };
    Stats st = RunHandler(ctx, "Prim1A", 0x59e6e0, 40000, Build1A,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) {
                              return reinterpret_cast<PrimRObject *>(o)->Prim1A(f, p);
                          });
    gBeforeRun = nullptr;
    if (st.throws < 2000 || st.byRet[1] < 3000 || st.byRet[0] < 2000) ctx.Fail("coverage: %u throws, %u/%u returns", st.throws, st.byRet[0], st.byRet[1]);
}
