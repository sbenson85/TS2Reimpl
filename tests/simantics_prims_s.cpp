// Differential tests for the lane S SimAntics primitive handlers (src/simantics/PrimS.h): opcodes 0x31 and 0x7c and the
// helper 0x5a9f00. Framework: tests/simantics_prims_s.h.
#include "simantics_prims_s.h"

namespace {

// 0x7c's slot 34 of the want handler: (entity, key, request*, extra); logs the 10-byte request it was handed.
uint32_t __fastcall Slot34(void *self, void *, uint32_t a1, uint32_t a2, const uint8_t *request, uint32_t a4)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(34u << 8 | 4u);
    uint32_t kind, value;
    uint16_t extra;
    std::memcpy(&kind, request, 4);
    std::memcpy(&value, request + 4, 4);
    std::memcpy(&extra, request + 8, 2);
    gTrace.insert(gTrace.end(), {a1, a2, kind, value, extra, a4});
    return Scripted(self, 34);
}


} // namespace

// ============================================================================================================
// 0x5a9f00 (the entity resolver)
// ============================================================================================================

DIFF_TEST(PrimS_ResolveEntityId, 0x5a9f00)
{
    gSavedSingleton = *GameAddress<void *>(0x10cc288);
    gBeforeRun = SetSingleton;
    auto original = ctx.Original<int16_t(__cdecl *)(int16_t, uint8_t)>(0x5a9f00);
    auto build = [](World &w, uint32_t seed) {
        std::mt19937 r(seed * 2654435761u + 91);
        Base(w, seed, r);
        SetParam(w, r);
        PutWord(w, 4, static_cast<uint16_t>(Edge16(r)));
        w.param[6] = static_cast<uint8_t>(Pick(r, {0, 0, 1, 1, 2, 255}));
        BuildResolver(w, r);
    };
    Stats st;
    Compare(
        ctx, "ResolveEntityId", 30000, build,
        [&](PrimMidObject *, void *, const void *p) {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            int16_t id;
            std::memcpy(&id, b + 4, 2);
            return static_cast<uint32_t>(static_cast<uint16_t>(original(id, b[6])));
        },
        [&](PrimMidObject *, void *, const void *p) {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            int16_t id;
            std::memcpy(&id, b + 4, 2);
            return static_cast<uint32_t>(static_cast<uint16_t>(ResolveEntityId(id, b[6])));
        },
        st);
    gBeforeRun = nullptr;
    *GameAddress<void *>(0x10cc288) = gSavedSingleton;
    if (st.throws < 500 || st.returns < 20000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.returns);
}

// ============================================================================================================
// 0x5a8b30 (opcode 0x7c)
// ============================================================================================================

namespace {

void Build7C(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 77);
    Base(w, seed, r);
    SetParam(w, r);
    BuildResolver(w, r);
    Fake &ctx = w.fk[0], &handlers = w.fk[3], &handler = w.fk[5], &object = w.fk[7], &entry = w.fk[9], &inner = w.fk[10];
    Fake &sec = *reinterpret_cast<Fake *>(reinterpret_cast<uint8_t *>(&w.obj) + 4);
    (void)sec;

    // Which kind, and how the operands read.
    gReadVals.clear();
    gReadPos = 0;
    for (int i = 0; i < 3; ++i) gReadVals.push_back(ReadWord(r));
    w.vtSec[7] = reinterpret_cast<void *>(&ReadSeq);

    Use(ctx.vt, 4, 0);
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 4, &handlers, w);
    RetObject(&ctx, 26, &w.fk[4], w);
    Use(handlers.vt, 23, 1);
    RetObject(&handlers, 23, (r() % 8 == 0) ? nullptr : &handler, w);
    Use(handler.vt, 16, 0);
    const uint32_t kind = Pick(r, {0, 1, 2, 3, 3, 4, 5, 6, 7, 8, 100, 0xffffffffu});
    RetValue(&handler, 16, kind, w);
    handlers.vt[34] = reinterpret_cast<void *>(&Slot34);
    RetValue(&handlers, 34, (r() % 2) ? (static_cast<uint32_t>(r()) | 1u) : (static_cast<uint32_t>(r()) & ~0xffu), w);

    // The object chain of kinds 2, 3, 5 and 6.
    Use(object.vt, 61, 0);
    // Kinds 5 and 6 use the entry without a check (the original crashes on a missing one): the world gives them one.
    RetObject(&object, 61, (r() % 6 == 0 && kind != 5 && kind != 6) ? nullptr : &entry, w);
    Use(entry.vt, 27, 0);
    Use(entry.vt, 28, 0);
    Use(inner.vt, 28, 0);
    RetObject(&entry, 27, (r() % 3 == 0) ? nullptr : &inner, w);
    auto dword = [&]() { return (r() % 4 == 0) ? 0u : static_cast<uint32_t>(r()); };
    RetValue(&entry, 28, dword(), w);
    RetValue(&inner, 28, dword(), w);
}

} // namespace

DIFF_TEST(PrimS_Prim7C, 0x5a8b30)
{
    gSavedSingleton = *GameAddress<void *>(0x10cc288);
    gBeforeRun = [](World &w) { SetSingleton(w); gReadPos = 0; };
    Stats st = RunHandler(ctx, "Prim7C", 0x5a8b30, 60000, Build7C,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return Self(o)->Prim7C(f, p); });
    gBeforeRun = nullptr;
    *GameAddress<void *>(0x10cc288) = gSavedSingleton;
    if (st.throws < 3000 || st.byRet[0] < 3000 || st.byRet[1] < 3000) ctx.Fail("coverage: %u throws, %u/%u", st.throws, st.byRet[0], st.byRet[1]);
}

// ============================================================================================================
// 0x59eb90 (opcode 0x31)
// ============================================================================================================

namespace {

void Build31(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 83);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[3], &person = w.fk[4], &a = w.fk[5], &b = w.fk[6], &c = w.fk[7], &d = w.fk[8],
         &e = w.fk[9];
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    Use(table.vt, 71, 2); // the value and a 0 the original pushes before calling the person's slot 47
    LookupObj((r() % 8 == 0) ? nullptr : &person, w);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);

    // The query always finds the interface (a failed query is a crash in the original): a, whose sub-interface at +4
    // has its own vtable.
    person.vt[0] = reinterpret_cast<void *>(&Qi0);
    RetObject(&person, 0, &a, w);
    Use(person.vt, 47, 0); // cTSObject::vf47 is a getter (ret)
    RetValue(&person, 47, static_cast<uint32_t>(r()), w);
    Use(a.vt, 2, 0);
    a.pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.vtSub[0]));
    uint8_t *subA = reinterpret_cast<uint8_t *>(&a) + 4;
    Use(w.vtSub[0], 21, 0);
    RetObject(subA, 21, (r() % 6 == 0) ? nullptr : &b, w);

    for (Fake *f : {&b, &c, &d, &e}) {
        Use(f->vt, 1, 0);
        Use(f->vt, 2, 0);
    }
    Use(b.vt, 20, 0);
    Use(b.vt, 9, 0);
    Use(b.vt, 14, 0);
    Use(b.vt, 32, 2);
    Use(c.vt, 23, 1);
    Use(d.vt, 22, 1);
    Use(e.vt, 3, 0);
    RetObject(&b, 20, (r() % 6 == 0) ? nullptr : &c, w);
    RetValue(&b, 9, static_cast<uint32_t>(r()), w);
    RetValue(&b, 14, static_cast<uint32_t>(r()), w);
    RetObject(&c, 23, (r() % 6 == 0) ? nullptr : &d, w);
    RetObject(&d, 22, (r() % 6 == 0) ? nullptr : &e, w);
    RetValue(&e, 3, Pick(r, {0, 0, 0x11, 0x11, 1, 0x10, 0x12, 0xffff0000u, 0xffff0011u, 0x10000, 0x10011}), w);
}

} // namespace

DIFF_TEST(PrimS_Prim31, 0x59eb90)
{
    Stats st = RunHandler(ctx, "Prim31", 0x59eb90, 60000, Build31,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return Self(o)->Prim31(f, p); });
    if (st.throws < 3000 || st.byRet[1] < 15000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.byRet[1]);
}
