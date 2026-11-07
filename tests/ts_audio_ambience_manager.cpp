// Differential tests for cTSAudioAmbienceManager (lane KG, src/audio/cTSAudioAmbienceManager.h): the whole class in the
// standalone shape.
//
// AudioAmbienceManager_Run: a manager built by the original constructor (0x2274a2, game vtables 0xdaa870 / 0xdaa8bc /
// 0xdaa8f4 / 0xdaa908) or factory (0x22784f) against one built by ours (the Construct adapter / Create), each driven
// through its own vtables by the same random operations, then destroyed by the last Release (0x227c34 reaching the
// deleting destructor 0x22767e and the destructor 0x22755d).
// Operations: Init (0x227887, also through the service face's thunk 0x2278f8), Shutdown (0x2283ff, thunk 0x228448;
// UnloadAll 0x22864a), the thread proxies on and off the renderer's thread: LoadSound (0x2279f4; DoLoad 0x228bfe, AddSound
// 0x2287b5), IsSoundLoaded (0x227900; DoIsLoaded 0x228bb5, FindSound 0x22887c), UnloadSound (0x2286e6; DoUnload 0x2290d3),
// PlaySound (0x227ad8; DoPlay 0x228cc0), StopSound (0x228564; DoStop 0x229039), slots 10..16 (0x228224, 0x228310,
// 0x228066, 0x227f5d, 0x227e4b, 0x227d5c, 0x227c6d; their workers 0x228f77, 0x228fd8, 0x228f16, 0x228e3d with point counts
// 0, 1, 8, 9 and 10 around the 9-cell grid, 0x228dd2, 0x228d86, 0x228d3a), HandleEvent (0x2276c3) with the events the
// proxies sent or posted and with made-up ones of every id (handlers 0x2288de, 0x2288b0, 0x228b87, 0x228919, 0x228b51,
// 0x228ad5, 0x228b13, 0x2289d8, 0x228a64 with 0..9 points, 0x228a16, 0x22899a, 0x22895c, 0x228b7f with DoUnloadAll
// 0x22909b), a null event, an object the event query fails on and unknown ids, the external face (0x228450, 0x228633,
// 0x2281aa, 0x2281fa, 0x228186, 0x228155, 0x2279cf), QueryInterface (0x227bbe) / AddRef (0x22769d) / Release (0x227c34)
// on every face (the thunks 0x227c1c / 0x227c24 / 0x227c2c, 0x2276ab / 0x2276b3 / 0x2276bb, 0x227c55 / 0x227c5d /
// 0x227c65) and the service accessors.
//
// Every object the manager reaches is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject's
// cell 0x10c9378 and the cache 0x10f5194), the renderer, the sound manager (GetCachedServiceObjectC's cell 0x10cc648), the
// COM framework (the director's), the sound objects. The events are real cTSAudioEvents on both sides (operator new
// 0x210087 and the constructors 0x20ff24 / 0x20ff8d patched to ours, over a real page pool) and are logged by content when a
// fake receives one. Stood in on both sides: the thread id 0x1b9cad. Compared after every operation: the result, every
// field (the never-written bytes +0x22 / +0x23 too: both blocks start as 0xc5), the vtable words (the side's own), the map in
// order (key, names, sound, values, node colours), the framework cache, the event log and every fake's count.
//
// Left out: a slot 13 event (0xec487275) with more than 9 points reaching HandleEvent: the original's 9-point stack buffer
// overruns onto its return address (0x228a64: `sub esp, 0x70`, the buffer at esp+4 after four pushes, 108 bytes); and an
// event of that id missing a point key (GetVector3 leaves the stack buffer as it was, so the original passes stack garbage
// to the sound). Slot 13 itself is called with 10 points (posting them is fine).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "audio/cTSAudioAmbienceManager.h"
#include "audio/cTSAudioEvent.h"
#include "game/game.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "misc/UtilitiesC.h"
#include "rz/RZFixedAllocator.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <limits>
#include <new>
#include <random>
#include <vector>

namespace audioambmgr_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{4, "dd"}, {5, "dd"}, {6, "dd"}, {7, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Mgr = cTSAudioAmbienceManager;

constexpr uint32_t kTag = 0x7d000000;     // the manager under test
constexpr uint32_t kPageTag = 0x7c000000; // the event pool's pages
constexpr uint32_t kThread = 0x7777;

enum : unsigned { kService = 0, kComFw, kRenderer, kSoundMgr, kSound0, kSound1, kSound2, kSound3, kNotEvent };
constexpr unsigned kSounds = 4;

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Roll() { return 0x100u | (gRng() & 1); }

// ---- the world ----
struct World {
    bool renderer;   // the framework has the renderer service
    bool onThread;   // the renderer's slot 51 answers this thread
    bool classOk;    // the COM framework hands out a sound
    uint32_t next;   // the next sound it hands out
};
World gWorld;

// ---- the event pool (real pages) ----
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

// ---- events by content ----
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
void DumpAny(Blob &b, void *any)
{
    if (!any) {
        b.push_back(0xdead0001);
        return;
    }
    const uint32_t type = (*static_cast<uint32_t(__thiscall ***)(void *)>(any))[4](any);
    b.push_back(type);
    const uint32_t *w = static_cast<const uint32_t *>(any);
    if (type == nGZNumerics::AnyVector3::kType)
        for (uint32_t i = 0; i < 3; ++i) b.push_back(w[1 + i]);
    // The payload by the value's type: numbers are data, anything else (a string set, a wrapped clone) a pointer.
    else if (type == gzany::kTypeUint32 || type == gzany::kTypeSint32 || type == gzany::kTypeFloat32) b.push_back(w[1]);
    else b.push_back(svcfx::Norm(w[1]));
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
        DumpAny(b, value);
    }
}
cITSAudioEvent *AsEvent(uint32_t a) { return reinterpret_cast<cITSAudioEvent *>(uintptr_t(a)); }

// The events the fakes were handed (counted), for HandleEvent later.
std::deque<cITSAudioEvent *> gQueue;
void Keep(cITSAudioEvent *e)
{
    if (!e) return;
    e->AddRef();
    gQueue.push_back(e);
    if (gQueue.size() > 6) {
        gQueue.front()->Release();
        gQueue.pop_front();
    }
}
void DropQueue()
{
    for (cITSAudioEvent *e : gQueue) e->Release();
    gQueue.clear();
}

// ---- stand-ins and fake handlers ----
void __cdecl StandThread(uint32_t *out) { *out = kThread; }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] == 0x23cecba0 && a[1] == 0x106077d2) return gWorld.renderer ? HandOut(a[2], F(kRenderer)) : 0x500u;
    return 0x5100u;
}
uint32_t GetClassObject(Obj *, uint32_t *a)
{
    if (!gWorld.classOk) return 0x300u;
    return HandOut(a[2], F(kSound0 + gWorld.next++ % kSounds));
}
uint32_t RendererThread(Obj *, uint32_t *) { return gWorld.onThread ? kThread : kThread + 1; }
uint32_t RendererSend(Obj *, uint32_t *a)
{
    DumpEvent(svcfx::gEv, AsEvent(a[0]));
    Keep(AsEvent(a[0]));
    return Roll();
}
uint32_t ManagerPost(Obj *, uint32_t *a)
{
    DumpEvent(svcfx::gEv, AsEvent(a[0]));
    Keep(AsEvent(a[0]));
    return 0x1700u;
}
uint32_t ManagerRequest22(Obj *, uint32_t *a)
{
    DumpEvent(svcfx::gEv, AsEvent(a[3]));
    return 0x2200u;
}
uint32_t ManagerRequest34(Obj *, uint32_t *a)
{
    const auto *p = reinterpret_cast<const uint32_t *>(uintptr_t(a[1]));
    for (int i = 0; i < 3; ++i) Ev(p[i]);
    return 0x3400u;
}
uint32_t RollHandler(Obj *, uint32_t *) { return Roll(); }
// A sound's QueryInterface: value[0] says whether it answers.
uint32_t SoundQuery(Obj *self, uint32_t *a)
{
    if (!self->value[0]) return 0x100u;
    return HandOut(a[1], *self);
}
// A sound's load (slot 3): the key's four words; value[1] says whether it loads.
uint32_t SoundLoad(Obj *self, uint32_t *a)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    return self->value[1] ? 1u : 0x100u;
}
uint32_t NotEventQuery(Obj *, uint32_t *) { return 0x100u; }

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 3u);
    svcfx::ResetAll();
    DropQueue();
    gWorld.renderer = rng() % 6 != 0;
    gWorld.onThread = rng() % 2 != 0;
    gWorld.classOk = rng() % 6 != 0;
    gWorld.next = 0;

    F(kService).Set(5, "ddp", 0, &GetSystemService);
    F(kService).Set(3, "p", 0x703);
    F(kService).Set(4, "p", 0x704);
    F(kComFw).Set(3, "ddp", 0, &GetClassObject);
    Obj &r = F(kRenderer);
    r.Set(8, "p", 0, &RendererSend);
    r.Set(15, "p", 0x1500);
    r.Set(16, "p", 0x1600);
    r.Set(51, 0, 0, &RendererThread);
    Obj &m = F(kSoundMgr);
    m.Set(17, "p", 0, &ManagerPost);
    m.Set(22, "dddp", 0, &ManagerRequest22);
    m.Set(26, "dddd", 0x2600);
    m.Set(27, "d-", 0x2700);
    m.Set(34, "dp", 0, &ManagerRequest34);
    m.Set(36, "dd-", 0x3600);
    m.Set(41, "d", 0, &RollHandler);
    m.Set(44, "dd", 0x4400);
    m.Set(45, "ddd", 0x4500);
    m.Set(48, "pd", 0x4800);
    m.Set(49, "pd", 0x4900);
    for (unsigned i = 0; i < kSounds; ++i) {
        Obj &s = F(kSound0 + i);
        s.Set(0, "dp", 0, &SoundQuery);
        s.Set(3, "p", 0, &SoundLoad);
        s.Set(7, 0, 0x707);
        s.Set(8, 0, 0, &RollHandler);
        s.Set(10, 0, 0x710);
        s.Set(11, "d", 0x711);
        s.Set(13, "d", 0x713);
        s.Set(14, "d", 0x714);
        s.Set(28, "d", 0x728);
        s.Set(29, "dd", 0x729);
        s.Set(32, "ddd", 0x732);
        s.Set(33, "ddddd", 0x733);
        s.Set(35, 0, 0x735);
        s.Set(36, "pp", 0x736);
        s.value[0] = rng() % 5 != 0;
        s.value[1] = rng() % 5 != 0;
    }
    F(kNotEvent).Set(0, "dp", 0, &NotEventQuery);

    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(0x10f5194) = rng() % 2 ? &F(kService) : nullptr;
    *GameAddress<void *>(0x10cc648) = &F(kSoundMgr);
    svcfx::gFrameworkIndex = kComFw;
}

// ---- driving a manager ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory; // this seed builds through the factories (no 0xc5 fill)
void *gOurVT[4];
constexpr uint32_t kFaces[4] = {0, 4, 8, 0xc};
constexpr uint32_t kGameVT[4] = {0xdaa870, 0xdaa8bc, 0xdaa8f4, 0xdaa908};

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) return ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0x22784f)();
    void *memory = ::operator new(Mgr::kSize);
    std::memset(memory, 0xc5, Mgr::kSize);
    if (ours) cTSAudioAmbienceManagerAdapters::Construct(memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0x2274a2)(memory);
    return memory;
}

uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

void Dump(Blob &b, void *object, bool ours)
{
    for (int f = 0; f < 4; ++f)
        b.push_back(Word(object, kFaces[f]) == (ours ? P(gOurVT[f]) : P(GameAddress<void>(kGameVT[f]))) ? 0x7a000000u + f : 0xbad);
    b.push_back(Word(object, 0x10));
    b.push_back(svcfx::Norm(Word(object, 0x14)));
    b.push_back(Word(object, 0x18));
    b.push_back(Word(object, 0x1c));
    // +0x22 / +0x23 are never written (the constructor stores the word +0x20 with `mov word ptr [edi + 0x20], ax` at
    // 0x2274fa, and nothing else touches them): compared on the 0xc5 blocks, left out on the factory route's heap blocks.
    b.push_back(gFactory ? Word(object, 0x20) & 0xffff : Word(object, 0x20));
    const auto &map = *reinterpret_cast<const AmbientSoundMap *>(Face(object, 0x24));
    b.push_back(uint32_t(map.size()));
    for (const auto &[key, s] : map) {
        b.push_back(key);
        b.push_back(s.instance);
        b.push_back(s.high);
        b.push_back(svcfx::Norm(P(s.sound.Get())));
        b.push_back(Bits(s.value15));
        b.push_back(Bits(s.value16));
        // the node's colour byte (the value sits at +0x14 of the node: key +0x10, colour +0xc)
        b.push_back(*(reinterpret_cast<const uint8_t *>(&key) - 4));
    }
    b.push_back(svcfx::Norm(*GameAddress<uint32_t>(0x10f5194)));
}

// ---- operations ----
struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};
std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 22;
    // Mostly initialised first, so the proxies have a renderer.
    if (rng() % 5) ops.push_back({0, rng(), rng()});
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 16, rng(), rng()});
    return ops;
}

const uint32_t kInstances[] = {1, 2, 3, 0x80000000u, 0xffffffffu};
const float kInf = std::numeric_limits<float>::infinity();
const float kValues[] = {0.0f, 0.5f, -0.5f, 1.25f, 3.7f, -2.99f, 100.0f, 0.0999f, 2.5e9f, -3e9f, 5e9f, 1e20f,
                         std::numeric_limits<float>::quiet_NaN(), kInf, -kInf, 214748364.0f};
const uint32_t kCounts[] = {0, 1, 2, 8, 9, 10};
const uint32_t kIids[] = {1, Mgr::kIid, Mgr::kHandlerIid, 0x0c1e612a, 0xabb90bd5, 0x12345678};
const uint32_t kEventIds[] = {0xabbd0bca, 0x2bbd0bd1, 0xebbd0bd6, 0x8bbd0bdc, 0x8bbd0be1, 0xcbbd0be6, 0x6bbd0bee,
                              0x6bbd0c0b, 0xec487275, 0x4c4887ad, 0x8c5b5545, 0xac5b5550, 0x8bc2894d, 0x8bc2894e,
                              0x2bbd0bd0, 0};

uint32_t Instance(uint32_t a) { return kInstances[a % 5]; }
uint32_t High(uint32_t a) { return a % 3 ? a % 4 : 0x55aa0000u; }
float Value(uint32_t a) { return kValues[a % 16]; }

// A made-up event: an id of `a`, values for some of the keys (all of the points up to the count, never more than 9).
cITSAudioEvent *MakeEvent(uint32_t a, uint32_t c)
{
    cITSAudioEvent *e = new cTSAudioEvent(kEventIds[a % 16]);
    e->AddRef();
    if (c % 8) e->SetUint32(0x4c3e66e8, Instance(c >> 1));
    if (c & 8) e->SetUint32(0xedd1b848, High(c >> 4));
    if (c & 0x80) e->SetUint32(0x4c3e66eb, c >> 8);
    if (c % 5) e->SetUint32(0x6cc65bad, (c >> 9) % 3); // 2: a flag word above 1 (the handler passes 0 / 1)
    for (uint32_t key : {0x4c3e65f3u, 0x4c3e66eeu, 0xcc488735u, 0xcc48873au, 0xac5b5564u})
        if ((c >> 12) % 4) e->SetFloat(key, Value(c >> 14) + float(key % 7));
    const uint32_t count = (c >> 20) % 10;
    if ((c >> 24) % 4) {
        e->SetUint32(0xac48771a, count);
        for (uint32_t i = 0; i < count; ++i) {
            const float p[3] = {Value(c + i), float(i) - 2.5f, Value(c >> (i + 1))};
            e->SetVector3(0xec487263 + i, p);
        }
    }
    return e;
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const service = Face(object, 4);
    void *const handler = Face(object, 8);
    void *const external = Face(object, 0xc);
    const uint32_t a = op.arg, c = op.arg2;
    gWorld.onThread = (c >> 28) % 2 != 0;
    const uint32_t inst = Instance(a), high = High(a >> 4);
    switch (op.kind) {
    case 0: b.push_back((a % 3 ? Slot<uint32_t>(primary, 3) : Slot<uint32_t>(service, 8)) & 0xff); break;
    case 1: b.push_back((a % 3 ? Slot<uint32_t>(primary, 4) : Slot<uint32_t>(service, 9)) & 0xff); break;
    case 2: b.push_back(Slot<uint32_t>(primary, 5, c >> 16, inst, high) & 0xff); break;
    case 3: b.push_back(Slot<uint32_t>(primary, 6, inst, high) & 0xff); break;
    case 4: b.push_back(Slot<uint32_t>(primary, 7, inst, high) & 0xff); break;
    case 5: {
        const uint32_t flags[] = {0, 1, 0x100, 0xffffff01u};
        b.push_back(Slot<uint32_t>(primary, 8, inst, high, flags[c % 4]) & 0xff);
        break;
    }
    case 6: b.push_back(Slot<uint32_t>(primary, 9, inst, high) & 0xff); break;
    case 7: {
        const unsigned slots[] = {10, 11, 12, 15, 16};
        Slot<void>(primary, slots[c % 5], inst, high, Value(c >> 4));
        break;
    }
    case 8: {
        float points[10][3];
        for (int i = 0; i < 10; ++i)
            for (int k = 0; k < 3; ++k) points[i][k] = Value(c >> (i + k)) * (k ? 1.0f : -1.0f) + float(i);
        Slot<void>(primary, 13, inst, high, &points[0][0], kCounts[c % 6]);
        break;
    }
    case 9: Slot<void>(primary, 14, inst, high, Value(c), Value(c >> 5)); break;
    case 10: { // HandleEvent with an event a fake was handed
        if (gQueue.empty()) return;
        cITSAudioEvent *e = gQueue[c % gQueue.size()];
        if (e->GetEventId() == 0xec487275 && e->GetUint32(0xac48771a) > 9) return; // see the file comment
        b.push_back(Slot<uint32_t>(handler, 3, static_cast<cIGZUnknown *>(e)) & 0xff);
        break;
    }
    case 11: { // HandleEvent with a made-up event, a null one or a non-event
        if (c % 9 == 0) {
            b.push_back(Slot<uint32_t>(handler, 3, static_cast<cIGZUnknown *>(nullptr)) & 0xff);
        } else if (c % 9 == 1) {
            b.push_back(Slot<uint32_t>(handler, 3, reinterpret_cast<cIGZUnknown *>(&F(kNotEvent))) & 0xff);
        } else if (c % 9 == 2) { // a play event (0x228919) whose flag word is above 1: the handler passes 1
            cITSAudioEvent *e = new cTSAudioEvent(0x8bbd0bdc);
            e->AddRef();
            e->SetUint32(0x4c3e66e8, inst);
            e->SetUint32(0x6cc65bad, (c >> 4) % 2 ? 2u : 0x100u);
            b.push_back(Slot<uint32_t>(handler, 3, static_cast<cIGZUnknown *>(e)) & 0xff);
            e->Release();
        } else {
            cITSAudioEvent *e = MakeEvent(a, c);
            b.push_back(Slot<uint32_t>(handler, 3, static_cast<cIGZUnknown *>(e)) & 0xff);
            e->Release();
        }
        break;
    }
    case 12: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, service, handler, external};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, kIids[(a >> 4) % 6], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 13: // the service face's accessors
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        if (a % 3 == 0) Slot<void>(service, 4, c);
        Slot<void>(service, 7, uint32_t(a % 2 ? 0x101u : 0x200u));
        break;
    case 14: { // the external face
        switch (a % 7) {
        case 0: b.push_back(Slot<uint32_t>(external, 3, a, c, a >> 3, c >> 3, a >> 7, (c >> 8) % 3 ? 0x100u : 1u, c >> 1,
                                            Value(c >> 5)) & 0xff);
            break;
        case 1: b.push_back(Slot<uint32_t>(external, 4, a, c, a >> 2) & 0xff); break;
        case 2: b.push_back(Slot<uint32_t>(external, 5, a, c, Value(c), Value(a >> 3), Value(c >> 7)) & 0xff); break;
        case 3: b.push_back(Slot<uint32_t>(external, 6, a, c, Value(c >> 2)) & 0xff); break;
        case 4: b.push_back(Slot<uint32_t>(external, 7, a, c, Value(c >> 2)) & 0xff); break;
        case 5: b.push_back(Slot<uint32_t>(external, 8, a, c, Value(c >> 2), Value(a >> 9)) & 0xff); break;
        default: b.push_back(Slot<uint32_t>(external, 9, a, c) & 0xff); break;
        }
        break;
    }
    default: // the world changes
        gWorld.renderer = a % 5 != 0;
        gWorld.classOk = (a >> 4) % 5 != 0;
        F(kSound0 + (a >> 8) % kSounds).value[0] = (a >> 12) % 3 != 0;
        F(kSound0 + (a >> 14) % kSounds).value[1] = (a >> 16) % 3 != 0;
        break;
    }
}

} // namespace audioambmgr_test

using namespace audioambmgr_test;

DIFF_TEST(AudioAmbienceManager_Run, 0x2274a2)
{
    // Also reached (see the file comment): 0x22784f (factory), 0x22755d, 0x22767e, 0x227bbe, 0x22769d, 0x227c34,
    // 0x227887, 0x2278f8, 0x2283ff, 0x228448, 0x22864a, 0x2279f4, 0x228bfe, 0x2287b5, 0x227900, 0x228bb5, 0x22887c,
    // 0x2286e6, 0x2290d3, 0x227ad8, 0x228cc0, 0x228564, 0x229039, 0x228224, 0x228310, 0x228066, 0x227f5d, 0x227e4b,
    // 0x227d5c, 0x227c6d, 0x228f77, 0x228fd8, 0x228f16, 0x228e3d, 0x228dd2, 0x228d86, 0x228d3a, 0x2276c3, 0x2288de,
    // 0x2288b0, 0x228b87, 0x228919, 0x228b51, 0x228ad5, 0x228b13, 0x2289d8, 0x228a64, 0x228a16, 0x22899a, 0x22895c,
    // 0x228b7f, 0x22909b, 0x228450, 0x228633, 0x2281aa, 0x2281fa, 0x228186, 0x228155, 0x2279cf, the adjustor thunks
    // 0x227c1c, 0x227c24, 0x227c2c, 0x2276ab, 0x2276b3, 0x2276bb, 0x227c55, 0x227c5d, 0x227c65.
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {0x10c9378u, 0x10f5194u, 0x10cc648u, cTSAudioEvent::kPool, cTSAudioEvent::kPool + 4,
                          cTSAudioEvent::kPool + 8, cTSAudioEvent::kPoolLock})
        globals.SetWord(cell, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch eventNew(0x210087, reinterpret_cast<void *>(static_cast<void *(*)(size_t)>(&cTSAudioEvent::operator new)));
    shape::CodePatch eventCtor(0x20ff24, shape::CodeOf(&cTSAudioEventAdapters::Construct));
    shape::CodePatch eventCtorDefault(0x20ff8d, shape::CodeOf(&cTSAudioEventAdapters::ConstructDefault));
    shape::StandIn thread(0x1b9cad, &nGZLog::StoreCurrentThreadId, &StandThread);

    { // our vtables, from an object our constructor built
        alignas(8) uint8_t scratch[Mgr::kSize];
        Mgr *probe = cTSAudioAmbienceManagerAdapters::Construct(scratch, nullptr);
        for (int f = 0; f < 4; ++f) gOurVT[f] = reinterpret_cast<void *>(uintptr_t(Word(scratch, kFaces[f])));
        probe->~Mgr();
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
            svcfx::AddRange(object, Mgr::kSize, kTag);
            for (uint32_t &w : svcfx::gEv) // the factory's construction ran before the object's range was known
                if (w >= P(object) && w < P(object) + Mgr::kSize) w = kTag | (w - P(object));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            DropQueue();
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "AudioAmbienceManager_Run", seed)) {
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
