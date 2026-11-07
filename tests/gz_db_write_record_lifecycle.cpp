// Differential tests for the write record's constructor, deleting destructor and the slots that reach the temporary file
// or the segment (src/gz/cGZDBWriteRecordPackedFile.h): Construct 0x9a70f0, DeletingDestructor 0x9a7b80, SetFieldVoid
// 0x9afe70, SetSize 0x9b0360, Close 0x9aa070, FlushToSegment 0x9b2550. (The slots that only read the buffer are in
// tests/gz_db_write_record_packed_file.cpp; the tables' other slots in tests/gz_db_write_record_faces.cpp.)
//
// The object lives in one fixed arena together with the fake segment, the fake sink (the temporary file) and the chunk
// buffer, so pointers compare as arena offsets. The record's buffer is a real std::vector<char> on the host heap: the
// original's allocator calls (operator new 0x824632 and its sized delete 0x824624) are stood in with the host's on both
// sides, so either side may free what the other allocated, and the buffer's size and bytes are compared (not its address).
// The record's own virtual calls go through a copy of the image's tables with fakes in GetAccessFlags, QueryChild,
// SetChild and SetAccessFlags; the sink is a cRZFile stood in by a fake table (its constructor, 0x1c0c07, installs it); the
// unique-name function 0x1c255e, the path delete 0x1c2e25 and the allocator's Malloc/Free are stood in on both sides.
// Each function runs on a fresh copy of the same scene: the original, ours called directly, and ours through our own
// rzface tables (called on the same object). Results, the calls the fakes saw and the object's state must match.
#include "difftest.h"
#include "code_patch.h"
#include "game/vtable.h"
#include "gz/cGZDBWriteRecordPackedFile.h"
#include "gz/cIGZString.h"
#include "rz/GeneralAllocator.h"
#include "rz/PathFileOps.h"
#include "rz/RZMemory.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

#include <cstdarg>

#include <climits>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr size_t kBytes = 0x5000;
constexpr size_t kSegObj = 0x100;   // the fake segment: primary vtable at +0, the +4 face's at +4
constexpr size_t kSinkObj = 0x140;  // the sink that already exists
constexpr size_t kPathObj = 0x180;  // what the sink's GetPath returns
constexpr size_t kSrc = 0x200;      // source bytes for SetFieldVoid (0x100)
constexpr size_t kKeyIn = 0x300;    // the key handed to Construct
constexpr size_t kChunk = 0x1000;   // the 0x4000-byte chunk buffer the allocator fake hands out
constexpr uint32_t kLimit = 2000000;

alignas(16) uint8_t gArena[kBytes];

using U32Fn = uint32_t(__thiscall *)(void *);
using BoolVoidU32Fn = bool(__thiscall *)(void *, const void *, uint32_t);
using BoolU32Fn = bool(__thiscall *)(void *, uint32_t);
using VoidFn = void(__thiscall *)(void *);
using BoolU32U32Fn = bool(__thiscall *)(void *, uint32_t, uint32_t);
using CtorFn = void *(__thiscall *)(void *, uint32_t, uint32_t, const void *, void *, uint32_t);
using DtorFn = void *(__thiscall *)(void *, uint32_t);

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

std::vector<char> &Vec() { return *reinterpret_cast<std::vector<char> *>(gArena + 0x4c); }

enum Kind : int64_t {
    kAccess = 1, kQueryChild, kSetChild, kSetAccess, kSegQuery, kSegAddRef, kSegRelease, kReadAt, kWriteAt, kSinkOpen,
    kSinkIsOpen, kSinkClose, kSinkPos, kSinkLen, kSinkSetLen, kSinkSeek, kSinkRead, kSinkWrite, kSinkPath, kSinkDelete,
    kTempName, kFileCtor, kPathDelete, kMalloc, kFree, kObjectFree, kChildClose, kChildAbort
};
std::vector<int64_t> gLog;
bool gVecLive = false;
void *gCreated = nullptr; // the sink the code made (heap block, constructed by the stand-in)

// Where a pointer is: an arena offset, the live buffer (0x100000 + offset), the created sink (-2), anything else (-9).
int64_t Loc(const void *p)
{
    auto q = static_cast<const uint8_t *>(p);
    if (q == nullptr) return 0;
    if (q >= gArena && q < gArena + kBytes) return q - gArena;
    if (q == gCreated) return -2;
    if (gVecLive) {
        auto &v = Vec();
        const char *b = v.data();
        if (b != nullptr && q >= reinterpret_cast<const uint8_t *>(b) && q <= reinterpret_cast<const uint8_t *>(b) + v.capacity())
            return 0x100000 + (q - reinterpret_cast<const uint8_t *>(b));
    }
    return -9;
}

void Push(int64_t kind, int64_t a = 0, int64_t b = 0, int64_t c = 0)
{
    gLog.insert(gLog.end(), {kind, a, b, c});
}

uint64_t Sum(const void *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    auto q = static_cast<const uint8_t *>(p);
    if (q == nullptr) return 0;
    for (size_t i = 0; i < n; ++i) h = (h ^ q[i]) * 1099511628211ull;
    return h;
}

// The scripted answers of the current run.
struct Script {
    uint32_t access = 0;
    bool queryOk = true;
    bool tempOk = true, openOk = true, isOpen = true, mallocNull = false;
    uint32_t sinkRet = 0;
    bool sinkBool = true;
    uint32_t writeMask = ~0u, readMask = ~0u, readAtMask = ~0u, writeAtMask = ~0u; // bit k: call k succeeds (k >= 32: yes)
    int writes = 0, reads = 0, readAts = 0, writeAts = 0;
};
Script gS;

bool Bit(uint32_t mask, int k) { return k >= 32 || (mask >> k & 1) != 0; }

void Fill(void *dst, uint32_t offset, uint32_t n)
{
    auto p = static_cast<uint8_t *>(dst);
    if (p == nullptr) return;
    for (uint32_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>((offset + i) * 7 + 0x5a);
}

// ---- Fakes on the record's +4 face ----
uint32_t __fastcall FakeAccess(void *self, void *)
{
    Push(kAccess, Loc(self));
    return gS.access;
}
bool __fastcall FakeQueryChild(void *self, void *, uint32_t riid, void **out)
{
    Push(kQueryChild, Loc(self), riid);
    if (gS.queryOk) *out = gArena + kSegObj + 4;
    return gS.queryOk;
}
void __fastcall FakeSetChild(void *self, void *, void *child)
{
    Push(kSetChild, Loc(self), Loc(child));
}
uint32_t __fastcall FakeSetAccess(void *self, void *, uint32_t value)
{
    Push(kSetAccess, Loc(self), value);
    return 0;
}

// ---- The fake segment (and the child the constructor asks) ----
void *gSegPrim[64];
void *gSegSub[32];
bool __fastcall SegQuery(void *self, void *, uint32_t riid, void **out)
{
    Push(kSegQuery, Loc(self), riid);
    if (gS.queryOk) *out = self;
    return gS.queryOk;
}
uint32_t __fastcall SegAddRef(void *self, void *)
{
    Push(kSegAddRef, Loc(self));
    return 1;
}
uint32_t __fastcall SegRelease(void *self, void *)
{
    Push(kSegRelease, Loc(self));
    return 1;
}
// The child's slots the base record's Close and Abort call (with the record as the argument).
bool __fastcall SegChildClose(void *self, void *, void *record)
{
    Push(kChildClose, Loc(self), Loc(record));
    return gS.sinkBool;
}
bool __fastcall SegChildAbort(void *self, void *, void *record)
{
    Push(kChildAbort, Loc(self), Loc(record));
    return gS.sinkBool;
}
bool __fastcall SegReadAt(void *self, void *, void *dst, uint32_t offset, uint32_t n)
{
    Push(kReadAt, Loc(self), Loc(dst), (static_cast<int64_t>(offset) << 32) | n);
    bool ok = Bit(gS.readAtMask, gS.readAts++);
    if (ok) Fill(dst, offset, n);
    return ok;
}
bool __fastcall SegWriteAt(void *self, void *, const void *src, uint32_t offset, uint32_t n)
{
    Push(kWriteAt, Loc(self), Loc(src), (static_cast<int64_t>(offset) << 32) | n);
    gLog.push_back(static_cast<int64_t>(Sum(src, n) & 0x7fffffffffffffffull));
    return Bit(gS.writeAtMask, gS.writeAts++);
}

// ---- The sink (cRZFile slots) ----
void *gSinkTable[40];
bool __fastcall SinkOpen(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Push(kSinkOpen, Loc(self), a, (static_cast<int64_t>(b) << 32) | c);
    return gS.openOk;
}
bool __fastcall SinkIsOpen(void *self, void *)
{
    Push(kSinkIsOpen, Loc(self));
    return gS.isOpen;
}
bool __fastcall SinkClose(void *self, void *)
{
    Push(kSinkClose, Loc(self));
    return true;
}
uint32_t __fastcall SinkPos(void *self, void *)
{
    Push(kSinkPos, Loc(self));
    return gS.sinkRet;
}
uint32_t __fastcall SinkLen(void *self, void *)
{
    Push(kSinkLen, Loc(self));
    return gS.sinkRet;
}
bool __fastcall SinkSetLen(void *self, void *, uint32_t n)
{
    Push(kSinkSetLen, Loc(self), n);
    return gS.sinkBool;
}
uint32_t __fastcall SinkSeek(void *self, void *, uint32_t pos)
{
    Push(kSinkSeek, Loc(self), pos);
    return gS.sinkRet;
}
bool __fastcall SinkRead(void *self, void *, void *dst, uint32_t n)
{
    Push(kSinkRead, Loc(self), Loc(dst), n);
    bool ok = Bit(gS.readMask, gS.reads++);
    if (ok) Fill(dst, 0x40 * gS.reads, n);
    return ok;
}
bool __fastcall SinkWrite(void *self, void *, const void *src, uint32_t n)
{
    Push(kSinkWrite, Loc(self), Loc(src), n);
    gLog.push_back(static_cast<int64_t>(Sum(src, n) & 0x7fffffffffffffffull));
    return Bit(gS.writeMask, gS.writes++);
}
const void *__fastcall SinkPath(void *self, void *)
{
    Push(kSinkPath, Loc(self));
    return gArena + kPathObj;
}
void *__fastcall SinkDelete(void *self, void *, uint32_t flags)
{
    Push(kSinkDelete, Loc(self), flags);
    return self;
}

// ---- Stand-ins for the image's functions ----
bool __cdecl StandTempName(const cIGZString *name, int mode)
{
    Push(kTempName, mode, static_cast<int64_t>(Sum(name->ToChar(), std::strlen(name->ToChar())) & 0x7fffffffffffffffull));
    return gS.tempOk;
}
cRZFile *__fastcall StandFileCtor(cRZFile *self, void *, const cIGZString &path)
{
    Push(kFileCtor, static_cast<int64_t>(Sum(path.ToChar(), std::strlen(path.ToChar())) & 0x7fffffffffffffffull));
    *reinterpret_cast<void **>(self) = gSinkTable;
    gCreated = self;
    return self;
}
bool __cdecl StandPathDelete(const cIGZString *path)
{
    Push(kPathDelete, Loc(path));
    return true;
}
void *__fastcall StandMalloc(GeneralAllocator *, void *, uint32_t bytes, uint32_t flags)
{
    Push(kMalloc, bytes, flags);
    return gS.mallocNull ? nullptr : gArena + kChunk;
}
void __fastcall StandFree(GeneralAllocator *, void *, void *p)
{
    Push(kFree, Loc(p));
}
void __cdecl StandObjectFree(void *p)
{
    Push(kObjectFree, Loc(p));
}
// cRZString::Sprintf: the image's formatting reaches CRT state the harness has not initialised (the call spins).
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    self->mString = text;
    return self;
}
void *__cdecl HostNew(size_t n) { return ::operator new(n); }
void __cdecl HostDelete(void *p, size_t) { ::operator delete(p); }

struct Stands {
    shape::CodePatch imageNew{0x824632, reinterpret_cast<const void *>(&HostNew)};
    shape::CodePatch imageDelete{0x824624, reinterpret_cast<const void *>(&HostDelete)};
    shape::StandIn tempName{0x1c255e, &MakeTempFile, &StandTempName};
    shape::StandIn fileCtor{0x1c0c07, static_cast<cRZFile *(cRZFile::*)(const cIGZString &)>(&cRZFile::Construct), &StandFileCtor};
    shape::StandIn pathDelete{0x1c2e25, &PathDelete, &StandPathDelete};
    shape::StandIn allocMalloc{0x1c70cc, &GeneralAllocator::Malloc, &StandMalloc};
    shape::StandIn allocFree{0x1c6c5a, &GeneralAllocator::Free, &StandFree};
    shape::StandIn objectFree{0x1be3a1, &RZMemory::Free, &StandObjectFree};
    shape::StandIn sprintfStand{0x1ad6f7, &cRZString::Sprintf, &StandSprintf};
};

void InitFakes()
{
    gSinkTable[3] = reinterpret_cast<void *>(&SinkOpen);
    gSinkTable[4] = reinterpret_cast<void *>(&SinkIsOpen);
    gSinkTable[5] = reinterpret_cast<void *>(&SinkClose);
    gSinkTable[6] = reinterpret_cast<void *>(&SinkPos);
    gSinkTable[7] = reinterpret_cast<void *>(&SinkLen);
    gSinkTable[8] = reinterpret_cast<void *>(&SinkSetLen);
    gSinkTable[12] = reinterpret_cast<void *>(&SinkSeek);
    gSinkTable[14] = reinterpret_cast<void *>(&SinkRead);
    gSinkTable[16] = reinterpret_cast<void *>(&SinkWrite);
    gSinkTable[25] = reinterpret_cast<void *>(&SinkPath);
    gSinkTable[33] = reinterpret_cast<void *>(&SinkDelete);
    gSegPrim[56] = reinterpret_cast<void *>(&SegReadAt);
    gSegPrim[58] = reinterpret_cast<void *>(&SegWriteAt);
    gSegSub[0] = reinterpret_cast<void *>(&SegQuery);
    gSegSub[1] = reinterpret_cast<void *>(&SegAddRef);
    gSegSub[2] = reinterpret_cast<void *>(&SegRelease);
    gSegSub[24] = reinterpret_cast<void *>(&SegChildClose);
    gSegSub[25] = reinterpret_cast<void *>(&SegChildAbort);
}

// The record's callback tables: the image's with the fakes in the +4 face.
struct Tables {
    void *primary[65];
    void *sub[21];
};

Tables MakeTables(const difftest::Context &ctx)
{
    Tables t{};
    std::memcpy(t.primary, ctx.image.At(cGZDBWriteRecordPackedFile::kVTable), sizeof(t.primary));
    std::memcpy(t.sub, ctx.image.At(cGZDBWriteRecordPackedFile::kSubVTable), sizeof(t.sub));
    t.sub[5] = reinterpret_cast<void *>(&FakeAccess);
    t.sub[15] = reinterpret_cast<void *>(&FakeQueryChild);
    t.sub[16] = reinterpret_cast<void *>(&FakeSetChild);
    t.sub[20] = reinterpret_cast<void *>(&FakeSetAccess);
    return t;
}

// ---- Scenes ----
struct Scene {
    std::vector<uint8_t> image; // the arena's random bytes
    Script script;
    bool object = true;         // build the record's fields (else the arena stays random: for the constructor)
    std::vector<char> data;     // the buffer's bytes
    size_t capacity = 0;
    uint32_t position = 0;
    bool sink = false;
    uint8_t tried = 0;
    bool buildBase = false; // construct the base with the original constructor first (the destructor's scenes)
    bool childFile = false; // ... with the fake segment as the child
};

CtorFn gBuild = nullptr;

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
    std::vector<char> data;
    size_t capacity = 0;
};

// Which pointers to normalise in the object's bytes after the call.
struct RunFlags {
    bool vectorLives = true;   // the record still holds its buffer after the call
    bool zeroVTables = false;  // the vtable words may differ (the image's against ours)
    bool keepCapacity = false; // compare the buffer's capacity (Close keeps it)
};

template <typename Call> Outcome Run(const Tables &t, const Scene &s, const RunFlags &flags, Call call, const void *ostream, const void *istream)
{
    std::memcpy(gArena, s.image.data(), kBytes);
    const void *segPrim = gSegPrim;
    const void *segSub = gSegSub;
    std::memcpy(gArena + kSegObj, &segPrim, sizeof(segPrim));
    std::memcpy(gArena + kSegObj + 4, &segSub, sizeof(segSub));
    const void *sinkTable = gSinkTable;
    std::memcpy(gArena + kSinkObj, &sinkTable, sizeof(sinkTable));
    gVecLive = false;
    gS = s.script;
    if (s.buildBase) {
        // The original constructor (an empty record): the base members the destructor releases are then real.
        gBuild(gArena, 0, 0, gArena + kKeyIn, s.childFile ? static_cast<void *>(gArena + kSegObj) : nullptr, 0);
    }
    const void *primary = t.primary;
    const void *sub = t.sub;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &sub, sizeof(sub));
    std::memcpy(gArena + 0x20, &ostream, sizeof(ostream));
    std::memcpy(gArena + 0x30, &istream, sizeof(istream));
    if (s.object) {
        if (!s.buildBase) new (gArena + 0x4c) std::vector<char>();
        Vec().reserve(s.capacity);
        Vec().assign(s.data.begin(), s.data.end());
        gVecLive = true;
        std::memcpy(gArena + 0x58, &s.position, 4);
        void *sink = s.sink ? static_cast<void *>(gArena + kSinkObj) : nullptr;
        std::memcpy(gArena + 0x5c, &sink, sizeof(sink));
        gArena[0x60] = s.tried;
    }
    gS = s.script;
    gS.writes = gS.reads = gS.readAts = gS.writeAts = 0;
    gLog.clear();
    gCreated = nullptr;

    Outcome out;
    out.result = call(gArena);
    out.log = gLog;
    if (flags.vectorLives) {
        gVecLive = true;
        out.data.assign(Vec().begin(), Vec().end());
        out.capacity = Vec().capacity();
        gVecLive = false;
        Vec().~vector();
    }
    gVecLive = false;
    uint32_t pointer = 0;
    int64_t sink = Loc(*reinterpret_cast<void **>(gArena + 0x5c));
    std::memset(gArena + 0x4c, 0, 12);
    std::memcpy(gArena + 0x5c, &sink, 4);
    (void)pointer;
    if (flags.zeroVTables) {
        std::memset(gArena, 0, 8);
        std::memset(gArena + 0x20, 0, 4);
        std::memset(gArena + 0x30, 0, 4);
    }
    out.bytes.assign(gArena, gArena + kBytes);
    if (!flags.keepCapacity) out.capacity = 0;
    if (gCreated != nullptr) {
        ::operator delete(gCreated);
        gCreated = nullptr;
    }
    return out;
}

void Compare(difftest::Context &ctx, const char *name, const Outcome &a, const Outcome &b, const char *how)
{
    ++ctx.cases;
    if (a.result != b.result) {
        ctx.Fail("%s (%s): result original %llx, ours %llx", name, how, static_cast<unsigned long long>(a.result),
                 static_cast<unsigned long long>(b.result));
    }
    if (a.log != b.log) {
        ctx.Fail("%s (%s): calls made differ (%zu / %zu values)", name, how, a.log.size(), b.log.size());
        for (size_t i = 0; i < a.log.size() && i < b.log.size(); ++i) {
            if (a.log[i] != b.log[i]) {
                ctx.Fail("  first difference at value %zu: %lld against %lld", i, static_cast<long long>(a.log[i]), static_cast<long long>(b.log[i]));
                std::string la, lb;
                for (size_t k = 0; k + 3 < a.log.size() && k < 400; k += 4) la += " [" + std::to_string(a.log[k]) + "," + std::to_string(a.log[k + 1]) + "," + std::to_string(a.log[k + 2]) + "," + std::to_string(a.log[k + 3]) + "]";
                for (size_t k = 0; k + 3 < b.log.size() && k < 400; k += 4) lb += " [" + std::to_string(b.log[k]) + "," + std::to_string(b.log[k + 1]) + "," + std::to_string(b.log[k + 2]) + "," + std::to_string(b.log[k + 3]) + "]";
                ctx.Fail("  original:%s", la.c_str());
                ctx.Fail("  ours:    %s", lb.c_str());
                break;
            }
        }
    }
    if (a.data != b.data) ctx.Fail("%s (%s): buffer contents differ (%zu / %zu bytes)", name, how, a.data.size(), b.data.size());
    if (a.capacity != b.capacity) ctx.Fail("%s (%s): buffer capacity differs (%zu / %zu)", name, how, a.capacity, b.capacity);
    if (a.bytes != b.bytes) ctx.Fail("%s (%s): object state differs", name, how);
}

uint32_t PickWord(std::mt19937 &rng)
{
    static const uint32_t edges[] = {0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff, 0xffff, 0x10000};
    if (rng() % 3 == 0) return edges[rng() % 8];
    return static_cast<uint32_t>(rng());
}

uint32_t PickAccess(std::mt19937 &rng)
{
    static const uint32_t flags[] = {0, 0, 1, 2, 0x80000000, 0xffffffff};
    return flags[rng() % 6];
}

uint32_t PickMask(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return ~0u;
    case 1: return ~(1u << (rng() % 5));
    case 2: return static_cast<uint32_t>(rng());
    default: return 0;
    }
}

void FillScript(std::mt19937 &rng, Scene &s)
{
    Script &c = s.script;
    c.access = PickAccess(rng);
    c.queryOk = rng() % 5 != 0;
    c.tempOk = rng() % 5 != 0;
    c.openOk = rng() % 5 != 0;
    c.isOpen = rng() % 3 != 0;
    c.mallocNull = rng() % 12 == 0;
    c.sinkRet = PickWord(rng);
    c.sinkBool = rng() % 3 != 0;
    c.writeMask = PickMask(rng);
    c.readMask = PickMask(rng);
    c.readAtMask = PickMask(rng);
    c.writeAtMask = PickMask(rng);
}

void MakeBuffer(std::mt19937 &rng, Scene &s, uint32_t maxSize)
{
    size_t size = rng() % 4 == 0 ? 0 : rng() % (maxSize + 1);
    s.data.resize(size);
    for (auto &b : s.data) b = static_cast<char>(rng());
    s.capacity = size + (rng() % 3 == 0 ? 0 : rng() % 0x80);
}

cGZDBWriteRecordPackedFile *Obj(void *p) { return static_cast<cGZDBWriteRecordPackedFile *>(p); }

void *const *Table(const void *p) { return static_cast<void *const *>(const_cast<void *>(p)); }

// One scene through every way of calling the function: variants[0] is the original.
struct Variant {
    const char *how;
    std::function<uint64_t(uint8_t *)> call;
    bool zeroVTables = false;
};

void RunAll(difftest::Context &ctx, const char *name, const Tables &t, const Scene &s, RunFlags flags, const std::vector<Variant> &variants)
{
    const void *ostream = ctx.image.At(cGZDBWriteRecordPackedFile::kOStreamVTable);
    const void *istream = ctx.image.At(cGZDBWriteRecordPackedFile::kIStreamVTable);
    Outcome base = Run(t, s, flags, variants[0].call, ostream, istream);
    for (size_t i = 1; i < variants.size(); ++i) {
        Outcome other = Run(t, s, flags, variants[i].call, ostream, istream);
        Compare(ctx, name, base, other, variants[i].how);
    }
}

} // namespace

// ---- SetFieldVoid ----

DIFF_TEST(cGZDBWriteRecordPackedFile_SetFieldVoid, 0x9afe70)
{
    InitFakes();
    Stands stands;
    auto orig = ctx.Original<BoolVoidU32Fn>(0x9afe70);
    Tables t = MakeTables(ctx);
    void *const *own = Table(cGZDBWriteRecordPackedFile::OwnVTable());
    void *const *ownRecord = Table(cGZDBWriteRecordPackedFile::OwnRecordVTable());
    std::mt19937 rng(0x9afe70);
    int spilled = 0, spillFailed = 0, sinkWrites = 0, appended = 0, closed = 0, big = 0;
    for (int i = 0; i < 1500; ++i) {
        Scene s = MakeScene(rng);
        FillScript(rng, s);
        s.script.access = rng() % 6 == 0 ? 0 : 1;
        s.sink = rng() % 5 == 0;
        MakeBuffer(rng, s, 0x120);
        s.tried = rng() % 6 == 0;
        uint32_t size = static_cast<uint32_t>(s.data.size());
        uint32_t n;
        switch (rng() % 5) {
        case 0: n = 0; break;
        case 1: n = 0x3e + rng() % 4; break; // around the byte-by-byte limit (0x40)
        case 2: n = rng() % 0x41; break;
        default: n = rng() % 0x90; break;
        }
        switch (rng() % 7) {
        case 0: case 1: case 2: s.position = size; break;                                   // an append
        case 3: s.position = rng() % (size + 0x21); break;                                  // inside or just past
        case 4: s.position = kLimit - n - (rng() % 3) + 1; break;                           // around the file limit (>=)
        case 5: s.position = kLimit - n - 1 - (rng() % 0x40); break;
        default: s.position = size + (rng() % 3); break;
        }
        bool limit = static_cast<int32_t>(s.position + n) >= static_cast<int32_t>(kLimit);
        if (limit) ++big;
        auto call = [&](bool ours, int how) {
            return [&, ours, how](uint8_t *self) -> uint64_t {
                const void *source = self + kSrc;
                if (!ours) return orig(self, source, n);
                if (how == 0) return Obj(self)->SetFieldVoid(source, n);
                if (how == 1) return reinterpret_cast<BoolVoidU32Fn>(own[cGZDBWriteRecordPackedFile::kSetFieldVoidSlot])(self, source, n);
                return reinterpret_cast<BoolVoidU32Fn>(ownRecord[9])(self + 4, source, n);
            };
        };
        RunFlags flags;
        std::vector<Variant> variants = {{"original", call(false, 0)}, {"ours", call(true, 0)}, {"ours, primary slot 51", call(true, 1)},
                                         {"ours, +4 slot 9", call(true, 2)}};
        RunAll(ctx, "SetFieldVoid", t, s, flags, variants);
        bool reaches = s.script.access != 0 && !s.sink && limit && !s.tried;
        spilled += reaches && s.script.tempOk && s.script.openOk;
        spillFailed += reaches && !(s.script.tempOk && s.script.openOk);
        sinkWrites += s.script.access != 0 && s.sink;
        appended += s.script.access != 0 && !s.sink && !limit && s.position == size && n < 0x40;
        closed += s.script.access == 0;
    }
    if (spilled < 30 || spillFailed < 30 || sinkWrites < 100 || appended < 100 || closed < 100 || big < 100)
        ctx.Fail("SetFieldVoid: setup reaches too little (%d spilled, %d failed spills, %d to a sink, %d appended, %d closed)", spilled,
                 spillFailed, sinkWrites, appended, closed);
}

// ---- SetSize ----

DIFF_TEST(cGZDBWriteRecordPackedFile_SetSize, 0x9b0360)
{
    InitFakes();
    Stands stands;
    auto orig = ctx.Original<BoolU32Fn>(0x9b0360);
    Tables t = MakeTables(ctx);
    void *const *own = Table(cGZDBWriteRecordPackedFile::OwnVTable());
    void *const *ownRecord = Table(cGZDBWriteRecordPackedFile::OwnRecordVTable());
    std::mt19937 rng(0x9b0360);
    int grown = 0, shrunk = 0, sunk = 0, closed = 0;
    for (int i = 0; i < 1200; ++i) {
        Scene s = MakeScene(rng);
        FillScript(rng, s);
        s.script.access = rng() % 6 == 0 ? 0 : 1;
        s.sink = rng() % 4 == 0;
        MakeBuffer(rng, s, 0x200);
        s.position = rng() % 0x210;
        s.tried = rng() % 2;
        uint32_t size = static_cast<uint32_t>(s.data.size());
        uint32_t n;
        switch (rng() % 5) {
        case 0: n = 0; break;
        case 1: n = size; break;
        case 2: n = size + 1; break;
        case 3: n = 70000 + rng() % 4; break;
        default: n = rng() % 0x400; break;
        }
        auto call = [&](int how) {
            return [&, how](uint8_t *self) -> uint64_t {
                if (how == 0) return orig(self, n);
                if (how == 1) return Obj(self)->SetSize(n);
                if (how == 2) return reinterpret_cast<BoolU32Fn>(own[cGZDBWriteRecordPackedFile::kSetSizeSlot])(self, n);
                return reinterpret_cast<BoolU32Fn>(ownRecord[11])(self + 4, n);
            };
        };
        RunAll(ctx, "SetSize", t, s, RunFlags(),
               {{"original", call(0)}, {"ours", call(1)}, {"ours, primary slot 53", call(2)}, {"ours, +4 slot 11", call(3)}});
        closed += s.script.access == 0;
        sunk += s.script.access != 0 && s.sink;
        grown += s.script.access != 0 && !s.sink && n > size;
        shrunk += s.script.access != 0 && !s.sink && n < size;
    }
    if (closed < 100 || sunk < 100 || grown < 100 || shrunk < 100)
        ctx.Fail("SetSize: setup reaches too little (%d closed, %d sink, %d grown, %d shrunk)", closed, sunk, grown, shrunk);
}

// ---- Close ----

DIFF_TEST(cGZDBWriteRecordPackedFile_Close, 0x9aa070)
{
    InitFakes();
    Stands stands;
    auto orig = ctx.Original<VoidFn>(0x9aa070);
    Tables t = MakeTables(ctx);
    void *const *own = Table(cGZDBWriteRecordPackedFile::OwnVTable());
    std::mt19937 rng(0x9aa070);
    int sunkOpen = 0, sunkShut = 0, buffered = 0;
    for (int i = 0; i < 600; ++i) {
        Scene s = MakeScene(rng);
        FillScript(rng, s);
        s.sink = rng() % 2;
        MakeBuffer(rng, s, 0x200);
        s.position = rng() % 0x210;
        s.tried = rng() % 2;
        auto call = [&](int how) {
            return [&, how](uint8_t *self) -> uint64_t {
                if (how == 0) orig(self);
                else if (how == 1) Obj(self)->Close();
                else reinterpret_cast<VoidFn>(own[cGZDBWriteRecordPackedFile::kCloseSlot])(self);
                return 0;
            };
        };
        RunFlags flags;
        flags.keepCapacity = true; // clearing keeps the memory
        RunAll(ctx, "Close", t, s, flags, {{"original", call(0)}, {"ours", call(1)}, {"ours, primary slot 62", call(2)}});
        sunkOpen += s.sink && s.script.isOpen;
        sunkShut += s.sink && !s.script.isOpen;
        buffered += !s.sink && !s.data.empty();
    }
    if (sunkOpen < 80 || sunkShut < 80 || buffered < 80)
        ctx.Fail("Close: setup reaches too little (%d open sinks, %d shut sinks, %d buffers)", sunkOpen, sunkShut, buffered);
}

// ---- FlushToSegment ----

DIFF_TEST(cGZDBWriteRecordPackedFile_FlushToSegment, 0x9b2550)
{
    InitFakes();
    Stands stands;
    auto orig = ctx.Original<BoolU32U32Fn>(0x9b2550);
    Tables t = MakeTables(ctx);
    void *const *own = Table(cGZDBWriteRecordPackedFile::OwnVTable());
    std::mt19937 rng(0x9b2550);
    int viaSink = 0, viaBuffer = 0, failedRead = 0, failedWrite = 0, noQuery = 0, closed = 0;
    for (int i = 0; i < 1500; ++i) {
        Scene s = MakeScene(rng);
        FillScript(rng, s);
        s.script.access = rng() % 8 == 0 ? 0 : 1;
        s.script.queryOk = rng() % 8 != 0;
        s.sink = rng() % 2;
        MakeBuffer(rng, s, 0x100);
        s.position = rng() % 0x110;
        uint32_t offset = rng() % 4 == 0 ? 0 : static_cast<uint32_t>(rng());
        static const uint32_t sizes[] = {0, 1, 0x3fff, 0x4000, 0x4001, 0x7fff, 0x8000, 0x8001, 0xc000, 0x10000};
        uint32_t size;
        if (s.sink) size = rng() % 3 == 0 ? static_cast<uint32_t>(rng() % 0x20000) : sizes[rng() % 10];
        else size = rng() % 3 == 0 ? static_cast<uint32_t>(s.data.size()) : rng() % 0x140;
        auto call = [&](int how) {
            return [&, how](uint8_t *self) -> uint64_t {
                if (how == 0) return orig(self, offset, size);
                if (how == 1) return Obj(self)->FlushToSegment(offset, size);
                return reinterpret_cast<BoolU32U32Fn>(own[cGZDBWriteRecordPackedFile::kFlushToSegmentSlot])(self, offset, size);
            };
        };
        RunAll(ctx, "FlushToSegment", t, s, RunFlags(), {{"original", call(0)}, {"ours", call(1)}, {"ours, primary slot 64", call(2)}});
        closed += s.script.access == 0;
        noQuery += s.script.access != 0 && !s.script.queryOk;
        bool reaches = s.script.access != 0 && s.script.queryOk;
        viaSink += reaches && s.sink;
        viaBuffer += reaches && !s.sink;
        failedRead += reaches && s.sink && s.script.readMask != ~0u;
        failedWrite += reaches && s.sink && s.script.writeAtMask != ~0u;
    }
    if (closed < 80 || noQuery < 80 || viaSink < 300 || viaBuffer < 300 || failedRead < 100 || failedWrite < 100)
        ctx.Fail("FlushToSegment: setup reaches too little (%d closed, %d no query, %d sink, %d buffer, %d failed reads, %d failed writes)",
                 closed, noQuery, viaSink, viaBuffer, failedRead, failedWrite);
}

// ---- Construct ----

DIFF_TEST(cGZDBWriteRecordPackedFile_Construct, 0x9a70f0)
{
    InitFakes();
    Stands stands;
    auto orig = ctx.Original<CtorFn>(0x9a70f0);
    Tables t = MakeTables(ctx);
    std::mt19937 rng(0x9a70f0);
    int empty = 0, smallRec = 0, bigRec = 0, bigDone = 0, noChild = 0, noQuery = 0;
    for (int i = 0; i < 500; ++i) {
        Scene s = MakeScene(rng);
        FillScript(rng, s);
        s.object = false;
        s.script.readAtMask = rng() % 2 ? ~0u : PickMask(rng);
        uint32_t offset = rng() % 3 == 0 ? 0 : static_cast<uint32_t>(rng() % 0x100000);
        uint32_t size;
        bool file = rng() % 5 != 0;
        if (i % 25 == 7) size = kLimit + 1 + (i % 3) * 0x3000;               // a bigRec record: through the temporary file
        else if (i % 25 == 8) size = kLimit + (i % 2);                       // at the limit
        else if (i % 6 == 0) size = 0;
        else if (i % 6 == 1) size = 1 + rng() % 0x40;
        else size = rng() % 0x2000;
        if (i % 25 == 7) {
            s.script.tempOk = rng() % 4 != 0;
            s.script.openOk = rng() % 4 != 0;
            s.script.writeMask = rng() % 2 ? ~0u : PickMask(rng);
            s.script.readAtMask = rng() % 2 ? ~0u : PickMask(rng);
            s.script.queryOk = rng() % 4 != 0;
        }
        const uint8_t *keyAt = nullptr;
        (void)keyAt;
        auto call = [&](int how) {
            return [&, how](uint8_t *self) -> uint64_t {
                void *seg = file ? static_cast<void *>(self + kSegObj) : nullptr;
                const void *key = self + kKeyIn;
                if (how == 0) orig(self, offset, size, key, seg, 0);
                else Obj(self)->Construct(offset, size, static_cast<const cGZPersistResourceKey *>(key), seg, 0);
                return 0;
            };
        };
        RunFlags flags;
        flags.zeroVTables = true; // the image's tables against ours
        RunAll(ctx, "Construct", t, s, flags, {{"original", call(0)}, {"ours", call(1)}});
        empty += size == 0;
        smallRec += size != 0 && size <= kLimit;
        bigRec += size > kLimit;
        bigDone += size > kLimit && file && s.script.tempOk && s.script.openOk;
        noChild += !file;
        noQuery += file && !s.script.queryOk;
    }
    if (empty < 50 || smallRec < 150 || bigRec < 15 || bigDone < 3 || noChild < 50 || noQuery < 30)
        ctx.Fail("Construct: setup reaches too little (%d empty, %d smallRec, %d bigRec, %d bigRec through a file, %d without a child, %d refusing)", empty,
                 smallRec, bigRec, bigDone, noChild, noQuery);
}

// ---- DeletingDestructor ----

DIFF_TEST(cGZDBWriteRecordPackedFile_DeletingDestructor, 0x9a7b80)
{
    InitFakes();
    Stands stands;
    auto orig = ctx.Original<DtorFn>(0x9a7b80);
    gBuild = ctx.Original<CtorFn>(0x9a70f0);
    Tables t = MakeTables(ctx);
    void *const *ownRecord = Table(cGZDBWriteRecordPackedFile::OwnRecordVTable());
    void *const *ownOStream = Table(cGZDBWriteRecordPackedFile::OwnOStreamVTable());
    void *const *ownIStream = Table(cGZDBWriteRecordPackedFile::OwnIStreamVTable());
    std::mt19937 rng(0x9a7b80);
    int withSink = 0, withShutSink = 0, freed = 0, kept = 0, withChild = 0;
    for (int i = 0; i < 800; ++i) {
        Scene s = MakeScene(rng);
        FillScript(rng, s);
        s.buildBase = true;
        s.childFile = rng() % 2;
        s.sink = rng() % 2;
        MakeBuffer(rng, s, 0x100);
        s.position = rng() % 0x110;
        s.tried = rng() % 2;
        uint32_t flags = rng() % 8;
        auto call = [&](int how) {
            return [&, how](uint8_t *self) -> uint64_t {
                if (how == 0) orig(self, flags);
                else if (how == 1) Obj(self)->DeletingDestructor(flags);
                else if (how == 2) reinterpret_cast<DtorFn>(ownRecord[19])(self + 4, flags);
                else if (how == 3) reinterpret_cast<DtorFn>(ownOStream[21])(self + 0x20, flags);
                else reinterpret_cast<DtorFn>(ownIStream[21])(self + 0x30, flags);
                return 0;
            };
        };
        RunFlags dtorFlags;
        dtorFlags.vectorLives = false;
        dtorFlags.zeroVTables = true;
        RunAll(ctx, "DeletingDestructor", t, s, dtorFlags,
               {{"original", call(0)}, {"ours", call(1)}, {"ours, +4 slot 19", call(2)}, {"ours, ostream slot 21", call(3)},
                {"ours, istream slot 21", call(4)}});
        withSink += s.sink && s.script.isOpen;
        withShutSink += s.sink && !s.script.isOpen;
        freed += (flags & 1) && !(flags & 4);
        kept += !(flags & 1) || (flags & 4);
        withChild += s.childFile;
    }
    if (withSink < 100 || withShutSink < 100 || freed < 100 || kept < 100 || withChild < 100)
        ctx.Fail("DeletingDestructor: setup reaches too little (%d open sinks, %d shut sinks, %d freed, %d kept, %d with a child)", withSink,
                 withShutSink, freed, kept, withChild);
}

// ---- The four faces, slot by slot ----
//
// Every slot of the image's tables against ours on the same scene (a real base from the original constructor, the fakes behind
// it); the slots the tests above already reach through the tables (destructors, SetFieldVoid, SetSize, Close, FlushToSegment)
// are covered here too, with other arguments. Each slot gets (pointer, 3, 0, 0) on the stack whatever its arity: the callee
// pops its own and the caller puts the stack pointer back. Slots that take a string or another object (nothing to hand them),
// the destructors and the +4 face's slots the test fakes are left out. The answer is compared as wide as the slot's type.

namespace {

uint32_t CallSlot(void *fn, void *self, uint32_t a, uint32_t b)
{
    uint32_t result = 0;
    __asm {
        mov edx, a
        mov eax, b
        mov esi, fn
        mov ecx, self
        mov edi, esp
        push 0
        push 0
        push eax
        push edx
        call esi
        mov esp, edi
        mov result, eax
    }
    return result;
}

enum Width { kFull, kByte, kNone };

struct FaceSpec {
    const char *name;
    int count;
    uint32_t offset;       // the face's offset in the object
    const void *image;     // the table the original is called through (a copy with the fakes where the scene needs them)
    const void *own;
    std::vector<int> skip;
    std::vector<int> full; // slots whose answer is a dword
    std::vector<int> none; // slots that answer nothing
};

bool In(const std::vector<int> &v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

void RunFace(difftest::Context &ctx, const Tables &t, const FaceSpec &f, uint32_t seed)
{
    std::mt19937 rng(seed);
    void *const *image = Table(f.image);
    void *const *own = Table(f.own);
    int runs = 0;
    for (int slot = 0; slot < f.count; ++slot) {
        if (In(f.skip, slot)) continue;
        Width width = In(f.none, slot) ? kNone : In(f.full, slot) ? kFull : kByte;
        for (int i = 0; i < 40; ++i) {
            Scene s = MakeScene(rng);
            FillScript(rng, s);
            s.script.access = rng() % 5 == 0 ? 0 : 1;
            s.buildBase = true;
            s.childFile = rng() % 2;
            s.sink = rng() % 4 == 0;
            MakeBuffer(rng, s, 0x80);
            s.position = rng() % 0x90;
            s.tried = rng() % 2;
            auto call = [&](void *fn) {
                return [&, fn](uint8_t *self) -> uint64_t {
                    uint32_t a = slot == 0 ? 3 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self + kChunk));
                    uint32_t b = slot == 0 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self + kChunk + 0x40)) : 3;
                    uint32_t r = CallSlot(fn, self + f.offset, a, b);
                    return width == kFull ? r : width == kByte ? (r & 0xff) : 0;
                };
            };
            char label[96];
            std::snprintf(label, sizeof(label), "%s slot %d", f.name, slot);
            RunAll(ctx, label, t, s, RunFlags(), {{"original", call(image[slot])}, {"ours", call(own[slot])}});
            ++runs;
        }
    }
    if (runs < 500) ctx.Fail("%s: only %d runs", f.name, runs);
}

} // namespace

DIFF_TEST(cGZDBWriteRecordPackedFile_Faces, 0x9a7b80)
{
    InitFakes();
    Stands stands;
    gBuild = ctx.Original<CtorFn>(0x9a70f0);
    Tables t = MakeTables(ctx);
    const void *ostream = ctx.image.At(cGZDBWriteRecordPackedFile::kOStreamVTable);
    const void *istream = ctx.image.At(cGZDBWriteRecordPackedFile::kIStreamVTable);
    RunFace(ctx, t, {"primary", 65, 0, t.primary, cGZDBWriteRecordPackedFile::OwnVTable(), {2, 24, 26, 48, 50, 59, 61}, {1, 3, 52, 54, 60}, {62, 63}}, 1);
    RunFace(ctx, t, {"record (+4)", 21, 4, t.sub, cGZDBWriteRecordPackedFile::OwnRecordVTable(), {2, 5, 15, 16, 19, 20}, {1, 3, 10, 12, 18}, {4}}, 2);
    RunFace(ctx, t, {"ostream (+0x20)", 24, 0x20, ostream, cGZDBWriteRecordPackedFile::OwnOStreamVTable(), {2, 15, 16, 19, 21},
                     {1, 16, 20}, {3}}, 3);
    RunFace(ctx, t, {"istream (+0x30)", 24, 0x30, istream, cGZDBWriteRecordPackedFile::OwnIStreamVTable(), {2, 15, 16, 19, 21}, {1, 16, 20}, {}}, 4);
}
