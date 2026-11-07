// Differential tests for cGZPersistDBSerialRecord's reference counting (QueryInterface, AddRef, Release),
// its counted and single field getters (Uint8..Uint64, Float32, Float64, ResKey) and the field setters
// that write through raw-write slot 51 (Uint8..Uint64, Sint16, Float32, Float64, ResKey). Objects are raw
// buffers with copies of the class's two vtables: the raw read (primary slot 27) is a fake that serves
// bytes from a script, the raw write (slot 51) records the bytes it is given, and the AddRef and
// deleting-destructor slots record their calls. Each side logs the
// calls it makes, the result, the object's bytes and the bytes consumed from the script, and the two
// sides must agree on all of them. Each function also runs once through a vtable whose slot points at
// our function, so the slot path is covered as well as the direct call.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBSerialRecord.h"

#include <windows.h>

#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using RefFn = uint32_t(__thiscall *)(void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using OneFn = bool(__thiscall *)(void *, void *);
using CountedFn = bool(__thiscall *)(void *, void *, uint32_t);
using OneMember = bool (cGZPersistDBSerialRecord::*)(void *);
using CountedMember = bool (cGZPersistDBSerialRecord::*)(void *, uint32_t);
using QueryMember = bool (cGZPersistDBSerialRecord::*)(uint32_t, void **);
using RefMember = uint32_t (cGZPersistDBSerialRecord::*)();
using ReleaseMember = uint32_t (cGZPersistDBSerialRecord::*)();

constexpr uint32_t kRecordVTable = 0xda6194; // the cGZPersistDBRecord subobject at +4
constexpr size_t kBytes = 0x200;             // whole object; dst is at kDst, away from the fields
constexpr size_t kDst = 0x80;
constexpr uintptr_t kUnchanged = 0xFACE0000u; // initial *ppvObj; never dereferenced

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// Event log: three int64s per event (kind, a, b).
enum Kind : int64_t {
    kAddRefEvent = 1,
    kRawReadEvent = 2,
    kDeleteEvent = 3,
    kRawWriteEvent = 4,
    kStrlenEvent = 5,     // fake string: slot 7
    kStrDataEvent = 6,    // fake string: slot 6
    kGetStringEvent = 7,  // primary slot 24 (GetFieldString), a fake that writes nothing
};
std::vector<int64_t> gLog;
uint8_t *gBase = nullptr; // the object under test; events report offsets from it
std::vector<uint8_t> gSource;
size_t gPos = 0;
bool gRefuse = false;

int64_t Off(const void *p)
{
    return reinterpret_cast<const uint8_t *>(p) - gBase;
}

// Whether p is inside the object. Stack temporaries have different addresses in the two runs, so the
// logs record those as -1 instead.
bool InObject(const void *p)
{
    const auto *b = static_cast<const uint8_t *>(p);
    return b >= gBase && b < gBase + kBytes;
}

void Push(int64_t kind, int64_t a, int64_t b)
{
    gLog.push_back(kind);
    gLog.push_back(a);
    gLog.push_back(b);
}

// AddRef on either subobject: records the offset of `self` and bumps the count at +8, as the original.
uint32_t __fastcall RecordAddRef(void *self, void * /*edx*/)
{
    Push(kAddRefEvent, Off(self), 0);
    uint16_t count;
    std::memcpy(&count, gBase + 8, sizeof(count));
    ++count;
    std::memcpy(gBase + 8, &count, sizeof(count));
    return count;
}

// Deleting destructor of the cGZPersistDBRecord subobject: records the call and does not free anything.
void __fastcall RecordDelete(void *self, void * /*edx*/, uint32_t flags)
{
    Push(kDeleteEvent, Off(self), flags);
}

// Raw read (GetVoid): serves n bytes from the script, or fails (writing nothing) when refused or when
// fewer than n bytes remain.
bool __fastcall FakeRawRead(void *self, void * /*edx*/, void *dst, uint32_t n)
{
    Push(kRawReadEvent, InObject(dst) ? Off(dst) : -1, n);
    (void)self;
    if (gRefuse || gSource.size() - gPos < n) return false;
    std::memcpy(dst, gSource.data() + gPos, n);
    gPos += n;
    return true;
}

// Raw write (SetVoid): records the bytes it is given and fails when refused. The buffer's address is not
// logged, since the original hands its swapped copies to this from its own stack or heap.
bool __fastcall FakeRawWrite(void *self, void * /*edx*/, const void *src, uint32_t n)
{
    (void)self;
    Push(kRawWriteEvent, n, 0);
    const auto *bytes = static_cast<const uint8_t *>(src);
    for (uint32_t i = 0; i < n; ++i) gLog.push_back(bytes[i]);
    return !gRefuse;
}

// A stand-in for a cIGZString: slot 6 is Data(), slot 7 is Strlen(). Its length and bytes are set per trial.
// The object sits at a fixed address, so the logs can record whether a call received it.
struct FakeString {
    void *vtable;
};
FakeString gStr;
void *gStrVTable[8];
uint32_t gStrLen = 0;
std::vector<uint8_t> gStrData;

uint32_t __fastcall FakeStrLen(void * /*self*/, void * /*edx*/)
{
    Push(kStrlenEvent, gStrLen, 0);
    return gStrLen;
}

const char *__fastcall FakeStrData(void * /*self*/, void * /*edx*/)
{
    Push(kStrDataEvent, 0, 0);
    return reinterpret_cast<const char *>(gStrData.data());
}

// Primary slot 24 (GetFieldString) as a fake: records the call and whether it got gStr. Writes nothing.
bool __fastcall FakeGetString(void * /*self*/, void * /*edx*/, void *dst)
{
    Push(kGetStringEvent, dst == static_cast<void *>(&gStr) ? 1 : 0, 0);
    return !gRefuse;
}

void SetupFakeString()
{
    gStrVTable[6] = reinterpret_cast<void *>(&FakeStrData);
    gStrVTable[7] = reinterpret_cast<void *>(&FakeStrLen);
    gStr.vtable = gStrVTable;
}

// Copies of the vtables used by every object, with the recording slots in place. `routed` copies have
// some slots replaced by our function. `stream` and `istream` are the +32 and +48 subobjects' vtables.
struct Tables {
    void *primary[62];
    void *record[21];
    void *stream[24];
    void *istream[24];
};

Tables MakeTables(const difftest::Context &ctx)
{
    Tables t{};
    std::memcpy(t.primary, ctx.image.At(cGZPersistDBSerialRecord::kVTable), sizeof(t.primary));
    std::memcpy(t.record, ctx.image.At(kRecordVTable), sizeof(t.record));
    std::memcpy(t.stream, ctx.image.At(0xda61ec), sizeof(t.stream));
    std::memcpy(t.istream, ctx.image.At(0xda6250), sizeof(t.istream));
    t.primary[1] = reinterpret_cast<void *>(&RecordAddRef);
    t.primary[cGZPersistDBSerialRecord::kRawReadSlot] = reinterpret_cast<void *>(&FakeRawRead);
    t.primary[cGZPersistDBSerialRecord::kRawWriteSlot] = reinterpret_cast<void *>(&FakeRawWrite);
    t.primary[24] = reinterpret_cast<void *>(&FakeGetString);
    t.record[1] = reinterpret_cast<void *>(&RecordAddRef);
    t.record[cGZPersistDBSerialRecord::kRecordDeleteSlot] = reinterpret_cast<void *>(&RecordDelete);
    return t;
}

// One trial's inputs, shared by the original and both replacement runs.
struct Scene {
    std::vector<uint8_t> image; // whole object, with the flag at +0x40 and the count at +8
    std::vector<uint8_t> source;
    bool refuse;
};

// Flag 0 means byte-swap; nonzero values skip it. Weighted towards 0, and includes the top bit.
uint8_t PickFlag(std::mt19937 &rng)
{
    static const uint8_t flags[] = {0, 0, 0, 1, 2, 0x80, 0xff};
    return flags[rng() % 7];
}

Scene MakeScene(std::mt19937 &rng, uint32_t maxBytes, uint16_t count)
{
    Scene s;
    s.image.resize(kBytes);
    for (auto &b : s.image) b = static_cast<uint8_t>(rng());
    std::memcpy(s.image.data() + 8, &count, sizeof(count));
    s.image[0x40] = PickFlag(rng);
    size_t len = rng() % (maxBytes + 8); // some scripts run short, so raw reads fail
    s.source.resize(len);
    for (auto &b : s.source) b = static_cast<uint8_t>(rng());
    s.refuse = rng() % 8 == 0;
    return s;
}

// A scene for the setters: random object bytes (flag included) and `len` random source bytes.
Scene MakeWriteScene(std::mt19937 &rng, size_t len)
{
    Scene s = MakeScene(rng, 0, 0);
    s.source.resize(len);
    for (auto &b : s.source) b = static_cast<uint8_t>(rng());
    return s;
}

struct Outcome {
    bool result = false;
    int64_t extra = 0; // QueryInterface: the out pointer relative to the object
    std::vector<int64_t> log;
    std::vector<uint8_t> bytes;
    size_t pos = 0;
};

// Runs one call on a fresh object built from the scene and the tables.
template <typename Call> Outcome Run(const Tables &t, const Scene &s, Call call)
{
    std::vector<uint8_t> buf(s.image);
    const void *primary = t.primary;
    const void *record = t.record;
    const void *stream = t.stream;
    const void *istream = t.istream;
    std::memcpy(buf.data(), &primary, sizeof(primary));
    std::memcpy(buf.data() + 4, &record, sizeof(record));
    std::memcpy(buf.data() + 32, &stream, sizeof(stream));
    std::memcpy(buf.data() + 48, &istream, sizeof(istream));
    gBase = buf.data();
    gLog.clear();
    gSource = s.source;
    gPos = 0;
    gRefuse = s.refuse;

    Outcome out;
    out.result = call(buf.data(), out.extra);
    out.log = gLog;
    // The vtable pointers differ between the plain and routed tables, so they are not compared.
    std::memset(buf.data(), 0, 8);
    std::memset(buf.data() + 32, 0, 4);
    std::memset(buf.data() + 48, 0, 4);
    out.bytes = buf;
    out.pos = gPos;
    return out;
}

void Compare(difftest::Context &ctx, const char *name, const Outcome &a, const Outcome &b, const Outcome &c)
{
    ++ctx.cases;
    if (a.result != b.result || a.result != c.result) {
        ctx.Fail("%s: return original %d, ours %d, via slot %d", name, a.result, b.result, c.result);
    }
    if (a.extra != b.extra || a.extra != c.extra) ctx.Fail("%s: output pointer differs", name);
    if (a.log != b.log || a.log != c.log) ctx.Fail("%s: calls made differ (%zu / %zu / %zu values)", name, a.log.size(),
                                                 b.log.size(), c.log.size());
    if (a.pos != b.pos || a.pos != c.pos) ctx.Fail("%s: bytes consumed differ (%zu, %zu, %zu)", name, a.pos, b.pos, c.pos);
    if (a.bytes != b.bytes || a.bytes != c.bytes) ctx.Fail("%s: object state differs", name);
}

// Reads (dst) and (dst, count) with the same element width; the single form reads one element.
void CheckSingle(difftest::Context &ctx, const char *name, uint32_t rva, int slot, OneMember ours, uint32_t width,
                 std::mt19937 &rng, int trials)
{
    auto orig = ctx.Original<OneFn>(rva);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[slot] = Addr(ours);

    for (int i = 0; i < trials; ++i) {
        Scene s = MakeScene(rng, width, 0);
        auto runOrig = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self, self + kDst); });
        auto runOurs = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return (reinterpret_cast<cGZPersistDBSerialRecord *>(self)->*ours)(self + kDst);
        });
        auto runRouted = Run(routed, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<OneFn>(self, slot)(self, self + kDst);
        });
        Compare(ctx, name, runOrig, runOurs, runRouted);
    }
}

void CheckCounted(difftest::Context &ctx, const char *name, uint32_t rva, int slot, CountedMember ours, uint32_t width,
                  std::mt19937 &rng, int trials)
{
    auto orig = ctx.Original<CountedFn>(rva);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[slot] = Addr(ours);

    for (int i = 0; i < trials; ++i) {
        uint32_t count = rng() % 7; // 0..6 elements, including the empty array
        Scene s = MakeScene(rng, width * count, 0);
        auto runOrig = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self, self + kDst, count); });
        auto runOurs = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return (reinterpret_cast<cGZPersistDBSerialRecord *>(self)->*ours)(self + kDst, count);
        });
        auto runRouted = Run(routed, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<CountedFn>(self, slot)(self, self + kDst, count);
        });
        Compare(ctx, name, runOrig, runOurs, runRouted);
    }
}

using ValueU32Fn = bool(__thiscall *)(void *, uint32_t);
using ValueU64Fn = bool(__thiscall *)(void *, uint64_t);
using CountedSetFn = bool(__thiscall *)(void *, const void *, uint32_t);
using KeyFn = bool(__thiscall *)(void *, const void *);
using CountedSetMember = bool (cGZPersistDBSerialRecord::*)(const void *, uint32_t);
using KeyMember = bool (cGZPersistDBSerialRecord::*)(const void *);
using U8Member = bool (cGZPersistDBSerialRecord::*)(uint8_t);
using U16Member = bool (cGZPersistDBSerialRecord::*)(uint16_t);
using S16Member = bool (cGZPersistDBSerialRecord::*)(int16_t);
using U32Member = bool (cGZPersistDBSerialRecord::*)(uint32_t);
using U64Member = bool (cGZPersistDBSerialRecord::*)(uint64_t);
using F32Member = bool (cGZPersistDBSerialRecord::*)(float);
using F64Member = bool (cGZPersistDBSerialRecord::*)(double);

// The single-value setters take their argument on the stack: Width 4 or 8 bytes. The arguments are random
// bit patterns passed as integers, and the replacement is called through its own code address, so floats
// keep their bits (no FPU round trip).
template <uint32_t Width> using ValueFn = std::conditional_t<Width == 8, ValueU64Fn, ValueU32Fn>;
template <uint32_t Width> using ValueArg = std::conditional_t<Width == 8, uint64_t, uint32_t>;

uint64_t RandomBits(std::mt19937 &rng)
{
    uint64_t hi = rng();
    uint64_t lo = rng();
    return (hi << 32) | lo;
}

template <uint32_t Width>
void CheckValueSet(difftest::Context &ctx, const char *name, uint32_t rva, int slot, void *ours, std::mt19937 &rng, int trials)
{
    using Fn = ValueFn<Width>;
    using Arg = ValueArg<Width>;
    auto orig = ctx.Original<Fn>(rva);
    auto replacement = reinterpret_cast<Fn>(ours);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[slot] = ours;

    for (int i = 0; i < trials; ++i) {
        Arg value = static_cast<Arg>(RandomBits(rng));
        Scene s = MakeWriteScene(rng, 0);
        auto runOrig = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self, value); });
        auto runOurs = Run(plain, s, [&](uint8_t *self, int64_t &) { return replacement(self, value); });
        auto runRouted = Run(routed, s, [&](uint8_t *self, int64_t &) { return VirtualSlot<Fn>(self, slot)(self, value); });
        Compare(ctx, name, runOrig, runOurs, runRouted);
    }
}

// Counted setters take (src, count) and write count * Width bytes, byte-reversed unless the flag is set.
// Counts run from 0 to 6, with the 0x100 boundary of the heap path (0xff, 0x100, 0x101, 0x102) every 40th trial.
template <uint32_t Width>
void CheckCountedSet(difftest::Context &ctx, const char *name, uint32_t rva, int slot, void *ours, std::mt19937 &rng,
                     int trials)
{
    constexpr uint32_t kEdges[] = {0xff, 0x100, 0x101, 0x102};
    auto orig = ctx.Original<CountedSetFn>(rva);
    auto replacement = reinterpret_cast<CountedSetFn>(ours);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[slot] = ours;

    for (int i = 0; i < trials; ++i) {
        uint32_t count = i % 40 == 0 ? kEdges[rng() % 4] : rng() % 7;
        Scene s = MakeWriteScene(rng, static_cast<size_t>(Width) * count + 1);
        auto runOrig = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self, s.source.data(), count); });
        auto runOurs = Run(plain, s, [&](uint8_t *self, int64_t &) { return replacement(self, s.source.data(), count); });
        auto runRouted = Run(routed, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<CountedSetFn>(self, slot)(self, s.source.data(), count);
        });
        Compare(ctx, name, runOrig, runOurs, runRouted);
    }
}

} // namespace

DIFF_TEST(cGZPersistDBSerialRecord_QueryInterface, 0x99cfa0)
{
    auto orig = ctx.Original<QueryFn>(0x99cfa0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[0] = Addr(static_cast<QueryMember>(&cGZPersistDBSerialRecord::QueryInterface));

    const uint32_t ids[] = {0, 1, 2, 0x051b11bd, 0x051b11bc, 0x051b11bb, 0xc51b11cb, 0xc56b8f08, 0xe56b8f03,
                            0xc51b11cc, 0xc56b8f09, 0xe56b8f04, 0x80000000u, 0xffffffffu};
    std::mt19937 rng(0x6b8f);
    for (int i = 0; i < 20000; ++i) {
        uint32_t riid = i < 14 ? ids[i] : (i % 3 == 0 ? ids[rng() % 14] : static_cast<uint32_t>(rng()));
        if (i >= 14 && i % 3 == 1) riid ^= 1u << (rng() % 32);
        uint16_t count = static_cast<uint16_t>(i % 4 == 0 ? 0xffff : i % 4 == 1 ? 0 : rng());
        Scene s = MakeScene(rng, 0, count);

        auto runOrig = Run(plain, s, [&](uint8_t *self, int64_t &extra) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            bool r = orig(self, riid, &out);
            extra = out == reinterpret_cast<void *>(kUnchanged) ? INT64_MIN
                : out == nullptr                                 ? INT64_MIN + 1
                                                                  : Off(out);
            return r;
        });
        auto runOurs = Run(plain, s, [&](uint8_t *self, int64_t &extra) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            bool r = reinterpret_cast<cGZPersistDBSerialRecord *>(self)->QueryInterface(riid, &out);
            extra = out == reinterpret_cast<void *>(kUnchanged) ? INT64_MIN
                : out == nullptr                                 ? INT64_MIN + 1
                                                                  : Off(out);
            return r;
        });
        auto runRouted = Run(routed, s, [&](uint8_t *self, int64_t &extra) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            bool r = VirtualSlot<QueryFn>(self, 0)(self, riid, &out);
            extra = out == reinterpret_cast<void *>(kUnchanged) ? INT64_MIN
                : out == nullptr                                 ? INT64_MIN + 1
                                                                  : Off(out);
            return r;
        });
        Compare(ctx, "QueryInterface", runOrig, runOurs, runRouted);
    }
}

DIFF_TEST(cGZPersistDBSerialRecord_AddRef, 0x97b790)
{
    auto orig = ctx.Original<RefFn>(0x97b790);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[1] = Addr(static_cast<RefMember>(&cGZPersistDBSerialRecord::AddRef));

    std::mt19937 rng(0xadd);
    for (int i = 0; i < 4000; ++i) {
        uint16_t count = static_cast<uint16_t>(i % 4 == 0 ? 0xffff : i % 4 == 1 ? 0 : rng());
        Scene s = MakeScene(rng, 0, count);
        uint32_t valOrig = 0, valOurs = 0, valRouted = 0;
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOrig = orig(self);
            return true;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOurs = reinterpret_cast<cGZPersistDBSerialRecord *>(self)->AddRef();
            return true;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            valRouted = VirtualSlot<RefFn>(self, 1)(self);
            return true;
        });
        Compare(ctx, "AddRef", a, b, c);
        if (valOrig != valOurs || valOrig != valRouted) ctx.Fail("AddRef: return original %u, ours %u, via slot %u", valOrig, valOurs, valRouted);
    }
}

DIFF_TEST(cGZPersistDBSerialRecord_Release, 0x99d0a0)
{
    auto orig = ctx.Original<RefFn>(0x99d0a0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[2] = Addr(static_cast<ReleaseMember>(&cGZPersistDBSerialRecord::Release));

    std::mt19937 rng(0x9e1);
    const uint16_t edges[] = {0, 1, 2, 3, 0xfffe, 0xffff};
    for (int i = 0; i < 4000; ++i) {
        uint16_t count = i < 6 ? edges[i] : static_cast<uint16_t>(rng() % 3 == 0 ? rng() % 4 : rng());
        Scene s = MakeScene(rng, 0, count);
        uint32_t valOrig = 0, valOurs = 0, valRouted = 0;
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOrig = orig(self);
            return true;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOurs = reinterpret_cast<cGZPersistDBSerialRecord *>(self)->Release();
            return true;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            valRouted = VirtualSlot<RefFn>(self, 2)(self);
            return true;
        });
        Compare(ctx, "Release", a, b, c);
        if (valOrig != valOurs || valOrig != valRouted) {
            ctx.Fail("Release (count %u): return original %u, ours %u, via slot %u", count, valOrig, valOurs, valRouted);
        }
    }
}

DIFF_TEST(cGZPersistDBSerialRecord_FieldUint8, 0x99bea0)
{
    std::mt19937 rng(0x08);
    CheckSingle(ctx, "GetFieldUint8", 0x99bea0, 5, static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint8), 1, rng,
                5000);
    CheckSingle(ctx, "GetFieldUint8 (alias)", 0x99bea0, 7, static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint8), 1,
                rng, 1000);
    CheckCounted(ctx, "GetFieldUint8 (count)", 0x99beb0, 4, static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldUint8),
                 1, rng, 5000);
    CheckCounted(ctx, "GetFieldUint8 (count, alias)", 0x99beb0, 6,
                 static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldUint8), 1, rng, 1000);
}

DIFF_TEST(cGZPersistDBSerialRecord_FieldUint16, 0x99bc80)
{
    std::mt19937 rng(0x16);
    CheckSingle(ctx, "GetFieldUint16", 0x99bc80, 9, static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint16), 2, rng,
                5000);
    CheckCounted(ctx, "GetFieldUint16 (count)", 0x99bcc0, 8,
                 static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldUint16), 2, rng, 5000);
}

DIFF_TEST(cGZPersistDBSerialRecord_FieldUint32, 0x99bd20)
{
    std::mt19937 rng(0x32);
    CheckSingle(ctx, "GetFieldUint32", 0x99bd20, 13, static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint32), 4, rng,
                5000);
    CheckCounted(ctx, "GetFieldUint32 (count)", 0x99bd60, 12,
                 static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldUint32), 4, rng, 5000);
}

DIFF_TEST(cGZPersistDBSerialRecord_FieldUint64, 0x99bdc0)
{
    std::mt19937 rng(0x64);
    CheckSingle(ctx, "GetFieldUint64", 0x99bdc0, 17, static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint64), 8, rng,
                5000);
    CheckCounted(ctx, "GetFieldUint64 (count)", 0x99be20, 16,
                 static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldUint64), 8, rng, 5000);
}

DIFF_TEST(cGZPersistDBSerialRecord_FieldFloat32, 0x99bbb0)
{
    std::mt19937 rng(0xf32);
    CheckSingle(ctx, "GetFieldFloat32", 0x99bbb0, 21, static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldFloat32), 4, rng,
                5000);
    CheckCounted(ctx, "GetFieldFloat32 (count)", 0x99bbc0, 20,
                 static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldFloat32), 4, rng, 5000);
}

DIFF_TEST(cGZPersistDBSerialRecord_FieldFloat64, 0x99bbe0)
{
    std::mt19937 rng(0xf64);
    CheckSingle(ctx, "GetFieldFloat64", 0x99bbe0, 23, static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldFloat64), 8, rng,
                5000);
    CheckCounted(ctx, "GetFieldFloat64 (count)", 0x99bbf0, 22,
                 static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldFloat64), 8, rng, 5000);
}

// GetFieldResKey reads three uint32s through slot 13. The chained run also routes slot 13 to our
// GetFieldUint32, so the three reads go through our code as well as the original's.
DIFF_TEST(cGZPersistDBSerialRecord_GetFieldResKey, 0x99bc10)
{
    auto orig = ctx.Original<OneFn>(0x99bc10);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[25] = Addr(static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldResKey));
    Tables chained = routed;
    chained.primary[cGZPersistDBSerialRecord::kGetUint32Slot] = Addr(static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint32));

    std::mt19937 rng(0x7e5);
    for (int i = 0; i < 5000; ++i) {
        // 12 bytes of script are needed for a full key; 0..19 bytes leave the later reads to fail.
        Scene s = MakeScene(rng, 12, 0);
        auto runOrig = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self, self + kDst); });
        auto runOurs = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecord *>(self)->GetFieldResKey(self + kDst);
        });
        auto runChained = Run(chained, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<OneFn>(self, 25)(self, self + kDst);
        });
        Compare(ctx, "GetFieldResKey", runOrig, runOurs, runChained);
    }
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldResKey, 0x99d140)
{
    auto orig = ctx.Original<KeyFn>(0x99d140);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[49] = Addr(static_cast<KeyMember>(&cGZPersistDBSerialRecord::SetFieldResKey));
    Tables chained = routed;
    chained.primary[cGZPersistDBSerialRecord::kSetUint32Slot] = Addr(static_cast<U32Member>(&cGZPersistDBSerialRecord::SetFieldUint32));

    std::mt19937 rng(0x5e7);
    for (int i = 0; i < 4000; ++i) {
        Scene s = MakeWriteScene(rng, 0);
        auto runOrig = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self, self + kDst); });
        auto runOurs = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecord *>(self)->SetFieldResKey(self + kDst);
        });
        auto runChained = Run(chained, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<KeyFn>(self, 49)(self, self + kDst);
        });
        Compare(ctx, "SetFieldResKey", runOrig, runOurs, runChained);
    }
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldUint8, 0x99d540)
{
    std::mt19937 rng(0x5a8);
    CheckValueSet<4>(ctx, "SetFieldUint8", 0x99d540, 29, Addr(static_cast<U8Member>(&cGZPersistDBSerialRecord::SetFieldUint8)), rng,
                     3000);
    CheckValueSet<4>(ctx, "SetFieldUint8 (alias)", 0x99d540, 31, Addr(static_cast<U8Member>(&cGZPersistDBSerialRecord::SetFieldUint8)),
                     rng, 1000);
    CheckCountedSet<1>(ctx, "SetFieldUint8 (count)", 0x99d560, 28,
                       Addr(static_cast<CountedSetMember>(&cGZPersistDBSerialRecord::SetFieldUint8)), rng, 3000);
    CheckCountedSet<1>(ctx, "SetFieldUint8 (count, alias)", 0x99d560, 30,
                       Addr(static_cast<CountedSetMember>(&cGZPersistDBSerialRecord::SetFieldUint8)), rng, 1000);
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldUint16, 0x99d5c0)
{
    std::mt19937 rng(0x5a16);
    CheckValueSet<4>(ctx, "SetFieldUint16", 0x99d5c0, 33, Addr(static_cast<U16Member>(&cGZPersistDBSerialRecord::SetFieldUint16)), rng,
                     3000);
    CheckCountedSet<2>(ctx, "SetFieldUint16 (count)", 0x99d1d0, 32,
                       Addr(static_cast<CountedSetMember>(&cGZPersistDBSerialRecord::SetFieldUint16)), rng, 3000);
    CheckCountedSet<2>(ctx, "SetFieldUint16 (count, alias)", 0x99d1d0, 34,
                       Addr(static_cast<CountedSetMember>(&cGZPersistDBSerialRecord::SetFieldUint16)), rng, 1000);
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldSint16, 0x99d190)
{
    std::mt19937 rng(0x5a17);
    CheckValueSet<4>(ctx, "SetFieldSint16", 0x99d190, 35, Addr(static_cast<S16Member>(&cGZPersistDBSerialRecord::SetFieldSint16)), rng,
                     3000);
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldUint32, 0x99d2b0)
{
    std::mt19937 rng(0x5a32);
    CheckValueSet<4>(ctx, "SetFieldUint32", 0x99d2b0, 37, Addr(static_cast<U32Member>(&cGZPersistDBSerialRecord::SetFieldUint32)), rng,
                     3000);
    CheckCountedSet<4>(ctx, "SetFieldUint32 (count)", 0x99d2f0, 36,
                       Addr(static_cast<CountedSetMember>(&cGZPersistDBSerialRecord::SetFieldUint32)), rng, 3000);
    CheckCountedSet<4>(ctx, "SetFieldUint32 (count, alias)", 0x99d2f0, 38,
                       Addr(static_cast<CountedSetMember>(&cGZPersistDBSerialRecord::SetFieldUint32)), rng, 1000);
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldUint64, 0x99d600)
{
    std::mt19937 rng(0x5a64);
    CheckValueSet<8>(ctx, "SetFieldUint64", 0x99d600, 41, Addr(static_cast<U64Member>(&cGZPersistDBSerialRecord::SetFieldUint64)), rng,
                     3000);
    CheckCountedSet<8>(ctx, "SetFieldUint64 (count)", 0x99d3d0, 40,
                       Addr(static_cast<CountedSetMember>(&cGZPersistDBSerialRecord::SetFieldUint64)), rng, 3000);
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldFloat32, 0x99d0c0)
{
    std::mt19937 rng(0x5af32);
    CheckValueSet<4>(ctx, "SetFieldFloat32", 0x99d0c0, 45, Addr(static_cast<F32Member>(&cGZPersistDBSerialRecord::SetFieldFloat32)),
                     rng, 3000);
    CheckCountedSet<4>(ctx, "SetFieldFloat32 (count)", 0x99d0e0, 44,
                       Addr(static_cast<CountedSetMember>(&cGZPersistDBSerialRecord::SetFieldFloat32)), rng, 3000);
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldFloat64, 0x99d100)
{
    std::mt19937 rng(0x5af64);
    CheckValueSet<8>(ctx, "SetFieldFloat64", 0x99d100, 47, Addr(static_cast<F64Member>(&cGZPersistDBSerialRecord::SetFieldFloat64)),
                     rng, 3000);
    CheckCountedSet<8>(ctx, "SetFieldFloat64 (count)", 0x99d120, 46,
                       Addr(static_cast<CountedSetMember>(&cGZPersistDBSerialRecord::SetFieldFloat64)), rng, 3000);
}

// ---- Further slots -------------------------------------------------------------------------------

namespace {

using StringSetFn = bool(__thiscall *)(void *, const void *);
using StringSetMember = bool (cGZPersistDBSerialRecord::*)(const cIGZString *);
using StringGetFn = bool(__thiscall *)(void *, void *);
using BlobReadFn = bool(__thiscall *)(void *, void *, uint32_t, uint32_t);
using BlobReadMember = bool (cGZPersistDBSerialRecord::*)(void *, uint32_t, uint32_t);
using BlobWriteFn = bool(__thiscall *)(void *, const void *, uint32_t, uint32_t);
using BlobWriteMember = bool (cGZPersistDBSerialRecord::*)(const void *, uint32_t, uint32_t);
using PtrFn = void *(__thiscall *)(void *, const void *);
using PtrMember = void *(cGZPersistDBSerialRecord::*)(const cIGZString *);
using PtrGetFn = void *(__thiscall *)(void *, void *);
using PtrGetMember = void *(cGZPersistDBSerialRecord::*)(cIGZString *);
using PtrUnusedFn = void *(__thiscall *)(void *, uint32_t);
using PtrUnusedMember = void *(cGZPersistDBSerialRecord::*)(uint32_t);
using S64Member = bool (cGZPersistDBSerialRecord::*)(int64_t);

// Returned pointers: the offset from the object if inside it, -1 for null, -2 for anything else.
int64_t PtrOff(const void *p)
{
    if (p == nullptr) return -1;
    return InObject(p) ? Off(p) : -2;
}

} // namespace

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldSint64, 0x99d4d0)
{
    // int64_t and uint64_t arguments share a calling convention, so the Uint64 harness takes this one too.
    std::mt19937 rng(0x5a64);
    auto ours = Addr(static_cast<S64Member>(&cGZPersistDBSerialRecord::SetFieldSint64));
    CheckValueSet<8>(ctx, "SetFieldSint64", 0x99d4d0, 43, ours, rng, 3000);
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldString, 0x99d570)
{
    SetupFakeString();
    auto orig = ctx.Original<StringSetFn>(0x99d570);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[48] = Addr(static_cast<StringSetMember>(&cGZPersistDBSerialRecord::SetFieldString));

    std::mt19937 rng(0x5a48);
    for (int i = 0; i < 4000; ++i) {
        // Mostly short strings; every fifth trial is long enough for the 16-bit length to matter.
        gStrLen = i % 5 == 0 ? static_cast<uint32_t>(rng() % 300) : static_cast<uint32_t>(rng() % 12);
        gStrData.assign(gStrLen + 1, 0);
        for (auto &b : gStrData) b = static_cast<uint8_t>(rng());
        Scene s = MakeWriteScene(rng, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self, &gStr); });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecord *>(self)->SetFieldString(reinterpret_cast<cIGZString *>(&gStr));
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<StringSetFn>(self, 48)(self, &gStr);
        });
        Compare(ctx, "SetFieldString", a, b, c);
    }
}

// Counted and blob forms: the three arguments (dst or src, element size, count) and the RawRead or
// RawWrite they reach. The sizes include 0 and products that wrap around to 0 or a small value.
DIFF_TEST(cGZPersistDBSerialRecord_GetFieldBlob, 0x99cd60)
{
    auto orig = ctx.Original<BlobReadFn>(0x99cd60);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[57] = Addr(static_cast<BlobReadMember>(&cGZPersistDBSerialRecord::GetFieldBlob));

    std::mt19937 rng(0x6b1);
    const uint32_t sizes[] = {0, 1, 2, 3, 4, 8, 0x40000000u, 0x80000000u};
    for (int i = 0; i < 4000; ++i) {
        uint32_t size = sizes[rng() % 8];
        uint32_t count = static_cast<uint32_t>(rng() % 7);
        if (i % 9 == 0) count = 2; // with 0x80000000 this wraps to 0; with 0x40000000 it is 0x80000000
        uint32_t maxBytes = size * count;
        Scene s = MakeScene(rng, maxBytes > 64 ? 64 : maxBytes, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self, self + kDst, size, count); });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecord *>(self)->GetFieldBlob(self + kDst, size, count);
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<BlobReadFn>(self, 57)(self, self + kDst, size, count);
        });
        Compare(ctx, "GetFieldBlob", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldBlob, 0x99dd40)
{
    auto orig = ctx.Original<BlobWriteFn>(0x99dd40);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[58] = Addr(static_cast<BlobWriteMember>(&cGZPersistDBSerialRecord::SetFieldBlob));

    std::mt19937 rng(0x6b2);
    // The fake raw write reads n bytes of the source, so the products stay within the 64-byte source. The
    // two wrapping pairs are the exception: 0x80000000 * 2 and 0x40000000 * 4 are 0 modulo 2^32.
    const uint32_t sizes[] = {0, 1, 2, 3, 4, 8};
    for (int i = 0; i < 4000; ++i) {
        uint32_t size = sizes[rng() % 6];
        uint32_t count = static_cast<uint32_t>(rng() % 7);
        if (i % 9 == 0) {
            size = i % 18 == 0 ? 0x80000000u : 0x40000000u;
            count = i % 18 == 0 ? 2 : 4;
        }
        Scene s = MakeWriteScene(rng, 64);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self, s.source.data(), size, count); });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecord *>(self)->SetFieldBlob(s.source.data(), size, count);
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<BlobWriteFn>(self, 58)(self, s.source.data(), size, count);
        });
        Compare(ctx, "SetFieldBlob", a, b, c);
    }
}

// Slot 59: the string call (slot 48 of this, routed to our SetFieldString in the chained run) and the
// cRZOStream subobject back.
DIFF_TEST(cGZPersistDBSerialRecord_SetFieldStringThenOStream, 0x99b930)
{
    SetupFakeString();
    auto orig = ctx.Original<PtrFn>(0x99b930);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[59] = Addr(static_cast<PtrMember>(&cGZPersistDBSerialRecord::SetFieldStringThenOStream));
    Tables chained = routed;
    chained.primary[48] = Addr(static_cast<StringSetMember>(&cGZPersistDBSerialRecord::SetFieldString));

    std::mt19937 rng(0x5b9);
    for (int i = 0; i < 3000; ++i) {
        gStrLen = static_cast<uint32_t>(rng() % 10);
        gStrData.assign(gStrLen + 1, 0x5a);
        for (auto &b : gStrData) b = static_cast<uint8_t>(rng());
        Scene s = MakeWriteScene(rng, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &e) {
            e = PtrOff(orig(self, &gStr));
            return true;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &e) {
            e = PtrOff(reinterpret_cast<cGZPersistDBSerialRecord *>(self)->SetFieldStringThenOStream(reinterpret_cast<cIGZString *>(&gStr)));
            return true;
        });
        auto c = Run(chained, s, [&](uint8_t *self, int64_t &e) {
            e = PtrOff(VirtualSlot<PtrFn>(self, 59)(self, &gStr));
            return true;
        });
        Compare(ctx, "SetFieldStringThenOStream", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBSerialRecord_GetOStream, 0x99b950)
{
    auto orig = ctx.Original<PtrUnusedFn>(0x99b950);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[60] = Addr(static_cast<PtrUnusedMember>(&cGZPersistDBSerialRecord::GetOStream));

    std::mt19937 rng(0x5b0);
    for (int i = 0; i < 2000; ++i) {
        uint32_t unused = static_cast<uint32_t>(rng());
        Scene s = MakeScene(rng, 0, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &e) {
            e = PtrOff(orig(self, unused));
            return true;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &e) {
            e = PtrOff(reinterpret_cast<cGZPersistDBSerialRecord *>(self)->GetOStream(unused));
            return true;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &e) {
            e = PtrOff(VirtualSlot<PtrUnusedFn>(self, 60)(self, unused));
            return true;
        });
        Compare(ctx, "GetOStream", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBSerialRecord_GetFieldStringThenIStream, 0x99b910)
{
    SetupFakeString();
    auto orig = ctx.Original<PtrGetFn>(0x99b910);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[61] = Addr(static_cast<PtrGetMember>(&cGZPersistDBSerialRecord::GetFieldStringThenIStream));

    std::mt19937 rng(0x5b1);
    for (int i = 0; i < 2000; ++i) {
        Scene s = MakeScene(rng, 0, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &e) {
            e = PtrOff(orig(self, &gStr));
            return true;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &e) {
            e = PtrOff(reinterpret_cast<cGZPersistDBSerialRecord *>(self)->GetFieldStringThenIStream(reinterpret_cast<cIGZString *>(&gStr)));
            return true;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &e) {
            e = PtrOff(VirtualSlot<PtrGetFn>(self, 61)(self, &gStr));
            return true;
        });
        Compare(ctx, "GetFieldStringThenIStream", a, b, c);
    }
}

// ---- Subobject thunks ----------------------------------------------------------------------------
//
// `this` is the +32 (cRZOStream) or +48 (cRZIStream) subobject. The original's thunk and ours get the
// subobject; the routed copy calls through the subobject's vtable, whose slot is ours. The subobject's
// vtable pointers are set by Run, so the vtable call is real.

namespace {

using ThunkRawReadMember = bool (cGZPersistDBSerialRecordThunks::*)(void *, uint32_t);
using ThunkRawWriteMember = bool (cGZPersistDBSerialRecordThunks::*)(const void *, uint32_t);
using ThunkSkipMember = bool (cGZPersistDBSerialRecordThunks::*)(uint32_t);
using ThunkStringGetMember = bool (cGZPersistDBSerialRecordThunks::*)(cIGZString *);
using ThunkStringSetMember = bool (cGZPersistDBSerialRecordThunks::*)(const cIGZString *);
using SkipFn = bool(__thiscall *)(void *, uint32_t);

// Primary slot 56 is a shared stub that returns false and has no effect. Replacing it with a recording
// fake (in both runs) makes a skipped call observable.
constexpr int64_t kSkipEvent = 8;
bool __fastcall FakeSkip(void * /*self*/, void * /*edx*/, uint32_t n)
{
    Push(kSkipEvent, n, 0);
    return !gRefuse;
}

} // namespace

DIFF_TEST(cGZPersistDBSerialRecordThunks_GetVoid, 0x99cee0)
{
    auto orig = ctx.Original<CountedFn>(0x99cee0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.istream[17] = Addr(static_cast<ThunkRawReadMember>(&cGZPersistDBSerialRecordThunks::GetVoid));

    std::mt19937 rng(0x7e1);
    for (int i = 0; i < 4000; ++i) {
        uint32_t n = static_cast<uint32_t>(rng() % 9);
        Scene s = MakeScene(rng, n, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self + 48, self + kDst, n); });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 48)->GetVoid(self + kDst, n);
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<CountedFn>(self + 48, 17)(self + 48, self + kDst, n);
        });
        Compare(ctx, "GetVoid", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_SetVoid, 0x99de60)
{
    using Fn = bool(__thiscall *)(void *, const void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x99de60);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.stream[17] = Addr(static_cast<ThunkRawWriteMember>(&cGZPersistDBSerialRecordThunks::SetVoid));

    std::mt19937 rng(0x7e2);
    for (int i = 0; i < 4000; ++i) {
        uint32_t n = static_cast<uint32_t>(rng() % 9);
        Scene s = MakeWriteScene(rng, 64);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self + 32, s.source.data(), n); });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 32)->SetVoid(s.source.data(), n);
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            return VirtualSlot<Fn>(self + 32, 17)(self + 32, s.source.data(), n);
        });
        Compare(ctx, "SetVoid", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_Skip, 0x99dea0)
{
    auto orig = ctx.Original<SkipFn>(0x99dea0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    plain.primary[56] = reinterpret_cast<void *>(&FakeSkip);
    routed.primary[56] = reinterpret_cast<void *>(&FakeSkip);
    routed.istream[3] = Addr(static_cast<ThunkSkipMember>(&cGZPersistDBSerialRecordThunks::Skip));

    std::mt19937 rng(0x7e3);
    for (int i = 0; i < 2000; ++i) {
        uint32_t n = static_cast<uint32_t>(rng());
        Scene s = MakeScene(rng, 0, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self + 48, n); });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 48)->Skip(n);
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) { return VirtualSlot<SkipFn>(self + 48, 3)(self + 48, n); });
        Compare(ctx, "Skip", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_GetGZStr, 0x99cd80)
{
    auto orig = ctx.Original<StringGetFn>(0x99cd80);
    SetupFakeString();
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.istream[15] = Addr(static_cast<ThunkStringGetMember>(&cGZPersistDBSerialRecordThunks::GetGZStr));

    std::mt19937 rng(0x7e4);
    for (int i = 0; i < 2000; ++i) {
        Scene s = MakeScene(rng, 0, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self + 48, &gStr); });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 48)->GetGZStr(reinterpret_cast<cIGZString *>(&gStr));
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) { return VirtualSlot<StringGetFn>(self + 48, 15)(self + 48, &gStr); });
        Compare(ctx, "GetGZStr", a, b, c);
    }
}

// SetGZStr reaches the primary's slot 48. The chained run routes that slot to our SetFieldString too.
DIFF_TEST(cGZPersistDBSerialRecordThunks_SetGZStr, 0x99dd60)
{
    SetupFakeString();
    auto orig = ctx.Original<StringSetFn>(0x99dd60);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.stream[15] = Addr(static_cast<ThunkStringSetMember>(&cGZPersistDBSerialRecordThunks::SetGZStr));
    Tables chained = routed;
    chained.primary[48] = Addr(static_cast<StringSetMember>(&cGZPersistDBSerialRecord::SetFieldString));

    std::mt19937 rng(0x7e5);
    for (int i = 0; i < 3000; ++i) {
        gStrLen = i % 5 == 0 ? static_cast<uint32_t>(rng() % 300) : static_cast<uint32_t>(rng() % 12);
        gStrData.assign(gStrLen + 1, 0);
        for (auto &b : gStrData) b = static_cast<uint8_t>(rng());
        Scene s = MakeWriteScene(rng, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) { return orig(self + 32, &gStr); });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 32)->SetGZStr(reinterpret_cast<cIGZString *>(&gStr));
        });
        auto c = Run(chained, s, [&](uint8_t *self, int64_t &) { return VirtualSlot<StringSetFn>(self + 32, 15)(self + 32, &gStr); });
        Compare(ctx, "SetGZStr", a, b, c);
    }
}

// The +4 (cGZPersistDBRecord) thunks: `this` is the record subobject at +4, and each one tail-jumps to the
// primary. The routed runs replace the +4 vtable's slot with our thunk, so the vtable path reaches it.
using ThunkRefMember = uint32_t (cGZPersistDBSerialRecordThunks::*)();
using ThunkQueryMember = bool (cGZPersistDBSerialRecordThunks::*)(uint32_t, void **);

DIFF_TEST(cGZPersistDBSerialRecordThunks_AddRef4, 0x1dab02)
{
    auto orig = ctx.Original<RefFn>(0x1dab02);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.record[1] = Addr(static_cast<ThunkRefMember>(&cGZPersistDBSerialRecordThunks::AddRef4));

    std::mt19937 rng(0x4a1);
    for (int i = 0; i < 4000; ++i) {
        uint16_t count = static_cast<uint16_t>(i % 4 == 0 ? 0xffff : i % 4 == 1 ? 0 : rng());
        Scene s = MakeScene(rng, 0, count);
        uint32_t valOrig = 0, valOurs = 0, valRouted = 0;
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOrig = orig(self + 4);
            return true;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOurs = reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 4)->AddRef4();
            return true;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            valRouted = VirtualSlot<RefFn>(self + 4, 1)(self + 4);
            return true;
        });
        Compare(ctx, "AddRef4", a, b, c);
        if (valOrig != valOurs || valOrig != valRouted) ctx.Fail("AddRef4: return original %u, ours %u, via slot %u", valOrig, valOurs, valRouted);
    }
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_Release4, 0x1e3950)
{
    auto orig = ctx.Original<RefFn>(0x1e3950);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.record[2] = Addr(static_cast<ThunkRefMember>(&cGZPersistDBSerialRecordThunks::Release4));

    std::mt19937 rng(0x4e3);
    const uint16_t edges[] = {0, 1, 2, 3, 0xfffe, 0xffff};
    for (int i = 0; i < 4000; ++i) {
        uint16_t count = i < 6 ? edges[i] : static_cast<uint16_t>(rng() % 3 == 0 ? rng() % 4 : rng());
        Scene s = MakeScene(rng, 0, count);
        uint32_t valOrig = 0, valOurs = 0, valRouted = 0;
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOrig = orig(self + 4);
            return true;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOurs = reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 4)->Release4();
            return true;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            valRouted = VirtualSlot<RefFn>(self + 4, 2)(self + 4);
            return true;
        });
        Compare(ctx, "Release4", a, b, c);
        if (valOrig != valOurs || valOrig != valRouted) {
            ctx.Fail("Release4 (count %u): return original %u, ours %u, via slot %u", count, valOrig, valOurs, valRouted);
        }
    }
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_QueryInterface4, 0x1e2de3)
{
    auto orig = ctx.Original<QueryFn>(0x1e2de3);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.record[0] = Addr(static_cast<ThunkQueryMember>(&cGZPersistDBSerialRecordThunks::QueryInterface4));

    const uint32_t ids[] = {0, 1, 2, 0x051b11bd, 0xc51b11cb, 0xc56b8f08, 0xe56b8f03, 0xffffffffu};
    std::mt19937 rng(0x4f1);
    for (int i = 0; i < 4000; ++i) {
        uint32_t riid = i < 8 ? ids[i] : (i % 3 == 0 ? ids[rng() % 8] : static_cast<uint32_t>(rng()));
        uint16_t count = static_cast<uint16_t>(i % 4 == 0 ? 0xffff : i % 4 == 1 ? 0 : rng());
        Scene s = MakeScene(rng, 0, count);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &extra) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            bool r = orig(self + 4, riid, &out);
            extra = out == reinterpret_cast<void *>(kUnchanged) ? INT64_MIN
                : out == nullptr                                 ? INT64_MIN + 1
                                                                  : Off(out);
            return r;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &extra) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            bool r = reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 4)->QueryInterface4(riid, &out);
            extra = out == reinterpret_cast<void *>(kUnchanged) ? INT64_MIN
                : out == nullptr                                 ? INT64_MIN + 1
                                                                  : Off(out);
            return r;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &extra) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            bool r = VirtualSlot<QueryFn>(self + 4, 0)(self + 4, riid, &out);
            extra = out == reinterpret_cast<void *>(kUnchanged) ? INT64_MIN
                : out == nullptr                                 ? INT64_MIN + 1
                                                                  : Off(out);
            return r;
        });
        Compare(ctx, "QueryInterface4", a, b, c);
    }
}

// The +32 and +48 IUnknown thunks. Each one must reach the primary's AddRef, Release or QueryInterface with the
// same arguments as the original, so the same cases as the +4 thunks run here against those subobjects.
// `slot` is the subobject's IUnknown slot, which the routed run replaces with our thunk.
void CheckRefThunk(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t offset, int slot, ThunkRefMember ours,
                   uint32_t seed)
{
    auto orig = ctx.Original<RefFn>(rva);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    auto &routedTable = offset == 0x20 ? routed.stream : routed.istream;
    routedTable[slot] = Addr(ours);

    std::mt19937 rng(seed);
    const uint16_t edges[] = {0, 1, 2, 3, 0xfffe, 0xffff};
    for (int i = 0; i < 4000; ++i) {
        uint16_t count = i < 6 ? edges[i] : static_cast<uint16_t>(rng() % 3 == 0 ? rng() % 4 : rng());
        Scene s = MakeScene(rng, 0, count);
        uint32_t valOrig = 0, valOurs = 0, valRouted = 0;
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOrig = orig(self + offset);
            return true;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOurs = (reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + offset)->*ours)();
            return true;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            valRouted = VirtualSlot<RefFn>(self + offset, slot)(self + offset);
            return true;
        });
        Compare(ctx, name, a, b, c);
        if (valOrig != valOurs || valOrig != valRouted) {
            ctx.Fail("%s (count %u): return original %u, ours %u, via slot %u", name, count, valOrig, valOurs, valRouted);
        }
    }
}

void CheckQueryThunk(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t offset, int slot, ThunkQueryMember ours,
                     uint32_t seed)
{
    auto orig = ctx.Original<QueryFn>(rva);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    auto &routedTable = offset == 0x20 ? routed.stream : routed.istream;
    routedTable[slot] = Addr(ours);

    const uint32_t ids[] = {0, 1, 2, 0x051b11bd, 0xc51b11cb, 0xc56b8f08, 0xe56b8f03, 0xffffffffu};
    std::mt19937 rng(seed);
    for (int i = 0; i < 4000; ++i) {
        uint32_t riid = i < 8 ? ids[i] : (i % 3 == 0 ? ids[rng() % 8] : static_cast<uint32_t>(rng()));
        uint16_t count = static_cast<uint16_t>(i % 4 == 0 ? 0xffff : i % 4 == 1 ? 0 : rng());
        Scene s = MakeScene(rng, 0, count);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &extra) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            bool r = orig(self + offset, riid, &out);
            extra = out == reinterpret_cast<void *>(kUnchanged) ? INT64_MIN
                : out == nullptr                                 ? INT64_MIN + 1
                                                                  : Off(out);
            return r;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &extra) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            bool r = (reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + offset)->*ours)(riid, &out);
            extra = out == reinterpret_cast<void *>(kUnchanged) ? INT64_MIN
                : out == nullptr                                 ? INT64_MIN + 1
                                                                  : Off(out);
            return r;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &extra) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            bool r = VirtualSlot<QueryFn>(self + offset, slot)(self + offset, riid, &out);
            extra = out == reinterpret_cast<void *>(kUnchanged) ? INT64_MIN
                : out == nullptr                                 ? INT64_MIN + 1
                                                                  : Off(out);
            return r;
        });
        Compare(ctx, name, a, b, c);
    }
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_AddRef32, 0x1dab0a)
{
    CheckRefThunk(ctx, "AddRef32", 0x1dab0a, 0x20, 1, static_cast<ThunkRefMember>(&cGZPersistDBSerialRecordThunks::AddRef32), 0x5a1);
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_Release32, 0x1e3958)
{
    CheckRefThunk(ctx, "Release32", 0x1e3958, 0x20, 2, static_cast<ThunkRefMember>(&cGZPersistDBSerialRecordThunks::Release32),
                  0x5e1);
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_QueryInterface32, 0x1e2deb)
{
    CheckQueryThunk(ctx, "QueryInterface32", 0x1e2deb, 0x20, 0,
                    static_cast<ThunkQueryMember>(&cGZPersistDBSerialRecordThunks::QueryInterface32), 0x5f1);
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_AddRef48, 0x1dab12)
{
    CheckRefThunk(ctx, "AddRef48", 0x1dab12, 0x30, 1, static_cast<ThunkRefMember>(&cGZPersistDBSerialRecordThunks::AddRef48), 0x6a1);
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_Release48, 0x1e3960)
{
    CheckRefThunk(ctx, "Release48", 0x1e3960, 0x30, 2, static_cast<ThunkRefMember>(&cGZPersistDBSerialRecordThunks::Release48),
                  0x6e1);
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_QueryInterface48, 0x1e2df3)
{
    CheckQueryThunk(ctx, "QueryInterface48", 0x1e2df3, 0x30, 0,
                    static_cast<ThunkQueryMember>(&cGZPersistDBSerialRecordThunks::QueryInterface48), 0x6f1);
}

// +48 slot 18: the original returns 0 in al (its shared target is `xor al, al; ret`). Only al is compared, since the
// original leaves the upper bits of eax alone; see the header.
DIFF_TEST(cGZPersistDBSerialRecordThunks_GetError48, 0x1dff06)
{
    using ErrFn = uint8_t(__thiscall *)(void *);
    using ErrMember = uint8_t (cGZPersistDBSerialRecordThunks::*)();
    auto orig = ctx.Original<ErrFn>(0x1dff06);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.istream[18] = Addr(static_cast<ErrMember>(&cGZPersistDBSerialRecordThunks::GetError48));

    std::mt19937 rng(0x6e4);
    for (int i = 0; i < 2000; ++i) {
        Scene s = MakeScene(rng, 0, 0);
        uint8_t valOrig = 0xee, valOurs = 0xee, valRouted = 0xee;
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOrig = orig(self + 48);
            return true;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &) {
            valOurs = reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 48)->GetError48();
            return true;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &) {
            valRouted = VirtualSlot<ErrFn>(self + 48, 18)(self + 48);
            return true;
        });
        Compare(ctx, "GetError48", a, b, c);
        if (valOrig != valOurs || valOrig != valRouted) {
            ctx.Fail("GetError48: al original %u, ours %u, via slot %u", valOrig, valOurs, valRouted);
        }
    }
}

// SetRZCharStr (+32 slot 14). The original builds a cRZString from the text on the stack (game vtable), and calls
// the primary's slot 48 with it. The fake slot 48 below asks the string for its length and data through its own
// vtable, records both and, for a heap buffer, that block's size. After the call the test checks whether the block
// is freed (the original destroys the string): a FreeRecorder runs from the fake to the end of the call.
constexpr int64_t kSetStringEvent = 8;
const void *gTempData = nullptr;
std::optional<difftest::FreeRecorder> gTempFrees;

bool __fastcall FakeSetString(void *self, void * /*edx*/, const cIGZString *value)
{
    (void)self;
    void *str = const_cast<cIGZString *>(value);
    using StrlenFn = uint32_t(__thiscall *)(void *);
    using DataFn = const char *(__thiscall *)(void *);
    const uint32_t len = VirtualSlot<StrlenFn>(str, 7)(str);
    const char *data = VirtualSlot<DataFn>(str, 6)(str);
    int64_t blockSize = 0;
    gTempData = nullptr;
    if (len > 15) { // a heap buffer
        gTempData = data;
        blockSize = static_cast<int64_t>(HeapSize(GetProcessHeap(), 0, const_cast<char *>(data)));
    }
    Push(kSetStringEvent, len, blockSize);
    for (uint32_t i = 0; i < len; ++i) gLog.push_back(static_cast<uint8_t>(data[i]));
    gTempFrees.reset();
    if (gTempData) gTempFrees.emplace(); // from here to the end of the call: is the buffer freed
    return !gRefuse;
}

// -1: no heap buffer was left by the call; 1: the buffer is freed; 0: it is still live (a leak).
int64_t TempFreed()
{
    if (gTempFrees) gTempFrees->Stop();
    int64_t freed = -1;
    if (gTempData != nullptr) freed = gTempFrees && gTempFrees->Freed(gTempData) ? 1 : 0;
    gTempData = nullptr;
    gTempFrees.reset();
    return freed;
}

DIFF_TEST(cGZPersistDBSerialRecordThunks_SetRZCharStr, 0x99dda0)
{
    using SetRZFn = bool(__thiscall *)(void *, const char *);
    using SetRZMember = bool (cGZPersistDBSerialRecordThunks::*)(const char *);
    auto orig = ctx.Original<SetRZFn>(0x99dda0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    plain.primary[48] = reinterpret_cast<void *>(&FakeSetString);
    routed.primary[48] = reinterpret_cast<void *>(&FakeSetString);
    routed.stream[14] = Addr(static_cast<SetRZMember>(&cGZPersistDBSerialRecordThunks::SetRZCharStr));

    const uint32_t edges[] = {0, 1, 15, 16, 17, 31, 32, 255, 256, 257};
    std::mt19937 rng(0x5c1);
    for (int i = 0; i < 3000; ++i) {
        uint32_t len = i < 10 ? edges[i] : i % 4 == 0 ? static_cast<uint32_t>(rng() % 300) : static_cast<uint32_t>(rng() % 20);
        std::string text(len, '\0');
        for (auto &ch : text) ch = static_cast<char>(1 + rng() % 255); // no embedded NULs
        Scene s = MakeWriteScene(rng, 0);
        auto a = Run(plain, s, [&](uint8_t *self, int64_t &extra) {
            bool r = orig(self + 32, text.c_str());
            extra = TempFreed();
            return r;
        });
        auto b = Run(plain, s, [&](uint8_t *self, int64_t &extra) {
            bool r = (reinterpret_cast<cGZPersistDBSerialRecordThunks *>(self + 32)->SetRZCharStr(text.c_str()));
            extra = TempFreed();
            return r;
        });
        auto c = Run(routed, s, [&](uint8_t *self, int64_t &extra) {
            bool r = VirtualSlot<SetRZFn>(self + 32, 14)(self + 32, text.c_str());
            extra = TempFreed();
            return r;
        });
        Compare(ctx, "SetRZCharStr", a, b, c);
    }
}
