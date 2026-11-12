// Differential test for a light set's ambient map load (lane 261111n, scenegraph leftovers;
// src/sg/cLightingManagerAmbientMap.cpp): LoadAmbientMapImage 0xb409f0, a free cdecl function (group, instance, the
// map's counted slot) that copies an image resource's pixels into a cImageData, made when the slot is empty.
//
// The world: the persist system's cache cell 0x10faf6c holds a fake persist system (restored after the test) whose slot 3
// answers the fake resource manager or null; the manager's GetResource (slot 3) answers a drawn byte (0, 1, 0x80, random
// low bytes, junk above) and writes the resource on success (both sides use it unchecked) and now and then on failure
// too; the resource's QueryBuffer (slot 4) the same
// with the buffer; the buffer's width and height (slots 9, 10) 0..5; its pixels (slot 21) hashed from the seed and the
// coordinates; the slot empty or holding a pool image; group and instance drawn (0, the caller's 0x8ba01057, random).
// Now and then a pixel read throws (the unwinding releases the buffer and the resource), and the slot's image (or the
// framework's new one) is a fresh pool image.
//
// Fakes on both sides: the framework getter 0x1ae18d (slot 3 hands out pool images for class 0x1c4a276c), the images
// (1 / 2, 6 Init with its float's bits, 19 SetPixelOpaque with the colour's bits and its level), the resource and the
// buffer (1 / 2). Every call is logged in order with its arguments; the key's four words are logged where GetResource
// reads them.
//
// Compared: the log, the result byte (al), what the slot holds, every fake's reference count, the throw, and the heap
// blocks and bytes; at the x87's 53-bit and 24-bit precision.
#include "sg_lighting_fakes.h"

namespace {

using namespace lightfx;

constexpr uint32_t kPersistCell = 0x10faf6c; // CachedServiceGetter<0x10faf6c, ...> (0xb390c0)'s cache
constexpr int kImages = 4;

FakeRef gImages[kImages], gResource, gBuffer;
Plain gPersist, gManager;
void *gPersistVT[8], *gManagerVT2[8], *gResourceVT[8], *gBufferVT[24], *gImageVT[24];
uint32_t gNextImage;

struct World {
    uint32_t seed;
    bool haveManager;
    uint32_t resourceAnswer, bufferAnswer; // the answers' words (al decides)
    bool writeResource, writeBuffer;
    uint32_t width, height;
    int32_t throwAt; // the pixel read that throws (-1: none)
    int32_t swapAt;  // the pixel read during which the slot is given another image (-1: none): the target is read
                     // before the pixel
    uint32_t group, instance;
    const void *slot;
};
World gW;
int32_t gReads;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kImages; ++i)
        if (p == &gImages[i]) return 0x100 + i;
    if (p == &gResource) return 0x200;
    if (p == &gBuffer) return 0x300;
    if (p == &gPersist) return 0x400;
    if (p == &gManager) return 0x500;
    if (p == &gW.slot) return 0x600;
    return 0xbad00000u | (P(p) & 0xfffff);
}

void *Make(uint32_t cls)
{
    if (cls != 0x1c4a276c) return nullptr;
    FakeRef *const made = &gImages[gNextImage++ % kImages];
    ++made->refs;
    return made;
}

void *__fastcall PersistManager(Plain *self, void *) { return L(0x4003), L(Id(self)), gW.haveManager ? &gManager : nullptr; }

uint32_t __fastcall GetResource(Plain *self, void *, const uint32_t *key, uint32_t iid, void **out, uint32_t a4, uint32_t a5)
{
    L(0x5003), L(Id(self)), L(key[0]), L(key[1]), L(key[2]), L(key[3]), L(iid), L(out != nullptr), L(Id(*out)), L(a4), L(a5); // out is a stack cell
    if (gW.writeResource) *out = &gResource, ++gResource.refs;
    return gW.resourceAnswer;
}

uint32_t __fastcall QueryBuffer(FakeRef *self, void *, uint32_t iid, void **out)
{
    L(0x2004), L(Id(self)), L(iid), L(Id(*out));
    if (gW.writeBuffer) *out = &gBuffer, ++gBuffer.refs;
    return gW.bufferAnswer;
}

uint32_t __fastcall BufferWidth(FakeRef *self, void *) { return L(0x3009), L(Id(self)), gW.width; }
uint32_t __fastcall BufferHeight(FakeRef *self, void *) { return L(0x300a), L(Id(self)), gW.height; }
uint32_t __fastcall BufferPixel(FakeRef *self, void *, uint32_t x, uint32_t y)
{
    L(0x3015), L(Id(self)), L(x), L(y);
    if (gReads == gW.swapAt) gW.slot = &gImages[(Id(gW.slot) + 1) % kImages];
    if (gReads++ == gW.throwAt) throw 0xb409u;
    uint32_t h = gW.seed * 0x9e3779b9u ^ (x * 0x85ebca6bu) ^ (y * 0xc2b2ae35u);
    h ^= h >> 15, h *= 0x2c1b3c6du, h ^= h >> 12;
    return h;
}

void __fastcall ImageInit(FakeRef *self, void *, int32_t w, int32_t h, uint32_t levels, int32_t format, float scale,
                          uint32_t entries)
{
    L(0x1006), L(Id(self)), L(w), L(h), L(levels), L(format), L(U(scale)), L(entries);
}
void __fastcall ImageSetPixelOpaque(FakeRef *self, void *, int32_t x, int32_t y, const float *rgb, int32_t level)
{
    L(0x1013), L(Id(self)), L(x), L(y), LF(rgb, 3), L(level);
    std::memcpy(self->rgb, rgb, 12);
}

void FillTables()
{
    gIdOf = &Id;
    gMake = &Make;
    gComVT[3] = reinterpret_cast<void *>(&ComGetClassObject);
    gCom.vt = gComVT;
    gPersistVT[3] = reinterpret_cast<void *>(&PersistManager);
    gPersist.vt = gPersistVT;
    gManagerVT2[3] = reinterpret_cast<void *>(&GetResource);
    gManager.vt = gManagerVT2;
    for (void **vt : {gResourceVT, gBufferVT, gImageVT}) {
        vt[1] = reinterpret_cast<void *>(&AddRef);
        vt[2] = reinterpret_cast<void *>(&Release);
    }
    gResourceVT[4] = reinterpret_cast<void *>(&QueryBuffer);
    gBufferVT[9] = reinterpret_cast<void *>(&BufferWidth);
    gBufferVT[10] = reinterpret_cast<void *>(&BufferHeight);
    gBufferVT[21] = reinterpret_cast<void *>(&BufferPixel);
    gImageVT[6] = reinterpret_cast<void *>(&ImageInit);
    gImageVT[19] = reinterpret_cast<void *>(&ImageSetPixelOpaque);
}

uint32_t Answer(std::mt19937 &gen)
{
    const uint32_t junk = gen() % 2 ? gen() & ~0xffu : 0;
    switch (gen() % 4) {
    case 0: return junk;
    case 1: return junk | 1;
    case 2: return junk | 0x80;
    default: return junk | (gen() & 0xff);
    }
}

void Draw(uint32_t seed)
{
    std::mt19937 gen(seed * 0x9e3779b9u + 0xb409);
    gW = World{};
    gW.seed = seed;
    for (FakeRef &r : gImages) r = FakeRef{gImageVT, 0, 1 + gen() % 3};
    gResource = FakeRef{gResourceVT, 0, 1};
    gBuffer = FakeRef{gBufferVT, 0, 1};
    gNextImage = gen() % kImages;
    gReads = 0;
    gW.haveManager = gen() % 6 != 0;
    gW.resourceAnswer = Answer(gen);
    // A query that answers success always writes its object (both sides use it unchecked; a success without one
    // would only be a null call); a failing one may still write it (then it must be released).
    gW.writeResource = (gW.resourceAnswer & 0xff) != 0 || gen() % 2 != 0;
    gW.bufferAnswer = Answer(gen);
    gW.writeBuffer = (gW.bufferAnswer & 0xff) != 0 || gen() % 2 != 0;
    gW.width = gen() % 6;
    gW.height = gen() % 6;
    gW.throwAt = gen() % 6 == 0 ? int32_t(gen() % 30) : -1;
    const uint32_t groups[] = {0, 0x8ba01057u, gen()};
    gW.group = groups[gen() % 3];
    gW.instance = gen() % 4 == 0 ? 0 : gen();
    gW.slot = gen() % 2 ? nullptr : &gImages[gen() % kImages];
    gW.swapAt = gen() % 4 == 0 ? int32_t(gen() % 12) : -1;
    // A slot that would stay empty after a failed creation is never drawn: the original uses it unchecked
    // (ORIGINAL-BUG in src), so the framework always makes one here.
}

Blob Snapshot(const SavedGlobals &globals, uint32_t result, bool threw, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    o.push_back(result & 0xff), o.push_back(threw), o.push_back(Id(gW.slot));
    for (const FakeRef &r : gImages) o.push_back(r.refs);
    o.push_back(gResource.refs), o.push_back(gBuffer.refs), o.push_back(gNextImage), o.push_back(uint32_t(gReads));
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

struct Scope {
    svcfx::AllocatorScope allocator;
    SavedGlobals globals;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &GetFramework};
    Scope()
    {
        globals.Save(kPersistCell, 4);
        FillTables();
        *GameAddress<void *>(kPersistCell) = &gPersist;
        gLog.reserve(1 << 14);
    }
};

using Fn = uint8_t(__cdecl *)(uint32_t, const void *, const void **);

} // namespace

// 0xb409f0: the image resource {0x856ddbac, group, instance, 0} copied pixel by pixel into the slot's image (made when
// empty), 1; else the slot's image dropped, 0.
DIFF_TEST(sg_lighting_ambient_map_load, 0xb409f0)
{
    Scope scope;
    const Fn original = ctx.Original<Fn>(0xb409f0);
    SidesAtBothPrecisions(ctx, "load", 4000, false, [&](uint32_t draw, int side) {
        Draw(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        const Fn fn = side ? &nTSSG::LoadAmbientMapImage : original;
        const void *const instance = reinterpret_cast<const void *>(uintptr_t(gW.instance));
        difftest::AllocationCounter heap;
        uint32_t result = 0;
        bool threw = false;
        // Every AddRef / Release notes what the slot holds then (the failure path empties it before the release).
        gOnRef = [] { L(0x6000), L(Id(gW.slot)); };
        try {
            result = fn(gW.group, instance, &gW.slot);
        } catch (uint32_t e) {
            threw = true;
            L(0xdead), L(e);
        }
        gOnRef = nullptr;
        heap.Stop();
        return Snapshot(scope.globals, result, threw, heap.Blocks(), heap.Bytes());
    });
}
