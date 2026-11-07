// Differential tests for cGZPersistResourceFactory (field map in src/gz/cGZPersistResourceFactory.h):
// Construct, Destruct, DeletingDestructor, QueryInterface, AddRef, Release, CreateInstance, CreateInstance2,
// Read and Write.
//
// Each run gets a fresh world built from one seed: the factory object (its vtable is a copy of the game's
// whose slots 1, 2, 4, 5, 7, 9, 10 and 11 are recorders; the lock at +8 has recording Lock/Unlock slots),
// a record argument (slot 0 QueryInterface and slot 3 key getter are recorders), a serial record and a
// created resource (recording QueryInterface, Release and the key setter at slot 4). The original and ours
// run in separate worlds and are compared on the return value, the recorded calls with their normalised
// arguments, and the final bytes of the object, the out buffer and the key.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistResourceFactory.h"
#include "hook/hooks.h"

#include <Windows.h>

#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <utility>
#include <vector>

namespace {

constexpr size_t kObjBytes = 0x40; // the object is 0x10 bytes; the rest is canary space
constexpr size_t kFakeBytes = 0x20;
constexpr size_t kOutBytes = 0x10;

std::vector<uint32_t> gTrace;
std::vector<std::pair<uint32_t, uint32_t>> gIds; // address, id
uint32_t gKeyBase = 0;
uint32_t gKeyCalls = 0;
void *gCreated = nullptr;
void *gSerial = nullptr;
uint32_t gRet = 0;
// Behaviour switches, set per iteration (the same for both sides).
bool gCreateOk, gReadOk, gCreatedQiOk, gRecordQiOk, gRecordQiWrites;

uint32_t Addr(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
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

// An out pointer: a world object is recorded by id; anything else is a local on the caller's stack, whose
// address differs between the sides, so only the fact is recorded.
void OutArg(const void *p)
{
    uint32_t v = Addr(p);
    uint32_t here = Addr(&v);
    gTrace.push_back(Id(v) != v ? Id(v) : (v - here + 0x10000u < 0x20000u ? 0x57ac0000u : v));
}

// A pointer into the key buffer is recorded as its offset.
void KeyArg(uint32_t value)
{
    gTrace.push_back(value - gKeyBase < 0x20 ? 0x4b000000u + (value - gKeyBase) : Id(value));
}

// __fastcall recorders pop their stack arguments like __thiscall callees do; the second parameter takes edx.
uint32_t __fastcall SelfAddRef(void *self, void *)
{
    Note(1, self);
    return gRet;
}

uint32_t __fastcall SelfRelease(void *self, void *)
{
    Note(2, self);
    return gRet;
}

bool __fastcall SelfSlot4(void *self, void *, uint32_t type, uint32_t riid, void **ppv, uint32_t a4, uint32_t a5)
{
    Note(4, self);
    Arg(type);
    Arg(riid);
    OutArg(ppv);
    Arg(a4);
    Arg(a5);
    if (gCreateOk) *ppv = gCreated;
    return gCreateOk;
}

bool __fastcall SelfSlot5(void *self, void *, void *resource, void *record)
{
    Note(5, self);
    Arg(Addr(resource));
    Arg(Addr(record));
    return gReadOk;
}

void *__fastcall SelfSlot7(void *self, void *, uint32_t flags)
{
    Note(7, self);
    Arg(flags);
    return self;
}

bool __fastcall SelfSlot9(void *self, void *, uint32_t type, uint32_t riid, void **ppv)
{
    Note(9, self);
    Arg(type);
    Arg(riid);
    OutArg(ppv);
    if (gCreateOk) *ppv = gCreated;
    return gCreateOk;
}

bool __fastcall SelfSlot10(void *self, void *, void *resource, void *serial)
{
    Note(10, self);
    Arg(Addr(resource));
    Arg(Addr(serial));
    return gRet & 1;
}

bool __fastcall SelfSlot11(void *self, void *, void *resource, void *serial)
{
    Note(11, self);
    Arg(Addr(resource));
    Arg(Addr(serial));
    return gRet & 1;
}

uint32_t __fastcall LockEnter(void *self, void *)
{
    Note(21, self);
    return 0;
}

uint32_t __fastcall LockLeave(void *self, void *)
{
    Note(22, self);
    return 0;
}

bool __fastcall RecordQi(void *self, void *, uint32_t riid, void **ppv)
{
    Note(30, self);
    Arg(riid);
    OutArg(ppv);
    if (gRecordQiOk && gRecordQiWrites) *ppv = gSerial;
    return gRecordQiOk;
}

const uint32_t *__fastcall RecordKey(void *self, void *)
{
    Note(33, self);
    return reinterpret_cast<const uint32_t *>(gKeyBase) + (gKeyCalls++ ? 1 : 0);
}

uint32_t __fastcall SerialRelease(void *self, void *)
{
    Note(40, self);
    return 0;
}

bool __fastcall CreatedQi(void *self, void *, uint32_t riid, void **ppv)
{
    Note(50, self);
    Arg(riid);
    OutArg(ppv);
    if (gCreatedQiOk) *ppv = self;
    return gCreatedQiOk;
}

uint32_t __fastcall CreatedRelease(void *self, void *)
{
    Note(52, self);
    return 0;
}

void __fastcall CreatedSetKey(void *self, void *, const void *key)
{
    Note(54, self);
    KeyArg(Addr(key));
}

struct World {
    alignas(8) uint8_t self[kObjBytes];
    alignas(8) uint8_t rec[kFakeBytes];
    alignas(8) uint8_t ser[kFakeBytes];
    alignas(8) uint8_t cre[kFakeBytes];
    alignas(8) uint32_t key[4];
    alignas(8) uint8_t out[kOutBytes];
    void *selfVT[12];
    void *lockVT[3];
    void *recVT[4];
    void *serVT[3];
    void *creVT[5];
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

void Build(World &w, const difftest::Context &ctx, uint32_t seed, uint32_t refCount)
{
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.self, sizeof(w.self));
    fill(w.rec, sizeof(w.rec));
    fill(w.ser, sizeof(w.ser));
    fill(w.cre, sizeof(w.cre));
    fill(w.key, sizeof(w.key));
    fill(w.out, sizeof(w.out));

    std::memcpy(w.selfVT, ctx.image.At(cGZPersistResourceFactory::kVTable), sizeof(w.selfVT));
    w.selfVT[1] = reinterpret_cast<void *>(&SelfAddRef);
    w.selfVT[2] = reinterpret_cast<void *>(&SelfRelease);
    w.selfVT[4] = reinterpret_cast<void *>(&SelfSlot4);
    w.selfVT[5] = reinterpret_cast<void *>(&SelfSlot5);
    w.selfVT[7] = reinterpret_cast<void *>(&SelfSlot7);
    w.selfVT[9] = reinterpret_cast<void *>(&SelfSlot9);
    w.selfVT[10] = reinterpret_cast<void *>(&SelfSlot10);
    w.selfVT[11] = reinterpret_cast<void *>(&SelfSlot11);
    w.lockVT[0] = nullptr;
    w.lockVT[1] = reinterpret_cast<void *>(&LockEnter);
    w.lockVT[2] = reinterpret_cast<void *>(&LockLeave);
    w.recVT[0] = reinterpret_cast<void *>(&RecordQi);
    w.recVT[1] = w.recVT[2] = nullptr;
    w.recVT[3] = reinterpret_cast<void *>(&RecordKey);
    w.serVT[0] = w.serVT[1] = nullptr;
    w.serVT[2] = reinterpret_cast<void *>(&SerialRelease);
    w.creVT[0] = reinterpret_cast<void *>(&CreatedQi);
    w.creVT[1] = nullptr;
    w.creVT[2] = reinterpret_cast<void *>(&CreatedRelease);
    w.creVT[3] = nullptr;
    w.creVT[4] = reinterpret_cast<void *>(&CreatedSetKey);

    Put(w.self, 0x00, Addr(w.selfVT));
    Put(w.self, 0x04, refCount);
    Put(w.self, 0x08, Addr(w.lockVT));
    Put(w.rec, 0, Addr(w.recVT));
    Put(w.ser, 0, Addr(w.serVT));
    Put(w.cre, 0, Addr(w.creVT));
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace;
};

void SetIds(const World &w)
{
    gIds = {{Addr(w.self), 0x100}, {Addr(w.self + 8), 0x105}, {Addr(w.rec), 0x101},   {Addr(w.ser), 0x102},
            {Addr(w.cre), 0x103},  {Addr(w.key), 0x104},      {Addr(w.out), 0x106},   {Addr(w.selfVT), 0x110},
            {Addr(w.lockVT), 0x111}, {Addr(w.recVT), 0x112},  {Addr(w.serVT), 0x113}, {Addr(w.creVT), 0x114}};
    gKeyBase = Addr(w.key);
    gKeyCalls = 0;
    gCreated = const_cast<uint8_t *>(w.cre);
    gSerial = const_cast<uint8_t *>(w.ser);
}

void Snapshot(const World &w, std::vector<uint32_t> &t)
{
    for (size_t i = 0; i < kObjBytes; i += 4) t.push_back(Id(Load32(w.self, i)));
    for (size_t i = 0; i < kOutBytes; i += 4) t.push_back(Id(Load32(w.out, i)));
    for (size_t i = 0; i < 4; ++i) t.push_back(w.key[i]);
    for (const uint8_t *fake : {w.rec, w.ser, w.cre})
        for (size_t i = 0; i < kFakeBytes; i += 4) t.push_back(Id(Load32(fake, i)));
}

// Where the world lives. Original and ours run on this one world, rebuilt from the seed before each, so they see it
// at the same address and Id is the same function on both sides: equal words stay equal and different words stay
// different, whatever the call wrote. Two worlds at two addresses made Id depend on the heap layout (a random word
// equal to one world's address was renamed in that world only; lane NB, tests/
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

cGZPersistResourceFactory *Self(World &w)
{
    return reinterpret_cast<cGZPersistResourceFactory *>(w.self);
}

void Compare(difftest::Context &ctx, const char *name, uint32_t seed, const Outcome &oa, const Outcome &ob, uint32_t retMask = 0xffffffffu)
{
    ++ctx.cases;
    if ((oa.ret & retMask) != (ob.ret & retMask) || oa.trace != ob.trace)
        ctx.Fail("%s seed %u: original returned %u (%zu words), ours %u (%zu)", name, seed, oa.ret, oa.trace.size(), ob.ret,
                 ob.trace.size());
}

void RandomBehaviour(std::mt19937 &rng)
{
    gRet = rng();
    gCreateOk = rng() % 4 != 0;
    gReadOk = rng() % 4 != 0;
    gCreatedQiOk = rng() % 3 != 0;
    gRecordQiOk = rng() % 4 != 0;
    gRecordQiWrites = rng() % 4 != 0; // not writing leaves the original's stack slot, which holds `this`
}

uint32_t PickIid(std::mt19937 &rng)
{
    static constexpr uint32_t kIds[] = {1, 0xa56b8f17, 0xa56b8f16, 0xa56b8f18, 0, 2, 0x456b8f1d};
    return rng() % 3 ? kIds[rng() % 7] : static_cast<uint32_t>(rng());
}

using Fn3 = bool(__thiscall *)(void *, uint32_t, void **);
using Fn1 = uint32_t(__thiscall *)(void *);
using Fn5 = bool(__thiscall *)(void *, void *, uint32_t, void **, uint32_t, uint32_t);
using Fn5T = bool(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t);
using FnIo = bool(__thiscall *)(void *, void *, void *);

bool SameLock(const cRZCriticalSection &a, const cRZCriticalSection &b)
{
    auto *ca = reinterpret_cast<const CRITICAL_SECTION *>(a.mBlock);
    auto *cb = reinterpret_cast<const CRITICAL_SECTION *>(b.mBlock);
    return a.mVTable == b.mVTable && a.mBlock->lockCount == b.mBlock->lockCount && ca->RecursionCount == cb->RecursionCount
        && ca->LockCount == cb->LockCount;
}

} // namespace

DIFF_TEST(cGZPersistResourceFactory_Construct, 0x9b6010)
{
    auto orig = ctx.Original<cGZPersistResourceFactory *(__thiscall *)(void *)>(0x9b6010);
    std::mt19937 rng(0x31);
    for (int i = 0; i < 100; ++i) {
        uint8_t ba[0x20], bb[0x20];
        for (size_t k = 0; k < sizeof(ba); ++k) ba[k] = bb[k] = static_cast<uint8_t>(rng());
        auto *a = reinterpret_cast<cGZPersistResourceFactory *>(ba);
        auto *b = reinterpret_cast<cGZPersistResourceFactory *>(bb);
        void *ra = orig(a);
        void *rb = b->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("Construct returned the wrong object");
        if (a->mVTable != b->mVTable || a->mRefCount != b->mRefCount || a->mVTable != GameAddress(cGZPersistResourceFactory::kVTable)
            || a->mRefCount != 0 || !SameLock(a->mLock, b->mLock) || std::memcmp(ba + 0x10, bb + 0x10, 0x10) != 0)
            ctx.Fail("Construct state differs");
        a->mLock.Destruct();
        b->mLock.Destruct();
    }
}

DIFF_TEST(cGZPersistResourceFactory_Destruct, 0x9b6040)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x9b6040);
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0x9b6010);
    std::mt19937 rng(0x32);
    for (int i = 0; i < 100; ++i) {
        uint8_t ba[0x20], bb[0x20];
        for (size_t k = 0; k < sizeof(ba); ++k) ba[k] = bb[k] = static_cast<uint8_t>(rng());
        auto *a = reinterpret_cast<cGZPersistResourceFactory *>(ba);
        auto *b = reinterpret_cast<cGZPersistResourceFactory *>(bb);
        construct(a);
        construct(b);
        // Anything in the vtable fields must be overwritten.
        a->mVTable = b->mVTable = reinterpret_cast<void *>(0x1234);
        a->mLock.mVTable = b->mLock.mVTable = reinterpret_cast<void *>(0x5678);
        auto *blockA = a->mLock.mBlock;
        auto *blockB = b->mLock.mBlock;
        difftest::FreeRecorder recA;
        orig(a);
        recA.Stop();
        bool freedA = recA.Freed(blockA);
        difftest::FreeRecorder recB;
        b->Destruct();
        recB.Stop();
        bool freedB = recB.Freed(blockB);
        ++ctx.cases;
        if (a->mVTable != b->mVTable || a->mVTable != GameAddress(cGZPersistResourceFactory::kVTable)
            || a->mLock.mVTable != b->mLock.mVTable || a->mRefCount != b->mRefCount || freedA != freedB || !freedB)
            ctx.Fail("Destruct state differs (freed: original %d, ours %d)", freedA, freedB);
    }
}

DIFF_TEST(cGZPersistResourceFactory_DeletingDestructor, 0x9b6080)
{
    auto orig = ctx.Original<cGZPersistResourceFactory *(__thiscall *)(void *, uint32_t)>(0x9b6080);
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0x9b6010);
    std::mt19937 rng(0x33);
    for (int i = 0; i < 100; ++i) {
        uint32_t flags = i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : static_cast<uint32_t>(rng());
        auto *a = static_cast<cGZPersistResourceFactory *>(::operator new(sizeof(cGZPersistResourceFactory)));
        auto *b = static_cast<cGZPersistResourceFactory *>(::operator new(sizeof(cGZPersistResourceFactory)));
        construct(a);
        construct(b);
        a->mVTable = b->mVTable = reinterpret_cast<void *>(0x1234);
        a->mLock.mVTable = b->mLock.mVTable = reinterpret_cast<void *>(0x5678);
        auto *blockA = a->mLock.mBlock;
        auto *blockB = b->mLock.mBlock;
        difftest::FreeRecorder recA;
        auto *ra = orig(a, flags);
        recA.Stop();
        bool objFreedA = recA.Freed(a);
        bool lockFreedA = recA.Freed(blockA);
        difftest::FreeRecorder recB;
        auto *rb = b->DeletingDestructor(flags);
        recB.Stop();
        bool objFreedB = recB.Freed(b);
        bool lockFreedB = recB.Freed(blockB);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("flags %u: wrong return", flags);
        if (objFreedA != objFreedB || objFreedB != ((flags & 1) != 0) || lockFreedA != lockFreedB || !lockFreedB)
            ctx.Fail("flags %u: freed object original %d ours %d, lock block original %d ours %d", flags, objFreedA, objFreedB,
                     lockFreedA, lockFreedB);
        if (!(flags & 1)) {
            if (a->mVTable != b->mVTable || a->mVTable != GameAddress(cGZPersistResourceFactory::kVTable)
                || a->mLock.mVTable != b->mLock.mVTable)
                ctx.Fail("flags %u: vtables differ after destruction", flags);
            ::operator delete(a, sizeof(*a));
            ::operator delete(b, sizeof(*b));
        }
    }
}

DIFF_TEST(cGZPersistResourceFactory_QueryInterface, 0x9b61d0)
{
    std::mt19937 rng(0x34);
    auto orig = ctx.Original<Fn3>(0x9b61d0);
    for (int i = 0; i < 1000; ++i) {
        uint32_t seed = rng();
        uint32_t riid = PickIid(rng);
        RandomBehaviour(rng);
        uint32_t refs = rng();
        World &sw = SharedWorld();
        Build(sw, ctx, seed, refs);
        Outcome oa = Run(sw, [&](World &w) { return uint32_t(orig(w.self, riid, reinterpret_cast<void **>(w.out)) & 1); });
        Build(sw, ctx, seed, refs);
        Outcome ob = Run(sw, [&](World &w) { return uint32_t(Self(w)->QueryInterface(riid, reinterpret_cast<void **>(w.out))); });
        Compare(ctx, "QueryInterface", seed, oa, ob, 0xff);
    }
}

DIFF_TEST(cGZPersistResourceFactory_AddRef, 0x9b60b0)
{
    std::mt19937 rng(0x35);
    auto orig = ctx.Original<Fn1>(0x9b60b0);
    static constexpr uint32_t kCounts[] = {0, 1, 2, 0xffffffffu, 0x7fffffffu};
    for (int i = 0; i < 400; ++i) {
        uint32_t seed = rng();
        RandomBehaviour(rng);
        uint32_t refs = i < 5 ? kCounts[i] : static_cast<uint32_t>(rng());
        World &sw = SharedWorld();
        Build(sw, ctx, seed, refs);
        Outcome oa = Run(sw, [&](World &w) { return orig(w.self); });
        Build(sw, ctx, seed, refs);
        Outcome ob = Run(sw, [&](World &w) { return Self(w)->AddRef(); });
        Compare(ctx, "AddRef", seed, oa, ob);
        if (ob.ret != refs + 1u) ctx.Fail("AddRef seed %u: result is not count + 1", seed);
    }
}

DIFF_TEST(cGZPersistResourceFactory_Release, 0x9b6290)
{
    std::mt19937 rng(0x36);
    auto orig = ctx.Original<Fn1>(0x9b6290);
    static constexpr uint32_t kCounts[] = {0, 1, 2, 3, 0xffffffffu, 0x80000000u};
    for (int i = 0; i < 500; ++i) {
        uint32_t seed = rng();
        RandomBehaviour(rng);
        uint32_t refs = i < 6 ? kCounts[i] : (i % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 5);
        World &sw = SharedWorld();
        Build(sw, ctx, seed, refs);
        Outcome oa = Run(sw, [&](World &w) { return orig(w.self); });
        Build(sw, ctx, seed, refs);
        Outcome ob = Run(sw, [&](World &w) { return Self(w)->Release(); });
        Compare(ctx, "Release", seed, oa, ob);
    }
}

DIFF_TEST(cGZPersistResourceFactory_CreateInstance, 0x9b60e0)
{
    std::mt19937 rng(0x37);
    auto orig = ctx.Original<Fn5>(0x9b60e0);
    for (int i = 0; i < 1500; ++i) {
        uint32_t seed = rng();
        RandomBehaviour(rng);
        uint32_t riid = PickIid(rng);
        uint32_t a4 = rng() % 3 ? 0 : static_cast<uint32_t>(rng());
        uint32_t a5 = rng() % 3 ? 0 : static_cast<uint32_t>(rng());
        World &sw = SharedWorld();
        Build(sw, ctx, seed, 1);
        Outcome oa = Run(sw, [&](World &w) { return uint32_t(orig(w.self, w.rec, riid, reinterpret_cast<void **>(w.out), a4, a5) & 1); });
        Build(sw, ctx, seed, 1);
        Outcome ob = Run(sw, [&](World &w) { return uint32_t(Self(w)->CreateInstance(w.rec, riid, reinterpret_cast<void **>(w.out), a4, a5)); });
        Compare(ctx, "CreateInstance", seed, oa, ob, 0xff);
    }
}

DIFF_TEST(cGZPersistResourceFactory_CreateInstance2, 0x9b61b0)
{
    std::mt19937 rng(0x38);
    auto orig = ctx.Original<Fn5T>(0x9b61b0);
    for (int i = 0; i < 400; ++i) {
        uint32_t seed = rng();
        RandomBehaviour(rng);
        uint32_t type = rng(), riid = PickIid(rng), a4 = rng(), a5 = rng();
        World &sw = SharedWorld();
        Build(sw, ctx, seed, 1);
        Outcome oa = Run(sw, [&](World &w) { return uint32_t(orig(w.self, type, riid, reinterpret_cast<void **>(w.out), a4, a5) & 1); });
        Build(sw, ctx, seed, 1);
        Outcome ob = Run(sw, [&](World &w) { return uint32_t(Self(w)->CreateInstance2(type, riid, reinterpret_cast<void **>(w.out), a4, a5)); });
        Compare(ctx, "CreateInstance2", seed, oa, ob, 0xff);
    }
}

DIFF_TEST(cGZPersistResourceFactory_Read, 0x9b6200)
{
    std::mt19937 rng(0x39);
    auto orig = ctx.Original<FnIo>(0x9b6200);
    for (int i = 0; i < 800; ++i) {
        uint32_t seed = rng();
        RandomBehaviour(rng);
        uint32_t res = rng() % 3 ? 0 : static_cast<uint32_t>(rng());
        World &sw = SharedWorld();
        Build(sw, ctx, seed, 1);
        Outcome oa = Run(sw, [&](World &w) { return uint32_t(orig(w.self, res ? reinterpret_cast<void *>(res) : w.cre, w.rec) & 1); });
        Build(sw, ctx, seed, 1);
        Outcome ob = Run(sw, [&](World &w) { return uint32_t(Self(w)->Read(res ? reinterpret_cast<void *>(res) : w.cre, w.rec)); });
        Compare(ctx, "Read", seed, oa, ob, 0xff);
    }
}

DIFF_TEST(cGZPersistResourceFactory_Write, 0x9b62d0)
{
    std::mt19937 rng(0x3a);
    auto orig = ctx.Original<FnIo>(0x9b62d0);
    for (int i = 0; i < 800; ++i) {
        uint32_t seed = rng();
        RandomBehaviour(rng);
        uint32_t res = rng() % 3 ? 0 : static_cast<uint32_t>(rng());
        World &sw = SharedWorld();
        Build(sw, ctx, seed, 1);
        Outcome oa = Run(sw, [&](World &w) { return uint32_t(orig(w.self, res ? reinterpret_cast<void *>(res) : w.cre, w.rec) & 1); });
        Build(sw, ctx, seed, 1);
        Outcome ob = Run(sw, [&](World &w) { return uint32_t(Self(w)->Write(res ? reinterpret_cast<void *>(res) : w.cre, w.rec)); });
        Compare(ctx, "Write", seed, oa, ob, 0xff);
    }
}
