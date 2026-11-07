// Differential test of nGZSceneGraph::cRenderTargetTexture (lane TN, milestone O row M4b; src/sg/cRenderTargetTexture.h).
//
//   SG_RenderTargetTextureSlots  the object's life on each side: made by the director's factory (the original's 0xab4df0
//       with the constructor 0xab42b0; ours New), then random calls through the interface: QueryInterface 0xab5a70,
//       AddRef / Release (0x148fb0, 0x2c4080), ReleaseResource 0xab5dd0, GetKind 0xab4b40, IsCreated 0xab46b0,
//       GetDeviceTextureCount (0x147770), GetDeviceTexture 0xab4b60, Create 0xab59b0, CreateSimple 0xab5940, CreateRelative
//       0xab58b0 (each releases the resources, stores the description and calls Restore), Clear 0xab4700, ClearColour
//       0xab4730 and ClearOwn 0xab4750 (the clear helper 0xab4b80 with its arguments), GetAspect 0xab5a30,
//       GetTargetSurface 0xab5ad0 and GetDepthSurface 0xab4a90, Restore 0xab5240 (through slots 9-11), slot 12
//       SetAsTargets 0xab5b10 (the state command handed to a device-state fake) and the clear helper 0xab4b80 (the
//       state command and the rect handed to the device's ClearTargets); the targets' and the depth surface's creation
//       state and the device's readiness changing between calls. The whole state is noted after every call; at the end
//       the last Release (the deleting destructor 0xab4660 with the destructor 0xab4560), the fakes' counts and the heap.
//
// Stood in on both sides: the device getter 0x18ae40 (a device fake, or none: then the constructor makes no targets), the
// COM getter 0x1ae18d (handing out the target and depth fakes), the formats manager getter 0x18acd0 (a fake whose slot 8
// answers the float format or null), the format searches Restore runs (0x18a9b0, 0x18ab60, 0x18ad50: verified on their
// own; the stands note the range and the bounds, release the range and answer a format fake or null), the size fit
// 0x18c990 (notes its arguments, answers another size and level count) and the device's render-target size 0x18aea0
// (not ours yet: answers a size, including one with the top bit set, and an aspect). The enumeration (device slot 16)
// hands out counted ranges of iterator fakes whose Clone answers fakes of a pool.
#include "sgfx.h"

#include "code_patch.h"

#include "gz/DeviceTargetSize.h"
#include "gz/GraphicModeSearch.h"
#include "gz/GraphicServices.h"
#include "rz/StaticGetters.h"
#include "sg/TextureDescription.h"
#include "sg/cRenderTargetTexture.h"

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

constexpr At kDeviceAt[] = {{16, {0, Ret::Int, true, "GetDeviceEnumConst", ""}},
                            {28, {6, Ret::Int, true, "ClearTargets", "aruF4fu"}},
                            {35, {0, Ret::Int, true, "IsDeviceReady", ""}}};
constexpr auto kDeviceSlots = Table<36>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 36, kDeviceSlots.data()};

constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data()};

// The device's render-state cache (cIDeviceState): 12 ApplyStateCommand (the command's address).
constexpr At kStateAt[] = {{12, {1, Ret::Int, true, "ApplyStateCommand", "a"}}};
constexpr auto kStateSlots = Table<13>(kStateAt);
constexpr drawfx::Iface kState{"DeviceState", 13, kStateSlots.data()};

// The device enumeration (cIDeviceEnum): 9 GetColorFormatsForDisplay, 13 GetDepthFormatsForDisplay (a range returned
// through the hidden first argument).
constexpr At kEnumAt[] = {{9, {3, Ret::Int, true, "GetColorFormatsForDisplay", "auu"}},
                          {13, {3, Ret::Int, true, "GetDepthFormatsForDisplay", "aua"}}};
constexpr auto kEnumSlots = Table<14>(kEnumAt);
constexpr drawfx::Iface kEnum{"Enum", 14, kEnumSlots.data()};

// A pixel format iterator: 3 Clone.
constexpr At kIterAt[] = {{3, {0, Ret::Int, true, "Clone", ""}}};
constexpr auto kIterSlots = Table<4>(kIterAt);
constexpr drawfx::Iface kIter{"Iterator", 4, kIterSlots.data()};

// The formats manager: 8 GetABGRFloat.
constexpr At kManagerAt[] = {{8, {4, Ret::Int, true, "GetABGRFloat", "uuuu"}}};
constexpr auto kManagerSlots = Table<9>(kManagerAt);
constexpr drawfx::Iface kManager{"FormatsManager", 9, kManagerSlots.data()};

// A colour target (cITexture): 3 ReleaseResource, 6 IsCreated, 23 CreateRenderTarget, 26 GetTargetSurface.
constexpr At kTargetAt[] = {{3, {0, Ret::Int, true, "ReleaseResource", ""}},
                            {6, {0, Ret::Int, true, "IsCreated", ""}},
                            {23, {5, Ret::Int, true, "CreateRenderTarget", "auuau"}},
                            {26, {1, Ret::Int, true, "GetTargetSurface", "u"}}};
constexpr auto kTargetSlots = Table<27>(kTargetAt);
constexpr drawfx::Iface kTarget{"Target", 27, kTargetSlots.data()};

// The depth-stencil surface: 3 ReleaseResource, 6 IsCreated, 25 Create.
constexpr At kDepthAt[] = {{3, {0, Ret::Int, true, "ReleaseResource", ""}},
                           {6, {0, Ret::Int, true, "IsCreated", ""}},
                           {25, {6, Ret::Int, true, "Create", "auuauu"}}};
constexpr auto kDepthSlots = Table<26>(kDepthAt);
constexpr drawfx::Iface kDepth{"Depth", 26, kDepthSlots.data()};

constexpr uint32_t kTargetClass = 0xbb892198, kTargetIid = 0x5b891e1e, kDepthClass = 0x7b75b73a;
constexpr uint32_t kClearCell = 0x10f675c;
constexpr unsigned kTargets = 4, kSurfaces = 4, kIters = 6, kClones = 6, kFormats = 4;

struct World {
    Fake device, com, depth, state, enumeration, manager;
    Fake targets[kTargets], surfaces[kSurfaces], iters[kIters], clones[kClones], formats[kFormats];
    bool hasDevice = true;
    unsigned made = 0, cloned = 0;
    uint32_t lcg = 1;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

uint32_t Word0(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t GetClassObject(Fake &, const uint32_t *a)
{
    drawfx::Note("class", {a[0], a[1]});
    Fake *made;
    if (a[0] == kTargetClass) made = &gw.targets[gw.made++ % kTargets];
    else made = &gw.depth;
    ++made->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = made;
    return 1;
}
uint32_t TargetSurface(Fake &self, const uint32_t *a)
{
    const unsigned i = unsigned(&self - gw.targets);
    return drawfx::P(&gw.surfaces[(a[0] + i) % kSurfaces]);
}

// A format fake, or null one time in `nullOneIn`.
Fake *AnyFormat(unsigned nullOneIn)
{
    const uint32_t r = gw.Next();
    return r % nullOneIn == 0 ? nullptr : &gw.formats[(r / nullOneIn) % kFormats];
}

// The enumeration's ranges (counted): iterators 0-2 for the colour formats, 3-5 for the depth formats.
uint32_t ColourFormats(Fake &, const uint32_t *a)
{
    auto *const out = reinterpret_cast<Fake **>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 3; ++i) {
        out[i] = &gw.iters[i];
        ++out[i]->refs;
    }
    return a[0];
}
uint32_t DepthFormats(Fake &, const uint32_t *a)
{
    auto *const out = reinterpret_cast<Fake **>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 3; ++i) {
        out[i] = &gw.iters[3 + i];
        ++out[i]->refs;
    }
    return a[0];
}
uint32_t Clone(Fake &, const uint32_t *) { return drawfx::P(&gw.clones[gw.cloned++ % kClones]); }
uint32_t FloatFormat(Fake &, const uint32_t *) { return drawfx::P(AnyFormat(4)); }
uint32_t Enumeration(Fake &, const uint32_t *) { return drawfx::P(&gw.enumeration); }
// The targets' and the depth surface's creation: whether it took is what IsCreated answers afterwards.
uint32_t Made(Fake &self, const uint32_t *)
{
    self.value[0] = gw.Next() % 4 != 0;
    return self.value[0];
}

// A render-targets state command: its vtable (the image's), id, size, four surfaces and the depth surface. Byte 5 of
// the header is left unset by the original.
void NoteState(uint32_t address)
{
    difftest::NotCounted bookkeeping;
    const uint32_t *w = reinterpret_cast<const uint32_t *>(uintptr_t(address));
    drawfx::Note("state", {drawfx::Ptr(w[0]), w[1] & 0xff, w[1] >> 16});
    drawfx::Note("state targets", {drawfx::Ptr(w[2]), drawfx::Ptr(w[3]), drawfx::Ptr(w[4]), drawfx::Ptr(w[5]),
                                   drawfx::Ptr(w[6])});
}
uint32_t ClearTargets(Fake &, const uint32_t *a)
{
    NoteState(a[0]);
    return 0;
}
uint32_t ApplyState(Fake &, const uint32_t *a)
{
    NoteState(a[0]);
    return 0;
}

void *__cdecl StandDevice() { return gw.hasDevice ? &gw.device : nullptr; }
void *StandCom() { return &gw.com; }
void *__cdecl StandManager() { return &gw.manager; }

using nGZGraphic4::cIPixelFormat;
using nGZGraphic4::cPixelFormatRange;

// The searches' range (by value, theirs): noted and released end, begin, cursor.
void TakeRange(const cPixelFormatRange &range)
{
    drawfx::Note("range", {drawfx::Ptr(range.mCurrent), drawfx::Ptr(range.mBegin), drawfx::Ptr(range.mEnd)});
    for (const void *it : {static_cast<const void *>(range.mEnd), static_cast<const void *>(range.mBegin),
                           static_cast<const void *>(range.mCurrent)})
        if (it) --static_cast<Fake *>(const_cast<void *>(it))->refs;
}
cIPixelFormat *__cdecl StandColour(cPixelFormatRange range, int32_t bits, int32_t alphaBits)
{
    drawfx::Note("colour search", {uint32_t(bits), uint32_t(alphaBits)});
    TakeRange(range);
    return reinterpret_cast<cIPixelFormat *>(AnyFormat(3));
}
cIPixelFormat *__cdecl StandDepth(cPixelFormatRange range, int32_t depthBits, int32_t stencilBits, bool floatDepth)
{
    drawfx::Note("depth search", {uint32_t(depthBits), uint32_t(stencilBits), uint32_t(floatDepth)});
    TakeRange(range);
    return reinterpret_cast<cIPixelFormat *>(AnyFormat(3));
}
cIPixelFormat *__cdecl StandLowerBound(cPixelFormatRange range, cIPixelFormat *wanted)
{
    drawfx::Note("lower bound", drawfx::Ptr(wanted));
    TakeRange(range);
    return reinterpret_cast<cIPixelFormat *>(AnyFormat(4));
}
void __cdecl StandFit(nGZGraphic4::cIDeviceDX *device, cIPixelFormat *format, int32_t *width, int32_t *height,
                      uint32_t *levels, bool allowNonPow2)
{
    drawfx::Note("fit", {drawfx::Ptr(device), drawfx::Ptr(format), uint32_t(*width), uint32_t(*height), *levels,
                         uint32_t(allowNonPow2)});
    static constexpr int32_t kSizes[] = {1, 2, 64, 256, 512, 1024, 2048};
    const uint32_t r = gw.Next();
    if (r % 3 != 0) {
        *width = kSizes[(r / 3) % 7];
        *height = kSizes[(r / 21) % 7];
        *levels = (r / 147) % 3;
    }
}
// The device's render-target size 0x18aea0: width, height, aspect and a fourth word.
const uint32_t *__cdecl StandTargetSize(uint32_t *out)
{
    static constexpr uint32_t kSizes[] = {0, 1, 640, 1023, 1024, 1600, 0x80000001u, 0xffffffffu};
    static constexpr float kAspects[] = {1.0f, 1.3333334f, 0.75f, 1.7777778f};
    const uint32_t r = gw.Next();
    out[0] = kSizes[r % 8];
    out[1] = kSizes[(r / 8) % 8];
    out[2] = std::bit_cast<uint32_t>(kAspects[(r / 64) % 4]);
    out[3] = r;
    drawfx::Note("target size", 0u);
    return out;
}

void NoteObject(const void *object)
{
    difftest::NotCounted bookkeeping;
    const uint32_t *w = static_cast<const uint32_t *>(object);
    drawfx::Note("refs, size", {w[1], w[2], w[3]});
    drawfx::Note("bits", {w[4], w[5], w[6], w[7]});
    drawfx::Note("scale, flags, count, aspect", {w[8], w[9], w[10], w[11], w[12]});
    drawfx::Note("clear", {w[13], w[14], w[15], w[16]});
    drawfx::Note("targets", {drawfx::Ptr(w[17]), drawfx::Ptr(w[18]), drawfx::Ptr(w[19]), drawfx::Ptr(w[20])});
    drawfx::Note("depth", drawfx::Ptr(w[21]));
}

struct Stands {
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn manager{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &StandManager};
    shape::StandIn colour{0x18a9b0, &nGZGraphic4::FindClosestColorFormat, &StandColour};
    shape::StandIn depth{0x18ab60, &nGZGraphic4::FindClosestDepthFormat, &StandDepth};
    shape::StandIn lowerBound{0x18ad50, &nGZGraphic4::FindFormatLowerBound, &StandLowerBound};
    shape::StandIn fit{0x18c990, &AdjustTextureSize, &StandFit};
    shape::StandIn targetSize{0x18aea0, &nGZGraphic4::GetDeviceTargetSize, &StandTargetSize};
};

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 5;
    gw.made = 0;
    gw.cloned = 0;
    gw.hasDevice = seed % 5 != 0;
    gw.device.Reset<kDevice>(660);
    gw.device.value[0] = seed % 7 != 0;
    gw.device.On(16, &Enumeration);
    gw.device.On(28, &ClearTargets);
    gw.device.On(35, &Word0);
    gw.com.Reset<kCom>(661);
    gw.com.On(3, &GetClassObject);
    gw.depth.Reset<kDepth>(662);
    gw.depth.On(6, &Word0);
    gw.depth.On(25, &Made);
    gw.state.Reset<kState>(663);
    gw.state.On(12, &ApplyState);
    gw.enumeration.Reset<kEnum>(664);
    gw.enumeration.On(9, &ColourFormats);
    gw.enumeration.On(13, &DepthFormats);
    gw.manager.Reset<kManager>(665);
    gw.manager.On(8, &FloatFormat);
    for (unsigned i = 0; i < kTargets; ++i) {
        gw.targets[i].Reset<kTarget>(670 + i);
        gw.targets[i].On(6, &Word0);
        gw.targets[i].On(23, &Made);
        gw.targets[i].On(26, &TargetSurface);
    }
    for (unsigned i = 0; i < kSurfaces; ++i) gw.surfaces[i].Reset<sgfx::kUnknown>(680 + i);
    for (unsigned i = 0; i < kIters; ++i) gw.iters[i].Reset<kIter>(690 + i);
    for (unsigned i = 0; i < kIters; ++i) gw.iters[i].On(3, &Clone);
    for (unsigned i = 0; i < kClones; ++i) gw.clones[i].Reset<kIter>(700 + i);
    for (unsigned i = 0; i < kFormats; ++i) gw.formats[i].Reset<sgfx::kUnknown>(710 + i);
    (void)cells;
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.device, &gw.com, &gw.depth, &gw.state, &gw.enumeration, &gw.manager})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.targets) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.iters) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.clones) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

void Step(cIRenderTargetTexture *t, std::mt19937 &rng)
{
    const unsigned op = rng() % 19;
    drawfx::Note("op", op);
    const bool device = gw.hasDevice; // the slots that call the targets unchecked need them
    static constexpr float kColours[] = {0.0f, 0.25f, 0.5f, 1.0f};
    float clear[4];
    for (float &c : clear) c = kColours[rng() % 4];
    int32_t rect[4] = {int32_t(rng() % 100), int32_t(rng() % 100), int32_t(rng() % 1000), int32_t(rng() % 1000)};
    const int32_t *const rectOrNull = rng() % 3 ? rect : nullptr;
    switch (op) {
    case 0: {
        static constexpr uint32_t kIids[] = {1, cIRenderTargetTexture::kIid, cRenderTargetTexture::kIidTexture,
                                             0x2a956da8, 0, 0xdeadbeef};
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
        if (device) t->ReleaseResource();
        break;
    case 3:
        drawfx::Note("kind", t->GetKind());
        // the whole of eax: image callers loop over this many device textures
        drawfx::Note("device texture count", reinterpret_cast<uint32_t(__thiscall *)(void *)>((*reinterpret_cast<void *const *const *>(t))[7])(t));
        drawfx::Note("aspect", std::bit_cast<uint32_t>(t->GetAspect()));
        for (uint32_t i = 0; i < 6; ++i) drawfx::Note("device texture", drawfx::Ptr(t->GetDeviceTexture(i)));
        break;
    case 4:
    case 5:
        if (device || reinterpret_cast<const uint32_t *>(t)[11] == 0) drawfx::Note("created", uint32_t(uint8_t(t->IsCreated())));
        break;
    case 6:
    case 7:
        if (device) {
            static constexpr uint32_t kFlags[] = {0, 1, 2, 3, 4, 5, 7};
            t->Create(int32_t(rng() % 2048), int32_t(rng() % 2048), int32_t(rng() % 40), int32_t(rng() % 10),
                      int32_t(rng() % 9), rng() % 5, clear, kFlags[rng() % 7]);
        }
        break;
    case 8:
        if (device) t->CreateSimple(int32_t(rng() % 2048), int32_t(rng() % 2048), int32_t(rng() % 40));
        break;
    case 9:
        if (device) {
            static constexpr uint32_t kFlags[] = {0, 1, 2, 3, 4, 5, 7};
            t->CreateRelative(0.25f * float(rng() % 9), 0.25f * float(rng() % 9), int32_t(rng() % 40),
                              int32_t(rng() % 10), int32_t(rng() % 9), rng() % 5, clear, kFlags[rng() % 7]);
        }
        break;
    // The clear helper asks the current device unchecked (ORIGINAL-BUG: no device crashes): only with one.
    case 10:
        if (device) t->Clear(rectOrNull, clear, 0.25f * float(rng() % 5), rng() % 4);
        break;
    case 11:
        if (device) t->ClearColour(rectOrNull, clear);
        break;
    case 12:
        if (device) t->ClearOwn(rectOrNull, 0.5f * float(rng() % 3), rng() % 4);
        break;
    case 13:
        for (uint32_t i = 0; i < 4; ++i) drawfx::Note("surface", drawfx::Ptr(t->GetTargetSurface(i)));
        break;
    case 14:
        if (device) drawfx::Note("depth surface", drawfx::Ptr(t->GetDepthSurface()));
        break;
    case 15:
        if (device) t->SetAsTargets(&gw.state, int32_t(rng() % 5) - 1); // the targets are asked unchecked
        break;
    case 16:
        for (Fake &f : gw.targets) f.value[0] = rng() % 2; // the device lost or regained them behind the texture's back
        gw.device.value[0] = rng() % 4 != 0;               // and the device its readiness
        break;
    case 18:
        if (device) t->Restore(); // slot 3 itself: made again only when the targets were lost
        break;
    default:
        gw.depth.value[0] = rng() % 2;
        break;
    }
    NoteObject(t);
}

void RunLife(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *made;
    if (original) made = ctx.Original<void *(__cdecl *)()>(0xab4df0)();
    else made = cRenderTargetTexture::New();
    auto *const t = static_cast<cIRenderTargetTexture *>(made);
    drawfx::Note("made", drawfx::Ptr(made));
    NoteObject(t);
    t->AddRef();
    const unsigned steps = 4 + rng() % 30;
    for (unsigned s = 0; s < steps; ++s) Step(t, rng);
    sgfx::MarkStep(ctx, "last release");
    drawfx::Note("last release", t->Release());
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the factory 0xab4df0, the constructor 0xab42b0, the destructor 0xab4560, the deleting destructor 0xab4660,
// QueryInterface 0xab5a70, ReleaseResource 0xab5dd0, GetKind 0xab4b40, IsCreated 0xab46b0, GetDeviceTexture 0xab4b60,
// Create 0xab59b0, CreateSimple 0xab5940, CreateRelative 0xab58b0, the clear slots 0xab4700 / 0xab4730 / 0xab4750,
// GetAspect 0xab5a30, GetTargetSurface 0xab5ad0, GetDepthSurface 0xab4a90, Restore 0xab5240, SetAsTargets 0xab5b10, the
// clear helper 0xab4b80, and the folded slots 0x148fb0 (AddRef),
// 0x2c4080 (Release) and 0x147770 (GetDeviceTextureCount) as this class's vtable has them.
DIFF_TEST(SG_RenderTargetTextureSlots, 0xab4df0)
{
    Stands stands;
    sgfx::CheckSides(
        ctx, "render target texture", 2000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, original, rng);
            ++ctx.cases;
        });
}
