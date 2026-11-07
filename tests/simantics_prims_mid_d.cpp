// Opcodes 0x21, 0x30 and 0x7a (framework and notes: tests/simantics_prims_mid.h).
#include "simantics_prims_mid.h"

#include <windows.h>

// ============================================================================================================
// 0x58a110 (opcode 0x21)
// ============================================================================================================

namespace {

void BuildPrim21(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 31);
    Base(w, seed, r);
    SetParam(w, r);
    *reinterpret_cast<uint32_t *>(GameAddress<void>(0x10dbf50)) = 0;
    Fake &ctx = w.fk[0], &table = w.fk[3], &target = w.fk[4], &named = w.fk[5], &state = w.fk[7], &iface = w.fk[6], &flag = w.fk[8];
    Fake &sec = *reinterpret_cast<Fake *>(reinterpret_cast<uint8_t *>(&w.obj) + 4);

    PutWord(w, 8, static_cast<uint16_t>(Pick(r, {0, 1, 1, 2, 3, 4, 0x100, static_cast<uint32_t>(r())})));
    PutWord(w, 0xa, static_cast<uint16_t>(Pick(r, {0, 1, 2, 2, 3, 0xffff, static_cast<uint32_t>(r())})));

    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 6 == 0) ? nullptr : &target, w);
    w.vtSec[20] = nullptr;
    Use(w.vtSec, 20, 0);
    RetObject(&sec, 20, &named, w);
    Use(named.vt, 12, 0);
    RetValue(&named, 12, static_cast<uint32_t>(r()), w);

    // The interface comes from the target (mode 1) or from this (otherwise): the same scripted object or none.
    const bool found = r() % 6 != 0;
    target.vt[0] = reinterpret_cast<void *>(&Qi0);
    w.vtThis[0] = reinterpret_cast<void *>(&Qi0);
    RetObject(&target, 0, found ? &iface : nullptr, w);
    RetObject(&w.obj, 0, found ? &iface : nullptr, w);
    Use(iface.vt, 2, 0);
    Use(iface.vt, 8, 0);
    Use(iface.vt, 15, 1);
    RetObject(&iface, 8, &state, w);
    RetObject(&iface, 15, w.rank, w);
    Use(state.vt, 98, 0);
    RetValue(&state, 98, (r() % 5 == 0) ? static_cast<uint32_t>(r()) : 2u, w);
    (void)flag;
    Use(w.vtThis, 20, 2);

    // The rank table: ties and near-ties decide the order qsort leaves them in.
    for (float &f : w.rank) {
        switch (r() % 4) {
        case 0: f = static_cast<float>(r() % 4); break;
        case 1: f = static_cast<float>(r() % 100) * 0.005f; break;
        case 2: f = static_cast<float>(static_cast<int>(r() % 2001) - 1000) / 100.0f; break;
        default: f = RandomFloat(r); if (std::isnan(f) || std::isinf(f)) f = 1.0f; break;
        }
    }
}

} // namespace

DIFF_TEST(PrimMid_Prim21, 0x58a110)
{
    Stats st = RunHandler(ctx, "Prim21", 0x58a110, 40000, BuildPrim21,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->Prim21(f, p); });
    if (st.throws < 2000 || st.byRet[1] < 3000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.byRet[1]);
}

// ============================================================================================================
// 0x59d590 (opcode 0x30)
// ============================================================================================================

namespace {

// The director's slot 3: (class id, interface id, void **out): writes the scripted object, returns the scripted al.
uint32_t __fastcall Director3(void *self, void *, uint32_t a1, uint32_t a2, void **out)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 3u);
    gTrace.insert(gTrace.end(), {a1, a2, out ? 1u : 0u});
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(&gW->fk[6], 0x777)));
    return Scripted(self, 3);
}

void BuildPrim30(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 37);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &frame = w.fk[1], &table = w.fk[3], &object = w.fk[4], &director = w.fk[5], &iface = w.fk[6], &notified = w.fk[7];
    if (r() % 2) PutWord(w, 4, 0);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    Use(w.vtThis, 47, 0);
    RetValue(&w.obj, 47, static_cast<uint32_t>(r()), w);
    Use(w.vtThis, 63, 0);
    RetObject(&w.obj, 63, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 5 == 0) ? nullptr : &object, w);
    director.vt[3] = reinterpret_cast<void *>(&Director3);
    // What the director writes to *out (stored under a private key): the interface object. When it answers false it
    // writes nothing useful; the handler dereferences the result whenever the director says true, so true always
    // comes with an object.
    const bool present = r() % 6 != 0;
    RetValue(&director, 3, present ? (static_cast<uint32_t>(r()) | 1u) : (static_cast<uint32_t>(r()) & ~0xffu), w);
    RetObject(&iface, 0x777, present ? &iface : nullptr, w);
    Use(iface.vt, 5, 1);
    Use(iface.vt, 12, 2);
    Use(iface.vt, 2, 0);
    Use(notified.vt, 12, 1);
}

} // namespace

namespace {
void *__fastcall NoFramework(void *, void *)
{
    return nullptr;
}
} // namespace

DIFF_TEST(PrimMid_Prim30, 0x59d590)
{
    // The director singleton in the image: built by its original constructor, on the getter's fast path, with its
    // framework field (+0x30, what GetDirectorFrameworkObject returns) pointing to the world's director object.
    // 0x9c5b7e (the cached service the handler notifies) is served from its cache cell, 0x10f51b4.
    uint8_t *director = ctx.image.At<uint8_t>(0x10c9390);
    const uint32_t tlsIndex = *ctx.image.At<uint32_t>(0x10df814);
    int *tls = reinterpret_cast<int **>(__readfsdword(0x2c))[tlsIndex];
    const int savedEpoch = tls[1];
    const int savedGuard = *ctx.image.At<int>(0x10c93f4);
    uint8_t savedBytes[0x64];
    std::memcpy(savedBytes, director, sizeof(savedBytes));
    // The constructor asks the framework service (game global 0x10c9378) for the framework pointer, through slot 23.
    static void *fakeVTable[32];
    static struct { void *vtable; uint32_t pad; } fakeService;
    fakeVTable[23] = reinterpret_cast<void *>(&NoFramework);
    fakeService.vtable = fakeVTable;
    void *const savedService = *ctx.image.At<void *>(0x10c9378);
    *ctx.image.At<void *>(0x10c9378) = &fakeService;
    void *const savedCache = *ctx.image.At<void *>(0x10f51b4);
    std::memset(director, 0xCD, 0x64);
    ctx.Original<void *(__thiscall *)(void *)>(0x144897)(director);
    *ctx.image.At<int>(0x10c93f4) = 5;
    tls[1] = 0x7fffffff;
    gBeforeRun = [&](World &w) {
        *reinterpret_cast<void **>(director + 0x30) = &w.fk[5];
        *ctx.image.At<void *>(0x10f51b4) = &w.fk[7]; // non-null: a null cache would make 0x9c5b7e look the service up
    };

    auto build = [](World &w, uint32_t seed) {
        BuildPrim30(w, seed);
        w.param[0x1f] = static_cast<uint8_t>(seed * 7919u >> 3);
    };
    Stats st = RunHandler(ctx, "Prim30", 0x59d590, 30000, build,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->Prim30(f, p); });
    if (st.byRet[1] < 20000) ctx.Fail("coverage: %u returns", st.byRet[1]);

    gBeforeRun = nullptr;
    tls[1] = savedEpoch;
    *ctx.image.At<int>(0x10c93f4) = savedGuard;
    std::memcpy(director, savedBytes, sizeof(savedBytes));
    *ctx.image.At<void *>(0x10f51b4) = savedCache;
    *ctx.image.At<void *>(0x10c9378) = savedService;
}

// ============================================================================================================
// 0x59efa0 (opcode 0x7a, OnTimer)
// ============================================================================================================

namespace {

void BuildTimer(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 41);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[3], &timers = w.fk[4], &holder = w.fk[5];
    Fake &sec = *reinterpret_cast<Fake *>(reinterpret_cast<uint8_t *>(&w.obj) + 4);
    w.param[0x13] = static_cast<uint8_t>(Pick(r, {0, 0, 1, 1, 2, 3, 255}));
    w.param[9] = static_cast<uint8_t>(r());

    Use(ctx.vt, 24, 0);
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 24, &holder, w);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 8 == 0) ? nullptr : &timers, w);
    Use(frame.vt, 12, 0);
    Use(frame.vt, 26, 0);
    frame.vt[35] = reinterpret_cast<void *>(&Frame35);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    RetValue(&frame, 26, static_cast<uint32_t>(r()), w);
    Use(w.vtThis, 19, 1);
    Use(w.vtThis, 47, 0);
    Use(w.vtThis, 20, 2);
    w.vtSec[7] = reinterpret_cast<void *>(&Read7);
    (void)sec;
    RetValue(&w.obj, 19, static_cast<uint32_t>(Edge16(r)) | (r() & 0xffff0000u), w);
    RetValue(&w.obj, 47, static_cast<uint32_t>(r()), w);
    RetValue(reinterpret_cast<uint8_t *>(&w.obj) + 4, 7, static_cast<uint32_t>(Edge16(r)) | (r() & 0xffff0000u), w);
    timers.vt[301] = reinterpret_cast<void *>(&Timer301);
    Use(timers.vt, 302, 4);
    Use(timers.vt, 303, 1);
    for (int16_t &c : w.counters) c = Edge16(r);
}

} // namespace

DIFF_TEST(PrimMid_OnTimer, 0x59efa0)
{
    Stats st = RunHandler(ctx, "OnTimer", 0x59efa0, 40000, BuildTimer,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return o->OnTimer(f, p); });
    if (st.throws < 2000 || st.byRet[1] < 10000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.byRet[1]);
}
