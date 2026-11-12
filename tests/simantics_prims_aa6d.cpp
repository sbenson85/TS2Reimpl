// Differential test for SimAntics opcode 0x6d, MaterialChange (0x5827e0, PrimAAObject::Prim6D). Framework:
// tests/simantics_prims_aa.h. The cached services (file service, state singleton, dialog holder, wall-art service, the
// material registry, the handle service ...) are fakes pointed into the world through their game globals; the director's
// framework object is reached through the director's own vtable cell; the string-table loader, the key parser, the
// texture builder, the resource finder and the option test are redirected to stand-ins; the file object's Open / SetPath
// and the property set's SetProperty are replaced in their vtables, so the real objects (file, property set, strings)
// still run their other code on both sides.
#include "sg/cTextureManager.h"
#include "sg/SGUtilities.h"
#include "code_patch.h"
#include "simantics_prims_aa.h"

#include "gz/cGZPropertySet.h"

extern "C" unsigned long _tls_index;

namespace {

enum Role {
    CTX, FRAME, TABLE, OWNER, LOCALE, GROUP, TARGET, OBJ2, SRC, COMP, SVC, STATE, DIALOGH, PANEL, DIALOGS, CTXA, FOUND, DIRFW, FACTORY, CUR, NAMED,
    TEXTURE, IFACE, SHADER, TEXLISTS, TEXTABLE, MANAGER, REGISTRY, MATOBJ, MATERIAL, MATLISTS, MATTABLE, TAG2SVC, PAINT, WALL, STATIONS, NAMES, CHAIN1,
    CHAIN2, SLOT23, RES, OBJQ
};

cRZString gSrcText; // what the object's text lookup (slot 6 of the field at +0x18c) hands out
bool gSrcConstructed = false;
bool gFileOpenOk = true;
bool gOption7 = false;
void *gTexture = nullptr;
void *gResource = nullptr;
void *gDirFw = nullptr;
void *gDirVt[16];

// ---- stand-ins for original functions ---------------------------------------------------------------------------------------

// 0xae7340 (__cdecl: out string, text, id, key): the key parser. Writes a derived text into the out string.
uint8_t __cdecl ParseKeyStub(cRZString *out, const char *text, void *id, const char *key)
{
    gTrace.push_back(0xae7340u);
    LogString(text);
    gTrace.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(id))));
    LogString(key);
    out->mString = std::string("k:") + (text ? text : "");
    return 1;
}

// 0xaece50 (__cdecl: material, result slot, name, flag): the texture builder.
void __cdecl MakeTextureStub(void *material, void **result, const char *name, uint32_t flag)
{
    gTrace.push_back(0xaece50u);
    gTrace.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(material))));
    gTrace.push_back(*result ? 0xbad0u : 0u);
    LogString(name);
    gTrace.push_back(flag);
    *result = gTexture;
}

// 0x54a410 (__cdecl: result slot): the resource finder.
uint8_t __cdecl FindResourceStub(void **result)
{
    gTrace.push_back(0x54a410u);
    gTrace.push_back(*result ? 0xbad0u : 0u);
    *result = gResource;
    return 1;
}

// 0xb394b0 (__cdecl: index): the option mask test.
uint8_t __cdecl OptionStub(uint32_t index)
{
    gTrace.push_back(0xb394b0u);
    gTrace.push_back(index);
    return gOption7 ? 1 : 0;
}

// The director's slot 12 (the framework object), called through the director cell in the image's data.
void *__fastcall DirectorSlot12(void *, void *)
{
    return gDirFw;
}

// ---- replaced vtable entries of real objects ---------------------------------------------------------------------------------

// cRZFile slot 3 (Open: access, creation, sharing) and slot 29 (SetPath: a cIGZString).
uint32_t __fastcall FileOpen(void *, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Tick();
    gTrace.push_back(0xf1e3u);
    gTrace.insert(gTrace.end(), {a, b, c});
    return gFileOpenOk ? 1 : 0;
}
uint32_t __fastcall FileSetPath(void *, void *, const cRZString *path)
{
    Tick();
    gTrace.push_back(0xf1e29u);
    LogString(path ? path->mString.c_str() : nullptr);
    return 0;
}

// Property set slot 3 (SetProperty: key std::string *, value holder).
uint32_t __fastcall PropSetSlot3(void *, void *, const std::string *key, const uint8_t *value)
{
    Tick();
    gTrace.push_back(0x7503u);
    LogString(key ? key->c_str() : nullptr);
    const void *vt = *reinterpret_cast<void *const *>(value);
    if (vt == GameAddress<void>(0xda3d48)) {
        gTrace.push_back(1);
        gTrace.push_back(*reinterpret_cast<const uint32_t *>(value + 4));
    } else if (vt == GameAddress<void>(0xd9c670)) {
        gTrace.push_back(2);
        gTrace.push_back(value[4]);
    } else {
        gTrace.push_back(3);
    }
    return 0;
}

// Replaces one 4-byte table entry in the image, or in one of our vtables (restored on exit).
struct VtPatch {
    uint32_t *entry;
    uint32_t saved;
    VtPatch(difftest::Context &ctx, uint32_t vtableRva, int slot, void *stand_in) : VtPatch(ctx.image.At<uint32_t>(vtableRva), slot, stand_in) {}
    VtPatch(uint32_t *vtable, int slot, void *stand_in) : entry(vtable + slot), saved(*entry)
    {
        DWORD old;
        VirtualProtect(entry, 4, PAGE_READWRITE, &old);
        *entry = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stand_in));
    }
    VtPatch(const VtPatch &) = delete;
    VtPatch &operator=(const VtPatch &) = delete;
    ~VtPatch() { *entry = saved; }
};

// Our cGZPropertySetString's vtable, read from an instance.
uint32_t *OurPropertySetVTable()
{
    cGZPropertySetString probe;
    return *reinterpret_cast<uint32_t **>(static_cast<cIGZPropertySetString *>(&probe));
}

void Build6D(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 6001);
    Base(w, seed, r);
    uint8_t *obj = w.obj;
    uint8_t *p = w.param;
    auto vt = [&](int i) { return w.fk[i].vt; };
    auto F = [&](int i) -> Fake & { return w.fk[i]; };
    auto ro = [&](int i, int slot, int target) { RetObject(&F(i), slot, target < 0 ? nullptr : &F(target), w); };
    auto rv = [&](int i, int slot, uint32_t v) { RetValue(&F(i), slot, v, w); };
    auto outObject = [&](int i, int slot, int arg, int target) {
        gRets.push_back({WorldOff(&F(i), w), OutKey(kOutObject, slot, arg), true, target < 0 ? 0xffffffffu : WorldOff(&F(target), w)});
    };
    auto oneIn = [&](uint32_t n) { return r() % n == 0; };

    if (!gSrcConstructed) {
        gSrcText.Construct();
        gSrcConstructed = true;
    }
    gSrcText.mString = oneIn(3) ? std::string() : std::string(kTexts[r() % kTextCount]);

    // ---- the parameter bytes ---------------------------------------------------------------------------------------
    PutWord(w, 4, static_cast<uint16_t>(r() % 6));
    p[6] = static_cast<uint8_t>(r());
    PutWord(w, 7, oneIn(5) ? 0xffffu : static_cast<uint16_t>(r() % 6));
    p[9] = static_cast<uint8_t>(r());
    PutWord(w, 0xa, static_cast<uint16_t>(r()));
    p[0xc] = static_cast<uint8_t>(r());
    PutWord(w, 0xd, static_cast<uint16_t>(r()));
    // The job: none, file/panel (bit 1 or bit 5), dialog (bit 2), name (bit 3), property set (bit 4); bit 0 any time.
    const uint32_t job = r() % 11;
    uint32_t flags11 = (r() & r()) & 0x3fu; // sparse random bits 0..5
    switch (job) {
    case 1:
    case 2: flags11 |= 1u << 1; break;
    case 3: flags11 |= 1u << 5; break;
    case 4:
    case 5:
    case 6: flags11 |= 1u << 2; flags11 &= ~0x22u; break;
    case 7:
    case 8: flags11 |= 1u << 3; flags11 &= ~0x26u; break;
    case 9: flags11 |= 1u << 4; flags11 &= ~0x2eu; break;
    default: flags11 &= ~0x3eu; break;
    }
    p[0x11] = static_cast<uint8_t>(flags11);
    const bool fA = (flags11 & 0x22u) != 0, fB = (flags11 & 4u) != 0, f6a = (flags11 & 8u) != 0, f3a = (flags11 & 0x10u) != 0;
    const bool bit0 = p[6] & 1;
    const bool bit3 = (p[6] >> 3) & 1;
    (void)f6a;
    (void)f3a;

    // ---- the object ------------------------------------------------------------------------------------------------
    Th<19, 'V', 'X'>(w.vtThis);
    Th<20, 'V', 'V'>(w.vtThis);
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(r())));
        if (oneIn(3)) v = Pick(r, {0, 1, 2, 3, 0xffffffffu, 5, 6});
        if ((i == 1 || i == 2) && oneIn(2)) v = (i == 1) ? 0 : 1;
        RetIndexed(obj, 19, i, v, w);
    }
    // The object's text source at +0x18c (also in the fakes that stand for other holders of the field).
    for (uint8_t *holder : {obj, reinterpret_cast<uint8_t *>(&F(TARGET)), reinterpret_cast<uint8_t *>(&F(OBJ2))}) {
        *reinterpret_cast<void **>(holder + 0x18c) = &F(SRC);
    }
    Th<6, 'V', 'V'>(vt(SRC));
    rv(SRC, 6, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gSrcText)));
    Th<7, 'V', 'V', 'V', 'V', 'V', 'V', 'V', 'R'>(w.vtSec);
    for (int i = 0; i < 6; ++i) QueueValue(7, static_cast<uint32_t>(static_cast<uint16_t>(r())) | (r() & 0xffff0000u));

    // ---- context, frame, tables ----------------------------------------------------------------------------------------
    Th<26>(vt(CTX));
    Th<24>(vt(CTX));
    Th<10>(vt(CTX));
    Th<23>(vt(CTX));
    ro(CTX, 26, TABLE);
    ro(CTX, 24, TABLE);
    ro(CTX, 10, CTXA);
    ro(CTX, 23, CHAIN1);
    Th<20>(vt(FRAME));
    Th<18>(vt(FRAME));
    Th<19, 'V'>(vt(FRAME));
    Th<12>(vt(FRAME));
    ro(FRAME, 20, OWNER);
    rv(FRAME, 18, oneIn(2) ? 0 : (1 + r() % 3));
    rv(FRAME, 12, oneIn(4) ? 0xffff : (r() & 0xffff));
    Th<38>(vt(OWNER));
    Th<29, 'V', 'L'>(vt(TABLE));
    Th<14, 'V', 'L'>(vt(TABLE));
    Th<42, 'V'>(vt(TABLE));
    QueueObject(29, oneIn(10) ? nullptr : &F(TARGET), w);
    for (int i = 0; i < 3; ++i) QueueObject(29, oneIn(10) ? nullptr : &F(OBJ2), w);
    // lists.slot14: the locale lookups come first, then (dialog job) the person
    if (!fA && !fB && !bit3) QueueObject(14, &F(LOCALE), w);
    if (!bit0) QueueObject(14, &F(LOCALE), w);
    QueueObject(14, oneIn(5) ? nullptr : &F(LOCALE), w);
    QueueObject(14, oneIn(5) ? nullptr : &F(LOCALE), w);
    ro(TABLE, 42, oneIn(3) ? -1 : OBJQ);
    Th<70>(vt(LOCALE));
    Th<18>(vt(LOCALE));
    Th<38>(vt(LOCALE));
    Th<61>(vt(OBJ2));
    ro(LOCALE, 70, oneIn(4) ? -1 : GROUP);
    ro(LOCALE, 18, GROUP);
    ro(OBJ2, 61, LOCALE);
    rv(LOCALE, 38, oneIn(2) ? 0 : 1);
    Th<11>(vt(GROUP));
    gLoaderTexts.clear();
    gLoaderPos = 0;
    for (int i = 0; i < 4; ++i) gLoaderTexts.push_back(oneIn(5) ? std::string() : std::string(kTexts[1 + r() % (kTextCount - 1)]));

    // ---- the target's component ------------------------------------------------------------------------------------------
    Th<9, 'V', 'V', 'P'>(vt(TARGET));
    Th<296, 'V'>(vt(TARGET));
    outObject(TARGET, 9, 2, oneIn(12) ? -1 : COMP);
    rv(TARGET, 9, 1);
    MakeVirtualBase(w, COMP, 0);
    Th<104, 'S', 'S', 'V'>(vt(COMP));

    // ---- the cached services ---------------------------------------------------------------------------------------------
    Th<5>(vt(SVC));
    ro(SVC, 5, REGISTRY);
    Th<33, 'y', 'V'>(vt(REGISTRY));
    Th<23, 'V', 'V'>(vt(REGISTRY));
    Th<32, 'S'>(vt(REGISTRY));
    outObject(REGISTRY, 33, 0, MATOBJ);
    rv(REGISTRY, 23, oneIn(3) ? 0xffffffffu : (1 + r() % 40));
    rv(REGISTRY, 32, oneIn(3) ? 1 : 0);
    Th<7>(vt(MATOBJ));
    ro(MATOBJ, 7, MATERIAL);
    Th<13, 'S', 'S'>(vt(MATERIAL));
    Th<10, 'S'>(vt(MATERIAL));
    Th<9>(vt(MATERIAL));
    ro(MATERIAL, 9, MATLISTS);
    Th<12, 'S', 'V'>(vt(MATLISTS));
    Th<8>(vt(MATLISTS));
    ro(MATLISTS, 8, MATTABLE);
    Th<3, 'p', 'V'>(vt(MATTABLE));
    Th<14, 'V', 'V'>(vt(MANAGER));
    Th<23>(vt(STATE));
    rv(STATE, 23, oneIn(5) ? 10 : (r() % 12));
    Th<7>(vt(DIALOGH));
    Th<4>(vt(DIALOGH));
    ro(DIALOGH, 7, PANEL);
    ro(DIALOGH, 4, oneIn(5) ? -1 : DIALOGS);
    Th<19, 'S'>(vt(TAG2SVC));
    Th<20, 'V'>(vt(TAG2SVC));
    Th<21, 'V'>(vt(TAG2SVC));
    rv(TAG2SVC, 19, 1 + r() % 60);
    Th<42>(vt(PAINT));
    Th<66>(vt(PAINT));
    ro(PAINT, 42, oneIn(5) ? -1 : WALL);
    ro(PAINT, 66, oneIn(4) ? -1 : STATIONS);
    Th<7, 'V'>(vt(WALL));
    Th<8, 'S'>(vt(WALL));
    Th<6, 'V', 'S'>(vt(WALL));
    rv(WALL, 8, oneIn(2));
    Th<8, 'C', 'c', 'V'>(vt(STATIONS));
    rv(STATIONS, 8, oneIn(2));
    RetValue(&F(STATIONS), OutKey(kOutText, 8, 1), r(), w);
    Th<11, 'V'>(vt(NAMES));
    Th<10>(vt(CHAIN1));
    Th<8>(vt(CHAIN2));
    ro(CHAIN1, 10, CHAIN2);
    Th<11>(vt(SLOT23));

    // ---- the panel and the file job ----------------------------------------------------------------------------------------
    Th<18, 'V', 'V'>(vt(PANEL));
    Th<20>(vt(PANEL));
    Th<17>(vt(PANEL));
    Th<22>(vt(PANEL));
    Th<21, 'c'>(vt(PANEL));
    Th<23, 'S', 'V'>(vt(PANEL));
    rv(PANEL, 20, oneIn(3) ? 0 : 1);
    rv(PANEL, 22, oneIn(2));
    rv(PANEL, 23, oneIn(2));
    RetValue(&F(PANEL), OutKey(kOutText, 21, 0), r(), w);
    Th<3, 'V', 'V', 'P'>(vt(DIRFW));
    Th<7, 'u'>(vt(DIRFW));
    const bool haveFactory = !oneIn(5); // a successful query always gives the object (used unchecked)
    outObject(DIRFW, 3, 2, haveFactory ? FACTORY : -1);
    rv(DIRFW, 3, haveFactory ? 1 : 0);
    RetValue(&F(DIRFW), OutKey(kOutByte, 7, 0), (r() % 3 == 0) ? 0xffffu : r(), w);
    Th<12, 'p', 'V', 'P', 'V'>(vt(FACTORY));
    const bool haveCur = !oneIn(5); // loaded implies the object (f69 then calls it unchecked)
    outObject(FACTORY, 12, 2, haveCur ? CUR : -1);
    rv(FACTORY, 12, haveCur ? 1 : 0);
    Th<12, 'V'>(vt(CUR));
    ro(CUR, 12, NAMED);
    gFileOpenOk = !oneIn(4);

    // ---- the dialog job ------------------------------------------------------------------------------------------------------
    Th<16, 'V', 'V'>(vt(CTXA));
    ro(CTXA, 16, oneIn(3) ? -1 : FOUND);
    Th<15>(vt(FOUND));
    rv(FOUND, 15, oneIn(4) ? 0 : (1 + r() % 100));
    Th<17, 'V', 'V', 'V', 'V', 'P', 'V', 'p'>(vt(DIALOGS));
    Th<15, 'V', 'V', 'P'>(vt(DIALOGS));
    Th<36, 'V', 'V', 'P'>(vt(DIALOGS));
    Th<63, 'p', 'V', 'P'>(vt(DIALOGS));
    outObject(DIALOGS, 17, 4, oneIn(3) ? -1 : CUR);
    outObject(DIALOGS, 15, 2, oneIn(3) ? -1 : CUR);
    outObject(DIALOGS, 36, 2, oneIn(3) ? -1 : CUR);
    outObject(DIALOGS, 63, 2, oneIn(3) ? -1 : CUR);

    // ---- the material job ------------------------------------------------------------------------------------------------------
    Th<0, 'V', 'P'>(vt(TEXTURE));
    // The query always succeeds in the world: when it fails the original goes on with a null shader and faults.
    outObject(TEXTURE, 0, 1, IFACE);
    rv(TEXTURE, 0, 1);
    Th<11>(vt(IFACE));
    ro(IFACE, 11, SHADER);
    Th<5>(vt(SHADER));
    Th<12, 'S', 'V'>(vt(TEXLISTS));
    Th<8>(vt(TEXLISTS));
    ro(SHADER, 5, TEXLISTS);
    ro(TEXLISTS, 8, TEXTABLE);
    Th<3, 'p', 'V'>(vt(TEXTABLE));
    (void)obj;
}

} // namespace

DIFF_TEST(PrimAA_Prim6D, 0x5827e0)
{
    if (const char *e = std::getenv("AA_DUMP_SEED")) gDumpSeed = static_cast<uint32_t>(std::atoi(e));
    gLive = std::getenv("AA_LIVE") != nullptr;
    std::set_terminate(&TerminateReport);
    GlobalCell svc(ctx, 0x10cc2c8), state(ctx, 0x10cc28c), dialogs(ctx, 0x10cc2a0), paint(ctx, 0x10cc458), manager(ctx, 0x10cc2d8),
        tag2(ctx, 0x10f8fcc), names(ctx, 0x10faf6c), slot23(ctx, 0x10cc2a8), serviceObject(ctx, 0x10c9378), directorGuard(ctx, 0x10c93f4),
        directorVt(ctx, 0x10c9390), tlsIndex(ctx, 0x10df814);
    *tlsIndex.cell = _tls_index; // the game's TLS index cell: the director getter reads this module's block through it
    LoaderFixture loader(ctx);
    shape::StandIn parser(0xae7340, &nGZSceneGraph::ResolveResourceName, &ParseKeyStub); // the image's and ours
    shape::StandIn texture(0xaece50, &nGZSceneGraph::MakeTextureFromBuffer, &MakeTextureStub); // the image's and ours
    Redirect finder(ctx, 0x54a410, reinterpret_cast<void *>(&FindResourceStub));
    Redirect option(ctx, 0xb394b0, reinterpret_cast<void *>(&OptionStub));
    AllocatorFixture allocator(ctx);
    VtPatch fileOpen(ctx, 0xda42a0, 3, reinterpret_cast<void *>(&FileOpen));
    VtPatch fileSetPath(ctx, 0xda42a0, 29, reinterpret_cast<void *>(&FileSetPath));
    // The original builds the game's set (vtable 0xd9f718), ours builds our cGZPropertySetString (NewPropertySet, lane EU):
    // slot 3 (SetProperty) of both vtables logs instead.
    VtPatch propSet(ctx, 0xd9f718, 3, reinterpret_cast<void *>(&PropSetSlot3));
    VtPatch ourPropSet(OurPropertySetVTable(), 3, reinterpret_cast<void *>(&PropSetSlot3));
    gDirVt[12] = reinterpret_cast<void *>(&DirectorSlot12);
    gBeforeRun = [&](World &w) {
        static uint32_t nullService = 0;
        const bool noService = (gSeed % 23) == 0;
        svc.Set(noService ? nullptr : &w.fk[SVC]);
        serviceObject.Set(nullptr);
        state.Set(&w.fk[STATE]);
        dialogs.Set(&w.fk[DIALOGH]);
        paint.Set(&w.fk[PAINT]);
        manager.Set(&w.fk[MANAGER]);
        tag2.Set(&w.fk[TAG2SVC]);
        names.Set(&w.fk[NAMES]);
        slot23.Set(&w.fk[SLOT23]);
        *directorGuard.cell = 0x80000000u;
        *directorVt.cell = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gDirVt));
        gDirFw = &w.fk[DIRFW];
        gTexture = &w.fk[TEXTURE];
        gResource = &w.fk[RES];
        gOption7 = (gSeed % 3) == 0;
        gLoaderPos = 0;
        (void)nullService;
    };
    Stats st = RunHandler(ctx, "Prim6D", 0x5827e0, 20000, Build6D,
                          [](uint8_t *o, void *f, const PrimParamView *p) { return reinterpret_cast<PrimAAObject *>(o)->Prim6D(f, p); });
    gBeforeRun = nullptr;
    PrintHists();
    if (st.throws < 100 || st.returns < 2000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.returns);
}
