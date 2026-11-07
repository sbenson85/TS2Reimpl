// Differential tests for the cGZPersistResourceManager slots reimplemented so far (field map in
// src/gz/cGZPersistResourceManager.h). Each run gets its own copy of a fake manager, built from the
// same seed: a copy of the game's primary vtable with recording slots 4, 8, 22, 23 and 34; three
// lock objects (+0x20, +0x68, +0x88) whose Enter/Leave record calls; a fake object behind +0x84 (slot
// 10 records); a fake object passed as vf9's argument (slot 4 records); a list of random length
// behind +0x2c; a list of value objects behind +0x90, whose fake objects record every slot the
// manager calls; and a fake holder object for vf40. Results and the recorded calls (kind, object,
// arguments) must match, and the fake manager's bytes must not change, except for the two slots
// that change the lists or +0x84 (vf41, vf46): for those the lists and +0x84 are compared instead.
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZPersistResourceManager.h"

#include <windows.h>

#include <cstring>
#include <exception>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using Vf3Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Vf9Fn = uint32_t(__thiscall *)(void *, uint32_t, void *);
using Vf23Fn = uint32_t(__thiscall *)(void *, const uint32_t *, uint32_t);
using Vf26Fn = uint32_t(__thiscall *)(void *, uint32_t);
using NoArgFn = uint32_t(__thiscall *)(void *);
using Vf11Fn = uint8_t(__thiscall *)(void *, uint32_t);
using Vf39Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using Vf40Fn = uint8_t(__thiscall *)(void *, uint32_t, void *const *);
using Vf41Fn = uint8_t(__thiscall *)(void *, void *);
using NoArgU8Fn = uint8_t(__thiscall *)(void *);
using Vf44Fn = void(__thiscall *)(void *, uint32_t);
using Vf46Fn = void(__thiscall *)(void *, void *);
using Vf50Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
using Vf51Fn = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, uint32_t, uint32_t, uint32_t);
using OnIdleFn = uint8_t(__thiscall *)(void *, uint32_t);
using QueryInterfaceFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
using Vf12Fn = uint8_t(__thiscall *)(void *, void *const *, uint32_t);
using Vf16Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using Vf24Fn = uint32_t(__thiscall *)(void *, void *, uint32_t);
using Vf33Fn = uint8_t(__thiscall *)(void *, uint32_t, void **);
using Vf4Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Vf5Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Vf49Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using Vf52Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Vf53Fn = uint8_t(__thiscall *)(void *, uint32_t, void *, void *, uint32_t, uint32_t, uint32_t, uint32_t);
using Vf38Fn = uint32_t(__thiscall *)(void *, void **, uint32_t *);

// Recorded calls: kind, object id, then the arguments.
std::vector<uint32_t> gTrace;
std::vector<std::pair<const void *, uint32_t>> gIds;
uint32_t gLeaveResult = 0;
uint32_t gCallResult = 0;
uint32_t gFindResult = 0;
uint32_t gFoundAddr = 0; // the object the find slots hand out; set by Run to the first value object
uint32_t gDqRet[4];      // what the fake deque objects' slot 15 returns, per object (set per seed)
uint8_t *gRangeBase = nullptr; // vf12's range base, so the recorder can log item offsets
uint32_t gKeyPtr = 0;          // the address of the world's `key` (vf52's first argument points at it)
uint32_t gFoundAddr34 = 0;     // the object FindSlot34 hands out (a different one from gFoundAddr)
uint32_t gFoundAddr2 = 0;      // the object value slot 7 hands out (obj2)
uint32_t gL0 = 0;              // the object the fake slot 22 hands out (obj84)
uint32_t gOut3 = 0;            // the object the fake slot 3 hands out (obj2)
uint32_t gSlot49Result = 0;    // what this->slot49 returns

uint32_t IdOf(const void *p)
{
    for (const auto &e : gIds)
        if (e.first == p) return e.second;
    return 0xffff;
}

// A key that is the address of the world's `key` is recorded as a fixed id, so it compares equal
// across the two worlds.
uint32_t NormKey(uint32_t v)
{
    return v == gKeyPtr ? 0x6b657921u : v;
}

uint32_t IdOfWord(uint32_t v)
{
    return IdOf(reinterpret_cast<const void *>(static_cast<uintptr_t>(v)));
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(IdOf(self));
}

// Fake vtable slots. Recorders are __fastcall so they pop their stack arguments like the game's
// callees do; `dummy` takes edx.
void __fastcall LockEnter(void *self, void *)
{
    Note(1, self);
}

uint32_t __fastcall LockLeave(void *self, void *)
{
    Note(2, self);
    return gLeaveResult;
}

void __fastcall ObjSlot10(void *self, void *)
{
    Note(10, self);
}

void __fastcall ObjSlot4(void *self, void *, uint32_t a)
{
    Note(40, self);
    gTrace.push_back(a);
}

uint32_t __fastcall SelfSlot4(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    Note(4, self);
    for (uint32_t a : {a1, a2, a3, a4, a5, a6}) gTrace.push_back(a);
    return gCallResult;
}

uint32_t __fastcall SelfSlot8(void *self, void *, void *obj)
{
    Note(8, self);
    gTrace.push_back(IdOf(obj));
    return gCallResult;
}

uint32_t __fastcall SelfSlot22(void *self, void *, uint32_t a, uint32_t b)
{
    Note(22, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    return gCallResult;
}

// Slot 15 of the fake deque objects: returns gDqRet for the object (ids 20..23).
uint32_t __fastcall DqSlot15(void *self, void *)
{
    Note(115, self);
    uint32_t id = IdOf(self);
    return id >= 20 && id < 24 ? gDqRet[id - 20] : 0;
}

// The find slots on the manager: record the key, then hand out the found object through `out`.
uint8_t __fastcall FindSlot34(void *self, void *, uint32_t key, uint32_t *out)
{
    Note(34, self);
    gTrace.push_back(NormKey(key));
    *out = gFoundAddr34;
    return static_cast<uint8_t>(gFindResult);
}

uint8_t __fastcall FindSlot23(void *self, void *, const uint32_t *key, uint32_t *out)
{
    Note(23, self);
    gTrace.push_back(*key);
    *out = gFoundAddr;
    return static_cast<uint8_t>(gFindResult);
}

// Results of the value objects' slots vary from call to call (so ORs of them are not just the last
// one): the sequence restarts in every run, so both sides see the same results.
uint32_t gSeq = 0;

uint32_t NextResult()
{
    return gCallResult ^ (++gSeq * 0x9e3779b9u);
}

// vf12's callee on the manager: records the item's offset from the range base and a2.
uint32_t __fastcall SelfSlot13(void *self, void *, uint32_t item, uint32_t a2)
{
    Note(13, self);
    gTrace.push_back(item - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gRangeBase)));
    gTrace.push_back(a2);
    return NextResult();
}

// Recorders for the value objects' slots, with the argument count of the game's signature. A slot
// that is called with N stack arguments gets a recorder that takes N (after the dummy edx).
template <uint32_t Slot> uint32_t __fastcall Rec0(void *self, void *)
{
    Note(Slot, self);
    return NextResult();
}

template <uint32_t Slot> uint32_t __fastcall Rec1(void *self, void *, uint32_t a)
{
    Note(Slot, self);
    gTrace.push_back(a);
    return NextResult();
}

template <uint32_t Slot> uint32_t __fastcall Rec2(void *self, void *, uint32_t a, uint32_t b)
{
    Note(Slot, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    return NextResult();
}

template <uint32_t Slot> uint32_t __fastcall Rec3(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Note(Slot, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    gTrace.push_back(c);
    return NextResult();
}

template <uint32_t Slot> uint32_t __fastcall Rec5(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    Note(Slot, self);
    for (uint32_t x : {a, b, c, d, e}) gTrace.push_back(x);
    return NextResult();
}

// A result the callers test as a byte: zero about half the time.
uint32_t ByteResult()
{
    uint32_t r = NextResult();
    return (r & 1) ? r : (r & 0xffffff00u);
}

// this->slot49(a, b, c) of the manager (vf4).
uint32_t __fastcall SelfSlot49(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Note(49, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    gTrace.push_back(c);
    return gSlot49Result;
}

// this->slot50(a, obj) of the manager (vf4).
uint32_t __fastcall SelfSlot50(void *self, void *, uint32_t a, uint32_t obj)
{
    Note(50, self);
    gTrace.push_back(a);
    gTrace.push_back(IdOfWord(obj));
    return NextResult();
}

// this->slot52(a1, iid, &out, a4, a5, a6) of the manager (vf4, vf5): hands out gFoundAddr through
// `out` (even when it returns 0, so the release on failure is seen).
uint32_t __fastcall SelfSlot52(void *self, void *, uint32_t a1, uint32_t iid, uint32_t *out, uint32_t a4, uint32_t a5, uint32_t a6)
{
    Note(52, self);
    for (uint32_t x : {NormKey(a1), iid, a4, a5, a6}) gTrace.push_back(x);
    *out = gFoundAddr;
    return static_cast<uint8_t>(gFindResult);
}

// this->slot53(a1, obj1, obj, a2, a3, a4, a5) of the manager (vf52).
uint32_t __fastcall SelfSlot53(void *self, void *, uint32_t a1, uint32_t obj1, uint32_t obj, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    Note(53, self);
    gTrace.push_back(NormKey(a1));
    gTrace.push_back(IdOfWord(obj1));
    gTrace.push_back(IdOfWord(obj));
    for (uint32_t x : {a2, a3, a4, a5}) gTrace.push_back(x);
    return gCallResult;
}

// Slot 7 of a value object (vf49): records a1 and hands out obj2 through `out`; the result is a
// byte that is zero about half the time.
uint32_t __fastcall ValueSlot7(void *self, void *, uint32_t a1, void **out)
{
    Note(7, self);
    gTrace.push_back(a1);
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(gFoundAddr2));
    return ByteResult();
}

// The fake object of vf53 (both fake objects share this vtable). Slot 3 (a2's, the key, the
// interface id, the out pointer, two arguments) hands out gOut3 through `out`; slot 22 (a3's: the
// argument, the out pointer, a flag) hands out gL0.
uint32_t __fastcall FakeSlot3(void *self, void *, uint32_t key, uint32_t iid, uint32_t *out, uint32_t a6, uint32_t a7)
{
    Note(3, self);
    gTrace.push_back(IdOfWord(key));
    gTrace.push_back(iid);
    gTrace.push_back(a6);
    gTrace.push_back(a7);
    *out = gOut3;
    return static_cast<uint8_t>(gFindResult);
}

uint32_t __fastcall FakeSlot22(void *self, void *, uint32_t a, uint32_t *out, uint32_t flag)
{
    Note(22, self);
    gTrace.push_back(a);
    gTrace.push_back(flag);
    *out = gL0;
    return static_cast<uint8_t>(gFindResult);
}

// Slots 24 and 25 of the fake object: take the key object (recorded by id).
template <uint32_t Slot> uint32_t __fastcall RecKey1(void *self, void *, uint32_t key)
{
    Note(Slot, self);
    gTrace.push_back(IdOfWord(key));
    return NextResult();
}

constexpr size_t kSelfBytes = 0x100;
constexpr size_t kObjBytes = 0x20;
constexpr int kValueSlots = 24;

struct World {
    alignas(8) uint8_t self[kSelfBytes];
    alignas(8) uint8_t obj84[kObjBytes];
    alignas(8) uint8_t obj2[kObjBytes];
    alignas(8) uint8_t values[4][kObjBytes];
    void *selfVT[54];
    void *lockVT[3];
    void *obj84VT[11];
    void *obj2VT[5];
    void *valueVT[kValueSlots];
    std::vector<cGZPersistListNode> nodes;
    cGZPersistListNode sentinel;
    std::vector<cGZPersistValueNode> vnodes;
    cGZPersistValueNode vsentinel;
    uint32_t key;
    void *holder; // vf40's holder: a pointer to values[1]
    // Deque at +0x70 (SetDeque fills it): four fake objects whose slots record calls, 8 blocks of
    // four dwords, the map and a fake proxy.
    alignas(8) uint8_t dq[4][kObjBytes];
    void *dqVT[kValueSlots];
    uint32_t blocks[8][4];
    uint32_t *map[8];
    uint32_t proxy[2];
    std::vector<uint32_t> dqVals; // the deque's elements in order (object addresses or dwords)
    // vf12's range {begin, end} over up to four 16-byte items.
    alignas(8) uint8_t rangeBuf[4 * 16];
    void *range[2];
    // A fake cRZSystemService subobject at +4 (only its vtable is used: slot 1 records AddRef).
    void *subVT[13];
    uint32_t outVal; // the out pointer for QueryInterface and vf33
    // Two fake objects for vf53 (fobj[0] takes slot 3, fobj[1] takes slots 22, 24 and 25).
    alignas(8) uint8_t fobj[2][kObjBytes];
    void *fobjVT[32];
    // The array vf38 fills, and the count it takes and returns.
    alignas(8) uint32_t arr[8];
    uint32_t cnt;
};

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uintptr_t Addr(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

void FillValueVT(void **vt)
{
    for (int i = 0; i < kValueSlots; ++i) vt[i] = nullptr;
    vt[1] = reinterpret_cast<void *>(&Rec0<1>);
    vt[2] = reinterpret_cast<void *>(&Rec0<2>);
    vt[4] = reinterpret_cast<void *>(&Rec5<4>);
    vt[6] = reinterpret_cast<void *>(&Rec0<6>);
    vt[8] = reinterpret_cast<void *>(&Rec2<8>);
    vt[10] = reinterpret_cast<void *>(&Rec1<10>);
    vt[11] = reinterpret_cast<void *>(&Rec1<11>);
    vt[14] = reinterpret_cast<void *>(&Rec1<14>);
    vt[15] = reinterpret_cast<void *>(&Rec0<15>);
    vt[16] = reinterpret_cast<void *>(&Rec1<16>);
    vt[17] = reinterpret_cast<void *>(&Rec0<17>);
    vt[22] = reinterpret_cast<void *>(&Rec3<22>);
    vt[5] = reinterpret_cast<void *>(&Rec0<5>);
    vt[9] = reinterpret_cast<void *>(&Rec1<9>);
    vt[12] = reinterpret_cast<void *>(&Rec2<12>);
    vt[0] = reinterpret_cast<void *>(&Rec2<0>);
    vt[7] = reinterpret_cast<void *>(&ValueSlot7);
}

void Build(World &w, const difftest::Context &ctx, uint32_t seed)
{
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.self, sizeof(w.self));
    fill(w.obj84, sizeof(w.obj84));
    fill(w.obj2, sizeof(w.obj2));
    fill(w.values, sizeof(w.values));

    std::memcpy(w.selfVT, ctx.image.At(cGZPersistResourceManager::kVTable), sizeof(w.selfVT));
    w.selfVT[4] = reinterpret_cast<void *>(&SelfSlot4);
    w.selfVT[8] = reinterpret_cast<void *>(&SelfSlot8);
    w.selfVT[22] = reinterpret_cast<void *>(&SelfSlot22);
    w.selfVT[23] = reinterpret_cast<void *>(&FindSlot23);
    w.selfVT[34] = reinterpret_cast<void *>(&FindSlot34);
    w.lockVT[0] = nullptr;
    w.lockVT[1] = reinterpret_cast<void *>(&LockEnter);
    w.lockVT[2] = reinterpret_cast<void *>(&LockLeave);
    for (auto &slot : w.obj84VT) slot = nullptr;
    w.obj84VT[1] = reinterpret_cast<void *>(&Rec0<1>);
    w.obj84VT[2] = reinterpret_cast<void *>(&Rec0<2>);
    w.obj84VT[10] = reinterpret_cast<void *>(&ObjSlot10);
    for (auto &slot : w.obj2VT) slot = nullptr;
    w.obj2VT[1] = reinterpret_cast<void *>(&Rec0<1>);
    w.obj2VT[2] = reinterpret_cast<void *>(&Rec0<2>);
    w.obj2VT[4] = reinterpret_cast<void *>(&ObjSlot4);
    FillValueVT(w.valueVT);

    // List of 0..4 nodes behind the sentinel, with random values.
    uint32_t count = rng() % 5;
    w.nodes.assign(count, cGZPersistListNode{});
    for (auto &n : w.nodes) {
        n.key = rng();
        n.value = rng();
    }
    w.sentinel = cGZPersistListNode{};
    w.sentinel.next = count ? &w.nodes[0] : &w.sentinel;
    w.sentinel.prev = count ? &w.nodes.back() : &w.sentinel;
    for (uint32_t i = 0; i < count; ++i) {
        w.nodes[i].next = i + 1 < count ? &w.nodes[i + 1] : &w.sentinel;
        w.nodes[i].prev = i ? &w.nodes[i - 1] : &w.sentinel;
    }
    w.key = rng();

    // Value list (lock C's list) of 0..4 nodes; the values are the first three value objects.
    uint32_t vcount = rng() % 5;
    w.vnodes.assign(vcount, cGZPersistValueNode{});
    for (auto &n : w.vnodes) n.value = w.values[rng() % 3];
    w.vsentinel = cGZPersistValueNode{};
    w.vsentinel.next = vcount ? &w.vnodes[0] : &w.vsentinel;
    w.vsentinel.prev = vcount ? &w.vnodes.back() : &w.vsentinel;
    for (uint32_t i = 0; i < vcount; ++i) {
        w.vnodes[i].next = i + 1 < vcount ? &w.vnodes[i + 1] : &w.vsentinel;
        w.vnodes[i].prev = i ? &w.vnodes[i - 1] : &w.vsentinel;
    }
    for (int k = 0; k < 4; ++k) Put(w.values[k], 0x00, Addr(w.valueVT));
    w.holder = w.values[1];

    Put(w.self, 0x00, Addr(w.selfVT));
    Put(w.self, 0x20, Addr(w.lockVT));
    Put(w.self, 0x2c, Addr(&w.sentinel));
    Put(w.self, 0x30, count);
    Put(w.self, 0x68, Addr(w.lockVT));
    Put(w.self, 0x84, rng() % 4 ? Addr(w.obj84) : uintptr_t(0));
    Put(w.self, 0x88, Addr(w.lockVT));
    Put(w.self, 0x90, Addr(&w.vsentinel));
    Put(w.self, 0x94, vcount);
    Put(w.obj84, 0x00, Addr(w.obj84VT));
    Put(w.obj2, 0x00, Addr(w.obj2VT));

    // Deque objects: slot 1 AddRef, slot 10 and 15 (no arguments), 19 and 20 with arguments.
    for (auto &slot : w.dqVT) slot = nullptr;
    w.dqVT[1] = reinterpret_cast<void *>(&Rec0<101>);
    w.dqVT[2] = reinterpret_cast<void *>(&Rec0<102>); // Release (vf30)
    w.dqVT[10] = reinterpret_cast<void *>(&Rec0<110>);
    w.dqVT[15] = reinterpret_cast<void *>(&DqSlot15);
    w.dqVT[19] = reinterpret_cast<void *>(&Rec2<119>);
    w.dqVT[20] = reinterpret_cast<void *>(&Rec1<120>);
    for (auto &obj : w.dq) {
        fill(obj, sizeof(obj));
        Put(obj, 0x00, Addr(w.dqVT));
    }
    // vf12's range: 0..4 items.
    fill(w.rangeBuf, sizeof(w.rangeBuf));
    uint32_t items = rng() % 5;
    w.range[0] = w.rangeBuf;
    w.range[1] = w.rangeBuf + 16 * items;
    // The subobject: slot 1 (AddRef) records, the rest is unused.
    for (auto &slot : w.subVT) slot = nullptr;
    w.subVT[1] = reinterpret_cast<void *>(&Rec0<301>);
    w.selfVT[1] = reinterpret_cast<void *>(&Rec0<201>);
    w.selfVT[13] = reinterpret_cast<void *>(&SelfSlot13);
    Put(w.self, 0x04, Addr(w.subVT));
    w.outVal = 0;

    // The slots the new functions call on the manager, the QueryInterface of obj2 (out objects of
    // vf49 and vf53), and the fake objects of vf53.
    w.selfVT[49] = reinterpret_cast<void *>(&SelfSlot49);
    w.selfVT[50] = reinterpret_cast<void *>(&SelfSlot50);
    w.selfVT[52] = reinterpret_cast<void *>(&SelfSlot52);
    w.selfVT[53] = reinterpret_cast<void *>(&SelfSlot53);
    w.obj2VT[0] = reinterpret_cast<void *>(&Rec2<0>);
    for (auto &slot : w.fobjVT) slot = nullptr;
    w.fobjVT[3] = reinterpret_cast<void *>(&FakeSlot3);
    w.fobjVT[22] = reinterpret_cast<void *>(&FakeSlot22);
    w.fobjVT[24] = reinterpret_cast<void *>(&RecKey1<124>);
    w.fobjVT[25] = reinterpret_cast<void *>(&RecKey1<125>);
    for (auto &obj : w.fobj) {
        fill(obj, sizeof(obj));
        Put(obj, 0x00, Addr(w.fobjVT));
    }
    fill(w.arr, sizeof(w.arr));
    w.cnt = 0;
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace;
    bool changed = false;
    bool threw = false;
    std::string what;
};

void SetIds(const World &w)
{
    gIds = {{w.self, 0},
            {w.obj84, 1},
            {w.obj2, 2},
            {w.self + 0x20, 3},
            {w.self + 0x68, 4},
            {w.self + 0x88, 5},
            {w.self + 0x98, 7},
            {w.values[0], 10},
            {w.values[1], 11},
            {w.values[2], 12},
            {w.values[3], 13},
            {w.self + 4, 6},
            {w.dq[0], 20},
            {w.dq[1], 21},
            {w.dq[2], 22},
            {w.dq[3], 23},
            {w.fobj[0], 30},
            {w.fobj[1], 31}};
}

// Where the world lives. Original and ours run on this one world, rebuilt from the seed before each, so they see
// it at the same address and the id map (and every comparison against the world's addresses) is the same function
// on both sides: equal words stay equal and different words stay different, whatever the call wrote. Two worlds at
// two addresses made the map depend on the heap layout (a random word equal to one world's address was renamed in
// that world only; lane NB, tests/gz_persist_db_segment_multi_packed_files.cpp). The list nodes a run allocates on
// the heap are not mapped (they all read as one id), so they may sit anywhere; Cleanup frees them between runs.
World &SharedWorld()
{
    static World *const w = new World;
    return *w;
}

template <typename Call> Outcome Run(World &w, Call call)
{
    SetIds(w);
    gFoundAddr = static_cast<uint32_t>(Addr(w.values[0]));
    gFoundAddr34 = static_cast<uint32_t>(Addr(w.values[2]));
    gFoundAddr2 = static_cast<uint32_t>(Addr(w.obj2));
    gL0 = static_cast<uint32_t>(Addr(w.obj84));
    gOut3 = static_cast<uint32_t>(Addr(w.obj2));
    gKeyPtr = static_cast<uint32_t>(Addr(&w.key));
    gSeq = 0;
    uint8_t before[kSelfBytes + 2 * kObjBytes];
    std::memcpy(before, w.self, kSelfBytes);
    std::memcpy(before + kSelfBytes, w.obj84, kObjBytes);
    std::memcpy(before + kSelfBytes + kObjBytes, w.obj2, kObjBytes);

    gTrace.clear();
    Outcome out;
    try {
        out.ret = call();
    } catch (const std::exception &e) {
        out.threw = true;
        out.what = e.what();
    }
    out.trace = gTrace;
    out.changed = std::memcmp(before, w.self, kSelfBytes) != 0
        || std::memcmp(before + kSelfBytes, w.obj84, kObjBytes) != 0
        || std::memcmp(before + kSelfBytes + kObjBytes, w.obj2, kObjBytes) != 0;
    return out;
}

// Returns from vf47 may be the address of the fake object (different in each world): mark it.
uint32_t Normalise(uint32_t value, const World &w)
{
    return value == Addr(w.obj84) ? 0xabcdef01u : value;
}

// Runs both sides on identical worlds. With `plainField84`, the dword at +0x84 is a random
// number instead of the object pointer (vf47 reads it as a plain value).
template <typename Prep, typename OrigCall, typename OursCall>
void CompareWith(difftest::Context &ctx, const char *name, uint32_t seed, Prep prep, OrigCall origCall, OursCall oursCall)
{
    World &w = SharedWorld();
    Build(w, ctx, seed);
    prep(w);
    Outcome oa = Run(w, [&] { return Normalise(origCall(w), w); });
    Build(w, ctx, seed);
    prep(w);
    Outcome ob = Run(w, [&] { return Normalise(oursCall(w), w); });
    ++ctx.cases;
    if (oa.ret != ob.ret || oa.trace != ob.trace || oa.threw != ob.threw || oa.what != ob.what) {
        ctx.Fail("%s seed %u: original returned %u (%zu trace words, threw %d), ours %u (%zu, threw %d)", name, seed, oa.ret,
                 oa.trace.size(), oa.threw, ob.ret, ob.trace.size(), ob.threw);
    }
    if (oa.changed || ob.changed) ctx.Fail("%s seed %u: the manager's bytes changed (original %d, ours %d)", name, seed, oa.changed, ob.changed);
}

// Like CompareWith, with the dword at +0x84 optionally replaced by a plain number.
template <typename OrigCall, typename OursCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, bool plainField84, OrigCall origCall, OursCall oursCall)
{
    CompareWith(
        ctx, name, seed,
        [&](World &w) {
            if (!plainField84) return;
            uint32_t plain = seed * 2654435761u;
            Put(w.self, 0x84, plain);
        },
        origCall, oursCall);
}

// The state that vf41 and vf46 are allowed to change: the value list (size, then the value ids in
// order) and the object at +0x84 (its id).
std::vector<uint32_t> Summary(const World &w)
{
    std::vector<uint32_t> s;
    uint32_t size, head, field84;
    std::memcpy(&size, w.self + 0x94, 4);
    std::memcpy(&head, w.self + 0x90, 4);
    std::memcpy(&field84, w.self + 0x84, 4);
    s.push_back(size);
    const auto *sentinel = reinterpret_cast<const cGZPersistValueNode *>(static_cast<uintptr_t>(head));
    int guard = 0;
    for (const cGZPersistValueNode *n = sentinel->next; n != sentinel && guard < 64; n = n->next, ++guard)
        s.push_back(IdOf(n->value));
    s.push_back(0x80000000u | IdOf(reinterpret_cast<const void *>(static_cast<uintptr_t>(field84))));
    return s;
}

bool Owned(const World &w, const cGZPersistValueNode *n)
{
    auto p = Addr(n);
    return p >= Addr(w.vnodes.data()) && p < Addr(w.vnodes.data() + w.vnodes.size());
}

// Frees the list nodes the run allocated (the ones that are not in the world's own vector).
void Cleanup(const World &w)
{
    uint32_t head;
    std::memcpy(&head, w.self + 0x90, 4);
    auto *sentinel = reinterpret_cast<cGZPersistValueNode *>(static_cast<uintptr_t>(head));
    std::vector<cGZPersistValueNode *> extra;
    int guard = 0;
    for (cGZPersistValueNode *n = sentinel->next; n != sentinel && guard < 64; n = n->next, ++guard)
        if (!Owned(w, n)) extra.push_back(n);
    for (auto *n : extra) ::operator delete(n);
}

// Like Compare, for the slots that change the value list or +0x84: the results, the recorded calls
// and the Summary must match, and each side frees its own new list nodes afterwards.
template <typename OrigCall, typename OursCall>
void CompareMutating(difftest::Context &ctx, const char *name, uint32_t seed, bool overflow, OrigCall origCall, OursCall oursCall)
{
    World &w = SharedWorld();
    auto prepare = [&] {
        Build(w, ctx, seed);
        if (overflow) Put(w.self, 0x94, 0x15555555u);
    };
    // The summaries use the id map that Run installs, so each is taken right after its own run; each side frees
    // its own new list nodes before the world is rebuilt.
    prepare();
    Outcome oa = Run(w, [&] { return static_cast<uint32_t>(origCall(w)); });
    std::vector<uint32_t> sa = Summary(w);
    Cleanup(w);
    prepare();
    Outcome ob = Run(w, [&] { return static_cast<uint32_t>(oursCall(w)); });
    std::vector<uint32_t> sb = Summary(w);
    Cleanup(w);
    ++ctx.cases;
    if (oa.ret != ob.ret || oa.trace != ob.trace || oa.threw != ob.threw || oa.what != ob.what) {
        ctx.Fail("%s seed %u: original returned %u (%zu trace words, threw %d), ours %u (%zu, threw %d)", name, seed, oa.ret,
                 oa.trace.size(), oa.threw, ob.ret, ob.trace.size(), ob.threw);
    }
    if (sa != sb) ctx.Fail("%s seed %u: the value list or +0x84 differs (%zu words against %zu)", name, seed, sa.size(), sb.size());
}

uint32_t Result(std::mt19937 &rng)
{
    // Mostly random, sometimes with a zero low byte (so the callers' byte tests see zero).
    return rng() % 3 ? rng() : (rng() & 0xffffff00u);
}

cGZPersistResourceManager *Mgr(World &w)
{
    return reinterpret_cast<cGZPersistResourceManager *>(w.self);
}

// Fills the deque at +0x70: 8 blocks of random dwords, an offset and 0..6 elements. With `objects`
// the elements are addresses of w.dq[] (fake objects whose slots record calls), otherwise random
// dwords. The fields are written before the snapshot; w.dqVals gets the elements in order.
void SetDeque(World &w, uint32_t dqSeed, bool objects)
{
    std::mt19937 rng(dqSeed);
    for (auto &block : w.blocks)
        for (auto &x : block) x = rng();
    for (int i = 0; i < 8; ++i) w.map[i] = w.blocks[i];
    uint32_t size = rng() % 7;
    uint32_t offset = rng() % 32;
    w.dqVals.clear();
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t pos = offset + i;
        uint32_t value = objects ? static_cast<uint32_t>(Addr(w.dq[rng() % 4])) : rng();
        w.blocks[(pos >> 2) & 7][pos & 3] = value;
        w.dqVals.push_back(value);
    }
    // The proxy's first dword points back at the deque (self + 0x70).
    w.proxy[0] = static_cast<uint32_t>(Addr(w.self + 0x70));
    w.proxy[1] = 0;
    Put(w.self, 0x70, Addr(w.proxy));
    Put(w.self, 0x74, Addr(w.map));
    Put(w.self, 0x78, uint32_t(8));
    Put(w.self, 0x7c, offset);
    Put(w.self, 0x80, size);
    std::mt19937 keys(dqSeed ^ 0x5bd1e995u);
    for (auto &r : gDqRet) r = keys();
}

// An element of the deque (or `fallback`); the probe decides, and the world's own elements are used.
uint32_t PickValue(const World &w, uint32_t probe, uint32_t fallback)
{
    if (w.dqVals.empty() || probe % 3 == 0) return fallback;
    return w.dqVals[(probe >> 2) % w.dqVals.size()];
}

// The key that finds one of the deque's objects through its slot 15 (object mode only).
uint32_t PickKey(const World &w, uint32_t probe, uint32_t fallback)
{
    if (w.dqVals.empty() || probe % 3 == 0) return fallback;
    uint32_t value = w.dqVals[(probe >> 2) % w.dqVals.size()];
    return gDqRet[(value - static_cast<uint32_t>(Addr(w.dq[0]))) / kObjBytes];
}

// The return value (0 or 1) and which object the out pointer holds (its id, so addresses compare).
uint32_t WithOut(uint8_t r, const World &w)
{
    return r | (IdOf(reinterpret_cast<const void *>(static_cast<uintptr_t>(w.outVal))) << 8);
}

} // namespace

DIFF_TEST(GZPersistResourceManager_Deques, 0x9b5100)
{
    auto qi = ctx.Original<QueryInterfaceFn>(0x9b5100);
    auto vf10 = ctx.Original<Vf11Fn>(0x9b5d30);
    auto vf12 = ctx.Original<Vf12Fn>(0x9b5560);
    auto vf15 = ctx.Original<Vf11Fn>(0x9b59c0);
    auto vf16 = ctx.Original<Vf16Fn>(0x9b4d90);
    auto vf18 = ctx.Original<Vf16Fn>(0x9b4970);
    auto vf24 = ctx.Original<Vf24Fn>(0x9b47b0);
    auto vf31 = ctx.Original<Vf11Fn>(0x9b5940);
    auto vf32 = ctx.Original<NoArgU8Fn>(0x9b4890);
    auto vf33 = ctx.Original<Vf33Fn>(0x9b46f0);
    auto vf36 = ctx.Original<Vf26Fn>(0x9b4ed0);

    std::mt19937 rng(41);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        uint32_t dqSeed = rng();
        bool objects = rng() % 2; // object mode: the elements are fake objects (vf15, vf18, vf32, vf33)
        uint32_t probe = rng();
        gLeaveResult = rng();
        gCallResult = Result(rng);
        gFindResult = Result(rng);
        uint32_t a1 = rng(), a2 = rng();
        unsigned valueIndex = rng() % 4;
        uint32_t index = rng() % 4 ? rng() % 8 : rng();
        // riid 1 would AddRef through this+8, which is not modelled: never picked.
        uint32_t riid = rng() % 3 == 0 ? 0x287fb697u : (rng() % 2 ? 0x656b8efcu : rng());
        if (riid == 1) riid = 2;

        auto prep = [&](World &w) { SetDeque(w, dqSeed, objects); };

        CompareWith(ctx, "QueryInterface", seed, prep,
                    [&](World &w) {
                        w.outVal = 0;
                        uint8_t r = qi(w.self, riid, reinterpret_cast<void **>(&w.outVal));
                        return WithOut(r, w);
                    },
                    [&](World &w) {
                        w.outVal = 0;
                        uint8_t r = Mgr(w)->QueryInterface(riid, reinterpret_cast<void **>(&w.outVal));
                        return WithOut(r, w);
                    });
        CompareWith(ctx, "vf10", seed, prep, [&](World &w) { return vf10(w.self, a1); },
                    [&](World &w) { return Mgr(w)->vf10(a1); });
        CompareWith(ctx, "vf12", seed, prep,
                    [&](World &w) {
                        gRangeBase = w.rangeBuf;
                        return vf12(w.self, w.range, a2);
                    },
                    [&](World &w) {
                        gRangeBase = w.rangeBuf;
                        return Mgr(w)->vf12(w.range, a2);
                    });
        CompareWith(ctx, "vf16", seed, prep, [&](World &w) { return vf16(w.self, a1, a2); },
                    [&](World &w) { return Mgr(w)->vf16(a1, a2); });
        CompareWith(ctx, "vf24", seed, prep, [&](World &w) { return vf24(w.self, w.values[valueIndex], a2); },
                    [&](World &w) { return Mgr(w)->vf24(w.values[valueIndex], a2); });
        CompareWith(ctx, "vf31", seed, prep, [&](World &w) { return vf31(w.self, PickValue(w, probe, a1)); },
                    [&](World &w) { return Mgr(w)->vf31(PickValue(w, probe, a1)); });
        if (objects) {
            CompareWith(ctx, "vf15", seed, prep, [&](World &w) { return vf15(w.self, a1); },
                        [&](World &w) { return Mgr(w)->vf15(a1); });
            CompareWith(ctx, "vf18", seed, prep, [&](World &w) { return vf18(w.self, a1, a2); },
                        [&](World &w) { return Mgr(w)->vf18(a1, a2); });
            CompareWith(ctx, "vf32", seed, prep, [&](World &w) { return vf32(w.self); },
                        [&](World &w) { return Mgr(w)->vf32(); });
            CompareWith(ctx, "vf33", seed, prep,
                        [&](World &w) {
                            w.outVal = 0;
                            uint8_t r = vf33(w.self, PickKey(w, probe, a1), reinterpret_cast<void **>(&w.outVal));
                            return WithOut(r, w);
                        },
                        [&](World &w) {
                            w.outVal = 0;
                            uint8_t r = Mgr(w)->vf33(PickKey(w, probe, a1), reinterpret_cast<void **>(&w.outVal));
                            return WithOut(r, w);
                        });
        } else {
            // vf36 returns an element as is, which is an address in object mode: plain dwords only.
            CompareWith(ctx, "vf36", seed, prep, [&](World &w) { return vf36(w.self, index); },
                        [&](World &w) { return Mgr(w)->vf36(index); });
        }
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

DIFF_TEST(GZPersistResourceManager_All, 0x9b4d70)
{
    auto vf3 = ctx.Original<Vf3Fn>(0x9b4d70);
    auto vf9 = ctx.Original<Vf9Fn>(0x9b5430);
    auto vf23 = ctx.Original<Vf23Fn>(0x9b4790);
    auto vf25 = ctx.Original<NoArgFn>(0x9b4b50);
    auto vf26 = ctx.Original<Vf26Fn>(0x9b4b10);
    auto vf35 = ctx.Original<NoArgFn>(0x9b4f50);
    auto vf47 = ctx.Original<NoArgFn>(0x9b4f20);
    auto vf48 = ctx.Original<NoArgFn>(0x9b4400);

    std::mt19937 rng(11);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gLeaveResult = rng();
        gCallResult = rng();
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), a4 = rng(), a5 = rng();
        uint32_t b = rng();
        // Indices inside and past the list, and huge ones (the comparison is unsigned).
        uint32_t index = rng() % 4 ? rng() % 7 : rng();

        Compare(ctx, "vf3", seed, false, [&](World &w) { return vf3(w.self, a1, a2, a3, a4, a5); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf3(a1, a2, a3, a4, a5); });
        Compare(ctx, "vf9", seed, false, [&](World &w) { return vf9(w.self, a1, w.obj2); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf9(a1, w.obj2); });
        Compare(ctx, "vf23", seed, false, [&](World &w) { return vf23(w.self, &w.key, b); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf23(&w.key, b); });
        Compare(ctx, "vf25", seed, false, [&](World &w) { return vf25(w.self); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf25(); });
        Compare(ctx, "vf26", seed, false, [&](World &w) { return vf26(w.self, index); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf26(index); });
        Compare(ctx, "vf35", seed, false, [&](World &w) { return vf35(w.self); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf35(); });
        Compare(ctx, "vf47", seed, false, [&](World &w) { return vf47(w.self); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf47(); });
        Compare(ctx, "vf47 (plain +0x84)", seed, true, [&](World &w) { return vf47(w.self); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf47(); });
        Compare(ctx, "vf48", seed, false, [&](World &w) { return vf48(w.self); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf48(); });
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

DIFF_TEST(GZPersistResourceManager_Lists, 0x9b4f80)
{
    auto vf11 = ctx.Original<Vf11Fn>(0x9b4f80);
    auto vf39 = ctx.Original<Vf39Fn>(0x9b50a0);
    auto vf40 = ctx.Original<Vf40Fn>(0x9b43e0);
    auto vf41 = ctx.Original<Vf41Fn>(0x9b4290);
    auto vf43 = ctx.Original<NoArgU8Fn>(0x9b4ff0);
    auto vf44 = ctx.Original<Vf44Fn>(0x9b5740);
    auto vf45 = ctx.Original<NoArgU8Fn>(0x9b4920);
    auto vf46 = ctx.Original<Vf46Fn>(0x9b5790);
    auto vf50 = ctx.Original<Vf50Fn>(0x9b4360);
    auto vf51 = ctx.Original<Vf51Fn>(0x9b44c0);
    auto onIdle = ctx.Original<OnIdleFn>(0x9b5040);

    std::mt19937 rng(23);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gLeaveResult = rng();
        gCallResult = Result(rng);
        gFindResult = Result(rng);
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), a4 = rng(), a5 = rng();
        // The flag argument of vf39: its low byte is zero about a third of the time.
        uint32_t flag = rng() % 3 ? rng() : (rng() & 0xffffff00u);
        unsigned valueIndex = rng() % 4;  // vf41's argument: one of the list's values, or a fresh one
        unsigned objIndex = rng() % 4;    // vf46's argument: null, a value, or one of the two fakes
        bool overflow = rng() % 8 == 0;

        auto pickObj = [objIndex](World &w) -> void * {
            switch (objIndex) {
            case 0: return nullptr;
            case 1: return w.values[0];
            case 2: return w.obj84;
            default: return w.obj2;
            }
        };

        Compare(ctx, "vf11", seed, false, [&](World &w) { return vf11(w.self, a1); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf11(a1); });
        Compare(ctx, "vf39", seed, false, [&](World &w) { return vf39(w.self, a1, a2, flag); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf39(a1, a2, flag); });
        Compare(ctx, "vf40", seed, false, [&](World &w) { return vf40(w.self, a1, &w.holder); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf40(a1, &w.holder); });
        Compare(ctx, "vf43", seed, false, [&](World &w) { return vf43(w.self); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf43(); });
        Compare(ctx, "vf44", seed, false,
                [&](World &w) { vf44(w.self, a1); return 0u; },
                [&](World &w) { reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf44(a1); return 0u; });
        Compare(ctx, "vf45", seed, false, [&](World &w) { return vf45(w.self); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf45(); });
        Compare(ctx, "vf50", seed, false, [&](World &w) { return vf50(w.self, a1, a2); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf50(a1, a2); });
        Compare(ctx, "vf51", seed, false, [&](World &w) { return vf51(w.self, &w.key, a2, a3, a4, a5); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf51(&w.key, a2, a3, a4, a5); });
        Compare(ctx, "OnIdle", seed, false, [&](World &w) { return onIdle(w.self + 4, a1); },
                [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self + 4)->OnIdle(a1); });

        CompareMutating(ctx, "vf41", seed, overflow, [&](World &w) { return vf41(w.self, w.values[valueIndex]); },
                        [&](World &w) { return reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf41(w.values[valueIndex]); });
        CompareMutating(ctx, "vf46", seed, false, [&](World &w) { vf46(w.self, pickObj(w)); return 0u; },
                        [&](World &w) { reinterpret_cast<cGZPersistResourceManager *>(w.self)->vf46(pickObj(w)); return 0u; });
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

DIFF_TEST(GZPersistResourceManager_Interfaces, 0x9b4e40)
{
    auto vf4 = ctx.Original<Vf4Fn>(0x9b4e40);
    auto vf5 = ctx.Original<Vf5Fn>(0x9b4d20);
    auto vf49 = ctx.Original<Vf49Fn>(0x9b4a40);
    auto vf52 = ctx.Original<Vf52Fn>(0x9b4430);
    auto vf53 = ctx.Original<Vf53Fn>(0x9b5140);

    std::mt19937 rng(59);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gLeaveResult = rng();
        gCallResult = Result(rng);
        gFindResult = Result(rng);
        gSlot49Result = Result(rng);
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), a4 = rng(), a5 = rng(), a7 = rng();
        // a6 is zero about a third of the time (vf52 then finds its object through slot 34).
        uint32_t a6 = rng() % 3 ? rng() : 0;

        // vf52 takes the address of the world's key as its first argument (slot 23 reads it).
        auto keyArg = [](World &w) { return static_cast<uint32_t>(Addr(&w.key)); };

        Compare(ctx, "vf4", seed, false, [&](World &w) { return vf4(w.self, a1, a2, a3, a4, a5, a6); },
                [&](World &w) { return Mgr(w)->vf4(a1, a2, a3, a4, a5, a6); });
        Compare(ctx, "vf5", seed, false, [&](World &w) { return vf5(w.self, a1, a2, a3, a4, a5); },
                [&](World &w) { return Mgr(w)->vf5(a1, a2, a3, a4, a5); });
        Compare(ctx, "vf49", seed, false, [&](World &w) { return vf49(w.self, a1, a2, a3); },
                [&](World &w) { return Mgr(w)->vf49(a1, a2, a3); });
        Compare(ctx, "vf52", seed, false, [&](World &w) { return vf52(w.self, keyArg(w), a2, a3, a4, a5, a6); },
                [&](World &w) { return Mgr(w)->vf52(keyArg(w), a2, a3, a4, a5, a6); });
        Compare(ctx, "vf53", seed, false, [&](World &w) { return vf53(w.self, a1, w.fobj[0], w.fobj[1], a4, a5, a6, a7); },
                [&](World &w) { return Mgr(w)->vf53(a1, w.fobj[0], w.fobj[1], a4, a5, a6, a7); });
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

DIFF_TEST(GZPersistResourceManager_DequeCopies, 0x9b4650)
{
    auto vf34 = ctx.Original<Vf33Fn>(0x9b4650);
    auto vf38 = ctx.Original<Vf38Fn>(0x9b4520);

    std::mt19937 rng(67);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        uint32_t dqSeed = rng();
        uint32_t a1 = rng();
        gLeaveResult = rng();
        gCallResult = Result(rng);
        // Elements are objects (vf34 calls their slot 20, vf38 AddRefs them).
        auto prep = [&](World &w) { SetDeque(w, dqSeed, true); };

        CompareWith(ctx, "vf34", seed, prep,
                    [&](World &w) {
                        w.outVal = 0;
                        uint8_t r = vf34(w.self, a1, reinterpret_cast<void **>(&w.outVal));
                        return WithOut(r, w);
                    },
                    [&](World &w) {
                        w.outVal = 0;
                        uint8_t r = Mgr(w)->vf34(a1, reinterpret_cast<void **>(&w.outVal));
                        return WithOut(r, w);
                    });

        // vf38: a null array (only the size comes back), a zero count (also just the size), or a
        // count past, inside and below the deque size.
        bool nullArr = rng() % 8 == 0;
        uint32_t wanted = rng() % 4 ? rng() % 8 : rng();
        if (rng() % 6 == 0) wanted = 0;
        auto runVf38 = [&](World &w, bool original) {
            w.cnt = wanted;
            void **arr = nullArr ? nullptr : reinterpret_cast<void **>(w.arr);
            uint32_t r = original ? vf38(w.self, arr, &w.cnt) : Mgr(w)->vf38(arr, &w.cnt);
            // The count and the array, by object id (unwritten entries are random words).
            gTrace.push_back(w.cnt);
            for (uint32_t x : w.arr) gTrace.push_back(IdOfWord(x));
            return r;
        };
        CompareWith(ctx, "vf38", seed, prep, [&](World &w) { return runVf38(w, true); },
                    [&](World &w) { return runVf38(w, false); });
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

namespace {

using Vf6Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Vf8Fn = uint8_t(__thiscall *)(void *, void *);
using Vf27Fn = uint32_t(__thiscall *)(void *, uint32_t);
using Vf37Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
using Vf42Fn = uint8_t(__thiscall *)(void *, void *);

// The word at address p (the callees below dereference the pointers the original passes).
// Records the 16-byte resource key at p (type, group, instance, resource): word 0 as an id, the others
// raw. The originals pass keys to these slots, so all four words must match (a 4-byte local in the
// replacement once let a callee overrun its stack in-game).
void TraceKey(uint32_t p);

uint32_t Deref(uint32_t p)
{
    uint32_t v = 0;
    std::memcpy(&v, reinterpret_cast<const void *>(static_cast<uintptr_t>(p)), sizeof(v));
    return v;
}

void TraceKey(uint32_t p)
{
    uint32_t w[4];
    std::memcpy(w, reinterpret_cast<const void *>(static_cast<uintptr_t>(p)), sizeof(w));
    gTrace.push_back(IdOfWord(w[0]));
    gTrace.push_back(w[1]);
    gTrace.push_back(w[2]);
    gTrace.push_back(w[3]);
}

// Writes a full 16-byte key at p, as a key getter does.
void WriteKey(uint32_t *p, uint32_t first)
{
    p[0] = first;
    p[1] = 0x6b657931;
    p[2] = 0x6b657932;
    p[3] = 0x6b657933;
}

// Slot 28 of the manager (vf27 tail-jumps to it): records its argument.
uint32_t __fastcall Slot28Rec(void *self, void *, uint32_t a)
{
    Note(28, self);
    gTrace.push_back(a);
    return NextResult();
}

// Slot 51 of the manager (vf6): records the key's value, the interface id and the two arguments,
// and hands out gFoundAddr2 (obj2) through the out pointer.
uint8_t __fastcall Vf51Rec(void *self, void *, uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5)
{
    Note(51, self);
    TraceKey(key);
    gTrace.push_back(iid);
    gTrace.push_back(a4);
    gTrace.push_back(a5);
    *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(out)) = gFoundAddr2;
    return static_cast<uint8_t>(gFindResult);
}

// Slot 50 of the manager (vf6, vf8): the first argument is a pointer to a local; record its value.
uint32_t __fastcall SlotVf50Deref(void *self, void *, uint32_t a, uint32_t obj)
{
    Note(50, self);
    TraceKey(a);
    gTrace.push_back(IdOfWord(obj));
    return NextResult();
}

// Slot 11 of the manager (vf8): its argument is a pointer to a local (the out object).
uint32_t __fastcall VfRec11Deref(void *self, void *, uint32_t a)
{
    Note(11, self);
    TraceKey(a);
    return ByteResult();
}

// Slot 4 of the object vf6 found: its argument is a pointer to the key local.
uint32_t __fastcall ObjSlot4Deref(void *self, void *, uint32_t a)
{
    Note(40, self);
    TraceKey(a);
    WriteKey(reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(a)), 0x6b657930);
    return 0;
}

// Slot 3 of a fake object (vf8): hands out gOut3 through its out pointer.
uint32_t __fastcall FakeObjSlot3(void *self, void *, uint32_t *out)
{
    Note(3, self);
    WriteKey(out, gOut3);
    return NextResult();
}

// The predicate of vf37: a __cdecl function (element, a2) returning a byte that is zero about half
// the time.
uint8_t __cdecl DequePred(uint32_t element, uint32_t a2)
{
    gTrace.push_back(IdOfWord(element));
    gTrace.push_back(a2);
    return static_cast<uint8_t>(ByteResult());
}

// Sets the manager's list at +0x90 to 0..4 heap nodes (vf42 frees the one it removes). Each node
// holds one of the first three value objects.
void HeapList(World &w, uint32_t seed)
{
    std::mt19937 rng(seed ^ 0x6a09e667u);
    uint32_t n = rng() % 5;
    w.vnodes.clear(); // nothing is owned by the vector any more, so Cleanup frees what is left
    cGZPersistValueNode *nodes[4] = {};
    for (uint32_t i = 0; i < n; ++i) {
        nodes[i] = static_cast<cGZPersistValueNode *>(::operator new(sizeof(cGZPersistValueNode)));
        nodes[i]->value = w.values[rng() % 3];
    }
    w.vsentinel = cGZPersistValueNode{};
    w.vsentinel.next = n ? nodes[0] : &w.vsentinel;
    w.vsentinel.prev = n ? nodes[n - 1] : &w.vsentinel;
    for (uint32_t i = 0; i < n; ++i) {
        nodes[i]->next = i + 1 < n ? nodes[i + 1] : &w.vsentinel;
        nodes[i]->prev = i ? nodes[i - 1] : &w.vsentinel;
    }
    Put(w.self, 0x90, Addr(&w.vsentinel));
    Put(w.self, 0x94, n);
}

void PrepVf6(World &w)
{
    // Lock D (+0x98) is a recording lock; slot 51, slot 50 and obj2's slot 4 take pointers.
    Put(w.self, 0x98, Addr(w.lockVT));
    w.selfVT[51] = reinterpret_cast<void *>(&Vf51Rec);
    w.selfVT[50] = reinterpret_cast<void *>(&SlotVf50Deref);
    w.obj2VT[4] = reinterpret_cast<void *>(&ObjSlot4Deref);
}

void PrepVf8(World &w)
{
    w.obj84VT[3] = reinterpret_cast<void *>(&FakeObjSlot3);
    w.obj2VT[3] = reinterpret_cast<void *>(&FakeObjSlot3);
    w.selfVT[11] = reinterpret_cast<void *>(&VfRec11Deref);
    w.selfVT[50] = reinterpret_cast<void *>(&SlotVf50Deref);
}

// Like CompareWith, for a slot that changes the value list: the results, traces and value-list
// summaries must match. The removed node is checked for being freed right after each side's call.
template <typename Prep, typename OrigCall, typename OursCall>
void CompareListWith(difftest::Context &ctx, const char *name, uint32_t seed, Prep prep, OrigCall origCall, OursCall oursCall)
{
    World &w = SharedWorld();
    Build(w, ctx, seed);
    prep(w);
    Outcome oa = Run(w, [&] { return static_cast<uint32_t>(origCall(w)); });
    std::vector<uint32_t> sa = Summary(w);
    Cleanup(w);
    Build(w, ctx, seed);
    prep(w);
    Outcome ob = Run(w, [&] { return static_cast<uint32_t>(oursCall(w)); });
    std::vector<uint32_t> sb = Summary(w);
    Cleanup(w);
    ++ctx.cases;
    if (oa.ret != ob.ret || oa.trace != ob.trace || oa.threw != ob.threw || oa.what != ob.what) {
        ctx.Fail("%s seed %u: original returned %u (%zu trace words), ours %u (%zu)", name, seed, oa.ret, oa.trace.size(),
                 ob.ret, ob.trace.size());
    }
    if (sa != sb) ctx.Fail("%s seed %u: the value list differs (%zu words against %zu)", name, seed, sa.size(), sb.size());
}

} // namespace

DIFF_TEST(GZPersistResourceManager_Part6, 0x9b4c20)
{
    auto vf6 = ctx.Original<Vf6Fn>(0x9b4c20);
    auto vf8 = ctx.Original<Vf8Fn>(0x9b5460);
    auto vf27 = ctx.Original<Vf27Fn>(0x1e36e0);
    auto vf37 = ctx.Original<Vf37Fn>(0x9b45f0);
    auto vf42 = ctx.Original<Vf42Fn>(0x9b54e0);

    std::mt19937 rng(71);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        uint32_t dqSeed = rng();
        gLeaveResult = rng();
        gCallResult = Result(rng);
        gFindResult = Result(rng);
        gSlot49Result = Result(rng);
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), a4 = rng(), a5 = rng();
        unsigned objIndex = rng() % 3;   // vf8's object: null, obj84 or obj2
        unsigned valueIndex = rng() % 4; // vf42's value: one of the list's three, or not in it
        uint32_t pred = rng() % 8 == 0 ? 0u : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&DequePred));

        // vf6: the serial at +0xa0 goes up by one in both worlds; the traces must match.
        {
            World &w = SharedWorld();
            Build(w, ctx, seed);
            PrepVf6(w);
            uint32_t before;
            std::memcpy(&before, w.self + 0xa0, 4);
            Outcome oa = Run(w, [&] { return static_cast<uint32_t>(vf6(w.self, a1, a2, a3, a4, a5)); });
            uint32_t sa, sb;
            std::memcpy(&sa, w.self + 0xa0, 4);
            Build(w, ctx, seed);
            PrepVf6(w);
            Outcome ob = Run(w, [&] { return static_cast<uint32_t>(Mgr(w)->vf6(a1, a2, a3, a4, a5)); });
            std::memcpy(&sb, w.self + 0xa0, 4);
            ++ctx.cases;
            if (oa.ret != ob.ret || oa.trace != ob.trace || oa.threw != ob.threw) {
                size_t k = 0;
                while (k < oa.trace.size() && k < ob.trace.size() && oa.trace[k] == ob.trace[k]) ++k;
                ctx.Fail("vf6 seed %u (a1 %08x): original returned %u (%zu trace words), ours %u (%zu); first difference at %zu: %08x / %08x",
                         seed, a1, oa.ret, oa.trace.size(), ob.ret, ob.trace.size(), k, k < oa.trace.size() ? oa.trace[k] : 0u,
                         k < ob.trace.size() ? ob.trace[k] : 0u);
            }
            if (sa != before + 1 || sb != before + 1) ctx.Fail("vf6 seed %u: serial %u / %u, expected %u", seed, sa, sb, before + 1);
        }

        // vf8: a null object (no calls at all), or obj84 / obj2 whose slot 3 hands out obj2.
        auto pickObj = [objIndex](World &w) -> void * {
            switch (objIndex) {
            case 0: return nullptr;
            case 1: return w.obj84;
            default: return w.obj2;
            }
        };
        CompareWith(ctx, "vf8", seed, PrepVf8,
                    [&](World &w) { return vf8(w.self, pickObj(w)); },
                    [&](World &w) { return Mgr(w)->vf8(pickObj(w)); });

        CompareWith(ctx, "vf27", seed, [](World &w) { w.selfVT[28] = reinterpret_cast<void *>(&Slot28Rec); },
                    [&](World &w) { return vf27(w.self, a1); },
                    [&](World &w) { return Mgr(w)->vf27(a1); });

        // vf37: a null predicate or an empty deque returns the caller's eax (undefined): both sides
        // return 0 there and the traces are compared.
        auto runVf37 = [&](World &w, bool original) {
            uint32_t size = 0;
            std::memcpy(&size, w.self + 0x80, 4);
            uint32_t r = original ? vf37(w.self, pred, a2) : Mgr(w)->vf37(pred, a2);
            return pred == 0 || size == 0 ? 0u : r;
        };
        CompareWith(ctx, "vf37", seed, [&](World &w) { SetDeque(w, dqSeed, true); },
                    [&](World &w) { return runVf37(w, true); }, [&](World &w) { return runVf37(w, false); });

        // vf42: the removed node is freed (a FreeRecorder across the call; 2 when no node matched).
        auto runVf42 = [&](World &w, bool original) {
            void *value = w.values[valueIndex];
            void *target = nullptr;
            uint32_t head;
            std::memcpy(&head, w.self + 0x90, 4);
            auto *sentinel = reinterpret_cast<cGZPersistValueNode *>(static_cast<uintptr_t>(head));
            for (cGZPersistValueNode *n = sentinel->next; n != sentinel; n = n->next) {
                if (n->value == value) {
                    target = n;
                    break;
                }
            }
            difftest::FreeRecorder rec;
            uint32_t r = original ? vf42(w.self, value) : Mgr(w)->vf42(value);
            rec.Stop();
            if (target) gTrace.push_back(rec.Freed(target) ? 1u : 0u);
            else gTrace.push_back(2u);
            return r;
        };
        CompareListWith(ctx, "vf42", seed, [&](World &w) { HeapList(w, seed); },
                        [&](World &w) { return runVf42(w, true); }, [&](World &w) { return runVf42(w, false); });
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

namespace {

// The deque at +0x70 after a run: map, map size, offset, size, then the element ids in order.
std::vector<uint32_t> DequeSummary(const World &w)
{
    std::vector<uint32_t> s;
    uint32_t map, mapSize, offset, size;
    std::memcpy(&map, w.self + 0x74, 4);
    std::memcpy(&mapSize, w.self + 0x78, 4);
    std::memcpy(&offset, w.self + 0x7c, 4);
    std::memcpy(&size, w.self + 0x80, 4);
    s.push_back(map == Addr(w.map) ? 1u : 0u);
    s.push_back(mapSize);
    s.push_back(offset);
    s.push_back(size);
    for (uint32_t i = 0; i < size && i < 16; ++i) {
        uint32_t pos = offset + i;
        s.push_back(IdOfWord(w.blocks[(pos >> 2) & 7][pos & 3]));
    }
    return s;
}

} // namespace

// vf30 (0x9b5a60) erases a deque element (the game's deque erase 0x2e9d10 against the library's):
// results, recorded calls (the lock, the element's Release) and the deque's state afterwards.
DIFF_TEST(GZPersistResourceManager_RemoveSegment, 0x9b5a60)
{
    auto vf30 = ctx.Original<Vf11Fn>(0x9b5a60);
    std::mt19937 rng(30);
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        uint32_t dqSeed = rng();
        uint32_t probe = rng();
        uint32_t a1 = rng();
        gLeaveResult = rng();
        gCallResult = Result(rng);
        World &w = SharedWorld();
        Build(w, ctx, seed);
        SetDeque(w, dqSeed, true);
        uint32_t segA = PickValue(w, probe, a1);
        Outcome oa = Run(w, [&] { return static_cast<uint32_t>(vf30(w.self, segA)); });
        std::vector<uint32_t> sa = DequeSummary(w);
        Build(w, ctx, seed);
        SetDeque(w, dqSeed, true);
        uint32_t segB = PickValue(w, probe, a1);
        Outcome ob = Run(w, [&] {
            return static_cast<uint32_t>(Mgr(w)->vf30(reinterpret_cast<void *>(static_cast<uintptr_t>(segB))));
        });
        std::vector<uint32_t> sb = DequeSummary(w);
        ++ctx.cases;
        if (oa.ret != ob.ret || oa.trace != ob.trace || oa.threw != ob.threw)
            ctx.Fail("vf30 seed %u: original returned %u (%zu trace words), ours %u (%zu)", seed, oa.ret, oa.trace.size(),
                     ob.ret, ob.trace.size());
        if (sa != sb) ctx.Fail("vf30 seed %u: the deque differs afterwards (%zu words against %zu)", seed, sa.size(), sb.size());
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
