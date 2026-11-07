// Differential tests for cTSPersistContext's resource index: QueryInterface (slot 0, 0x1fabb0) and slots 3..8 and 10..16
// (0x1faaa0, 0x1fa720, 0x1fabf0, 0x1fab80, 0x1faa00, 0x1faa60, 0x1faf50, 0x1fa990, 0x1faf00, 0x1fa940, 0x1fad60,
// 0x1fa7f0, 0x1fafb0), and the deleting destructor (0x1fa6e0).
//
// The object is built on both sides the same way from a seed: its index a real MSVC 14.40 std::unordered_map (the
// game's layout) holding entries for svcfx fakes (tests/ts_services_fixture.h) standing for resources (slot 3 writes
// their key; slot 0 answers the state face 0x456b8543 with a state fake, other ids by their parity) and state faces
// (slot 14 answers a byte, slot 15 logs). Keys are drawn so that hash sums collide; some index keys are not the
// resource's own key. The resource manager (the cached service cell 0x10faf6c) loads (slot 5), saves (slot 16, a byte
// per resource), deletes (slot 17) and hands out the release target (slot 3; its slot 10 answers a byte). Compared
// after each call: the result, the log of every fake call, the index (load factor, size, mask, max index, every node's
// key and entry in list order, every bucket's first and last node by list position), the object's count word, every
// fake's reference count and the heap balance.
#include "ts_services_fixture.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ts/cTSPersistContext.h"

#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace {

using Context = cTSPersistContext;
using Key = PersistKey;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Norm;
using svcfx::Obj;

constexpr unsigned kResources = 10; // fakes 0..9
constexpr unsigned kState = 10;     // 10..15: state faces
constexpr unsigned kManager = 16;
constexpr unsigned kTarget = 17;
constexpr unsigned kLoaded = 18;    // what the manager loads (a resource too)
constexpr uint32_t kObjectTag = 0x7e000000;
constexpr uint32_t kManagerCell = 0x10faf6c;
constexpr uint32_t kStateIid = 0x456b8543;

alignas(16) unsigned char gStorage[0x40];
Key gKeys[kLoaded + 1];
uint8_t gHasState[kLoaded + 1];
uint8_t gStateByte[16];
uint8_t gSaveByte[kLoaded + 1];
uint32_t gLoadAnswer = 0;
uint8_t gTargetByte = 1;

Context *Object() { return reinterpret_cast<Context *>(gStorage); }
unsigned FakeIndex(const Obj *self) { return self->id - 200; }

Key RandomKey(std::mt19937 &rng)
{
    Key k;
    k.mWords[0] = 0x2000u + rng() % 3;
    k.mWords[1] = rng() % 4;
    k.mWords[2] = uint32_t(rng());
    k.mWords[3] = rng() % 2 ? 0 : uint32_t(rng());
    return k;
}

uint32_t ResourceKeyOut(Obj *self, uint32_t *args)
{
    *reinterpret_cast<Key *>(uintptr_t(args[0])) = gKeys[FakeIndex(self)];
    return args[0];
}

uint32_t StateOutMark(Obj *self, uint32_t out)
{
    const auto found = Object()->Map().find(gKeys[FakeIndex(self)]);
    if (found != Object()->Map().end() && out == svcfx::P(found->second.mState.Out())) return 0x57a70000;
    return svcfx::Norm(out);
}

uint32_t ResourceQuery(Obj *self, uint32_t *args)
{
    auto *out = reinterpret_cast<void **>(uintptr_t(args[1]));
    const unsigned i = FakeIndex(self);
    if (args[0] == kStateIid) {
        // The out slot is the entry's state field, in a node the call may have just allocated (a heap address that
        // differs between the sides): logged as whether it is the queried key's entry's field. The field's contents
        // are compared after the call.
        svcfx::gEv.back() = StateOutMark(self, args[1]);
        if (!gHasState[i]) return 0x5500;
        Obj &state = F(kState + i % 6);
        *out = &state;
        ++state.refs;
        return 0x5501;
    }
    if (args[0] & 1) {
        *out = self;
        ++self->refs;
        return 0x7701;
    }
    return 0x7700;
}

uint32_t StateByte(Obj *self, uint32_t *) { return 0xab00u | gStateByte[FakeIndex(self) - kState]; }

// Save passes slot 15 its flag from a setz, the upper bits whatever the register held, so in the Save test only the
// low byte of the logged argument is kept (SetDirty passes its whole argument on, compared in full).
bool gStateArgByte = false;
uint32_t StateSet(Obj *, uint32_t *)
{
    if (gStateArgByte) svcfx::gEv.back() &= 0xff;
    return 0;
}

uint32_t ManagerLoad(Obj *, uint32_t *args)
{
    if (!gLoadAnswer) return 0x100;
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(kLoaded);
    ++F(kLoaded).refs;
    return 0x101;
}

uint32_t ManagerSave(Obj *, uint32_t *args)
{
    const uint32_t r = args[0];
    for (unsigned i = 0; i <= kLoaded; ++i)
        if (r == svcfx::P(&F(i))) return 0xcd00u | gSaveByte[i];
    return 0xcdffu;
}

uint32_t TargetTake(Obj *, uint32_t *args)
{
    const auto *k = reinterpret_cast<const Key *>(uintptr_t(args[0]));
    for (uint32_t w : k->mWords) Ev(w);
    return 0xee00u | (gTargetByte = uint8_t((gTargetByte * 5 + 1) % 3));
}

struct World {
    std::vector<Key> indexKeys;
};

World Build(uint32_t seed)
{
    std::mt19937 rng(seed);
    World w;
    svcfx::ResetAll();
    for (unsigned i = 0; i <= kLoaded; ++i) {
        gKeys[i] = RandomKey(rng);
        if (i > 0 && rng() % 5 == 0) {
            gKeys[i] = gKeys[i - 1];
            std::swap(gKeys[i].mWords[0], gKeys[i].mWords[2]);
        }
        gHasState[i] = rng() % 2;
        gSaveByte[i] = uint8_t(rng() % 3);
    }
    for (unsigned i = 0; i < kResources; ++i) {
        F(i).Set(3, "p", 0, &ResourceKeyOut);
        F(i).Set(0, "-p", 0, &ResourceQuery);
        F(i).Data(0, 1);
    }
    F(kLoaded).Set(3, "p", 0, &ResourceKeyOut);
    F(kLoaded).Set(0, "-p", 0, &ResourceQuery);
    F(kLoaded).Data(0, 1);
    if (rng() % 3 == 0) gKeys[kLoaded] = gKeys[rng() % kResources]; // the loaded one may be indexed already
    for (unsigned i = 0; i < 6; ++i) {
        gStateByte[i] = uint8_t(rng() % 3);
        F(kState + i).Set(14, 0, 0, &StateByte);
        F(kState + i).Set(15, 1, 0, &StateSet);
        F(kState + i).Data(15, 1);
    }
    gLoadAnswer = rng() % 3 != 0;
    gTargetByte = uint8_t(rng() % 3);
    F(kManager).Set(3, 0, svcfx::P(&F(kTarget)));
    F(kManager).Set(5, "p-p--", 0, &ManagerLoad);
    F(kManager).Data(5, 2 | 8 | 16);
    F(kManager).Set(16, "p-", 0, &ManagerSave);
    F(kManager).Set(17, "p", rng() % 2 ? 0x3301u : 0x3300u);
    F(kTarget).Set(10, "p", 0, &TargetTake);

    std::memset(gStorage, 0, sizeof(gStorage));
    auto *words = reinterpret_cast<uint32_t *>(gStorage);
    words[0] = svcfx::P(GameAddress<void>(0xda7228));
    words[1] = svcfx::P(GameAddress<void>(0xda7270));
    words[2] = 1;
    Context *c = Object();
    new (&c->Map()) Context::Index();
    static const uint32_t kMasks[] = {0, 1, 3, 0xffffffffu, 0x10, 2};
    for (unsigned i = 0; i < kResources; ++i) {
        if (rng() % 4 == 0) continue;
        Key key = rng() % 7 == 0 ? RandomKey(rng) : gKeys[i];
        Context::Entry &e = c->Map()[key];
        e.mDirty = uint8_t(rng() % 3 == 0 ? 2 : rng() % 2);
        e.mPriority = uint32_t(int32_t(rng() % 7) - 2);
        e.mMask = kMasks[rng() % 6];
        if (gHasState[i]) {
            *e.mState.Out() = &F(kState + i % 6);
            ++F(kState + i % 6).refs;
        }
        *e.mResource.Out() = &F(i);
        ++F(i).refs;
    }
    for (const auto &item : c->Map()) w.indexKeys.push_back(item.first);
    svcfx::AddRange(gStorage, sizeof(gStorage), kObjectTag);
    return w;
}

void Capture(Blob &b)
{
    Context *c = Object();
    const auto *words = reinterpret_cast<const uint32_t *>(gStorage);
    b.push_back(words[2]);
    const uint32_t *m = words + 0x0c / 4;
    b.push_back(m[0]);
    b.push_back(m[2]);
    b.push_back(m[6]);
    b.push_back(m[7]);
    std::map<uint32_t, uint32_t> order;
    uint32_t position = 0;
    for (auto it = c->Map().begin(); it != c->Map().end(); ++it, ++position) {
        for (uint32_t w : it->first.mWords) b.push_back(w);
        const Context::Entry &e = it->second;
        b.push_back(e.mDirty);
        b.push_back(e.mPriority);
        b.push_back(e.mMask);
        b.push_back(Norm(svcfx::P(e.mState.Get())));
        b.push_back(Norm(svcfx::P(e.mResource.Get())));
        order[svcfx::P(&*it)] = position;
    }
    const auto *vecBegin = reinterpret_cast<const uint32_t *>(uintptr_t(m[3]));
    const auto *vecEnd = reinterpret_cast<const uint32_t *>(uintptr_t(m[4]));
    b.push_back(uint32_t(vecEnd - vecBegin));
    for (const uint32_t *p = vecBegin; p != vecEnd; ++p) {
        const auto found = order.find(*p + 8);
        b.push_back(*p == m[1] ? 0xffffu : found == order.end() ? 0xbadu : found->second);
    }
    svcfx::CaptureFakes(b);
}

template <class Call> Blob RunSide(uint32_t seed, Call &&call)
{
    const World w = Build(seed);
    svcfx::gEv.reserve(4096);
    difftest::AllocationCounter heap;
    const uint32_t result = call(w);
    heap.Stop();
    Blob b = svcfx::gEv;
    b.push_back(0xa110c);
    b.push_back(result);
    b.push_back(uint32_t(heap.Blocks()));
    b.push_back(uint32_t(heap.Bytes()));
    Capture(b);
    std::destroy_at(&Object()->Map());
    return b;
}

Key PickKey(std::mt19937 &rng, const World &w)
{
    if (!w.indexKeys.empty() && rng() % 4 != 0) return w.indexKeys[rng() % w.indexKeys.size()];
    if (rng() % 2) return gKeys[rng() % kResources];
    return RandomKey(rng);
}

alignas(16) uint32_t gOut[2];

// Runs `seeds` cases of one function: `call(side, rng, world)` returns the result to compare (al only where that is
// all the original defines). gOut's first word is logged by `out`: 'o' an out pointer (or 0x0badf00d, untouched), 'd'
// data (GetDirty's flag, GetMask's mask).
template <class Call> void Run(difftest::Context &ctx, const char *what, uint32_t seeds, uint32_t salt, Call &&call, char out = 'o')
{
    svcfx::Globals globals;
    globals.Set(kManagerCell, &F(kManager));
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            sides[side] = RunSide(seed, [&](const World &w) -> uint32_t {
                std::mt19937 rng(seed ^ salt);
                gOut[0] = 0x0badf00d;
                gOut[1] = 0;
                svcfx::AddRange(gOut, sizeof(gOut), 0x7d000000);
                const uint32_t r = call(side, rng, w);
                Ev(out == 'd' ? gOut[0] : svcfx::NormOut(gOut[0], 0x0badf00d));
                return r;
            });
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], what, seed)) break;
    }
}

template <class Fn> Fn Orig(difftest::Context &ctx, uint32_t rva) { return ctx.Original<Fn>(rva); }

} // namespace

DIFF_TEST(TSPersistContext_QueryInterface, 0x1fabb0)
{
    Run(ctx, "QueryInterface", 600, 0x1fabb0, [&](int side, std::mt19937 &rng, const World &) -> uint32_t {
        static const uint32_t kIids[] = {0x105231cf, 1, 0x105231ce, 0};
        const uint32_t iid = kIids[rng() % 4];
        void **out = rng() % 6 == 0 ? nullptr : reinterpret_cast<void **>(gOut);
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x1fabb0)(Object(), iid, out)
                          : uint32_t(Object()->QueryInterface(iid, out))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_GetResource, 0x1faaa0)
{
    Run(ctx, "GetResource", 1500, 0x1faaa0, [&](int side, std::mt19937 &rng, const World &w) -> uint32_t {
        const Key key = PickKey(rng, w);
        const uint32_t iid = uint32_t(rng());
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, const Key *, uint32_t, void **)>(ctx, 0x1faaa0)(
                                Object(), &key, iid, reinterpret_cast<void **>(gOut))
                          : uint32_t(Object()->GetResource(&key, iid, reinterpret_cast<void **>(gOut)))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_AddResource, 0x1fa720)
{
    Run(ctx, "AddResource", 1500, 0x1fa720, [&](int side, std::mt19937 &rng, const World &) -> uint32_t {
        const unsigned which = unsigned(rng() % (kResources + 2));
        void *resource = which == kResources ? nullptr : which > kResources ? &F(kLoaded) : &F(which);
        const uint32_t dirty = rng() % 3 == 0 ? 0x1200u : uint32_t(rng() % 3); // only the low byte counts
        const uint32_t priority = uint32_t(rng());
        const uint32_t mask = uint32_t(rng());
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t)>(ctx, 0x1fa720)(
                                Object(), resource, dirty, priority, mask)
                          : uint32_t(Object()->AddResource(resource, uint8_t(dirty), priority, mask))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_RemoveResource, 0x1fabf0)
{
    Run(ctx, "RemoveResource", 1000, 0x1fabf0, [&](int side, std::mt19937 &rng, const World &w) -> uint32_t {
        const Key key = PickKey(rng, w);
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, const Key *)>(ctx, 0x1fabf0)(Object(), &key)
                          : uint32_t(Object()->RemoveResource(&key))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_DeleteResource, 0x1fab80)
{
    Run(ctx, "DeleteResource", 1000, 0x1fab80, [&](int side, std::mt19937 &rng, const World &w) -> uint32_t {
        const Key key = PickKey(rng, w);
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, const Key *)>(ctx, 0x1fab80)(Object(), &key)
                          : uint32_t(Object()->DeleteResource(&key))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_FindIndexedResource, 0x1faa00)
{
    Run(ctx, "FindIndexedResource", 1000, 0x1faa00, [&](int side, std::mt19937 &rng, const World &w) -> uint32_t {
        const Key key = PickKey(rng, w);
        const uint32_t iid = rng() % 3 == 0 ? kStateIid : uint32_t(rng());
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, const Key *, uint32_t, void **)>(ctx, 0x1faa00)(
                                Object(), &key, iid, reinterpret_cast<void **>(gOut))
                          : uint32_t(Object()->FindIndexedResource(&key, iid, reinterpret_cast<void **>(gOut)))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_HasResource, 0x1faa60)
{
    Run(ctx, "HasResource", 1000, 0x1faa60, [&](int side, std::mt19937 &rng, const World &w) -> uint32_t {
        const Key key = PickKey(rng, w);
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, const Key *)>(ctx, 0x1faa60)(Object(), &key)
                          : uint32_t(Object()->HasResource(&key))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_SetDirty, 0x1faf50)
{
    Run(ctx, "SetDirty", 1000, 0x1faf50, [&](int side, std::mt19937 &rng, const World &w) -> uint32_t {
        const Key key = PickKey(rng, w);
        const uint32_t dirty = uint32_t(rng() % 4) | (rng() % 2 ? 0x300u : 0u);
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, const Key *, uint32_t)>(ctx, 0x1faf50)(Object(), &key, dirty)
                          : uint32_t(Object()->SetDirty(&key, dirty))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_GetDirty, 0x1fa990)
{
    Run(ctx, "GetDirty", 1000, 0x1fa990, [&](int side, std::mt19937 &rng, const World &w) -> uint32_t {
        const Key key = PickKey(rng, w);
        auto *out = reinterpret_cast<uint8_t *>(gOut);
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, const Key *, uint8_t *)>(ctx, 0x1fa990)(Object(), &key, out)
                          : uint32_t(Object()->GetDirty(&key, out))) & 0xff;
    }, 'd');
}

DIFF_TEST(TSPersistContext_SetMask, 0x1faf00)
{
    Run(ctx, "SetMask", 800, 0x1faf00, [&](int side, std::mt19937 &rng, const World &w) -> uint32_t {
        const Key key = PickKey(rng, w);
        const uint32_t mask = uint32_t(rng());
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, const Key *, uint32_t)>(ctx, 0x1faf00)(Object(), &key, mask)
                          : uint32_t(Object()->SetMask(&key, mask))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_GetMask, 0x1fa940)
{
    Run(ctx, "GetMask", 800, 0x1fa940, [&](int side, std::mt19937 &rng, const World &w) -> uint32_t {
        const Key key = PickKey(rng, w);
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, const Key *, uint32_t *)>(ctx, 0x1fa940)(Object(), &key, gOut)
                          : uint32_t(Object()->GetMask(&key, gOut))) & 0xff;
    }, 'd');
}

DIFF_TEST(TSPersistContext_Save, 0x1fad60)
{
    gStateArgByte = true;
    Run(ctx, "Save", 2000, 0x1fad60, [&](int side, std::mt19937 &rng, const World &) -> uint32_t {
        const uint32_t force = rng() % 4 == 0 ? 1u : rng() % 3 == 0 ? 0x100u : 0u;
        static const uint32_t kMasks[] = {0, 1, 3, 0xffffffffu, 0x10, 2, 0x13};
        const uint32_t mask = kMasks[rng() % 7];
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(ctx, 0x1fad60)(Object(), force, mask)
                          : uint32_t(Object()->Save(uint8_t(force), mask))) & 0xff;
    });
    gStateArgByte = false;
}

DIFF_TEST(TSPersistContext_Clear, 0x1fa7f0)
{
    Run(ctx, "Clear", 600, 0x1fa7f0, [&](int side, std::mt19937 &, const World &) -> uint32_t {
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *)>(ctx, 0x1fa7f0)(Object())
                          : uint32_t(Object()->Clear())) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_ReleaseResources, 0x1fafb0)
{
    Run(ctx, "ReleaseResources", 1500, 0x1fafb0, [&](int side, std::mt19937 &rng, const World &) -> uint32_t {
        static const uint32_t kMasks[] = {0, 1, 3, 0xffffffffu, 0x10, 2, 0x13};
        const uint32_t mask = kMasks[rng() % 7];
        return (side == 0 ? Orig<uint32_t(__thiscall *)(void *, uint32_t)>(ctx, 0x1fafb0)(Object(), mask)
                          : uint32_t(Object()->ReleaseResources(mask))) & 0xff;
    });
}

DIFF_TEST(TSPersistContext_DeletingDestructor, 0x1fa6e0)
{
    // The built object is moved to the heap (the index holds no pointer to itself) and destroyed with flags 0, 1 or
    // 0x100. Compared: the log (each entry's releases), the result, the number and sorted sizes of the blocks freed,
    // whether the object was freed, its vtable words when it was not, every fake's count.
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed * 5 + 3);
        const uint32_t flags = rng() % 2 ? 1u : rng() % 2 ? 0x100u : 0u;
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            Build(seed);
            auto *object = static_cast<Context *>(::operator new(Context::kObjectSize));
            std::memcpy(static_cast<void *>(object), gStorage, Context::kObjectSize);
            reinterpret_cast<uint32_t *>(object)[0] = 0x0b0b0b0b; // both vtables are set anew
            reinterpret_cast<uint32_t *>(object)[1] = 0x0c0c0c0c;
            svcfx::gEv.reserve(1024); // the log's own growth would free its old buffer inside the recording
            difftest::FreeRecorder frees;
            Context *result = side == 0 ? ctx.Original<Context *(__thiscall *)(void *, uint32_t)>(0x1fa6e0)(object, flags)
                                        : object->DeletingDestructor(flags);
            frees.Stop();
            Blob &b = sides[side];
            b = svcfx::gEv;
            b.push_back(result == object);
            b.push_back(uint32_t(frees.Count()));
            size_t sizes[64];
            const size_t n = frees.SortedSizes(sizes, 64);
            for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(frees.Freed(object));
            if ((flags & 1) == 0) {
                const auto *words = reinterpret_cast<const uint32_t *>(object);
                b.push_back(words[0] - svcfx::P(GameAddress<void>(Context::kVTable)));
                b.push_back(words[1]);
                ::operator delete(object);
            }
            svcfx::CaptureFakes(b);
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "DeletingDestructor", seed)) break;
    }
}
