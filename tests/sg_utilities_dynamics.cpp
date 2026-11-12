// Differential test of nGZSceneGraph's dynamics behaviour set-up (src/sg/SGUtilitiesDynamics.cpp, lane 261111k).
//
//   SG_UtilitiesDynamics_MakeDynamicsBehaviors   0xae5840
//
// Every object is a fake that logs its calls (its own id each, the out-parameters' contents compared as floats); the
// framework (0x1ae18d), NameCrc (0xae6870), FindDataList (0xaeb5b0), GetNodeTag (0xae7900) and cRZString::Sprintf
// (0x1ad6f7) are stood in on both sides and log what they are asked. QuatRotate runs as each side has it (the image's,
// ours). The image's operator new and sized delete go to ours (the limit names' strings).
//
// The skeleton table (0x1071460: four {cRZString, float scale} entries; the harness runs no static initialisers) is
// written into the image for the test and put back after: names from the game's four and others, scales NaN-dense.
// The skeleton answers slot 52 per name (no, 1, 0x100, 0x80000000), a NaN-dense scale, and an owner or null; the owner
// answers the tree query no, yes, yes but false, or 0x100. The tree's range holds 0 to 3 nodes; the end test answers
// 0x100 (al clear) as well as 0 while nodes remain. Each node: its transform query the same four ways, a tag (null, "",
// short, long), poses, a data list or none, the list's skin type (10, 9, or not found), value (-1, 0, 1, 2, 3, or
// INT_MIN), rest pose and the six limits found or not. The behaviours, springs and dampers come from the framework
// (the force object or null); the behaviour set's slot 31 answers 0, 1 or 0x100.
#include "sgfx.h"

#include "code_patch.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"

#include <array>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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

constexpr Slots<68> SkeletonSlots()
{
    auto r = Unsettled<68>();
    r[52] = {1, Ret::Int, true, "HasKind", "u"};
    r[66] = {0, Ret::Int, true, "MaskBit", ""};
    r[67] = {0, Ret::Float, true, "Scale", ""};
    return r;
}
constexpr auto kSkeletonSlots = SkeletonSlots();
constexpr drawfx::Iface kSkeleton{"Skeleton", 68, kSkeletonSlots.data()};

constexpr Slots<19> SkeletonBaseSlots()
{
    auto r = Unsettled<19>();
    r[18] = {0, Ret::Int, true, "Owner", ""};
    return r;
}
constexpr auto kSkeletonBaseSlots = SkeletonBaseSlots();
constexpr drawfx::Iface kSkeletonBase{"SkeletonBase", 19, kSkeletonBaseSlots.data()};

constexpr Slots<4> DirectorSlots()
{
    auto r = Unsettled<4>();
    r[3] = {3, Ret::Int, true, "GetClassObject", "uua"};
    return r;
}
constexpr auto kDirectorSlots = DirectorSlots();
constexpr drawfx::Iface kDirector{"Director", 4, kDirectorSlots.data()};

constexpr Slots<19> DynamicsSlots()
{
    auto r = Unsettled<19>();
    r[0] = {1, Ret::Int, true, "SetKind", "s"};
    r[3] = {1, Ret::Int, true, "SetSleep", "f"};
    r[14] = {1, Ret::Int, true, "SetMode", "u"};
    r[18] = {1, Ret::Int, true, "SetBehaviors", "a"};
    return r;
}
constexpr auto kDynamicsSlots = DynamicsSlots();
constexpr drawfx::Iface kDynamics{"Dynamics", 19, kDynamicsSlots.data()};

constexpr Slots<32> BehaviorSetSlots()
{
    auto r = Unsettled<32>();
    r[8] = {1, Ret::Int, true, "SetKind", "s"};
    r[9] = {1, Ret::Int, true, "SetOn", "u"};
    r[10] = {1, Ret::Int, true, "Add", "a"};
    r[31] = {0, Ret::Int, true, "Finish", ""};
    return r;
}
constexpr auto kBehaviorSetSlots = BehaviorSetSlots();
constexpr drawfx::Iface kBehaviorSet{"BehaviorSet", 32, kBehaviorSetSlots.data()};

constexpr Slots<45> TreeSlots()
{
    auto r = Unsettled<45>();
    r[44] = {3, Ret::Int, true, "Range", "aua"};
    return r;
}
constexpr auto kTreeSlots = TreeSlots();
constexpr drawfx::Iface kTree{"Tree", 45, kTreeSlots.data()};

constexpr Slots<7> IterSlots()
{
    auto r = Unsettled<7>();
    r[4] = {1, Ret::Int, true, "IsEqual", "a"};
    r[5] = {0, Ret::Int, true, "Get", ""};
    r[6] = {0, Ret::Int, true, "Next", ""};
    return r;
}
constexpr auto kIterSlots = IterSlots();
constexpr drawfx::Iface kIter{"Iterator", 7, kIterSlots.data()};

constexpr Slots<54> NodeSlots()
{
    auto r = Unsettled<54>();
    r[53] = {1, Ret::Int, true, "Pose", "a"};
    return r;
}
constexpr auto kNodeSlots = NodeSlots();
constexpr drawfx::Iface kNode{"Node", 54, kNodeSlots.data()};

constexpr Slots<20> ListSlots()
{
    auto r = Unsettled<20>();
    r[14] = {2, Ret::Int, true, "GetSint32", "sa"};
    r[15] = {2, Ret::Int, true, "GetFloat", "sa"};
    r[17] = {2, Ret::Int, true, "GetVector3", "sa"};
    r[19] = {2, Ret::Int, true, "GetQuaternion", "sa"};
    return r;
}
constexpr auto kListSlots = ListSlots();
constexpr drawfx::Iface kList{"DataList", 20, kListSlots.data()};

constexpr Slots<10> BehaviorSlots()
{
    auto r = Unsettled<10>();
    r[0] = {1, Ret::Int, true, "SetValue", "u"};
    r[1] = {1, Ret::Int, true, "SetWeight", "f"};
    r[2] = {1, Ret::Int, true, "SetRestPose", "F7"};
    r[3] = {1, Ret::Int, true, "SetLower", "F3"};
    r[4] = {1, Ret::Int, true, "SetUpper", "F3"};
    r[5] = {1, Ret::Int, true, "SetPull", "F3"};
    r[6] = {1, Ret::Int, true, "SetStiffness", "F3"};
    r[7] = {1, Ret::Int, true, "SetDamping", "F3"};
    r[8] = {1, Ret::Int, true, "SetActive", "b"};
    r[9] = {1, Ret::Int, true, "SetFrozen", "u"};
    return r;
}
constexpr auto kBehaviorSlots = BehaviorSlots();
constexpr drawfx::Iface kBehavior{"Behavior", 10, kBehaviorSlots.data()};

constexpr Slots<10> BehaviorBaseSlots()
{
    auto r = Unsettled<10>();
    r[7] = {2, Ret::Int, true, "Attach", "au"};
    r[9] = {1, Ret::Int, true, "SetTag", "s"};
    return r;
}
constexpr auto kBehaviorBaseSlots = BehaviorBaseSlots();
constexpr drawfx::Iface kBehaviorBase{"BehaviorBase", 10, kBehaviorBaseSlots.data()};

struct Embedded {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[3];
};
// A face that is only a virtual base table pointer (the spring and the damper as the framework hands them out).
struct Bare {
    const int32_t *vbptr;
    int32_t vbtable[3];
};

constexpr uint32_t kTreeIid = 0x4512dd13, kTransformIid = 0xe51c10f5;
constexpr unsigned kNodes = 3;
constexpr uint32_t kTable = 0x1071460;

struct World {
    Fake director, skeleton, skeletonBase, owner, dynamics, behaviorSet, tree, iters[3];
    Fake nodes[kNodes], lists[kNodes], behaviors[kNodes], behaviorBases[kNodes];
    Fake forceBases[kNodes], forceTargets[kNodes];
    Embedded skeletonE, behaviorE[kNodes];
    Bare forceE[kNodes];
    void *outSlot;
    unsigned at, nodeCount, behaviorsMade, forcesMade, poseCalls;
    uint32_t kindAnswers[4], ownerMode, treeMode, notEnd;
    uint32_t queryModes[kNodes], tagModes[kNodes], poseModes[kNodes], listModes[kNodes], typeModes[kNodes];
    uint32_t values[kNodes], valueModes[kNodes], restModes[kNodes], limitModes[kNodes][6], forceModes[kNodes];
    uint32_t poseWords[2][7], restWords[kNodes][7], limitWords[kNodes][6];
    uint32_t poseStore[2][7];
} gd;

void Link(Embedded &e, Fake &face, Fake *base1, uint32_t tag)
{
    face.Embed(&e);
    e.vbptr = e.vbtable;
    e.vbtable[0] = -4;
    e.vbtable[1] = base1 ? int32_t(drawfx::P(base1) - drawfx::P(&e.vbptr)) : 0;
    e.vbtable[2] = 0;
    drawfx::AddRange(&e, sizeof(e), tag);
}
void LinkBare(Bare &e, Fake &base1, Fake &base2, uint32_t tag)
{
    e.vbptr = e.vbtable;
    e.vbtable[0] = 0;
    e.vbtable[1] = int32_t(drawfx::P(&base1) - drawfx::P(&e));
    e.vbtable[2] = int32_t(drawfx::P(&base2) - drawfx::P(&e));
    drawfx::AddRange(&e, sizeof(e), tag);
}

uint32_t WordOf(const void *p) { return uint32_t(uintptr_t(p)); }
void *PtrOf(uint32_t w) { return reinterpret_cast<void *>(uintptr_t(w)); }

void NoteText(const char *label, const char *text, uint32_t word)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = label;
    c.text = std::string(text ? text : "(null)") + '\x1f';
    c.words.push_back(word);
    drawfx::gLog.push_back(std::move(c));
}

// A query's answer: no, yes, yes but false (the reference stored anyway), or 0x100 (al clear, stored).
uint32_t Answer(uint32_t mode, uint32_t outWord, void *object, Fake &counted)
{
    if (mode == 0) return 0;
    ++counted.refs;
    *static_cast<void **>(PtrOf(outWord)) = object;
    return mode == 1 ? 1u : mode == 2 ? 0u : 0x100u;
}

// ---- the framework ----
uint32_t DirectorCreate(Fake &, const uint32_t *a)
{
    void **const out = static_cast<void **>(PtrOf(a[2]));
    void *made = nullptr;
    switch (a[0]) {
    case 0xfb00791d: made = &gd.dynamics, ++gd.dynamics.refs; break;
    case 0xa9de45ba: made = &gd.behaviorSet, ++gd.behaviorSet.refs; break;
    case 0x8c1eb8dc:
        if (gd.behaviorsMade < kNodes) {
            const unsigned i = gd.behaviorsMade++;
            made = &gd.behaviorE[i];
            ++gd.behaviorBases[i].refs;
        }
        break;
    case 0x1b115b24:
    case 0x5b0aff38:
        if (gd.forcesMade < kNodes) {
            const unsigned i = gd.forcesMade++;
            if (gd.forceModes[i]) {
                made = &gd.forceE[i];
                ++gd.forceBases[i].refs;
            }
        }
        break;
    }
    *out = made;
    return made ? 1 : 0;
}
void *__cdecl StandFramework()
{
    drawfx::Note("framework", 0);
    return &gd.director;
}

// ---- the skeleton ----
uint32_t TextHash(const char *s)
{
    uint32_t h = 0x811c9dc5;
    for (; s && *s; ++s) h = (h ^ uint8_t(*s)) * 0x01000193;
    return h;
}
uint32_t __cdecl StandCrc(const char *name)
{
    NoteText("NameCrc", name, 0);
    return TextHash(name);
}
uint32_t HasKind(Fake &, const uint32_t *a)
{
    const auto *table = static_cast<const uint8_t *>(PtrOf(WordOf(GameAddress<void>(kTable))));
    for (unsigned k = 0; k < 4; ++k)
        if (TextHash(reinterpret_cast<const cRZString *>(table + 0x24 * k)->ToChar()) == a[0]) return gd.kindAnswers[k];
    return 0;
}
uint32_t OwnerOf(Fake &, const uint32_t *) { return gd.ownerMode ? WordOf(&gd.owner) : 0; }
uint32_t OwnerQuery(Fake &, const uint32_t *a)
{
    if (a[0] != kTreeIid) return 0;
    return Answer(gd.treeMode, a[1], &gd.tree, gd.tree);
}

// ---- the tree and its range ----
uint32_t Range(Fake &, const uint32_t *a)
{
    const uint32_t *filter = static_cast<const uint32_t *>(PtrOf(a[2]));
    drawfx::Note("filter", {drawfx::Norm(filter[0]), filter[1], filter[2]});
    void **const range = static_cast<void **>(PtrOf(a[0]));
    for (unsigned i = 0; i < 3; ++i) {
        range[i] = &gd.iters[i];
        ++gd.iters[i].refs;
    }
    gd.at = 0;
    return 0;
}
uint32_t IsEqual(Fake &, const uint32_t *) { return gd.at >= gd.nodeCount ? 1u : gd.notEnd; }
uint32_t Current(Fake &, const uint32_t *) { return gd.at < kNodes ? WordOf(&gd.nodes[gd.at]) : 0; }
uint32_t Next(Fake &, const uint32_t *)
{
    ++gd.at;
    return 0;
}

// ---- the nodes ----
unsigned NodeIndex(const void *p)
{
    for (unsigned i = 0; i < kNodes; ++i)
        if (p == &gd.nodes[i]) return i;
    return kNodes;
}
uint32_t NodeQuery(Fake &self, const uint32_t *a)
{
    if (a[0] != kTransformIid) return 0;
    return Answer(gd.queryModes[NodeIndex(&self)], a[1], &self, self);
}
uint32_t Pose(Fake &self, const uint32_t *a)
{
    const unsigned k = gd.poseCalls++ % 2;
    std::memcpy(PtrOf(a[0]), gd.poseWords[k], sizeof gd.poseWords[k]);
    if (!gd.poseModes[NodeIndex(&self)]) return a[0];
    std::memcpy(gd.poseStore[k], gd.poseWords[k ^ 1], sizeof gd.poseStore[k]);
    return WordOf(gd.poseStore[k]);
}
const char *__cdecl StandTag(cICompositionTreeNode *node)
{
    drawfx::Note("tag", drawfx::Norm(drawfx::P(node)));
    static const char *const kTags[] = {nullptr, "", "Bone_tail", "a_dynamics_bone_tag_past_sixteen"};
    const unsigned i = NodeIndex(node);
    return kTags[i < kNodes ? gd.tagModes[i] : 0];
}
cIDataListExtension *__cdecl StandFind(cICompositionTreeNode *node, const char *name, cIDataListExtension *after)
{
    NoteText("FindDataList", name, drawfx::Norm(drawfx::P(node)));
    drawfx::Note("after", drawfx::Norm(drawfx::P(after)));
    const unsigned i = NodeIndex(node);
    return i < kNodes && gd.listModes[i] ? reinterpret_cast<cIDataListExtension *>(&gd.lists[i]) : nullptr;
}

// ---- the data lists ----
unsigned ListIndex(const Fake &self) { return unsigned(&self - gd.lists); }
// Writes on yes (1), and on "yes but false" (2); 3 answers 0x100 with the write.
uint32_t Written(uint32_t mode, uint32_t outWord, const uint32_t *words, unsigned count)
{
    if (mode == 0) return 0;
    std::memcpy(PtrOf(outWord), words, count * 4);
    return mode == 1 ? 1u : mode == 2 ? 0u : 0x100u;
}
uint32_t GetSint32(Fake &self, const uint32_t *a)
{
    const unsigned i = ListIndex(self);
    const char *const name = static_cast<const char *>(PtrOf(a[0]));
    if (std::strcmp(name, "DynamicsBehaviorSkinType") == 0) {
        static constexpr uint32_t kTypes[] = {10, 9, 10, 10};
        const uint32_t mode = gd.typeModes[i] % 4;
        return Written(mode == 1 ? 1 : mode == 2 ? 0 : mode == 3 ? 3 : 1, a[1], &kTypes[gd.typeModes[i] / 4 % 4], 1);
    }
    if (std::strcmp(name, "DynamicsBehaviorValue") == 0) return Written(gd.valueModes[i], a[1], &gd.values[i], 1);
    return 0;
}
uint32_t GetFloat(Fake &self, const uint32_t *a)
{
    const unsigned i = ListIndex(self);
    const char *const name = static_cast<const char *>(PtrOf(a[0]));
    const size_t n = std::strlen(name);
    const unsigned k = n ? unsigned(name[n - 1] - '0') : 0;
    if (k >= 6) return 0;
    // A true answer always writes (the original reads the value only then).
    const uint32_t mode = gd.limitModes[i][k];
    return Written(mode, a[1], &gd.limitWords[i][k], 1);
}
uint32_t GetVector(Fake &self, const uint32_t *a)
{
    const unsigned i = ListIndex(self);
    return Written(gd.restModes[i] & 3, a[1], gd.restWords[i], 3);
}
uint32_t GetQuaternion(Fake &self, const uint32_t *a)
{
    const unsigned i = ListIndex(self);
    return Written(gd.restModes[i] >> 2, a[1], gd.restWords[i] + 3, 4);
}
uint32_t Zero(Fake &, const uint32_t *) { return 0; }

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
                                          0x3dcccccd, 0x447a0000, 0xc479f99a, 0x3f19999a, 0x3ecccccd};
    switch (rng() % 5) {
    case 0: return 0x7f800001u | (rng() & 0x807fffffu);
    case 1: return kEdges[rng() % std::size(kEdges)];
    default: return std::bit_cast<uint32_t>((float(rng() % 200001) / 100000.0f - 1.0f) * 2.5f);
    }
}

void Build(uint32_t)
{
    gd.director.Reset<kDirector>(100);
    gd.director.On(3, &DirectorCreate);
    gd.skeleton.Reset<kSkeleton>(101);
    gd.skeleton.On(52, &HasKind);
    gd.skeletonBase.Reset<kSkeletonBase>(102);
    gd.skeletonBase.On(18, &OwnerOf);
    Link(gd.skeletonE, gd.skeleton, &gd.skeletonBase, 0x7d000000);
    gd.owner.Reset<sgfx::kUnknown>(103);
    gd.owner.On(0, &OwnerQuery);
    gd.dynamics.Reset<kDynamics>(104);
    gd.behaviorSet.Reset<kBehaviorSet>(105);
    gd.tree.Reset<kTree>(106);
    gd.tree.On(44, &Range);
    for (unsigned i = 0; i < 3; ++i) {
        gd.iters[i].Reset<kIter>(107 + i);
        gd.iters[i].On(4, &IsEqual);
        gd.iters[i].On(5, &Current);
        gd.iters[i].On(6, &Next);
    }
    for (unsigned i = 0; i < kNodes; ++i) {
        gd.nodes[i].Reset<kNode>(110 + i);
        gd.nodes[i].On(0, &NodeQuery);
        gd.nodes[i].On(53, &Pose);
        gd.lists[i].Reset<kList>(115 + i);
        gd.lists[i].On(14, &GetSint32);
        gd.lists[i].On(15, &GetFloat);
        gd.lists[i].On(17, &GetVector);
        gd.lists[i].On(19, &GetQuaternion);
        gd.behaviors[i].Reset<kBehavior>(120 + i);
        gd.behaviors[i].On(1, &Zero);
        gd.behaviors[i].On(2, &Zero);
        gd.behaviorBases[i].Reset<kBehaviorBase>(125 + i);
        Link(gd.behaviorE[i], gd.behaviors[i], &gd.behaviorBases[i], 0x7d100000 + (i << 16));
        gd.forceBases[i].Reset<sgfx::kUnknown>(130 + i);
        gd.forceTargets[i].Reset<sgfx::kUnknown>(135 + i);
        LinkBare(gd.forceE[i], gd.forceBases[i], gd.forceTargets[i], 0x7d400000 + (i << 16));
    }
    drawfx::AddRange(&gd.outSlot, sizeof gd.outSlot, 0x7d800000);
}

// The image's table, written for the test (four cRZString, float entries) and put back after.
struct Table {
    uint8_t *at;
    uint8_t saved[0x90];
    explicit Table(void *p) : at(static_cast<uint8_t *>(p))
    {
        std::memcpy(saved, at, sizeof saved);
        for (unsigned k = 0; k < 4; ++k) Name(k)->Construct();
    }
    ~Table()
    {
        for (unsigned k = 0; k < 4; ++k) Name(k)->Destruct();
        std::memcpy(at, saved, sizeof saved);
    }
    cRZString *Name(unsigned k) { return reinterpret_cast<cRZString *>(at + 0x24 * k); }
    uint32_t *Scale(unsigned k) { return reinterpret_cast<uint32_t *>(at + 0x24 * k + 0x20); }
};

} // namespace

DIFF_TEST(SG_UtilitiesDynamics_MakeDynamicsBehaviors, 0xae5840)
{
    using Fn = bool(__cdecl *)(void *, uint32_t, uint32_t, void **);
    const Fn original = ctx.Original<Fn>(0xae5840);
    shape::CodePatch gameNew(0x824632u, shape::CodeOf(&ImageNew));
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(&ImageSizedDelete));
    shape::StandIn sprintfStand(0x1ad6f7u, &cRZString::Sprintf, &StandSprintf);
    shape::StandIn tag(0xae7900u, &GetNodeTag, &StandTag);
    shape::StandIn framework(0x1ae18du, &GetDirectorFrameworkObject, &StandFramework);
    shape::StandIn crc(0xae6870u, &NameCrc, &StandCrc);
    shape::StandIn find(0xaeb5b0u, &FindDataList, &StandFind);
    Table table(ctx.image.At(kTable));
    sgfx::CheckSides(
        ctx, "MakeDynamicsBehaviors", 800, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
        [&](bool isOriginal, std::mt19937 &rng) {
            static const char *const kNames[] = {"auskel", "tuskel", "cuskel", "puskel", "", "a_skeleton_kind_past_16"};
            for (unsigned k = 0; k < 4; ++k) {
                table.Name(k)->mString = rng() % 4 ? kNames[k] : kNames[rng() % std::size(kNames)];
                *table.Scale(k) = rng() % 2 ? NumberWord(rng) : (k < 2 ? 0x3f800000u : k == 2 ? 0x3f19999au : 0x3ecccccdu);
                static constexpr uint32_t kAnswers[] = {0, 0, 1, 0x100, 0x80000000};
                gd.kindAnswers[k] = kAnswers[rng() % std::size(kAnswers)];
            }
            gd.skeleton.Returns(66, rng());
            gd.skeleton.Returns(67, NumberWord(rng));
            gd.behaviorSet.Returns(31, std::array<uint32_t, 3>{0, 1, 0x100}[rng() % 3]);
            gd.ownerMode = rng() % 5 != 0;
            gd.treeMode = rng() % 5 == 0 ? rng() % 4 : 1;
            gd.notEnd = rng() % 3 ? 0u : 0x100u;
            gd.nodeCount = rng() % 4;
            gd.at = 0;
            gd.behaviorsMade = gd.forcesMade = gd.poseCalls = 0;
            for (unsigned i = 0; i < kNodes; ++i) {
                gd.queryModes[i] = rng() % 5 == 0 ? rng() % 4 : 1;
                gd.tagModes[i] = rng() % 4 == 0 ? rng() % 3 : 2 + rng() % 2;
                gd.poseModes[i] = rng() % 2;
                gd.listModes[i] = rng() % 5 != 0;
                gd.typeModes[i] = rng() % 5 == 0 ? rng() % 16 : 1;
                static constexpr uint32_t kValues[] = {0xffffffff, 0, 1, 2, 2, 3, 0x80000000};
                gd.values[i] = kValues[rng() % std::size(kValues)];
                gd.valueModes[i] = rng() % 5 == 0 ? rng() % 4 : 1;
                gd.restModes[i] = rng() % 16;
                gd.forceModes[i] = rng() % 5 != 0;
                for (unsigned k = 0; k < 6; ++k) {
                    gd.limitModes[i][k] = rng() % 4;
                    gd.limitWords[i][k] = NumberWord(rng);
                }
                for (uint32_t &w : gd.restWords[i]) w = NumberWord(rng);
            }
            for (auto &pose : gd.poseWords)
                for (uint32_t &w : pose) w = NumberWord(rng);
            // One run in four: no rest offset at all (signed zeros), so the hinge's side tests sit exactly on zero.
            if (rng() % 4 == 0) {
                for (auto &pose : gd.poseWords)
                    for (unsigned k = 0; k < 3; ++k) pose[k] = (rng() & 1u) << 31;
                for (unsigned i = 0; i < kNodes; ++i)
                    for (unsigned k = 0; k < 3; ++k) gd.restWords[i][k] = (rng() & 1u) << 31;
            }
            const uint32_t enable = std::array<uint32_t, 5>{0, 1, 0x100, 0xabcd0001, 0xff}[rng() % 5];
            const uint32_t freeze = std::array<uint32_t, 5>{0, 1, 0x200, 0x5a5a0000, 0x77}[rng() % 5];
            gd.outSlot = nullptr;
            drawfx::Note("args", {gd.ownerMode, gd.treeMode, gd.notEnd, gd.nodeCount, enable, freeze});
            const bool result = isOriginal ? original(&gd.skeletonE, enable, freeze, &gd.outSlot)
                                           : MakeDynamicsBehaviors(&gd.skeletonE, enable, freeze, &gd.outSlot);
            drawfx::Note("result", {uint32_t(result), drawfx::Norm(drawfx::P(gd.outSlot))});
            for (const Fake *f : {&gd.dynamics, &gd.behaviorSet, &gd.tree, &gd.iters[0], &gd.iters[1], &gd.iters[2],
                                  &gd.nodes[0], &gd.nodes[1], &gd.nodes[2], &gd.behaviorBases[0], &gd.behaviorBases[1],
                                  &gd.behaviorBases[2], &gd.forceBases[0], &gd.forceBases[1], &gd.forceBases[2]})
                drawfx::Note("refs", {f->id, uint32_t(f->refs)});
        });
}
