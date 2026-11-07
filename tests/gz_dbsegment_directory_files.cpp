// Differential tests for the cGZDBSegmentDirectoryFiles and cGZDBSDFRecord slots reimplemented so far (field
// maps in src/gz/cGZDBSegmentDirectoryFiles.h and src/gz/cGZDBSDFRecord.h). Each object lives in one fixed
// arena, so pointers compare equal between runs. Each run starts from the same random image and gets copies
// of the vtables whose AddRef, deleting-destructor, string and stream slots record their calls. Results,
// recorded calls and the arena's bytes must match between the original, our function called directly, and
// our function called through a vtable slot.
#include "difftest.h"
#include "game/vtable.h"
#include "gz/cGZDBSDFRecord.h"
#include "gz/cGZDBSegmentDirectoryFiles.h"

#include <cstring>
#include <random>
#include <vector>

class cIGZString;

namespace {

using RefFn = uint32_t(__thiscall *)(void *);
using VoidFn = void(__thiscall *)(void *);
using VoidPtrFn = void(__thiscall *)(void *, void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using BoolFn = bool(__thiscall *)(void *);
using BoolU32Fn = bool(__thiscall *)(void *, uint32_t);
using BoolU32U32Fn = bool(__thiscall *)(void *, uint32_t, uint32_t);
using BoolPtrFn = bool(__thiscall *)(void *, const void *);
using BoolI32Fn = bool(__thiscall *)(void *, int32_t);
using I32Fn = int32_t(__thiscall *)(void *);
using VoidU32Fn = void(__thiscall *)(void *, uint32_t);
using VoidU32U32Fn = void(__thiscall *)(void *, uint32_t, uint32_t);
using U32Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
using U32Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using U32Fn3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

using QueryMember = bool (cGZDBSegmentDirectoryFiles::*)(uint32_t, void **);
using ReleaseMember = uint32_t (cGZDBSegmentDirectoryFiles::*)();
using VoidMember = void (cGZDBSegmentDirectoryFiles::*)();
using VoidStrMember = void (cGZDBSegmentDirectoryFiles::*)(cIGZString *);
using ViewBoolMember = bool (DirectorySegmentView::*)() const;
using ViewU8U8Member = bool (DirectorySegmentView::*)(uint8_t, uint8_t);
using ViewIdMember = bool (DirectorySegmentView::*)(const uint32_t *);
using DirBytesMember = void (cGZDBSegmentDirectoryFiles::*)(uint8_t, uint8_t);
using DirByteMember = void (cGZDBSegmentDirectoryFiles::*)(uint8_t);
using DirObjMember = bool (cGZDBSegmentDirectoryFiles::*)(void *);
using DirU32U32Member = uint32_t (cGZDBSegmentDirectoryFiles::*)(uint32_t, uint32_t);
using DirU32x3Member = uint32_t (cGZDBSegmentDirectoryFiles::*)(uint32_t, uint32_t, uint32_t);
using ViewVoidMember = void (DirectorySegmentView::*)();
using ViewPathMember = void (DirectorySegmentView::*)(cIGZString *);
using ViewU32Member = uint32_t (DirectorySegmentView::*)(uint32_t);

using RecQueryMember = bool (cGZDBSDFRecord::*)(uint32_t, void **);
using RecRefMember = uint32_t (cGZDBSDFRecord::*)();
using ViewI32Member = int32_t (cGZDBSDFRecordSerialView::*)();
using ViewBoolI32Member = bool (cGZDBSDFRecordSerialView::*)(int32_t);
using ViewBoolU32Member = bool (cGZDBSDFRecordSerialView::*)(uint32_t);
using ViewPtrMember = void *(cGZDBSDFRecordSerialView::*)();
using RecI32Member = int32_t (cGZDBSDFRecord::*)();
using RecBoolPtrU32Member = bool (cGZDBSDFRecord::*)(void *, uint32_t);
using RecBoolCPtrU32Member = bool (cGZDBSDFRecord::*)(const void *, uint32_t);
using RecU32PtrU32Member = uint32_t (cGZDBSDFRecord::*)(void *, uint32_t);
using RecU32CPtrU32Member = uint32_t (cGZDBSDFRecord::*)(const void *, uint32_t);
using RecPosMember = bool (cGZDBSDFRecord::*)(int32_t *);
using RecSeekToMember = bool (cGZDBSDFRecord::*)(uint32_t, uint32_t);

using PtrFn = void *(__thiscall *)(void *);
using BoolPtrU32Fn = bool(__thiscall *)(void *, void *, uint32_t);
using BoolCPtrU32Fn = bool(__thiscall *)(void *, const void *, uint32_t);
using U32PtrU32Fn = uint32_t(__thiscall *)(void *, void *, uint32_t);
using U32CPtrU32Fn = uint32_t(__thiscall *)(void *, const void *, uint32_t);
using BoolPosFn = bool(__thiscall *)(void *, int32_t *);

constexpr size_t kBytes = 0x200;
constexpr size_t kStr = 0x1c0;   // fake output string object (vtable at +0), directory tests
constexpr size_t kDst = 0x1e8;   // the dword the pointer-taking calls read or write
constexpr size_t kHeapCap = 64;
constexpr uint32_t kUnchangedOut = 0xfeedf00d;

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// The object, fixed in memory so that pointers into it are the same in every run.
alignas(16) uint8_t gArena[kBytes];
uint8_t gHeap[kHeapCap];

enum Kind : int64_t {
    kAddRef = 1,
    kDelete = 3,
    kFromChar = 6,
    kPosition = 7,
    kSeekAbs = 8,
    kSeekRel = 9,
    kSetLength = 10,
    kSlot32 = 11,   // the primary's slot 32, called by Vf31
    kSlot35 = 12,   // the primary's slot 35, called by Vf36
    kSlot45 = 13,   // the subobject's slot 45, called by Vf44
    kBool8 = 14,    // the argument object's slot 8 (a bool)
    kEmbed16 = 15,  // the embedded object's slot 16
    kObjRelease = 16,
    kLength = 17,
    kReadBytes = 18,
    kReadCounted = 19,
    kWriteBytes = 20,
    kWriteCounted = 21,
    kSeekTo = 22,
};
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

// Recorders for the copied vtables. They take ecx and edx as __thiscall does, and pop their stack arguments.
uint32_t __fastcall RecordAddRef(void *self, void *)
{
    Push(kAddRef, Off(self), 0);
    return 1;
}

void __fastcall RecordDelete(void *self, void *)
{
    Push(kDelete, Off(self), 0);
}

// The record subobject's deleting destructor takes the flags as one stack argument (the serial's Release passes 1).
void __fastcall RecordDeleteWithFlags(void *self, void *, uint32_t flags)
{
    Push(kDelete, Off(self), flags);
}

// cIGZString slot 3: records the length and every character read.
void __fastcall RecordFromChar(void *self, void *, const char *src, uint32_t len)
{
    Push(kFromChar, Off(self), len);
    for (uint32_t i = 0; i < len; ++i) gLog.push_back(static_cast<uint8_t>(src[i]));
}

// The stream's answers are fixed per trial, so the three runs of a trial see the same values.
int32_t gPosRet = 0;
int32_t gSeekRet = 0;
bool gSetRet = false;

int32_t __fastcall RecordPosition(void *self, void *)
{
    Push(kPosition, Off(self), 0);
    return gPosRet;
}

int32_t __fastcall RecordSeekAbs(void *self, void *, int32_t pos)
{
    Push(kSeekAbs, Off(self), pos);
    return gSeekRet;
}

int32_t __fastcall RecordSeekRel(void *self, void *, int32_t offset)
{
    Push(kSeekRel, Off(self), offset);
    return gSeekRet;
}

bool __fastcall RecordSetLength(void *self, void *, uint32_t size)
{
    Push(kSetLength, Off(self), size);
    return gSetRet;
}

// Fakes for the directory tests: the slots called on the primary (32, 35) and the subobject (45) record their
// arguments and return gSlotRet; the argument objects (slots 1, 2, 8) and the embedded object (slot 16) record too.
uint32_t gSlotRet = 0;
bool gBool8 = false;

uint32_t __fastcall RecordSlot32(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Push(kSlot32, Off(self), a);
    gLog.push_back(b);
    gLog.push_back(c);
    return gSlotRet;
}

uint32_t __fastcall RecordSlot35(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Push(kSlot35, Off(self), a);
    gLog.push_back(b);
    gLog.push_back(c);
    return gSlotRet;
}

uint32_t __fastcall RecordSlot45(void *self, void *, uint32_t a, uint32_t b)
{
    Push(kSlot45, Off(self), a);
    gLog.push_back(b);
    return gSlotRet;
}

bool __fastcall RecordBool8(void *self, void *)
{
    Push(kBool8, Off(self), 0);
    return gBool8;
}

void __fastcall RecordObjRelease(void *self, void *)
{
    Push(kObjRelease, Off(self), 0);
}

void __fastcall RecordEmbed16(void *self, void *, void *arg)
{
    Push(kEmbed16, Off(self), static_cast<int64_t>(reinterpret_cast<uintptr_t>(arg)));
}

// The vtables the fakes live in: the output string (slot 3) and the stream (slots 6, 8, 11, 12).
// An argument object's table (slots 1, 2, 8) and the embedded object's table (slot 16).
void *gObjTable[9];
void *gEmbedTable[17];
uint32_t gLenRet = 0;
uint32_t gCountOut = 0;
uint8_t gReadRet = 0;
uint8_t gWriteRet = 0;
int32_t gSeekToRet = 0;

// The stream's Length (slot 7) and the byte-moving slots. Only the first eight bytes of a buffer are written, so
// a random size cannot run past the arena; the bytes written are the same on both sides of every trial.
uint32_t __fastcall RecordLength(void *self, void *)
{
    Push(kLength, Off(self), 0);
    return gLenRet;
}

void FillBytes(void *data, uint32_t size)
{
    uint8_t *p = static_cast<uint8_t *>(data);
    uint32_t n = size < 8 ? size : 8;
    for (uint32_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(0xa0 + i);
}

uint8_t __fastcall RecordReadBytes(void *self, void *, void *data, uint32_t size)
{
    Push(kReadBytes, Off(data), size);
    FillBytes(data, size);
    (void)self;
    return gReadRet;
}

void __fastcall RecordReadCounted(void *self, void *, void *data, uint32_t *count)
{
    Push(kReadCounted, Off(data), *count);
    FillBytes(data, *count);
    (void)self;
    *count = gCountOut;
}

uint8_t __fastcall RecordWriteBytes(void *self, void *, const void *data, uint32_t size)
{
    Push(kWriteBytes, Off(data), size);
    (void)self;
    return gWriteRet;
}

void __fastcall RecordWriteCounted(void *self, void *, const void *data, uint32_t *count)
{
    Push(kWriteCounted, Off(data), *count);
    (void)self;
    *count = gCountOut;
}

int32_t __fastcall RecordSeekTo(void *self, void *, int32_t offset, int32_t whence)
{
    Push(kSeekTo, offset, whence);
    (void)self;
    return gSeekToRet;
}

// The vtables the fakes live in: the output string (slot 3) and the stream (slots 6, 7, 8, 11 to 17).
void *gStrTable[4];
void *gStreamTable[20];

void InitFakes()
{
    gObjTable[1] = reinterpret_cast<void *>(&RecordAddRef);
    gObjTable[2] = reinterpret_cast<void *>(&RecordObjRelease);
    gObjTable[8] = reinterpret_cast<void *>(&RecordBool8);
    gEmbedTable[16] = reinterpret_cast<void *>(&RecordEmbed16);
    gStrTable[3] = reinterpret_cast<void *>(&RecordFromChar);
    gStreamTable[6] = reinterpret_cast<void *>(&RecordPosition);
    gStreamTable[7] = reinterpret_cast<void *>(&RecordLength);
    gStreamTable[8] = reinterpret_cast<void *>(&RecordSetLength);
    gStreamTable[11] = reinterpret_cast<void *>(&RecordSeekRel);
    gStreamTable[12] = reinterpret_cast<void *>(&RecordSeekAbs);
    gStreamTable[13] = reinterpret_cast<void *>(&RecordSeekTo);
    gStreamTable[14] = reinterpret_cast<void *>(&RecordReadBytes);
    gStreamTable[15] = reinterpret_cast<void *>(&RecordReadCounted);
    gStreamTable[16] = reinterpret_cast<void *>(&RecordWriteBytes);
    gStreamTable[17] = reinterpret_cast<void *>(&RecordWriteCounted);
}

// ---- cGZDBSegmentDirectoryFiles ----

struct DirTables {
    void *primary[37];
    void *sub[50];
};

DirTables MakeDirTables(const difftest::Context &ctx)
{
    DirTables t{};
    std::memcpy(t.primary, ctx.image.At(cGZDBSegmentDirectoryFiles::kVTable), sizeof(t.primary));
    std::memcpy(t.sub, ctx.image.At(cGZDBSegmentDirectoryFiles::kSubVTable), sizeof(t.sub));
    t.primary[1] = reinterpret_cast<void *>(&RecordAddRef);
    t.sub[1] = reinterpret_cast<void *>(&RecordAddRef);
    t.sub[cGZDBSegmentDirectoryFiles::kDeleteSlot] = reinterpret_cast<void *>(&RecordDelete);
    return t;
}

// ---- cGZDBSDFRecord ----

struct RecTables {
    void *primary[17];
    void *serial[62];
    void *sub8[21];
};

RecTables MakeRecTables(const difftest::Context &ctx)
{
    RecTables t{};
    std::memcpy(t.primary, ctx.image.At(cGZDBSDFRecord::kVTable), sizeof(t.primary));
    std::memcpy(t.serial, ctx.image.At(cGZDBSDFRecord::kSerialVTable), sizeof(t.serial));
    std::memcpy(t.sub8, ctx.image.At(0xe5ec18), sizeof(t.sub8));
    t.primary[1] = reinterpret_cast<void *>(&RecordAddRef);
    t.serial[1] = reinterpret_cast<void *>(&RecordAddRef);
    t.sub8[1] = reinterpret_cast<void *>(&RecordAddRef);
    t.sub8[19] = reinterpret_cast<void *>(&RecordDeleteWithFlags); // the record subobject's deleting destructor
    return t;
}

// One trial's inputs: the image of the whole object, and the output string's heap bytes when the path is long.
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

// Runs one call on a fresh copy of the directory object. `call` gets the arena and returns the result.
template <typename Call> Outcome RunDir(const DirTables &t, const Scene &s, Call call)
{
    std::memcpy(gArena, s.image.data(), kBytes);
    const void *primary = t.primary;
    const void *sub = t.sub;
    const void *str = gStrTable;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &sub, sizeof(sub));
    std::memcpy(gArena + kStr, &str, sizeof(str));
    if (s.heapPath) {
        std::memcpy(gHeap, s.heap.data(), kHeapCap);
        const void *data = gHeap;
        std::memcpy(gArena + 0x50, &data, sizeof(data));
    }
    gLog.clear();

    Outcome out;
    out.result = call(gArena);
    out.log = gLog;
    // The vtable pointers differ between the plain and routed tables, so they are not compared.
    std::memset(gArena, 0, 8);
    std::memset(gArena + kStr, 0, sizeof(void *));
    out.bytes.assign(gArena, gArena + kBytes);
    return out;
}

// Runs one call on a fresh copy of the record. The record's +8 subobject and its stream are set up too.
template <typename Call> Outcome RunRec(const RecTables &t, const Scene &s, Call call)
{
    std::memcpy(gArena, s.image.data(), kBytes);
    const void *primary = t.primary;
    const void *serial = t.serial;
    const void *sub8 = t.sub8;
    const void *stream = gStreamTable;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &serial, sizeof(serial));
    std::memcpy(gArena + 8, &sub8, sizeof(sub8));
    std::memcpy(gArena + 0x4c, &stream, sizeof(stream));
    gLog.clear();

    Outcome out;
    out.result = call(gArena);
    out.log = gLog;
    std::memset(gArena, 0, 12);
    std::memset(gArena + 0x4c, 0, sizeof(void *));
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

void SetWord(Scene &s, size_t at, uint32_t v)
{
    std::memcpy(s.image.data() + at, &v, sizeof(v));
}

void SetByte(Scene &s, size_t at, uint8_t v)
{
    s.image[at] = v;
}

void SetHalf(Scene &s, size_t at, uint16_t v)
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

// ---- cGZDBSegmentDirectoryFiles ----

DIFF_TEST(cGZDBSegmentDirectoryFiles_QueryInterface, 0x9a2660)
{
    InitFakes();
    auto orig = ctx.Original<QueryFn>(0x9a2660);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.primary[0] = Addr(static_cast<QueryMember>(&cGZDBSegmentDirectoryFiles::QueryInterface));
    // 0x856c8cf7 is ours; 1 and 0x656b8f0c go to the base's QueryInterface, which answers this + 4.
    static const uint32_t ids[] = {0x856c8cf7, 1, 0x656b8f0c, 0, 2, 0x856c8cf8, 0x656b8f0d, 0xffffffff};
    std::mt19937 rng(0x5a1);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        uint32_t id = (rng() % 4 == 0) ? PickWord(rng) : ids[rng() % 8];
        // The out pointer starts as a known value, so a refused id must leave it alone.
        SetWord(s, kDst, kUnchangedOut);
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return orig(self, id, reinterpret_cast<void **>(self + kDst));
        });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->QueryInterface(id, reinterpret_cast<void **>(self + kDst));
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<QueryFn>(self, 0)(self, id, reinterpret_cast<void **>(self + kDst));
        });
        Compare(ctx, "QueryInterface", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_Release, 0x9a27b0)
{
    InitFakes();
    auto orig = ctx.Original<RefFn>(0x9a27b0);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.primary[2] = Addr(static_cast<ReleaseMember>(&cGZDBSegmentDirectoryFiles::Release));
    static const uint32_t counts[] = {0, 1, 2, 3, 0xffffffff, 0x80000000, 0x7fffffff};
    std::mt19937 rng(0x5a2);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetWord(s, 8, rng() % 2 ? counts[rng() % 7] : PickWord(rng)); // the count at +8
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->Release();
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<RefFn>(self, 2)(self); });
        Compare(ctx, "Release", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_vf4, 0x9a1de0)
{
    InitFakes();
    auto orig = ctx.Original<VoidFn>(0x9a1de0);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.primary[4] = Addr(static_cast<VoidMember>(&cGZDBSegmentDirectoryFiles::SetFlag3C));
    std::mt19937 rng(0x5a4);
    for (int i = 0; i < 200; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x3c, PickFlag(rng));
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self);
            return 0;
        });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->SetFlag3C();
            return 0;
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidFn>(self, 4)(self);
            return 0;
        });
        Compare(ctx, "SetFlag3C", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_GetPath, 0x9a1b70)
{
    InitFakes();
    auto orig = ctx.Original<VoidPtrFn>(0x9a1b70);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.primary[8] = Addr(static_cast<VoidStrMember>(&cGZDBSegmentDirectoryFiles::GetPath));
    std::mt19937 rng(0x5a8);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        // Inline storage (capacity 15) or a heap buffer (capacity above 15), with the size inside it.
        uint32_t cap = (rng() % 2) ? 15 : (16 + static_cast<uint32_t>(rng() % 3) * 16 - 1);
        uint32_t size = static_cast<uint32_t>(rng() % (cap + 1));
        SetWord(s, 0x60, size);
        SetWord(s, 0x64, cap);
        s.heapPath = cap > 15;
        s.heap.resize(kHeapCap);
        for (auto &b : s.heap) b = static_cast<uint8_t>(rng());
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self, self + kStr);
            return 0;
        });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->GetPath(reinterpret_cast<cIGZString *>(self + kStr));
            return 0;
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidPtrFn>(self, 8)(self, self + kStr);
            return 0;
        });
        Compare(ctx, "GetPath", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_AutoOpen, 0x99f340)
{
    InitFakes();
    auto orig = ctx.Original<BoolU32U32Fn>(0x99f340);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.sub[6] = Addr(static_cast<ViewU8U8Member>(&DirectorySegmentView::AutoOpen));
    std::mt19937 rng(0x5a6);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        // The original stores the low bytes of the two stack arguments.
        uint32_t state = PickWord(rng), flag = PickWord(rng);
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, state, flag); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<DirectorySegmentView *>(self + 4)->AutoOpen(static_cast<uint8_t>(state),
                                                                               static_cast<uint8_t>(flag));
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolU32U32Fn>(self + 4, 6)(self + 4, state, flag);
        });
        Compare(ctx, "AutoOpen", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_IsOpen, 0x1d3df0)
{
    InitFakes();
    auto orig = ctx.Original<BoolFn>(0x1d3df0);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.sub[7] = Addr(static_cast<ViewBoolMember>(&DirectorySegmentView::IsOpen));
    std::mt19937 rng(0x5a7);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetWord(s, 0x44, PickWord(rng)); // the state dword at sub +0x40
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<DirectorySegmentView *>(self + 4)->IsOpen();
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolFn>(self + 4, 7)(self + 4); });
        Compare(ctx, "IsOpen", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_IsWritable, 0x1d3e00)
{
    InitFakes();
    auto orig = ctx.Original<BoolFn>(0x1d3e00);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.sub[8] = Addr(static_cast<ViewBoolMember>(&DirectorySegmentView::IsWritable));
    std::mt19937 rng(0x5a9);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetWord(s, 0x44, PickWord(rng));
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<DirectorySegmentView *>(self + 4)->IsWritable();
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<BoolFn>(self + 4, 8)(self + 4); });
        Compare(ctx, "IsWritable", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_SetSegmentID, 0x9a2a20)
{
    InitFakes();
    auto orig = ctx.Original<BoolPtrFn>(0x9a2a20);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.sub[16] = Addr(static_cast<ViewIdMember>(&DirectorySegmentView::SetSegmentID));
    std::mt19937 rng(0x5aa);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetWord(s, kDst, PickWord(rng)); // the ID the caller points at
        if (rng() % 2) SetWord(s, 0x70, 0); // the "first time" word at sub +0x6c
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, self + kDst); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<DirectorySegmentView *>(self + 4)->SetSegmentID(reinterpret_cast<uint32_t *>(self + kDst));
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolPtrFn>(self + 4, 16)(self + 4, self + kDst);
        });
        Compare(ctx, "SetSegmentID", a, b, c);
    }
}

// ---- cGZDBSDFRecord ----

DIFF_TEST(cGZDBSDFRecord_QueryInterface, 0x9a2610)
{
    InitFakes();
    auto orig = ctx.Original<QueryFn>(0x9a2610);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[0] = Addr(static_cast<RecQueryMember>(&cGZDBSDFRecord::QueryInterface));
    // 0x856c8d04 is ours; the serial subobject answers the others it knows (1, record, serial, streams).
    static const uint32_t ids[] = {0x856c8d04, 1, 0xe56b8f03, 0xc56b8f08, 0xc51b11cb, 0x051b11bd, 0, 0x856c8d05};
    std::mt19937 rng(0x6a1);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        uint32_t id = (rng() % 4 == 0) ? PickWord(rng) : ids[rng() % 8];
        SetWord(s, kDst, kUnchangedOut);
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return orig(self, id, reinterpret_cast<void **>(self + kDst));
        });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSDFRecord *>(self)->QueryInterface(id, reinterpret_cast<void **>(self + kDst));
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<QueryFn>(self, 0)(self, id, reinterpret_cast<void **>(self + kDst));
        });
        Compare(ctx, "record QueryInterface", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_AddRef, 0x99f2e0)
{
    InitFakes();
    auto orig = ctx.Original<RefFn>(0x99f2e0);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[1] = Addr(static_cast<RecRefMember>(&cGZDBSDFRecord::AddRef));
    std::mt19937 rng(0x6a2);
    for (int i = 0; i < 200; ++i) {
        Scene s = MakeScene(rng);
        SetHalf(s, 0xc, static_cast<uint16_t>(PickWord(rng)));
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSDFRecord *>(self)->AddRef();
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<RefFn>(self, 1)(self); });
        Compare(ctx, "record AddRef", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_Release, 0x9a2780)
{
    InitFakes();
    auto orig = ctx.Original<RefFn>(0x9a2780);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[2] = Addr(static_cast<RecRefMember>(&cGZDBSDFRecord::Release));
    static const uint16_t counts[] = {0, 1, 2, 3, 0xffff, 0x8000};
    std::mt19937 rng(0x6a3);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetHalf(s, 0xc, rng() % 2 ? counts[rng() % 6] : static_cast<uint16_t>(PickWord(rng)));
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSDFRecord *>(self)->Release();
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<RefFn>(self, 2)(self); });
        Compare(ctx, "record Release", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_GetPosition, 0x9a1b90)
{
    InitFakes();
    auto orig = ctx.Original<I32Fn>(0x9a1b90);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.serial[54] = Addr(static_cast<ViewI32Member>(&cGZDBSDFRecordSerialView::GetPosition));
    std::mt19937 rng(0x6a4);
    for (int i = 0; i < 200; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng)); // the open byte at serial +0x44
        gPosRet = static_cast<int32_t>(PickWord(rng));
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint32_t>(orig(self + 4));
        });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint32_t>(reinterpret_cast<cGZDBSDFRecordSerialView *>(self + 4)->GetPosition());
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint32_t>(VirtualSlot<I32Fn>(self + 4, 54)(self + 4));
        });
        Compare(ctx, "GetPosition", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_SeekAbsolute, 0x9a2820)
{
    InitFakes();
    auto orig = ctx.Original<BoolI32Fn>(0x9a2820);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.serial[55] = Addr(static_cast<ViewBoolI32Member>(&cGZDBSDFRecordSerialView::SeekAbsolute));
    std::mt19937 rng(0x6a5);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        SetWord(s, 0x184, rng() % 2 ? 1 : PickWord(rng)); // the seek mode at serial +0x180
        gSeekRet = static_cast<int32_t>(rng() % 3 == 0 ? -1 : PickWord(rng));
        int32_t pos = static_cast<int32_t>(PickWord(rng));
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, pos); });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSDFRecordSerialView *>(self + 4)->SeekAbsolute(pos);
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolI32Fn>(self + 4, 55)(self + 4, pos);
        });
        Compare(ctx, "SeekAbsolute", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_SeekRelative, 0x9a2860)
{
    InitFakes();
    auto orig = ctx.Original<BoolI32Fn>(0x9a2860);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.serial[56] = Addr(static_cast<ViewBoolI32Member>(&cGZDBSDFRecordSerialView::SeekRelative));
    std::mt19937 rng(0x6a6);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        SetWord(s, 0x184, rng() % 2 ? 1 : PickWord(rng));
        gSeekRet = static_cast<int32_t>(rng() % 3 == 0 ? -1 : PickWord(rng));
        int32_t offset = static_cast<int32_t>(PickWord(rng));
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, offset); });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSDFRecordSerialView *>(self + 4)->SeekRelative(offset);
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolI32Fn>(self + 4, 56)(self + 4, offset);
        });
        Compare(ctx, "SeekRelative", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_SetSize, 0x9a2a40)
{
    InitFakes();
    auto orig = ctx.Original<BoolU32Fn>(0x9a2a40);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.serial[53] = Addr(static_cast<ViewBoolU32Member>(&cGZDBSDFRecordSerialView::SetSize));
    std::mt19937 rng(0x6a7);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        gSetRet = rng() % 2;
        uint32_t size = PickWord(rng);
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, size); });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSDFRecordSerialView *>(self + 4)->SetSize(size);
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolU32Fn>(self + 4, 53)(self + 4, size);
        });
        Compare(ctx, "SetSize", a, b, c);
    }
}

// ---- cGZDBSegmentDirectoryFiles, continued ----

namespace {

// Offsets of the fake argument object (two of them: 0x1f0 and 0x1f8) and of the embedded object's vtable word.
constexpr size_t kObjA = 0x1f0;
constexpr size_t kObjB = 0x1f8;

uint32_t AddressWord(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// The fake objects in the arena: their vtable words, and the embedded object's vtable at +0x4c.
void PlaceFakeObjects(Scene &s)
{
    SetWord(s, kObjA, AddressWord(gObjTable));
    SetWord(s, kObjB, AddressWord(gObjTable));
    SetWord(s, 0x4c, AddressWord(gEmbedTable));
}

// A fake object at arena offset `off`, or null for 0.
const void *ObjAt(const uint8_t *self, size_t off)
{
    return off ? static_cast<const void *>(self + off) : nullptr;
}

} // namespace

DIFF_TEST(cGZDBSegmentDirectoryFiles_vf9, 0x9a29c0)
{
    InitFakes();
    auto orig = ctx.Original<BoolPtrFn>(0x9a29c0);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.primary[9] = Addr(static_cast<DirObjMember>(&cGZDBSegmentDirectoryFiles::Vf9));
    std::mt19937 rng(0x6b1);
    for (int i = 0; i < 500; ++i) {
        Scene s = MakeScene(rng);
        PlaceFakeObjects(s);
        // The state's bits 0..1 are clear in half the trials, so the call goes on.
        SetByte(s, 0x44, rng() % 2 ? static_cast<uint8_t>(rng() & 0xfc) : PickFlag(rng));
        gBool8 = rng() % 4 == 0;
        size_t argOff = rng() % 2 ? kObjA : kObjB;
        // The path: inline (capacity 15) or heap (capacity 16 to 47), always shorter than the capacity so no
        // reallocation happens (growing it would give the two sides different heap addresses).
        bool heap = rng() % 2;
        uint32_t cap = heap ? 16 + static_cast<uint32_t>(rng() % 32) : 15;
        // Inline paths may be empty (reads the byte before the buffer); heap paths are not (that read is out of
        // the test's own buffer).
        uint32_t size = heap ? 1 + static_cast<uint32_t>(rng() % (cap - 1)) : static_cast<uint32_t>(rng() % cap);
        SetWord(s, 0x60, size);
        SetWord(s, 0x64, cap);
        s.heapPath = heap;
        s.heap.resize(kHeapCap);
        for (auto &b : s.heap) b = static_cast<uint8_t>(rng());
        static const uint8_t tails[] = {'\\', '/', 'a', '\0', ':'};
        uint8_t last = tails[rng() % 5];
        if (size > 0) {
            if (heap) s.heap[size - 1] = last;
            else s.image[0x50 + size - 1] = last;
        }
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, ObjAt(self, argOff)); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->Vf9(const_cast<void *>(ObjAt(self, argOff)));
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolPtrFn>(self, 9)(self, ObjAt(self, argOff));
        });
        Compare(ctx, "Vf9", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_vf15, 0x9a28b0)
{
    InitFakes();
    auto orig = ctx.Original<VoidU32U32Fn>(0x9a28b0);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.primary[15] = Addr(static_cast<DirBytesMember>(&cGZDBSegmentDirectoryFiles::SetBytes78));
    std::mt19937 rng(0x6b2);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        // The original stores only the low byte of each stack argument, so the upper bits are random.
        uint32_t x = PickWord(rng), y = PickWord(rng);
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self, x, y);
            return 0;
        });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->SetBytes78(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
            return 0;
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidU32U32Fn>(self, 15)(self, x, y);
            return 0;
        });
        Compare(ctx, "SetBytes78", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_vf16, 0x9a28a0)
{
    InitFakes();
    auto orig = ctx.Original<VoidU32Fn>(0x9a28a0);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.primary[16] = Addr(static_cast<DirByteMember>(&cGZDBSegmentDirectoryFiles::SetByte7A));
    std::mt19937 rng(0x6b3);
    for (int i = 0; i < 200; ++i) {
        Scene s = MakeScene(rng);
        uint32_t x = PickWord(rng);
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self, x);
            return 0;
        });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->SetByte7A(static_cast<uint8_t>(x));
            return 0;
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidU32Fn>(self, 16)(self, x);
            return 0;
        });
        Compare(ctx, "SetByte7A", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_vf23, 0x9a2980)
{
    InitFakes();
    auto orig = ctx.Original<BoolPtrFn>(0x9a2980);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.primary[23] = Addr(static_cast<DirObjMember>(&cGZDBSegmentDirectoryFiles::SetObjectAC));
    static const size_t objs[] = {0, kObjA, kObjB};
    std::mt19937 rng(0x6b4);
    for (int i = 0; i < 400; ++i) {
        Scene s = MakeScene(rng);
        PlaceFakeObjects(s);
        size_t oldOff = objs[rng() % 3];
        size_t newOff = rng() % 4 == 0 ? oldOff : objs[rng() % 3]; // same object sometimes: no calls at all
        SetWord(s, 0xac, oldOff ? AddressWord(gArena + oldOff) : 0);
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, ObjAt(self, newOff)); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->SetObjectAC(const_cast<void *>(ObjAt(self, newOff)));
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolPtrFn>(self, 23)(self, ObjAt(self, newOff));
        });
        Compare(ctx, "SetObjectAC", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_vf28, 0x1d3ac0)
{
    InitFakes();
    auto orig = ctx.Original<VoidU32Fn>(0x1d3ac0);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.primary[28] = Addr(static_cast<DirByteMember>(&cGZDBSegmentDirectoryFiles::SetByteB0));
    std::mt19937 rng(0x6b5);
    for (int i = 0; i < 200; ++i) {
        Scene s = MakeScene(rng);
        uint32_t x = PickWord(rng);
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self, x);
            return 0;
        });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->SetByteB0(static_cast<uint8_t>(x));
            return 0;
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidU32Fn>(self, 28)(self, x);
            return 0;
        });
        Compare(ctx, "SetByteB0", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_vf31, 0x9a0990)
{
    InitFakes();
    auto orig = ctx.Original<U32Fn2>(0x9a0990);
    // Slot 32 is faked in both tables, so the original's call and ours land in the same recorder.
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    plain.primary[32] = reinterpret_cast<void *>(&RecordSlot32);
    routed.primary[32] = reinterpret_cast<void *>(&RecordSlot32);
    routed.primary[31] = Addr(static_cast<DirU32U32Member>(&cGZDBSegmentDirectoryFiles::Vf31));
    std::mt19937 rng(0x6b6);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t x = PickWord(rng), y = PickWord(rng);
        gSlotRet = PickWord(rng);
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, x, y); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->Vf31(x, y);
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<U32Fn2>(self, 31)(self, x, y); });
        Compare(ctx, "Vf31", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_vf36, 0x9a2fa0)
{
    InitFakes();
    auto orig = ctx.Original<U32Fn3>(0x9a2fa0);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    plain.primary[35] = reinterpret_cast<void *>(&RecordSlot35);
    routed.primary[35] = reinterpret_cast<void *>(&RecordSlot35);
    routed.primary[36] = Addr(static_cast<DirU32x3Member>(&cGZDBSegmentDirectoryFiles::Vf36));
    std::mt19937 rng(0x6b7);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t x = PickWord(rng), y = PickWord(rng), z = PickWord(rng);
        gSlotRet = PickWord(rng);
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, x, y, z); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(self)->Vf36(x, y, z);
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<U32Fn3>(self, 36)(self, x, y, z);
        });
        Compare(ctx, "Vf36", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_SubInit, 0x1d3db4)
{
    InitFakes();
    auto orig = ctx.Original<VoidFn>(0x1d3db4);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.sub[3] = Addr(static_cast<ViewVoidMember>(&DirectorySegmentView::Init));
    std::mt19937 rng(0x6b8);
    for (int i = 0; i < 200; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x3c, PickFlag(rng));
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self + 4);
            return 0;
        });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            reinterpret_cast<DirectorySegmentView *>(self + 4)->Init();
            return 0;
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidFn>(self + 4, 3)(self + 4);
            return 0;
        });
        Compare(ctx, "sub Init", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_SubGetPath, 0x1d3b56)
{
    InitFakes();
    auto orig = ctx.Original<VoidPtrFn>(0x1d3b56);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    routed.sub[11] = Addr(static_cast<ViewPathMember>(&DirectorySegmentView::GetPath));
    std::mt19937 rng(0x6b9);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t cap = (rng() % 2) ? 15 : (16 + static_cast<uint32_t>(rng() % 3) * 16 - 1);
        uint32_t size = static_cast<uint32_t>(rng() % (cap + 1));
        SetWord(s, 0x60, size);
        SetWord(s, 0x64, cap);
        s.heapPath = cap > 15;
        s.heap.resize(kHeapCap);
        for (auto &b : s.heap) b = static_cast<uint8_t>(rng());
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            orig(self + 4, self + kStr);
            return 0;
        });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            reinterpret_cast<DirectorySegmentView *>(self + 4)->GetPath(reinterpret_cast<cIGZString *>(self + kStr));
            return 0;
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t {
            VirtualSlot<VoidPtrFn>(self + 4, 11)(self + 4, self + kStr);
            return 0;
        });
        Compare(ctx, "sub GetPath", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_SubVf44, 0x9a0630)
{
    InitFakes();
    auto orig = ctx.Original<U32Fn1>(0x9a0630);
    DirTables plain = MakeDirTables(ctx);
    DirTables routed = MakeDirTables(ctx);
    plain.sub[45] = reinterpret_cast<void *>(&RecordSlot45);
    routed.sub[45] = reinterpret_cast<void *>(&RecordSlot45);
    routed.sub[44] = Addr(static_cast<ViewU32Member>(&DirectorySegmentView::Vf44));
    std::mt19937 rng(0x6ba);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        uint32_t x = PickWord(rng);
        gSlotRet = PickWord(rng);
        auto a = RunDir(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, x); });
        auto b = RunDir(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<DirectorySegmentView *>(self + 4)->Vf44(x);
        });
        auto c = RunDir(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<U32Fn1>(self + 4, 44)(self + 4, x); });
        Compare(ctx, "sub Vf44", a, b, c);
    }
}

// The primary slots 3 to 9 and the serial slot 3. The data buffers sit at kData inside the arena; the stream's
// fakes write at most eight bytes into them.
constexpr size_t kData = 0x100;

// Seek mode values: the edges the gates compare against, and random values.
uint32_t PickMode(std::mt19937 &rng)
{
    static const uint32_t modes[] = {0, 1, 2, 3, 4, 0xffffffff};
    if (rng() % 4 == 0) return PickWord(rng);
    return modes[rng() % 6];
}

DIFF_TEST(cGZDBSDFRecord_GetSize, 0x9a1c50)
{
    InitFakes();
    auto orig = ctx.Original<I32Fn>(0x9a1c50);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[3] = Addr(static_cast<RecI32Member>(&cGZDBSDFRecord::GetSize));
    std::mt19937 rng(0x7a1);
    for (int i = 0; i < 200; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        gLenRet = PickWord(rng);
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t { return static_cast<uint32_t>(orig(self)); });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint32_t>(reinterpret_cast<cGZDBSDFRecord *>(self)->GetSize());
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint32_t>(VirtualSlot<I32Fn>(self, 3)(self));
        });
        Compare(ctx, "GetSize", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_GetFieldVoid, 0x9a1460)
{
    InitFakes();
    auto orig = ctx.Original<BoolPtrU32Fn>(0x9a1460);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[4] = Addr(static_cast<RecBoolPtrU32Member>(&cGZDBSDFRecord::GetFieldVoid));
    std::mt19937 rng(0x7a2);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        SetWord(s, 0x184, PickMode(rng));
        gReadRet = static_cast<uint8_t>(rng()); // any nonzero byte reads as true
        uint32_t size = PickWord(rng);
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint64_t>(orig(self, self + kData, size));
        });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint64_t>(reinterpret_cast<cGZDBSDFRecord *>(self)->GetFieldVoid(self + kData, size));
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint64_t>(VirtualSlot<BoolPtrU32Fn>(self, 4)(self, self + kData, size));
        });
        Compare(ctx, "GetFieldVoid", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_SetFieldVoid, 0x9a28d0)
{
    InitFakes();
    auto orig = ctx.Original<BoolCPtrU32Fn>(0x9a28d0);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[5] = Addr(static_cast<RecBoolCPtrU32Member>(&cGZDBSDFRecord::SetFieldVoid));
    std::mt19937 rng(0x7a3);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        SetWord(s, 0x184, PickMode(rng));
        gWriteRet = static_cast<uint8_t>(rng());
        uint32_t size = PickWord(rng);
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint64_t>(orig(self, self + kData, size));
        });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint64_t>(
                reinterpret_cast<cGZDBSDFRecord *>(self)->SetFieldVoid(self + kData, size));
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint64_t>(VirtualSlot<BoolCPtrU32Fn>(self, 5)(self, self + kData, size));
        });
        Compare(ctx, "SetFieldVoid", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_ReadCounted, 0x9a2740)
{
    InitFakes();
    auto orig = ctx.Original<U32PtrU32Fn>(0x9a2740);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[6] = Addr(static_cast<RecU32PtrU32Member>(&cGZDBSDFRecord::ReadCounted));
    std::mt19937 rng(0x7a4);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        SetWord(s, 0x184, PickMode(rng));
        gCountOut = PickWord(rng);
        uint32_t count = PickWord(rng);
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kData, count); });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSDFRecord *>(self)->ReadCounted(self + kData, count);
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<U32PtrU32Fn>(self, 6)(self, self + kData, count);
        });
        Compare(ctx, "ReadCounted", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_WriteCounted, 0x9a2bd0)
{
    InitFakes();
    auto orig = ctx.Original<U32CPtrU32Fn>(0x9a2bd0);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[7] = Addr(static_cast<RecU32CPtrU32Member>(&cGZDBSDFRecord::WriteCounted));
    std::mt19937 rng(0x7a5);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        SetWord(s, 0x184, PickMode(rng));
        gCountOut = PickWord(rng);
        uint32_t count = PickWord(rng);
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kData, count); });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSDFRecord *>(self)->WriteCounted(self + kData, count);
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<U32CPtrU32Fn>(self, 7)(self, self + kData, count);
        });
        Compare(ctx, "WriteCounted", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_GetPositionTo, 0x9a2ba0)
{
    InitFakes();
    auto orig = ctx.Original<BoolPosFn>(0x9a2ba0);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[8] = Addr(static_cast<RecPosMember>(&cGZDBSDFRecord::GetPositionTo));
    std::mt19937 rng(0x7a6);
    for (int i = 0; i < 200; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        gPosRet = static_cast<int32_t>(PickWord(rng));
        // The out pointer starts as a known value: a closed stream must overwrite it with 0.
        SetWord(s, kDst, kUnchangedOut);
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint64_t>(orig(self, reinterpret_cast<int32_t *>(self + kDst)));
        });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint64_t>(
                reinterpret_cast<cGZDBSDFRecord *>(self)->GetPositionTo(reinterpret_cast<int32_t *>(self + kDst)));
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return static_cast<uint64_t>(VirtualSlot<BoolPosFn>(self, 8)(self, reinterpret_cast<int32_t *>(self + kDst)));
        });
        Compare(ctx, "GetPositionTo", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecord_SeekTo, 0x9a27e0)
{
    InitFakes();
    auto orig = ctx.Original<BoolU32U32Fn>(0x9a27e0);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.primary[9] = Addr(static_cast<RecSeekToMember>(&cGZDBSDFRecord::SeekTo));
    std::mt19937 rng(0x7a7);
    for (int i = 0; i < 300; ++i) {
        Scene s = MakeScene(rng);
        SetByte(s, 0x48, PickFlag(rng));
        gLenRet = PickWord(rng);
        gSeekToRet = rng() % 3 == 0 ? -1 : static_cast<int32_t>(PickWord(rng));
        // Positions at and around the length (the bound is unsigned), and random ones.
        uint32_t position = rng() % 2 ? gLenRet - rng() % 3 : PickWord(rng);
        // The origin indexes the game's table: only its first eight entries are used here.
        uint32_t origin = rng() % 8;
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, position, origin); });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<cGZDBSDFRecord *>(self)->SeekTo(position, origin);
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<BoolU32U32Fn>(self, 9)(self, position, origin);
        });
        Compare(ctx, "SeekTo", a, b, c);
    }
}

DIFF_TEST(cGZDBSDFRecordSerialView_AsIGZPersistDBRecord, 0x99f330)
{
    InitFakes();
    auto orig = ctx.Original<PtrFn>(0x99f330);
    RecTables plain = MakeRecTables(ctx);
    RecTables routed = MakeRecTables(ctx);
    routed.serial[3] = Addr(static_cast<ViewPtrMember>(&cGZDBSDFRecordSerialView::AsIGZPersistDBRecord));
    std::mt19937 rng(0x7a8);
    for (int i = 0; i < 100; ++i) {
        Scene s = MakeScene(rng);
        auto a = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<uintptr_t>(orig(self + 4));
        });
        auto b = RunRec(plain, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<uintptr_t>(
                reinterpret_cast<cGZDBSDFRecordSerialView *>(self + 4)->AsIGZPersistDBRecord());
        });
        auto c = RunRec(routed, s, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<uintptr_t>(VirtualSlot<PtrFn>(self + 4, 3)(self + 4));
        });
        Compare(ctx, "AsIGZPersistDBRecord", a, b, c);
    }
    // A null record: its serial subobject is at address 4, which must map to null. Only the direct call is
    // compared here, since the routed table needs a real object.
    Scene s = MakeScene(rng);
    auto a = RunRec(plain, s, [&](uint8_t *) -> uint64_t {
        return reinterpret_cast<uintptr_t>(orig(reinterpret_cast<void *>(4)));
    });
    auto b = RunRec(plain, s, [&](uint8_t *) -> uint64_t {
        return reinterpret_cast<uintptr_t>(
            reinterpret_cast<cGZDBSDFRecordSerialView *>(static_cast<uintptr_t>(4))->AsIGZPersistDBRecord());
    });
    Compare(ctx, "AsIGZPersistDBRecord (null record)", a, b, b);
}
