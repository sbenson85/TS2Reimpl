// Differential tests for the Direct3D resource classes as whole classes (lane NJ, rendering R3): the surfaces
// (src/gz/cSurfaceDX.h: cImageSurfaceDX, cDepthStencilSurfaceDX, cRenderTargetDX, cLockableRenderTargetDX,
// cMipSurfaceDX) and the textures (src/gz/cTextureDX.h: cTextureDX, cCubeTextureDX).
//
// Each case makes objects with the class's creator (the original's at its RVA, ours through New()), then runs a seeded
// sequence of slot calls through each object's vtable (the same calls on both sides: RawSlot on the primary or the +4
// face), and finally releases them. Our side reaches every slot through our vtables (the standalone shape's slots
// test); the RVAs this file verifies:
//   creators 0x161d40 0x161ce0 0x161e00 0x161da0 0x163600 0x1648a0;
//   surfaces: QI 0x162a90 0x162a00 0x162bb0 0x162b20, Release 0x162c60 0x162c40, 0x162d20, 0x1624f0, 0x162f50,
//   0x162f10, 0x162f00 (slot 19 of the lockable render target, folded with cLinearFogLight: our override, SurfaceOp's
//   ops 6 / 7), 0x163060, 0x162d10, 0x1630d0, 0x160ec0, 0x162500, 0x1628d0, 0x161370, 0x1613f0, 0x161120, 0x1612f0,
//   0x1630e0, 0x161ea0, 0x161ca0, 0x163540, 0x162870, 0x1628a0, 0x162590, 0x162750, 0x162ff0, 0x162000, 0x1621a0,
//   0x161eb0, 0x1622f0, Init 0x162440, SetSurface 0x162c80, 0x163ff0, the destructors (0x160c80, 0x160d40, 0x160bc0,
//   0x160e00, 0x1632f0, 0x160b20, and the bases' 0x1609a0, 0x160a00, 0x160a60, 0x160ac0), the mip constructor
//   0x163190 / 0x160900 and operator new 0x1631c0; the +4 face's thunks 0x162b14, 0x162a7d, 0x162ba4, 0x162c2d;
//   textures: QI 0x1641b0 0x165410, Release 0x164270, 0x1643c0 0x1655f0, 0x164480 0x1656d0, 0x164440 0x165690,
//   0x164530 0x165780, 0x1634b0 0x1647c0, 0x164070 0x1652d0, 0x1642a0 0x1654c0, 0x164570 0x1657b0, 0x1636c0 0x164960,
//   0x1635a0 0x164840, 0x164140 0x1653b0, 0x163700 0x1649a0, 0x163d30 0x164fc0, 0x163a70 0x164d10,
//   0x164130 0x165390, the destructors 0x1633c0 0x1646d0, the face's 0x163500 0x164800 0x164010 0x165270 and its
//   thunks (0x164251 0x16442a 0x164527 0x164476 0x164556 0x1634e2 0x16411e 0x1643b5 0x1645a6 0x1636f6 0x1635f5
//   0x164189; 0x1654b1 0x16567c 0x165775 0x1656c6 0x1657a6 0x1647f2 0x165381 0x1655da 0x1657e6 0x164996
//   0x164895 0x1653f9).
//
// The world (built alike for each side from the case's seed):
// - The engine side: the device (cIDeviceDX: readiness, scene state, its resource manager, device enum and Direct3D
//   device), the manager (Add / Remove / RefreshAvailableMemory / AddPending), the device enum (display-format support,
//   CanStretchRect, the caps, the P8-substitute face), three pixel formats and the formats manager the getter 0x18acd0
//   hands out (stood in on both sides: shape::CodePatch) are drawfx fakes.
// - Direct3D: the device's Create* slots by d3dfx::Script (results per call, a failure can still hand out an object);
//   surfaces and textures are d3dfx::Level / d3dfx::Resource fakes whose GetDesc / GetLevelDesc / LockRect write their
//   out-parameters whatever they answer (the original reads them regardless).
// - The pools: each class's game pool (the fixed allocators at 0x10ca058 ... 0xfec898) is set up fresh for each side
//   on one fixed region, so both sides' objects sit at the same addresses (their pointers compare as they are).
// - The surface copies 0x1614b0 / 0x162d60 and the clip 0x18ce70 are game code on both sides (ours calls them through
//   OriginalFunction); they reach our objects only through the faces' slots.
// Compared: the log (every call on every fake with its arguments, LogD3DError lines), each call's result, the objects'
// words after +4 (vtables excluded: each side has its own) after every call, the lock records, every fake's reference
// count, every pixel buffer and the pools' state after the objects are released.
// Uninitialised stack the original reads: LockRect's empty case and Unlock write a 16-byte local nobody set into the
// lock record's rectangle (0x162590, 0x162ff0: movups from [esp+4] never written); ours writes zeros, and the stack
// below the call is zeroed first (ZeroStackBelow), so both compare.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "gz/GraphicServices.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cSurfaceDX.h"
#include "gz/cTextureDX.h"
#include "rz/RZFixedAllocator.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

// ---- engine-side slot tables ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, drawfx::Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kDeviceSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetDeviceEnum", ""},       // 15
    {0, drawfx::Ret::Int, true, "GetDeviceEnumConst", ""},  // 16
    U,
    {0, drawfx::Ret::Int, true, "GetResourceManager", ""},  // 18
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "IsDeviceReady", ""},       // 35
    {0, drawfx::Ret::Int, true, "InScene", ""},             // 36
    U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetDeviceEnum45", ""},     // 45
    {0, drawfx::Ret::Int, true, "WaitForGpu", ""},          // 46
    U,
    {0, drawfx::Ret::Int, true, "GetD3DDevice", ""},        // 48
};
inline constexpr drawfx::Iface kDevice{"cIDeviceDX", 49, kDeviceSlots};

inline constexpr drawfx::Slot kManagerSlots[] = {
    kQI, kAddRef, kRelease, U, U, U,
    {1, drawfx::Ret::Int, true, "Add", "i"},
    U,
    {1, drawfx::Ret::Int, true, "Remove", "i"},
    U, U, U, U,
    {0, drawfx::Ret::Int, true, "RefreshAvailableMemory", ""},
    {1, drawfx::Ret::Int, true, "AddPending", "u"},
};
inline constexpr drawfx::Iface kManager{"cIDeviceResourceManager", 15, kManagerSlots};

inline constexpr drawfx::Slot kEnumSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U,
    {2, drawfx::Ret::Int, true, "IsFormatSupportedOnDisplay", "iu"},
    U, U, U, U, U, U, U,
    {8, drawfx::Ret::Int, true, "CanStretchRect", "upriupri"},
    U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetCaps", ""},
};
inline constexpr drawfx::Iface kEnum{"cIDeviceEnum", 25, kEnumSlots};

inline constexpr drawfx::Slot kEnumFormatsSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "Slot28", "u"},
};
inline constexpr drawfx::Iface kEnumFormats{"cIDeviceEnum.0x6c02c311", 29, kEnumFormatsSlots};

inline constexpr drawfx::Slot kFormatSlots[] = {
    kQI, kAddRef, kRelease,
    {0, drawfx::Ret::Int, true, "GetFormatId", ""},
    U, U,
    {0, drawfx::Ret::Int, true, "GetBitsPerPixel", ""},
    {0, drawfx::Ret::Int, true, "GetBlockHeight", ""},
    U,
    {1, drawfx::Ret::Int, true, "GetRowBytes", "u"},
};
inline constexpr drawfx::Iface kFormat{"cIPixelFormat", 10, kFormatSlots};

inline constexpr drawfx::Slot kFormatsSlots[] = {
    kQI, kAddRef, kRelease, U,
    {1, drawfx::Ret::Int, true, "GetFormat", "u"},
};
inline constexpr drawfx::Iface kFormats{"cIPixelFormatsManager", 5, kFormatsSlots};

// ---- the world ----
constexpr unsigned kLevels = 64, kResources = 4, kFormatCount = 3;
constexpr uint32_t kIidDeviceDX = 0x553b7cf, kIidEnumFormats = 0x6c02c311;
constexpr uint32_t kQuiet[] = {0x8876017c, 0x8007000e, 0x88760827};

struct Args {
    cSurfaceLock lock[2];
    RECT rect[2];
    POINT point;
    void *out[2];
};
struct FormatRec {
    uint32_t id, bits, blockHeight;
};
struct World {
    d3dfx::Level level[kLevels];
    d3dfx::Resource res[kResources];
    FormatRec format[kFormatCount];
    uint32_t ready, inScene, displaySupport, canStretch, substitute;
    uint8_t caps[0x130];
    Args args;
} gWorld;

drawfx::Fake gDevice, gManager, gEnum, gEnumFormats, gFormat[kFormatCount], gFormats, gD3D, gD3DSurf[kLevels],
    gD3DTex[kResources];
d3dfx::Script *gScript = nullptr;

uint32_t W(const void *p) { return drawfx::P(p); }
// A note whose words are filled in afterwards (gLog.back().words).
void NoteWords(const char *label, std::vector<uint32_t> words)
{
    drawfx::Call c;
    c.note = label;
    c.words = std::move(words);
    drawfx::gLog.push_back(std::move(c));
}
uint32_t N(uint32_t w) { return drawfx::Norm(w); }

uint32_t FormatId(drawfx::Fake &f, const uint32_t *) { return gWorld.format[f.value[0]].id; }
uint32_t FormatBits(drawfx::Fake &f, const uint32_t *) { return gWorld.format[f.value[0]].bits; }
uint32_t FormatBlock(drawfx::Fake &f, const uint32_t *) { return gWorld.format[f.value[0]].blockHeight; }
uint32_t FormatRow(drawfx::Fake &f, const uint32_t *a) { return (a[0] * gWorld.format[f.value[0]].bits + 7) / 8; }
uint32_t FormatsGet(drawfx::Fake &, const uint32_t *a) { return W(&gFormat[a[0] % kFormatCount]); }
void *__cdecl StandFormats() { return &gFormats; }

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }

// `wide`: a width past 16 bits (the u16 width field) is possible; only for texture levels, which the test never copies
// (the copies' row loops would run past the pixel buffer).
void SeedLevel(std::mt19937 &g, d3dfx::Level &l, bool wide)
{
    l = d3dfx::Level{};
    l.desc.Format = D3DFORMAT(Pick(g, {21, 22, 41, 28, 50}));
    l.desc.Type = D3DRTYPE_SURFACE;
    l.desc.Usage = Pick(g, {0, 0, 1, 2, 0x200});
    l.desc.Pool = D3DPOOL(Pick(g, {0, 0, 1, 2, 3}));
    l.desc.MultiSampleType = D3DMULTISAMPLE_TYPE(g() % 4 == 0 ? Pick(g, {1, 2, 4}) : 0);
    l.desc.MultiSampleQuality = g() % 4 == 0 ? 1 : 0;
    l.desc.Width = wide && g() % 16 == 0 ? 0x10004 : Pick(g, {1, 2, 4, 8, 16, 33, 64});
    l.desc.Height = Pick(g, {1, 2, 4, 8, 16, 40, 64});
    l.descHr = g() % 8 == 0 ? d3dfx::kResults[2 + g() % 2] : 0;
    l.lockHr = g() % 8 == 0 ? d3dfx::kResults[2 + g() % 2] : 0;
    l.unlockHr = g() % 8 == 0 ? d3dfx::kResults[2 + g() % 2] : 0;
    l.pitch = int32_t(Pick(g, {16, 32, 64}));
    l.priority = g() % 4;
    l.device = &gD3D;
    for (uint8_t &b : l.bits) b = uint8_t(g());
}

void Seed(uint32_t seed)
{
    std::mt19937 g(seed);
    World &w = gWorld;
    std::memset(&w.args, 0, sizeof(w.args));
    for (unsigned i = 0; i < kLevels; ++i) SeedLevel(g, w.level[i], i >= 16);
    for (unsigned r = 0; r < kResources; ++r) {
        d3dfx::Resource &res = w.res[r];
        res = d3dfx::Resource{};
        res.cube = r >= 2;
        res.levels = Pick(g, {0, 1, 2, 3, 3, 16, 17});
        res.lod = Pick(g, {0, 0, 1, 2, 5});
        res.surfaceHr = g() % 8 == 0 ? d3dfx::kResults[2] : 0;
        for (unsigned f = 0; f < 6; ++f)
            for (unsigned l = 0; l < d3dfx::Resource::kMaxLevels; ++l) {
                const unsigned i = 16 + (r * 7 + f * 5 + l) % 48;
                res.surface[f][l] = g() % 16 == 0 ? nullptr : &gD3DSurf[i];
                res.level[f][l] = &w.level[i];
            }
    }
    for (FormatRec &f : w.format) {
        f.id = Pick(g, {21, 22, 28, 41, 50});
        f.bits = Pick(g, {4, 8, 16, 32});
        f.blockHeight = Pick(g, {1, 1, 4});
    }
    w.ready = g() % 10 != 0;
    w.inScene = g() % 2;
    w.displaySupport = g() % 2;
    w.canStretch = g() % 3 != 0;
    w.substitute = Pick(g, {0, 21, 50});
    std::memset(w.caps, 0, sizeof(w.caps));
    const uint32_t filterCaps = Pick(g, {0, 0x1000100, 0x2000200, 0x3000300, 0x100});
    std::memcpy(w.caps + 0xf4, &filterCaps, 4);
    w.caps[0xd4] = uint8_t(g() % 2 ? 0x10 : 0);
    for (RECT &r : w.args.rect) {
        r.left = LONG(int32_t(g() % 24) - 4);
        r.top = LONG(int32_t(g() % 24) - 4);
        r.right = r.left + LONG(g() % 6 == 0 ? -1 : int32_t(g() % 40));
        r.bottom = r.top + LONG(g() % 6 == 0 ? 0 : int32_t(g() % 40));
    }
    w.args.point = POINT{LONG(int32_t(g() % 20) - 4), LONG(int32_t(g() % 20) - 4)};
}

// The Create* script: per slot, results and objects.
void Script(uint32_t seed, d3dfx::Script &s)
{
    std::mt19937 g(seed ^ 0x5eed);
    unsigned next = 0;
    for (unsigned slot : {fx_d3d9::dev::CreateOffscreenPlainSurface, fx_d3d9::dev::CreateRenderTarget,
                          fx_d3d9::dev::CreateDepthStencilSurface}) {
        for (unsigned i = 0; i < 4; ++i) {
            const uint32_t hr = g() % 4 == 0 ? Pick(g, {1, 0x8876086c, 0x80004005}) : 0;
            s.Add(slot, hr, int32_t(hr) >= 0 ? &gD3DSurf[next++ % 16] : nullptr);
        }
    }
    for (unsigned slot : {fx_d3d9::dev::CreateTexture, fx_d3d9::dev::CreateCubeTexture}) {
        const unsigned base = slot == fx_d3d9::dev::CreateTexture ? 0 : 2;
        for (unsigned i = 0; i < 4; ++i) {
            uint32_t hr = 0;
            if (g() % 4 == 0) hr = g() % 2 ? kQuiet[g() % 3] : 0x80004005;
            bool quiet = false;
            for (uint32_t q : kQuiet) quiet = quiet || q == hr;
            s.Add(slot, hr, quiet ? nullptr : &gD3DTex[base + g() % 2]);
        }
    }
}

void Build(uint32_t seed)
{
    gDevice.Reset<kDevice>(0x6d001);
    gDevice.AddFace(kIidDeviceDX, gDevice);
    gDevice.Returns(15, &gEnum);
    gDevice.Returns(16, &gEnum);
    gDevice.Returns(45, &gEnum);
    gDevice.Returns(18, &gManager);
    gDevice.Returns(35, gWorld.ready);
    gDevice.Returns(36, gWorld.inScene);
    gDevice.Returns(48, &gD3D);
    gManager.Reset<kManager>(0x6d002);
    gEnum.Reset<kEnum>(0x6d003);
    gEnum.Returns(11, gWorld.displaySupport);
    gEnum.Returns(19, gWorld.canStretch);
    gEnum.Returns(24, gWorld.caps);
    gEnum.AddFace(kIidEnumFormats, gEnumFormats);
    gEnumFormats.Reset<kEnumFormats>(0x6d004);
    gEnumFormats.Returns(28, gWorld.substitute);
    for (unsigned i = 0; i < kFormatCount; ++i) {
        gFormat[i].Reset<kFormat>(0x6f000 + i);
        gFormat[i].value[0] = i;
        gFormat[i].On(3, &FormatId);
        gFormat[i].On(6, &FormatBits);
        gFormat[i].On(7, &FormatBlock);
        gFormat[i].On(9, &FormatRow);
    }
    gFormats.Reset<kFormats>(0x6e000);
    gFormats.On(4, &FormatsGet);
    gD3D.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
    for (unsigned i = 0; i < kLevels; ++i) d3dfx::MakeLevel(gD3DSurf[i], 0xd3d50000 + i, gWorld.level[i]);
    for (unsigned r = 0; r < kResources; ++r) d3dfx::MakeResource(gD3DTex[r], 0xd3d60000 + r, gWorld.res[r]);
    static d3dfx::Script *script = nullptr;
    delete script;
    script = new d3dfx::Script(gD3D);
    gScript = script;
    Script(seed, *script);
    drawfx::AddRange(&gWorld.args, sizeof(gWorld.args), 0x7a000000);
    drawfx::AddRange(gWorld.caps, sizeof(gWorld.caps), 0x7b000000);
}

// ---- the pools ----
struct Pool {
    uint32_t rva, pageBytes, blockBytes;
    uint8_t *region = nullptr;
    unsigned next = 0;
    uint32_t saved[3] = {};
    void *vt[4] = {};
    std::vector<uint32_t> freed;
};
constexpr unsigned kPoolPages = 8;
Pool gPools[] = {
    {0x10ca058, 0x800, 0x1c}, {0x10ca064, 0x800, 0x18}, {0x10ca070, 0x800, 0x18}, {0x10ca07c, 0x800, 0x1c},
    {0xfec63c, 0x1000, 0x20}, {0xfec630, 0x2000, 0x5c}, {0xfec898, 0x8000, 0x19c},
};
Pool *PoolOf(void *self)
{
    for (Pool &p : gPools)
        if (GameAddress<void>(p.rva) == self) return &p;
    return nullptr;
}
RZFixedPage *InitPage(Pool &p)
{
    auto *page = reinterpret_cast<RZFixedPage *>(p.region + (p.next++ % kPoolPages) * p.pageBytes);
    page->next = page;
    page->prev = page;
    page->used = 0;
    uint8_t *base = reinterpret_cast<uint8_t *>(page);
    const uint32_t blocks = (p.pageBytes - 0x10) / p.blockBytes;
    for (uint32_t k = 0; k < blocks; ++k)
        *reinterpret_cast<void **>(base + 0x10 + k * p.blockBytes) =
            k + 1 < blocks ? base + 0x10 + (k + 1) * p.blockBytes : nullptr;
    page->freeList = base + 0x10;
    return page;
}
RZFixedPage *__fastcall FakeNewPage(void *self, void *) { return InitPage(*PoolOf(self)); }
void __fastcall FakeFreePage(void *self, void *, RZFixedPage *page)
{
    Pool &p = *PoolOf(self);
    p.freed.push_back(uint32_t(reinterpret_cast<uint8_t *>(page) - p.region));
}
// Slots 0 / 1 (Alloc, Free) are called directly by both sides, never through the vtable: a call here is an error.
void *__fastcall PoolAllocTrap(void *, void *)
{
    drawfx::gErrors.push_back("a pool's vtable slot 0 was called");
    return nullptr;
}
void __fastcall PoolFreeTrap(void *, void *, void *)
{
    drawfx::gErrors.push_back("a pool's vtable slot 1 was called");
}
struct Pools {
    Pools()
    {
        for (Pool &p : gPools) {
            p.vt[0] = reinterpret_cast<void *>(&PoolAllocTrap);
            p.vt[1] = reinterpret_cast<void *>(&PoolFreeTrap);
            p.region = static_cast<uint8_t *>(
                VirtualAlloc(nullptr, kPoolPages * p.pageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            p.vt[2] = reinterpret_cast<void *>(&FakeNewPage);
            p.vt[3] = reinterpret_cast<void *>(&FakeFreePage);
            std::memcpy(p.saved, GameAddress<void>(p.rva), sizeof(p.saved));
        }
    }
    ~Pools()
    {
        for (Pool &p : gPools) {
            std::memcpy(GameAddress<void>(p.rva), p.saved, sizeof(p.saved));
            VirtualFree(p.region, 0, MEM_RELEASE);
        }
    }
    // A fresh pool (one page) at the same addresses for each side.
    static void Reset()
    {
        for (Pool &p : gPools) {
            p.next = 0;
            p.freed.clear();
            std::memset(p.region, 0xcd, kPoolPages * p.pageBytes);
            uint32_t *words = GameAddress<uint32_t>(p.rva);
            words[0] = W(p.vt);
            words[1] = W(InitPage(p));
            words[2] = 0;
        }
    }
    // Each pool's rings and use counts after a side.
    static void Note()
    {
        for (Pool &p : gPools) {
            const uint32_t *words = GameAddress<uint32_t>(p.rva);
            std::vector<uint32_t> w{words[1], words[2], p.next};
            for (unsigned i = 0; i < p.next && i < kPoolPages; ++i) {
                const auto *page = reinterpret_cast<const RZFixedPage *>(p.region + i * p.pageBytes);
                w.insert(w.end(), {W(page->next), W(page->prev), W(page->freeList), page->used});
            }
            w.insert(w.end(), p.freed.begin(), p.freed.end());
            NoteWords("pool", {});
            drawfx::gLog.back().words = std::move(w);
        }
    }
};

// ---- objects under test ----
enum Kind { kImage, kDepth, kTarget, kLockableTarget, kMip, kTexture, kCube };
constexpr uint32_t kCreator[] = {0x161d40, 0x161ce0, 0x161e00, 0x161da0, 0, 0x163600, 0x1648a0};
constexpr uint32_t kObjectSize[] = {0x1c, 0x18, 0x18, 0x1c, 0x20, 0x5c, 0x19c};

void *NewObject(difftest::Context &ctx, Kind k, bool original)
{
    if (original) return ctx.Original<void *(__cdecl *)()>(kCreator[k])();
    switch (k) {
    case kImage: return cImageSurfaceDX::New();
    case kDepth: return cDepthStencilSurfaceDX::New();
    case kTarget: return cRenderTargetDX::New();
    case kLockableTarget: return cLockableRenderTargetDX::New();
    case kTexture: return cTextureDX::New();
    case kCube: return cCubeTextureDX::New();
    default: return nullptr;
    }
}

// No /GS cookie (safebuffers): its value would stay in the zeroed words the original's frame reads.
__declspec(noinline, safebuffers) void ZeroStackBelow()
{
    volatile uint8_t pad[4096];
    for (size_t i = 0; i < sizeof(pad); ++i) pad[i] = 0;
}

void NoteObject(const char *label, const void *p, Kind k)
{
    std::vector<uint32_t> w;
    const uint32_t *words = static_cast<const uint32_t *>(p);
    for (uint32_t off = 8; off < kObjectSize[k]; off += 4) w.push_back(N(words[off / 4]));
    NoteWords(label, {});
    drawfx::gLog.back().words = std::move(w);
}
void NoteLocks()
{
    std::vector<uint32_t> w;
    for (const cSurfaceLock &l : gWorld.args.lock) {
        const uint32_t *words = reinterpret_cast<const uint32_t *>(&l);
        for (unsigned i = 0; i < 9; ++i) w.push_back(N(words[i]));
    }
    NoteWords("locks", {});
    drawfx::gLog.back().words = std::move(w);
}
void NoteRefs()
{
    std::vector<uint32_t> refs;
    for (drawfx::Fake *f : drawfx::gFakes) refs.push_back(uint32_t(f->refs));
    NoteWords("refs", {});
    drawfx::gLog.back().words = std::move(refs);
}
void NoteResult(uint32_t r) { drawfx::Note("result", r); }

struct Seen {
    unsigned creates = 0, failedCreates = 0, locks = 0, copies = 0, mips = 0, reports = 0;
};

bool IsLockable(Kind k) { return k == kImage || k == kLockableTarget || k == kMip; }

// IIDs asked of the objects (answered or not).
constexpr uint32_t kIids[] = {1, 0x973e60f, 0x9b71bf9d, 0xda02e09f, 0x5b71b5ce, 0x1b75b70b, 0xdb75b6e9, 0xfb75b72b,
                              0xbb75b749, 0x4dee64cf, 0x4a5a7223, 0xcb60e61d, 0x694312a3, 0x5b891e1e, 0xe9443438,
                              0x1b891e38, 0x998dbd2, 0x8b54da90, 0xab54dab1, 0x12345678};
bool Counted(uint32_t iid) { return iid != 0x4a5a7223 && iid != 0xcb60e61d && iid != 0x998dbd2 && iid != 0x8b54da90 && iid != 0xab54dab1; }

// One QueryInterface (null out sometimes, except for the iid the textures answer before the null check).
void Query(std::mt19937 &g, void *obj)
{
    const uint32_t iid = kIids[g() % std::size(kIids)];
    void **out = (g() % 8 == 0 && iid != 0x998dbd2) ? nullptr : &gWorld.args.out[0];
    gWorld.args.out[0] = nullptr;
    const bool r = RawSlot<0, bool>(obj, iid, out);
    drawfx::Note("QueryInterface", {iid, r, N(W(gWorld.args.out[0]))});
    if (r && out && Counted(iid) && gWorld.args.out[0]) RawSlot<2>(gWorld.args.out[0]);
}

// A surface's slot calls. `created`: the object holds a Direct3D surface; `other`: a second created surface or null.
void SurfaceOp(std::mt19937 &g, void *s, Kind k, bool &created, void *other, Seen &seen)
{
    const unsigned op = g() % 26;
    const bool need = op >= 8 && op != 19 && op != 20 && op != 21 && op != 25;
    if (need && !created) return;
    Args &a = gWorld.args;
    switch (op) {
    case 0: Query(g, s); break;
    case 1: RawSlot<3, void>(s); created = (k == kMip) && created; break;
    case 2: NoteResult(N(RawSlot<4>(s))); NoteResult(N(RawSlot<5>(s))); break;
    case 3: NoteResult(RawSlot<6, bool>(s)); break;
    case 4: case 5: NoteResult(RawSlot<7>(s)); break;
    case 6: case 7: NoteResult(RawSlot<19>(s)); break;
    case 8: NoteResult(RawSlot<8, bool>(s)); break;
    case 9: NoteResult(RawSlot<9, bool>(s)); break;
    case 10: NoteResult(RawSlot<10>(s, uint32_t(g() % 5))); break;
    case 11: RawSlot<11, void>(s); break;
    case 12: NoteResult(RawSlot<12>(s)); break;
    case 13: NoteResult(RawSlot<13>(s)); break;
    case 14: NoteResult(RawSlot<14>(s)); break;
    case 15:
        if (!other) break;
        ++seen.copies;
        // The copy helper 0x1614b0 reads the point and the rectangle unchecked: callers always pass both.
        NoteResult(RawSlot<15, bool>(s, &a.point, other, &a.rect[0]));
        break;
    case 16: if (other) ++seen.copies, NoteResult(RawSlot<16, bool>(s, other)); break;
    case 17: if (other) ++seen.copies, NoteResult(RawSlot<17, bool>(s, &a.rect[1], other, &a.rect[0])); break;
    case 18: if (other) ++seen.copies, NoteResult(RawSlot<18, bool>(s, other)); break;
    case 19: case 20: NoteResult(RawSlot<20>(s)); NoteResult(RawSlot<21>(s)); break;
    case 21: break;
    case 22: NoteResult(N(RawSlot<22>(s))); break;
    case 23: NoteResult(RawSlot<23>(s)); NoteResult(RawSlot<24>(s)); break;
    case 24: {
        // The +4 face: 3 / 4 the primary, 6 the device, 7 the surface.
        void *face = nullptr;
        RawSlot<0, bool>(s, kIidSurfaceDX, &face);
        NoteResult(N(RawSlot<3>(face)) == N(W(s)));
        NoteResult(N(RawSlot<4>(face)) == N(W(s)));
        NoteResult(N(RawSlot<6>(face)));
        NoteResult(N(RawSlot<7>(face)));
        Query(g, face); // the QueryInterface thunk
        RawSlot<2>(face);
        break;
    }
    case 25:
        if (!IsLockable(k)) break;
        if (!created) break;
        {
            cSurfaceLock *lock = &a.lock[g() % 2];
            const unsigned which = g() % 3;
            ++seen.locks;
            if (which == 0) {
                const int32_t x = int32_t(g() % 24) - 6, y = int32_t(g() % 24) - 6;
                const int32_t w = int32_t(g() % 40) - 4, h = int32_t(g() % 40) - 4;
                const bool ro = g() % 2;
                ZeroStackBelow();
                NoteResult(RawSlot<25>(s, lock, x, y, w, h, ro));
            } else if (which == 1) {
                NoteResult(RawSlot<26, uint32_t>(s, lock, bool(g() % 2))); // eax: the pitch
            } else {
                ZeroStackBelow();
                RawSlot<27, void>(s, lock);
            }
            NoteResult(RawSlot<28>(s));
        }
        break;
    }
}

void Release(void *p) { RawSlot<2>(p); }

// Each pixel buffer and the levels' lock counts.
struct After {
    drawfx::Log log;
    std::vector<std::string> errors;
    std::vector<uint8_t> pixels;
};

template <class Run> void Case(difftest::Context &ctx, const char *what, uint32_t seed, Seen &seen, Run run)
{
    auto side = [&](bool original) {
        After a;
        Seed(seed);
        Pools::Reset();
        auto [log, errors] = drawfx::Side([&] { Build(seed); }, [&] {
            drawfx::gLog.reserve(256);
            run(original);
            NoteRefs();
            Pools::Note();
        });
        a.log = std::move(log);
        a.errors = std::move(errors);
        for (const d3dfx::Level &l : gWorld.level) {
            a.pixels.insert(a.pixels.end(), l.bits, l.bits + 64);
            a.pixels.push_back(uint8_t(l.locks));
            a.pixels.push_back(uint8_t(l.priority));
        }
        return a;
    };
    const After a = side(true);
    const After b = side(false);
    for (const drawfx::Call &c : a.log) {
        if (!c.iface && std::strcmp(c.note, "LogD3DError") == 0) ++seen.reports;
        if (!c.iface && std::strcmp(c.note, "created") == 0) (c.words[0] ? seen.creates : seen.failedCreates)++;
    }
    std::vector<std::string> errors = a.errors;
    errors.insert(errors.end(), b.errors.begin(), b.errors.end());
    if (!drawfx::Same(ctx, a.log, b.log, what, seed, errors)) return;
    if (a.pixels != b.pixels) ctx.Fail("%s seed %u: pixel buffers or lock counts differ", what, seed);
}

struct Fixture {
    Pools pools;
    d3dfx::ErrorLog errors;
    shape::StandIn formats{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &StandFormats};
    explicit Fixture(difftest::Context &ctx) : errors(ctx) {}
};

// A surface's Create with seeded arguments; notes and answers whether it made the Direct3D surface.
bool CreateSurface(std::mt19937 &g, void *s, Kind k)
{
    void *device = &gDevice;
    void *format = &gFormat[g() % kFormatCount];
    const uint32_t w = Pick(g, {1, 4, 8, 64}), h = Pick(g, {1, 4, 8, 64});
    bool r = false;
    switch (k) {
    case kImage: r = RawSlot<29, bool>(s, device, w, h, format, uint32_t(g() % 6)); break;
    case kLockableTarget: r = RawSlot<29, bool>(s, device, w, h, format); break;
    default: r = RawSlot<25, bool>(s, device, w, h, format, uint32_t(g() % 3), uint32_t(g() % 2)); break;
    }
    drawfx::Note("created", r);
    return r;
}

} // namespace

DIFF_TEST(gz_dx_Surfaces, 0x161d40)
{
    Fixture fx(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        Case(ctx, "Surfaces", seed, seen, [&](bool original) {
            std::mt19937 g(seed * 31 + 7);
            const Kind ka = Kind(g() % 4), kb = Kind(g() % 4);
            void *a = NewObject(ctx, ka, original);
            void *b = NewObject(ctx, kb, original);
            NoteObject("new a", a, ka);
            NoteObject("new b", b, kb);
            RawSlot<1>(a);
            RawSlot<1>(b);
            bool ca = CreateSurface(g, a, ka), cb = CreateSurface(g, b, kb);
            NoteObject("a", a, ka);
            NoteObject("b", b, kb);
            for (unsigned step = 0; step < 14; ++step) {
                const bool onA = g() % 3 != 0;
                void *s = onA ? a : b;
                const Kind k = onA ? ka : kb;
                bool &created = onA ? ca : cb;
                if (g() % 12 == 0) {
                    created = CreateSurface(g, s, k) || created; // a failed create keeps the old surface
                } else {
                    void *other = (onA ? cb : ca) && created ? (onA ? b : a) : nullptr;
                    SurfaceOp(g, s, k, created, other, seen);
                }
                NoteObject("a", a, ka);
                NoteObject("b", b, kb);
                NoteLocks();
            }
            // Locks still held keep a count on their surface: let them go first.
            for (cSurfaceLock &l : gWorld.args.lock)
                if (l.surface) {
                    RawSlot<2>(l.surface);
                    l.surface = nullptr;
                }
            NoteResult(RawSlot<2>(a));
            NoteResult(RawSlot<2>(b));
        });
    }
    if (!seen.creates || !seen.failedCreates || !seen.locks || !seen.copies || !seen.reports)
        ctx.Fail("Surfaces: a path never ran (creates %u, failed %u, locks %u, copies %u, reports %u)", seen.creates,
                 seen.failedCreates, seen.locks, seen.copies, seen.reports);
}

namespace {

float PickQuality(std::mt19937 &g)
{
    switch (g() % 5) {
    case 0: return 0.0f;
    case 1: return 0.5f;
    case 2: return 1.0f;
    case 3: return std::numeric_limits<float>::quiet_NaN();
    default: return 2.0f;
    }
}

bool CreateTexture(std::mt19937 &g, void *t, Kind k)
{
    void *device = &gDevice;
    void *format = &gFormat[g() % kFormatCount];
    const uint32_t w = Pick(g, {1, 4, 8, 64}), h = Pick(g, {1, 4, 8, 64}), levels = g() % 5;
    const unsigned which = g() % 3;
    bool r = false;
    if (k == kTexture) {
        if (which == 0) r = RawSlot<22, bool>(t, device, w, h, format, levels, PickQuality(g), uint32_t(g() % 4));
        else if (which == 1) r = RawSlot<23, bool>(t, device, w, h, format, levels);
        else r = RawSlot<24, bool>(t, device, w, h, format, levels);
    } else {
        if (which == 0) r = RawSlot<22, bool>(t, device, w, format, levels, PickQuality(g), uint32_t(g() % 4));
        else if (which == 1) r = RawSlot<23, bool>(t, device, w, format, levels);
        else r = RawSlot<24, bool>(t, device, w, format, levels);
    }
    drawfx::Note("created", r);
    return r;
}

void TextureOp(std::mt19937 &g, void *t, Kind k, bool &created, Seen &seen)
{
    const unsigned op = g() % 24;
    const bool need = op >= 7 && op <= 17;
    if (need && !created) return;
    switch (op) {
    case 0: Query(g, t); break;
    case 1: RawSlot<3, void>(t); created = false; break;
    case 2: NoteResult(N(RawSlot<4>(t))); NoteResult(N(RawSlot<5>(t))); break;
    case 3: NoteResult(RawSlot<6, bool>(t)); break;
    case 4: NoteResult(RawSlot<7>(t)); break;
    case 5: NoteResult(RawSlot<16>(t)); break;
    case 6: drawfx::NoteFloat("quality", RawSlot<21, float>(t)); break;
    case 7: NoteResult(RawSlot<8, bool>(t)); break;
    case 8: NoteResult(RawSlot<9, bool>(t)); break;
    case 9: NoteResult(RawSlot<10>(t, uint32_t(g() % 5))); RawSlot<11, void>(t); NoteResult(RawSlot<14>(t)); break;
    case 10: NoteResult(RawSlot<12>(t)); break;
    case 11: NoteResult(RawSlot<13>(t)); break;
    case 12: RawSlot<15, void>(t, PickQuality(g)); break;
    case 13: {
        const uint32_t level = g() % 4;
        NoteResult(RawSlot<17>(t, level));
        NoteResult(RawSlot<18>(t, level));
        break;
    }
    case 14: NoteResult(N(RawSlot<19>(t))); break;
    case 15: NoteResult(RawSlot<20>(t)); break;
    case 16: {
        // The +4 face: 22 the device, 23 the texture, 24 the format, 25 a level's bytes, 7 / 16 through the thunks.
        void *face = nullptr;
        RawSlot<0, bool>(t, kIidTextureBaseDX, &face);
        NoteResult(N(RawSlot<22>(face)));
        NoteResult(N(RawSlot<23>(face)));
        NoteResult(RawSlot<24>(face));
        NoteResult(RawSlot<25>(face, uint32_t(g() % 3)));
        NoteResult(RawSlot<7>(face));
        NoteResult(RawSlot<16>(face));
        // The face's other thunks (this -= 4 to the primary's bodies).
        Query(g, face);
        switch (g() % 9) {
        case 0: NoteResult(RawSlot<8, bool>(face)); NoteResult(RawSlot<9, bool>(face)); break;
        case 1: NoteResult(RawSlot<12>(face)); NoteResult(RawSlot<13>(face)); break;
        case 2: RawSlot<15, void>(face, PickQuality(g)); break;
        case 3: {
            const uint32_t level = g() % 4;
            NoteResult(RawSlot<17>(face, level));
            NoteResult(RawSlot<18>(face, level));
            break;
        }
        case 4: NoteResult(N(RawSlot<19>(face))); break;
        case 5: NoteResult(RawSlot<20>(face)); break;
        case 6: RawSlot<3, void>(face); created = false; break;
        default: break;
        }
        RawSlot<2>(face);
        break;
    }
    case 17: case 18: case 19: {
        // A level's surface, and a call on it.
        const uint32_t face = k == kCube ? g() % 6 : 0, level = g() % 4;
        void *mip = nullptr;
        switch (g() % 4) {
        case 0: mip = k == kCube ? RawSlot<25, void *>(t, face, level) : RawSlot<25, void *>(t, level); break;
        case 1: mip = k == kCube ? RawSlot<26, void *>(t, face, level) : RawSlot<26, void *>(t, level); break;
        case 2: mip = k == kCube ? RawSlot<27, void *>(t, face, level) : RawSlot<27, void *>(t, level); break;
        default: mip = k == kCube ? RawSlot<28, void *>(t, face, level) : RawSlot<28, void *>(t, level); break;
        }
        drawfx::Note("level", N(W(mip)));
        if (!mip) break;
        ++seen.mips;
        NoteObject("mip", mip, kMip);
        // A level holds a surface while the texture holds its Direct3D texture and the level is below the count.
        bool mipCreated = RawSlot<6, bool>(mip);
        SurfaceOp(g, mip, kMip, mipCreated, nullptr, seen);
        NoteObject("mip", mip, kMip);
        break;
    }
    default: {
        const bool r = CreateTexture(g, t, k);
        created = r || created;
        break;
    }
    }
}

} // namespace

DIFF_TEST(gz_dx_Textures, 0x163600)
{
    Fixture fx(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        Case(ctx, "Textures", seed, seen, [&](bool original) {
            std::mt19937 g(seed * 17 + 3);
            const Kind k = g() % 2 ? kCube : kTexture;
            void *t = NewObject(ctx, k, original);
            NoteObject("new", t, k);
            RawSlot<1>(t);
            bool created = CreateTexture(g, t, k);
            NoteObject("texture", t, k);
            for (unsigned step = 0; step < 12; ++step) {
                TextureOp(g, t, k, created, seen);
                NoteObject("texture", t, k);
                NoteLocks();
            }
            for (cSurfaceLock &l : gWorld.args.lock)
                if (l.surface) {
                    RawSlot<2>(l.surface);
                    l.surface = nullptr;
                }
            NoteResult(RawSlot<2>(t));
        });
    }
    if (!seen.creates || !seen.failedCreates || !seen.mips || !seen.reports || !seen.locks)
        ctx.Fail("Textures: a path never ran (creates %u, failed %u, levels %u, reports %u, locks %u)", seen.creates,
                 seen.failedCreates, seen.mips, seen.reports, seen.locks);
}

// ---- cLockableSurfaceDX<cIImageSurface>::QueryInterface ----
//
// gz_dx_LockableQueryInterface: 0x1628e0 and its +4 face thunk 0x16295d, an abstract base's slot no complete surface
// inherits, so the object is a small complete class of the test's that keeps it. Two objects alike: the original runs on
// one (its AddRef through our vtable), ours on the other through its own vtable, each face; compared: the result, the
// answer relative to the object, the count.
namespace {

class LockableProbe final : public nGZGraphic4::cLockableSurfaceDX<nGZGraphic4::cIImageSurface> {
public:
    explicit LockableProbe(void *surface) { mSurface = static_cast<IDirect3DSurface9 *>(surface); }
    bool SurvivesModeChange() override { return false; }
    uint32_t GetAccessFlags() override { return 1; }
    bool Create(nGZGraphic4::cIDeviceDX *, uint32_t, uint32_t, void *, uint32_t) override { return false; }
    ~LockableProbe() override { mSurface = nullptr; }
    void *Primary() { return static_cast<nGZGraphic4::cIImageSurface *>(this); }
    void *Face() { return static_cast<nGZGraphic4::cISurfaceDX *>(this); }
    uint32_t Refs() const { return mRefs; }
};

} // namespace

DIFF_TEST(gz_dx_LockableQueryInterface, 0x1628e0)
{
    using Query = uint8_t(__thiscall *)(void *, uint32_t, void **);
    const Query primary = ctx.Original<Query>(0x1628e0), thunk = ctx.Original<Query>(0x16295d);
    using namespace nGZGraphic4;
    const uint32_t ids[] = {1, 2, kIidDeviceResource, kIidAllocDeviceResource, kIidSurface, kIidSurfaceDX, kIidImageSurface,
                            kIidLockableSurface, kIidDepthStencilSurface, kIidRenderTarget, kIidLockableRenderTarget,
                            kIidD3DSurface, kIidD3DResource, 0};
    int surfaceMark = 0;
    for (const uint32_t id : ids)
        for (int face = 0; face < 2; ++face)
            for (int nullOut = 0; nullOut < 2; ++nullOut) {
                ++ctx.cases;
                LockableProbe a(&surfaceMark), b(&surfaceMark);
                void *outA = &a, *outB = &b;
                const auto relative = [&](void *out, LockableProbe &p) {
                    return reinterpret_cast<uintptr_t>(out) - reinterpret_cast<uintptr_t>(&p);
                };
                const uint8_t ra = face ? thunk(a.Face(), id, nullOut ? nullptr : &outA) : primary(a.Primary(), id, nullOut ? nullptr : &outA);
                void *const self = face ? b.Face() : b.Primary();
                const uint8_t rb = uint8_t(
                    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(self, id, nullOut ? nullptr : &outB));
                const bool sameAnswer = outA == &surfaceMark || outB == &surfaceMark ? outA == outB : relative(outA, a) == relative(outB, b);
                if (ra != rb || !sameAnswer || a.Refs() != b.Refs())
                    ctx.Fail("id %08x face %d null %d: original %u refs %u, ours %u refs %u", id, face, nullOut, ra, a.Refs(), rb, b.Refs());
            }
}
