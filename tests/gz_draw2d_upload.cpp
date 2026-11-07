// Differential tests for cDraw2D's pixel upload (lane OZ, src/gz/cDraw2D.cpp): UploadRecord 0x152410 (a record's buffer
// pixels copied into its cells' textures: colour key, border texels) and UploadKeyed32 0x14dfa0 (the 32-bit keyed copy).
//
// World: plain recording fakes with hand-made vtables. The record's buffer (Lock/Unlock, the native colour type, bytes per
// pixel, the colour converter, the bits and stride), the pool's textures (slot 28 answers their lockable surface), the
// lockable surfaces (a pixel format, LockRect / Unlock over real memory with a pitch, counted while locked), the pixel
// formats (id, bits, the ARGB face by QueryInterface 0x9ac38991 and its four widths) and the image manipulator (slot 5, the
// fallback copy). The two cached getters UploadRecord calls (0x18a7f0, 0x18d120) are stood in for. Compared: every call
// with its normalised arguments (addresses become tag | offset), the reference counts and a hash of every surface's memory.
//
// UploadRecord with rows but no columns reads stale locals in the original (not reproduced; not run). Borders are only
// made with regions of at least 3 x 3 texels (the border copies read the neighbouring column and row).
#include "difftest.h"

#include "code_patch.h"
#include "gz/GraphicServices.h"
#include "gz/cDraw2D.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace {

using D = nGZGraphic4::cDraw2D;
using nGZGraphic4::cSurfaceLock;

std::vector<uint32_t> gLog;
void Log(std::initializer_list<uint32_t> words) { gLog.insert(gLog.end(), words.begin(), words.end()); }

struct Range {
    uintptr_t lo, hi;
    uint32_t tag;
};
std::vector<Range> gRanges;
uint32_t Norm(const void *p)
{
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    if (!v) return 0;
    for (const Range &r : gRanges)
        if (v >= r.lo && v < r.hi) return r.tag | uint32_t(v - r.lo);
    return 0xdead0000u;
}
void AddRange(const void *p, size_t size, uint32_t tag)
{
    gRanges.push_back({reinterpret_cast<uintptr_t>(p), reinterpret_cast<uintptr_t>(p) + size, tag});
}

struct Obj {
    void **vt;
    int32_t refs = 1;
};
uint32_t __fastcall AddRefSlot(Obj *self, void *)
{
    return uint32_t(++self->refs);
}
uint32_t __fastcall ReleaseSlot(Obj *self, void *)
{
    Log({0x01, Norm(self)});
    return uint32_t(--self->refs);
}

// ---- pixel formats ----
struct Argb : Obj {
    uint32_t alpha = 0, red = 0, green = 0, blue = 0;
};
struct Format : Obj {
    uint32_t id = 0, bits = 0;
    Argb *argb = nullptr;
};
uint32_t __fastcall FormatQuery(Format *self, void *, uint32_t iid, Argb **out)
{
    Log({0x10, Norm(self), iid});
    if (iid == 0x9ac38991 && self->argb) {
        ++self->argb->refs;
        *out = self->argb;
        return 0;
    }
    *out = nullptr;
    return 0x80004002;
}
uint32_t __fastcall FormatId(Format *self, void *)
{
    Log({0x11, Norm(self)});
    return self->id;
}
uint32_t __fastcall FormatBits(Format *self, void *)
{
    Log({0x12, Norm(self)});
    return self->bits;
}
uint32_t __fastcall ArgbBits(Argb *self, void *)
{
    Log({0x13, Norm(self)});
    return 32;
}
uint32_t __fastcall ArgbAlpha(Argb *self, void *)
{
    Log({0x14, Norm(self)});
    return self->alpha;
}
uint32_t __fastcall ArgbRed(Argb *self, void *)
{
    Log({0x15, Norm(self)});
    return self->red;
}
uint32_t __fastcall ArgbGreen(Argb *self, void *)
{
    Log({0x16, Norm(self)});
    return self->green;
}
uint32_t __fastcall ArgbBlue(Argb *self, void *)
{
    Log({0x17, Norm(self)});
    return self->blue;
}

// ---- surfaces and textures ----
struct Surface : Obj {
    Format *format = nullptr;
    std::vector<uint8_t> mem;
    int32_t width = 0, height = 0, pitch = 0, bpp = 0;
    uint32_t index = 0;
};
Format *__fastcall SurfaceFormat(Surface *self, void *)
{
    Log({0x20, Norm(self)});
    return self->format;
}
uint32_t __fastcall SurfaceLockRect(Surface *self, void *, cSurfaceLock *lock, int32_t x, int32_t y, int32_t w, int32_t h,
                                    uint32_t readOnly)
{
    Log({0x21, Norm(self), uint32_t(x), uint32_t(y), uint32_t(w), uint32_t(h), readOnly & 0xff});
    ++self->refs;
    lock->surface = self;
    lock->rect = {x, y, x + w, y + h};
    lock->bits = self->mem.data() + size_t(y) * size_t(self->pitch) + size_t(x) * size_t(self->bpp);
    lock->pitch = self->pitch;
    lock->rowBytes = uint32_t(w * self->bpp);
    lock->rows = uint32_t(h);
    return uint32_t(h);
}
void __fastcall SurfaceUnlock(Surface *self, void *, cSurfaceLock *lock)
{
    Log({0x22, Norm(self), Norm(lock->bits), uint32_t(lock->pitch), Norm(lock->surface)});
}
struct Texture : Obj {
    Surface *surface = nullptr;
};
Surface *__fastcall TextureLockable(Texture *self, void *, uint32_t level)
{
    Log({0x23, Norm(self), level});
    return self->surface;
}

// ---- the buffer ----
struct Buffer : Obj {
    bool lockResult = true;
    uint32_t colorFormat = 0, colorBits = 0, bytesPerPixel = 0;
    const uint8_t *bits = nullptr;
    uint32_t stride = 0;
};
uint32_t __fastcall BufferLock(Buffer *self, void *, uint32_t flags)
{
    Log({0x30, Norm(self), flags});
    return self->lockResult ? 1u : 0u;
}
uint32_t __fastcall BufferUnlock(Buffer *self, void *, uint32_t flags)
{
    Log({0x31, Norm(self), flags});
    return 1;
}
uint32_t *__fastcall BufferNativeType(Buffer *self, void *, uint32_t *out)
{
    Log({0x32, Norm(self)});
    out[0] = self->colorFormat;
    out[1] = self->colorBits;
    return out;
}
uint32_t __fastcall BufferBytes(Buffer *self, void *)
{
    Log({0x33, Norm(self)});
    return self->bytesPerPixel;
}
uint32_t __fastcall BufferConvert(Buffer *self, void *, uint32_t r, uint32_t g, uint32_t b)
{
    Log({0x34, Norm(self), r & 0xff, g & 0xff, b & 0xff});
    r &= 0xff;
    g &= 0xff;
    b &= 0xff;
    return (r * 0x01010101u) ^ (g * 0x00010203u) ^ (b * 0x05040302u);
}
const uint8_t *__fastcall BufferBits(Buffer *self, void *)
{
    Log({0x35, Norm(self)});
    return self->bits;
}
uint32_t __fastcall BufferStride(Buffer *self, void *)
{
    Log({0x36, Norm(self)});
    return self->stride;
}

// ---- the image manipulator (the fallback copy) ----
struct Manip : Obj {};
uint32_t __fastcall ManipLoad(Manip *self, void *, const void *bits, void *format, uint32_t w, uint32_t h, uint32_t pitch,
                              void *target, const int32_t *rect, const uint32_t *key)
{
    Log({0x40, Norm(self), Norm(bits), Norm(format), w, h, pitch, Norm(target), uint32_t(rect[0]), uint32_t(rect[1]),
         uint32_t(rect[2]), uint32_t(rect[3]), Norm(key), *key});
    return 1;
}

constexpr unsigned kSlots = 40;
void *gFormatVT[kSlots], *gArgbVT[kSlots], *gSurfaceVT[kSlots], *gTextureVT[kSlots], *gBufferVT[kSlots], *gManipVT[kSlots];

void SetupVtables()
{
    for (void **vt : {gFormatVT, gArgbVT, gSurfaceVT, gTextureVT, gBufferVT, gManipVT})
        for (unsigned i = 0; i < kSlots; ++i) vt[i] = nullptr;
    for (void **vt : {gFormatVT, gArgbVT, gSurfaceVT, gTextureVT, gBufferVT, gManipVT}) {
        vt[1] = reinterpret_cast<void *>(&AddRefSlot);
        vt[2] = reinterpret_cast<void *>(&ReleaseSlot);
    }
    gFormatVT[0] = reinterpret_cast<void *>(&FormatQuery);
    gFormatVT[3] = reinterpret_cast<void *>(&FormatId);
    gFormatVT[6] = reinterpret_cast<void *>(&FormatBits);
    gArgbVT[6] = reinterpret_cast<void *>(&ArgbBits);
    gArgbVT[11] = reinterpret_cast<void *>(&ArgbAlpha);
    gArgbVT[12] = reinterpret_cast<void *>(&ArgbRed);
    gArgbVT[13] = reinterpret_cast<void *>(&ArgbGreen);
    gArgbVT[14] = reinterpret_cast<void *>(&ArgbBlue);
    gSurfaceVT[22] = reinterpret_cast<void *>(&SurfaceFormat);
    gSurfaceVT[25] = reinterpret_cast<void *>(&SurfaceLockRect);
    gSurfaceVT[27] = reinterpret_cast<void *>(&SurfaceUnlock);
    gTextureVT[28] = reinterpret_cast<void *>(&TextureLockable);
    gBufferVT[6] = reinterpret_cast<void *>(&BufferLock);
    gBufferVT[7] = reinterpret_cast<void *>(&BufferUnlock);
    gBufferVT[15] = reinterpret_cast<void *>(&BufferNativeType);
    gBufferVT[17] = reinterpret_cast<void *>(&BufferBytes);
    gBufferVT[30] = reinterpret_cast<void *>(&BufferConvert);
    gBufferVT[34] = reinterpret_cast<void *>(&BufferBits);
    gBufferVT[35] = reinterpret_cast<void *>(&BufferStride);
    gManipVT[5] = reinterpret_cast<void *>(&ManipLoad);
}

// ---- the two cached getters UploadRecord calls ----
Format *gTargetFormat = nullptr;
Manip *gManip = nullptr;
void *__cdecl StandFormat(uint32_t colorType)
{
    Log({0x50, colorType});
    return gTargetFormat;
}
void *__cdecl StandManip()
{
    Log({0x51});
    return gManip;
}

// ---- the world of one case ----
struct World {
    Argb argb;
    Format target, surfaceFormat;
    Buffer buffer;
    Manip manip;
    std::vector<Texture> textures;
    std::vector<Surface> surfaces;
    std::vector<uint8_t> source;
    alignas(16) uint8_t block[D::kSize]{};
    D::Record record;
    uint32_t pool = 0;

    D *Draw() { return reinterpret_cast<D *>(block); }
    World() = default;
    ~World()
    {
        for (unsigned i = 0; i < 3; ++i) {
            Draw()->mPools[i].textures.items.clear();
            Draw()->mPools[i].~Pool();
        }
        record.buffer = nullptr;
    }
    World(const World &) = delete;
    World &operator=(const World &) = delete;
};

uint32_t Fnv(const std::vector<uint8_t> &bytes)
{
    uint32_t h = 2166136261u;
    for (uint8_t b : bytes) h = (h ^ b) * 16777619u;
    return h;
}

// Builds the world from the seed (the same world for both sides).
void Build(World &w, uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 17);
    gLog.clear();
    gRanges.clear();
    const auto pick = [&](std::initializer_list<uint32_t> v) { return *(v.begin() + rng() % v.size()); };
    w.argb.vt = gArgbVT;
    w.argb.red = pick({0, 4, 5, 5, 5, 6, 8, 8});
    w.argb.green = pick({0, 4, 5, 6, 6, 8, 8});
    w.argb.blue = pick({0, 4, 5, 5, 8, 8});
    w.argb.alpha = pick({0, 1, 4, 8});
    if (rng() % 12 == 0) w.argb.red = w.argb.green = w.argb.blue = 0; // no colour bits
    const uint32_t formatId = 20 + rng() % 8;
    w.target.vt = gFormatVT;
    w.target.id = formatId;
    w.target.bits = pick({16, 32});
    w.target.argb = &w.argb;
    w.surfaceFormat.vt = gFormatVT;
    w.surfaceFormat.id = rng() % 6 == 0 ? formatId + 1 : formatId; // mostly the same format as the buffer's
    const uint32_t surfBpp = pick({2, 4});
    w.surfaceFormat.bits = surfBpp * 8;
    w.buffer.vt = gBufferVT;
    w.manip.vt = gManipVT;
    w.buffer.lockResult = rng() % 20 != 0;
    w.buffer.colorFormat = pick({2, 4, 5, 6, 7, 8, 9, 3});
    w.buffer.colorBits = pick({16, 32});
    w.buffer.bytesPerPixel = pick({2, 2, 4, 4, 4, 1, 3});
    const uint32_t bpp = w.buffer.bytesPerPixel;

    const uint32_t atlasCount = 1 + rng() % 3;
    w.pool = rng() % 3;
    // the key and the mask the keyed copies work out (so the source pixels can hold the key)
    const uint32_t colourBits = w.argb.red + w.argb.green + w.argb.blue;
    const uint32_t mask = colourBits ? 0xffffffffu >> ((32 - colourBits) & 31) : 0;
    uint32_t word1c = rng() % 4 == 0 ? 0u : (rng() | 1u);
    if (rng() % 8 == 0) word1c = 1u << (24 + rng() % 8); // a key set only in the unused byte
    const uint32_t keyNative = ((((word1c >> 16) & 0xff) * 0x01010101u) ^ (((word1c >> 8) & 0xff) * 0x00010203u) ^
                                ((word1c & 0xff) * 0x05040302u)) &
                               mask;

    // the source image: 200 x 220 texels of the buffer's size
    const uint32_t sourceWidth = 200, sourceHeight = 220;
    w.buffer.stride = sourceWidth * bpp + (rng() % 3) * 4 + (rng() % 2);
    w.source.assign(size_t(w.buffer.stride) * sourceHeight + 16, 0);
    for (size_t i = 0; i + 4 <= w.source.size(); i += bpp == 4 ? 4 : bpp == 2 ? 2 : 1) {
        uint32_t px = rng();
        if (rng() % 3 == 0) px = keyNative | (rng() & ~mask);
        else if (rng() % 7 == 0) px = keyNative;
        if (bpp == 4) std::memcpy(&w.source[i], &px, 4);
        else if (bpp == 2) std::memcpy(&w.source[i], &px, 2);
        else w.source[i] = uint8_t(px);
    }
    w.buffer.bits = w.source.data();

    w.surfaces.resize(atlasCount);
    w.textures.resize(atlasCount);
    D *const draw = w.Draw();
    for (unsigned i = 0; i < 3; ++i) new (&draw->mPools[i]) D::Pool;
    D::Pool &pool = draw->mPools[w.pool];
    pool.atlases.resize(atlasCount);
    pool.textures.items.resize(atlasCount);
    for (uint32_t a = 0; a < atlasCount; ++a) {
        Surface &s = w.surfaces[a];
        s.vt = gSurfaceVT;
        s.format = &w.surfaceFormat;
        s.width = s.height = 128;
        s.bpp = int32_t(surfBpp);
        s.pitch = s.width * s.bpp + int32_t(rng() % 4) * 4;
        s.mem.resize(size_t(s.pitch) * size_t(s.height) + 64);
        for (uint8_t &b : s.mem) b = uint8_t(rng());
        s.index = a;
        w.textures[a].vt = gTextureVT;
        w.textures[a].surface = &s;
        pool.textures.items[a] = reinterpret_cast<cIGZUnknown *>(&w.textures[a]);
        // a few nodes, each with a rectangle inside the surface
    }

    const uint32_t columns = 1 + rng() % 4, rows = rng() % 5 == 0 ? 0 : 1 + rng() % 4;
    const bool borderX = rng() % 3 == 0, borderY = rng() % 3 == 0;
    const uint32_t nodeCount = 2 + rng() % 5;
    for (uint32_t a = 0; a < atlasCount; ++a) {
        auto &nodes = pool.atlases[a].mNodes;
        nodes.resize(nodeCount);
        for (auto &n : nodes) {
            const int32_t minW = borderX ? 3 : 0, minH = borderY ? 3 : 0;
            const int32_t width = minW + int32_t(rng() % (40 - minW + 1)), height = minH + int32_t(rng() % (40 - minH + 1));
            const int32_t left = int32_t(rng() % 80), top = int32_t(rng() % 80);
            n.rect = {left, top, left + width, top + height};
        }
    }
    D::Record &r = w.record;
    r.width = 200;
    r.height = 200;
    r.buffer = reinterpret_cast<cIGZBuffer *>(&w.buffer);
    r.word1c = word1c;
    r.columns = columns;
    r.rows = rows;
    r.pool = w.pool;
    r.borderX = borderX;
    r.borderY = borderY;
    r.cells.clear();
    for (uint32_t i = 0; i < columns * rows; ++i) r.cells.push_back({rng() % atlasCount, rng() % nodeCount});

    AddRange(&w.buffer, sizeof w.buffer, 0x01000000);
    AddRange(&w.target, sizeof w.target, 0x02000000);
    AddRange(&w.surfaceFormat, sizeof w.surfaceFormat, 0x03000000);
    AddRange(&w.argb, sizeof w.argb, 0x04000000);
    AddRange(&w.manip, sizeof w.manip, 0x05000000);
    AddRange(&r, sizeof r, 0x06000000);
    AddRange(w.source.data(), w.source.size(), 0x10000000);
    AddRange(&w.record.word1c, 4, 0x07000000);
    for (uint32_t a = 0; a < atlasCount; ++a) {
        AddRange(&w.textures[a], sizeof(Texture), 0x08000000 + a * 0x100);
        AddRange(&w.surfaces[a], sizeof(Surface), 0x09000000 + a * 0x100);
        AddRange(w.surfaces[a].mem.data(), w.surfaces[a].mem.size(), 0x20000000 + a * 0x01000000);
    }
    gTargetFormat = &w.target;
    gManip = &w.manip;
}

void Finish(World &w)
{
    Log({0xf0, uint32_t(w.buffer.refs), uint32_t(w.argb.refs), uint32_t(w.target.refs), uint32_t(w.surfaceFormat.refs)});
    for (const Surface &s : w.surfaces) Log({0xf1, uint32_t(s.refs), Fnv(s.mem)});
    Log({0xf2, Fnv(w.source)});
}

using UploadFn = void(__thiscall *)(void *, void *);

std::vector<uint32_t> RunRecord(difftest::Context &ctx, uint32_t seed, bool ours)
{
    World w;
    Build(w, seed);
    if (ours)
        w.Draw()->UploadRecord(w.record);
    else
        ctx.Original<UploadFn>(0x152410)(w.block, &w.record);
    Finish(w);
    return gLog;
}

void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
{
    ++ctx.cases;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: logs differ at word %zu (original %zu words, ours %zu); original 0x%x, ours 0x%x", what, seed, i,
             a.size(), b.size(), i < a.size() ? a[i] : 0u, i < b.size() ? b[i] : 0u);
}

struct Patches {
    shape::StandIn format{0x18a7f0, &nGZGraphic4::GetPixelFormatForColorType, &StandFormat};
    shape::StandIn manip{0x18d120, &nGZGraphic4::GetImageManipCached, &StandManip};
};

} // namespace

DIFF_TEST(cDraw2D_UploadRecord, 0x152410)
{
    SetupVtables();
    Patches patches;
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<uint32_t> original = RunRecord(ctx, seed, false);
        const std::vector<uint32_t> ours = RunRecord(ctx, seed, true);
        Compare(ctx, "UploadRecord", seed, original, ours);
        if (!ctx.failures.empty()) return;
    }
}

// ---- UploadKeyed32 0x14dfa0 ----
//
// One surface (LockRect / Unlock over memory) and a source that is either a buffer of its own or a window into the surface's
// memory itself (so rows overlap the destination at every offset: the original's scalar path). The rectangle and the
// width / height arguments are independent (the lock is of the rectangle, the loops run over the arguments). Compared: the
// calls, the surface's reference count and memory.
namespace {

using Keyed32Fn = void(__cdecl *)(const uint8_t *, uint32_t, uint32_t, uint32_t, void *, const cGZRect *, uint32_t,
                                  uint32_t);

std::vector<uint32_t> RunKeyed(difftest::Context &ctx, uint32_t seed, bool ours)
{
    std::mt19937 rng(seed * 40503u + 5);
    gLog.clear();
    gRanges.clear();
    Surface s;
    s.vt = gSurfaceVT;
    s.width = s.height = 128;
    s.bpp = 4;
    s.pitch = s.width * 4 + int32_t(rng() % 3) * 4;
    s.mem.resize(size_t(s.pitch) * size_t(s.height) + 64);
    const uint32_t key = rng() % 3 == 0 ? 0u : rng();
    const uint32_t mask = rng() % 4 == 0 ? 0xffffffffu : rng() % 3 == 0 ? 0u : 0xffffffffu >> (rng() % 32);
    for (size_t i = 0; i + 4 <= s.mem.size(); i += 4) {
        uint32_t px = rng();
        if (rng() % 3 == 0) px = key | (rng() & ~mask);
        std::memcpy(&s.mem[i], &px, 4);
    }
    const uint32_t width = rng() % 6 == 0 ? 0 : rng() % 3 == 0 ? 16 + rng() % 25 : rng() % 20;
    const uint32_t height = rng() % 8 == 0 ? 0 : 1 + rng() % 8;
    std::vector<uint8_t> own(size_t(256) * 10 + 64);
    for (size_t i = 0; i + 4 <= own.size(); i += 4) {
        uint32_t px = rng();
        if (rng() % 3 == 0) px = key | (rng() & ~mask);
        std::memcpy(&own[i], &px, 4);
    }
    uint32_t stride = width * 4 + (rng() % 4) * 4 + (rng() % 5 == 0 ? 1 : 0);
    if (stride > 256) stride = 256;
    const uint8_t *source = own.data();
    if (rng() % 3 == 0) { // inside the surface's own memory
        const size_t need = size_t(stride) * height + width * 4 + 4;
        const size_t span = s.mem.size() > need ? s.mem.size() - need : 0;
        source = s.mem.data() + (rng() % 2 ? rng() % (span + 1) : (size_t(rng() % 40) * size_t(s.pitch) + rng() % 200));
        if (source + need > s.mem.data() + s.mem.size()) source = s.mem.data();
    }
    const int32_t left = int32_t(rng() % 40), top = int32_t(rng() % 40);
    const cGZRect rect{left, top, left + int32_t(rng() % 45), top + int32_t(rng() % 12)};
    AddRange(&s, sizeof s, 0x09000000);
    AddRange(s.mem.data(), s.mem.size(), 0x20000000);
    AddRange(own.data(), own.size(), 0x10000000);
    Log({0xe0, width, height, stride, Norm(source)});
    if (ours)
        D::UploadKeyed32(source, width, height, stride, &s, &rect, key, mask);
    else
        ctx.Original<Keyed32Fn>(0x14dfa0)(source, width, height, stride, &s, &rect, key, mask);
    Log({0xf1, uint32_t(s.refs), Fnv(s.mem)});
    return gLog;
}

} // namespace

DIFF_TEST(cDraw2D_UploadKeyed32, 0x14dfa0)
{
    SetupVtables();
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        const std::vector<uint32_t> original = RunKeyed(ctx, seed, false);
        const std::vector<uint32_t> ours = RunKeyed(ctx, seed, true);
        Compare(ctx, "UploadKeyed32", seed, original, ours);
        if (!ctx.failures.empty()) return;
    }
}
