// Differential test of nGZSceneGraph::cProceduralTexture (lane TH, milestone O row M4b; src/sg/cProceduralTexture.h).
//
//   SG_ProceduralTextureSlots  the object's life on each side: made by the director's factory (the original's 0xab4d90
//       with the constructor 0xab4230; ours New), then random calls through the interface: QueryInterface 0xab5a40,
//       AddRef / Release (0x148fb0, shared_vf_151920), Restore 0xab5090 (the strategy's description, the description
//       chooser, 0xab57d0, the strategy drawing level 0 or each cube face, the mipmaps, Finish), ReleaseResource
//       0xab5dc0, GetKind (0x145770), IsCreated (shared_vf_ab46a0), GetDeviceTextureCount (0x15e510), GetDeviceTexture
//       (shared_vf_ab4b50), Create 0xab4f50, CreateClosest 0xab4ef0 and CreateCube 0xab4fe0 (the format from the
//       formats manager's ARGB iterator through FindClosestColorFormat), Redraw 0xab6320, GetStrategy (0x46390); the
//       device's readiness, the device texture's state and level count, and the strategy's answers changing between
//       calls. The whole state is noted after every call; at the end the last Release (the deleting destructor 0xab4640
//       with the destructor 0xab44f0), the fakes' counts and the heap.
//
// Stood in on both sides: the device getter 0x18ae40, the COM getter 0x1ae18d (handing out the device texture fake) and
// the description chooser 0xab5f50 (ours, tested in sg_texture_description.cpp; here it notes its arguments, writes a
// description and answers true four times in five). The image manipulator is a fake in its cache cell 0x10ca990, the
// formats manager one in 0x10ca830.
#include "sgfx.h"

#include "code_patch.h"

#include "gz/GraphicServices.h"
#include "rz/StaticGetters.h"
#include "sg/TextureDescription.h"
#include "sg/cProceduralTexture.h"
#include "sg/cTextureUpdateStrategy.h"

#include <array>
#include <bit>
#include <cstdint>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

constexpr At kDeviceAt[] = {{35, {0, Ret::Int, true, "IsDeviceReady", ""}}};
constexpr auto kDeviceSlots = Table<36>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 36, kDeviceSlots.data()};

constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data()};

// The device texture's cITextureBase face: 3 ReleaseResource, 6 IsCreated, 16 GetLevelCount.
constexpr At kBaseAt[] = {{3, {0, Ret::Int, true, "ReleaseResource", ""}},
                          {6, {0, Ret::Int, true, "IsCreated", ""}},
                          {16, {0, Ret::Int, true, "GetLevelCount", ""}}};
constexpr auto kBaseSlots = Table<17>(kBaseAt);
constexpr drawfx::Iface kBase{"TextureBase", 17, kBaseSlots.data()};

// Its 2D and cube faces: 22 Create, 28 GetTargetLockableSurface.
constexpr At kFace2DAt[] = {{22, {7, Ret::Int, true, "Create", "auuuufu"}},
                            {28, {1, Ret::Int, true, "GetTargetLockableSurface", "u"}}};
constexpr auto kFace2DSlots = Table<29>(kFace2DAt);
constexpr drawfx::Iface kFace2D{"Texture2D", 29, kFace2DSlots.data()};
constexpr At kFaceCubeAt[] = {{22, {6, Ret::Int, true, "Create", "auuufu"}},
                              {28, {2, Ret::Int, true, "GetTargetLockableSurface", "uu"}}};
constexpr auto kFaceCubeSlots = Table<29>(kFaceCubeAt);
constexpr drawfx::Iface kFaceCube{"CubeTexture", 29, kFaceCubeSlots.data()};

// The update strategy: 3 GetDescription, 4 Update, 5 Finish.
constexpr At kStrategyAt[] = {{3, {4, Ret::Int, true, "GetDescription", "aaaa"}},
                              {4, {3, Ret::Int, true, "Update", "auu"}},
                              {5, {0, Ret::Int, true, "Finish", ""}}};
constexpr auto kStrategySlots = Table<6>(kStrategyAt);
constexpr drawfx::Iface kStrategy{"UpdateStrategy", 6, kStrategySlots.data()};

constexpr At kManipAt[] = {{12, {1, Ret::Int, true, "GenerateMipmaps", "a"}}};
constexpr auto kManipSlots = Table<13>(kManipAt);
constexpr drawfx::Iface kManip{"ImageManip", 13, kManipSlots.data()};

constexpr At kFormatsAt[] = {{3, {2, Ret::Int, true, "CreateIterator", "au"}}};
constexpr auto kFormatsSlots = Table<4>(kFormatsAt);
constexpr drawfx::Iface kFormats{"FormatsManager", 4, kFormatsSlots.data()};

// An ARGB pixel format (its own face): 6 bits per pixel, 11 alpha bits, 15 unused bits.
constexpr At kFormatAt[] = {{6, {0, Ret::Int, true, "BitsPerPixel", ""}},
                            {11, {0, Ret::Int, true, "AlphaBits", ""}},
                            {15, {0, Ret::Int, true, "UnusedBits", ""}}};
constexpr auto kFormatSlots = Table<16>(kFormatAt);
constexpr drawfx::Iface kFormat{"PixelFormat", 16, kFormatSlots.data()};

constexpr At kIterAt[] = {{4, {1, Ret::Int, true, "IsEqual", "a"}},
                          {5, {0, Ret::Int, true, "Get", ""}},
                          {6, {0, Ret::Int, true, "Next", ""}}};
constexpr auto kIterSlots = Table<7>(kIterAt);
constexpr drawfx::Iface kIter{"FormatIterator", 7, kIterSlots.data()};

// ---- the world ----
constexpr uint32_t kManipCell = 0x10ca990, kFormatsCell = 0x10ca830;
constexpr uint32_t kIidArgb = 0x9ac38991, kIidTexture2D = 0x5b891e1e, kIidCube = 0xe9443438;
constexpr unsigned kStrategies = 2, kFormatCount = 4, kSurfaces = 6, kIters = 3;

struct World {
    Fake device, com, base, face2d, faceCube, manip, formatsManager;
    Fake strategies[kStrategies], formats[kFormatCount], surfaces[kSurfaces], iters[kIters];
    uint32_t lcg = 1;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

uint32_t Word0(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t Word1(Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t Word2(Fake &self, const uint32_t *) { return self.value[2]; }
uint32_t GetClassObject(Fake &, const uint32_t *a)
{
    ++gw.base.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gw.base;
    return 1;
}
uint32_t BaseRelease(Fake &self, const uint32_t *)
{
    self.value[0] = 0;
    return 0;
}
// Create on a face: the device texture is created three times in four (and the result says so).
uint32_t FaceCreate(Fake &, const uint32_t *)
{
    gw.base.value[0] = gw.Next() % 4 != 0;
    return gw.base.value[0];
}
uint32_t Surface2D(Fake &, const uint32_t *a) { return drawfx::P(&gw.surfaces[a[0] % kSurfaces]); }
uint32_t SurfaceCube(Fake &, const uint32_t *a) { return drawfx::P(&gw.surfaces[(a[0] + a[1]) % kSurfaces]); }
// GetDescription: two times in three a description written (value 0..3) and true, else nothing and false.
uint32_t StrategyDescription(Fake &self, const uint32_t *a)
{
    if (gw.Next() % 3 == 0) return 0;
    for (unsigned i = 0; i < 4; ++i) *reinterpret_cast<uint32_t *>(uintptr_t(a[i])) = self.value[i];
    return 1;
}
// CreateIterator(out, iid): the formats, each end counted (the cursor at the start).
uint32_t CreateIterator(Fake &, const uint32_t *a)
{
    Fake **const range = reinterpret_cast<Fake **>(uintptr_t(a[0]));
    gw.iters[0].value[0] = 0;
    gw.iters[1].value[0] = 0;
    gw.iters[2].value[0] = kFormatCount;
    for (unsigned i = 0; i < 3; ++i) {
        range[i] = &gw.iters[i];
        ++gw.iters[i].refs;
    }
    return a[0];
}
uint32_t IterEqual(Fake &self, const uint32_t *a)
{
    return self.value[0] == reinterpret_cast<const Fake *>(uintptr_t(a[0]))->value[0];
}
uint32_t IterGet(Fake &self, const uint32_t *) { return self.value[0] < kFormatCount ? drawfx::P(&gw.formats[self.value[0]]) : 0; }
uint32_t IterNext(Fake &self, const uint32_t *)
{
    ++self.value[0];
    return 0;
}

void *__cdecl StandDevice() { return &gw.device; }
void *StandCom() { return &gw.com; }
// 0xab5f50's stand: notes what it is asked, writes a description from it, true four times in five.
bool __cdecl StandChoose(TextureDescription *desc, int32_t width, int32_t height, uint32_t levels,
                         nGZGraphic4::cIPixelFormat *format, bool normalMap, uint32_t kind)
{
    drawfx::Note("choose", {uint32_t(width), uint32_t(height), levels, drawfx::Ptr(format), uint32_t(normalMap), kind});
    desc->width = width + 1;
    desc->height = height + 2;
    desc->levels = levels + 3;
    desc->format = reinterpret_cast<nGZGraphic4::cIPixelFormat *>(uintptr_t(0x3000 + (gw.Next() & 0xff)));
    return gw.Next() % 5 != 0;
}

struct Stands {
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn choose{0xab5f50, &ChooseTextureDescription, &StandChoose};
};

void RandomStrategy(Fake &s)
{
    const uint32_t r = s.value[4] = gw.Next();
    s.value[0] = 1u << (r % 9);
    s.value[1] = 1u << ((r >> 4) % 9);
    s.value[2] = (r >> 8) % 6;
    s.value[3] = std::bit_cast<uint32_t>(0.25f * float((r >> 12) % 5));
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 5;
    gw.device.Reset<kDevice>(560);
    gw.device.On(35, &Word0);
    gw.device.value[0] = seed % 4 != 0;
    gw.com.Reset<kCom>(561);
    gw.com.On(3, &GetClassObject);
    gw.base.Reset<kBase>(562);
    gw.base.On(3, &BaseRelease);
    gw.base.On(6, &Word0);
    gw.base.On(16, &Word1);
    gw.base.value[1] = gw.Next() % 3; // the level count (1: no mipmaps)
    gw.face2d.Reset<kFace2D>(563);
    gw.face2d.On(22, &FaceCreate);
    gw.face2d.On(28, &Surface2D);
    gw.faceCube.Reset<kFaceCube>(564);
    gw.faceCube.On(22, &FaceCreate);
    gw.faceCube.On(28, &SurfaceCube);
    gw.base.AddFace(kIidTexture2D, gw.face2d);
    gw.base.AddFace(kIidCube, gw.faceCube);
    gw.manip.Reset<kManip>(565);
    gw.formatsManager.Reset<kFormats>(566);
    gw.formatsManager.On(3, &CreateIterator);
    for (unsigned i = 0; i < kStrategies; ++i) {
        gw.strategies[i].Reset<kStrategy>(570 + i);
        gw.strategies[i].On(3, &StrategyDescription);
        RandomStrategy(gw.strategies[i]);
    }
    static constexpr uint32_t kBits[] = {16, 16, 24, 32};
    static constexpr uint32_t kAlpha[] = {0, 1, 4, 8};
    for (unsigned i = 0; i < kFormatCount; ++i) {
        Fake &f = gw.formats[i];
        f.Reset<kFormat>(580 + i);
        f.On(6, &Word0);
        f.On(11, &Word1);
        f.On(15, &Word2);
        const uint32_t r = gw.Next();
        f.value[0] = kBits[r % 4];
        f.value[1] = kAlpha[(r >> 2) % 4];
        f.value[2] = (r >> 4) % 3;
        if ((r >> 6) % 5) f.AddFace(kIidArgb, f);
    }
    for (unsigned i = 0; i < kSurfaces; ++i) gw.surfaces[i].Reset<sgfx::kUnknown>(590 + i);
    for (unsigned i = 0; i < kIters; ++i) {
        gw.iters[i].Reset<kIter>(600 + i);
        gw.iters[i].On(4, &IterEqual);
        gw.iters[i].On(5, &IterGet);
        gw.iters[i].On(6, &IterNext);
    }
    cells.Set(kManipCell, &gw.manip);
    cells.Set(kFormatsCell, &gw.formatsManager);
}

// ---- noting ----
void NoteState(const cIProceduralTexture *t)
{
    difftest::NotCounted bookkeeping;
    const uint32_t *w = reinterpret_cast<const uint32_t *>(t);
    drawfx::Note("refs, strategy, kind", {w[1], drawfx::Ptr(w[2]), w[3]});
    drawfx::Note("width, height, levels, quality", {w[4], w[5], w[6], w[7]});
    drawfx::Note("format, texture", {drawfx::Ptr(w[8]), drawfx::Ptr(w[9])});
    drawfx::Note("created, ready, levels", {gw.base.value[0], gw.device.value[0], gw.base.value[1]});
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.device, &gw.com, &gw.base, &gw.face2d, &gw.faceCube, &gw.manip, &gw.formatsManager})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.strategies) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.formats) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.iters) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

struct Life {
    cIProceduralTexture *t;
    bool hasTexture = false;  // set up once: the slots that call the device texture unchecked may run
    bool hasStrategy = false; // Restore and Redraw call the strategy unchecked
};

nRZSceneGraph::cITextureUpdateStrategy *PickStrategy(std::mt19937 &rng)
{
    const unsigned pick = rng() % (kStrategies + 1);
    if (pick == kStrategies) return nullptr;
    return reinterpret_cast<nRZSceneGraph::cITextureUpdateStrategy *>(&gw.strategies[pick]);
}

void Step(Life &life, std::mt19937 &rng)
{
    cIProceduralTexture *const t = life.t;
    const unsigned op = rng() % 16;
    drawfx::Note("op", op);
    const bool canDraw = life.hasTexture && life.hasStrategy;
    switch (op) {
    case 0: {
        static constexpr uint32_t kIids[] = {1, cIProceduralTexture::kIid, cProceduralTexture::kIidTexture, 0x2a956da8,
                                             0, 0xdeadbeef};
        const uint32_t iid = kIids[rng() % 6];
        void *out = nullptr;
        const bool got = t->QueryInterface(iid, rng() % 5 ? &out : nullptr);
        drawfx::Note("query", {iid, uint32_t(got), drawfx::Ptr(out)});
        if (got) t->Release();
        break;
    }
    case 1: {
        const uint32_t a = t->AddRef();
        const uint32_t b = t->Release();
        drawfx::Note("addref, release", {a, b});
        break;
    }
    case 2:
    case 3:
        // Restore asks the device first and the device texture only when the device is ready.
        if (canDraw || !gw.device.value[0]) t->Restore();
        break;
    case 4:
        if (life.hasTexture) t->ReleaseResource();
        break;
    case 5:
        drawfx::Note("kind", t->GetKind());
        if (life.hasTexture) drawfx::Note("created", uint32_t(uint8_t(t->IsCreated())));
        drawfx::Note("device texture count", t->GetDeviceTextureCount());
        drawfx::Note("device texture", drawfx::Ptr(t->GetDeviceTexture(rng() % 3)));
        drawfx::Note("strategy", drawfx::Ptr(t->GetStrategy()));
        break;
    case 6:
    case 7: {
        nRZSceneGraph::cITextureUpdateStrategy *const s = PickStrategy(rng);
        const uint32_t w = 1u << (rng() % 10), h = 1u << (rng() % 10), levels = rng() % 8;
        const float quality = 0.125f * float(rng() % 9);
        if (op == 6) {
            t->Create(s, w, h, levels, reinterpret_cast<nGZGraphic4::cIPixelFormat *>(&gw.formats[rng() % kFormatCount]),
                      quality);
        } else {
            t->CreateClosest(s, w, h, levels, int32_t(rng() % 40), int32_t(rng() % 10), quality);
        }
        life.hasTexture = true;
        life.hasStrategy = s != nullptr;
        break;
    }
    case 8: {
        nRZSceneGraph::cITextureUpdateStrategy *const s = PickStrategy(rng);
        t->CreateCube(s, 1u << (rng() % 10), rng() % 8, int32_t(rng() % 40), int32_t(rng() % 10),
                      0.125f * float(rng() % 9));
        life.hasTexture = true;
        life.hasStrategy = s != nullptr;
        break;
    }
    case 9:
    case 10:
        if (canDraw) t->Redraw();
        break;
    case 11:
        gw.device.value[0] = rng() % 4 != 0;
        break;
    case 12:
        gw.base.value[0] = rng() % 2; // the device lost or regained it behind the texture's back
        break;
    case 13:
        gw.base.value[1] = rng() % 3;
        break;
    default:
        RandomStrategy(gw.strategies[rng() % kStrategies]);
        break;
    }
    NoteState(t);
}

void RunLife(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *made;
    if (original) made = ctx.Original<void *(__cdecl *)()>(0xab4d90)();
    else made = cProceduralTexture::New();
    Life life{static_cast<cIProceduralTexture *>(made)};
    drawfx::Note("made", drawfx::Ptr(made));
    NoteState(life.t);
    life.t->AddRef();
    const unsigned steps = 4 + rng() % 30;
    for (unsigned s = 0; s < steps; ++s) Step(life, rng);
    sgfx::MarkStep(ctx, "last release");
    drawfx::Note("last release", life.t->Release());
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the factory 0xab4d90, the constructor 0xab4230, the destructor 0xab44f0, the deleting destructor 0xab4640,
// QueryInterface 0xab5a40, Restore 0xab5090, ReleaseResource 0xab5dc0, Create 0xab4f50, CreateClosest 0xab4ef0,
// CreateCube 0xab4fe0, Redraw 0xab6320, and the folded slots shared_vf_151920 (Release), shared_vf_ab46a0 (IsCreated)
// and shared_vf_ab4b50 (GetDeviceTexture) as this class's vtable has them.
DIFF_TEST(SG_ProceduralTextureSlots, 0xab5090)
{
    Stands stands;
    sgfx::CheckSides(
        ctx, "procedural texture", 2000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, original, rng);
            ++ctx.cases;
        });
}
