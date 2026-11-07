// Differential tests for cTSNeighborhoodManager (src/ts/cTSNeighborhoodManager.h): construction and destruction, Read/Write
// against recording fake streams, the id and name lookups, the filters, Open and Clear.
//
// A world is one manager in a guarded buffer, populated by our own code (the containers are the library's, the game's layout),
// with recording fakes: neighbourhoods, their group object, a predicate, the persist system (cache 0x10faf6c), the common
// implementation (cache 0x10cc040), the allocator service (0x10c95d4), streams. Every call a fake receives is appended to
// `events` (pointers to fakes become ids, pointers into the manager their offset, stack addresses a marker). Two worlds built
// from one seed start the same; one runs the original, the other ours, and the results, the manager's bytes (heap pointers
// normalised), the containers' contents, the fakes' reference counts and the event logs are compared.
#include "game_allocator.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/GeneralAllocator.h"
#include "ts/cTSNeighborhoodManager.h"
#include "ts_neighborhood_manager_shape.h"

#include <Windows.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {
using Mgr = cTSNeighborhoodManager;
using namespace hoodmgrshape;
constexpr unsigned kSlots = 32;
constexpr unsigned kHoods = 8;
constexpr unsigned kMaxArgs = 8;

struct SObj;
using Custom = uint32_t (*)(SObj *self, uint32_t *args);
struct SObj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[4];
    uint32_t skipLog; // bit i: argument i is not logged (the handler logs it)
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
};

enum Fake : unsigned {
    kHood0, kHoodLast = kHood0 + kHoods - 1, kGroup, kPred, kPersist, kCommon, kStream, kHolder, kAllocator, kFakeCount
};

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[sizeof(Mgr)];
    uint8_t after[16];
};

struct World {
    Guarded work;
    SObj fakes[kFakeCount];
    std::vector<uint32_t> events;
    bool alive = false;
    uint32_t allocations = 0;
};
World gW;
Mgr *M() { return reinterpret_cast<Mgr *>(gW.work.bytes); }
SObj &S(Fake f) { return gW.fakes[f]; }

enum : uint32_t { evCall = 0x60, evMgr = 0x61, evAlloc = 0x62, evFree = 0x63, evText = 0x64 };
void Ev(std::initializer_list<uint32_t> items) { for (uint32_t v : items) gW.events.push_back(v); }

uint32_t Norm(uint32_t value)
{
    if (!value) return 0;
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (value == uint32_t(uintptr_t(&gW.fakes[i]))) return 0x1000 + i;
    const uint32_t base = uint32_t(uintptr_t(gW.work.bytes));
    if (value >= base && value < base + sizeof(Mgr)) return 0x2000 + (value - base);
    uint32_t local = 0;
    const uint32_t sp = uint32_t(uintptr_t(&local));
    if (value - sp < 0x10000 || sp - value < 0x10000) return 0x57ac;
    return value;
}

// ---- the generic recording fakes (0 to 7 arguments) ----
uint32_t Dispatch(SObj *self, unsigned slot, uint32_t *args, unsigned count)
{
    Ev({evCall, self->id, slot, count});
    for (unsigned i = 0; i < count; ++i) Ev({(self->skipLog >> i) & 1 ? 0x5e1f : Norm(args[i])});
    if (self->custom[slot]) return self->custom[slot](self, args);
    if (slot == 1) return uint32_t(++self->refs);
    if (slot == 2) return uint32_t(--self->refs);
    return self->ret[slot];
}
template <size_t> using Word = uint32_t;
void *gTable[kSlots][kMaxArgs];
template <unsigned Slot, class Seq> struct Gen;
template <unsigned Slot, size_t... I> struct Gen<Slot, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(SObj *self, void *, Word<I>... a)
    {
        uint32_t args[] = {a..., 0u};
        return Dispatch(self, Slot, args, unsigned(sizeof...(I)));
    }
};
template <unsigned Slot, size_t... N> void FillSlotN(std::index_sequence<N...>)
{
    ((gTable[Slot][N] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<N>>::Fn)), ...);
}
template <unsigned Slot> void FillSlot() { FillSlotN<Slot>(std::make_index_sequence<kMaxArgs>()); }
template <size_t... S> void FillAll(std::index_sequence<S...>) { (FillSlot<unsigned(S)>(), ...); }

void Set(SObj &o, unsigned slot, unsigned args, uint32_t result = 0, Custom fn = nullptr)
{
    o.table[slot] = gTable[slot][args];
    o.ret[slot] = result;
    o.custom[slot] = fn;
}
void Set(SObj &o, unsigned slot, unsigned args, const void *result) { Set(o, slot, args, uint32_t(uintptr_t(result))); }

void ResetFake(SObj &o, uint32_t id)
{
    o.vt = o.table;
    o.id = id;
    o.refs = 1000;
    o.skipLog = 0;
    for (auto &v : o.value) v = 0;
    for (unsigned i = 0; i < kSlots; ++i) o.ret[i] = 0, o.custom[i] = nullptr, o.table[i] = nullptr;
    Set(o, 1, 0);
    Set(o, 2, 0);
}

// Custom handlers.
uint32_t StreamGetVersion(SObj *self, uint32_t *args) // slot 8: delivers value[0] unless value[2] says the read fails
{
    if (!self->value[2]) *reinterpret_cast<uint32_t *>(args[0]) = self->value[0];
    return 1;
}
uint32_t StreamGetSecond(SObj *self, uint32_t *args) // slot 9 (reading): delivers value[1] unless value[3] says it fails
{
    if (!self->value[3]) *reinterpret_cast<uint32_t *>(args[0]) = self->value[1];
    return 1;
}
// The common implementation's slot 4 (the option query): leaves some text in the string it is given.
const char *const kTexts[] = {"", "hoodopt", "an option text that is longer than fifteen characters"};
uint32_t CommonQuery(SObj *self, uint32_t *args)
{
    reinterpret_cast<NeighborhoodName *>(args[1])->mString = kTexts[self->value[0] % 3];
    return 1;
}
// The persist system's slot 21 (seven arguments): logs the text (the pointer differs per side), hands out a holder.
uint32_t PersistOpen(SObj *self, uint32_t *args)
{
    const char *text = reinterpret_cast<const char *>(args[3]);
    Ev({evText, uint32_t(std::strlen(text))});
    for (const char *p = text; *p; ++p) Ev({uint32_t(uint8_t(*p))});
    if (self->value[0]) *reinterpret_cast<void **>(args[1]) = &S(kHolder);
    return self->value[1];
}

// The game's global allocator (the image's object at 0x10cb098, unconstructed in the harness) built for the length of a test, so
// the original's RZMemory::Malloc / Free and ours (the lists' storage) work on the same blocks.
struct GameAllocatorFixture {
    GeneralAllocator *allocator;
    bool built = false;
    GameAllocatorFixture() : allocator(GameAddress<GeneralAllocator>(0x10cb098))
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(allocator);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) {
            allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
            built = true;
        }
    }
    ~GameAllocatorFixture()
    {
        if (built) {
            difftest_alloc::ShutdownAndRelease(allocator);
        }
    }
};

// While alive, the allocator service cache of the image (0x10c95d4, guard 0x10c95d8) holds an object with the game's own service
// vtable (slot 3 allocates, slot 4 frees, both through the game allocator).
struct AllocatorServiceScope {
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
    alignas(8) uint8_t object[16];
    explicit AllocatorServiceScope(difftest::Context &ctx) : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int>(0x10c95d8))
    {
        savedCache = *cache;
        savedGuard = *guard;
        std::memset(object, 0, sizeof(object));
        const void *vtable = ctx.image.At<void>(0xda3cd4);
        std::memcpy(object, &vtable, sizeof(vtable));
        *cache = object;
        *guard = INT32_MIN;
    }
    AllocatorServiceScope(const AllocatorServiceScope &) = delete;
    ~AllocatorServiceScope()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

// The manager's own slots that the members call through `this` and the persist face (gPatch: over each route's own copies).
uint32_t __fastcall MgrAddRef(Mgr *m, void *) { Ev({evMgr, 1}); return ++m->mRefCount; }
void *__fastcall MgrNext(Mgr *m, void *)
{
    auto *cursor = m->mCursor; // a heap node: logged as its id (or a marker)
    Ev({evMgr, 8, cursor == nullptr ? 0xc001 : cursor->mIsNil ? 0xc002 : *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(cursor) + 0x10)});
    return &S(kHood0);
}
uint32_t __fastcall MgrLoad(Mgr *, void *) { Ev({evMgr, 3}); return 1; }
void *__fastcall MgrDelete(Mgr *m, void *, uint32_t flags) { Ev({evMgr, 20, flags}); return m; }
uint32_t __fastcall FaceSetKey(void *face, void *, const uint32_t *key)
{
    Ev({evMgr, 0x104, Norm(uint32_t(uintptr_t(face))), key[0], key[1], key[2], key[3]});
    return 1;
}

// The in-order successor of a map node (the test's own oracle for placing the cursor).
NeighborhoodTreeNode *TreeNext(NeighborhoodTreeNode *node)
{
    if (!node->mRight->mIsNil) {
        node = node->mRight;
        while (!node->mLeft->mIsNil) node = node->mLeft;
        return node;
    }
    NeighborhoodTreeNode *parent;
    while (!(parent = node->mParent)->mIsNil && node == parent->mRight) node = parent;
    return parent;
}

const char *const kNames[] ={"alpha", "Bravo", "charlie", "DELTA", "a name longer than fifteen chars", "echo", "foxtrot"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);

void Teardown()
{
    if (!gW.alive) return;
    gW.alive = false;
    Destroy(gW.work.bytes);
}

enum Fill : uint32_t { fIds = 1, fNames = 2, fList = 4, fNullHood = 8 };

void Build(uint32_t seed, uint32_t fill)
{
    static bool filled = false;
    if (!filled) FillAll(std::make_index_sequence<kSlots>()), filled = true;
    World &w = gW;
    Teardown();
    std::mt19937 r(seed * 2654435761u + 3);
    w.events.clear();
    w.allocations = 0;
    for (auto &b : w.work.before) b = uint8_t(r());
    for (auto &b : w.work.after) b = uint8_t(r());
    for (auto &b : w.work.bytes) b = uint8_t(r());
    for (unsigned i = 0; i < kFakeCount; ++i) ResetFake(w.fakes[i], 0x100 + i);
    for (unsigned i = kHood0; i <= kHoodLast; ++i) {
        SObj &n = w.fakes[i];
        Set(n, 4, 0, &S(kGroup));
        Set(n, 14, 1, r() % 3 == 0 ? 1 + r() % 0xff : (r() % 2 ? 0 : (r() & 0xffffff00)));
        Set(n, 18, 0, r() % 4);
        Set(n, 22, 0, r() % 3 ? r() % 4 : 0xffffffffu);
        Set(n, 26, 0);
    }
    Set(S(kGroup), 10, 2, r() % 3 == 0 ? 1 : (r() & 0xffffff00));
    Set(S(kPred), 0, 1, 0u);
    Set(S(kCommon), 4, 5, 1, CommonQuery);
    S(kCommon).value[0] = r();
    Set(S(kPersist), 21, 7, 0, PersistOpen);
    S(kPersist).skipLog = 1u << 3;
    S(kPersist).value[0] = r() % 4 != 0;
    S(kPersist).value[1] = r();
    Set(S(kHolder), 3, 0);

    Mgr *m = Construct(w.work.bytes);
    gW.alive = true;
    m->mRefCount = r() % 4;
    m->mNextId = r();
    m->mCursor = nullptr;
    auto hood = [&]() -> void * { return &w.fakes[kHood0 + r() % kHoods]; };
    if (fill & fIds) {
        const unsigned count = r() % 8;
        for (unsigned i = 0; i < count; ++i) {
            const uint32_t key = r() % 4 ? r() % 40 : r();
            m->mHoodsById[key].Assign(hood());
        }
    }
    if (fill & fNames) {
        const unsigned count = 1 + r() % 6;
        for (unsigned i = 0; i < count; ++i) {
            NeighborhoodName key(kNames[r() % kNameCount]);
            if (r() % 3 == 0) for (char &c : key.mString) c = char(r() % 2 ? toupper(c) : tolower(c));
            auto it = m->mHoodsByName.try_emplace(key).first;
            NeighborhoodNameEntry &e = it->second;
            if (!e.mObject && r() % 3) { e.mObject = hood(); }
            const unsigned n = r() % 5;
            for (unsigned k = 0; k < n; ++k) e.mHoods.PushBack(hood());
        }
    }
    if (fill & fList) {
        const unsigned n = r() % 6;
        for (unsigned k = 0; k < n; ++k) m->mHoods.PushBack((fill & fNullHood) && r() % 4 == 0 ? nullptr : hood());
    }
    // The route's vtable copies with the fakes last, so the object was built with the real ones.
    Install(w.work.bytes);
    w.events.clear();
}

using Blob = std::vector<uint32_t>;

void CaptureList(Blob &b, const ServiceRefVector &v)
{
    b.push_back(uint32_t(v.mLast - v.mFirst));
    b.push_back(uint32_t(v.mEnd - v.mFirst));
    for (void **p = v.mFirst; p != v.mLast; ++p) b.push_back(Norm(uint32_t(uintptr_t(*p))));
}

void Capture(Blob &b, bool alive = true)
{
    World &w = gW;
    b.push_back(0xb10b);
    for (auto v : w.work.before) b.push_back(v);
    for (auto v : w.work.after) b.push_back(v);
    if (alive) {
        uint8_t copy[sizeof(Mgr)];
        std::memcpy(copy, w.work.bytes, sizeof(copy));
        auto word = [&](unsigned off) -> uint32_t & { return *reinterpret_cast<uint32_t *>(copy + off); };
        for (unsigned off : {0x10u, 0x18u, 0x24u, 0x28u, 0x2cu, 0x30u}) word(off) = 0; // heap pointers (contents below)
        for (unsigned f = 0; f < kFaces; ++f) word(kFaceOffset[f]) = VtCode(f, word(kFaceOffset[f]));
        for (unsigned i = 0; i < sizeof(copy) / 4; ++i) b.push_back(reinterpret_cast<uint32_t *>(copy)[i]);
        Mgr *m = M();
        b.push_back(0xc0de);
        for (auto &e : m->mHoodsById) { b.push_back(e.first); b.push_back(Norm(uint32_t(uintptr_t(e.second.Get())))); }
        b.push_back(0xc0df);
        for (auto &e : m->mHoodsByName) {
            b.push_back(uint32_t(e.first.mString.size()));
            for (char c : e.first.mString) b.push_back(uint8_t(c));
            b.push_back(e.first.mRefCount);
            b.push_back(Norm(uint32_t(uintptr_t(e.first.mVTable))));
            b.push_back(Norm(uint32_t(uintptr_t(e.second.mObject))));
            CaptureList(b, e.second.mHoods);
        }
        b.push_back(0xc0e0);
        CaptureList(b, m->mHoods);
        // The cursor: the id of its node, or a marker for the head.
        auto *cursor = m->mCursor;
        b.push_back(cursor == nullptr ? 0xc001 : cursor->mIsNil ? 0xc002 : *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(cursor) + 0x10));
    } else {
        for (unsigned f = 0; f < kFaces; ++f) b.push_back(VtCode(f, shape::Word(w.work.bytes, kFaceOffset[f])));
        b.push_back(shape::Word(w.work.bytes, 0xc));
    }
    for (auto &o : w.fakes) b.push_back(uint32_t(o.refs));
    b.push_back(0xe7e7);
    for (uint32_t e : w.events) b.push_back(e);
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    if (at == a.size() && at == b.size()) return true;
    ctx.Fail("%s seed %u differs at word %zu (original %x, replacement %x; sizes %zu/%zu)", what, seed, at,
        at < a.size() ? a[at] : 0xdeadbeef, at < b.size() ? b[at] : 0xdeadbeef, a.size(), b.size());
    return false;
}

// Points the game's globals at the fakes for the scope.
struct Globals {
    difftest::Context &ctx;
    static constexpr uint32_t kRvas[] = {0x10c95d4, 0x10c95d8, 0x10faf6c, 0x10cc040};
    uint32_t saved[4];
    GameAllocatorFixture allocator;
    AllocatorServiceScope service;
    explicit Globals(difftest::Context &c) : ctx(c), service(c)
    {
        for (unsigned i = 0; i < 4; ++i) saved[i] = *ctx.image.At<uint32_t>(kRvas[i]);
        for (auto &face : gPatch)
            for (void *&slot : face) slot = nullptr;
        gPatch[0][1] = reinterpret_cast<void *>(&MgrAddRef);
        gPatch[0][3] = reinterpret_cast<void *>(&MgrLoad);
        gPatch[0][8] = reinterpret_cast<void *>(&MgrNext);
        gPatch[0][20] = reinterpret_cast<void *>(&MgrDelete);
        gPatch[1][4] = reinterpret_cast<void *>(&FaceSetKey);
        *ctx.image.At<void *>(0x10faf6c) = &S(kPersist);
        *ctx.image.At<void *>(0x10cc040) = &S(kCommon);
    }
    ~Globals()
    {
        Teardown();
        for (unsigned i = 0; i < 4; ++i) *ctx.image.At<uint32_t>(kRvas[i]) = saved[i];
    }
};

template <class Setup, class Run>
void Pair(difftest::Context &ctx, const char *what, uint32_t seed, uint32_t fill, Setup setup, Run run, bool alive = true)
{
    Blob blobs[3];
    for (int route = 0; route < 3; ++route) {
        gRoute = route;
        gTrapHits = 0;
        Build(seed, fill);
        std::mt19937 rng(seed * 7919u + 5);
        setup(rng);
        gW.events.clear();
        const uint32_t result = uint32_t(run());
        if (!alive) gW.alive = false;
        blobs[route].push_back(result);
        Capture(blobs[route], alive);
        if (gTrapHits) ctx.Fail("%s seed %u route %d: %u trap hits", what, seed, route, gTrapHits);
    }
    gRoute = 0;
    Same(ctx, blobs[0], blobs[1], (std::string(what) + " (vtable route)").c_str(), seed);
    Same(ctx, blobs[0], blobs[2], (std::string(what) + " (adapter route)").c_str(), seed);
}
auto NoSetup = [](std::mt19937 &) {};

constexpr uint32_t kSeeds = 120;
#define ORIG(Fn, rva) ctx.Original<Fn>(rva)
#define ADAPTER(name) HOODMGR_ADAPTER(name)
// Calls the function at `rva` of face `face` on the world's manager, the route's way.
template <class Fn, class... A> auto Run(difftest::Context &ctx, uint32_t rva, unsigned face, void *adapter, A... args)
{
    return Call<Fn>(ctx, gW.work.bytes, rva, face, adapter, args...);
}
} // namespace

// ---- construction and destruction ----
DIFF_TEST(NeighborhoodManager_Create, 0x4a3f20)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 24; ++seed) {
        Build(seed, 0);
        Teardown();
        using Fn = Mgr *(__cdecl *)();
        // a: the original's object; b: the Create hook's entry (our class, our constructor); c: a game-built one for the adapter.
        Mgr *a = ORIG(Fn, 0x4a3f20)();
        Mgr *b = Mgr::Create();
        Mgr *c = ORIG(Fn, 0x4a3f20)();
        ++ctx.cases;
        if (!a || !b || !c) { ctx.Fail("create returned null"); continue; }
        if (HeapSize(GetProcessHeap(), 0, a) < sizeof(Mgr) || HeapSize(GetProcessHeap(), 0, b) < sizeof(Mgr)) ctx.Fail("create block size");
        Blob blobs[2];
        for (Mgr *m : {a, b}) {
            Blob &out = blobs[m == b];
            uint8_t copy[sizeof(Mgr)];
            std::memcpy(copy, m, sizeof(copy));
            for (unsigned off : {0x10u, 0x18u}) *reinterpret_cast<uint32_t *>(copy + off) = 0; // head nodes (checked below)
            // Each side's own vtables: the game's in the original's object, ours in ours.
            for (unsigned f = 0; f < kFaces; ++f) {
                uint32_t &word = *reinterpret_cast<uint32_t *>(copy + kFaceOffset[f]);
                word = word == shape::P(m == a ? GameVt(f) : OurVt(f)) ? 0x7d00 + f : word;
            }
            for (unsigned i = 0; i < sizeof(Mgr); ++i) out.push_back(copy[i]);
            out.push_back(uint32_t(m->mHoodsById.size()));
            out.push_back(m->mHoodsById.begin() == m->mHoodsById.end());
            out.push_back(uint32_t(m->mHoodsByName.size()));
            out.push_back(m->mHoodsByName.begin() == m->mHoodsByName.end());
            out.push_back(m->IdHead()->mIsNil);
            out.push_back(m->IdHead()->mColor);
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
        // Bit 0 of the flags frees the block: a by the original deleting destructor, b through slot 20 of its own vtable, c by
        // the original again (its adapter was dropped, lane GJ: only the game's vtable reached it); each frees the same blocks.
        size_t sizes[3][8] = {};
        size_t counts[3];
        using DeleteFn = Mgr *(__thiscall *)(Mgr *, uint32_t);
        for (int side = 0; side < 3; ++side) {
            Mgr *m = side == 0 ? a : side == 1 ? b : c;
            difftest::FreeRecorder rec;
            Mgr *r = side == 0   ? ORIG(DeleteFn, 0x4a1fb0)(m, 1)
                     : side == 1 ? reinterpret_cast<DeleteFn>(OurVt(0)[20])(m, 1)
                                 : ORIG(DeleteFn, 0x4a1fb0)(m, 1);
            rec.Stop();
            ++ctx.cases;
            if (!rec.Freed(m) || r != m) ctx.Fail("DeletingDestructor(1) seed %u side %d kept the block", seed, side);
            counts[side] = rec.SortedSizes(sizes[side], 8);
        }
        for (int side = 1; side < 3; ++side)
            if (counts[0] != counts[side] || std::memcmp(sizes[0], sizes[side], sizeof(sizes[0])) != 0)
                ctx.Fail("DeletingDestructor(1) frees differ (side %d)", side);
    }
}

DIFF_TEST(NeighborhoodManager_DeletingDestructor, 0x4a1fb0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "DeletingDestructor", seed, seed % 16, NoSetup, [&]() {
            using Fn = Mgr *(__thiscall *)(Mgr *, uint32_t);
            const uint32_t flags = (seed % 3) * 2; // bit 0 (freeing) is covered by Create
            Mgr *r = Run<Fn>(ctx, 0x4a1fb0, 0, NO_ADAPTER, flags);
            return Norm(uint32_t(uintptr_t(r)));
        }, false);
}

DIFF_TEST(NeighborhoodManager_ReleaseAndQuery, 0x4a5540)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair(ctx, "Release", seed, 0, [&](std::mt19937 &rng) { M()->mRefCount = rng() % 4 == 0 ? rng() : rng() % 4; },
            [&]() { return Run<uint32_t(__thiscall *)(Mgr *)>(ctx, 0x4a5540, 0, ADAPTER(Release)); });
        constexpr uint32_t ids[] = {1, 0xcc8a6a62, 0xe4fda3d4, 0x456b8f1d, 0, 2, 0x456b8f1c, 0x456b8f1e, 0xcc8a6a63, 0xcc8a6a61,
            0xcc8a6a69, 0xffffffff, 0xe4fda3d5, 0xe4fda3d3};
        const uint32_t id = ids[seed % (sizeof(ids) / 4)];
        Pair(ctx, "QueryInterface", seed, 0, NoSetup, [&]() {
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const uint8_t r = Run<uint8_t(__thiscall *)(Mgr *, uint32_t, void **)>(ctx, 0x4a5480, 0, ADAPTER(QueryInterface), id, &out);
            return uint32_t(r) | (Norm(uint32_t(uintptr_t(out))) << 8);
        });
    }
    // GetGZCLSID (a constant; the face is never read): the original, our serial vtable's slot 5 and the adapter.
    using ClsidFn = uint32_t(__thiscall *)(void *);
    void *face = reinterpret_cast<void *>(0x1234);
    const uint32_t expected = ORIG(ClsidFn, 0x4a4050)(face);
    ++ctx.cases;
    if (reinterpret_cast<ClsidFn>(OurVt(2)[5])(face) != expected) ctx.Fail("GetGZCLSID (vtable route)");
    if (reinterpret_cast<ClsidFn>(ADAPTER(GetGZCLSID))(face) != expected) ctx.Fail("GetGZCLSID (adapter route)");
    if (shape::SlotOf(Mgr::kSerialVTable, 6, 0x4a4050) != 5) ctx.Fail("GetGZCLSID is not the serial face's slot 5");
}

// The slots no other test calls (folded bodies, the face getter): the game's slot on a game-shaped object, our vtable's slot on
// ours. Persist face: key accessors and the count; primary: AddRef and GetPersistFace; serial: AddRef (thunk).
DIFF_TEST(NeighborhoodManager_Slots, 0x4a4060)
{
    Globals globals(ctx);
    struct Case {
        unsigned face, slot, words;
    };
    static const Case cases[] = {{0, 1, 0}, {0, 6, 0}, {1, 1, 0}, {1, 3, 1}, {1, 4, 1}, {1, 5, 0}, {1, 6, 0}, {1, 7, 0},
        {1, 8, 1}, {1, 9, 1}, {1, 10, 1}, {1, 11, 0}, {2, 1, 0}};
    for (uint32_t seed = 0; seed < 60; ++seed)
        for (const Case &c : cases) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                gRoute = side;
                gTrapHits = 0;
                Build(seed, fIds);
                std::mt19937 rng(seed * 131 + c.slot);
                Mgr *m = M();
                m->mRefCount = rng() % 5;
                m->mKey = {rng(), rng(), rng(), rng()};
                uint32_t key[4] = {rng(), rng(), rng(), rng()};
                uint32_t out[5] = {0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e};
                void *self = gW.work.bytes + kFaceOffset[c.face];
                void *fn = (side == 0 ? GameVt(c.face) : OurVt(c.face))[c.slot];
                uint32_t r;
                if (c.words == 0) {
                    r = reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self);
                } else {
                    // Slot 3 takes the hidden result pointer, slot 4 the key, slots 8..10 a value.
                    const uint32_t arg = c.slot == 3 ? shape::P(out) : c.slot == 4 ? shape::P(key) : key[seed % 4];
                    r = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(self, arg);
                }
                // Byte results (AL) for the setters; pointers become offsets or markers.
                if (c.face == 1 && (c.slot == 4 || c.slot >= 8) && c.slot != 11) r &= 0xff;
                blobs[side].push_back(Norm(r));
                for (uint32_t v : out) blobs[side].push_back(v);
                Capture(blobs[side]);
            }
            gRoute = 0;
            char what[48];
            std::snprintf(what, sizeof(what), "Slots face %u slot %u", c.face, c.slot);
            Same(ctx, blobs[0], blobs[1], what, seed);
        }
}

// ---- serialisation ----
DIFF_TEST(NeighborhoodManager_Write, 0x4a60d0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Write", seed, seed % 4,
            [&](std::mt19937 &rng) {
                SObj &s = S(kStream);
                Set(s, 9, 1, rng() % 2);
                Set(s, 18, 0, rng() % 2 ? 0 : (rng() % 3 ? 1 : rng() & 0xffffff00));
            },
            [&]() { return Run<uint8_t(__thiscall *)(void *, void *)>(ctx, 0x4a60d0, 2, NO_ADAPTER, &S(kStream)); });
}

DIFF_TEST(NeighborhoodManager_Read, 0x4a54f0)
{
    Globals globals(ctx);
    constexpr uint32_t versions[] = {0, 1, 2, 3, 0x11, 0x12, 0xffffffffu, 0x80000000u, 5};
    constexpr uint32_t seconds[] = {0, 1, 0x10, 0x11, 0x12, 0x13, 0xffffffffu, 0x80000000u};
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed)
        Pair(ctx, "Read", seed, 0,
            [&](std::mt19937 &rng) {
                SObj &s = S(kStream);
                s.value[0] = versions[rng() % (sizeof(versions) / 4)];
                s.value[1] = seconds[rng() % (sizeof(seconds) / 4)];
                s.value[2] = rng() % 6 == 0; // the version read delivers nothing
                s.value[3] = rng() % 6 == 0; // the second read delivers nothing
                Set(s, 8, 1, 0, StreamGetVersion);
                Set(s, 9, 1, 0, StreamGetSecond);
                Set(s, 18, 0, rng() % 2 ? 0 : (rng() % 3 ? 1 : rng() & 0xffffff00));
            },
            [&]() { return Run<uint8_t(__thiscall *)(void *, void *)>(ctx, 0x4a54f0, 2, NO_ADAPTER, &S(kStream)); });
}

// ---- the id map ----
DIFF_TEST(NeighborhoodManager_IdLookups, 0x4a5140)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        Pair(ctx, "GetById", seed, fIds, NoSetup, [&]() {
            uint32_t id = seed % 5 == 0 ? seed : seed % 40;
            if (seed % 3 == 0 && !M()->mHoodsById.empty()) id = std::next(M()->mHoodsById.begin(), seed % M()->mHoodsById.size())->first;
            void *r = Run<void *(__thiscall *)(Mgr *, uint32_t)>(ctx, 0x4a5140, 0, NO_ADAPTER, id);
            return Norm(uint32_t(uintptr_t(r)));
        });
        Pair(ctx, "FindAccepting", seed, fIds,
            [&](std::mt19937 &rng) { Set(S(kGroup), 10, 2, rng() % 3 == 0 ? 1 : (rng() & 0xffffff00)); },
            [&]() {
                void *r = Run<void *(__thiscall *)(Mgr *, uint32_t)>(ctx, 0x4a52a0, 0, NO_ADAPTER, seed);
                return Norm(uint32_t(uintptr_t(r)));
            });
        // First / Next: the cursor starts anywhere (here: null, the head, or a node).
        Pair(ctx, "Next", seed, fIds,
            [&](std::mt19937 &rng) {
                Mgr *m = M();
                const unsigned pick = rng() % 4;
                if (pick == 0) m->mCursor = m->IdHead();
                else if (pick == 1) m->mCursor = m->IdHead()->mLeft;
                else if (!m->mHoodsById.empty()) {
                    m->mCursor = m->IdHead()->mLeft;
                    for (unsigned steps = rng() % m->mHoodsById.size(); steps; --steps) m->mCursor = TreeNext(m->mCursor);
                } else m->mCursor = m->IdHead();
            },
            [&]() {
                void *r = Run<void *(__thiscall *)(Mgr *)>(ctx, 0x4a5300, 0, NO_ADAPTER);
                return Norm(uint32_t(uintptr_t(r)));
            });
        Pair(ctx, "First", seed, fIds, NoSetup, [&]() {
            void *r = Run<void *(__thiscall *)(Mgr *)>(ctx, 0x4a3f10, 0, ADAPTER(First));
            return Norm(uint32_t(uintptr_t(r)));
        });
    }
}

// ---- the name map ----
namespace {
const char *PickName(std::mt19937 &rng, bool &known)
{
    static const char *const extra[] = {"ALPHA", "bravo", "Charlie", "delta", "A NAME LONGER THAN FIFTEEN CHARS", "zulu", "", "al", "alphab",
        "FOXTROT", "Echo"};
    known = true;
    switch (rng() % 3) {
    case 0: return kNames[rng() % kNameCount];
    case 1: return extra[rng() % (sizeof(extra) / sizeof(extra[0]))];
    default: known = false; return "unrelated";
    }
}
} // namespace

DIFF_TEST(NeighborhoodManager_NameLookups, 0x4a5190)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        std::mt19937 pick(seed * 31 + 1);
        bool known;
        const char *name = PickName(pick, known);
        const int32_t a = pick() % 3 ? int32_t(pick() % 4) : 0;
        const int32_t b = pick() % 3 ? int32_t(pick() % 4) : -1;
        Pair(ctx, "GetByName", seed, fNames, NoSetup, [&]() {
            void *r = Run<void *(__thiscall *)(Mgr *, const char *)>(ctx, 0x4a5190, 0, NO_ADAPTER, name);
            return Norm(uint32_t(uintptr_t(r)));
        });
        Pair(ctx, "CountNamed", seed, fNames, NoSetup, [&]() {
            using Fn = int32_t(__thiscall *)(Mgr *, const char *, int32_t, int32_t);
            return Run<Fn>(ctx, 0x4a5350, 0, NO_ADAPTER, name, a, b);
        });
        // The output vector: empty, with entries (some room, no room), or the entry's own list.
        Pair(ctx, "GetNamedList", seed, fNames,
            [&](std::mt19937 &) {},
            [&]() {
                ServiceRefVector out{};
                ServiceRefVector *target = &out;
                const unsigned mode = seed % 4;
                for (unsigned k = 0; k < mode; ++k) out.PushBack(&gW.fakes[kHood0 + k]);
                if (mode == 3) {
                    NeighborhoodName key(name);
                    auto it = M()->mHoodsByName.find(key);
                    if (it != M()->mHoodsByName.end()) target = &it->second.mHoods;
                }
                using Fn = uint8_t(__thiscall *)(Mgr *, const char *, ServiceRefVector *);
                const uint8_t r = Run<Fn>(ctx, 0x4a3bc0, 0, NO_ADAPTER, name, target);
                Blob scratch;
                CaptureList(scratch, out);
                for (uint32_t v : scratch) gW.events.push_back(v);
                if (target == &out) { out.ReleaseAndFree(); }
                return uint32_t(r);
            });
    }
}

// ---- the filters over the manager's own list ----
DIFF_TEST(NeighborhoodManager_Filters, 0x4a5df0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        std::mt19937 pick(seed * 17 + 9);
        const int32_t a = pick() % 3 ? int32_t(pick() % 4) : 0;
        const int32_t b = pick() % 3 ? int32_t(pick() % 4) : -1;
        const bool plain = seed % 5 == 0; // a = 0, b = -1: a copy of the list
        auto useOut = [&](bool selfTarget, auto call) {
            ServiceRefVector out{};
            ServiceRefVector *target = selfTarget ? &M()->mHoods : &out;
            const unsigned mode = seed % 4;
            if (!selfTarget) for (unsigned k = 0; k < mode; ++k) out.PushBack(&gW.fakes[kHood0 + k]);
            const uint32_t r = call(target);
            Blob scratch;
            CaptureList(scratch, out);
            for (uint32_t v : scratch) gW.events.push_back(v);
            if (!selfTarget) out.ReleaseAndFree();
            return r;
        };
        Pair(ctx, "Filter", seed, fList | (plain ? fNullHood : 0), NoSetup, [&]() {
            using Fn = uint8_t(__thiscall *)(Mgr *, ServiceRefVector *, int32_t, int32_t);
            const int32_t fa = plain ? 0 : a, fb = plain ? -1 : b;
            return useOut(seed % 7 == 0 && plain, [&](ServiceRefVector *t) -> uint32_t {
                return Run<Fn>(ctx, 0x4a5df0, 0, NO_ADAPTER, t, fa, fb);
            });
        });
        Pair(ctx, "FilterWith", seed, fList | fNullHood,
            [&](std::mt19937 &rng) { Set(S(kPred), 0, 1, rng() % 2 ? 1 : (rng() % 2 ? 0 : (rng() & 0xffffff00))); },
            [&]() {
                using Fn = int32_t(__thiscall *)(Mgr *, ServiceRefVector *, void *);
                void *predicate = seed % 4 == 0 ? nullptr : &S(kPred);
                return useOut(seed % 11 == 0 && !predicate, [&](ServiceRefVector *t) -> uint32_t {
                    return uint32_t(Run<Fn>(ctx, 0x4a4070, 0, NO_ADAPTER, t, predicate));
                });
            });
    }
}

// ---- Open and Clear ----
DIFF_TEST(NeighborhoodManager_OpenAndClear, 0x4a5cc0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const uint32_t key[4] = {seed, seed * 3, 0xaace2efb, 0};
        Pair(ctx, "Open", seed, 0, NoSetup, [&]() {
            using Fn = void(__thiscall *)(Mgr *, const uint32_t *, uint32_t);
            Run<Fn>(ctx, 0x4a5cc0, 0, NO_ADAPTER, key, seed * 5);
            return 0;
        });
        Pair(ctx, "Clear", seed, fIds | fNames | fList | (seed % 3 == 0 ? fNullHood : 0), NoSetup, [&]() {
            // Slot 26 of a null list entry is called unchecked: no null entries here (the original would crash).
            if (seed % 3 == 0) {
                for (void **p = M()->mHoods.mFirst; p != M()->mHoods.mLast; ++p)
                    if (!*p) *p = &S(kHood0);
            }
            Run<void(__thiscall *)(Mgr *)>(ctx, 0x4a5f00, 0, NO_ADAPTER);
            return 0;
        });
    }
}
