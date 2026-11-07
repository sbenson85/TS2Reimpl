// Differential tests for src/gz/cGZGimexFactory.* (standalone shape): the factory's slots on both faces, its helpers,
// the row converters and palette packers, the constructor and the creator.
//
// The original side's factory comes from the image's creator 0x87b412 (the game's heap: its vectors grow and are freed
// there), ours from our creator. A script of slot calls (each through the object's own vtables, the +0 face and the
// cIGZPersistResourceFactory face at +4) runs on each side from the same seeds; the traces are compared: every answer
// and out value, the factory's fields after each call (+8 the base's count, its lock's count, +0x14 .. +0x1c, the
// pixel scratch vector's size and bytes, the dither rows' size and words), the cells it caches in (0x10e6924 resource
// manager, 0x10e6928 graphics system, 0x10e692c / 0x10e6930 the IJL load), every fake's calls and counts, and the fake
// buffers' surfaces. The world is fakes on both sides:
//   * GetDirectorFrameworkObject (0x1ae18d) answers a COM double: GetClassObject hands out fake readers, writers and
//     buffers by class (or fails, with junk above al) from a script;
//   * the fake reader (cIGZGimexReader) opens with a scripted frame count and describes frames from a pool of Gimex
//     infos (32-, 24- and 8-bit, palettes random), Read fills the factory's scratch rows with a pattern; the fake writer
//     (cIGZGimexWriter) answers Open with raw bytes (0x80, 0x100) and hashes Write's whole 0x2a70-byte info and pixels;
//   * fake buffers (cIGZBuffer) keep a surface (random content, a stride with padding), Init / Lock / IsReady scripted,
//     a native colour type that can differ from the colour type;
//   * cRZFile's constructor (0x1c0c07, ours too) stands in: a fake file whose Open / IsOpen answer from the script;
//   * the cGZPersistBufferResource the factory builds is the real class on both sides (the original's constructor 0x9b64a0,
//     ours): the director answers its buffer requests; Read / ReadRecord are given fake resources (GetType, QueryInterface, its +0x1c face's GetBuffer, the colour format at +0x28);
//   * the resource manager (OpenRecord, slot 39), a segment (OpenRecord, slot 22), records (QueryInterface for the
//     serial record, GetKey, Close), the graphics system (CreateBuffer, slot 3) and the framework at 0x10c9378
//     (GetSystemService, slot 5) for empty cells; LoadLibraryA (impfx) answers a fake module.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cGZGimexFactory.h"
#include "gz/cGZGimexReader.h"
#include "gz/cIGZPersistResource.h"
#include "impfx.h"
#include "rz/StaticGetters.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "shape.h"

#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kCreate = 0x87b412;
constexpr uint32_t kConstruct = 0x87bcdc;
constexpr uint32_t kVTable = 0xe4a464;
constexpr uint32_t kFaceVTable = 0xe4a4bc;
constexpr uint32_t kResourceManagerCell = 0x10e6924;
constexpr uint32_t kGraphicSystemCell = 0x10e6928;
constexpr uint32_t kIjlTriedCell = 0x10e692c;
constexpr uint32_t kIjlModuleCell = 0x10e6930;
constexpr uint32_t kServiceObjectCell = 0x10c9378;
constexpr uint32_t kResourceManagerId = 0x056b906e;
constexpr uint32_t kImageType = 0x856ddbac;
constexpr uint32_t kIjlModule = 0x6a000000;

template <class F> void *VFn(F f)
{
    return reinterpret_cast<void *>(f);
}

uint32_t Hash(const void *data, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(data)[i]) * 16777619u;
    return h;
}

// ---- the trace and the script ----

std::vector<std::string> gLog;
std::mt19937 gScript;

void Log(const char *format, ...)
{
    char text[320];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    gLog.emplace_back(text);
}

uint32_t Decide(uint32_t n)
{
    return gScript() % n;
}

// Side-specific addresses (the factory, the memory it allocates) as tokens; the fakes are the same on both sides.
struct Range {
    uint32_t begin, size, token;
};
std::vector<Range> gRanges;
const uint8_t *gFactory = nullptr;

uint32_t Norm(uint32_t w)
{
    if (gFactory) {
        if (w - shape::P(gFactory) < cGZGimexFactory::kSize) return 0xfa000000 | (w - shape::P(gFactory));
        uint32_t begin, end;
        std::memcpy(&begin, gFactory + 0x20, 4);
        std::memcpy(&end, gFactory + 0x24, 4);
        // the scratch vector's bytes, and its end (an empty vector's data pointer)
        if (begin != 0 && w - begin <= end - begin) return 0xfb000000 | (w - begin);
    }
    for (const Range &r : gRanges)
        if (w - r.begin < r.size) return r.token | (w - r.begin);
    return w;
}

const char *const kKinds[] = {"com",     "reader",   "writer", "buffer",   "file",     "record",   "serial",
                              "manager", "segment",  "gfx",    "resource", "resface",  "framework"};
template <int K, int N> uint32_t __fastcall Trap(void *self, void *)
{
    Log("%s %08x TRAP slot %d", kKinds[K], Norm(shape::P(self)), N);
    return 0;
}
template <int K, size_t... I> void FillTraps(void **vt, std::index_sequence<I...>)
{
    ((vt[I] = VFn(&Trap<K, static_cast<int>(I)>)), ...);
}
template <int K, size_t N> void FillTraps(void *(&vt)[N])
{
    FillTraps<K>(vt, std::make_index_sequence<N>());
}

// A counted fake's head.
struct Counted {
    void **vt;
    int id;
    uint32_t refs;
};
template <int K> uint32_t __fastcall AddRefOf(Counted *c, void *)
{
    Log("%s%d.AddRef -> %u", kKinds[K], c->id, c->refs + 1);
    return ++c->refs;
}
template <int K> uint32_t __fastcall ReleaseOf(Counted *c, void *)
{
    Log("%s%d.Release -> %u", kKinds[K], c->id, c->refs - 1);
    return --c->refs;
}
template <int K, size_t N> void Counting(void *(&vt)[N])
{
    FillTraps<K>(vt);
    vt[1] = VFn(&AddRefOf<K>);
    vt[2] = VFn(&ReleaseOf<K>);
}

enum Kind { kCom, kReader, kWriter, kBuffer, kFile, kRecord, kSerial, kManager, kSegment, kGfx, kResource, kResFace, kFw };

// ---- Gimex infos the fake reader describes ----

constexpr int kInfos = 7;
gimex::Info gInfos[kInfos];

void MakeInfos(std::mt19937 &rng)
{
    // The last: a negative depth that is not a multiple of 8, no rows (the read's pitch shows the signed division).
    static const int32_t kBits[kInfos] = {32, 32, 32, 8, 8, 24, -12};
    for (int i = 0; i < kInfos; ++i) {
        gimex::Info &info = gInfos[i];
        for (size_t k = 0; k < sizeof(info); ++k) reinterpret_cast<uint8_t *>(&info)[k] = static_cast<uint8_t>(rng());
        info.width = static_cast<int32_t>(1 + rng() % 9);
        info.height = kBits[i] < 0 ? 0 : static_cast<int32_t>(1 + rng() % 5);
        info.bitsPerPixel = kBits[i];
    }
}

// ---- fake buffers ----

struct FakeBuffer : Counted {
    uint32_t width, height, format, native, bits, stride, ready;
    cGZRect bounds;
    std::vector<uint8_t> surface;
};
constexpr int kBuffers = 4;
FakeBuffer gBuffers[kBuffers];
void *gBufferVt[40];

uint32_t BytesOf(uint32_t bits)
{
    return bits >= 8 && bits <= 32 ? bits / 8 : 4;
}

void Shape(FakeBuffer &b, uint32_t w, uint32_t h, uint32_t format, uint32_t bits, uint32_t pad, std::mt19937 &rng)
{
    b.width = w;
    b.height = h;
    b.format = format;
    b.bits = bits;
    b.stride = w * BytesOf(bits) + pad;
    b.bounds = {0, 0, static_cast<int32_t>(w), static_cast<int32_t>(h)};
    b.surface.assign(b.stride * h + w * 4 + 64, 0);
    for (uint8_t &c : b.surface) c = static_cast<uint8_t>(rng());
}

// The buffer's QueryInterface, as the resource's (real, on both sides) falls through to it: answers from the script.
uint32_t __fastcall BufQuery(FakeBuffer *b, void *, uint32_t riid, void **out)
{
    const uint32_t ok = Decide(3) != 0;
    Log("buffer%d.QueryInterface(%08x) -> %u", b->id, riid, ok);
    if (!ok) return 0;
    ++b->refs;
    *out = b;
    return 1;
}
uint32_t __fastcall BufInit(FakeBuffer *b, void *, uint32_t w, uint32_t h, uint32_t format, uint32_t bits)
{
    const uint32_t ok = Decide(6) != 0;
    Log("buffer%d.Init(%u, %u, %u, %u) -> %u", b->id, w, h, format, bits, ok);
    if (ok) {
        std::mt19937 fill(gScript());
        Shape(*b, w, h, format, bits, Decide(3) * 4, fill);
        static const uint32_t kNative[] = {4, 5, 6, 7, 3};
        b->native = Decide(3) ? format : kNative[Decide(5)];
        b->ready = 1;
    }
    return ok ? 0x4401 : 0x4400;
}
uint32_t __fastcall BufShutdown(FakeBuffer *b, void *)
{
    Log("buffer%d.Shutdown", b->id);
    b->ready = 0;
    return 1;
}
uint32_t __fastcall BufLock(FakeBuffer *b, void *, uint32_t flags)
{
    const uint32_t ok = Decide(7) != 0;
    Log("buffer%d.Lock(%x) -> %u", b->id, flags, ok);
    return ok ? 0x5501 : 0x5500;
}
uint32_t __fastcall BufUnlock(FakeBuffer *b, void *, uint32_t flags)
{
    Log("buffer%d.Unlock(%x)", b->id, flags);
    return 1;
}
uint32_t __fastcall BufWidth(FakeBuffer *b, void *)
{
    Log("buffer%d.GetWidth", b->id);
    return b->width;
}
uint32_t __fastcall BufHeight(FakeBuffer *b, void *)
{
    Log("buffer%d.GetHeight", b->id);
    return b->height;
}
const cGZRect *__fastcall BufBoundsPtr(FakeBuffer *b, void *)
{
    Log("buffer%d.GetBoundsPtr", b->id);
    return &b->bounds;
}
cGZColorType *__fastcall BufColorType(FakeBuffer *b, void *, cGZColorType *out)
{
    Log("buffer%d.GetColorType", b->id);
    out->format = b->format;
    out->bits = b->bits;
    return out;
}
cGZColorType *__fastcall BufNativeColorType(FakeBuffer *b, void *, cGZColorType *out)
{
    Log("buffer%d.GetNativeColorType", b->id);
    out->format = b->native;
    out->bits = b->bits;
    return out;
}
uint32_t __fastcall BufBitsPerPixel(FakeBuffer *b, void *)
{
    Log("buffer%d.GetBitsPerPixel", b->id);
    return b->bits;
}
uint32_t __fastcall BufGetPixel(FakeBuffer *b, void *, uint32_t x, uint32_t y)
{
    uint32_t v = 0;
    std::memcpy(&v, b->surface.data() + y * b->stride + x * BytesOf(b->bits), BytesOf(b->bits));
    Log("buffer%d.GetPixel(%u, %u) -> %08x", b->id, x, y, v);
    return v;
}
void *__fastcall BufBits(FakeBuffer *b, void *)
{
    Log("buffer%d.GetColorSurfaceBits", b->id);
    return b->surface.data();
}
uint32_t __fastcall BufStride(FakeBuffer *b, void *)
{
    Log("buffer%d.GetColorSurfaceStride", b->id);
    return b->stride;
}
uint32_t __fastcall BufIsReady(FakeBuffer *b, void *)
{
    Log("buffer%d.IsReady -> %u", b->id, b->ready);
    return b->ready | 0x6600;
}

void BuildBufferVt()
{
    Counting<kBuffer>(gBufferVt);
    gBufferVt[0] = VFn(&BufQuery);
    gBufferVt[3] = VFn(&BufInit);
    gBufferVt[4] = VFn(&BufShutdown);
    gBufferVt[6] = VFn(&BufLock);
    gBufferVt[7] = VFn(&BufUnlock);
    gBufferVt[9] = VFn(&BufWidth);
    gBufferVt[10] = VFn(&BufHeight);
    gBufferVt[12] = VFn(&BufBoundsPtr);
    gBufferVt[14] = VFn(&BufColorType);
    gBufferVt[15] = VFn(&BufNativeColorType);
    gBufferVt[16] = VFn(&BufBitsPerPixel);
    gBufferVt[21] = VFn(&BufGetPixel);
    gBufferVt[34] = VFn(&BufBits);
    gBufferVt[35] = VFn(&BufStride);
    gBufferVt[39] = VFn(&BufIsReady);
}

// A buffer for the write paths and the helpers: a supported (or not) colour type, random pixels.
void RandomBuffer(FakeBuffer &b, int id, std::mt19937 &rng)
{
    static const uint32_t kFormats[] = {4, 5, 6, 7, 8, 9, 7, 9, 3};
    const uint32_t format = kFormats[rng() % 9];
    const uint32_t bits = format >= 8 ? 32 : format == 3 ? 24 : 16;
    b.vt = gBufferVt;
    b.id = id;
    b.refs = 1;
    Shape(b, 1 + rng() % 9, 1 + rng() % 5, format, bits, (rng() % 3) * 4, rng);
    b.native = rng() % 3 ? format : 4 + rng() % 4;
    b.ready = rng() % 2;
    // Some pixels opaque everywhere (HasAlpha's scan finds none).
    if (rng() % 3 == 0) {
        for (uint32_t y = 0; y < b.height; ++y)
            for (uint32_t x = 0; x < b.width; ++x) {
                uint8_t *p = b.surface.data() + y * b.stride + x * BytesOf(bits);
                if (bits == 16) p[1] |= format == 7 ? 0xf0 : 0x80;
                if (bits == 32) p[3] = 0xff;
            }
    }
}

// ---- the fake reader and writer ----

struct FakeCodecObject : Counted {};
FakeCodecObject gReaders[2], gWriters[2];
void *gReaderVt[11], *gWriterVt[9];

uint32_t __fastcall ReaderSetRecord(Counted *r, void *, void *record)
{
    const uint32_t ok = Decide(5) != 0;
    Log("reader%d.SetRecord(%08x) -> %u", r->id, Norm(shape::P(record)), ok);
    return ok | 0x1200;
}
uint32_t __fastcall ReaderSetStream(Counted *r, void *, void *stream)
{
    const uint32_t ok = Decide(5) != 0;
    Log("reader%d.SetStream(%08x) -> %u", r->id, Norm(shape::P(stream)), ok);
    return ok | 0x1300;
}
uint32_t __fastcall ReaderOpen(Counted *r, void *, uint32_t format, int32_t *frames)
{
    static const int32_t kFrames[] = {0, 1, 1, 2, -1};
    *frames = kFrames[Decide(5)];
    const uint32_t ok = Decide(6) != 0;
    Log("reader%d.Open(%u) -> %u frames %d", r->id, format, ok, *frames);
    return ok | 0x1400;
}
gimex::Info *__fastcall ReaderGetInfo(Counted *r, void *, int32_t frame)
{
    gimex::Info *info = Decide(6) ? &gInfos[Decide(kInfos)] : nullptr;
    Log("reader%d.GetInfo(%d) -> %d", r->id, frame, info ? static_cast<int>(info - gInfos) : -1);
    return info;
}
uint32_t __fastcall ReaderRead(Counted *r, void *, gimex::Info *info, uint8_t *pixels, int32_t pitch)
{
    const uint32_t ok = Decide(6) != 0;
    const uint32_t seed = gScript();
    Log("reader%d.Read(%d, %08x, %d) -> %u", r->id, static_cast<int>(info - gInfos), Norm(shape::P(pixels)), pitch, ok);
    for (int32_t i = 0; i < pitch * info->height; ++i) pixels[i] = static_cast<uint8_t>(seed >> (i % 24) ^ i * 37);
    return ok | 0x1500;
}
uint32_t __fastcall ReaderFreeInfo(Counted *r, void *, gimex::Info *info)
{
    Log("reader%d.FreeInfo(%d)", r->id, static_cast<int>(info - gInfos));
    return 1;
}
uint32_t __fastcall ReaderClose(Counted *r, void *)
{
    Log("reader%d.Close", r->id);
    return 1;
}

uint32_t __fastcall WriterSetRecord(Counted *w, void *, void *record)
{
    const uint32_t ok = Decide(5) != 0;
    Log("writer%d.SetRecord(%08x) -> %u", w->id, Norm(shape::P(record)), ok);
    return ok | 0x2200;
}
uint32_t __fastcall WriterSetStream(Counted *w, void *, void *stream)
{
    const uint32_t ok = Decide(5) != 0;
    Log("writer%d.SetStream(%08x) -> %u", w->id, Norm(shape::P(stream)), ok);
    return ok | 0x2300;
}
uint32_t __fastcall WriterOpen(Counted *w, void *, uint32_t format, int32_t flag)
{
    static const uint32_t kAnswers[] = {0, 1, 1, 0x80, 0x100};
    const uint32_t answer = kAnswers[Decide(5)];
    Log("writer%d.Open(%u, %d) -> %x", w->id, format, flag, answer);
    return answer;
}
uint32_t __fastcall WriterWrite(Counted *w, void *, const gimex::Info *info, const uint8_t *pixels, int32_t pitch)
{
    const uint32_t ok = Decide(5) != 0;
    Log("writer%d.Write(info %08x: %ux%u bpp %d/%d alpha %u q %u bytes %u, pixels %08x %08x, %d) -> %u", w->id,
        Hash(info, gimex::kWriteInfoSize), info->width, info->height, info->bitsPerPixel, info->sourceBits,
        info->alphaBits, info->quality, info->imageBytes, Norm(shape::P(pixels)),
        Hash(pixels, static_cast<size_t>(pitch) * static_cast<uint32_t>(info->height)), pitch, ok);
    return ok | 0x2400;
}
uint32_t __fastcall WriterClose(Counted *w, void *)
{
    Log("writer%d.Close", w->id);
    return 1;
}

void BuildCodecVts()
{
    Counting<kReader>(gReaderVt);
    gReaderVt[3] = VFn(&ReaderSetRecord);
    gReaderVt[4] = VFn(&ReaderSetStream);
    gReaderVt[6] = VFn(&ReaderOpen);
    gReaderVt[7] = VFn(&ReaderGetInfo);
    gReaderVt[8] = VFn(&ReaderRead);
    gReaderVt[9] = VFn(&ReaderFreeInfo);
    gReaderVt[10] = VFn(&ReaderClose);
    Counting<kWriter>(gWriterVt);
    gWriterVt[3] = VFn(&WriterSetRecord);
    gWriterVt[4] = VFn(&WriterSetStream);
    gWriterVt[6] = VFn(&WriterOpen);
    gWriterVt[7] = VFn(&WriterWrite);
    gWriterVt[8] = VFn(&WriterClose);
}

// ---- the COM director ----

constexpr uint32_t kResSize = cGZGimexFactory::kResourceSize;
int gResources = 0;

uint32_t __fastcall ComGetClassObject(void *, void *, uint32_t classId, uint32_t iid, void **out)
{
    Counted *object = nullptr;
    // A cGZPersistBufferResource's constructor asks for the aggregated buffer class first, into its +0x20 field: that finds
    // the resource (the original's is built by the game's new, ours by the toolchain's) and gives it its id.
    if (classId == 0xc470d326) {
        const int id = gResources++;
        const uint32_t begin = shape::P(reinterpret_cast<uint8_t *>(out) - 0x20);
        // A freed resource's block may be handed out again (the heap decides, and the sides' heaps differ): the newer wins.
        gRanges.erase(std::remove_if(gRanges.begin(), gRanges.end(), [&](const Range &r) { return r.begin == begin; }), gRanges.end());
        gRanges.push_back({begin, kResSize, 0xf2000000u | static_cast<uint32_t>(id) << 16});
        Log("resource%d(%08x)", id, iid);
    }
    if (Decide(6) != 0) {
        if (classId == cGZGimexReader::kClassId) object = &gReaders[Decide(2)];
        if (classId == cGZGimexWriter::kClassId) object = &gWriters[Decide(2)];
        if (classId == cGZGimexFactory::kBufferClass) object = &gBuffers[Decide(kBuffers)];
    }
    Log("com.GetClassObject(%08x, %08x) -> %s%d", classId, iid, object ? kKinds[object->vt == gReaderVt   ? kReader
                                                                                   : object->vt == gWriterVt ? kWriter
                                                                                                             : kBuffer]
                                                                         : "none",
        object ? object->id : -1);
    if (!object) return 0xab00;
    ++object->refs;
    *out = object;
    return 0xab01;
}
void *gComVt[6];
Counted gCom{gComVt, 0, 1};
void *__cdecl FakeDirectorObject()
{
    return &gCom;
}

// ---- files (the cRZFile constructor's stand-in) ----

struct FakeFile : Counted {
    uint32_t open;
};
void *gFileVt[34];
int gFiles = 0;

void *__fastcall FileConstruct(void *self, void *, const cIGZString &path)
{
    auto *f = static_cast<FakeFile *>(self);
    std::memset(self, 0xab, 0x9c);
    *f = {};
    f->vt = gFileVt;
    f->id = gFiles++;
    f->refs = 0;
    gRanges.push_back({shape::P(self), 0x9c, 0xf1000000u | static_cast<uint32_t>(f->id) << 16});
    Log("file%d(%s)", f->id, reinterpret_cast<const cRZString *>(&path)->mString.c_str());
    return self;
}
uint32_t __fastcall FileOpen(FakeFile *f, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    const uint32_t ok = Decide(5) != 0;
    Log("file%d.Open(%u, %u, %u) -> %u", f->id, access, creation, sharing, ok);
    f->open = ok;
    return ok | 0x3100;
}
uint32_t __fastcall FileIsOpen(FakeFile *f, void *)
{
    const uint32_t answer = Decide(3) ? f->open : 0;
    Log("file%d.IsOpen -> %u", f->id, answer);
    return answer | 0x3200;
}
uint32_t __fastcall FileClose(FakeFile *f, void *)
{
    Log("file%d.Close", f->id);
    f->open = 0;
    return 1;
}

// ---- records, the resource manager, a segment, the graphics system, the framework ----

struct FakeRecord : Counted {
    uint32_t key[3];
};
FakeRecord gRecords[2];
Counted gSerials[2];
void *gRecordVt[12], *gSerialVt[4];
uint32_t gKey[3] = {kImageType, 0x1234, 0x5678};
uint32_t gOtherKey[3] = {0x12345678, 0x9, 0xa};

uint32_t __fastcall RecordQuery(FakeRecord *r, void *, uint32_t riid, void **out)
{
    const uint32_t ok = Decide(4) != 0;
    Log("record%d.QueryInterface(%08x) -> %u", r->id, riid, ok);
    if (!ok) return 0x4100;
    Counted &s = gSerials[r->id];
    ++s.refs;
    *out = &s;
    return 0x4101;
}
void *__fastcall RecordGetKey(FakeRecord *r, void *)
{
    Log("record%d.GetKey", r->id);
    return r->key;
}
uint32_t __fastcall RecordClose(FakeRecord *r, void *)
{
    Log("record%d.Close", r->id);
    return 1;
}
uint32_t __fastcall SerialQuery(Counted *s, void *, uint32_t riid, void **out)
{
    const uint32_t ok = Decide(4) != 0;
    Log("serial%d.QueryInterface(%08x) -> %u", s->id, riid, ok);
    if (!ok) return 0x4200;
    ++s->refs;
    *out = s;
    return 0x4201;
}

FakeRecord *OpenRecord(const char *who, const void *key, void **out, uint32_t mode)
{
    FakeRecord *r = Decide(5) ? &gRecords[Decide(2)] : nullptr;
    Log("%s.OpenRecord(%08x, %u) -> %d", who, Norm(shape::P(key)), mode, r ? r->id : -1);
    if (r) {
        ++r->refs;
        *out = r;
    }
    return r;
}
uint32_t __fastcall ManagerOpenRecord(Counted *, void *, const void *key, void **out, uint32_t mode)
{
    return OpenRecord("manager", key, out, mode) ? 0x5101 : 0x5100;
}
uint32_t __fastcall SegmentOpenRecord(Counted *, void *, const void *key, void **out, uint32_t mode)
{
    return OpenRecord("segment", key, out, mode) ? 0x5201 : 0x5200;
}
uint32_t __fastcall GfxCreateBuffer(Counted *, void *, void **out)
{
    FakeBuffer *b = Decide(5) ? &gBuffers[Decide(kBuffers)] : nullptr;
    Log("gfx.CreateBuffer -> %d", b ? b->id : -1);
    if (!b) return 0x5300;
    ++b->refs;
    *out = b;
    return 0x5301;
}
Counted gManager, gSegment, gGfx, gFramework;
void *gManagerVt[48], *gSegmentVt[30], *gGfxVt[8], *gFrameworkVt[8];

uint32_t __fastcall FrameworkGetSystemService(Counted *, void *, uint32_t id, uint32_t iid, void **out)
{
    // Always found: the factory uses what it gets without a null test (both sides would crash).
    Counted *s = id == kResourceManagerId ? &gManager : &gGfx;
    Log("framework.GetSystemService(%08x, %08x) -> %s", id, iid, s == &gManager ? "manager" : "gfx");
    ++s->refs;
    *out = s;
    return 1;
}

// ---- resources (the cGZPersistBufferResource constructor patched) ----

void *gResourceVt[12], *gResFaceVt[6];

struct ResourceView {
    uint8_t *p;
    Counted &head() { return *reinterpret_cast<Counted *>(p); }
    uint32_t &type() { return *reinterpret_cast<uint32_t *>(p + 0xc); }
    void **&face() { return *reinterpret_cast<void ***>(p + 0x1c); }
    uint32_t &format() { return *reinterpret_cast<uint32_t *>(p + 0x28); }
};

void BuildResource(uint8_t *memory, int id, uint32_t type, uint32_t format)
{
    std::memset(memory, 0xcd, kResSize);
    ResourceView r{memory};
    r.head() = {gResourceVt, id, 1};
    r.type() = type;
    r.face() = gResFaceVt;
    r.format() = format;
}

uint32_t __fastcall ResourceQuery(Counted *r, void *, uint32_t riid, void **out)
{
    const uint32_t ok = Decide(5) != 0;
    Log("resource%d.QueryInterface(%08x) -> %u", r->id, riid, ok);
    if (!ok) return 0x6100;
    ++r->refs;
    *out = r;
    return 0x6101;
}
uint32_t __fastcall ResourceSetKey(Counted *r, void *, const uint32_t *key)
{
    Log("resource%d.SetKey(%08x %08x %08x)", r->id, key[0], key[1], key[2]);
    return 1;
}
uint32_t __fastcall ResourceGetType(Counted *r, void *)
{
    const uint32_t type = ResourceView{reinterpret_cast<uint8_t *>(r)}.type();
    Log("resource%d.GetType -> %08x", r->id, type);
    return type;
}
void *__fastcall ResFaceGetBuffer(void ***face, void *)
{
    ResourceView r{reinterpret_cast<uint8_t *>(face) - 0x1c};
    FakeBuffer *b = Decide(5) ? &gBuffers[Decide(kBuffers)] : nullptr;
    Log("resource%d.GetBuffer -> %d", r.head().id, b ? b->id : -1);
    if (b) ++b->refs;
    return b;
}

void BuildWorldVts()
{
    BuildBufferVt();
    BuildCodecVts();
    FillTraps<kCom>(gComVt);
    gComVt[3] = VFn(&ComGetClassObject);
    Counting<kFile>(gFileVt);
    gFileVt[3] = VFn(&FileOpen);
    gFileVt[4] = VFn(&FileIsOpen);
    gFileVt[5] = VFn(&FileClose);
    Counting<kRecord>(gRecordVt);
    gRecordVt[0] = VFn(&RecordQuery);
    gRecordVt[3] = VFn(&RecordGetKey);
    gRecordVt[6] = VFn(&RecordClose);
    Counting<kSerial>(gSerialVt);
    gSerialVt[0] = VFn(&SerialQuery);
    Counting<kManager>(gManagerVt);
    gManagerVt[39] = VFn(&ManagerOpenRecord);
    Counting<kSegment>(gSegmentVt);
    gSegmentVt[22] = VFn(&SegmentOpenRecord);
    Counting<kGfx>(gGfxVt);
    gGfxVt[3] = VFn(&GfxCreateBuffer);
    Counting<kFw>(gFrameworkVt);
    gFrameworkVt[5] = VFn(&FrameworkGetSystemService);
    Counting<kResource>(gResourceVt);
    gResourceVt[0] = VFn(&ResourceQuery);
    gResourceVt[4] = VFn(&ResourceSetKey);
    gResourceVt[5] = VFn(&ResourceGetType);
    FillTraps<kResFace>(gResFaceVt);
    gResFaceVt[3] = VFn(&ResFaceGetBuffer);
}

// ---- the game's allocator, ours ----

void *__cdecl GameMalloc(size_t n) { return std::malloc(n); }
void __cdecl GameFree(void *p) { std::free(p); }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t n) { ::operator delete(p, n); }

// ---- the world ----

struct Cells {
    uint32_t manager, gfx, module, service;
    uint8_t tried;
    static Cells Read()
    {
        return {*GameAddress<uint32_t>(kResourceManagerCell), *GameAddress<uint32_t>(kGraphicSystemCell),
                *GameAddress<uint32_t>(kIjlModuleCell), *GameAddress<uint32_t>(kServiceObjectCell),
                *GameAddress<uint8_t>(kIjlTriedCell)};
    }
    void Write() const
    {
        *GameAddress<uint32_t>(kResourceManagerCell) = manager;
        *GameAddress<uint32_t>(kGraphicSystemCell) = gfx;
        *GameAddress<uint32_t>(kIjlModuleCell) = module;
        *GameAddress<uint32_t>(kServiceObjectCell) = service;
        *GameAddress<uint8_t>(kIjlTriedCell) = tried;
    }
};

struct World {
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &FakeDirectorObject};
    // The resource the original builds is the real class: the game's allocator functions are ours here.
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::CodePatch gameMalloc{0x8252b1, reinterpret_cast<const void *>(&GameMalloc)};
    shape::CodePatch gameFree{0x6c4b50, reinterpret_cast<const void *>(&GameFree)};
    shape::StandIn file{0x1c0c07, static_cast<cRZFile *(cRZFile::*)(const cIGZString &)>(&cRZFile::Construct),
                        &FileConstruct};
    impfx::Imports imp;
    Cells saved = Cells::Read();
    alignas(cRZString) uint8_t pathMemory[4][sizeof(cRZString)];
    uint8_t resources[3][kResSize];

    explicit World(difftest::Context &ctx) : imp(ctx)
    {
        BuildWorldVts();
        IMPFX_ADD(imp, LoadLibraryA, "s").Returns(kIjlModule);
        static const char *const kPaths[] = {"c:\\pics\\a.png", "b.jpg", "noext", "d:/x/y.tga"};
        for (int i = 0; i < 4; ++i) Path(i)->Construct(kPaths[i]);
    }
    ~World()
    {
        for (int i = 0; i < 4; ++i) Path(i)->Destruct();
        saved.Write();
    }
    World(const World &) = delete;
    World &operator=(const World &) = delete;
    cRZString *Path(int i) { return reinterpret_cast<cRZString *>(pathMemory[i]); }

    // Every fake, the cells and the logs as the script's seed has them.
    void Reset(uint32_t seed)
    {
        std::mt19937 rng(seed);
        gScript.seed(seed ^ 0x51ed270b);
        gLog.clear();
        gRanges.clear();
        gFiles = 0;
        gResources = 0;
        MakeInfos(rng);
        for (int i = 0; i < kBuffers; ++i) RandomBuffer(gBuffers[i], i, rng);
        for (int i = 0; i < 2; ++i) {
            gReaders[i] = {};
            gReaders[i].vt = gReaderVt;
            gReaders[i].id = i;
            gWriters[i] = {};
            gWriters[i].vt = gWriterVt;
            gWriters[i].id = i;
            gRecords[i] = {};
            gRecords[i].vt = gRecordVt;
            gRecords[i].id = i;
            std::memcpy(gRecords[i].key, rng() % 4 ? gKey : gOtherKey, sizeof(gKey));
            gSerials[i] = {gSerialVt, i, 1};
        }
        gManager = {gManagerVt, 0, 1};
        gSegment = {gSegmentVt, 0, 1};
        gGfx = {gGfxVt, 0, 1};
        gFramework = {gFrameworkVt, 0, 1};
        for (int i = 0; i < 3; ++i) {
            static const uint32_t kTypes[] = {kImageType, kImageType, 0x12345678};
            BuildResource(resources[i], 100 + i, kTypes[rng() % 3], 1 + rng() % 9);
        }
        Cells cells{};
        cells.manager = rng() % 3 ? shape::P(&gManager) : 0;
        cells.gfx = rng() % 3 ? shape::P(&gGfx) : 0;
        cells.tried = rng() % 4 == 0;
        cells.module = rng() % 4 == 0 ? 0x77000000 : 0;
        // No framework only when both services are cached (the factory would call through null otherwise).
        cells.service = rng() % 4 || !cells.manager || !cells.gfx ? shape::P(&gFramework) : 0;
        cells.Write();
    }
};

// ---- the script ----

uint32_t Word(const void *p, uint32_t offset)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(p) + offset, 4);
    return w;
}

void Snapshot(const char *what, uint32_t result)
{
    const uint8_t *f = gFactory;
    const auto *lock = reinterpret_cast<const cRZCriticalSection *>(f + 0xc);
    const auto *pixels = reinterpret_cast<const std::vector<uint8_t> *>(f + 0x20);
    const auto *errors = reinterpret_cast<const std::vector<uint32_t> *>(f + 0x2c);
    const Cells c = Cells::Read();
    Log("%s -> %08x | %u lock %d | %u %u %02x | pixels %zu %08x errors %zu %08x | cells %08x %08x %02x %08x", what,
        Norm(result), Word(f, 8), lock->mBlock->lockCount, Word(f, 0x14), Word(f, 0x18), f[0x1c], pixels->size(),
        Hash(pixels->data(), pixels->size()), errors->size(), Hash(errors->data(), errors->size() * 4), c.manager,
        c.gfx, c.tried, c.module);
    for (const FakeBuffer &b : gBuffers)
        Log("  buffer%d refs %u %ux%u %u/%u/%u stride %u ready %u surface %08x", b.id, b.refs, b.width, b.height,
            b.format, b.native, b.bits, b.stride, b.ready, Hash(b.surface.data(), b.surface.size()));
    Log("  refs reader %u %u writer %u %u record %u %u serial %u %u manager %u gfx %u", gReaders[0].refs,
        gReaders[1].refs, gWriters[0].refs, gWriters[1].refs, gRecords[0].refs, gRecords[1].refs, gSerials[0].refs,
        gSerials[1].refs, gManager.refs, gGfx.refs);
}

const uint32_t kFormats[] = {0, 1, 2, 4, 5, 6, 50, 51, 52};
const uint32_t kColorFormats[] = {1, 2, 3, 4, 5, 6, 7, 7, 8, 9, 0x10};

template <class T, size_t N> T Pick(std::mt19937 &rng, const T (&items)[N])
{
    return items[rng() % N];
}

cGZRect RandomRect(std::mt19937 &rng, const FakeBuffer &b)
{
    cGZRect r;
    if (b.width == 0 || b.height == 0) { // a buffer initialised from a frame with no rows: an empty rectangle
        r.left = r.top = r.right = r.bottom = 0;
        return r;
    }
    r.left = static_cast<int32_t>(rng() % b.width);
    r.top = static_cast<int32_t>(rng() % b.height);
    r.right = r.left + 1 + static_cast<int32_t>(rng() % (b.width - r.left));
    r.bottom = r.top + 1 + static_cast<int32_t>(rng() % (b.height - r.top));
    return r;
}

using Face = cGZGimexResourceFactory;

void RunScript(World &world, void *object, uint32_t seed, int steps)
{
    auto *o = static_cast<cIGZGimexFactory *>(object);
    auto *face = reinterpret_cast<Face *>(static_cast<uint8_t *>(object) + 4);
    gFactory = static_cast<const uint8_t *>(object);
    std::mt19937 rng(seed);
    o->AddRef();
    Snapshot("AddRef", 0);
    char what[128];
    for (int step = 0; step < steps; ++step) {
        const uint32_t op = rng() % 28;
        const uint32_t format = Pick(rng, kFormats);
        const uint32_t colorFormat = Pick(rng, kColorFormats);
        const uint32_t quality = rng() % 101;
        FakeBuffer &buffer = gBuffers[rng() % kBuffers];
        const cGZRect rect = RandomRect(rng, buffer);
        const cIGZString &path = *world.Path(static_cast<int>(rng() % 4))->AsInterface();
        void *stream = &gSerials[rng() % 2]; // the fake reader and writer only pass it on
        const void *key = rng() % 2 ? static_cast<const void *>(gKey) : gOtherKey;
        auto *segment = rng() % 2 ? reinterpret_cast<ifaces::cIGZPersistDBSegment *>(&gSegment) : nullptr;
        cIGZUnknown *record = reinterpret_cast<cIGZUnknown *>(&gSerials[rng() % 2]);
        auto *dbRecord = reinterpret_cast<ifaces::cIGZPersistDBRecord *>(&gRecords[rng() % 2]);
        uint8_t *resource = world.resources[rng() % 3];
        static const uint32_t kIids[] = {0, 1, cIGZGimexFactory::kIid, 0xa56b8f17, 0x456b8f1d, 0x12345};
        const uint32_t iid = Pick(rng, kIids);
        static const uint32_t kTypes[] = {kImageType, kImageType, 0x12345678, 0};
        const uint32_t type = Pick(rng, kTypes);
        const uint32_t a4 = rng() % 3 ? 0 : colorFormat | (rng() % 2) << 16;
        const uint32_t a5 = rng() % 4 ? 0 : 7;
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        cIGZBuffer *newBuffer = reinterpret_cast<cIGZBuffer *>(0x5a5a5a5a);
        uint32_t size[2] = {0x5a5a, 0x5a5a};
        auto *b = reinterpret_cast<cIGZBuffer *>(&buffer);
        auto *s = static_cast<ifaces::cIGZFile *>(stream);
        auto *k = static_cast<const cGZPersistResourceKey *>(key);
        uint32_t result = 0;
        switch (op) {
        case 0: result = o->ReadStream(s, format, b, colorFormat); break;
        case 1: result = o->NewFromFile(path, &newBuffer, colorFormat); break;
        case 2: result = o->ReadFile(path, b, colorFormat); break;
        case 3: result = o->WriteStream(s, format, b, quality); break;
        case 4: result = o->WriteStreamRect(s, format, b, &rect, quality); break;
        case 5: result = o->WriteFile(path, b, quality); break;
        case 6: result = o->WriteFileRect(path, b, &rect, quality); break;
        case 7: result = o->IsReadable(path); break;
        case 8: result = o->GetImageSize(path, size); break;
        case 9: result = o->NewFromStream(s, format, &newBuffer, colorFormat); break;
        case 10: result = o->ReadKey(k, format, b, colorFormat); break;
        case 11: result = o->NewFromKey(k, format, &newBuffer, colorFormat); break;
        case 12: result = o->ReadDBRecord(record, format, b, colorFormat); break;
        case 13: result = o->NewFromDBRecord(record, format, &newBuffer, colorFormat); break;
        case 14: result = o->WriteKey(k, segment, format, b, quality); break;
        case 15: result = o->WriteKeyRect(k, segment, format, b, &rect, quality); break;
        case 16: result = o->WriteDBRecord(record, format, b, quality); break;
        case 17: result = o->WriteDBRecordRect(record, format, b, &rect, quality); break;
        case 18: result = face->CreateInstance(dbRecord, iid, &out, a4, a5); break;
        case 19: result = face->CreateInstanceByType(type, iid, &out, a4, a5); break;
        case 20: result = face->Read(reinterpret_cast<cIGZUnknown *>(resource), reinterpret_cast<cIGZUnknown *>(dbRecord)); break;
        case 21: result = face->Write(reinterpret_cast<cIGZUnknown *>(resource), reinterpret_cast<cIGZUnknown *>(dbRecord)); break;
        case 22: result = face->CreateInstance3(dbRecord, iid, &out); break;
        case 23: result = face->CreateResource(type, iid, &out); break;
        case 24: result = face->ReadRecord(reinterpret_cast<cIGZPersistResource *>(resource), record); break;
        case 25: result = face->WriteRecord(reinterpret_cast<cIGZPersistResource *>(resource), record); break;
        case 26:
            result = rng() % 2 ? o->QueryInterface(iid, &out) : face->QueryInterface(iid, &out);
            break;
        default: {
            const bool viaFace = rng() % 2 != 0;
            const uint32_t added = viaFace ? face->AddRef() : o->AddRef();
            result = (viaFace ? face->Release() : o->Release()) | added << 16;
            break;
        }
        }
        std::snprintf(what, sizeof(what), "op %u fmt %u cf %u q %u buf %d rect %d %d %d %d out %08x new %08x size %x %x",
                      op, format, colorFormat, quality, buffer.id, rect.left, rect.top, rect.right, rect.bottom,
                      Norm(shape::P(out)), Norm(shape::P(newBuffer)), size[0], size[1]);
        Snapshot(what, static_cast<uint8_t>(result) | (result & 0xffff0000));
    }
    // Release to the end: the +4 face's deleting destructor.
    for (int i = 0; i < 8; ++i) {
        const uint32_t left = o->Release();
        if (left == 0) {
            gFactory = nullptr;
            Log("Release -> 0");
            break;
        }
        Snapshot("Release", left);
    }
    gFactory = nullptr;
}

// The +0 face's slots through its vtable: ReadStream 0x87c26d, NewFromFile 0x87c1b3, ReadFile 0x87c1f1, WriteStream
// 0x87c9f9, WriteStreamRect 0x87c9a2, WriteFile 0x87c964, WriteFileRect 0x87c8f7, IsReadable 0x87c16c, GetImageSize
// 0x87c045, NewFromStream 0x87beab, ReadKey 0x87c2c9, NewFromKey 0x87beed, ReadDBRecord 0x87c33c, NewFromDBRecord
// 0x87bf60, WriteKey 0x87cace, WriteKeyRect 0x87ca3b, WriteDBRecord 0x87cb6b, WriteDBRecordRect 0x87cb14,
// QueryInterface 0x87c8a2, AddRef (folded 0x192140), Release 0x87c8d5; the +4 face's: QueryInterface 0x87c8cd, AddRef
// (folded 0x4805dd), Release 0x87c8ef, CreateInstanceByType 0x87bfc7, CreateInstance3 (folded 0x1f62f0),
// CreateResource 0x87bfa2, ReadRecord 0x87cd33, WriteRecord 0x87cd83, the destructor 0x87bd22 and the deleting one
// 0x87bd55 (thunk 0x87bd4d); the helpers CreateBuffer 0x87be75, ReadImage 0x87c3a0, CopyToBuffer 0x87d494,
// CopyIndices8 0x87d441, CopyIndices16 0x87d204, CopyIndices32 0x87d37e, Dither 0x87cfbf, WriteImage 0x87cbad,
// ConvertFromBuffer 0x87cdcc, HasAlpha 0x87bd74, LoadIjl 0x87c189, GetResourceManager 0x87c13c, GetGraphicSystem
// 0x87c015; the creator 0x87b412.
DIFF_TEST(GimexFactory_Script, 0x87c3a0)
{
    World world(ctx);
    std::mt19937 rng(77);
    // The paths the scripts must reach (checked on the original's traces), so a change in the fakes cannot quietly
    // leave one out.
    static const char *const kMarkers[] = {
        "reader0.Read(", "writer0.Write(", ".Init(", "GetNativeColorType", "GetBitsPerPixel", "resource0(",
        ".GetBuffer -> ", ".GetType -> ", "file0.Close", "file0.IsOpen", "gfx.CreateBuffer",
        "framework.GetSystemService", "manager.OpenRecord", "segment.OpenRecord", "record0.Close",
        "record0.QueryInterface(c56b8f08)", "Release -> 0", "Shutdown", "Unlock(8040)", "Unlock(8080)", "size ",
        "new 000000", "errors 0 "};
    std::vector<int> seen(sizeof(kMarkers) / sizeof(*kMarkers), 0);
    int dithered = 0;
    for (int i = 0; i < 300; ++i) {
        const uint32_t seed = rng();
        std::vector<std::string> traces[2];
        for (int side = 0; side < 2; ++side) {
            world.Reset(seed);
            void *object = side == 0 ? ctx.Original<void *(__cdecl *)()>(kCreate)()
                                     : static_cast<void *>(cGZGimexFactoryAdapters::Create());
            // Nothing in the class changes the colour format (+0x18) or the dither flag (+0x1c) after construction;
            // some scripts set them on both sides' objects so their tests are seen.
            if (seed % 4 == 1) *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(object) + 0x18) = 2 + seed / 4 % 7;
            if (seed % 4 == 2) *reinterpret_cast<uint8_t *>(static_cast<uint8_t *>(object) + 0x1c) = 0;
            RunScript(world, object, seed, 30);
            traces[side] = gLog;
        }
        for (const std::string &line : traces[0]) {
            for (size_t m = 0; m < seen.size(); ++m)
                if (line.find(kMarkers[m]) != std::string::npos) ++seen[m];
            const size_t e = line.find(" errors ");
            if (e != std::string::npos && line[e + 8] != '0') ++dithered;
        }
        ++ctx.cases;
        if (traces[0] != traces[1]) {
            size_t k = 0;
            while (k < traces[0].size() && k < traces[1].size() && traces[0][k] == traces[1][k]) ++k;
            ctx.Fail("script %d line %zu:\n    original: %s\n    ours:     %s\n    before:   %s", i, k,
                     k < traces[0].size() ? traces[0][k].c_str() : "(end)",
                     k < traces[1].size() ? traces[1][k].c_str() : "(end)", k ? traces[0][k - 1].c_str() : "");
            if (ctx.failures.size() >= 6) return;
        }
    }
    for (size_t m = 0; m < seen.size(); ++m)
        if (seen[m] == 0) ctx.Fail("no script reached '%s'", kMarkers[m]);
    if (dithered == 0) ctx.Fail("no script dithered");
}

// ---- the helpers called directly ----

// HasAlpha 0x87bd74 (__cdecl), with and without the scan, over random buffers.
DIFF_TEST(GimexFactory_HasAlpha, 0x87bd74)
{
    World world(ctx);
    std::mt19937 rng(5);
    using Fn = bool(__cdecl *)(cIGZBuffer *, bool);
    for (int i = 0; i < 600; ++i) {
        const uint32_t seed = rng();
        const bool scan = rng() % 4 != 0;
        uint32_t results[2];
        std::vector<std::string> logs[2];
        for (int side = 0; side < 2; ++side) {
            world.Reset(seed);
            auto *b = reinterpret_cast<cIGZBuffer *>(&gBuffers[0]);
            results[side] = side ? cGZGimexFactory::HasAlpha(b, scan) : ctx.Original<Fn>(0x87bd74)(b, scan);
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (results[0] != results[1] || logs[0] != logs[1])
            ctx.Fail("case %d (format %u, scan %d): original %u, ours %u", i, gBuffers[0].format, scan, results[0],
                     results[1]);
    }
}

// The converters (__fastcall source, destination, count; cGZGimexFactoryRows.cpp): To1555Opaque 0x87c790, To565
// 0x87c7e0, To1555 0x87c730, To4444 0x87c826, Copy32 0x87c880, From1555Opaque 0x87c5b0, From565 0x87c660, From1555
// 0x87c4f0, From4444 0x87c6f8.
DIFF_TEST(GimexFactory_Rows, 0x87c790)
{
    struct To {
        uint32_t rva;
        void(__fastcall *ours)(const uint32_t *, uint16_t *, uint32_t);
    };
    struct From {
        uint32_t rva;
        void(__fastcall *ours)(const uint16_t *, uint32_t *, uint32_t);
    };
    const To tos[] = {{0x87c790, &gimexrows::To1555Opaque},
                      {0x87c7e0, &gimexrows::To565},
                      {0x87c730, &gimexrows::To1555},
                      {0x87c826, &gimexrows::To4444}};
    const From froms[] = {{0x87c5b0, &gimexrows::From1555Opaque},
                          {0x87c660, &gimexrows::From565},
                          {0x87c4f0, &gimexrows::From1555},
                          {0x87c6f8, &gimexrows::From4444}};
    std::mt19937 rng(9);
    for (int i = 0; i < 300; ++i) {
        const uint32_t count = i < 20 ? static_cast<uint32_t>(i) : rng() % 70;
        std::vector<uint32_t> source32(count + 4);
        std::vector<uint16_t> source16(count + 4);
        for (uint32_t &w : source32) w = rng();
        for (uint16_t &w : source16) w = static_cast<uint16_t>(rng());
        for (const To &t : tos) {
            std::vector<uint16_t> a(count + 4, 0xabcd), b(count + 4, 0xabcd);
            ctx.Original<void(__fastcall *)(const uint32_t *, uint16_t *, uint32_t)>(t.rva)(source32.data(), a.data(), count);
            t.ours(source32.data(), b.data(), count);
            ++ctx.cases;
            if (a != b) ctx.Fail("%06x count %u: rows differ", t.rva, count);
        }
        for (const From &f : froms) {
            std::vector<uint32_t> a(count + 4, 0xabcdef01), b(count + 4, 0xabcdef01);
            ctx.Original<void(__fastcall *)(const uint16_t *, uint32_t *, uint32_t)>(f.rva)(source16.data(), a.data(), count);
            f.ours(source16.data(), b.data(), count);
            ++ctx.cases;
            if (a != b) ctx.Fail("%06x count %u: rows differ", f.rva, count);
        }
        std::vector<uint32_t> a(count + 4, 0x13579bdf), b(count + 4, 0x13579bdf);
        ctx.Original<void(__fastcall *)(const uint32_t *, uint32_t *, uint32_t)>(0x87c880)(source32.data(), a.data(), count);
        gimexrows::Copy32(source32.data(), b.data(), count);
        ++ctx.cases;
        if (a != b) ctx.Fail("Copy32 count %u: rows differ", count);
    }
}

// The palette packers (__cdecl r, g, b): Pack1555 0x87d6fa, Pack565 0x87d728, Pack4444 0x87d75b.
DIFF_TEST(GimexFactory_Pack, 0x87d6fa)
{
    using Fn = uint16_t(__cdecl *)(uint8_t, uint8_t, uint8_t);
    const std::pair<uint32_t, Fn> packers[] = {
        {0x87d6fa, &gimexrows::Pack1555}, {0x87d728, &gimexrows::Pack565}, {0x87d75b, &gimexrows::Pack4444}};
    std::mt19937 rng(13);
    for (int i = 0; i < 4000; ++i) {
        const uint8_t r = static_cast<uint8_t>(i < 256 ? i : rng());
        const uint8_t g = static_cast<uint8_t>(i < 256 ? 255 - i : rng());
        const uint8_t b = static_cast<uint8_t>(i < 256 ? i * 7 : rng());
        for (const auto &p : packers) {
            // The result as eax holds it: the packers' upper half is part of what a caller could read.
            const uint32_t a = reinterpret_cast<uint32_t(__cdecl *)(uint8_t, uint8_t, uint8_t)>(
                GameAddress<void>(p.first))(r, g, b);
            const uint16_t o = p.second(r, g, b);
            ++ctx.cases;
            if (static_cast<uint16_t>(a) != o) ctx.Fail("%06x(%02x, %02x, %02x): original %04x, ours %04x", p.first, r, g, b, a, o);
        }
    }
}

// The constructor on filled memory (0x87bcdc) and the creator's size (0x87b412): everything but the vtables, the
// lock's block (each side's own allocation, compared by contents) and the bytes after the dither flag.
DIFF_TEST(GimexFactory_Construct, 0x87bcdc)
{
    using Ctor = void *(__thiscall *)(void *);
    using Dtor = void(__thiscall *)(void *);
    for (const uint8_t fill : {uint8_t(0x00), uint8_t(0xcd)}) {
        alignas(8) uint8_t a[0x40], b[0x40];
        std::memset(a, fill, sizeof(a));
        std::memset(b, fill, sizeof(b));
        void *ra = ctx.Original<Ctor>(kConstruct)(a);
        void *rb = cGZGimexFactoryAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("fill %02x: the constructors' results", fill);
        if (Word(a, 0) != shape::P(GameAddress<void>(kVTable)) || Word(a, 4) != shape::P(GameAddress<void>(kFaceVTable)))
            ctx.Fail("fill %02x: the original's vtables", fill);
        const auto *la = reinterpret_cast<const cRZCriticalSection *>(a + 0xc);
        const auto *lb = reinterpret_cast<const cRZCriticalSection *>(b + 0xc);
        if (Word(a, 8) != Word(b, 8) || la->mVTable != lb->mVTable || la->mBlock->lockCount != lb->mBlock->lockCount ||
            std::memcmp(a + 0x14, b + 0x14, 9) != 0 || std::memcmp(a + 0x20, b + 0x20, sizeof(a) - 0x20) != 0)
            ctx.Fail("fill %02x: objects differ", fill);
        // The destructors (0x87bd22 the original's): the lock destroyed.
        ctx.Original<Dtor>(0x87bd22)(a);
        reinterpret_cast<cGZGimexFactory *>(b)->~cGZGimexFactory();
    }
}

} // namespace
