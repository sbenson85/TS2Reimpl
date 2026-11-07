// Opcode 0x77, Message (0x59de90). Framework: tests/simantics_prims_s.h.
//
// The helper 0x57ea20 (the object filter) is an original callee that runs on both sides. Its kinds 3 and 4 compare
// positions through cached services the world does not have, so the param byte +4 stays off them; kinds 0 and 1
// (slots 131 and 36 of the object) and the matching default are covered.
#include "simantics_prims_s.h"

namespace {

// this slot 317: (object, message*): logs the six message words.
uint32_t __fastcall Slot317(void *self, void *, uint32_t a1, const uint16_t *message)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(317u << 8 | 2u);
    gTrace.push_back(Norm(a1));
    for (int i = 0; i < 6; ++i) gTrace.push_back(message[i]);
    return Scripted(self, 317);
}

void Build77(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 97);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[3], &holder = w.fk[4], &listOwner = w.fk[5], &direct = w.fk[6], &iface = w.fk[7];
    gReadVals.clear();
    gReadPos = 0;
    for (int i = 0; i < 5; ++i) gReadVals.push_back(ReadWord(r));
    w.vtSec[7] = reinterpret_cast<void *>(&ReadSeq);

    w.param[7] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 4, 5, 6, 6, 7, 7, 8, 255}));
    w.param[4] = static_cast<uint8_t>(Pick(r, {0, 0, 1, 1, 2, 5, 9, 255}));
    w.param[8] = static_cast<uint8_t>((r() % 5 == 0) ? 2 : (r() & 0xfd));

    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 6 == 0) ? nullptr : &direct, w);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    const uint32_t small[] = {0, 1, 2, 3, 0x7fff, 0x8000, 0xffff};
    auto pick = [&]() { return small[r() % 7] | (r() & 0xffff0000u); };
    Use(w.vtThis, 19, 1);
    Use(w.vtThis, 36, 0);
    Use(w.vtThis, 131, 0);
    Use(w.vtThis, 63, 0);
    w.vtThis[317] = reinterpret_cast<void *>(&Slot317);
    RetValue(&w.obj, 19, static_cast<uint32_t>(r()), w);
    RetValue(&w.obj, 36, pick(), w);
    RetValue(&w.obj, 131, pick(), w);
    RetObject(&w.obj, 63, &holder, w);

    // Scratch memory for the vector and the list (world memory that is neither captured nor logged).
    void **mem = w.vtSub[3];
    std::memset(mem, 0, sizeof(w.vtSub[3]));
    const int count = static_cast<int>(r() % 5);
    // the vector: container {begin, end}, entries
    mem[0] = &mem[2];
    mem[1] = &mem[2 + count];
    for (int i = 0; i < count; ++i) mem[2 + i] = &w.fk[8 + i];
    Use(holder.vt, 45, 0);
    RetObject(&holder, 45, mem, w);
    // the list: object {head}, sentinel {next, prev, value}, nodes
    const int nodes = static_cast<int>(r() % 4);
    mem[8] = &mem[12];
    void **sentinel = &mem[12];
    void **prev = sentinel;
    for (int i = 0; i < nodes; ++i) {
        void **node = &mem[16 + 4 * i];
        node[2] = &w.fk[8 + (i % 4)];
        prev[0] = node;
        node[1] = prev;
        prev = node;
    }
    prev[0] = sentinel;
    sentinel[1] = prev;
    // The list owner sits at this + 0x1c4 (inside the world's vtable array for the object, which the handler does not call).
    void **at = reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(&w.obj) + 0x1c4);
    *at = &listOwner;
    Use(listOwner.vt, 31, 0);
    RetObject(&listOwner, 31, &mem[8], w);

    Use(iface.vt, 2, 0);
    for (int i = 8; i < 12; ++i) {
        Fake &e = w.fk[i];
        Use(e.vt, 10, 2);
        Use(e.vt, 8, 0);
        Use(e.vt, 131, 0);
        Use(e.vt, 36, 0);
        e.vt[0] = reinterpret_cast<void *>(&Qi0);
        RetValue(&e, 10, Pick(r, {0, 0, 2, 2, 1, 0x10000, 0x10002, 3}), w);
        RetObject(&e, 8, &w.fk[12 + (i % 2)], w);
        RetValue(&e, 131, pick(), w);
        RetValue(&e, 36, pick(), w);
        RetObject(&e, 0, (r() % 2) ? nullptr : &iface, w);
    }
    for (int i = 12; i < 14; ++i) {
        Fake &o = w.fk[i];
        Use(o.vt, 131, 0);
        Use(o.vt, 36, 0);
        RetValue(&o, 131, pick(), w);
        RetValue(&o, 36, pick(), w);
    }
}

} // namespace

DIFF_TEST(PrimS_Prim77, 0x59de90)
{
    // The handler reads the list owner at this + 0x1c4: check that it falls inside the test world's object area.
    {
        auto probe = std::make_unique<World>();
        const uint8_t *at = reinterpret_cast<const uint8_t *>(&probe->obj) + 0x1c4;
        const uint8_t *lo = reinterpret_cast<const uint8_t *>(probe->vtThis);
        if (at < lo || at + 4 > lo + sizeof(probe->vtThis)) {
            ctx.Fail("this + 0x1c4 is outside the vtThis array");
            return;
        }
    }
    gBeforeRun = [](World &) { gReadPos = 0; };
    Stats st = RunHandler(ctx, "Prim77", 0x59de90, 60000, Build77,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return Self(o)->Prim77(f, p); });
    gBeforeRun = nullptr;
    if (st.throws < 1000 || st.byRet[0] < 3000 || st.byRet[1] < 3000) ctx.Fail("coverage: %u throws, %u/%u", st.throws, st.byRet[0], st.byRet[1]);
}
