// Opcode 0x23 (framework and notes: tests/simantics_prims_mid.h).
#include "simantics_prims_mid.h"

namespace {

// F's slot 12: (class id, interface id, void **out). Writes the scripted reference object (present: also answers true).
uint32_t __fastcall Qi3(void *self, void *, uint32_t a1, uint32_t a2, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(12u << 8 | 3u);
    gTrace.insert(gTrace.end(), {a1, a2, out ? 1u : 0u});
    const uint32_t v = Scripted(self, 0x778);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(v));
    return Scripted(self, 12);
}

// The source object's virtual base, slot 0: (interface id, void **out).
uint32_t __fastcall QiBase(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.insert(gTrace.end(), {iid, out ? 1u : 0u});
    const uint32_t v = Scripted(self, 0x779);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(v));
    return Scripted(self, 0);
}

// Makes fk[index] an object with a virtual base (vbptr at +4, offset table entry 1) at sub[k].
void MakeVirtualBase(World &w, int index, int k)
{
    w.fk[index].pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.vbtab[k]));
    w.vbtab[k][0] = 0;
    w.vbtab[k][1] = static_cast<int32_t>(reinterpret_cast<uint8_t *>(&w.sub[k]) - (reinterpret_cast<uint8_t *>(&w.fk[index]) + 4));
    Use(w.sub[k].vt, 2, 0);
}

void BuildPrim23(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 43);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &middle = w.fk[2], &table = w.fk[3], &state = w.fk[4], &found = w.fk[5], &service = w.fk[6],
         &ref1 = w.fk[7], &helper = w.fk[8], &lister = w.fk[9], &source = w.fk[10], &ref2 = w.fk[11];

    w.param[7] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 255}));
    w.param[8] = static_cast<uint8_t>(r());
    PutWord(w, 4, static_cast<uint16_t>(r()));

    Use(ctx.vt, 26, 0);
    Use(ctx.vt, 3, 0);
    RetObject(&ctx, 26, &table, w);
    RetObject(&ctx, 3, &state, w);
    Use(frame.vt, 18, 0);
    Use(frame.vt, 19, 1);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 18, Pick(r, {0, 0, 1, 1, 1, 2, static_cast<uint32_t>(r())}), w);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    Use(state.vt, 15, 1);
    Use(state.vt, 16, 2);
    Use(state.vt, 21, 0);
    Use(state.vt, 20, 2);
    RetValue(&state, 15, (r() % 2) ? 0u : static_cast<uint32_t>(r()) | 1u, w);
    RetValue(&state, 21, (r() % 2) ? 2u : static_cast<uint32_t>(r()), w);
    Use(w.vtThis, 19, 1);
    RetValue(&w.obj, 19, static_cast<uint32_t>(r()), w);

    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 8 == 0) ? nullptr : &found, w);
    LookupObj(&helper, w);
    Use(found.vt, 188, 0);
    RetValue(&found, 188, (r() % 3 == 0) ? (static_cast<uint32_t>(r()) & ~0xffu) : (static_cast<uint32_t>(r()) | 1u), w);
    Use(service.vt, 37, 1);
    Use(service.vt, 44, 1);

    helper.vt[12] = reinterpret_cast<void *>(&Qi3);
    const bool first = r() % 5 != 0;
    RetObject(&helper, 0x778, first ? &ref1 : nullptr, w);
    RetValue(&helper, 12, first ? (static_cast<uint32_t>(r()) | 1u) : (static_cast<uint32_t>(r()) & ~0xffu), w);
    MakeVirtualBase(w, 7, 0);
    Use(ref1.vt, 118, 0);

    Use(lister.vt, 53, 0);
    RetObject(&lister, 53, &middle, w);
    Use(middle.vt, 37, 0);
    RetObject(&middle, 37, &source, w);
    MakeVirtualBase(w, 10, 1);
    w.sub[1].vt[0] = reinterpret_cast<void *>(&QiBase);
    const bool second = r() % 4 != 0;
    // The sub-object is the "self" of the QueryInterface call: scripts are keyed by it.
    RetObject(&w.sub[1], 0x779, second ? &ref2 : nullptr, w);
    RetValue(&w.sub[1], 0, second ? (static_cast<uint32_t>(r()) | 1u) : (static_cast<uint32_t>(r()) & ~0xffu), w);
    MakeVirtualBase(w, 11, 2);
    Use(ref2.vt, 20, 0);
    Use(ref2.vt, 5, 1);
    const int32_t counts[] = {-5, -1, 0, 1, 2, 3, 4, 7, 100, INT32_MIN, INT32_MAX};
    RetValue(&ref2, 20, static_cast<uint32_t>(counts[r() % 11]), w);
}

} // namespace

DIFF_TEST(PrimMid_Prim23, 0x5a87f0)
{
    // The cached services the handler gets: GetCachedSingletonB (0x10cc28c) and 0x211da3's cache (0x10cc458), served
    // from their cache cells (a null cell would make them look the service up).
    void *const savedB = *ctx.image.At<void *>(0x10cc28c);
    void *const savedC = *ctx.image.At<void *>(0x10cc458);
    gBeforeRun = [&](World &w) {
        *ctx.image.At<void *>(0x10cc28c) = &w.fk[6];
        *ctx.image.At<void *>(0x10cc458) = &w.fk[9];
    };
    Stats st = RunHandler(ctx, "Prim23", 0x5a87f0, 60000, BuildPrim23,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->Prim23(f, p); });
    gBeforeRun = nullptr;
    *ctx.image.At<void *>(0x10cc28c) = savedB;
    *ctx.image.At<void *>(0x10cc458) = savedC;
    if (st.throws < 1000 || st.byRet[1] < 3000 || st.byRet[2] < 3000) ctx.Fail("coverage: %u throws, %u/%u", st.throws, st.byRet[1], st.byRet[2]);
}
