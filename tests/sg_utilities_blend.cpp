// Differential tests of nGZSceneGraph's controller-blend helpers (src/sg/SGUtilitiesBlend.cpp, lane 261111j).
//
//   SG_UtilitiesBlend_BlendOverlapMode        0xae50e0 a blend fake (slot 10 its time, 26 a curve fake or null)
//   SG_UtilitiesBlend_MakeBlendForSource      0xae4d10 the framework (stood in) handing out a new blend; a source fake
//   SG_UtilitiesBlend_MakeTransitionBlend     0xae4900 the same new blend, the phase terms crafted onto the thresholds
//   SG_UtilitiesBlend_StartControllerBlend    0xae5140 a controller fake, its blend or a new one (vbptr objects)
//   SG_UtilitiesBlend_RetimeControllerBlends  0xae5470 a controller object (vbptr: its cIGZUnknown answers the queued
//                                                      blend face), two blend fakes and a curve
//   SG_UtilitiesBlend_MakeControllerInstance  0xae2b50 a new instance object with three virtual bases
//   SG_UtilitiesBlend_ScaleSkeleton           0xae3cd0 a skeleton object, its owner, a traversal over node fakes, bone
//                                                      extensions (vbptr objects)
//
// Every slot call goes to one ordered log with each fake's own id; the caller's out cells are noted inside the fakes'
// calls. Floats are about a third NaN (random payloads and signs, signalling and quiet), with infinities, signed zeros,
// denormals and the thresholds (1e-6, 1e-5, 0.001, 2147483392) and values crafted exactly onto every comparison and one
// ulp either side. Byte results carry garbage high bytes.
#include "sgfx.h"

#include "code_patch.h"
#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

// ---- shared ---------------------------------------------------------------------------------------------------------

struct SlotAt {
    unsigned index;
    Slot slot;
};
template <unsigned N> constexpr std::array<Slot, N> TableOf(std::initializer_list<SlotAt> set)
{
    std::array<Slot, N> r{};
    for (auto &s : r) s = {0, Ret::Int, false, "", ""};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    for (const SlotAt &s : set) r[s.index] = s.slot;
    return r;
}
constexpr Slot F0(const char *name) { return {0, Ret::Float, true, name, ""}; }
constexpr Slot I0(const char *name) { return {0, Ret::Int, true, name, ""}; }
constexpr Slot I1(const char *name, const char *kinds) { return {1, Ret::Int, true, name, kinds}; }

constexpr uint32_t kBlendClass = 0x8b969d64, kInstanceClass = 0x2a6f2e97, kTraversalClass = 0x5ca10ca7;

constexpr Slot kMakerSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "CreateInstance", "uua"}, // 3
};
constexpr drawfx::Iface kMaker{"Framework", 4, kMakerSlots, false, drawfx::kTestTable};

// A blend (0x8b969d64): slot 2 its set-up (13 words, compared bit for bit), the getters, 24 a span, 26 its curve.
constexpr auto kBlendSlots = TableOf<27>({{2, {13, Ret::Int, true, "Setup", "uufffffffffff"}},
                                          {10, F0("Time")},
                                          {11, F0("Elapsed")},
                                          {14, F0("Length")},
                                          {15, F0("FadeIn")},
                                          {16, F0("FadeOut")},
                                          {20, F0("Param20")},
                                          {21, F0("Param21")},
                                          {22, F0("Param22")},
                                          {24, {2, Ret::Float, true, "Span", "ff"}},
                                          {26, I1("Curve", "u")}});
constexpr drawfx::Iface kBlend{"Blend", 27, kBlendSlots.data(), false, drawfx::kTestTable};
constexpr auto kCurveSlots = TableOf<8>({{7, F0("Lead")}});
constexpr drawfx::Iface kCurve{"BlendCurve", 8, kCurveSlots.data(), false, drawfx::kTestTable};
constexpr auto kPlainSlots = TableOf<3>({});
constexpr drawfx::Iface kPlain{"BlendUnknown", 3, kPlainSlots.data(), false, drawfx::kTestTable};

// An object with its own face (an embedded fake) and a vbptr at +4: entry k its k-th virtual base.
struct VObj {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[4];
};
void Wire(VObj &o, Fake &own, std::initializer_list<Fake *> bases, uint32_t tag)
{
    o.vbptr = o.vbtable;
    o.vbtable[0] = -4;
    unsigned k = 1;
    for (Fake *b : bases) o.vbtable[k++] = int32_t(drawfx::P(b) - drawfx::P(&o.vbptr));
    own.Embed(&o);
    drawfx::AddRange(&o, sizeof o, tag);
}

uint32_t gSentinel[2]; // the out cells' starting values (registered)
void **gCell;          // the caller's out cell during a call (noted inside the calls)
void NoteCell(const char *label)
{
    if (gCell) drawfx::Note(label, drawfx::Norm(drawfx::P(*gCell)));
}

// The generators work in bits: on Win32 a float return goes through st0, whose load quiets a signalling NaN, so a
// float-returning generator never hands out one. Values that reach the code under test through memory or the stack
// (arguments, fields) are drawn as words; fakes' float returns are loaded into st0 on both sides anyway.
uint32_t AnyNaNWord(std::mt19937 &rng)
{
    uint32_t m = rng() & 0x007fffffu;
    if (!m) m = 1;
    if (rng() % 3 == 0) m |= 0x00400000u; // quiet; otherwise mostly signalling
    return (rng() & 0x80000000u) | 0x7f800000u | m;
}
float AnyNaN(std::mt19937 &rng) { return std::bit_cast<float>(AnyNaNWord(rng)); }
constexpr uint32_t kEdges[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x3f000000, 0x40000000, 0x358637bd, 0xb58637bd, 0x3727c5ac,
    0x3a83126f, 0x3f7ff972, 0x3c23d70a, 0x3f7ae148, 0x4efffffe, 0x4effffff, 0x4f000000, 0x7f800000, 0xff800000,
    0x00000001, 0x80400000, 0x007fffff, 0x41f00000, 0x43695555, 0xc2c80000, 0x7f7fffff,
};
// About a third NaN; edges; small halves; hundredths; random bits.
uint32_t AnyWord(std::mt19937 &rng)
{
    switch (rng() % 9) {
    case 0:
    case 1:
    case 2: return AnyNaNWord(rng);
    case 3:
    case 4: return kEdges[rng() % std::size(kEdges)];
    case 5: return std::bit_cast<uint32_t>(float(int(rng() % 13) - 4) * 0.5f);
    case 6: return std::bit_cast<uint32_t>(float(int(rng() % 4001) - 1000) * 0.01f);
    case 7: return rng();
    default: return std::bit_cast<uint32_t>(float(rng() % 100000) * 0.001f);
    }
}
float AnyFloat(std::mt19937 &rng) { return std::bit_cast<float>(AnyWord(rng)); }
// A finite non-negative value.
float Plain(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return float(rng() % 1000) * 0.25f;
    case 1: return float(rng() % 100000) * 1e-4f;
    case 2: return std::bit_cast<float>(rng() & 0x4effffffu);
    default: return float(rng() % 8);
    }
}
// x moved by -1, 0 or +1 ulp.
float Nudge(float x, std::mt19937 &rng)
{
    switch (rng() % 3) {
    case 0: return std::nextafter(x, -INFINITY);
    case 1: return std::nextafter(x, INFINITY);
    default: return x;
    }
}
constexpr uint32_t kBytes[] = {0, 1, 0x100, 0x101, 0xff00, 0x80, 0xabcd0000};
uint32_t AnyByte(std::mt19937 &rng) { return kBytes[rng() % std::size(kBytes)]; }
constexpr int32_t kLoops[] = {-1, -1, 0, 1, 1, 2, 3, int32_t(0x80000000), -2, 1000};
float W(uint32_t w) { return std::bit_cast<float>(w); }
uint32_t B(float f) { return std::bit_cast<uint32_t>(f); }

// The framework: CreateInstance hands out what the test set (counted on its cIGZUnknown), or fails.
struct Made {
    uint32_t classId;
    void *object;
    Fake *counted;
};
struct Maker {
    Fake fake;
    Made made[2];
    unsigned count;
    bool fail;
} gk;
uint32_t MakerCreate(Fake &, const uint32_t *a)
{
    NoteCell("cell at Create");
    if (gk.fail) return 0;
    for (unsigned i = 0; i < gk.count; ++i)
        if (gk.made[i].classId == a[0]) {
            ++gk.made[i].counted->refs;
            *reinterpret_cast<void **>(uintptr_t(a[2])) = gk.made[i].object;
            return 1;
        }
    return 0;
}
void *StandMaker() { return &gk.fake; }
void ResetMaker()
{
    gk.fake.Reset<kMaker>(500);
    gk.fake.On(3, &MakerCreate);
    gk.count = 0;
    gk.fail = false;
}
void Offer(uint32_t classId, void *object, Fake &counted) { gk.made[gk.count++] = {classId, object, &counted}; }

void NoteRefs(std::initializer_list<const Fake *> fakes)
{
    for (const Fake *f : fakes) drawfx::Note("refs", {f->id, uint32_t(f->refs)});
}

constexpr unsigned kSeeds = 400;

// ---- BlendOverlapMode -------------------------------------------------------------------------------------------------

struct Overlap {
    Fake blend, curve;
} go;

void BuildOverlap(uint32_t)
{
    go.blend.Reset<kBlend>(510);
    go.curve.Reset<kCurve>(511);
}

// Sets the blend's time and curve: a quarter with no curve; the time on -1e-6 and on the curve's lead, exactly and by
// one ulp, or anything.
void ArmOverlap(Fake &blend, Fake &curve, std::mt19937 &rng)
{
    const bool withCurve = rng() % 4 != 0;
    blend.Returns(26, withCurve ? static_cast<const void *>(&curve) : nullptr);
    const float lead = rng() % 3 ? Plain(rng) * (rng() % 2 ? 1.0f : -1.0f) : AnyFloat(rng);
    curve.ReturnsFloat(7, lead);
    const float edge = float(double(1e-6f) - double(lead));
    float time;
    switch (rng() % 5) {
    case 0: time = Nudge(-1e-6f, rng); break;
    case 1:
    case 2: time = Nudge(edge, rng); break;
    default: time = AnyFloat(rng);
    }
    blend.ReturnsFloat(10, time);
}

// ---- MakeBlendForSource / MakeTransitionBlend ----------------------------------------------------------------------

struct NewBlend {
    Maker *maker;
    Fake blend, unknown, source;
    VObj object;
} gn;

uint32_t NewBlendSetup(Fake &, const uint32_t *)
{
    NoteCell("cell at Setup");
    return 0;
}
void BuildNewBlend(uint32_t)
{
    ResetMaker();
    gn.blend.Reset<kBlend>(520);
    gn.blend.On(2, &NewBlendSetup);
    gn.unknown.Reset<kPlain>(521);
    gn.source.Reset<kBlend>(522);
    Wire(gn.object, gn.blend, {&gn.unknown}, 0x71000000);
    Offer(kBlendClass, &gn.object, gn.unknown);
    drawfx::AddRange(gSentinel, sizeof gSentinel, 0x7b000000);
}

// ---- StartControllerBlend -----------------------------------------------------------------------------------------

constexpr auto kStartCtrlSlots = TableOf<61>({{7, {3, Ret::Int, true, "Announce", "uuu"}},
                                              {13, I1("SetState", "u")},
                                              {15, I1("SetFade", "f")},
                                              {16, I1("SetBlend", "a")},
                                              {18, I1("SetFlag18", "u")},
                                              {30, I0("State")},
                                              {32, I0("Pending")},
                                              {34, I0("Linked")},
                                              {35, I0("StartTick")},
                                              {38, I1("TicksFrom", "u")},
                                              {39, I0("NowTick")},
                                              {44, F0("FadeTime")},
                                              {45, I0("Blend")},
                                              {48, I0("LinkedFirst")},
                                              {56, I0("CanFade")},
                                              {60, I0("Busy")}});
constexpr drawfx::Iface kStartCtrl{"Controller", 61, kStartCtrlSlots.data(), false, drawfx::kTestTable};
constexpr auto kLinkedSlots = TableOf<37>({{36, F0("Level")}});
constexpr drawfx::Iface kLinked{"LinkedController", 37, kLinkedSlots.data(), false, drawfx::kTestTable};

struct Start {
    Fake ctrl, linked, blend, blendUnknown, fresh, freshUnknown;
    VObj blendObject, freshObject;
    float fade[3];
    unsigned fadeCalls;
} gs;

uint32_t StartFade(Fake &, const uint32_t *) { return B(gs.fade[gs.fadeCalls++ % 3]); }

void BuildStart(uint32_t seed)
{
    std::mt19937 rng(seed * 2246822519u + 17u);
    ResetMaker();
    gs.ctrl.Reset<kStartCtrl>(530);
    gs.ctrl.On(44, &StartFade);
    gs.linked.Reset<kLinked>(531);
    gs.blend.Reset<kBlend>(532);
    gs.blendUnknown.Reset<kPlain>(533);
    gs.fresh.Reset<kBlend>(534);
    gs.freshUnknown.Reset<kPlain>(535);
    Wire(gs.blendObject, gs.blend, {&gs.blendUnknown}, 0x72000000);
    Wire(gs.freshObject, gs.fresh, {&gs.freshUnknown}, 0x73000000);
    Offer(kBlendClass, &gs.freshObject, gs.freshUnknown);
    gs.fadeCalls = 0;
    (void)rng;
}

void ArmStart(std::mt19937 &rng)
{
    gs.ctrl.Returns(60, rng() % 6 ? kBytes[rng() % 3 ? 0 : 2] : AnyByte(rng));
    gs.ctrl.Returns(32, rng() % 5 ? kBytes[rng() % 2 ? 0 : 2] : AnyByte(rng));
    gs.ctrl.Returns(48, AnyByte(rng));
    gs.ctrl.Returns(56, AnyByte(rng));
    gs.ctrl.Returns(30, rng() % 7);
    gs.ctrl.Returns(34, rng() % 3 ? static_cast<const void *>(&gs.linked) : nullptr);
    gs.ctrl.Returns(45, rng() % 2 ? static_cast<const void *>(&gs.blendObject) : nullptr);
    static constexpr uint32_t kTicks[] = {0, 1, 100, 0x7fffffff, 0x80000000u, 0xffffffffu, 0x80000100u};
    const uint32_t start = rng() % 2 ? kTicks[rng() % std::size(kTicks)] : rng();
    gs.ctrl.Returns(35, start);
    gs.ctrl.Returns(39, rng() % 3 ? start + (rng() % 5 ? rng() % 200 : rng()) : rng());
    gs.ctrl.Returns(38, rng() % 2 ? rng() % 50 : rng());
    gs.linked.ReturnsFloat(36, rng() % 3 ? std::bit_cast<float>(kEdges[rng() % 3]) : AnyFloat(rng));
    for (float &f : gs.fade) f = rng() % 3 ? Nudge(0.001f, rng) : AnyFloat(rng);
    gs.fadeCalls = 0;
}

// ---- RetimeControllerBlends ---------------------------------------------------------------------------------------

constexpr auto kRetimeCtrlSlots = TableOf<50>({{19, I1("SetRate", "f")},
                                               {35, I0("StartTick")},
                                               {39, I0("NowTick")},
                                               {45, I0("Blend")},
                                               {47, I0("BlendIndex")},
                                               {49, F0("RateScale")}});
constexpr drawfx::Iface kRetimeCtrl{"Controller", 50, kRetimeCtrlSlots.data(), false, drawfx::kTestTable};
constexpr auto kQueryUnknownSlots = TableOf<3>({{0, {2, Ret::Int, true, "QueryInterface", "ua"}}});
constexpr drawfx::Iface kQueryUnknown{"ControllerUnknown", 3, kQueryUnknownSlots.data(), false, drawfx::kTestTable};
constexpr auto kQueuedSlots = TableOf<26>({{7, I1("Clear", "u")}, {25, I0("QueuedBlend")}});
constexpr drawfx::Iface kQueued{"QueuedBlendFace", 26, kQueuedSlots.data(), false, drawfx::kTestTable};
constexpr auto kQueuedBaseSlots = TableOf<48>({{47, I0("BlendIndex")}});
constexpr drawfx::Iface kQueuedBase{"QueuedBlendBase", 48, kQueuedBaseSlots.data(), false, drawfx::kTestTable};

enum : uint32_t { kQiNone, kQiAnswer, kQiDirty };
struct Retime {
    Fake ctrl, ctrlUnknown, queued, queuedUnknown, queuedBase, first, second, curve;
    VObj ctrlObject, queuedObject;
    uint32_t qi;
    float scale[3];
    unsigned scaleCalls;
} gr;

uint32_t RetimeQuery(Fake &, const uint32_t *a)
{
    if (gr.qi == kQiNone) return 0x100;
    ++gr.queuedUnknown.refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &gr.queuedObject;
    return gr.qi == kQiAnswer ? 1 : 0xff00;
}
uint32_t RetimeScale(Fake &, const uint32_t *) { return B(gr.scale[gr.scaleCalls++ % 3]); }

void BuildRetime(uint32_t)
{
    gr.ctrl.Reset<kRetimeCtrl>(540);
    gr.ctrl.On(49, &RetimeScale);
    gr.ctrlUnknown.Reset<kQueryUnknown>(541);
    gr.ctrlUnknown.On(0, &RetimeQuery);
    gr.queued.Reset<kQueued>(542);
    gr.queuedUnknown.Reset<kPlain>(543);
    gr.queuedBase.Reset<kQueuedBase>(544);
    gr.first.Reset<kBlend>(545);
    gr.second.Reset<kBlend>(546);
    gr.curve.Reset<kCurve>(547);
    Wire(gr.ctrlObject, gr.ctrl, {&gr.ctrlUnknown}, 0x74000000);
    Wire(gr.queuedObject, gr.queued, {&gr.queuedUnknown, &gr.queuedBase}, 0x75000000);
}

void ArmBlendGetters(Fake &blend, std::mt19937 &rng)
{
    for (unsigned s : {11u, 14u, 15u, 16u, 20u, 21u, 22u})
        blend.ReturnsFloat(s, rng() % 2 ? Plain(rng) * (rng() % 4 ? 1.0f : -1.0f) : AnyFloat(rng));
    blend.ReturnsFloat(24, rng() % 2 ? Plain(rng) : AnyFloat(rng));
    blend.Returns(2, AnyByte(rng));
}
void ArmRetime(std::mt19937 &rng)
{
    gr.qi = rng() % 4 ? kQiAnswer : rng() % 2 ? kQiDirty : kQiNone;
    const uint32_t start = rng() % 3 ? rng() % 1000 : rng();
    gr.ctrl.Returns(35, start);
    gr.ctrl.Returns(39, rng() % 8 == 0 ? 0x80000100u : rng() % 3 ? start + rng() % 300 : rng());
    gr.ctrl.Returns(45, rng() % 5 ? static_cast<const void *>(&gr.second) : nullptr);
    static constexpr int32_t kIndex[] = {-1, 0, 0, 1, 7, int32_t(0x80000000)};
    gr.ctrl.Returns(47, uint32_t(kIndex[rng() % std::size(kIndex)]));
    gr.queued.Returns(25, rng() % 5 ? static_cast<const void *>(&gr.first) : nullptr);
    gr.queuedBase.Returns(47, uint32_t(kIndex[rng() % std::size(kIndex)]));
    for (float &f : gr.scale) f = rng() % 3 ? (rng() % 2 ? 0.0f : Plain(rng)) : AnyFloat(rng);
    gr.scaleCalls = 0;
    ArmBlendGetters(gr.first, rng);
    ArmBlendGetters(gr.second, rng);
    ArmOverlap(gr.second, gr.curve, rng);
    // Now and then the fades sit on the overlap thresholds against the elapsed ticks: T == elapsed + 1e-6 (or twice).
    if (rng() % 3 == 0) {
        const float elapsed = float(int32_t(rng() % 300));
        float edge = elapsed + 1e-6f;
        if (rng() % 2) edge = edge * 2.0f;
        const float in = Nudge(edge, rng);
        gr.second.ReturnsFloat(15, in);
        gr.second.ReturnsFloat(16, rng() % 2 ? 0.0f : -0.0f);
        gr.ctrl.Returns(35, start);
        gr.ctrl.Returns(39, start + uint32_t(elapsed));
        gr.scale[0] = gr.scale[1] = gr.scale[2] = 0.0f;
    }
}

// ---- MakeControllerInstance ---------------------------------------------------------------------------------------

constexpr auto kInstanceSlots = TableOf<9>({{0, I1("Set0", "u")},
                                            {1, I1("Set1", "f")},
                                            {2, I1("Set2", "f")},
                                            {3, I1("Set3", "u")},
                                            {4, I1("Set4", "u")},
                                            {6, I1("Set6", "u")},
                                            {7, I1("Set7", "u")},
                                            {8, I1("Set8", "u")}});
constexpr drawfx::Iface kInstance{"ControllerInstance", 9, kInstanceSlots.data(), false, drawfx::kTestTable};
constexpr auto kSettingsSlots = TableOf<26>({{10, I1("Set10", "u")},
                                             {11, I1("Set11", "f")},
                                             {12, I1("Set12", "f")},
                                             {13, I1("Set13", "u")},
                                             {14, I1("Set14", "u")},
                                             {15, I1("Set15", "f")},
                                             {17, I1("Set17", "u")},
                                             {18, I1("Set18", "u")},
                                             {19, I1("Set19", "f")},
                                             {24, I1("Set24", "u")},
                                             {25, I1("Set25", "u")}});
constexpr drawfx::Iface kSettings{"InstanceSettings", 26, kSettingsSlots.data(), false, drawfx::kTestTable};
constexpr auto kPlacementSlots = TableOf<3>({{0, {2, Ret::Int, true, "Place", "uF7"}}});
constexpr drawfx::Iface kPlacement{"InstancePlacement", 3, kPlacementSlots.data(), false, drawfx::kTestTable};
constexpr auto kHostSlots = TableOf<11>({{10, I1("Attach", "a")}});
constexpr drawfx::Iface kHost{"InstanceHost", 11, kHostSlots.data(), false, drawfx::kTestTable};

struct InstanceWorld {
    Fake instance, unknown, settings, placement, host;
    VObj object;
    float position[3];
} gi;

uint32_t InstanceAddRef(Fake &self, const uint32_t *)
{
    NoteCell("cell at AddRef");
    return uint32_t(++self.refs);
}
uint32_t InstanceSet0(Fake &, const uint32_t *)
{
    NoteCell("cell at Set0");
    return 0;
}

void BuildInstance(uint32_t)
{
    ResetMaker();
    gi.instance.Reset<kInstance>(550);
    gi.instance.On(0, &InstanceSet0);
    gi.unknown.Reset<kPlain>(551);
    gi.unknown.On(1, &InstanceAddRef);
    gi.settings.Reset<kSettings>(552);
    gi.placement.Reset<kPlacement>(553);
    gi.host.Reset<kHost>(554);
    Wire(gi.object, gi.instance, {&gi.unknown, &gi.settings, &gi.placement}, 0x76000000);
    Offer(kInstanceClass, &gi.object, gi.unknown);
    drawfx::AddRange(gi.position, sizeof gi.position, 0x77000000);
    drawfx::AddRange(gSentinel, sizeof gSentinel, 0x7b000000);
}

// ---- ScaleSkeleton ------------------------------------------------------------------------------------------------

constexpr auto kSkeletonSlots = TableOf<68>({{31, I1("SetScale", "f")}, {66, I0("Filter")}, {67, F0("Scale")}});
constexpr drawfx::Iface kSkeleton{"Skeleton", 68, kSkeletonSlots.data(), false, drawfx::kTestTable};
constexpr auto kSkeletonExtSlots = TableOf<19>({{18, I0("Owner")}});
constexpr drawfx::Iface kSkeletonExt{"SkeletonExtension", 19, kSkeletonExtSlots.data(), false, drawfx::kTestTable};
constexpr auto kOwnerSlots = TableOf<3>({{0, {2, Ret::Int, true, "QueryInterface", "ua"}}});
constexpr drawfx::Iface kOwner{"SkeletonOwner", 3, kOwnerSlots.data(), false, drawfx::kTestTable};
constexpr auto kTraversalSlots = TableOf<23>({{3, I1("Traverse", "a")},
                                              {16, I1("SetFilter", "u")},
                                              {17, I1("Slot17", "u")},
                                              {21, I1("At", "u")},
                                              {22, I0("Count")}});
constexpr drawfx::Iface kTraversal{"Traversal", 23, kTraversalSlots.data(), false, drawfx::kTestTable};
constexpr auto kSkelNodeSlots = TableOf<65>({{24, {3, Ret::Int, true, "FindExtension", "uau"}},
                                             {53, I1("LocalTransform", "a")},
                                             {64, I1("SetTranslation", "F3")}});
constexpr drawfx::Iface kSkelNode{"SkeletonNode", 65, kSkelNodeSlots.data(), false, drawfx::kTestTable};
constexpr auto kBoneSlots = TableOf<9>({{7, I0("Axis")}, {8, F0("Weight")}});
constexpr drawfx::Iface kBone{"BoneScale", 9, kBoneSlots.data(), false, drawfx::kTestTable};

constexpr unsigned kNodes = 5; // four nodes and the model (index 4)
constexpr unsigned kMaxCount = 7;
enum : unsigned { kFindMode, kFindBone, kLocalCopy }; // a node's value words
enum : uint32_t { kFindNone, kFindAnswer, kFindDirty };
struct Skeleton {
    Fake skeleton, ext, owners[2], traversal, nodes[kNodes], bones[2], boneUnknowns[2];
    VObj object, boneObjects[2];
    float local[kNodes][7];
    float scales[3];
    unsigned scaleCalls, ownerCalls;
    uint32_t ownerFirst, ownerLater, order[kMaxCount];
} gx;

uint32_t SkeletonScale(Fake &, const uint32_t *) { return B(gx.scales[gx.scaleCalls++ % 3]); }
uint32_t SkeletonOwner(Fake &, const uint32_t *) { return gx.ownerCalls++ ? gx.ownerLater : gx.ownerFirst; }
uint32_t OwnerQuery(Fake &self, const uint32_t *a)
{
    if (self.value[0] == kQiNone) return 0x100;
    ++gx.nodes[4].refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &gx.nodes[4];
    return self.value[0] == kQiAnswer ? 0x201 : 0xff00;
}
uint32_t TraversalAt(Fake &, const uint32_t *a) { return drawfx::P(&gx.nodes[gx.order[a[0] % kMaxCount]]); }
uint32_t NodeFind(Fake &self, const uint32_t *a)
{
    if (self.value[kFindMode] == kFindNone) return 0xab00;
    const unsigned k = self.value[kFindBone];
    ++gx.boneUnknowns[k].refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &gx.boneObjects[k];
    return self.value[kFindMode] == kFindAnswer ? 0x101 : 0x200;
}
uint32_t NodeLocal(Fake &self, const uint32_t *a)
{
    const float *const local = gx.local[self.id - 600];
    if (!self.value[kLocalCopy]) return drawfx::P(local);
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), local, sizeof gx.local[0]);
    return a[0];
}

void BuildSkeleton(uint32_t seed)
{
    std::mt19937 rng(seed * 2246822519u + 29u);
    ResetMaker();
    gx.skeleton.Reset<kSkeleton>(560);
    gx.skeleton.On(67, &SkeletonScale);
    gx.ext.Reset<kSkeletonExt>(561);
    gx.ext.On(18, &SkeletonOwner);
    for (unsigned i = 0; i < 2; ++i) {
        gx.owners[i].Reset<kOwner>(562 + i);
        gx.owners[i].On(0, &OwnerQuery);
        gx.owners[i].value[0] = rng() % 5 ? kQiAnswer : rng() % 2 ? kQiDirty : kQiNone;
        gx.bones[i].Reset<kBone>(570 + i);
        gx.boneUnknowns[i].Reset<kPlain>(572 + i);
        Wire(gx.boneObjects[i], gx.bones[i], {&gx.boneUnknowns[i]}, 0x79000000 + (i << 20));
        static constexpr uint32_t kAxes[] = {0, 1, 2, 3, 4, 4, 1};
        gx.bones[i].Returns(7, kAxes[rng() % std::size(kAxes)]);
        gx.bones[i].ReturnsFloat(8, rng() % 2 ? Plain(rng) : AnyFloat(rng));
    }
    gx.traversal.Reset<kTraversal>(564);
    gx.traversal.On(21, &TraversalAt);
    static constexpr uint32_t kCounts[] = {0, 1, 2, 3, 5, 7, 7};
    gx.traversal.Returns(22, kCounts[rng() % std::size(kCounts)]);
    for (unsigned i = 0; i < kNodes; ++i) {
        Fake &n = gx.nodes[i];
        n.Reset<kSkelNode>(600 + i);
        n.On(24, &NodeFind);
        n.On(53, &NodeLocal);
        n.value[kFindMode] = rng() % 3 ? kFindAnswer : rng() % 2 ? kFindDirty : kFindNone;
        n.value[kFindBone] = rng() % 2;
        n.value[kLocalCopy] = rng() % 2;
        for (float &c : gx.local[i])
            c = std::bit_cast<float>(rng() % 3 ? B(Plain(rng) * (rng() % 3 ? 1.0f : -1.0f)) : AnyWord(rng));
    }
    for (uint32_t &o : gx.order) o = rng() % kNodes;
    drawfx::AddRange(gx.local, sizeof gx.local, 0x7a000000);
    Wire(gx.object, gx.skeleton, {&gx.ext}, 0x78000000);
    Offer(kTraversalClass, &gx.traversal, gx.traversal);
    gx.ownerFirst = rng() % 6 ? drawfx::P(&gx.owners[0]) : 0;
    gx.ownerLater = drawfx::P(&gx.owners[rng() % 2]);
    gx.ownerCalls = 0;
    gx.skeleton.Returns(66, rng());
    for (float &s : gx.scales) s = rng() % 3 ? Plain(rng) : AnyFloat(rng);
    gx.scaleCalls = 0;
}

} // namespace

DIFF_TEST(SG_UtilitiesBlend_BlendOverlapMode, 0xae50e0)
{
    using Fn = int32_t(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae50e0);
    sgfx::CheckSides(ctx, "BlendOverlapMode", kSeeds * 4, [](uint32_t seed, drawfx::Cells &) { BuildOverlap(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 4; ++k) {
                             ArmOverlap(go.blend, go.curve, rng);
                             const int32_t r = isOriginal ? original(&go.blend) : BlendOverlapMode(&go.blend);
                             drawfx::Note("mode", uint32_t(r));
                         }
                     });
}

DIFF_TEST(SG_UtilitiesBlend_MakeBlendForSource, 0xae4d10)
{
    using Fn = uint8_t(__cdecl *)(void **, uint32_t, void *, uint32_t, uint32_t, int32_t, uint32_t, uint32_t, int32_t,
                                  uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xae4d10);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandMaker};
    sgfx::CheckSides(
        ctx, "MakeBlendForSource", kSeeds, [](uint32_t seed, drawfx::Cells &) { BuildNewBlend(seed); },
        [&](bool isOriginal, std::mt19937 &rng) {
            for (unsigned k = 0; k < 6; ++k) {
                const bool withSource = rng() % 2;
                uint32_t rate = rng() % 3 ? B(Plain(rng)) : AnyWord(rng), a4 = AnyWord(rng), a5 = AnyWord(rng),
                         start = AnyWord(rng), end = AnyWord(rng), in = AnyWord(rng), out = AnyWord(rng);
                if (rng() % 2) a4 = B(Plain(rng)), a5 = B(Plain(rng));
                const int32_t loops = kLoops[rng() % std::size(kLoops)];
                static constexpr int32_t kModes[] = {0, 0, 1, 1, 2, -1, 3};
                const int32_t mode = kModes[rng() % std::size(kModes)];
                const float length = rng() % 3 ? Plain(rng) : AnyFloat(rng);
                gn.source.ReturnsFloat(14, length);
                // Crafted onto the comparisons against the source's length: the fades' sum (or half of it) on
                // length + 1e-6 or length + 1e-5, exactly or one ulp off.
                if (withSource && rng() % 2) {
                    rate = B(rng() % 2 ? 1.0f : Plain(rng));
                    float edge = length + (rng() % 2 ? 1e-6f : 1e-5f);
                    if (mode == 1 || rng() % 4 == 0) edge = edge * 2.0f;
                    in = B(Nudge(edge, rng));
                    out = rng() % 2 ? 0u : 0x80000000u;
                    if (rng() % 2) std::swap(in, out);
                }
                const uint32_t a12 = AnyWord(rng), a13 = AnyWord(rng), a14 = AnyWord(rng);
                void *made = &gSentinel[0];
                gCell = &made;
                void *const source = withSource ? &gn.source : nullptr;
                const uint8_t al =
                    isOriginal ? original(&made, rate, source, a4, a5, loops, start, end, mode, in, out, a12, a13, a14)
                               : uint8_t(MakeBlendForSource(&made, W(rate), source, W(a4), W(a5), loops, W(start),
                                                            W(end), mode, W(in), W(out), W(a12), W(a13), W(a14)));
                gCell = nullptr;
                drawfx::Note("al", al);
                drawfx::Note("made", drawfx::Norm(drawfx::P(made)));
                NoteRefs({&gn.unknown, &gn.source});
            }
        });
}

DIFF_TEST(SG_UtilitiesBlend_MakeTransitionBlend, 0xae4900)
{
    using Fn = uint8_t(__cdecl *)(void **, uint32_t, int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xae4900);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandMaker};
    sgfx::CheckSides(
        ctx, "MakeTransitionBlend", kSeeds, [](uint32_t seed, drawfx::Cells &) { BuildNewBlend(seed); },
        [&](bool isOriginal, std::mt19937 &rng) {
            for (unsigned k = 0; k < 6; ++k) {
                uint32_t w[16]; // w[1] rate, w[3] start, w[4] end, w[5..10] a6-a11, w[11] a12, w[12..15] a13-a16
                for (uint32_t &x : w) x = rng() % 2 ? B(Plain(rng) * (rng() % 4 ? 1.0f : -1.0f)) : AnyWord(rng);
                w[11] = rng() % 2 ? rng() % 2 : rng();
                int32_t loops = kLoops[rng() % std::size(kLoops)];
                switch (rng() % 4) {
                case 0: { // the phase total (a9 scaled) crafted onto length + 1e-6 or + 1e-5
                    w[11] = 0x101 + (rng() % 2 ? 0x1000 : 0);
                    w[1] = B(1.0f), w[3] = B(rng() % 2 ? 0.0f : -0.0f), loops = 1;
                    w[6] = B(1.0f), w[7] = B(rng() % 2 ? 0.0f : -0.0f), w[12] = w[13] = 0;
                    for (unsigned i : {9u, 10u, 14u, 15u}) w[i] = B(Plain(rng));
                    w[8] = B(Plain(rng) + 0.5f);
                    const float total = w[8] ? W(w[8]) * 0.001f : 0.0f;
                    w[4] = B(Nudge(total - (rng() % 2 ? 1e-6f : 1e-5f), rng));
                    break;
                }
                case 1: // the length exactly 2147483392 (no hold)
                    w[1] = B(1.0f), w[3] = 0, w[4] = 0x4efffffe, loops = 1;
                    break;
                default: break;
                }
                void *made = &gSentinel[0];
                gCell = &made;
                const uint8_t al =
                    isOriginal
                        ? original(&made, w[1], loops, w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12],
                                   w[13], w[14], w[15])
                        : uint8_t(MakeTransitionBlend(&made, W(w[1]), loops, W(w[3]), W(w[4]), W(w[5]), W(w[6]), W(w[7]),
                                                      W(w[8]), W(w[9]), W(w[10]), w[11], W(w[12]), W(w[13]), W(w[14]),
                                                      W(w[15])));
                gCell = nullptr;
                drawfx::Note("al", al);
                drawfx::Note("made", drawfx::Norm(drawfx::P(made)));
                NoteRefs({&gn.unknown});
            }
        });
}

DIFF_TEST(SG_UtilitiesBlend_StartControllerBlend, 0xae5140)
{
    using Fn = uint8_t(__cdecl *)(uint32_t, void *, int32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xae5140);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandMaker};
    sgfx::CheckSides(
        ctx, "StartControllerBlend", kSeeds, [](uint32_t seed, drawfx::Cells &) { BuildStart(seed); },
        [&](bool isOriginal, std::mt19937 &rng) {
            for (unsigned k = 0; k < 6; ++k) {
                ArmStart(rng);
                const uint32_t a1 = rng();
                static constexpr int32_t kModes[] = {0, 0, 1, 1, 2, -1};
                const int32_t mode = kModes[rng() % std::size(kModes)];
                uint32_t a4 = rng() % 2 ? B(Plain(rng)) : AnyWord(rng);
                const uint32_t a5 = rng() % 2 ? AnyByte(rng) : rng();
                uint32_t a6 = rng() % 4 ? AnyWord(rng) : (rng() % 2 ? 0u : 0x80000000u);
                // Crafted: the lead exactly at zero after subtracting the fade (a6 == a4, or a6 == a4 / 2).
                if (rng() % 3 == 0) {
                    a4 = B(Plain(rng));
                    a6 = B(Nudge(mode == 1 ? W(a4) * 0.5f : W(a4), rng));
                }
                const uint8_t al = isOriginal ? original(a1, &gs.ctrl, mode, a4, a5, a6)
                                              : uint8_t(StartControllerBlend(a1, &gs.ctrl, mode, W(a4), a5, W(a6)));
                drawfx::Note("al", al);
                NoteRefs({&gs.blendUnknown, &gs.freshUnknown, &gs.linked});
            }
        });
}

DIFF_TEST(SG_UtilitiesBlend_RetimeControllerBlends, 0xae5470)
{
    using Fn = uint8_t(__cdecl *)(void *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xae5470);
    sgfx::CheckSides(ctx, "RetimeControllerBlends", kSeeds, [](uint32_t seed, drawfx::Cells &) { BuildRetime(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned k = 0; k < 6; ++k) {
                             ArmRetime(rng);
                             const uint32_t rate = rng() % 3 ? B(Plain(rng)) : AnyWord(rng);
                             const uint8_t al = isOriginal ? original(&gr.ctrlObject, rate)
                                                           : uint8_t(RetimeControllerBlends(&gr.ctrlObject, W(rate)));
                             drawfx::Note("al", al);
                             NoteRefs({&gr.queuedUnknown, &gr.first, &gr.second});
                         }
                     });
}

DIFF_TEST(SG_UtilitiesBlend_MakeControllerInstance, 0xae2b50)
{
    using Fn = uint8_t(__cdecl *)(void **, void *, uint32_t, uint32_t, const float *, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xae2b50);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandMaker};
    sgfx::CheckSides(
        ctx, "MakeControllerInstance", kSeeds, [](uint32_t seed, drawfx::Cells &) { BuildInstance(seed); },
        [&](bool isOriginal, std::mt19937 &rng) {
            for (unsigned k = 0; k < 3; ++k) {
                for (float &p : gi.position) p = std::bit_cast<float>(AnyWord(rng));
                uint32_t w[16];
                for (uint32_t &x : w) x = rng() % 2 ? rng() : AnyWord(rng);
                void *const host = rng() % 3 ? &gi.host : nullptr;
                void *made = &gSentinel[0];
                gCell = &made;
                const uint8_t al =
                    isOriginal ? original(&made, host, w[3], w[4], gi.position, w[6], w[7], w[8], w[9], w[10], w[11],
                                          w[12], w[13], w[14], w[15])
                               : uint8_t(MakeControllerInstance(&made, host, w[3], w[4], gi.position, W(w[6]), W(w[7]),
                                                                w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15]));
                gCell = nullptr;
                drawfx::Note("al", al);
                drawfx::Note("made", drawfx::Norm(drawfx::P(made)));
                drawfx::Note("position", {B(gi.position[0]), B(gi.position[1]), B(gi.position[2])});
                NoteRefs({&gi.unknown, &gi.host});
            }
        });
}

DIFF_TEST(SG_UtilitiesBlend_ScaleSkeleton, 0xae3cd0)
{
    using Fn = uint8_t(__cdecl *)(void *, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xae3cd0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandMaker};
    sgfx::CheckSides(ctx, "ScaleSkeleton", kSeeds, [](uint32_t seed, drawfx::Cells &) { BuildSkeleton(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         uint32_t scale = rng() % 3 ? B(Plain(rng)) : AnyWord(rng);
                         // a quarter of the time the scale equals the skeleton's (a ratio of exactly one); a fifth
                         // both NaN (the ratio's fdivr picks between the two payloads)
                         const uint32_t skip = rng() % 2 ? AnyByte(rng) : rng();
                         if (rng() % 4 == 0) gx.scales[0] = W(scale);
                         if (rng() % 5 == 0) {
                             scale = AnyNaNWord(rng);
                             gx.scales[0] = AnyNaN(rng);
                         }
                         const uint8_t al = isOriginal ? original(&gx.object, scale, skip)
                                                       : uint8_t(ScaleSkeleton(&gx.object, W(scale), skip));
                         drawfx::Note("al", al);
                         for (unsigned i = 0; i < kNodes; ++i)
                             drawfx::Note("local", {B(gx.local[i][0]), B(gx.local[i][1]), B(gx.local[i][2])});
                         NoteRefs({&gx.nodes[4], &gx.traversal, &gx.boneUnknowns[0], &gx.boneUnknowns[1]});
                     });
}
