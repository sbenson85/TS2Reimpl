// Cinematic (0x585590), UIEffect (0x5a7c60), MakeActionString (0x59d740) and the text copy helper (0x26be10).
// Fixture: tests/simantics_prims_s_text.h. The string loaders (0x1d0fb0, 0x1d0df0, 0x1d0e90, 0x1d1000: ours on one side, the original on the other, both against the
// fixture's fake string service), the cached service getters and the UI manager helpers run on both sides.
#include "simantics_prims_s_text.h"
#include "simantics/AutonomyHelpers.h"

#include <string>

// ============================================================================================================
// 0x26be10 (the text copy)
// ============================================================================================================

DIFF_TEST(PrimS_AssignText, 0x26be10)
{
    auto original = ctx.Original<SString *(__thiscall *)(void *, const void *)>(0x26be10);
    std::mt19937 r(5);
    const char *texts[] = {"", "a", "hello", "exactly15chars_", "sixteen chars!!!", "a much longer text that needs a heap block, really"};
    for (int i = 0; i < 4000; ++i) {
        const char *dstText = texts[r() % 6];
        const char *srcText = texts[r() % 6];
        const bool same = r() % 5 == 0;
        auto make = [&](cRZString &s, const char *t) { s.Construct(t); };
        // The original's world and ours.
        cRZString a1, a2, b1, b2;
        make(a1, dstText);
        make(a2, srcText);
        make(b1, dstText);
        make(b2, srcText);
        SString *ra = original(&a1, same ? static_cast<void *>(&a1) : static_cast<void *>(&a2));
        SString *rb = reinterpret_cast<SString *>(&b1)->AssignText(same ? reinterpret_cast<const SString *>(&b1) : reinterpret_cast<const SString *>(&b2));
        ++ctx.cases;
        const bool ok = ra == reinterpret_cast<SString *>(&a1) && rb == reinterpret_cast<SString *>(&b1) && a1.mString == b1.mString &&
                        a1.mString.capacity() == b1.mString.capacity() && a2.mString == b2.mString && a1.mRefCount == b1.mRefCount;
        if (!ok) ctx.Fail("AssignText case %d: result/text/capacity differ ('%s' vs '%s')", i, a1.mString.c_str(), b1.mString.c_str());
        a1.Destruct();
        a2.Destruct();
        b1.Destruct();
        b2.Destruct();
    }
}

// ============================================================================================================
// 0x585590 (opcode 0x7b, Cinematic)
// ============================================================================================================

namespace {

// The cinematic service's slot 27: (text, target, flag, flag, flag, flag, int *error). Logs the text, answers from the
// script and writes the error code.
uint32_t __fastcall Svc27(void *self, void *, const char *text, uint32_t target, uint32_t f1, uint32_t f2, uint32_t f3, uint32_t f4, int32_t *error)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(27u << 8 | 7u);
    gTrace.push_back(static_cast<uint32_t>(std::strlen(text)));
    for (const char *p = text; *p; ++p) gTrace.push_back(static_cast<uint8_t>(*p));
    gTrace.insert(gTrace.end(), {Norm(target), f1, f2, f3, f4, error ? 1u : 0u});
    if (error) *error = static_cast<int32_t>(Scripted(self, 1027));
    return Scripted(self, 27);
}

void Build7B(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 101);
    Base(w, seed, r);
    SetParam(w, r);
    gNullUi = false;
    gManagerOn = false;
    BuildLocale(w, r);
    BuildStrings(w, r, 1);
    Fake &ctx = w.fk[0], &table = w.fk[3], &object = w.fk[8], &target = w.fk[9], &service = w.fk[12];
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 6 == 0) ? nullptr : &object, w);
    Use(object.vt, 266, 0);
    Use(object.vt, 265, 1);
    RetValue(&object, 266, static_cast<uint32_t>(Pick(r, {1, 2, 3, 5, 0x7fff, 1, 2, 0, 0xffff, 0x8000})) | (r() & 0xffff0000u) * (r() % 2), w);
    RetObject(&object, 265, (r() % 5 == 0) ? nullptr : &target, w);
    gReadVals.clear();
    gReadPos = 0;
    for (int i = 0; i < 3; ++i) gReadVals.push_back(ReadWord(r));
    w.vtSec[7] = reinterpret_cast<void *>(&ReadSeq);
    Use(w.vtThis, 19, 1);
    RetValue(&w.obj, 19, (r() % 2) ? 0u : static_cast<uint32_t>(r()), w);
    service.vt[27] = reinterpret_cast<void *>(&Svc27);
    RetValue(&service, 27, (r() % 2) ? (static_cast<uint32_t>(r()) | 1u) : (static_cast<uint32_t>(r()) & ~0xffu), w);
    RetValue(&service, 1027, Pick(r, {0xffffffffu, 0xfffffffbu, 0xffffffffu, 0xfffffffeu, 0xffffff00u, 0, 1, 0x1c, 0x18000u, 0x80000000u}), w);
}

} // namespace

DIFF_TEST(PrimS_Prim7B, 0x585590)
{
    TextFixture fixture(ctx);
    Stats st = RunHandler(ctx, "Prim7B", 0x585590, 40000, Build7B,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return Self(o)->Prim7B(f, p); });
    if (st.throws < 2000 || st.byRet[0] < 2000 || st.byRet[1] < 2000) ctx.Fail("coverage: %u throws, %u/%u", st.throws, st.byRet[0], st.byRet[1]);
}

// ============================================================================================================
// 0x5a7c60 (opcode 0x22, UIEffect)
// ============================================================================================================

namespace {

void Build22(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 103);
    Base(w, seed, r);
    SetParam(w, r);
    gNullUi = false; // a null cache would make the getter (0x20e031) look the service up
    gManagerOn = false;
    BuildLocale(w, r);
    BuildStrings(w, r, 2);
    Fake &ui = w.fk[12], &t = w.fk[9], &target = w.fk[8], &director = w.fk[13], &registry = w.fk[6], &outer = w.fk[7];
    Fake &inner = w.sub[0];
    w.param[4] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 3, 4, 4, 5, 6, 7, 7, 8, 8, 8, 9, 10, 11, 255}));
    gReadVals.clear();
    gReadPos = 0;
    gReadVals.push_back(ReadWord(r));
    w.vtSec[7] = reinterpret_cast<void *>(&ReadSeq);
    Use(w.vtThis, 47, 0);
    RetValue(&w.obj, 47, static_cast<uint32_t>(r()), w);

    Use(ui.vt, 3, 0);
    Use(ui.vt, 12, 0);
    RetObject(&ui, 3, (r() % 4 == 0) ? nullptr : &t, w);
    RetObject(&ui, 12, &target, w);
    Use(t.vt, 5, 3);
    Use(target.vt, 8, 1);
    Use(target.vt, 9, 1);
    Use(target.vt, 10, 2);
    Use(target.vt, 11, 2);
    Use(target.vt, 12, 1);
    Use(target.vt, 13, 3);
    Use(target.vt, 14, 0);
    Use(target.vt, 15, 1);
    Use(director.vt, 3, 0);
    RetObject(&director, 3, &registry, w);
    Use(registry.vt, 39, 1);
    Use(outer.vt, 39, 1);
    RetObject(&registry, 39, (r() % 6 == 0) ? nullptr : &outer, w);
    RetObject(&outer, 39, (r() % 6 == 0) ? nullptr : &inner, w);
}

} // namespace

DIFF_TEST(PrimS_Prim22, 0x5a7c60)
{
    TextFixture fixture(ctx);
    Stats st = RunHandler(ctx, "Prim22", 0x5a7c60, 60000, Build22, [](PrimMidObject *o, void *f, const PrimParamView *p) { return Self(o)->Prim22(f, p); });
    if (st.throws < 3000 || st.byRet[1] < 8000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.byRet[1]);
}

// ============================================================================================================
// 0x59d740 (opcode 0x32, MakeActionString)
// ============================================================================================================

namespace {

void LogText(const void *s)
{
    const std::string &text = reinterpret_cast<const cRZString *>(s)->mString;
    gTrace.push_back(static_cast<uint32_t>(text.size()));
    for (char c : text) gTrace.push_back(static_cast<uint8_t>(c));
}

// this slot 262: (text*, 0, frame, block*, extra*): logs the text, writes the first block word and the extra dword
// from the script.
uint32_t __fastcall Slot262(void *self, void *, void *text, uint32_t zero, void *frame, uint16_t *block, uint32_t *extra)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(262u << 8 | 5u);
    gTrace.insert(gTrace.end(), {zero, Norm(reinterpret_cast<uint32_t>(frame)), block[0], block[1], block[2], block[3], *extra});
    LogText(text);
    block[0] = static_cast<uint16_t>(Scripted(self, 2620));
    *extra = Scripted(self, 2621);
    return Scripted(self, 262);
}

// The display target's slot 26: (text*).
uint32_t __fastcall Slot26(void *self, void *, void *text)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(26u << 8 | 1u);
    LogText(text);
    return Scripted(self, 26);
}

void Build32(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 107);
    Base(w, seed, r);
    SetParam(w, r);
    gNullUi = false;
    gManagerOn = (r() % 3 == 0);
    BuildLocale(w, r);
    BuildStrings(w, r, 4);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[3], &iface = w.fk[6], &display = w.fk[7], &object = w.fk[8], &manager = w.fk[13];
    Fake &sec = *reinterpret_cast<Fake *>(reinterpret_cast<uint8_t *>(&w.obj) + 4);
    (void)sec;

    PutWord(w, 6, static_cast<uint16_t>(r()));
    w.param[0xd] = (r() % 2) ? static_cast<uint8_t>(r() | 1) : 0;
    gReadVals.clear();
    gReadPos = 0;
    gReadVals.push_back(ReadWord(r));
    w.vtSec[7] = reinterpret_cast<void *>(&ReadSeq);
    Use(w.vtSec, 33, 0);
    RetValue(reinterpret_cast<uint8_t *>(&w.obj) + 4, 33, (r() % 3) ? (static_cast<uint32_t>(r()) | 1u) : (static_cast<uint32_t>(r()) & ~0xffu), w);

    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 6 == 0) ? nullptr : &object, w);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    Use(w.vtThis, 19, 1);
    RetValue(&w.obj, 19, (r() % 4 == 0) ? 0u : static_cast<uint32_t>(r()), w);

    // this slot 0: QueryInterface, the answer from a separate script. With the UI manager present and the answer yes,
    // the handler calls the display code 0x4526a0, which the test redirects to a recording stub.
    w.vtThis[0] = reinterpret_cast<void *>(&QiS);
    // (the handlers use the object whenever the answer is yes: the original crashes on a yes without an object)
    const bool haveIface = r() % 6 != 0;
    RetObject(&w.obj, 0, haveIface ? &iface : nullptr, w);
    RetValue(&w.obj, 1000, (!haveIface) ? (static_cast<uint32_t>(r()) & ~0xffu) : ((r() % 4) ? (static_cast<uint32_t>(r()) | 1u) : 0u), w);
    w.vtThis[262] = reinterpret_cast<void *>(&Slot262);
    RetValue(&w.obj, 262, static_cast<uint32_t>(r()), w);
    RetValue(&w.obj, 2620, (r() % 2) ? 0u : static_cast<uint32_t>(r()), w);
    RetValue(&w.obj, 2621, static_cast<uint32_t>(r()), w);

    Use(iface.vt, 2, 0);
    Use(iface.vt, 132, 0);
    Use(iface.vt, 133, 0);
    RetObject(&iface, 132, &display, w);
    RetObject(&iface, 133, (r() % 3 == 0) ? nullptr : &display, w);
    Use(display.vt, 29, 0);
    Use(display.vt, 65, 1);
    Use(display.vt, 72, 2);
    display.vt[26] = reinterpret_cast<void *>(&Slot26);
    RetValue(&display, 29, (r() % 3) ? (static_cast<uint32_t>(r()) | 1u) : 0u, w);
    Use(manager.vt, 65, 1);
    Use(manager.vt, 72, 2);
}

// The display call 0x4526a0 (__cdecl: text, block*, interface object, flag, flag), reached when the UI manager exists
// and this answers the interface query: a recording stub takes its place in the mapped image for the test, so the
// original handler and ours both call it. The flags are dwords with only the low byte meaningful; the upper bytes are
// leftovers of the frame slots the compiler reused (the table pointer or the entry, and the param pointer): the
// stub records them against what the world makes of them.
void __cdecl Stub4526a0(const char *text, const uint16_t *block, void *object, uint32_t flagA, uint32_t flagB)
{
    Tick();
    gTrace.push_back(0x4526a0u);
    gTrace.push_back(static_cast<uint32_t>(std::strlen(text)));
    for (const char *p = text; *p; ++p) gTrace.push_back(static_cast<uint8_t>(*p));
    gTrace.insert(gTrace.end(), {block[0], block[1], block[2], block[3], Norm(reinterpret_cast<uint32_t>(object)), flagA & 0xff, flagB & 0xff});
    const uint32_t tableHigh = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gW->fk[3])) & ~0xffu;
    const uint32_t paramHigh = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW->param)) & ~0xffu;
    gTrace.push_back((flagA & ~0xffu) == tableHigh ? 0xA0001u : (flagA & ~0xffu));
    gTrace.push_back((flagB & ~0xffu) == paramHigh ? 0xB0001u : (flagB & ~0xffu));
}

// 0x4529b0 (__cdecl, one argument) stores the argument in a game global; redirected too so the call is observable.
void __cdecl Stub4529b0(uint32_t value)
{
    Tick();
    gTrace.push_back(0x4529b0u);
    gTrace.push_back(value);
}

struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(difftest::Context &ctx, uint32_t rva, void *stub) : Redirect(ctx.image.At<uint8_t>(rva), stub) {}
    // The same on our own code: the callee a replacement reaches directly (SetActionStringShown), so the stub serves both sides.
    Redirect(void *ours, void *stub) : site(static_cast<uint8_t *>(ours))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~Redirect() { std::memcpy(site, saved, 5); }
};

} // namespace

DIFF_TEST(PrimS_Prim32, 0x59d740)
{
    TextFixture fixture(ctx);
    Redirect display(ctx, 0x4526a0, reinterpret_cast<void *>(&Stub4526a0));
    Redirect shown(ctx, 0x4529b0, reinterpret_cast<void *>(&Stub4529b0));
    Redirect shownOurs(reinterpret_cast<void *>(&SetActionStringShown), reinterpret_cast<void *>(&Stub4529b0));
    Stats st = RunHandler(ctx, "Prim32", 0x59d740, 60000, Build32, [](PrimMidObject *o, void *f, const PrimParamView *p) { return Self(o)->Prim32(f, p); });
    if (st.throws < 3000 || st.byRet[1] < 8000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.byRet[1]);
}
