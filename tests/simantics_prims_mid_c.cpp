// Opcodes 0x14 and 0x20 (framework and notes: tests/simantics_prims_mid.h).
#include "simantics_prims_mid.h"

// ============================================================================================================
// 0x580360 (opcode 0x14)
// ============================================================================================================

namespace {

void BuildPrim14(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 23);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[3], &person = w.fk[4];
    int sel;
    switch (r() % 5) {
    case 0: sel = static_cast<int>(r() % 0x1c); break;
    case 1: sel = 0x18; break;
    case 2: sel = 0x1c + static_cast<int>(r() % 4); break;
    case 3: sel = -1 - static_cast<int>(r() % 3); break;
    default: sel = static_cast<int>(r() % 0x1c); break;
    }
    PutWord(w, 4, static_cast<uint16_t>(sel));
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 8 == 0) ? nullptr : &person, w);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    Use(person.vt, 64, 0);
    Use(person.vt, 93, 1);
    Use(person.vt, 94, 1);
    const uint32_t junk = static_cast<uint32_t>(r()) & 0xffff0000u;
    RetValue(&person, 93, junk | ((r() % 3 == 0) ? 0u : (r() & 0xffff)), w);
    RetValue(&person, 94, junk | ((r() % 3 == 0) ? 0u : (r() & 0xffff)), w);
    frame.vt[35] = reinterpret_cast<void *>(&Frame35);
    w.vtThis[248] = reinterpret_cast<void *>(&Slot248);
    RetValue(&w.obj, 248, (r() % 2) ? (static_cast<uint32_t>(r()) | 1u) : (static_cast<uint32_t>(r()) & ~0xffu), w);
}

} // namespace

DIFF_TEST(PrimMid_Prim14, 0x580360)
{
    Stats st = RunHandler(ctx, "Prim14", 0x580360, 40000, BuildPrim14,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->Prim14(f, p); });
    if (st.throws < 1000 || st.byRet[0] < 3000 || st.byRet[3] < 3000)
        ctx.Fail("coverage: %u throws, %u/%u", st.throws, st.byRet[0], st.byRet[3]);
}

// ============================================================================================================
// 0x5a79f0 (opcode 0x20)
// ============================================================================================================

namespace {

void BuildPrim20(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 29);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &table = w.fk[3], &list = w.fk[4], &owner = w.fk[5], &mid = w.fk[6], &entry = w.fk[7], &alt = w.fk[8],
         &inner = w.fk[9];
    Fake &sec = *reinterpret_cast<Fake *>(reinterpret_cast<uint8_t *>(&w.obj) + 4);

    const bool magic = r() % 4 == 0;
    uint32_t wanted = magic ? 0x4c7cab2bu : (r() % 3 == 0 ? static_cast<uint32_t>(r()) : (r() % 1000));
    std::memcpy(w.param + 4, &wanted, 4);
    uint32_t fromObject = (r() % 3 == 0) ? static_cast<uint32_t>(r()) : (r() % 1000);
    std::memcpy(w.bytes + 0xa, &fromObject, 4);
    const uint32_t effective = magic ? fromObject : wanted;
    w.param[0xb] = static_cast<uint8_t>((r() % 6 == 0) ? r() : (r() % 8));
    sec.vt = w.vtSec;
    w.vtSec[7] = reinterpret_cast<void *>(&Read7);
    RetValue(&sec, 7, Edge16(r) | (r() & 0xffff0000u), w);

    Use(w.vtThis, 289, 0);
    RetObject(&w.obj, 289, w.bytes, w);
    Use(w.vtThis, 20, 2);
    Use(w.vtThis, 313, 1);
    Use(w.vtThis, 63, 0);
    RetObject(&w.obj, 63, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 6 == 0) ? nullptr : &mid, w);

    Use(ctx.vt, 24, 0);
    Use(ctx.vt, 10, 0);
    RetObject(&ctx, 24, &list, w);
    RetObject(&ctx, 10, &owner, w);
    Use(list.vt, 14, 1);
    RetValue(&list, 14, (r() % 3 == 0) ? 0u : static_cast<uint32_t>(r()) | 1u, w);
    Use(owner.vt, 16, 2);
    RetObject(&owner, 16, (r() % 6 == 0) ? nullptr : &mid, w);
    Use(mid.vt, 10, 0);
    Use(mid.vt, 61, 0);
    RetObject(&mid, 10, (r() % 6 == 0) ? nullptr : &entry, w);
    RetObject(&mid, 61, (r() % 6 == 0) ? nullptr : &entry, w);
    Use(entry.vt, 32, 0);
    Use(entry.vt, 27, 0);
    Use(entry.vt, 28, 0);
    RetObject(&entry, 32, (r() % 4 == 0) ? nullptr : &alt, w);
    RetObject(&entry, 27, &inner, w);
    Use(alt.vt, 28, 0);
    Use(inner.vt, 28, 0);
    auto value = [&]() { return (r() % 3 == 0) ? effective : static_cast<uint32_t>(r()); };
    RetValue(&entry, 28, value(), w);
    RetValue(&alt, 28, value(), w);
    RetValue(&inner, 28, value(), w);
}

} // namespace

DIFF_TEST(PrimMid_Prim20, 0x5a79f0)
{
    Stats st = RunHandler(ctx, "Prim20", 0x5a79f0, 60000, BuildPrim20,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->Prim20(f, p); });
    if (st.byRet[0] < 3000 || st.byRet[1] < 3000) ctx.Fail("coverage: %u/%u", st.byRet[0], st.byRet[1]);
}
