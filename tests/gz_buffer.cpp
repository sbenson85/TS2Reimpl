// Differential test for cGZBuffer and cGZTransientBuffer (lane LQ, src/gz/cGZBuffer.h), in the standalone shape.
//
// GZBuffer_Run: a buffer built by the original creator (0x8c75d0: a block from the 0x54 pool, the constructor 0x8c5710;
// or 0x8c7620 for a transient buffer: new 0x68, the same constructor, the rest inline) against one built by ours
// (cGZBuffer::Create / cGZTransientBuffer::Create), each driven through its own vtables by the same random operations,
// then destroyed by the last Release (cGZBuffer: the deleting destructor 0x8c78b0, back to the pool; cGZTransientBuffer:
// 0x8c7950, back to the heap). Operations, on every face by slot number:
// - cIGZUnknown on each face: QueryInterface (0x8c5850, transient 0x8c72c0; thunks 0x8c7d09 / 0x8c7d11, 0x8c7d19 / 0x8c7d21
//   / 0x8c7d29), AddRef (0x8c5870, 0x8c7270; 0x8c7a13 / 0x8c7a1b, 0x8c7a23 / 0x8c7a2b / 0x8c7a33), Release (0x8c5890,
//   0x8c7290; 0x8c7d31 / 0x8c7d39, 0x8c7d41 / 0x8c7d49 / 0x8c7d51), with and without an outer unknown;
// - cIGZBuffer: Init 0x8c58c0 (with formats 3 / 1, the display's format: InitPixelFormat 0x8c6f60 through the graphics
//   service getter, a stand-in, with and without its display driver), Shutdown 0x8c5930, Uninitialize 0x8c5950, Lock 0x8c5970
//   (transient 0x8c74e0, which realizes through the target), Unlock 0x8c59a0, IsLocked 0x8c7d00, GetWidth (shared_vf_15e550),
//   GetHeight (shared_vf_15e970), GetBounds (shared_vf_1f4e50), GetBoundsPtr (shared_vf_158f90), GetMemoryUsage 0x8c59d0,
//   GetColorType / GetNativeColorType (shared_vf_149d60), GetBitsPerPixel (shared_vf_149d00), GetBytesPerPixel 0x8c7c90,
//   Fill 0x8c5c00, FillCoords 0x8c5bc0, FillRect 0x8c59f0 (8, 16, 24 and 32 bits; the row helpers 0x1b2ccf / 0x1b2d0a and
//   memset), GetPixel 0x8c5c20, SetPixel 0x8c5c90, SetTransparency 0x8c5d00, GetTransparency 0x8c7cf0, IsPixelTransparent
//   0x8c5d40 (the mask 0x8c7ca0), GetTransparentColor 0x8c5d90, RemoveTransparency 0x8c5d20, DrawLine 0x8c5db0
//   (thin at every depth, thick, horizontal and vertical; the line clipper 0x8c7a50), Blt 0x8c6420, the four colour conversions
//   0x8c65f0 / 0x8c66d0 / 0x8c67f0 / 0x8c68f0, GetColorSurfaceBits 0x8c6a60, GetColorSurfaceStride 0x8c6a70,
//   GetChangeCounter 0x8c6a80, GetHardwareCache (shared_vf_169f60), SetHardwareCache 0x8c7d60, IsReady 0x8c59c0;
// - the class's slots: ClipRects 0x8c64d0, AllocatePixels 0x8c6ee0, AllocatePixelMemory 0x8c6f00 (transient 0x8c7530),
//   FreePixelMemory 0x8c6f40 (transient 0x8c7580), InitPixelFormat 0x8c6f60, BltUnclipped 0x8c7060 (the bytes per pixel
//   0x8c76f0; the converter 0x8c6b10 a stand-in on both sides, its arguments logged);
// - cIGZBufferShader: SetColor 0x8c6aa0, GetColor 0x8c6ad0; cIGZAggregate: NonDelegatingQueryInterface 0x8c57f0 (transient
//   0x8c72d0), SetOuter 0x8c57d0, GetOuter 0x8c57e0;
// - cIGZTransientBuffer: GetBuffer 0x8c7a40, SetMemory 0x8c7360 (the colour type constructor 0x18a220), Commit 0x8c7420,
//   Realize 0x8c7450, SetTarget 0x8c7480, GetTarget (shared_vf_147a30), GetLockTotal (shared_vf_145770).
// The fakes (svcfx): the outer unknown, two hardware caches (slot 3 answers, slot 9 logs), two targets (Realize may hand
// the buffer memory through SetMemory), two memory owners, a source buffer for Blt / ClipRects, the graphics service and
// its display driver. Byte arguments are pushed as dwords with random high bytes (the callee reads the low byte).
// Compared after every operation: the result (the low byte for bools), the outputs (sentinel-filled), the stack balance,
// the event log, every field (vtable words as "still the side's own"; padding the constructor never writes masked: +0x0d..
// +0x0f, +0x3f, +0x65..+0x67), the pixel memory's bytes, the fakes' counts and words; at the end, the pool's rings and pages
// and whether the last Release freed the pixel block, with what it left allocated minus freed (blocks, bytes). Lane MH:
// these come from this thread's exact records (difftest::FreeRecorder, AllocationCounter), for pixel blocks of any size;
// before, a HeapWalk read only blocks of 0x4400 bytes or more (a smaller one was logged as 2) and had no net count.
//
// The test fills pixel memory fresh from new[] with a pattern of its own (new[] leaves it uninitialised: the bytes are the
// heap's, different on each side). The external memory SetMemory takes is the test's, from the heap: the destructor
// 0x8c7950 frees the pixel pointer whether it is external or not (the base destructor's `push [esi + 0x40]; call 0x6c4b50`).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/GZConvertPixels.h"
#include "gz/cGZBuffer.h"
#include "gz/cRZCursor.h"
#include "rz/RZFixedAllocator.h"

#include <windows.h>

#include <malloc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <vector>

namespace gzbuffer_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "dd"}, {2, "dd"}, {3, "ddddd"}, {4, "ddddd"}, {7, "--dddddd"}, {8, "ddd"}, {9, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;

constexpr uint32_t kTag = 0x7e000000;      // the buffer under test
constexpr uint32_t kPageTag = 0x7d000000;  // the pool's pages
constexpr uint32_t kSourceTag = 0x7b000000; // the source fake's pixels
constexpr uint32_t kBlockTag = 0x70000000; // pixel blocks: kBlockTag | index << 16 | offset

// The game's vtables of each class's faces.
constexpr uint32_t kBufferVTables[3] = {0xe4f210, 0xe4f2d0, 0xe4f2e8};
constexpr uint32_t kTransientVTables[4] = {0xe4f304, 0xe4f3c4, 0xe4f3dc, 0xe4f3f8};
constexpr uint32_t kFaceOffsets[4] = {0, 4, 8, 0x54};

// The fakes.
enum : unsigned {
    kOuter = 0,
    kCacheA = 1,
    kCacheB = 2,
    kTargetA = 3,
    kTargetB = 4,
    kOwnerA = 5,
    kOwnerB = 6,
    kSource = 7,
    kGraphics = 8,
    kDriver = 9,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint8_t *At(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

// ---- calls with the stack balance checked ----

int32_t gStackSkew;
uint32_t CallChecked(void *fn, void *self, const uint32_t *args, uint32_t count)
{
    uint32_t result, before, after;
    __asm {
        mov before, esp
        mov ecx, count
        mov edx, args
    push_next:
        test ecx, ecx
        jz pushed
        dec ecx
        push dword ptr [edx + ecx * 4]
        jmp push_next
    pushed:
        mov ecx, self
        call fn
        mov result, eax
        mov after, esp
        mov esp, before
    }
    if (after != before) gStackSkew = int32_t(after - before);
    return result;
}
template <class... A> uint32_t Slot(void *face, unsigned slot, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return CallChecked((*static_cast<void ***>(face))[slot], face, args, uint32_t(sizeof...(A)));
}
template <class... A> uint32_t BoolSlot(void *face, unsigned slot, A... a) { return Slot(face, slot, a...) & 0xff; }

// ---- the game's heap and the pool ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

constexpr uint32_t kPageSize = 0x2000;
constexpr uint32_t kBlocks = 97; // 0x54-byte blocks after the 16-byte header
uint8_t *gPages; // 3 pages: 0 current, 1 handed out by NewPage, 2 spare
RZFixedPage *Page(int i) { return reinterpret_cast<RZFixedPage *>(gPages + kPageSize * uint32_t(i)); }
uint8_t *Block(int page, uint32_t k) { return gPages + kPageSize * uint32_t(page) + 16 + k * 0x54; }

void ChainPage(int p, uint32_t freeBlocks)
{
    RZFixedPage *page = Page(p);
    uint8_t *first = nullptr;
    for (uint32_t k = freeBlocks; k-- > 0;) {
        *reinterpret_cast<uint8_t **>(Block(p, k)) = first;
        first = Block(p, k);
    }
    page->freeList = first;
    page->used = kBlocks - freeBlocks;
    page->next = page->prev = page;
}

bool gNewPage; // the pool built page 1 (PoolNewPage): until then its header is the test's random fill
RZFixedPage *__fastcall PoolNewPage(void *, void *)
{
    Ev(0x77000002);
    gNewPage = true;
    ChainPage(1, kBlocks);
    Page(1)->used = 0;
    return Page(1);
}
void __fastcall PoolFreePage(void *, void *, RZFixedPage *page)
{
    Ev(0x77000003);
    Ev(svcfx::Norm(P(page)));
}
void *gPoolVT[4] = {nullptr, nullptr, reinterpret_cast<void *>(&PoolNewPage), reinterpret_cast<void *>(&PoolFreePage)};

void BuildPool(uint32_t seed, uint32_t freeBlocks)
{
    if (!gPages) gPages = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPageSize * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::mt19937 r(seed * 0x9e3779b1u + 5);
    for (uint32_t i = 0; i < kPageSize * 3; ++i) gPages[i] = uint8_t(r());
    gNewPage = false;
    // Page 0 holds no free block when freeBlocks is 0: the creator takes the next page (TakeFreePage, NewPage).
    ChainPage(0, freeBlocks);
    uint32_t *pool = GameAddress<uint32_t>(cGZBuffer::kPool);
    pool[0] = P(gPoolVT);
    pool[1] = P(Page(0));
    pool[2] = 0;
}

void DumpPool(Blob &b)
{
    const uint32_t *pool = GameAddress<uint32_t>(cGZBuffer::kPool);
    b.push_back(svcfx::Norm(pool[1]));
    b.push_back(svcfx::Norm(pool[2]));
    for (int p = 0; p < 2; ++p) {
        const RZFixedPage *page = Page(p);
        if (p == 1 && !gNewPage) { // the fill, as data (the same on both sides)
            b.insert(b.end(), {P(page->next), P(page->prev), P(page->freeList), page->used});
            continue;
        }
        b.push_back(svcfx::Norm(P(page->next)));
        b.push_back(svcfx::Norm(P(page->prev)));
        b.push_back(svcfx::Norm(P(page->freeList)));
        b.push_back(page->used);
    }
}

// ---- pixel blocks ----

// Every pixel block of the side (new[] by the buffer, or the test's external memory), by address, with its size and index;
// the side's ranges name them kBlockTag | index << 16 | offset.
struct PixelBlock {
    uint32_t size;
    uint32_t index;
    bool external;
};
std::map<uintptr_t, PixelBlock> gBlocks;
uint32_t gBlockCount;
uint32_t gSeed;

void ForgetRange(uintptr_t lo)
{
    for (auto it = svcfx::gRanges.begin(); it != svcfx::gRanges.end();)
        it = it->lo == lo ? svcfx::gRanges.erase(it) : it + 1;
}

void TrackBlock(void *p, uint32_t size, bool external)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(p);
    ForgetRange(at);
    const uint32_t index = gBlockCount++;
    gBlocks[at] = {size, index, external};
    svcfx::gRanges.insert(svcfx::gRanges.begin(), {at, at + (size ? size : 1), kBlockTag | index << 16});
}

// The external blocks' least size. It made them heap back-end blocks for a HeapWalk that once read whether the pixel block
// was freed; a FreeRecorder answers that now, for blocks of any size (lane MH), and the size stays so the inputs do.
constexpr uint32_t kLargeBlock = 0x4400;

// The test's external memory: `size` bytes from the heap with a pattern (the block is always large: see kLargeBlock).
void *NewExternal(uint32_t size)
{
    auto *p = static_cast<uint8_t *>(::operator new(size > kLargeBlock ? size : kLargeBlock));
    std::mt19937 r(gSeed * 31 + gBlockCount);
    for (uint32_t i = 0; i < size; ++i) p[i] = uint8_t(r());
    TrackBlock(p, size, true);
    return p;
}

// After an operation: a pixel pointer that changed to memory the test did not hand out is a block the buffer just made
// with new[] (stride * height bytes, uninitialised): the test's pattern, and tracked.
void SeeOwnBlock(void *object, uint32_t before)
{
    const uint32_t now = Word(object, 0x40);
    if (!now || now == before) return;
    const auto it = gBlocks.find(uintptr_t(now));
    if (it != gBlocks.end() && it->second.external) return;
    auto *pixels = reinterpret_cast<uint8_t *>(uintptr_t(now));
    const uint32_t size = Word(object, 0x44) * Word(object, 0x24);
    std::mt19937 r(gSeed * 17 + gBlockCount);
    for (uint32_t i = 0; i < size; ++i) pixels[i] = uint8_t(r());
    TrackBlock(pixels, size, false);
}

// The usable size of the buffer's pixel memory (0 when it has none).
uint32_t PixelBytes(void *object)
{
    const auto it = gBlocks.find(uintptr_t(Word(object, 0x40)));
    if (it == gBlocks.end()) return 0;
    const uint32_t want = Word(object, 0x44) * Word(object, 0x24);
    return want <= it->second.size ? want : it->second.size;
}

// ---- the object ----

void *gObject;    // the buffer (its primary face)
bool gTransient;  // a cGZTransientBuffer
uint32_t gSize;   // 0x54 or 0x68
uint32_t gFaces;  // 3 or 4
uint32_t gVTables[4]; // the side's vtable words as built
uint8_t gSourcePixels[256]; // the source fake's pixels
bool gSelfSource;            // the Blt in progress copies the buffer onto itself

void *Face(unsigned f) { return At(gObject, kFaceOffsets[f]); }

void DumpObject(Blob &b)
{
    for (uint32_t off = 0; off < gSize; off += 4) {
        const uint32_t w = Word(gObject, off);
        unsigned face = ~0u;
        for (unsigned f = 0; f < gFaces; ++f)
            if (kFaceOffsets[f] == off) face = f;
        if (face != ~0u) {
            b.push_back(w == gVTables[face] ? 0x0fac0000u | face : 0xbad00000u | face);
            continue;
        }
        uint32_t mask = 0xffffffffu;
        if (off == 0x0c || off == 0x64) mask = 0xff;  // a bool; the rest padding
        if (off == 0x3c) mask = 0x00ffffffu;          // lock count, bytes per pixel; padding
        // Pointers by what they point at; data as it is (a colour or counter that happened to fall inside one side's
        // heap block would read as a pointer on that side only).
        const bool pointer = off == 0x40 || off == 0x4c || off == 0x50 || off == 0x58 || off == 0x5c;
        b.push_back(pointer ? svcfx::Norm(w) : w & mask);
    }
    const uint32_t bytes = PixelBytes(gObject);
    b.push_back(bytes);
    const auto *pixels = reinterpret_cast<const uint8_t *>(uintptr_t(Word(gObject, 0x40)));
    // The bytes by hash (FNV-1a), and the first 64 as they are.
    uint32_t hash = 0x811c9dc5u;
    for (uint32_t i = 0; i < bytes; ++i) hash = (hash ^ pixels[i]) * 0x01000193u;
    b.push_back(hash);
    for (uint32_t i = 0; i < bytes && (i < 64 || svcfx::gTrace); i += 4) {
        uint32_t w = 0;
        for (uint32_t k = 0; k < 4 && i + k < bytes; ++k) w |= uint32_t(pixels[i + k]) << (8 * k);
        b.push_back(w);
    }
}

// ---- stand-ins ----

// The pixel converter 0x8c6b10 (the original BltUnclipped calls the image's, ours calls ours; a StandIn over both): its arguments.
// The pointers as offsets: the target's from the buffer's pixels; the source's from the source fake's pixels, or from the
// buffer's (a buffer copying from itself). (Unclipped rectangles put them outside the memory, so not by range.)
bool __cdecl FakeConvert(uint8_t *to, uint32_t toStride, uint32_t toFormat, const uint8_t *from, uint32_t fromStride,
                         uint32_t fromFormat, int32_t width, int32_t height, const uint32_t *key)
{
    const uint32_t pixels = Word(gObject, 0x40);
    Ev(0x77000020);
    Ev(P(to) - pixels);
    Ev(toStride);
    Ev(toFormat);
    Ev(P(from) - (gSelfSource ? pixels : P(gSourcePixels)));
    Ev(fromStride);
    Ev(fromFormat);
    Ev(uint32_t(width));
    Ev(uint32_t(height));
    Ev(key ? 1u : 0u);
    if (key) Ev(*key);
    return true;
}

// The graphics service getter 0x9c5a05: the graphics fake or null.
Obj *gGraphics;
void *__cdecl FakeGraphics()
{
    Ev(0x77000022);
    return gGraphics;
}

// ---- the fakes' handlers ----

// Graphics service slot 0 (QueryInterface): the display driver when value[0] says so.
uint32_t GraphicsQuery(Obj *self, uint32_t *args)
{
    if (!(self->value[0] & 1)) return 0;
    *reinterpret_cast<uint32_t *>(uintptr_t(args[1])) = P(&F(kDriver));
    return 1;
}
// Graphics service slot 9: its colour type, through the result pointer.
uint32_t GraphicsColorType(Obj *self, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    out[0] = self->value[1];
    out[1] = self->value[2];
    return args[0];
}
// Display driver slot 15: the request's defined bits and words are logged, then the depth written.
uint32_t DriverMode(Obj *self, uint32_t *args)
{
    auto *mode = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    // Bits 9..31 of the first word are left as the stack had them by the original (`and eax, 0xfffffe40; or eax, 0x40`
    // at 0x8c6fb1): only bits 0..8 are defined.
    Ev(mode[0] & 0x1ff);
    for (int i = 1; i < 5; ++i) Ev(mode[i]);
    mode[0] = (mode[0] & ~0x3fu) | (self->value[0] & 0x3f);
    return 0;
}
// Source buffer slot 15 (GetNativeColorType): through the result pointer.
uint32_t SourceColorType(Obj *self, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    out[0] = self->value[4];
    out[1] = self->value[5];
    return args[0];
}
// Source buffer slot 26 (GetTransparentColor): the key when value[7] says so.
uint32_t SourceKey(Obj *self, uint32_t *args)
{
    if (!(self->value[7] & 1)) return 0;
    *reinterpret_cast<uint32_t *>(uintptr_t(args[0])) = self->value[6];
    return 1;
}
// Hardware cache slot 3 (takes the buffer): answers value[1]; when value[0] says so, first installs a cache on the
// buffer itself through SetHardwareCache (slot 38): itself (value[0] bit 1) or the other one, once (the inner call's
// slot 3 only answers), so the outer call finds a current cache, the same as its argument or not.
bool gInCacheAttach;
uint32_t CacheAttach(Obj *self, uint32_t *args)
{
    if ((self->value[0] & 1) && !gInCacheAttach) {
        gInCacheAttach = true;
        void *other = &F(self == &F(kCacheA) ? kCacheB : kCacheA);
        Ev(BoolSlot(reinterpret_cast<void *>(uintptr_t(args[0])), 38, (self->value[0] & 2) ? static_cast<void *>(self) : other));
        gInCacheAttach = false;
    }
    return self->value[1];
}
// Target slot 5 (Realize): hands the buffer memory through SetMemory when value[0] says so, and answers value[1].
uint32_t TargetRealize(Obj *self, uint32_t *args)
{
    if (self->value[0] & 1) {
        const uint32_t width = 1 + self->value[2] % 9, height = 1 + self->value[3] % 7, stride = width * 4 + 4;
        void *memory = NewExternal(stride * height);
        Slot(reinterpret_cast<void *>(uintptr_t(args[0])), 4, memory, stride, width, height, self->value[4] % 11,
             (self->value[0] & 2) ? &F(kOwnerA) : nullptr);
    }
    return self->value[1];
}


void BuildFakes(uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 7);
    svcfx::ResetAll();
    // Room for every event up front: a log that grew while the destructor ran could take the block it just freed.
    svcfx::gEv.reserve(1 << 16);
    gBlocks.clear();
    gBlockCount = 0;
    Obj &outer = F(kOuter);
    outer.Set(0, "dp", r() & 1);
    for (unsigned c : {kCacheA, kCacheB}) {
        F(c).Set(3, "p", 0u, &CacheAttach);
        F(c).value[0] = r() % 6; // 1, 3, 5: install a cache first (3: itself)
        F(c).value[1] = r() % 3 != 0;
        F(c).Set(9, 0);
    }
    for (unsigned t : {kTargetA, kTargetB}) {
        Obj &target = F(t);
        target.Set(3, "p");
        target.Set(4, "p");
        target.Set(5, "p", 0u, &TargetRealize);
        target.Set(6, "p");
        // The fakes' values are captured through svcfx::Norm: keep data out of the user address range, so it never reads
        // as a pointer into one side's heap.
        for (int i = 0; i < 5; ++i) target.value[i] = r() & 0xffff;
    }
    Obj &source = F(kSource);
    source.value[0] = 0;
    source.value[1] = 0;
    source.value[2] = 1 + r() % 16;
    source.value[3] = 1 + r() % 12;
    source.value[4] = r() % 11;
    source.value[5] = r() % 40;
    source.value[6] = 0xff000000u | (r() & 0xffffff); // the transparent colour (alpha byte set: never an address)
    source.value[7] = r() & 0xff;
    source.Set(6, "d", r() % 4 != 0);
    source.Set(7, "d");
    source.Set(12, 0, &source.value[0]); // GetBoundsPtr: {0, 0, value[2], value[3]}
    source.Set(15, "p", 0u, &SourceColorType);
    source.Set(26, "p", 0u, &SourceKey);
    source.Set(34, 0, r() % 5 ? P(gSourcePixels) : 0u);
    source.Set(35, 0, 16 + r() % 16);
    Obj &graphics = F(kGraphics);
    graphics.value[0] = r() & 0xff;
    graphics.value[1] = 4 + r() % 6;
    graphics.value[2] = (r() & 1) ? 16 : 32;
    graphics.Set(0, "dp", 0u, &GraphicsQuery);
    graphics.Set(9, "p", 0u, &GraphicsColorType);
    Obj &driver = F(kDriver);
    driver.value[0] = r() % 40;
    driver.Set(15, "p", 0u, &DriverMode);
    gGraphics = r() % 4 ? &graphics : nullptr;
    svcfx::AddRange(gSourcePixels, sizeof(gSourcePixels), kSourceTag);
}

// ---- operations ----

// A rectangle near the buffer's (sometimes far off, or wrapping), on the stack of the caller.
cGZRect RandomRect()
{
    const int32_t w = int32_t(Word(gObject, 0x20)), h = int32_t(Word(gObject, 0x24));
    if (Draw(16) == 0) return {int32_t(gRng()), int32_t(gRng()), int32_t(gRng()), int32_t(gRng())};
    cGZRect r;
    r.left = int32_t(Draw(uint32_t(w) + 10)) - 5;
    r.top = int32_t(Draw(uint32_t(h) + 10)) - 5;
    r.right = r.left + int32_t(Draw(uint32_t(w) + 8)) - 1;
    r.bottom = r.top + int32_t(Draw(uint32_t(h) + 8)) - 1;
    return r;
}
void PushRect(Blob &b, const cGZRect &r)
{
    b.push_back(uint32_t(r.left));
    b.push_back(uint32_t(r.top));
    b.push_back(uint32_t(r.right));
    b.push_back(uint32_t(r.bottom));
}
uint32_t RandomFormat() { return Draw(8) == 0 ? gRng() : Draw(11); }
uint32_t RandomBits()
{
    static const uint32_t kBits[] = {8, 16, 24, 32, 16, 32, 0, 12};
    return kBits[Draw(8)];
}
uint32_t Garbage(uint32_t low) { return (gRng() & 0xffffff00u) | (low & 0xff); }

// Whether the pixel at (x, y) lies in the buffer's memory (so GetPixel / SetPixel stay inside it).
bool PixelInside(uint32_t x, uint32_t y)
{
    const uint32_t bytes = PixelBytes(gObject);
    if (!bytes) return false;
    const uint32_t bpp = (Word(gObject, 0x3c) >> 16) & 0xff;
    // GetPixel / SetPixel touch bytes per pixel bytes for 1..4, else one.
    const uint64_t end = uint64_t(Word(gObject, 0x44)) * y + uint64_t(bpp) * x + (bpp >= 1 && bpp <= 4 ? bpp : 1);
    return end <= bytes;
}
// Whether filling is safe: memory for the whole bounds at the stride (the clipped target stays inside the bounds).
bool FillSafe()
{
    const uint32_t bytes = PixelBytes(gObject);
    const uint32_t w = Word(gObject, 0x20), h = Word(gObject, 0x24), stride = Word(gObject, 0x44);
    const uint32_t bits = Word(gObject, 0x14);
    if (!bytes || w > 256 || h > 256) return false;
    return uint64_t(stride) * h <= bytes && uint64_t(w) * (bits >> 3) <= stride;
}

constexpr uint32_t kSentinel = 0xc5c5c5c5u;

enum Kind : uint32_t {
    kQuery, kAddRef, kRelease, kInit, kShutdown, kUninitialize, kLock, kUnlock, kIsLocked, kGetWidth, kGetHeight, kGetBounds,
    kGetBoundsPtr, kGetMemoryUsage, kGetColorType, kGetNativeColorType, kGetBitsPerPixel, kGetBytesPerPixel, kFill, kFillCoords,
    kFillRect, kGetPixel, kSetPixel, kSetTransparency, kGetTransparency, kIsPixelTransparent, kGetTransparentColor,
    kRemoveTransparency, kDrawLine, kBlt, kRgbToNative, kNativeToRgb, kRgbaToNative, kNativeToRgba, kGetBits, kGetStride,
    kGetChangeCounter, kGetCache, kSetCache, kIsReady, kClipRects, kAllocatePixels, kAllocatePixelMemory, kFreePixelMemory,
    kInitPixelFormat, kBltUnclipped, kSetColor, kGetColor, kNonDelegatingQuery, kSetOuter, kGetOuter,
    // cIGZTransientBuffer (a plain buffer does an Init instead)
    kGetBuffer, kSetMemory, kCommit, kRealize, kSetTarget, kGetTarget, kGetLockTotal,
    kKinds
};

void RunOp(Blob &b, uint32_t kind)
{
    void *primary = Face(0);
    if (!gTransient && kind >= kGetBuffer) kind = kInit;
    b.push_back(kind);
    switch (kind) {
    case kQuery: {
        static const uint32_t kIids[] = {1, cIGZBuffer::kIid, cIGZAggregate::kIid, cIGZBufferShader::kIid,
                                         cIGZTransientBuffer::kIid, 0x12345678};
        const uint32_t riid = Draw(8) == 0 ? 0xff000000u | (gRng() & 0xffffff) : kIids[Draw(6)]; // (logged through svcfx::Norm by the outer: never an address)
        uint32_t out = kSentinel;
        const uint32_t ok = BoolSlot(Face(Draw(gFaces)), 0, riid, &out);
        b.push_back(ok);
        b.push_back(svcfx::NormOut(out, 0xc5c5c5c5));
        // Hand the reference back (a fake outer's answer leaves the out word alone).
        if (ok && out != kSentinel && out) b.push_back(Slot(reinterpret_cast<void *>(uintptr_t(out)), 2));
        break;
    }
    case kAddRef: b.push_back(Slot(Face(Draw(gFaces)), 1)); break;
    case kRelease:
        // Never the last reference (the end of the run takes that).
        if (!Word(gObject, 0x50) && Word(gObject, 0x30) == 1) {
            b.push_back(0xdead);
            break;
        }
        b.push_back(Slot(Face(Draw(gFaces)), 2));
        break;
    case kInit: {
        // Small, or large enough for the heap's back end (the end of the run checks the destructor freed it).
        const bool large = Draw(3) == 0;
        uint32_t w = large ? 90 + Draw(60) : 1 + Draw(20), h = large ? 50 + Draw(40) : 1 + Draw(16);
        switch (Draw(12)) {
        case 0: w = 0; break;
        case 1: h = 0; break;
        case 2: w = 0x80000000u | Draw(4); break;
        case 3: h = 0xfffffff0u; break;
        default: break;
        }
        const uint32_t format = Draw(3) == 0 ? (Draw(2) ? 3u : 1u) : RandomFormat();
        b.push_back(BoolSlot(primary, 3, w, h, format, RandomBits()));
        break;
    }
    case kShutdown: b.push_back(BoolSlot(primary, 4)); break;
    case kUninitialize: b.push_back(BoolSlot(primary, 5)); break;
    case kLock: {
        static const uint32_t kFlags[] = {0, 0x40, 0x8000, 0x8040, 3};
        b.push_back(BoolSlot(primary, 6, Draw(6) == 0 ? gRng() : kFlags[Draw(5)]));
        break;
    }
    case kUnlock: b.push_back(BoolSlot(primary, 7, Draw(3) == 0 ? gRng() : 0x40u)); break;
    case kIsLocked: b.push_back(BoolSlot(primary, 8)); break;
    case kGetWidth: b.push_back(Slot(primary, 9)); break;
    case kGetHeight: b.push_back(Slot(primary, 10)); break;
    case kGetBounds: {
        uint32_t out[5] = {kSentinel, kSentinel, kSentinel, kSentinel, kSentinel};
        b.push_back(svcfx::Norm(Slot(primary, 11, &out[0])) == svcfx::Norm(P(out)));
        b.insert(b.end(), out, out + 5);
        break;
    }
    case kGetBoundsPtr: b.push_back(svcfx::Norm(Slot(primary, 12))); break;
    case kGetMemoryUsage: b.push_back(Slot(primary, 13)); break;
    case kGetColorType:
    case kGetNativeColorType: {
        uint32_t out[3] = {kSentinel, kSentinel, kSentinel};
        b.push_back(Slot(primary, kind == kGetColorType ? 14 : 15, &out[0]) == P(out));
        b.insert(b.end(), out, out + 3);
        break;
    }
    case kGetBitsPerPixel: b.push_back(Slot(primary, 16)); break;
    case kGetBytesPerPixel: b.push_back(Slot(primary, 17)); break;
    case kFill:
    case kFillCoords:
    case kFillRect: {
        if (!FillSafe()) {
            b.push_back(0xdead);
            break;
        }
        const uint32_t value = gRng();
        cGZRect clip = RandomRect();
        const cGZRect *clipArg = Draw(2) ? &clip : nullptr;
        cGZRect rect = RandomRect();
        // The clip step of ClipRects compares edges by a wrapping subtraction (0x8c658e: sub eax, [esi]; jle), so a wild
        // rectangle against a clip can move an edge outside the buffer and the fill writes past its memory: those pairs
        // are left out (a wild rectangle with no clip, which only the bounds step sees, still runs).
        const auto wild = [](const cGZRect &r) {
            const int32_t v[] = {r.left, r.top, r.right, r.bottom};
            for (int32_t x : v)
                if (x < -0x40000000 || x > 0x40000000) return true;
            return false;
        };
        if (clipArg && (wild(rect) || wild(clip))) {
            b.push_back(0xdeae);
            break;
        }
        if (svcfx::gTrace)
            fprintf(stderr, "  fill rect %d %d %d %d clip %d %d %d %d%s pixels %08x\n", rect.left, rect.top, rect.right,
                    rect.bottom, clip.left, clip.top, clip.right, clip.bottom, clipArg ? "" : " (none)", Word(gObject, 0x40));
        if (kind == kFill) b.push_back(BoolSlot(primary, 18, value, clipArg));
        else if (kind == kFillCoords)
            b.push_back(BoolSlot(primary, 19, value, rect.left, rect.top, rect.right, rect.bottom, clipArg));
        else b.push_back(BoolSlot(primary, 20, value, &rect, clipArg));
        PushRect(b, rect);
        PushRect(b, clip);
        break;
    }
    case kGetPixel:
    case kSetPixel:
    case kIsPixelTransparent: {
        uint32_t x = Draw(Word(gObject, 0x20) + 1), y = Draw(Word(gObject, 0x24) + 1);
        if (kind == kIsPixelTransparent && Draw(4) == 0) x = gRng();
        const bool inside = x < Word(gObject, 0x20) && y < Word(gObject, 0x24) && PixelInside(x, y);
        if (kind == kIsPixelTransparent) {
            // Reads the pixel only when transparency is on and (x, y) is inside the bounds.
            const bool reads = (Word(gObject, 0x2c) & 2) && x < Word(gObject, 0x20) && y < Word(gObject, 0x24);
            if (reads && !inside) {
                b.push_back(0xdead);
                break;
            }
            b.push_back(BoolSlot(primary, 25, x, y));
            break;
        }
        if (!inside) {
            b.push_back(0xdead);
            break;
        }
        if (kind == kGetPixel) b.push_back(Slot(primary, 21, x, y));
        else Slot(primary, 22, x, y, gRng());
        break;
    }
    case kSetTransparency: {
        // Often a pixel's value, so IsPixelTransparent matches.
        uint32_t colour = gRng();
        const uint32_t x = Draw(Word(gObject, 0x20) + 1), y = Draw(Word(gObject, 0x24) + 1);
        if (Draw(2) && PixelInside(x, y) && x < Word(gObject, 0x20) && y < Word(gObject, 0x24))
            colour = Slot(primary, 21, x, y) ^ (Draw(2) ? 0 : 0xff000000u);
        Slot(primary, 23, colour);
        break;
    }
    case kGetTransparency: b.push_back(BoolSlot(primary, 24)); break;
    case kGetTransparentColor: {
        uint32_t out = kSentinel;
        b.push_back(BoolSlot(primary, 26, &out));
        b.push_back(out);
        break;
    }
    case kRemoveTransparency: Slot(primary, 27); break;
    case kDrawLine: {
        // Lines (and their horizontal / vertical fills) only into memory that holds the bounds at the stride.
        if (!FillSafe()) {
            b.push_back(0xdead);
            break;
        }
        const int32_t w = int32_t(Word(gObject, 0x20)), h = int32_t(Word(gObject, 0x24));
        auto coord = [](int32_t extent) { return int32_t(Draw(uint32_t(extent) + 40)) - 20; };
        int32_t x0 = coord(w), y0 = coord(h), x1 = coord(w), y1 = coord(h);
        if (Draw(6) == 0) y1 = y0;
        else if (Draw(6) == 0) x1 = x0;
        static const int32_t kThickness[] = {1, 1, 1, 0, 2, 3, 4, 6};
        int32_t thickness = kThickness[Draw(8)];
        // Thick lines write 16-bit pixels whatever the depth: only where a row of them fits the stride.
        if (thickness != 1 && uint32_t(w) * 2 > Word(gObject, 0x44)) thickness = 1;
        if (svcfx::gTrace) fprintf(stderr, "DrawLine %d %d %d %d t %d\n", x0, y0, x1, y1, thickness);
        b.push_back(BoolSlot(primary, 28, x0, y0, x1, y1, gRng(), thickness));
        break;
    }
    case kBlt:
    case kBltUnclipped: {
        const bool self = Draw(3) == 0;
        gSelfSource = self;
        void *source = self ? primary : static_cast<void *>(&F(kSource));
        cGZRect from = RandomRect(), to = RandomRect(), clip = RandomRect();
        if (kind == kBlt) b.push_back(BoolSlot(primary, 29, source, &from, &to, Draw(2) ? &clip : nullptr));
        else b.push_back(BoolSlot(primary, 46, source, &from, &to));
        PushRect(b, from);
        PushRect(b, to);
        PushRect(b, clip);
        break;
    }
    case kRgbToNative: b.push_back(Slot(primary, 30, Garbage(gRng()), Garbage(gRng()), Garbage(gRng()))); break;
    case kRgbaToNative: b.push_back(Slot(primary, 32, Garbage(gRng()), Garbage(gRng()), Garbage(gRng()), Garbage(gRng()))); break;
    case kNativeToRgb:
    case kNativeToRgba: {
        uint32_t out[4] = {kSentinel, kSentinel, kSentinel, kSentinel};
        const uint32_t value = Draw(4) ? gRng() & 0xffff : gRng();
        if (kind == kNativeToRgb) b.push_back(BoolSlot(primary, 31, value, &out[0], &out[1], &out[2]));
        else b.push_back(BoolSlot(primary, 33, value, &out[0], &out[1], &out[2], &out[3]));
        b.insert(b.end(), out, out + 4);
        break;
    }
    case kGetBits: b.push_back(svcfx::Norm(Slot(primary, 34))); break;
    case kGetStride: b.push_back(Slot(primary, 35)); break;
    case kGetChangeCounter: b.push_back(Slot(primary, 36)); break;
    case kGetCache: b.push_back(svcfx::Norm(Slot(primary, 37))); break;
    case kSetCache: {
        const uint32_t current = Word(gObject, 0x4c);
        void *cache = Draw(4) == 0 && current ? reinterpret_cast<void *>(uintptr_t(current)) : &F(Draw(2) ? kCacheA : kCacheB);
        b.push_back(BoolSlot(primary, 38, cache));
        break;
    }
    case kIsReady: b.push_back(BoolSlot(primary, 39)); break;
    case kClipRects: {
        void *bounds = Draw(2) ? primary : static_cast<void *>(&F(kSource));
        cGZRect source = RandomRect(), destination = RandomRect(), clip = RandomRect();
        b.push_back(BoolSlot(primary, 41, bounds, &source, &destination, Draw(2) ? &clip : nullptr));
        PushRect(b, source);
        PushRect(b, destination);
        break;
    }
    case kAllocatePixels:
    case kAllocatePixelMemory: {
        // Sized by the bounds as they are: only small ones (the old block, if any, is left as the original leaves it).
        if (Word(gObject, 0x20) > 256 || Word(gObject, 0x24) > 256) {
            b.push_back(0xdead);
            break;
        }
        b.push_back(BoolSlot(primary, kind == kAllocatePixels ? 42 : 43));
        break;
    }
    case kFreePixelMemory: b.push_back(BoolSlot(primary, 44)); break;
    case kInitPixelFormat: Slot(primary, 45); break;
    case kSetColor:
        b.push_back(BoolSlot(Face(1), 3, Garbage(gRng()), Garbage(gRng()), Garbage(gRng()), Draw(8) ? Garbage(gRng()) : 0xffu));
        break;
    case kGetColor: {
        uint32_t out[4] = {kSentinel, kSentinel, kSentinel, kSentinel};
        b.push_back(BoolSlot(Face(1), 4, &out[0], &out[1], &out[2], &out[3]));
        b.insert(b.end(), out, out + 4);
        break;
    }
    case kNonDelegatingQuery: {
        static const uint32_t kIids[] = {1, cIGZBuffer::kIid, cIGZAggregate::kIid, cIGZBufferShader::kIid,
                                         cIGZTransientBuffer::kIid, 0x12f + 1};
        uint32_t out = kSentinel;
        const uint32_t ok = BoolSlot(Face(2), 3, kIids[Draw(6)], &out);
        b.push_back(ok);
        b.push_back(svcfx::NormOut(out, 0xc5c5c5c5));
        break;
    }
    case kSetOuter: Slot(Face(2), 4, Draw(3) == 0 ? &F(kOuter) : nullptr); break;
    case kGetOuter: b.push_back(svcfx::Norm(Slot(Face(2), 5))); break;
    case kGetBuffer: b.push_back(svcfx::Norm(Slot(Face(3), 3))); break;
    case kSetMemory: {
        const uint32_t width = 1 + Draw(12), height = 1 + Draw(9), stride = width * 4 + Draw(3) * 4;
        void *memory = Draw(6) ? NewExternal(stride * height) : nullptr;
        void *owner = Draw(3) == 0 ? nullptr : &F(Draw(2) ? kOwnerA : kOwnerB);
        Slot(Face(3), 4, memory, stride, width, height, RandomFormat(), owner);
        break;
    }
    case kCommit: Slot(Face(3), 5); break;
    case kRealize: b.push_back(BoolSlot(Face(3), 6)); break;
    case kSetTarget: {
        const uint32_t pick = Draw(5);
        void *target = pick == 0 ? nullptr : &F(pick < 3 ? kTargetA : kTargetB);
        Slot(Face(3), 7, target);
        break;
    }
    case kGetTarget: b.push_back(svcfx::Norm(Slot(Face(3), 8))); break;
    case kGetLockTotal: b.push_back(Slot(Face(3), 9)); break;
    default: break;
    }
}

void Finish(Blob &b)
{
    b.push_back(uint32_t(gStackSkew)); // bytes popped beyond (or short of) what was pushed: 0
    gStackSkew = 0;
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b, kValueKinds);
}

void *Build(difftest::Context &ctx, bool ours, bool transient)
{
    if (transient) {
        void *face = ours ? static_cast<void *>(cGZTransientBuffer::Create())
                          : ctx.Original<void *(__cdecl *)()>(0x8c7620)();
        return face ? At(face, 0) - 0x54 : nullptr;
    }
    return ours ? static_cast<void *>(cGZBuffer::Create()) : ctx.Original<void *(__cdecl *)()>(0x8c75d0)();
}

struct Scope {
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameFree{0x82540f, shape::CodeOf(&GameFree)};
    shape::StandIn convert{0x8c6b10, &GZConvertPixels, &FakeConvert};
    shape::StandIn graphics{0x9c5a05, &GetGraphicSystem, &FakeGraphics};
    Scope()
    {
        char traceFlag[4];
        svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
        globals.SetWord(cGZBuffer::kPool, 0);
        globals.SetWord(cGZBuffer::kPool + 4, 0);
        globals.SetWord(cGZBuffer::kPool + 8, 0);
    }
    ~Scope() { svcfx::gRanges.clear(); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

} // namespace gzbuffer_test

using namespace gzbuffer_test;

DIFF_TEST(GZBuffer_Run, 0x8c75d0)
{
    // Also reached (see the file comment): 0x8c7620, 0x8c5710, 0x8c78b0, 0x8c7950, the destructors they hold (0x8c5790
    // cGZBuffer's, 0x8c71d0 cGZTransientBuffer's) and the transient constructor 0x8c7190 (ours: the same code paths), 0x8c5850, 0x8c72c0, 0x8c7d09, 0x8c7d11,
    // 0x8c7d19, 0x8c7d21, 0x8c7d29, 0x8c5870, 0x8c7270, 0x8c7a13, 0x8c7a1b, 0x8c7a23, 0x8c7a2b, 0x8c7a33, 0x8c5890, 0x8c7290,
    // 0x8c7d31, 0x8c7d39, 0x8c7d41, 0x8c7d49, 0x8c7d51, 0x8c58c0, 0x8c5930, 0x8c5950, 0x8c5970, 0x8c74e0, 0x8c59a0, 0x8c7d00,
    // 0x15e550, 0x15e970, 0x1f4e50, 0x158f90, 0x8c59d0, 0x149d60, 0x149d00, 0x8c7c90, 0x8c5c00, 0x8c5bc0, 0x8c59f0, 0x8c5c20,
    // 0x8c5c90, 0x8c5d00, 0x8c7cf0, 0x8c5d40, 0x8c7ca0, 0x8c5d90, 0x8c5d20, 0x8c5db0, 0x8c7a50, 0x8c6420, 0x8c65f0, 0x8c66d0, 0x8c67f0, 0x8c68f0,
    // 0x8c6a60, 0x8c6a70, 0x8c6a80, 0x169f60, 0x8c7d60, 0x8c59c0, 0x8c64d0, 0x8c6ee0, 0x8c6f00, 0x8c7530, 0x8c6f40, 0x8c7580,
    // 0x8c6f60, 0x8c7060, 0x8c76f0, 0x8c6aa0, 0x8c6ad0, 0x8c57f0, 0x8c72d0, 0x8c57d0, 0x8c57e0, 0x8c7a40, 0x8c7360, 0x18a220,
    // 0x8c7420, 0x8c7450, 0x8c7480, 0x147a30, 0x145770.
    Scope scope;
    // TS2_TEST_SEED=n runs that seed alone (with TS2_TEST_TRACE, to trace one failure).
    char seedText[16] = {};
    const uint32_t only = GetEnvironmentVariableA("TS2_TEST_SEED", seedText, sizeof(seedText)) ? uint32_t(atoi(seedText)) : 0;
    // TS2_TEST_PROGRESS names each seed before it runs (to find the seed of a crash).
    const bool progress = GetEnvironmentVariableA("TS2_TEST_PROGRESS", nullptr, 0) != 0;
    for (uint32_t seed = only ? only : 1; seed <= (only ? only : 3000); ++seed) {
        if (progress) fprintf(stderr, "seed %u\n", unsigned(seed));
        std::mt19937 plan(seed);
        std::vector<uint32_t> ops(10 + plan() % 60);
        for (uint32_t &k : ops) k = plan() % kKinds;
        const bool transient = seed % 2 == 0;
        const uint32_t freeBlocks = plan() % 4;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            BuildFakes(seed);
            BuildPool(seed, freeBlocks);
            svcfx::AddRange(gPages, kPageSize * 3, kPageTag);
            gTransient = transient;
            gSize = transient ? cGZTransientBuffer::kSize : cGZBuffer::kSize;
            gFaces = transient ? 4 : 3;
            Blob first;
            gObject = Build(ctx, side == 1, transient);
            svcfx::AddRange(gObject, gSize, kTag);
            for (unsigned f = 0; f < gFaces; ++f) {
                gVTables[f] = Word(gObject, kFaceOffsets[f]);
                // The original's words are the game's vtables, ours are not.
                const uint32_t game = P(GameAddress<void>(transient ? kTransientVTables[f] : kBufferVTables[f]));
                first.push_back((gVTables[f] == game) == (side == 0));
            }
            DumpObject(first);
            DumpPool(first);
            Finish(first);
            results[side].push_back(std::move(first));
            Slot(gObject, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace)
                        fprintf(stderr, "seed %u side %d op %u kind %u (w %u h %u bits %u fmt %08x stride %u bytes %u)\n",
                                seed, side, unsigned(i), ops[i], Word(gObject, 0x20), Word(gObject, 0x24),
                                Word(gObject, 0x14), Word(gObject, 0x3c), Word(gObject, 0x44), PixelBytes(gObject));
                    const uint32_t before = Word(gObject, 0x40);
                    RunOp(b, ops[i]);
                    SeeOwnBlock(gObject, before);
                    DumpObject(b);
                } else {
                    // The last reference: no outer, a count of one (written by the test), then Release.
                    Slot(Face(2), 4, static_cast<void *>(nullptr));
                    *reinterpret_cast<uint32_t *>(At(gObject, 0x30)) = 1;
                    svcfx::gEv.clear();
                    // The pixel block it holds (its own or external) goes back to the heap with it. (Blocks it let go of
                    // without freeing, as the original does, stay with the heap: the test leaks them rather than guess
                    // which of the heap's blocks are still theirs.) Whether the pixel block was freed, of any size, and
                    // what the Release left allocated minus freed (blocks, bytes) on this thread: exact records.
                    void *pixels = reinterpret_cast<void *>(uintptr_t(Word(gObject, 0x40)));
                    const uint32_t face = Draw(gFaces);
                    difftest::AllocationCounter net;
                    difftest::FreeRecorder frees;
                    const uint32_t released = Slot(Face(face), 2);
                    frees.Stop();
                    net.Stop();
                    const uint32_t busy = !pixels ? 0 : frees.Freed(pixels) ? 0 : 1;
                    b.push_back(released);
                    b.push_back(pixels != nullptr);
                    b.push_back(busy);
                    b.push_back(frees.Overflowed() || net.Overflowed());
                    b.push_back(uint32_t(net.Blocks()));
                    b.push_back(uint32_t(net.Bytes()));
                    DumpPool(b);
                }
                Finish(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        bool stop = false;
        for (size_t i = 0; i < results[0].size() || i < results[1].size(); ++i) {
            const Blob empty;
            const Blob &x = i < results[0].size() ? results[0][i] : empty;
            const Blob &y = i < results[1].size() ? results[1][i] : empty;
            if (!svcfx::Same(ctx, x, y, "GZBuffer_Run", seed)) {
                // Both sides' words of the failing step (also without the trace: a failure that does not repeat).
                for (size_t w = 0; w < x.size() || w < y.size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < x.size() ? x[w] : 0, w < y.size() ? y[w] : 0);
                const size_t op = i ? i - 1 : 0;
                ctx.Fail("  at step %u of %u (kind %u, %s)", unsigned(i), unsigned(ops.size() + 2),
                         i && op < ops.size() ? ops[op] : 99u, transient ? "transient" : "buffer");
                stop = true;
                break;
            }
        }
        if (stop) break;
    }
}
