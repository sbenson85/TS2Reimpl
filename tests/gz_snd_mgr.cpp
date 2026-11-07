// Differential test for cGZSndMgr (lane KJ, src/gz/cGZSndMgr.h): the whole class in the standalone shape.
//
// SndMgr_Run: a manager built by the original constructor (0xb256f5, game vtables 0xe81114 / 0xe8114c / 0xe81168 / 0xe811ec)
// or factory (0xb22333) against one built by ours (the Construct adapter / Create), each driven through its own vtables by
// the same random operations, then destroyed by the last Release (0xb25f0c: Shutdown, then the service's Release reaching
// the +4 face's deleting destructor thunk 0xb258d8, the deleting destructor 0xb25906 and the destructor 0xb2582c).
// Operations: Init (0xb25c00) and Shutdown (0xb260d0) through the service face, every cIGZSndMgr slot (OpenThroughResources
// 0xb25980, the folded flags accessors 0x20daf0 / 0x149d00, GetAlias 0xb262e2, SetAlias 0xb2606d, the words 0xb2604f /
// 0xb2595e / 0xb2605e / 0xb2596f, RegisterObject 0xb25cce, UnregisterObject 0xb26322, UnregisterObjectByPointer 0xb2637c,
// AddCreator 0xb25d66, RemoveCreator 0xb25f43, CreateObjectByName 0xb25ad1, CreateObject 0xb25b22, Play 0xb2617f,
// PlayWithFlags 0xb26192, PlayAlias 0xb26257, PlayAliasWithFlags 0xb2626a, AddPlaying 0xb25928, CreateObject2 0xb259a3,
// AddCreator2 0xb25e77, RemoveCreator2 0xb25f95, RemoveCreator2ByPointer 0xb25fe1, QueryCreator2 0xb25a73, RegisterObject2
// 0xb25dee, UnregisterObject2 0xb263bf, UnregisterObject2ByPointer 0xb26410), Update (0xb26474), QueryInterface (0xb25c7f)
// / AddRef / Release on every face (the thunks 0xb25cb6 / 0xb25cbe / 0xb25cc6, 0xb25f2b / 0xb25f33 / 0xb25f3b) and the
// service accessors.
//
// Every object the manager reaches is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject's cell
// 0x10c9378), the renderer (the probe 0x9c5b7e stood in for on both sides), the resource manager, a name string and six
// items that answer QueryInterface by a mask, create (slot 3), start (slot 6) and poll (slot 7). Their QueryInterface,
// AddRef, Release and slot 3 also log the manager's five busy counts and its lock's count, so what runs under the lock or a
// busy count is compared. Run for real: the critical section, the CRCs (0x9cbbdd / 0x9cbc3e). The game's operator new /
// delete (0x824632 / 0x824624) are ours, so nodes either side allocates may be freed by the other's code. Compared after
// every operation: the result and out word, every field (the never-written bytes on the 0xc5 route), the vtable words, the
// containers by content (the hash maps' lists and buckets as list indices, mask, size), the lock count, the event log and
// every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZSndMgr.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace sndmgr_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{3, "d"}, {4, "dd"}, {5, "dd"}, {6, "dd"}, {7, "dd"}, {8, "dd"}, {9, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Mgr = cGZSndMgr;

constexpr uint32_t kTag = 0x7d000000; // the manager under test

enum : unsigned { kService = 0, kRenderer, kRes, kName, kItem0, kItems = 6 };

constexpr uint32_t kSoundIid = 0x206077d2, kCreatorIid = 0x3af9866f, kCreator2Iid = 0x9b75709e, kOtherIid = 0x11111111;
constexpr uint32_t kIids[4] = {kSoundIid, kCreatorIid, kCreator2Iid, kOtherIid};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

struct World {
    bool renderer;  // the probe answers
    uint32_t ready; // the renderer's slot 38
    bool resources; // the framework has the resource manager
};
World gWorld;
uint8_t *gObject; // the manager under test

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

// The manager's busy counts and its lock's count (the lock's block +0x18), or a marker once the lock is gone.
void LogState()
{
    if (!gObject) return;
    for (uint32_t off : {0x54u, 0x84u, 0x90u, 0x9cu, 0xa8u}) Ev(Word(gObject, off));
    const uint32_t lock = Word(gObject, 0x78);
    if (!lock) {
        Ev(0xdead0001);
        return;
    }
    const uint32_t block = *reinterpret_cast<const uint32_t *>(uintptr_t(lock) + 4);
    Ev(*reinterpret_cast<const uint32_t *>(uintptr_t(block) + 0x18));
}

uint32_t ItemAddRef(Obj *self, uint32_t *)
{
    LogState();
    return uint32_t(++self->refs);
}
uint32_t ItemRelease(Obj *self, uint32_t *)
{
    LogState();
    return uint32_t(--self->refs);
}
// QueryInterface by the item's mask (value[0], bit per kIids entry): true writes the item and counts it.
uint32_t ItemQuery(Obj *self, uint32_t *a)
{
    LogState();
    for (unsigned i = 0; i < 4; ++i) {
        if (a[0] == kIids[i] && (self->value[0] >> i & 1)) {
            ++self->refs;
            *reinterpret_cast<void **>(uintptr_t(a[1])) = self;
            return 0x101;
        }
    }
    return 0x100;
}
// Slot 3 as a creator (id low, id high, a, b, iid, out): answers for some ids with another item (counted).
uint32_t ItemCreate(Obj *self, uint32_t *a)
{
    LogState();
    const uint32_t pick = a[0] + a[1] + self->value[1];
    if (pick % 3 == 0) return 0x200;
    Obj &made = F(kItem0 + pick % kItems);
    ++made.refs;
    *reinterpret_cast<void **>(uintptr_t(a[5])) = &made;
    return 0x201;
}
uint32_t ItemPoll(Obj *, uint32_t *) { return 0x300u | uint32_t(Draw(3) == 0); }
uint32_t ItemStart(Obj *, uint32_t *) { return 0x400u | Draw(2); }
uint32_t RendererReady(Obj *, uint32_t *) { return gWorld.ready; }
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] != 0x056b906e || a[1] != 0x656b8efc || !gWorld.resources) return 0;
    ++F(kRes).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kRes);
    return 1;
}
const char *const kNames[] = {"", "sound", "Sound_Two", "music/loop", "UI_click", "x"};
uint32_t NameText(Obj *self, uint32_t *) { return P(kNames[self->value[0] % 6]); }
uint32_t ResourcesOpen(Obj *, uint32_t *) { return 0x500u | Draw(2); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void *__cdecl StandProbe() { return gWorld.renderer ? &F(kRenderer) : nullptr; }

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 3);
    svcfx::ResetAll();
    gWorld.renderer = rng() % 5 != 0;
    gWorld.ready = rng() % 5 ? 1 + rng() % 3 : 0;
    gWorld.resources = rng() % 4 != 0;
    F(kService).Set(5, "--p", 0, &GetSystemService);
    F(kService).Data(5, 0x3); // the two ids (then the out pointer)
    F(kRenderer).Set(30, "pd", 0x3030);
    F(kRenderer).Set(38, 0, 0, &RendererReady);
    F(kRes).Set(3, 5, 0, &ResourcesOpen);
    F(kRes).Data(3, 0x1f); // OpenThroughResources passes its three full-width words on: data (svcfx::Obj::Data)
    F(kName).Set(5, 0, 0, &NameText);
    for (unsigned i = 0; i < kItems; ++i) {
        Obj &item = F(kItem0 + i);
        item.Set(0, "dp", 0, &ItemQuery);
        item.Set(1, 0, 0, &ItemAddRef);
        item.Set(2, 0, 0, &ItemRelease);
        item.Set(3, "-----p", 0, &ItemCreate);
        item.Data(3, 0x1f); // id low (0x10000010, 0xff000010 among them), id high, a, b, iid (then the out pointer)
        item.Set(6, 3, 0, &ItemStart);
        item.Data(6, 0x7); // the flags (full-width: SetDefaultFlags, PlayWithFlags), 0, 1
        item.Set(7, 0, 0, &ItemPoll);
        item.value[0] = rng() % 16;
        item.value[1] = rng() % 7;
    }
    *GameAddress<void *>(0x10c9378) = &F(kService);
}

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory; // this seed builds through the factories (no 0xc5 fill)

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) return ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0xb22333)();
    void *memory = ::operator new(Mgr::kSize);
    std::memset(memory, 0xc5, Mgr::kSize);
    if (ours) cGZSndMgrAdapters::Construct(memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0xb256f5)(memory);
    return memory;
}

// A hash map's words, its list (the given node words of each node, `refAt` normalised), the buckets as list indices.
void DumpHash(Blob &b, const uint8_t *at, std::initializer_list<unsigned> words, unsigned refAt)
{
    b.push_back(Word(at, 0));    // max load factor
    b.push_back(Word(at, 8));    // size
    b.push_back(Word(at, 0x18)); // mask
    b.push_back(Word(at, 0x1c)); // max index
    std::vector<uint32_t> order;
    const uint32_t head = Word(at, 4);
    for (uint32_t n = *reinterpret_cast<const uint32_t *>(uintptr_t(head)); n != head;
         n = *reinterpret_cast<const uint32_t *>(uintptr_t(n))) {
        order.push_back(n);
        const auto *w = reinterpret_cast<const uint32_t *>(uintptr_t(n));
        for (unsigned i : words) b.push_back(i == refAt ? svcfx::Norm(w[i]) : w[i]);
    }
    const auto *first = reinterpret_cast<const uint32_t *>(uintptr_t(Word(at, 0xc)));
    const auto *last = reinterpret_cast<const uint32_t *>(uintptr_t(Word(at, 0x10)));
    b.push_back(uint32_t(last - first));
    for (const uint32_t *p = first; p != last; ++p) {
        uint32_t index = 0xffffffff;
        for (uint32_t i = 0; i < order.size(); ++i)
            if (order[i] == *p) index = i;
        if (*p != head && index == 0xffffffff) index = 0xeeeeeeee;
        b.push_back(index);
    }
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    const auto &mgr = *static_cast<const Mgr *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[4][2] = {{0, 0xe81114}, {4, 0xe8114c}, {0x1c, 0xe81168}, {0x20, 0xe811ec}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(m, off));
    b.push_back(gFactory ? m[0x18] : Word(m, 0x18)); // the running flag; its padding is the allocator's from a factory
    b.push_back(gFactory ? m[0x24] : Word(m, 0x24)); // the initialised flag, the same
    b.push_back(svcfx::Norm(Word(m, 0x28)));
    b.push_back(svcfx::Norm(Word(m, 0x2c)));
    b.push_back(Word(m, 0x30));
    DumpHash(b, m + 0x34, {2, 3, 4}, 4); // objects: id low, id high, the object
    b.push_back(Word(m, 0x54));
    DumpHash(b, m + 0x58, {2, 4, 5}, 99); // aliases: name, id low, id high
    b.push_back(Word(m, 0x78) != 0);
    for (const Mgr::RefList *list : {&mgr.mCreators, &mgr.mPlaying}) {
        b.push_back(uint32_t(list->size()));
        for (const AudioRef &r : *list) b.push_back(svcfx::Norm(P(r.Get())));
        for (auto it = list->rbegin(); it != list->rend(); ++it) b.push_back(svcfx::Norm(P(it->Get())));
    }
    b.push_back(Word(m, 0x84));
    b.push_back(Word(m, 0x90));
    b.push_back(uint32_t(mgr.mCreators2.size()));
    for (const auto &e : mgr.mCreators2) b.push_back(e.first), b.push_back(svcfx::Norm(P(e.second.Get())));
    b.push_back(Word(m, 0x9c));
    b.push_back(uint32_t(mgr.mObjects2.size()));
    for (const auto &e : mgr.mObjects2)
        b.push_back(uint32_t(e.first)), b.push_back(uint32_t(e.first >> 32)), b.push_back(svcfx::Norm(P(e.second.Get())));
    b.push_back(Word(m, 0xa8));
    b.push_back(uint32_t(mgr.mMapAC.size()));
    b.push_back(Word(m, 0xb4));
    b.push_back(Word(m, 0xb8));
    LogState(); // the lock's count (and the busy counts again)
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 7);
    std::vector<Op> ops;
    const unsigned n = 10 + rng() % 50;
    if (rng() % 6) ops.push_back({0, rng(), 0}); // Init
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 30, rng(), rng()});
    return ops;
}

// The id pool: low words colliding in the hash's buckets (the low word is the hash) and differing high words; 16 ids, so
// the maps rehash (8 buckets, load 1).
uint64_t PickId(uint32_t a)
{
    static const uint32_t kLows[8] = {0x10, 0x18, 0x20, 0x11, 0xff000010, 0x7, 0x10000010, 0x3};
    return uint64_t((a >> 3) % 2 ? 1u + (a >> 4) % 2 : 0u) << 32 | kLows[a % 8];
}
uint32_t PickName(uint32_t a) { return (a % 12) * 0x9e3779b1u; }
cIGZUnknown *PickItem(uint32_t a)
{
    return a % 9 == 0 ? nullptr : reinterpret_cast<cIGZUnknown *>(&F(kItem0 + a % kItems));
}

// Calls that crash both sides (the original's unchecked pointers): Create / Play with no renderer, and Play of an id
// registered with a null object (0xb261e9 counts it unchecked).
bool CanCreate(const Mgr &mgr) { return Word(reinterpret_cast<const uint8_t *>(&mgr), 0x28) != 0; }
bool CanPlay(const Mgr &mgr, uint64_t id)
{
    const auto it = mgr.mObjects.find(id);
    if (it != mgr.mObjects.end()) return it->second.Get() != nullptr;
    return CanCreate(mgr);
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const service = object;
    void *const mgrFace = Face(object, 0x1c);
    void *const update = Face(object, 0x20);
    const Mgr &mgr = *static_cast<const Mgr *>(object);
    const uint32_t a = op.arg, c = op.arg2;
    // The out word before the call (NormOut: left alone, or a pointer).
    void *out = reinterpret_cast<void *>(uintptr_t(0xffff5e5e));
    const uint32_t ab = (c >> 4) % 5, bb = c % 3 ? 0u : 0x77u, iid = kIids[(c >> 8) % 4];
    switch (op.kind) {
    case 0: b.push_back(Slot<uint32_t>(service, 8) & 0xff); break;
    case 1: b.push_back(Slot<uint32_t>(service, 9) & 0xff); break;
    case 2: b.push_back(Slot<uint32_t>(mgrFace, 3, a, c, a ^ c) & 0xff); break;
    case 3:
        if (a % 2) Slot<void>(mgrFace, 4, c % 3 ? c : 0u);
        b.push_back(Slot<uint32_t>(mgrFace, 5));
        break;
    case 4: b.push_back(Slot<uint32_t>(mgrFace, 6, PickName(a))); break;
    case 5: Slot<void>(mgrFace, 7, PickName(a), PickId(c)); break;
    case 6: { // the words
        uint32_t word = 0x99;
        b.push_back(Slot<uint32_t>(mgrFace, 8 + 2 * (a % 2), c) & 0xff);
        b.push_back(Slot<uint32_t>(mgrFace, 9 + 2 * ((a >> 1) % 2), &word) & 0xff);
        b.push_back(word);
        break;
    }
    case 7: case 8: Slot<void>(mgrFace, 12, PickItem(a), PickId(c)); break;
    case 9: Slot<void>(mgrFace, 13, PickId(a)); break;
    case 10: Slot<void>(mgrFace, 14, PickItem(a)); break;
    case 11: b.push_back(Slot<uint32_t>(mgrFace, 15, PickItem(a)) & 0xff); break;
    case 12: b.push_back(Slot<uint32_t>(mgrFace, 16, PickItem(a)) & 0xff); break;
    case 13: // CreateObjectByName
        if (!CanCreate(mgr)) return;
        F(kName).value[0] = a % 6; // NameText reads it modulo 6; a value word is normalised (CaptureFakes)
        b.push_back(Slot<uint32_t>(mgrFace, 17, static_cast<void *>(&F(kName)), ab, bb, iid, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out), 0xffff5e5e));
        break;
    case 14: // CreateObject
        if (!CanCreate(mgr)) return;
        b.push_back(Slot<uint32_t>(mgrFace, 18, PickId(a), ab, bb, iid, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out), 0xffff5e5e));
        break;
    case 15: case 16: { // Play / PlayWithFlags
        const uint64_t id = PickId(a);
        if (!CanPlay(mgr, id)) return;
        b.push_back((op.kind == 15 ? Slot<uint32_t>(mgrFace, 19, id) : Slot<uint32_t>(mgrFace, 20, id, c)) & 0xff);
        break;
    }
    case 17: { // PlayAlias / PlayAliasWithFlags
        const uint32_t name = PickName(a);
        const auto it = mgr.mAliases.find(name);
        if (!CanPlay(mgr, it == mgr.mAliases.end() ? 0 : it->second)) return;
        b.push_back((c % 2 ? Slot<uint32_t>(mgrFace, 21, name, c) : Slot<uint32_t>(mgrFace, 22, name, c, c >> 3)) & 0xff);
        break;
    }
    case 18: // AddPlaying (not null: Update polls the first sound unchecked, 0xb2648d)
        b.push_back(Slot<uint32_t>(mgrFace, 23, static_cast<void *>(&F(kItem0 + a % kItems))) & 0xff);
        break;
    case 19: // CreateObject2
        b.push_back(Slot<uint32_t>(mgrFace, 24, PickId(a), ab, bb, iid, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out), 0xffff5e5e));
        break;
    case 20: b.push_back(Slot<uint32_t>(mgrFace, 25, PickItem(a), c % 5) & 0xff); break;
    case 21: b.push_back(Slot<uint32_t>(mgrFace, 26, c % 5) & 0xff); break;
    case 22: b.push_back(Slot<uint32_t>(mgrFace, 27, PickItem(a)) & 0xff); break;
    case 23: // QueryCreator2
        b.push_back(Slot<uint32_t>(mgrFace, 28, c % 5, iid, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out), 0xffff5e5e));
        break;
    case 24: Slot<void>(mgrFace, 29, PickItem(a), PickId(c)); break;
    case 25: Slot<void>(mgrFace, 30, PickId(a)); break;
    case 26: Slot<void>(mgrFace, 31, PickItem(a)); break;
    case 27: { // Update, a few times
        const unsigned times = 1 + a % 3;
        for (unsigned t = 0; t < times; ++t) Slot<void>(update, 3, c);
        break;
    }
    case 28: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kAsk[] = {1, cIGZSndMgr::kIid, 0x5b34f0af, 0x287fb697, Mgr::kServiceId, 0x12345678};
        void *faces[] = {service, Face(object, 4), mgrFace, update};
        void *q = out;
        const uint32_t ask = kAsk[(a >> 4) % 6];
        const bool nullOut = ask == cIGZSndMgr::kIid && (a >> 7) % 4 == 0;
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, ask, nullOut ? nullptr : &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q), 0xffff5e5e));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 2));
        if (ok && !nullOut) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 29: // the cIGZSystemService accessors, the base's count; the world changes
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        Slot<void>(service, 7, a % 2);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        gWorld.ready = (a >> 4) % 4 ? 1 + (a >> 6) % 3 : 0;
        gWorld.renderer = (a >> 8) % 5 != 0;
        break;
    }
}

} // namespace sndmgr_test

using namespace sndmgr_test;

DIFF_TEST(SndMgr_Run, 0xb256f5)
{
    // Also reached (see the file comment): 0xb22333 (factory), 0xb2582c, 0xb25906, 0xb258d8, 0xb25c7f, 0xb25f0c, 0xb25c00,
    // 0xb260d0, 0xb25980, 0x20daf0, 0x149d00, 0xb262e2, 0xb2606d, 0xb2604f, 0xb2595e, 0xb2605e, 0xb2596f, 0xb25cce, 0xb26322,
    // 0xb2637c, 0xb25d66, 0xb25f43, 0xb25ad1, 0xb25b22, 0xb2617f, 0xb26192, 0xb26257, 0xb2626a, 0xb25928, 0xb259a3, 0xb25e77,
    // 0xb25f95, 0xb25fe1, 0xb25a73, 0xb25dee, 0xb263bf, 0xb26410, 0xb26474, the adjustor thunks 0xb25cb6, 0xb25cbe, 0xb25cc6,
    // 0xb25f2b, 0xb25f33, 0xb25f3b.
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch probe(0x9c5b7e, shape::CodeOf(&StandProbe));

    for (uint32_t seed = 1; seed <= 2500; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            void *object = Build(ctx, side == 1);
            gObject = static_cast<uint8_t *>(object);
            svcfx::AddRange(object, Mgr::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    // The last release shuts it down and destroys it; every fifth seed the +4 face's deleting destructor
                    // (flags 1) instead, so the destructor's own Shutdown runs on a full manager.
                    if (seed % 5) b.push_back(Slot<uint32_t>(object, 2));
                    else b.push_back(svcfx::Norm(Slot<uint32_t>(Face(object, 4), 3, 1u)));
                    gObject = nullptr; // (read until the block is freed: no callback follows)
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "SndMgr_Run", seed)) {
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
