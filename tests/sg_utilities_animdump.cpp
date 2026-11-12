// Differential test of nGZSceneGraph's animation-channel dump (src/sg/SGUtilitiesAnimDump.cpp, lane 261111k).
//
//   SG_UtilitiesAnimDump_DumpAnimationChannels   0xae3050
//
// The dump's sink is compiled out: each line is built in strings on the stack and dropped. So the test compares what
// is observable: every getter call in order (each fake logs itself), the references (each virtual base its own fake),
// and the text of every cRZString::Sprintf (0x1ad6f7, ours, stood in on both sides: the format and the formatted text
// are logged). GetNodeTag (0xae7900, ours) is stood in too: it logs the object asked and answers null, "", a short tag
// or one past the small-string buffer. The image's operator new and sized delete go to ours (the original's
// string assign and append allocate).
//
// The animatable (its own face and virtual base 1) answers a channel count of -1, 0, 1, 2 or 3 and the channels; its
// node (virtual base 1, slot 18) is null or an owner whose QueryInterface says no, yes, yes but false, or 0x100 (al
// clear); the node's source (slot 32) is null or answers slot 24 the same ways. Each channel's virtual base 1 answers
// QueryInterface per iid (the clip 0xc9de69ad, the look-at 0x0a6f2e8d, the locomotion 0xdc1d0085) the same four ways;
// the clip's player (virtual base 2) gives an animation or null; the clip's slot 7 and slot 5 decide the channel tag.
// The channels' numbers are NaN-dense floats and edge integers; the activity answers 0, 1 and 0x100.
#include "sgfx.h"

#include "code_patch.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"

#include <array>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <random>
#include <string>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

template <unsigned N> using Slots = std::array<Slot, N>;
template <unsigned N> constexpr Slots<N> Unsettled()
{
    auto r = Slots<N>{};
    for (unsigned i = 0; i < N; ++i) r[i] = {0, Ret::Int, false, "", ""};
    if (N > 2) {
        r[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
        r[1] = {0, Ret::Int, true, "AddRef", ""};
        r[2] = {0, Ret::Int, true, "Release", ""};
    }
    return r;
}

constexpr Slots<59> AnimatableSlots()
{
    auto r = Unsettled<59>();
    r[9] = {1, Ret::Int, true, "Channel", "u"};
    r[58] = {0, Ret::Int, true, "ChannelCount", ""};
    return r;
}
constexpr auto kAnimatableSlots = AnimatableSlots();
constexpr drawfx::Iface kAnimatable{"Animatable", 59, kAnimatableSlots.data()};

constexpr Slots<19> AnimBaseSlots()
{
    auto r = Unsettled<19>();
    r[18] = {0, Ret::Int, true, "OwnerNode", ""};
    return r;
}
constexpr auto kAnimBaseSlots = AnimBaseSlots();
constexpr drawfx::Iface kAnimBase{"AnimatableBase", 19, kAnimBaseSlots.data()};

constexpr Slots<33> NodeSlots()
{
    auto r = Unsettled<33>();
    r[32] = {0, Ret::Int, true, "Source", ""};
    return r;
}
constexpr auto kNodeSlots = NodeSlots();
constexpr drawfx::Iface kNode{"TransformNode", 33, kNodeSlots.data()};

constexpr Slots<25> SourceSlots()
{
    auto r = Unsettled<25>();
    r[24] = {3, Ret::Int, true, "Animated", "uau"};
    return r;
}
constexpr auto kSourceSlots = SourceSlots();
constexpr drawfx::Iface kSource{"Source", 25, kSourceSlots.data()};

constexpr Slots<15> AnimatedSlots()
{
    auto r = Unsettled<15>();
    r[14] = {0, Ret::Int, true, "Clip", ""};
    return r;
}
constexpr auto kAnimatedSlots = AnimatedSlots();
constexpr drawfx::Iface kAnimated{"Animated", 15, kAnimatedSlots.data()};

constexpr Slots<12> TouchSlots()
{
    auto r = Unsettled<12>();
    r[11] = {0, Ret::Int, true, "Touch", ""};
    return r;
}
constexpr auto kTouchSlots = TouchSlots();
constexpr drawfx::Iface kTouch{"AnimatedClip", 12, kTouchSlots.data()};

constexpr Slots<63> ChannelSlots()
{
    auto r = Unsettled<63>();
    r[35] = {0, Ret::Int, true, "Id", ""};
    r[40] = {0, Ret::Float, true, "Pc", ""};
    r[41] = {0, Ret::Float, true, "Epc", ""};
    r[43] = {0, Ret::Int, true, "P", ""};
    r[44] = {0, Ret::Float, true, "W", ""};
    r[47] = {0, Ret::Int, true, "L", ""};
    r[49] = {0, Ret::Float, true, "S", ""};
    r[61] = {0, Ret::Int, true, "Active", ""};
    r[62] = {0, Ret::Float, true, "Dp", ""};
    return r;
}
constexpr auto kChannelSlots = ChannelSlots();
constexpr drawfx::Iface kChannel{"Channel", 63, kChannelSlots.data()};

constexpr Slots<8> ClipSlots()
{
    auto r = Unsettled<8>();
    r[5] = {1, Ret::Int, true, "Target", "u"};
    r[7] = {0, Ret::Int, true, "HasTarget", ""};
    return r;
}
constexpr auto kClipSlots = ClipSlots();
constexpr drawfx::Iface kClip{"Clip", 8, kClipSlots.data()};

constexpr Slots<35> PlayerSlots()
{
    auto r = Unsettled<35>();
    r[34] = {0, Ret::Int, true, "Animation", ""};
    return r;
}
constexpr auto kPlayerSlots = PlayerSlots();
constexpr drawfx::Iface kPlayer{"Player", 35, kPlayerSlots.data()};

constexpr Slots<37> AnimationSlots()
{
    auto r = Unsettled<37>();
    r[36] = {0, Ret::Float, true, "Weight", ""};
    return r;
}
constexpr auto kAnimationSlots = AnimationSlots();
constexpr drawfx::Iface kAnimation{"Animation", 37, kAnimationSlots.data()};

constexpr Slots<18> AnimationBaseSlots()
{
    auto r = Unsettled<18>();
    r[17] = {0, Ret::Int, true, "Name", ""};
    return r;
}
constexpr auto kAnimationBaseSlots = AnimationBaseSlots();
constexpr drawfx::Iface kAnimationBase{"AnimationBase", 18, kAnimationBaseSlots.data()};

constexpr Slots<10> LookAtSlots()
{
    auto r = Unsettled<10>();
    r[9] = {0, Ret::Int, true, "Name", ""};
    return r;
}
constexpr auto kLookAtSlots = LookAtSlots();
constexpr drawfx::Iface kLookAt{"LookAt", 10, kLookAtSlots.data()};

constexpr Slots<28> LocoSlots()
{
    auto r = Unsettled<28>();
    r[23] = {0, Ret::Float, true, "First", ""};
    r[27] = {0, Ret::Float, true, "Second", ""};
    return r;
}
constexpr auto kLocoSlots = LocoSlots();
constexpr drawfx::Iface kLoco{"Locomotion", 28, kLocoSlots.data()};

struct Embedded {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[3];
};

constexpr uint32_t kIidTransform = 0xe51c10f5, kIidAnimated = 0x3afeef61, kIidClip = 0xc9de69ad, kIidLookAt = 0x0a6f2e8d,
                   kIidLoco = 0xdc1d0085;
constexpr unsigned kChannels = 3;

struct World {
    Fake animatable, animBase, owner, node, source, animated, animatedBase, touch;
    Fake channels[kChannels], channelBases[kChannels];
    Fake clip, clipBase, player, animation, animationBase, lookAt, lookAtBase, loco, locoBase, targets[2];
    Embedded animatableE, animatedE, channelE[kChannels], clipE, animationE, lookAtE, locoE;
    int32_t count;
    uint32_t ownerMode, queryMode, sourceMode, animatedMode, playerMode, hasTarget, targetMode;
    uint32_t clipModes[kChannels], lookAtModes[kChannels], locoModes[kChannels];
    uint32_t tagModes[4]; // node, targets[0], targets[1], anything else
    uint32_t active[kChannels], words[kChannels][9];
    uint32_t names[2];
} gd;

void Link(Embedded &e, Fake &face, Fake *base1, Fake *base2, uint32_t tag)
{
    face.Embed(&e);
    e.vbptr = e.vbtable;
    e.vbtable[0] = -4;
    e.vbtable[1] = base1 ? int32_t(drawfx::P(base1) - drawfx::P(&e.vbptr)) : 0;
    e.vbtable[2] = base2 ? int32_t(drawfx::P(base2) - drawfx::P(&e.vbptr)) : 0;
    drawfx::AddRange(&e, sizeof(e), tag);
}

uint32_t WordOf(const void *p) { return uint32_t(uintptr_t(p)); }

// A query's answer: no, yes, yes but false (the reference stored anyway), or 0x100 (al clear, stored).
uint32_t Answer(uint32_t mode, void **out, void *object, Fake &counted)
{
    if (mode == 0) return 0;
    ++counted.refs;
    *out = object;
    return mode == 1 ? 1u : mode == 2 ? 0u : 0x100u;
}

uint32_t OwnerNode(Fake &, const uint32_t *) { return gd.ownerMode ? WordOf(&gd.owner) : 0; }
uint32_t OwnerQuery(Fake &, const uint32_t *a)
{
    if (a[0] != kIidTransform) return 0;
    return Answer(gd.queryMode, reinterpret_cast<void **>(uintptr_t(a[1])), &gd.node, gd.node);
}
uint32_t Source(Fake &, const uint32_t *) { return gd.sourceMode ? WordOf(&gd.source) : 0; }
uint32_t Animated(Fake &, const uint32_t *a)
{
    if (a[0] != kIidAnimated) return 0;
    return Answer(gd.animatedMode, reinterpret_cast<void **>(uintptr_t(a[1])), &gd.animatedE, gd.animatedBase);
}
uint32_t TouchOf(Fake &, const uint32_t *) { return WordOf(&gd.touch); }

uint32_t ChannelCount(Fake &, const uint32_t *) { return uint32_t(gd.count); }
uint32_t ChannelAt(Fake &, const uint32_t *a) { return WordOf(&gd.channelE[a[0] % kChannels]); }
uint32_t ChannelQuery(Fake &self, const uint32_t *a)
{
    const unsigned i = unsigned(&self - gd.channelBases);
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    switch (a[0]) {
    case kIidClip: return Answer(gd.clipModes[i], out, &gd.clipE, gd.clipBase);
    case kIidLookAt: return Answer(gd.lookAtModes[i], out, &gd.lookAtE, gd.lookAtBase);
    case kIidLoco: return Answer(gd.locoModes[i], out, &gd.locoE, gd.locoBase);
    }
    return 0;
}
int ChannelIndex(const Fake &self) { return int(&self - gd.channels); }
template <unsigned K> uint32_t Number(Fake &self, const uint32_t *) { return gd.words[ChannelIndex(self)][K]; }
uint32_t Active(Fake &self, const uint32_t *) { return gd.active[ChannelIndex(self)]; }

uint32_t HasTarget(Fake &, const uint32_t *) { return gd.hasTarget; }
uint32_t Target(Fake &, const uint32_t *) { return gd.targetMode ? WordOf(&gd.targets[gd.targetMode - 1]) : 0; }
uint32_t AnimationOf(Fake &, const uint32_t *) { return gd.playerMode ? WordOf(&gd.animationE) : 0; }

const char *const kTexts[] = {"", "walk", "a-locomotion-clip-name-past-16", "50%d %s"};
uint32_t AnimationName(Fake &, const uint32_t *) { return WordOf(kTexts[gd.names[0]]); }
uint32_t LookAtName(Fake &, const uint32_t *) { return WordOf(kTexts[gd.names[1]]); }

// GetNodeTag: null, "", a short tag or a long one, by the object asked.
const char *__cdecl StandTag(cICompositionTreeNode *node)
{
    drawfx::Note("tag", drawfx::Norm(drawfx::P(node)));
    const void *const p = node;
    const unsigned which = p == &gd.node ? 0 : p == &gd.targets[0] ? 1 : p == &gd.targets[1] ? 2 : 3;
    static const char *const kTags[] = {nullptr, "", "Tag_head", "a_node_tag_longer_than_sixteen"};
    return kTags[gd.tagModes[which]];
}

// cRZString::Sprintf: the format and the text, logged; the string gets the text.
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    {
        difftest::NotCounted bookkeeping;
        drawfx::Call c;
        c.note = "sprintf";
        c.text = std::string(fmt) + '\x1f' + text + '\x1f';
        drawfx::gLog.push_back(std::move(c));
    }
    self->mString = text;
    return self;
}
void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

uint32_t NumberWord(std::mt19937 &rng)
{
    static constexpr uint32_t kEdges[] = {0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000,
                                          0x00000001, 0x7f7fffff, 0x3ca3d70a, 0x7fc00000, 0xffc00001, 0x7f800123,
                                          0x3dcccccd, 0x447a0000, 0xc479f99a};
    switch (rng() % 5) {
    case 0: return 0x7f800001u | (rng() & 0x807fffffu);
    case 1: case 2: return kEdges[rng() % std::size(kEdges)];
    default: return std::bit_cast<uint32_t>((float(rng() % 200001) / 100000.0f - 1.0f) * 1234.5f);
    }
}
uint32_t IntWord(std::mt19937 &rng)
{
    static constexpr uint32_t kInts[] = {0, 1, 9, 10, 0xffffffff, 0x80000000, 0x7fffffff, 1000};
    return rng() % 2 ? kInts[rng() % std::size(kInts)] : rng();
}

void Build(uint32_t)
{
    gd.animatable.Reset<kAnimatable>(60);
    gd.animatable.On(9, &ChannelAt);
    gd.animatable.On(58, &ChannelCount);
    gd.animBase.Reset<kAnimBase>(61);
    gd.animBase.On(18, &OwnerNode);
    Link(gd.animatableE, gd.animatable, &gd.animBase, nullptr, 0x7d000000);
    gd.owner.Reset<kNode>(62);
    gd.owner.On(0, &OwnerQuery);
    gd.node.Reset<kNode>(63);
    gd.node.On(32, &Source);
    gd.source.Reset<kSource>(64);
    gd.source.On(24, &Animated);
    gd.animated.Reset<kAnimated>(65);
    gd.animated.On(14, &TouchOf);
    gd.animatedBase.Reset<sgfx::kUnknown>(66);
    Link(gd.animatedE, gd.animated, &gd.animatedBase, nullptr, 0x7d100000);
    gd.touch.Reset<kTouch>(67);
    for (unsigned i = 0; i < kChannels; ++i) {
        gd.channels[i].Reset<kChannel>(70 + i);
        gd.channels[i].On(35, &Number<0>);
        gd.channels[i].On(40, &Number<1>);
        gd.channels[i].On(41, &Number<2>);
        gd.channels[i].On(43, &Number<3>);
        gd.channels[i].On(44, &Number<4>);
        gd.channels[i].On(47, &Number<5>);
        gd.channels[i].On(49, &Number<6>);
        gd.channels[i].On(62, &Number<7>);
        gd.channels[i].On(61, &Active);
        gd.channelBases[i].Reset<sgfx::kUnknown>(75 + i);
        gd.channelBases[i].On(0, &ChannelQuery);
        Link(gd.channelE[i], gd.channels[i], &gd.channelBases[i], nullptr, 0x7d200000 + (i << 16));
    }
    gd.clip.Reset<kClip>(80);
    gd.clip.On(5, &Target);
    gd.clip.On(7, &HasTarget);
    gd.clipBase.Reset<sgfx::kUnknown>(81);
    gd.player.Reset<kPlayer>(82);
    gd.player.On(34, &AnimationOf);
    Link(gd.clipE, gd.clip, &gd.clipBase, &gd.player, 0x7d600000);
    gd.animation.Reset<kAnimation>(83);
    gd.animationBase.Reset<kAnimationBase>(84);
    gd.animationBase.On(17, &AnimationName);
    Link(gd.animationE, gd.animation, &gd.animationBase, nullptr, 0x7d700000);
    gd.lookAt.Reset<kLookAt>(85);
    gd.lookAt.On(9, &LookAtName);
    gd.lookAtBase.Reset<sgfx::kUnknown>(86);
    Link(gd.lookAtE, gd.lookAt, &gd.lookAtBase, nullptr, 0x7d800000);
    gd.loco.Reset<kLoco>(87);
    gd.locoBase.Reset<sgfx::kUnknown>(88);
    Link(gd.locoE, gd.loco, &gd.locoBase, nullptr, 0x7d900000);
    gd.targets[0].Reset<sgfx::kUnknown>(90);
    gd.targets[1].Reset<sgfx::kUnknown>(91);
}

} // namespace

DIFF_TEST(SG_UtilitiesAnimDump_DumpAnimationChannels, 0xae3050)
{
    using Fn = void(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae3050);
    shape::CodePatch gameNew(0x824632u, shape::CodeOf(&ImageNew));
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(&ImageSizedDelete));
    shape::StandIn sprintfStand(0x1ad6f7u, &cRZString::Sprintf, &StandSprintf);
    shape::StandIn tag(0xae7900u, &GetNodeTag, &StandTag);
    sgfx::CheckSides(
        ctx, "DumpAnimationChannels", 600, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
        [&](bool isOriginal, std::mt19937 &rng) {
            static constexpr int32_t kCounts[] = {-1, 0, 1, 2, 3, 3};
            gd.count = kCounts[rng() % std::size(kCounts)];
            gd.ownerMode = rng() % 4 != 0;
            gd.queryMode = rng() % 4;
            gd.sourceMode = rng() % 3 != 0;
            gd.animatedMode = rng() % 4;
            gd.playerMode = rng() % 3 != 0;
            static constexpr uint32_t kTargets[] = {0, 1, 0x100, 0xffffffff};
            gd.hasTarget = kTargets[rng() % std::size(kTargets)];
            gd.targetMode = rng() % 3;
            for (uint32_t &m : gd.tagModes) m = rng() % 4;
            for (unsigned i = 0; i < kChannels; ++i) {
                gd.clipModes[i] = rng() % 4;
                gd.lookAtModes[i] = rng() % 4;
                gd.locoModes[i] = rng() % 4;
                static constexpr uint32_t kActive[] = {0, 1, 0x100, 0xff};
                gd.active[i] = kActive[rng() % std::size(kActive)];
                for (unsigned k = 0; k < 8; ++k)
                    gd.words[i][k] = (k == 0 || k == 3 || k == 5) ? IntWord(rng) : NumberWord(rng);
            }
            gd.names[0] = rng() % std::size(kTexts);
            gd.names[1] = rng() % std::size(kTexts);
            gd.animation.Returns(36, NumberWord(rng));
            gd.loco.Returns(23, NumberWord(rng));
            gd.loco.Returns(27, NumberWord(rng));
            drawfx::Note("args", {uint32_t(gd.count), gd.ownerMode, gd.queryMode, gd.sourceMode, gd.animatedMode,
                                  gd.playerMode, gd.hasTarget, gd.targetMode});
            if (isOriginal)
                original(&gd.animatableE);
            else
                DumpAnimationChannels(&gd.animatableE);
            for (const Fake *f : {&gd.animBase, &gd.node, &gd.animatedBase, &gd.clipBase, &gd.lookAtBase, &gd.locoBase,
                                  &gd.channelBases[0], &gd.channelBases[1], &gd.channelBases[2]})
                drawfx::Note("refs", {f->id, uint32_t(f->refs)});
        });
}
