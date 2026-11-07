// Differential tests for nGZGraphic4::cDeviceResourceManagerDX as a whole class (src/gz/cDeviceResourceManagerDX.h, lane
// NT, rendering R1c): the constructor 0x17ad50, every slot of vftable 0xd9f9d0, the members cDeviceDX's CreateDevice and
// ReleaseDevice call (0x17bb80 ReleaseAll, 0x17b710, 0x17bb30, 0x17bb00) and the iterator class (vftable 0xd9fa20, the
// fixed pool 0xfedb58) GetResources hands out. RVAs this file verifies:
//   0x17ad50 0x17b7d0 0x17baa0 0x17b8e0 0x17b800 0x17bc10 0x17bbb0 0x17b990 0x17bac0 0x17baf0 0x17b080 0x17b690
//   0x17ba90 0x17b060 0x17b010 0x17af90 0x17bb80 0x17b710 0x17bb30 0x17bb00; the iterator's 0x17b620 0x17b680 0x17b670
//   0x17b700 0x17b760 0x17aed0 (and the shared 0x178b20 0x178b60 0x148fb0).
//
// Each case builds a manager (the original's constructor on one side, ours on the other), then runs a seeded sequence of
// operations through the object's own vtable (the image's vftable on one side, ours on the other), the members at their
// RVAs or as ours, and finally releases it. The world, built alike for each side from the case's seed:
// - Six resources, fakes of cIAllocDeviceResource / cITextureBaseDX (QueryInterface answers itself for the texture face
//   when it is a texture; the pool, video memory size, survival flags, levels, sizes and quality are the case's and
//   change between operations). Their ReleaseResource unregisters through slot 8 or slot 7 of the manager under test, as
//   the real resources do, so ReleaseAll and the walks see the set shrink.
// - The device (cIDeviceDX: readiness, WaitForGpu, the Direct3D device, the device enumeration), the enumeration (its
//   QueryInterface answers itself, GetSetting is the minimum dimension, its property set answers modulateClampedTextureDim
//   or nothing) and the Direct3D device (GetAvailableTextureMem, EvictManagedResources: failing sometimes, which the
//   ErrorLog records) and the Direct3D textures (GetLOD / SetLOD) are fakes.
// - The pool 0xfedb58 is rebuilt for each side on one set of pages, so both sides' iterators sit at the same addresses.
// Compared: the log (every call on every fake with its arguments, the errors reported, each operation's result), the
// manager's fields after every operation (the budget words, the working vector's size and capacity, and the set walked in
// order with each node's colour and links), the pool's pages, every fake's reference count, and the blocks and bytes
// allocated less freed over the whole case.
// Left out of the comparison, with the reason: the byte at +0x15..+0x17 (the constructor never writes them: bytes of the
// new block, 0x17ad50 stores only +0x14), and the two bytes after each set node's colour and nil flags (0x17b800 stores a
// word at +0xc and the node's other bytes come from the allocator).
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceResourceManagerDX.h"
#include "rz/RZFixedAllocator.h"

#include <windows.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

constexpr uint32_t kVTableRva = 0xd9f9d0;
constexpr uint32_t kConstructRva = 0x17ad50, kReleaseAllRva = 0x17bb80, kNonSurvivorsRva = 0x17b710,
                   kModeChangeRva = 0x17bb30, kTakeDeviceRva = 0x17bb00;
constexpr uint32_t kIidManager = 0xbae8fd06, kIidTextureBaseDX = 0x1b891e38, kIidEnum = 0x6c02c311;
constexpr unsigned kResources = 6, kLevels = 6;
constexpr uint32_t kManagerTag = 0x5e000000;

// ---- slot tables ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, drawfx::Ret::Int, true, "Release", ""};

// cIAllocDeviceResource and cITextureBaseDX in one table (the texture face's slots 16 and up).
inline constexpr drawfx::Slot kResourceSlots[] = {
    kQI, kAddRef, kRelease,
    {0, drawfx::Ret::Int, true, "ReleaseResource", ""},   // 3
    U, U, U, U,
    {0, drawfx::Ret::Int, true, "SurvivesReset", ""},      // 8
    {0, drawfx::Ret::Int, true, "SurvivesModeChange", ""}, // 9
    U, U,
    {0, drawfx::Ret::Int, true, "GetPool", ""},            // 12
    {0, drawfx::Ret::Int, true, "GetVideoMemorySize", ""}, // 13
    U, U,
    {0, drawfx::Ret::Int, true, "GetLevelCount", ""},      // 16
    {1, drawfx::Ret::Int, true, "GetWidth", "u"},          // 17
    {1, drawfx::Ret::Int, true, "GetHeight", "u"},         // 18
    U, U,
    {0, drawfx::Ret::Float, true, "GetQuality", ""},       // 21
    U,
    {0, drawfx::Ret::Int, true, "GetD3DTexture", ""},      // 23
    U,
    {1, drawfx::Ret::Int, true, "GetLevelBytes", "u"},     // 25
};
inline constexpr drawfx::Iface kResource{"cIAllocDeviceResource", 26, kResourceSlots};

inline constexpr drawfx::Slot kDeviceSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetDeviceEnumConst", ""}, // 16
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "IsDeviceReady", ""},      // 35
    U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "WaitForGpu", ""},         // 46
    U,
    {0, drawfx::Ret::Int, true, "GetD3DDevice", ""},       // 48
};
inline constexpr drawfx::Iface kDevice{"cIDeviceDX", 49, kDeviceSlots};

inline constexpr drawfx::Slot kEnumSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetProperties", ""}, // 22
    U, U, U,
    {1, drawfx::Ret::Int, true, "GetSetting", "u"},   // 26
};
inline constexpr drawfx::Iface kEnum{"cIDeviceEnum", 27, kEnumSlots};

inline constexpr drawfx::Slot kPropertySlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U,
    {2, drawfx::Ret::Int, true, "GetPropertyValue", "ii"}, // 9
};
inline constexpr drawfx::Iface kProperties{"cIGZPropertySetString", 10, kPropertySlots};

// ---- the world ----
struct ResourceData {
    bool texture;           // QueryInterface answers the texture face
    bool unregistersByErase;
    uint32_t pool, memory;
    bool survivesReset, survivesModeChange;
    uint32_t levels, width[kLevels], height[kLevels], bytes[kLevels];
    float quality;
    uint32_t d3dTexture;
};

struct WorldData {
    ResourceData resource[kResources];
    d3dfx::Resource d3dTexture[kResources];
    uint32_t ready, available, evictResult, minimumDimension;
    bool hasModulate;
    uint8_t modulate;
} gWorld;

drawfx::Fake gResource[kResources], gDevice, gEnum, gProperties, gD3D, gD3DTexture[kResources];
void *gManager; // the object under test, for the resources' ReleaseResource

uint32_t W(const void *p) { return drawfx::P(p); }
uint32_t N(uint32_t w) { return drawfx::Norm(w); }
uint32_t Data(drawfx::Fake &f) { return f.value[0]; }
ResourceData &R(drawfx::Fake &f) { return gWorld.resource[Data(f)]; }

uint32_t ResourceRelease(drawfx::Fake &f, const uint32_t *)
{
    if (R(f).unregistersByErase) return RawSlot<7>(gManager, &f);
    RawSlot<8, void>(gManager, &f);
    return 0;
}
uint32_t ResourceSurvivesReset(drawfx::Fake &f, const uint32_t *) { return R(f).survivesReset; }
uint32_t ResourceSurvivesMode(drawfx::Fake &f, const uint32_t *) { return R(f).survivesModeChange; }
uint32_t ResourcePool(drawfx::Fake &f, const uint32_t *) { return R(f).pool; }
uint32_t ResourceMemory(drawfx::Fake &f, const uint32_t *) { return R(f).memory; }
uint32_t ResourceLevels(drawfx::Fake &f, const uint32_t *) { return R(f).levels; }
uint32_t ResourceWidth(drawfx::Fake &f, const uint32_t *a) { return R(f).width[a[0] % kLevels]; }
uint32_t ResourceHeight(drawfx::Fake &f, const uint32_t *a) { return R(f).height[a[0] % kLevels]; }
uint32_t ResourceBytes(drawfx::Fake &f, const uint32_t *a) { return R(f).bytes[a[0] % kLevels]; }
uint32_t ResourceTexture(drawfx::Fake &f, const uint32_t *) { return W(&gD3DTexture[R(f).d3dTexture]); }

uint32_t DeviceReady(drawfx::Fake &, const uint32_t *) { return gWorld.ready; }
uint32_t EnumMinimum(drawfx::Fake &, const uint32_t *) { return gWorld.minimumDimension; }
uint32_t D3DAvailable(drawfx::Fake &, const uint32_t *) { return gWorld.available; }
uint32_t D3DEvict(drawfx::Fake &, const uint32_t *) { return gWorld.evictResult; }

// GetPropertyValue(key, value): the key's text is compared (length and a hash), the boolean it is handed is noted before
// it is written; answers whether the property exists.
uint32_t PropertyGet(drawfx::Fake &, const uint32_t *a)
{
    const auto *key = reinterpret_cast<const GZServiceString *>(uintptr_t(a[0]));
    uint32_t hash = 2166136261u;
    for (char c : *key) hash = (hash ^ uint8_t(c)) * 16777619u;
    uint8_t *value = reinterpret_cast<uint8_t *>(uintptr_t(a[1])) + 4;
    drawfx::Note("property", {uint32_t(key->size()), hash, *value});
    if (gWorld.hasModulate) *value = gWorld.modulate;
    return gWorld.hasModulate;
}

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }
float PickFloat(std::mt19937 &g, std::initializer_list<float> v) { return v.begin()[g() % v.size()]; }

void SeedResource(std::mt19937 &g, ResourceData &r)
{
    r.texture = g() % 5 != 0;
    r.unregistersByErase = g() % 4 == 0;
    r.pool = Pick(g, {0, 1, 1, 1, 1, 2, 3});
    r.memory = Pick(g, {0, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0xffffff00});
    r.survivesReset = g() % 2;
    r.survivesModeChange = g() % 2;
    r.levels = Pick(g, {0, 1, 2, 3, 4, 5, 6, 6});
    const uint32_t base = Pick(g, {1, 2, 8, 32, 128, 512, 2048});
    for (unsigned l = 0; l < kLevels; ++l) {
        r.width[l] = std::max<uint32_t>(1, base >> l);
        r.height[l] = std::max<uint32_t>(1, (g() % 3 ? base : base * 2) >> l);
        r.bytes[l] = g() % 8 == 0 ? Pick(g, {0, 0x7fffffff, 0xffffffff}) : (r.memory >> (2 * l)) + g() % 64;
    }
    r.quality = PickFloat(g, {0.25f, 0.5f, 1.0f, 1.0f, 2.0f, 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity()});
    r.d3dTexture = g() % kResources;
}

void Seed(uint32_t seed)
{
    std::mt19937 g(seed);
    for (ResourceData &r : gWorld.resource) SeedResource(g, r);
    for (d3dfx::Resource &t : gWorld.d3dTexture) {
        t = d3dfx::Resource{};
        t.levels = g() % 7;
        t.lod = Pick(g, {0, 0, 1, 2, 3});
    }
    gWorld.ready = g() % 5 != 0;
    gWorld.available = Pick(g, {0, 0x1000, 0x20000, 0x80000, 0x200000, 0x7fffffff, 0xffffffff});
    gWorld.evictResult = g() % 5 == 0 ? Pick(g, {0x8876086c, 0x80004005, 1}) : 0;
    gWorld.minimumDimension = Pick(g, {0, 1, 4, 16, 64, 256, 0xffffffff});
    gWorld.hasModulate = g() % 4 != 0;
    gWorld.modulate = uint8_t(g() % 2);
}

void Build()
{
    for (unsigned i = 0; i < kResources; ++i) {
        drawfx::Fake &f = gResource[i];
        f.Reset<kResource>(0x6e0000 + i);
        f.value[0] = i;
        if (gWorld.resource[i].texture) f.AddFace(kIidTextureBaseDX, f);
        f.On(3, &ResourceRelease);
        f.On(8, &ResourceSurvivesReset);
        f.On(9, &ResourceSurvivesMode);
        f.On(12, &ResourcePool);
        f.On(13, &ResourceMemory);
        f.On(16, &ResourceLevels);
        f.On(17, &ResourceWidth);
        f.On(18, &ResourceHeight);
        f.On(23, &ResourceTexture);
        f.On(25, &ResourceBytes);
        f.ReturnsFloat(21, gWorld.resource[i].quality);
        d3dfx::MakeResource(gD3DTexture[i], 0xd3d60000 + i, gWorld.d3dTexture[i]);
    }
    gDevice.Reset<kDevice>(0x6d0001);
    gDevice.Returns(16, &gEnum);
    gDevice.On(35, &DeviceReady);
    gDevice.Returns(48, &gD3D);
    gEnum.Reset<kEnum>(0x6d0002);
    gEnum.AddFace(kIidEnum, gEnum);
    gEnum.Returns(22, &gProperties);
    gEnum.On(26, &EnumMinimum);
    gProperties.Reset<kProperties>(0x6d0003);
    gProperties.On(9, &PropertyGet);
    gD3D.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
    gD3D.On(4, &D3DAvailable);
    gD3D.On(5, &D3DEvict);
}

// ---- the pool 0xfedb58 (blocks of 0xc, pages of 0x800) ----
constexpr uint32_t kPoolRva = cDeviceResourceIterator::kPool;
constexpr uint32_t kPage = 0x800, kBlock = 0xc, kBlocks = (kPage - 16) / kBlock, kPages = 3;
uint8_t *gPages;
RZFixedPage *Page(int i) { return reinterpret_cast<RZFixedPage *>(gPages + kPage * uint32_t(i)); }
uint8_t *Block(int p, uint32_t k) { return gPages + kPage * uint32_t(p) + 16 + k * kBlock; }
void ChainPage(int p, uint32_t freeBlocks)
{
    uint8_t *first = nullptr;
    for (uint32_t k = freeBlocks; k-- > 0;) {
        *reinterpret_cast<uint8_t **>(Block(p, k)) = first;
        first = Block(p, k);
    }
    Page(p)->freeList = first;
    Page(p)->used = kBlocks - freeBlocks;
    Page(p)->next = Page(p)->prev = Page(p);
}
int gNextPage;
RZFixedPage *__fastcall PoolNewPage(void *, void *)
{
    drawfx::Note("pool NewPage", 0);
    const int p = gNextPage < int(kPages) ? gNextPage++ : int(kPages) - 1;
    ChainPage(p, kBlocks);
    Page(p)->used = 0;
    return Page(p);
}
void __fastcall PoolFreePage(void *, void *, RZFixedPage *page)
{
    drawfx::Note("pool FreePage", uint32_t((reinterpret_cast<uint8_t *>(page) - gPages) / kPage));
}
void *gPoolVT[4] = {nullptr, nullptr, reinterpret_cast<void *>(&PoolNewPage), reinterpret_cast<void *>(&PoolFreePage)};
void BuildPool(uint32_t freeBlocks)
{
    if (!gPages) gPages = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memset(gPages, 0xcd, kPage * kPages);
    ChainPage(0, freeBlocks);
    gNextPage = 1;
    uint32_t *pool = GameAddress<uint32_t>(kPoolRva);
    pool[0] = W(gPoolVT);
    pool[1] = W(Page(0));
    pool[2] = 0;
}
uint32_t PoolNorm(uint32_t w)
{
    const uintptr_t base = uintptr_t(gPages);
    if (w >= base && w < base + kPage * kPages) return 0x9a000000u | uint32_t(w - base);
    return w;
}
void DumpPool()
{
    const uint32_t *pool = GameAddress<uint32_t>(kPoolRva);
    std::vector<uint32_t> w{PoolNorm(pool[1]), PoolNorm(pool[2])};
    for (unsigned p = 0; p < kPages; ++p) {
        w.push_back(PoolNorm(W(Page(int(p))->next)));
        w.push_back(PoolNorm(W(Page(int(p))->prev)));
        w.push_back(PoolNorm(W(Page(int(p))->freeList)));
        w.push_back(Page(int(p))->used);
    }
    drawfx::Call c;
    c.note = "pool";
    c.words = std::move(w);
    drawfx::gLog.push_back(std::move(c));
}

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

// ---- observing the object ----
// What the manager holds after an operation, as words: the budget fields, the working vector's size and capacity (its
// entries too, while it holds any), and the set in order with each node's colour and the links as in-order indexes.
struct Node {
    const uint8_t *at;
    uint32_t Left() const { uint32_t v; std::memcpy(&v, at, 4); return v; }
    uint32_t Parent() const { uint32_t v; std::memcpy(&v, at + 4, 4); return v; }
    uint32_t Right() const { uint32_t v; std::memcpy(&v, at + 8, 4); return v; }
    bool Nil() const { return at[0xd] != 0; }
    uint32_t Value() const { uint32_t v; std::memcpy(&v, at + 0x10, 4); return v; }
};
Node At(uint32_t p) { return Node{reinterpret_cast<const uint8_t *>(uintptr_t(p))}; }
void InOrder(uint32_t node, std::vector<uint32_t> &order)
{
    if (At(node).Nil()) return;
    InOrder(At(node).Left(), order);
    order.push_back(node);
    InOrder(At(node).Right(), order);
}

void NoteManager(const char *label, const void *object)
{
    const auto *m = static_cast<const uint8_t *>(object);
    auto word = [&](uint32_t off) {
        uint32_t v;
        std::memcpy(&v, m + off, 4);
        return v;
    };
    std::vector<uint32_t> w;
    w.push_back(word(4));                 // count
    w.push_back(N(word(8)));              // the device
    w.push_back(word(0xc));               // delta
    w.push_back(word(0x10));              // pending
    w.push_back(m[0x14]);                 // enabled (the three bytes after it are never written)
    for (uint32_t off : {0x18u, 0x1cu, 0x20u, 0x24u}) w.push_back(word(off)); // quality, limit, reserved, available
    const uint32_t begin = word(0x28), end = word(0x2c), cap = word(0x30);
    w.push_back((end - begin) / 16);
    w.push_back((cap - begin) / 16);
    for (uint32_t p = begin; p < end; p += 16) {
        const auto *e = reinterpret_cast<const uint32_t *>(uintptr_t(p));
        w.insert(w.end(), {N(e[0]), e[1], e[2], e[3]});
    }
    const uint32_t head = word(0x34);
    w.push_back(word(0x38)); // size
    std::vector<uint32_t> order;
    InOrder(At(head).Parent(), order);
    auto index = [&](uint32_t node) -> uint32_t {
        if (node == head) return 0xffff;
        for (uint32_t i = 0; i < order.size(); ++i)
            if (order[i] == node) return i;
        return 0xeeee;
    };
    w.push_back(uint32_t(order.size()));
    w.push_back(index(At(head).Left()));
    w.push_back(index(At(head).Right()));
    w.push_back(index(At(head).Parent()));
    for (uint32_t node : order) {
        const Node n = At(node);
        w.insert(w.end(), {N(n.Value()), n.at[0xc], index(n.Left()), index(n.Parent()), index(n.Right())});
    }
    drawfx::Call c;
    c.note = label;
    c.words = std::move(w);
    drawfx::gLog.push_back(std::move(c));
}

void NoteRefs()
{
    std::vector<uint32_t> w;
    for (const drawfx::Fake *f : drawfx::gFakes) w.push_back(uint32_t(f->refs));
    drawfx::Call c;
    c.note = "refs";
    c.words = std::move(w);
    drawfx::gLog.push_back(std::move(c));
}

// ---- running an operation ----
struct Coverage {
    unsigned manages = 0, lowered = 0, settled = 0, evictions = 0, reports = 0, ranges = 0, releases = 0, properties = 0,
             modulated = 0, adds = 0;
};

struct Side {
    difftest::Context &ctx;
    bool original;
    void *m = nullptr;
    uint32_t refs = 0;

    template <class Fn> Fn Orig(uint32_t rva) const { return ctx.Original<Fn>(rva); }

    void *Make()
    {
        void *memory = ::operator new(cDeviceResourceManagerDX::kSize);
        std::memset(memory, 0xcd, cDeviceResourceManagerDX::kSize);
        if (original) return Orig<void *(__thiscall *)(void *, void *)>(kConstructRva)(memory, &gDevice);
        return cDeviceResourceManagerAdapters::Construct(memory, nullptr, reinterpret_cast<cIDeviceDX *>(&gDevice));
    }
    cDeviceResourceManagerDX *Ours() const { return static_cast<cDeviceResourceManagerDX *>(m); }
    void ReleaseAll() { original ? Orig<void(__thiscall *)(void *)>(kReleaseAllRva)(m) : Ours()->ReleaseAll(); }
    void NonSurvivors() { original ? Orig<void(__thiscall *)(void *)>(kNonSurvivorsRva)(m) : Ours()->ReleaseNonSurvivors(); }
    void ModeChange()
    {
        // 0x17bb30 is called with two stack arguments it pops and never reads; ours takes none.
        if (original) Orig<void(__thiscall *)(void *, void *, uint32_t)>(kModeChangeRva)(m, &gD3D, uint32_t(m != nullptr));
        else Ours()->ReleaseOnModeChange();
    }
    void TakeDevice(void *d3d)
    {
        original ? Orig<void(__thiscall *)(void *, void *)>(kTakeDeviceRva)(m, d3d)
                 : Ours()->TakeDevice(reinterpret_cast<IDirect3DDevice9 *>(d3d));
    }
};

void NoteResult(uint32_t v) { drawfx::Note("result", v); }

// GetResources and a walk over what it hands out.
void WalkRange(Side &s, Coverage &cov, std::mt19937 &g)
{
    uint32_t out[3] = {0xa5a5a5a5, 0x5a5a5a5a, 0xa5a5a5a5};
    void *const returned = RawSlot<9, void *>(s.m, out);
    drawfx::Note("range", {W(returned) == W(out), PoolNorm(out[0]), PoolNorm(out[1]), PoolNorm(out[2])});
    ++cov.ranges;
    void *const cursor = reinterpret_cast<void *>(uintptr_t(out[0]));
    void *const begin = reinterpret_cast<void *>(uintptr_t(out[1]));
    void *const end = reinterpret_cast<void *>(uintptr_t(out[2]));
    for (unsigned guard = 0; guard < 12 && !RawSlot<4, bool>(cursor, end); ++guard) {
        NoteResult(N(RawSlot<5>(cursor)));
        RawSlot<6, void>(cursor);
    }
    NoteResult(RawSlot<4, bool>(cursor, end));
    NoteResult(RawSlot<4, bool>(begin, end));
    if (!RawSlot<4, bool>(begin, end)) {
        RawSlot<7, void>(end); // the last resource
        NoteResult(N(RawSlot<5>(end)));
        NoteResult(RawSlot<4, bool>(end, begin));
        RawSlot<6, void>(end);
        NoteResult(RawSlot<4, bool>(end, begin));
    } else {
        RawSlot<7, void>(end); // on an empty set the end stays the end
        NoteResult(RawSlot<4, bool>(end, begin));
    }
    void *const clone = RawSlot<3, void *>(begin);
    drawfx::Note("clone", {PoolNorm(W(clone)), RawSlot<4, bool>(clone, begin)});
    RawSlot<1>(clone);
    NoteResult(RawSlot<2>(clone)); // a clone starts uncounted: one AddRef, one Release frees it
    // QueryInterface: only 1; a null out; the count it raises.
    void *got = nullptr;
    NoteResult(RawSlot<0, bool>(begin, uint32_t(1), &got));
    drawfx::Note("iterator qi", {PoolNorm(W(got)) == PoolNorm(W(begin))});
    if (got) NoteResult(RawSlot<2>(got));
    got = nullptr;
    NoteResult(RawSlot<0, bool>(begin, uint32_t(g() % 2 ? 0xbae8fd06u : 2u), &got));
    NoteResult(RawSlot<0, bool>(begin, uint32_t(1), static_cast<void **>(nullptr)));
    DumpPool();
    NoteResult(RawSlot<2>(cursor));
    NoteResult(RawSlot<2>(begin));
    NoteResult(RawSlot<2>(end));
}

void Operation(Side &s, Coverage &cov, std::mt19937 &g)
{
    void *const m = s.m;
    drawfx::Fake &res = gResource[g() % kResources];
    const uint32_t op = g() % 30;
    switch (op) {
    case 0: {
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        const uint32_t iid = Pick(g, {1, kIidManager, kIidTextureBaseDX, 0xdeadbeef, 0});
        void **target = g() % 5 == 0 ? nullptr : &out;
        const bool answered = RawSlot<0, bool>(m, iid, target);
        NoteResult(answered);
        drawfx::Note("qi out", {N(W(out))});
        if (answered && target) ++s.refs;
        break;
    }
    case 1: ++s.refs; NoteResult(RawSlot<1>(m)); break;
    case 2:
        if (s.refs > 1) {
            --s.refs;
            NoteResult(RawSlot<2>(m));
        }
        break;
    case 3: NoteResult(N(RawSlot<3>(m))); NoteResult(N(RawSlot<4>(m))); break;
    case 4: case 5: RawSlot<5, void>(m, &res); break;
    case 6: case 7: case 8: RawSlot<6, void>(m, &res); ++cov.adds; break;
    case 9: NoteResult(RawSlot<7>(m, &res)); break;
    case 10: case 11: RawSlot<8, void>(m, &res); break;
    case 12: WalkRange(s, cov, g); break;
    case 13: {
        const uint32_t flag = (g() % 2) | (g() % 3 == 0 ? 0xabcd00u : 0u); // a bool argument: only its low byte counts
        const float quality = PickFloat(g, {0.0f, 25.0f, 50.0f, 95.0f, 100.0f, 250.0f, std::numeric_limits<float>::quiet_NaN(),
                                            -10.0f, 1e30f});
        const uint32_t limit = Pick(g, {0, 0x100, 0x2000, 0x40000, 0x200000, 0x7fffffff, 0xffffffff});
        RawSlot<10, void>(m, flag, quality, limit);
        break;
    }
    case 14: RawSlot<11, void>(m); break;
    case 15: case 16: case 17: case 18: {
        // Budget pressure: sometimes the pending bytes, sometimes a fresh available figure, then the pass.
        if (g() % 3 == 0) RawSlot<14, void>(m, Pick(g, {0, 5, uint32_t(-5), 0x1000, uint32_t(-0x40000), 0x7fffffff, 0x80000000}));
        if (g() % 3 == 0) gWorld.available = Pick(g, {0, 0x2000, 0x40000, 0x100000, 0x400000});
        if (g() % 4 == 0) gWorld.minimumDimension = Pick(g, {0, 1, 4, 16, 64, 256});
        if (g() % 6 == 0) SeedResource(g, gWorld.resource[g() % kResources]);
        const uint32_t before = unsigned(drawfx::gLog.size());
        RawSlot<12, void>(m);
        ++cov.manages;
        for (size_t i = before; i < drawfx::gLog.size(); ++i) {
            const drawfx::Call &c = drawfx::gLog[i];
            if (c.iface && c.iface->name == std::string("IDirect3DTexture9") && c.slot == 11) ++cov.lowered;
            if (c.iface == &kResource && c.slot == 25) ++cov.settled;
            if (c.iface == &kProperties) ++cov.properties;
            if (!c.iface && std::strcmp(c.note, "property") == 0 && c.words[2] != 0) ++cov.modulated;
            if (c.iface && c.iface->name == std::string("IDirect3DDevice9") && c.slot == 5) ++cov.evictions;
            if (!c.iface && std::strcmp(c.note, "LogD3DError") == 0) ++cov.reports;
        }
        break;
    }
    case 19: RawSlot<13, void>(m); break;
    case 20: RawSlot<14, void>(m, Pick(g, {0, 1, uint32_t(-1), 0x100, uint32_t(-0x100), 0x7fffffff, 0x80000000})); break;
    case 21: {
        uint8_t buffer[12];
        std::memset(buffer, 0xa5, sizeof(buffer));
        void *const r = RawSlot<15, void *>(m, buffer);
        uint32_t w[3];
        std::memcpy(w, buffer, sizeof(w));
        drawfx::Note("policy", {W(r) == W(buffer), w[0], w[1], w[2]});
        break;
    }
    case 22: NoteResult(RawSlot<16>(m)); drawfx::NoteFloat("quality", RawSlot<17, float>(m)); break;
    case 23: s.NonSurvivors(); break;
    case 24: s.ModeChange(); break;
    case 25: s.ReleaseAll(); break;
    case 26: s.TakeDevice(&gD3D); break;
    case 27: {
        // Nothing in the game writes the reserved bytes; the budget arithmetic reads them.
        const uint32_t reserved = Pick(g, {0, 0x1000, 0x30000, 0x100000, 0xffffffff});
        std::memcpy(static_cast<uint8_t *>(m) + 0x20, &reserved, 4);
        break;
    }
    case 28: gWorld.ready = g() % 4 != 0; gWorld.available = Pick(g, {0, 0x2000, 0x40000, 0x100000, 0xffffffff}); break;
    default: SeedResource(g, gWorld.resource[g() % kResources]); break;
    }
}

struct After {
    drawfx::Log log;
    std::vector<std::string> errors;
    long long blocks = 0, bytes = 0;
    size_t allocations = 0, frees = 0;
    Coverage coverage;
};

After RunSide(difftest::Context &ctx, uint32_t seed, bool original, bool keepSet)
{
    After a;
    Seed(seed);
    auto [log, errors] = drawfx::Side([&] { Build(); BuildPool(1 + seed % 40); }, [&] {
        drawfx::gLog.reserve(8192);
        Side s{ctx, original};
        // The harness allocates when an original is first asked for: resolve them all before counting.
        for (uint32_t rva : {kConstructRva, kReleaseAllRva, kNonSurvivorsRva, kModeChangeRva, kTakeDeviceRva})
            (void)s.Orig<void *(*)()>(rva);
        {
            difftest::AllocationCounter counter;
            std::mt19937 g(seed * 7919 + 13);
            s.m = s.Make();
            gManager = s.m;
            drawfx::AddRange(s.m, cDeviceResourceManagerDX::kSize, kManagerTag);
            NoteManager("new", s.m);
            RawSlot<1>(s.m);
            s.refs = 1;
            for (unsigned step = 0; step < 36; ++step) {
                Operation(s, a.coverage, g);
                NoteManager("state", s.m);
            }
            if (!keepSet) {
                s.ReleaseAll();
                NoteManager("released", s.m);
            }
            NoteRefs();
            DumpPool();
            while (s.refs > 0) {
                --s.refs;
                const uint32_t left = RawSlot<2>(s.m);
                drawfx::Note("last", left);
            }
            counter.Stop();
            a.blocks = counter.Blocks();
            a.bytes = counter.Bytes();
            a.allocations = counter.Allocations();
            a.frees = counter.Frees();
        }
        gManager = nullptr;
        NoteRefs();
        DumpPool();
    });
    a.log = std::move(log);
    a.errors = std::move(errors);
    return a;
}

struct Fixture {
    svcfx::AllocatorScope allocator;
    d3dfx::ErrorLog errors;
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    uint32_t savedPool[3];
    explicit Fixture(difftest::Context &ctx) : errors(ctx)
    {
        std::memcpy(savedPool, GameAddress<uint32_t>(kPoolRva), sizeof(savedPool));
    }
    ~Fixture() { std::memcpy(GameAddress<uint32_t>(kPoolRva), savedPool, sizeof(savedPool)); }
};

} // namespace

DIFF_TEST(gz_device_resource_manager, kConstructRva)
{
    Fixture fx(ctx);
    Coverage seen;
    // A warm-up case on each side: the first run of anything in the process allocates what later runs reuse.
    (void)RunSide(ctx, 0, true, false);
    (void)RunSide(ctx, 0, false, false);
    for (uint32_t seed = 1; seed <= 1200; ++seed) {
        const bool keepSet = seed % 3 == 0;
        const After a = RunSide(ctx, seed, true, keepSet);
        const After b = RunSide(ctx, seed, false, keepSet);
        seen.manages += a.coverage.manages;
        seen.lowered += a.coverage.lowered;
        seen.settled += a.coverage.settled;
        seen.evictions += a.coverage.evictions;
        seen.reports += a.coverage.reports;
        seen.ranges += a.coverage.ranges;
        seen.properties += a.coverage.properties;
        seen.modulated += a.coverage.modulated;
        seen.adds += a.coverage.adds;
        std::vector<std::string> errors = a.errors;
        errors.insert(errors.end(), b.errors.begin(), b.errors.end());
        if (!drawfx::Same(ctx, a.log, b.log, "Manager", seed, errors)) return;
        if (a.blocks != b.blocks || a.bytes != b.bytes || a.allocations != b.allocations || a.frees != b.frees) {
            ctx.Fail("Manager seed %u: allocations differ: blocks %lld/%lld bytes %lld/%lld allocated %zu/%zu freed %zu/%zu",
                     seed, a.blocks, b.blocks, a.bytes, b.bytes, a.allocations, b.allocations, a.frees, b.frees);
            return;
        }
    }
    if (!seen.manages || !seen.lowered || !seen.settled || !seen.evictions || !seen.reports || !seen.ranges ||
        !seen.properties || !seen.modulated)
        ctx.Fail("Manager: a path never ran (passes %u, lowered %u, settled %u, evictions %u, reports %u, ranges %u, property "
                 "reads %u, modulated %u)",
                 seen.manages, seen.lowered, seen.settled, seen.evictions, seen.reports, seen.ranges, seen.properties,
                 seen.modulated);
}
