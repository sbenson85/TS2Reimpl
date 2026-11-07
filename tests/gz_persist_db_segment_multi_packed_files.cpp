// Differential tests for the cGZPersistDBSegmentMultiPackedFiles slots reimplemented so far (field map in
// src/gz/cGZPersistDBSegmentMultiPackedFiles.h): QueryInterface, Shutdown, GetPath, SetPath, SetSegmentID,
// slot 44 (Field7C), slot 45 (SumItems), and the subobject's slots 7 (ItemCount) and 8 (ItemAt).
//
// Each run gets a fresh object built from the same seed. Its vtables are copies of the original ones with
// recorders in the slots the functions call: the primary's AddRef (slot 1) and Close (slot 9), the embedded
// path object's slot 16, the output object's slot 16, and the items' slot 19. The base's cache and namespace
// fakes record their Release. Three runs are compared on the return value, the recorded calls (kind, object,
// the normalised arguments) and the final bytes of the object, the output, the fakes, the argument buffer and
// the item array:
//   original : the original code; the subobject's slots 7 and 8 are the original's
//   direct   : our function called directly; the subobject's slots 7 and 8 are ours (where the function calls them)
//   via slot : our function is also written into the vtable slot it is reached through, and called through it
#include "difftest.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBSegment.h"
#include "gz/cGZPersistDBSegmentMultiPackedFiles.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using QiFn = bool(__thiscall *)(void *, uint32_t, void **);
using VoidFn = void(__thiscall *)(void *);
using BoolFn = bool(__thiscall *)(void *);
using VoidPtrFn = void(__thiscall *)(void *, void *);
using BoolPtrFn = bool(__thiscall *)(void *, void *);
using BoolU32PtrFn = bool(__thiscall *)(void *, const uint32_t *);
using U32ArgFn = uint32_t(__thiscall *)(void *, uint32_t);
using U32Fn = uint32_t(__thiscall *)(void *);
using PtrArgFn = void *(__thiscall *)(void *, uint32_t);
using U32U32Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);

constexpr size_t kItems = 4;

// Recorded events: kind, then the normalised arguments. Addresses differ between the runs, so every pointer
// that names one of the run's objects is replaced by its id.
std::vector<uint32_t> gTrace;
std::vector<std::pair<uint32_t, uint32_t>> gIds; // address, id
uint32_t gRet = 0;                               // what the recorders return

uint32_t Addr(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// The address of a member function (single inheritance: the pointer is one word).
template <typename T> void *MemberFn(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

uint32_t Id(uint32_t value)
{
    for (const auto &e : gIds)
        if (e.first == value) return e.second;
    return value;
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(Id(Addr(self)));
}

void Arg(uint32_t value)
{
    gTrace.push_back(Id(value));
}

uint32_t Load32(const void *base, size_t off)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(base) + off, sizeof(v));
    return v;
}

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

// Recorders. __fastcall takes ecx and edx as a __thiscall callee does, and pops its stack arguments.
uint32_t __fastcall SelfAddRef(void *self, void *)
{
    Note(1, self);
    return gRet;
}

void __fastcall SelfClose(void *self, void *)
{
    Note(9, self);
}

uint32_t __fastcall ObjAddRef(void *self, void *)
{
    Note(4, self);
    return gRet;
}

uint32_t __fastcall ObjRelease(void *self, void *)
{
    Note(5, self);
    return 0;
}

void __fastcall PathSet(void *self, void *, void *path)
{
    Note(16, self);
    Arg(Addr(path));
}

void __fastcall OutSet(void *self, void *, void *arg)
{
    Note(17, self);
    Arg(Addr(arg));
}

// An item's slot 19: records the two arguments and returns the word at +4 of the item plus gRet.
uint32_t __fastcall ItemSum(void *self, void *, uint32_t a, uint32_t b)
{
    Note(19, self);
    Arg(a);
    Arg(b);
    return Load32(self, 4) + gRet;
}

struct World {
    alignas(8) uint8_t self[0x80];
    alignas(8) uint8_t out[0x20];
    alignas(8) uint8_t cache[0x20];
    alignas(8) uint8_t ns[0x20];
    alignas(8) uint8_t items[kItems][0x10];
    alignas(8) uint8_t arg[0x10];
    void *itemPtrs[kItems];
    void *selfVT[50];
    void *subVT[11];
    void *pathVT[17];
    void *lockVT[3];
    void *objVT[3];
    void *outVT[17];
    void *itemVT[20];
};

// How a world is set up: which slots of which table hold our functions, and the item array.
struct Options {
    bool ourSub = false;      // our ItemCount and ItemAt in the subobject's table (slots 7 and 8)
    int primarySlot = -1;     // our function in this primary slot ...
    void *primaryFn = nullptr; // ... and its address
    int subSlot = -1;         // our function in this subobject slot ...
    void *subFn = nullptr;    // ... and its address
    int items = -1;           // -1: random begin and end words; otherwise an array of this many items
};

void Build(World &w, const difftest::Context &ctx, uint32_t seed, const Options &o)
{
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.self, sizeof(w.self));
    fill(w.out, sizeof(w.out));
    fill(w.cache, sizeof(w.cache));
    fill(w.ns, sizeof(w.ns));
    fill(w.items, sizeof(w.items));
    fill(w.arg, sizeof(w.arg));

    std::memcpy(w.selfVT, ctx.image.At(cGZPersistDBSegmentMultiPackedFiles::kVTable), sizeof(w.selfVT));
    std::memcpy(w.subVT, ctx.image.At(cGZPersistDBSegmentMultiPackedFiles::kSubVTable), sizeof(w.subVT));
    w.selfVT[1] = reinterpret_cast<void *>(&SelfAddRef);
    w.selfVT[cGZPersistDBSegmentMultiPackedFiles::kCloseSlot] = reinterpret_cast<void *>(&SelfClose);
    if (o.primarySlot >= 0) w.selfVT[o.primarySlot] = o.primaryFn;
    if (o.ourSub) {
        w.subVT[7] = MemberFn(&MultiPackedFilesSegmentView::ItemCount);
        w.subVT[8] = MemberFn(&MultiPackedFilesSegmentView::ItemAt);
    }
    if (o.subSlot >= 0) w.subVT[o.subSlot] = o.subFn;

    std::memset(w.pathVT, 0, sizeof(w.pathVT));
    std::memset(w.lockVT, 0, sizeof(w.lockVT));
    std::memset(w.objVT, 0, sizeof(w.objVT));
    std::memset(w.outVT, 0, sizeof(w.outVT));
    std::memset(w.itemVT, 0, sizeof(w.itemVT));
    w.pathVT[16] = reinterpret_cast<void *>(&PathSet);
    w.outVT[16] = reinterpret_cast<void *>(&OutSet);
    w.itemVT[19] = reinterpret_cast<void *>(&ItemSum);
    w.objVT[1] = reinterpret_cast<void *>(&ObjAddRef);
    w.objVT[2] = reinterpret_cast<void *>(&ObjRelease);

    // The base (cGZPersistDBSegment) at +0: vptr, count, lock, cache, namespace.
    static constexpr int32_t kCounts[] = {1, 1, 2, 3, 0, -5};
    Put(w.self, 0x00, Addr(w.selfVT));
    Put(w.self, 0x04, kCounts[rng() % 6]);
    Put(w.self, 0x08, Addr(w.lockVT));
    Put(w.self, 0x10, rng() % 3 ? Addr(w.cache) : 0u);
    Put(w.self, 0x14, rng() % 3 ? Addr(w.ns) : 0u);
    // The subobject's vptr, the embedded path object's vptr, the open flag, the state and ID, the dword.
    Put(w.self, 0x18, Addr(w.subVT));
    Put(w.self, 0x24, Addr(w.pathVT));
    static constexpr uint8_t kOpen[] = {0, 0, 1, 0x80};
    Put(w.self, 0x64, kOpen[rng() % 4]);
    Put(w.self, 0x1c, static_cast<uint8_t>(rng()));
    Put(w.self, 0x20, static_cast<uint32_t>(rng()));
    Put(w.self, 0x7c, static_cast<uint32_t>(rng()));

    std::memset(w.itemPtrs, 0, sizeof(w.itemPtrs));
    if (o.items < 0) {
        Put(w.self, 0x68, static_cast<uint32_t>(rng()));
        Put(w.self, 0x6c, static_cast<uint32_t>(rng()));
    } else {
        for (size_t i = 0; i < kItems; ++i) {
            w.itemPtrs[i] = w.items[i];
            Put(w.items[i], 0x00, Addr(w.itemVT));
        }
        Put(w.self, 0x68, Addr(w.itemPtrs));
        Put(w.self, 0x6c, Addr(w.itemPtrs + o.items));
    }

    Put(w.out, 0x00, Addr(w.outVT));
    Put(w.cache, 0x00, Addr(w.objVT));
    Put(w.ns, 0x00, Addr(w.objVT));
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace;
};

void SetIds(const World &w)
{
    gIds = {{Addr(w.self), 0x100},          {Addr(w.ns), 0x101},          {Addr(w.cache), 0x102},
            {Addr(w.out), 0x104},           {Addr(w.arg), 0x106},         {Addr(w.self + 0x18), 0x118},
            {Addr(w.self + 0x24), 0x124},   {Addr(w.selfVT), 0x110},      {Addr(w.subVT), 0x111},
            {Addr(w.pathVT), 0x112},        {Addr(w.lockVT), 0x113},      {Addr(w.objVT), 0x114},
            {Addr(w.outVT), 0x115},         {Addr(w.itemVT), 0x116},      {Addr(w.itemPtrs), 0x140}};
    for (size_t i = 0; i < kItems; ++i) gIds.push_back({Addr(w.items[i]), 0x130 + static_cast<uint32_t>(i)});
    // The item array's end pointer is one past the last element (up to kItems), and the object stores it.
    for (size_t k = 0; k <= kItems; ++k) gIds.push_back({Addr(w.itemPtrs + k), 0x150 + static_cast<uint32_t>(k)});
}

// The object's words, the output, the fakes, the argument buffer and the item array, normalised.
void Snapshot(const World &w, std::vector<uint32_t> &t)
{
    for (size_t i = 0; i < sizeof(w.self); i += 4) t.push_back(Id(Load32(w.self, i)));
    for (size_t i = 0; i < sizeof(w.out); i += 4) t.push_back(Id(Load32(w.out, i)));
    for (size_t i = 0; i < sizeof(w.cache); i += 4) t.push_back(Id(Load32(w.cache, i)));
    for (size_t i = 0; i < sizeof(w.ns); i += 4) t.push_back(Id(Load32(w.ns, i)));
    for (size_t i = 0; i < sizeof(w.arg); i += 4) t.push_back(Id(Load32(w.arg, i)));
    for (size_t i = 0; i < sizeof(w.items); i += 4) t.push_back(Id(Load32(w.items, i)));
    for (size_t i = 0; i < kItems; ++i) t.push_back(Id(Load32(w.itemPtrs, i * sizeof(void *))));
}

// Where the world lives. The three runs share this one world, rebuilt from the seed before each (see Compare), so
// they see it at the same address and Id is the same function on every side: equal words stay equal and different
// words stay different, whatever the call wrote. Three worlds at three addresses made Id depend on the heap layout:
// a word holding random bits that equal one world's address was renamed in that world only. A check of the states
// before the call (rebuilding the set elsewhere when they differed) could not see a word the call changes: lane NB,
// Shutdown seed 4106028625, a young shard's heap: the state word at +0x1c was 0x0ac96043 (random bytes), Shutdown
// cleared its low byte, and 0x0ac96000 was the address of the "ours" world's subobject, so it read as 0x118 there.
World &SharedWorld()
{
    static World *const w = new World;
    return *w;
}

template <typename Call> Outcome Run(World &w, Call call)
{
    SetIds(w);
    gTrace.clear();
    Outcome out;
    out.ret = call(w);
    Snapshot(w, gTrace);
    out.trace = gTrace;
    return out;
}

// Where two traces first differ, as text for the failure message (the length when one is a prefix of the other).
std::string FirstDifference(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
{
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "first difference at word %zu: original 0x%x, ours 0x%x", i, a[i], b[i]);
            return buf;
        }
    }
    return "same words, different lengths";
}

// Runs the original and both forms of ours on identical worlds and compares them.
template <typename OrigCall, typename OursCall, typename ViaCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, const Options &plain, const Options &ours,
             const Options &via, OrigCall origCall, OursCall oursCall, ViaCall viaCall)
{
    World &w = SharedWorld();
    Build(w, ctx, seed, plain);
    Outcome oa = Run(w, origCall);
    Build(w, ctx, seed, ours);
    Outcome ob = Run(w, oursCall);
    Build(w, ctx, seed, via);
    Outcome oc = Run(w, viaCall);
    ++ctx.cases;
    if (oa.ret != ob.ret || oa.trace != ob.trace) {
        ctx.Fail("%s seed %u: direct: original returned %u (%zu words), ours %u (%zu); %s", name, seed, oa.ret,
                 oa.trace.size(), ob.ret, ob.trace.size(), FirstDifference(oa.trace, ob.trace).c_str());
    }
    if (oa.ret != oc.ret || oa.trace != oc.trace) {
        ctx.Fail("%s seed %u: via slot: original returned %u (%zu words), ours %u (%zu); %s", name, seed, oa.ret,
                 oa.trace.size(), oc.ret, oc.trace.size(), FirstDifference(oa.trace, oc.trace).c_str());
    }
}

cGZPersistDBSegmentMultiPackedFiles *Self(World &w)
{
    return reinterpret_cast<cGZPersistDBSegmentMultiPackedFiles *>(w.self);
}

MultiPackedFilesSegmentView *View(World &w)
{
    return reinterpret_cast<MultiPackedFilesSegmentView *>(w.self + 0x18);
}

} // namespace

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_QueryInterface, 0x9b9950)
{
    std::mt19937 rng(0x6a1);
    auto orig = ctx.Original<QiFn>(0x9b9950);
    Options plain;
    Options via;
    via.primarySlot = 0;
    via.primaryFn = MemberFn(&cGZPersistDBSegmentMultiPackedFiles::QueryInterface);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        // 0xe90f85f3 is ours; 0x656b8f0c and 1 go to the base; the rest are refused.
        static constexpr uint32_t kIds[] = {0xe90f85f3, 0x656b8f0c, 1, 2, 0xe90f85f4, 0};
        uint32_t riid = rng() % 3 ? kIds[rng() % 6] : rng();
        Compare(
            ctx, "QueryInterface", seed, plain, plain, via,
            [&](World &w) { return uint32_t(orig(w.self, riid, reinterpret_cast<void **>(w.arg))); },
            [&](World &w) { return uint32_t(Self(w)->QueryInterface(riid, reinterpret_cast<void **>(w.arg))); },
            [&](World &w) {
                return uint32_t(VirtualSlot<QiFn>(w.self, 0)(w.self, riid, reinterpret_cast<void **>(w.arg)));
            });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_Shutdown, 0x9b9a60)
{
    auto orig = ctx.Original<BoolFn>(0x9b9a60);
    Options plain;
    Options via;
    via.primarySlot = 4;
    via.primaryFn = MemberFn(&cGZPersistDBSegmentMultiPackedFiles::Shutdown);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = std::mt19937(i * 2654435761u)();
        gRet = seed;
        Compare(
            ctx, "Shutdown", seed, plain, plain, via,
            [&](World &w) { return uint32_t(orig(w.self)); },
            [&](World &w) { return uint32_t(Self(w)->Shutdown()); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolFn>(w.self, 4)(w.self)); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_GetPath, 0x9b9630)
{
    auto orig = ctx.Original<VoidPtrFn>(0x9b9630);
    Options plain;
    Options via;
    via.primarySlot = 11;
    via.primaryFn = MemberFn(&cGZPersistDBSegmentMultiPackedFiles::GetPath);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = std::mt19937(i * 40503u + 7)();
        Compare(
            ctx, "GetPath", seed, plain, plain, via,
            [&](World &w) { orig(w.self, w.out); return 0u; },
            [&](World &w) { Self(w)->GetPath(w.out); return 0u; },
            [&](World &w) { VirtualSlot<VoidPtrFn>(w.self, 11)(w.self, w.out); return 0u; });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_SetPath, 0x9b9a20)
{
    std::mt19937 rng(0x5e7);
    auto orig = ctx.Original<BoolPtrFn>(0x9b9a20);
    Options plain;
    Options via;
    via.primarySlot = 12;
    via.primaryFn = MemberFn(&cGZPersistDBSegmentMultiPackedFiles::SetPath);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        uint32_t k = rng() % 3;
        auto pick = [k](World &w) -> void * {
            switch (k) {
            case 0: return w.arg;
            case 1: return w.out;
            default: return nullptr;
            }
        };
        Compare(
            ctx, "SetPath", seed, plain, plain, via,
            [&](World &w) { return uint32_t(orig(w.self, pick(w))); },
            [&](World &w) { return uint32_t(Self(w)->SetPath(pick(w))); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolPtrFn>(w.self, 12)(w.self, pick(w))); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_SetSegmentID, 0x9b9a50)
{
    auto orig = ctx.Original<BoolU32PtrFn>(0x9b9a50);
    Options plain;
    Options via;
    via.primarySlot = 16;
    via.primaryFn = MemberFn(&cGZPersistDBSegmentMultiPackedFiles::SetSegmentID);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = std::mt19937(i * 9973u + 1)();
        Compare(
            ctx, "SetSegmentID", seed, plain, plain, via,
            [&](World &w) { return uint32_t(orig(w.self, reinterpret_cast<const uint32_t *>(w.arg))); },
            [&](World &w) {
                return uint32_t(Self(w)->SetSegmentID(reinterpret_cast<const uint32_t *>(w.arg)));
            },
            [&](World &w) {
                return uint32_t(VirtualSlot<BoolU32PtrFn>(w.self, 16)(w.self, reinterpret_cast<const uint32_t *>(w.arg)));
            });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_Field7C, 0x9b9400)
{
    std::mt19937 rng(0x7c);
    auto orig = ctx.Original<U32ArgFn>(0x9b9400);
    Options plain;
    Options via;
    via.primarySlot = 44;
    via.primaryFn = MemberFn(&cGZPersistDBSegmentMultiPackedFiles::Field7C);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        uint32_t a = rng();
        Compare(
            ctx, "Field7C", seed, plain, plain, via,
            [&](World &w) { return orig(w.self, a); },
            [&](World &w) { return Self(w)->Field7C(a); },
            [&](World &w) { return VirtualSlot<U32ArgFn>(w.self, 44)(w.self, a); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_SumItems, 0x9b9460)
{
    std::mt19937 rng(0x45);
    auto orig = ctx.Original<U32U32Fn>(0x9b9460);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), b = rng();
        int n = static_cast<int>(rng() % (kItems + 1));
        // The original reaches the count through the subobject's slot 7: the reimplementation of the
        // count is installed there in the other two runs (plain has the original's).
        Options plain;
        plain.items = n;
        Options ours;
        ours.items = n;
        ours.ourSub = true;
        Options via;
        via.items = n;
        via.ourSub = true;
        via.primarySlot = 45;
        via.primaryFn = MemberFn(&cGZPersistDBSegmentMultiPackedFiles::SumItems);
        Compare(
            ctx, "SumItems", seed, plain, ours, via,
            [&](World &w) { return orig(w.self, a, b); },
            [&](World &w) { return Self(w)->SumItems(a, b); },
            [&](World &w) { return VirtualSlot<U32U32Fn>(w.self, 45)(w.self, a, b); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_vf24_7, 0x9b9660)
{
    std::mt19937 rng(0x67);
    auto orig = ctx.Original<U32Fn>(0x9b9660);
    Options plain;
    Options via;
    via.subSlot = MultiPackedFilesSegmentView::kCountSlot;
    via.subFn = MemberFn(&MultiPackedFilesSegmentView::ItemCount);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        // Random begin and end words: the spans include ends below the begin, which the shift makes negative.
        Compare(
            ctx, "ItemCount", seed, plain, plain, via,
            [&](World &w) { return orig(w.self + 0x18); },
            [&](World &w) { return View(w)->ItemCount(); },
            [&](World &w) { return VirtualSlot<U32Fn>(w.self + 0x18, 7)(w.self + 0x18); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_vf24_8, 0x9b9650)
{
    std::mt19937 rng(0x68);
    auto orig = ctx.Original<PtrArgFn>(0x9b9650);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        int n = 1 + static_cast<int>(rng() % kItems); // an index is only read when there is an item
        uint32_t index = rng() % static_cast<uint32_t>(n);
        Options plain;
        plain.items = n;
        Options via;
        via.items = n;
        via.subSlot = MultiPackedFilesSegmentView::kItemSlot;
        via.subFn = MemberFn(&MultiPackedFilesSegmentView::ItemAt);
        Compare(
            ctx, "ItemAt", seed, plain, plain, via,
            [&](World &w) { return Id(Addr(orig(w.self + 0x18, index))); },
            [&](World &w) { return Id(Addr(View(w)->ItemAt(index))); },
            [&](World &w) { return Id(Addr(VirtualSlot<PtrArgFn>(w.self + 0x18, 8)(w.self + 0x18, index))); });
    }
}
