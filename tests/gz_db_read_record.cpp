// Differential tests for the read records (src/gz/cGZDBReadRecord.h): cGZDBReadRecordPackedFile (0x1060 bytes, the
// file-backed record, constructor 0x9a6de0) and cGZDBReadRecordRAM (0x58 bytes, memory-backed, constructor 0x9a6e60).
// The unhooked adjustor thunks in our vtables (compiler-made, compared slot by slot against the image's in the Faces tests):
// 0x9abd10 0x9afe0d 0x9afb3b 0x9a76c6 0x9a76ce 0x9a76d6 0x9abd67 0x9afe67 0x9afb7d 0x9a76de 0x9a76e6 0x9a76ee 0x1e4a82.
//
// The object lives in one fixed arena, so pointers compare equal between runs. The record's own virtual calls go through
// copies of its vtables with fakes in the slots the function under test calls (GetAccessFlags, QueryChild, SetChild,
// SetAccessFlags on the +4 face; SetError, ReadSource, ReadBuffered and CachedRemaining on the primary; the segment's
// ReadAt and Release). Each function runs three ways on a fresh copy of the same scene: the original, ours called
// directly, and ours through our own vtable (rzface, the table the image-free program carries; the tables' other slots
// are the same fakes). Results, the calls the fakes saw and every byte of the arena must match.
#include "difftest.h"
#include "game/vtable.h"
#include "gz/cGZDBReadRecord.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"

#include <windows.h>
#include <cstdio>

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr size_t kBytes = 0x1400;
constexpr size_t kSeg = 0x1080;   // the fake segment (file) object: primary vtable at +0, the +4 face's at +4
constexpr size_t kBlock = 0x10c0; // a counted block: the count dword, then 0x60 bytes of data
constexpr size_t kKey = 0x1130;   // a resource key (16 bytes)
constexpr size_t kOut = 0x1180;   // output buffer, 0x280 bytes

alignas(16) uint8_t gArena[kBytes];

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

int64_t Off(const void *p)
{
    auto q = static_cast<const uint8_t *>(p);
    return (q >= gArena && q < gArena + kBytes) ? q - gArena : -1;
}

uint32_t AddrOf(size_t offset) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena + offset)); }

bool InArena(const void *p, size_t n)
{
    auto q = static_cast<const uint8_t *>(p);
    return q >= gArena && n <= kBytes && q + n <= gArena + kBytes;
}

enum Kind : int64_t {
    kAccess = 1, kQuery, kSetChild, kSetAccess, kSetError, kReadSource, kReadBuffered, kCached, kReadAt, kSegRelease, kSegAddRef, kFreeBlock,
    kFreeObject
};
std::vector<int64_t> gLog;

void Push(int64_t kind, int64_t a = 0, int64_t b = 0, int64_t c = 0)
{
    gLog.push_back(kind);
    gLog.push_back(a);
    gLog.push_back(b);
    gLog.push_back(c);
}

// What the fakes answer in the current run.
uint32_t gAccess = 0;
bool gQueryOk = false, gReadAtOk = false, gReadSourceOk = false, gReadBufferedOk = false;
uint32_t gCached = 0;
int32_t gPositionDelta = 0; // ReadSource's fake moves the record's position (the original's ReadBuffered reloads it)
uint8_t gSeed = 0;

void Fill(void *dst, uint32_t offset, uint32_t n)
{
    if (dst == nullptr || n > 0x2000 || !InArena(dst, n)) return;
    auto *out = static_cast<uint8_t *>(dst);
    for (uint32_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>((offset + i) * 7 + gSeed);
}

// ---- Fakes. __fastcall takes ecx and edx, as __thiscall does, and pops its stack arguments as a __thiscall callee would. ----

uint32_t __fastcall FakeAccess(void *self, void *)
{
    Push(kAccess, Off(self));
    return gAccess;
}

bool __fastcall FakeQuery(void *self, void *, uint32_t id, void **out)
{
    Push(kQuery, Off(self), id);
    if (gQueryOk) *out = gArena + kSeg + 4;
    return gQueryOk;
}

void __fastcall FakeSetChild(void *self, void *, void *child)
{
    Push(kSetChild, Off(self), Off(child));
}

void __fastcall FakeSetAccess(void *self, void *, uint32_t value)
{
    Push(kSetAccess, Off(self), value);
}

void __fastcall FakeSetError(void *self, void *, uint32_t value)
{
    Push(kSetError, Off(self), value);
    *(static_cast<uint8_t *>(self) + 0x48) = static_cast<uint8_t>(value);
}

bool __fastcall FakeReadSource(void *self, void *, void *dst, uint32_t offset, uint32_t n)
{
    Push(kReadSource, Off(self), Off(dst), (static_cast<int64_t>(offset) << 32) | n);
    if (gReadSourceOk) Fill(dst, offset, n);
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 0x50) += static_cast<uint32_t>(gPositionDelta);
    return gReadSourceOk;
}

bool __fastcall FakeReadBuffered(void *self, void *, void *dst, uint32_t n)
{
    Push(kReadBuffered, Off(self), Off(dst), n);
    if (gReadBufferedOk) Fill(dst, 0x100, n);
    return gReadBufferedOk;
}

uint32_t __fastcall FakeCached(void *self, void *)
{
    Push(kCached, Off(self));
    return gCached;
}

bool __fastcall FakeReadAt(void *self, void *, void *dst, uint32_t offset, uint32_t n)
{
    Push(kReadAt, Off(self), Off(dst), (static_cast<int64_t>(offset) << 32) | n);
    if (gReadAtOk) Fill(dst, offset, n);
    return gReadAtOk;
}

uint32_t __fastcall FakeSegRelease(void *self, void *)
{
    Push(kSegRelease, Off(self));
    return 1;
}

uint32_t __fastcall FakeSegAddRef(void *self, void *)
{
    Push(kSegAddRef, Off(self));
    return 1;
}

// Frees: the block's (GeneralAllocator::Free, 0x1c6c5a) and the object's (RZMemory::Free, 0x1be3a1) do nothing but log.
void __fastcall FakeAllocatorFree(void *, void *, void *p) { Push(kFreeBlock, Off(p)); }
void __cdecl FakeRZFree(void *p) { Push(kFreeObject, Off(p)); }

struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(difftest::Context &ctx, uint32_t rva, void *target) : Redirect(ctx.image.At<uint8_t>(rva), target) {}
    Redirect(void *code, void *target) : site(static_cast<uint8_t *>(code))
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

// The frees redirected on both sides while alive.
struct FreeFakes {
    Redirect origBlock, origObject, ourBlock, ourObject;
    explicit FreeFakes(difftest::Context &ctx)
        : origBlock(ctx, 0x1c6c5a, reinterpret_cast<void *>(&FakeAllocatorFree)), origObject(ctx, 0x1be3a1, reinterpret_cast<void *>(&FakeRZFree)),
          ourBlock(Addr(&GeneralAllocator::Free), reinterpret_cast<void *>(&FakeAllocatorFree)),
          ourObject(reinterpret_cast<void *>(&RZMemory::Free), reinterpret_cast<void *>(&FakeRZFree))
    {
    }
};

// ---- The two variants ----

template <bool Ram> struct V;
template <> struct V<false> {
    using Rec = cGZDBReadRecordPackedFile;
    static constexpr int kPrimSlots = 67;
    static constexpr size_t kSize = 0x1060;
};
template <> struct V<true> {
    using Rec = cGZDBReadRecordRAM;
    static constexpr int kPrimSlots = 64;
    static constexpr size_t kSize = 0x58;
};

// Fakes to install: bits.
enum Fake : unsigned {
    kFAccess = 1, kFQuery = 2, kFSetChild = 4, kFSetAccess = 8, kFSetError = 16, kFReadSource = 32, kFReadBuffered = 64, kFCached = 128
};

struct Tables {
    void *prim[67];
    void *rec[21];
};

void *gSegPrim[64];
void *gSegSub[8];

template <bool Ram> Tables MakeTables(difftest::Context &ctx, bool own, unsigned fakes)
{
    using R = typename V<Ram>::Rec;
    Tables t{};
    void *const *prim = own ? static_cast<void *const *>(const_cast<void *>(R::OwnVTable())) : ctx.image.At<void *>(R::kVTable);
    void *const *rec = own ? static_cast<void *const *>(const_cast<void *>(R::OwnRecordVTable())) : ctx.image.At<void *>(R::kRecordVTable);
    std::memcpy(t.prim, prim, sizeof(void *) * V<Ram>::kPrimSlots);
    std::memcpy(t.rec, rec, sizeof(t.rec));
    if (fakes & kFAccess) t.rec[5] = reinterpret_cast<void *>(&FakeAccess);
    if (fakes & kFQuery) t.rec[15] = reinterpret_cast<void *>(&FakeQuery);
    if (fakes & kFSetChild) t.rec[16] = reinterpret_cast<void *>(&FakeSetChild);
    if (fakes & kFSetAccess) t.rec[20] = reinterpret_cast<void *>(&FakeSetAccess);
    if (fakes & kFSetError) t.prim[63] = reinterpret_cast<void *>(&FakeSetError);
    if (!Ram) {
        if (fakes & kFReadSource) t.prim[64] = reinterpret_cast<void *>(&FakeReadSource);
        if (fakes & kFReadBuffered) t.prim[65] = reinterpret_cast<void *>(&FakeReadBuffered);
        if (fakes & kFCached) t.prim[66] = reinterpret_cast<void *>(&FakeCached);
    }
    return t;
}

void InitFakes()
{
    gSegPrim[56] = reinterpret_cast<void *>(&FakeReadAt);
    gSegSub[1] = reinterpret_cast<void *>(&FakeSegAddRef);
    gSegSub[2] = reinterpret_cast<void *>(&FakeSegRelease);
}

// One trial's inputs.
struct Scene {
    std::vector<uint8_t> image;
    uint32_t kind = 1;
    uint8_t error = 0;
    uint32_t data = 0;
    uint32_t position = 0;
    uint32_t size = 0;
    uint32_t cacheStart = 0xffffffffu;
    uint32_t cacheLength = 0;
    uint32_t refCount = 1; // the block's count (RAM)
    uint32_t access = 0;
    bool queryOk = false, readAtOk = false, readSourceOk = false, readBufferedOk = false;
    uint32_t cached = 0;
    int32_t positionDelta = 0;
    uint8_t seed = 0;
};

Scene MakeScene(std::mt19937 &rng)
{
    Scene s;
    s.image.resize(kBytes);
    for (auto &b : s.image) b = static_cast<uint8_t>(rng());
    s.seed = static_cast<uint8_t>(rng());
    return s;
}

uint32_t PickAccess(std::mt19937 &rng)
{
    static const uint32_t flags[] = {0, 0, 1, 1, 2, 3, 0x80000000, 0xffffffff};
    return flags[rng() % 8];
}

struct Outcome {
    uint64_t result = 0;
    std::vector<int64_t> log;
    std::vector<uint8_t> bytes;
};

// Arranges the arena: the scene's bytes, the tables' pointers, the record's fields, the fake segment and the block.
template <bool Ram> void Arrange(const Tables &t, const Scene &s)
{
    std::memcpy(gArena, s.image.data(), kBytes);
    const void *primary = t.prim;
    const void *rec = t.rec;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &rec, sizeof(rec));
    std::memcpy(gArena + 0x44, &s.kind, 4);
    gArena[0x48] = s.error;
    std::memcpy(gArena + 0x4c, &s.data, 4);
    std::memcpy(gArena + 0x50, &s.position, 4);
    std::memcpy(gArena + 0x54, &s.size, 4);
    if (!Ram) {
        std::memcpy(gArena + 0x58, &s.cacheStart, 4);
        std::memcpy(gArena + 0x5c, &s.cacheLength, 4);
    }
    const void *segPrim = gSegPrim;
    const void *segSub = gSegSub;
    std::memcpy(gArena + kSeg, &segPrim, 4);
    std::memcpy(gArena + kSeg + 4, &segSub, 4);
    std::memcpy(gArena + kBlock, &s.refCount, 4);
    gAccess = s.access;
    gQueryOk = s.queryOk;
    gReadAtOk = s.readAtOk;
    gReadSourceOk = s.readSourceOk;
    gReadBufferedOk = s.readBufferedOk;
    gCached = s.cached;
    gPositionDelta = s.positionDelta;
    gSeed = s.seed;
    gLog.clear();
}

Outcome Collect(uint64_t result, bool keepVTables)
{
    Outcome out;
    out.result = result;
    out.log = gLog;
    if (!keepVTables) std::memset(gArena, 0, 8); // the plain and routed tables' addresses differ
    out.bytes.assign(gArena, gArena + kBytes);
    return out;
}

template <bool Ram, typename Call> Outcome Run(const Tables &t, const Scene &s, Call call)
{
    Arrange<Ram>(t, s);
    uint64_t result = call(gArena);
    return Collect(result, false);
}

void Compare(difftest::Context &ctx, const char *name, const Outcome &a, const Outcome &b, const Outcome &c)
{
    ++ctx.cases;
    if (a.result != b.result || a.result != c.result)
        ctx.Fail("%s: result original %llu, ours %llu, via slot %llu", name, static_cast<unsigned long long>(a.result),
                 static_cast<unsigned long long>(b.result), static_cast<unsigned long long>(c.result));
    if (a.log != b.log || a.log != c.log) ctx.Fail("%s: calls made differ (%zu / %zu / %zu values)", name, a.log.size(), b.log.size(), c.log.size());
    if (a.bytes != b.bytes || a.bytes != c.bytes) ctx.Fail("%s: object state differs", name);
}

void Compare2(difftest::Context &ctx, const char *name, const Outcome &a, const Outcome &b)
{
    ++ctx.cases;
    if (a.result != b.result)
        ctx.Fail("%s: result original %llu, ours %llu", name, static_cast<unsigned long long>(a.result), static_cast<unsigned long long>(b.result));
    if (a.log != b.log) ctx.Fail("%s: calls made differ (%zu / %zu values)", name, a.log.size(), b.log.size());
    if (a.bytes != b.bytes) ctx.Fail("%s: object state differs", name);
}

uint32_t PickWord(std::mt19937 &rng)
{
    static const uint32_t edges[] = {0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff, 0xffff, 0x10000};
    if (rng() % 3 == 0) return edges[rng() % 8];
    return static_cast<uint32_t>(rng());
}

// A value near `v`.
uint32_t Near(std::mt19937 &rng, uint32_t v)
{
    return v + static_cast<uint32_t>(static_cast<int32_t>(rng() % 5) - 2);
}

template <bool Ram> typename V<Ram>::Rec *Obj(void *p) { return static_cast<typename V<Ram>::Rec *>(p); }

using BoolVoidU32Fn = bool(__thiscall *)(void *, void *, uint32_t);
using BoolU32Fn = bool(__thiscall *)(void *, uint32_t);
using BoolI32Fn = bool(__thiscall *)(void *, int32_t);
using VoidFn = void(__thiscall *)(void *);
using U32Fn = uint32_t(__thiscall *)(void *);
using BoolVoidU32U32Fn = bool(__thiscall *)(void *, void *, uint32_t, uint32_t);

// ---- GetFieldVoid ----

template <bool Ram> void GetFieldVoidTest(difftest::Context &ctx, uint32_t rva, const char *name)
{
    using R = typename V<Ram>::Rec;
    InitFakes();
    auto orig = ctx.Original<BoolVoidU32Fn>(rva);
    unsigned fakes = kFAccess | kFSetError | kFReadSource | kFReadBuffered | kFCached;
    Tables plain = MakeTables<Ram>(ctx, false, fakes);
    Tables routed = MakeTables<Ram>(ctx, true, fakes);
    std::mt19937 rng(rva);
    int closed = 0, outOfRange = 0, buffered = 0, direct = 0, copied = 0;
    for (int i = 0; i < 1500; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.readSourceOk = rng() % 5 != 0;
        s.readBufferedOk = rng() % 5 != 0;
        s.cached = rng() % 3 == 0 ? 0 : static_cast<uint32_t>(rng() % 0x1400);
        uint32_t n;
        if (Ram) {
            s.size = static_cast<uint32_t>(rng() % 0x60);
            s.data = AddrOf(kBlock);
            s.position = rng() % 4 == 0 ? Near(rng, s.size) : static_cast<uint32_t>(rng() % 0x70);
            if (rng() % 16 == 0) s.position = 0xfffffff0u;
            n = rng() % 3 == 0 ? Near(rng, s.size - s.position) : static_cast<uint32_t>(rng() % 0x70);
            // A size that passes the signed check must stay inside the arena (the original copies as many bytes as asked).
            if (static_cast<int32_t>(s.position + n) <= static_cast<int32_t>(s.size) && n > 0x80) n = 0;
        } else {
            s.size = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 0x6000);
            s.data = static_cast<uint32_t>(rng() % 0x100000);
            s.position = rng() % 4 == 0 ? Near(rng, s.size) : static_cast<uint32_t>(rng() % (static_cast<uint64_t>(s.size) + 1));
            switch (rng() % 4) {
            case 0: n = Near(rng, s.size - s.position); break;
            case 1: n = static_cast<uint32_t>(rng() % 0x80); break;
            case 2: n = s.cached + 0x1000 + (static_cast<uint32_t>(rng() % 3) - 1); break;
            default: n = static_cast<uint32_t>(rng() % 0x3000); break;
            }
            if (n > 0x3000 && s.position + n <= s.size) n = 0x3000;
        }
        auto a = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kOut, n) & 0xff; });
        auto b = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { return Obj<Ram>(self)->GetFieldVoid(self + kOut, n) & 0xff; });
        auto c = Run<Ram>(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolVoidU32Fn>(self, 27)(self, self + kOut, n) & 0xff; });
        Compare(ctx, name, a, b, c);
        closed += s.access == 0;
        if (!Ram) {
            bool inRange = s.position + n <= s.size;
            outOfRange += s.access != 0 && !inRange;
            buffered += s.access != 0 && inRange && n <= s.cached + 0x1000;
            direct += s.access != 0 && inRange && n > s.cached + 0x1000;
        } else {
            copied += s.access != 0 && a.result != 0;
            outOfRange += s.access != 0 && a.result == 0;
        }
    }
    if (closed < 100 || outOfRange < 100 || (!Ram && (buffered < 100 || direct < 50)) || (Ram && copied < 100))
        ctx.Fail("%s: setup reaches too little (%d closed, %d out of range, %d buffered, %d direct, %d copied)", name, closed, outOfRange, buffered, direct, copied);
}

// ---- SetFieldVoid ----

template <bool Ram> void SetFieldVoidTest(difftest::Context &ctx, uint32_t rva, const char *name)
{
    InitFakes();
    auto orig = ctx.Original<BoolVoidU32Fn>(rva);
    Tables plain = MakeTables<Ram>(ctx, false, kFAccess);
    Tables routed = MakeTables<Ram>(ctx, true, kFAccess);
    std::mt19937 rng(rva);
    int closed = 0, written = 0, atEnd = 0, past = 0;
    for (int i = 0; i < 1500; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.size = static_cast<uint32_t>(rng() % 0x60);
        s.data = AddrOf(kBlock);
        s.position = rng() % 4 == 0 ? Near(rng, s.size) : static_cast<uint32_t>(rng() % 0x60);
        uint32_t n = rng() % 3 == 0 ? Near(rng, s.size - s.position) : static_cast<uint32_t>(rng() % 0x40);
        if (static_cast<int32_t>(s.position + n) < static_cast<int32_t>(s.size) && n > 0x40) n = 0;
        // The bytes written come from the arena's output area.
        auto a = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kOut, n) & 0xff; });
        auto b = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { return Obj<Ram>(self)->SetFieldVoid(self + kOut, n) & 0xff; });
        auto c = Run<Ram>(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolVoidU32Fn>(self, 51)(self, self + kOut, n) & 0xff; });
        Compare(ctx, name, a, b, c);
        closed += s.access == 0;
        written += a.result != 0;
        int32_t end = static_cast<int32_t>(s.position + n);
        atEnd += s.access != 0 && end == static_cast<int32_t>(s.size);
        past += s.access != 0 && end > static_cast<int32_t>(s.size);
    }
    if (Ram && (closed < 100 || written < 100 || atEnd < 20 || past < 100))
        ctx.Fail("%s: setup reaches too little (%d closed, %d written, %d at the end, %d past)", name, closed, written, atEnd, past);
    if (!Ram && closed < 100) ctx.Fail("%s: setup reaches too little (%d closed)", name, closed);
}

// ---- Seek (slot 55) and SeekRelative (slot 56) ----

template <bool Ram> void SeekTest(difftest::Context &ctx, uint32_t rva, const char *name)
{
    InitFakes();
    auto orig = ctx.Original<BoolU32Fn>(rva);
    Tables plain = MakeTables<Ram>(ctx, false, kFAccess);
    Tables routed = MakeTables<Ram>(ctx, true, kFAccess);
    std::mt19937 rng(rva + Ram);
    int closed = 0, moved = 0, atSize = 0;
    for (int i = 0; i < 1500; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.size = PickWord(rng);
        s.position = PickWord(rng);
        uint32_t target = rng() % 3 == 0 ? Near(rng, s.size) : PickWord(rng);
        auto a = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, target) & 0xff; });
        auto b = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { return Obj<Ram>(self)->Seek(target) & 0xff; });
        auto c = Run<Ram>(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolU32Fn>(self, 55)(self, target) & 0xff; });
        Compare(ctx, name, a, b, c);
        closed += s.access == 0;
        moved += a.result != 0;
        atSize += s.access != 0 && target == s.size;
    }
    if (closed < 100 || moved < 100 || atSize < 30) ctx.Fail("%s: setup reaches too little (%d closed, %d moved, %d at the size)", name, closed, moved, atSize);
}

template <bool Ram> void SeekRelativeTest(difftest::Context &ctx, uint32_t rva, const char *name)
{
    InitFakes();
    auto orig = ctx.Original<BoolI32Fn>(rva);
    Tables plain = MakeTables<Ram>(ctx, false, kFAccess);
    Tables routed = MakeTables<Ram>(ctx, true, kFAccess);
    std::mt19937 rng(rva);
    int closed = 0, moved = 0, negative = 0, atSize = 0, wrapped = 0;
    for (int i = 0; i < 2000; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.size = rng() % 2 ? static_cast<uint32_t>(rng() % 0x100) : PickWord(rng);
        s.position = rng() % 2 ? static_cast<uint32_t>(rng() % (static_cast<uint64_t>(s.size) + 1)) : PickWord(rng);
        int32_t offset;
        switch (rng() % 4) {
        case 0: offset = static_cast<int32_t>(Near(rng, s.size - s.position)); break;
        case 1: offset = static_cast<int32_t>(0u - s.position + (rng() % 3) - 1); break;
        case 2: offset = static_cast<int32_t>(rng() % 0x80) - 0x40; break;
        default: offset = static_cast<int32_t>(PickWord(rng)); break;
        }
        auto a = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, offset) & 0xff; });
        auto b = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { return Obj<Ram>(self)->SeekRelative(offset) & 0xff; });
        auto c = Run<Ram>(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolI32Fn>(self, 56)(self, offset) & 0xff; });
        Compare(ctx, name, a, b, c);
        uint32_t target = s.position + static_cast<uint32_t>(offset);
        closed += s.access == 0;
        moved += a.result != 0;
        negative += s.access != 0 && static_cast<int32_t>(target) < 0;
        atSize += s.access != 0 && target == s.size;
        wrapped += s.access != 0 && static_cast<int32_t>(target) < 0 && target >= s.size && s.size > 0x80000000u;
    }
    if (closed < 100 || moved < 100 || negative < 100 || atSize < 5)
        ctx.Fail("%s: setup reaches too little (%d closed, %d moved, %d negative, %d at the size, %d wrapped)", name, closed, moved, negative, atSize, wrapped);
}

// ---- Close (slot 62) ----

template <bool Ram> void CloseTest(difftest::Context &ctx, uint32_t rva, const char *name)
{
    InitFakes();
    FreeFakes frees(ctx);
    auto orig = ctx.Original<VoidFn>(rva);
    unsigned fakes = kFSetChild | kFSetAccess;
    Tables plain = MakeTables<Ram>(ctx, false, fakes);
    Tables routed = MakeTables<Ram>(ctx, true, fakes);
    std::mt19937 rng(rva);
    int released = 0, shared = 0, noBlock = 0;
    for (int i = 0; i < 800; ++i) {
        Scene s = MakeScene(rng);
        s.position = PickWord(rng);
        s.size = PickWord(rng);
        s.data = rng() % 4 == 0 ? 0 : AddrOf(kBlock);
        static const uint32_t counts[] = {1, 1, 2, 3, 0, 0xffffffffu};
        s.refCount = counts[rng() % 6];
        auto a = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { orig(self); return 0; });
        auto b = Run<Ram>(plain, s, [&](uint8_t *self) -> uint64_t { Obj<Ram>(self)->Close(); return 0; });
        auto c = Run<Ram>(routed, s, [&](uint8_t *self) -> uint64_t { VirtualSlot<VoidFn>(self, 62)(self); return 0; });
        Compare(ctx, name, a, b, c);
        if (Ram) {
            released += s.data != 0 && s.refCount == 1;
            shared += s.data != 0 && s.refCount != 1;
            noBlock += s.data == 0;
        }
    }
    if (Ram && (released < 100 || shared < 100 || noBlock < 100))
        ctx.Fail("%s: setup reaches too little (%d released, %d still shared, %d without a block)", name, released, shared, noBlock);
}

// ---- ReadSource (slot 64) ----

void ReadSourceTest(difftest::Context &ctx)
{
    InitFakes();
    auto orig = ctx.Original<BoolVoidU32U32Fn>(0x9af120);
    unsigned fakes = kFQuery | kFSetError;
    Tables plain = MakeTables<false>(ctx, false, fakes);
    Tables routed = MakeTables<false>(ctx, true, fakes);
    std::mt19937 rng(0x9af120);
    int noQuery = 0, readFailed = 0, ok = 0;
    for (int i = 0; i < 600; ++i) {
        Scene s = MakeScene(rng);
        s.queryOk = rng() % 4 != 0;
        s.readAtOk = rng() % 4 != 0;
        uint32_t offset = PickWord(rng), n = static_cast<uint32_t>(rng() % 0x200);
        auto a = Run<false>(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kOut, offset, n) & 0xff; });
        auto b = Run<false>(plain, s, [&](uint8_t *self) -> uint64_t { return Obj<false>(self)->ReadSource(self + kOut, offset, n) & 0xff; });
        auto c = Run<false>(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolVoidU32U32Fn>(self, 64)(self, self + kOut, offset, n) & 0xff; });
        Compare(ctx, "ReadSource", a, b, c);
        noQuery += !s.queryOk;
        readFailed += s.queryOk && !s.readAtOk;
        ok += a.result != 0;
    }
    if (noQuery < 100 || readFailed < 100 || ok < 100) ctx.Fail("ReadSource: setup reaches too little (%d refused, %d failed reads, %d ok)", noQuery, readFailed, ok);
}

// ---- ReadBuffered (slot 65) and CachedRemaining (slot 66) ----

void ReadBufferedTest(difftest::Context &ctx)
{
    InitFakes();
    auto orig = ctx.Original<BoolVoidU32Fn>(0x9ae500);
    unsigned fakes = kFSetError | kFReadSource;
    Tables plain = MakeTables<false>(ctx, false, fakes);
    Tables routed = MakeTables<false>(ctx, true, fakes);
    std::mt19937 rng(0x9ae500);
    int hits = 0, refills = 0, failed = 0, multi = 0, shifted = 0;
    for (int i = 0; i < 1200; ++i) {
        Scene s = MakeScene(rng);
        s.size = static_cast<uint32_t>(rng() % 0x5000);
        s.data = static_cast<uint32_t>(rng() % 0x100000);
        s.position = static_cast<uint32_t>(rng() % (static_cast<uint64_t>(s.size) + 1));
        // Cache: empty, a window around the position, or somewhere else.
        switch (rng() % 3) {
        case 0: s.cacheStart = 0xffffffffu; s.cacheLength = 0; break;
        case 1:
        {
            uint32_t d = std::min<uint32_t>(static_cast<uint32_t>(rng() % 0x1000), s.position);
            s.cacheStart = s.position - d;
            s.cacheLength = std::min<uint32_t>(0x1000, s.size - s.cacheStart); // a cache that reaches the end of the record or is full (else the original loops)
            break;
        }
        default:
            s.cacheStart = static_cast<uint32_t>(rng() % 0x6000);
            s.cacheLength = 0x1000;
            break;
        }
        s.readSourceOk = rng() % 8 != 0;
        s.positionDelta = rng() % 6 == 0 ? static_cast<int32_t>(rng() % 3) : 0;
        uint32_t remaining = s.size - s.position;
        uint32_t n;
        if (remaining < 10) n = 0;
        else if (rng() % 3 == 0) n = static_cast<uint32_t>(rng() % (std::min<uint32_t>(remaining - 8, 0x240) + 1));
        else n = static_cast<uint32_t>(rng() % (std::min<uint32_t>(remaining - 8, 0x40) + 1));
        auto a = Run<false>(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kOut, n) & 0xff; });
        auto b = Run<false>(plain, s, [&](uint8_t *self) -> uint64_t { return Obj<false>(self)->ReadBuffered(self + kOut, n) & 0xff; });
        auto c = Run<false>(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolVoidU32Fn>(self, 65)(self, self + kOut, n) & 0xff; });
        Compare(ctx, "ReadBuffered", a, b, c);
        size_t sources = 0;
        for (size_t k = 0; k + 3 < a.log.size(); k += 4) sources += a.log[k] == kReadSource;
        refills += sources > 0;
        failed += a.result == 0;
        shifted += s.positionDelta != 0 && sources > 0;
        hits += n > 0 && sources == 0;
        multi += sources > 1;
    }
    if (hits < 50 || refills < 200 || failed < 30 || shifted < 20)
        ctx.Fail("ReadBuffered: setup reaches too little (%d from the cache, %d refills, %d failed, %d shifted, %d multi)", hits, refills, failed, shifted, multi);
}

void CachedRemainingTest(difftest::Context &ctx)
{
    InitFakes();
    auto orig = ctx.Original<U32Fn>(0x9a8050);
    Tables plain = MakeTables<false>(ctx, false, 0);
    Tables routed = MakeTables<false>(ctx, true, 0);
    std::mt19937 rng(0x9a8050);
    int empty = 0, inside = 0, outside = 0;
    for (int i = 0; i < 1500; ++i) {
        Scene s = MakeScene(rng);
        s.position = PickWord(rng);
        s.cacheLength = rng() % 5 == 0 ? 0 : static_cast<uint32_t>(rng() % 0x1001);
        switch (rng() % 3) {
        case 0: s.cacheStart = s.position - static_cast<uint32_t>(rng() % (s.cacheLength + 2)) + 1; break;
        case 1: s.cacheStart = Near(rng, s.position); break;
        default: s.cacheStart = PickWord(rng); break;
        }
        auto a = Run<false>(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = Run<false>(plain, s, [&](uint8_t *self) -> uint64_t { return Obj<false>(self)->CachedRemaining(); });
        auto c = Run<false>(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<U32Fn>(self, 66)(self); });
        Compare(ctx, "CachedRemaining", a, b, c);
        empty += s.cacheLength == 0;
        inside += a.result != 0;
        outside += s.cacheLength != 0 && a.result == 0;
    }
    if (empty < 100 || inside < 200 || outside < 200) ctx.Fail("CachedRemaining: setup reaches too little (%d empty, %d inside, %d outside)", empty, inside, outside);
}

// ---- Constructors and destructors ----

using FileCtorFn = void *(__thiscall *)(void *, uint32_t, uint32_t, const void *, void *, uint32_t);
using RamCtorFn = void *(__thiscall *)(void *, void *, uint32_t, const void *, void *, uint32_t);
using DtorFn = void *(__thiscall *)(void *, uint32_t);

template <bool Ram> void ConstructTest(difftest::Context &ctx, uint32_t rva, const char *name)
{
    using R = typename V<Ram>::Rec;
    InitFakes();
    Tables plain = MakeTables<Ram>(ctx, false, 0);
    auto orig = ctx.Original<FileCtorFn>(rva);
    std::mt19937 rng(rva);
    int withFile = 0, withoutFile = 0;
    for (int i = 0; i < 600; ++i) {
        Scene s = MakeScene(rng);
        s.refCount = static_cast<uint32_t>(rng() % 4);
        uint32_t data = Ram ? AddrOf(kBlock) : PickWord(rng);
        uint32_t size = PickWord(rng);
        uint32_t flag = rng() % 3 == 0 ? 0 : static_cast<uint32_t>(rng());
        bool hasFile = rng() % 4 != 0;
        uint8_t key[16];
        for (auto &b : key) b = static_cast<uint8_t>(rng());
        auto call = [&](bool ours) {
            return [&, ours](uint8_t *self) -> uint64_t {
                std::memcpy(self + kKey, key, 16);
                void *file = hasFile ? self + kSeg : nullptr;
                const void *k = self + kKey;
                void *r;
                if (ours) {
                    if constexpr (Ram)
                        r = static_cast<R *>(static_cast<void *>(self))->Construct(reinterpret_cast<void *>(static_cast<uintptr_t>(data)), size,
                                                                                    static_cast<const cGZPersistResourceKey *>(k), file, flag);
                    else
                        r = static_cast<R *>(static_cast<void *>(self))->Construct(data, size, static_cast<const cGZPersistResourceKey *>(k), file, flag);
                } else {
                    r = orig(self, data, size, k, file, flag);
                }
                return static_cast<uint64_t>(Off(r));
            };
        };
        Arrange<Ram>(plain, s);
        std::memcpy(gArena + kKey, key, 16);
        // The tables are the objects' own (nothing routes): the vtable words are the image's on both sides.
        auto run = [&](bool ours) {
            Arrange<Ram>(plain, s);
            // Nothing of the scene's arena is the record's yet: the constructor sets everything it reads.
            uint64_t result = call(ours)(gArena);
            return Collect(result, true);
        };
        Outcome a = run(false);
        Outcome b = run(true);
        Compare2(ctx, name, a, b);
        withFile += hasFile;
        withoutFile += !hasFile;
    }
    if (withFile < 100 || withoutFile < 100) ctx.Fail("%s: setup reaches too little (%d with a file, %d without)", name, withFile, withoutFile);
}

template <bool Ram> void DestructTest(difftest::Context &ctx, uint32_t rva, const char *name)
{
    using R = typename V<Ram>::Rec;
    InitFakes();
    FreeFakes frees(ctx);
    // The record is built by the original constructor, whose tables are the image's: the destructor's child release is
    // the fake segment's.
    Tables plain = MakeTables<Ram>(ctx, false, 0);
    auto ctor = ctx.Original<FileCtorFn>(Ram ? 0x9a6e60 : 0x9a6de0);
    auto orig = ctx.Original<DtorFn>(rva);
    Tables routed = MakeTables<Ram>(ctx, true, 0);
    std::mt19937 rng(rva);
    int freed = 0, unsized = 0, kept = 0, blockFreed = 0;
    for (int i = 0; i < 800; ++i) {
        Scene s = MakeScene(rng);
        static const uint32_t counts[] = {1, 1, 2, 3, 0};
        s.refCount = counts[rng() % 5];
        static const uint32_t flagValues[] = {0, 1, 4, 5, 2, 3, 6, 7, 0x10, 0x11};
        uint32_t flags = flagValues[rng() % 10];
        bool nullData = Ram && rng() % 5 == 0;
        auto build = [&](uint8_t *self) {
            void *file = self + kSeg;
            uint8_t key[16];
            std::memcpy(key, self + kKey, 16);
            ctor(self, Ram ? AddrOf(kBlock) : 0x1234u, 0x40u, self + kKey, file, 0u);
            if (Ram && nullData) std::memset(self + 0x4c, 0, 4);
            gLog.clear();
        };
        auto run = [&](int mode) {
            Arrange<Ram>(plain, s);
            build(gArena);
            uint64_t result;
            if (mode == 0) result = reinterpret_cast<uintptr_t>(orig(gArena, flags));
            else if (mode == 1) result = reinterpret_cast<uintptr_t>(static_cast<R *>(static_cast<void *>(gArena))->DeletingDestructor(flags));
            else result = reinterpret_cast<uintptr_t>(VirtualSlot<DtorFn>(gArena + 4, 19)(gArena + 4, flags));
            Outcome out = Collect(result, true);
            if (mode == 2) out.result -= 4; // the +4 face's adjustor returns the primary
            return out;
        };
        Outcome a = run(0);
        Outcome b = run(1);
        (void)routed;
        Compare2(ctx, name, a, b);
        // Through the +4 face's slot 19 of our own table: the image's constructor wrote the image's tables into the object,
        // so call our table's entry directly on the +4 face.
        {
            Arrange<Ram>(plain, s);
            build(gArena);
            auto *table = static_cast<void *const *>(const_cast<void *>(R::OwnRecordVTable()));
            auto thunk = reinterpret_cast<DtorFn>(table[19]);
            uint64_t result = reinterpret_cast<uintptr_t>(thunk(gArena + 4, flags));
            Outcome c = Collect(result, true);
            Compare2(ctx, name, a, c);
        }
        freed += (flags & 1) && !(flags & 4);
        unsized += (flags & 1) && (flags & 4);
        kept += !(flags & 1);
        if (Ram) blockFreed += !nullData && s.refCount <= 1;
    }
    if (freed < 100 || unsized < 50 || kept < 100 || (Ram && blockFreed < 100))
        ctx.Fail("%s: setup reaches too little (%d freed, %d with the sized delete, %d kept, %d blocks freed)", name, freed, unsized, kept, blockFreed);
}

// ---- The faces: the slots that are not hooked members, ours against the image's, on the same objects ----

using PtrFn = void *(__thiscall *)(void *);
using U8Fn = uint8_t(__thiscall *)(void *);
using BoolFn = bool(__thiscall *)(void *);
using VoidU8Fn = void(__thiscall *)(void *, uint8_t);
using BoolCharU32Fn = bool(__thiscall *)(void *, char *, uint32_t);

template <bool Ram> void FacesTest(difftest::Context &ctx)
{
    using R = typename V<Ram>::Rec;
    InitFakes();
    const char *who = Ram ? "cGZDBReadRecordRAM faces" : "cGZDBReadRecordPackedFile faces";
    Tables imageT = MakeTables<Ram>(ctx, false, kFAccess | kFReadSource | kFReadBuffered | kFCached);
    Tables ours = MakeTables<Ram>(ctx, true, kFAccess | kFReadSource | kFReadBuffered | kFCached);
    // The ostream and istream faces are not in Tables: take the image's and ours as they are.
    void *const *imageO = ctx.image.At<void *>(Ram ? 0xe5f8c0 : 0xe5f69c);
    void *const *imageI = ctx.image.At<void *>(Ram ? 0xe5f924 : 0xe5f700);
    void *const *ourO = static_cast<void *const *>(const_cast<void *>(R::OwnOStreamVTable()));
    void *const *ourI = static_cast<void *const *>(const_cast<void *>(R::OwnIStreamVTable()));
    std::mt19937 rng(Ram ? 0xface1 : 0xface0);
    int checked = 0;
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        s.access = PickAccess(rng);
        s.size = static_cast<uint32_t>(rng() % 0x60);
        s.position = static_cast<uint32_t>(rng() % 0x60);
        s.data = Ram ? AddrOf(kBlock) : AddrOf(kBlock);
        uint32_t arg = PickWord(rng);
        uint8_t value = static_cast<uint8_t>(rng());
        auto both = [&](const char *name, auto call) {
            Outcome a = Run<Ram>(imageT, s, [&](uint8_t *self) -> uint64_t { return call(self, imageT.prim, imageT.rec, imageO, imageI); });
            Outcome b = Run<Ram>(ours, s, [&](uint8_t *self) -> uint64_t { return call(self, ours.prim, ours.rec, ourO, ourI); });
            Compare2(ctx, (std::string(who) + " " + name).c_str(), a, b);
            ++checked;
        };
        using T = void *const *;
        using P = void **;
        both("primary 3", [&](uint8_t *self, P p, P, T, T) -> uint64_t { return reinterpret_cast<uintptr_t>(reinterpret_cast<PtrFn>(p[3])(self)); });
        both("primary 52", [&](uint8_t *self, P p, P, T, T) -> uint64_t { return reinterpret_cast<U32Fn>(p[52])(self); });
        both("primary 53", [&](uint8_t *self, P p, P, T, T) -> uint64_t { return reinterpret_cast<BoolU32Fn>(p[53])(self, arg) & 0xff; });
        both("primary 54", [&](uint8_t *self, P p, P, T, T) -> uint64_t { return reinterpret_cast<U32Fn>(p[54])(self); });
        both("primary 63", [&](uint8_t *self, P p, P, T, T) -> uint64_t { reinterpret_cast<VoidU8Fn>(p[63])(self, value); return 0; });
        both("record 3", [&](uint8_t *self, P, P r, T, T) -> uint64_t { return reinterpret_cast<uintptr_t>(reinterpret_cast<PtrFn>(r[3])(self + 4)); });
        both("record 8", [&](uint8_t *self, P, P r, T, T) -> uint64_t { return reinterpret_cast<BoolVoidU32Fn>(r[8])(self + 4, self + kOut, arg & 0x3f) & 0xff; });
        both("record 9", [&](uint8_t *self, P, P r, T, T) -> uint64_t { return reinterpret_cast<BoolVoidU32Fn>(r[9])(self + 4, self + kOut, arg & 0x3f) & 0xff; });
        both("record 10", [&](uint8_t *self, P, P r, T, T) -> uint64_t { return reinterpret_cast<U32Fn>(r[10])(self + 4); });
        both("record 11", [&](uint8_t *self, P, P r, T, T) -> uint64_t { return reinterpret_cast<BoolU32Fn>(r[11])(self + 4, arg) & 0xff; });
        both("record 12", [&](uint8_t *self, P, P r, T, T) -> uint64_t { return reinterpret_cast<U32Fn>(r[12])(self + 4); });
        both("record 13", [&](uint8_t *self, P, P r, T, T) -> uint64_t { return reinterpret_cast<BoolU32Fn>(r[13])(self + 4, arg) & 0xff; });
        both("record 14", [&](uint8_t *self, P, P r, T, T) -> uint64_t { return reinterpret_cast<BoolI32Fn>(r[14])(self + 4, static_cast<int32_t>(arg)) & 0xff; });
        both("ostream 3", [&](uint8_t *self, P, P, T o, T) -> uint64_t { reinterpret_cast<VoidFn>(o[3])(self + 0x20); return 0; });
        both("ostream 18", [&](uint8_t *self, P, P, T o, T) -> uint64_t { return reinterpret_cast<U8Fn>(o[18])(self + 0x20); });
        both("ostream 20", [&](uint8_t *self, P, P, T o, T) -> uint64_t { return reinterpret_cast<uintptr_t>(reinterpret_cast<PtrFn>(o[20])(self + 0x20)); });
        both("ostream 23", [&](uint8_t *self, P, P, T o, T) -> uint64_t { return reinterpret_cast<BoolFn>(o[23])(self + 0x20) & 0xff; });
        both("istream 14", [&](uint8_t *self, P, P, T, T in) -> uint64_t { return reinterpret_cast<BoolCharU32Fn>(in[14])(self + 0x30, reinterpret_cast<char *>(self + kOut), arg) & 0xff; });
        both("istream 18", [&](uint8_t *self, P, P, T, T in) -> uint64_t { return reinterpret_cast<U8Fn>(in[18])(self + 0x30); });
        both("istream 20", [&](uint8_t *self, P, P, T, T in) -> uint64_t { return reinterpret_cast<uintptr_t>(reinterpret_cast<PtrFn>(in[20])(self + 0x30)); });
        both("istream 23", [&](uint8_t *self, P, P, T, T in) -> uint64_t { return reinterpret_cast<BoolFn>(in[23])(self + 0x30) & 0xff; });
    }
    if (checked < 7000) ctx.Fail("%s: only %d checks", who, checked);
}


} // namespace

DIFF_TEST(cGZDBReadRecordPackedFile_Construct, 0x9a6de0) { ConstructTest<false>(ctx, 0x9a6de0, "cGZDBReadRecordPackedFile::Construct"); }
DIFF_TEST(cGZDBReadRecordRAM_Construct, 0x9a6e60) { ConstructTest<true>(ctx, 0x9a6e60, "cGZDBReadRecordRAM::Construct"); }
DIFF_TEST(cGZDBReadRecordPackedFile_DeletingDestructor, 0x9a78e0) { DestructTest<false>(ctx, 0x9a78e0, "cGZDBReadRecordPackedFile::DeletingDestructor"); }
DIFF_TEST(cGZDBReadRecordRAM_DeletingDestructor, 0x9a7950) { DestructTest<true>(ctx, 0x9a7950, "cGZDBReadRecordRAM::DeletingDestructor"); }
DIFF_TEST(cGZDBReadRecordPackedFile_GetFieldVoid, 0x9abc70) { GetFieldVoidTest<false>(ctx, 0x9abc70, "cGZDBReadRecordPackedFile::GetFieldVoid"); }
DIFF_TEST(cGZDBReadRecordRAM_GetFieldVoid, 0x9abd20) { GetFieldVoidTest<true>(ctx, 0x9abd20, "cGZDBReadRecordRAM::GetFieldVoid"); }
DIFF_TEST(cGZDBReadRecordPackedFile_SetFieldVoid, 0x9afdf0) { SetFieldVoidTest<false>(ctx, 0x9afdf0, "cGZDBReadRecordPackedFile::SetFieldVoid"); }
DIFF_TEST(cGZDBReadRecordRAM_SetFieldVoid, 0x9afe20) { SetFieldVoidTest<true>(ctx, 0x9afe20, "cGZDBReadRecordRAM::SetFieldVoid"); }
DIFF_TEST(cGZDBRecordPackedFile_Seek, 0x9afa20)
{
    SeekTest<false>(ctx, 0x9afa20, "cGZDBReadRecordPackedFile::Seek");
    SeekTest<true>(ctx, 0x9afa20, "cGZDBReadRecordRAM::Seek");
}
DIFF_TEST(cGZDBReadRecordPackedFile_SeekRelative, 0x9afb10) { SeekRelativeTest<false>(ctx, 0x9afb10, "cGZDBReadRecordPackedFile::SeekRelative"); }
DIFF_TEST(cGZDBReadRecordRAM_SeekRelative, 0x9afb50) { SeekRelativeTest<true>(ctx, 0x9afb50, "cGZDBReadRecordRAM::SeekRelative"); }
DIFF_TEST(cGZDBReadRecordPackedFile_Close, 0x9a9ff0) { CloseTest<false>(ctx, 0x9a9ff0, "cGZDBReadRecordPackedFile::Close"); }
DIFF_TEST(cGZDBReadRecordRAM_Close, 0x9aa020) { CloseTest<true>(ctx, 0x9aa020, "cGZDBReadRecordRAM::Close"); }
DIFF_TEST(cGZDBReadRecordPackedFile_ReadSource, 0x9af120) { ReadSourceTest(ctx); }
DIFF_TEST(cGZDBReadRecordPackedFile_ReadBuffered, 0x9ae500) { ReadBufferedTest(ctx); }
DIFF_TEST(cGZDBReadRecordPackedFile_CachedRemaining, 0x9a8050) { CachedRemainingTest(ctx); }
DIFF_TEST(cGZDBReadRecordPackedFile_Faces, 0xe5f534) { FacesTest<false>(ctx); }
DIFF_TEST(cGZDBReadRecordRAM_Faces, 0xe5f764) { FacesTest<true>(ctx); }
