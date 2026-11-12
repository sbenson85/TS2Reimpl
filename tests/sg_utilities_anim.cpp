// Differential tests of nGZSceneGraph's free helpers (src/sg/SGUtilities.h), part 4 (lane 261111c): the two animation
// helpers cTSSGSystem::PlayCinematic calls.
//
//   SG_Utilities_MakeAnimationBlend   0xae4ef0
//   SG_Utilities_AttachAnimation      0xae1f50
//
// Fakes: the framework (0x1ae18d stood in on both sides; slot 3 CreateInstance hands out the blend fake or the
// animation instance, counted), the animation (slot 36 its duration, an x87 float; slot 24 (0) a metadata fake or
// null, whose slot 16 (kind, group) answers by a per-seed script), a blend (slot 19 (elapsed) a float), a target (slot
// 10) and the instance (an object with its own embedded face, slots 0 and 2, and a vbptr at +4 to its cIGZUnknown fake
// and its controller fake, slots 9-26). Every slot call is logged with its argument words (floats as bits); after each
// call the result, *out and every count are noted. Floats come from a set of edge values (zeros of both signs, the
// thresholds 1e-6 and 1e-5 apart, infinities, NaNs with payloads, denormals) and random ones.
#include "sgfx.h"

#include "code_patch.h"
#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"

#include <array>
#include <bit>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kBlendClass = 0x8b969d64, kInstanceClass = 0xc9de69b6;

constexpr Slot kFwSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "CreateInstance", "uui"}, // 3
};
constexpr drawfx::Iface kFw{"Framework", 4, kFwSlots};

template <unsigned N> using Slots = std::array<Slot, N>;
template <unsigned N> constexpr Slots<N> Unsettled()
{
    Slots<N> r{};
    for (unsigned i = 0; i < N; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    return r;
}

// The new blend (0x8b969d59): slot 2 its set-up, 13 words.
constexpr Slots<3> BlendSlots()
{
    auto r = Unsettled<3>();
    r[2] = {13, Ret::Int, true, "Setup", "uuuuuuuuuuuuu"};
    return r;
}
constexpr auto kBlendSlots = BlendSlots();
constexpr drawfx::Iface kNewBlend{"NewBlend", 3, kBlendSlots.data()};

// The animation: slot 24 (0) its metadata, slot 36 () its duration.
constexpr Slots<37> AnimationSlots()
{
    auto r = Unsettled<37>();
    r[24] = {1, Ret::Int, true, "Metadata", "u"};
    r[36] = {0, Ret::Float, true, "Duration", ""};
    return r;
}
constexpr auto kAnimationSlots = AnimationSlots();
constexpr drawfx::Iface kAnimation{"Animation", 37, kAnimationSlots.data()};

constexpr Slots<17> MetadataSlots()
{
    auto r = Unsettled<17>();
    r[16] = {2, Ret::Int, true, "HasKind", "uu"};
    return r;
}
constexpr auto kMetadataSlots = MetadataSlots();
constexpr drawfx::Iface kMetadata{"Metadata", 17, kMetadataSlots.data()};

// A blend handed to AttachAnimation: slot 19 (elapsed) a float.
constexpr Slots<20> CurveSlots()
{
    auto r = Unsettled<20>();
    r[19] = {1, Ret::Float, true, "CurveDuration", "u"};
    return r;
}
constexpr auto kCurveSlots = CurveSlots();
constexpr drawfx::Iface kCurve{"Curve", 20, kCurveSlots.data()};

constexpr Slots<11> TargetSlots()
{
    auto r = Unsettled<11>();
    r[10] = {1, Ret::Int, true, "Attach", "i"};
    return r;
}
constexpr auto kTargetSlots = TargetSlots();
constexpr drawfx::Iface kTarget{"Target", 11, kTargetSlots.data()};

// The instance's own face: slot 0 (scene, placement), slot 2 (1).
constexpr Slots<3> InstanceSlots()
{
    Slots<3> r{};
    r[0] = {2, Ret::Int, true, "Place", "ii"};
    r[1] = {0, Ret::Int, false, "", ""};
    r[2] = {1, Ret::Int, true, "Slot2", "u"};
    return r;
}
constexpr auto kInstanceSlots = InstanceSlots();
constexpr drawfx::Iface kInstanceFace{"AnimationInstance", 3, kInstanceSlots.data()};

// The controller face: slots 9-26, one word each but slot 21 (two).
constexpr Slots<27> ControllerSlots()
{
    auto r = Unsettled<27>();
    for (unsigned i = 9; i <= 26; ++i) r[i] = {1, Ret::Int, true, "Set", "u"};
    r[9].args = r[16].args = r[24].args = "i";
    r[21] = {2, Ret::Int, true, "SetEvent", "uu"};
    return r;
}
constexpr auto kControllerSlots = ControllerSlots();
constexpr drawfx::Iface kController{"Controller", 27, kControllerSlots.data()};

struct Instance {
    void *vt; // the embedded face
    const int32_t *vbptr;
    int32_t vbtable[3];
};

struct World {
    Fake framework, newBlend, animation, metadata, curve, target, unknown, controller, instanceFake;
    Fake scene, placement, events;
    Instance instance;
    uint32_t kinds; // metadata slot 16: bit 0 answers the first kind, bit 1 the second
    bool noMetadata;
} gw;

uint32_t Create(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (a[0] == kBlendClass) {
        ++gw.newBlend.refs;
        *out = &gw.newBlend;
        return 1;
    }
    if (a[0] == kInstanceClass) {
        ++gw.unknown.refs;
        *out = &gw.instance;
        return 1;
    }
    return 0;
}
void *StandFramework() { return &gw.framework; }
// The instance's AddRef notes whether the caller's result cell already holds the instance (the store comes first).
void **gResultCell;
uint32_t UnknownAddRef(Fake &self, const uint32_t *)
{
    if (gResultCell) drawfx::Note("cell holds instance", uint32_t(*gResultCell == &gw.instance));
    return uint32_t(++self.refs);
}
uint32_t Metadata(Fake &, const uint32_t *) { return gw.noMetadata ? 0 : drawfx::P(&gw.metadata); }
uint32_t HasKind(Fake &, const uint32_t *a) { return a[0] == 0xe6e97a71u ? gw.kinds & 1 : (gw.kinds >> 1) & 1; }

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2246822519u + 7u);
    gw.framework.Reset<kFw>(80);
    gw.framework.On(3, &Create);
    gw.newBlend.Reset<kNewBlend>(81);
    gw.animation.Reset<kAnimation>(82);
    gw.animation.On(24, &Metadata);
    gw.metadata.Reset<kMetadata>(83);
    gw.metadata.On(16, &HasKind);
    gw.curve.Reset<kCurve>(84);
    gw.target.Reset<kTarget>(85);
    sgfx::MakeExtension(gw.unknown, 86);
    gw.unknown.On(1, &UnknownAddRef);
    gw.controller.Reset<kController>(87);
    gw.instanceFake.Reset<kInstanceFace>(88);
    gw.scene.Reset<sgfx::kUnknown>(89);
    gw.placement.Reset<sgfx::kUnknown>(90);
    gw.events.Reset<sgfx::kUnknown>(91);
    gw.instanceFake.Embed(&gw.instance);
    gw.instance.vbptr = gw.instance.vbtable;
    gw.instance.vbtable[0] = -4;
    gw.instance.vbtable[1] = int32_t(drawfx::P(&gw.unknown) - drawfx::P(&gw.instance.vbptr));
    gw.instance.vbtable[2] = int32_t(drawfx::P(&gw.controller) - drawfx::P(&gw.instance.vbptr));
    drawfx::AddRange(&gw.instance, sizeof(gw.instance), 0x7b000000);
    gw.kinds = rng() % 4;
    gw.noMetadata = rng() % 4 == 0;
}

// Edge floats: zeros, ones, the thresholds, infinities, NaN payloads, denormals; otherwise random ones of either sign.
float Pick(std::mt19937 &rng)
{
    static constexpr uint32_t kEdges[] = {
        0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x3f000000, 0x40000000, 0x3f800008, 0x3f800054, // 0, 1, .5, 2,
        0x3f80000b, 0x3f7fffef, 0x358637bd, 0x3727c5ac, 0x4effffff, 0x4f000000, 0x7f800000, 0xff800000, // 1+-1e-6/1e-5
        0x7fc12345, 0xffc00001, 0x7fa00042, 0x00000001, 0x80400000, 0x41f00000, 0x43695555, 0x3a83126f,
    };
    switch (rng() % 4) {
    case 0:
    case 1: return std::bit_cast<float>(kEdges[rng() % std::size(kEdges)]);
    case 2: return float(int(rng() % 9) - 3) * 0.5f;
    default: return std::bit_cast<float>(rng()) ;
    }
}
uint32_t PickWord(std::mt19937 &rng) { return std::bit_cast<uint32_t>(Pick(rng)); }

void NoteState(void *out)
{
    drawfx::Note("out", drawfx::Norm(drawfx::P(out)));
    for (const Fake *f : {&gw.newBlend, &gw.animation, &gw.metadata, &gw.curve, &gw.target, &gw.unknown, &gw.scene,
                          &gw.placement, &gw.events})
        drawfx::Note("refs", {f->id, uint32_t(f->refs)});
}

constexpr unsigned kSeeds = 400;

} // namespace

DIFF_TEST(SG_Utilities_MakeAnimationBlend, 0xae4ef0)
{
    using Fn = uint32_t(__cdecl *)(void **, void *, uint32_t, int32_t, uint32_t, uint32_t, int32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xae4ef0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "MakeAnimationBlend", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 4; ++k) {
                             gw.animation.ReturnsFloat(36, Pick(rng));
                             const uint32_t speed = PickWord(rng), end = PickWord(rng), in = PickWord(rng),
                                            out = PickWord(rng), a9 = PickWord(rng), a10 = PickWord(rng),
                                            a11 = PickWord(rng);
                             // A quarter of the starts sit exactly on the zero-duration threshold end - 1e-6.
                             const uint32_t start = rng() % 4 ? PickWord(rng)
                                                              : std::bit_cast<uint32_t>(std::bit_cast<float>(end) - 1e-6f);
                             static constexpr int32_t kLoops[] = {-1, -1, 0, 1, 2, 3, int32_t(0x80000000), -2, 1000};
                             const int32_t loops = kLoops[rng() % std::size(kLoops)];
                             const int32_t mode = int32_t(rng() % 4) - (rng() % 8 == 0 ? 5 : 0);
                             drawfx::Note("args", {speed, uint32_t(loops), start, end, uint32_t(mode), in, out});
                             void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                             uint8_t fits;
                             if (isOriginal)
                                 fits = uint8_t(original(&made, &gw.animation, speed, loops, start, end, mode, in, out,
                                                         a9, a10, a11));
                             else
                                 fits = MakeAnimationBlend(&made, &gw.animation, std::bit_cast<float>(speed), loops,
                                                           std::bit_cast<float>(start), std::bit_cast<float>(end), mode,
                                                           std::bit_cast<float>(in), std::bit_cast<float>(out),
                                                           std::bit_cast<float>(a9), std::bit_cast<float>(a10),
                                                           std::bit_cast<float>(a11));
                             drawfx::Note("fits", fits);
                             NoteState(made);
                         }
                     });
}

DIFF_TEST(SG_Utilities_AttachAnimation, 0xae1f50)
{
    using Fn = uint32_t(__cdecl *)(void **, void *, void *, const void *, void *, int32_t, int32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t, uint32_t, void *, uint32_t, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t, void *, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xae1f50);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(
        ctx, "AttachAnimation", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
        [&](bool isOriginal, std::mt19937 &rng) {
            for (unsigned k = 0; k < 3; ++k) {
                gw.animation.ReturnsFloat(36, Pick(rng));
                gw.curve.ReturnsFloat(19, Pick(rng));
                void *const target = rng() % 4 ? &gw.target : nullptr;
                void *const scene = rng() % 2 ? &gw.scene : nullptr;
                const void *const placement = rng() % 2 ? &gw.placement : nullptr;
                void *const blend = rng() % 3 ? &gw.curve : nullptr;
                void *const events = rng() % 2 ? &gw.events : nullptr;
                static constexpr int32_t kTimes[] = {0, 1, 100, -100, int32_t(0x80000100), int32_t(0x80000000),
                                                     0x7fffffff, 16777217};
                const int32_t now = kTimes[rng() % std::size(kTimes)], start = kTimes[rng() % std::size(kTimes)];
                const uint32_t a7 = PickWord(rng), a8 = PickWord(rng), a9 = rng(), a10 = rng(), a11 = PickWord(rng);
                const uint32_t a13 = rng(), a14 = rng(), a15 = PickWord(rng), a16 = PickWord(rng);
                const uint32_t a17 = rng() % 3 ? 0 : rng(), a18 = rng(), a20 = rng(), a21 = rng();
                drawfx::Note("args", {uint32_t(now), uint32_t(start), a7, a11, a15, a17});
                void *made = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                uint8_t ok;
                gResultCell = &made;
                if (isOriginal)
                    ok = uint8_t(original(&made, target, scene, placement, &gw.animation, now, start, a7, a8, a9, a10,
                                          a11, blend, a13, a14, a15, a16, a17, a18, events, a20, a21));
                else
                    ok = AttachAnimation(&made, target, scene, placement, &gw.animation, now, start,
                                         std::bit_cast<float>(a7), std::bit_cast<float>(a8), a9, a10,
                                         std::bit_cast<float>(a11), blend, a13, a14, std::bit_cast<float>(a15),
                                         std::bit_cast<float>(a16), a17, a18, events, a20, a21);
                gResultCell = nullptr;
                drawfx::Note("ok", ok);
                NoteState(made);
            }
        });
}
