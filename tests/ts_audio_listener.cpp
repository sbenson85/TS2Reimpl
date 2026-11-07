// Differential tests for cTSAudioListener (lane KH, src/audio/cTSAudioListener.h): the whole class in the standalone shape.
//
// AudioListener_Run: a listener built by the original constructor (0x210b4d, game vtables 0xda8454 / 0xda8490 / 0xda84c8 /
// 0xda84dc) or factory (0x211065) against one built by ours (the Construct adapter / Create), each driven through its own
// vtables by the same random operations, then destroyed by the last Release (0x211c9d reaching the +0xc face's deleting
// destructor thunk 0x210e0a, the deleting destructor 0x210e12 and the destructor 0x210d39) or by that thunk with flags 0.
// Operations: Init (0x2112ee, also through the service face's thunk 0x2113ca; LoadTuning 0x2118bd with and without a
// manager, a held set, a failing load, every property present or not), Shutdown (0x211d4d, thunk 0x211d9b), the getters
// (0x2111dc, 0x211132, 0x211183, 0x211284, 0x21121f, 0x2110a0, 0x2110e5, 0x211425, 0x2113e7), OnTick (0x211469: no
// renderer, no world, no active object, an object on and off each screen margin and at each margin's edge -1 / -0.5 / 0 /
// +0.5 / +1, the snap, the secondary's first placing),
// DoMessage (0x210e60 with RequestSnap 0x211cd9: the three handled ids and others), the cheat face (Slot4 0x210e9f with
// "lerp" / "slerp" answered with 0..3 arguments or none, GetName 0x211463, GetHelp 0x1d0a53, GetDescription 0x210e5a, the
// folded slots 3 / 7 / 8 / 11), QueryInterface (0x211865) / AddRef (0x210e34) / Release (0x211c9d) on every face (the
// thunks 0x2118a5 / 0x2118ad / 0x2118b5, 0x210e42 / 0x210e4a / 0x210e52, 0x211cc1 / 0x211cc9 / 0x211cd1) and the service
// accessors (the folded 0x2112e0, 0x211d30, 0x2112e7, 0x2113e0, 0x211d40).
//
// Every object the listener reaches is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject's
// cell 0x10c9378 and the cache 0x10f5194), the persist resource manager, the tuning set and its persist face, the renderer
// and its rolloff face, the app (0x10cc288) with the world and the active object, the state service (0x10cc458) with the
// view (slots 110 / 142, a hand-made vtable), the message server (0x10f519c), the cheat parser. The events are real
// cTSAudioEvents on both sides (operator new 0x210087 and the constructor 0x20ff24 patched to ours, over a real page
// pool), the message a real cRZMessage2Standard (0x1c033d, RZMemory), both logged by content. Stood in on both sides: the
// world update 0x211dce (writes +0x30, +0x54, +0xd8, +0xdc and the primary's target), the cell's world position 0x7dcc00,
// the stopwatch's Start / Elapsed / restart, the message-id walks, the log line (cRZStd's slot 2). Run for real: the
// smoothing step 0x98ba40, the number reader 0x1b1008, strtoul, the critical section. Compared after every operation: the
// result and the out buffers, every field (the never-written bytes on the 0xc5 route; the factory route fills them with
// 0xc5 after construction), the vtable words, the message ids, the lock's count, the event log and every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "audio/cTSAudioEvent.h"
#include "audio/cTSAudioListener.h"
#include "game/game.h"
#include "gz/cIGZPropertySet.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersA.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZFixedAllocator.h"
#include "rz/cRZStopwatch.h"
#include "world/WorldTileMath.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace audiolistener_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{8, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Lis = cTSAudioListener;

constexpr uint32_t kTag = 0x7d000000;     // the listener under test
constexpr uint32_t kPageTag = 0x7c000000; // the event pool's pages

enum : unsigned {
    kService = 0, kRenderer, kRolloff, kRM, kTuning, kPersist, kApp, kWorld, kActive, kPlaced, kLocated, kState, kStateT,
    kServer, kParser, kMessage,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
const float kInf = std::numeric_limits<float>::infinity();
const float kValues[] = {0.0f, 0.25f, -0.5f, 1.8f, 3.7f, -2.99f, 100.0f, 0.01f, 2.5e9f, -3e9f, 5e9f, 1e20f,
                         std::numeric_limits<float>::quiet_NaN(), kInf, -kInf, 10.0f};
float Value(uint32_t a) { return kValues[a % 16]; }

// ---- the world for a seed ----
struct World {
    bool renderer, rm, resOk, persistOk, world, active;
    uint32_t props;        // which tuning properties the set has (bit per name)
    float values[8];
    int32_t level;         // the placed object's slot 36 (a short)
    int32_t cell[2];       // the located object's slot 33 points here
    int32_t width, height; // the view's size
    float scale, offset;   // the view's screen position: x * scale + offset, z * scale - offset
    uint32_t edge;         // 1..4: the screen position put on the left / right / top / bottom margin's edge
    float delta;           // plus this (the comparisons' boundaries: -1 .. 1)
    uint32_t elapsed;      // the tick watch's Elapsed
    uint32_t argc;         // the parser's count
    bool argsNull;
    uint32_t message;      // the message's id
};
World gWorld;
uint8_t *gObject;

const char *const kPropNames[8] = {"listenerheightscale", "rolloff", "primarylistenersteps", "primarylistenerminchange",
                                   "secondarylistenersteps", "secondarylistenerminchange", "listenerminheight",
                                   "listenertocameraoffset"};
const char *gArgs[3];
const char *const kStepTexts[] = {"10", "0x1f", "-3", "abc", "4294967296", "017", "0", "1"};
const char *const kNumberTexts[] = {"0.5", "1e3", "-2.25", "x", "3", "0", "1e40", ".125"};

// ---- the event pool (real pages; tests/ts_audio_ambience_manager.cpp) ----
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPages = 256;
constexpr uint32_t kBlocks = 78;
uint8_t *gPages;
uint32_t gNextPage, gHighPage;
RZFixedPage *Page(uint32_t i) { return reinterpret_cast<RZFixedPage *>(gPages + kPageSize * i); }
RZFixedPage *ChainPage(uint32_t p)
{
    RZFixedPage *page = Page(p);
    uint8_t *first = nullptr;
    for (uint32_t k = kBlocks; k-- > 0;) {
        uint8_t *block = gPages + kPageSize * p + 16 + k * 0x34;
        *reinterpret_cast<uint8_t **>(block) = first;
        first = block;
    }
    page->freeList = first;
    page->used = 0;
    page->next = page->prev = page;
    return page;
}
RZFixedPage *__fastcall PoolNewPage(void *, void *)
{
    if (gNextPage >= kPages) std::abort();
    if (gNextPage > gHighPage) gHighPage = gNextPage;
    return ChainPage(gNextPage++);
}
void __fastcall PoolFreePage(void *, void *, RZFixedPage *) {}
void *gPoolVT[4] = {nullptr, nullptr, reinterpret_cast<void *>(&PoolNewPage), reinterpret_cast<void *>(&PoolFreePage)};
int32_t __fastcall LockLock(void *, void *) { return 1; }
int32_t __fastcall LockUnlock(void *, void *) { return 0; }
void *gLockVT[6] = {nullptr, reinterpret_cast<void *>(&LockLock), reinterpret_cast<void *>(&LockUnlock), nullptr, nullptr, nullptr};
void BuildPool()
{
    if (!gPages) gPages = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPageSize * kPages, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memset(gPages, 0x3b, kPageSize * (gHighPage + 1));
    gNextPage = 0;
    gHighPage = 0;
    uint32_t *pool = GameAddress<uint32_t>(cTSAudioEvent::kPool);
    pool[0] = P(gPoolVT);
    pool[1] = P(PoolNewPage(nullptr, nullptr));
    pool[2] = 0;
    *GameAddress<uint32_t>(cTSAudioEvent::kPoolLock) = P(gLockVT);
}

void DumpEvent(Blob &b, cITSAudioEvent *e)
{
    if (!e) {
        b.push_back(0xdead0002);
        return;
    }
    b.push_back(0xe7e70000);
    b.push_back(e->GetEventId());
    uint32_t key = 0;
    void *value = nullptr;
    for (bool ok = e->GetFirst(&key, &value); ok; ok = e->GetNext(&key, &value)) {
        b.push_back(key);
        const uint32_t *w = static_cast<const uint32_t *>(value);
        for (uint32_t i = 0; i < 4; ++i) b.push_back(i ? w[i] : (*static_cast<uint32_t(__thiscall ***)(void *)>(value))[4](value));
    }
}

// ---- stand-ins ----
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c0458);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
    return 0;
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c081e);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
}
void __fastcall StandStart(cRZStopwatch *self, void *)
{
    Ev(0x1c982b);
    Ev(svcfx::Norm(P(self)));
    self->mStart = 1;
}
uint32_t __fastcall StandElapsed(const cRZStopwatch *self, void *)
{
    Ev(0x1c9256);
    Ev(svcfx::Norm(P(self)));
    return gWorld.elapsed;
}
void __fastcall StandRestart(cRZStopwatch *self, void *)
{
    Ev(0x1c95ec);
    Ev(svcfx::Norm(P(self)));
    self->mAccumulated = 7;
}
// The world update: some of +0x30, +0x54, +0xd8 / +0xdc and the primary's target.
void __fastcall StandUpdateWorld(uint8_t *self, void *)
{
    Ev(0x211dce);
    Ev(svcfx::Norm(P(self)));
    const uint32_t r = gRng();
    float *const v30 = reinterpret_cast<float *>(self + 0x30), *const v54 = reinterpret_cast<float *>(self + 0x54);
    float *const target = reinterpret_cast<float *>(self + 0x7c);
    for (int i = 0; i < 3; ++i) {
        if (r & 1) v30[i] = Value(r >> (i + 1)) + float(i);
        if (r & 2) v54[i] = Value(r >> (i + 3)) - float(i);
        if (r & 4) target[i] = float(int32_t(r >> (8 + 4 * i)) % 64 - 20) * 0.75f;
    }
    if (r & 8) *reinterpret_cast<uint32_t *>(self + 0xd8) = r >> 20;
    if (r & 16) *reinterpret_cast<uint32_t *>(self + 0xdc) = r >> 12;
    if (r & 32) self[0xf0] = 0; // the real update clears the dirty flag once it has the view's frame (0x211ece / 0x21253b)
}
// The cell's world position (cdecl: out, cell, level).
void *__cdecl StandCellToWorld(float *out, const int32_t *cell, int32_t level)
{
    Ev(0x7dcc00);
    Ev(svcfx::Norm(P(cell)));
    Ev(uint32_t(level));
    out[0] = float(cell[0]) * 16.0f + 0.5f;
    out[1] = float(level) * 3.5f;
    out[2] = float(cell[1]) * 16.0f - 0.25f;
    return out;
}
// The log line: cRZStd's slot 2 (cdecl: the object, channel, level, format, arguments: words; %f a double).
void __cdecl LogLine(cRZStd *self, uint32_t channel, uint32_t level, const char *format, ...)
{
    Ev(0x10600);
    Ev(self->mFirst - P(GameAddress<void>(0)));
    Ev(self->mSecond);
    Ev(channel);
    Ev(level);
    Ev(P(format) - P(GameAddress<void>(0)));
    va_list args;
    va_start(args, format);
    for (const char *f = format; *f; ++f) {
        if (*f != '%' || !f[1] || f[1] == '%') continue;
        const char *c = f + 1;
        while (*c && !std::strchr("diufxs", *c)) ++c;
        if (*c == 'f') {
            const double d = va_arg(args, double);
            uint32_t w[2];
            std::memcpy(w, &d, 8);
            Ev(w[0]);
            Ev(w[1]);
        } else {
            Ev(va_arg(args, uint32_t));
        }
    }
    va_end(args);
}
struct VtablePatch {
    void **slot;
    void *saved;
    VtablePatch(uint32_t vtable, unsigned index, void *stand) : slot(GameAddress<void *>(vtable + index * 4))
    {
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        saved = *slot;
        *slot = stand;
    }
    ~VtablePatch() { *slot = saved; }
    VtablePatch(const VtablePatch &) = delete;
    VtablePatch &operator=(const VtablePatch &) = delete;
};

// ---- the view (slots past the fakes' 80) ----
struct View {
    void **vt;
};
void *gViewVT[150];
View gView{gViewVT};
uint32_t __fastcall ViewSize(View *, void *, int32_t *out)
{
    Ev(0x76110);
    Ev(svcfx::Norm(P(out)));
    out[0] = gWorld.width;
    out[1] = gWorld.height;
    out[2] = 0x1234; // the third word of the frame slot (whatever the real view writes: 12 bytes at most)
    return P(out);
}
uint32_t __fastcall ViewProject(View *, void *, float *out, const float *pos, uint32_t flag)
{
    Ev(0x76142);
    Ev(svcfx::Norm(P(out)));
    for (int i = 0; i < 3; ++i) Ev(Bits(pos[i]));
    Ev(flag);
    out[0] = pos[0] * gWorld.scale + gWorld.offset;
    out[1] = pos[2] * gWorld.scale - gWorld.offset;
    out[2] = 0.5f;
    if (gWorld.edge) { // the margin edges as 0x211469 computes them (signed percent of the size, uint32 product)
        const auto percent = [](uint32_t cell, int32_t extent) {
            return int32_t(*GameAddress<uint32_t>(cell) * uint32_t(extent)) / 100;
        };
        const int32_t w = gWorld.width, h = gWorld.height;
        const int32_t edges[4] = {percent(0x10cc414, w), w - percent(0x10cc41c, w), percent(0x10cc418, h),
                                  h - percent(0x10cc420, h)};
        out[gWorld.edge > 2 ? 1 : 0] = float(edges[gWorld.edge - 1]) + gWorld.delta;
    }
    return 0x14200;
}

// ---- fake handlers ----
uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] == 0x23cecba0 && a[1] == 0x106077d2) return gWorld.renderer ? HandOut(a[2], F(kRenderer)) : 0x500u;
    if (a[0] == 0x056b906e && a[1] == 0x656b8efc) return gWorld.rm ? HandOut(a[2], F(kRM)) : 0x500u;
    return 0x5100u;
}
uint32_t RendererQuery(Obj *, uint32_t *a) { return HandOut(a[1], F(kRolloff)); } // the original uses the face unchecked
uint32_t RendererEvent(Obj *, uint32_t *a)
{
    DumpEvent(svcfx::gEv, reinterpret_cast<cITSAudioEvent *>(uintptr_t(a[0])));
    return 0x1200u;
}
uint32_t GetResource(Obj *, uint32_t *a)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    return gWorld.resOk ? HandOut(a[2], F(kTuning)) : 0x100u;
}
uint32_t ForgetResource(Obj *, uint32_t *a)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    return 0x1000u;
}
uint32_t TuningQuery(Obj *, uint32_t *a) { return gWorld.persistOk ? HandOut(a[1], F(kPersist)) : 0x100u; }
uint32_t PersistKey(Obj *, uint32_t *a)
{
    auto *key = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    key[0] = 0xebfee33f;
    key[1] = 0x4c1940c5;
    key[2] = 0x1111;
    key[3] = 0x2222;
    return a[0];
}
uint32_t TuningValue(Obj *, uint32_t *a)
{
    const auto *name = reinterpret_cast<const GZServiceString *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 8; ++i) {
        if (*name != kPropNames[i]) continue;
        Ev(0x9a0000 + i);
        void *const any = reinterpret_cast<void *>(uintptr_t(a[1]));
        Ev((*static_cast<uint32_t(__thiscall ***)(void *)>(any))[4](any)); // its type id (AnyFloat32 on both sides)
        Ev(reinterpret_cast<const uint32_t *>(any)[1]);                    // the value it starts with
        if (!(gWorld.props >> i & 1)) return 0x100u;
        reinterpret_cast<float *>(uintptr_t(a[1]))[1] = gWorld.values[i];
        return 1;
    }
    Ev(0x9affff);
    return 0;
}
uint32_t WorldOf(Obj *, uint32_t *) { return gWorld.world ? P(&F(kWorld)) : 0; }
uint32_t ActiveOf(Obj *, uint32_t *) { return gWorld.active ? P(&F(kActive)) : 0; }
uint32_t PartOf(Obj *self, uint32_t *) { return P(&F(self->value[0]++ % 2 ? kLocated : kPlaced)); }
uint32_t LevelOf(Obj *, uint32_t *) { return 0xabcd0000u | (uint32_t(gWorld.level) & 0xffff); }
uint32_t CellOf(Obj *, uint32_t *) { return P(gWorld.cell); }
uint32_t ViewOf(Obj *, uint32_t *) { return P(&gView); }
uint32_t Post(Obj *, uint32_t *a)
{
    svcfx::LogMessage(a[0]);
    return 0x400u;
}
uint32_t ParserArgs(Obj *, uint32_t *a)
{
    Ev(0x9b0000);
    for (const char *s = reinterpret_cast<const char *>(uintptr_t(a[0])); *s; ++s) Ev(uint8_t(*s));
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gWorld.argc;
    return gWorld.argsNull ? 0 : P(gArgs);
}
uint32_t MessageId(Obj *, uint32_t *) { return gWorld.message; }

void SetWorld(uint32_t r)
{
    gWorld.renderer = r % 6 != 0;
    gWorld.rm = (r >> 3) % 6 != 0;
    gWorld.resOk = (r >> 6) % 5 != 0;
    gWorld.persistOk = (r >> 9) % 4 != 0;
    gWorld.world = (r >> 11) % 5 != 0;
    gWorld.active = (r >> 14) % 4 != 0;
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 3u);
    svcfx::ResetAll();
    SetWorld(rng());
    gWorld.props = rng() & 0xff;
    for (float &v : gWorld.values) v = Value(rng());
    if (rng() % 2) gWorld.values[2] = float(rng() % 40), gWorld.values[4] = float(rng() % 40); // usable step counts
    gWorld.level = int32_t(rng() % 7) - 3;
    gWorld.cell[0] = int32_t(rng() % 80);
    gWorld.cell[1] = int32_t(rng() % 80);
    gWorld.width = 800;
    gWorld.height = 600;
    gWorld.scale = 1.0f;
    gWorld.offset = 0.0f;
    gWorld.edge = 0;
    gWorld.delta = 0.0f;
    gWorld.elapsed = 33;
    gWorld.argc = 2;
    gWorld.argsNull = false;
    gWorld.message = 0xb3;

    F(kService).Set(5, "ddp", 0, &GetSystemService);
    F(kService).Set(3, "p", 0x703);
    F(kService).Set(4, "p", 0x704);
    F(kService).Set(9, "p", 0x709);
    F(kService).Set(10, "p", 0x710);
    F(kRenderer).Set(0, "dp", 0, &RendererQuery);
    F(kRenderer).Set(12, "p", 0, &RendererEvent);
    F(kRolloff).Set(10, "d", 0x1010);
    F(kRM).Set(3, "pdp--", 0, &GetResource);
    F(kRM).Set(10, "p", 0, &ForgetResource);
    F(kTuning).Set(0, "dp", 0, &TuningQuery);
    F(kTuning).Set(9, "pp", 0, &TuningValue);
    F(kPersist).Set(3, "p", 0, &PersistKey);
    F(kApp).Set(26, 0, 0, &WorldOf);
    F(kWorld).Set(39, 0, 0, &ActiveOf);
    F(kActive).Set(8, 0, 0, &PartOf);
    for (unsigned i : {kPlaced, kLocated}) {
        F(i).Set(36, 0, 0, &LevelOf);
        F(i).Set(33, 0, 0, &CellOf);
    }
    F(kState).Set(33, 0, P(&F(kStateT)));
    F(kStateT).Set(18, 0, 0, &ViewOf);
    F(kServer).Set(4, "p-", 0, &Post);
    F(kParser).Set(2, "ppdd", 0, &ParserArgs);
    F(kMessage).Set(4, 0, 0, &MessageId);

    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(0x10f5194) = rng() % 2 ? &F(kService) : nullptr;
    *GameAddress<void *>(svcfx::kAppCell) = &F(kApp);
    *GameAddress<void *>(0x10cc458) = &F(kState);
    *GameAddress<void *>(svcfx::kMessageServerCell) = &F(kServer);
    // the margins (percent): small ones, zero, and ones whose products wrap
    const uint32_t margins[] = {0, 5, 10, 25, 50, 99, 0x7fffffffu, 0x80000001u};
    for (uint32_t i = 0; i < 4; ++i) *GameAddress<uint32_t>(0x10cc414 + 4 * i) = margins[rng() % 8];
    *GameAddress<uint32_t>(0x10cc428) = rng();
    *GameAddress<uint32_t>(0x10cc42c) = rng();
}

// ---- driving a listener ----
template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory; // this seed builds through the factories (the never-written bytes filled afterwards)
void *gOurVT[4];
constexpr uint32_t kFaces[4] = {0, 4, 8, 0xc};
constexpr uint32_t kGameVT[4] = {0xda8454, 0xda8490, 0xda84c8, 0xda84dc};
// The bytes the constructor never writes: [from, to).
constexpr uint32_t kUnwritten[][2] = {{0x30, 0x3c}, {0x54, 0x64}, {0x89, 0x90}, {0xb5, 0xb8}, {0xba, 0xc0}, {0xc4, 0xc8},
                                      {0xe0, 0xec}, {0xf1, 0xf4}, {0x11d, 0x120}};

uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

void *Build(difftest::Context &ctx, bool ours)
{
    void *object;
    if (gFactory) {
        object = ours ? static_cast<void *>(Lis::Create()) : ctx.Original<void *(__cdecl *)()>(0x211065)();
        for (const auto &r : kUnwritten) std::memset(Face(object, r[0]), 0xc5, r[1] - r[0]);
    } else {
        object = ::operator new(Lis::kSize);
        std::memset(object, 0xc5, Lis::kSize);
        if (ours) cTSAudioListenerAdapters::Construct(object, nullptr);
        else ctx.Original<void *(__thiscall *)(void *)>(0x210b4d)(object);
    }
    return object;
}

// The object's words as Dump logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] =
    "----d-dp ----ddd- -d-d-ddd dddddddd dddddddd dddddddd ddd-d-dd dddddddd "
    "---dpd-d";
void Dump(Blob &b, void *object, bool ours)
{

    for (int f = 0; f < 4; ++f)
        b.push_back(Word(object, kFaces[f]) == (ours ? P(gOurVT[f]) : P(GameAddress<void>(kGameVT[f]))) ? 0x7a000000u + f : 0xbad);
    for (uint32_t off = 0x10; off < Lis::kSize; off += 4) {
        if (off == 0x20) { // the lock: the game's vtable, the count in its block
            b.push_back(Word(object, off) == P(GameAddress<void>(cRZCriticalSection::kVTable)) ? 0x10c4u : 0xbad);
            b.push_back(reinterpret_cast<const cRZCriticalSection *>(Face(object, off))->mBlock->lockCount);
            off += 4;
            continue;
        }
        if (off == 0x28) { // the message ids
            const auto &ids = *reinterpret_cast<const std::set<uint32_t> *>(Face(object, off));
            b.push_back(uint32_t(ids.size()));
            for (uint32_t id : ids) b.push_back(id);
            off += 4;
            continue;
        }
        svcfx::Field(b, Word(object, off), kObjectKinds, off);
    }
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};
std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 24;
    if (rng() % 5) ops.push_back({0, rng(), rng()}); // mostly initialised first
    for (unsigned i = 0; i < n; ++i) {
        const unsigned k = rng() % 14;
        ops.push_back({k >= 10 ? 3u : k, rng(), rng()}); // ticks most often
    }
    return ops;
}

const uint32_t kIids[] = {1, Lis::kIid, Lis::kMessageTargetIid, Lis::kServiceId, 0x6b8d3d09, 0x12345678};
const uint32_t kMessages[] = {0xb3, 0xb6, 0xece20be8, 0xb2, 0x0b8d81d5, 0};
const uint32_t kElapsed[] = {0, 1, 16, 33, 50, 1000, 0x7fffffffu, 0x80000000u};

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const service = Face(object, 4);
    void *const target = Face(object, 8);
    void *const cheat = Face(object, 0xc);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: b.push_back((a % 3 ? Slot<uint32_t>(primary, 3) : Slot<uint32_t>(service, 8)) & 0xff); break;
    case 1: b.push_back((a % 3 ? Slot<uint32_t>(primary, 4) : Slot<uint32_t>(service, 9)) & 0xff); break;
    case 2: { // the getters, into 0x5a-filled buffers
        float out[2][4];
        std::memset(out, 0x5a, sizeof out);
        const unsigned slot = 5 + a % 9;
        switch (slot) {
        case 5: Slot<void>(primary, 5, out[0]); break;
        case 7: case 9: case 11: b.push_back(Slot<uint32_t>(primary, slot, out[0], out[1]) & 0xff); break;
        case 12: case 13: b.push_back(Slot<uint32_t>(primary, slot)); break;
        default: b.push_back(Slot<uint32_t>(primary, slot, out[0]) & 0xff); break;
        }
        for (const auto &o : out)
            for (float f : o) b.push_back(Bits(f));
        break;
    }
    case 3: // a tick
        gWorld.elapsed = kElapsed[c % 8];
        b.push_back(Slot<uint32_t>(service, 10, a) & 0xff);
        break;
    case 4:
        gWorld.message = kMessages[c % 6];
        b.push_back(Slot<uint32_t>(target, 3, &F(kMessage)) & 0xff);
        break;
    case 5: // the cheat
        gWorld.argc = c % 5 == 4 ? 2 : c % 4;
        gWorld.argsNull = (c >> 3) % 7 == 0;
        gArgs[0] = kStepTexts[(c >> 6) % 8];
        gArgs[1] = kNumberTexts[(c >> 9) % 8];
        gArgs[2] = "7";
        Slot<void>(cheat, 4, &F(kParser));
        break;
    case 6: { // the cheat face's other slots
        switch (a % 7) {
        case 0: Slot<void>(cheat, 3, c, a); break;
        case 1: b.push_back(Slot<uint32_t>(cheat, 5) - P(GameAddress<void>(0))); break;
        case 2: b.push_back(Slot<uint32_t>(cheat, 6, c) - P(GameAddress<void>(0))); break;
        case 3: b.push_back(Slot<uint32_t>(cheat, 7) & 0xff); break;
        case 4: b.push_back(Slot<uint32_t>(cheat, 8)); break;
        case 5: b.push_back(Slot<uint32_t>(cheat, 10) - P(GameAddress<void>(0))); break;
        default: b.push_back(Slot<uint32_t>(cheat, 11)); break;
        }
        break;
    }
    case 7: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, service, target, cheat};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, kIids[(a >> 4) % 6], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 8: // the service face's accessors
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        if (a % 3 == 0) Slot<void>(service, 4, c);
        Slot<void>(service, 7, uint32_t(a % 2 ? 0x101u : 0x200u));
        break;
    default: { // the world changes
        SetWorld(a);
        gWorld.props = c & 0xff;
        gWorld.values[(c >> 8) % 8] = Value(c >> 11);
        if ((c >> 15) % 2) gWorld.values[2 + 2 * ((c >> 16) % 2)] = float((c >> 17) % 30);
        gWorld.level = (c >> 20) % 4 ? int32_t((c >> 22) % 9) - 4 : ((c >> 22) % 2 ? 0x7fff : -0x8000);
        gWorld.cell[0] = int32_t((c >> 3) % 90) - 5;
        gWorld.cell[1] = int32_t((c >> 9) % 90) - 5;
        const int32_t sizes[] = {800, 600, 0, 1, -40, 100000000};
        gWorld.width = sizes[(a >> 16) % 6];
        gWorld.height = sizes[(a >> 19) % 6];
        const float scales[] = {1.0f, 0.5f, -1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 8.0f};
        gWorld.scale = scales[(a >> 22) % 6];
        gWorld.offset = float(int32_t((a >> 25) % 64) - 16) * 10.0f;
        const uint32_t e = gRng();
        const float deltas[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
        gWorld.edge = e % 7 < 4 ? e % 7 + 1 : 0;
        gWorld.delta = deltas[(e >> 3) % 5];
        break;
    }
    }
}

} // namespace audiolistener_test

using namespace audiolistener_test;

DIFF_TEST(AudioListener_Run, 0x210b4d)
{
    // Also reached (see the file comment): 0x211065 (factory), 0x210d39, 0x210e12, 0x210e0a, 0x211865, 0x210e34, 0x211c9d,
    // 0x2112ee, 0x2113ca, 0x211d4d, 0x211d9b, 0x2118bd, 0x2111dc, 0x211132, 0x211183, 0x211284, 0x21121f, 0x2110a0,
    // 0x2110e5, 0x211425, 0x2113e7, 0x211469, 0x210e60, 0x211cd9, 0x210e9f, 0x211463, 0x1d0a53, 0x210e5a, the folded
    // accessors 0x2112e0, 0x211d30, 0x2112e7, 0x2113e0, 0x211d40, the adjustor thunks 0x2118a5, 0x2118ad, 0x2118b5,
    // 0x210e42, 0x210e4a, 0x210e52, 0x211cc1, 0x211cc9, 0x211cd1.
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {0x10c9378u, 0x10f5194u, svcfx::kAppCell, 0x10cc458u, svcfx::kMessageServerCell, 0x10cc414u,
                          0x10cc418u, 0x10cc41cu, 0x10cc420u, 0x10cc428u, 0x10cc42cu, cTSAudioEvent::kPool,
                          cTSAudioEvent::kPool + 4, cTSAudioEvent::kPool + 8, cTSAudioEvent::kPoolLock})
        globals.SetWord(cell, 0);
    globals.SetWord(0xff5f10, 0x10f5);
    // the constructor's vectors (static data the harness leaves zero)
    const float vectors[9] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.5f, -2.25f, 3.0f};
    for (uint32_t i = 0; i < 3; ++i) {
        globals.SetWord(0xff394c + 4 * i, Bits(vectors[i]));
        globals.SetWord(0xff3958 + 4 * i, Bits(vectors[3 + i]));
        globals.SetWord(0x10cc430 + 4 * i, Bits(vectors[6 + i]));
    }
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch eventNew(0x210087, reinterpret_cast<void *>(static_cast<void *(*)(size_t)>(&cTSAudioEvent::operator new)));
    shape::CodePatch eventCtor(0x20ff24, shape::CodeOf(&cTSAudioEventAdapters::Construct));
    shape::CodePatch updateWorld(0x211dce, shape::CodeOf(&StandUpdateWorld));
    shape::StandIn cellToWorld(0x7dcc00, &worldmath::FixedToPosition, &StandCellToWorld);
    shape::CodePatch restart(0x1c95ec, shape::CodeOf(&StandRestart));
    shape::StandIn walk5(0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5);
    shape::StandIn walk6(0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6);
    shape::StandIn start(0x1c982b, &cRZStopwatch::Start, &StandStart);
    shape::StandIn elapsed(0x1c9256, &cRZStopwatch::Elapsed, &StandElapsed);
    VtablePatch logLine(cRZStd::kVTable, 2, reinterpret_cast<void *>(&LogLine));
    gViewVT[110] = reinterpret_cast<void *>(&ViewSize);
    gViewVT[142] = reinterpret_cast<void *>(&ViewProject);

    { // our vtables, from an object our constructor built
        alignas(8) uint8_t scratch[Lis::kSize];
        Lis *probe = cTSAudioListenerAdapters::Construct(scratch, nullptr);
        for (int f = 0; f < 4; ++f) gOurVT[f] = reinterpret_cast<void *>(uintptr_t(Word(scratch, kFaces[f])));
        probe->~Lis();
    }

    for (uint32_t seed = 1; seed <= 2500; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            BuildPool();
            svcfx::AddRange(gPages, kPageSize * kPages, kPageTag);
            void *object = Build(ctx, side == 1);
            gObject = static_cast<uint8_t *>(object);
            svcfx::AddRange(object, Lis::kSize, kTag);
            for (uint32_t &w : svcfx::gEv) // the factory's construction ran before the object's range was known
                if (w >= P(object) && w < P(object) + Lis::kSize) w = kTag | (w - P(object));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else if (seed % 5 == 0) {
                    // The cheat face's deleting destructor (thunk 0x210e0a) with flags 0: destroyed, not freed.
                    b.push_back(svcfx::Norm(Slot<uint32_t>(Face(object, 0xc), 9, 0u)));
                    ::operator delete(object);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "AudioListener_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                return;
            }
        }
    }
}
