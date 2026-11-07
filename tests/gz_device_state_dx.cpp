// Differential tests for nGZGraphic4::cDeviceStateDX (src/gz/cDeviceStateDX.h), the render-state cache in front of the
// Direct3D 9 device, against the campaign's shared fake device (tests/d3dfx.h).
//
// Standalone shape (docs/standalone-shape.md, "Tests"): our object is built by our constructor (the hook's adapter) and
// every slot is called through our vtable; the original's object is the same bytes with the game's vtable word, and the
// original functions are called at their RVAs. Only the vtable word is left out of the byte comparison.
//
// - cDeviceStateDX_Ops: seeded objects (wanted and sent state, pending groups and constant ranges, change record, held
//   objects) and per case a sequence of up to six operations drawn from three (so that repeats meet the "unchanged"
//   paths), every slot and non-virtual member, with arguments from small pools (NaN, -0.0 and fractions that round
//   differently among the floats). Compared: the device log (calls with normalised arguments, LogD3DError reports in
//   order), the engine objects' calls (QueryInterface ids, GetDescription, GetWidth / GetHeight, Release), results
//   (notes), and every byte of the object after the sequence. The test fails if an operation never took one of its paths.
// - cDeviceStateDX_Construct: the constructor on random bytes, both sides from the same bytes and the same garbage in
//   the three global index maps; compared: every byte but the vtable word, the maps, the result.
// - cDeviceStateDX_Destroy: Release from 1 (the deleting destructor reached through the vtable with flags 1: a recording
//   stand-in in a copy of each side's vtable runs the side's own non-deleting destructor), Release from above 1, and the
//   destructor itself. mList (+0x9a08, the std::vector filled by the state-block slots) stays empty: freeing it is the
//   library's vector destructor (the original's 0x17ca08..0x17ca55 is the inlined std::vector teardown).
// - cDeviceStateDX_Blocks: the state blocks (slots 10 and 11 with StateDelta::Build / Serialize, the apply and restore
//   helpers, and the cStateBlockDX class with its pool, its list in the cache and the resource manager's Insert / Erase)
//   against the same seeded objects: sequences of CreateStateBlock (all the group masks, equal blocks made again),
//   ApplyStateBlock, the state setters, and the blocks' own slots (QueryInterface, AddRef, Release to zero,
//   ReleaseResource, the getters). Compared: the device log, the resource manager's log, the pool's state after each
//   side, every byte of the cache and of each live block (the cache's mList storage pointers are the library's vector;
//   its elements, in order, are compared), then the same after everything is released.
// - cDeviceStateDX_ValidateCommand / _CheckVertexProcessing / _Validate (lane PQ): slots 29, 27 and 28 (0x1829d0,
//   0x181f00, 0x182220) and the validators they call, on seeded objects, random and biased caps, fake textures,
//   buffers and framework; the report text, device and held-buffer calls, reference counts, lock memory and every
//   object byte compared. Slot 9 (Reset, 0x17e070) is run in Ops (kReset).
// - cDeviceStateDX_SetUp (lane PO): SetUpDevice (0x1807b0, the set-up after every device creation or Reset) on seeded
//   objects whose defaults are built or not (then the 33 default commands run), against a device whose getters hand
//   out a swap chain, back buffer and depth surface (or none) and whose results fail at random, an enumeration with
//   programmed caps (noise above the answer's byte), limits and D3DCAPS9, and the graphic service's palette. Compared:
//   the device log, the engine objects' and services' calls, LogD3DError reports, reference counts and every object
//   byte. Fails if a path (first set-up and repeat, palette or not, stream frequencies or not, depth or none, default
//   lights, a failure reported) ran too rarely.
//
// RVAs of the functions this file covers that no Orig() call names (the guard greps this file for each verified RVA):
//   slot 12's command helpers, run through ApplyStateCommand in the Ops and Blocks sequences: 0x186530 0x18a180
//   0x18a0e0 0x18a060 0x186ab0 0x186bb0 0x186f60 0x1868c0 0x186c30 0x1877e0 0x187550 0x187330 0x1878f0 0x187b30
//   0x187c10 0x187d90 0x187e90 0x188010 0x1883c0 0x188290 0x188620 0x1884f0 0x188ab0 0x188770 0x188830 0x188ce0
//   0x188b50 0x188c00 0x189390 0x1891a0 0x1898b0 0x189c90 0x186720;
//   reached through CreateStateBlock / ApplyStateBlock in the Blocks sequences: StateDelta::Build 0x17d510,
//   Serialize 0x182d90, ApplyRecordStates 0x17f180, ApplyRecordStreams 0x17ee30, RestoreChangedStates 0x181820,
//   RestoreChangedStreams 0x1816d0, and the cStateBlockDX list lookup 0x17bdc0, list removal 0x17dda0, ReleaseRecord
//   0x17dcd0 and deleting destructor 0x17caa0;
//   SetUpDevice's 33 default commands, run in the SetUp cases that build the defaults: 0x186550 0x18a1a0 0x18a110
//   0x18a080 0x186ad0 0x186bd0 0x186f80 0x186900 0x186d60 0x187820 0x187660 0x187410 0x1879f0 0x187b80 0x187c70
//   0x187db0 0x187ec0 0x188060 0x1883f0 0x1882b0 0x188650 0x188510 0x188ae0 0x1887b0 0x188870 0x188df0 0x188b70
//   0x188c20 0x1893d0 0x1891c0 0x1898f0 0x189cd0 0x186740;
//   the validators of slots 27-29 (lane PQ): 0x1865f0 0x1867c0 0x186980 0x186b30 0x186ed0 0x186fe0 0x187890 0x187d20
//   0x187f40 0x187fa0 0x1880e0 0x188310 0x188460 0x188570 0x1888e0 0x188c80 0x188ec0 0x189010 0x189240 0x189470
//   0x1895c0 0x189990 0x189a70 0x189d70 0x189e40.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "gz/TextureSupportDX.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cStateBlockDX.h"
#include "rz/RZFixedAllocator.h"
#include "rz/StaticGetters.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <type_traits>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using drawfx::Fake;
using drawfx::Ret;
using nGZGraphic4::cDeviceStateDX;
using nGZGraphic4::cDeviceStateDXAdapters;
using nGZGraphic4::cIDeviceState;
namespace ds = nGZGraphic4::devstate;

constexpr size_t kSize = sizeof(cDeviceStateDX);
constexpr uint32_t kTag = 0x57000000;
alignas(16) uint8_t gA[kSize]; // the original's side
alignas(16) uint8_t gB[kSize]; // ours
difftest::Context *gCtx;

// The engine's own objects the cache is handed (textures, shaders, buffers, surfaces, the owning device, held state
// blocks): thiscall cIGZUnknown objects whose QueryInterface hands out the Direct3D object (a face).
constexpr drawfx::Slot kU{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot kEngineSlots[] = {
    {2, Ret::Int, true, "QueryInterface", ""}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    kU, kU, kU,
    {0, Ret::Int, true, "IsValid", ""}, // lane PQ: cISurface slot 6 (a render target the command names)
    kU, kU, kU,
    {0, Ret::Int, true, "GetDescription", ""}, // cIShaderProgram slot 10
    kU, kU, kU, kU, kU, kU, kU,
    {0, Ret::Int, true, "GetResourceManager", ""}, // cIDevice slot 18
    kU,
    {0, Ret::Int, true, "GetWidth", ""},  // cISurface slot 20
    {0, Ret::Int, true, "GetHeight", ""}, // cISurface slot 21
    kU, kU, kU, kU, kU, kU, kU,
    {0, Ret::Int, true, "BeginScene", ""}, // lane PQ: cIDeviceDX slots 29 and 30 (the trial draw of Validate)
    {0, Ret::Int, true, "EndScene", ""},
    kU, kU, kU, kU, kU, kU, kU, kU,
    {1, Ret::Int, true, "GetBackBuffer", "u"},           // lane PO: cIDevice slot 39 (the owner, in SetUpDevice)
    {0, Ret::Int, true, "GetTargetProviderSurface", ""}, // cIDevice slot 40
};
static_assert(sizeof(kEngineSlots) / sizeof(kEngineSlots[0]) == 41);
constexpr drawfx::Iface kEngine{"engine object", 41, kEngineSlots};

// The ids the cache asks its arguments for (test constants: the original's code is the reference).
constexpr uint32_t kTextureId = 0x0998dbd2, kVertexShaderId = 0x09b1b574, kPixelShaderId = 0xcc433307,
                   kDeclarationId = 0x8b448b4c, kVertexBufferId = 0x0998d7e9, kIndexBufferId = 0x4998d917,
                   kSurfaceId = 0x4a5a7223;
void *const kDeviceEnum = reinterpret_cast<void *>(uintptr_t(0x5e5e0040));

// Lane OV: the device enumeration Reset asks for the limits of the vertex processing mode (cIDeviceEnum slot 21,
// GetLimit(index): a word of the table the case programs).
constexpr drawfx::Slot kEnumSlots[] = {
    {2, Ret::Int, true, "QueryInterface", ""}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU,
    {1, Ret::Int, true, "HasProcessingCap", "u"}, // lane PO: 20 (SetUpDevice)
    {1, Ret::Int, true, "GetLimit", "u"},
    kU,
    {1, Ret::Int, true, "GetCaps", "b"}, // lane PO: 23, GetCaps(bool hardware): the argument's low byte
    {0, Ret::Int, true, "GetCurrentCaps", ""}, // lane PQ: 24 and 25 (Validate)
    {1, Ret::Int, true, "HasFeature", "u"},
};
static_assert(sizeof(kEnumSlots) / sizeof(kEnumSlots[0]) == 26);
constexpr drawfx::Iface kEnumIface{"device enum", 26, kEnumSlots};

Fake gDevice, gOwner, gNone, gEnum;
Fake gTex[2], gVs[2], gPs[2], gDecl[2], gVb[2], gIb[2], gSurf[3];                       // Direct3D objects
Fake gEngTex[2], gEngVs[2], gEngPs[2], gEngDecl[2], gEngVb[2], gEngIb[2], gEngSurf[3], gHeld[4]; // engine objects
d3dfx::Answers gAnswers;

// What the engine objects answer in one case.
struct Program {
    uint8_t hardware[3];   // byte 7 of the shaders' descriptions (gEngVs[0], [1], gNone)
    uint32_t width[4], height[4]; // gEngSurf[0..2], gNone
    uint32_t resourceManager;
    uint32_t limit[32];    // lane OV: the device enumeration's table (GetLimit)
    uint32_t behavior;     // lane OV: the creation parameters' BehaviorFlags
} gProgram;

uint32_t Description(Fake &f, const uint32_t *) { return drawfx::P(f.value); }
// IDirect3DDevice9::GetCreationParameters (slot 9): the structure is written whole, whatever the result.
uint32_t CreationParameters(Fake &f, const uint32_t *args)
{
    auto *out = reinterpret_cast<D3DDEVICE_CREATION_PARAMETERS *>(uintptr_t(args[0]));
    out->AdapterOrdinal = 0;
    out->DeviceType = D3DDEVTYPE_HAL;
    out->hFocusWindow = nullptr;
    out->BehaviorFlags = gProgram.behavior;
    return f.ret[9];
}
uint32_t Limit(Fake &, const uint32_t *args) { return gProgram.limit[args[0] % 32]; }

template <const drawfx::Iface &I, size_t N> void ResetGroup(Fake (&fakes)[N], uint32_t &id)
{
    for (Fake &f : fakes) f.Reset<I>(++id);
}

// An empty log and fresh fakes programmed for this case; `object` is the side's object (pointers into it normalise).
void Fresh(const void *object)
{
    drawfx::ResetAll();
    gDevice.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
    gAnswers.Apply(gDevice);
    gDevice.On(9, CreationParameters);
    gEnum.Reset<kEnumIface>(0xe7000001);
    gEnum.On(21, Limit);
    uint32_t id = 0xd3d00100;
    ResetGroup<fx_d3d9::IDirect3DTexture9>(gTex, id);
    ResetGroup<fx_d3d9::IDirect3DVertexShader9>(gVs, id);
    ResetGroup<fx_d3d9::IDirect3DPixelShader9>(gPs, id);
    ResetGroup<fx_d3d9::IDirect3DVertexDeclaration9>(gDecl, id);
    ResetGroup<fx_d3d9::IDirect3DVertexBuffer9>(gVb, id);
    ResetGroup<fx_d3d9::IDirect3DIndexBuffer9>(gIb, id);
    ResetGroup<fx_d3d9::IDirect3DSurface9>(gSurf, id);
    uint32_t eid = 0xe0000000;
    ResetGroup<kEngine>(gEngTex, eid);
    ResetGroup<kEngine>(gEngVs, eid);
    ResetGroup<kEngine>(gEngPs, eid);
    ResetGroup<kEngine>(gEngDecl, eid);
    ResetGroup<kEngine>(gEngVb, eid);
    ResetGroup<kEngine>(gEngIb, eid);
    ResetGroup<kEngine>(gEngSurf, eid);
    ResetGroup<kEngine>(gHeld, eid);
    gOwner.Reset<kEngine>(++eid);
    gNone.Reset<kEngine>(++eid);
    for (unsigned i = 0; i < 2; ++i) {
        gEngTex[i].AddFace(kTextureId, gTex[i]);
        gEngVs[i].AddFace(kVertexShaderId, gVs[i]);
        gEngVs[i].On(10, Description);
        gEngVs[i].value[1] = uint32_t(gProgram.hardware[i]) << 24;
        gEngPs[i].AddFace(kPixelShaderId, gPs[i]);
        gEngDecl[i].AddFace(kDeclarationId, gDecl[i]);
        gEngVb[i].AddFace(kVertexBufferId, gVb[i]);
        gEngIb[i].AddFace(kIndexBufferId, gIb[i]);
    }
    for (unsigned i = 0; i < 3; ++i) {
        gEngSurf[i].AddFace(kSurfaceId, gSurf[i]);
        gEngSurf[i].Returns(20, gProgram.width[i]);
        gEngSurf[i].Returns(21, gProgram.height[i]);
    }
    // gNone answers no id (its QueryInterface leaves the out-parameter as the caller set it).
    gNone.On(10, Description);
    gNone.value[1] = uint32_t(gProgram.hardware[2]) << 24;
    gNone.Returns(20, gProgram.width[3]);
    gNone.Returns(21, gProgram.height[3]);
    gOwner.Returns(18, gProgram.resourceManager);
    drawfx::AddRange(object, kSize, kTag);
}

cDeviceStateDX *Obj(uint8_t *raw) { return reinterpret_cast<cDeviceStateDX *>(raw); }
uint32_t Word(const uint8_t *raw, size_t offset)
{
    uint32_t w;
    std::memcpy(&w, raw + offset, 4);
    return w;
}
void Note(const char *label, uint32_t value) { drawfx::Note(label, drawfx::Norm(value)); }

// Original functions (thiscall unless named otherwise).
template <class R = void, class... A> R Orig(uint32_t rva, void *self, A... a)
{
    return gCtx->Original<R(__thiscall *)(void *, A...)>(rva)(self, a...);
}

// ---- pools ----

template <size_t N> void *Native(Fake (&pool)[N], uint32_t r)
{
    const uint32_t k = r % (N + 1);
    return k == N ? nullptr : &pool[k];
}
template <size_t N> cIGZUnknown *Engine(Fake (&pool)[N], uint32_t r)
{
    const uint32_t k = r % (N + 2);
    if (k < N) return reinterpret_cast<cIGZUnknown *>(&pool[k]);
    return k == N ? reinterpret_cast<cIGZUnknown *>(&gNone) : nullptr;
}
float FloatOf(uint32_t r)
{
    static const float k[] = {0.0f, 0.25f, 0.5f, 1.0f, 0.3333f, -0.0f, std::numeric_limits<float>::quiet_NaN(), 1.5f,
                              -0.25f, 0.7f};
    return k[r % 10];
}
uint32_t Dimension(uint32_t r)
{
    switch (r % 8) {
    case 0: return 0x80000000u + (r >> 8) % 16; // above INT_MAX: the unsigned conversion
    case 1: return 0;
    default: return (r >> 4) % 2048;
    }
}

D3DMATRIX gMatrices[4];
uint32_t gWorlds[3][13];
D3DMATERIAL9 gMaterials[4];
D3DLIGHT9 gLights[2];

void FillPools()
{
    std::mt19937 gen(0x17d980);
    std::memset(gMatrices, 0, sizeof(gMatrices));
    gMatrices[0]._11 = gMatrices[0]._22 = gMatrices[0]._33 = gMatrices[0]._44 = 1.0f;
    float *m1 = &gMatrices[1]._11, *m2 = &gMatrices[2]._11;
    for (unsigned i = 0; i < 16; ++i) {
        m1[i] = float(int(gen() % 9) - 4) * 0.5f;
        m2[i] = float(int(gen() % 9) - 4) * 0.25f;
    }
    m2[5] = std::numeric_limits<float>::quiet_NaN();
    m2[6] = -0.0f;
    for (auto &w : gWorlds)
        for (uint32_t &v : w) v = std::bit_cast<uint32_t>(float(int(gen() % 9) - 4) * 0.5f);
    gWorlds[2][4] = 0x80000000u; // -0.0: equal as a float, not as a word
    for (auto &m : gMaterials) {
        auto *w = reinterpret_cast<uint32_t *>(&m);
        for (unsigned i = 0; i < sizeof(m) / 4; ++i) w[i] = gen() % 3;
    }
    // Lane OV: the last word apart (a comparison that stops one word short must see the difference).
    gMatrices[3] = gMatrices[0]; // the default matrix but for the last word
    gMatrices[3]._44 = 0.5f;
    gMaterials[1].Power = std::bit_cast<float>(0x00123456u);
    gMaterials[2] = gMaterials[1];
    gMaterials[2].Power = std::bit_cast<float>(0x3f123456u); // differs from material 1 in the record's last byte alone
    std::memset(&gMaterials[3], 0, sizeof(D3DMATERIAL9)); // the default material but for the last word
    gMaterials[3].Power = 1.0f;
    for (auto &l : gLights) {
        auto *w = reinterpret_cast<uint32_t *>(&l);
        for (unsigned i = 0; i < sizeof(l) / 4; ++i) w[i] = gen() % 3;
    }
}

// ---- building and seeding ----

// Our object built by our constructor (the hook's adapter) on gB, then seeded; gA gets the same bytes and the game's
// vtable word.
void Seed(std::mt19937 &gen)
{
    std::memset(gB, 0, kSize);
    cDeviceStateDX *s = cDeviceStateDXAdapters::Construct(gB, nullptr, reinterpret_cast<nGZGraphic4::cDeviceDX *>(&gOwner),
                                                          kDeviceEnum);
    s->mD3D = reinterpret_cast<IDirect3DDevice9 *>(&gDevice);
    s->mDeviceEnum = &gEnum;
    s->mRefCount = 2 + gen() % 3;
    s->mDefaultsBuilt = static_cast<uint8_t>(gen());
    s->mHardwareVertexProcessing = static_cast<uint8_t>(gen() % 2);
    s->mRenderTargetCount = static_cast<uint8_t>(gen() % 5);
    s->mPad17 = static_cast<uint8_t>(gen());
    s->mChangeCount = gen() % 1000;
    auto *changes = reinterpret_cast<uint8_t *>(&s->mChanges);
    for (size_t i = 0; i < sizeof(s->mChanges); ++i) changes[i] = gen() % 2 ? 0 : static_cast<uint8_t>(gen());
    s->mStreamChanges = static_cast<uint16_t>(gen());
    s->mDeclarationChanged = static_cast<uint8_t>(gen() % 2);
    s->mIndicesChanged = static_cast<uint8_t>(gen() % 2);

    // Pending groups: flags, and constant ranges [start, end) inside each array (or none: start kNoStart, end 0).
    s->mPending.targets = static_cast<uint8_t>(gen() % 2);
    s->mPending.viewport = static_cast<uint8_t>(gen() % 2);
    s->mPending.scissor = static_cast<uint8_t>(gen() % 2);
    auto range = [&](uint32_t limit, uint32_t &start, auto &end) {
        if (gen() % 3 == 0) {
            start = ds::kNoStart;
            end = 0;
            return;
        }
        uint32_t a = gen() % limit, b = gen() % limit;
        if (a > b) std::swap(a, b);
        if (a == b) a > 0 ? --a : ++b;
        start = a;
        end = static_cast<std::remove_reference_t<decltype(end)>>(b);
    };
    range(0x400, s->mVertexFloatStart, s->mPending.vertexFloatEnd);
    range(0x100, s->mVertexIntStart, s->mPending.vertexIntEnd);
    range(0x100, s->mVertexBoolStart, s->mPending.vertexBoolEnd);
    range(0xe0, s->mPixelFloatStart, s->mPending.pixelFloatEnd);
    range(0x10, s->mPixelIntStart, s->mPending.pixelIntEnd);
    range(0x10, s->mPixelBoolStart, s->mPending.pixelBoolEnd);
    uint32_t x = gen();
    auto fill = [&x](void *p, size_t bytes) {
        auto *w = static_cast<uint32_t *>(p);
        for (size_t i = 0; i < bytes / 4; ++i) w[i] = x = x * 1664525u + 1013904223u;
    };
    fill(s->mVertexFloats, sizeof(s->mVertexFloats));
    fill(s->mVertexInts, sizeof(s->mVertexInts));
    fill(s->mVertexBools, sizeof(s->mVertexBools));
    fill(s->mPixelFloats, sizeof(s->mPixelFloats));
    fill(s->mPixelInts, sizeof(s->mPixelInts));
    fill(s->mPixelBools, sizeof(s->mPixelBools));

    ds::DeviceState &w = s->mWanted;
    for (auto &t : w.textures) t = static_cast<IDirect3DBaseTexture9 *>(Native(gTex, gen()));
    w.softwareVertexProcessing = gen() % 4 == 0 ? 2 : gen() % 2;
    w.vertexShader = static_cast<IDirect3DVertexShader9 *>(Native(gVs, gen()));
    w.pixelShader = static_cast<IDirect3DPixelShader9 *>(Native(gPs, gen()));
    for (auto &v : w.renderStates) v = gen() % 3;
    for (uint32_t stage = 0; stage < 8; ++stage) {
        for (auto &v : w.stageStates[stage]) v = gen() % 3;
        w.stageStates[stage][10] = gen() % 2 ? stage : gen() % 3;
        w.stageStates[stage][11] = gen() % 2 ? 0 : 1 + gen() % 2;
    }
    for (auto &sampler : w.samplerStates)
        for (auto &v : sampler) v = gen() % 3;
    for (auto &m : w.textureTransforms) m = gMatrices[gen() % 4];
    w.material = gMaterials[gen() % 4];
    w.declaration = static_cast<IDirect3DVertexDeclaration9 *>(Native(gDecl, gen()));
    for (auto &stream : w.streams) {
        stream.buffer = static_cast<IDirect3DVertexBuffer9 *>(Native(gVb, gen()));
        stream.stride = gen() % 2 * 16;
        stream.offset = gen() % 2 * 4;
    }
    w.indices = static_cast<IDirect3DIndexBuffer9 *>(Native(gIb, gen()));
    for (auto &t : w.targets) t = static_cast<IDirect3DSurface9 *>(Native(gSurf, gen()));
    w.depth = static_cast<IDirect3DSurface9 *>(Native(gSurf, gen()));
    w.targetWidth = Dimension(gen());
    w.targetHeight = Dimension(gen());
    for (float &f : w.scissor) f = FloatOf(gen());
    w.scissorEnable = gen() % 3;
    for (float &f : w.viewport) f = FloatOf(gen());
    w.minZ = FloatOf(gen());
    w.maxZ = FloatOf(gen());
    w.clipping = gen() % 3;
    w.projection = gMatrices[gen() % 4];
    w.world = gMatrices[gen() % 4];
    w.ambient = gen() % 2 ? 0 : 0xff336699u;
    w.lightCount = gen() % 5;
    for (auto &l : w.lights) l = gLights[gen() % 2];

    for (auto &t : s->mSentTargets) t = static_cast<IDirect3DSurface9 *>(Native(gSurf, gen()));
    s->mSentDepth = static_cast<IDirect3DSurface9 *>(Native(gSurf, gen()));
    s->mSentViewport = {gen() % 4, gen() % 4, gen() % 64, gen() % 64, FloatOf(gen()), FloatOf(gen())};
    s->mSentScissor = {LONG(gen() % 4), LONG(gen() % 4), LONG(gen() % 64), LONG(gen() % 64)};
    s->mSentScissorEnable = gen() % 3;
    auto held = [&](unsigned i) { return gen() % 3 == 0 ? nullptr : reinterpret_cast<cIGZUnknown *>(&gHeld[i]); };
    s->mHeld9a14 = held(0);
    s->mHeld9a18 = held(1);
    s->mStateBlock98 = reinterpret_cast<nGZGraphic4::cStateBlockDX *>(held(2)); // fakes: only released
    s->mStateBlock9c = reinterpret_cast<nGZGraphic4::cStateBlockDX *>(held(3));

    std::memcpy(gA + 4, gB + 4, kSize - 4);
    *reinterpret_cast<void **>(gA) = GameAddress<void>(cDeviceStateDX::kOriginalVTable);

    gAnswers.Random(gen, fx_d3d9::IDirect3DDevice9.count);
    for (uint8_t &h : gProgram.hardware) h = static_cast<uint8_t>(gen() % 3 == 0 ? 0 : gen() % 2 ? 1 : 0x80);
    for (unsigned i = 0; i < 4; ++i) {
        gProgram.width[i] = Dimension(gen());
        gProgram.height[i] = Dimension(gen());
    }
    gProgram.resourceManager = 0x7e5e0000u + gen() % 16;
    // Lane OV: the limits (hardware mode at the lower index of each pair, software at the next; none beyond the
    // arrays they size), and the creation parameters' behaviour: software (0x20) or mixed (0x80) vertex processing,
    // or neither (0x40 is hardware; other bits are noise).
    static const struct {
        unsigned index, count;
        uint32_t max;
    } kLimitSizes[] = {{10, 2, 16}, {0x10, 2, 0x400}, {0x12, 2, 0x100}, {0x14, 2, 0x100}, {0x16, 1, 0xe0},
                       {0x17, 1, 0x10}, {0x18, 1, 0x10}, {0x19, 2, 16}};
    std::memset(gProgram.limit, 0, sizeof(gProgram.limit));
    for (const auto &size : kLimitSizes)
        for (unsigned k = 0; k < size.count; ++k) {
            const uint32_t pick[] = {0, 1, 2, 3, size.max, size.max / 2, size.max - 1};
            gProgram.limit[size.index + k] = pick[gen() % 7];
        }
    gProgram.behavior = (gen() % 2 ? 0x20 : 0) | (gen() % 3 == 0 ? 0x80 : 0) | (gen() % 2 ? 0x40 : 0) | (gen() % 2 ? 0x1 : 0);
}

// First differing byte after the vtable word, or kSize.
size_t FirstDifference()
{
    for (size_t i = 4; i < kSize; ++i)
        if (gA[i] != gB[i]) return i;
    return kSize;
}

// ---- render state commands (slot 12) ----

constexpr unsigned kCommandIds = 33;
unsigned gCommandRuns[kCommandIds]; // per id, on the original's side

// Colour channels that clamp both ways, round differently, and are not numbers.
float ColorOf(uint32_t r)
{
    static const float k[] = {0.0f, 0.25f, 0.5f, 0.999f, 1.0f, 1.5f, -0.5f, 255.0f, 1e30f, -1e30f,
                              std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.3333f,
                              0.0039f, 3.99f, -0.0f};
    return k[r % 16];
}

// A command of an id drawn from r: id byte at +4, size at +6, the fields with values from small pools (the enumerations
// the apply functions map by table stay inside their tables).
void MakeCommand(uint8_t *cmd, const uint32_t *r)
{
    std::memset(cmd, 0, 0x60);
    const unsigned id = r[0] % kCommandIds;
    cmd[4] = static_cast<uint8_t>(id);
    uint32_t x = r[1];
    auto next = [&x] { return x = x * 1664525u + 1013904223u; };
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(cmd + at, &v, 4); };
    auto putf = [&](size_t at, float v) { std::memcpy(cmd + at, &v, 4); };
    auto putb = [&](size_t at, uint32_t v) { cmd[at] = static_cast<uint8_t>(v); };
    auto under = [&](uint32_t n) { return (next() >> 8) % n; };
    switch (id) {
    case 0: {
        cIGZUnknown *t[5];
        t[0] = next() % 4 == 3 ? reinterpret_cast<cIGZUnknown *>(&gNone) : reinterpret_cast<cIGZUnknown *>(&gEngSurf[next() % 3]);
        for (unsigned k = 1; k < 4; ++k) t[k] = Engine(gEngSurf, next());
        t[4] = Engine(gEngSurf, next());
        for (unsigned k = 0; k < 5; ++k) put32(k < 4 ? 8 + k * 4 : 0x18, uint32_t(reinterpret_cast<uintptr_t>(t[k])));
        break;
    }
    case 1: for (unsigned k = 0; k < 4; ++k) putf(8 + k * 4, FloatOf(next() >> 8)); break;
    case 2: putf(8, FloatOf(next() >> 8)); putf(0xc, FloatOf(next() >> 8)); break;
    case 3: case 4: case 5: case 6: case 27: case 19: case 21: putb(8, under(id == 19 || id == 21 ? 8 : 3)); break;
    case 7: for (unsigned k = 0; k < 4; ++k) putb(8 + k, under(2)); break;
    case 8:
        putb(8, under(2)); putb(9, under(3)); putb(0xa, under(2)); putb(0xb, under(3) + under(2) * 100);
        for (unsigned k = 0; k < 3; ++k) putf(0xc + k * 4, ColorOf(next() >> 8));
        for (unsigned k = 0; k < 3; ++k) putf(0x18 + k * 4, FloatOf(next() >> 8));
        break;
    case 9: putb(8, under(2)); putb(9, under(3)); putb(0xa, under(2)); break;
    case 10:
        put32(8, under(16));
        put32(0xc, under(3));
        for (size_t at = 0x10; at <= 0x54; at += 4) putf(at, FloatOf(next() >> 8));
        break;
    case 11:
        put32(8, under(6));
        for (unsigned k = 0; k < 3; ++k) putf(0xc + k * 4, ColorOf(next() >> 8));
        break;
    case 12: for (size_t at = 8; at <= 0x48; at += 4) putf(at, FloatOf(next() >> 8)); break;
    case 13: for (unsigned k = 0; k < 5; ++k) putb(8 + k, under(3)); break;
    case 14: {
        static const uint32_t kSources[] = {0x100, 0x101, 0x102, 0x103, 0, 2};
        put32(8, under(8));
        put32(0xc, kSources[under(6)]);
        break;
    }
    case 15: put32(8, under(8)); std::memcpy(cmd + 0xc, &gMatrices[under(4)], 64); break;
    case 16: put32(8, under(8)); putb(0xc, under(4)); putb(0xd, under(3)); break;
    case 17: putb(8, under(2)); for (unsigned k = 1; k < 4; ++k) putb(8 + k, under(5)); break;
    case 18: putb(8, under(2)); putb(9, under(256)); break;
    case 20: putb(8, under(2)); putb(9, under(2)); break;
    case 22: putb(8, under(2)); put32(0xc, next()); break;
    case 23: putb(8, under(8)); put32(0xc, next()); put32(0x10, next()); break;
    case 24: for (unsigned k = 0; k < 3; ++k) putb(8 + k, under(8)); break;
    case 25:
        put32(8, under(8));
        for (size_t at : {0xcu, 0xeu, 0xfu, 0x10u, 0x12u, 0x13u, 0x14u}) putb(at, under(16) << 4 | under(6));
        putb(0xd, under(20));
        putb(0x11, under(20));
        break;
    case 26: put32(8, next()); break;
    case 28: put32(8, under(16)); for (unsigned k = 0; k < 3; ++k) putb(0xc + k, under(6)); break;
    case 29: putb(8, under(0x58)); putb(9, under(3)); break;
    case 30: put32(8, under(16)); putb(0xc, under(5)); putb(0xd, under(5)); put32(0x10, under(3)); break;
    case 31: put32(8, under(16)); putb(0xc, under(4)); putf(0x10, FloatOf(next() >> 8)); break;
    case 32:
        for (unsigned k = 0; k < 4; ++k) putf(8 + k * 4, FloatOf(next() >> 8));
        putb(0x18, under(3));
        break;
    }
}

// ---- operations ----

enum Op : unsigned {
    kSetTexture, kSetVertexShader, kSetPixelShader, kSetVertexDeclaration, kSetStreamSource, kSetIndices,
    kLockVF, kLockVI, kLockVB, kLockPF, kLockPI, kLockPB, kSetProjection, kSetWorld, kGetChangeCount, kGetters,
    kGetResourceManager, kQueryInterface, kAddRef, kRelease, kFlush, kMarkTargetsDirty, kSetBackBufferTarget,
    kResetTexCoord, kRestoreTexCoord, kReleaseDeviceObjects, kSetRenderState, kSetRenderStates, kSetStageState,
    kSetStageStates, kSetSamplerStates, kSetTextureTransform, kSetMaterial, kSetLight, kSetLightCount, kSetClipping,
    kSetDepthRange, kSetViewportFractions, kSetScissor, kSetRenderTargets, kCopyMatrix, kSetDefaults, kClearDelta,
    kApplyCommand, kReset,
    kOps
};
// How a test case shows an operation took each of its paths (on the original's side): 'c' the change count moved and
// stood still, 'd' the device was called and was not, 'r' the result was 0 and nonzero, 'n' ran.
struct OpInfo {
    const char *name;
    char kind;
};
constexpr OpInfo kOpInfo[kOps] = {
    {"SetTexture", 'c'}, {"SetVertexShader", 'c'}, {"SetPixelShader", 'c'}, {"SetVertexDeclaration", 'c'},
    {"SetStreamSource", 'c'}, {"SetIndices", 'c'}, {"LockVF", 'n'}, {"LockVI", 'n'}, {"LockVB", 'n'}, {"LockPF", 'n'},
    {"LockPI", 'n'}, {"LockPB", 'n'}, {"SetProjection", 'c'}, {"SetWorld", 'c'}, {"GetChangeCount", 'n'},
    {"Getters", 'n'}, {"GetResourceManager", 'n'}, {"QueryInterface", 'r'}, {"AddRef", 'n'}, {"Release", 'n'},
    {"Flush", 'd'}, {"MarkTargetsDirty", 'n'}, {"SetBackBufferTarget", 'n'}, {"ResetTexCoord", 'n'},
    {"RestoreTexCoord", 'n'}, {"ReleaseDeviceObjects", 'n'}, {"SetRenderState", 'c'}, {"SetRenderStates", 'c'},
    {"SetStageState", 'c'}, {"SetStageStates", 'c'}, {"SetSamplerStates", 'c'}, {"SetTextureTransform", 'c'},
    {"SetMaterial", 'c'}, {"SetLight", 'c'}, {"SetLightCount", 'c'}, {"SetClipping", 'c'}, {"SetDepthRange", 'c'},
    {"SetViewportFractions", 'c'}, {"SetScissor", 'c'}, {"SetRenderTargets", 'c'}, {"CopyMatrix", 'r'},
    {"SetDefaults", 'n'}, {"ClearDelta", 'n'}, {"ApplyCommand", 'c'}, {"Reset", 'n'},
};

constexpr unsigned kArgs = 12;
unsigned gResetBits[7], gResetRuns; // lane OV: how often each group of Reset was asked for (original's side)

// One operation on one side with the arguments derived from r; the result (for 'r' kinds).
uint32_t Run(unsigned op, const uint32_t *r, bool ours)
{
    uint8_t *raw = ours ? gB : gA;
    cDeviceStateDX *s = Obj(raw);
    cIDeviceState *i = s;
    uint32_t result = 0;
    // The constant locks: the registers handed out are filled (as the engine does) so Flush sends them.
    auto lock = [&](uint32_t rva, auto member, uint32_t limit, uint32_t per) {
        const uint32_t start = r[0] % limit, count = 1 + r[1] % std::min<uint32_t>(16, limit - start);
        void *p = ours ? static_cast<void *>((i->*member)(start, count)) : Orig<void *>(rva, raw, start, count);
        Note("locked", drawfx::P(p));
        auto *words = static_cast<uint32_t *>(p);
        if (p >= raw && words + count * per <= reinterpret_cast<uint32_t *>(raw + kSize))
            for (uint32_t k = 0; k < count * per; ++k) words[k] = r[2] * (k + 1);
    };
    uint32_t indices[4], values[4];
    const uint32_t count = 1 + r[1] % 4;
    for (unsigned k = 0; k < 4; ++k) values[k] = r[6 + k] % 3;
    switch (op) {
    case kSetTexture: {
        const uint32_t stage = r[0] % 16;
        cIGZUnknown *t = Engine(gEngTex, r[1]);
        ours ? i->SetTexture(stage, t) : Orig(0x1801b0, raw, stage, t);
        break;
    }
    case kSetVertexShader: {
        cIGZUnknown *v = Engine(gEngVs, r[0]);
        ours ? i->SetVertexShader(v) : Orig(0x180550, raw, v);
        break;
    }
    case kSetPixelShader: {
        cIGZUnknown *v = Engine(gEngPs, r[0]);
        ours ? i->SetPixelShader(v) : Orig(0x17f770, raw, v);
        break;
    }
    case kSetVertexDeclaration: {
        cIGZUnknown *v = Engine(gEngDecl, r[0]);
        ours ? i->SetVertexDeclaration(v) : Orig(0x180470, raw, v);
        break;
    }
    case kSetStreamSource: {
        const uint32_t stream = r[0] % 16, stride = r[2] % 2 * 16, offset = r[3] % 2 * 4;
        cIGZUnknown *v = Engine(gEngVb, r[1]);
        ours ? i->SetStreamSource(stream, v, stride, offset) : Orig(0x1803c0, raw, stream, v, stride, offset);
        break;
    }
    case kSetIndices: {
        cIGZUnknown *v = Engine(gEngIb, r[0]);
        ours ? i->SetIndices(v) : Orig(0x1804e0, raw, v);
        break;
    }
    case kLockVF: lock(0x180680, &cIDeviceState::LockVertexFloatConstants, 0x400, 4); break;
    case kLockVI: lock(0x1806d0, &cIDeviceState::LockVertexIntConstants, 0x100, 4); break;
    case kLockVB: lock(0x180630, &cIDeviceState::LockVertexBoolConstants, 0x100, 1); break;
    case kLockPF: lock(0x17f830, &cIDeviceState::LockPixelFloatConstants, 0xe0, 4); break;
    case kLockPI: lock(0x17f880, &cIDeviceState::LockPixelIntConstants, 0x10, 4); break;
    case kLockPB: lock(0x17f7e0, &cIDeviceState::LockPixelBoolConstants, 0x10, 1); break;
    case kSetProjection: {
        const D3DMATRIX *m = &gMatrices[r[0] % 4];
        ours ? i->SetProjection(m) : Orig(0x17ede0, raw, m);
        break;
    }
    case kSetWorld: {
        const void *t = gWorlds[r[0] % 3];
        ours ? i->SetWorld(t) : Orig(0x17ecf0, raw, t);
        break;
    }
    case kGetChangeCount: Note("changes", ours ? i->GetChangeCount() : Orig<uint32_t>(0x148ba0, raw)); break;
    case kGetters:
        Note("device", ours ? drawfx::P(i->GetDevice()) : Orig<uint32_t>(0x46390, raw));
        Note("device const", ours ? drawfx::P(i->GetDeviceConst()) : Orig<uint32_t>(0x46390, raw));
        Note("enum", ours ? drawfx::P(i->GetDeviceEnum()) : Orig<uint32_t>(0x145770, raw));
        Note("enum const", ours ? drawfx::P(i->GetDeviceEnumConst()) : Orig<uint32_t>(0x145770, raw));
        break;
    case kGetResourceManager:
        if (r[0] % 2)
            Note("resources", ours ? drawfx::P(i->GetResourceManager()) : Orig<uint32_t>(0x17cbf0, raw));
        else
            Note("resources const", ours ? drawfx::P(i->GetResourceManagerConst()) : Orig<uint32_t>(0x17cbf0, raw));
        break;
    case kQueryInterface: {
        static const uint32_t kIds[] = {1, cDeviceStateDX::kInterfaceId, cDeviceStateDX::kInterfaceId + 1, 0};
        const uint32_t riid = kIds[r[0] % 4];
        void *out = reinterpret_cast<void *>(uintptr_t(0x0bad0001));
        void **pout = r[1] % 4 == 0 ? nullptr : &out;
        result = (ours ? uint32_t(i->QueryInterface(riid, pout)) : Orig<uint32_t>(0x17dc50, raw, riid, pout)) & 0xff;
        Note("qi", result);
        Note("qi out", drawfx::P(out));
        break;
    }
    case kAddRef: Note("addref", ours ? i->AddRef() : Orig<uint32_t>(0x148fb0, raw)); break;
    case kRelease: // from above 1 only (cDeviceStateDX_Destroy has the last reference)
        if (s->mRefCount > 1)
            Note("release", ours ? i->Release() : Orig<uint32_t>(0x17dcb0, raw));
        else
            Note("addref", ours ? i->AddRef() : Orig<uint32_t>(0x148fb0, raw));
        break;
    case kFlush: {
        const uint32_t groups = (r[0] & 0x7f) | (r[1] % 2 ? 0x1c : 0);
        ours ? s->Flush(groups) : Orig(0x17cdf0, raw, groups);
        break;
    }
    case kMarkTargetsDirty: ours ? s->MarkTargetsDirty() : Orig(0x17ec40, raw); break;
    case kSetBackBufferTarget: {
        auto *surface = static_cast<IDirect3DSurface9 *>(Native(gSurf, r[0]));
        auto *depth = static_cast<IDirect3DSurface9 *>(Native(gSurf, r[1]));
        const uint32_t width = r[2] % 2048, height = r[3] % 2048;
        ours ? s->SetBackBufferTarget(surface, depth, width, height) : Orig(0x180050, raw, surface, depth, width, height);
        break;
    }
    case kResetTexCoord: ours ? s->ResetTexCoordStates() : Orig(0x17ff90, raw); break;
    case kRestoreTexCoord: ours ? s->RestoreTexCoordStates() : Orig(0x17eb90, raw); break;
    case kReleaseDeviceObjects:
        ours ? s->ReleaseDeviceObjects() : Orig(0x1815f0, raw);
        // The device pointer is cleared: noted, then given back so the sequence can go on.
        Note("device after release", Word(raw, 0x10));
        s->mD3D = reinterpret_cast<IDirect3DDevice9 *>(&gDevice);
        break;
    case kSetRenderState: {
        const uint32_t index = r[0] % 0x59, value = r[2] % 3;
        ours ? s->SetRenderState(index, value) : Orig(0x17f8d0, raw, index, value);
        break;
    }
    case kSetRenderStates:
        for (unsigned k = 0; k < 4; ++k) indices[k] = r[2 + k] % 0x59;
        ours ? s->SetRenderStates(count, indices, values) : Orig(0x17f940, raw, count, indices, values);
        break;
    case kSetStageState: {
        const uint32_t stage = r[0] % 8, index = r[2] % 0x12, value = r[3] % 3;
        ours ? s->SetTextureStageState(stage, index, value) : Orig(0x180230, raw, stage, index, value);
        break;
    }
    case kSetStageStates: {
        const uint32_t stage = r[0] % 8;
        for (unsigned k = 0; k < 4; ++k) indices[k] = r[2 + k] % 0x12;
        ours ? s->SetTextureStageStates(stage, count, indices, values) : Orig(0x1802b0, raw, stage, count, indices, values);
        break;
    }
    case kSetSamplerStates: {
        const uint32_t sampler = r[0] % 16;
        for (unsigned k = 0; k < 4; ++k) indices[k] = r[2 + k] % 0xd;
        ours ? s->SetSamplerStates(sampler, count, indices, values) : Orig(0x17fac0, raw, sampler, count, indices, values);
        break;
    }
    case kSetTextureTransform: {
        const uint32_t index = r[0] % 8;
        const D3DMATRIX *m = &gMatrices[r[2] % 4];
        ours ? s->SetTextureTransform(index, m) : Orig(0x180350, raw, index, m);
        break;
    }
    case kSetMaterial: {
        const D3DMATERIAL9 *m = &gMaterials[r[0] % 4];
        ours ? s->SetMaterial(m) : Orig(0x17f0a0, raw, m);
        break;
    }
    case kSetLight: {
        const uint32_t index = r[0] % 16;
        const D3DLIGHT9 *l = &gLights[r[2] % 2];
        ours ? s->SetLight(index, l) : Orig(0x17efb0, raw, index, l);
        break;
    }
    case kSetLightCount: {
        const uint32_t lights = r[0] % 6, ambient = r[2] % 2 ? 0 : 0xff336699u;
        ours ? s->SetLightCount(lights, ambient) : Orig(0x17f6a0, raw, lights, ambient);
        break;
    }
    case kSetClipping: {
        const uint32_t clipping = r[0] % 3;
        ours ? s->SetClipping(clipping) : Orig(0x17ec50, raw, clipping);
        break;
    }
    case kSetDepthRange: {
        const float minZ = FloatOf(r[0]), maxZ = FloatOf(r[2]);
        ours ? s->SetDepthRange(minZ, maxZ) : Orig(0x17eca0, raw, minZ, maxZ);
        break;
    }
    case kSetViewportFractions: {
        const float a = FloatOf(r[0]), b = FloatOf(r[2]), c = FloatOf(r[3]), d = FloatOf(r[4]);
        ours ? s->SetViewportFractions(a, b, c, d) : Orig(0x180720, raw, a, b, c, d);
        break;
    }
    case kSetScissor: {
        const float a = FloatOf(r[0]), b = FloatOf(r[2]), c = FloatOf(r[3]), d = FloatOf(r[4]);
        const uint8_t enable = static_cast<uint8_t>(r[5] % 3);
        ours ? s->SetScissor(a, b, c, d, enable) : Orig(0x17fb60, raw, a, b, c, d, enable);
        break;
    }
    case kSetRenderTargets: {
        cIGZUnknown *targets[4];
        targets[0] = r[0] % 4 == 3 ? reinterpret_cast<cIGZUnknown *>(&gNone) : reinterpret_cast<cIGZUnknown *>(&gEngSurf[r[0] % 4]);
        for (unsigned k = 1; k < 4; ++k) targets[k] = Engine(gEngSurf, r[1 + k]);
        cIGZUnknown *depth = Engine(gEngSurf, r[5]);
        ours ? s->SetRenderTargets(targets, depth) : Orig(0x17f9e0, raw, static_cast<cIGZUnknown *const *>(targets), depth);
        break;
    }
    case kCopyMatrix: {
        D3DMATRIX *dst = &s->mWanted.textureTransforms[r[0] % 8];
        const D3DMATRIX *src = &gMatrices[r[2] % 4];
        result = (ours ? uint32_t(cDeviceStateDX::CopyMatrixTransposed(dst, src))
                       : gCtx->Original<uint32_t(__cdecl *)(D3DMATRIX *, const D3DMATRIX *)>(0x17d980)(dst, src)) & 0xff;
        Note("copy", result);
        break;
    }
    case kSetDefaults: {
        ds::DeviceState *state = r[0] % 2 ? &s->mDefaults : &s->mWanted;
        ours ? state->SetDefaults() : Orig(0x17c1a0, state);
        break;
    }
    case kClearDelta: ours ? s->mDelta.Clear() : Orig(0x17dee0, &s->mDelta); break;
    case kApplyCommand: {
        alignas(16) uint8_t command[0x60];
        MakeCommand(command, r);
        const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command));
        if (!ours) ++gCommandRuns[command[4]];
        ours ? i->ApplyStateCommand(address) : Orig(0x17fd40, raw, address);
        break;
    }
    case kReset: {
        // Lane OV: the groups asked for (a quarter of the cases ask for every one), and the mode the case names.
        const uint32_t flags = r[1] % 4 == 0 ? 0x7f : r[0] % 0x80;
        if (!ours) {
            for (unsigned bit = 0; bit < 7; ++bit) gResetBits[bit] += (flags >> bit) & 1;
            ++gResetRuns;
        }
        ours ? i->Reset(flags) : Orig(0x17e070, raw, flags);
        break;
    }
    default: break;
    }
    return result;
}

struct OpSeen {
    unsigned runs = 0, moved = 0, still = 0, calls = 0, quiet = 0, zero = 0, nonzero = 0;
};

unsigned DeviceCalls(const drawfx::Log &log, size_t from)
{
    unsigned n = 0;
    for (size_t k = from; k < log.size(); ++k) n += log[k].iface == &fx_d3d9::IDirect3DDevice9;
    return n;
}

} // namespace

DIFF_TEST(cDeviceStateDX_Ops, 0x17cdf0)
{
    gCtx = &ctx;
    d3dfx::ErrorLog errors(ctx);
    FillPools();
    std::mt19937 gen(0x17cdf0);
    OpSeen seen[kOps];
    unsigned failures = 0;
    for (uint32_t round = 0; round < 6000; ++round) {
        Seed(gen);
        // Three operations (Flush one of them in a third of the cases), then a sequence of one to six of them.
        unsigned pick[3];
        for (unsigned &p : pick) p = gen() % kOps;
        if (gen() % 3 == 0) pick[2] = kFlush;
        if (gen() % 4 == 0) pick[1] = kApplyCommand; // 33 commands, each wants its runs
        if (gen() % 5 == 0) pick[gen() % 3] = kReset; // lane OV
        if (round < kOps) pick[0] = round; // every operation early
        const unsigned steps = 1 + gen() % 6;
        std::vector<unsigned> ops(steps);
        std::vector<std::array<uint32_t, kArgs>> args(steps);
        std::string label = "ops";
        for (unsigned k = 0; k < steps; ++k) {
            ops[k] = k == 0 ? pick[0] : pick[gen() % 3];
            // Arguments: a third of the steps repeat the previous step's (the unchanged paths).
            if (k && gen() % 3 == 0) args[k] = args[k - 1];
            else for (uint32_t &a : args[k]) a = gen();
            if (label.size() < 80) label += std::string(" ") + kOpInfo[ops[k]].name;
        }

        Fresh(gA);
        for (unsigned k = 0; k < steps; ++k) {
            OpSeen &o = seen[ops[k]];
            const size_t at = drawfx::gLog.size();
            const uint32_t changes = Obj(gA)->mChangeCount;
            const uint32_t result = Run(ops[k], args[k].data(), false);
            ++o.runs;
            (Obj(gA)->mChangeCount != changes ? o.moved : o.still) += 1;
            (DeviceCalls(drawfx::gLog, at) ? o.calls : o.quiet) += 1;
            (result ? o.nonzero : o.zero) += 1;
        }
        const drawfx::Log a = drawfx::gLog;
        std::vector<std::string> errs = drawfx::gErrors;
        failures += d3dfx::Notes(a, "LogD3DError");

        Fresh(gB);
        for (unsigned k = 0; k < steps; ++k) Run(ops[k], args[k].data(), true);
        const drawfx::Log b = drawfx::gLog;
        errs.insert(errs.end(), drawfx::gErrors.begin(), drawfx::gErrors.end());
        drawfx::ResetAll();

        if (!drawfx::Same(ctx, a, b, label.c_str(), round, errs)) continue;
        if (const size_t at = FirstDifference(); at != kSize)
            ctx.Fail("%s round %u: object byte +0x%x differs (orig %02x, ours %02x)", label.c_str(), round, unsigned(at),
                     gA[at], gB[at]);
    }
    for (unsigned op = 0; op < kOps; ++op) {
        const OpSeen &o = seen[op];
        bool ok = o.runs != 0;
        switch (kOpInfo[op].kind) {
        case 'c': ok = ok && o.moved && o.still; break;
        case 'd': ok = ok && o.calls && o.quiet; break;
        case 'r': ok = ok && o.zero && o.nonzero; break;
        default: break;
        }
        if (!ok)
            ctx.Fail("%s: a path never ran (runs %u, count moved %u / still %u, device calls %u / none %u, result 0 %u / "
                     "nonzero %u)", kOpInfo[op].name, o.runs, o.moved, o.still, o.calls, o.quiet, o.zero, o.nonzero);
    }
    for (unsigned bit = 0; bit < 7; ++bit)
        if (gResetBits[bit] < 50) ctx.Fail("Reset group 0x%x was asked for only %u times", 1u << bit, gResetBits[bit]);
    if (!failures) ctx.Fail("no failing HRESULT was reported");
    for (unsigned id = 0; id < kCommandIds; ++id)
        if (gCommandRuns[id] < 20) ctx.Fail("render state command %u ran only %u times", id, gCommandRuns[id]);
}

namespace {

uint8_t gMapsBefore[0x59 * 2 + 0x12 + 0xd];

void SaveMaps(uint8_t *out)
{
    std::memcpy(out, GameAddress<uint8_t>(ds::kRenderStateMap), 0x59 * 2);
    std::memcpy(out + 0x59 * 2, GameAddress<uint8_t>(ds::kStageStateMap), 0x12);
    std::memcpy(out + 0x59 * 2 + 0x12, GameAddress<uint8_t>(ds::kSamplerStateMap), 0xd);
}
void LoadMaps(const uint8_t *in)
{
    std::memcpy(GameAddress<uint8_t>(ds::kRenderStateMap), in, 0x59 * 2);
    std::memcpy(GameAddress<uint8_t>(ds::kStageStateMap), in + 0x59 * 2, 0x12);
    std::memcpy(GameAddress<uint8_t>(ds::kSamplerStateMap), in + 0x59 * 2 + 0x12, 0xd);
}

void *OurVTable()
{
    alignas(16) static uint8_t scratch[kSize];
    cDeviceStateDX *s = cDeviceStateDXAdapters::Construct(scratch, nullptr, nullptr, nullptr);
    void *vt = *reinterpret_cast<void **>(s);
    s->cDeviceStateDX::~cDeviceStateDX();
    return vt;
}

} // namespace

DIFF_TEST(cDeviceStateDX_Construct, 0x17c490)
{
    gCtx = &ctx;
    std::mt19937 gen(0x17c490);
    void *const ourVt = OurVTable();
    void *const gameVt = GameAddress<void>(cDeviceStateDX::kOriginalVTable);
    uint8_t mapsA[sizeof(gMapsBefore)], mapsB[sizeof(gMapsBefore)], mapsReal[sizeof(gMapsBefore)];
    for (uint32_t round = 0; round < 60; ++round) {
        for (size_t i = 0; i < kSize; ++i) gA[i] = static_cast<uint8_t>(gen());
        std::memcpy(gB, gA, kSize);
        for (uint8_t &m : gMapsBefore) m = static_cast<uint8_t>(gen());
        auto *owner = reinterpret_cast<nGZGraphic4::cDeviceDX *>(uintptr_t(0x0de00000u + gen() % 0x1000 * 4));
        void *deviceEnum = reinterpret_cast<void *>(uintptr_t(gen()));

        LoadMaps(gMapsBefore);
        Fresh(gA);
        Note("result", drawfx::P(Orig<void *>(0x17c490, gA, owner, deviceEnum)));
        SaveMaps(mapsA);
        const drawfx::Log a = drawfx::gLog;

        LoadMaps(gMapsBefore);
        Fresh(gB);
        Note("result", drawfx::P(cDeviceStateDXAdapters::Construct(gB, nullptr, owner, deviceEnum)));
        SaveMaps(mapsB);
        const drawfx::Log b = drawfx::gLog;
        drawfx::ResetAll();

        if (!drawfx::Same(ctx, a, b, "Construct", round)) continue;
        if (Word(gA, 0) != drawfx::P(gameVt) || Word(gB, 0) != drawfx::P(ourVt))
            ctx.Fail("Construct round %u: vtable words %08x / %08x", round, Word(gA, 0), Word(gB, 0));
        if (const size_t at = FirstDifference(); at != kSize)
            ctx.Fail("Construct round %u: byte +0x%x differs (orig %02x, ours %02x)", round, unsigned(at), gA[at], gB[at]);
        if (std::memcmp(mapsA, mapsB, sizeof(mapsA)) != 0) ctx.Fail("Construct round %u: the index maps differ", round);
        if (round == 0) std::memcpy(mapsReal, mapsA, sizeof(mapsReal));
    }
    LoadMaps(mapsReal);
}

namespace {

bool gOursSide = false;
void *gGameCopy[32];
void *gOurCopy[32];

// The deleting destructor's slot in both copies: noted, then the side's own destructor without the delete.
void *__fastcall DeleteStandIn(void *self, void *, uint32_t flags)
{
    drawfx::Note("deleting destructor", flags);
    if (gOursSide)
        static_cast<cDeviceStateDX *>(self)->cDeviceStateDX::~cDeviceStateDX();
    else
        Orig(0x17c950, self, flags & ~1u);
    return self;
}

} // namespace

DIFF_TEST(cDeviceStateDX_Destroy, 0x17c950)
{
    gCtx = &ctx;
    d3dfx::ErrorLog errors(ctx);
    FillPools();
    std::memcpy(gGameCopy, GameAddress<void *>(cDeviceStateDX::kOriginalVTable), sizeof(gGameCopy));
    std::memcpy(gOurCopy, OurVTable(), sizeof(gOurCopy));
    gGameCopy[31] = gOurCopy[31] = reinterpret_cast<void *>(&DeleteStandIn);
    std::mt19937 gen(0x17c950);
    unsigned deleted = 0, released = 0;
    for (uint32_t round = 0; round < 600; ++round) {
        Seed(gen);
        const unsigned mode = round % 3; // 0: Release from 1, 1: Release from above 1, 2: the destructor
        const uint32_t refs = mode == 0 ? 1 : 2 + gen() % 3;
        Obj(gA)->mRefCount = Obj(gB)->mRefCount = refs;
        *reinterpret_cast<void **>(gA) = gGameCopy;
        *reinterpret_cast<void **>(gB) = gOurCopy;

        gOursSide = false;
        Fresh(gA);
        if (mode == 2)
            Orig(0x17c950, gA, 0u);
        else
            Note("release", Orig<uint32_t>(0x17dcb0, gA));
        const drawfx::Log a = drawfx::gLog;
        deleted += d3dfx::Notes(a, "deleting destructor");
        released += d3dfx::Calls(a, kEngine);

        gOursSide = true;
        Fresh(gB);
        if (mode == 2)
            Obj(gB)->cDeviceStateDX::~cDeviceStateDX();
        else
            Note("release", static_cast<cIDeviceState *>(Obj(gB))->Release());
        const drawfx::Log b = drawfx::gLog;
        gOursSide = false;
        drawfx::ResetAll();

        if (!drawfx::Same(ctx, a, b, mode == 0 ? "Release 1" : mode == 1 ? "Release" : "Destructor", round)) continue;
        if (const size_t at = FirstDifference(); at != kSize)
            ctx.Fail("Destroy round %u mode %u: byte +0x%x differs (orig %02x, ours %02x)", round, mode, unsigned(at),
                     gA[at], gB[at]);
    }
    if (!deleted || !released) ctx.Fail("Destroy: a path never ran (deleted %u, held objects released %u)", deleted, released);
}

// ---- the state blocks ----

namespace {

using nGZGraphic4::cStateBlockDX;

// The resource manager the owner hands out (cIDeviceResourceManager: slot 5 Insert, slot 7 Erase). The blocks are
// logged as the words they are: the pool below gives both sides the same addresses.
constexpr drawfx::Slot kMgrSlots[] = {
    {2, Ret::Int, true, "QueryInterface", ""}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    kU, kU, {1, Ret::Int, true, "Insert", "u"}, kU, {1, Ret::Int, true, "Erase", "u"},
};
constexpr drawfx::Iface kMgr{"resource manager", 8, kMgrSlots};
Fake gMgr;

// The blocks' pool (0xfee35c, 0x18-byte blocks, pages of 0x800) in the harness: the game's allocator globals are not
// built here, so the pool's object is, over a region of pages, the same for each side (the same allocations land at the
// same addresses, which makes the blocks comparable as words). A copy of the one in tests/gz_dx_resources.cpp.
constexpr uint32_t kPoolRva = 0xfee35c, kPoolPageBytes = 0x800, kPoolBlockBytes = 0x18, kPoolPages = 8;
uint8_t *gPoolRegion;
unsigned gPoolNext;
uint32_t gPoolSaved[3];
void *gPoolVt[4];
std::vector<uint32_t> gPoolFreed;

uint32_t PW(const void *p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }
RZFixedPage *PoolInitPage()
{
    auto *page = reinterpret_cast<RZFixedPage *>(gPoolRegion + (gPoolNext++ % kPoolPages) * kPoolPageBytes);
    page->next = page;
    page->prev = page;
    page->used = 0;
    uint8_t *base = reinterpret_cast<uint8_t *>(page);
    const uint32_t blocks = (kPoolPageBytes - 0x10) / kPoolBlockBytes;
    for (uint32_t k = 0; k < blocks; ++k)
        *reinterpret_cast<void **>(base + 0x10 + k * kPoolBlockBytes) =
            k + 1 < blocks ? base + 0x10 + (k + 1) * kPoolBlockBytes : nullptr;
    page->freeList = base + 0x10;
    return page;
}
RZFixedPage *__fastcall PoolNewPage(void *, void *) { return PoolInitPage(); }
void __fastcall PoolFreePage(void *, void *, RZFixedPage *page)
{
    gPoolFreed.push_back(uint32_t(reinterpret_cast<uint8_t *>(page) - gPoolRegion));
}
// Slots 0 / 1 (Alloc, Free) are called directly by both sides, never through the vtable: a call here is an error.
void *__fastcall PoolAllocTrap(void *, void *)
{
    drawfx::gErrors.push_back("the block pool's vtable slot 0 was called");
    return nullptr;
}
void __fastcall PoolFreeTrap(void *, void *, void *) { drawfx::gErrors.push_back("the block pool's vtable slot 1 was called"); }

struct BlockPool {
    BlockPool()
    {
        gPoolVt[0] = reinterpret_cast<void *>(&PoolAllocTrap);
        gPoolVt[1] = reinterpret_cast<void *>(&PoolFreeTrap);
        gPoolVt[2] = reinterpret_cast<void *>(&PoolNewPage);
        gPoolVt[3] = reinterpret_cast<void *>(&PoolFreePage);
        gPoolRegion = static_cast<uint8_t *>(
            VirtualAlloc(nullptr, kPoolPages * kPoolPageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        std::memcpy(gPoolSaved, GameAddress<void>(kPoolRva), sizeof(gPoolSaved));
    }
    ~BlockPool()
    {
        std::memcpy(GameAddress<void>(kPoolRva), gPoolSaved, sizeof(gPoolSaved));
        VirtualFree(gPoolRegion, 0, MEM_RELEASE);
    }
    // A fresh pool (one page) at the same addresses for each side.
    static void Reset()
    {
        gPoolNext = 0;
        gPoolFreed.clear();
        std::memset(gPoolRegion, 0xcd, kPoolPages * kPoolPageBytes);
        uint32_t *words = GameAddress<uint32_t>(kPoolRva);
        words[0] = PW(gPoolVt);
        words[1] = PW(PoolInitPage());
        words[2] = 0;
    }
    // The pool's rings and use counts after a side.
    static void Note()
    {
        const uint32_t *words = GameAddress<uint32_t>(kPoolRva);
        std::vector<uint32_t> w{words[1], words[2], gPoolNext};
        for (unsigned i = 0; i < gPoolNext && i < kPoolPages; ++i) {
            const auto *page = reinterpret_cast<const RZFixedPage *>(gPoolRegion + i * kPoolPageBytes);
            w.insert(w.end(), {PW(page->next), PW(page->prev), PW(page->freeList), page->used});
        }
        w.insert(w.end(), gPoolFreed.begin(), gPoolFreed.end());
        drawfx::Call c;
        c.note = "pool";
        c.words = std::move(w);
        drawfx::gLog.push_back(std::move(c));
    }
};

// A block the test holds a reference to (the one CreateStateBlock handed out; several entries can name one block),
// and the references it took beyond that.
struct BlockRef {
    cStateBlockDX *block;
    bool dropped;
    unsigned extra;
};

uint32_t BlockWord(const cStateBlockDX *b, size_t offset) { return Word(reinterpret_cast<const uint8_t *>(b), offset); }
bool BlockEmpty(const cStateBlockDX *b) { return BlockWord(b, 0xc) == BlockWord(b, 0x10); } // released: no record
bool BlockOwned(const cStateBlockDX *b) { return BlockWord(b, 8) != 0; }

// The block as the test compares it: address, count, whether the owner is the cache, the record's bytes.
void SnapBlock(std::vector<uint32_t> &out, const uint8_t *raw, const cStateBlockDX *b)
{
    const uint32_t begin = BlockWord(b, 0xc), end = BlockWord(b, 0x10);
    out.insert(out.end(), {PW(b), BlockWord(b, 4), BlockWord(b, 8) == PW(raw) ? 1u : BlockWord(b, 8), end - begin});
    const auto *bytes = reinterpret_cast<const uint8_t *>(uintptr_t(begin));
    for (uint32_t k = 0; k < end - begin; ++k) out.push_back(bytes[k]);
}

enum BlockOp : unsigned { kBCreate, kBApply, kBSetter, kBQuery, kBAddRef, kBDrop, kBDetach, kBGetter, kBOps };
constexpr Op kSetters[] = {kSetTexture, kSetVertexShader, kSetPixelShader, kSetVertexDeclaration, kSetStreamSource,
                           kSetIndices, kFlush, kSetRenderState, kSetRenderStates, kSetStageState, kSetStageStates,
                           kSetSamplerStates, kSetTextureTransform, kSetMaterial, kApplyCommand, kResetTexCoord,
                           kRestoreTexCoord, kSetDefaults, kReset};

struct BlockSeen {
    unsigned made = 0, dup = 0, group[8] = {}, first98 = 0, switched98 = 0, same98 = 0, first9c = 0, switched9c = 0,
             same9c = 0, applied = 0, detached = 0, freed = 0, qiYes = 0, qiNo = 0, queued = 0, restoreCalls = 0;
};

// One block step on one side. Arguments from r; `seen` counts on the original's side.
void RunBlockStep(unsigned op, const uint32_t *r, bool ours, std::vector<BlockRef> &refs, BlockSeen *seen)
{
    uint8_t *raw = ours ? gB : gA;
    cDeviceStateDX *s = Obj(raw);
    nGZGraphic4::cIDeviceState *i = s;
    // The entries a step may use: not dropped, and (for most) with a record.
    auto pick = [&](bool needRecord, bool needOwner = false) -> BlockRef * {
        std::vector<BlockRef *> live;
        for (BlockRef &e : refs)
            if (!e.dropped && (!needRecord || !BlockEmpty(e.block)) && (!needOwner || BlockOwned(e.block))) live.push_back(&e);
        return live.empty() ? nullptr : live[r[0] % live.size()];
    };
    switch (op) {
    case kBCreate: {
        static const uint32_t kGroups[] = {3, 3, 3, 1, 2, 1, 2, 0, 5, 10, 0x17, 0x7f};
        const uint32_t groups = kGroups[r[1] % 12];
        uint32_t out = 0xdeadbeef;
        const uint32_t address = PW(&out);
        const uint32_t result = ours ? i->CreateStateBlock(groups, address) : Orig<uint32_t>(0x17cc00, raw, groups, address);
        Note("create", result);
        drawfx::Note("created", out);
        if (!ours) {
            ++seen->made;
            seen->dup += result == 0;
            seen->group[groups & 7] += 1;
        }
        refs.push_back({reinterpret_cast<cStateBlockDX *>(uintptr_t(out)), false, 0});
        break;
    }
    case kBApply: {
        BlockRef *e = pick(true);
        if (!e) break;
        cStateBlockDX *b = e->block;
        const uint8_t flags = *reinterpret_cast<const uint8_t *>(uintptr_t(BlockWord(b, 0xc)) + 2);
        if (!ours) {
            ++seen->applied;
            const uint32_t h98 = Word(raw, 0x98), h9c = Word(raw, 0x9c);
            if (flags & 1) (h98 == 0 ? seen->first98 : h98 == PW(b) ? seen->same98 : seen->switched98) += 1;
            if (flags & 2) (h9c == 0 ? seen->first9c : h9c == PW(b) ? seen->same9c : seen->switched9c) += 1;
        }
        ours ? i->ApplyStateBlock(PW(b)) : Orig(0x17fc00, raw, PW(b));
        break;
    }
    case kBSetter: Run(kSetters[r[0] % (sizeof(kSetters) / sizeof(kSetters[0]))], r + 1, ours); break;
    case kBQuery: {
        BlockRef *e = pick(false);
        if (!e) break;
        static const uint32_t kIds[] = {1, 0xc9714531, 0xda02e09f, 0x9b81aed2, 0};
        const uint32_t iid = kIds[r[1] % 5];
        void *out = reinterpret_cast<void *>(uintptr_t(0x77777777));
        void **outArg = r[2] % 5 == 0 ? nullptr : &out;
        const uint32_t result =
            (ours ? uint32_t(e->block->QueryInterface(iid, outArg)) : Orig<uint32_t>(0x17dc80, e->block, iid, outArg)) & 0xff;
        Note("query", result);
        drawfx::Note("query out", outArg ? PW(out) : 0);
        if (result) ++e->extra;
        if (!ours) ++(result ? seen->qiYes : seen->qiNo);
        break;
    }
    case kBAddRef: {
        BlockRef *e = pick(false);
        if (!e) break;
        Note("addref", ours ? e->block->AddRef() : Orig<uint32_t>(0x148fb0, e->block));
        ++e->extra;
        break;
    }
    case kBDrop: {
        BlockRef *e = pick(false);
        if (!e) break;
        if (e->extra && r[1] % 2) {
            --e->extra;
        } else {
            e->dropped = true;
        }
        const uint32_t result = ours ? e->block->Release() : Orig<uint32_t>(0x153f70, e->block);
        Note("release", result);
        if (!ours) seen->freed += result == 0;
        break;
    }
    case kBDetach: {
        // Not a block the cache holds (its record is read again), and one with an owner.
        BlockRef *e = pick(true, true);
        if (!e || BlockWord(e->block, 8) == 0 || Word(raw, 0x98) == PW(e->block) || Word(raw, 0x9c) == PW(e->block)) break;
        ours ? e->block->ReleaseResource() : Orig(0x181660, e->block);
        if (!ours) ++seen->detached;
        break;
    }
    case kBGetter: {
        BlockRef *e = pick(false);
        if (!e) break;
        cStateBlockDX *b = e->block;
        switch (r[1] % 6) {
        case 0:
            if (!BlockOwned(b)) break;
            Note("device", drawfx::Norm(ours ? PW(b->GetDevice()) : Orig<uint32_t>(0x17da70, b)));
            Note("device const", drawfx::Norm(ours ? PW(b->GetDeviceConst()) : Orig<uint32_t>(0x17da70, b)));
            break;
        case 1: Note("created", (ours ? uint32_t(b->IsCreated()) : Orig<uint32_t>(0x17da80, b)) & 0xff); break;
        case 2: Note("memory", ours ? b->GetMemorySize() : Orig<uint32_t>(0x1816c0, b)); break;
        case 3:
            Note("survives reset", (ours ? uint32_t(b->SurvivesReset()) : Orig<uint32_t>(0x147480, b)) & 0xff);
            Note("survives mode", (ours ? uint32_t(b->SurvivesModeChange()) : Orig<uint32_t>(0x147480, b)) & 0xff);
            break;
        default:
            if (BlockEmpty(b)) break;
            Note("groups", (ours ? uint32_t(b->GetGroups()) : Orig<uint32_t>(0x17cde0, b)) & 0xff);
            break;
        }
        break;
    }
    default: break;
    }
}

// Seeds the object for the block tests, the same on both sides: no held blocks (the fakes Seed leaves would be read
// as blocks), a change record inside the tables (the restore walks the tables of its bits: a bit beyond them reads the
// image's neighbours), and, in two cases of three, defaults equal to the wanted state so that blocks are small and
// repeat.
void BlockSeed(std::mt19937 &gen, unsigned mode, bool forceSame = false)
{
    const bool sameBuffer = gen() % 4 == 0 || forceSame;
    for (uint8_t *raw : {gA, gB}) {
        cDeviceStateDX *s = Obj(raw);
        s->mStateBlock98 = s->mStateBlock9c = nullptr;
        s->mChanges.renderStates[2] &= 0x1ffffff;
        s->mChanges.stages &= 0xff;
        for (uint32_t &w : s->mChanges.stageStates) w &= 0x3ffff;
        for (uint16_t &w : s->mChanges.samplerStates) w &= 0x1fff;
        // Lane OV: in a quarter of the cases every stream is the one buffer at one stride, so that the streams a block
        // records differ from the wanted ones by the offset alone (the apply compares the offset too).
        if (sameBuffer)
            for (auto &stream : s->mWanted.streams) {
                stream.buffer = static_cast<IDirect3DVertexBuffer9 *>(static_cast<void *>(&gVb[0]));
                stream.stride = 16;
                stream.offset = 0;
            }
        if (mode) std::memcpy(&s->mDefaults, &s->mWanted, sizeof(s->mDefaults));
    }
}

// The cache's bytes with the list's vector (the library's) left out: +0x9a08..0x9a13.
size_t FirstBlockDifference(const uint8_t *a, const uint8_t *b)
{
    for (size_t i = 4; i < kSize; ++i) {
        if (i >= 0x9a08 && i < 0x9a14) continue;
        if (a[i] != b[i]) return i;
    }
    return kSize;
}

struct BlockSide {
    drawfx::Log log;
    std::vector<std::string> errors;
    std::vector<uint32_t> blocks, after;
    uint8_t snapshot[kSize];
};

} // namespace

DIFF_TEST(cDeviceStateDX_Blocks, 0x17cc00)
{
    gCtx = &ctx;
    d3dfx::ErrorLog errors(ctx);
    BlockPool pool;
    FillPools();
    std::mt19937 gen(0x17fc00);
    BlockSeen seen;
    constexpr unsigned kRounds = 4000;
    for (uint32_t round = 0; round < kRounds; ++round) {
        Seed(gen);
        // Lane OV: directed rounds among the random ones (1: two materials, 2: a stream's offset alone, 3: a block that
        // records no streams after one that does).
        const unsigned directed = round % 10 == 3 ? 1 : round % 20 == 7 ? 2 : round % 20 == 17 ? 3 : 0;
        const bool twoMaterials = directed == 1;
        BlockSeed(gen, directed ? 0 : gen() % 3, directed == 2);
        unsigned steps = 2 + gen() % 9;
        if (directed == 1) steps = 4;
        if (directed >= 2) steps = 5;
        std::vector<unsigned> ops(steps);
        std::vector<std::array<uint32_t, kArgs>> args(steps);
        std::string label = "blocks";
        for (unsigned k = 0; k < steps; ++k) {
            static const unsigned kWeights[] = {kBCreate, kBCreate, kBCreate, kBApply, kBApply, kBApply, kBApply, kBSetter,
                                                kBSetter, kBSetter, kBQuery, kBAddRef, kBDrop, kBDetach, kBGetter};
            ops[k] = k == 0 ? kBCreate : kWeights[gen() % (sizeof(kWeights) / sizeof(kWeights[0]))];
            if (k && gen() % 3 == 0) args[k] = args[k - 1];
            else for (uint32_t &a : args[k]) a = gen();
            static const char *const kNames[] = {"Create", "Apply", "Setter", "Query", "AddRef", "Drop", "Detach", "Getter"};
            if (label.size() < 80) label += std::string(" ") + kNames[ops[k]];
        }
        if (twoMaterials) {
            // Lane OV: two blocks of group 1 alone, the second after a different material only: the records have the same
            // size and differ in their last byte (the material's last word), which the list's order must see.
            const unsigned kPlan[4] = {kBSetter, kBCreate, kBSetter, kBCreate};
            const uint32_t kFirst[4] = {13, 0, 13, 0}, kSecond[4] = {1, 3, 2, 3};
            for (unsigned k = 0; k < 4; ++k) {
                ops[k] = kPlan[k];
                args[k] = {};
                args[k][0] = kFirst[k];
                args[k][1] = kSecond[k];
            }
            label = "blocks two materials";
        }
        if (directed >= 2) {
            // Block A of group 2 from the seeded streams; the state then changes (stream 1's offset, or all of it back to
            // the defaults) and block B is made; then A is applied, and B over it.
            const unsigned kPlan[5] = {kBCreate, kBSetter, kBCreate, kBApply, kBApply};
            for (unsigned k = 0; k < 5; ++k) {
                ops[k] = kPlan[k];
                args[k] = {};
            }
            args[0][1] = 4;
            args[2][1] = 4;
            args[4][0] = 1;
            if (directed == 2) {
                args[1][0] = 4; // SetStreamSource
                args[1][1] = 1; // stream 1
                args[1][2] = 0; // the first buffer
                args[1][3] = 1; // stride 16
                args[1][4] = 1; // offset 4
            } else {
                args[1][0] = 17; // SetDefaults
                args[1][1] = 0;  // of the wanted state
            }
            label = directed == 2 ? "blocks stream offset" : "blocks streams back";
        }

        auto side = [&](bool ours) {
            BlockSide out;
            uint8_t *raw = ours ? gB : gA;
            Fresh(raw);
            gMgr.Reset<kMgr>(0xe8000000);
            gOwner.Returns(18, &gMgr);
            BlockPool::Reset();
            std::vector<BlockRef> refs;
            for (unsigned k = 0; k < steps; ++k) RunBlockStep(ops[k], args[k].data(), ours, refs, ours ? nullptr : &seen);
            // What stands: the cache, the blocks it lists and the ones the test holds.
            std::memcpy(out.snapshot, raw, kSize);
            const cDeviceStateDX *s = Obj(raw);
            out.blocks.push_back(uint32_t(s->mList.size()));
            for (const cStateBlockDX *b : s->mList) SnapBlock(out.blocks, raw, b);
            for (const BlockRef &e : refs)
                if (!e.dropped) SnapBlock(out.blocks, raw, e.block);
            BlockPool::Note();
            // Everything released: the test's references, then the cache's.
            for (BlockRef &e : refs) {
                for (; e.extra; --e.extra) ours ? e.block->Release() : Orig<uint32_t>(0x153f70, e.block);
                if (!e.dropped) ours ? e.block->Release() : Orig<uint32_t>(0x153f70, e.block);
            }
            uint32_t none[kArgs] = {};
            Run(kReleaseDeviceObjects, none, ours);
            out.after.push_back(uint32_t(Obj(raw)->mList.size()));
            // The list's storage: the original's came from the image's operator new, ours from the library's.
            auto &list = Obj(raw)->mList;
            if (ours) {
                std::vector<cStateBlockDX *>().swap(list);
            } else {
                uint32_t *vec = reinterpret_cast<uint32_t *>(raw + 0x9a08);
                if (vec[0]) gCtx->Original<void(__cdecl *)(void *, size_t)>(0x824624)(reinterpret_cast<void *>(uintptr_t(vec[0])),
                                                                                     vec[2] - vec[0]);
                vec[0] = vec[1] = vec[2] = 0;
            }
            BlockPool::Note();
            out.log = drawfx::gLog;
            out.errors = drawfx::gErrors;
            return out;
        };
        const BlockSide a = side(false);
        const BlockSide b = side(true);        drawfx::ResetAll();
        std::vector<std::string> errs = a.errors;
        errs.insert(errs.end(), b.errors.begin(), b.errors.end());

        if (!drawfx::Same(ctx, a.log, b.log, label.c_str(), round, errs)) continue;
        if (a.blocks != b.blocks) {
            size_t at = 0;
            while (at < a.blocks.size() && at < b.blocks.size() && a.blocks[at] == b.blocks[at]) ++at;
            ctx.Fail("%s round %u: the blocks differ at word %zu of %zu / %zu (orig %x, ours %x)", label.c_str(), round, at,
                     a.blocks.size(), b.blocks.size(), at < a.blocks.size() ? a.blocks[at] : 0,
                     at < b.blocks.size() ? b.blocks[at] : 0);
            continue;
        }
        if (const size_t at = FirstBlockDifference(a.snapshot, b.snapshot); at != kSize)
            ctx.Fail("%s round %u: object byte +0x%x differs (orig %02x, ours %02x)", label.c_str(), round, unsigned(at),
                     a.snapshot[at], b.snapshot[at]);
        if (a.after != b.after) ctx.Fail("%s round %u: the list after the release differs", label.c_str(), round);
    }
    if (!seen.made || !seen.dup || !seen.applied || !seen.first98 || !seen.switched98 || !seen.same98 || !seen.first9c ||
        !seen.switched9c || !seen.same9c || !seen.detached || !seen.freed || !seen.qiYes || !seen.qiNo || !seen.group[0] ||
        !seen.group[1] || !seen.group[2] || !seen.group[3] || !seen.group[5])
        ctx.Fail("a path never ran: made %u dup %u applied %u first %u/%u switched %u/%u same %u/%u detached %u freed %u "
                 "query %u/%u groups %u %u %u %u %u",
                 seen.made, seen.dup, seen.applied, seen.first98, seen.first9c, seen.switched98, seen.switched9c, seen.same98,
                 seen.same9c, seen.detached, seen.freed, seen.qiYes, seen.qiNo, seen.group[0], seen.group[1], seen.group[2],
                 seen.group[3], seen.group[5]);
}

// ---- SetUpDevice (lane PO) ----

namespace {

// The new device's surfaces, its swap chain, and the graphic system service (the palette).
Fake gSwap, gBack, gDepth, gGraphic;
constexpr drawfx::Slot kGraphicSlots[] = {
    {2, Ret::Int, true, "QueryInterface", ""}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU, kU,
    {0, Ret::Int, true, "GetPalette", ""}, // cIGraphicSystemDX slot 28
    kU,
    {1, Ret::Int, true, "GetErrorString", "u"}, // slot 30: LogD3DError asks the same service for its text
};
static_assert(sizeof(kGraphicSlots) / sizeof(kGraphicSlots[0]) == 31);
constexpr drawfx::Iface kGraphicIface{"graphic system", 31, kGraphicSlots};
PALETTEENTRY gPalette[256];

// What the device, the enumeration and the owner answer in one case.
struct SetUpProgram {
    uint32_t caps[16];      // HasProcessingCap(index & 15): the low byte is the answer, the rest noise
    D3DCAPS9 deviceCaps;    // GetCaps (MaxActiveLights sizes default command 10)
    BOOL autoDepth;         // the present parameters' EnableAutoDepthStencil
    uint32_t width, height; // the back buffer's description
    bool depth;             // GetDepthStencilSurface hands out a surface (or null)
    unsigned providerDepth; // the owner's GetTargetProviderSurface: gEngSurf[k], or null for 3
} gSetUp;

uint32_t HasCap(Fake &, const uint32_t *args) { return gSetUp.caps[args[0] & 15]; }
uint32_t Caps(Fake &, const uint32_t *) { return uint32_t(uintptr_t(&gSetUp.deviceCaps)); }
// The device's getters hand out their object (AddRef'd) whatever the result: the original uses it unchecked.
uint32_t SwapChain(Fake &f, const uint32_t *args)
{
    ++gSwap.refs;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = &gSwap;
    return f.ret[fx_d3d9::dev::GetSwapChain];
}
uint32_t BackBuffer(Fake &f, const uint32_t *args)
{
    ++gBack.refs;
    *reinterpret_cast<void **>(uintptr_t(args[3])) = &gBack;
    return f.ret[fx_d3d9::dev::GetBackBuffer];
}
uint32_t DepthSurface(Fake &f, const uint32_t *args)
{
    if (gSetUp.depth) ++gDepth.refs;
    *reinterpret_cast<void **>(uintptr_t(args[0])) = gSetUp.depth ? &gDepth : nullptr;
    return f.ret[fx_d3d9::dev::GetDepthStencilSurface];
}
// IDirect3DSwapChain9::GetPresentParameters (slot 9) and IDirect3DSurface9::GetDesc (slot 12): written whole, whatever
// the result (the original reads them unchecked).
uint32_t PresentParameters(Fake &f, const uint32_t *args)
{
    auto *out = reinterpret_cast<D3DPRESENT_PARAMETERS *>(uintptr_t(args[0]));
    std::memset(out, 0, sizeof(*out));
    out->BackBufferWidth = gSetUp.width;
    out->EnableAutoDepthStencil = gSetUp.autoDepth;
    return f.ret[9];
}
uint32_t BackBufferDesc(Fake &f, const uint32_t *args)
{
    auto *out = reinterpret_cast<D3DSURFACE_DESC *>(uintptr_t(args[0]));
    std::memset(out, 0, sizeof(*out));
    out->Format = D3DFMT_X8R8G8B8;
    out->Width = gSetUp.width;
    out->Height = gSetUp.height;
    return f.ret[d3dfx::surf::GetDesc];
}

// Fresh, plus the set-up's fakes.
void FreshSetUp(const void *object)
{
    Fresh(object);
    gEnum.On(20, HasCap);
    gEnum.On(23, Caps);
    gDevice.On(fx_d3d9::dev::GetSwapChain, SwapChain);
    gDevice.On(fx_d3d9::dev::GetBackBuffer, BackBuffer);
    gDevice.On(fx_d3d9::dev::GetDepthStencilSurface, DepthSurface);
    gSwap.Reset<fx_d3d9::IDirect3DSwapChain9>(0xd3d00200);
    gSwap.On(9, PresentParameters);
    gBack.Reset<fx_d3d9::IDirect3DSurface9>(0xd3d00201);
    gBack.On(d3dfx::surf::GetDesc, BackBufferDesc);
    gDepth.Reset<fx_d3d9::IDirect3DSurface9>(0xd3d00202);
    gGraphic.Reset<kGraphicIface>(0xe7000002);
    gGraphic.Returns(28, static_cast<const void *>(gPalette));
    gOwner.Returns(39, static_cast<const void *>(&gEngSurf[0]));
    gOwner.Returns(40, gSetUp.providerDepth < 3 ? static_cast<const void *>(&gEngSurf[gSetUp.providerDepth]) : nullptr);
}

// The case: the object as Seed left it, its defaults built or not (and other defaults when built), and what the
// device, the enumeration and the owner answer.
void SeedSetUp(std::mt19937 &gen)
{
    Seed(gen);
    cDeviceStateDX *s = Obj(gB);
    s->mDefaultsBuilt = gen() % 2 ? 0 : static_cast<uint8_t>(1 + gen() % 255);
    std::memcpy(&s->mDefaults, &s->mWanted, sizeof(s->mDefaults));
    for (auto &v : s->mDefaults.renderStates) v = gen() % 2 ? v : gen() % 5;
    s->mDefaults.lights[gen() % 16] = gLights[gen() % 2];
    s->mDefaults.clipping = gen() % 3;
    s->mDefaults.ambient = gen();
    s->mD3D = reinterpret_cast<IDirect3DDevice9 *>(&gNone); // the set-up takes the device it is handed
    std::memcpy(gA + 4, gB + 4, kSize - 4);

    for (uint32_t &c : gSetUp.caps) c = (gen() & 0xffffff00u) | (gen() % 2 ? 0 : 1 + gen() % 255);
    if (gen() % 4 == 0) gSetUp.caps[8] &= 0xffffff00u; // palettes off, with noise above the byte
    std::memset(&gSetUp.deviceCaps, 0, sizeof(gSetUp.deviceCaps));
    gSetUp.deviceCaps.MaxActiveLights = gen() % 3 == 0 ? 16 : gen() % 9;
    const BOOL autoDepth[] = {0, 1, 0x100};
    gSetUp.autoDepth = autoDepth[gen() % 3];
    gSetUp.width = Dimension(gen());
    gSetUp.height = Dimension(gen());
    gSetUp.depth = gen() % 3 != 0;
    gSetUp.providerDepth = gen() % 4;
    gProgram.limit[1] = gen() % 5; // render targets
    gProgram.limit[4] = gen() % 5; // stream frequency: 3 and above set the stream frequencies
    // The stream count is signed: a word above INT_MAX sends none.
    if (gen() % 8 == 0) gProgram.limit[10 + gen() % 2] = 0x80000000u + gen() % 4;
}

struct SetUpSeen {
    unsigned built, rebuilt, palette, noPalette, frequency, noFrequency, depth, noDepth, failures, lights;
};

} // namespace

DIFF_TEST(cDeviceStateDX_SetUp, 0x1807b0)
{
    gCtx = &ctx;
    d3dfx::ErrorLog errors(ctx);
    errors.Answer(0x85522803, 0xc55289ba, gGraphic);
    FillPools();
    std::mt19937 gen(0x1807b0);
    SetUpSeen seen{};
    for (uint32_t round = 0; round < 1500; ++round) {
        SeedSetUp(gen);
        const bool built = Obj(gA)->mDefaultsBuilt != 0;
        const bool palette = (gSetUp.caps[8] & 0xff) != 0;
        const bool frequency = gProgram.limit[4] >= 3;
        (built ? seen.rebuilt : seen.built) += 1;
        (palette ? seen.palette : seen.noPalette) += 1;
        (frequency ? seen.frequency : seen.noFrequency) += 1;
        (gSetUp.depth ? seen.depth : seen.noDepth) += 1;
        if (!built && gSetUp.deviceCaps.MaxActiveLights) ++seen.lights;

        FreshSetUp(gA);
        Orig(0x1807b0, gA, reinterpret_cast<IDirect3DDevice9 *>(&gDevice));
        const drawfx::Log a = drawfx::gLog;
        std::vector<std::string> errs = drawfx::gErrors;
        seen.failures += d3dfx::Notes(a, "LogD3DError");
        const int32_t refsA[] = {gSwap.refs, gBack.refs, gDepth.refs, gGraphic.refs, gEngSurf[0].refs, gSurf[0].refs};

        FreshSetUp(gB);
        Obj(gB)->SetUpDevice(reinterpret_cast<IDirect3DDevice9 *>(&gDevice));
        const drawfx::Log b = drawfx::gLog;
        errs.insert(errs.end(), drawfx::gErrors.begin(), drawfx::gErrors.end());
        const int32_t refsB[] = {gSwap.refs, gBack.refs, gDepth.refs, gGraphic.refs, gEngSurf[0].refs, gSurf[0].refs};
        drawfx::ResetAll();

        const char *label = built ? "set-up (defaults built)" : "set-up (first)";
        if (!drawfx::Same(ctx, a, b, label, round, errs)) continue;
        for (unsigned k = 0; k < 6; ++k)
            if (refsA[k] != refsB[k])
                ctx.Fail("%s round %u: reference count %u is %d (orig) / %d (ours)", label, round, k, refsA[k], refsB[k]);
        if (const size_t at = FirstDifference(); at != kSize)
            ctx.Fail("%s round %u: object byte +0x%x differs (orig %02x, ours %02x)", label, round, unsigned(at), gA[at],
                     gB[at]);
    }
    if (seen.built < 100 || seen.rebuilt < 100 || seen.palette < 100 || seen.noPalette < 100 || seen.frequency < 100 ||
        seen.noFrequency < 100 || seen.depth < 100 || seen.noDepth < 100 || seen.failures == 0 || seen.lights < 100)
        ctx.Fail("a path ran too rarely: first %u / built %u, palette %u / none %u, frequency %u / none %u, depth %u / "
                 "none %u, failures %u, default lights %u",
                 seen.built, seen.rebuilt, seen.palette, seen.noPalette, seen.frequency, seen.noFrequency, seen.depth,
                 seen.noDepth, seen.failures, seen.lights);
}

// ---- Validation slots 27-29 and their validators (lane PQ) ----

namespace {

// The engine's held buffers (the ones slots 27 and 28 keep at +0x9a14 and +0x9a18, and the ones the framework makes).
constexpr drawfx::Slot kHeldSlots[] = {
    {2, Ret::Int, true, "QueryInterface", ""}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    kU, kU, kU,
    {0, Ret::Int, true, "IsUsable", ""}, // 6
    kU, kU, kU, kU, kU, kU, kU, kU,
    {3, Ret::Int, true, "Lock", "uuu"}, // 15
    {0, Ret::Int, true, "Unlock", ""},  // 16
    kU, kU, kU,
    {3, Ret::Int, true, "Create", "iuu"}, // 20: (device, size, mode)
};
static_assert(sizeof(kHeldSlots) / sizeof(kHeldSlots[0]) == 21);
constexpr drawfx::Iface kHeldIface{"held buffer", 21, kHeldSlots};
// The framework's COM object: GetClassObject(class id, interface id, out).
constexpr drawfx::Slot kComSlots[] = {
    {2, Ret::Int, true, "QueryInterface", ""}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "GetClassObject", "uui"},
};
constexpr drawfx::Iface kComIface{"framework", 4, kComSlots};

constexpr uint32_t kVertexHeldClass = 0x3bffec52, kIndexHeldClass = 0x9bfff0ea;

// What the fakes answer in one validation case.
struct ValProgram {
    D3DCAPS9 caps[3];                 // GetCurrentCaps, GetCaps(false), GetCaps(true)
    bool noCaps;                      // GetCaps answers null
    uint32_t texType[2];              // gTex[k]: GetType
    uint32_t texWidth[2], texHeight[2];
    bool supported[2], supportedNull; // the texture and cube format checks (stood in), per texture and for none
    uint32_t vbUsage[2], ibUsage[2];  // the buffers' descriptions
    uint32_t vbResult[2], ibResult[2]; // and their results
    uint32_t levelResult[2];           // gTex[k]: GetLevelDesc
    uint32_t feature;                 // HasFeature: the low byte is the answer, the rest noise
    uint32_t hardwareCap;             // HasProcessingCap(0), the same
    bool usable[4], creates[4];       // gHB[k]: IsUsable, Create
    bool surfValid[3];                // gEngSurf[k]: slot 6
} gVal;
Fake gHB[4], gCom;
uint32_t gLockData[4][0x120];

uint32_t ValCurrentCaps(Fake &, const uint32_t *) { return drawfx::P(&gVal.caps[0]); }
uint32_t ValCaps(Fake &, const uint32_t *a)
{
    return gVal.noCaps ? 0 : drawfx::P(&gVal.caps[(a[0] & 0xff) ? 2 : 1]);
}
uint32_t ValFeature(Fake &, const uint32_t *) { return gVal.feature; }
uint32_t ValHardware(Fake &, const uint32_t *) { return gVal.hardwareCap; }
uint32_t SurfaceValid(Fake &f, const uint32_t *) { return gVal.surfValid[f.value[0]]; }
uint32_t TexType(Fake &f, const uint32_t *) { return gVal.texType[f.value[0]]; }
// QueryInterface for the texture and cube texture interfaces (by id): answers itself when the type fits.
uint32_t TexQuery(Fake &f, const uint32_t *a)
{
    const void *iid = reinterpret_cast<const void *>(uintptr_t(a[0]));
    void **out = reinterpret_cast<void **>(uintptr_t(a[1]));
    const uint32_t type = gVal.texType[f.value[0]];
    const bool texture = std::memcmp(iid, GameAddress<void>(0xd9c8b8), 16) == 0;
    const bool cube = std::memcmp(iid, GameAddress<void>(0xd9c8c8), 16) == 0;
    if ((texture && type == D3DRTYPE_TEXTURE) || (cube && type == D3DRTYPE_CUBETEXTURE)) {
        ++f.refs;
        *out = &f;
        return 0;
    }
    *out = nullptr;
    return 0x80004002;
}
// GetLevelDesc: written whole, whatever the result (the original reads it unchecked).
uint32_t LevelDesc(Fake &f, const uint32_t *a)
{
    auto *out = reinterpret_cast<D3DSURFACE_DESC *>(uintptr_t(a[1]));
    std::memset(out, 0, sizeof(*out));
    out->Width = gVal.texWidth[f.value[0]];
    out->Height = gVal.texHeight[f.value[0]];
    return gVal.levelResult[f.value[0]];
}
uint32_t VertexDesc(Fake &f, const uint32_t *a)
{
    auto *out = reinterpret_cast<D3DVERTEXBUFFER_DESC *>(uintptr_t(a[0]));
    std::memset(out, 0, sizeof(*out));
    out->Usage = gVal.vbUsage[f.value[0]];
    return gVal.vbResult[f.value[0]];
}
uint32_t IndexDesc(Fake &f, const uint32_t *a)
{
    auto *out = reinterpret_cast<D3DINDEXBUFFER_DESC *>(uintptr_t(a[0]));
    std::memset(out, 0, sizeof(*out));
    out->Usage = gVal.ibUsage[f.value[0]];
    return gVal.ibResult[f.value[0]];
}
uint32_t HeldUsable(Fake &f, const uint32_t *) { return gVal.usable[f.value[0]]; }
uint32_t HeldCreate(Fake &f, const uint32_t *) { return gVal.creates[f.value[0]]; }
uint32_t HeldLock(Fake &f, const uint32_t *) { return drawfx::P(gLockData[f.value[0]]); }
uint32_t ClassObject(Fake &, const uint32_t *a)
{
    void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
    const unsigned k = a[0] == kVertexHeldClass ? 2 : a[0] == kIndexHeldClass ? 3 : 4;
    if (k == 4) {
        if (out) *out = nullptr;
        return 0x80004002;
    }
    ++gHB[k].refs;
    *out = &gHB[k];
    return 0;
}
void *__cdecl StandFramework() { return &gCom; }
// The texture and cube format checks (0x18c7f0, 0x18df30; ours in gz/TextureSupportDX.h, tested by gz_texture_support.cpp): the case's answers.
bool SupportedFor(const char *label, void *texture)
{
    drawfx::Note(label, drawfx::Norm(drawfx::P(texture)));
    for (unsigned k = 0; k < 2; ++k)
        if (texture == &gTex[k]) return gVal.supported[k];
    return gVal.supportedNull;
}
bool __cdecl StandTextureSupported(void *texture) { return SupportedFor("texture format", texture); }
bool __cdecl StandCubeSupported(void *texture) { return SupportedFor("cube format", texture); }

// The framework's report (cRZStd slot 2) as a note of its fields; the argument only where the format takes one.
void __cdecl ReportHook(cRZStd *self, const char *category, uint32_t number, const char *format, void *value)
{
    const bool argument = format && std::strchr(format, '%');
    drawfx::Note("report", {self->mFlags, self->mFirst, self->mSecond, drawfx::P(category), number, drawfx::P(format),
                            argument ? drawfx::Norm(drawfx::P(value)) : 0u});
}

void FreshValidate(const void *object)
{
    Fresh(object);
    gEnum.On(20, ValHardware);
    gEnum.On(23, ValCaps);
    gEnum.On(24, ValCurrentCaps);
    gEnum.On(25, ValFeature);
    for (uint32_t k = 0; k < 2; ++k) {
        gTex[k].value[0] = k;
        gTex[k].On(0, TexQuery);
        gTex[k].On(10, TexType);
        gTex[k].On(d3dfx::tex::GetLevelDesc, LevelDesc);
        gVb[k].value[0] = k;
        gVb[k].On(13, VertexDesc);
        gIb[k].value[0] = k;
        gIb[k].On(13, IndexDesc);
    }
    for (uint32_t k = 0; k < 3; ++k) {
        gEngSurf[k].value[0] = k;
        gEngSurf[k].On(6, SurfaceValid);
    }
    for (uint32_t k = 0; k < 4; ++k) {
        gHB[k].Reset<kHeldIface>(0xe0000100 + k);
        gHB[k].value[0] = k;
        gHB[k].On(6, HeldUsable);
        gHB[k].On(15, HeldLock);
        gHB[k].On(20, HeldCreate);
        if (k % 2 == 0)
            gHB[k].AddFace(kVertexBufferId, gVb[k / 2]);
        else
            gHB[k].AddFace(kIndexBufferId, gIb[k / 2]);
        for (uint32_t i = 0; i < 0x120; ++i) gLockData[k][i] = 0xa5a50000u + k * 0x1000 + i;
    }
    gCom.Reset<kComIface>(0xe7000010);
    gCom.On(3, ClassObject);
}

// A set of caps: all ones, random, all ones but a few bits, or zero but a few bits (the words the validators read),
// and the small counts.
constexpr uint32_t kMaskOffsets[] = {0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x50, 0x88,
                                     0x90, 0x9c, 0xd4};
uint32_t CapBit(std::mt19937 &gen) { return gen() % 2 ? gen() % 12 : gen() % 32; }
void MakeCaps(std::mt19937 &gen, D3DCAPS9 &caps, bool generous)
{
    auto *w = reinterpret_cast<uint8_t *>(&caps);
    std::memset(&caps, 0, sizeof(caps));
    unsigned style = gen() % 4;
    if (generous) style = gen() % 3 == 0 ? 2 : 0;
    for (const uint32_t offset : kMaskOffsets) {
        uint32_t v = style == 0 || style == 2 ? ~0u : style == 1 ? gen() : 0u;
        if (style == 1 && gen() % 2) v &= gen();
        std::memcpy(w + offset, &v, 4);
    }
    const unsigned flips = 1 + gen() % 3;
    if (style >= 2)
        for (unsigned k = 0; k < flips; ++k) {
            uint32_t v;
            uint8_t *at = w + kMaskOffsets[gen() % std::size(kMaskOffsets)];
            std::memcpy(&v, at, 4);
            v = style == 2 ? v & ~(1u << CapBit(gen)) : v | (1u << CapBit(gen));
            std::memcpy(at, &v, 4);
        }
    const bool big = style < 2;
    caps.MaxTextureBlendStages = big ? 8 + gen() % 2 * 8 : gen() % 10;
    caps.MaxSimultaneousTextures = big ? 16 : gen() % 18;
    caps.MaxStreams = big ? 16 : gen() % 18;
    caps.MaxStreamStride = big ? 255 : gen() % 40;
    caps.MaxAnisotropy = big ? 16 : gen() % 20;
    caps.NumSimultaneousRTs = big ? 4 : gen() % 6;
    if (gen() % 8 == 0) caps.NumSimultaneousRTs = gen() % 6;
    // The alpha test check masks bits 1-6 of the comparison caps: only "never" (bit 0) or bit 7 alone must fail it.
    if (gen() % 12 == 0) {
        static const uint32_t kAlphaOnly[] = {0, 1, 0x80, 0x81, 0x7e, 0x02};
        caps.AlphaCmpCaps = kAlphaOnly[gen() % std::size(kAlphaOnly)];
    }
}

// The case: a seeded object and what the validation fakes answer. `biased`: the state is made mostly valid under the
// caps (so that slot 28 reaches the device check and the trial draw).
void SeedValidate(std::mt19937 &gen, bool biased)
{
    Seed(gen);
    cDeviceStateDX *s = Obj(gB);
    std::memset(&gVal, 0, sizeof(gVal));
    for (D3DCAPS9 &caps : gVal.caps) MakeCaps(gen, caps, biased);
    if (gen() % 3) gVal.caps[1] = gVal.caps[gen() % 2 ? 0 : 2];
    gVal.noCaps = gen() % 12 == 0;
    for (unsigned k = 0; k < 2; ++k) {
        static const uint32_t kTypes[] = {D3DRTYPE_TEXTURE, D3DRTYPE_TEXTURE, D3DRTYPE_TEXTURE, D3DRTYPE_TEXTURE,
                                          D3DRTYPE_CUBETEXTURE, D3DRTYPE_VOLUMETEXTURE, D3DRTYPE_SURFACE,
                                          D3DRTYPE_VERTEXBUFFER};
        gVal.texType[k] = kTypes[gen() % (biased ? 6 : 8)];
        static const uint32_t kDims[] = {64, 256, 1, 100, 0, 3, 128, 96};
        gVal.texWidth[k] = kDims[gen() % (biased ? 5 : 8)];
        gVal.texHeight[k] = kDims[gen() % (biased ? 5 : 8)];
        gVal.supported[k] = gen() % 3 != 0;
        gVal.levelResult[k] = gen() % 8 ? 0 : d3dfx::kResults[1 + gen() % 3];
        static const uint32_t kUsage[] = {0, 0x10, 0x18, 0x8, 0x10, 0x10};
        gVal.vbUsage[k] = kUsage[gen() % 6];
        gVal.ibUsage[k] = kUsage[gen() % 6];
        gVal.vbResult[k] = gen() % 8 ? 0 : d3dfx::kResults[1 + gen() % 3];
        gVal.ibResult[k] = gen() % 8 ? 0 : d3dfx::kResults[1 + gen() % 3];
    }
    gVal.supportedNull = gen() % 2;
    gVal.feature = (gen() & 0xffffff00u) | (gen() % 2);
    gVal.hardwareCap = (gen() & 0xffffff00u) | (gen() % 2);
    for (unsigned k = 0; k < 4; ++k) {
        gVal.usable[k] = gen() % 3 != 0;
        gVal.creates[k] = gen() % 6 != 0;
    }
    for (bool &v : gVal.surfValid) v = gen() % 4 != 0;
    auto held = [&](unsigned i) { return gen() % 4 == 0 ? nullptr : reinterpret_cast<cIGZUnknown *>(&gHB[i]); };
    s->mHeld9a14 = held(0);
    s->mHeld9a18 = held(1);
    if (biased) {
        ds::DeviceState &w = s->mWanted;
        if (gen() % 10 < 7) w.vertexShader = nullptr;
        w.renderStates[11] = gen() % 10 < 8 ? (gen() % 2 ? 0 : 0x100) : gen() % 3;
        const uint32_t used = gen() % 9;
        for (uint32_t i = 0; i < 8; ++i) {
            const bool on = i < used;
            w.stageStates[i][0] = on ? 2 + gen() % 3 : 1;
            w.stageStates[i][4] = on ? 2 + gen() % 3 : 1;
            if (gen() % 12 == 0) w.stageStates[i][gen() % 2 ? 0 : 4] = gen() % 3;
        }
        if (used && gen() % 10) w.stageStates[used - 1][8] = gen() % 2 ? 0 : 1 + gen() % 4;
        for (auto &sampler : w.samplerStates) {
            for (unsigned k = 0; k < 3; ++k) sampler[k] = gen() % 12 ? 1 + gen() % 4 : gen() % 7;
            sampler[4] = gen() % 12 ? 1 + gen() % 3 : gen() % 5;
            sampler[5] = gen() % 12 ? 1 + gen() % 3 : gen() % 5;
            sampler[9] = 1u << (gen() % 5);
            sampler[6] = gen() % 12 ? gen() % 3 : gen() % 5;
        }
        for (auto &t : w.textures)
            if (gen() % 10 < 7) t = nullptr;
        if (gen() % 2) w.pixelShader = nullptr;
        for (auto &stream : w.streams) {
            if (gen() % 100 >= 20) stream.buffer = nullptr;
            stream.stride = gen() % 2 * 16;
            stream.offset = gen() % 8 == 0 ? 4 : 0;
        }
    }
    std::memcpy(gA + 4, gB + 4, kSize - 4);
}

// One side's run, as the log (with the result as its last note), the reference counts and the lock buffers.
struct Outcome {
    drawfx::Log log;
    std::vector<std::string> errs;
    std::vector<int32_t> refs;
    std::vector<uint32_t> lock;
};
Outcome Capture(uint32_t result)
{
    drawfx::Note("result", result);
    Outcome r;
    r.log = drawfx::gLog;
    r.errs = drawfx::gErrors;
    for (Fake &f : gTex) r.refs.push_back(f.refs);
    for (Fake &f : gHB) r.refs.push_back(f.refs);
    for (Fake &f : gVb) r.refs.push_back(f.refs);
    for (Fake &f : gIb) r.refs.push_back(f.refs);
    for (Fake &f : gEngSurf) r.refs.push_back(f.refs);
    r.refs.push_back(gCom.refs);
    for (auto &buffer : gLockData) r.lock.insert(r.lock.end(), std::begin(buffer), std::end(buffer));
    return r;
}
bool Check(difftest::Context &ctx, const char *label, uint32_t round, const Outcome &a, const Outcome &b)
{
    std::vector<std::string> errs = a.errs;
    errs.insert(errs.end(), b.errs.begin(), b.errs.end());
    if (!drawfx::Same(ctx, a.log, b.log, label, round, errs)) return false;
    bool ok = true;
    if (a.refs != b.refs) ctx.Fail("%s round %u: the reference counts differ", label, round), ok = false;
    if (a.lock != b.lock) ctx.Fail("%s round %u: the held buffers' memory differs", label, round), ok = false;
    if (const size_t at = FirstDifference(); at != kSize)
        ctx.Fail("%s round %u: object byte +0x%x differs (orig %02x, ours %02x)", label, round, unsigned(at), gA[at], gB[at]),
            ok = false;
    return ok;
}

unsigned CountSlot(const drawfx::Log &log, const drawfx::Iface &iface, unsigned slot)
{
    unsigned n = 0;
    for (const drawfx::Call &c : log) n += c.iface == &iface && c.slot == slot;
    return n;
}
unsigned CountReports(const drawfx::Log &log, uint32_t line)
{
    unsigned n = 0;
    for (const drawfx::Call &c : log) n += !c.iface && std::strcmp(c.note, "report") == 0 && c.words[2] == line;
    return n;
}
unsigned CountNotes(const drawfx::Log &log, const char *label)
{
    unsigned n = 0;
    for (const drawfx::Call &c : log) n += !c.iface && std::strcmp(c.note, label) == 0;
    return n;
}

// The stand-ins every validation case runs under.
struct ValidateStage {
    shape::StandIn framework, texture, cube;
    explicit ValidateStage(difftest::Context &ctx)
        : framework(0x1ae18d, &::GetDirectorFrameworkObject, &StandFramework),
          texture(0x18c7f0, &nGZGraphic4::TextureFilterSupported, &StandTextureSupported),
          cube(0x18df30, &nGZGraphic4::CubeTextureFilterSupported, &StandCubeSupported)
    {
        *ctx.image.At<void *>(cRZStd::kVTable + 8) = reinterpret_cast<void *>(&ReportHook);
    }
};

// A command for slot 29 of a type (byte +4), the fields drawn from small pools.
uint8_t SmallByte(std::mt19937 &gen)
{
    switch (gen() % 8) {
    case 0: case 1: case 2: return 0;
    case 3: case 4: return static_cast<uint8_t>(gen() % 5);
    case 5: case 6: return static_cast<uint8_t>(gen() % 0x1c);
    default: return static_cast<uint8_t>(gen());
    }
}
void MakeCommand(std::mt19937 &gen, uint8_t *c, uint32_t type)
{
    std::memset(c, 0, 0x40);
    for (unsigned i = 8; i < 0x40; ++i) c[i] = SmallByte(gen);
    c[4] = static_cast<uint8_t>(type);
    auto dword = [&](unsigned at, uint32_t v) { std::memcpy(c + at, &v, 4); };
    switch (type) {
    case 0: // render targets: four surfaces and the depth surface
        for (unsigned i = 0; i < 5; ++i) {
            const uint32_t k = gen() % 4;
            cIGZUnknown *p = k == 3 || (i == 0 && gen() % 6 == 0) ? nullptr : reinterpret_cast<cIGZUnknown *>(&gEngSurf[k]);
            std::memcpy(c + 8 + 4 * i, &p, 4);
        }
        break;
    case 4: c[8] = static_cast<uint8_t>(gen() % 4); break;
    case 7:
        if (gen() % 2) std::memset(c + 8, 0xf, 4);
        if (gen() % 3 == 0) c[8 + gen() % 4] = static_cast<uint8_t>(gen() % 16);
        break;
    case 8:
        for (unsigned i = 8; i < 12; ++i) c[i] = static_cast<uint8_t>(gen() % 2);
        break;
    case 0xe: {
        static const uint32_t k[] = {0x100, 0x101, 0x102, 0x103, 0xff, 0, 1, 7, 8, 9, 0x10000};
        dword(0xc, k[gen() % std::size(k)]);
        break;
    }
    case 0x11:
        c[8] = static_cast<uint8_t>(gen() % 2);
        c[9] = static_cast<uint8_t>(gen() % 12);
        c[0xa] = static_cast<uint8_t>(gen() % 12);
        c[0xb] = static_cast<uint8_t>(gen() % 2);
        break;
    case 0x13: case 0x15: c[8] = static_cast<uint8_t>(gen() % 9); break;
    case 0x18:
        for (unsigned i = 8; i < 11; ++i) c[i] = static_cast<uint8_t>(gen() % 9);
        break;
    case 0x19:
        c[0xd] = static_cast<uint8_t>(gen() % 24); // the engine's operation tables end at 24 (the original reads on)
        c[0x11] = static_cast<uint8_t>(gen() % 24);
        for (const unsigned at : {0xcu, 0x12u, 0x13u, 0x14u}) c[at] = static_cast<uint8_t>((gen() % 3 ? 0 : gen() % 6 * 0x10) | gen() % 7);
        break;
    case 0x1c:
        for (unsigned i = 0xc; i < 0xf; ++i) c[i] = static_cast<uint8_t>(gen() % 5);
        break;
    case 0x1e: {
        c[0xc] = static_cast<uint8_t>(gen() % 4);
        c[0xd] = static_cast<uint8_t>(gen() % 4);
        dword(0x10, gen() % 20);
        break;
    }
    case 0x1f: {
        static const float k[] = {0.0f, -0.0f, 0.5f, std::numeric_limits<float>::quiet_NaN(), 1e-30f, -2.0f};
        c[0xc] = static_cast<uint8_t>(gen() % 4);
        dword(0x10, std::bit_cast<uint32_t>(k[gen() % std::size(k)]));
        break;
    }
    default: break;
    }
}

} // namespace

DIFF_TEST(cDeviceStateDX_ValidateCommand, 0x1829d0)
{
    gCtx = &ctx;
    d3dfx::ErrorLog errors(ctx);
    ValidateStage stage(ctx);
    FillPools();
    static const uint8_t kValidated[] = {0,    4,    6,    7,    8,    9,    0xe,  0x10, 0x11, 0x12,
                                         0x13, 0x15, 0x18, 0x19, 0x1b, 0x1c, 0x1e, 0x1f, 0x20};
    static const uint8_t kOthers[] = {1, 2, 3, 5, 0xa, 0xb, 0xc, 0xd, 0xf, 0x14, 0x16, 0x17, 0x1a, 0x1d, 0x21, 0x40, 0xff};
    std::mt19937 gen(0x1829d0);
    unsigned passed[0x100] = {}, failed[0x100] = {}, reported[0x100] = {}, nullCaps = 0;
    alignas(16) uint8_t command[0x40];
    for (uint32_t round = 0; round < 40000; ++round) {
        SeedValidate(gen, false);
        const uint32_t type = gen() % 8 ? kValidated[gen() % std::size(kValidated)] : kOthers[gen() % std::size(kOthers)];
        MakeCommand(gen, command, type);
        const uint32_t hardware = gen() % 2, log = gen() % 5 != 0;

        FreshValidate(gA);
        const uint32_t ra = Orig<uint32_t>(0x1829d0, gA, command, hardware, log) & 0xff;
        const Outcome a = Capture(ra);
        FreshValidate(gB);
        const uint32_t rb = static_cast<cIDeviceState *>(Obj(gB))->ValidateCommand(command, hardware != 0, log != 0) ? 1 : 0;
        const Outcome b = Capture(rb);
        drawfx::ResetAll();
        Check(ctx, "ValidateCommand", round, a, b);

        (ra ? passed : failed)[type] += 1;
        reported[type] += CountNotes(a.log, "report") != 0;
        nullCaps += gVal.noCaps;
    }
    for (const uint8_t t : kValidated)
        if (t != 6 && (passed[t] < 100 || failed[t] < 100 || reported[t] < 50))
            ctx.Fail("command type 0x%x ran too rarely: %u passed, %u failed, %u reported", t, passed[t], failed[t], reported[t]);
    if (reported[6] < 50 || nullCaps < 100) ctx.Fail("shade mode reports %u, no caps %u", reported[6], nullCaps);
    for (const uint8_t t : kOthers)
        if (passed[t] < 20) ctx.Fail("command type 0x%x: %u cases", t, passed[t]);
}

DIFF_TEST(cDeviceStateDX_CheckVertexProcessing, 0x181f00)
{
    gCtx = &ctx;
    d3dfx::ErrorLog errors(ctx);
    ValidateStage stage(ctx);
    FillPools();
    std::mt19937 gen(0x181f00);
    unsigned lines[0x100] = {}, processed = 0, made = 0, usedHeld = 0, ok = 0;
    for (uint32_t round = 0; round < 6000; ++round) {
        SeedValidate(gen, false);
        gAnswers.Random(gen, fx_d3d9::IDirect3DDevice9.count, 16);
        gAnswers.ret[fx_d3d9::dev::ProcessVertices] = gen() % 3 == 0 ? d3dfx::kResults[1 + gen() % 3] : 0;
        gAnswers.ret[fx_d3d9::dev::GetCreationParameters] = gen() % 8 == 0 ? d3dfx::kResults[2] : 0;
        if (gen() % 3) Obj(gB)->mWanted.softwareVertexProcessing = 1;
        std::memcpy(gA + 4, gB + 4, kSize - 4);
        if (gen() % 3) gProgram.behavior |= 0x20;
        gProgram.behavior &= ~0x40u;
        cIGZUnknown *declaration = reinterpret_cast<cIGZUnknown *>(&gEngDecl[gen() % 2]);
        const uint32_t log = gen() % 4 != 0;

        FreshValidate(gA);
        const uint32_t ra = Orig<uint32_t>(0x181f00, gA, declaration, log) & 0xff;
        const Outcome a = Capture(ra);
        FreshValidate(gB);
        const uint32_t rb = static_cast<cIDeviceState *>(Obj(gB))->CheckVertexProcessing(declaration, log != 0) ? 1 : 0;
        const Outcome b = Capture(rb);
        drawfx::ResetAll();
        Check(ctx, "CheckVertexProcessing", round, a, b);

        for (uint32_t line : {0xb62u, 0xb69u, 0xb74u, 0xb84u}) lines[line - 0xb00] += CountReports(a.log, line) != 0;
        processed += CountSlot(a.log, fx_d3d9::IDirect3DDevice9, fx_d3d9::dev::ProcessVertices) != 0;
        made += CountSlot(a.log, kComIface, 3) != 0;
        usedHeld += CountSlot(a.log, kHeldIface, 6) != 0;
        ok += ra;
    }
    if (lines[0x62] < 50 || lines[0x69] < 50 || lines[0x74] < 50 || lines[0x84] < 50 || processed < 500 || made < 300 ||
        usedHeld < 500 || ok < 500)
        ctx.Fail("a path ran too rarely: reports %u %u %u %u, processed %u, held made %u, held checked %u, ok %u",
                 lines[0x62], lines[0x69], lines[0x74], lines[0x84], processed, made, usedHeld, ok);
}

DIFF_TEST(cDeviceStateDX_Validate, 0x182220)
{
    gCtx = &ctx;
    d3dfx::ErrorLog errors(ctx);
    ValidateStage stage(ctx);
    FillPools();
    std::mt19937 gen(0x182220);
    unsigned lines[0x100] = {}, validated = 0, validateFailed = 0, shaderDone = 0, indexed = 0, plain = 0, created = 0,
             createFailed = 0, drawFailed = 0, passed = 0, scenes = 0, features = 0;
    for (uint32_t round = 0; round < 60000; ++round) {
        const bool biased = round % 4 != 0;
        SeedValidate(gen, biased);
        gAnswers.Random(gen, fx_d3d9::IDirect3DDevice9.count, biased ? 40 : 6);
        for (const unsigned slot : {fx_d3d9::dev::ValidateDevice, fx_d3d9::dev::DrawPrimitive, fx_d3d9::dev::DrawIndexedPrimitive})
            gAnswers.ret[slot] = gen() % 4 == 0 ? d3dfx::kResults[1 + gen() % 3] : 0;
        const uint32_t log = gen() % 4 != 0;

        FreshValidate(gA);
        const uint32_t ra = Orig<uint32_t>(0x182220, gA, log) & 0xff;
        const Outcome a = Capture(ra);
        FreshValidate(gB);
        const uint32_t rb = static_cast<cIDeviceState *>(Obj(gB))->Validate(log != 0) ? 1 : 0;
        const Outcome b = Capture(rb);
        drawfx::ResetAll();
        Check(ctx, "Validate", round, a, b);

        for (uint32_t line : {0xbc7u, 0xbbeu, 0xbd6u, 0xbefu, 0xbf3u, 0xbffu, 0xc07u, 0xc0bu, 0xc2au, 0xc4bu, 0xc64u})
            lines[line - 0xb00] += CountReports(a.log, line) != 0;
        const bool v = CountSlot(a.log, fx_d3d9::IDirect3DDevice9, fx_d3d9::dev::ValidateDevice) != 0;
        validated += v;
        validateFailed += CountReports(a.log, 0xc2a) != 0;
        features += gVal.feature & 0xff;
        scenes += CountSlot(a.log, kEngine, 29) != 0;
        indexed += CountSlot(a.log, fx_d3d9::IDirect3DDevice9, fx_d3d9::dev::DrawIndexedPrimitive) != 0;
        plain += CountSlot(a.log, fx_d3d9::IDirect3DDevice9, fx_d3d9::dev::DrawPrimitive) != 0;
        created += CountSlot(a.log, kComIface, 3) != 0;
        createFailed += CountReports(a.log, 0xc4b) != 0;
        drawFailed += CountReports(a.log, 0xc64) != 0;
        shaderDone += v && CountSlot(a.log, kEngine, 29) == 0 && ra;
        passed += ra;
    }
    if (validated < 800 || validateFailed < 30 || scenes < 600 || indexed < 150 || plain < 150 || created < 50 ||
        createFailed < 10 || drawFailed < 30 || shaderDone < 100 || passed < 300 || features < 100)
        ctx.Fail("a path ran too rarely: device checks %u (failed %u), scenes %u, indexed %u, plain %u, held made %u (failed "
                 "%u), draw failed %u, shader return %u, passed %u, features %u",
                 validated, validateFailed, scenes, indexed, plain, created, createFailed, drawFailed, shaderDone, passed,
                 features);
    for (const uint32_t l : {0xbc7u, 0xbbeu, 0xbd6u, 0xbefu, 0xbf3u, 0xbffu, 0xc07u, 0xc0bu})
        if (lines[l - 0xb00] < 20) ctx.Fail("report at line 0x%x ran %u times", l, lines[l - 0xb00]);
}
