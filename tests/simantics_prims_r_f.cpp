// Differential test for the SimAntics primitive handler of opcode 0x10 (0x58ae50, PrimRObject::Prim10).
// Framework and notes: tests/simantics_prims_mid.h, tests/simantics_prims_r.h.
//
// Everything is a recording fake except: the framework messages the handler posts (real cRZMessage2Standard objects,
// built by the allocator and the game's vtable; the message server is a fake that logs them), the generator
// (a real cRZRandom with the same state on both sides) and the director singleton (set up as in the Prim30 test).
// The route check 0x7e8c60 is redirected to a logging stand-in.
#include "game_allocator.h"
#include "simantics_prims_r.h"

#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZRandom.h"

#include <cstdlib>

namespace {

constexpr int kStaticItems = 4;
uint32_t gItems[kStaticItems][3];       // waypoint items: x, y, level word (+pad)
uint16_t gItemHeading[kStaticItems];
uint32_t gDestPosition[2];              // what the destination's slot 33 points at
uint32_t gVector[2];                    // the destination list's vector: begin, end
int32_t gVbTable[3];                    // the tracer's virtual-base table
cRZRandom gRng;                         // the generator the owner hands out
uint32_t gRngStart[3];

uint8_t gRouteAllowedCalls = 0;

// 0x7e8c60 (cdecl, seven stack arguments): the route check.
uint8_t __cdecl StubRouteAllowed(void *walker, const uint32_t *position, uint32_t *flags, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gTrace.push_back(0x7e8c60u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(walker)));
    gTrace.insert(gTrace.end(), {position[0], position[1], position[2], *flags, a, b, c, d});
    ++gRouteAllowedCalls;
    return Mix(gSeed, 0x7e8c60, gRouteAllowedCalls) % 3 == 0 ? 1 : 0;
}

void LogRequest(const uint8_t *r)
{
    uint32_t v;
    gTrace.push_back(r[0]);
    for (int off : {4, 8, 0xc, 0x10}) {
        std::memcpy(&v, r + off, 4);
        gTrace.push_back(v);
    }
    for (int off = 0x14; off <= 0x19; ++off) gTrace.push_back(r[off]);
    uint16_t w;
    for (int off : {0x1a, 0x1c, 0x1e}) {
        std::memcpy(&w, r + off, 2);
        gTrace.push_back(w);
    }
    gTrace.push_back(r[0x20]);
}

// The walker's slot 186 (route request, out position, out flags).
uint32_t __fastcall Walker186(void *self, void *, const uint8_t *request, uint32_t *position, uint32_t *flags)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(186u << 8 | 3u);
    LogRequest(request);
    gTrace.insert(gTrace.end(), {position[0], position[1], position[2]});
    // Always write the outputs (the original leaves the flags as stale stack when the call writes nothing).
    for (uint32_t i = 0; i < 3; ++i) position[i] = Mix(gSeed, 186, i);
    *flags = Mix(gSeed, 186, 3);
    return Scripted(self, 186);
}

uint32_t __fastcall Walker171(void *self, void *, const uint32_t *position, uint32_t a, uint32_t b)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(171u << 8 | 3u);
    gTrace.insert(gTrace.end(), {position[0], position[1], position[2], a, b});
    return 0;
}

uint32_t __fastcall Walker170(void *self, void *, const uint32_t *flags, uint32_t a)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(170u << 8 | 2u);
    gTrace.insert(gTrace.end(), {*flags, a});
    return 0;
}

// The walker's slot 12 (iid, iid, out): the tracer object.
uint32_t __fastcall Walker12(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(12u << 8 | 3u);
    gTrace.insert(gTrace.end(), {a, b, out ? 1u : 0u});
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 0x600)));
    return Scripted(self, 12);
}

// Slot 0 of the walker: per interface id.
uint32_t __fastcall WalkerQi(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(out ? 1u : 0u);
    const int key = iid == 0x5a6216f6 ? 0x500 : 0x510;
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, key)));
    return Scripted(self, key + 1);
}

// The frame's slots 31 and 30 (one argument): a number (or -1).
template <int S> uint32_t __fastcall FrameNum(void *self, void *, uint32_t arg)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(S) << 8 | 1u);
    gTrace.push_back(arg);
    const uint32_t h = Mix(gSeed, static_cast<uint32_t>(S), arg);
    return (h % 9 == 0) ? 0xffffu : (h % 4) | (h & 0xffff0000u);
}

uint32_t __fastcall Dest33(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(33u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gDestPosition));
}

uint32_t __fastcall Holder52(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(52u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVector));
}

uint32_t __fastcall Owner55(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(55u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gRng));
}

// The director's slot 3: (id, id, out) -> the steps object.
uint32_t __fastcall Director3c(void *self, void *, uint32_t a1, uint32_t a2, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 3u);
    gTrace.insert(gTrace.end(), {a1, a2, out ? 1u : 0u});
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 0x500)));
    return Scripted(self, 3);
}

// The steps object: slot 3 (seven arguments), 62 (item), 9 (heading of an item).
uint32_t __fastcall Steps3(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 7u);
    gTrace.insert(gTrace.end(), {Norm(a1), Norm(a2), Norm(a3), Norm(a4), a5, a6, a7});
    return 0;
}

uint32_t __fastcall Steps62(void *self, void *, uint32_t index)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(62u << 8 | 1u);
    gTrace.push_back(index);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gItems[index % kStaticItems]));
}

uint32_t __fastcall Steps9(void *self, void *, const uint32_t *item)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(9u << 8 | 1u);
    const uint32_t index = static_cast<uint32_t>(item - &gItems[0][0]) / 3;
    gTrace.push_back(index);
    return gItemHeading[index % kStaticItems] | (Mix(gSeed, 9, index) & 0xffff0000u);
}

// The message server's slot 3: the message's own slots record (below), so only the call is logged.
uint32_t __fastcall Server3(void *self, void *, const uint8_t *message)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 1u);
    gTrace.push_back(message ? 1u : 0u);
    return 0;
}

// The message class's slots (AddRef 1, Release 2, SetId 5, slot 12) record instead of running; the allocator the
// original's malloc wrapper reaches is replaced by plain memory.
uint32_t __fastcall MsgAddRef(void *, void *)
{
    gTrace.push_back(0x4d01u);
    return 1;
}

uint32_t __fastcall MsgRelease(void *, void *)
{
    gTrace.push_back(0x4d02u);
    return 0;
}

void __fastcall MsgSetId(void *, void *, uint32_t id)
{
    gTrace.push_back(0x4d05u);
    gTrace.push_back(id);
}

void __fastcall MsgSlot12(void *, void *, uint32_t value)
{
    gTrace.push_back(0x4d0cu);
    gTrace.push_back(Norm(value));
}

void *__cdecl TestMalloc(uint32_t bytes)
{
    void *p = std::malloc(bytes);
    if (p) std::memset(p, 0xcd, bytes);
    return p;
}

void Build10(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 606);
    Base(w, seed, r);
    SetParam(w, r);
    ClearQueues();
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[2], &walker = w.fk[3], &probe = w.fk[4], &dest = w.fk[5], &holder = w.fk[6],
         &tracer = w.fk[7], &tracerRelease = w.fk[8], &tracerSub = w.fk[9], &iface = w.fk[10], &steps = w.fk[11], &director = w.fk[12],
         &owner = w.fk[13];
    w.obj.mOwner = &owner;

    // Parameter bytes.
    w.param[4] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, static_cast<uint32_t>(r())}));
    w.param[5] = static_cast<uint8_t>(r() % 6);
    if (r() % 2) w.param[6] &= static_cast<uint8_t>(r());
    PutWord(w, 0xe, static_cast<uint16_t>(Pick(r, {0, 1, 2, 3, 0xffff, 0x8000, static_cast<uint32_t>(r() % 6)})));

    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Q1<29>);
    QObj(29, (r() % 10 == 0) ? nullptr : &walker, w);
    QObj(29, (r() % 10 == 0) ? nullptr : &dest, w);
    Use(table.vt, 39, 0);
    RetValue(&table, 39, (r() % 3 == 0) ? static_cast<uint32_t>(r()) | 1 : 0u, w);
    Use(table.vt, 40, 1);

    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, Wide16(r), w);
    Use(frame.vt, 27, 0);
    RetValue(&frame, 27, Pick(r, {0, 1, 2, 3, 4, 5, 6, 0x105}), w);
    Use(frame.vt, 26, 0);
    RetValue(&frame, 26, Pick(r, {0, 1, 2, 3, 4, 5, 6, 0x105}), w);
    frame.vt[31] = reinterpret_cast<void *>(&FrameNum<31>);
    frame.vt[30] = reinterpret_cast<void *>(&FrameNum<30>);
    w.vtSec[7] = reinterpret_cast<void *>(&ReadQ);
    gReadQ = {Wide16(r), Wide16(r)};

    // The walker.
    Fake &wk = walker;
    Use(wk.vt, 25, 1);
    RetValue(&wk, 25, (r() % 8 == 0) ? 1u : (r() & ~0xffu), w);
    Use(wk.vt, 64, 0);
    RetObject(&wk, 64, &probe, w);
    Use(probe.vt, 3, 1);
    RetValue(&probe, 3, (r() % 3 == 0) ? 0x13u : Pick(r, {0, 0x12, 0x14, static_cast<uint32_t>(r())}), w);
    wk.vt[12] = reinterpret_cast<void *>(&Walker12);
    // (the answer says yes only with a tracer: the original uses the object without checking it)
    const bool tracerPresent = r() % 3 != 0;
    RetObject(&wk, 0x600, tracerPresent ? &tracer : nullptr, w);
    RetValue(&wk, 12, (!tracerPresent || r() % 4 == 0) ? Pick(r, {0, 0x100}) : Pick(r, {1, 0xff}), w);
    Use(wk.vt, 188, 0);
    RetValue(&wk, 188, (r() % 2) ? Pick(r, {1, 0xff}) : Pick(r, {0, 0x100}), w);
    Use(wk.vt, 104, 0);
    RetValue(&wk, 104, (r() % 2) ? Pick(r, {1, 0xff}) : Pick(r, {0, 0x100}), w);
    Use(wk.vt, 175, 2);
    wk.vt[0] = reinterpret_cast<void *>(&WalkerQi);
    // (the answers say yes only with an object: the original uses it without checking)
    const bool traceIface = r() % 5 != 0, ownIface = r() % 5 != 0;
    RetObject(&wk, 0x500, traceIface ? &iface : nullptr, w);
    RetValue(&wk, 0x501, (!traceIface || r() % 4 == 0) ? 0u : 1u, w);
    RetObject(&wk, 0x510, ownIface ? &iface : nullptr, w);
    RetValue(&wk, 0x511, (!ownIface || r() % 4 == 0) ? 0u : 1u, w);
    wk.vt[186] = reinterpret_cast<void *>(&Walker186);
    RetValue(&wk, 186, (r() % 3 == 0) ? Pick(r, {0, 0x100}) : Pick(r, {1, 0xff}), w);
    Use(wk.vt, 13, 1);
    RetValue(&wk, 13, Pick(r, {0, 1, 2, 3, 0x10003, static_cast<uint32_t>(r())}), w);
    Use(wk.vt, 215, 0);
    wk.vt[171] = reinterpret_cast<void *>(&Walker171);
    wk.vt[170] = reinterpret_cast<void *>(&Walker170);
    Use(wk.vt, 216, 0);
    Use(wk.vt, 98, 0);
    RetValue(&wk, 98, (r() % 3 == 0) ? static_cast<uint32_t>(r()) : 2u, w);
    Use(wk.vt, 43, 0);
    RetValue(&wk, 43, static_cast<uint32_t>(r()), w);
    Use(wk.vt, 47, 0);
    RetValue(&wk, 47, Wide16(r), w);

    // The destination (also `this` when no destination is given).
    for (void **vt : {w.vtThis, dest.vt}) {
        vt[33] = reinterpret_cast<void *>(&Dest33);
        Use(vt, 36, 0);
        Use(vt, 13, 1);
        Use(vt, 47, 0);
        Use(vt, 43, 0);
        Use(vt, 63, 0);
        Use(vt, 250, 0);
        vt[251] = reinterpret_cast<void *>(&Q1<251>);
    }
    RetValue(&dest, 36, Wide16(r), w);
    RetValue(&dest, 13, Wide16(r), w);
    RetValue(&dest, 47, Wide16(r), w);
    RetValue(&dest, 43, static_cast<uint32_t>(r()), w);
    RetObject(&dest, 63, &holder, w);
    RetValue(&dest, 250, Pick(r, {0, 1, 2, 3, 0x80000000u, static_cast<uint32_t>(r() % 6)}), w);
    RetValue(&w.obj, 36, Wide16(r), w);
    RetValue(&w.obj, 13, Wide16(r), w);
    RetValue(&w.obj, 47, Wide16(r), w);
    RetValue(&w.obj, 43, static_cast<uint32_t>(r()), w);
    RetObject(&w.obj, 63, &holder, w);
    RetValue(&w.obj, 250, Pick(r, {0, 1, 2, 3, 0x80000000u, static_cast<uint32_t>(r() % 6)}), w);
    QObj(251, (r() % 5 == 0) ? nullptr : &iface, w);
    holder.vt[52] = reinterpret_cast<void *>(&Holder52);
    Use(holder.vt, 106, 1);
    gDestPosition[0] = r();
    gDestPosition[1] = r();
    // The destination list: pointers to world fakes (or none).
    const uint32_t listCount = r() % 4;
    for (uint32_t i = 0; i < listCount; ++i) {
        void *p = (r() % 4 == 0) ? nullptr : static_cast<void *>(&iface);
        std::memcpy(w.bytes + i * 4, &p, 4);
    }
    w.bytes[0x3c] = static_cast<uint8_t>(listCount);

    // The registry slot (this + 0x1c4): the holder doubles as the registry.
    {
        const size_t vtOffset = reinterpret_cast<uint8_t *>(w.vtThis) - reinterpret_cast<uint8_t *>(&w.obj);
        const size_t index = (0x1c4 - vtOffset) / 4;
        for (int s : {13, 33, 36, 43, 47, 63, 250, 251}) {
            if (index == static_cast<size_t>(s)) w.canary = 0;
        }
        *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(&w.obj) + 0x1c4) = &holder;
    }

    // The tracer: a virtual-base object (vbptr at +4, entry 1 -> the released subobject, entry 2 -> the traced one).
    tracer.pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVbTable));
    Use(tracerRelease.vt, 2, 0);
    Use(tracerSub.vt, 86, 2);

    // The interface the walker offers.
    Use(iface.vt, 2, 0);
    Use(iface.vt, 15, 0);
    RetValue(&iface, 15, (r() % 2) ? Pick(r, {1, 0xff}) : Pick(r, {0, 0x100}), w);
    Use(iface.vt, 17, 0);

    // The owner and the generator.
    owner.vt[55] = reinterpret_cast<void *>(&Owner55);
    gRngStart[0] = r();
    gRngStart[1] = r();
    gRngStart[2] = 0;

    // The director's steps.
    director.vt[3] = reinterpret_cast<void *>(&Director3c);
    RetObject(&director, 0x500, &steps, w);
    steps.vt[3] = reinterpret_cast<void *>(&Steps3);
    Use(steps.vt, 4, 0);
    Use(steps.vt, 8, 0);
    Use(steps.vt, 61, 0);
    RetValue(&steps, 61, Pick(r, {0, 1, 2, 3, 4, 5, 0x80000000u}), w);
    steps.vt[62] = reinterpret_cast<void *>(&Steps62);
    steps.vt[9] = reinterpret_cast<void *>(&Steps9);
    Use(steps.vt, 2, 0);
    for (int i = 0; i < kStaticItems; ++i) {
        gItems[i][0] = r();
        gItems[i][1] = r();
        gItems[i][2] = static_cast<uint32_t>(Edge16(r)) & 0xffff;
        gItemHeading[i] = static_cast<uint16_t>(Pick(r, {0, 1, 2, 3, 0xffff, 0xffff, 5, 7, 8, 9, 0x8000, static_cast<uint32_t>(r())}));
    }

    // The message server (fk9 doubles as the traced subobject: different slots).
    tracerSub.vt[3] = reinterpret_cast<void *>(&Server3);
    gRouteAllowedCalls = 0;
}

} // namespace

DIFF_TEST(PrimR_Prim10, 0x58ae50)
{
    CodePatch p1(ctx, 0x7e8c60, reinterpret_cast<void *>(&StubRouteAllowed));
    CodePatch p2(ctx, 0x1be3b0, reinterpret_cast<void *>(&TestMalloc));
    auto *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    RZMemory::testGlobalAllocator = allocator;
    void **msgVtable = ctx.image.At<void *>(cRZMessage2Standard::kVTable);
    void *savedSlots[4] = {msgVtable[1], msgVtable[2], msgVtable[5], msgVtable[12]};
    {
        DWORD old;
        VirtualProtect(msgVtable, 64, PAGE_READWRITE, &old);
    }
    msgVtable[1] = reinterpret_cast<void *>(&MsgAddRef);
    msgVtable[2] = reinterpret_cast<void *>(&MsgRelease);
    msgVtable[5] = reinterpret_cast<void *>(&MsgSetId);
    msgVtable[12] = reinterpret_cast<void *>(&MsgSlot12);

    // The director singleton in the image (as in the Prim30 test), the cached message server and the vb table.
    uint8_t *director = ctx.image.At<uint8_t>(0x10c9390);
    const uint32_t tlsIndex = *ctx.image.At<uint32_t>(0x10df814);
    int *tls = reinterpret_cast<int **>(__readfsdword(0x2c))[tlsIndex];
    const int savedEpoch = tls[1];
    const int savedGuard = *ctx.image.At<int>(0x10c93f4);
    uint8_t savedBytes[0x64];
    std::memcpy(savedBytes, director, sizeof(savedBytes));
    static void *fakeVTable[32];
    static struct {
        void *vtable;
        uint32_t pad;
    } fakeService;
    fakeVTable[23] = reinterpret_cast<void *>(+[](void *, void *) -> void * { return nullptr; });
    fakeService.vtable = fakeVTable;
    void *const savedService = *ctx.image.At<void *>(0x10c9378);
    *ctx.image.At<void *>(0x10c9378) = &fakeService;
    std::memset(director, 0xCD, 0x64);
    ctx.Original<void *(__thiscall *)(void *)>(0x144897)(director);
    *ctx.image.At<int>(0x10c93f4) = 5;
    tls[1] = 0x7fffffff;
    void **server = ctx.image.At<void *>(0x10f519c);
    void *const savedServer = *server;

    gBeforeRun = [&](World &w) {
        ResetQueues();
        *reinterpret_cast<void **>(director + 0x30) = &w.fk[12];
        *server = &w.fk[9];
        gRouteAllowedCalls = 0;
        gRng.mIntegerSeed = gRngStart[0];
        gRng.mRealSeed = static_cast<int32_t>(gRngStart[1]);
        gRng.mUnknown = 0;
        // The virtual-base table: entry 1 releases fk8 (obj + 4 + 4 = fk[8]), entry 2 traces fk9 (obj + 4 + 12 = fk[9]).
        gVbTable[0] = 0;
        gVbTable[1] = static_cast<int32_t>(reinterpret_cast<uint8_t *>(&w.fk[8]) - reinterpret_cast<uint8_t *>(&w.fk[7]) - 4);
        gVbTable[2] = static_cast<int32_t>(reinterpret_cast<uint8_t *>(&w.fk[9]) - reinterpret_cast<uint8_t *>(&w.fk[7]) - 4);
        // The destination list's vector.
        gVector[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.bytes));
        gVector[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.bytes + w.bytes[0x3c] * 4));
    };
    Stats st = RunHandler(ctx, "Prim10", 0x58ae50, 80000, Build10,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) {
                              return reinterpret_cast<PrimRObject *>(o)->Prim10(f, p);
                          });
    gBeforeRun = nullptr;
    tls[1] = savedEpoch;
    *ctx.image.At<int>(0x10c93f4) = savedGuard;
    std::memcpy(director, savedBytes, sizeof(savedBytes));
    *ctx.image.At<void *>(0x10c9378) = savedService;
    *server = savedServer;
    msgVtable[1] = savedSlots[0];
    msgVtable[2] = savedSlots[1];
    msgVtable[5] = savedSlots[2];
    msgVtable[12] = savedSlots[3];
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
    if (st.throws < 500 || st.byRet[0] < 3000 || st.byRet[1] < 1000)
        ctx.Fail("coverage: %u throws, returns 0/1: %u/%u", st.throws, st.byRet[0], st.byRet[1]);
}
