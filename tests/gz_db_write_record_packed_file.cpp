// Differential tests for the cGZDBWriteRecordPackedFile slots reimplemented so far (layout in
// src/gz/cGZDBWriteRecordPackedFile.h). The object lives in one fixed arena, so pointers compare equal between
// runs. The record's GetAccessFlags (subobject slot 5) and the sink object's slots are fakes that record their
// calls. The sink's answers come from the scene. The buffer the record writes from is inside the arena, so
// results and arena bytes (including the output) must match between the original, our function called directly,
// and our function called through a vtable slot.
#include "difftest.h"
#include "game/vtable.h"
#include "gz/cGZDBWriteRecordPackedFile.h"

#include <climits>
#include <cstring>
#include <random>
#include <vector>

namespace {

using U32Fn = uint32_t (__thiscall *)(void *);
using BoolU32Fn = bool (__thiscall *)(void *, uint32_t);
using BoolI32Fn = bool (__thiscall *)(void *, int32_t);
using BoolVoidU32Fn = bool (__thiscall *)(void *, void *, uint32_t);

using GetFieldMember = bool (cGZDBWriteRecordPackedFile::*)(void *, uint32_t);
using U32Member = uint32_t (cGZDBWriteRecordPackedFile::*)();
using BoolU32Member = bool (cGZDBWriteRecordPackedFile::*)(uint32_t);
using BoolI32Member = bool (cGZDBWriteRecordPackedFile::*)(int32_t);

constexpr size_t kBytes = 0x300;
constexpr size_t kSinkObj = 0x80; // fake sink object (vtable pointer at its +0)
constexpr size_t kBufBase = 0x100; // the buffer's start is inside [kBufBase, kBufBase + 0x40)
constexpr size_t kOut = 0x200;     // GetFieldVoid's output (at most 0x80 bytes are written)

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

cGZDBWriteRecordPackedFile *Obj(void *p)
{
    return static_cast<cGZDBWriteRecordPackedFile *>(p);
}

// The object, fixed in memory so that pointers into it are the same in every run.
alignas(16) uint8_t gArena[kBytes];

enum Kind : int64_t { kAccess = 1, kSinkPos = 6, kSinkSize = 7, kSinkSeek = 12, kSinkWrite = 14, kPrimarySeek = 55 };
std::vector<int64_t> gLog;

// The fakes' answers for the current run.
uint32_t gAccess = 0;
uint32_t gSinkRet = 0;

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

// Vtable slots of the copies. __fastcall recorders take ecx and edx, as __thiscall does, and pop their stack
// arguments as a __thiscall callee would.
uint32_t __fastcall RecordAccess(void *self, void *)
{
    Push(kAccess, Off(self), 0);
    return gAccess;
}

uint32_t __fastcall RecordSinkPos(void *self, void *)
{
    Push(kSinkPos, Off(self), 0);
    return gSinkRet;
}

uint32_t __fastcall RecordSinkSize(void *self, void *)
{
    Push(kSinkSize, Off(self), 0);
    return gSinkRet;
}

uint32_t __fastcall RecordSinkSeek(void *self, void *, uint32_t position)
{
    Push(kSinkSeek, Off(self), position);
    return gSinkRet;
}

bool __fastcall RecordSinkWrite(void *, void *, void *out, uint32_t size)
{
    Push(kSinkWrite, Off(out), size);
    return (gSinkRet & 1) != 0;
}

// Primary slot 55 replaced by a fake: records the position it is asked for and answers from gSinkRet.
bool __fastcall RecordSeekFake(void *self, void *, uint32_t position)
{
    Push(kPrimarySeek, Off(self), position);
    return (gSinkRet & 1) != 0;
}

// Vtables: the primary has 65 slots, the subobject 21. The sink has 16 slots, of which 6, 7, 12 and 14 are used.
void *gSinkTable[16];

struct Tables {
    void *primary[65];
    void *sub[21];
};

Tables MakeTables(const difftest::Context &ctx)
{
    Tables t{};
    std::memcpy(t.primary, ctx.image.At(cGZDBWriteRecordPackedFile::kVTable), sizeof(t.primary));
    std::memcpy(t.sub, ctx.image.At(cGZDBWriteRecordPackedFile::kSubVTable), sizeof(t.sub));
    t.sub[cGZDBWriteRecordPackedFile::kAccessFlagsSlot] = reinterpret_cast<void *>(&RecordAccess);
    return t;
}

// One trial's inputs: the arena's random bytes and the fields the functions read.
struct Scene {
    std::vector<uint8_t> image;
    uint32_t access = 0;
    uint32_t sinkRet = 0;
    bool hasSink = false;
    uint32_t bufferStart = 0; // address
    uint32_t bufferEnd = 0;   // address
    uint32_t position = 0;
    uint32_t arg = 0; // size, position or offset for the call
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

uint32_t AddrOf(size_t offset)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena + offset));
}

// Runs one call on a fresh copy of the scene. `call` gets the arena and returns the result.
template <typename Call> Outcome Run(const Tables &t, const Scene &s, Call call)
{
    std::memcpy(gArena, s.image.data(), kBytes);
    const void *primary = t.primary;
    const void *sub = t.sub;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &sub, sizeof(sub));
    std::memcpy(gArena + 0x4c, &s.bufferStart, sizeof(uint32_t));
    std::memcpy(gArena + 0x50, &s.bufferEnd, sizeof(uint32_t));
    std::memcpy(gArena + 0x58, &s.position, sizeof(uint32_t));
    uint32_t sink = s.hasSink ? AddrOf(kSinkObj) : 0;
    std::memcpy(gArena + 0x5c, &sink, sizeof(sink));
    const void *sinkTable = gSinkTable;
    std::memcpy(gArena + kSinkObj, &sinkTable, sizeof(sinkTable));
    gAccess = s.access;
    gSinkRet = s.sinkRet;
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

void Compare2(difftest::Context &ctx, const char *name, const Outcome &a, const Outcome &b)
{
    ++ctx.cases;
    if (a.result != b.result) {
        ctx.Fail("%s: result original %llu, ours %llu", name, static_cast<unsigned long long>(a.result),
                 static_cast<unsigned long long>(b.result));
    }
    if (a.log != b.log) ctx.Fail("%s: calls made differ (%zu / %zu values)", name, a.log.size(), b.log.size());
    if (a.bytes != b.bytes) ctx.Fail("%s: object state differs", name);
}

void InitFakes()
{
    gSinkTable[6] = reinterpret_cast<void *>(&RecordSinkPos);
    gSinkTable[7] = reinterpret_cast<void *>(&RecordSinkSize);
    gSinkTable[12] = reinterpret_cast<void *>(&RecordSinkSeek);
    gSinkTable[14] = reinterpret_cast<void *>(&RecordSinkWrite);
}

// Interesting edges of a 32-bit value: zero, one, the top bit, all ones, and random values.
uint32_t PickWord(std::mt19937 &rng)
{
    static const uint32_t edges[] = {0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff, 0xffff, 0x10000};
    if (rng() % 3 == 0) return edges[rng() % 8];
    return static_cast<uint32_t>(rng());
}

// The access flags: mostly zero or one, sometimes other nonzero values (the test only checks for zero).
uint32_t PickAccess(std::mt19937 &rng)
{
    static const uint32_t flags[] = {0, 0, 1, 2, 0x80000000, 0xffffffff};
    return flags[rng() % 6];
}

// A buffer inside the arena: start in [kBufBase, kBufBase + 0x40), size 0 to 0x80. The end is an address too.
void SetBuffer(std::mt19937 &rng, Scene &s, uint32_t &beginOffset, uint32_t &capacity)
{
    beginOffset = static_cast<uint32_t>(rng() % 0x41);
    capacity = static_cast<uint32_t>(rng() % 0x81);
    s.bufferStart = AddrOf(kBufBase + beginOffset);
    s.bufferEnd = s.bufferStart + capacity;
}

} // namespace

DIFF_TEST(cGZDBWriteRecordPackedFile_GetFieldVoid, 0x9abd70)
{
    InitFakes();
    auto orig = ctx.Original<BoolVoidU32Fn>(0x9abd70);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[cGZDBWriteRecordPackedFile::kGetFieldVoidSlot] =
        Addr(static_cast<GetFieldMember>(&cGZDBWriteRecordPackedFile::GetFieldVoid));
    std::mt19937 rng(0x0a27);
    for (int i = 0; i < 800; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.hasSink = rng() % 4 == 0;
        s.sinkRet = PickWord(rng);
        uint32_t beginOffset, capacity;
        SetBuffer(rng, s, beginOffset, capacity);
        // Positions: small (inside or just past the buffer), a wrap just below zero (reaching back through the
        // buffer's start, which the arena has room for), or any value.
        switch (rng() % 3) {
        case 0: s.position = static_cast<uint32_t>(rng() % (capacity + 4)); break;
        case 1: s.position = 0u - static_cast<uint32_t>(rng() % (beginOffset + 1)); break;
        default: s.position = PickWord(rng); break;
        }
        // Sizes: the exact remainder, one more or less, or any value. A size that passes the check is kept small
        // enough for the copy to stay in the arena (the original would copy as many bytes as asked).
        switch (rng() % 4) {
        case 0: s.arg = static_cast<uint32_t>(rng() % (capacity + 4)); break;
        case 1: s.arg = capacity - s.position + (rng() % 3) - 1; break;
        case 2: s.arg = 0; break;
        default: s.arg = PickWord(rng); break;
        }
        bool passes = (s.position + s.arg) <= capacity;
        if (passes && s.arg > 0x80) s.arg = 0;
        // The output region starts zeroed in every run, so unwritten bytes compare equal.
        uint32_t size = s.arg;
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return orig(self, self + kOut, size);
        });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return Obj(self)->GetFieldVoid(self + kOut, size);
        });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolVoidU32Fn>(self, cGZDBWriteRecordPackedFile::kGetFieldVoidSlot)(self, self + kOut, size);
        });
        Compare(ctx, "GetFieldVoid", a, b, c);
    }
}

DIFF_TEST(cGZDBWriteRecordPackedFile_GetSize, 0x9acb80)
{
    InitFakes();
    auto orig = ctx.Original<U32Fn>(0x9acb80);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[cGZDBWriteRecordPackedFile::kGetSizeSlot] = Addr(static_cast<U32Member>(&cGZDBWriteRecordPackedFile::GetSize));
    std::mt19937 rng(0x0a52);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.hasSink = rng() % 3 == 0;
        s.sinkRet = PickWord(rng);
        uint32_t beginOffset, capacity;
        SetBuffer(rng, s, beginOffset, capacity);
        s.position = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->GetSize(); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<U32Fn>(self, cGZDBWriteRecordPackedFile::kGetSizeSlot)(self);
        });
        Compare(ctx, "GetSize", a, b, c);
    }
}

DIFF_TEST(cGZDBWriteRecordPackedFile_GetPosition, 0x9ac910)
{
    InitFakes();
    auto orig = ctx.Original<U32Fn>(0x9ac910);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[cGZDBWriteRecordPackedFile::kGetPositionSlot] =
        Addr(static_cast<U32Member>(&cGZDBWriteRecordPackedFile::GetPosition));
    std::mt19937 rng(0x0a91);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.hasSink = rng() % 3 == 0;
        s.sinkRet = PickWord(rng);
        uint32_t beginOffset, capacity;
        SetBuffer(rng, s, beginOffset, capacity);
        s.position = PickWord(rng);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->GetPosition(); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<U32Fn>(self, cGZDBWriteRecordPackedFile::kGetPositionSlot)(self);
        });
        Compare(ctx, "GetPosition", a, b, c);
    }
}

DIFF_TEST(cGZDBWriteRecordPackedFile_SeekAbsolute, 0x9afac0)
{
    InitFakes();
    auto orig = ctx.Original<BoolU32Fn>(0x9afac0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[cGZDBWriteRecordPackedFile::kSeekAbsoluteSlot] =
        Addr(static_cast<BoolU32Member>(&cGZDBWriteRecordPackedFile::SeekAbsolute));
    std::mt19937 rng(0x0ab1);
    for (int i = 0; i < 600; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.hasSink = rng() % 2 == 0;
        s.sinkRet = PickWord(rng);
        uint32_t beginOffset, capacity;
        SetBuffer(rng, s, beginOffset, capacity);
        s.position = PickWord(rng);
        // A third of the trials ask for the position the sink reports, so the sink's answer is true.
        s.arg = (rng() % 3 == 0) ? s.sinkRet : PickWord(rng);
        uint32_t position = s.arg;
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, position); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->SeekAbsolute(position); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolU32Fn>(self, cGZDBWriteRecordPackedFile::kSeekAbsoluteSlot)(self, position);
        });
        Compare(ctx, "SeekAbsolute", a, b, c);
    }
}

DIFF_TEST(cGZDBWriteRecordPackedFile_SeekRelative, 0x9afb90)
{
    InitFakes();
    auto orig = ctx.Original<BoolI32Fn>(0x9afb90);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    // SeekRelative calls this's GetPosition and SeekAbsolute through the vtable, so route all three.
    routed.primary[cGZDBWriteRecordPackedFile::kSeekRelativeSlot] =
        Addr(static_cast<BoolI32Member>(&cGZDBWriteRecordPackedFile::SeekRelative));
    routed.primary[cGZDBWriteRecordPackedFile::kGetPositionSlot] =
        Addr(static_cast<U32Member>(&cGZDBWriteRecordPackedFile::GetPosition));
    routed.primary[cGZDBWriteRecordPackedFile::kSeekAbsoluteSlot] =
        Addr(static_cast<BoolU32Member>(&cGZDBWriteRecordPackedFile::SeekAbsolute));
    Tables fake = MakeTables(ctx);
    fake.primary[cGZDBWriteRecordPackedFile::kSeekAbsoluteSlot] = reinterpret_cast<void *>(&RecordSeekFake);
    std::mt19937 rng(0x0ac3);
    static const int32_t edges[] = {0, 1, -1, 2, -2, INT_MAX, INT_MIN, 0x7ffffff0, -0x7ffffff0};
    for (int i = 0; i < 800; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.hasSink = rng() % 3 == 0;
        s.sinkRet = PickWord(rng);
        uint32_t beginOffset, capacity;
        SetBuffer(rng, s, beginOffset, capacity);
        s.position = PickWord(rng);
        int32_t offset;
        switch (rng() % 3) {
        case 0: offset = edges[rng() % 9]; break;
        case 1: offset = static_cast<int32_t>(0u - s.position); break; // lands on zero, or wraps past 2^32
        default: offset = static_cast<int32_t>(rng()); break;
        }
        // The sink's answer to SeekAbsolute matches the position in a third of the trials.
        if (rng() % 3 == 0) s.sinkRet = static_cast<uint32_t>(s.position + offset);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, offset); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->SeekRelative(offset); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolI32Fn>(self, cGZDBWriteRecordPackedFile::kSeekRelativeSlot)(self, offset);
        });
        Compare(ctx, "SeekRelative", a, b, c);
        // With a fake SeekAbsolute in primary slot 55, the call must go through the vtable: both the original and
        // ours must hand the fake the same position.
        auto d = Run(fake, s, [&](uint8_t *self) -> uint64_t { return orig(self, offset); });
        auto e = Run(fake, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->SeekRelative(offset); });
        Compare2(ctx, "SeekRelative via fake SeekAbsolute", d, e);
    }
}
