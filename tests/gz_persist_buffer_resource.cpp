// cGZPersistBufferResource (src/gz/cGZPersistBufferResource.h): original vs ours, one DIFF_TEST per body. Lane NU.
//
// Both sides build the resource the game's way (the original's constructor 0x9b64a0 on one, our adapter on the other) over the
// same doubles and compare the result, every call the body makes (a log), the buffer's surface byte for byte, the object's
// fields and the blocks freed. The doubles: the COM director (GetDirectorFrameworkObject 0x1ae18d stood in on both sides) that
// hands out one fake buffer (cIGZBuffer: Init, Lock, Unlock, the colour type, bits per pixel, ConvertRGBValueToNative, the
// surface and its stride) and its aggregate face; the serial record (a byte stream with typed reads, seeks, size and position;
// the same table on both sides); the image's CRT (fopen 0x83aa0f, fread 0x8398bb, fseek 0x839cd3, fclose 0x838ff4 and memset 0x8281e0 stand in with the toolchain's on the
// original side, whose CRT is not initialised); the TGA reader's pixel converter (0x9cd6a0 / 0x9cea90 / 0x9cd890, another
// class's code, patched with loggers that see the formats, the strides and a hash of the source rows). The game's new / delete
// and malloc / free are ours.
//
// The inputs are synthetic BMP and TGA files built to what the readers branch on (bit depths, palettes, RLE, orientation, bad
// headers), kept to what the original does without reading what it never wrote: no palette beyond 256 entries, no run past
// the end of an image, no 4 bit bitmap with rows in the record reader (its row size is 0 there, so ours would read what the
// original reads from nothing), no short RLE read in the record reader or the file reader (the original then decodes bytes it
// never wrote). Equivalent or untestable mutants are argued in docs/subsystems/persistence.md (lane NU).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistBufferResource.h"
#include "ifaces/cIGZPersistDBSerialRecord.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using Self = cGZPersistBufferResource;
using Record = ifaces::cIGZPersistDBSerialRecord;
using Adapters = cGZPersistBufferResourceAdapters;

std::vector<int64_t> gLog;
std::mt19937 gRng; // the doubles' behaviour stream, reseeded alike for each side
uint32_t Next() { return gRng(); }
template <typename... T> void Log(T... values) { (gLog.push_back(static_cast<int64_t>(values)), ...); }
// A bool as the game's code returns it: only the low byte counts, and the BMP reader tests it against 1.
uint32_t Answer(bool ok) { return (Next() & 0xffffff00u) | (ok ? 1u : 0u); }

uint32_t Hash(const void *data, size_t n)
{
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(data)[i]) * 0x01000193;
    return h;
}

// ---- the world ----

constexpr size_t kResourceSize = 0x3c;
constexpr size_t kSurfaceBytes = 0x4000;

// A cRZString on the stack around a copy of the text, as the factory builds it.
struct StackString {
    union {
        cRZString s;
    };
    explicit StackString(const std::string &text)
    {
        new (&s.mString) std::string(text);
        s.mVTable = GameAddress(cRZString::kVTable);
        s.mRefCount = 0;
    }
    ~StackString() { s.mString.~basic_string(); }
    StackString(const StackString &) = delete;
    StackString &operator=(const StackString &) = delete;
};
alignas(16) uint8_t gSurface[kSurfaceBytes];

struct FakeObject {
    void *const *vtable;
};
struct FakeBuffer {
    void *const *vtable;
    uint32_t width, height, bpp, stride, nativeFormat, nativeBits;
};
FakeBuffer gBuf, gBuf2; // the buffer the director hands out; SetBuffer's other one
FakeObject gAgg, gCom, gRecord, gSink;
void *gBufTable[48], *gAggTable[8], *gComTable[8], *gSinkTable[40];
void *gRecordTable[2][64]; // one table per side (alike)
uint8_t *gObj = nullptr;   // the resource under test
bool gInitOk, gLockOk, gAggClass, gPlainClass;
uint32_t gSalt;
std::vector<uint8_t> gData;
size_t gPos;

int64_t Id(const void *p)
{
    const auto a = reinterpret_cast<uintptr_t>(p);
    const auto in = [&](const void *base, size_t n) {
        const auto b = reinterpret_cast<uintptr_t>(base);
        return a >= b && a < b + n;
    };
    const auto off = [&](const void *base) { return static_cast<int64_t>(a - reinterpret_cast<uintptr_t>(base)); };
    if (p == nullptr) return 0;
    if (gObj != nullptr && in(gObj, kResourceSize)) return 0x80000 + off(gObj);
    if (p == &gBuf) return 0x20000;
    if (p == &gBuf2) return 0x21000;
    if (in(gSurface, sizeof(gSurface))) return 0x30000 + off(gSurface);
    if (p == &gRecord) return 0x40000;
    if (p == &gSink) return 0x50000;
    if (p == &gAgg) return 0x60000;
    if (p == &gCom) return 0x70000;
    return -1;
}

enum : int64_t { kQi = 100, kAddRef, kRelease, kInit, kLock, kUnlock, kNative, kBpp, kConvert, kBits, kStride, kOther, kClass, kDirector,
                 kAggRelease, kNdQi, kSetOuter, kU8, kU16, kVoid, kSize, kPos, kSeekAbs, kSeekRel, kCopy, kMakeFormat, kBuild,
                 kRows, kObject };

FakeBuffer *B(void *self) { return static_cast<FakeBuffer *>(self); }

// ---- the buffer double ----

uint32_t __fastcall BufQi(void *self, void *, uint32_t riid, void **out)
{
    Log(kQi, Id(self), riid, Id(out));
    if (riid == 0x12f) {
        *out = &gAgg;
        return Answer(true);
    }
    const bool ok = Next() % 2 != 0;
    if (ok) *out = self;
    return Answer(ok);
}
uint32_t __fastcall BufAddRef(void *self, void *) { Log(kAddRef, Id(self)); return 7; }
uint32_t __fastcall BufRelease(void *self, void *) { Log(kRelease, Id(self)); return 3; }
uint32_t __fastcall BufInit(void *self, void *, uint32_t w, uint32_t h, uint32_t f, uint32_t b)
{
    Log(kInit, Id(self), w, h, f, b);
    B(self)->width = w;
    B(self)->height = h;
    return Answer(gInitOk);
}
uint32_t __fastcall BufLock(void *self, void *, uint32_t flags) { Log(kLock, Id(self), flags); return Answer(gLockOk); }
uint32_t __fastcall BufUnlock(void *self, void *, uint32_t flags) { Log(kUnlock, Id(self), flags); return 1; }
cGZColorType *__fastcall BufNative(void *self, void *, cGZColorType *out)
{
    Log(kNative, Id(self));
    out->format = B(self)->nativeFormat;
    out->bits = B(self)->nativeBits;
    return out;
}
uint32_t __fastcall BufBpp(void *self, void *) { Log(kBpp, Id(self)); return B(self)->bpp; }
uint32_t __fastcall BufConvert(void *self, void *, uint32_t r, uint32_t g, uint32_t b)
{
    r &= 0xff, g &= 0xff, b &= 0xff;
    Log(kConvert, Id(self), r, g, b);
    return (r * 0x9e3779b1u) ^ (g * 0x85ebca6bu) ^ (b * 0xc2b2ae35u) ^ gSalt;
}
void *__fastcall BufBits(void *self, void *) { Log(kBits, Id(self)); return gSurface + 64; }
uint32_t __fastcall BufStride(void *self, void *) { Log(kStride, Id(self)); return B(self)->stride; }
uint32_t __fastcall BufOther(void *self, void *) { Log(kOther, Id(self)); return 0; }

// ---- the aggregate face and the director ----

uint32_t __fastcall AggRelease(void *self, void *) { Log(kAggRelease, Id(self)); return 1; }
uint32_t __fastcall AggNdQi(void *self, void *, uint32_t riid, void **out)
{
    Log(kNdQi, Id(self), riid, Id(out));
    const bool ok = Next() % 2 != 0;
    if (ok) *out = &gBuf;
    return Answer(ok);
}
void __fastcall AggSetOuter(void *self, void *, void *outer) { Log(kSetOuter, Id(self), Id(outer)); }

uint32_t __fastcall ComClassObject(void *self, void *, uint32_t clsid, uint32_t iid, void **out)
{
    Log(kClass, Id(self), clsid, iid, Id(out));
    const bool ok = clsid == Self::kBufferClsidAggregated ? gAggClass : gPlainClass;
    if (ok) *out = &gBuf;
    return Answer(ok);
}
void *__cdecl FakeDirector()
{
    Log(kDirector);
    return &gCom;
}

// ---- the record double ----

uint32_t __fastcall RecU8(void *, void *, void *dst)
{
    Log(kU8, gPos);
    if (gPos + 1 > gData.size()) return Answer(false);
    *static_cast<uint8_t *>(dst) = gData[gPos++];
    return Answer(true);
}
uint32_t __fastcall RecU16(void *, void *, void *dst)
{
    Log(kU16, gPos);
    if (gPos + 2 > gData.size()) return Answer(false);
    std::memcpy(dst, &gData[gPos], 2);
    gPos += 2;
    return Answer(true);
}
uint32_t __fastcall RecVoid(void *, void *, void *dst, uint32_t n)
{
    Log(kVoid, gPos, n);
    if (n > gData.size() - gPos) return Answer(false);
    if (n != 0) std::memcpy(dst, &gData[gPos], n);
    gPos += n;
    return Answer(true);
}
uint32_t __fastcall RecSize(void *, void *) { Log(kSize); return static_cast<uint32_t>(gData.size()); }
uint32_t __fastcall RecPos(void *, void *) { Log(kPos); return static_cast<uint32_t>(gPos); }
uint32_t __fastcall RecSeekAbs(void *, void *, uint32_t pos)
{
    Log(kSeekAbs, pos);
    if (pos > gData.size()) return Answer(false);
    gPos = pos;
    return Answer(true);
}
uint32_t __fastcall RecSeekRel(void *, void *, int32_t delta)
{
    Log(kSeekRel, delta);
    const int64_t target = static_cast<int64_t>(gPos) + delta;
    if (target < 0 || target > static_cast<int64_t>(gData.size())) return Answer(false);
    gPos = static_cast<size_t>(target);
    return Answer(true);
}

// ---- the string sink slot 14 copies into ----

void __fastcall SinkCopy(void *self, void *, const cRZString *other)
{
    Log(kCopy, Id(self), other->mString.size(), Hash(other->mString.data(), other->mString.size()), other->mRefCount,
        other->mVTable == GameAddress(cRZString::kVTable));
}

// ---- the TGA reader's pixel converter ----

void *__fastcall MakeFormatDouble(void *out, void *, const uint32_t *type)
{
    Log(kMakeFormat, type[0], type[1]);
    auto *o = static_cast<uint32_t *>(out);
    o[0] = type[0] * 3 + 0x10;
    o[1] = type[1];
    o[2] = 0xf00;
    o[3] = 0xf0;
    o[4] = 0xf;
    return out;
}
void *__fastcall BuildDouble(void *conv, void *, const uint32_t *a, const uint32_t *b)
{
    Log(kBuild, a[0], a[1], a[2], a[3], a[4], b[0], b[1], b[2], b[3], b[4]);
    return conv;
}
void __fastcall RowsDouble(void *, void *, void *bits, uint32_t stride, const uint8_t *pixels, int32_t step, uint32_t width,
                           uint32_t rows)
{
    const uint32_t span = static_cast<uint32_t>(step < 0 ? -step : step);
    uint32_t h = 0;
    if (rows != 0) {
        const uint8_t *first = step < 0 ? pixels + static_cast<ptrdiff_t>(step) * (rows - 1) : pixels;
        h = Hash(first, static_cast<size_t>(span) * rows);
    }
    Log(kRows, Id(bits), stride, step, width, rows, h);
}

// ---- the image's CRT, for the original side ----

FILE *__cdecl StandFopen(const char *path, const char *mode) { return std::fopen(path, mode); }
size_t __cdecl StandFread(void *buffer, size_t size, size_t count, FILE *file) { return std::fread(buffer, size, count, file); }
int __cdecl StandFseek(FILE *file, long offset, int origin) { return std::fseek(file, offset, origin); }
int __cdecl StandFclose(FILE *file) { return std::fclose(file); }
void *__cdecl StandMemset(void *dst, int value, size_t n) { return std::memset(dst, value, n); }

void *__cdecl GameMalloc(size_t n) { return std::malloc(n); }
void __cdecl GameFree(void *p) { std::free(p); }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t n) { ::operator delete(p, n); }

template <typename T> void *Code(T fn) { return reinterpret_cast<void *>(fn); }

void InitTables()
{
    for (auto &slot : gBufTable) slot = Code(&BufOther);
    gBufTable[0] = Code(&BufQi);
    gBufTable[1] = Code(&BufAddRef);
    gBufTable[2] = Code(&BufRelease);
    gBufTable[3] = Code(&BufInit);
    gBufTable[6] = Code(&BufLock);
    gBufTable[7] = Code(&BufUnlock);
    gBufTable[15] = Code(&BufNative);
    gBufTable[16] = Code(&BufBpp);
    gBufTable[30] = Code(&BufConvert);
    gBufTable[34] = Code(&BufBits);
    gBufTable[35] = Code(&BufStride);
    gBuf.vtable = gBuf2.vtable = gBufTable;
    for (auto &slot : gAggTable) slot = Code(&BufOther);
    gAggTable[2] = Code(&AggRelease);
    gAggTable[3] = Code(&AggNdQi);
    gAggTable[4] = Code(&AggSetOuter);
    gAgg.vtable = gAggTable;
    for (auto &slot : gComTable) slot = Code(&BufOther);
    gComTable[3] = Code(&ComClassObject);
    gCom.vtable = gComTable;
    for (auto &table : gRecordTable) {
        for (auto &slot : table) slot = Code(&BufOther);
        table[5] = Code(&RecU8);
        table[9] = Code(&RecU16);
        table[27] = Code(&RecVoid);
        table[52] = Code(&RecSize);
        table[55] = Code(&RecSeekAbs);
        table[56] = Code(&RecSeekRel);
    }
    gRecordTable[0][54] = gRecordTable[1][54] = Code(&RecPos);
    for (auto &slot : gSinkTable) slot = Code(&BufOther);
    gSinkTable[17] = Code(&SinkCopy);
    gSink.vtable = gSinkTable;
}

struct Patches {
    shape::CodePatch gameNew{0x824632, Code(&GameNew)};
    shape::CodePatch gameDelete{0x824624, Code(&GameDelete)};
    shape::CodePatch gameMalloc{0x8252b1, Code(&GameMalloc)};
    shape::CodePatch gameFree{0x6c4b50, Code(&GameFree)};
    shape::CodePatch fopenPatch{0x83aa0f, Code(&StandFopen)};
    shape::CodePatch freadPatch{0x8398bb, Code(&StandFread)};
    shape::CodePatch fseekPatch{0x839cd3, Code(&StandFseek)};
    shape::CodePatch fclosePatch{0x838ff4, Code(&StandFclose)};
    shape::CodePatch memsetPatch{0x8281e0, Code(&StandMemset)};
    shape::CodePatch makeFormat{0x9cd6a0, Code(&MakeFormatDouble)};
    shape::CodePatch build{0x9cea90, Code(&BuildDouble)};
    shape::CodePatch rows{0x9cd890, Code(&RowsDouble)};
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &FakeDirector};
    Patches()
    {
        InitTables();
        gLog.reserve(1 << 18);
        gData.reserve(1 << 16); // assigned inside the recorded span: no allocation to free
    }
};

// What the buffer and the director do in one case.
struct World {
    uint32_t format, bits;                 // the resource's colour type
    uint32_t bpp, stride;                  // the buffer's
    uint32_t nativeFormat, nativeBits;     // its native colour type
    uint32_t salt;
    bool initOk, lockOk, aggClass, plainClass;
};

World RandomWorld(std::mt19937 &rng)
{
    World w;
    w.format = rng() % 5 == 0 ? rng() % 10 : 1;
    w.bits = rng() % 4 == 0 ? rng() % 40 : 0;
    static constexpr uint32_t kBpps[] = {16, 16, 16, 8, 24, 32, 32, 0, 1, 4, 15, 64};
    w.bpp = kBpps[rng() % 12];
    const uint32_t bytes = w.bpp >= 8 ? w.bpp / 8 : 1;
    w.stride = (bytes * 10 + 4 * (rng() % 4) + 3) & ~3u;
    w.nativeFormat = rng() % 3 != 0 ? 7 : rng() % 10;
    w.nativeBits = rng() % 2 ? 16 : 32;
    w.salt = rng();
    w.initOk = rng() % 12 != 0;
    w.lockOk = rng() % 12 != 0;
    w.aggClass = rng() % 5 != 0;
    w.plainClass = rng() % 4 != 0;
    if (!w.aggClass && !w.plainClass) w.plainClass = true; // the readers need a buffer (the original dereferences none)
    return w;
}

// Set the world up and fill the surface with the seed's bytes: the same for each side.
void Apply(const World &w, uint32_t seed)
{
    gInitOk = w.initOk;
    gLockOk = w.lockOk;
    gAggClass = w.aggClass;
    gPlainClass = w.plainClass;
    gSalt = w.salt;
    gBuf = {gBufTable, 0, 0, w.bpp, w.stride, w.nativeFormat, w.nativeBits};
    gBuf2 = {gBufTable, 0, 0, w.bpp, w.stride, w.nativeFormat, w.nativeBits};
    std::mt19937 fill(seed * 31337u + 5u);
    for (auto &byte : gSurface) byte = static_cast<uint8_t>(fill());
}

struct Outcome {
    uint32_t result = 0;
    std::vector<int64_t> log;
    std::vector<uint8_t> state;
};

// One side: the call runs between a log reset and the surface capture, its frees recorded.
template <typename Call> Outcome Side(uint32_t seed, Call call)
{
    gLog.clear();
    gPos = 0;
    gObj = nullptr;
    gRng.seed(seed * 7919u + 17u);
    Outcome out;
    {
        difftest::FreeRecorder frees;
        out.result = call();
        frees.Stop();
        size_t sizes[256];
        const size_t n = frees.SortedSizes(sizes, 256);
        Log(0xf7ee, n);
        for (size_t i = 0; i < n; ++i) Log(sizes[i]);
        Log(frees.Overflowed());
    }
    out.log = gLog;
    out.state.assign(gSurface, gSurface + sizeof(gSurface));
    return out;
}

void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const Outcome &a, const Outcome &b)
{
    ++ctx.cases;
    if (a.result != b.result) ctx.Fail("%s seed %u: result original %u, ours %u", what, seed, a.result, b.result);
    if (a.log != b.log) {
        size_t i = 0;
        while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
        ctx.Fail("%s seed %u: calls differ at %zu (%zu / %zu values; original %lld, ours %lld)", what, seed, i, a.log.size(),
                 b.log.size(), i < a.log.size() ? a.log[i] : -1LL, i < b.log.size() ? b.log[i] : -1LL);
    }
    if (a.state != b.state) ctx.Fail("%s seed %u: surfaces differ", what, seed);
}

uint32_t WordAt(const uint8_t *object, size_t offset)
{
    uint32_t value;
    std::memcpy(&value, object + offset, 4);
    return value;
}

// The object after a call: the fields, the error rows by content.
void LogObject(const uint8_t *object)
{
    Log(kObject, Id(reinterpret_cast<const void *>(WordAt(object, 0x20))), Id(reinterpret_cast<const void *>(WordAt(object, 0x24))),
        WordAt(object, 0x28), WordAt(object, 0x2c));
    const auto *first = reinterpret_cast<const uint32_t *>(WordAt(object, 0x30));
    const auto *last = reinterpret_cast<const uint32_t *>(WordAt(object, 0x34));
    Log(last - first);
    for (const uint32_t *p = first; p < last; ++p) Log(*p);
}

// The resource on one side: the original's constructor, or our adapter, over the same memory.
uint8_t *Make(difftest::Context &ctx, int side, const World &w)
{
    auto *memory = static_cast<uint8_t *>(::operator new(kResourceSize));
    std::memset(memory, 0xcd, kResourceSize);
    gObj = memory;
    if (side == 0)
        ctx.Original<void *(__thiscall *)(void *, uint32_t, uint32_t)>(0x9b64a0)(memory, w.format, w.bits);
    else
        Adapters::Construct(memory, nullptr, w.format, w.bits);
    return memory;
}
void Destroy(difftest::Context &ctx, int side, uint8_t *memory)
{
    if (side == 0)
        ctx.Original<void(__thiscall *)(void *)>(0x9b6590)(memory);
    else
        Adapters::Destruct(reinterpret_cast<Self *>(memory), nullptr);
    gObj = nullptr;
    ::operator delete(memory);
}
// The original's reader entry points (this in ecx) against ours.
Self *Obj(uint8_t *memory) { return reinterpret_cast<Self *>(memory); }

// Error rows to start from: the original's vector is raw memory, ours a real one.
void SetErrorRows(int side, uint8_t *memory, std::mt19937 &rng, uint32_t count)
{
    std::vector<uint32_t> words(count);
    for (auto &word : words) word = rng();
    if (side == 1) {
        Obj(memory)->mErrorRows = words;
        return;
    }
    if (count == 0) return;
    auto *block = static_cast<uint32_t *>(::operator new(count * 4));
    std::memcpy(block, words.data(), count * 4);
    const uint32_t first = reinterpret_cast<uint32_t>(block), last = first + count * 4;
    std::memcpy(memory + 0x30, &first, 4);
    std::memcpy(memory + 0x34, &last, 4);
    std::memcpy(memory + 0x38, &last, 4);
}

// ---- synthetic files ----

void Put16(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(std::vector<uint8_t> &out, uint32_t v)
{
    Put16(out, v & 0xffff);
    Put16(out, v >> 16);
}
void PatchWord(std::vector<uint8_t> &out, size_t at, uint32_t v) { std::memcpy(&out[at], &v, 4); }

// RLE8 rows of `pitch` bytes: runs, absolute runs, the odd delta (whose operands the original reads as the next run), a line
// end after each row and the end of the bitmap. Values below `limit`.
void MakeRle(std::mt19937 &rng, std::vector<uint8_t> &out, uint32_t pitch, uint32_t rows, uint32_t limit, bool simple = false)
{
    for (uint32_t y = 0; y < rows; ++y) {
        uint32_t x = 0;
        if (!simple && pitch >= 2 && rng() % 6 == 0) {
            out.insert(out.end(), {0, 2, 1, 0});
            x = 2;
        }
        while (x < pitch) {
            const uint32_t room = pitch - x;
            if (!simple && x != 0 && room >= 3 && rng() % 8 == 0) {
                out.insert(out.end(), {0, 2, 1, 0}); // a delta after other runs: the column it adds to counts what was written
                x += 2;
            } else if (simple || room < 3 || rng() % 3 != 0) {
                const uint32_t count = 1 + rng() % (room < 255 ? room : 255);
                out.push_back(static_cast<uint8_t>(count));
                out.push_back(static_cast<uint8_t>(rng() % limit));
                x += count;
            } else {
                const uint32_t cap = room < 255 ? room : 255;
                const uint32_t count = 3 + rng() % (cap - 2);
                out.push_back(0);
                out.push_back(static_cast<uint8_t>(count));
                for (uint32_t i = 0; i < count; ++i) out.push_back(static_cast<uint8_t>(rng() % limit));
                if (count & 1) out.push_back(0);
                x += count;
            }
        }
        out.insert(out.end(), {0, 0});
    }
    out.insert(out.end(), {0, 1});
}

// A BMP file. `record`: for the record reader (palette bytes only where it reads them, no 4 bit rows, RLE sized right).
std::vector<uint8_t> MakeBmp(std::mt19937 &rng, bool record)
{
    const uint32_t kind = rng() % 20;
    uint32_t bitCount = kind < 6 ? 4 : kind < 12 ? 8 : kind < 18 ? 24 : (rng() % 2 ? 16 : rng() % 2 ? 1 : 32);
    uint32_t compression = rng() % 10 < 7 ? 0 : rng() % 10 < 8 ? 1 : 2;
    if (compression == 1 && bitCount != 8 && rng() % 3 != 0) bitCount = 8;
    const uint32_t width = rng() % 12 == 0 ? 0 : 1 + rng() % 9;
    uint32_t height = rng() % 10;
    if (record && bitCount == 4) height = 0;
    if (!record && bitCount == 4 && compression == 1) compression = 0; // the file reader's RLE sizes differ for 4 bit
    uint32_t colorsUsed;
    if (bitCount == 4)
        colorsUsed = 16 + rng() % 241;
    else
        colorsUsed = rng() % 3 == 0 ? 0 : 1 + rng() % 256;
    const bool paletteRead = colorsUsed != 0 || compression == 1 || bitCount == 8;
    const uint32_t entries = colorsUsed != 0 ? colorsUsed : 0x100;
    const uint32_t paletteBytes = paletteRead ? entries * 4 : 0;

    uint32_t rowBytes;
    if (record || bitCount != 4)
        rowBytes = (bitCount >> 3) * width;
    else
        rowBytes = ((width & 1) + width) / 2;
    const uint32_t pitch = ((rowBytes - 1) | 3) + 1;
    const uint32_t limit = !record ? 256 : (paletteRead ? entries : 256);

    std::vector<uint8_t> pixels;
    if (compression == 1)
    {
        MakeRle(rng, pixels, pitch, height, bitCount == 8 ? limit : 256);
        if (!record && pixels.size() > 2 * height * pitch) {
            // the file reader takes twice the image size, no more: runs only, one per row
            pixels.clear();
            MakeRle(rng, pixels, pitch, height, bitCount == 8 ? limit : 256, true);
        }
    }
    else
        for (uint32_t i = 0; i < height * pitch; ++i) pixels.push_back(static_cast<uint8_t>(bitCount == 8 ? rng() % limit : rng()));

    uint32_t gap = !record && rng() % 3 == 0 ? 1 + rng() % 7 : 0;
    uint32_t offBits = 54 + paletteBytes + gap;
    if (compression != 1 && rng() % 15 == 0)
        offBits = rng() % 20 == 0 ? 0x80000000u + rng() % 100 : rng() % 2 ? 0x24 + rng() % 8 : rng() % 0x40;

    std::vector<uint8_t> out = {'B', 'M'};
    Put32(out, 0);
    Put32(out, 0);
    Put32(out, offBits);
    Put32(out, 40);
    Put32(out, width);
    Put32(out, height);
    Put16(out, 1);
    Put16(out, bitCount);
    Put32(out, compression);
    Put32(out, compression == 1 && rng() % 3 != 0 ? static_cast<uint32_t>(pixels.size()) : 0);
    Put32(out, rng());
    Put32(out, rng());
    Put32(out, colorsUsed);
    Put32(out, rng());
    for (uint32_t i = 0; i < paletteBytes; ++i) out.push_back(static_cast<uint8_t>(rng()));
    for (uint32_t i = 0; i < gap; ++i) out.push_back(static_cast<uint8_t>(rng()));
    out.insert(out.end(), pixels.begin(), pixels.end());
    if (!record) out.resize(out.size() > 14 + 0x428 + 16 ? out.size() : 14 + 0x428 + 16, 0x5a);
    uint32_t fileSize = static_cast<uint32_t>(out.size());
    if (compression != 1 && rng() % 12 == 0) fileSize = rng();
    PatchWord(out, 2, fileSize);
    if (rng() % 14 == 0) out[rng() % 2] = 'Z'; // not a bitmap
    // sizeImage as large as the file is refused; a smaller one that the record cannot supply makes the original decode
    // uninitialised bytes, so that is not generated.
    if (compression == 1 && record && rng() % 6 == 0) PatchWord(out, 34, fileSize);
    return out;
}

// A TGA file: the 18 byte header, the image id and the colour map, the pixels (runs of pixels for type 10), sometimes a footer.
std::vector<uint8_t> MakeTga(std::mt19937 &rng)
{
    static constexpr uint32_t kDepths[] = {16, 24, 32, 32, 24, 16, 8, 15, 0, 12, 32, 16};
    const uint32_t idLength = rng() % 4;
    const uint32_t imageKind = rng() % 24;
    const uint32_t imageType = imageKind < 11 ? 2 : imageKind < 17 ? 10 : imageKind < 18 ? 1 : imageKind < 19 ? 3 : imageKind < 20 ? 9
                             : imageKind < 21 ? 0x12 : imageKind < 22 ? 0x1a : imageKind < 23 ? 0x82 : 0x42;
    static constexpr uint32_t kMapBits[] = {0, 15, 16, 24, 32};
    const uint32_t mapLength = rng() % 4, mapBits = kMapBits[rng() % 5];
    const uint32_t width = rng() % 10, height = rng() % 10;
    const uint32_t depth = kDepths[rng() % 12];
    const uint32_t descriptor = rng() % 3 == 0 ? rng() & 0xff : (rng() % 2 ? 0x20 : 0) | (rng() % 9);
    const uint32_t bpp = (depth + 7) >> 3;

    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(idLength));
    out.push_back(static_cast<uint8_t>(rng() % 2));
    out.push_back(static_cast<uint8_t>(imageType));
    Put16(out, rng() % 5);
    Put16(out, mapLength);
    out.push_back(static_cast<uint8_t>(mapBits));
    Put16(out, rng() % 5);
    Put16(out, rng() % 5);
    Put16(out, width);
    Put16(out, height);
    out.push_back(static_cast<uint8_t>(depth));
    out.push_back(static_cast<uint8_t>(descriptor));
    for (uint32_t i = 0; i < idLength + mapLength * ((mapBits + 7) >> 3); ++i) out.push_back(static_cast<uint8_t>(rng()));

    const uint32_t pixels = width * height;
    if ((imageType & ~8u) != 2 || (imageType != 2 && imageType != 10)) {
        for (uint32_t i = 0; i < pixels * bpp; ++i) out.push_back(static_cast<uint8_t>(rng()));
    } else if (imageType == 2) {
        for (uint32_t i = 0; i < pixels * bpp; ++i) out.push_back(static_cast<uint8_t>(rng()));
        if (pixels * bpp != 0 && rng() % 15 == 0) out.pop_back(); // a short file: the record's read fails
    } else {
        uint32_t left = pixels;
        while (left > 0) {
            const uint32_t count = 1 + rng() % (left < 128 ? left : 128);
            const bool run = rng() % 2 != 0;
            out.push_back(static_cast<uint8_t>((run ? 0x80 : 0) | (count - 1)));
            for (uint32_t i = 0; i < (run ? 1 : count) * bpp; ++i) out.push_back(static_cast<uint8_t>(rng()));
            left -= count;
        }
    }
    if (rng() % 4 == 0)
        for (int i = 0; i < 26; ++i) out.push_back(static_cast<uint8_t>(rng()));
    return out;
}

} // namespace

// ---- construction and destruction ----

// 0x9b64a0 / 0x9b6590: the buffer asked of the director (the aggregated class first, the plain one when it is missing), the
// aggregate's outer set, and everything let go in the destructor's order (the aggregate's outer cleared, the aggregate and the
// buffer released, the rows freed). The object is compared field by field but for its vtables.
DIFF_TEST(PersistBufferResource_ConstructDestroy, 0x9b64a0)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed);
        World w = RandomWorld(rng);
        if (rng() % 6 == 0) w.aggClass = w.plainClass = false; // no buffer at all
        const uint32_t rows = rng() % 3 == 0 ? rng() % 20 : 0;
        const auto run = [&](int side) {
            return Side(seed, [&] {
                Apply(w, seed);
                std::mt19937 rowRng(seed);
                uint8_t *memory = Make(ctx, side, w);
                Log(0xc7);
                for (size_t i = 4; i < 0x1c; ++i) Log(memory[i]);
                for (size_t i = 0x20; i < 0x3c; ++i) Log(memory[i]);
                if (rows != 0) SetErrorRows(side, memory, rowRng, rows);
                Log(0xd7);
                Destroy(ctx, side, memory);
                return 1u;
            });
        };
        Compare(ctx, "ConstructDestroy", seed, run(0), run(1));
    }
}

// 0x9b6630 (slot 12, the scalar deleting destructor): the destructor body, then the block freed when bit 0 of the flags is set;
// returns the object. The original's through its own vtable, ours through the compiler's slot 12 of our vtable.
DIFF_TEST(PersistBufferResource_DeletingDestructor, 0x9b6630)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed);
        World w = RandomWorld(rng);
        if (rng() % 6 == 0) w.aggClass = w.plainClass = false;
        const uint32_t rows = rng() % 3 == 0 ? rng() % 20 : 0;
        const uint32_t flags = rng() % 4;
        const auto run = [&](int side) {
            return Side(seed, [&] {
                Apply(w, seed);
                std::mt19937 rowRng(seed);
                uint8_t *memory = Make(ctx, side, w);
                if (rows != 0) SetErrorRows(side, memory, rowRng, rows);
                void *result;
                if (side == 0)
                    result = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x9b6630)(memory, flags);
                else
                    result = RawSlot<12, void *, uint32_t>(memory, flags);
                Log(0xd8, result == memory, flags);
                gObj = nullptr;
                if ((flags & 1) == 0) ::operator delete(memory); // the body leaves the block to the caller
                return 1u;
            });
        };
        Compare(ctx, "DeletingDestructor", seed, run(0), run(1));
    }
}

// ---- the slots ----

// The slots on both sides through the object's vtables (the original's for the game's object): QueryInterface on the faces'
// iids and others (with and without the aggregate and the buffer), AddRef / Release, GetBuffer, QueryBuffer, SetBuffer,
// the label (slot 14) and the two folded slots that answer false.
// Also verifies, through the same slot calls: GetDescription vf14 0x9b67b0, the +0x1c face's GetBuffer 0x9b6910,
// QueryBuffer 0x9b68f0 and SetBuffer 0x9b80f0.
DIFF_TEST(PersistBufferResource_Slots, 0x9b7c80)
{
    Patches patches;
    static constexpr uint32_t kIids[] = {cIGZPersistBufferResource::kIid, 1, 0x456b8f1d, 0x12f, 0x20732180, 0, 0x06f1568f, 2};
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 rng(seed);
        World w = RandomWorld(rng);
        if (rng() % 6 == 0) w.aggClass = w.plainClass = false; // no buffer at all
        const auto run = [&](int side) {
            return Side(seed, [&] {
                Apply(w, seed);
                std::mt19937 ops(seed + 99);
                uint8_t *memory = Make(ctx, side, w);
                Self *resource = Obj(memory);
                resource->cGZPersistResource::AddRef(); // two counts, so the Releases below do not destroy it
                resource->cGZPersistResource::AddRef();
                for (int step = 0; step < 10; ++step) {
                    const uint32_t op = ops() % 9;
                    const uint32_t iid = ops() % 4 ? kIids[ops() % 8] : ops();
                    void *out = reinterpret_cast<void *>(0x7777);
                    switch (op) {
                    case 0:
                    case 1: {
                        const bool ok = resource->QueryInterface(iid, &out);
                        Log(0x51, ok, out == reinterpret_cast<void *>(0x7777) ? -2 : Id(out),
                            out == memory + 0x1c ? 1 : out == memory ? 2 : 0);
                        break;
                    }
                    case 2: Log(0x52, resource->AddRef()); break;
                    case 3:
                        if (WordAt(memory, 4) > 2) Log(0x53, resource->Release());
                        break;
                    case 4: {
                        cIGZBuffer *buffer = static_cast<cIGZPersistBufferResource *>(resource)->GetBuffer();
                        Log(0x54, Id(buffer));
                        break;
                    }
                    case 5: {
                        const bool ok = static_cast<cIGZPersistBufferResource *>(resource)->QueryBuffer(iid, &out);
                        Log(0x55, ok, out == reinterpret_cast<void *>(0x7777) ? -2 : Id(out));
                        break;
                    }
                    case 6: {
                        cIGZBuffer *buffer = ops() % 3 == 0 ? nullptr : ops() % 2 ? reinterpret_cast<cIGZBuffer *>(&gBuf2)
                                                                                    : reinterpret_cast<cIGZBuffer *>(&gBuf);
                        static_cast<cIGZPersistBufferResource *>(resource)->SetBuffer(buffer);
                        Log(0x56, Id(buffer));
                        break;
                    }
                    case 7: {
                        cRZString *sink = reinterpret_cast<cRZString *>(&gSink);
                        const bool ok = resource->GetDescription(sink->AsInterface());
                        Log(0x57, ok);
                        break;
                    }
                    default: Log(0x58, resource->Slot13(nullptr), resource->Slot15(&out)); break;
                    }
                }
                LogObject(memory);
                // leave the object as the destructor finds it: two counts held by the test
                Destroy(ctx, side, memory);
                return 1u;
            });
        };
        Compare(ctx, "Slots", seed, run(0), run(1));
    }
}

// ---- IsSupportedBitmap ----

// 0x9b8db0: compression 0 or 1 and 4, 8 or 24 bits per pixel.
DIFF_TEST(PersistBufferResource_IsSupportedBitmap, 0x9b8db0)
{
    Patches patches;
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, const BmpInfo *)>(0x9b8db0);
    World w{};
    Apply(w, 0);
    alignas(4) uint8_t memory[kResourceSize] = {};
    static constexpr uint32_t kBits[] = {0, 1, 2, 4, 8, 16, 24, 32, 0x18 + 0x10000, 12, 0x104};
    for (uint32_t compression = 0; compression < 5; ++compression)
        for (uint32_t bits : kBits) {
            BmpInfo info{};
            info.compression = compression;
            info.bitCount = static_cast<uint16_t>(bits);
            ++ctx.cases;
            const bool a = (original(memory, &info) & 0xff) != 0;
            const bool b = Obj(memory)->IsSupportedBitmap(&info);
            if (a != b) ctx.Fail("IsSupportedBitmap compression %u bits %u: original %d, ours %d", compression, bits, a, b);
        }
}

// ---- the readers ----

// 0x9b6930: a BMP file in the record, from its current position.
DIFF_TEST(PersistBufferResource_ReadBmpRecord, 0x9b6930)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::mt19937 rng(seed);
        const World w = RandomWorld(rng);
        const std::vector<uint8_t> bytes = MakeBmp(rng, true);
        const uint32_t again = rng() % 4 == 0;
        const auto run = [&](int side) {
            return Side(seed, [&] {
                Apply(w, seed);
                gData = bytes;
                gRecord.vtable = gRecordTable[side];
                uint8_t *memory = Make(ctx, side, w);
                uint32_t result = 0;
                for (uint32_t pass = 0; pass <= again; ++pass) {
                    gPos = 0;
                    if (side == 0)
                        result = ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t)>(0x9b6930)(memory, &gRecord, 0) & 0xff;
                    else
                        result = Obj(memory)->ReadBmpRecord(reinterpret_cast<Record *>(&gRecord), 0);
                    Log(0x5e, result, gPos);
                }
                LogObject(memory);
                Destroy(ctx, side, memory);
                return result;
            });
        };
        Compare(ctx, "ReadBmpRecord", seed, run(0), run(1));
    }
}

// 0x9b77b0: a TGA file in the record (true colour, raw or run-length), through the pixel converter or the dither.
DIFF_TEST(PersistBufferResource_ReadTgaRecord, 0x9b77b0)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 5000; ++seed) {
        std::mt19937 rng(seed);
        const World w = RandomWorld(rng);
        const std::vector<uint8_t> bytes = MakeTga(rng);
        const uint32_t again = rng() % 4 == 0;
        const auto run = [&](int side) {
            return Side(seed, [&] {
                Apply(w, seed);
                gData = bytes;
                gRecord.vtable = gRecordTable[side];
                uint8_t *memory = Make(ctx, side, w);
                uint32_t result = 0;
                for (uint32_t pass = 0; pass <= again; ++pass) {
                    gPos = pass == 0 ? 0 : bytes.size() / 2; // the reader seeks to 0 itself
                    if (side == 0)
                        result = ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t)>(0x9b77b0)(memory, &gRecord, 0) & 0xff;
                    else
                        result = Obj(memory)->ReadTgaRecord(reinterpret_cast<Record *>(&gRecord), 0);
                    Log(0x5f, result, gPos);
                }
                LogObject(memory);
                Destroy(ctx, side, memory);
                return result;
            });
        };
        Compare(ctx, "ReadTgaRecord", seed, run(0), run(1));
    }
}

// 0x9b6c30: a BMP file by path (the file is the test's own, in the temp directory).
DIFF_TEST(PersistBufferResource_ReadBmpFile, 0x9b6c30)
{
    Patches patches;
    char directory[MAX_PATH];
    GetTempPathA(sizeof(directory), directory);
    for (uint32_t seed = 0; seed < 2500; ++seed) {
        std::mt19937 rng(seed);
        const World w = RandomWorld(rng);
        const std::vector<uint8_t> bytes = MakeBmp(rng, false);
        const uint32_t format = rng() % 4 ? w.format : rng() % 10, bits = rng() % 4 ? w.bits : rng() % 40;
        const uint32_t kind = rng() % 20; // 0: no such file, 1: an empty path
        const std::string path = std::string(directory) + "ts2_nu_bmp_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(seed) + ".bmp"; // per process: suites in other worktrees share %TEMP%
        if (kind > 1) {
            FILE *file = std::fopen(path.c_str(), "wb");
            std::fwrite(bytes.data(), 1, bytes.size(), file);
            std::fclose(file);
        }
        const std::string used = kind == 1 ? std::string() : kind == 0 ? path + ".missing" : path;
        const auto run = [&](int side) {
            return Side(seed, [&] {
                Apply(w, seed);
                uint8_t *memory = Make(ctx, side, w);
                uint32_t result;
                {
                    StackString name(used);
                    if (side == 0)
                        result = ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t)>(
                                     0x9b6c30)(memory, &name.s, 7, format, bits, 9) & 0xff;
                    else
                        result = Obj(memory)->ReadBmpFile(name.s.AsInterface(), 7, format, bits, 9);
                }
                Log(0x5d, result);
                LogObject(memory);
                Destroy(ctx, side, memory);
                return result;
            });
        };
        Compare(ctx, "ReadBmpFile", seed, run(0), run(1));
        if (kind > 1) std::remove(path.c_str());
    }
}

// ---- the converter and the dithers ----

// 0x9b8140: the pixel rows of a bottom-up bitmap into the locked buffer: 4, 8 and 24 bits against the buffer's 8, 16, 24 and 32.
DIFF_TEST(PersistBufferResource_ConvertBitmap, 0x9b8140)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        std::mt19937 rng(seed);
        World w = RandomWorld(rng);
        w.lockOk = true;
        BmpInfo info{};
        info.size = 40;
        const uint32_t kind = rng() % 20;
        info.bitCount = static_cast<uint16_t>(kind < 6 ? 4 : kind < 12 ? 8 : kind < 19 ? 24 : 16);
        info.width = rng() % 12 == 0 ? 0 : 1 + rng() % 9;
        info.height = rng() % 9;
        for (auto &byte : info.palette) byte = static_cast<uint8_t>(rng());
        std::vector<uint8_t> pixels((info.height + 2) * (info.width * 4 + 8) + 64);
        for (auto &byte : pixels) byte = static_cast<uint8_t>(rng());
        const auto run = [&](int side) {
            return Side(seed, [&] {
                Apply(w, seed);
                uint8_t *memory = Make(ctx, side, w);
                const uint32_t result =
                    side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, const BmpInfo *, const uint8_t *)>(0x9b8140)(
                                    memory, &info, pixels.data()) & 0xff
                              : Obj(memory)->ConvertBitmap(&info, pixels.data());
                Log(0x5c, result);
                LogObject(memory);
                Destroy(ctx, side, memory);
                return result;
            });
        };
        Compare(ctx, "ConvertBitmap", seed, run(0), run(1));
    }
}

// 0x9b88e0: 24 bit rows into the 16 bit buffer, error-diffused (the error rows start from what an earlier call left).
DIFF_TEST(PersistBufferResource_DitherRgb24, 0x9b88e0)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::mt19937 rng(seed);
        World w = RandomWorld(rng);
        w.bpp = 16;
        w.stride = 4 * (6 + rng() % 8) + (rng() % 3 == 0 ? 1 : 0);
        BmpInfo info{};
        info.size = 40;
        info.bitCount = 24;
        info.width = rng() % 10;
        info.height = rng() % 9;
        const uint32_t pitch = ((info.width + 1) * 3) & ~3u;
        std::vector<uint8_t> pixels(info.height * pitch + 16);
        for (auto &byte : pixels) byte = static_cast<uint8_t>(rng() % 3 == 0 ? (rng() % 2 ? 0xff : 0) : rng());
        const uint32_t startRows = rng() % 2 ? 0 : rng() % 40;
        const uint32_t repeat = rng() % 3 == 0;
        const auto run = [&](int side) {
            return Side(seed, [&] {
                Apply(w, seed);
                std::mt19937 rowRng(seed);
                uint8_t *memory = Make(ctx, side, w);
                SetErrorRows(side, memory, rowRng, startRows);
                uint32_t result = 0;
                for (uint32_t pass = 0; pass <= repeat; ++pass) {
                    result = side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, const BmpInfo *, const uint8_t *)>(0x9b88e0)(
                                             memory, &info, pixels.data()) & 0xff
                                       : Obj(memory)->DitherRgb24(&info, pixels.data());
                    Log(0x5b, result);
                }
                LogObject(memory);
                Destroy(ctx, side, memory);
                return result;
            });
        };
        Compare(ctx, "DitherRgb24", seed, run(0), run(1));
    }
}

// 0x9b8b40: 32 bit B, G, R, A rows (upwards or downwards) into the 16 bit buffer, error-diffused.
DIFF_TEST(PersistBufferResource_DitherArgb32, 0x9b8b40)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::mt19937 rng(seed);
        World w = RandomWorld(rng);
        w.bpp = 16;
        w.stride = 4 * (6 + rng() % 8) + (rng() % 3 == 0 ? 1 : 0);
        const int32_t width = rng() % 10, height = rng() % 9;
        const bool upwards = rng() % 2 != 0;
        const int32_t span = width * 4 + 4 * (rng() % 3);
        std::vector<uint8_t> pixels(static_cast<size_t>(span) * (height + 1) + 16);
        for (auto &byte : pixels) byte = static_cast<uint8_t>(rng() % 3 == 0 ? (rng() % 2 ? 0xff : 0) : rng());
        const uint8_t *first = upwards ? pixels.data() + static_cast<size_t>(span) * (height ? height - 1 : 0) : pixels.data();
        const int32_t step = upwards ? -span : span;
        const uint32_t startRows = rng() % 2 ? 0 : rng() % 40;
        const uint32_t repeat = rng() % 3 == 0;
        const auto run = [&](int side) {
            return Side(seed, [&] {
                Apply(w, seed);
                std::mt19937 rowRng(seed);
                uint8_t *memory = Make(ctx, side, w);
                SetErrorRows(side, memory, rowRng, startRows);
                uint32_t result = 0;
                for (uint32_t pass = 0; pass <= repeat; ++pass) {
                    result = side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, int32_t, int32_t, const uint8_t *, int32_t)>(
                                             0x9b8b40)(memory, width, height, first, step) & 0xff
                                       : Obj(memory)->DitherArgb32(width, height, first, step);
                    Log(0x5a, result);
                }
                LogObject(memory);
                Destroy(ctx, side, memory);
                return result;
            });
        };
        Compare(ctx, "DitherArgb32", seed, run(0), run(1));
    }
}
