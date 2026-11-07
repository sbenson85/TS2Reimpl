// Differential tests for the cGZDBSegmentPackedFile slots reimplemented so far (field map in
// src/gz/cGZDBSegmentPackedFile.h). The object lives in one fixed arena, so pointers compare equal
// between runs. Each run starts from the same random image and gets copies of the primary and
// subobject vtables whose AddRef, deleting-destructor and string slots record their calls; the lock
// object at +0x0c and the output string are fakes that record Lock, Unlock and FromChar. Results,
// recorded calls and the arena's bytes must match between the original, our function called
// directly, and our function called through a vtable slot.
#include "game_allocator.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZCriticalSection.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <vector>

class cIGZString;

namespace {

using RefFn = uint32_t(__thiscall *)(void *);
using VoidFn = void(__thiscall *)(void *);
using ReleaseFn = uint32_t(__thiscall *)(void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using BoolFn = bool(__thiscall *)(void *);
using BoolU32Fn = bool(__thiscall *)(void *, uint32_t);
using VoidU32Fn = void(__thiscall *)(void *, uint32_t);
using VoidU32U32Fn = void(__thiscall *)(void *, uint32_t, uint32_t);
using VoidFloatFn = void(__thiscall *)(void *, float);
using VoidPtrsFn = void(__thiscall *)(void *, uint32_t *, uint32_t *);
using VoidPtrFn = void(__thiscall *)(void *, void *);

using QueryMember = bool (cGZDBSegmentPackedFile::*)(uint32_t, void **);
using ReleaseMember = uint32_t (cGZDBSegmentPackedFile::*)();
using BoolU32Member = bool (cGZDBSegmentPackedFile::*)(uint32_t);
using VoidU32Member = void (cGZDBSegmentPackedFile::*)(uint32_t);
using VoidU8Member = void (cGZDBSegmentPackedFile::*)(uint8_t);
using BoolU8Member = bool (cGZDBSegmentPackedFile::*)(uint8_t);
using VoidU32U32Member = void (cGZDBSegmentPackedFile::*)(uint32_t, uint32_t);
using VoidFloatMember = void (cGZDBSegmentPackedFile::*)(float);
using BoolBoolMember = bool (cGZDBSegmentPackedFile::*)();
using VoidPtrsMember = void (cGZDBSegmentPackedFile::*)(uint32_t *, uint32_t *);
using VoidStrMember = void (cGZDBSegmentPackedFile::*)(cIGZString *);

constexpr size_t kBytes = 0x300;
constexpr size_t kStr = 0x1c0;   // fake output string object (vtable at +0)
constexpr size_t kDst = 0x1d0;   // output dwords for the pointer-taking getters
constexpr size_t kStore = 0x280; // fake record store object (vtable at +0), reached through +0x30
constexpr size_t kHit = 0x2a0;   // the record the store returns (16 bytes)
constexpr size_t kRecIn = 0x2c0; // the record the caller passes (16 bytes)
constexpr size_t kHeapCap = 64;
constexpr uint32_t kUnchangedOut = 0xfeedf00d;

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

cGZDBSegmentPackedFile *Obj(void *p)
{
    return static_cast<cGZDBSegmentPackedFile *>(p);
}

// The object, fixed in memory so that pointers into it are the same in every run.
alignas(16) uint8_t gArena[kBytes];
uint8_t gHeap[kHeapCap];

enum Kind : int64_t { kAddRef = 1, kDelete = 3, kLock = 4, kUnlock = 5, kFromChar = 6 };
std::vector<int64_t> gLog;

int64_t Off(const void *p)
{
    return static_cast<const uint8_t *>(p) - gArena;
}

void Push(int64_t kind, int64_t a, int64_t b)
{
    gLog.push_back(kind);
    gLog.push_back(a);
    gLog.push_back(b);
}

// Vtable slots of the copies. __fastcall recorders take ecx and edx, as __thiscall does, and pop
// their stack arguments as a __thiscall callee would.
uint32_t __fastcall RecordAddRef(void *self, void *)
{
    Push(kAddRef, Off(self), 0);
    uint32_t &count = *reinterpret_cast<uint32_t *>(gArena + 8);
    return ++count;
}

void __fastcall RecordDelete(void *self, void *)
{
    Push(kDelete, Off(self), 0);
}

void __fastcall RecordLock(void *self, void *)
{
    Push(kLock, Off(self), 0);
}

void __fastcall RecordUnlock(void *self, void *)
{
    Push(kUnlock, Off(self), 0);
}

// cIGZString slot 3: records the length and every character read.
void __fastcall RecordFromChar(void *self, void *, const char *src, uint32_t len)
{
    Push(kFromChar, Off(self), len);
    for (uint32_t i = 0; i < len; ++i) gLog.push_back(static_cast<uint8_t>(src[i]));
}

// The lock's vtable (slot 0 unused) and the output string's vtable (slot 3 is FromChar).
void *gLockTable[3];
void *gStrTable[4];

struct Tables {
    void *primary[59];
    void *sub[50];
};

Tables MakeTables(const difftest::Context &ctx)
{
    Tables t{};
    std::memcpy(t.primary, ctx.image.At(cGZDBSegmentPackedFile::kVTable), sizeof(t.primary));
    std::memcpy(t.sub, ctx.image.At(cGZDBSegmentPackedFile::kSubVTable), sizeof(t.sub));
    t.primary[1] = reinterpret_cast<void *>(&RecordAddRef);
    t.sub[1] = reinterpret_cast<void *>(&RecordAddRef);
    t.sub[cGZDBSegmentPackedFile::kDeleteSlot] = reinterpret_cast<void *>(&RecordDelete);
    return t;
}

// One trial's inputs: the image of the whole object, and the path's heap bytes when the path is long.
struct Scene {
    std::vector<uint8_t> image;
    bool heapPath = false;
    std::vector<uint8_t> heap;
};

Scene MakeScene(std::mt19937 &rng)
{
    Scene s;
    s.image.resize(kBytes);
    for (auto &b : s.image) b = static_cast<uint8_t>(rng());
    return s;
}

struct Outcome {
    uint64_t result = 0;
    std::vector<int64_t> log;
    std::vector<uint8_t> bytes;
};

// Runs one call on a fresh copy of the scene. `call` gets the arena and returns the result.
template <typename Call> Outcome Run(const Tables &t, const Scene &s, Call call)
{
    std::memcpy(gArena, s.image.data(), kBytes);
    const void *primary = t.primary;
    const void *sub = t.sub;
    const void *lock = gLockTable;
    const void *str = gStrTable;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &sub, sizeof(sub));
    std::memcpy(gArena + 0x0c, &lock, sizeof(lock));
    std::memcpy(gArena + kStr, &str, sizeof(str));
    if (s.heapPath) {
        std::memcpy(gHeap, s.heap.data(), kHeapCap);
        const void *data = gHeap;
        std::memcpy(gArena + 0x3c, &data, sizeof(data));
    }
    gLog.clear();

    Outcome out;
    out.result = call(gArena);
    out.log = gLog;
    // The vtable pointers differ between the plain and routed tables, so they are not compared.
    std::memset(gArena, 0, 8);
    out.bytes.assign(gArena, gArena + kBytes);
    return out;
}

void Compare(difftest::Context &ctx, const char *name, const Outcome &a, const Outcome &b, const Outcome &c)
{
    ++ctx.cases;
    if (a.result != b.result || a.result != c.result) {
        ctx.Fail("%s: result original %llu, ours %llu, via slot %llu", name, static_cast<unsigned long long>(a.result),
                 static_cast<unsigned long long>(b.result), static_cast<unsigned long long>(c.result));
    }
    if (a.log != b.log || a.log != c.log) {
        ctx.Fail("%s: calls made differ (%zu / %zu / %zu values)", name, a.log.size(), b.log.size(), c.log.size());
    }
    if (a.bytes != b.bytes || a.bytes != c.bytes) ctx.Fail("%s: object state differs", name);
}

// Shared setup of the fake lock, the output string and the vtable recorders.
void InitFakes()
{
    gLockTable[1] = reinterpret_cast<void *>(&RecordLock);
    gLockTable[2] = reinterpret_cast<void *>(&RecordUnlock);
    gStrTable[3] = reinterpret_cast<void *>(&RecordFromChar);
}

void SetWord(Scene &s, size_t at, uint32_t v)
{
    std::memcpy(s.image.data() + at, &v, sizeof(v));
}

// Interesting edges of a 32-bit value: zero, one, the top bit, all ones, and random values.
uint32_t PickWord(std::mt19937 &rng)
{
    static const uint32_t edges[] = {0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff, 0xffff, 0x10000};
    if (rng() % 3 == 0) return edges[rng() % 8];
    return static_cast<uint32_t>(rng());
}

// Bytes that gate the functions (flags) are biased towards zero and towards the open/writable bits.
uint8_t PickFlag(std::mt19937 &rng)
{
    static const uint8_t flags[] = {0, 0, 1, 2, 3, 4, 0x80, 0xff};
    return flags[rng() % 8];
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_QueryInterface, 0x9adaa0)
{
    InitFakes();
    auto orig = ctx.Original<QueryFn>(0x9adaa0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[0] = Addr(static_cast<QueryMember>(&cGZDBSegmentPackedFile::QueryInterface));
    static const uint32_t ids[] = {0x286d4a6a, 0x656b8f0c, 1, 0, 2, 0x286d4a6b, 0x656b8f0d, 0xffffffff};
    std::mt19937 rng(0x01a);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        uint32_t id = (rng() % 4 == 0) ? PickWord(rng) : ids[rng() % 8];
        // The out pointer starts as a known value, so a refused id must leave it alone.
        SetWord(s, kDst, kUnchangedOut);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return orig(self, id, reinterpret_cast<void **>(self + kDst));
        });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return Obj(self)->QueryInterface(id, reinterpret_cast<void **>(self + kDst));
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<QueryFn>(self, 0)(self, id, reinterpret_cast<void **>(self + kDst));
        });
        Compare(ctx, "QueryInterface", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_Release, 0x9af980)
{
    InitFakes();
    auto orig = ctx.Original<ReleaseFn>(0x9af980);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[2] = Addr(static_cast<ReleaseMember>(&cGZDBSegmentPackedFile::Release));
    std::mt19937 rng(0x2e1);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        // Counts 0, 1 and 2 are the interesting ones: 0 underflows, 1 deletes.
        SetWord(s, 8, rng() % 2 ? PickWord(rng) : static_cast<uint32_t>(rng() % 3));
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->Release(); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<ReleaseFn>(self, 2)(self); });
        Compare(ctx, "Release", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetField0F4, 0x9b0160)
{
    InitFakes();
    auto orig = ctx.Original<BoolU32Fn>(0x9b0160);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[7] = Addr(static_cast<BoolU32Member>(&cGZDBSegmentPackedFile::SetBaseOffset));
    std::mt19937 rng(0xf4);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        s.image[0x24] = PickFlag(rng);
        uint32_t v = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, v); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->SetBaseOffset(v); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolU32Fn>(self, 7)(self, v); });
        Compare(ctx, "SetBaseOffset", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_GetPath, 0x9ac850)
{
    InitFakes();
    auto orig = ctx.Original<VoidPtrFn>(0x9ac850);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[8] = Addr(static_cast<VoidStrMember>(&cGZDBSegmentPackedFile::GetPath));
    std::mt19937 rng(0x9a8);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        // Path length from 0 to 40, both inline (capacity 15) and in the heap (capacity 16 or more).
        uint32_t len = static_cast<uint32_t>(rng() % 41);
        if (rng() % 4 == 0) len = (rng() % 2) ? 15 : 16;
        s.heapPath = len > 15 || rng() % 3 == 0;
        uint32_t capacity = s.heapPath ? (len > 15 ? len + rng() % 8 : 16 + rng() % 8) : 15;
        SetWord(s, 0x4c, len);
        SetWord(s, 0x50, capacity);
        if (s.heapPath) {
            s.heap.resize(kHeapCap);
            for (auto &b : s.heap) b = static_cast<uint8_t>(rng());
        }
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self, gArena + kStr);
            return 0;
        });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            Obj(self)->GetPath(reinterpret_cast<cIGZString *>(gArena + kStr));
            return 0;
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidPtrFn>(self, 8)(self, gArena + kStr);
            return 0;
        });
        Compare(ctx, "GetPath", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_GetFields124, 0x9ab940)
{
    InitFakes();
    auto orig = ctx.Original<VoidPtrsFn>(0x9ab940);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[10] = Addr(static_cast<VoidPtrsMember>(&cGZDBSegmentPackedFile::GetFileUserVersion));
    std::mt19937 rng(0x124);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self, reinterpret_cast<uint32_t *>(self + kDst), reinterpret_cast<uint32_t *>(self + kDst + 4));
            return 0;
        });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            Obj(self)->GetFileUserVersion(reinterpret_cast<uint32_t *>(self + kDst), reinterpret_cast<uint32_t *>(self + kDst + 4));
            return 0;
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidPtrsFn>(self, 10)(self, reinterpret_cast<uint32_t *>(self + kDst),
                                            reinterpret_cast<uint32_t *>(self + kDst + 4));
            return 0;
        });
        Compare(ctx, "GetFileUserVersion", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_GetFields104, 0x9ac7c0)
{
    InitFakes();
    auto orig = ctx.Original<VoidPtrsFn>(0x9ac7c0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[11] = Addr(static_cast<VoidPtrsMember>(&cGZDBSegmentPackedFile::GetUserVersion));
    std::mt19937 rng(0x104);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self, reinterpret_cast<uint32_t *>(self + kDst), reinterpret_cast<uint32_t *>(self + kDst + 4));
            return 0;
        });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            Obj(self)->GetUserVersion(reinterpret_cast<uint32_t *>(self + kDst), reinterpret_cast<uint32_t *>(self + kDst + 4));
            return 0;
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidPtrsFn>(self, 11)(self, reinterpret_cast<uint32_t *>(self + kDst),
                                            reinterpret_cast<uint32_t *>(self + kDst + 4));
            return 0;
        });
        Compare(ctx, "GetUserVersion", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetFields104, 0x9b0200)
{
    InitFakes();
    auto orig = ctx.Original<VoidU32U32Fn>(0x9b0200);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[12] = Addr(static_cast<VoidU32U32Member>(&cGZDBSegmentPackedFile::SetUserVersion));
    std::mt19937 rng(0x1040);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t a1 = PickWord(rng), a2 = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { orig(self, a1, a2); return 0; });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { Obj(self)->SetUserVersion(a1, a2); return 0; });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidU32U32Fn>(self, 12)(self, a1, a2);
            return 0;
        });
        Compare(ctx, "SetUserVersion", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetField17C, 0x9b01b0)
{
    InitFakes();
    auto orig = ctx.Original<BoolU32Fn>(0x9b01b0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[15] = Addr(static_cast<BoolU32Member>(&cGZDBSegmentPackedFile::SetOptions));
    std::mt19937 rng(0x17c);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t v = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, v); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->SetOptions(v); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolU32Fn>(self, 15)(self, v); });
        Compare(ctx, "SetOptions", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetFloat180, 0x9b0190)
{
    InitFakes();
    auto orig = ctx.Original<VoidFloatFn>(0x9b0190);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[16] = Addr(static_cast<VoidFloatMember>(&cGZDBSegmentPackedFile::SetCompactRatio));
    std::mt19937 rng(0x180);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t bits = PickWord(rng);
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { orig(self, v); return 0; });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { Obj(self)->SetCompactRatio(v); return 0; });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidFloatFn>(self, 16)(self, v);
            return 0;
        });
        Compare(ctx, "SetCompactRatio", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetByte2A, 0x9b0180)
{
    InitFakes();
    auto orig = ctx.Original<VoidU32Fn>(0x9b0180);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[17] = Addr(static_cast<VoidU8Member>(&cGZDBSegmentPackedFile::SetFlag2A));
    std::mt19937 rng(0x2a);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t v = PickWord(rng); // only the low byte is stored, by both
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { orig(self, v); return 0; });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { Obj(self)->SetFlag2A(static_cast<uint8_t>(v)); return 0; });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidU32Fn>(self, 17)(self, v);
            return 0;
        });
        Compare(ctx, "SetFlag2A", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_AnyFlagSet, 0x1d3dd0)
{
    InitFakes();
    auto orig = ctx.Original<BoolFn>(0x1d3dd0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[24] = Addr(static_cast<BoolBoolMember>(&cGZDBSegmentPackedFile::IsDirty));
    std::mt19937 rng(0x24);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        // Each of the three flags is zero or not, independently.
        for (size_t at : {size_t(0x34), size_t(0x178), size_t(0x19d)}) {
            s.image[at] = (rng() % 2) ? 0 : static_cast<uint8_t>(rng() % 256);
        }
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->IsDirty(); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolFn>(self, 24)(self); });
        Compare(ctx, "IsDirty", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetField158, 0x9b0280)
{
    InitFakes();
    auto orig = ctx.Original<VoidU32Fn>(0x9b0280);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[26] = Addr(static_cast<VoidU32Member>(&cGZDBSegmentPackedFile::SetGroupInfo));
    std::mt19937 rng(0x158);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        s.image[0x178] = 0;
        uint32_t v = PickWord(rng);
        // Half the trials store the value already held, which must not set the flag.
        if (rng() % 2) SetWord(s, 0x158, v);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { orig(self, v); return 0; });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { Obj(self)->SetGroupInfo(v); return 0; });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidU32Fn>(self, 26)(self, v);
            return 0;
        });
        Compare(ctx, "SetGroupInfo", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetByte1AA, 0x9aacc0)
{
    InitFakes();
    auto orig = ctx.Original<BoolU32Fn>(0x9aacc0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[38] = Addr(static_cast<BoolU8Member>(&cGZDBSegmentPackedFile::SetCompressAll));
    std::mt19937 rng(0x1aa);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t v = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, v); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->SetCompressAll(static_cast<uint8_t>(v)); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolU32Fn>(self, 38)(self, v);
        });
        Compare(ctx, "SetCompressAll", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetByte1A8, 0x1d3ad0)
{
    InitFakes();
    auto orig = ctx.Original<VoidU32Fn>(0x1d3ad0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[49] = Addr(static_cast<VoidU8Member>(&cGZDBSegmentPackedFile::SetMatchFlagA));
    std::mt19937 rng(0x1a8);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t v = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { orig(self, v); return 0; });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { Obj(self)->SetMatchFlagA(static_cast<uint8_t>(v)); return 0; });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidU32Fn>(self, 49)(self, v);
            return 0;
        });
        Compare(ctx, "SetMatchFlagA", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetByte1A9, 0x1d3ab0)
{
    InitFakes();
    auto orig = ctx.Original<VoidU32Fn>(0x1d3ab0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[50] = Addr(static_cast<VoidU8Member>(&cGZDBSegmentPackedFile::SetMatchFlagB));
    std::mt19937 rng(0x1a9);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t v = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { orig(self, v); return 0; });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { Obj(self)->SetMatchFlagB(static_cast<uint8_t>(v)); return 0; });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidU32Fn>(self, 50)(self, v);
            return 0;
        });
        Compare(ctx, "SetMatchFlagB", a, b, c);
    }
}

// ---- Slots added with the record store, the +0x38 and +0x58 objects, and the subobject views. ----
namespace {

using BoolPtrFn = bool(__thiscall *)(void *, void *);
using BoolPtrOutFn = bool(__thiscall *)(void *, void *, uint32_t *);
using BoolU32U32Fn = bool(__thiscall *)(void *, uint32_t, uint32_t);
using BoolRecFn = bool(__thiscall *)(void *, const void *);
using U32RecFn = uint32_t(__thiscall *)(void *, const void *);
using VoidRecFn = void(__thiscall *)(void *, const void *, uint32_t, uint32_t);

using BoolPtrMember = bool (cGZDBSegmentPackedFile::*)(void *);
using BoolU32U32Member = bool (cGZDBSegmentPackedFile::*)(uint32_t, uint32_t);
using BoolRecMember = bool (cGZDBSegmentPackedFile::*)(const uint32_t *);
using U32RecMember = uint32_t (cGZDBSegmentPackedFile::*)(const uint32_t *);
using VoidRecMember = void (cGZDBSegmentPackedFile::*)(const uint32_t *, uint8_t, uint32_t);

using ViewBoolMember = bool (SegmentView::*)() const;
using ViewAutoOpenMember = bool (SegmentView::*)(uint8_t, uint8_t);
using ViewIdMember = bool (SegmentView::*)(const uint32_t *);
using ViewQueryMember = bool (SegmentView::*)(void *, uint32_t *);

// Record kinds in the call log (after the object offset and the argument).
constexpr int64_t kFind11 = 7, kFind12 = 8, kSink16 = 9, kSlot32 = 10, kSub35 = 11, kSub46 = 12;

// Per-trial answers of the fakes: the record the store finds, and the subobject's slot results.
void *gFindRet = nullptr;
bool gSlot32Ret = false;
bool gSub35Ret = false;
uint32_t gSub46Ret = 0;

// Vtables of the fake objects. Slot 16 of the +0x38 object, slot 32 of the +0x58 object, slots 11 and 12
// of the record store, and slots 35 and 46 of the subobject.
void *gSink38Tab[64];
void *gObj58Tab[64];
void *gStoreTab[64];

// The record store's lookups: record copy, then the two bytes. Returns the fake answer.
void *__fastcall RecFind11(void *self, void *, const uint32_t *copy, uint32_t b1, uint32_t b2)
{
    Push(kFind11, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(copy[i]);
    gLog.push_back(b1);
    gLog.push_back(b2);
    return gFindRet;
}

void *__fastcall RecFind12(void *self, void *, const uint32_t *copy, uint32_t b1, uint32_t b2)
{
    Push(kFind12, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(copy[i]);
    gLog.push_back(b1);
    gLog.push_back(b2);
    return gFindRet;
}

void __fastcall RecSink16(void *self, void *, void *path)
{
    Push(kSink16, Off(self), static_cast<int64_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path))));
}

bool __fastcall RecSlot32(void *self, void *, uint32_t a, uint32_t b)
{
    Push(kSlot32, Off(self), a);
    gLog.push_back(b);
    return gSlot32Ret;
}

bool __fastcall RecSub35(void *self, void *, void *arg)
{
    Push(kSub35, Off(self), static_cast<int64_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg))));
    return gSub35Ret;
}

uint32_t __fastcall RecSub46(void *self, void *)
{
    Push(kSub46, Off(self), 0);
    return gSub46Ret;
}

void InitNewFakes()
{
    InitFakes();
    gSink38Tab[16] = reinterpret_cast<void *>(&RecSink16);
    gObj58Tab[32] = reinterpret_cast<void *>(&RecSlot32);
    gStoreTab[11] = reinterpret_cast<void *>(&RecFind11);
    gStoreTab[12] = reinterpret_cast<void *>(&RecFind12);
}

// The subobject's slots 35 and 46 are fakes, so the original's calls through this's vtable are recorded.
Tables WithSubFakes(Tables t)
{
    t.sub[35] = reinterpret_cast<void *>(&RecSub35);
    t.sub[46] = reinterpret_cast<void *>(&RecSub46);
    return t;
}

void SetPointer(Scene &s, size_t at, const void *p)
{
    SetWord(s, at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)));
}

void *AsPointer(uint32_t v)
{
    return reinterpret_cast<void *>(static_cast<uintptr_t>(v));
}

// The record the store answers with: the record at kHit, or none (a quarter of the trials). Its +8 word
// is random and its +4 word equals the masked +8 a third of the time, the edge of the ">" comparisons.
void PickFind(Scene &s, std::mt19937 &rng)
{
    gFindRet = (rng() % 4 == 0) ? nullptr : static_cast<void *>(gArena + kHit);
    uint32_t w8 = PickWord(rng);
    uint32_t w4 = (rng() % 3 == 0) ? (w8 & 0x7fffffffu) : PickWord(rng);
    SetWord(s, kHit + 8, w8);
    SetWord(s, kHit + 4, w4);
}

// Sets up a scene with the record store wired to +0x30 and its fake vtable at kStore.
void SetupStore(Scene &s)
{
    SetPointer(s, kStore, gStoreTab);
    SetPointer(s, 0x30, gArena + kStore);
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_SetPath, 0x9b0230)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolPtrFn>(0x9b0230);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[9] = Addr(static_cast<BoolPtrMember>(&cGZDBSegmentPackedFile::SetPath));
    std::mt19937 rng(0x9b0);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        s.image[0x24] = PickFlag(rng); // bits 0..1 decide whether the path is taken
        SetPointer(s, 0x38, gSink38Tab);
        void *path = AsPointer(PickWord(rng));
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, path); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->SetPath(path); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolPtrFn>(self, 9)(self, path);
        });
        Compare(ctx, "SetPath", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetPair, 0x9b0100)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolU32U32Fn>(0x9b0100);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[14] = Addr(static_cast<BoolU32U32Member>(&cGZDBSegmentPackedFile::SetBuffering));
    std::mt19937 rng(0x9b1);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetPointer(s, 0x58, gObj58Tab);
        gSlot32Ret = rng() % 2;
        uint32_t a1 = PickWord(rng), a2 = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, a1, a2); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->SetBuffering(a1, a2); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolU32U32Fn>(self, 14)(self, a1, a2);
        });
        Compare(ctx, "SetBuffering", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_CompareRecord, 0x9ac9b0)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolRecFn>(0x9ac9b0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[41] = Addr(static_cast<BoolRecMember>(&cGZDBSegmentPackedFile::IsRecordCompressed));
    std::mt19937 rng(0x41);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetupStore(s);
        PickFind(s, rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kRecIn); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->IsRecordCompressed(reinterpret_cast<uint32_t *>(self + kRecIn)); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolRecFn>(self, 41)(self, self + kRecIn);
        });
        Compare(ctx, "IsRecordCompressed", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_StoreRecord, 0x9b02c0)
{
    InitNewFakes();
    auto orig = ctx.Original<VoidRecFn>(0x9b02c0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[42] = Addr(static_cast<VoidRecMember>(&cGZDBSegmentPackedFile::SetRecordFullSize));
    std::mt19937 rng(0x42);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetupStore(s);
        PickFind(s, rng);
        uint32_t useArg = PickWord(rng), value = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self, self + kRecIn, useArg, value);
            return 0;
        });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            Obj(self)->SetRecordFullSize(reinterpret_cast<uint32_t *>(self + kRecIn), static_cast<uint8_t>(useArg), value);
            return 0;
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidRecFn>(self, 42)(self, self + kRecIn, useArg, value);
            return 0;
        });
        Compare(ctx, "SetRecordFullSize", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_LookupRecord, 0x9ab8c0)
{
    InitNewFakes();
    auto orig = ctx.Original<U32RecFn>(0x9ab8c0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[43] = Addr(static_cast<U32RecMember>(&cGZDBSegmentPackedFile::GetCompressedStoredSize));
    std::mt19937 rng(0x43);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetupStore(s);
        PickFind(s, rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kRecIn); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return Obj(self)->GetCompressedStoredSize(reinterpret_cast<uint32_t *>(self + kRecIn));
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<U32RecFn>(self, 43)(self, self + kRecIn);
        });
        Compare(ctx, "GetCompressedStoredSize", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_RecordFlag, 0x9ad660)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolRecFn>(0x9ad660);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[52] = Addr(static_cast<BoolRecMember>(&cGZDBSegmentPackedFile::GetRecordFlag));
    std::mt19937 rng(0x52);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetupStore(s);
        PickFind(s, rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kRecIn); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return Obj(self)->GetRecordFlag(reinterpret_cast<uint32_t *>(self + kRecIn));
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolRecFn>(self, 52)(self, self + kRecIn);
        });
        Compare(ctx, "GetRecordFlag", a, b, c);
    }
}

// The subobject views: `this` is object + 4, the original is called with that pointer, and the routed
// case calls our member through the subobject's vtable slot.
DIFF_TEST(cGZDBSegmentPackedFile_IsOpen, 0x9ad650)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolFn>(0x9ad650);
    Tables plain = WithSubFakes(MakeTables(ctx));
    Tables routed = WithSubFakes(MakeTables(ctx));
    routed.sub[7] = Addr(static_cast<ViewBoolMember>(&SegmentView::IsOpen));
    std::mt19937 rng(0x7e);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        s.image[0x24] = PickFlag(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<SegmentView *>(self + 4)->IsOpen();
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolFn>(self + 4, 7)(self + 4); });
        Compare(ctx, "IsOpen", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_IsWritable, 0x1d3e10)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolFn>(0x1d3e10);
    Tables plain = WithSubFakes(MakeTables(ctx));
    Tables routed = WithSubFakes(MakeTables(ctx));
    routed.sub[8] = Addr(static_cast<ViewBoolMember>(&SegmentView::IsWritable));
    std::mt19937 rng(0x8e);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        // The dword at +0x24 (sub +0x20): bit 1 decides, and the bytes above it must not matter.
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<SegmentView *>(self + 4)->IsWritable();
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolFn>(self + 4, 8)(self + 4); });
        Compare(ctx, "IsWritable", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_AutoOpen, 0x9a8030)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolU32U32Fn>(0x9a8030);
    Tables plain = WithSubFakes(MakeTables(ctx));
    Tables routed = WithSubFakes(MakeTables(ctx));
    routed.sub[6] = Addr(static_cast<ViewAutoOpenMember>(&SegmentView::AutoOpen));
    std::mt19937 rng(0x6a);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t state = PickWord(rng), flag = PickWord(rng); // only the low bytes are stored
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, state, flag); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<SegmentView *>(self + 4)->AutoOpen(static_cast<uint8_t>(state), static_cast<uint8_t>(flag));
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolU32U32Fn>(self + 4, 6)(self + 4, state, flag);
        });
        Compare(ctx, "AutoOpen", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SetSegmentID, 0x9b0350)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolPtrFn>(0x9b0350);
    Tables plain = WithSubFakes(MakeTables(ctx));
    Tables routed = WithSubFakes(MakeTables(ctx));
    routed.sub[16] = Addr(static_cast<ViewIdMember>(&SegmentView::SetSegmentID));
    std::mt19937 rng(0x1d);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetWord(s, kDst, PickWord(rng)); // the ID the caller points at
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, self + kDst); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<SegmentView *>(self + 4)->SetSegmentID(reinterpret_cast<uint32_t *>(self + kDst));
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolPtrFn>(self + 4, 16)(self + 4, self + kDst);
        });
        Compare(ctx, "SetSegmentID", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_Query47, 0x9a9850)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolPtrOutFn>(0x9a9850);
    Tables plain = WithSubFakes(MakeTables(ctx));
    Tables routed = WithSubFakes(MakeTables(ctx));
    routed.sub[47] = Addr(static_cast<ViewQueryMember>(&SegmentView::DoGetRecordChangeID));
    std::mt19937 rng(0x47);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetWord(s, kDst, kUnchangedOut); // a refused call must leave the output alone
        gSub35Ret = rng() % 2;
        gSub46Ret = PickWord(rng);
        void *arg = AsPointer(PickWord(rng));
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return orig(self + 4, arg, reinterpret_cast<uint32_t *>(self + kDst));
        });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<SegmentView *>(self + 4)->DoGetRecordChangeID(arg, reinterpret_cast<uint32_t *>(self + kDst));
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolPtrOutFn>(self + 4, 47)(self + 4, arg, reinterpret_cast<uint32_t *>(self + kDst));
        });
        Compare(ctx, "DoGetRecordChangeID", a, b, c);
    }
}

namespace {

constexpr size_t kOwner = 0x2e0; // the fake owner's vtable at +0; the owner pointer is kOwner + 4
constexpr size_t kNew = 0x2f0;   // the 12 bytes at the returned pointer (kBytes is 0x300)
constexpr int64_t kOwnerRef = 13;

using BoolPtrPtrFn = bool(__thiscall *)(void *, void *, void **);
using CreateMember = bool (cGZDBSegmentPackedFile::*)(void *, void **);

void *gOwnerTab[64];

// The owner's base (slot 1 is AddRef): records the call.
uint32_t __fastcall RecOwnerAddRef(void *self, void *)
{
    Push(kOwnerRef, Off(self), 0);
    return 0;
}

// The returned object lives on the heap, which the two runs don't share. The caller's out word is
// replaced by 1 (a pointer was returned) or 0, and the 12 bytes at the returned pointer are copied into
// the arena: object + 4 (secondary vtable, refcount, holder). A return of the block's start instead of
// object + 4 copies different bytes. The block is not freed, which is a few bytes per trial.
void CaptureObject(uint8_t *self)
{
    void *r;
    std::memcpy(&r, self + kDst, sizeof(r));
    uint32_t flag = r != nullptr ? 1 : 0;
    if (r != nullptr) std::memcpy(self + kNew, r, 12);
    std::memcpy(self + kDst, &flag, sizeof(flag));
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_CreateRecordFile, 0x9ac940)
{
    InitFakes();
    gOwnerTab[1] = reinterpret_cast<void *>(&RecOwnerAddRef);
    auto orig = ctx.Original<BoolPtrPtrFn>(0x9ac940);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[20] = Addr(static_cast<CreateMember>(&cGZDBSegmentPackedFile::CreateRecordFile));
    std::mt19937 rng(0x14);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetPointer(s, kOwner, gOwnerTab);
        // Owner 0 (no AddRef, holder 0), owner 4 (holder 0, the `and` gives zero), or a fake owner whose
        // base at owner - 4 is the arena's kOwner word (AddRef recorded).
        const uint32_t pick = rng() % 4;
        void *owner = pick == 0   ? nullptr
                      : pick == 1 ? AsPointer(4)
                                  : static_cast<void *>(gArena + kOwner + 4);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            bool ok = orig(self, owner, reinterpret_cast<void **>(self + kDst));
            CaptureObject(self);
            return ok;
        });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            bool ok = Obj(self)->CreateRecordFile(owner, reinterpret_cast<void **>(self + kDst));
            CaptureObject(self);
            return ok;
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            bool ok = VirtualSlot<BoolPtrPtrFn>(self, 20)(self, owner, reinterpret_cast<void **>(self + kDst));
            CaptureObject(self);
            return ok;
        });
        Compare(ctx, "CreateRecordFile", a, b, c);
    }
}

// ---- Constructor, the auto-open helper, the file operations, the key tree, the thunks, the small slots ----
namespace {

using U8Fn = uint8_t(__thiscall *)(void *);
using BoolPtrsFn = bool(__thiscall *)(void *, uint32_t *, uint32_t *);
using BoolKeyFn = bool(__thiscall *)(void *, const void *);
using IoFn = bool(__thiscall *)(void *, void *, uint32_t, uint32_t);
using U32U32Fn = uint32_t(__thiscall *)(void *, uint32_t);
using TreeFindFn = void **(__thiscall *)(void *, void **, const uint32_t *);
using CtorFn = void *(__thiscall *)(void *);
using BoolU32U32U32Fn = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

using GetPtrsMember = bool (cGZDBSegmentPackedFile::*)(uint32_t *, uint32_t *);
using KeyMember = bool (cGZDBSegmentPackedFile::*)(const uint32_t *);
using IoMember = bool (cGZDBSegmentPackedFile::*)(void *, uint32_t, uint32_t);
using WriteMember = bool (cGZDBSegmentPackedFile::*)(const void *, uint32_t, uint32_t);
using SeekMember = bool (cGZDBSegmentPackedFile::*)(uint32_t);
using RecU32Member = uint32_t (cGZDBSegmentPackedFile::*)(const uint32_t *);
using ViewQiMember = bool (SegmentView::*)(uint32_t, void **);
using ViewU32Member = uint32_t (SegmentView::*)();
using ViewBoolMember2 = bool (SegmentView::*)();
using ViewPathMember = void (SegmentView::*)(cIGZString *);
using ViewSetPathMember = bool (SegmentView::*)(void *);
using ViewGetIdMember = uint32_t (SegmentView::*)() const;
using ViewVoidMember = void (SegmentView::*)();

constexpr int64_t kOpen = 20, kSeek = 21, kRead14 = 22, kRead15 = 23, kWrite16 = 24, kClose9 = 25, kShutdown5 = 26,
                  kSub33 = 27;

// The behaviour of the fakes for the current trial. The fakes live in the vtables of the subobject (slot 5,
// Open; 9 Close; 33 deleting destructor), of the primary (slot 5 Shutdown) and of the file object at +0x58
// (slots 12 seek, 14 and 15 read, 16 write).
std::vector<uint8_t> gOpenRets{1};
size_t gOpenIdx = 0;
bool gMutate = false;      // Open also rewrites the auto-open bytes (+0x28, +0x29)
uint8_t gMutA = 0, gMutB = 0;
int gStateAfterOpen = -1;  // Open also sets the state byte (+0x24) when this is not negative
uint32_t gSeekDelta = 0;   // seek answers with the position plus this
uint32_t gSeekMut = 0;     // seek also adds this to +0xf4
uint8_t gIoRet = 0;
void *gFileTab[64];

// Open: logs the low bytes of both arguments (the original reads only those), may rewrite the bytes the
// caller reads again afterwards, and answers with the next byte of the trial's sequence.
uint8_t __fastcall RecOpen(void *self, void *, uint32_t a, uint32_t b)
{
    Push(kOpen, Off(self), a & 0xff);
    gLog.push_back(b & 0xff);
    if (gMutate) {
        gArena[0x28] = gMutA;
        gArena[0x29] = gMutB;
    }
    if (gStateAfterOpen >= 0) gArena[0x24] = static_cast<uint8_t>(gStateAfterOpen);
    uint8_t r = gOpenRets[gOpenIdx % gOpenRets.size()];
    ++gOpenIdx;
    return r;
}

uint32_t __fastcall RecSeek(void *self, void *, uint32_t pos)
{
    Push(kSeek, Off(self), pos);
    if (gSeekMut != 0) *reinterpret_cast<uint32_t *>(gArena + 0xf4) += gSeekMut;
    return pos + gSeekDelta;
}

template <int64_t Kind> uint8_t __fastcall RecIo(void *self, void *, const void *p, uint32_t len)
{
    Push(Kind, Off(self), static_cast<int64_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p))));
    gLog.push_back(len);
    return gIoRet;
}

void __fastcall RecClose9(void *self, void *)
{
    Push(kClose9, Off(self), 0);
}

void __fastcall RecShutdown5(void *self, void *)
{
    Push(kShutdown5, Off(self), 0);
}

void __fastcall RecSub33(void *self, void *, uint32_t flags)
{
    Push(kSub33, Off(self), flags);
}

void InitFileFakes()
{
    InitNewFakes();
    gFileTab[12] = reinterpret_cast<void *>(&RecSeek);
    gFileTab[14] = reinterpret_cast<void *>(&RecIo<kRead14>);
    gFileTab[15] = reinterpret_cast<void *>(&RecIo<kRead15>);
    gFileTab[16] = reinterpret_cast<void *>(&RecIo<kWrite16>);
}

Tables WithOpenFakes(Tables t)
{
    t.sub[5] = reinterpret_cast<void *>(&RecOpen);
    t.sub[9] = reinterpret_cast<void *>(&RecClose9);
    t.sub[33] = reinterpret_cast<void *>(&RecSub33);
    t.primary[5] = reinterpret_cast<void *>(&RecShutdown5);
    return t;
}

// Per-trial behaviour of the fakes and the scene's bytes that gate the functions.
void PickBehaviour(Scene &s, std::mt19937 &rng)
{
    gOpenRets.clear();
    static const uint8_t answers[] = {0, 1, 1, 2, 0x80, 0xff};
    for (int i = 0, n = 1 + rng() % 3; i < n; ++i) gOpenRets.push_back(answers[rng() % 6]);
    gOpenIdx = 0;
    gMutate = rng() % 2;
    gMutA = PickFlag(rng);
    gMutB = PickFlag(rng);
    gStateAfterOpen = rng() % 3 == 0 ? PickFlag(rng) : -1;
    gSeekDelta = rng() % 3 == 0 ? 1 : 0;
    gSeekMut = rng() % 3 == 0 ? 7 : 0;
    if (gSeekMut != 0 && rng() % 2) gSeekDelta = gSeekMut; // lands only when +0xf4 is read again
    gIoRet = rng() % 3 == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 3);
    s.image[0x24] = PickFlag(rng);
    s.image[0x28] = (rng() % 3 == 0) ? 0 : PickFlag(rng);
    s.image[0x29] = (rng() % 3 == 0) ? 0 : PickFlag(rng);
    SetPointer(s, 0x58, gFileTab);
    SetupStore(s);
    PickFind(s, rng);
}

// Runs the original, ours, and ours through the slot, resetting the Open sequence before each.
template <typename OrigCall, typename OursCall, typename ViaCall>
void Trio(difftest::Context &ctx, const char *name, const Tables &plain, const Tables &routed, const Scene &s,
          OrigCall origCall, OursCall oursCall, ViaCall viaCall)
{
    auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { gOpenIdx = 0; return origCall(self); });
    auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { gOpenIdx = 0; return oursCall(self); });
    auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { gOpenIdx = 0; return viaCall(self); });
    Compare(ctx, name, a, b, c);
}

// ---- the key tree: nodes in a fixed arena so that the pointers agree between runs ----
alignas(16) uint8_t gTreeArena[0x24 * 16];

KeyTreeNode *TreeNode(uint32_t i)
{
    return reinterpret_cast<KeyTreeNode *>(gTreeArena + 0x24 * i);
}

uint32_t KeyWord(std::mt19937 &rng)
{
    static const uint32_t words[] = {0, 1, 2, 0x80000000u, 0xffffffffu};
    return words[rng() % 5];
}

// The order the original uses: (key[0], key[1], key[3], key[2]).
bool TestLess(const uint32_t *a, const uint32_t *b)
{
    if (a[0] != b[0]) return a[0] < b[0];
    if (a[1] != b[1]) return a[1] < b[1];
    if (a[3] != b[3]) return a[3] < b[3];
    return a[2] < b[2];
}

// A binary search tree of n nodes (not balanced; the search does not care). Node 0 is the head.
void BuildTree(std::mt19937 &rng, uint32_t n)
{
    std::memset(gTreeArena, 0xaa, sizeof(gTreeArena));
    KeyTreeNode *head = TreeNode(0);
    head->left = head->parent = head->right = head;
    head->isNil = 1;
    head->color = 1;
    for (uint32_t i = 1; i <= n; ++i) {
        KeyTreeNode *node = TreeNode(i);
        node->left = node->right = head;
        node->isNil = 0;
        node->color = 0;
        for (int k = 0; k < 4; ++k) node->key[k] = KeyWord(rng);
        node->value = rng();
        if (head->parent == head) {
            head->parent = node;
            node->parent = head;
            continue;
        }
        KeyTreeNode *cur = head->parent;
        for (;;) {
            KeyTreeNode *&next = TestLess(node->key, cur->key) ? cur->left : cur->right;
            if (next == head) {
                next = node;
                node->parent = cur;
                break;
            }
            cur = next;
        }
    }
}

void PickSearchKey(std::mt19937 &rng, uint32_t n, uint32_t *key)
{
    if (n > 0 && rng() % 2) {
        std::memcpy(key, TreeNode(1 + rng() % n)->key, 16);
        return;
    }
    for (int k = 0; k < 4; ++k) key[k] = KeyWord(rng);
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_EnsureLoaded, 0x9b05c0)
{
    InitFileFakes();
    auto orig = ctx.Original<U8Fn>(0x9b05c0);
    Tables plain = WithOpenFakes(MakeTables(ctx));
    std::mt19937 rng(0x5c0);
    for (int i = 0; i < 600; ++i) {
        Scene s = MakeScene(rng);
        PickBehaviour(s, rng);
        Trio(ctx, "EnsureLoaded", plain, plain, s,
             [&](uint8_t *self) -> uint64_t { return orig(self); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->EnsureLoaded(); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->EnsureLoaded(); });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_GetFields130, 0x9ac770)
{
    InitFileFakes();
    auto orig = ctx.Original<BoolPtrsFn>(0x9ac770);
    Tables plain = WithOpenFakes(MakeTables(ctx));
    Tables routed = WithOpenFakes(MakeTables(ctx));
    routed.primary[13] = Addr(static_cast<GetPtrsMember>(&cGZDBSegmentPackedFile::GetDates));
    std::mt19937 rng(0x13);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        PickBehaviour(s, rng);
        Trio(ctx, "GetDates", plain, routed, s,
             [&](uint8_t *self) -> uint64_t {
                 return orig(self, reinterpret_cast<uint32_t *>(self + kDst), reinterpret_cast<uint32_t *>(self + kDst + 4));
             },
             [&](uint8_t *self) -> uint64_t {
                 return Obj(self)->GetDates(reinterpret_cast<uint32_t *>(self + kDst), reinterpret_cast<uint32_t *>(self + kDst + 4));
             },
             [&](uint8_t *self) -> uint64_t {
                 return VirtualSlot<BoolPtrsFn>(self, 13)(self, reinterpret_cast<uint32_t *>(self + kDst), reinterpret_cast<uint32_t *>(self + kDst + 4));
             });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_LookupWord1, 0x9acac0)
{
    InitFileFakes();
    auto orig = ctx.Original<U32RecFn>(0x9acac0);
    Tables plain = WithOpenFakes(MakeTables(ctx));
    Tables routed = WithOpenFakes(MakeTables(ctx));
    routed.primary[27] = Addr(static_cast<RecU32Member>(&cGZDBSegmentPackedFile::GetRecordStoredSize));
    std::mt19937 rng(0x27);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        PickBehaviour(s, rng);
        Trio(ctx, "GetRecordStoredSize", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return orig(self, self + kRecIn); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->GetRecordStoredSize(reinterpret_cast<uint32_t *>(self + kRecIn)); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<U32RecFn>(self, 27)(self, self + kRecIn); });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_LookupWord0, 0x9aca40)
{
    InitFileFakes();
    auto orig = ctx.Original<U32RecFn>(0x9aca40);
    Tables plain = WithOpenFakes(MakeTables(ctx));
    Tables routed = WithOpenFakes(MakeTables(ctx));
    routed.primary[30] = Addr(static_cast<RecU32Member>(&cGZDBSegmentPackedFile::GetRecordOffset));
    std::mt19937 rng(0x30);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        PickBehaviour(s, rng);
        Trio(ctx, "GetRecordOffset", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return orig(self, self + kRecIn); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->GetRecordOffset(reinterpret_cast<uint32_t *>(self + kRecIn)); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<U32RecFn>(self, 30)(self, self + kRecIn); });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SeekTo, 0x9afa50)
{
    InitFileFakes();
    auto orig = ctx.Original<BoolU32Fn>(0x9afa50);
    Tables plain = WithOpenFakes(MakeTables(ctx));
    Tables routed = WithOpenFakes(MakeTables(ctx));
    routed.primary[55] = Addr(static_cast<SeekMember>(&cGZDBSegmentPackedFile::SeekTo));
    std::mt19937 rng(0x55);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        PickBehaviour(s, rng);
        uint32_t offset = PickWord(rng);
        Trio(ctx, "SeekTo", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return orig(self, offset); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->SeekTo(offset); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolU32Fn>(self, 55)(self, offset); });
    }
}

namespace {

// The three read/write slots differ in the file slot they use and the state they need.
template <int Slot, typename Member, typename Call>
void IoTest(difftest::Context &ctx, const char *name, uint32_t rva, Member member, uint32_t seed, Call callOurs)
{
    auto orig = ctx.Original<IoFn>(rva);
    Tables plain = WithOpenFakes(MakeTables(ctx));
    Tables routed = WithOpenFakes(MakeTables(ctx));
    routed.primary[Slot] = Addr(member);
    std::mt19937 rng(seed);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        PickBehaviour(s, rng);
        void *buf = AsPointer(PickWord(rng));
        uint32_t offset = PickWord(rng), length = PickWord(rng);
        Trio(ctx, name, plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return orig(self, buf, offset, length); },
             [&](uint8_t *self) -> uint64_t { return callOurs(Obj(self), buf, offset, length); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<IoFn>(self, Slot)(self, buf, offset, length); });
    }
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_ReadAt, 0x9af1a0)
{
    InitFileFakes();
    IoTest<56>(ctx, "ReadAt", 0x9af1a0, &cGZDBSegmentPackedFile::ReadAt, 0x56,
               [](cGZDBSegmentPackedFile *f, void *b, uint32_t o, uint32_t l) { return f->ReadAt(b, o, l); });
}

DIFF_TEST(cGZDBSegmentPackedFile_ReadCountedAt, 0x9af220)
{
    InitFileFakes();
    IoTest<57>(ctx, "ReadCountedAt", 0x9af220, &cGZDBSegmentPackedFile::ReadCountedAt, 0x57,
               [](cGZDBSegmentPackedFile *f, void *b, uint32_t o, uint32_t l) { return f->ReadCountedAt(b, o, reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(l))); });
}

DIFF_TEST(cGZDBSegmentPackedFile_WriteAt, 0x9b18d0)
{
    InitFileFakes();
    IoTest<58>(ctx, "WriteAt", 0x9b18d0, &cGZDBSegmentPackedFile::WriteAt, 0x58,
               [](cGZDBSegmentPackedFile *f, void *b, uint32_t o, uint32_t l) { return f->WriteAt(b, o, l); });
}

DIFF_TEST(cGZDBSegmentPackedFile_KeyTreeFind, 0x9b2bf0)
{
    auto orig = ctx.Original<TreeFindFn>(0x9b2bf0);
    std::mt19937 rng(0xb2);
    for (int i = 0; i < 1500; ++i) {
        uint32_t n = rng() % 12;
        BuildTree(rng, n);
        uint32_t key[4];
        PickSearchKey(rng, n, key);
        void *outA[1], *outB[1];
        outA[0] = outB[0] = nullptr;
        // The tree is the head pointer and the count; `this` is the address of the head pointer.
        struct {
            KeyTreeNode *head;
            uint32_t size;
        } tree = {TreeNode(0), n};
        void **ra = orig(&tree, outA, key);
        KeyTreeNode **rb = reinterpret_cast<KeyTree *>(&tree)->Find(reinterpret_cast<KeyTreeNode **>(outB), key);
        ++ctx.cases;
        if (ra != outA) ctx.Fail("KeyTreeFind trial %d: the original does not return out", i);
        if (rb != reinterpret_cast<KeyTreeNode **>(outB)) ctx.Fail("KeyTreeFind trial %d: ours does not return out", i);
        if (outA[0] != outB[0]) ctx.Fail("KeyTreeFind trial %d (n=%u): original found %p, ours %p", i, n, outA[0], outB[0]);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_ContainsKey, 0x9ad6f0)
{
    InitFileFakes();
    auto orig = ctx.Original<BoolKeyFn>(0x9ad6f0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[19] = Addr(static_cast<KeyMember>(&cGZDBSegmentPackedFile::IsRecordOpen));
    std::mt19937 rng(0x19);
    for (int i = 0; i < 600; ++i) {
        Scene s = MakeScene(rng);
        uint32_t n = rng() % 12;
        BuildTree(rng, n);
        SetPointer(s, 0x110, TreeNode(0));
        uint32_t key[4];
        PickSearchKey(rng, n, key);
        Trio(ctx, "IsRecordOpen", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return orig(self, key); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->IsRecordOpen(key); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolKeyFn>(self, 19)(self, key); });
    }
}

// ---- the thunks and the shared one-liners ----

DIFF_TEST(cGZDBSegmentPackedFile_SubQueryInterface, 0x9adac7)
{
    InitFakes();
    auto orig = ctx.Original<QueryFn>(0x9adac7);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[0] = Addr(static_cast<ViewQiMember>(&SegmentView::QueryInterface));
    static const uint32_t ids[] = {0x286d4a6a, 0x656b8f0c, 1, 0, 2, 0xffffffff};
    std::mt19937 rng(0xadac7);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t id = (rng() % 4 == 0) ? PickWord(rng) : ids[rng() % 6];
        SetWord(s, kDst, kUnchangedOut);
        Trio(ctx, "SubQueryInterface", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return orig(self + 4, id, reinterpret_cast<void **>(self + kDst)); },
             [&](uint8_t *self) -> uint64_t {
                 return reinterpret_cast<SegmentView *>(self + 4)->QueryInterface(id, reinterpret_cast<void **>(self + kDst));
             },
             [&](uint8_t *self) -> uint64_t {
                 return VirtualSlot<QueryFn>(self + 4, 0)(self + 4, id, reinterpret_cast<void **>(self + kDst));
             });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SubRelease, 0x9af988)
{
    InitFakes();
    auto orig = ctx.Original<ReleaseFn>(0x9af988);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[2] = Addr(static_cast<ViewU32Member>(&SegmentView::Release));
    std::mt19937 rng(0xaf988);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetWord(s, 8, rng() % 2 ? PickWord(rng) : static_cast<uint32_t>(rng() % 3));
        Trio(ctx, "SubRelease", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return orig(self + 4); },
             [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->Release(); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<ReleaseFn>(self + 4, 2)(self + 4); });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_AddRef, 0x9a7d10)
{
    InitFakes();
    auto orig = ctx.Original<RefFn>(0x9a7d10);
    auto origSub = ctx.Original<RefFn>(0x9a7d18);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0xa7d10);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetWord(s, 8, PickWord(rng));
        // Primary slot 1 (0x9a7d10), then the subobject's (0x9a7d18): both add one to the count at +8.
        Trio(ctx, "AddRef", plain, plain, s,
             [&](uint8_t *self) -> uint64_t { return orig(self); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->AddRef(); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->AddRef(); });
        Trio(ctx, "SubAddRef", plain, plain, s,
             [&](uint8_t *self) -> uint64_t { return origSub(self + 4); },
             [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->AddRef(); },
             [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->AddRef(); });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_Init, 0x9ad210)
{
    InitFakes();
    auto orig = ctx.Original<U8Fn>(0x9ad210);
    auto origSub = ctx.Original<U8Fn>(0x1d3dbc);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0xad210);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        s.image[0x1c] = (rng() % 2) ? 0 : static_cast<uint8_t>(rng());
        Trio(ctx, "Init", plain, plain, s,
             [&](uint8_t *self) -> uint64_t { return orig(self); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->Init(); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->Init(); });
        Trio(ctx, "SubInit", plain, plain, s,
             [&](uint8_t *self) -> uint64_t { return origSub(self + 4); },
             [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->Init(); },
             [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->Init(); });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SubGetPath, 0x1d3b5e)
{
    InitFakes();
    auto orig = ctx.Original<VoidPtrFn>(0x1d3b5e);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[11] = Addr(static_cast<ViewPathMember>(&SegmentView::GetPath));
    std::mt19937 rng(0x1d3b5e);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t len = static_cast<uint32_t>(rng() % 41);
        s.heapPath = len > 15 || rng() % 3 == 0;
        uint32_t capacity = s.heapPath ? (len > 15 ? len + rng() % 8 : 16 + rng() % 8) : 15;
        SetWord(s, 0x4c, len);
        SetWord(s, 0x50, capacity);
        if (s.heapPath) {
            s.heap.resize(kHeapCap);
            for (auto &b : s.heap) b = static_cast<uint8_t>(rng());
        }
        Trio(ctx, "SubGetPath", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { orig(self + 4, self + kStr); return 0; },
             [&](uint8_t *self) -> uint64_t { reinterpret_cast<SegmentView *>(self + 4)->GetPath(reinterpret_cast<cIGZString *>(self + kStr)); return 0; },
             [&](uint8_t *self) -> uint64_t { VirtualSlot<VoidPtrFn>(self + 4, 11)(self + 4, self + kStr); return 0; });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SubSetPath, 0x1d450d)
{
    InitNewFakes();
    auto orig = ctx.Original<BoolPtrFn>(0x1d450d);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[12] = Addr(static_cast<ViewSetPathMember>(&SegmentView::SetPath));
    std::mt19937 rng(0x1d450d);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        s.image[0x24] = PickFlag(rng);
        SetPointer(s, 0x38, gSink38Tab);
        void *path = AsPointer(PickWord(rng));
        Trio(ctx, "SubSetPath", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return orig(self + 4, path); },
             [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->SetPath(path); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolPtrFn>(self + 4, 12)(self + 4, path); });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SubGetSegmentID, 0x147d00)
{
    InitFakes();
    auto orig = ctx.Original<RefFn>(0x147d00);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[15] = Addr(static_cast<ViewGetIdMember>(&SegmentView::GetSegmentID));
    std::mt19937 rng(0x147d00);
    for (int i = 0; i < 100; ++i) {
        Scene s = MakeScene(rng);
        Trio(ctx, "SubGetSegmentID", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return orig(self + 4); },
             [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->GetSegmentID(); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<RefFn>(self + 4, 15)(self + 4); });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SubDelete, 0x9aadc0)
{
    InitFileFakes();
    auto orig = ctx.Original<VoidFn>(0x9aadc0);
    Tables plain = WithOpenFakes(MakeTables(ctx));
    Tables routed = WithOpenFakes(MakeTables(ctx));
    routed.sub[34] = Addr(static_cast<ViewVoidMember>(&SegmentView::Delete));
    std::mt19937 rng(0xaadc0);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        s.image[0x24] = PickFlag(rng);
        s.image[0x1c] = (rng() % 2) ? 0 : static_cast<uint8_t>(rng());
        Trio(ctx, "SubDelete", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { orig(self + 4); return 0; },
             [&](uint8_t *self) -> uint64_t { reinterpret_cast<SegmentView *>(self + 4)->Delete(); return 0; },
             [&](uint8_t *self) -> uint64_t { VirtualSlot<VoidFn>(self + 4, 34)(self + 4); return 0; });
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_SmallSlots, 0x254bc0)
{
    InitFakes();
    auto getSub = ctx.Original<RefFn>(0x1c04e0);
    auto getF4 = ctx.Original<RefFn>(0x254bc0);
    auto setB = ctx.Original<VoidU32Fn>(0x9afcc0);
    auto getFile = ctx.Original<RefFn>(0x238ec0);
    auto get158 = ctx.Original<RefFn>(0x5b1920);
    auto no46 = ctx.Original<BoolU32U32U32Fn>(0x1474a0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[3] = Addr(static_cast<void *(cGZDBSegmentPackedFile::*)()>(&cGZDBSegmentPackedFile::AsPersistDBSegment));
    routed.primary[6] = Addr(static_cast<uint32_t (cGZDBSegmentPackedFile::*)()>(&cGZDBSegmentPackedFile::GetBaseOffset));
    routed.primary[18] = Addr(static_cast<VoidU8Member>(&cGZDBSegmentPackedFile::SetCreateIfMissing));
    routed.primary[22] = Addr(static_cast<void *(cGZDBSegmentPackedFile::*)()>(&cGZDBSegmentPackedFile::GetFile));
    routed.primary[25] = Addr(static_cast<uint32_t (cGZDBSegmentPackedFile::*)()>(&cGZDBSegmentPackedFile::GetGroupInfo));
    routed.primary[46] = Addr(static_cast<bool (cGZDBSegmentPackedFile::*)(uint32_t, uint32_t, uint32_t)>(&cGZDBSegmentPackedFile::Reserved46));
    std::mt19937 rng(0x254);
    for (int i = 0; i < 200; ++i) {
        Scene s = MakeScene(rng);
        uint32_t v = PickWord(rng);
        auto off = [](uintptr_t p) -> uint64_t { return static_cast<uint64_t>(p) - reinterpret_cast<uintptr_t>(gArena); };
        Trio(ctx, "AsPersistDBSegment", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return off(getSub(self)); },
             [&](uint8_t *self) -> uint64_t { return off(reinterpret_cast<uintptr_t>(Obj(self)->AsPersistDBSegment())); },
             [&](uint8_t *self) -> uint64_t { return off(reinterpret_cast<uintptr_t>(VirtualSlot<void *(__thiscall *)(void *)>(self, 3)(self))); });
        Trio(ctx, "GetBaseOffset", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return getF4(self); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->GetBaseOffset(); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<RefFn>(self, 6)(self); });
        Trio(ctx, "SetCreateIfMissing", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { setB(self, v); return 0; },
             [&](uint8_t *self) -> uint64_t { Obj(self)->SetCreateIfMissing(static_cast<uint8_t>(v)); return 0; },
             [&](uint8_t *self) -> uint64_t { VirtualSlot<VoidU32Fn>(self, 18)(self, v); return 0; });
        Trio(ctx, "GetFile", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return off(getFile(self)); },
             [&](uint8_t *self) -> uint64_t { return off(reinterpret_cast<uintptr_t>(Obj(self)->GetFile())); },
             [&](uint8_t *self) -> uint64_t { return off(reinterpret_cast<uintptr_t>(VirtualSlot<void *(__thiscall *)(void *)>(self, 22)(self))); });
        Trio(ctx, "GetGroupInfo", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return get158(self); },
             [&](uint8_t *self) -> uint64_t { return Obj(self)->GetGroupInfo(); },
             [&](uint8_t *self) -> uint64_t { return VirtualSlot<RefFn>(self, 25)(self); });
        // The original clears al only; the rest of eax is not part of the result.
        Trio(ctx, "Reserved46", plain, routed, s,
             [&](uint8_t *self) -> uint64_t { return static_cast<uint8_t>(no46(self, v, v + 1, v + 2)); },
             [&](uint8_t *self) -> uint64_t { return static_cast<uint8_t>(Obj(self)->Reserved46(v, v + 1, v + 2)); },
             [&](uint8_t *self) -> uint64_t { return static_cast<uint8_t>(VirtualSlot<BoolU32U32U32Fn>(self, 46)(self, v, v + 1, v + 2)); });
    }
}

// ---- the constructor ----

namespace {

std::vector<uint32_t> gMallocSizes;

void *__cdecl LoggingMalloc(uint32_t size)
{
    gMallocSizes.push_back(size);
    void *p = HeapAlloc(GetProcessHeap(), 0, size);
    std::memset(p, 0xcd, size);
    return p;
}

// Points the original's RZMalloc at the process heap while alive.
struct MallocRedirect {
    uint8_t *site;
    uint8_t saved[5];
    MallocRedirect(difftest::Context &ctx, void *target) : site(ctx.image.At<uint8_t>(0x1be3b0))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~MallocRedirect() { std::memcpy(site, saved, 5); }
};

uint32_t Word(const uint8_t *base, size_t off)
{
    uint32_t v;
    std::memcpy(&v, base + off, sizeof(v));
    return v;
}

constexpr size_t kObjSize = 0x1b8;
constexpr size_t kHeads[4] = {0x110, 0x184, 0x18c, 0x194};

// The chunk size the game's allocator (ours) gives a block: a request of `bytes` allocated and freed.
uint32_t ChunkSize(uint32_t bytes)
{
    void *p = RZMemory::Malloc(bytes);
    uint32_t size = Word(static_cast<uint8_t *>(p), static_cast<size_t>(-4)) & ~7u;
    RZMemory::Free(p);
    return size;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_Construct, 0x9a6ed0)
{
    // The original's RZMalloc goes to the process heap and logs its request; ours allocates from a real
    // allocator object (the game's global one is not constructed here).
    MallocRedirect redirect(ctx, reinterpret_cast<void *>(&LoggingMalloc));
    auto *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    RZMemory::testGlobalAllocator = allocator;
    auto orig = ctx.Original<CtorFn>(0x9a6ed0);
    std::mt19937 rng(0xc0de);
    for (int i = 0; i < 200; ++i) {
        uint32_t seed = rng();
        auto make = [&]() {
            std::mt19937 fill(seed);
            auto *mem = static_cast<uint8_t *>(::operator new(kObjSize));
            for (size_t k = 0; k < kObjSize; ++k) mem[k] = static_cast<uint8_t>(fill());
            return mem;
        };
        uint8_t *a = make();
        uint8_t *b = make();
        gMallocSizes.clear();
        void *ra = orig(a);
        std::vector<uint32_t> requested = gMallocSizes;
        void *rb = reinterpret_cast<cGZDBSegmentPackedFile *>(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("Construct seed %u: does not return this", seed);
        if (requested != std::vector<uint32_t>{0x24, 0x18, 0x18, 0x18}) ctx.Fail("Construct seed %u: unexpected requests from the original", seed);
        // Every word, with the heap pointers reduced to what can be compared.
        std::vector<uint32_t> da, db;
        for (size_t off = 0; off < kObjSize; off += 4) {
            uint32_t va = Word(a, off), vb = Word(b, off);
            bool isHead = false;
            for (size_t h : kHeads) isHead |= (off == h);
            if (off == 0x10) {
                va = va != 0;
                vb = vb != 0;
            } else if (isHead) {
                va = vb = 0x1111;
            }
            da.push_back(va);
            db.push_back(vb);
        }
        // The head nodes: self-linked, black and nil, and of the size the original asked for.
        for (int k = 0; k < 4; ++k) {
            const uint8_t *na = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(Word(a, kHeads[k])));
            const uint8_t *nb = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(Word(b, kHeads[k])));
            for (size_t w = 0; w < 3; ++w) {
                da.push_back(Word(na, w * 4) == reinterpret_cast<uintptr_t>(na));
                db.push_back(Word(nb, w * 4) == reinterpret_cast<uintptr_t>(nb));
            }
            da.push_back(na[0xc]);
            db.push_back(nb[0xc]);
            da.push_back(na[0xd]);
            db.push_back(nb[0xd]);
            da.push_back(ChunkSize(requested[k]));
            db.push_back(Word(nb, static_cast<size_t>(-4)) & ~7u);
        }
        if (da != db) {
            size_t at = 0;
            while (at < da.size() && da[at] == db[at]) ++at;
            ctx.Fail("Construct seed %u: word %zu differs (original %08x, ours %08x)", seed, at, da[at], db[at]);
        }
        for (uint8_t *o : {a, b}) {
            for (size_t h : kHeads) {
                void *node = reinterpret_cast<void *>(static_cast<uintptr_t>(Word(o, h)));
                if (o == a) HeapFree(GetProcessHeap(), 0, node);
                else RZMemory::Free(node);
            }
            reinterpret_cast<cRZCriticalSection *>(o + 0xc)->Destruct();
            ::operator delete(o, kObjSize);
        }
    }
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
}

// ---- The hash map constructor, the tree erase, Shutdown and the destructor ----

namespace {

constexpr size_t kRelA = 0x2e0; // two fake refcounted objects in the arena (vtable gRelTab), for +0x14 and +0x18
constexpr size_t kRelB = 0x2f0;
constexpr int64_t kRelease = 30;
void *gRelTab[8];

uint32_t __fastcall RecRelease(void *self, void *)
{
    Push(kRelease, Off(self), 0);
    return 0;
}

// Redirects an image function (at least five bytes long) to a replacement while alive.
struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(difftest::Context &ctx, uint32_t rva, void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~Redirect() { std::memcpy(site, saved, 5); }
};

std::vector<void *> gFreed;

void __cdecl LoggingFree(void *p)
{
    gFreed.push_back(p);
    HeapFree(GetProcessHeap(), 0, p);
}

// A binary tree of nodes with the std::map header layout, shaped by random insertion. `allocate` provides the
// node memory. all[0] is the head.
struct SimpleNode {
    SimpleNode *left, *parent, *right;
    uint8_t color, isNil, pad[2];
};

template <typename Alloc> void BuildShape(std::mt19937 &rng, uint32_t n, Alloc allocate, std::vector<SimpleNode *> &all)
{
    SimpleNode *head = static_cast<SimpleNode *>(allocate());
    head->left = head->parent = head->right = head;
    head->color = 1;
    head->isNil = 1;
    all.push_back(head);
    std::vector<uint32_t> keys{0};
    for (uint32_t i = 1; i <= n; ++i) {
        SimpleNode *node = static_cast<SimpleNode *>(allocate());
        node->left = node->right = head;
        node->color = 0;
        node->isNil = 0;
        all.push_back(node);
        keys.push_back(static_cast<uint32_t>(rng()));
        if (head->parent == head) {
            head->parent = node;
            node->parent = head;
            continue;
        }
        SimpleNode *cur = head->parent;
        for (;;) {
            size_t ci = std::find(all.begin(), all.end(), cur) - all.begin();
            SimpleNode *&next = keys[i] < keys[ci] ? cur->left : cur->right;
            if (next == head) {
                next = node;
                node->parent = cur;
                break;
            }
            cur = next;
        }
    }
}

void SetWordArena(uint8_t *self, size_t off, uint32_t v)
{
    std::memcpy(self + off, &v, sizeof(v));
}

struct Dummy {
    uint32_t a[4];
};

uint32_t Word32(const uint8_t *base, size_t off)
{
    uint32_t v;
    std::memcpy(&v, base + off, sizeof(v));
    return v;
}

GeneralAllocator *NewAllocator()
{
    auto *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    return allocator;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_HashMap32Construct, 0x9a6ad0)
{
    auto orig = ctx.Original<CtorFn>(0x9a6ad0);
    HANDLE heap = GetProcessHeap();
    std::mt19937 rng(0xa6ad);
    for (int i = 0; i < 200; ++i) {
        uint32_t seed = rng();
        auto make = [&]() {
            std::mt19937 fill(seed);
            auto *mem = static_cast<uint8_t *>(::operator new(0x20));
            for (int k = 0; k < 0x20; ++k) mem[k] = static_cast<uint8_t>(fill());
            return mem;
        };
        // The words with the heap pointers reduced to sizes and links that can be compared.
        auto dump = [&](uint8_t *m) {
            std::vector<uint32_t> out;
            uint32_t buckets = Word32(m, 0xc);
            auto *head = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(Word32(m, 4)));
            for (size_t off = 0; off < 0x20; off += 4) {
                uint32_t v = Word32(m, off);
                if (off == 4 || off == 0xc) v = 0x1111;
                else if (off == 0x10 || off == 0x14) v -= buckets;
                out.push_back(v);
            }
            out.push_back(static_cast<uint32_t>(HeapSize(heap, 0, head)));
            out.push_back(static_cast<uint32_t>(HeapSize(heap, 0, reinterpret_cast<void *>(static_cast<uintptr_t>(buckets)))));
            out.push_back(head[0] == reinterpret_cast<uintptr_t>(head));
            out.push_back(head[1] == reinterpret_cast<uintptr_t>(head));
            for (int k = 0; k < 16; ++k)
                out.push_back(reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(buckets))[k] == reinterpret_cast<uintptr_t>(head));
            return out;
        };
        uint8_t *a = make();
        uint8_t *b = make();
        void *ra = orig(a);
        void *rb = reinterpret_cast<HashMap32 *>(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("HashMap32 Construct seed %u: does not return this", seed);
        std::vector<uint32_t> da = dump(a), db = dump(b);
        if (da != db) {
            size_t at = 0;
            while (at < da.size() && da[at] == db[at]) ++at;
            ctx.Fail("HashMap32 Construct seed %u: word %zu differs (original %08x, ours %08x)", seed, at, da[at], db[at]);
        }
        // Both are destroyed by the library's destructor.
        reinterpret_cast<std::unordered_map<uint32_t, uint32_t> *>(a)->~unordered_map();
        reinterpret_cast<std::unordered_map<uint32_t, uint32_t> *>(b)->~unordered_map();
        ::operator delete(a, 0x20);
        ::operator delete(b, 0x20);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_KeyTreeEraseSubtree, 0x9a3900)
{
    // The original frees through RZFree, which is redirected to the process heap and logged; ours frees
    // through a real allocator, and its frees are seen by what that allocator hands out next (the most
    // recently freed chunks of a size come back first).
    Redirect redirect(ctx, 0x1be3a1, reinterpret_cast<void *>(&LoggingFree));
    GeneralAllocator *allocator = NewAllocator();
    RZMemory::testGlobalAllocator = allocator;
    using EraseFn = void(__thiscall *)(void *, const void *, void *);
    auto origErase = ctx.Original<EraseFn>(0x9a3900);
    std::mt19937 rng(0x3900);
    for (int i = 0; i < 400; ++i) {
        uint32_t n = rng() % 12;
        Dummy dummy{};
        // Original side.
        std::vector<SimpleNode *> a;
        BuildShape(rng, n, [] { return HeapAlloc(GetProcessHeap(), 0, 0x18); }, a);
        gFreed.clear();
        origErase(&dummy, &dummy, a[0]->parent);
        std::vector<void *> freed = gFreed;
        std::vector<void *> expected(a.begin() + 1, a.end());
        std::sort(freed.begin(), freed.end());
        std::sort(expected.begin(), expected.end());
        bool origOk = freed == expected;
        HeapFree(GetProcessHeap(), 0, a[0]);
        // Our side.
        std::vector<SimpleNode *> b;
        BuildShape(rng, n, [] { return RZMemory::Malloc(0x18); }, b);
        reinterpret_cast<KeyTree *>(&dummy)->EraseSubtree(&dummy, reinterpret_cast<KeyTreeNode *>(b[0]->parent));
        std::vector<void *> again;
        for (uint32_t k = 0; k < n; ++k) again.push_back(RZMemory::Malloc(0x18));
        std::vector<void *> created(b.begin() + 1, b.end());
        std::sort(again.begin(), again.end());
        std::sort(created.begin(), created.end());
        bool oursOk = again == created;
        for (void *p : again) RZMemory::Free(p);
        RZMemory::Free(b[0]);
        ++ctx.cases;
        if (!origOk) ctx.Fail("EraseSubtree trial %d (n=%u): the original did not free exactly the nodes (test setup)", i, n);
        if (!oursOk) ctx.Fail("EraseSubtree trial %d (n=%u): ours did not free exactly the nodes", i, n);
    }
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
}

namespace {

// A heap block of 0x400 bytes holding an unordered_set<unsigned> with k elements (its free is seen by a
// FreeRecorder).
void *MakeSetBlock(std::mt19937 &rng)
{
    void *p = ::operator new(0x400);
    auto *set = new (p) std::unordered_set<uint32_t>();
    for (uint32_t k = 0, n = rng() % 20; k < n; ++k) set->insert(static_cast<uint32_t>(rng()));
    return p;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_Shutdown, 0x9b0530)
{
    InitFakes();
    gRelTab[2] = reinterpret_cast<void *>(&RecRelease);
    auto orig = ctx.Original<BoolFn>(0x9b0530);
    auto origThunk = ctx.Original<BoolFn>(0x9b05a9);
    Tables plain = MakeTables(ctx);
    plain.sub[9] = reinterpret_cast<void *>(&RecClose9);
    Tables routed = plain;
    routed.primary[5] = Addr(static_cast<BoolBoolMember>(&cGZDBSegmentPackedFile::Shutdown));
    routed.sub[4] = Addr(static_cast<ViewBoolMember2>(&SegmentView::Shutdown));
    std::mt19937 rng(0xb0530);
    for (int i = 0; i < 500; ++i) {
        Scene s = MakeScene(rng);
        s.image[0x24] = PickFlag(rng);
        s.image[0x1c] = (rng() % 3 == 0) ? 0 : static_cast<uint8_t>(1 + rng() % 255);
        SetPointer(s, kRelA, gRelTab);
        SetPointer(s, kRelB, gRelTab);
        SetWord(s, 0x14, rng() % 3 ? 0u : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena + kRelB)));
        SetWord(s, 0x18, rng() % 3 ? 0u : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena + kRelA)));
        const bool haveA = rng() % 3 != 0, haveB = rng() % 3 != 0;
        uint32_t seedA = rng(), seedB = rng();
        // Each run gets its own blocks: placed at +0x1ac and +0x1b0 before the call, and checked afterwards.
        auto prepare = [&](uint8_t *self, void **blockA, void **blockB) {
            std::mt19937 ra(seedA), rb(seedB);
            *blockA = haveA ? MakeSetBlock(ra) : nullptr;
            *blockB = haveB ? MakeSetBlock(rb) : nullptr;
            SetWordArena(self, 0x1ac, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*blockA)));
            SetWordArena(self, 0x1b0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*blockB)));
        };
        // Frees recorded across the call (rec started right before it).
        auto freedMask = [&](difftest::FreeRecorder &rec, void *a, void *b) -> uint64_t {
            rec.Stop();
            uint64_t m = 0;
            if (a != nullptr && rec.Freed(a)) m |= 0x100;
            if (b != nullptr && rec.Freed(b)) m |= 0x200;
            // A pointer left in the object is a run-specific address: reduce it to "not null".
            for (size_t off : {size_t(0x1ac), size_t(0x1b0)})
                if (Word32(gArena, off) != 0) SetWordArena(gArena, off, 0x1);
            return m;
        };
        Trio(ctx, "Shutdown", plain, routed, s,
             [&](uint8_t *self) -> uint64_t {
                 void *x, *y;
                 prepare(self, &x, &y);
                 difftest::FreeRecorder rec;
                 uint64_t r = orig(self);
                 return r | freedMask(rec, x, y);
             },
             [&](uint8_t *self) -> uint64_t {
                 void *x, *y;
                 prepare(self, &x, &y);
                 difftest::FreeRecorder rec;
                 uint64_t r = Obj(self)->Shutdown();
                 return r | freedMask(rec, x, y);
             },
             [&](uint8_t *self) -> uint64_t {
                 void *x, *y;
                 prepare(self, &x, &y);
                 difftest::FreeRecorder rec;
                 uint64_t r = VirtualSlot<BoolFn>(self, 5)(self);
                 return r | freedMask(rec, x, y);
             });
        // Second pass through the subobject's thunk (sub slot 4) for the same scene.
        auto viaSub = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            void *x, *y;
            prepare(self, &x, &y);
            difftest::FreeRecorder rec;
            uint64_t r = VirtualSlot<BoolFn>(self + 4, 4)(self + 4);
            return r | freedMask(rec, x, y);
        });
        auto direct = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            void *x, *y;
            prepare(self, &x, &y);
            difftest::FreeRecorder rec;
            uint64_t r = origThunk(self + 4);
            return r | freedMask(rec, x, y);
        });
        Compare(ctx, "Shutdown (sub slot 4, thunk 0x9b05a9)", direct, viaSub, viaSub);
    }
}

// ---- The destructor ----

namespace {

alignas(8) uint8_t gFakeCache[16];
alignas(8) uint8_t gFakeNs[16];

template <typename Alloc>
void FillTree(SimpleNode *head, uint32_t count, std::mt19937 &rng, Alloc allocate, std::vector<void *> &created)
{
    std::unordered_map<SimpleNode *, uint32_t> keys;
    for (uint32_t i = 0; i < count; ++i) {
        SimpleNode *node = static_cast<SimpleNode *>(allocate());
        node->left = node->right = head;
        node->color = 0;
        node->isNil = 0;
        created.push_back(node);
        uint32_t key = static_cast<uint32_t>(rng());
        keys[node] = key;
        if (head->parent == head) {
            head->parent = node;
            node->parent = head;
            continue;
        }
        SimpleNode *cur = head->parent;
        for (;;) {
            SimpleNode *&next = key < keys[cur] ? cur->left : cur->right;
            if (next == head) {
                next = node;
                node->parent = cur;
                break;
            }
            cur = next;
        }
    }
}

struct DtorObject {
    uint8_t *mem = nullptr;
    std::vector<void *> nodes18, nodes24;
    bool large = false;
    bool initialized = false;
    void *fileBuffers[2] = {nullptr, nullptr}; // storage of the cRZFile's read and write buffers
    void *setA = nullptr, *setB = nullptr;
};

// Builds a constructed object (by the original constructor or ours) and puts the same random contents in it.
// Everything that decides what the destructor does is chosen from `seed`, so two objects built from the same
// seed hold the same shapes. The state byte stays zero: the destructor would otherwise call the real Close.
void BuildDtorObject(difftest::Context &ctx, bool original, uint32_t seed, DtorObject &o)
{
    using Ctor = void *(__thiscall *)(void *);
    std::mt19937 rng(seed);
    o.mem = static_cast<uint8_t *>(::operator new(0x400)); // too big for the low-fragmentation heap
    std::memset(o.mem, 0xa5, 0x400);
    if (original) ctx.Original<Ctor>(0x9a6ed0)(o.mem);
    else reinterpret_cast<cGZDBSegmentPackedFile *>(o.mem)->Construct();
    auto allocate = [&](uint32_t bytes) -> void * {
        return original ? HeapAlloc(GetProcessHeap(), 0, bytes) : RZMemory::Malloc(bytes);
    };
    // The head nodes the constructor made, and extra nodes under them.
    for (int k = 0; k < 4; ++k) {
        size_t off = kHeads[k];
        auto *head = reinterpret_cast<SimpleNode *>(static_cast<uintptr_t>(Word(o.mem, off)));
        std::vector<void *> &list = k == 0 ? o.nodes24 : o.nodes18;
        list.push_back(head);
        uint32_t bytes = k == 0 ? 0x24 : 0x18;
        FillTree(head, rng() % 5, rng, [&] { return allocate(bytes); }, list);
    }
    o.initialized = (rng() % 3) != 0;
    o.mem[0x1c] = o.initialized ? static_cast<uint8_t>(1 + rng() % 255) : 0;
    uint32_t cache = (rng() % 3 == 0) ? 0u : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gFakeCache));
    uint32_t ns = (rng() % 3 == 0) ? 0u : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gFakeNs));
    std::memcpy(o.mem + 0x14, &cache, 4);
    std::memcpy(o.mem + 0x18, &ns, 4);
    if (o.initialized) {
        std::mt19937 ra(rng()), rb(rng());
        o.setA = (rng() % 3 == 0) ? nullptr : MakeSetBlock(ra);
        o.setB = (rng() % 3 == 0) ? nullptr : MakeSetBlock(rb);
        uint32_t pa = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o.setA));
        uint32_t pb = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o.setB));
        std::memcpy(o.mem + 0x1ac, &pa, 4);
        std::memcpy(o.mem + 0x1b0, &pb, 4);
    }
    uint32_t len = rng() % 41;
    o.large = len > 15;
    if (len > 0) new (o.mem + 0x3c) std::string(len, 'x');
    // The cRZFile at +0x58 keeps its read and write buffers in vectors (at +0x70 and +0x88 of the file); the
    // destructor frees them.
    for (int k = 0; k < 2; ++k) {
        if (rng() % 2) {
            auto *v = new (o.mem + 0x58 + (k == 0 ? 0x70 : 0x88)) std::vector<char>(0x400);
            o.fileBuffers[k] = v->data();
        }
    }
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_DeletingDestructor, 0x9a7a00)
{
    gRelTab[2] = reinterpret_cast<void *>(&RecRelease);
    void *relVT = gRelTab;
    std::memcpy(gFakeCache, &relVT, sizeof(relVT));
    std::memcpy(gFakeNs, &relVT, sizeof(relVT));
    // The original allocates and frees through the process heap (logged); ours through a real allocator.
    Redirect mallocRedirect(ctx, 0x1be3b0, reinterpret_cast<void *>(&LoggingMalloc));
    Redirect freeRedirect(ctx, 0x1be3a1, reinterpret_cast<void *>(&LoggingFree));
    GeneralAllocator *allocator = NewAllocator();
    RZMemory::testGlobalAllocator = allocator;
    using DtorFn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<DtorFn>(0x9a7a00);
    auto origThunk = ctx.Original<DtorFn>(0x9a76f6);
    std::mt19937 rng(0x7a00);
    for (int i = 0; i < 500; ++i) {
        uint32_t seed = rng();
        uint32_t flags = rng() % 4; // bit 0 frees the object
        bool viaThunk = (i % 2) != 0;
        DtorObject a, b;
        BuildDtorObject(ctx, true, seed, a);
        BuildDtorObject(ctx, false, seed, b);
        // Original.
        gFreed.clear();
        gLog.clear();
        difftest::FreeRecorder recA;
        void *ra = viaThunk ? origThunk(a.mem + 4, flags) : orig(a.mem, flags);
        recA.Stop();
        std::vector<void *> freedByOriginal = gFreed;
        std::vector<int64_t> logA = gLog;
        bool freedA = (flags & 1) && recA.Freed(a.mem);
        uint32_t buffersA = 0;
        for (int k = 0; k < 2; ++k)
            if (a.fileBuffers[k] != nullptr && recA.Freed(a.fileBuffers[k])) buffersA |= 1u << k;
        // Ours.
        gLog.clear();
        difftest::FreeRecorder recB;
        void *rb = viaThunk ? static_cast<void *>(reinterpret_cast<SegmentView *>(b.mem + 4)->DeletingDestructor(flags))
                            : static_cast<void *>(reinterpret_cast<cGZDBSegmentPackedFile *>(b.mem)->DeletingDestructor(flags));
        recB.Stop();
        std::vector<int64_t> logB = gLog;
        bool freedB = (flags & 1) && recB.Freed(b.mem);
        uint32_t buffersB = 0;
        for (int k = 0; k < 2; ++k)
            if (b.fileBuffers[k] != nullptr && recB.Freed(b.fileBuffers[k])) buffersB |= 1u << k;
        ++ctx.cases;
        if (buffersA != buffersB) ctx.Fail("Destructor trial %d: file buffers freed: original %u, ours %u", i, buffersA, buffersB);
        if (int(a.fileBuffers[0] != nullptr) + int(a.fileBuffers[1] != nullptr) != int((buffersA & 1) + ((buffersA >> 1) & 1)))
            ctx.Fail("Destructor trial %d: the original did not free the file buffers (test setup)", i);
        if (ra != a.mem || rb != b.mem) ctx.Fail("Destructor trial %d: does not return this", i);
        if (logA != logB) ctx.Fail("Destructor trial %d: the Release calls differ (%zu vs %zu values)", i, logA.size(), logB.size());
        if (freedA != freedB) ctx.Fail("Destructor trial %d (flags %u): object freed: original %d, ours %d", i, flags, freedA, freedB);
        if (!(flags & 1)) {
            std::vector<uint32_t> da, db;
            for (size_t off = 0; off < kObjSize; off += 4) {
                uint32_t va = Word(a.mem, off), vb = Word(b.mem, off);
                bool isHead = false;
                for (size_t h : kHeads) isHead |= (off == h);
                if (off == 0x10 || isHead) va = vb = 0; // the lock block and the heads are freed
                if (off == 0x1ac || off == 0x1b0) {      // left behind only when Shutdown did not run
                    va = va != 0;
                    vb = vb != 0;
                }
                if (off == 0x3c && a.large) va &= 0xff, vb &= 0xff; // a freed string buffer's address
                da.push_back(va);
                db.push_back(vb);
            }
            if (da != db) {
                size_t at = 0;
                while (at < da.size() && da[at] == db[at]) ++at;
                ctx.Fail("Destructor trial %d: word at +0x%zx differs (original %08x, ours %08x)", i, at * 4, da[at], db[at]);
            }
        }
        // The nodes: the original freed exactly the nodes it was given; ours leaves exactly the same number
        // of blocks of each size to be handed out again.
        std::vector<void *> all;
        all.insert(all.end(), a.nodes18.begin(), a.nodes18.end());
        all.insert(all.end(), a.nodes24.begin(), a.nodes24.end());
        std::sort(all.begin(), all.end());
        std::sort(freedByOriginal.begin(), freedByOriginal.end());
        if (freedByOriginal != all) ctx.Fail("Destructor trial %d: the original did not free exactly the tree nodes (test setup)", i);
        for (int cls = 0; cls < 2; ++cls) {
            const std::vector<void *> &made = cls == 0 ? b.nodes18 : b.nodes24;
            std::vector<void *> again, expect(made);
            for (size_t k = 0; k < made.size(); ++k) again.push_back(RZMemory::Malloc(cls == 0 ? 0x18 : 0x24));
            std::sort(again.begin(), again.end());
            std::sort(expect.begin(), expect.end());
            if (again != expect) ctx.Fail("Destructor trial %d: ours did not free exactly the %s nodes", i, cls == 0 ? "0x18-byte" : "0x24-byte");
            for (void *p : again) RZMemory::Free(p);
        }
        if (!(flags & 1)) {
            ::operator delete(a.mem);
            ::operator delete(b.mem);
        }
    }
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
}
