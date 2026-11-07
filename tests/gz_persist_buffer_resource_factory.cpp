// cGZPersistBufferResourceFactory (src/gz/cGZPersistBufferResourceFactory.h): original vs ours, one DIFF_TEST per body.
// Lane NP.
//
// Every object the bodies touch is a logging double: the buffers (cIGZBuffer: the slots the bodies call, answers drawn
// from a behaviour stream reseeded alike for each side), the serial record (typed reads from a script, raw reads filled
// from the stream, writes logged with a hash of their bytes), the graphics system (cached cell 0x10f51a4), the COM
// director (GetDirectorFrameworkObject 0x1ae18d stood in on both sides), the resource the factory builds (cGZPersistBufferResource
// is ours now, lane NU: its constructor 0x9b64a0 and file readers 0x9b6930 / 0x9b77b0 / 0x9b6c30 are patched over with
// loggers here, the destructor body is ours inlined) and the pixel codec (0x9b6670 compress, 0x9b6860 decompress, 0x9cdec0 the format fix-up: patched with
// loggers that write stream bytes, so the copies around them are checked byte for byte). The game's new / delete and
// malloc / free are patched to ours, so both sides allocate alike; every freed block is compared by size
// (FreeRecorder) and every surface byte afterwards.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "gz/cGZPersistBufferResourceFactory.h"
#include "gz/cIGZBuffer.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using Factory = cGZPersistBufferResourceFactory;
using Face = cGZPersistBufferResourceFactory::Face;

constexpr uint32_t kType = Factory::kResourceType;
constexpr uint32_t kGraphicSystemCell = 0x10f51a4;
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114, kIatLoadImageA = 0xd9a4c4, kIatGetObjectA = 0xd9a098,
                   kIatDeleteObject = 0xd9a0a8;

std::vector<int64_t> gLog;
std::mt19937 gRng; // the doubles' behaviour stream: reseeded alike before each side
uint32_t Next() { return gRng(); }
template <typename... T> void Log(T... values) { (gLog.push_back(static_cast<int64_t>(values)), ...); }

uint32_t Hash(const void *data, size_t n)
{
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(data)[i]) * 0x01000193;
    return h;
}

// ---- the world ----

constexpr int kBuffers = 3;
constexpr size_t kSurfaceBytes = 0x400;

struct FakeBuffer {
    void *const *vtable;
    uint32_t width, height, bpp, stride, format, bits;
    cGZRect bounds;
};
alignas(16) uint8_t gFactory[0x40];
FakeBuffer gBuf[kBuffers];
alignas(16) uint8_t gSurface[kBuffers][kSurfaceBytes];
void *gBufTable[48];

struct FakeRecord {
    void *const *vtable;
    uint32_t pad[3];
};
FakeRecord gRecord;
void *gRecordTable[64];

struct FakeObject {
    void *const *vtable;
};
FakeObject gGfx, gCom, gAgg;
void *gGfxTable[8], *gComTable[8], *gAggTable[8];

alignas(16) uint8_t gWorldRes[0x40]; // the resource ReadRecord is given
void *gWorldResTable[8], *gWorldFaceTable[8];
void *gNewResource = nullptr;          // the resource the call under test made (found by its first class request)
alignas(16) uint8_t gDib[0x400];       // the bitmap's bits

// The script: typed reads in order, which read (if any) fails, failure odds for the other calls.
std::vector<uint32_t> gReads;
size_t gReadPos;
int gFailRead;    // the index of the typed read that fails (-1 none)
uint32_t gFailMod; // other record calls fail when Next() % gFailMod == 0 (0: never)
uint32_t gWorldType, gTargetBuffer; // ReadRecord: the resource's type, the target buffer (kBuffers: none)

int64_t Id(const void *p)
{
    const auto a = reinterpret_cast<uintptr_t>(p);
    const auto in = [&](const void *base, size_t n) {
        const auto b = reinterpret_cast<uintptr_t>(base);
        return a >= b && a < b + n;
    };
    const auto off = [&](const void *base) { return static_cast<int64_t>(a - reinterpret_cast<uintptr_t>(base)); };
    if (p == nullptr) return 0;
    if (in(gFactory, sizeof(gFactory))) return 0x10000 + off(gFactory);
    if (in(gBuf, sizeof(gBuf))) return 0x20000 + off(gBuf);
    if (in(gSurface, sizeof(gSurface))) return 0x30000 + off(gSurface);
    if (in(&gRecord, sizeof(gRecord))) return 0x40000 + off(&gRecord);
    if (in(gWorldRes, sizeof(gWorldRes))) return 0x50000 + off(gWorldRes);
    if (p == &gGfx) return 0x60000;
    if (p == &gCom) return 0x70000;
    if (p == &gAgg) return 0xa0000;
    if (gNewResource != nullptr && in(gNewResource, Factory::kResourceSize)) return 0x80000 + off(gNewResource);
    if (in(gDib, sizeof(gDib))) return 0x90000 + off(gDib);
    return -1; // a heap block or a local of the caller
}

bool Fails() { return gFailMod != 0 && Next() % gFailMod == 0; }
// A bool as the game's code returns it: only the low byte counts.
uint32_t Answer(bool ok) { return (Next() & 0xffffff00u) | (ok ? 1u : 0u); }

// ---- buffer double ----

enum : int64_t { kAddRef = 100, kRelease, kInit, kLock, kUnlock, kWidth, kHeight, kBounds, kNative, kBpp, kSetPixel, kBlt,
                 kConvert, kBits, kStride, kOther };

FakeBuffer *B(void *self) { return static_cast<FakeBuffer *>(self); }
uint32_t __fastcall BufAddRef(void *self, void *) { Log(kAddRef, Id(self)); return 7; }
uint32_t __fastcall BufRelease(void *self, void *) { Log(kRelease, Id(self)); return 3; }
uint32_t __fastcall BufInit(void *self, void *, uint32_t w, uint32_t h, uint32_t f, uint32_t b)
{
    Log(kInit, Id(self), w, h, f, b);
    return Answer(Next() % 5 != 0);
}
uint32_t __fastcall BufLock(void *self, void *, uint32_t flags) { Log(kLock, Id(self), flags); return Answer(Next() % 6 != 0); }
uint32_t __fastcall BufUnlock(void *self, void *, uint32_t flags) { Log(kUnlock, Id(self), flags); return 1; }
uint32_t __fastcall BufWidth(void *self, void *) { Log(kWidth, Id(self)); return B(self)->width; }
uint32_t __fastcall BufHeight(void *self, void *) { Log(kHeight, Id(self)); return B(self)->height; }
const cGZRect *__fastcall BufBounds(void *self, void *) { Log(kBounds, Id(self)); return &B(self)->bounds; }
cGZColorType *__fastcall BufNative(void *self, void *, cGZColorType *out)
{
    Log(kNative, Id(self));
    out->format = B(self)->format;
    out->bits = B(self)->bits;
    return out;
}
uint32_t __fastcall BufBpp(void *self, void *) { Log(kBpp, Id(self)); return B(self)->bpp; }
void __fastcall BufSetPixel(void *self, void *, uint32_t x, uint32_t y, uint32_t v) { Log(kSetPixel, Id(self), x, y, v); }
uint32_t __fastcall BufBlt(void *self, void *, void *src, const cGZRect *from, const cGZRect *to, const cGZRect *clip)
{
    Log(kBlt, Id(self), Id(src), Id(from), Id(to), Id(clip));
    return Answer(Next() % 2 != 0);
}
uint32_t __fastcall BufConvert(void *self, void *, uint32_t r, uint32_t g, uint32_t b)
{
    r &= 0xff, g &= 0xff, b &= 0xff;
    Log(kConvert, Id(self), r, g, b);
    return 0x5a5a0000u ^ (r << 11) ^ (g << 5) ^ b ^ (Next() & 0xffff0000u);
}
void *__fastcall BufBits(void *self, void *) { Log(kBits, Id(self)); return gSurface[B(self) - gBuf]; }
uint32_t __fastcall BufStride(void *self, void *) { Log(kStride, Id(self)); return B(self)->stride; }
uint32_t __fastcall BufOther(void *self, void *) { Log(kOther, Id(self)); return 0; }

// ---- record double ----

enum : int64_t { kGetU32 = 200, kGetS32, kGetArray, kGetVoid, kSetU32, kSetS32, kSetVoid, kSeek, kRecOther };

// ReadRecord's mode: the record is a byte image (the readers of the resource run on it), not a script.
bool gDataMode = false;
std::vector<uint8_t> gData;
size_t gPos = 0;

uint32_t TypedRead(int64_t kind, void *dst)
{
    const size_t index = gReadPos++;
    Log(kind, index);
    if (static_cast<int>(index) == gFailRead) return Answer(false);
    const uint32_t value = index < gReads.size() ? gReads[index] : Next();
    std::memcpy(dst, &value, 4);
    return Answer(true);
}
uint32_t __fastcall RecU32(void *, void *, void *dst) { return TypedRead(kGetU32, dst); }
uint32_t __fastcall RecS32(void *, void *, void *dst) { return TypedRead(kGetS32, dst); }
uint32_t __fastcall RecArray(void *, void *, void *dst, uint32_t count)
{
    Log(kGetArray, Id(dst), count);
    if (Fails()) return Answer(false);
    for (uint32_t i = 0; i < count; ++i) static_cast<uint32_t *>(dst)[i] = Next();
    return Answer(true);
}
uint32_t __fastcall RecVoid(void *, void *, void *dst, uint32_t n)
{
    Log(kGetVoid, Id(dst), n);
    if (gDataMode) {
        if (n > gData.size() - gPos) return Answer(false);
        if (n != 0) std::memcpy(dst, &gData[gPos], n);
        gPos += n;
        return Answer(true);
    }
    if (Fails()) return Answer(false);
    for (uint32_t i = 0; i < n; ++i) static_cast<uint8_t *>(dst)[i] = static_cast<uint8_t>(Next());
    return Answer(true);
}
uint32_t __fastcall RecSetU32(void *, void *, uint32_t v) { Log(kSetU32, v); return Answer(!Fails()); }
uint32_t __fastcall RecSetS32(void *, void *, int32_t v) { Log(kSetS32, v); return Answer(!Fails()); }
uint32_t __fastcall RecSetVoid(void *, void *, const void *src, uint32_t n)
{
    Log(kSetVoid, Id(src), n, Hash(src, n));
    return Answer(!Fails());
}
uint32_t __fastcall RecSeek(void *, void *, uint32_t pos)
{
    Log(kSeek, pos);
    if (gDataMode) {
        if (pos > gData.size()) return Answer(false);
        gPos = pos;
        return Answer(true);
    }
    return Answer(!Fails());
}
uint32_t __fastcall RecDataU8(void *, void *, void *dst)
{
    Log(kRecOther, 5, gPos);
    if (gPos + 1 > gData.size()) return Answer(false);
    *static_cast<uint8_t *>(dst) = gData[gPos++];
    return Answer(true);
}
uint32_t __fastcall RecDataU16(void *, void *, void *dst)
{
    Log(kRecOther, 9, gPos);
    if (gPos + 2 > gData.size()) return Answer(false);
    std::memcpy(dst, &gData[gPos], 2);
    gPos += 2;
    return Answer(true);
}
uint32_t __fastcall RecDataSize(void *, void *) { Log(kRecOther, 52); return static_cast<uint32_t>(gData.size()); }
uint32_t __fastcall RecDataPos(void *, void *) { Log(kRecOther, 54); return static_cast<uint32_t>(gPos); }
uint32_t __fastcall RecDataRel(void *, void *, int32_t delta)
{
    Log(kRecOther, 56, delta);
    const int64_t target = static_cast<int64_t>(gPos) + delta;
    if (target < 0 || target > static_cast<int64_t>(gData.size())) return Answer(false);
    gPos = static_cast<size_t>(target);
    return Answer(true);
}
uint32_t __fastcall RecOther(void *, void *) { Log(kRecOther); return 0; }

// ---- graphics system, COM director ----

enum : int64_t { kCreateBuffer = 300, kClassObject, kDirector };

uint32_t PickBuffer(void **out)
{
    const uint32_t k = Next() % kBuffers;
    *out = &gBuf[k];
    return k;
}
uint32_t __fastcall GfxCreateBuffer(void *self, void *, void **out)
{
    Log(kCreateBuffer, Id(self));
    PickBuffer(out); // the double always hands one out: the original calls through a null one (see ReadBuffer)
    return Answer(Next() % 5 != 0);
}
uint32_t __fastcall ComClassObject(void *self, void *, uint32_t clsid, uint32_t iid, void **out)
{
    // The first request of a resource's life is for its +0x20 field: that finds the resource (ours is made by the new
    // of the toolchain, the original's by the patched game's, so neither is the other's to tell).
    if (gNewResource == nullptr) gNewResource = reinterpret_cast<uint8_t *>(out) - 0x20;
    Log(kClassObject, Id(self), clsid, iid, Id(out));
    const bool ok = Next() % 5 != 0 || gDataMode; // ReadRecord's local resource has no null check on its buffer
    if (ok) PickBuffer(out);
    return Answer(ok);
}
void *__cdecl FakeDirectorObject()
{
    Log(kDirector);
    return &gCom;
}

// ---- the world's resource double (ReadRecord's argument), the buffer's aggregate face, the CRT ----

enum : int64_t { kResType = 400, kGetBuffer, kResOther, kQi, kSetOuter, kNdQi };

// The buffer's aggregate face (cIGZAggregate): the real resource constructor hands it its +0x1c face as the outer unknown.
uint32_t __fastcall AggQi(void *self, void *, uint32_t riid, void **out)
{
    Log(kNdQi, Id(self), riid, Id(out));
    *out = &gBuf[0];
    return Answer(Next() % 2 != 0);
}
void __fastcall AggSetOuter(void *self, void *, void *outer) { Log(kSetOuter, Id(self), Id(outer)); }
// The buffer's slot 0: the aggregate for its face (the constructor does not test for it), else random.
uint32_t __fastcall BufQi(void *self, void *, uint32_t riid, void **out)
{
    Log(kQi, Id(self), riid, Id(out));
    if (riid == 0x12f) {
        *out = &gAgg;
        return Answer(true);
    }
    *out = &gBuf[0];
    return Answer(Next() % 2 != 0);
}
// The image's fopen, the CRT of the original side (ours runs the toolchain's): the real one, unlogged.
FILE *__cdecl StandFopen(const char *path, const char *mode) { return std::fopen(path, mode); }

uint32_t __fastcall WorldType(void *self, void *) { Log(kResType, Id(self)); return gWorldType; }
void *__fastcall WorldGetBuffer(void *self, void *)
{
    Log(kGetBuffer, Id(self));
    return gTargetBuffer < kBuffers ? &gBuf[gTargetBuffer] : nullptr;
}
uint32_t __fastcall ResOther(void *self, void *) { Log(kResOther, Id(self)); return 0; }

// ---- codec doubles ----

enum : int64_t { kCompress = 500, kDecompress, kFinish, kMalloc, kNew };

bool __stdcall FakeCompress(const void *src, uint32_t size, void *dst, uint32_t *outSize)
{
    Log(kCompress, Id(src), size, Hash(src, size), Id(dst), *outSize);
    const uint32_t produced = Next() % (size + 8);
    for (uint32_t i = 0; i < produced; ++i) static_cast<uint8_t *>(dst)[i] = static_cast<uint8_t>(Next());
    const bool ok = Next() % 4 != 0;
    *outSize = ok ? produced : 0;
    return ok;
}
bool __stdcall FakeDecompress(const void *src, uint32_t size, void *dst, uint32_t dstSize, uint32_t *outSize)
{
    Log(kDecompress, Id(src), size, Hash(src, size), Id(dst), dstSize, *outSize);
    for (uint32_t i = 0; i < dstSize; ++i) static_cast<uint8_t *>(dst)[i] = static_cast<uint8_t>(Next());
    *outSize = Next() % 5 == 0 ? dstSize + 1 : dstSize;
    return Next() % 5 != 0;
}
uint32_t __cdecl FakeFinish(void *buffer, uint32_t format)
{
    Log(kFinish, Id(buffer), format);
    return Answer(Next() % 4 != 0);
}

void *__cdecl GameMalloc(size_t n) { return std::malloc(n); }
void __cdecl GameFree(void *p) { std::free(p); }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t n) { ::operator delete(p, n); }

// ---- Windows doubles (vf16_5) ----

enum : int64_t { kModule = 600, kLoadImage, kGetObject, kDeleteObject };
int32_t gDibWidth, gDibHeight;
uint16_t gDibBits;
HMODULE WINAPI FakeGetModuleHandleA(LPCSTR name)
{
    Log(kModule, reinterpret_cast<uintptr_t>(name));
    return reinterpret_cast<HMODULE>(0x400000);
}
HANDLE WINAPI FakeLoadImageA(HINSTANCE module, LPCSTR name, UINT type, int cx, int cy, UINT flags)
{
    Log(kLoadImage, reinterpret_cast<uintptr_t>(module), reinterpret_cast<uintptr_t>(name), type, cx, cy, flags);
    return Next() % 6 == 0 ? nullptr : reinterpret_cast<HANDLE>(0x1234);
}
int WINAPI FakeGetObjectA(HANDLE object, int size, LPVOID out)
{
    Log(kGetObject, reinterpret_cast<uintptr_t>(object), size);
    if (Next() % 6 == 0) return 0;
    DIBSECTION section{};
    section.dsBm.bmWidth = gDibWidth;
    section.dsBm.bmHeight = gDibHeight;
    section.dsBm.bmBitsPixel = gDibBits;
    section.dsBm.bmBits = gDib;
    std::memcpy(out, &section, sizeof(section) < static_cast<size_t>(size) ? sizeof(section) : size);
    return size;
}
BOOL WINAPI FakeDeleteObject(HGDIOBJ object)
{
    Log(kDeleteObject, reinterpret_cast<uintptr_t>(object));
    return TRUE;
}

struct ImportSlot {
    difftest::Context &ctx;
    uint32_t rva;
    void *saved;
    ImportSlot(difftest::Context &c, uint32_t r, void *fake) : ctx(c), rva(r), saved(*c.image.At<void *>(r))
    {
        *ctx.image.At<void *>(rva) = fake;
    }
    ~ImportSlot() { *ctx.image.At<void *>(rva) = saved; }
    ImportSlot(const ImportSlot &) = delete;
    ImportSlot &operator=(const ImportSlot &) = delete;
};

// ---- set-up ----

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
    gBufTable[9] = Code(&BufWidth);
    gBufTable[10] = Code(&BufHeight);
    gBufTable[12] = Code(&BufBounds);
    gBufTable[15] = Code(&BufNative);
    gBufTable[17] = Code(&BufBpp);
    gBufTable[22] = Code(&BufSetPixel);
    gBufTable[29] = Code(&BufBlt);
    gBufTable[30] = Code(&BufConvert);
    gBufTable[34] = Code(&BufBits);
    gBufTable[35] = Code(&BufStride);
    for (auto &b : gBuf) b.vtable = gBufTable;

    for (auto &slot : gRecordTable) slot = Code(&RecOther);
    gRecordTable[12] = Code(&RecArray);
    gRecordTable[13] = Code(&RecU32);
    gRecordTable[15] = Code(&RecS32);
    gRecordTable[27] = Code(&RecVoid);
    gRecordTable[37] = Code(&RecSetU32);
    gRecordTable[39] = Code(&RecSetS32);
    gRecordTable[51] = Code(&RecSetVoid);
    gRecordTable[55] = Code(&RecSeek);
    gRecordTable[5] = Code(&RecDataU8);
    gRecordTable[9] = Code(&RecDataU16);
    gRecordTable[52] = Code(&RecDataSize);
    gRecordTable[54] = Code(&RecDataPos);
    gRecordTable[56] = Code(&RecDataRel);
    gRecord.vtable = gRecordTable;

    for (auto &slot : gGfxTable) slot = Code(&BufOther);
    gGfxTable[3] = Code(&GfxCreateBuffer);
    gGfx.vtable = gGfxTable;
    for (auto &slot : gComTable) slot = Code(&BufOther);
    gComTable[3] = Code(&ComClassObject);
    gCom.vtable = gComTable;

    for (auto &slot : gAggTable) slot = Code(&BufOther);
    gAggTable[2] = Code(&BufRelease);
    gAggTable[3] = Code(&AggQi);
    gAggTable[4] = Code(&AggSetOuter);
    gAgg.vtable = gAggTable;
    for (auto &slot : gWorldResTable) slot = Code(&ResOther);
    gWorldResTable[5] = Code(&WorldType);
    for (auto &slot : gWorldFaceTable) slot = Code(&ResOther);
    gWorldFaceTable[3] = Code(&WorldGetBuffer);
    *reinterpret_cast<void **>(gWorldRes) = gWorldResTable;
    *reinterpret_cast<void **>(gWorldRes + 0x1c) = gWorldFaceTable;
}

// Buffers with random dimensions (stride at least the packed row, sometimes padded) and surfaces of random bytes.
void BuildBuffers(std::mt19937 &rng, uint32_t bppHint)
{
    for (auto &b : gBuf) {
        b.width = rng() % 9;
        b.height = rng() % 9;
        b.bpp = rng() % 4 == 0 ? rng() % 5 : bppHint;
        const uint32_t row = b.bpp * b.width;
        b.stride = rng() % 2 ? row : row + 4 * (1 + rng() % 4);
        b.format = rng() % 12;
        b.bits = rng() % 40;
        b.bounds = {static_cast<int32_t>(rng() % 9), static_cast<int32_t>(rng() % 9), static_cast<int32_t>(rng() % 9),
                    static_cast<int32_t>(rng() % 9)};
    }
    for (auto &s : gSurface)
        for (auto &byte : s) byte = static_cast<uint8_t>(rng());
}

struct Outcome {
    uint32_t result = 0;
    std::vector<int64_t> log;
    std::vector<uint8_t> state;
};

// Run one side: the world is rebuilt by the caller first; this reseeds the behaviour stream, records the frees and
// collects the log, the surfaces and the object.
template <typename Call> Outcome Side(uint32_t seed, Call call)
{
    gLog.clear();
    gReadPos = 0;
    gNewResource = nullptr;
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
    out.state.assign(&gSurface[0][0], &gSurface[0][0] + sizeof(gSurface));
    out.state.insert(out.state.end(), gFactory, gFactory + sizeof(gFactory));
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
    if (a.state != b.state) ctx.Fail("%s seed %u: surfaces or factory differ", what, seed);
}

void FillFactory(std::mt19937 &rng)
{
    for (auto &byte : gFactory) byte = static_cast<uint8_t>(rng());
    *reinterpret_cast<void **>(gFactory + 0x10) = GameAddress(Factory::kFaceVTable);
}

struct Patches {
    shape::CodePatch gameNew{0x824632, Code(&GameNew)};
    shape::CodePatch gameDelete{0x824624, Code(&GameDelete)};
    shape::CodePatch gameMalloc{0x8252b1, Code(&GameMalloc)};
    shape::CodePatch gameFree{0x6c4b50, Code(&GameFree)};
    shape::CodePatch fopenPatch{0x83aa0f, Code(&StandFopen)};
    shape::CodePatch compress{0x9b6670, Code(&FakeCompress)};
    shape::CodePatch decompress{0x9b6860, Code(&FakeDecompress)};
    shape::CodePatch finish{0x9cdec0, Code(&FakeFinish)};
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &FakeDirectorObject};
    void *savedGfx;
    Patches()
    {
        InitTables();
        gDataMode = false;
        gFailMod = 0;
        gFailRead = -1;
        gReads.clear();
        savedGfx = *GameAddress<void *>(kGraphicSystemCell);
        *GameAddress<void *>(kGraphicSystemCell) = &gGfx;
        gLog.reserve(1 << 16);
    }
    ~Patches() { *GameAddress<void *>(kGraphicSystemCell) = savedGfx; }
};

Factory *Ours() { return reinterpret_cast<Factory *>(gFactory); }
Face *OurFace() { return reinterpret_cast<Face *>(gFactory + 0x10); }

} // namespace

// ---- primary face ----

// 0x9b7d00: the factory face for its iid (AddRef through slot 1), anything else to the base's QueryInterface.
DIFF_TEST(BufferResourceFactory_QueryInterface, 0x9b7d00)
{
    Patches patches;
    void *table[12];
    for (auto &slot : table) slot = Code(&BufOther);
    table[1] = Code(&BufAddRef);
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x9b7d00);
    static constexpr uint32_t kIids[] = {Factory::kIid, 0xa56b8f17, 1, 0x456b8f1d, 0x656ddc27, 0};
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t iid = rng() % 4 ? kIids[rng() % 6] : static_cast<uint32_t>(rng());
        void *outA = &gRecord, *outB = &gRecord;
        const auto run = [&](auto call) {
            rng.seed(seed);
            FillFactory(rng);
            *reinterpret_cast<void **>(gFactory) = table;
            return Side(seed, call);
        };
        auto a = run([&] { return static_cast<uint32_t>(orig(gFactory, iid, &outA)); });
        auto b = run([&] { return static_cast<uint32_t>(Ours()->QueryInterface(iid, &outB)); });
        Compare(ctx, "QueryInterface", seed, a, b);
        if (Id(outA) != Id(outB)) ctx.Fail("QueryInterface seed %u: out differs", seed);
    }
}

// 0x9b6710: slot 4 for image records (the stored format, bits 0), false otherwise.
DIFF_TEST(BufferResourceFactory_Create, 0x9b6710)
{
    Patches patches;
    void *table[12];
    for (auto &slot : table) slot = Code(&BufOther);
    struct Slot4 {
        static uint32_t __fastcall Call(void *self, void *, uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
        {
            Log(kOther, Id(self), type, riid, Id(out), a4, a5);
            return Answer(Next() % 2 != 0);
        }
    };
    table[4] = Code(&Slot4::Call);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **)>(0x9b6710);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t type = rng() % 3 ? kType : static_cast<uint32_t>(rng());
        const uint32_t riid = rng();
        void *out = &gRecord;
        const auto run = [&](auto call) {
            rng.seed(seed);
            FillFactory(rng);
            *reinterpret_cast<void **>(gFactory) = table;
            return Side(seed, call);
        };
        auto a = run([&] { return orig(gFactory, type, riid, &out) & 0xff; });
        auto b = run([&] { return static_cast<uint32_t>(Ours()->Create(type, riid, &out)); });
        Compare(ctx, "Create", seed, a, b);
    }
}

// 0x9b6740: for image records a new resource (0x3c bytes) in the colour type of the low word of a4, asked for riid.
DIFF_TEST(BufferResourceFactory_CreateInstanceByType, 0x9b6740)
{
    Patches patches;
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t)>(0x9b6740);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t type = rng() % 4 ? kType : static_cast<uint32_t>(rng());
        const uint32_t riid = rng();
        const uint32_t format = rng() % 3 ? (rng() & 0xffff0000u) | rng() % 12 : static_cast<uint32_t>(rng());
        const uint32_t a5 = rng();
        void *out = &gRecord;
        const auto run = [&](auto call) {
            rng.seed(seed);
            FillFactory(rng);
            auto o = Side(seed, call);
            if (gNewResource) ::operator delete(gNewResource);
            return o;
        };
        auto a = run([&] { return orig(gFactory, type, riid, &out, format, a5) & 0xff; });
        auto b = run([&] { return static_cast<uint32_t>(Ours()->CreateInstanceByType(type, riid, &out, format, a5)); });
        Compare(ctx, "CreateInstanceByType", seed, a, b);
    }
}

// 0x9b6650: the base destructor body (vtable, lock), then the 0x1c-byte block freed on flags & 1.
DIFF_TEST(BufferResourceFactory_DeletingDestructor, 0x9b6650)
{
    Patches patches;
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x9b6650);
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0x9b6010);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t flags = seed % 3 == 0 ? 0 : seed % 3 == 1 ? 1 : static_cast<uint32_t>(rng());
        uint8_t afterA[0x1c] = {}, afterB[0x1c] = {};
        const auto run = [&](auto call, uint8_t *after) {
            auto *object = static_cast<uint8_t *>(::operator new(sizeof(Factory)));
            construct(object);
            for (size_t i = 0x10; i < sizeof(Factory); ++i) object[i] = static_cast<uint8_t>(seed * 31 + i);
            *reinterpret_cast<void **>(object) = gRecordTable; // overwritten by the destructor
            void *returned = nullptr;
            auto o = Side(seed, [&] {
                returned = call(object);
                return static_cast<uint32_t>(returned == object);
            });
            if (!(flags & 1)) {
                std::memcpy(after, object, 4); // the vtable; the lock's block is gone
                std::memcpy(after + 0x10, object + 0x10, sizeof(Factory) - 0x10);
                ::operator delete(object);
            }
            return o;
        };
        auto a = run([&](void *o) { return orig(o, flags); }, afterA);
        auto b = run([&](void *o) { return static_cast<void *>(reinterpret_cast<Factory *>(o)->DeletingDestructor(flags)); }, afterB);
        Compare(ctx, "DeletingDestructor", seed, a, b);
        if (std::memcmp(afterA, afterB, sizeof(afterA)) != 0) ctx.Fail("DeletingDestructor seed %u: object differs", seed);
    }
}

// 0x9b7fa0: the record read into a local resource of the target's colour type (BMP, else rewind and TGA), then the
// target buffer re-initialised from the read one and blitted.
// A small image for the record: a BMP (24 or 8 bit, uncompressed), a TGA (uncompressed true colour) or a few bytes of noise.
std::vector<uint8_t> MakeImage(std::mt19937 &rng)
{
    std::vector<uint8_t> out;
    const auto put = [&](uint32_t v, int n) {
        for (int i = 0; i < n; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    const uint32_t kind = rng() % 8;
    const uint32_t width = 1 + rng() % 3, height = 1 + rng() % 3;
    if (kind < 3 || kind == 3) {
        const uint32_t bits = kind == 3 ? 8 : 24;
        const uint32_t colors = bits == 8 ? 1 + rng() % 4 : 0;
        const uint32_t pitch = (((bits >> 3) * width - 1) | 3) + 1;
        out = {'B', 'M'};
        put(0, 4), put(0, 4), put(54 + colors * 4, 4), put(40, 4), put(width, 4), put(height, 4), put(1, 2), put(bits, 2);
        put(0, 4), put(0, 4), put(rng(), 4), put(rng(), 4), put(colors, 4), put(rng(), 4);
        for (uint32_t i = 0; i < colors * 4; ++i) put(rng(), 1);
        for (uint32_t i = 0; i < height * pitch; ++i) put(bits == 8 ? rng() % colors : rng(), 1);
        put(0, 1);
        return out;
    }
    if (kind < 6) {
        static constexpr uint32_t kDepths[] = {32, 24, 16};
        const uint32_t depth = kDepths[rng() % 3];
        put(0, 1), put(0, 1), put(2, 1), put(0, 2), put(0, 2), put(0, 1), put(0, 2), put(0, 2), put(width, 2), put(height, 2);
        put(depth, 1), put(rng() % 2 ? 0x20 : 0, 1);
        for (uint32_t i = 0; i < width * height * (depth >> 3); ++i) put(rng(), 1);
        return out;
    }
    for (uint32_t i = rng() % 18; i > 0; --i) put(rng(), 1);
    return out;
}

DIFF_TEST(BufferResourceFactory_ReadRecord, 0x9b7fa0)
{
    Patches patches;
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(0x9b7fa0);
    gDataMode = true;
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        const auto run = [&](auto call) {
            std::mt19937 rng(seed);
            gData = MakeImage(rng);
            gPos = 0;
            gData.reserve(1 << 12);
            FillFactory(rng);
            BuildBuffers(rng, 2);
            gWorldType = rng() % 5 ? kType : static_cast<uint32_t>(rng());
            gTargetBuffer = rng() % (kBuffers + 1);
            for (size_t i = 0x20; i < sizeof(gWorldRes); ++i) gWorldRes[i] = static_cast<uint8_t>(rng());
            gFailMod = rng() % 3 ? 0 : 3;
            return Side(seed, call);
        };
        auto a = run([&] { return orig(gFactory, gWorldRes, &gRecord) & 0xff; });
        auto b = run([&] {
            return static_cast<uint32_t>(Ours()->ReadRecord(reinterpret_cast<cIGZUnknown *>(gWorldRes),
                                                            reinterpret_cast<ifaces::cIGZPersistDBSerialRecord *>(&gRecord)));
        });
        Compare(ctx, "ReadRecord", seed, a, b);
    }
}

// ---- cIGZPersistBufferResourceFactory face ----

// 0x9b8120: the colour type stored after the face's vtable.
DIFF_TEST(BufferResourceFactory_SetColorType, 0x9b8120)
{
    Patches patches;
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0x9b8120);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t format = rng(), bits = rng();
        const auto run = [&](auto call) {
            rng.seed(seed);
            FillFactory(rng);
            return Side(seed, [&] {
                call();
                return 0u;
            });
        };
        auto a = run([&] { orig(gFactory + 0x10, format, bits); });
        auto b = run([&] { OurFace()->SetColorType(format, bits); });
        Compare(ctx, "SetColorType", seed, a, b);
    }
}

// 0x9b6f60: a resource in colour type {1, 0}, the path copied into a cRZString for the BMP file reader, the resource asked
// for its buffer on success. Paths of 0..40 characters (short and heap strings).
DIFF_TEST(BufferResourceFactory_NewBufferFromFile, 0x9b6f60)
{
    Patches patches;
    struct PathDouble {
        void *const *vtable;
        std::string text;
        static const char *__fastcall Data(PathDouble *self, void *)
        {
            Log(kOther, 1);
            return self->text.c_str();
        }
        static uint32_t __fastcall Strlen(PathDouble *self, void *)
        {
            Log(kOther, 2);
            return static_cast<uint32_t>(self->text.size());
        }
    };
    void *table[34];
    for (auto &slot : table) slot = Code(&BufOther);
    table[6] = Code(&PathDouble::Data);
    table[7] = Code(&PathDouble::Strlen);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *, void **, uint32_t, uint32_t, uint32_t, uint32_t)>(0x9b6f60);
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        std::mt19937 rng(seed);
        PathDouble path{table, {}};
        for (uint32_t n = rng() % 41; n > 0; --n) path.text.push_back(static_cast<char>('a' + rng() % 26));
        const uint32_t a3 = rng(), a4 = rng(), a5 = rng(), a6 = rng();
        void *out = &gRecord;
        const auto run = [&](auto call) {
            rng.seed(seed);
            FillFactory(rng);
            auto o = Side(seed, call);
            if (gNewResource) ::operator delete(gNewResource); // leaked by the body
            return o;
        };
        auto a = run([&] { return orig(gFactory + 0x10, &path, &out, a3, a4, a5, a6) & 0xff; });
        auto b = run([&] {
            return static_cast<uint32_t>(OurFace()->NewBufferFromFile(reinterpret_cast<cIGZString *>(&path), &out, a3, a4, a5, a6));
        });
        Compare(ctx, "NewBufferFromFile", seed, a, b);
    }
}

// 0x9b7060: an exe bitmap through LoadImageA / GetObjectA (24 bits, buffer bits 16 only), each pixel converted through
// slot 30 into the buffer bottom-up, rows padded to 4 bytes.
DIFF_TEST(BufferResourceFactory_NewBufferFromBitmap, 0x9b7060)
{
    Patches patches;
    ImportSlot module(ctx, kIatGetModuleHandleA, Code(&FakeGetModuleHandleA));
    ImportSlot load(ctx, kIatLoadImageA, Code(&FakeLoadImageA));
    ImportSlot object(ctx, kIatGetObjectA, Code(&FakeGetObjectA));
    ImportSlot remove(ctx, kIatDeleteObject, Code(&FakeDeleteObject));
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **, uint32_t, uint32_t, uint32_t, uint32_t)>(0x9b7060);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t id = rng();
        const uint32_t a3 = rng(), format = rng() % 12, bits = rng() % 4 ? 16 : rng() % 40, a6 = rng();
        void *out = &gRecord;
        const auto run = [&](auto call) {
            rng.seed(seed + 1000000);
            FillFactory(rng);
            BuildBuffers(rng, 2);
            // Surface rows of 2 * width bytes at least; the bitmap fits gDib (width * 3 + 3 per row).
            for (auto &b : gBuf)
                b.stride = 2 * (rng() % 9) + (rng() % 2) * 2 * (1 + rng() % 3);
            gDibWidth = static_cast<int32_t>(rng() % 10) - 1;
            gDibHeight = static_cast<int32_t>(rng() % 10) - 1;
            gDibBits = rng() % 4 ? 24 : static_cast<uint16_t>(rng() % 33);
            for (auto &byte : gDib) byte = static_cast<uint8_t>(rng());
            out = &gRecord;
            return Side(seed, call);
        };
        // The surface must hold the rows the body writes: (2 * height - 2) * (stride / 2) + 2 * width bytes.
        auto a = run([&] { return orig(gFactory + 0x10, id, &out, a3, format, bits, a6) & 0xff; });
        auto b = run([&] {
            return static_cast<uint32_t>(
                OurFace()->NewBufferFromBitmap(id, reinterpret_cast<cIGZBuffer **>(&out), a3, format, bits, a6));
        });
        Compare(ctx, "NewBufferFromBitmap", seed, a, b);
    }
}

// 0x9b7280: a buffer from a record: kind 1 (raw rows), 2 (compressed rows) or 0x856ddbac (old: uint32 pixels one by
// one), anything else true with no buffer. The doubles never fail a typed read after the first of kinds 1 and 2, and
// the graphics system always hands out a buffer (even when it reports failure): the original then calls through a null
// buffer (ORIGINAL-BUG in src), which a test cannot run.
DIFF_TEST(BufferResourceFactory_ReadBuffer, 0x9b7280)
{
    Patches patches;
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void **, void *, uint32_t)>(0x9b7280);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        void *out = nullptr;
        const auto run = [&](auto call) {
            std::mt19937 rng(seed);
            FillFactory(rng);
            const uint32_t kind = rng() % 8 < 3 ? 1 : rng() % 5 < 3 ? 2 : rng() % 3 ? kType : rng() % 4;
            const uint32_t format = rng() % 5 ? 2 + rng() % 8 : rng() % 12;
            const uint32_t bpp = format == 2 ? 1 : format >= 3 && format <= 7 ? 2 : format == 8 || format == 9 ? 4 : 0;
            BuildBuffers(rng, bpp);
            const uint32_t width = rng() % 9, height = rng() % 9;
            for (auto &b : gBuf) {
                b.bpp = bpp;
                b.stride = bpp * width + (rng() % 2 ? 0 : 4 * (1 + rng() % 3));
            }
            uint32_t maxStride = 0;
            for (auto &b : gBuf) maxStride = b.stride > maxStride ? b.stride : maxStride;
            // Raw kind: the record's bytes cover what the swapped-stride copy reads (stride per row), and fit the surface.
            const uint32_t dataSize = kind == 1 ? maxStride * height + rng() % 8 : rng() % 0x200;
            gReads.clear();
            if (kind == kType) gReads = {kind, width, height};
            else gReads = {kind, dataSize, width, height, format};
            gFailRead = rng() % 6 ? -1 : kind == kType ? static_cast<int>(rng() % 3) : 0;
            gFailMod = rng() % 3 ? 0 : 4;
            out = &gRecord;
            return Side(seed, call);
        };
        // A null record now and then (the null check before the first read; coordinator, 2026-10-26).
        void *const record = seed % 20 == 7 ? nullptr : &gRecord;
        auto a = run([&] { return orig(gFactory + 0x10, &out, record, 0x77) & 0xff; });
        const int64_t outA = Id(out);
        auto b = run([&] {
            return static_cast<uint32_t>(OurFace()->ReadBuffer(reinterpret_cast<cIGZBuffer **>(&out),
                                                               reinterpret_cast<ifaces::cIGZPersistDBSerialRecord *>(record), 0x77));
        });
        Compare(ctx, "ReadBuffer", seed, a, b);
        if (outA != Id(out)) ctx.Fail("ReadBuffer seed %u: out differs", seed);
    }
}

// 0x9b7d40: a buffer to a record: kind (2 when compressed, else 1), size, width, height, format, then the packed rows.
DIFF_TEST(BufferResourceFactory_WriteBuffer, 0x9b7d40)
{
    Patches patches;
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *, uint32_t)>(0x9b7d40);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 pick(seed);
        const uint32_t which = pick() % (kBuffers + 1);
        const bool noRecord = pick() % 20 == 0;
        const uint32_t compress = pick() % 2 ? (pick() & 0xffffff00u) | 1 : pick() & 0xffffff00u;
        const auto run = [&](auto call) {
            std::mt19937 rng(seed);
            FillFactory(rng);
            BuildBuffers(rng, 1 + rng() % 4);
            gFailMod = rng() % 3 ? 0 : 5;
            return Side(seed, call);
        };
        void *buffer = which < kBuffers ? &gBuf[which] : nullptr;
        void *record = noRecord ? nullptr : &gRecord;
        auto a = run([&] { return orig(gFactory + 0x10, buffer, record, compress) & 0xff; });
        auto b = run([&] {
            return static_cast<uint32_t>(OurFace()->WriteBuffer(static_cast<cIGZBuffer *>(buffer),
                                                                static_cast<ifaces::cIGZPersistDBSerialRecord *>(record),
                                                                static_cast<uint8_t>(compress) != 0));
        });
        Compare(ctx, "WriteBuffer", seed, a, b);
    }
}
