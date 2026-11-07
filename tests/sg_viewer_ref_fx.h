#pragma once
// The viewer reference's world (lane PY, scenegraph N3 part 2): sg_renderable_fx.h's 0xe8-byte block (58 words: the
// renderable part, then the viewer's) plus what the viewer reaches, as stand-ins for the scope of a test:
//   the framework (the director's, and the game global the service getter reads): slot 3 answers a counted event fake,
//     slot 5 the event system service; the event service's slot 3 the connection manager, whose slot 12 (IsConnected)
//     answers a programmed value;
//   a bounded node (the held bound: slots 70-75 programmed, 71 / 72 answer a box through the hidden out pointer);
//   the current device (0x18ae40: a fake, or none) with its ready flag and two surfaces;
//   the texture manager, a fake in 0xae8750's cache 0x10f8f94: the handle operations (0xae8260 .. 0xae8730) run for real
//     on both sides (the image's and ours, sg/cTextureManager.h), and its slots 17-20 / 23 / 24 log their arguments;
//   the 16 bytes at 0x10f8d1c (.bss) the constructor copies, random per seed.
#include "sg_renderable_fx.h"

#include "gz/GraphicServices.h"
#include "rz/StaticGetters.h"
#include "sg/cViewerRefNodeBaseImpl.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace svx {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using sgrx::Services;

#define SVX_UNSETTLED {0, Ret::Int, false, "", ""}
// Slot tables: only the slots the viewer calls are settled.
inline constexpr Slot kFwSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "GetEvent", "uua"}, // 3 (the constructor: 0xbcf4e33, 1, &out)
    SVX_UNSETTLED,
    {3, Ret::Int, true, "GetSystemService", "uui"}, // 5
};
inline constexpr drawfx::Iface kFw{"Framework", 6, kFwSlots};
inline constexpr Slot kEvtSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {0, Ret::Int, true, "GetConnectionManager", ""}, // 3
};
inline constexpr drawfx::Iface kEvt{"EventService", 4, kEvtSlots};
inline constexpr Slot kConnSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    SVX_UNSETTLED, SVX_UNSETTLED, SVX_UNSETTLED, SVX_UNSETTLED, SVX_UNSETTLED, SVX_UNSETTLED, SVX_UNSETTLED,
    SVX_UNSETTLED, SVX_UNSETTLED,
    {4, Ret::Int, true, "IsConnected", "iuiu"}, // 12
};
inline constexpr drawfx::Iface kConn{"ConnectionManager", 13, kConnSlots};

template <size_t N> constexpr std::array<Slot, N> Sparse(std::initializer_list<std::pair<unsigned, Slot>> set)
{
    std::array<Slot, N> a{};
    for (size_t i = 0; i < N; ++i) a[i] = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    for (const auto &p : set) a[p.first] = p.second;
    return a;
}
inline constexpr auto kDevSlots = Sparse<41>({{16, {0, Ret::Int, true, "GetTargetProvider", ""}},
                                              {35, {0, Ret::Int, true, "IsDeviceReady", ""}},
                                              {39, {1, Ret::Int, true, "GetBackBuffer", "u"}},
                                              {40, {0, Ret::Int, true, "GetTargetProviderSurface", ""}}});
inline constexpr drawfx::Iface kDev{"Device", 41, kDevSlots.data()};
inline constexpr auto kMgrSlots = Sparse<25>({{17, {1, Ret::Int, true, "FindOrLoad", "s"}},
                                              {18, {1, Ret::Int, true, "AddRefId", "u"}},
                                              {19, {1, Ret::Int, true, "ReleaseId", "u"}},
                                              {20, {3, Ret::Int, true, "FindTexture", "usu"}},
                                              {23, {1, Ret::Int, true, "GetTexture", "u"}},
                                              {24, {2, Ret::Int, true, "GetRecord", "iu"}}});
inline constexpr drawfx::Iface kMgr{"TextureManager", 25, kMgrSlots.data()};
inline constexpr auto kTexSlots = Sparse<8>({{6, {0, Ret::Int, true, "Slot6", ""}}, {7, {0, Ret::Int, true, "Slot7", ""}}});
inline constexpr drawfx::Iface kTex{"Texture", 8, kTexSlots.data()};
// The texture target the handle object is asked for (iid 0xb96cc22): slot 6 the test, 16 the scale (a float), 17 a level's
// surface by index, 18 the reference.
inline constexpr auto kTgtSlots = Sparse<22>({{6, {0, Ret::Int, true, "Slot6", ""}},
                                              {16, {0, Ret::Float, true, "GetScale", ""}},
                                              {17, {1, Ret::Int, true, "GetLevel", "u"}},
                                              {18, {0, Ret::Int, true, "GetRef", ""}}});
inline constexpr drawfx::Iface kTgt{"TextureTarget", 22, kTgtSlots.data()};
// A surface or texture level: its width, height and slot 22 (what the device's target-size function reads).
inline constexpr auto kSurfSlots = Sparse<23>({{20, {0, Ret::Int, true, "GetWidth", ""}},
                                               {21, {0, Ret::Int, true, "GetHeight", ""}},
                                               {22, {0, Ret::Int, true, "GetSlot22", ""}}});
inline constexpr drawfx::Iface kSurf{"Surface", 23, kSurfSlots.data()};
// The device's target provider (its slot 6 fills five words: the size record the target-size function reads).
inline constexpr auto kProvSlots = Sparse<7>({{6, {1, Ret::Int, true, "GetSize", "i"}}});
inline constexpr drawfx::Iface kProv{"TargetProvider", 7, kProvSlots.data()};
// The drawer a view holds: slot 27 draws a node with a plane mask.
inline constexpr auto kDrawSlots = Sparse<28>({{27, {2, Ret::Int, true, "DrawNode", "iu"}}});
inline constexpr drawfx::Iface kDraw{"Drawer", 28, kDrawSlots.data()};
#undef SVX_UNSETTLED

constexpr unsigned kWords = 58;
constexpr unsigned kHeld = 16;
constexpr unsigned kViewerBase = 0x70 / 4;

struct NameRecord {
    const char *name;
    uint32_t pad[3];
};

struct Vw {
    Fake fw, evt, conn, bound, dev, mgr, texObj;
    Fake tgt, lvl, ref, prov, drawer; // the texture target (iid 0xb96cc22), its level surface and reference, the device's
                                      // target provider, a view's drawer
    Fake held[kHeld];
    NameRecord record{};
    float box[6];
    float world[7];     // what the held bound's slot 54 answers
    uint32_t flagBits;  // what its TestFlag answers: bit n for flag n
    uint32_t provWords[5];
    cViewerView view;   // the view the cull walk reads (random per seed by the tests)
    bool haveDevice = true;
    bool serviceAlways = true; // (the original's SetBound calls through the event service unchecked: with the framework global
                               // empty it crashes, so the tests keep it set; ours tests the answer)
};
inline Vw gVw;
inline sgtx::World<kWords> gW;

inline cViewerRefNodeBaseImpl *Impl() { return gW.Impl<cViewerRefNodeBaseImpl>(); }
inline cIGZUnknown *Held(Fake *f)
{
    ++f->refs;
    return reinterpret_cast<cIGZUnknown *>(f);
}

// ---- stand-ins ----
inline uint32_t FwEvent(Fake &, const uint32_t *a)
{
    *reinterpret_cast<void **>(uintptr_t(a[2])) = Held(&gVw.held[13]);
    return 1;
}
inline uint32_t FwService(Fake &, const uint32_t *a)
{
    *reinterpret_cast<void **>(uintptr_t(a[2])) = Held(&gVw.evt);
    return 1;
}
inline uint32_t BoxOut(Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gVw.box, sizeof(gVw.box));
    return a[0];
}
inline uint32_t BoxInOut(Fake &, const uint32_t *a)
{
    float box[6];
    const float shift = *reinterpret_cast<const float *>(uintptr_t(a[1]));
    for (unsigned i = 0; i < 6; ++i) box[i] = gVw.box[i] + shift;
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), box, sizeof(box));
    return a[0];
}
inline uint32_t WorldOut(Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gVw.world, sizeof(gVw.world));
    return a[0];
}
// A query that refuses yet leaves a pointer in the out word: what the caller does with it (the original releases it).
inline uint32_t QiRefuseButFill(Fake &, const uint32_t *a)
{
    if (a[1]) *reinterpret_cast<void **>(uintptr_t(a[1])) = &gVw.tgt;
    return 0;
}
inline uint32_t FlagAnswer(Fake &, const uint32_t *a) { return a[0] < 32 ? (gVw.flagBits >> a[0]) & 1u : 0u; }
inline uint32_t ProvFill(Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gVw.provWords, sizeof(gVw.provWords));
    return 0;
}

// A float of any kind the cull and size code may meet: ordinary values, small integers, wide exponents, denormals, signed
// zeros, infinities, NaNs with random payloads (quiet and signalling), the largest finite value.
inline float WildFloat(std::mt19937 &rng)
{
    const uint32_t sign = (rng() & 1u) << 31;
    switch (rng() % 18) {
    case 6: return float(int(rng() % 9) - 4);
    case 7: return std::ldexp(sgtx::RandomFloat(rng), int(rng() % 200) - 100);
    case 8: return std::bit_cast<float>(sign | (uint32_t(rng()) & 0x7fffffu));
    case 9: return std::bit_cast<float>(sign);
    case 10: return std::bit_cast<float>(sign | 0x7f800000u);
    case 11: return std::bit_cast<float>(sign | 0x7f800000u | ((uint32_t(rng()) & 0x7fffffu) | 1u));
    case 12: return std::bit_cast<float>(sign | 0x7f7fffffu);
    case 13: return std::bit_cast<float>(sign | 0x3f800000u);
    default: return sgtx::RandomFloat(rng);
    }
}
inline float NanWith(std::mt19937 &rng) { return std::bit_cast<float>(0x7f800000u | ((uint32_t(rng()) & 0x7fffffu) | 1u) | ((rng() & 1u) << 31)); }
inline uint32_t MgrFind(Fake &, const uint32_t *a)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(a[1]));
    return name[0] == 'x' ? 0xffffffffu : 200u + a[0] + uint32_t(std::strlen(name));
}
inline uint32_t MgrRecord(Fake &, const uint32_t *a)
{
    gVw.record.name = a[1] % 3 == 0 ? nullptr : "texture-name";
    return drawfx::P(&gVw.record);
}

// Slot 17 (a name's id: -1 for names starting 'x') and slot 23 (the texture object for any id but -1).
inline uint32_t MgrFindOrLoad(Fake &, const uint32_t *a)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(a[0]));
    return name[0] == 'x' ? 0xffffffffu : 100u + uint32_t(std::strlen(name));
}
inline uint32_t MgrGetTexture(Fake &, const uint32_t *a) { return a[0] == 0xffffffffu ? 0u : drawfx::P(&gVw.texObj); }

inline void *StandDirector() { return &gVw.fw; }
inline nGZGraphic4::cIDeviceDX *__cdecl StandDevice()
{
    drawfx::Note("0x18ae40", gVw.haveDevice);
    return gVw.haveDevice ? reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gVw.dev) : nullptr;
}
// The patches over the image's code and the getters (both sides at once for the ones that are ours).
struct Stands {
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &StandDirector};
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    Services services;
};

inline uint32_t ReaderArray(Fake &self, const uint32_t *a)
{
    float *out = reinterpret_cast<float *>(uintptr_t(a[0]));
    for (uint32_t i = 0; i < a[1]; ++i) out[i] = std::bit_cast<float>(sgfx::detail::ReaderUint32(self, a));
    return 0;
}
inline uint32_t WriterArray(Fake &, const uint32_t *a)
{
    sgtx::NoteFloats("floats", reinterpret_cast<const float *>(uintptr_t(a[0])), a[1]);
    return 1;
}

inline void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed * 131u + 5u);
    gW.Build(seed, cells);
    Vw &v = gVw;
    v.fw.Reset<kFw>(801);
    v.fw.On(3, &FwEvent);
    v.fw.On(5, &FwService);
    v.evt.Reset<kEvt>(802);
    v.evt.Returns(3, &v.conn);
    v.conn.Reset<kConn>(803);
    v.conn.Returns(12, rng() % 2);
    sgfx::MakeNode<sgfx::kBoundedNode>(v.bound, 804);
    v.bound.AddFace(0xfb859324, v.bound);
    v.bound.AddFace(1, v.bound);
    gW.outer.AddFace(0xfb859324, gW.outer);
    for (unsigned slot : {70u, 73u, 74u, 75u}) v.bound.Returns(slot, uint32_t(rng() % 2));
    v.bound.On(71, &BoxInOut);
    v.bound.On(72, &BoxOut);
    for (float &f : v.box) f = sgtx::RandomFloat(rng);
    v.dev.Reset<kDev>(805);
    v.haveDevice = rng() % 4 != 0;
    v.dev.Returns(35, uint32_t(rng() % 3 != 0));
    v.dev.Returns(39, &v.held[14]);
    v.dev.Returns(40, &v.held[15]);
    v.mgr.Reset<kMgr>(806);
    v.mgr.On(20, &MgrFind);
    v.mgr.On(24, &MgrRecord);
    v.mgr.On(17, &MgrFindOrLoad);
    v.mgr.On(23, &MgrGetTexture);
    v.texObj.Reset<kTex>(807);
    v.texObj.Returns(7, uint32_t(rng()));
    for (unsigned i = 0; i < kHeld; ++i) {
        v.held[i].Reset<kSurf>(820 + i);
        for (unsigned slot : {20u, 21u, 22u}) v.held[i].Returns(slot, uint32_t(rng() % 5 ? rng() % 3000 : rng()));
    }
    // The texture target the handle object answers to (when it does), the device's target provider, the view's drawer.
    v.texObj.Returns(6, uint32_t(rng() % 3 != 0));
    v.tgt.Reset<kTgt>(830);
    v.tgt.Returns(6, uint32_t(rng() % 3 != 0));
    v.tgt.Returns(16, std::bit_cast<uint32_t>(WildFloat(rng)));
    v.tgt.Returns(17, &v.lvl);
    v.tgt.Returns(18, &v.ref);
    v.lvl.Reset<kSurf>(831);
    for (unsigned slot : {20u, 21u, 22u}) v.lvl.Returns(slot, uint32_t(rng() % 5 ? rng() % 3000 : rng()));
    v.ref.Reset<sgfx::kUnknown>(832);
    v.prov.Reset<kProv>(833);
    v.prov.On(6, &ProvFill);
    for (uint32_t &w : v.provWords) w = rng() % 4 ? rng() % 3000 : uint32_t(rng());
    v.provWords[2] = std::bit_cast<uint32_t>(WildFloat(rng));
    v.dev.Returns(16, &v.prov);
    v.drawer.Reset<kDraw>(834);
    if (rng() % 3)
        v.texObj.AddFace(0xb96cc22, v.tgt);
    else if (rng() % 2)
        v.texObj.On(0, &QiRefuseButFill);
    v.bound.On(48, &FlagAnswer);
    v.bound.On(54, &WorldOut);
    v.flagBits = uint32_t(rng());
    for (float &f : v.world) f = WildFloat(rng);
    cells.Set(0x10c9378, rng() % 8 || v.serviceAlways ? static_cast<const void *>(&v.fw) : nullptr);
    for (unsigned i = 0; i < 4; ++i) cells.Set(0x10f8d1c + 4 * i, uint32_t(rng()));
    cells.Set(0x10f8f94, static_cast<const void *>(&v.mgr)); // the texture manager's cache (0xae8750)
    // The reader's and writer's slots the viewer's records use.
    sgfx::gReader.fake.Settle<6, 0>();
    sgfx::gReader.fake.On(6, &sgfx::detail::ReaderUint32);
    sgfx::gReader.fake.Settle<20, 2>();
    sgfx::gReader.fake.On(20, &ReaderArray);
    sgfx::gWriter.fake.Settle<6, 1>();
    sgfx::gWriter.fake.Settle<20, 2>();
    sgfx::gWriter.fake.On(20, &WriterArray);
}

inline void NoteExtras()
{
    Vw &v = gVw;
    for (Fake *f : {&v.fw, &v.evt, &v.conn, &v.bound, &v.dev, &v.mgr, &v.texObj, &v.tgt, &v.lvl, &v.ref, &v.prov, &v.drawer})
        drawfx::Note("fake", {f->id, uint32_t(f->refs)});
    for (const Fake &f : v.held) drawfx::Note("held", {f.id, uint32_t(f.refs)});
    drawfx::Note("outer", {uint32_t(gW.outer.refs)});
}

// A random state of the viewer's part of the block (the references counted, as held ones are).
inline void SeedState(std::mt19937 &rng)
{
    Vw &v = gVw;
    cViewerRefNodeBaseImpl *p = Impl();
    auto pick = [&]() -> cIGZUnknown * { return rng() % 3 == 0 ? nullptr : Held(&v.held[rng() % 12]); };
    p->mRef70 = pick();
    p->mEvent74 = pick();
    p->mWord78 = uint32_t(rng());
    p->mMode = rng() % 5;
    p->mScale = sgtx::RandomFloat(rng);
    for (cIGZUnknown *&t : p->mTextures) t = pick();
    p->mRef94 = pick();
    p->mTextureHandle = rng() % 3 == 0 ? 0xffffffffu : 1u + rng() % 9;
    p->mColorSurface = pick();
    p->mDepthSurface = pick();
    p->mRefA4 = pick();
    p->mRefA8 = pick();
    p->mBound = rng() % 2 ? reinterpret_cast<cIBoundedNode *>(&v.bound) : nullptr;
    for (float &f : p->mPlane) f = sgtx::RandomFloat(rng);
    p->mPlaneFlags = uint32_t(rng());
    p->mWordC4 = uint32_t(rng());
    const uint32_t wordC8 = uint32_t(rng());
    std::memcpy(reinterpret_cast<uint8_t *>(p) + 0xc8, &wordC8, 4);
    p->mWordCC = uint32_t(rng());
    for (float &f : p->mVec) f = sgtx::RandomFloat(rng);
    p->mScaleE0 = sgtx::RandomFloat(rng);
    p->mWordE4 = uint32_t(rng());
}

// The renderable part: the layer words and the registry.
inline void SeedBase(std::mt19937 &rng)
{
    sgrx::Setup(rng);
    sgrx::RandomLayerWords(gW.block, rng);
}

// Where the original dereferences null, for a node whose viewer base Impl is at `base` (cViewerRefNode's +0xc,
// cViewerRefNodeRecursive's +0x10): its mode +0x7c, first texture +0x84 and texture handle +0x98. The original takes the
// first texture unchecked in mode 2 and above (the target size 0xae1000, so also every aspect refresh and the recursive
// viewer's viewport), and asks an invalid handle's object unchecked in mode 0 (0xae10f0 / 0xae1200, and 0xae0e00: the
// count). Both sides skip those calls alike.
inline uint32_t BaseWord(const void *node, uint32_t base, uint32_t offset)
{
    return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(node) + base + offset);
}
inline bool TargetSafe(const void *node, uint32_t base)
{
    return BaseWord(node, base, 0x7c) < 2 || BaseWord(node, base, 0x84) != 0;
}
inline bool HandleSafe(const void *node, uint32_t base)
{
    return BaseWord(node, base, 0x7c) != 0 || BaseWord(node, base, 0x98) != 0xffffffffu;
}

template <class Run> void Check(difftest::Context &ctx, const char *what, Run run, unsigned seeds = sgtx::kSeeds)
{
    sgfx::CheckSides(
        ctx, what, seeds, [](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            run(original, rng);
            gW.NoteWorld();
            NoteExtras();
        });
}

} // namespace svx
