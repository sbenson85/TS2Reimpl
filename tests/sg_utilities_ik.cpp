// Differential tests of nGZSceneGraph's look-at and IK helpers (lane 261111i; src/sg/SGUtilitiesIK.cpp).
//
//   SG_UtilitiesIK_QuatInverse          0xc63600  edge floats (NaN payloads, infinities, signed zeros, denormals),
//   SG_UtilitiesIK_QuatMultiply         0xc63690  each result compared bit for bit; `out` also aliasing an input
//   SG_UtilitiesIK_ClampLookAngles      0xae43c0  the four limits, their float neighbours and the edge floats; `out` the
//                                                 same array as `in`, and overlapping it one word on
//   SG_UtilitiesIK_LookAngles           0xae4780  both axis conventions (the flag word's high bytes set too), the angles
//                                                 bit for bit; `out` the same array as the direction, and one word on
//   SG_UtilitiesIK_CanLookAt            0xae4490  a subject fake (slots 49 / 73 / 76 / 77), a head node whose parent and
//                                                 grandparent are found through GetParent (asked twice: the second answer
//                                                 may differ) and QueryInterface (false, true, or false still writing a
//                                                 counted pointer); random transforms, or identities with the point near
//                                                 the forward axis so the angles cross the limits
//   SG_UtilitiesIK_FindNodeByNameConst  0xae75c0  a random tree of node fakes (the sg_utilities_tree.cpp shape): tags
//   SG_UtilitiesIK_FindNodeByTagCrc     0xae76f0  in mixed case, empty and null, skip flags; the CRC find asks a string
//                                                 pool fake (GetCachedObjectB 0xaeb820 stood in on both sides)
//   SG_UtilitiesIK_FindIKTargetNode     0xae37c0  target, rig (a vbptr to its virtual base), settings and animated-face
//                                                 fakes on the same tree; the out cells noted inside the calls
//   SG_UtilitiesIK_QuatToMatrix3        0xc63b20  (lane 261111j) edge and NaN-dense quaternions, strides 3 and 4, out
//   SG_UtilitiesIK_Matrix3ToEuler       0xc63c50  aliasing the input; every axis order (repeated and three-axis, both
//   SG_UtilitiesIK_QuatToEuler          0xc63af0  signs), matrices on the gimbal threshold
//   SG_UtilitiesIK_MakeIKTargetsFromNames 0xae6290 a subject (vbptr at +4) walking up to 12 node fakes tagged with every
//                                                 skipped suffix (exact length, longer, mixed case, near misses, empty,
//                                                 null), the framework handing out the set, list, spaces and targets;
//                                                 cursors with a parent rotation or none; the caller's cell noted inside
//   SG_UtilitiesIK_QuatRotate           0xc63380  (lane 261111k) NaN-dense vectors and quaternions, bit for bit; `out`
//   SG_UtilitiesIK_QuatFromAxisAngle    0xc63510  aliasing an input; axis strides, angles around the fsincos 2^63 limit
//   SG_UtilitiesIK_QuatSlerp            0xc63790  (lane 261111l) dots around 0.95 and 1 by ulps, both signs, NaN-dense
//                                                 inputs, t at 0 / 1 / NaN; `out` aliasing an input
//   SG_UtilitiesIK_Matrix3RotationX / Y 0xc64040 / 0xc640c0  edge angles, strides 3 and 4, untouched words noted
//   SG_UtilitiesIK_Matrix3Multiply      0xc64140  NaN-dense matrices, `out` aliasing either input or both
//   SG_UtilitiesIK_ToleranceForBits     0xc643a0  bits 0-23 (the table 0x10859e8 is never written)
//
// Every fake call is logged in order, with its fake's id; after each call the result and every count are noted.
#include "sgfx.h"

#include "code_patch.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

// ---- edge floats ---------------------------------------------------------------------------------------------------

constexpr uint32_t kLimitBits[] = {0xbf63dec4u, 0x3f20d97cu, 0x3f9c61aau, 0x3f32b8c2u}; // ClampLookAngles' limits
constexpr uint32_t kEdgeBits[] = {
    0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7f800000u, 0xff800000u, // zeros, ones, infinities
    0x7fc00000u, 0x7fc12345u, 0xffc00001u, 0x7f800001u, 0xff9abcdeu,              // NaNs (quiet, payloads, signalling)
    0x00000001u, 0x807fffffu, 0x00800000u, 0x7f7fffffu, 0xff7fffffu,              // denormals, smallest normal, max
};

// The generators hand out bits: on Win32 a float return goes through st0, whose load quiets a signalling NaN, so a
// float-returning generator never produces one. Sites store std::bit_cast<float>(...Word(rng)) into memory.
uint32_t Uniform(std::mt19937 &rng, float lo, float hi)
{
    return std::bit_cast<uint32_t>(std::uniform_real_distribution<float>(lo, hi)(rng));
}

// A limit, its negation, or either's neighbour one ulp either side.
uint32_t NearLimitWord(std::mt19937 &rng)
{
    uint32_t bits = kLimitBits[rng() % 4];
    if (rng() % 2) bits ^= 0x80000000u;
    const int step = int(rng() % 3) - 1;
    return bits + uint32_t(step);
}

// An edge value, a value near a limit, a plain value or random bits.
uint32_t AnyWord(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return kEdgeBits[rng() % std::size(kEdgeBits)];
    case 1:
    case 2: return NearLimitWord(rng);
    case 3: return uint32_t(rng());
    default: return Uniform(rng, -3.0f, 3.0f);
    }
}

// A NaN with a random sign and payload (quiet or signalling), so that two NaN operands are told apart by which one
// comes back.
uint32_t AnyNaNWord(std::mt19937 &rng)
{
    uint32_t payload = uint32_t(rng()) & 0x7fffffu;
    if (payload == 0) payload = 1;
    return (rng() % 2 ? 0xff800000u : 0x7f800000u) | payload;
}

// A quaternion component: mostly plain, often a NaN (operand order shows only when two meet), sometimes an edge.
uint32_t QuatWord(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0:
    case 1: return AnyNaNWord(rng);
    case 2: return AnyWord(rng);
    default: return Uniform(rng, -2.0f, 2.0f);
    }
}
void NoteFloats(const char *label, const float *p, unsigned n)
{
    std::vector<uint32_t> words;
    for (unsigned i = 0; i < n; ++i) words.push_back(std::bit_cast<uint32_t>(p[i]));
    drawfx::Call c;
    c.note = label;
    c.words = std::move(words);
    drawfx::gLog.push_back(std::move(c));
}

constexpr unsigned kPureSeeds = 400;

// For the pure functions: no fakes, only the inputs drawn alike.
template <class Run> void CheckPure(difftest::Context &ctx, const char *what, Run &&run)
{
    sgfx::CheckSides(ctx, what, kPureSeeds, [](uint32_t, drawfx::Cells &) {}, run);
}

// ---- the tree (FindNodeByNameConst, FindNodeByTagCrc, FindIKTargetNode) ------------------------------------------

constexpr unsigned kTreeSlotCount = 62;
constexpr std::array<Slot, kTreeSlotCount> TreeSlots()
{
    std::array<Slot, kTreeSlotCount> r{};
    for (unsigned i = 0; i < kTreeSlotCount; ++i) r[i] = sgfx::kNodeSlots[i];
    return r;
}
constexpr auto kTreeSlots = TreeSlots();
constexpr drawfx::Iface kTreeNode{"TreeNode", kTreeSlotCount, kTreeSlots.data()};

// The string pool: only slot 11 (GetCrc) is asked.
constexpr std::array<Slot, 12> PoolSlots()
{
    std::array<Slot, 12> r{};
    for (auto &s : r) s = {0, Ret::Int, false, "", ""};
    r[11] = {1, Ret::Int, true, "GetCrc", "u"};
    return r;
}
constexpr auto kPoolSlots = PoolSlots();
constexpr drawfx::Iface kPool{"StringPool", 12, kPoolSlots.data(), false, drawfx::kTestTable};

// An extension face (animated face or bone data): the animated face's (cIChoreoQueue's) entry (9), node (49), target
// (51) and count (58).
constexpr std::array<Slot, 59> FaceSlots()
{
    std::array<Slot, 59> r{};
    for (auto &s : r) s = {0, Ret::Int, false, "", ""};
    r[9] = {1, Ret::Int, true, "Entry", "u"};
    r[49] = {1, Ret::Int, true, "FindNode", "u"};
    r[51] = {2, Ret::Int, true, "FindTarget", "ua"};
    r[58] = {0, Ret::Int, true, "EntryCount", ""};
    return r;
}
constexpr auto kFaceSlots = FaceSlots();
constexpr drawfx::Iface kFace{"IkFace", 59, kFaceSlots.data(), false, drawfx::kTestTable};

// A test table: every slot unsettled but AddRef / Release (1, 2) and the ones given.
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

// An object with its own vtable (an embedded fake) and a vbptr: entry 1 its first virtual base, entry 2 its second.
struct VObj {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[3];
};
void Wire(VObj &o, Fake &own, Fake *first, Fake *second, uint32_t tag)
{
    o.vbptr = o.vbtable;
    o.vbtable[0] = -4;
    o.vbtable[1] = first ? int32_t(drawfx::P(first) - drawfx::P(&o.vbptr)) : 0;
    o.vbtable[2] = second ? int32_t(drawfx::P(second) - drawfx::P(&o.vbptr)) : 0;
    own.Embed(&o);
    drawfx::AddRange(&o, sizeof o, tag);
}

constexpr uint32_t kTransformIid = 0xe51c10f5, kAnimatedIid = 0x1aff3bd4, kBoneIid = 0xe9075bbe, kOtherIid = 0x22220003;
const char *const kTags[] = {"Bone", "bone", "BONE", "Bone_L", "root", "", nullptr};
const char *const kSought[] = {"bone", "ROOT", "Bon", "none", ""}; // names are never null (_stricmp ends the process)
// The pool's CRC of each tag handle (handles 1 and 3 share one); the sought CRCs include one nothing has.
constexpr uint32_t kHandleCrcs[] = {0, 0x11111111u, 0x22222222u, 0x11111111u, 0x33333333u};
constexpr uint32_t kSoughtCrcs[] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u, 0};

enum : unsigned { kFlags, kFirstChild, kNext, kLastChild, kTag, kTagHandle, kQi, kExtHead };
enum : uint32_t { kQiAnswers = 1, kQiDirtyMiss = 2 }; // in value[kQi]: answers the transform id; writes even when not

constexpr unsigned kMaxNodes = 10, kMaxExts = 3;

struct Ext {
    Fake base;
    sgfx::TagFace face;
    Fake faceFake;
};
enum : unsigned { kExtKindWord = 6 };
enum : unsigned { kFaceTarget, kFaceBone, kFaceKnown }; // FindTarget's answers, FindNode's mask (faceFake.value)

struct World {
    Fake nodes[kMaxNodes];
    Ext exts[kMaxNodes][kMaxExts];
    unsigned extCount[kMaxNodes];
    unsigned count;
    Fake pool;
} gw;

// The caller's out cells during a FindIKTargetNode call (noted inside the calls), or null.
cICompositionTreeNode **gNodeCell;
void **gBoneCell;
uint32_t *gIdCell; // the id out cell: what it holds shows whether the id was stored before the call
void NoteCells(const char *label)
{
    if (gNodeCell)
        drawfx::Note(label, {drawfx::Norm(drawfx::P(*gNodeCell)), drawfx::Norm(drawfx::P(*gBoneCell)),
                             gIdCell ? *gIdCell : 0u});
}

uint32_t TestFlag(Fake &f, const uint32_t *a) { return (f.value[kFlags] >> (a[0] & 31)) & 1; }
template <unsigned W> uint32_t Word(Fake &f, const uint32_t *) { return f.value[W]; }
uint32_t Children(Fake &f, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = out[1] = f.value[kFirstChild];
    out[2] = 0;
    return a[0];
}
uint32_t Query(Fake &f, const uint32_t *a)
{
    const bool answers = a[0] == kTransformIid && (f.value[kQi] & kQiAnswers);
    if (!answers && !(a[0] == kTransformIid && (f.value[kQi] & kQiDirtyMiss))) return 0;
    ++f.refs;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = drawfx::P(&f);
    return answers;
}

Ext *ExtOfBase(uint32_t base)
{
    for (unsigned n = 0; n < kMaxNodes; ++n)
        for (unsigned k = 0; k < gw.extCount[n]; ++k)
            if (drawfx::P(&gw.exts[n][k].base) == base) return &gw.exts[n][k];
    return nullptr;
}

// Slot 24 (iid, out, start): from `start` or the first, the first extension of kind `iid` (its face, counted).
uint32_t FindExtension(Fake &f, const uint32_t *a)
{
    NoteCells("cells at FindExtension");
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    for (Ext *e = ExtOfBase(a[2] ? a[2] : f.value[kExtHead]); e; e = ExtOfBase(e->base.value[sgfx::kExtNextWord])) {
        if (e->base.value[kExtKindWord] != a[0]) continue;
        ++e->base.refs;
        *out = &e->face;
        return 1;
    }
    return 0;
}

// The animated face's slot 51 (id, bone out): writes its bone (when it has one), answers its node.
uint32_t FindTarget(Fake &f, const uint32_t *a)
{
    NoteCells("cells at FindTarget");
    if (f.value[kFaceBone]) *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = f.value[kFaceBone];
    return f.value[kFaceTarget];
}

// Node ids the IK check asks for: 0x10-0x13 are known where the asked object's mask has the bit; nothing else is.
bool KnownId(uint32_t mask, uint32_t id) { return id >= 0x10 && id < 0x14 && (mask >> (id - 0x10)) & 1; }

// The animated face's slot 49 (id): a node when it knows the id.
uint32_t FaceNode(Fake &f, const uint32_t *a) { return KnownId(f.value[kFaceKnown], a[0]) ? drawfx::P(&gw.nodes[0]) : 0; }

// The animated face's queue (ShowPlayingAnimations): every animated face answers the same entries.
int32_t gQueueCount;
uint32_t gQueue[4];
uint32_t QueueCount(Fake &, const uint32_t *) { return uint32_t(gQueueCount); }
uint32_t QueueEntry(Fake &, const uint32_t *a) { return a[0] < std::size(gQueue) ? gQueue[a[0]] : 0; }

uint32_t PoolCrc(Fake &, const uint32_t *a) { return a[0] < std::size(kHandleCrcs) ? kHandleCrcs[a[0]] : 0x55555555u; }
// The pool is asked for before the node's tag handle: the note places it in the call log.
void *StandPool()
{
    drawfx::Note("pool asked", uint32_t(0));
    return &gw.pool;
}

void MakeExt(Ext &e, uint32_t id, uint32_t kind)
{
    sgfx::MakeExtension(e.base, id);
    e.base.value[kExtKindWord] = kind;
    e.faceFake.Reset<kFace>(id + 1000);
    e.faceFake.On(51, &FindTarget);
    e.faceFake.On(49, &FaceNode);
    e.faceFake.On(58, &QueueCount);
    e.faceFake.On(9, &QueueEntry);
    e.face.vbptr = e.face.vbtable;
    e.face.vbtable[0] = -4;
    e.face.vbtable[1] = int32_t(drawfx::P(&e.base) - drawfx::P(&e.face.vbptr));
    e.faceFake.Embed(&e.face);
    drawfx::AddRange(&e.face, sizeof(e.face), 0x7e000000 | (id << 8));
}

void BuildTree(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 5u);
    gw.count = 1 + rng() % kMaxNodes;
    gw.pool.Reset<kPool>(90);
    gw.pool.On(11, &PoolCrc);
    gNodeCell = nullptr;
    gBoneCell = nullptr;
    gIdCell = nullptr;
    for (unsigned i = 0; i < kMaxNodes; ++i) gw.extCount[i] = 0;
    for (unsigned i = 0; i < gw.count; ++i) {
        Fake &n = gw.nodes[i];
        n.Reset<kTreeNode>(100 + i);
        n.On(0, &Query);
        n.On(24, &FindExtension);
        n.On(26, &Word<kTag>);
        n.On(27, &Word<kTagHandle>);
        n.On(37, &Word<kNext>);
        n.On(38, &Word<kNext>);
        n.On(41, &Children);
        n.On(42, &Children);
        n.On(48, &TestFlag);
        n.value[kFlags] = rng() & 0x3ff;
        n.value[kTag] = drawfx::P(kTags[rng() % std::size(kTags)]);
        n.value[kTagHandle] = rng() % std::size(kHandleCrcs);
        n.value[kQi] = rng() % 4 ? kQiAnswers : rng() % 2 ? kQiDirtyMiss : 0;
        gw.extCount[i] = rng() % (kMaxExts + 1);
        for (unsigned k = 0; k < gw.extCount[i]; ++k) {
            const unsigned kind = rng() % 3;
            MakeExt(gw.exts[i][k], 300 + i * 10 + k, kind == 0 ? kAnimatedIid : kind == 1 ? kBoneIid : kOtherIid);
            if (k) gw.exts[i][k - 1].base.value[sgfx::kExtNextWord] = drawfx::P(&gw.exts[i][k].base);
        }
        n.value[kExtHead] = gw.extCount[i] ? drawfx::P(&gw.exts[i][0].base) : 0;
        if (i == 0) continue;
        Fake &parent = gw.nodes[rng() % i];
        if (parent.value[kLastChild])
            reinterpret_cast<Fake *>(uintptr_t(parent.value[kLastChild]))->value[kNext] = drawfx::P(&n);
        else
            parent.value[kFirstChild] = drawfx::P(&n);
        parent.value[kLastChild] = drawfx::P(&n);
    }
}

void NoteTree()
{
    for (unsigned i = 0; i < gw.count; ++i) {
        drawfx::Note("node", {gw.nodes[i].id, uint32_t(gw.nodes[i].refs)});
        for (unsigned k = 0; k < gw.extCount[i]; ++k)
            drawfx::Note("ext", {gw.exts[i][k].base.id, uint32_t(gw.exts[i][k].base.refs)});
    }
}

cICompositionTreeNode *AsNode(Fake &f) { return reinterpret_cast<cICompositionTreeNode *>(&f); }
Fake &AnyNode(std::mt19937 &rng) { return gw.nodes[rng() % gw.count]; }
int32_t SkipFlag(std::mt19937 &rng) { return rng() % 3 ? int32_t(rng() % 10) : -int32_t(1 + rng() % 3); }
void NoteFound(const char *label, const void *p) { drawfx::Note(label, drawfx::Norm(drawfx::P(p))); }

constexpr unsigned kTreeSeeds = 96;

// The tree built per seed (and `more` after it); `call(isOriginal, rng)` 1-6 times, the counts noted after each.
template <class More, class Call> void CheckTree(difftest::Context &ctx, const char *what, More &&more, Call &&call)
{
    sgfx::CheckSides(ctx, what, kTreeSeeds,
                     [&](uint32_t seed, drawfx::Cells &) {
                         BuildTree(seed);
                         more(seed);
                     },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const unsigned calls = 1 + rng() % 6;
                         for (unsigned k = 0; k < calls; ++k) {
                             call(isOriginal, rng);
                             NoteTree();
                         }
                     });
}

// ---- FindIKTargetNode's target, rig, rig base and settings ---------------------------------------------------------

uint32_t gSentinel[2]; // the out cells' starting values (registered)

constexpr std::array<Slot, 24> TargetSlots()
{
    std::array<Slot, 24> r{};
    for (auto &s : r) s = {0, Ret::Int, false, "", ""};
    r[15] = {0, Ret::Int, true, "Kind", ""};
    r[16] = {0, Ret::Int, true, "Name", ""};
    r[17] = {0, Ret::Int, true, "AltName", ""};
    r[22] = {0, Ret::Int, true, "NameId", ""};
    r[23] = {0, Ret::Int, true, "AltNameId", ""};
    return r;
}
constexpr auto kTargetSlots = TargetSlots();
constexpr drawfx::Iface kTarget{"IkTarget", 24, kTargetSlots.data(), false, drawfx::kTestTable};
constexpr std::array<Slot, 8> RigSlots()
{
    std::array<Slot, 8> r{};
    for (auto &s : r) s = {0, Ret::Int, false, "", ""};
    r[5] = {1, Ret::Int, true, "Node", "u"};
    r[7] = {0, Ret::Int, true, "HasNode", ""};
    return r;
}
constexpr auto kRigSlots = RigSlots();
constexpr drawfx::Iface kRig{"IkRig", 8, kRigSlots.data(), false, drawfx::kTestTable};
constexpr std::array<Slot, 47> RigBaseSlots()
{
    std::array<Slot, 47> r{};
    for (auto &s : r) s = {0, Ret::Int, false, "", ""};
    r[28] = {0, Ret::Int, true, "Settings", ""};
    r[46] = {0, Ret::Int, true, "UsesAltId", ""};
    return r;
}
constexpr auto kRigBaseSlots = RigBaseSlots();
constexpr drawfx::Iface kRigBase{"IkRigBase", 47, kRigBaseSlots.data(), false, drawfx::kTestTable};
constexpr std::array<Slot, 67> SettingsSlots()
{
    std::array<Slot, 67> r{};
    for (auto &s : r) s = {0, Ret::Int, false, "", ""};
    r[66] = {0, Ret::Int, true, "SkipFlag", ""};
    return r;
}
constexpr auto kSettingsSlots = SettingsSlots();
constexpr drawfx::Iface kSettings{"IkSettings", 67, kSettingsSlots.data(), false, drawfx::kTestTable};

enum : unsigned { kIdFirst22, kIdLater22, kIdFirst23, kIdLater23, kIdCalls, kKind };
struct Ik {
    Fake target, rigFake, rigFirstBase, rigBase, settings;
    // {vt (rigFake), vbptr, vbtable}: the rig's first virtual base is rigFirstBase (nothing calls it), its second rigBase.
    struct {
        void *vt;
        const int32_t *vbptr;
        int32_t vbtable[3];
    } rig;
} gik;

uint32_t TargetKind(Fake &f, const uint32_t *)
{
    NoteCells("cells at Kind");
    return f.value[kKind];
}
template <unsigned First> uint32_t TargetId(Fake &f, const uint32_t *) { return f.value[f.value[kIdCalls]++ ? First + 1 : First]; }

void BuildIk(uint32_t)
{
    gik.target.Reset<kTarget>(70);
    gik.target.On(15, &TargetKind);
    gik.target.On(22, &TargetId<kIdFirst22>);
    gik.target.On(23, &TargetId<kIdFirst23>);
    gik.rigFake.Reset<kRig>(71);
    gik.rigFirstBase.Reset<kRigBase>(74);
    gik.rigBase.Reset<kRigBase>(72);
    gik.settings.Reset<kSettings>(73);
    gik.rig.vbptr = gik.rig.vbtable;
    gik.rig.vbtable[0] = -4;
    gik.rig.vbtable[1] = int32_t(drawfx::P(&gik.rigFirstBase) - drawfx::P(&gik.rig.vbptr));
    gik.rig.vbtable[2] = int32_t(drawfx::P(&gik.rigBase) - drawfx::P(&gik.rig.vbptr));
    gik.rigFake.Embed(&gik.rig);
    gik.rigBase.Returns(28, &gik.settings);
    drawfx::AddRange(&gik.rig, sizeof(gik.rig), 0x7d000000);
    drawfx::AddRange(gSentinel, sizeof gSentinel, 0x7b000000);
}

uint32_t AnyId(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return 0;
    case 1: return 0x6e80a11au;
    default: return kSoughtCrcs[rng() % std::size(kSoughtCrcs)];
    }
}

// ---- CanLookAt's subject and neck ----------------------------------------------------------------------------------

constexpr std::array<Slot, 78> SubjectSlots()
{
    std::array<Slot, 78> r{};
    for (auto &s : r) s = {0, Ret::Int, false, "", ""};
    r[49] = {1, Ret::Int, true, "Node", "u"};
    r[73] = {0, Ret::Int, true, "CanLook", ""};
    r[76] = {0, Ret::Int, true, "TurnHolder", ""};
    r[77] = {0, Ret::Int, true, "Convention", ""};
    return r;
}
constexpr auto kSubjectSlots = SubjectSlots();
constexpr drawfx::Iface kSubject{"LookSubject", 78, kSubjectSlots.data(), false, drawfx::kTestTable};

// The neck nodes: head 0, parents 1-2, grandparents 3-4, the frame 5.
enum : unsigned { kHead, kParentA, kParentB, kGrandA, kGrandB, kFrame, kNeckNodes };
enum : unsigned { kUp, kUpLater, kUpCalls, kNeckQi }; // value words: GetParent's first and later answers, QI mode
struct Neck {
    Fake subject;
    Fake nodes[kNeckNodes];
    float local[kNeckNodes][7], world[kNeckNodes][7];
    float turnHolder[7]; // the turn quaternion at +0xc
} gn;

unsigned NeckIndex(const Fake &f) { return f.id - 500; }
uint32_t NeckParent(Fake &f, const uint32_t *) { return f.value[f.value[kUpCalls]++ ? kUpLater : kUp]; }
uint32_t NeckQuery(Fake &f, const uint32_t *a)
{
    if (a[0] != kTransformIid || f.value[kNeckQi] == 0) return 0;
    ++f.refs;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = drawfx::P(&f);
    return f.value[kNeckQi] == 1;
}
uint32_t NeckLocal(Fake &f, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gn.local[NeckIndex(f)], sizeof gn.local[0]);
    return a[0];
}
uint32_t NeckWorld(Fake &f, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gn.world[NeckIndex(f)], sizeof gn.world[0]);
    return a[0];
}

void RandomRigid(std::mt19937 &rng, float *t, bool identity)
{
    std::uniform_real_distribution<float> coord(-5.0f, 5.0f), unit(-1.0f, 1.0f);
    if (identity) {
        const float id[7] = {0, 0, 0, 0, 0, 0, 1};
        std::memcpy(t, id, sizeof id);
        return;
    }
    for (int k = 0; k < 3; ++k) t[k] = coord(rng);
    float q[4], len;
    do {
        len = 0.0f;
        for (float &c : q) c = unit(rng), len += c * c;
    } while (len < 0.01f);
    for (int k = 0; k < 4; ++k) t[3 + k] = q[k] / std::sqrt(len);
}

void BuildNeck(uint32_t seed)
{
    gn.subject.Reset<kSubject>(499);
    for (unsigned i = 0; i < kNeckNodes; ++i) {
        Fake &n = gn.nodes[i];
        n.Reset<kTreeNode>(500 + i);
        n.On(0, &NeckQuery);
        n.On(34, &NeckParent);
        n.On(53, &NeckLocal);
        n.On(54, &NeckWorld);
    }
    gn.subject.Returns(76, gn.turnHolder);
    drawfx::AddRange(gn.turnHolder, sizeof gn.turnHolder, 0x7c000000);
    (void)seed;
}

// One call's scene: the answers, the transforms and the point.
void DrawNeck(std::mt19937 &rng, float *point, bool &able)
{
    const bool aligned = rng() % 2;
    for (unsigned i = 0; i < kNeckNodes; ++i) {
        Fake &n = gn.nodes[i];
        n.value[kUpCalls] = 0;
        n.value[kNeckQi] = rng() % 6 == 0 ? rng() % 3 : 1; // mostly answering
        RandomRigid(rng, gn.local[i], aligned);
        RandomRigid(rng, gn.world[i], aligned);
    }
    RandomRigid(rng, gn.turnHolder, aligned); // words 3-6 are the turn
    // The head's parent: none (one call in eight), A, or A then B; the parents' parent likewise to the grandparents.
    auto link = [&](Fake &n, unsigned a, unsigned b) {
        const unsigned r = rng() % 8;
        n.value[kUp] = r == 0 ? 0 : drawfx::P(&gn.nodes[a]);
        n.value[kUpLater] = r < 5 ? n.value[kUp] : drawfx::P(&gn.nodes[b]);
        if (!n.value[kUp]) n.value[kUpLater] = 0;
    };
    link(gn.nodes[kHead], kParentA, kParentB);
    link(gn.nodes[kParentA], kGrandA, kGrandB);
    link(gn.nodes[kParentB], kGrandB, kGrandA);
    const uint32_t ableWords[] = {0, 1, 0x100, 0x101, 0xffffff00u};
    const uint32_t ableWord = ableWords[rng() % std::size(ableWords)];
    able = (ableWord & 0xff) != 0;
    gn.subject.Returns(73, ableWord);
    gn.subject.Returns(77, rng() % 2 ? 0x2fa18737u : 0x1234u);
    // ORIGINAL-BUG: a subject that can look and has no head node faults in the original (0xae4490 calls through the
    // empty grandparent cell), so only a subject that cannot look goes without one.
    gn.subject.Returns(49, !able && rng() % 3 == 0 ? nullptr : &gn.nodes[kHead]);
    if (aligned) {
        // Near the forward axis of either convention (z when y is up, y otherwise), across the limits.
        std::uniform_real_distribution<float> off(-1.6f, 1.6f);
        const float a = off(rng), b = off(rng);
        const float yUp[3] = {a, b, 1.0f}, zUp[3] = {-a, 1.0f, -b};
        std::memcpy(point, rng() % 2 ? yUp : zUp, 3 * sizeof(float));
        if (rng() % 8 == 0) point[rng() % 3] = std::bit_cast<float>(kEdgeBits[rng() % std::size(kEdgeBits)]);
    } else {
        std::uniform_real_distribution<float> coord(-5.0f, 5.0f);
        for (int k = 0; k < 3; ++k) point[k] = coord(rng);
    }
}

void NoteNeck()
{
    for (unsigned i = 0; i < kNeckNodes; ++i) drawfx::Note("neck", {gn.nodes[i].id, uint32_t(gn.nodes[i].refs)});
}

// ---- CanFindIKTargets' subject, chains, links and targets (on the tree) --------------------------------------------

constexpr auto kCheckSubjectSlots = TableOf<67>({{49, {1, Ret::Int, true, "Node", "u"}}, {66, {0, Ret::Int, true, "SkipFlag", ""}}});
constexpr drawfx::Iface kCheckSubject{"IkSubject", 67, kCheckSubjectSlots.data(), false, drawfx::kTestTable};
constexpr auto kSubjectBaseSlots = TableOf<19>({{18, {0, Ret::Int, true, "ModelHolder", ""}}});
constexpr drawfx::Iface kSubjectBase{"IkSubjectBase", 19, kSubjectBaseSlots.data(), false, drawfx::kTestTable};
constexpr auto kUnknownSlots = TableOf<3>({{0, {2, Ret::Int, true, "QueryInterface", "ua"}}});
constexpr drawfx::Iface kUnknown{"IkUnknown", 3, kUnknownSlots.data(), false, drawfx::kTestTable};
constexpr auto kChainsSlots = TableOf<52>({{20, {1, Ret::Int, true, "At", "u"}},
                                           {36, {0, Ret::Int, true, "LinkCount", ""}},
                                           {51, {0, Ret::Int, true, "ChainCount", ""}}});
constexpr drawfx::Iface kChains{"IkChains", 52, kChainsSlots.data(), false, drawfx::kTestTable};
constexpr auto kLinkSlots = TableOf<48>({{23, {1, Ret::Int, true, "Target", "u"}},
                                         {42, {0, Ret::Int, true, "NodeId", ""}},
                                         {43, {0, Ret::Int, true, "AltNodeId", ""}},
                                         {47, {0, Ret::Int, true, "TargetCount", ""}}});
constexpr drawfx::Iface kLink{"IkLink", 48, kLinkSlots.data(), false, drawfx::kTestTable};

// List fakes (the chain set, chains, links): value[0] the count, [1..4] the items, [5] the item past them (a stopper
// that ends the check, for the counts with the top bit set).
enum : unsigned { kListCount, kListItems, kListPast = 5 };
uint32_t ListCount(Fake &f, const uint32_t *) { return f.value[kListCount]; }
uint32_t ListAt(Fake &f, const uint32_t *a) { return a[0] < 4 ? f.value[kListItems + a[0]] : f.value[kListPast]; }

enum : unsigned { kHolderMode, kHolderNode }; // 0 no, 1 answers, 2 no but writes; the tree node it gives
enum : unsigned { kSubjectKnown };
constexpr uint32_t kModelIid = 0x4512dd13, kStopId = 0xdead;
constexpr unsigned kNormalLinks = 5, kNormalTargets = 7;
struct IkCheck {
    Fake subjectOwn, subjectBase, holder, chainSet, chains[3], links[kNormalLinks + 1], targets[kNormalTargets + 1];
    VObj subject;
} gc;

uint32_t HolderQuery(Fake &f, const uint32_t *a)
{
    if (a[0] != kModelIid || f.value[kHolderMode] == 0) return 0;
    Fake &node = gw.nodes[f.value[kHolderNode] % gw.count];
    ++node.refs;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = drawfx::P(&node);
    return f.value[kHolderMode] == 1;
}
uint32_t SubjectNode(Fake &f, const uint32_t *a)
{
    return KnownId(f.value[kSubjectKnown], a[0]) ? drawfx::P(&gw.nodes[gw.count - 1]) : 0;
}

void BuildCheck(uint32_t)
{
    gc.subjectOwn.Reset<kCheckSubject>(50);
    gc.subjectBase.Reset<kSubjectBase>(51);
    gc.holder.Reset<kUnknown>(52);
    gc.chainSet.Reset<kChains>(53);
    Wire(gc.subject, gc.subjectOwn, &gc.subjectBase, nullptr, 0x7a000000);
    gc.subjectOwn.On(49, &SubjectNode);
    gc.subjectBase.Returns(18, &gc.holder);
    gc.holder.On(0, &HolderQuery);
    gc.chainSet.On(51, &ListCount);
    gc.chainSet.On(20, &ListAt);
    for (unsigned i = 0; i < 3; ++i) {
        gc.chains[i].Reset<kChains>(54 + i);
        gc.chains[i].On(36, &ListCount);
        gc.chains[i].On(20, &ListAt);
    }
    for (unsigned i = 0; i <= kNormalLinks; ++i) {
        gc.links[i].Reset<kLink>(60 + i);
        gc.links[i].On(47, &ListCount);
        gc.links[i].On(23, &ListAt);
    }
    for (unsigned i = 0; i <= kNormalTargets; ++i) gc.targets[i].Reset<kTarget>(70 + i);
    // The stoppers: chain 2 holds link 5 (on a node the subject never has); target 7 looks in `other` for an id
    // nothing has, by a name nothing has.
    gc.chains[2].value[kListCount] = 1;
    gc.chains[2].value[kListItems] = drawfx::P(&gc.links[kNormalLinks]);
    gc.links[kNormalLinks].Returns(42, kStopId);
    gc.links[kNormalLinks].Returns(43, kStopId);
    Fake &stop = gc.targets[kNormalTargets];
    stop.Returns(15, 2u);
    stop.Returns(16, "none");
    stop.Returns(17, "none");
    stop.Returns(22, kStopId);
    stop.Returns(23, kStopId);
}

// A list count: mostly 0-3, sometimes one with the top bit set (the loops compare unsigned).
uint32_t ListSize(std::mt19937 &rng)
{
    const uint32_t big[] = {0x80000000u, 0xffffffffu};
    return rng() % 10 == 0 ? big[rng() % 2] : rng() % 4;
}

// One call's answers.
void DrawCheck(std::mt19937 &rng)
{
    const uint32_t nodeIds[] = {0, 0x10, 0x11, 0x12, 0x13};
    const uint32_t targetIds[] = {0, 0x6e80a11au, 0x10, 0x11, 0x12, 0x13, kStopId};
    gc.subjectOwn.value[kSubjectKnown] = rng() & 0xf;
    gc.subjectOwn.Returns(66, uint32_t(SkipFlag(rng)));
    gc.holder.value[kHolderMode] = rng() % 3;
    gc.holder.value[kHolderNode] = rng();
    for (unsigned i = 0; i < gw.count; ++i)
        for (unsigned k = 0; k < gw.extCount[i]; ++k) gw.exts[i][k].faceFake.value[kFaceKnown] = rng() & 0xf;
    gc.chainSet.value[kListCount] = ListSize(rng);
    for (unsigned k = 0; k < 4; ++k) gc.chainSet.value[kListItems + k] = drawfx::P(&gc.chains[rng() % 2]);
    gc.chainSet.value[kListPast] = drawfx::P(&gc.chains[2]);
    for (unsigned i = 0; i < 2; ++i) {
        Fake &c = gc.chains[i];
        c.value[kListCount] = ListSize(rng);
        for (unsigned k = 0; k < 4; ++k) c.value[kListItems + k] = drawfx::P(&gc.links[rng() % kNormalLinks]);
        c.value[kListPast] = drawfx::P(&gc.links[kNormalLinks]);
    }
    for (unsigned i = 0; i < kNormalLinks; ++i) {
        Fake &l = gc.links[i];
        l.Returns(42, nodeIds[rng() % std::size(nodeIds)]);
        l.Returns(43, nodeIds[rng() % std::size(nodeIds)]);
        l.value[kListCount] = ListSize(rng);
        for (unsigned k = 0; k < 4; ++k) l.value[kListItems + k] = drawfx::P(&gc.targets[rng() % kNormalTargets]);
        l.value[kListPast] = drawfx::P(&gc.targets[kNormalTargets]);
    }
    const uint32_t kindWords[] = {0, 1, 2, 3, 0x101};
    for (unsigned i = 0; i < kNormalTargets; ++i) {
        Fake &t = gc.targets[i];
        t.Returns(15, kindWords[rng() % std::size(kindWords)]);
        t.Returns(16, kSought[rng() % std::size(kSought)]);
        t.Returns(17, kSought[rng() % std::size(kSought)]);
        t.Returns(22, targetIds[rng() % std::size(targetIds)]);
        t.Returns(23, targetIds[rng() % std::size(targetIds)]);
    }
}

void NoteCheck()
{
    drawfx::Note("holder refs", uint32_t(gc.holder.refs));
    drawfx::Note("subject refs", {uint32_t(gc.subjectOwn.refs), uint32_t(gc.subjectBase.refs)});
}

// ---- ShowPlayingAnimations' queue entries, animations and resources ------------------------------------------------

constexpr auto kEntrySlots = TableOf<61>({{56, {0, Ret::Int, true, "Active", ""}}, {60, {0, Ret::Int, true, "Done", ""}}});
constexpr drawfx::Iface kEntry{"IkQueueEntry", 61, kEntrySlots.data(), false, drawfx::kTestTable};
constexpr auto kAnimCtlSlots = TableOf<35>({{34, {0, Ret::Int, true, "Resource", ""}}});
constexpr drawfx::Iface kAnimCtl{"IkAnimController", 35, kAnimCtlSlots.data(), false, drawfx::kTestTable};
constexpr auto kResBaseSlots = TableOf<18>({{17, {0, Ret::Int, true, "Name", ""}}});
constexpr drawfx::Iface kResBase{"IkResourceBase", 18, kResBaseSlots.data(), false, drawfx::kTestTable};
constexpr auto kPlainSlots = TableOf<3>({});
constexpr drawfx::Iface kPlain{"IkPlain", 3, kPlainSlots.data(), false, drawfx::kTestTable};

constexpr unsigned kEntries = 4, kAnims = 3;
constexpr uint32_t kAnimIid = 0xc9de69adu;
enum : unsigned { kEntryQiMode, kEntryAnim }; // the entry base's QI: 0 no, 1 answers, 2 no but writes; which animation
struct Queue {
    Fake entryOwn[kEntries], entryBase[kEntries];
    VObj entry[kEntries];
    Fake animOwn[kAnims], animUnknown[kAnims], animCtl[kAnims];
    VObj anim[kAnims];
    Fake resOwn[kAnims], resBase[kAnims];
    VObj res[kAnims];
} gq;
uint32_t gDisplay[2]; // the display argument (registered)

uint32_t EntryQuery(Fake &f, const uint32_t *a)
{
    if (a[0] != kAnimIid || f.value[kEntryQiMode] == 0) return 0;
    const unsigned k = f.value[kEntryAnim] % kAnims;
    ++gq.animUnknown[k].refs;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = drawfx::P(&gq.anim[k]);
    return f.value[kEntryQiMode] == 1;
}

void BuildQueue(uint32_t)
{
    for (unsigned i = 0; i < kEntries; ++i) {
        gq.entryOwn[i].Reset<kEntry>(30 + i);
        gq.entryBase[i].Reset<kUnknown>(34 + i);
        gq.entryBase[i].On(0, &EntryQuery);
        Wire(gq.entry[i], gq.entryOwn[i], &gq.entryBase[i], nullptr, 0x79000000 | (i << 8));
    }
    for (unsigned k = 0; k < kAnims; ++k) {
        gq.animOwn[k].Reset<kPlain>(40 + k);
        gq.animUnknown[k].Reset<kPlain>(43 + k);
        gq.animCtl[k].Reset<kAnimCtl>(46 + k);
        Wire(gq.anim[k], gq.animOwn[k], &gq.animUnknown[k], &gq.animCtl[k], 0x78000000 | (k << 8));
        gq.resOwn[k].Reset<kPlain>(20 + k);
        gq.resBase[k].Reset<kResBase>(23 + k);
        Wire(gq.res[k], gq.resOwn[k], &gq.resBase[k], nullptr, 0x77000000 | (k << 8));
    }
    drawfx::AddRange(gDisplay, sizeof gDisplay, 0x76000000);
}

void DrawQueue(std::mt19937 &rng)
{
    const int32_t counts[] = {-1, int32_t(0x80000000u), 0, 1, 2, 3, 4, 4};
    gQueueCount = counts[rng() % std::size(counts)];
    for (uint32_t &e : gQueue) e = rng() % 5 ? drawfx::P(&gq.entry[rng() % kEntries]) : 0;
    const uint32_t boolWords[] = {0, 1, 0x100, 0x1ff};
    const char *const names[] = {"walk", "", nullptr, "Idle_A"};
    for (unsigned i = 0; i < kEntries; ++i) {
        gq.entryOwn[i].Returns(56, boolWords[rng() % std::size(boolWords)]);
        gq.entryOwn[i].Returns(60, boolWords[rng() % std::size(boolWords)]);
        gq.entryBase[i].value[kEntryQiMode] = rng() % 4 ? 1 : rng() % 2 ? 2 : 0;
        gq.entryBase[i].value[kEntryAnim] = rng();
    }
    for (unsigned k = 0; k < kAnims; ++k) {
        gq.animCtl[k].Returns(34, rng() % 4 ? &gq.res[rng() % kAnims] : nullptr);
        gq.resBase[k].Returns(17, names[rng() % std::size(names)]);
    }
}

void NoteText(const char *label, const char *text)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = label;
    c.words.push_back(text != nullptr);
    if (text) c.text = text;
    drawfx::gLog.push_back(std::move(c));
}

// 0xaf42a0 and 0xaf4250 (the text display's clear and add), stood in on both sides.
void __cdecl StandClearLabels(cICompositionTreeNode *node, const char *display)
{
    drawfx::Note("clear labels", {drawfx::Norm(drawfx::P(node)), drawfx::Norm(drawfx::P(display))});
}
void __cdecl StandAddLabel(cICompositionTreeNode *node, const char *display, const uint32_t *colour, const char *text)
{
    drawfx::Note("add label", {drawfx::Norm(drawfx::P(node)), drawfx::Norm(drawfx::P(display)), *colour});
    NoteText("label text", text);
}

void NoteQueue()
{
    for (unsigned i = 0; i < kEntries; ++i)
        drawfx::Note("entry refs", {gq.entryOwn[i].id, uint32_t(gq.entryOwn[i].refs), uint32_t(gq.entryBase[i].refs)});
    for (unsigned k = 0; k < kAnims; ++k)
        drawfx::Note("anim refs", {gq.animOwn[k].id, uint32_t(gq.animUnknown[k].refs), uint32_t(gq.animCtl[k].refs),
                                   uint32_t(gq.resBase[k].refs)});
}

} // namespace

DIFF_TEST(SG_UtilitiesIK_QuatInverse, 0xc63600)
{
    using Fn = float *(__cdecl *)(float *, const float *);
    const Fn original = ctx.Original<Fn>(0xc63600);
    CheckPure(ctx, "QuatInverse", [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            float q[4], out[4] = {9, 9, 9, 9};
            for (float &c : q) c = std::bit_cast<float>(QuatWord(rng));
            const bool inPlace = rng() % 3 == 0;
            float *const dst = inPlace ? q : out;
            float *const r = isOriginal ? original(dst, q) : QuatInverse(dst, q);
            drawfx::Note("returns out", uint32_t(r == dst));
            NoteFloats("out", dst, 4);
        }
    });
}

DIFF_TEST(SG_UtilitiesIK_QuatMultiply, 0xc63690)
{
    using Fn = float *(__cdecl *)(float *, const float *, const float *);
    const Fn original = ctx.Original<Fn>(0xc63690);
    CheckPure(ctx, "QuatMultiply", [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            float a[4], b[4], out[4] = {9, 9, 9, 9};
            for (float &c : a) c = std::bit_cast<float>(QuatWord(rng));
            for (float &c : b) c = std::bit_cast<float>(QuatWord(rng));
            const unsigned mode = rng() % 4; // 0-1 a separate out, 2 out is a, 3 out is b
            float *const dst = mode == 2 ? a : mode == 3 ? b : out;
            float *const r = isOriginal ? original(dst, a, b) : QuatMultiply(dst, a, b);
            drawfx::Note("returns out", uint32_t(r == dst));
            NoteFloats("out", dst, 4);
        }
    });
}

// Lane 261111k: QuatRotate 0xc63380 and QuatFromAxisAngle 0xc63510.
DIFF_TEST(SG_UtilitiesIK_QuatRotate, 0xc63380)
{
    using Fn = float *(__cdecl *)(float *, const float *, const float *);
    const Fn original = ctx.Original<Fn>(0xc63380);
    CheckPure(ctx, "QuatRotate", [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            float v[4], q[4], out[4] = {9, 9, 9, 9};
            for (float &c : v) c = std::bit_cast<float>(QuatWord(rng));
            for (float &c : q) c = std::bit_cast<float>(QuatWord(rng));
            // A quarter of the time q's components are huge (their squares overflow), zeros or one: the differences of
            // squares are then default NaNs while the cross terms stay numbers, so a product with a NaN v component
            // shows its operand order.
            if (rng() % 4 == 0) {
                static constexpr uint32_t kBig[] = {0x5f800000u, 0xdf800000u, 0x00000000u, 0x80000000u, 0x3f800000u};
                for (float &c : q) c = std::bit_cast<float>(kBig[rng() % std::size(kBig)]);
            }
            const unsigned mode = rng() % 4; // 0-1 a separate out, 2 out is v, 3 out is q
            float *const dst = mode == 2 ? v : mode == 3 ? q : out;
            float *const r = isOriginal ? original(dst, v, q) : QuatRotate(dst, v, q);
            drawfx::Note("returns out", uint32_t(r == dst));
            NoteFloats("out", dst, 4);
            NoteFloats("v", v, 4);
            NoteFloats("q", q, 4);
        }
    });
}

// The angle: plain ones, multiples of pi near the ulp, edges, and from 2^64 up (the x87's unreduced case). Not drawn:
// halves between 2^20 and 2^63, where the result follows the x87's 66-bit pi reduction (SinCos87 in
// src/sg/SGUtilitiesIK.cpp); the game's angles are a turn or two.
uint32_t AngleWord(std::mt19937 &rng)
{
    switch (rng() % 10) {
    case 0: return AnyNaNWord(rng);
    case 1: {
        static constexpr uint32_t kEdges[] = {0x00000000u, 0x80000000u, 0x7f800000u, 0xff800000u, 0x00000001u,
                                              0x807fffffu, 0x5f800000u, 0xdf800000u, 0x5f7fffffu + 1u, 0x7f7fffffu,
                                              0x40490fdbu, 0xc0490fdbu, 0x40c90fdbu, 0x3fc90fdbu};
        return kEdges[rng() % std::size(kEdges)];
    }
    case 2: return 0x5f800000u + (uint32_t(rng()) % 0x1f800000u) | (rng() % 2 ? 0x80000000u : 0u); // 2^64 up
    case 3: return std::bit_cast<uint32_t>(float(int(rng() % 17) - 8) * 3.14159265f) + uint32_t(int(rng() % 5) - 2);
    case 4: return Uniform(rng, -2.0e6f, 2.0e6f);
    default: return Uniform(rng, -7.0f, 7.0f);
    }
}

DIFF_TEST(SG_UtilitiesIK_QuatFromAxisAngle, 0xc63510)
{
    using Fn = float *(__cdecl *)(float *, const float *, int32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0xc63510);
    CheckPure(ctx, "QuatFromAxisAngle", [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            // fsincos ignores precision control; the rest is SSE: the same at 24 bits.
            std::optional<difftest::X87Precision> pc;
            if (k % 2) pc.emplace(difftest::X87Pc::Bits24);
            float buf[16];
            for (float &c : buf) c = std::bit_cast<float>(rng() % 3 ? Uniform(rng, -2.0f, 2.0f) : QuatWord(rng));
            // A fifth of the time NaNs everywhere half the time (two axis NaNs meet in a product).
            if (rng() % 5 == 0)
                for (float &c : buf)
                    if (rng() % 2) c = std::bit_cast<float>(AnyNaNWord(rng));
            if (rng() % 4 == 0) buf[4] = buf[7] = buf[10] = buf[13] = 0.0f; // a zero axis (stride 3 from 4)
            static constexpr int32_t kStrides[] = {1, 1, 3, 4, 0, -1};
            const int32_t stride = kStrides[rng() % std::size(kStrides)];
            const uint32_t angle = AngleWord(rng);
            // The axis at word 4; out at word 12, or over the axis (word 4).
            float *const axis = buf + 4;
            float *const dst = rng() % 4 == 0 ? buf + 4 : buf + 12;
            drawfx::Note("stride and angle", {uint32_t(stride), angle});
            float *const r = isOriginal ? original(dst, axis, stride, angle)
                                        : QuatFromAxisAngle(dst, axis, stride, std::bit_cast<float>(angle));
            drawfx::Note("returns out", uint32_t(r == dst));
            NoteFloats("buffer", buf, 16);
        }
    });
}

DIFF_TEST(SG_UtilitiesIK_QuatToMatrix3, 0xc63b20)
{
    using Fn = void(__cdecl *)(float *, int32_t, const float *);
    const Fn original = ctx.Original<Fn>(0xc63b20);
    CheckPure(ctx, "QuatToMatrix3", [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            // A buffer the matrix lands in at word 8; the strides 3 and 4 the callers use, and overlapping ones.
            float buf[24];
            for (float &c : buf) c = 7.0f;
            for (int i = 8; i < 12; ++i) buf[i] = std::bit_cast<float>(QuatWord(rng));
            static constexpr int32_t kStrides[] = {3, 3, 4, 0, 1, 2, -1, -3};
            const int32_t stride = kStrides[rng() % std::size(kStrides)];
            // The quaternion apart from the matrix, or overlapping it (at its first word, or one or two words on).
            const unsigned mode = rng() % 5;
            float q[4];
            for (float &c : q) c = std::bit_cast<float>(QuatWord(rng));
            const float *const src = mode < 2 ? q : buf + 8 + (mode - 2);
            float *const m = buf + 8;
            isOriginal ? original(m, stride, src) : QuatToMatrix3(m, stride, src);
            NoteFloats("buf", buf, 24);
        }
    });
}

namespace {

// A matrix element: a third NaN (random payloads and signs), else an edge, random bits or a plain value.
uint32_t MatrixWord(std::mt19937 &rng)
{
    switch (rng() % 9) {
    case 0:
    case 1:
    case 2: return AnyNaNWord(rng);
    case 3: return AnyWord(rng);
    default: return Uniform(rng, -1.0f, 1.0f);
    }
}

// The gimbal threshold (0x387f0000) or a few ulps either side, of either sign.
float NearEpsilon(std::mt19937 &rng)
{
    const float f = std::bit_cast<float>(0x387f0000u + uint32_t(int(rng() % 7) - 3));
    return rng() % 2 ? -f : f;
}

// A packed Euler order: axes 0-2 (sometimes any byte), the third-axis bits and the repeated-axis bits at random (with
// junk in the bits between), a sign byte of 1, -1, 0, 2, -128 or anything.
// `inMatrix`: every index within a 3x3 matrix (axes and both third-axis fields 0-2).
uint32_t EulerOrder(std::mt19937 &rng, bool inMatrix = false)
{
    const uint32_t i = rng() % 8 || inMatrix ? rng() % 3 : rng() % 256;
    const uint32_t j = rng() % 8 || inMatrix ? rng() % 3 : rng() % 256;
    uint32_t h = rng() % 3 ? (i & 3) : rng() % (inMatrix ? 3 : 4); // a third of them not repeated by chance
    if (rng() % 2) h = (i + 1 + rng() % 2) % 3;
    const uint32_t k = rng() % (inMatrix ? 3 : 4), junk = rng() % 4 ? 0 : (rng() & 0x00cc0000u);
    static constexpr uint32_t kSigns[] = {1, 1, 0xff, 0xff, 0, 2, 0x80};
    const uint32_t sign = rng() % 8 ? kSigns[rng() % std::size(kSigns)] : rng() & 0xff;
    return i | (j << 8) | (h << 16) | (k << 20) | junk | (sign << 24);
}

// A rotation matrix (rows 3 apart) from a random unit quaternion, in double.
void RotationMatrix(std::mt19937 &rng, float *m)
{
    std::normal_distribution<double> n;
    double x = n(rng), y = n(rng), z = n(rng), w = n(rng);
    const double len = std::sqrt(x * x + y * y + z * z + w * w);
    x /= len, y /= len, z /= len, w /= len;
    const double r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
                         2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
                         2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)};
    for (int i = 0; i < 9; ++i) m[i] = float(r[i]);
}

constexpr unsigned kEulerBuf = 1040; // an axis byte up to 255 reads m[1020]

} // namespace

DIFF_TEST(SG_UtilitiesIK_Matrix3ToEuler, 0xc63c50)
{
    using Fn = float *(__cdecl *)(float *, const float *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xc63c50);
    static float buf[kEulerBuf];
    CheckPure(ctx, "Matrix3ToEuler", [&](bool isOriginal, std::mt19937 &rng) {
        for (int n = 0; n < 6; ++n) {
            for (float &c : buf) c = std::bit_cast<float>(MatrixWord(rng));
            float *const m = buf + 8;
            const unsigned kind = rng() % 6;
            if (kind < 2) RotationMatrix(rng, m);
            const uint32_t order = EulerOrder(rng);
            const uint32_t i = order & 0xff, j = (order >> 8) & 0xff, h = (order >> 16) & 3, k3 = (order >> 20) & 3;
            if (kind == 2 || kind == 3) {
                // At the gimbal threshold: the pair's length set to a value near it (the other element 0 or tiny).
                const bool repeated = (i & 3) == h;
                const uint32_t a = repeated ? 3 * i + j : 3 * j + i, b = repeated ? 3 * i + k3 : 4 * i;
                const bool which = rng() % 2;
                m[which ? a : b] = NearEpsilon(rng);
                m[which ? b : a] = rng() % 2 ? 0.0f : rng() % 2 ? -0.0f : std::bit_cast<float>(0x00000001u);
            }
            float out[4] = {9, 9, 9, 9};
            // Out apart, or the matrix itself, or one to three words into it.
            const unsigned alias = rng() % 4 ? 0 : 1 + rng() % 4;
            float *const dst = alias ? m + (alias - 1) : out;
            float *const r = isOriginal ? original(dst, m, order) : Matrix3ToEuler(dst, m, order);
            drawfx::Note("order", order);
            drawfx::Note("returns out", uint32_t(r == dst));
            NoteFloats("out", out, 4);
            NoteFloats("m", buf, 24);
        }
    });
}

DIFF_TEST(SG_UtilitiesIK_QuatToEuler, 0xc63af0)
{
    using Fn = float *(__cdecl *)(float *, const float *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xc63af0);
    CheckPure(ctx, "QuatToEuler", [&](bool isOriginal, std::mt19937 &rng) {
        for (int n = 0; n < 8; ++n) {
            float q[4], out[4] = {9, 9, 9, 9};
            if (rng() % 3) {
                // A unit quaternion, sometimes turned onto the gimbal (y = +-90 degrees in the XYZ order).
                std::normal_distribution<double> d;
                double c[4] = {d(rng), d(rng), d(rng), d(rng)};
                if (rng() % 3 == 0) c[0] = c[2] * (rng() % 2 ? 1 : -1), c[1] = c[3] * (rng() % 2 ? 1 : -1);
                const double len = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2] + c[3] * c[3]);
                for (int i = 0; i < 4; ++i) q[i] = float(c[i] / len);
            } else {
                for (float &c : q) c = std::bit_cast<float>(QuatWord(rng));
            }
            // The order IK targets use (0x1020100), or any.
            // (Indices past the 3x3 local would read the callee's own stack.)
            const uint32_t order = rng() % 2 ? 0x1020100u : EulerOrder(rng, true);
            const bool inPlace = rng() % 4 == 0;
            float *const dst = inPlace ? q : out;
            float *const r = isOriginal ? original(dst, q, order) : QuatToEuler(dst, q, order);
            drawfx::Note("returns out", uint32_t(r == dst));
            NoteFloats("out", out, 4);
            NoteFloats("q", q, 4);
        }
    });
}

// ---- lane 261111l: QuatSlerp 0xc63790, Matrix3RotationX / Y 0xc64040 / 0xc640c0, Matrix3Multiply 0xc64140,
// ToleranceForBits 0xc643a0 ----------------------------------------------------------------------------------------------

namespace {

// A blend parameter: plain ones around [0, 1], edges (zeros, ones, infinities, NaNs, denormals), and from 2^65 up, where
// angle * t (the angle at least acos(0.95), about 0.318) passes fsincos' 2^63 limit. Not drawn: |angle * t| between
// 2^20 and 2^63 (the x87's 66-bit pi reduction, see SinCos87); the game blends with t in [0, 1].
uint32_t BlendWord(std::mt19937 &rng)
{
    switch (rng() % 10) {
    case 0: return AnyNaNWord(rng);
    case 1: return kEdgeBits[rng() % std::size(kEdgeBits)];
    case 2: return 0x60000000u + (uint32_t(rng()) % 0x1f000000u) | (rng() % 2 ? 0x80000000u : 0u);
    case 3: {
        static constexpr uint32_t kPlain[] = {0x3f000000u, 0x3f7fffffu, 0x3f800001u, 0x00000000u, 0x3f800000u};
        return kPlain[rng() % std::size(kPlain)];
    }
    case 4: return Uniform(rng, -8.0f, 8.0f);
    default: return Uniform(rng, 0.0f, 1.0f);
    }
}

// A random unit quaternion (in double, rounded).
void UnitQuat(std::mt19937 &rng, float *q)
{
    std::normal_distribution<double> d;
    double c[4] = {d(rng), d(rng), d(rng), d(rng)};
    const double len = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2] + c[3] * c[3]);
    for (int i = 0; i < 4; ++i) q[i] = float(c[i] / len);
}

// Two quaternions whose dot is exactly `dot`: a has a 1 (or -1) at component i and zeros elsewhere (signed at random),
// b has `dot` (times a's sign) at i and plain values elsewhere. Every product but one is a signed zero.
void QuatsWithDot(std::mt19937 &rng, float *a, float *b, uint32_t dotBits)
{
    const unsigned i = rng() % 4;
    const bool negative = rng() % 2;
    for (int c = 0; c < 4; ++c) {
        a[c] = rng() % 2 ? -0.0f : 0.0f;
        b[c] = std::bit_cast<float>(Uniform(rng, -0.4f, 0.4f));
    }
    a[i] = negative ? -1.0f : 1.0f;
    b[i] = std::bit_cast<float>(dotBits ^ (negative ? 0x80000000u : 0u));
}

// 0.95f (0x3f733333), +-1 or 2 ulps either side, or 1 and its neighbours, of either sign.
uint32_t NearSlerpLimit(std::mt19937 &rng)
{
    static constexpr uint32_t kBase[] = {0x3f733333u, 0x3f733333u, 0x3f800000u, 0x00000000u};
    const uint32_t base = kBase[rng() % std::size(kBase)];
    const uint32_t bits = base == 0 ? 0u : base + uint32_t(int(rng() % 5) - 2);
    return bits | (rng() % 2 ? 0x80000000u : 0u);
}

} // namespace

DIFF_TEST(SG_UtilitiesIK_QuatSlerp, 0xc63790)
{
    using Fn = float *(__cdecl *)(float *, uint32_t, const float *, const float *);
    const Fn original = ctx.Original<Fn>(0xc63790);
    CheckPure(ctx, "QuatSlerp", [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            // The CRT's atan2 and fsincos are x87 code: at 24 bits too.
            std::optional<difftest::X87Precision> pc;
            if (k % 2) pc.emplace(difftest::X87Pc::Bits24);
            float a[4], b[4], out[4] = {9, 9, 9, 9};
            switch (rng() % 6) {
            case 0:
            case 1: // two unit quaternions (both branches: random dots, and b near +-a)
                UnitQuat(rng, a);
                UnitQuat(rng, b);
                if (rng() % 3 == 0)
                    for (int c = 0; c < 4; ++c) b[c] = (rng() % 2 ? 1.0f : -1.0f) * a[c] + float(int(rng() % 9) - 4) * 0.01f;
                break;
            case 2: QuatsWithDot(rng, a, b, NearSlerpLimit(rng)); break; // the 0.95 boundary, exactly
            case 3: QuatsWithDot(rng, a, b, rng() % 2 ? AnyNaNWord(rng) : kEdgeBits[rng() % std::size(kEdgeBits)]); break;
            default: // NaN-dense: two NaNs meet in a product or a sum
                for (float &c : a) c = std::bit_cast<float>(QuatWord(rng));
                for (float &c : b) c = std::bit_cast<float>(QuatWord(rng));
                break;
            }
            const uint32_t t = BlendWord(rng);
            const unsigned mode = rng() % 5; // 0-2 a separate out, 3 out is a, 4 out is b
            float *const dst = mode == 3 ? a : mode == 4 ? b : out;
            drawfx::Note("t", t);
            float *const r = isOriginal ? original(dst, t, a, b) : QuatSlerp(dst, std::bit_cast<float>(t), a, b);
            drawfx::Note("returns out", uint32_t(r == dst));
            NoteFloats("out", out, 4);
            NoteFloats("a", a, 4);
            NoteFloats("b", b, 4);
        }
    });
}

namespace {

template <class Ours> void CheckRotation(difftest::Context &ctx, const char *what, uint32_t rva, Ours ours)
{
    using Fn = void(__cdecl *)(float *, int32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(rva);
    CheckPure(ctx, what, [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            std::optional<difftest::X87Precision> pc;
            if (k % 2) pc.emplace(difftest::X87Pc::Bits24);
            // The matrix at word 12 of a buffer of 7s: strides 3 and 4 (the callers'), and overlapping ones.
            float buf[32];
            for (float &c : buf) c = 7.0f;
            static constexpr int32_t kStrides[] = {3, 3, 4, 0, 1, 2, -1, -3};
            const int32_t stride = kStrides[rng() % std::size(kStrides)];
            const uint32_t angle = AngleWord(rng);
            drawfx::Note("stride and angle", {uint32_t(stride), angle});
            isOriginal ? original(buf + 12, stride, angle) : ours(buf + 12, stride, std::bit_cast<float>(angle));
            NoteFloats("buf", buf, 32);
        }
    });
}

} // namespace

DIFF_TEST(SG_UtilitiesIK_Matrix3RotationX, 0xc64040) { CheckRotation(ctx, "Matrix3RotationX", 0xc64040, Matrix3RotationX); }

DIFF_TEST(SG_UtilitiesIK_Matrix3RotationY, 0xc640c0) { CheckRotation(ctx, "Matrix3RotationY", 0xc640c0, Matrix3RotationY); }

DIFF_TEST(SG_UtilitiesIK_Matrix3Multiply, 0xc64140)
{
    using Fn = void(__cdecl *)(float *, const float *, const float *);
    const Fn original = ctx.Original<Fn>(0xc64140);
    CheckPure(ctx, "Matrix3Multiply", [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            float a[9], b[9], out[9];
            for (float &c : out) c = 7.0f;
            const unsigned kind = rng() % 4;
            for (int i = 0; i < 9; ++i) {
                if (kind == 0) { // rotations
                    a[i] = std::bit_cast<float>(Uniform(rng, -1.0f, 1.0f));
                    b[i] = std::bit_cast<float>(Uniform(rng, -1.0f, 1.0f));
                } else if (kind == 1) { // signed zeros and ones: the +0 start shows on -0 sums
                    static constexpr uint32_t kZeros[] = {0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x80000001u};
                    a[i] = std::bit_cast<float>(kZeros[rng() % std::size(kZeros)]);
                    b[i] = std::bit_cast<float>(kZeros[rng() % std::size(kZeros)]);
                } else { // NaN-dense: each product's operand order and each sum's
                    a[i] = std::bit_cast<float>(MatrixWord(rng));
                    b[i] = std::bit_cast<float>(MatrixWord(rng));
                }
            }
            if (kind == 0 && rng() % 2) RotationMatrix(rng, a), RotationMatrix(rng, b);
            const unsigned mode = rng() % 6; // 0-2 apart, 3 out is a, 4 out is b, 5 a is b and out is both
            const float *src = b;
            float *dst = out;
            if (mode == 3) dst = a;
            if (mode == 4) dst = b;
            if (mode == 5) dst = a, src = a;
            isOriginal ? original(dst, a, src) : Matrix3Multiply(dst, a, src);
            NoteFloats("out", out, 9);
            NoteFloats("a", a, 9);
            NoteFloats("b", b, 9);
        }
    });
}

DIFF_TEST(SG_UtilitiesIK_ToleranceForBits, 0xc643a0)
{
    using Fn = float(__cdecl *)(int32_t);
    const Fn original = ctx.Original<Fn>(0xc643a0);
    // Every entry, in order (the image's table: the original reads it, ours holds it), at both precisions (fld of a
    // float is exact either way).
    sgfx::CheckSides(ctx, "ToleranceForBits", 2, [](uint32_t, drawfx::Cells &) {}, [&](bool isOriginal, std::mt19937 &) {
        for (int32_t i = 0; i < 24; ++i) {
            std::optional<difftest::X87Precision> pc;
            if (i % 2) pc.emplace(difftest::X87Pc::Bits24);
            const float v = isOriginal ? original(i) : ToleranceForBits(i);
            drawfx::Note("entry", {uint32_t(i), std::bit_cast<uint32_t>(v)});
        }
    });
}

DIFF_TEST(SG_UtilitiesIK_ClampLookAngles, 0xae43c0)
{
    using Fn = uint8_t(__cdecl *)(const float *, float *);
    const Fn original = ctx.Original<Fn>(0xae43c0);
    CheckPure(ctx, "ClampLookAngles", [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            float buf[5] = {7, 7, 7, 7, 7};
            for (int i = 0; i < 4; ++i) buf[i] = std::bit_cast<float>(AnyWord(rng));
            float out[3] = {9, 9, 9};
            const unsigned mode = rng() % 4; // 0-1 a separate out, 2 out is in, 3 out one word past in
            float *const dst = mode == 2 ? buf : mode == 3 ? buf + 1 : out;
            const uint8_t al = isOriginal ? original(buf, dst) : uint8_t(ClampLookAngles(buf, dst));
            drawfx::Note("al", al);
            NoteFloats("buf", buf, 5);
            NoteFloats("out", out, 3);
        }
    });
}

DIFF_TEST(SG_UtilitiesIK_LookAngles, 0xae4780)
{
    using Fn = void(__cdecl *)(uint32_t, const float *, float *);
    const Fn original = ctx.Original<Fn>(0xae4780);
    // Ours through the same type: the flag's high bytes are a register's leftovers that neither side reads.
    const Fn ours = reinterpret_cast<Fn>(&LookAngles);
    CheckPure(ctx, "LookAngles", [&](bool isOriginal, std::mt19937 &rng) {
        for (int k = 0; k < 8; ++k) {
            float buf[4] = {7, 7, 7, 7};
            for (int i = 0; i < 3; ++i)
                buf[i] = std::bit_cast<float>(rng() % 3 ? Uniform(rng, -4.0f, 4.0f)
                                              : rng() % 2 ? AnyNaNWord(rng)
                                                          : AnyWord(rng));
            float out[3] = {9, 9, 9};
            const uint32_t flags[] = {0, 1, 0x100, 0x1ff, 0xabcdef00u};
            const uint32_t flag = flags[rng() % std::size(flags)];
            const unsigned mode = rng() % 4; // 0-1 a separate out, 2 out is the direction, 3 one word past it
            float *const dst = mode == 2 ? buf : mode == 3 ? buf + 1 : out;
            (isOriginal ? original : ours)(flag, buf, dst);
            NoteFloats("buf", buf, 4);
            NoteFloats("out", out, 3);
        }
    });
}

DIFF_TEST(SG_UtilitiesIK_CanLookAt, 0xae4490)
{
    using Fn = uint8_t(__cdecl *)(void *, void *, const float *);
    const Fn original = ctx.Original<Fn>(0xae4490);
    sgfx::CheckSides(ctx, "CanLookAt", 160, [](uint32_t seed, drawfx::Cells &) { BuildNeck(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (int k = 0; k < 4; ++k) {
                             float point[3];
                             bool able;
                             DrawNeck(rng, point, able);
                             void *const subject = &gn.subject;
                             Fake &frame = gn.nodes[kFrame];
                             const uint8_t al = isOriginal ? original(subject, &frame, point)
                                                           : uint8_t(CanLookAt(subject, AsNode(frame), point));
                             drawfx::Note("al", al);
                             NoteNeck();
                         }
                     });
}

DIFF_TEST(SG_UtilitiesIK_FindNodeByNameConst, 0xae75c0)
{
    using Fn = void *(__cdecl *)(const char *, void *, int32_t);
    const Fn original = ctx.Original<Fn>(0xae75c0);
    CheckTree(ctx, "FindNodeByNameConst", [](uint32_t) {}, [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const char *name = kSought[rng() % std::size(kSought)];
        const int32_t skip = SkipFlag(rng);
        drawfx::Note("call", {node.id, uint32_t(skip)});
        NoteFound("found", isOriginal ? original(name, &node, skip) : FindNodeByNameConst(name, AsNode(node), skip));
    });
}

DIFF_TEST(SG_UtilitiesIK_FindNodeByTagCrc, 0xae76f0)
{
    using Fn = void *(__cdecl *)(uint32_t, void *, int32_t);
    const Fn original = ctx.Original<Fn>(0xae76f0);
    shape::StandIn pool{0xaeb820, &GetCachedObjectB, &StandPool};
    CheckTree(ctx, "FindNodeByTagCrc", [](uint32_t) {}, [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = AnyNode(rng);
        const uint32_t crc = kSoughtCrcs[rng() % std::size(kSoughtCrcs)];
        const int32_t skip = SkipFlag(rng);
        drawfx::Note("call", {node.id, crc, uint32_t(skip)});
        NoteFound("found", isOriginal ? original(crc, &node, skip) : FindNodeByTagCrc(crc, AsNode(node), skip));
    });
}

DIFF_TEST(SG_UtilitiesIK_FindIKTargetNode, 0xae37c0)
{
    using Fn = uint8_t(__cdecl *)(void *, void *, void *, uint32_t *, void **, void **);
    const Fn original = ctx.Original<Fn>(0xae37c0);
    shape::StandIn pool{0xaeb820, &GetCachedObjectB, &StandPool};
    CheckTree(ctx, "FindIKTargetNode", &BuildIk, [&](bool isOriginal, std::mt19937 &rng) {
        // This call's answers.
        Fake &t = gik.target;
        t.value[kKind] = rng() % 4; // 0 and 3: neither kind
        for (unsigned w = kIdFirst22; w <= kIdLater23; ++w) t.value[w] = AnyId(rng);
        if (rng() % 2) t.value[kIdLater22] = t.value[kIdFirst22], t.value[kIdLater23] = t.value[kIdFirst23];
        t.value[kIdCalls] = 0;
        const uint32_t hasWords[] = {0, 1, 0x100};
        gik.rigFake.Returns(7, hasWords[rng() % 3]);
        gik.rigFake.Returns(5, rng() % 5 ? &AnyNode(rng) : nullptr);
        const uint32_t altWords[] = {0, 1, 0x100, 0x1ff};
        gik.rigBase.Returns(46, altWords[rng() % 4]);
        gik.settings.Returns(66, uint32_t(SkipFlag(rng)));
        for (unsigned i = 0; i < gw.count; ++i)
            for (unsigned k = 0; k < gw.extCount[i]; ++k) {
                Fake &face = gw.exts[i][k].faceFake;
                face.value[kFaceTarget] = rng() % 4 ? drawfx::P(&AnyNode(rng)) : 0;
                const unsigned owner = rng() % gw.count;
                face.value[kFaceBone] = rng() % 3 && gw.extCount[owner] ? drawfx::P(&gw.exts[owner][0].face) : 0;
            }
        Fake *const self = rng() % 6 ? &AnyNode(rng) : nullptr;

        uint32_t id = 0xdddd0000u + (rng() & 0xff);
        // The cells start on registered sentinels, so each store shows.
        cICompositionTreeNode *node = reinterpret_cast<cICompositionTreeNode *>(&gSentinel[0]);
        void *bone = &gSentinel[1];
        gNodeCell = &node;
        gBoneCell = &bone;
        gIdCell = &id;
        drawfx::Note("call", {self ? self->id : 0, t.value[kKind]});
        const uint8_t al = isOriginal ? original(self, &gik.rig, &t, &id, reinterpret_cast<void **>(&node), &bone)
                                      : uint8_t(FindIKTargetNode(self ? AsNode(*self) : nullptr, &gik.rig, &t, &id,
                                                                 &node, &bone));
        gNodeCell = nullptr;
        gBoneCell = nullptr;
        gIdCell = nullptr;
        drawfx::Note("al", al);
        drawfx::Note("id", id);
        NoteFound("node", node);
        NoteFound("bone", bone);
    });
}

DIFF_TEST(SG_UtilitiesIK_CanFindIKTargets, 0xae3fb0)
{
    using Fn = uint8_t(__cdecl *)(void *, void *, void *, uint32_t, uint8_t *);
    const Fn original = ctx.Original<Fn>(0xae3fb0);
    // Ours through the same shape, so the `alt` word's upper bytes reach it as they reach the original.
    const Fn ours = reinterpret_cast<Fn>(&CanFindIKTargets);
    CheckTree(ctx, "CanFindIKTargets", &BuildCheck, [&](bool isOriginal, std::mt19937 &rng) {
        DrawCheck(rng);
        Fake *const other = rng() % 5 ? &AnyNode(rng) : nullptr;
        const uint32_t altWords[] = {0, 1, 0x100, 0x1ff};
        const uint32_t alt = altWords[rng() % std::size(altWords)];
        uint8_t missing = 0xcc;
        drawfx::Note("call", {other ? other->id : 0, alt, gc.chainSet.value[kListCount], gc.holder.value[kHolderMode]});
        const uint8_t al = (isOriginal ? original : ours)(&gc.subject, other, &gc.chainSet, alt, &missing);
        drawfx::Note("al", al);
        drawfx::Note("missing", missing);
        NoteCheck();
    });
}

DIFF_TEST(SG_UtilitiesIK_ShowPlayingAnimations, 0xae2e70)
{
    using Fn = void(__cdecl *)(void *, void *);
    const Fn original = ctx.Original<Fn>(0xae2e70);
    shape::StandIn clear{0xaf42a0, &ClearNodeTextLabels, &StandClearLabels};
    shape::StandIn add{0xaf4250, &AddNodeTextLabel, &StandAddLabel};
    CheckTree(ctx, "ShowPlayingAnimations", &BuildQueue, [&](bool isOriginal, std::mt19937 &rng) {
        DrawQueue(rng);
        Fake &node = AnyNode(rng);
        void *const display = rng() % 4 ? static_cast<void *>(gDisplay) : nullptr;
        drawfx::Note("call", {node.id, uint32_t(gQueueCount)});
        if (isOriginal) original(&node, display);
        else ShowPlayingAnimations(AsNode(node), display);
        NoteQueue();
    });
}

// ---- MakeIKTargetsFromNames' subject, walk and new objects ---------------------------------------------------------

namespace {

constexpr uint32_t kSetClass = 0xfb00791d, kListClass = 0xa9de45ba, kRootSpaceClass = 0x5b0aff38,
                   kBoneSpaceClass = 0x1b115b24, kNewTargetClass = 0x5d35d710;
constexpr Slot kMakerSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "CreateInstance", "uua"}, // 3
};
constexpr drawfx::Iface kMaker{"Framework", 4, kMakerSlots, false, drawfx::kTestTable};
constexpr auto kSetSlots = TableOf<19>({{3, {1, Ret::Int, true, "Mode3", "u"}},
                                        {14, {1, Ret::Int, true, "Mode14", "u"}},
                                        {18, {1, Ret::Int, true, "SetList", "a"}}});
constexpr drawfx::Iface kSet{"IkSet", 19, kSetSlots.data(), false, drawfx::kTestTable};
constexpr auto kListSlots = TableOf<32>({{9, {1, Ret::Int, true, "Slot9", "u"}},
                                         {10, {1, Ret::Int, true, "Add", "a"}},
                                         {31, {0, Ret::Int, true, "Count", ""}}});
constexpr drawfx::Iface kList{"IkTargetList", 32, kListSlots.data(), false, drawfx::kTestTable};
constexpr auto kNewTargetSlots = TableOf<40>({{7, {2, Ret::Int, true, "Space", "au"}},
                                              {9, {1, Ret::Int, true, "Tag", "s"}},
                                              {12, {0, Ret::Int, true, "Slot12", ""}},
                                              {29, {2, Ret::Int, true, "Axis", "uu"}},
                                              {30, {6, Ret::Int, true, "Value", "ufuuuu"}},
                                              {39, {1, Ret::Int, true, "Slot39", "u"}}});
constexpr drawfx::Iface kNewTarget{"IkNewTarget", 40, kNewTargetSlots.data(), false, drawfx::kTestTable};
constexpr auto kWalkerSlots = TableOf<75>({{45, {1, Ret::Int, true, "First", "a"}},
                                           {46, {1, Ret::Int, true, "Next", "a"}},
                                           {74, {0, Ret::Int, true, "Root", ""}}});
constexpr drawfx::Iface kWalker{"IkSubject", 75, kWalkerSlots.data(), false, drawfx::kTestTable};
constexpr auto kWalkerBaseSlots = TableOf<19>({{18, {0, Ret::Int, true, "Holder", ""}}});
constexpr drawfx::Iface kWalkerBase{"IkSubjectBase", 19, kWalkerBaseSlots.data(), false, drawfx::kTestTable};
constexpr auto kHolderSlots = TableOf<3>({{0, {2, Ret::Int, true, "QueryInterface", "ua"}}});
constexpr drawfx::Iface kHolder{"IkModelHolder", 3, kHolderSlots.data(), false, drawfx::kTestTable};
constexpr auto kCursorSlots = TableOf<10>({{9, {0, Ret::Int, true, "Rotation", ""}}});
constexpr drawfx::Iface kCursor{"IkCursor", 10, kCursorSlots.data(), false, drawfx::kTestTable};
constexpr auto kNamesPlainSlots = TableOf<3>({});
constexpr drawfx::Iface kNamesPlain{"IkPlain", 3, kNamesPlainSlots.data(), false, drawfx::kTestTable};

// Tags: every skipped suffix, at its exact length and longer, in mixed case; near misses one letter short or off; plain
// names; empty and null.
const char *const kNameTags[] = {
    "_grip",       "L_GRIP",      "grip",        "_gri",        "x_grip_",     "target_surface", "aTARGET_Surface",
    "arget_surface", "_ikpole",   "arm_IKPOLE",  "_ikpol",      "ikpole",      "_hair",          "Hair_hair",
    "hair",        "_ikctr",      "leg_IkCtr",   "_ikct",       "control0",    "xcontrol1",      "CONTROL1",
    "control2",    "control0rot", "a_Control1ROT", "control0ro", "ontrol1rot",  "bone",           "b",
    "spine2",      "ROOT_bind",   "",            nullptr,
};

constexpr unsigned kWalkNodes = 8, kMaxSteps = 12, kTargets = kMaxSteps + 1;
enum : unsigned { kHolderFirst, kHolderLater, kHolderCalls };          // the subject base's value words
enum : unsigned { kQiAnswer = 1, kQiDirty = 2 };                       // a holder's value[0]
struct Space {                                                         // a space object: vbptr at +0
    const int32_t *vbptr;
    int32_t vbtable[3];
};
struct Walker {                                                        // the subject: own face, vbptr at +4
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[2];
};
struct Names {
    Fake maker, set, list, walkerFake, walkerBase, model;
    Fake holders[2], cursors[2], nodes[kWalkNodes], targets[kTargets];
    Fake rootUnknown, rootFace, boneUnknown, boneFace;
    Space rootSpace, boneSpace;
    Walker walker;
    float local[kWalkNodes][7];
    float cursorRotation[2][4];
    unsigned steps, step;
    uint32_t walk[kMaxSteps], cursorOf[kMaxSteps]; // node index per step; its cursor (0 none, 1-2)
    uint32_t root, spaces, targetCount;            // root: a node index, or kWalkNodes (none); spaces: bit 0 root, 1 bone
} gm;
void **gSetCell; // the caller's cell during a call (noted inside the calls)

void NoteSetCell(const char *label)
{
    if (gSetCell) drawfx::Note(label, drawfx::Norm(drawfx::P(*gSetCell)));
}

uint32_t MakerCreate(Fake &, const uint32_t *a)
{
    NoteSetCell("cell at Create");
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Fake *made = nullptr;
    void *face = nullptr;
    switch (a[0]) {
    case kSetClass: made = &gm.set; break;
    case kListClass: made = &gm.list; break;
    case kRootSpaceClass:
        if (gm.spaces & 1) made = &gm.rootUnknown, face = &gm.rootSpace;
        break;
    case kBoneSpaceClass:
        if (gm.spaces & 2) made = &gm.boneUnknown, face = &gm.boneSpace;
        break;
    case kNewTargetClass:
        if (gm.targetCount < kTargets) made = &gm.targets[gm.targetCount++];
        break;
    }
    if (!made) return 0;
    ++made->refs;
    *out = face ? face : made;
    return 1;
}
void *StandMaker() { return &gm.maker; }

uint32_t HolderAnswer(Fake &f, const uint32_t *)
{
    return f.value[f.value[kHolderCalls]++ ? kHolderLater : kHolderFirst];
}
uint32_t NamesHolderQuery(Fake &f, const uint32_t *a)
{
    NoteSetCell("cell at QueryInterface");
    if (!f.value[0]) return 0;
    ++gm.model.refs;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = drawfx::P(&gm.model);
    return f.value[0] == kQiAnswer;
}
uint32_t WalkStep(Fake &, const uint32_t *a)
{
    uint32_t *const cursor = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    if (gm.step >= gm.steps) {
        *cursor = 0;
        return 0;
    }
    const unsigned s = gm.step++;
    *cursor = gm.cursorOf[s] ? drawfx::P(&gm.cursors[gm.cursorOf[s] - 1]) : 0;
    return drawfx::P(&gm.nodes[gm.walk[s]]);
}
uint32_t WalkFirst(Fake &f, const uint32_t *a)
{
    gm.step = 0;
    return WalkStep(f, a);
}
uint32_t WalkLocal(Fake &f, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gm.local[f.id - 700], sizeof gm.local[0]);
    return a[0];
}
uint32_t ListSlot9(Fake &, const uint32_t *)
{
    NoteSetCell("cell at list slot 9");
    return 0;
}

void WireSpace(Space &s, Fake &unknown, Fake &face, uint32_t tag)
{
    s.vbptr = s.vbtable;
    s.vbtable[0] = 0;
    s.vbtable[1] = int32_t(drawfx::P(&unknown) - drawfx::P(&s));
    s.vbtable[2] = int32_t(drawfx::P(&face) - drawfx::P(&s));
    drawfx::AddRange(&s, sizeof s, tag);
}

void BuildNames(uint32_t seed)
{
    std::mt19937 rng(seed * 2246822519u + 11u);
    gm.maker.Reset<kMaker>(600);
    gm.maker.On(3, &MakerCreate);
    gm.set.Reset<kSet>(601);
    gm.list.Reset<kList>(602);
    gm.list.On(9, &ListSlot9);
    static constexpr uint32_t kCounts[] = {0, 1, 2, 0x100, 0x80000000u};
    gm.list.Returns(31, kCounts[rng() % std::size(kCounts)]);
    gm.walkerFake.Reset<kWalker>(603);
    gm.walkerFake.On(45, &WalkFirst);
    gm.walkerFake.On(46, &WalkStep);
    gm.walkerBase.Reset<kWalkerBase>(604);
    gm.walkerBase.On(18, &HolderAnswer);
    gm.model.Reset<kNamesPlain>(605);
    for (unsigned i = 0; i < 2; ++i) {
        gm.holders[i].Reset<kHolder>(610 + i);
        gm.holders[i].On(0, &NamesHolderQuery);
        gm.holders[i].value[0] = rng() % 4 ? kQiAnswer : rng() % 2 ? kQiDirty : 0;
        gm.cursors[i].Reset<kCursor>(620 + i);
        gm.cursors[i].Returns(9, gm.cursorRotation[i]);
        for (float &c : gm.cursorRotation[i]) c = std::bit_cast<float>(QuatWord(rng));
    }
    drawfx::AddRange(gm.cursorRotation, sizeof gm.cursorRotation, 0x71000000);
    // The holder: the first answer null now and then (the model is missing); the second may be the other holder.
    gm.walkerBase.value[kHolderFirst] = rng() % 6 ? drawfx::P(&gm.holders[0]) : 0;
    gm.walkerBase.value[kHolderLater] = drawfx::P(&gm.holders[rng() % 2]);
    gm.walker.vbptr = gm.walker.vbtable;
    gm.walker.vbtable[0] = -4;
    gm.walker.vbtable[1] = int32_t(drawfx::P(&gm.walkerBase) - drawfx::P(&gm.walker.vbptr));
    gm.walkerFake.Embed(&gm.walker);
    drawfx::AddRange(&gm.walker, sizeof gm.walker, 0x72000000);

    gm.rootUnknown.Reset<kNamesPlain>(630);
    gm.rootFace.Reset<kNamesPlain>(631);
    gm.boneUnknown.Reset<kNamesPlain>(632);
    gm.boneFace.Reset<kNamesPlain>(633);
    WireSpace(gm.rootSpace, gm.rootUnknown, gm.rootFace, 0x73000000);
    WireSpace(gm.boneSpace, gm.boneUnknown, gm.boneFace, 0x74000000);
    gm.spaces = rng() % 4 ? 3 : rng() % 3;
    for (unsigned i = 0; i < kTargets; ++i) gm.targets[i].Reset<kNewTarget>(640 + i);
    gm.targetCount = 0;

    for (unsigned i = 0; i < kWalkNodes; ++i) {
        Fake &n = gm.nodes[i];
        n.Reset<kTreeNode>(700 + i);
        n.Returns(26, kNameTags[rng() % std::size(kNameTags)]);
        n.On(53, &WalkLocal);
        // Translations bit for bit (edge values and NaNs); rotations mostly plain, often NaN.
        for (int k = 0; k < 3; ++k) gm.local[i][k] = std::bit_cast<float>(rng() % 3 ? AnyWord(rng) : AnyNaNWord(rng));
        for (int k = 3; k < 7; ++k) gm.local[i][k] = std::bit_cast<float>(QuatWord(rng));
    }
    gm.root = rng() % (kWalkNodes + 2); // kWalkNodes and over: none of the walk's nodes
    gm.walkerFake.Returns(74, gm.root < kWalkNodes ? static_cast<void *>(&gm.nodes[gm.root]) : nullptr);
    gm.steps = rng() % (kMaxSteps + 1);
    for (unsigned s = 0; s < gm.steps; ++s) {
        gm.walk[s] = rng() % kWalkNodes;
        gm.cursorOf[s] = rng() % 3;
    }
    gm.step = 0;
}

void NoteNames()
{
    for (const Fake *f : {&gm.set, &gm.list, &gm.model, &gm.holders[0], &gm.holders[1], &gm.rootUnknown, &gm.boneUnknown,
                          &gm.cursors[0], &gm.cursors[1]})
        drawfx::Note("refs", {f->id, uint32_t(f->refs)});
    for (unsigned i = 0; i < kTargets; ++i) drawfx::Note("target refs", {gm.targets[i].id, uint32_t(gm.targets[i].refs)});
    drawfx::Note("made", gm.targetCount);
}

} // namespace

DIFF_TEST(SG_UtilitiesIK_MakeIKTargetsFromNames, 0xae6290)
{
    using Fn = uint8_t(__cdecl *)(void *, void **);
    const Fn original = ctx.Original<Fn>(0xae6290);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandMaker};
    sgfx::CheckSides(ctx, "MakeIKTargetsFromNames", 400, [](uint32_t seed, drawfx::Cells &) { BuildNames(seed); },
                     [&](bool isOriginal, std::mt19937 &) {
                         void *made = &gSentinel[0];
                         drawfx::AddRange(gSentinel, sizeof gSentinel, 0x7b000000);
                         gSetCell = &made;
                         const uint8_t al = isOriginal ? original(&gm.walker, &made)
                                                       : uint8_t(MakeIKTargetsFromNames(&gm.walker, &made));
                         gSetCell = nullptr;
                         drawfx::Note("al", al);
                         drawfx::Note("made", drawfx::Norm(drawfx::P(made)));
                         NoteNames();
                     });
}
