// Differential tests for cGZDBSerialRecordAsIGZFile: the primary slots (QueryInterface, Release,
// The unhooked adjustor thunks of the file face (compiler-made, compared in the Faces test): 0x9adb17 (vf4_0), 0x9af9b5 (vf4_2).
// SetRecord) and the cIGZFile slots (Close through Write). The object lives in a world buffer together
// with two fake records and their persist objects, so every pointer the calls make is comparable
// across runs. The fakes serve scripted bytes and sizes, can refuse operations, and log every call
// (kind and arguments, in order). Each function runs three ways: the original, ours called directly,
// and ours through the object's own vtable slot (the rest of the vtable being the original). The three
// must agree on the return value, the event log, the world bytes (object, records, out pointer, read
// destination), the bytes consumed from the script and the bytes written to the sink.
#include "difftest.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZDBSerialRecordAsIGZFile.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

using C = cGZDBSerialRecordAsIGZFile;
using File = cGZDBSerialRecordAsIGZFile::File;

using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using RefFn = uint32_t(__thiscall *)(void *);
using SetRecordFn = bool(__thiscall *)(void *, void *);
using BoolFn = bool(__thiscall *)(void *);
using IntFn = int32_t(__thiscall *)(void *);
using UintFn = uint32_t(__thiscall *)(void *);
using SetUintFn = bool(__thiscall *)(void *, uint32_t);
using IntArgFn = int32_t(__thiscall *)(void *, int32_t);
using SeekOriginFn = int32_t(__thiscall *)(void *, int32_t, uint32_t);
using ReadFn = bool(__thiscall *)(void *, void *, uint32_t);
using ReadCountFn = bool(__thiscall *)(void *, void *, uint32_t *);
using WriteFn = bool(__thiscall *)(void *, const void *, uint32_t);

// World layout (offsets from the start of the buffer).
constexpr size_t kWorld = 0x300;
constexpr size_t kObj = 0x000;      // the object: +0 primary vtable, +4 file vtable, +8 count, +0xc record
constexpr size_t kRecA = 0x100;     // fake record A (vtable, count, position, size)
constexpr size_t kPersistA = 0x140; // A's persist object (+0x40 from A)
constexpr size_t kRecB = 0x180;
constexpr size_t kPersistB = 0x1c0;
constexpr size_t kDst = 0x200;      // 0x40 bytes: read destination or write source
constexpr size_t kOut = 0x280;      // QueryInterface's out pointer
constexpr size_t kLen = 0x284;      // uint32 length passed to ReadWithCount
constexpr uint32_t kUnchanged = 0xFACE0000u;

struct FakeRecord {
    const void *vt;
    uint32_t refs;
    int32_t position;
    int32_t size;
};
static_assert(sizeof(FakeRecord) == 16, "fake record size");

enum Kind : int64_t {
    kObjAddRef = 1,
    kObjDelete,
    kRecAddRef,
    kRecRelease,
    kAsPersist,
    kGetVoid,
    kSetVoid,
    kGetSize,
    kSetSize,
    kGetPosition,
    kSeekAbs,
    kSeekRel,
    kClose,
    kAccessFlags,
};

// Per-run state, set up by Run().
std::vector<int64_t> gEvents;
uint8_t *gBase = nullptr;
std::vector<uint8_t> gScript;
size_t gScriptPos = 0;
std::vector<uint8_t> gSink;
bool gOkGet = true, gOkSet = true, gOkSetSize = true, gOkSeekAbs = true, gOkSeekRel = true;
bool gUnexpected = false;
uint32_t gAccess = 0;

int64_t Off(const void *p)
{
    return static_cast<const uint8_t *>(p) - gBase;
}

void Ev(int64_t kind, int64_t a = 0, int64_t b = 0, int64_t c = 0)
{
    gEvents.push_back(kind);
    gEvents.push_back(a);
    gEvents.push_back(b);
    gEvents.push_back(c);
}

FakeRecord *Rec(void *self)
{
    return static_cast<FakeRecord *>(self);
}

// Object's own AddRef (primary slot 1): counts at +8 as the original does, and records the call.
uint32_t __fastcall RecordObjAddRef(void *self, void *)
{
    Ev(kObjAddRef, Off(self));
    auto *count = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 8);
    return ++*count;
}

// Object's deleting destructor (primary slot 6): records the flags and frees nothing.
void __fastcall RecordObjDelete(void *self, void *, uint32_t flags)
{
    Ev(kObjDelete, Off(self), flags);
}

uint32_t __fastcall FakeRecAddRef(void *self, void *)
{
    Ev(kRecAddRef, Off(self));
    return ++Rec(self)->refs;
}

uint32_t __fastcall FakeRecRelease(void *self, void *)
{
    Ev(kRecRelease, Off(self));
    return --Rec(self)->refs;
}

void *__fastcall FakeAsPersist(void *self, void *)
{
    Ev(kAsPersist, Off(self));
    return static_cast<uint8_t *>(self) + 0x40;
}

bool __fastcall FakeGetVoid(void *self, void *, void *dst, uint32_t n)
{
    Ev(kGetVoid, Off(self), Off(dst), n);
    if (!gOkGet || gScript.size() - gScriptPos < n) return false;
    std::memcpy(dst, gScript.data() + gScriptPos, n);
    gScriptPos += n;
    return true;
}

bool __fastcall FakeSetVoid(void *self, void *, const void *src, uint32_t n)
{
    Ev(kSetVoid, Off(self), Off(src), n);
    if (!gOkSet) return false;
    const auto *p = static_cast<const uint8_t *>(src);
    gSink.insert(gSink.end(), p, p + n);
    return true;
}

int32_t __fastcall FakeGetSize(void *self, void *)
{
    Ev(kGetSize, Off(self));
    return Rec(self)->size;
}

bool __fastcall FakeSetSize(void *self, void *, uint32_t n)
{
    Ev(kSetSize, Off(self), n);
    if (!gOkSetSize) return false;
    Rec(self)->size = static_cast<int32_t>(n);
    return true;
}

int32_t __fastcall FakeGetPosition(void *self, void *)
{
    Ev(kGetPosition, Off(self));
    return Rec(self)->position;
}

bool __fastcall FakeSeekAbs(void *self, void *, int32_t pos)
{
    Ev(kSeekAbs, Off(self), pos);
    if (!gOkSeekAbs) return false;
    Rec(self)->position = pos;
    return true;
}

bool __fastcall FakeSeekRel(void *self, void *, int32_t offset)
{
    Ev(kSeekRel, Off(self), offset);
    if (!gOkSeekRel) return false;
    Rec(self)->position = static_cast<int32_t>(static_cast<uint32_t>(Rec(self)->position) + static_cast<uint32_t>(offset));
    return true;
}

void __fastcall FakePersistClose(void *self, void *)
{
    Ev(kClose, Off(self));
}

uint32_t __fastcall FakeAccessFlags(void *self, void *)
{
    Ev(kAccessFlags, Off(self));
    return gAccess;
}

// Any other slot: flags the run (the test then fails) rather than returning plausible data.
void __fastcall Trap(void *, void *)
{
    gUnexpected = true;
}

template <typename T> void *Fn(T f)
{
    return reinterpret_cast<void *>(f);
}

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// Vtables of the fake record (only the slots the object uses are real) and of its persist object.
struct FakeVtables {
    void *record[57];
    void *persist[7];
    FakeVtables()
    {
        for (auto &p : record) p = Fn(&Trap);
        record[1] = Fn(&FakeRecAddRef);
        record[2] = Fn(&FakeRecRelease);
        record[3] = Fn(&FakeAsPersist);
        record[27] = Fn(&FakeGetVoid);
        record[51] = Fn(&FakeSetVoid);
        record[52] = Fn(&FakeGetSize);
        record[53] = Fn(&FakeSetSize);
        record[54] = Fn(&FakeGetPosition);
        record[55] = Fn(&FakeSeekAbs);
        record[56] = Fn(&FakeSeekRel);
        for (auto &p : persist) p = Fn(&Trap);
        persist[5] = Fn(&FakeAccessFlags);
        persist[6] = Fn(&FakePersistClose);
    }
};

const FakeVtables &Fakes()
{
    static const FakeVtables v;
    return v;
}

// The object's two vtables: copies of the originals with AddRef and the deleting destructor recording.
struct ObjTables {
    void *primary[7];
    void *file[33];
};

ObjTables MakeTables(const difftest::Context &ctx)
{
    ObjTables t{};
    std::memcpy(t.primary, ctx.image.At(C::kVTable), sizeof(t.primary));
    std::memcpy(t.file, ctx.image.At(C::kFileVTable), sizeof(t.file));
    t.primary[1] = Fn(&RecordObjAddRef);
    t.primary[C::kDeleteSlot] = Fn(&RecordObjDelete);
    return t;
}

// One trial's inputs. Random values are kept small so that positions and sizes straddle the
// boundaries the code compares against.
struct RecordState {
    uint32_t refs = 1;
    int32_t position = 0;
    int32_t size = 0;
};

struct Setup {
    std::vector<uint8_t> world;
    uint32_t refs = 1;       // the object's count
    int recordKind = 1;      // 0: no record, 1: A, 2: B
    RecordState a, b;
    bool okGet = true, okSet = true, okSetSize = true, okSeekAbs = true, okSeekRel = true;
    std::vector<uint8_t> script;
    uint32_t access = 0;     // what the persist object's access flags answer
};

int32_t SmallInt(std::mt19937 &rng)
{
    if (rng() % 16 == 0) return static_cast<int32_t>(rng());
    return static_cast<int32_t>(rng() % 80) - 4;
}

Setup MakeSetup(std::mt19937 &rng, uint32_t maxScript)
{
    Setup s;
    s.world.resize(kWorld);
    // Random bytes with top byte 0xC3, so that no byte pattern is mistaken for a world address.
    for (size_t i = 0; i + 4 <= kWorld; i += 4) {
        uint32_t w = (static_cast<uint32_t>(rng()) & 0x00ffffffu) | 0xC3000000u;
        std::memcpy(&s.world[i], &w, sizeof(w));
    }
    uint32_t sentinel = kUnchanged;
    std::memcpy(&s.world[kOut], &sentinel, sizeof(sentinel));
    uint32_t len = rng() % 8 == 0 ? static_cast<uint32_t>(rng()) : rng() % 80;
    std::memcpy(&s.world[kLen], &len, sizeof(len));

    static const uint32_t kRefs[] = {0, 1, 2, 3, 0xffffffffu};
    s.refs = kRefs[rng() % 5];
    s.recordKind = rng() % 6 == 0 ? 0 : (rng() % 4 == 0 ? 2 : 1);
    s.a = {rng() % 4, SmallInt(rng), SmallInt(rng)};
    s.b = {rng() % 4, SmallInt(rng), SmallInt(rng)};
    s.okGet = rng() % 5 != 0;
    s.okSet = rng() % 5 != 0;
    s.okSetSize = rng() % 5 != 0;
    s.okSeekAbs = rng() % 5 != 0;
    s.okSeekRel = rng() % 5 != 0;
    s.access = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
    s.script.resize(rng() % (maxScript + 8));
    for (auto &b : s.script) b = static_cast<uint8_t>(rng());
    return s;
}

struct Outcome {
    int64_t result = 0;
    std::vector<int64_t> events;
    std::vector<uint8_t> world;
    std::vector<uint8_t> sink;
    size_t scriptPos = 0;
    bool unexpected = false;
};

// Writes a pointer word in the world as an offset, so two runs with different buffers compare equal.
void NormalizeWord(std::vector<uint8_t> &world, size_t offset)
{
    uint32_t w;
    std::memcpy(&w, &world[offset], sizeof(w));
    uintptr_t base = reinterpret_cast<uintptr_t>(gBase);
    if (w >= base && w < base + kWorld) {
        w = static_cast<uint32_t>(w - base) | 0x80000000u;
        std::memcpy(&world[offset], &w, sizeof(w));
    }
}

// Runs one call on a fresh world built from the setup and the tables.
template <typename Call> Outcome Run(const ObjTables &t, const Setup &s, Call call)
{
    std::vector<uint8_t> world(s.world);
    gBase = world.data();
    const FakeVtables &fv = Fakes();

    FakeRecord recA{fv.record, s.a.refs, s.a.position, s.a.size};
    FakeRecord recB{fv.record, s.b.refs, s.b.position, s.b.size};
    std::memcpy(gBase + kRecA, &recA, sizeof(recA));
    std::memcpy(gBase + kRecB, &recB, sizeof(recB));
    const void *persist = fv.persist;
    std::memcpy(gBase + kPersistA, &persist, sizeof(persist));
    std::memcpy(gBase + kPersistB, &persist, sizeof(persist));

    C obj{};
    obj.mVTable = const_cast<void **>(t.primary);
    obj.mFileVTable = const_cast<void **>(t.file);
    obj.mRefCount = s.refs;
    obj.mRecord = s.recordKind == 1 ? gBase + kRecA : (s.recordKind == 2 ? gBase + kRecB : nullptr);
    std::memcpy(gBase + kObj, &obj, sizeof(obj));

    gEvents.clear();
    gScript = s.script;
    gScriptPos = 0;
    gSink.clear();
    gUnexpected = false;
    gOkGet = s.okGet;
    gOkSet = s.okSet;
    gOkSetSize = s.okSetSize;
    gOkSeekAbs = s.okSeekAbs;
    gOkSeekRel = s.okSeekRel;
    gAccess = s.access;

    Outcome out;
    out.result = call(gBase);
    out.events = gEvents;
    out.sink = gSink;
    out.scriptPos = gScriptPos;
    out.unexpected = gUnexpected;

    // The vtable words differ between the plain and routed tables, so they are not compared.
    std::memset(world.data() + kObj, 0, 8);
    std::memset(world.data() + kRecA, 0, 4);
    std::memset(world.data() + kRecB, 0, 4);
    std::memset(world.data() + kPersistA, 0, 4);
    std::memset(world.data() + kPersistB, 0, 4);
    NormalizeWord(world, kObj + 0x0c); // the record pointer
    NormalizeWord(world, kOut);        // QueryInterface's out pointer
    out.world = world;
    gBase = nullptr;
    return out;
}

enum class Impl { Orig, Ours, Routed };

// Runs f three ways on the same setup and reports any disagreement.
template <typename F>
void Check(difftest::Context &ctx, const char *name, const ObjTables &plain, const ObjTables &routed, const Setup &s, F f)
{
    Outcome a = Run(plain, s, [&](uint8_t *b) { return f(b, Impl::Orig); });
    Outcome b = Run(plain, s, [&](uint8_t *b) { return f(b, Impl::Ours); });
    Outcome c = Run(routed, s, [&](uint8_t *b) { return f(b, Impl::Routed); });
    ++ctx.cases;
    if (a.result != b.result || a.result != c.result) {
        ctx.Fail("%s: return original %lld, ours %lld, via slot %lld", name, static_cast<long long>(a.result),
                 static_cast<long long>(b.result), static_cast<long long>(c.result));
    }
    if (a.events != b.events || a.events != c.events) {
        ctx.Fail("%s: calls made differ (%zu / %zu / %zu values)", name, a.events.size(), b.events.size(), c.events.size());
    }
    if (a.world != b.world || a.world != c.world) ctx.Fail("%s: object or record state differs", name);
    if (a.sink != b.sink || a.sink != c.sink) ctx.Fail("%s: bytes written differ", name);
    if (a.scriptPos != b.scriptPos || a.scriptPos != c.scriptPos) ctx.Fail("%s: bytes consumed differ", name);
    if (a.unexpected || b.unexpected || c.unexpected) ctx.Fail("%s: an unexpected vtable slot was called", name);
}

int64_t Ret(bool v)
{
    return v ? 1 : 0;
}

constexpr int kTrials = 3000;

// The original's atexit (library code, RVA 0x824cbe) works on its CRT's exit table, which the harness never initialises;
// while the original's GetPath runs, the image's atexit is redirected to this stub (5-byte jmp patched over its entry,
// restored afterwards), which records the handler (as tests/gz_event_system_service.cpp does).
int gAtexitCalls = 0;
void (*gAtexitHandler)() = nullptr;
int __cdecl StubAtexit(void (*handler)())
{
    ++gAtexitCalls;
    gAtexitHandler = handler;
    return 0;
}

struct AtexitRedirect {
    uint8_t *site;
    uint8_t saved[5];
    explicit AtexitRedirect(difftest::Context &ctx) : site(ctx.image.At<uint8_t>(0x824cbe))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StubAtexit) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~AtexitRedirect() { std::memcpy(site, saved, 5); }
};

} // namespace

DIFF_TEST(cGZDBSerialRecordAsIGZFile_QueryInterface, 0x9adad0)
{
    std::mt19937 rng(0x0a1);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.primary[0] = Addr(&C::QueryInterface);
    auto orig = ctx.Original<QueryFn>(0x9adad0);
    const uint32_t riids[] = {1, 0xe0203645u, 0x8a32c0acu, 0, 0xffffffffu, 2, 0x8a32c0adu};
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        uint32_t riid = i % 3 == 0 ? static_cast<uint32_t>(rng()) : riids[rng() % 7];
        Check(ctx, "QueryInterface", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            auto *out = reinterpret_cast<void **>(b + kOut);
            void *obj = b + kObj;
            switch (impl) {
            case Impl::Orig: return Ret(orig(obj, riid, out));
            case Impl::Ours: return Ret(reinterpret_cast<C *>(obj)->QueryInterface(riid, out));
            case Impl::Routed: return Ret(VirtualSlot<QueryFn>(obj, 0)(obj, riid, out));
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_Release, 0x9af990)
{
    std::mt19937 rng(0x0a2);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.primary[2] = Addr(&C::Release);
    routed.primary[C::kSetRecordSlot] = Addr(&C::SetRecord);
    auto orig = ctx.Original<RefFn>(0x9af990);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        Check(ctx, "Release", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *obj = b + kObj;
            switch (impl) {
            case Impl::Orig: return orig(obj);
            case Impl::Ours: return reinterpret_cast<C *>(obj)->Release();
            case Impl::Routed: return VirtualSlot<RefFn>(obj, 2)(obj);
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_SetRecord, 0x9afcd0)
{
    std::mt19937 rng(0x0a3);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.primary[C::kSetRecordSlot] = Addr(&C::SetRecord);
    auto orig = ctx.Original<SetRecordFn>(0x9afcd0);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        // The new record: null, A or B, or the same one that is held already.
        int pick = static_cast<int>(rng() % 4);
        Check(ctx, "SetRecord", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *obj = b + kObj;
            void *record = pick == 0 ? nullptr : (pick == 1 ? b + kRecA : (pick == 2 ? b + kRecB : nullptr));
            if (pick == 3) record = reinterpret_cast<C *>(obj)->mRecord; // same record again (may be null)
            switch (impl) {
            case Impl::Orig: return Ret(orig(obj, record));
            case Impl::Ours: return Ret(reinterpret_cast<C *>(obj)->SetRecord(record));
            case Impl::Routed: return Ret(VirtualSlot<SetRecordFn>(obj, C::kSetRecordSlot)(obj, record));
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_Close, 0x9a8090)
{
    std::mt19937 rng(0x0a5);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[5] = Addr(&File::Close);
    auto orig = ctx.Original<BoolFn>(0x9a8090);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        Check(ctx, "Close", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            switch (impl) {
            case Impl::Orig: return Ret(orig(file));
            case Impl::Ours: return Ret(reinterpret_cast<File *>(file)->Close());
            case Impl::Routed: return Ret(VirtualSlot<BoolFn>(file, 5)(file));
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_Position, 0x9ada80)
{
    std::mt19937 rng(0x0a6);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[6] = Addr(&File::Position);
    auto orig = ctx.Original<IntFn>(0x9ada80);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        Check(ctx, "Position", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            switch (impl) {
            case Impl::Orig: return orig(file);
            case Impl::Ours: return reinterpret_cast<File *>(file)->Position();
            case Impl::Routed: return VirtualSlot<IntFn>(file, 6)(file);
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_Length, 0x9ad730)
{
    std::mt19937 rng(0x0a7);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[7] = Addr(&File::Length);
    auto orig = ctx.Original<UintFn>(0x9ad730);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        Check(ctx, "Length", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            switch (impl) {
            case Impl::Orig: return orig(file);
            case Impl::Ours: return reinterpret_cast<File *>(file)->Length();
            case Impl::Routed: return VirtualSlot<UintFn>(file, 7)(file);
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_SetLength, 0x9b01e0)
{
    std::mt19937 rng(0x0a8);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[8] = Addr(&File::SetLength);
    auto orig = ctx.Original<SetUintFn>(0x9b01e0);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        uint32_t length = i % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 80;
        Check(ctx, "SetLength", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            switch (impl) {
            case Impl::Orig: return Ret(orig(file, length));
            case Impl::Ours: return Ret(reinterpret_cast<File *>(file)->SetLength(length));
            case Impl::Routed: return Ret(VirtualSlot<SetUintFn>(file, 8)(file, length));
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_SeekToBegin, 0x9afbe0)
{
    std::mt19937 rng(0x0a9);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[9] = Addr(&File::SeekToBegin);
    auto orig = ctx.Original<IntFn>(0x9afbe0);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        Check(ctx, "SeekToBegin", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            switch (impl) {
            case Impl::Orig: return orig(file);
            case Impl::Ours: return reinterpret_cast<File *>(file)->SeekToBegin();
            case Impl::Routed: return VirtualSlot<IntFn>(file, 9)(file);
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_SeekToEnd, 0x9afc10)
{
    std::mt19937 rng(0x0aa);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[10] = Addr(&File::SeekToEnd);
    auto orig = ctx.Original<IntFn>(0x9afc10);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        Check(ctx, "SeekToEnd", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            switch (impl) {
            case Impl::Orig: return orig(file);
            case Impl::Ours: return reinterpret_cast<File *>(file)->SeekToEnd();
            case Impl::Routed: return VirtualSlot<IntFn>(file, 10)(file);
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_SeekToRelativePosition, 0x9afc80)
{
    std::mt19937 rng(0x0ab);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[11] = Addr(&File::SeekToRelativePosition);
    auto orig = ctx.Original<IntArgFn>(0x9afc80);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        int32_t offset = SmallInt(rng);
        Check(ctx, "SeekToRelativePosition", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            switch (impl) {
            case Impl::Orig: return orig(file, offset);
            case Impl::Ours: return reinterpret_cast<File *>(file)->SeekToRelativePosition(offset);
            case Impl::Routed: return VirtualSlot<IntArgFn>(file, 11)(file, offset);
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_SeekToPosition, 0x9afc50)
{
    std::mt19937 rng(0x0ac);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[12] = Addr(&File::SeekToPosition);
    auto orig = ctx.Original<IntArgFn>(0x9afc50);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        int32_t position = SmallInt(rng);
        Check(ctx, "SeekToPosition", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            switch (impl) {
            case Impl::Orig: return orig(file, position);
            case Impl::Ours: return reinterpret_cast<File *>(file)->SeekToPosition(position);
            case Impl::Routed: return VirtualSlot<IntArgFn>(file, 12)(file, position);
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_Seek, 0x9af9c0)
{
    std::mt19937 rng(0x0ad);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[13] = Addr(&File::Seek);
    auto orig = ctx.Original<SeekOriginFn>(0x9af9c0);
    for (int i = 0; i < kTrials; ++i) {
        // Origins 0..2 are the cases; anything else falls through to -1.
        Setup s = MakeSetup(rng, 0);
        int32_t offset = SmallInt(rng);
        uint32_t origin = i % 5 == 4 ? static_cast<uint32_t>(rng()) : rng() % 4;
        Check(ctx, "Seek", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            switch (impl) {
            case Impl::Orig: return orig(file, offset, origin);
            case Impl::Ours: return reinterpret_cast<File *>(file)->Seek(offset, origin);
            case Impl::Routed: return VirtualSlot<SeekOriginFn>(file, 13)(file, offset, origin);
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_Read, 0x9ae4e0)
{
    std::mt19937 rng(0x0ae);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[14] = Addr(&File::Read);
    auto orig = ctx.Original<ReadFn>(0x9ae4e0);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0x40);
        uint32_t n = rng() % 0x41;
        Check(ctx, "Read", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            void *dst = b + kDst;
            switch (impl) {
            case Impl::Orig: return Ret(orig(file, dst, n));
            case Impl::Ours: return Ret(reinterpret_cast<File *>(file)->Read(dst, n));
            case Impl::Routed: return Ret(VirtualSlot<ReadFn>(file, 14)(file, dst, n));
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_ReadWithCount, 0x9af930)
{
    std::mt19937 rng(0x0af);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[15] = Addr(&File::ReadWithCount);
    auto orig = ctx.Original<ReadCountFn>(0x9af930);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0x40);
        Check(ctx, "ReadWithCount", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            void *dst = b + kDst;
            auto *length = reinterpret_cast<uint32_t *>(b + kLen);
            switch (impl) {
            case Impl::Orig: return Ret(orig(file, dst, length));
            case Impl::Ours: return Ret(reinterpret_cast<File *>(file)->ReadWithCount(dst, *length));
            case Impl::Routed: return Ret(VirtualSlot<ReadCountFn>(file, 15)(file, dst, length));
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_Write, 0x9b1180)
{
    std::mt19937 rng(0x0b0);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[16] = Addr(&File::Write);
    auto orig = ctx.Original<WriteFn>(0x9b1180);
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        uint32_t n = rng() % 0x41;
        Check(ctx, "Write", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            const void *src = b + kDst;
            switch (impl) {
            case Impl::Orig: return Ret(orig(file, src, n));
            case Impl::Ours: return Ret(reinterpret_cast<File *>(file)->Write(src, n));
            case Impl::Routed: return Ret(VirtualSlot<WriteFn>(file, 16)(file, src, n));
            }
            return 0;
        });
    }
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_WriteWithCount, 0x9b26b0)
{
    std::mt19937 rng(0x0b1);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[17] = Addr(&File::WriteWithCount);
    auto orig = ctx.Original<ReadCountFn>(0x9b26b0);
    int failed = 0, written = 0;
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        uint32_t n = rng() % 0x41;
        std::memcpy(&s.world[kLen], &n, sizeof(n));
        Check(ctx, "WriteWithCount", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            const void *src = b + kDst;
            auto *length = reinterpret_cast<uint32_t *>(b + kLen);
            switch (impl) {
            case Impl::Orig: return Ret(orig(file, const_cast<void *>(src), length));
            case Impl::Ours: return Ret(reinterpret_cast<File *>(file)->WriteWithCount(src, *length));
            case Impl::Routed: return Ret(VirtualSlot<ReadCountFn>(file, 17)(file, const_cast<void *>(src), length));
            }
            return 0;
        });
        failed += s.recordKind != 0 && !s.okSet;
        written += s.recordKind != 0 && s.okSet;
    }
    if (failed < 100 || written < 100) ctx.Fail("WriteWithCount: setup reaches too little (%d failed, %d written)", failed, written);
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_GetOpenModes, 0x9ac800)
{
    std::mt19937 rng(0x0b2);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.file[30] = Addr(&File::GetOpenModes);
    using ModesFn = bool(__thiscall *)(void *, uint32_t *, uint32_t *, uint32_t *);
    auto orig = ctx.Original<ModesFn>(0x9ac800);
    int bits[4] = {0, 0, 0, 0};
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        Check(ctx, "GetOpenModes", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *file = b + kObj + C::kFileOffset;
            auto *out = reinterpret_cast<uint32_t *>(b + kDst);
            switch (impl) {
            case Impl::Orig: return Ret(orig(file, out, out + 1, out + 2));
            case Impl::Ours: return Ret(reinterpret_cast<File *>(file)->GetOpenModes(out, out + 1, out + 2));
            case Impl::Routed: return Ret(VirtualSlot<ModesFn>(file, 30)(file, out, out + 1, out + 2));
            }
            return 0;
        });
        if (s.recordKind != 0) ++bits[s.access & 3];
    }
    if (bits[0] < 100 || bits[1] < 100 || bits[2] < 100 || bits[3] < 100)
        ctx.Fail("GetOpenModes: access flag combinations reached %d/%d/%d/%d", bits[0], bits[1], bits[2], bits[3]);
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_DeletingDestructor, 0x9a7b30)
{
    std::mt19937 rng(0x0b3);
    ObjTables plain = MakeTables(ctx);
    ObjTables routed = plain;
    routed.primary[C::kDeleteSlot] = Addr(&C::DeletingDestructor);
    using DtorFn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<DtorFn>(0x9a7b30);
    // Bit 0 would free the world's buffer: the delete itself is the library's, exercised by the other bits only.
    const uint32_t flagValues[] = {0, 2, 0x100, 0xfffffffeu, 4};
    const void *wantPrimary = ctx.image.At(C::kVTable);
    const void *wantFile = ctx.image.At(C::kFileVTable);
    int closed = 0;
    for (int i = 0; i < kTrials; ++i) {
        Setup s = MakeSetup(rng, 0);
        uint32_t flags = flagValues[rng() % 5];
        Check(ctx, "DeletingDestructor", plain, routed, s, [&](uint8_t *b, Impl impl) -> int64_t {
            void *obj = b + kObj;
            void *ret = nullptr;
            switch (impl) {
            case Impl::Orig: ret = orig(obj, flags); break;
            case Impl::Ours: ret = reinterpret_cast<C *>(obj)->DeletingDestructor(flags); break;
            case Impl::Routed: ret = VirtualSlot<DtorFn>(obj, C::kDeleteSlot)(obj, flags); break;
            }
            // The result is the vtable words the destructor left (the class's own) and that it returns this.
            const void *words[2];
            std::memcpy(words, b + kObj, sizeof(words));
            return (words[0] == wantPrimary ? 1 : 0) | (words[1] == wantFile ? 2 : 0) | (ret == obj ? 4 : 0);
        });
        closed += s.recordKind != 0;
    }
    if (closed < 1000) ctx.Fail("DeletingDestructor: only %d runs with a record", closed);
}

DIFF_TEST(cGZDBSerialRecordAsIGZFile_GetPath1, 0x9ac870)
{
    // The function-local static: the same address, the same constructed string, guard set, for both.
    uint8_t *path = ctx.image.At<uint8_t>(0x10f500c);
    int *guard = ctx.image.At<int>(0x10f502c);
    using PathFn = void *(__thiscall *)(void *);
    auto orig = ctx.Original<PathFn>(0x9ac870);
    uint8_t dummy[0x10] = {};
    auto reset = [&] {
        std::memset(path, 0xcd, 0x20);
        *guard = 0;
    };
    reset();
    void *a;
    {
        AtexitRedirect redirect(ctx);
        a = orig(dummy + 4);
    }
    std::vector<uint8_t> stateA(path, path + 0x20);
    int guardA = *guard;
    void *a2 = orig(dummy + 4); // the second call is the fast path
    std::vector<uint8_t> stateA2(path, path + 0x20);
    reset();
    void *b = reinterpret_cast<File *>(dummy + 4)->GetPath();
    std::vector<uint8_t> stateB(path, path + 0x20);
    int guardB = *guard;
    void *b2 = reinterpret_cast<File *>(dummy + 4)->GetPath();
    std::vector<uint8_t> stateB2(path, path + 0x20);
    reset();
    void *vtable[33];
    std::memcpy(vtable, ctx.image.At(C::kFileVTable), sizeof(vtable));
    vtable[25] = Addr(&File::GetPath);
    void *self[1] = {vtable};
    void *c = VirtualSlot<PathFn>(self, 25)(self);
    std::vector<uint8_t> stateC(path, path + 0x20);
    ++ctx.cases;
    if (a != path || a2 != path || b != path || b2 != path || c != path) ctx.Fail("GetPath: the result is not the static string");
    if (stateA != stateB || stateA != stateC || stateA2 != stateA || stateB2 != stateB) ctx.Fail("GetPath: the string differs");
    if (guardA == 0 || guardA == -1 || guardB == 0 || guardB == -1) ctx.Fail("GetPath: guard original %d, ours %d", guardA, guardB);
    if (gAtexitCalls != 1 || gAtexitHandler != GameAddress<void()>(0xd95310)) ctx.Fail("GetPath: the original registered %d exit handlers", gAtexitCalls);
    // Built: a later call does not rebuild the string.
    path[0x10] ^= 1;
    reinterpret_cast<File *>(dummy + 4)->GetPath();
    ++ctx.cases;
    if (path[0x10] != (stateB[0x10] ^ 1)) ctx.Fail("GetPath: a second call rebuilt the string");
    path[0x10] ^= 1;
}

// ---- The faces: every slot of our two tables against the image's, on the same objects ----

DIFF_TEST(cGZDBSerialRecordAsIGZFile_Faces, 0xe5fbb0)
{
    std::mt19937 rng(0xface2);
    ObjTables imageT{}, ours{};
    std::memcpy(imageT.primary, ctx.image.At(C::kVTable), sizeof(imageT.primary));
    std::memcpy(imageT.file, ctx.image.At(C::kFileVTable), sizeof(imageT.file));
    std::memcpy(ours.primary, C::OwnVTable(), sizeof(ours.primary));
    std::memcpy(ours.file, C::OwnFileVTable(), sizeof(ours.file));
    // Ours reach the object through its own vtable (ours too), so the runs use a count of 2 or more and flags without bit 0.
    auto check2 = [&](const char *name, const Setup &s, auto call) {
        Outcome a = Run(imageT, s, [&](uint8_t *b) { return call(b, imageT.primary, imageT.file); });
        Outcome b = Run(ours, s, [&](uint8_t *b) { return call(b, ours.primary, ours.file); });
        ++ctx.cases;
        if (a.result != b.result) ctx.Fail("faces %s: result image %lld, ours %lld", name, static_cast<long long>(a.result), static_cast<long long>(b.result));
        if (a.events != b.events) ctx.Fail("faces %s: calls made differ", name);
        if (a.world != b.world) ctx.Fail("faces %s: state differs", name);
        if (a.sink != b.sink || a.scriptPos != b.scriptPos) ctx.Fail("faces %s: bytes differ", name);
        if (a.unexpected || b.unexpected) ctx.Fail("faces %s: an unexpected vtable slot was called", name);
    };
    using V0 = void *(__thiscall *)(void *);
    using U0 = uint32_t(__thiscall *)(void *);
    using B0 = bool(__thiscall *)(void *);
    using B3 = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    using B1 = bool(__thiscall *)(void *, uint32_t);
    using B2 = bool(__thiscall *)(void *, uint32_t, uint32_t);
    using PV1 = void(__thiscall *)(void *, uint32_t);
    using PV2 = void(__thiscall *)(void *, uint32_t, uint32_t);
    using CS0 = const char *(__thiscall *)(void *);
    auto word = [](bool b) { return static_cast<int64_t>(b ? 1 : 0); };
    for (int i = 0; i < 300; ++i) {
        Setup s = MakeSetup(rng, 0x40);
        s.refs = 2 + rng() % 3;
        uint32_t arg = rng();
        uint32_t smallLen = rng() % 0x41; // the counted write copies as many bytes as the count says
        std::memcpy(&s.world[kLen], &smallLen, sizeof(smallLen));
        uint32_t flags = (rng() % 3 == 0 ? 0x100u : 0u) | (rng() % 2 ? 2u : 0u);
        int pick = static_cast<int>(rng() % 3);
        uint32_t riid = arg % 3 == 0 ? C::kGZIID_cIGZFile : (arg % 3 == 1 ? C::kGZIID_Self : arg);
        using P = void **;
        check2("primary 0", s, [&](uint8_t *b, P p, P) -> int64_t {
            return word(reinterpret_cast<QueryFn>(p[0])(b + kObj, riid, reinterpret_cast<void **>(b + kOut)));
        });
        check2("primary 1", s, [&](uint8_t *b, P p, P) -> int64_t { return reinterpret_cast<U0>(p[1])(b + kObj); });
        check2("primary 2", s, [&](uint8_t *b, P p, P) -> int64_t { return reinterpret_cast<U0>(p[2])(b + kObj); });
        check2("primary 3", s, [&](uint8_t *b, P p, P) -> int64_t {
            void *rec = pick == 0 ? nullptr : (pick == 1 ? b + kRecA : b + kRecB);
            return word(reinterpret_cast<SetRecordFn>(p[3])(b + kObj, rec));
        });
        check2("primary 4", s, [&](uint8_t *b, P p, P) -> int64_t {
            void *r = reinterpret_cast<V0>(p[4])(b + kObj);
            return r ? Off(r) : -1;
        });
        check2("primary 5", s, [&](uint8_t *b, P p, P) -> int64_t { return Off(reinterpret_cast<V0>(p[5])(b + kObj)); });
        check2("primary 6", s, [&](uint8_t *b, P p, P) -> int64_t { reinterpret_cast<PV1>(p[6])(b + kObj, flags); return 0; });
        auto f = [&](const char *name, int slot, auto call) {
            check2(name, s, [&, slot](uint8_t *b, P, P q) -> int64_t { return call(b + kObj + C::kFileOffset, q[slot], b); });
        };
        f("file 0", 0, [&](void *file, void *fn, uint8_t *b) -> int64_t {
            return word(reinterpret_cast<QueryFn>(fn)(file, riid, reinterpret_cast<void **>(b + kOut)));
        });
        f("file 1", 1, [&](void *file, void *fn, uint8_t *) -> int64_t { return reinterpret_cast<U0>(fn)(file); });
        f("file 2", 2, [&](void *file, void *fn, uint8_t *) -> int64_t { return reinterpret_cast<U0>(fn)(file); });
        f("file 3", 3, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B3>(fn)(file, arg, arg >> 3, 7)); });
        f("file 4", 4, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B0>(fn)(file)); });
        f("file 5", 5, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B0>(fn)(file)); });
        f("file 6", 6, [&](void *file, void *fn, uint8_t *) -> int64_t { return reinterpret_cast<U0>(fn)(file); });
        f("file 7", 7, [&](void *file, void *fn, uint8_t *) -> int64_t { return reinterpret_cast<U0>(fn)(file); });
        f("file 8", 8, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B1>(fn)(file, arg & 0x3f)); });
        f("file 9", 9, [&](void *file, void *fn, uint8_t *) -> int64_t { return reinterpret_cast<U0>(fn)(file); });
        f("file 10", 10, [&](void *file, void *fn, uint8_t *) -> int64_t { return reinterpret_cast<U0>(fn)(file); });
        f("file 11", 11, [&](void *file, void *fn, uint8_t *) -> int64_t { return reinterpret_cast<IntArgFn>(fn)(file, static_cast<int32_t>(arg % 90) - 5); });
        f("file 12", 12, [&](void *file, void *fn, uint8_t *) -> int64_t { return reinterpret_cast<IntArgFn>(fn)(file, static_cast<int32_t>(arg % 90) - 5); });
        f("file 13", 13, [&](void *file, void *fn, uint8_t *) -> int64_t { return reinterpret_cast<SeekOriginFn>(fn)(file, static_cast<int32_t>(arg % 90) - 5, (arg >> 8) % 4); });
        f("file 14", 14, [&](void *file, void *fn, uint8_t *b) -> int64_t { return word(reinterpret_cast<ReadFn>(fn)(file, b + kDst, arg % 0x41)); });
        f("file 15", 15, [&](void *file, void *fn, uint8_t *b) -> int64_t { return word(reinterpret_cast<ReadCountFn>(fn)(file, b + kDst, reinterpret_cast<uint32_t *>(b + kLen))); });
        f("file 16", 16, [&](void *file, void *fn, uint8_t *b) -> int64_t { return word(reinterpret_cast<WriteFn>(fn)(file, b + kDst, arg % 0x41)); });
        f("file 17", 17, [&](void *file, void *fn, uint8_t *b) -> int64_t { return word(reinterpret_cast<ReadCountFn>(fn)(file, b + kDst, reinterpret_cast<uint32_t *>(b + kLen))); });
        f("file 18", 18, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B0>(fn)(file)); });
        f("file 19", 19, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B0>(fn)(file)); });
        f("file 20", 20, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B0>(fn)(file)); });
        f("file 21", 21, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B1>(fn)(file, 0)); });
        f("file 22", 22, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B0>(fn)(file)); });
        f("file 23", 23, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B2>(fn)(file, arg, arg >> 4)); });
        f("file 24", 24, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B2>(fn)(file, arg, arg >> 4)); });
        f("file 26", 26, [&](void *file, void *fn, uint8_t *) -> int64_t { reinterpret_cast<PV1>(fn)(file, 0); return 0; });
        f("file 27", 27, [&](void *file, void *fn, uint8_t *) -> int64_t { return static_cast<int64_t>(reinterpret_cast<uintptr_t>(reinterpret_cast<CS0>(fn)(file))); });
        f("file 28", 28, [&](void *file, void *fn, uint8_t *) -> int64_t { reinterpret_cast<PV1>(fn)(file, 0); return 0; });
        f("file 29", 29, [&](void *file, void *fn, uint8_t *) -> int64_t { reinterpret_cast<PV1>(fn)(file, 0); return 0; });
        f("file 30", 30, [&](void *file, void *fn, uint8_t *b) -> int64_t {
            auto *out = reinterpret_cast<uint32_t *>(b + kDst);
            return word(reinterpret_cast<bool(__thiscall *)(void *, uint32_t *, uint32_t *, uint32_t *)>(fn)(file, out, out + 1, out + 2));
        });
        f("file 31", 31, [&](void *file, void *fn, uint8_t *) -> int64_t { reinterpret_cast<PV2>(fn)(file, 0, 0); return 0; });
        f("file 32", 32, [&](void *file, void *fn, uint8_t *) -> int64_t { return word(reinterpret_cast<B2>(fn)(file, arg, arg >> 5)); });
    }
}
