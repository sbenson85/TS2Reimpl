// Differential tests for the cGZPersistDBSegment functions reimplemented so far (field map in
// src/gz/cGZPersistDBSegment.h): QueryInterface, Release, Shutdown, Lock, Unlock, SetSegmentCache, the
// locked forwarders (GetRecordCount, GetRecordSize, GetRecordChangeID, SetNamespace, GetNamespace), the
// unlocked GetSegmentChangeID, vf34, vf47, vf49, and the record operations (GetResourceKeyList, TestForRecord,
// AbortRecord, CloseRecord, CreateNewRecord, DeleteRecord, OpenRecord, ReadRecord, WriteRecord), which lock,
// call one pure slot 35..45 with their arguments, and unlock.
//
// Each run gets a fresh object built from the same seed: a copy of the primary vtable whose slots that
// the functions call are recorders (AddRef, the deleting slot 33, the pure slots 35..49), a lock whose
// Lock and Unlock record calls, and two refcounted fakes for the cache and namespace objects (AddRef and
// Release record). Three runs are compared on the return value, the recorded calls (kind, object, the
// arguments), and the final bytes of the object, the out buffer and the fakes:
//   original  : the original code, the vtable's vf34 is the original's
//   direct    : our member function, the vtable's vf34 is ours
//   via slot  : our function is also written into the vtable slot it is reached through, and called
//               through that slot, as the original's callers do
// The original's vf34 is the only other code the object reaches; its own tests cover it separately.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBSegment.h"
#include "rz/cRZCriticalSection.h"

#include <Windows.h>

#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <random>
#include <utility>
#include <vector>

namespace {

using VoidFn = void(__thiscall *)(void *);
using BoolFn = bool(__thiscall *)(void *);
using U32Fn = uint32_t(__thiscall *)(void *);
using U32ArgFn = uint32_t(__thiscall *)(void *, uint32_t);
using BoolArgFn = bool(__thiscall *)(void *, uint32_t);
using BoolArg2Fn = bool(__thiscall *)(void *, uint32_t, uint32_t *);
using VoidPtrFn = void(__thiscall *)(void *, void *);
using BoolPtrFn = bool(__thiscall *)(void *, void **);
using QiFn = bool(__thiscall *)(void *, uint32_t, void **);
using ReleaseOrigFn = uint32_t(__thiscall *)(void *);
using BoolArg2Val = bool(__thiscall *)(void *, uint32_t, uint32_t);
using BoolArg3Val = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using U32Arg2Val = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using U32Arg3Val = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

constexpr size_t kObjBytes = 0x40; // the object is 0x18 bytes; the rest is canary space
constexpr size_t kFakeBytes = 0x20;
constexpr size_t kOutBytes = 0x10;

// Recorded events: kind, then the normalised arguments. Object and vtable addresses differ between
// the runs, so every pointer that names one of the run's objects is replaced by its id.
std::vector<uint32_t> gTrace;
std::vector<std::pair<uint32_t, uint32_t>> gIds; // address, id
uint32_t gRet = 0;                               // what the recorders return (bit 0 for bools)

uint32_t Addr(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

template <typename T> uint32_t MemberAddr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return Addr(p);
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

// Recorders in the object's vtable. __fastcall pops its stack arguments like a __thiscall callee does;
// the second parameter takes edx.
uint32_t __fastcall SelfAddRef(void *self, void *)
{
    Note(1, self);
    return gRet;
}

void __fastcall SelfDelete(void *self, void *, uint32_t flags)
{
    Note(33, self);
    Arg(flags);
}

bool __fastcall Slot35(void *self, void *, uint32_t a)
{
    Note(35, self);
    Arg(a);
    return gRet & 1;
}

uint32_t __fastcall Slot36(void *self, void *, uint32_t a)
{
    Note(36, self);
    Arg(a);
    return gRet;
}

uint32_t __fastcall Slot44(void *self, void *, uint32_t a)
{
    Note(44, self);
    Arg(a);
    return gRet;
}

uint32_t __fastcall Slot46(void *self, void *)
{
    Note(46, self);
    return gRet;
}

bool __fastcall Slot47(void *self, void *, uint32_t a, uint32_t b)
{
    Note(47, self);
    Arg(a);
    Arg(b);
    return gRet & 2;
}

void __fastcall Slot48(void *self, void *, void *ns)
{
    Note(48, self);
    Arg(Addr(ns));
}

bool __fastcall Slot49(void *self, void *, void **out)
{
    Note(49, self);
    Arg(Addr(out));
    return gRet & 4;
}

// Record recorders for the pure slots 37..43 and 45 (35 is Slot35 above). Each records its kind and
// arguments and returns gRet (bool results take bit 0).
template <uint32_t Slot> bool __fastcall RecBool1(void *self, void *, uint32_t a)
{
    Note(Slot, self);
    Arg(a);
    return gRet & 1;
}

template <uint32_t Slot> bool __fastcall RecBool2(void *self, void *, uint32_t a, uint32_t b)
{
    Note(Slot, self);
    Arg(a);
    Arg(b);
    return gRet & 1;
}

template <uint32_t Slot> bool __fastcall RecBool3(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Note(Slot, self);
    Arg(a);
    Arg(b);
    Arg(c);
    return gRet & 1;
}

template <uint32_t Slot> uint32_t __fastcall RecU32_2(void *self, void *, uint32_t a, uint32_t b)
{
    Note(Slot, self);
    Arg(a);
    Arg(b);
    return gRet;
}

template <uint32_t Slot> uint32_t __fastcall RecU32_3(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Note(Slot, self);
    Arg(a);
    Arg(b);
    Arg(c);
    return gRet;
}

// Lock object (the cRZCriticalSection at +8): slot 1 and slot 2.
uint32_t __fastcall LockEnter(void *self, void *)
{
    Note(2, self);
    return 0;
}

uint32_t __fastcall LockLeave(void *self, void *)
{
    Note(3, self);
    return 0;
}

// Fake refcounted objects (cache and namespace).
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

struct World {
    alignas(8) uint8_t self[kObjBytes];
    alignas(8) uint8_t cache[kFakeBytes];
    alignas(8) uint8_t ns[kFakeBytes];
    alignas(8) uint8_t other[kFakeBytes];
    alignas(8) uint8_t out[kOutBytes];
    void *selfVT[50];
    void *lockVT[3];
    void *objVT[3];
};

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uint32_t Load32(const uint8_t *base, size_t off)
{
    uint32_t v;
    std::memcpy(&v, base + off, sizeof(v));
    return v;
}

// Address of the k-th candidate pointer argument (cache, other, null, namespace).
void *Pick(World &w, uint32_t k)
{
    switch (k % 4) {
    case 0: return w.cache;
    case 1: return w.other;
    case 2: return nullptr;
    default: return w.ns;
    }
}

// A record argument: a random word, or the address of one of the world's buffers (normalised to an id
// in the trace), chosen by k.
uint32_t ArgVal(World &w, uint32_t k, uint32_t r)
{
    switch (k % 5) {
    case 0: return r;
    case 1: return Addr(w.out);
    case 2: return Addr(w.cache);
    case 3: return Addr(w.self);
    default: return 0;
    }
}

void Build(World &w, const difftest::Context &ctx, uint32_t seed, bool oursVf34, int slot, void *fn)
{
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.self, sizeof(w.self));
    fill(w.cache, sizeof(w.cache));
    fill(w.ns, sizeof(w.ns));
    fill(w.other, sizeof(w.other));
    fill(w.out, sizeof(w.out));

    std::memcpy(w.selfVT, ctx.image.At(cGZPersistDBSegment::kVTable), sizeof(w.selfVT));
    w.selfVT[1] = reinterpret_cast<void *>(&SelfAddRef);
    w.selfVT[33] = reinterpret_cast<void *>(&SelfDelete);
    w.selfVT[35] = reinterpret_cast<void *>(&Slot35);
    w.selfVT[36] = reinterpret_cast<void *>(&Slot36);
    w.selfVT[44] = reinterpret_cast<void *>(&Slot44);
    w.selfVT[46] = reinterpret_cast<void *>(&Slot46);
    w.selfVT[47] = reinterpret_cast<void *>(&Slot47);
    w.selfVT[48] = reinterpret_cast<void *>(&Slot48);
    w.selfVT[49] = reinterpret_cast<void *>(&Slot49);
    w.selfVT[37] = reinterpret_cast<void *>(&RecBool3<37>);
    w.selfVT[38] = reinterpret_cast<void *>(&RecBool2<38>);
    w.selfVT[39] = reinterpret_cast<void *>(&RecBool1<39>);
    w.selfVT[40] = reinterpret_cast<void *>(&RecBool1<40>);
    w.selfVT[41] = reinterpret_cast<void *>(&RecBool1<41>);
    w.selfVT[42] = reinterpret_cast<void *>(&RecU32_3<42>);
    w.selfVT[43] = reinterpret_cast<void *>(&RecBool3<43>);
    w.selfVT[45] = reinterpret_cast<void *>(&RecU32_2<45>);
    if (oursVf34) w.selfVT[34] = reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::vf34));
    if (slot >= 0) w.selfVT[slot] = fn;

    w.lockVT[0] = nullptr;
    w.lockVT[1] = reinterpret_cast<void *>(&LockEnter);
    w.lockVT[2] = reinterpret_cast<void *>(&LockLeave);
    w.objVT[0] = nullptr;
    w.objVT[1] = reinterpret_cast<void *>(&ObjAddRef);
    w.objVT[2] = reinterpret_cast<void *>(&ObjRelease);

    static constexpr int32_t kCounts[] = {1, 1, 2, 3, 0, -5};
    Put(w.self, 0x00, Addr(w.selfVT));
    Put(w.self, 0x04, kCounts[rng() % 6]);
    Put(w.self, 0x08, Addr(w.lockVT));
    Put(w.self, 0x10, rng() % 3 ? Addr(w.cache) : 0u);
    Put(w.self, 0x14, rng() % 3 ? Addr(w.ns) : 0u);
    Put(w.cache, 0x00, Addr(w.objVT));
    Put(w.ns, 0x00, Addr(w.objVT));
    Put(w.other, 0x00, Addr(w.objVT));
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace;
};

void SetIds(const World &w)
{
    gIds = {{Addr(w.self), 0x100},          {Addr(w.self + 8), 0x105}, {Addr(w.ns), 0x101},
            {Addr(w.cache), 0x102},         {Addr(w.other), 0x103},    {Addr(w.out), 0x104},
            {Addr(w.selfVT), 0x110},        {Addr(w.lockVT), 0x111},   {Addr(w.objVT), 0x112}};
}

// The world's state, normalised: the object, the out buffer, and the three fakes.
void Snapshot(const World &w, std::vector<uint32_t> &t)
{
    for (size_t i = 0; i < kObjBytes; i += 4) t.push_back(Id(Load32(w.self, i)));
    for (size_t i = 0; i < kOutBytes; i += 4) t.push_back(Id(Load32(w.out, i)));
    for (const uint8_t *fake : {w.cache, w.ns, w.other})
        for (size_t i = 0; i < kFakeBytes; i += 4) t.push_back(Id(Load32(fake, i)));
}

// Where the world lives. The runs share this one world, rebuilt from the seed before each, so they see it at the
// same address and Id is the same function on every side: equal words stay equal and different words stay
// different, whatever the call wrote. Separate worlds at separate addresses made Id depend on the heap layout (a
// random word equal to one world's address was renamed in that world only; lane NB, tests/
// gz_persist_db_segment_multi_packed_files.cpp).
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

// Runs the original and both forms of ours on identical worlds and compares them.
template <typename OrigCall, typename OursCall, typename ViaCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, int slot, void *fn, OrigCall origCall,
             OursCall oursCall, ViaCall viaCall)
{
    World &w = SharedWorld();
    Build(w, ctx, seed, false, -1, nullptr);
    Outcome oa = Run(w, origCall);
    Build(w, ctx, seed, true, -1, nullptr);
    Outcome ob = Run(w, oursCall);
    Build(w, ctx, seed, true, slot, fn);
    Outcome oc = Run(w, viaCall);
    ++ctx.cases;
    if (oa.ret != ob.ret || oa.trace != ob.trace) {
        ctx.Fail("%s seed %u: direct: original returned %u (%zu words), ours %u (%zu)", name, seed, oa.ret, oa.trace.size(),
                 ob.ret, ob.trace.size());
    }
    if (oa.ret != oc.ret || oa.trace != oc.trace) {
        ctx.Fail("%s seed %u: via slot %d: original returned %u (%zu words), ours %u (%zu)", name, seed, slot, oa.ret,
                 oa.trace.size(), oc.ret, oc.trace.size());
    }
}

template <typename T> T *Self(World &w)
{
    return reinterpret_cast<T *>(w.self);
}

} // namespace

DIFF_TEST(GZPersistDBSegment_QueryInterface, 0x99cf70)
{
    std::mt19937 rng(0x51);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        static constexpr uint32_t kIds[] = {0x656b8f0c, 1, 2, 0x656b8f0d, 0};
        uint32_t riid = rng() % 3 ? kIds[rng() % 5] : rng();
        auto orig = ctx.Original<QiFn>(0x99cf70);
        Compare(
            ctx, "QueryInterface", seed, 0, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::QueryInterface)),
            [&](World &w) { return uint32_t(orig(w.self, riid, reinterpret_cast<void **>(w.out))); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->QueryInterface(riid, reinterpret_cast<void **>(w.out))); },
            [&](World &w) {
                return uint32_t(VirtualSlot<QiFn>(w.self, 0)(w.self, riid, reinterpret_cast<void **>(w.out)));
            });
    }
}

DIFF_TEST(GZPersistDBSegment_Release, 0x99d070)
{
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = std::mt19937(i * 2654435761u)();
        gRet = seed;
        auto orig = ctx.Original<ReleaseOrigFn>(0x99d070);
        Compare(
            ctx, "Release", seed, 2, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::Release)),
            [&](World &w) { return orig(w.self); },
            [&](World &w) { return Self<cGZPersistDBSegment>(w)->Release(); },
            [&](World &w) { return VirtualSlot<U32Fn>(w.self, 2)(w.self); });
    }
}

DIFF_TEST(GZPersistDBSegment_Shutdown, 0x99de70)
{
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = std::mt19937(i * 40503u + 7)();
        auto orig = ctx.Original<BoolFn>(0x99de70);
        Compare(
            ctx, "Shutdown", seed, 4, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::Shutdown)),
            [&](World &w) { return uint32_t(orig(w.self)); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->Shutdown()); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolFn>(w.self, 4)(w.self)); });
    }
}

DIFF_TEST(GZPersistDBSegment_Lock, 0x99cef0)
{
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = std::mt19937(i * 9973u + 1)();
        auto orig = ctx.Original<BoolFn>(0x99cef0);
        Compare(
            ctx, "Lock", seed, 13, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::Lock)),
            [&](World &w) { return uint32_t(orig(w.self)); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->Lock()); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolFn>(w.self, 13)(w.self)); });
    }
}

DIFF_TEST(GZPersistDBSegment_Unlock, 0x99def0)
{
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = std::mt19937(i * 7919u + 3)();
        auto orig = ctx.Original<BoolFn>(0x99def0);
        Compare(
            ctx, "Unlock", seed, 14, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::Unlock)),
            [&](World &w) { return uint32_t(orig(w.self)); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->Unlock()); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolFn>(w.self, 14)(w.self)); });
    }
}

DIFF_TEST(GZPersistDBSegment_SetSegmentCache, 0x1d4540)
{
    std::mt19937 rng(0x77);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        uint32_t k = rng();
        auto orig = ctx.Original<VoidPtrFn>(0x1d4540);
        Compare(
            ctx, "SetSegmentCache", seed, 17, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::SetSegmentCache)),
            [&](World &w) { orig(w.self, Pick(w, k)); return 0u; },
            [&](World &w) { Self<cGZPersistDBSegment>(w)->SetSegmentCache(Pick(w, k)); return 0u; },
            [&](World &w) { VirtualSlot<VoidPtrFn>(w.self, 17)(w.self, Pick(w, k)); return 0u; });
    }
}

DIFF_TEST(GZPersistDBSegment_GetRecordCount, 0x99ce10)
{
    std::mt19937 rng(0x13);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng();
        auto orig = ctx.Original<U32ArgFn>(0x99ce10);
        Compare(
            ctx, "GetRecordCount", seed, 18, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::GetRecordCount)),
            [&](World &w) { return orig(w.self, a); },
            [&](World &w) { return Self<cGZPersistDBSegment>(w)->GetRecordCount(a); },
            [&](World &w) { return VirtualSlot<U32ArgFn>(w.self, 18)(w.self, a); });
    }
}

DIFF_TEST(GZPersistDBSegment_GetRecordSize, 0x99ce40)
{
    std::mt19937 rng(0x29);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng();
        auto orig = ctx.Original<U32ArgFn>(0x99ce40);
        Compare(
            ctx, "GetRecordSize", seed, 21, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::GetRecordSize)),
            [&](World &w) { return orig(w.self, a); },
            [&](World &w) { return Self<cGZPersistDBSegment>(w)->GetRecordSize(a); },
            [&](World &w) { return VirtualSlot<U32ArgFn>(w.self, 21)(w.self, a); });
    }
}

DIFF_TEST(GZPersistDBSegment_GetSegmentChangeID, 0x99ceb0)
{
    std::mt19937 rng(0x31);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        auto orig = ctx.Original<U32Fn>(0x99ceb0);
        Compare(
            ctx, "GetSegmentChangeID", seed, 29, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::GetSegmentChangeID)),
            [&](World &w) { return orig(w.self); },
            [&](World &w) { return Self<cGZPersistDBSegment>(w)->GetSegmentChangeID(); },
            [&](World &w) { return VirtualSlot<U32Fn>(w.self, 29)(w.self); });
    }
}

DIFF_TEST(GZPersistDBSegment_GetRecordChangeID, 0x99cdd0)
{
    std::mt19937 rng(0x41);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t key = rng();
        auto orig = ctx.Original<BoolArg2Fn>(0x99cdd0);
        Compare(
            ctx, "GetRecordChangeID", seed, 30, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::GetRecordChangeID)),
            [&](World &w) { return uint32_t(orig(w.self, key, reinterpret_cast<uint32_t *>(w.out))); },
            [&](World &w) {
                return uint32_t(Self<cGZPersistDBSegment>(w)->GetRecordChangeID(key, reinterpret_cast<uint32_t *>(w.out)));
            },
            [&](World &w) {
                return uint32_t(VirtualSlot<BoolArg2Fn>(w.self, 30)(w.self, key, reinterpret_cast<uint32_t *>(w.out)));
            });
    }
}

DIFF_TEST(GZPersistDBSegment_SetNamespace, 0x99dd70)
{
    std::mt19937 rng(0x53);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        uint32_t k = rng();
        auto orig = ctx.Original<VoidPtrFn>(0x99dd70);
        Compare(
            ctx, "SetNamespace", seed, 31, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::SetNamespace)),
            [&](World &w) { orig(w.self, Pick(w, k)); return 0u; },
            [&](World &w) { Self<cGZPersistDBSegment>(w)->SetNamespace(Pick(w, k)); return 0u; },
            [&](World &w) { VirtualSlot<VoidPtrFn>(w.self, 31)(w.self, Pick(w, k)); return 0u; });
    }
}

DIFF_TEST(GZPersistDBSegment_GetNamespace, 0x99cd90)
{
    std::mt19937 rng(0x61);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        auto orig = ctx.Original<BoolPtrFn>(0x99cd90);
        Compare(
            ctx, "GetNamespace", seed, 32, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::GetNamespace)),
            [&](World &w) { return uint32_t(orig(w.self, reinterpret_cast<void **>(w.out))); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->GetNamespace(reinterpret_cast<void **>(w.out))); },
            [&](World &w) {
                return uint32_t(VirtualSlot<BoolPtrFn>(w.self, 32)(w.self, reinterpret_cast<void **>(w.out)));
            });
    }
}

DIFF_TEST(GZPersistDBSegment_vf34, 0x99bba0)
{
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = std::mt19937(i * 1234567u + 5)();
        gRet = seed;
        auto orig = ctx.Original<VoidFn>(0x99bba0);
        Compare(
            ctx, "vf34", seed, 34, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::vf34)),
            [&](World &w) { orig(w.self); return 0u; },
            [&](World &w) { Self<cGZPersistDBSegment>(w)->vf34(); return 0u; },
            [&](World &w) { VirtualSlot<VoidFn>(w.self, 34)(w.self); return 0u; });
    }
}

DIFF_TEST(GZPersistDBSegment_vf47, 0x99bb70)
{
    std::mt19937 rng(0x71);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t key = rng();
        auto orig = ctx.Original<BoolArg2Fn>(0x99bb70);
        Compare(
            ctx, "vf47", seed, 47, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::vf47)),
            [&](World &w) { return uint32_t(orig(w.self, key, reinterpret_cast<uint32_t *>(w.out))); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->vf47(key, reinterpret_cast<uint32_t *>(w.out))); },
            [&](World &w) {
                return uint32_t(VirtualSlot<BoolArg2Fn>(w.self, 47)(w.self, key, reinterpret_cast<uint32_t *>(w.out)));
            });
    }
}

DIFF_TEST(GZPersistDBSegment_vf49, 0x99bb50)
{
    std::mt19937 rng(0x81);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        auto orig = ctx.Original<BoolPtrFn>(0x99bb50);
        Compare(
            ctx, "vf49", seed, 49, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::vf49)),
            [&](World &w) { return uint32_t(orig(w.self, reinterpret_cast<void **>(w.out))); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->vf49(reinterpret_cast<void **>(w.out))); },
            [&](World &w) {
                return uint32_t(VirtualSlot<BoolPtrFn>(w.self, 49)(w.self, reinterpret_cast<void **>(w.out)));
            });
    }
}

DIFF_TEST(GZPersistDBSegment_GetResourceKeyList, 0x99ce70)
{
    std::mt19937 rng(0x91);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), b = rng(), k1 = rng(), k2 = rng();
        auto orig = ctx.Original<U32Arg2Val>(0x99ce70);
        Compare(
            ctx, "GetResourceKeyList", seed, 19, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::GetResourceKeyList)),
            [&](World &w) { return orig(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b)); },
            [&](World &w) { return Self<cGZPersistDBSegment>(w)->GetResourceKeyList(ArgVal(w, k1, a), ArgVal(w, k2, b)); },
            [&](World &w) { return VirtualSlot<U32Arg2Val>(w.self, 19)(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b)); });
    }
}

DIFF_TEST(GZPersistDBSegment_TestForRecord, 0x99deb0)
{
    std::mt19937 rng(0xa1);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), k1 = rng();
        auto orig = ctx.Original<BoolArgFn>(0x99deb0);
        Compare(
            ctx, "TestForRecord", seed, 20, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::TestForRecord)),
            [&](World &w) { return uint32_t(orig(w.self, ArgVal(w, k1, a))); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->TestForRecord(ArgVal(w, k1, a))); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolArgFn>(w.self, 20)(w.self, ArgVal(w, k1, a))); });
    }
}

DIFF_TEST(GZPersistDBSegment_AbortRecord, 0x99ba10)
{
    std::mt19937 rng(0xa3);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), k1 = rng();
        auto orig = ctx.Original<BoolArgFn>(0x99ba10);
        Compare(
            ctx, "AbortRecord", seed, 25, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::AbortRecord)),
            [&](World &w) { return uint32_t(orig(w.self, ArgVal(w, k1, a))); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->AbortRecord(ArgVal(w, k1, a))); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolArgFn>(w.self, 25)(w.self, ArgVal(w, k1, a))); });
    }
}

DIFF_TEST(GZPersistDBSegment_CloseRecord, 0x99ba90)
{
    std::mt19937 rng(0xa5);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), k1 = rng();
        auto orig = ctx.Original<BoolArgFn>(0x99ba90);
        Compare(
            ctx, "CloseRecord", seed, 24, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::CloseRecord)),
            [&](World &w) { return uint32_t(orig(w.self, ArgVal(w, k1, a))); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->CloseRecord(ArgVal(w, k1, a))); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolArgFn>(w.self, 24)(w.self, ArgVal(w, k1, a))); });
    }
}

DIFF_TEST(GZPersistDBSegment_CreateNewRecord, 0x99bad0)
{
    std::mt19937 rng(0xa7);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), b = rng(), k1 = rng(), k2 = rng();
        auto orig = ctx.Original<BoolArg2Val>(0x99bad0);
        Compare(
            ctx, "CreateNewRecord", seed, 23, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::CreateNewRecord)),
            [&](World &w) { return uint32_t(orig(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b))); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->CreateNewRecord(ArgVal(w, k1, a), ArgVal(w, k2, b))); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolArg2Val>(w.self, 23)(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b))); });
    }
}

DIFF_TEST(GZPersistDBSegment_DeleteRecord, 0x99bb10)
{
    std::mt19937 rng(0xa9);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), k1 = rng();
        auto orig = ctx.Original<BoolArgFn>(0x99bb10);
        Compare(
            ctx, "DeleteRecord", seed, 26, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::DeleteRecord)),
            [&](World &w) { return uint32_t(orig(w.self, ArgVal(w, k1, a))); },
            [&](World &w) { return uint32_t(Self<cGZPersistDBSegment>(w)->DeleteRecord(ArgVal(w, k1, a))); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolArgFn>(w.self, 26)(w.self, ArgVal(w, k1, a))); });
    }
}

DIFF_TEST(GZPersistDBSegment_OpenRecord, 0x99cf00)
{
    std::mt19937 rng(0xab);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), b = rng(), c = rng(), k1 = rng(), k2 = rng(), k3 = rng();
        auto orig = ctx.Original<BoolArg3Val>(0x99cf00);
        Compare(
            ctx, "OpenRecord", seed, 22, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::OpenRecord)),
            [&](World &w) { return uint32_t(orig(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b), ArgVal(w, k3, c))); },
            [&](World &w) {
                return uint32_t(Self<cGZPersistDBSegment>(w)->OpenRecord(ArgVal(w, k1, a), ArgVal(w, k2, b), ArgVal(w, k3, c)));
            },
            [&](World &w) {
                return uint32_t(VirtualSlot<BoolArg3Val>(w.self, 22)(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b), ArgVal(w, k3, c)));
            });
    }
}

DIFF_TEST(GZPersistDBSegment_ReadRecord, 0x99d010)
{
    std::mt19937 rng(0xad);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), b = rng(), c = rng(), k1 = rng(), k2 = rng(), k3 = rng();
        auto orig = ctx.Original<U32Arg3Val>(0x99d010);
        Compare(
            ctx, "ReadRecord", seed, 27, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::ReadRecord)),
            [&](World &w) { return orig(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b), ArgVal(w, k3, c)); },
            [&](World &w) {
                return Self<cGZPersistDBSegment>(w)->ReadRecord(ArgVal(w, k1, a), ArgVal(w, k2, b), ArgVal(w, k3, c));
            },
            [&](World &w) {
                return VirtualSlot<U32Arg3Val>(w.self, 27)(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b), ArgVal(w, k3, c));
            });
    }
}

DIFF_TEST(GZPersistDBSegment_WriteRecord, 0x99df00)
{
    std::mt19937 rng(0xaf);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), b = rng(), c = rng(), k1 = rng(), k2 = rng(), k3 = rng();
        auto orig = ctx.Original<BoolArg3Val>(0x99df00);
        Compare(
            ctx, "WriteRecord", seed, 28, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::WriteRecord)),
            [&](World &w) { return uint32_t(orig(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b), ArgVal(w, k3, c))); },
            [&](World &w) {
                return uint32_t(Self<cGZPersistDBSegment>(w)->WriteRecord(ArgVal(w, k1, a), ArgVal(w, k2, b), ArgVal(w, k3, c)));
            },
            [&](World &w) {
                return uint32_t(VirtualSlot<BoolArg3Val>(w.self, 28)(w.self, ArgVal(w, k1, a), ArgVal(w, k2, b), ArgVal(w, k3, c)));
            });
    }
}

// ---- constructor, destructor body, deleting destructor, AddRef, inner namespace setter -------------------

namespace {

using CtorFn = void *(__thiscall *)(void *);
using DtorFn = void *(__thiscall *)(void *, uint32_t);

// A heap object (the class is 0x18 bytes; the block is bigger so a free is observable), a real lock built by our
// verified constructor, and two fakes for the cache and namespace objects whose Release is recorded.
struct DtorWorld {
    uint8_t *self = nullptr;
    alignas(8) uint8_t cache[kFakeBytes];
    alignas(8) uint8_t ns[kFakeBytes];
    void *objVT[3];
    void *selfVT[50];
};

void BuildDtorWorld(DtorWorld &w, const difftest::Context &ctx, uint32_t seed, bool oursSlot33)
{
    std::mt19937 rng(seed);
    w.self = static_cast<uint8_t *>(::operator new(0x400)); // (its free is judged by a FreeRecorder)
    for (size_t i = 0; i < 0x18; ++i) w.self[i] = static_cast<uint8_t>(rng());
    for (size_t i = 0; i < kFakeBytes; ++i) w.cache[i] = static_cast<uint8_t>(rng());
    for (size_t i = 0; i < kFakeBytes; ++i) w.ns[i] = static_cast<uint8_t>(rng());
    std::memcpy(w.selfVT, ctx.image.At(cGZPersistDBSegment::kVTable), sizeof(w.selfVT));
    if (oursSlot33) w.selfVT[33] = reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::DeletingDestructor));
    w.objVT[0] = nullptr;
    w.objVT[1] = reinterpret_cast<void *>(&ObjAddRef);
    w.objVT[2] = reinterpret_cast<void *>(&ObjRelease);
    reinterpret_cast<cRZCriticalSection *>(w.self + 8)->Construct();
    Put(w.self, 0x00, Addr(w.selfVT));
    Put(w.self, 0x04, static_cast<int32_t>(rng()));
    // The lock destructor rewrites the lock's vtable word; a random value there makes that visible.
    Put(w.self, 0x08, static_cast<uint32_t>(rng()));
    Put(w.self, 0x10, rng() % 3 ? Addr(w.cache) : 0u);
    Put(w.self, 0x14, rng() % 3 ? Addr(w.ns) : 0u);
    Put(w.cache, 0x00, Addr(w.objVT));
    Put(w.ns, 0x00, Addr(w.objVT));
}

// The object's words with the lock's block pointer reduced to a constant (the destructor frees the block and
// the address differs between runs) and the run's own addresses replaced by ids.
void DumpObject(const uint8_t *self, std::vector<uint32_t> &out)
{
    for (size_t i = 0; i < 0x18; i += 4) {
        uint32_t v = Load32(self, i);
        if (i == 0xc) v = 0;
        out.push_back(Id(v));
    }
}

// The id map holds every world of the case (the same id per role), set once before the first run: Id is then one
// function on every side, so a random word that equals some world's address is renamed alike on all of them. (A map
// of the running world alone renamed such a word on that side only: lanes NB/NE, the same hazard as PlaceWorlds.)
void UseWorlds(std::initializer_list<const DtorWorld *> worlds)
{
    gIds.clear();
    for (const DtorWorld *w : worlds) {
        gIds.push_back({Addr(w->self), 0x100});
        gIds.push_back({Addr(w->ns), 0x101});
        gIds.push_back({Addr(w->cache), 0x102});
        gIds.push_back({Addr(w->selfVT), 0x110});
        gIds.push_back({Addr(w->objVT), 0x112});
    }
}

void StartRun()
{
    gTrace.clear();
}

} // namespace

DIFF_TEST(GZPersistDBSegment_Construct, 0x99b790)
{
    std::mt19937 rng(0xc7);
    auto orig = ctx.Original<CtorFn>(0x99b790);
    for (int i = 0; i < 300; ++i) {
        uint32_t seed = rng();
        auto make = [&]() {
            std::mt19937 fill(seed);
            auto *mem = static_cast<uint8_t *>(::operator new(0x18));
            for (size_t k = 0; k < 0x18; ++k) mem[k] = static_cast<uint8_t>(fill());
            return mem;
        };
        uint8_t *a = make();
        uint8_t *b = make();
        void *ra = orig(a);
        void *rb = reinterpret_cast<cGZPersistDBSegment *>(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("Construct seed %u: does not return this", seed);
        gIds.clear();
        std::vector<uint32_t> da, db;
        DumpObject(a, da);
        DumpObject(b, db);
        if (da != db) ctx.Fail("Construct seed %u: object words differ", seed);
        if (Load32(a, 0xc) == 0 || Load32(b, 0xc) == 0) ctx.Fail("Construct seed %u: no lock block", seed);
        // Both are destroyed by our destructor (the original's is tested below) and freed.
        reinterpret_cast<cGZPersistDBSegment *>(a)->DeletingDestructor(1);
        reinterpret_cast<cGZPersistDBSegment *>(b)->DeletingDestructor(1);
    }
}

DIFF_TEST(GZPersistDBSegment_Destruct, 0x99b8e0)
{
    std::mt19937 rng(0xd8);
    auto orig = ctx.Original<VoidFn>(0x99b8e0);
    for (int i = 0; i < 400; ++i) {
        uint32_t seed = rng();
        DtorWorld a, b;
        BuildDtorWorld(a, ctx, seed, false);
        BuildDtorWorld(b, ctx, seed, false);
        UseWorlds({&a, &b});
        StartRun();
        orig(a.self);
        std::vector<uint32_t> ta = gTrace;
        DumpObject(a.self, ta);
        StartRun();
        reinterpret_cast<cGZPersistDBSegment *>(b.self)->Destruct();
        std::vector<uint32_t> tb = gTrace;
        DumpObject(b.self, tb);
        ++ctx.cases;
        if (ta != tb) ctx.Fail("Destruct seed %u: releases or object words differ (%zu vs %zu words)", seed, ta.size(), tb.size());
        ::operator delete(a.self, 0x18);
        ::operator delete(b.self, 0x18);
    }
}

DIFF_TEST(GZPersistDBSegment_DeletingDestructor, 0x99b990)
{
    std::mt19937 rng(0xd9);
    auto orig = ctx.Original<DtorFn>(0x99b990);
    for (int i = 0; i < 600; ++i) {
        uint32_t seed = rng();
        uint32_t flags = rng() % 4; // bit 0 frees the object
        DtorWorld a, b, c;
        BuildDtorWorld(a, ctx, seed, false);
        BuildDtorWorld(b, ctx, seed, true);
        BuildDtorWorld(c, ctx, seed, true);
        // Each side's result: the returned pointer is the object, whether the block was freed (checked right
        // after its own call), the recorded releases, and, while the object is alive, its words.
        // The frees are recorded across the call (rec), which finish() stops.
        auto finish = [&](difftest::FreeRecorder &rec, DtorWorld &w, void *ret, std::vector<uint32_t> &t, bool &freed) {
            rec.Stop();
            t = gTrace;
            t.push_back(ret == w.self);
            freed = (flags & 1) && rec.Freed(w.self);
            t.push_back(freed);
            if (!(flags & 1)) DumpObject(w.self, t);
        };
        std::vector<uint32_t> ta, tb, tc;
        bool fa, fb, fc;
        UseWorlds({&a, &b, &c});
        StartRun();
        {
            difftest::FreeRecorder rec;
            void *ret = orig(a.self, flags);
            finish(rec, a, ret, ta, fa);
        }
        StartRun();
        {
            difftest::FreeRecorder rec;
            void *ret = reinterpret_cast<cGZPersistDBSegment *>(b.self)->DeletingDestructor(flags);
            finish(rec, b, ret, tb, fb);
        }
        StartRun();
        {
            difftest::FreeRecorder rec;
            void *ret = VirtualSlot<DtorFn>(c.self, 33)(c.self, flags);
            finish(rec, c, ret, tc, fc);
        }
        ++ctx.cases;
        if (ta != tb) ctx.Fail("DeletingDestructor seed %u flags %u: direct differs from the original", seed, flags);
        if (ta != tc) ctx.Fail("DeletingDestructor seed %u flags %u: via slot differs from the original", seed, flags);
        if ((flags & 1) && !fa) ctx.Fail("DeletingDestructor seed %u: the original's free is not visible", seed);
        if (!(flags & 1)) {
            ::operator delete(a.self, 0x18);
            ::operator delete(b.self, 0x18);
            ::operator delete(c.self, 0x18);
        }
    }
}

DIFF_TEST(GZPersistDBSegment_AddRef, 0x99ba50)
{
    std::mt19937 rng(0xa1);
    auto orig = ctx.Original<ReleaseOrigFn>(0x99ba50);
    static constexpr int32_t kCounts[] = {0, 1, -1, 0x7ffffffe, 5};
    for (int i = 0; i < 300; ++i) {
        uint32_t seed = rng();
        World &w = SharedWorld();
        int32_t count = i < 5 ? kCounts[i] : static_cast<int32_t>(rng());
        Build(w, ctx, seed, false, -1, nullptr);
        Put(w.self, 4, count);
        Outcome oa = Run(w, [&](World &x) { return orig(x.self); });
        Build(w, ctx, seed, false, -1, nullptr);
        Put(w.self, 4, count);
        Outcome ob = Run(w, [&](World &x) { return Self<cGZPersistDBSegment>(x)->AddRef(); });
        ++ctx.cases;
        if (oa.ret != ob.ret || oa.trace != ob.trace) ctx.Fail("AddRef seed %u: original %u, ours %u", seed, oa.ret, ob.ret);
        if (ob.ret != static_cast<uint32_t>(count) + 1u) ctx.Fail("AddRef seed %u: result is not count + 1", seed);
    }
}

DIFF_TEST(GZPersistDBSegment_SetNamespaceInner, 0x4b8e40)
{
    std::mt19937 rng(0xa2);
    auto orig = ctx.Original<VoidPtrFn>(0x4b8e40);
    for (int i = 0; i < 800; ++i) {
        uint32_t seed = rng();
        uint32_t k = rng();
        gRet = rng();
        Compare(
            ctx, "SetNamespaceInner", seed, 48, reinterpret_cast<void *>(MemberAddr(&cGZPersistDBSegment::SetNamespaceInner)),
            [&](World &w) { orig(w.self, Pick(w, k)); return 0u; },
            [&](World &w) { Self<cGZPersistDBSegment>(w)->SetNamespaceInner(Pick(w, k)); return 0u; },
            [&](World &w) { VirtualSlot<VoidPtrFn>(w.self, 48)(w.self, Pick(w, k)); return 0u; });
    }
}
