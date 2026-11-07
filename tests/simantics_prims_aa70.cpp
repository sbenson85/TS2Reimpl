// Differential test for SimAntics opcode 0x70, Effect (0x5805c0, PrimAAObject::Prim70). Framework: tests/simantics_prims_aa.h.
//
// The cached services (singleton, dialog holder, UI service, the handle service), the framework object and the
// string-table loader are fakes pointed into the world through their game globals; the effect manager getter 0xb3c1d0
// is redirected to a stand-in. Every fake logs its calls (slot, arguments by kind), so the two runs are compared on
// the full call sequence, including the references given back while errors unwind.
#include "simantics_prims_aa.h"

namespace {

enum Role {
    CTX, FRAME, TABLE, SETTINGS, OWNER, ENTRY, GROUP, TARGET, IFACE, MID, P, ENTRYA, COMP, COMP2, EFF, EFFECT, UI, CURSOR, RECORD, INFO, OBJX,
    NAMED, FOUND, SINGLE, REGISTRY, IFACE3, ITEM, OWNER6C, DIALOGH, DIALOGS, HEADSET, FRAMEWORK, TAGSVC, G1OBJ, G2OBJ
};

void *gEff = nullptr;

// 0xb3c1d0 (__cdecl, one argument): the effect manager of a scene component.
void *__cdecl EffectManagerStub(void *component)
{
    gTrace.push_back(0xb3c1d0u);
    gTrace.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(component))));
    return gEff;
}

void Build70(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 701);
    Base(w, seed, r);
    uint8_t *obj = w.obj;
    uint8_t *p = w.param;
    auto vt = [&](int i) { return w.fk[i].vt; };
    auto F = [&](int i) -> Fake & { return w.fk[i]; };
    auto ro = [&](int i, int slot, int target) { RetObject(&F(i), slot, target < 0 ? nullptr : &F(target), w); };
    auto rv = [&](int i, int slot, uint32_t v) { RetValue(&F(i), slot, v, w); };
    auto oneIn = [&](uint32_t n) { return r() % n == 0; };

    // ---- the parameter bytes ---------------------------------------------------------------------------------------
    static const uint32_t kModes[] = {0, 0, 0, 1, 1, 1, 2, 3, 4, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15};
    const uint32_t mode = kModes[r() % (sizeof(kModes) / sizeof(kModes[0]))];
    p[4] = static_cast<uint8_t>(mode);
    p[5] = static_cast<uint8_t>(r());
    PutWord(w, 6, static_cast<uint16_t>(r()));
    PutWord(w, 8, oneIn(5) ? 0xffffu : static_cast<uint16_t>(r() % 8));
    p[0xa] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 4, 4, 5}));
    p[0xd] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 2, 3}));
    uint32_t flags = r() & 0xffffu;
    flags &= ~((1u << 12) | (1u << 2) | (1u << 5) | (1u << 4) | (1u << 13) | (1u << 8) | (1u << 10));
    const uint32_t attach = r() % 9;
    switch (attach) {
    case 1: flags |= 1u << 12; break;
    case 2: flags |= (1u << 2) | (1u << 8); break;
    case 3: flags |= (1u << 2) | (1u << 10); break;
    case 4: flags |= 1u << 2; break;
    case 5: flags |= 1u << 5; break;
    case 6: flags |= 1u << 4; break;
    case 7:
    case 8: flags |= 1u << 13; break;
    default: break;
    }
    PutWord(w, 0xe, static_cast<uint16_t>(flags));
    p[0x10] = static_cast<uint8_t>(r());
    PutWord(w, 0x11, static_cast<uint16_t>(r()));
    p[0x13] = static_cast<uint8_t>(r());
    const bool bit3 = (flags >> 3) & 1;

    // ---- the object ------------------------------------------------------------------------------------------------
    Th<19, 'V', 'X'>(w.vtThis);
    Th<13, 'V'>(w.vtThis);
    Th<265, 'V'>(w.vtThis);
    Th<266>(w.vtThis);
    for (uint32_t i = 1; i < 8; ++i) {
        uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(r())));
        if (oneIn(4)) v = Pick(r, {0, 1, 2, 3, 0xffffffffu, 5, 6});
        RetIndexed(obj, 19, i, v, w);
    }
    // Data word 0 is the name index with flag bit 3, data word 1 the group word with bit 6.
    RetIndexed(obj, 19, 0, oneIn(4) ? 0xffffffffu : (r() % 6), w);
    RetValue(obj, 13, oneIn(3) ? 0xffffffffu : (r() & 0xffff), w);
    gRets.push_back({WorldOff(obj, w), 265, true, WorldOff(w.vec, w)});
    RetValue(obj, 266, oneIn(4) ? Pick(r, {0, 0xffff, 0xfffe, 0x8000}) : (1 + r() % 5), w);
    // The shorts list the mode-7 branch clears.
    // (first, last, end) of the list: empty, or a malloc'd buffer of a few shorts with some of them in use.
    if (r() % 3 != 0) {
        const uint32_t cap = 1 + r() % 4, size = r() % (cap + 1);
        int16_t *buffer = static_cast<int16_t *>(std::malloc(cap * 2));
        for (uint32_t i = 0; i < cap; ++i) buffer[i] = static_cast<int16_t>(r());
        w.vec[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
        w.vec[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer + size));
        w.vec[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer + cap));
    }
    Th<7, 'V', 'V', 'V', 'V', 'V', 'V', 'V', 'R'>(w.vtSec);
    for (int i = 0; i < 6; ++i) QueueValue(7, static_cast<uint32_t>(static_cast<uint16_t>(r())) | (r() & 0xffff0000u));
    *reinterpret_cast<void **>(obj + 0x1c0) = &F(CURSOR);

    // ---- context, frame, tables ----------------------------------------------------------------------------------------
    Th<26>(vt(CTX));
    Th<10>(vt(CTX));
    Th<24>(vt(CTX));
    ro(CTX, 26, TABLE);
    ro(CTX, 10, SETTINGS);
    ro(CTX, 24, TABLE);
    Th<20>(vt(FRAME));
    ro(FRAME, 20, OWNER);
    Th<38>(vt(OWNER));
    Th<29, 'V', 'L'>(vt(TABLE));
    Th<14, 'V', 'L'>(vt(TABLE));
    const bool noTarget = oneIn(10);
    QueueObject(29, noTarget ? nullptr : &F(TARGET), w);
    QueueObject(29, oneIn(6) ? nullptr : &F(OBJX), w);
    QueueObject(29, oneIn(6) ? nullptr : &F(OBJX), w);
    QueueObject(14, &F(ENTRY), w);
    QueueObject(14, oneIn(5) ? nullptr : &F(RECORD), w);
    QueueObject(14, oneIn(5) ? nullptr : &F(RECORD), w);

    // ---- the language entries ----------------------------------------------------------------------------------------
    Th<70>(vt(ENTRY));
    Th<18>(vt(ENTRY));
    ro(ENTRY, 70, oneIn(4) ? -1 : GROUP);
    ro(ENTRY, 18, GROUP);
    Th<11>(vt(GROUP));
    gLoaderTexts.clear();
    gLoaderPos = 0;
    for (int i = 0; i < 4; ++i) gLoaderTexts.push_back(oneIn(5) ? std::string() : std::string(kTexts[1 + r() % (kTextCount - 1)]));

    // ---- the target and its component -----------------------------------------------------------------------------------
    Th<9, 'V', 'V', 'P'>(vt(TARGET));
    Th<104>(vt(TARGET));
    Th<0, 'V', 'P'>(vt(TARGET));
    Th<254, 'V'>(vt(TARGET));
    Th<251, 'V'>(vt(TARGET));
    Th<68, 'V'>(vt(TARGET));
    Th<82, 'V', 'V', 'c', 'V'>(vt(TARGET));
    Th<188>(vt(TARGET));
    Th<281, 'V', 'V', 'V', 'V', 'V'>(vt(TARGET));
    Th<282>(vt(TARGET));
    const bool wrapped = oneIn(4);
    rv(TARGET, 104, wrapped ? 1 : 0);
    gRets.push_back({WorldOff(&F(TARGET), w), OutKey(kOutObject, 9, 2), true, oneIn(10) ? 0xffffffffu : WorldOff(&F(COMP), w)});
    rv(TARGET, 9, oneIn(3) ? 0 : 1);
    // The wrapped path: QueryInterface -> iface slot 6 -> slot 11 gives the base object.
    gRets.push_back({WorldOff(&F(TARGET), w), OutKey(kOutObject, 0, 1), true, WorldOff(&F(IFACE), w)});
    rv(TARGET, 0, 1);
    Th<6>(vt(IFACE));
    ro(IFACE, 6, MID);
    Th<11>(vt(MID));
    ro(MID, 11, P);
    Th<9, 'V', 'V', 'P'>(vt(P));
    Th<98>(vt(P));
    gRets.push_back({WorldOff(&F(P), w), OutKey(kOutObject, 9, 2), true, oneIn(10) ? 0xffffffffu : WorldOff(&F(COMP2), w)});
    gRets.push_back({WorldOff(&F(TARGET), w), OutKey(kOutObject, 9, 2), true, oneIn(10) ? 0xffffffffu : WorldOff(&F(COMP), w)});
    rv(P, 9, oneIn(5) ? 0 : 1);
    const uint32_t kindOfBase = oneIn(3) ? Pick(r, {0, 1, 3, 5}) : 2;
    Th<98>(vt(TARGET));
    rv(P, 98, kindOfBase);
    rv(TARGET, 98, kindOfBase);
    MakeVirtualBase(w, COMP, 0);
    MakeVirtualBase(w, COMP2, 1);
    // kind lists: slots 254 / 251 / 68 give an entry object, its slot 3 an id
    Th<3>(vt(ENTRYA));
    ro(TARGET, 254, oneIn(5) ? -1 : ENTRYA);
    ro(TARGET, 251, oneIn(5) ? -1 : ENTRYA);
    ro(TARGET, 68, oneIn(5) ? -1 : ENTRYA);
    rv(ENTRYA, 3, oneIn(6) ? 0 : (1 + r() % 100));
    rv(TARGET, 188, oneIn(4) ? 0 : 1);

    // ---- the headline setting ------------------------------------------------------------------------------------------
    Th<5, 'V', 'V', 'P'>(vt(FRAMEWORK));
    gRets.push_back({WorldOff(&F(FRAMEWORK), w), OutKey(kOutObject, 5, 2), true, oneIn(4) ? 0xffffffffu : WorldOff(&F(HEADSET), w)});
    Th<9, 's', 'B'>(vt(HEADSET));
    rv(HEADSET, 9, oneIn(3) ? 0 : 1);
    gRets.push_back({WorldOff(&F(HEADSET), w), OutKey(kOutByte, 9, 1), false, oneIn(3) ? 0u : 1u});

    // ---- the effect manager and the effect -----------------------------------------------------------------------------
    Th<1, 'V', 'S', 'S', 'V', 'V', 'V'>(vt(EFF));
    Th<2, 'V', 'S', 'S', 'V', 'V'>(vt(EFF));
    Th<3, 'S', 'v'>(vt(EFF));
    Th<6, 'S', 'S', 'V'>(vt(EFF));
    Th<7, 'V', 'V'>(vt(EFF));
    Th<8, 'V'>(vt(EFF));
    Th<10, 'V'>(vt(EFF));
    Th<12, 'V', 'F', 'V'>(vt(EFF));
    ro(EFF, 6, oneIn(4) ? -1 : EFFECT);
    rv(EFF, 3, oneIn(4) ? 0 : 1);
    Th<13, 'V', 'F', 'V'>(vt(EFFECT));
    Th<14, 'V', 'M'>(vt(EFFECT));
    // handles from the UI service (and -1: invalid)
    Th<7, 'V', 'c'>(vt(UI));
    Th<6, 'c', 'V', 'V', 'V'>(vt(UI));
    Th<4, 'q', 'C', 'V'>(vt(UI));
    rv(UI, 7, oneIn(3) ? 0xffffffffu : (1 + r() % 50));
    rv(UI, 6, oneIn(3) ? 0xffffffffu : (1 + r() % 50));
    rv(UI, 4, oneIn(3) ? 0xffffffffu : (1 + r() % 50));
    Th<18, 'V'>(vt(TAGSVC));
    Th<19, 'V'>(vt(TAGSVC));

    // ---- the attach branches ---------------------------------------------------------------------------------------------
    Th<6>(vt(CURSOR));
    Th<4>(vt(RECORD));
    ro(RECORD, 4, INFO);
    Th<3, 'V'>(vt(INFO));
    Th<61>(vt(OBJX));
    Th<0, 'V', 'P'>(vt(OBJX));
    Th<27>(vt(NAMED));
    ro(OBJX, 61, NAMED);
    ro(NAMED, 27, RECORD);
    // A successful query always hands out the interface (the original uses it without a check).
    const bool qiOk = !oneIn(5);
    gRets.push_back({WorldOff(&F(OBJX), w), OutKey(kOutObject, 0, 1), true, qiOk ? WorldOff(&F(IFACE3), w) : 0xffffffffu});
    rv(OBJX, 0, qiOk ? 1 : 0);
    Th<16, 'V', 'V'>(vt(SETTINGS));
    ro(SETTINGS, 16, oneIn(4) ? -1 : FOUND);
    Th<10>(vt(FOUND));
    ro(FOUND, 10, RECORD);
    Th<4>(vt(SINGLE));
    ro(SINGLE, 4, oneIn(5) ? -1 : REGISTRY);
    Th<21, 'V'>(vt(REGISTRY));
    Th<22, 'V'>(vt(REGISTRY));
    gRets.push_back({WorldOff(&F(REGISTRY), w), 21, true, WorldOff(&w.vec[8], w)});
    gRets.push_back({WorldOff(&F(REGISTRY), w), 22, true, WorldOff(&w.vec[8], w)});
    const uint32_t itemCount = r() % 4;
    w.vec[8] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.vec[12]));
    w.vec[9] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.vec[12 + itemCount]));
    w.vec[10] = w.vec[9];
    for (uint32_t i = 0; i < itemCount; ++i) w.vec[12 + i] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&F(ITEM)));
    Th<57>(vt(IFACE3));
    Th<17>(vt(ITEM));
    Th<8, 'k'>(vt(ITEM));
    Th<9, 'k'>(vt(ITEM));
    ro(ITEM, 17, oneIn(3) ? -1 : OWNER6C);
    Th<38>(vt(OWNER6C));
    Th<28>(vt(OWNER6C));
    rv(OWNER6C, 38, oneIn(2) ? 0 : 1);
    Th<4>(vt(DIALOGH));
    ro(DIALOGH, 4, oneIn(5) ? -1 : DIALOGS);
    Th<14, 'V', 'V', 'P'>(vt(DIALOGS));
    Th<31, 'b', 'V', 'V', 'P', 'V'>(vt(DIALOGS));
    Th<60, 'K', 'V', 'P'>(vt(DIALOGS));
    gRets.push_back({WorldOff(&F(DIALOGS), w), OutKey(kOutObject, 14, 2), true, oneIn(3) ? 0xffffffffu : WorldOff(&F(G1OBJ), w)});
    gRets.push_back({WorldOff(&F(DIALOGS), w), OutKey(kOutObject, 31, 3), true, oneIn(3) ? 0xffffffffu : WorldOff(&F(G1OBJ), w)});
    gRets.push_back({WorldOff(&F(DIALOGS), w), OutKey(kOutObject, 60, 2), true, oneIn(3) ? 0xffffffffu : WorldOff(&F(G2OBJ), w)});
    rv(DIALOGS, 60, oneIn(2));
    gRets.push_back({WorldOff(&F(DIALOGS), w), OutKey(kOutByte, 31, 0), false, oneIn(2) ? 0u : 1u});
    // out texts of the fakes
    RetValue(&F(TARGET), OutKey(kOutText, 82, 2), r(), w);
    RetValue(&F(UI), OutKey(kOutText, 7, 1), r(), w);
    RetValue(&F(UI), OutKey(kOutText, 6, 0), r(), w);
    (void)bit3;
}

} // namespace

DIFF_TEST(PrimAA_Prim70, 0x5805c0)
{
    if (const char *e = std::getenv("AA_DUMP_SEED")) gDumpSeed = static_cast<uint32_t>(std::atoi(e));
    GlobalCell singleton(ctx, 0x10cc288), dialogs(ctx, 0x10cc2a0), ui(ctx, 0x10cc2a4), tags(ctx, 0x10f8f94), framework(ctx, 0x10c9378);
    LoaderFixture loader(ctx);
    Redirect manager(ctx, 0xb3c1d0, reinterpret_cast<void *>(&EffectManagerStub));
    AllocatorFixture allocator(ctx);
    gBeforeRun = [&](World &w) {
        singleton.Set(&w.fk[SINGLE]);
        dialogs.Set(&w.fk[DIALOGH]);
        ui.Set(&w.fk[UI]);
        tags.Set(&w.fk[TAGSVC]);
        framework.Set(&w.fk[FRAMEWORK]);
        gEff = &w.fk[EFF];
        gLoaderPos = 0;
    };
    Stats st = RunHandler(ctx, "Prim70", 0x5805c0, 20000, Build70,
                          [](uint8_t *o, void *f, const PrimParamView *p) { return reinterpret_cast<PrimAAObject *>(o)->Prim70(f, p); });
    gBeforeRun = nullptr;
    PrintHists();
    if (st.throws < 200 || st.returns < 2000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.returns);
}
