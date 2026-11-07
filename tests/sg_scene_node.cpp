// Differential tests of nGZSceneGraph::cSceneNode (lane OK, scenegraph N1 step 3; src/sg/cSceneNode.h): the game's node (its
// factory 0xa4e9b0, constructor 0xa4ddf0, vtables 0xe6d0b4 / 0xe6d240 / 0xe6d260 / 0xe6d298 and the bodies behind them)
// against ours (Create, our vtables), each side building its own nodes with its own factory.
//
//   SG_SceneNode_Factory / _Slots / _Persist / _Clone / _Events  the pilot (sg_transform_pilot.h, level 3): scene nodes
//       (roots only, and flags 1, 6 and 13 left alone: Cfg says why) through the composition, transform and bound slots,
//       the +8 persist face (the cSGResource record between the class's own record and the partition's:
//       Cfg::ReadResource), Clone (empty) and the behaviour. A scene node whose bound changes queues itself (31 / 32
//       answer the node itself; the tree code's 0xa39ae0 calls 95), and a dying one takes itself off through the tree
//       code (0xa39710, which calls 96).
//   SG_SceneNode_EventRecord  the registry's count and records (the missing record 0x106b120 patterned).
//   SG_SceneNode_ClassId      the class-id static 0xa4ea10 (hooked) against GetClassId.
//   SG_SceneNode_Resource     the cSGResource part's face (+0x6c) and the virtual base: QueryInterface, AddRef / Release
//       and the count through every face, the resource slots, the records 18 / 19, the owner, the type record, Write
//       through either persist face and Read through either (into a fresh node).
//   SG_SceneNode_Content / _ContentLong  one scene node holding content nodes (cIRenderableNode fakes) and sub-partitions
//       (cIPartitionNode fakes that take what they are offered when their answer says so), driven through slots 67 and
//       70-96: the lists, the queries (direct and through the scene slots, which flush the queues first), the queues (the
//       tree code's 0xa39ae0 and 0xa39710 on the fakes, and 94 directly), the flush's compaction, and the destructor (its
//       Clear, the queue loop through 0xa39710, the system's slot 14 with and without a system).
//
// The class's own bodies (in effect through callers; db/status/cSceneNode.jsonl has an entry for each):
//   0xa4ddf0 cSceneNode, 0xa4e0a0 ~cSceneNode, 0xa4e480 scalar_deleting_destructor, 0xa4ef70 QueryInterface, 0xa4f6e0
//   IsKindOf, 0xa4f710 Accept, 0xa4eaf0 GetBoundBoxIn, 0xa4eb20 GetBoundBox, 0xa4eae0 FirstHitLinear, 0xa4e7e0 Clear,
//   0xa4f280 RemovePartition, 0xa4f1a0 RemoveNode, 0xa4ec10 AddPartition, 0xa4eda0 UpdateNode, 0xa4eb50 InsertNode,
//   0xa4f470 QueryShape, 0xa4f510 QueryLinear, 0xa4f5a0 RayCast, 0xa4f630 Cull, 0xa4e540 GetResource, 0xa4ee80
//   SceneQueryShape, 0xa4eed0 SceneQueryLinear, 0xa4efd0 SceneRayCast, 0xa4f040 SceneCull, 0xa4f800 FlushPending,
//   0xa4e4b0 QueueNode, 0xa4f200 UnqueueNode, 0xa4f0a0 Read, 0xa4f750 GetTypeInfo, 0xa4fc00 Write, 0xa4f2f0 GetOwnerInfo,
//   0xa4ea20 GetRefCount, 0xa4fbd0 the partition part's Write (the records 18 / 19 have no body of their own: 0xa4ead0 /
//   0xa4eac0 lie inside the vtordisp thunks 0xa4eac6 / 0xa4eaad below, which fall through into them)
// This instantiation's template-level bodies (reached through our vtables; the behaviour in _Events / _EventRecord):
//   0xa4dc40 cAbstractBehaviorTypeT, 0xa4dcd0 cObjectGraphNodeT, 0xa4dcb0 cCompositionTreeNodeT, 0xa4dd90
//   cTransformNodeT, 0xa4dc50 cBoundedNodeT, 0xa4dd30 cPartitionNodeT, 0xa4dfb0 ~cAbstractBehaviorTypeT, 0xa4e020
//   ~cObjectGraphNodeT, 0xa4dfe0 ~cCompositionTreeNodeT, 0xa4e080 ~cTransformNodeT, 0xa4dfc0 ~cBoundedNodeT, 0xa4e060
//   ~cPartitionNodeT, 0xa4e3c0 / 0xa4e420 / 0xa4e400 / 0xa4e460 / 0xa4e3e0 / 0xa4e440 their scalar_deleting_destructors,
//   0xa4f110 Release, 0xa4f180 / 0xa4f160 Release (-> 0xa4f110), 0xa4ea30 OnEvent, 0xa4ea70 OnEvent (-> 0xa4ea30),
//   0xa4f300 NotifyListeners, 0xa4f440 / 0xa4f3f0 NotifyListeners (-> 0xa4f300), 0xa4e850 AskListeners, 0xa4e970
//   AskListeners (-> 0xa4e850), 0xa4e720 Connect, 0xa4e7c0 Disconnect, 0xa4e740 GetEventManager (the cell 0x10f61e8),
//   0xa4e560 GetEventAttribute (the missing record 0x106b120), 0xa4e640 GetEventCount
// The partition level's bodies the octree shares (claimable now that both classes are ours): 0xa4ee60
//   RemovePartitionFalse, 0xa4ef20 QueryInterface, 0xa4f700 Accept, 0xa4f6d0 IsKindOf
// Adjustor thunks (ours are the compiler's, reached through the +8 / +0x6c faces):
//   0xa4f165 Release, 0xa4efbe QueryInterface, 0xa4f185 Release, 0xa4efc6 QueryInterface, 0xa4e529 AddRef, 0xa4f18d
//   Release, 0xa4e3ae scalar_deleting_destructor
// Vtordisp thunks of the virtual base (ours are the compiler's, reached through the virtual base):
//   0xa4ef5a QueryInterface, 0xa4e51b AddRef, 0xa4f16d Release, 0xa4f087 Read, 0xa4f735 GetTypeInfo, 0xa4fbe3 Write,
//   0xa4f2de GetOwnerInfo, 0xa4eac6 GetRecord18, 0xa4eaad GetRecord19
// The virtual base's other slots (7-17, 20-24) are thunks folded with nTSSG::cHeightMapShadowLight's: driven here (the
// Resource test), not counted.
//
// Left out of the word comparison (kSkip): the vtables and the vbptr (words 0, 2, 27, 58 and 34: each side's own; the
// vbtable's two entries are noted instead), words 37 and 38, whose upper bytes are padding the constructors never write
// (the cSGResource constructor 0xa3c270 clears its two flags at +0x94 / +0x95 with one 16-bit store, and 0xa4ddf0 stores
// one byte at +0x98, the in-query flag, so both sides keep the heap's leftovers there; the low half / byte are noted),
// and the six vectors (words 39-56: heap addresses), whose sizes, capacities and elements are noted instead.
#include "sg_transform_pilot.h"

#include "sg/cSceneNode.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kFactory = 0xa4e9b0;
constexpr unsigned kPartWord = 0x6c / 4;  // the cSGResource part's vtable
constexpr unsigned kVbptrWord = 0x88 / 4; // its vbptr
constexpr unsigned kFlagsWordAt = 0x94 / 4;
constexpr unsigned kInQueryWord = 0x98 / 4;
constexpr unsigned kVectorWord = 0x9c / 4; // six vectors of three words
constexpr unsigned kBaseWord = 0xe8 / 4;  // the virtual base's vtable
constexpr uint64_t kSkip = (1ull << kPartWord) | (1ull << kVbptrWord) | (1ull << kFlagsWordAt) | (1ull << kInQueryWord) |
                           (0x3ffffull << kVectorWord) | (1ull << kBaseWord);

uint32_t W(const void *p, unsigned word) { return reinterpret_cast<const uint32_t *>(p)[word]; }

// A vector's size, capacity and elements (normalised).
void NoteVector(const char *label, const void *node, unsigned word)
{
    const uint32_t begin = W(node, word), end = W(node, word + 1), capacity = W(node, word + 2);
    drawfx::Call c;
    c.note = label;
    c.words = {(end - begin) / 4, (capacity - begin) / 4};
    for (uint32_t p = begin; p != end; p += 4) c.words.push_back(drawfx::Norm(*reinterpret_cast<const uint32_t *>(uintptr_t(p))));
    drawfx::gLog.push_back(std::move(c));
}

// What kSkip leaves out of a scene node, in comparable form.
void NoteScene(const char *label, const cICompositionTreeNode *n)
{
    const uint8_t *b = reinterpret_cast<const uint8_t *>(n);
    const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(b + kVbptrWord * 4);
    drawfx::Note(label, {W(n, kFlagsWordAt) & 0xffff, W(n, kInQueryWord) & 0xff, uint32_t(vbtable[0]), uint32_t(vbtable[1]),
                        sgfx::gResources.SceneNodeIndex(n)});
    static const char *const kNames[] = {"partitions", "free partitions", "nodes", "free nodes", "pending partitions",
                                         "pending nodes"};
    for (unsigned i = 0; i < 6; ++i) NoteVector(kNames[i], n, kVectorWord + 3 * i);
}

// ---- the pilot ----
struct Cfg {
    using Interface = cISceneNode;
    static constexpr uint32_t kFactory = 0xa4e9b0;
    static constexpr uint32_t kSize = cSceneNode::kSize;
    static constexpr int kLevel = 3;
    static constexpr uint32_t kIid = cISceneNode::kIid;
    static constexpr uint32_t kEventCell = cSceneNodeBehaviorTraits::kEventManagerCell;
    static constexpr uint64_t kSkipWords = kSkip; // see the top
    // Every scene node a root. Each answers slot 32 with itself, so a node queues on the nearest scene node above it
    // (0xa39ae0); the tree code's propagation (0xa39820) asks the scene of the root for slot 96 instead. With scene nodes
    // nested those differ, and the original's 96 (0xa4f200) erases at find's end (no check): heap corruption (Slots seed
    // 1, the memcpy at 0x8264a0 reading past the queue). So the game cannot nest them, and the tests do not.
    static constexpr bool kRootsOnly = true;
    // A scene node queues itself (its 32 answers itself) and its destructor loops until the tree code (0xa39820) has
    // taken it off: that needs the bounded flag (1) and the queued flag (13), and 96 picks the queue by flag 6 as 95 did.
    // A flip of any of them between the two leaves the loop running for good (or, in the original, erasing at end).
    static constexpr uint32_t kFixedFlags = (1u << 1) | (1u << 6) | (1u << 13);
    // The framework (the constructor and destructor ask it for the scene graph system), the resource manager and a name.
    static void SetupCells(uint32_t seed, drawfx::Cells &cells)
    {
        sgfx::gPool.Handle("lamp_scen");
        sgfx::gWriter.fake.Returns(23, 0x2300u + seed);
        sgfx::gResources.Reset(cells);
        sgfx::gResources.manager.Returns(9, seed % 3);
        sgfx::gResources.manager.Returns(14, (seed + 1) % 3);
    }
    // cSGResource's record (0xa3c540: a version and the name), between the class's own record and the partition's.
    static void ReadResource(sgfx::Reader &r, std::mt19937 &rng)
    {
        r.Version(rng() % 3);
        r.Text(rng() % 2 ? "lamp_scen" : "");
    }
    static void NoteNode(const char *label, cICompositionTreeNode *n) { NoteScene(label, n); }
    static cICompositionTreeNode *Create() { return cSceneNode::Create(); }
};
using PilotP = sgtp::Pilot<Cfg>;

cISceneNode *S(cICompositionTreeNode *n) { return static_cast<cISceneNode *>(n); }
uint32_t CountOf(const cICompositionTreeNode *n) { return W(n, 1); }

// ---- the cSGResource faces ----
cIGZPersistResource *PartFace(cICompositionTreeNode *n)
{
    return reinterpret_cast<cIGZPersistResource *>(reinterpret_cast<uint8_t *>(n) + 0x6c);
}
cIGZUnknown *FaceOf(cICompositionTreeNode *n, unsigned via)
{
    switch (via) {
    case 0: return static_cast<cIGZUnknown *>(S(n));
    case 1: return static_cast<cIGZUnknown *>(n->GetReferent());
    case 2: return static_cast<cIGZUnknown *>(PartFace(n));
    default: return static_cast<cIGZUnknown *>(S(n)->GetResource());
    }
}
cIGZUnknown *Segment(std::mt19937 &rng)
{
    const unsigned k = rng() % 4;
    return k < 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[k]) : nullptr;
}

// A read's stream for a scene node: the records, the cSGResource part, the object-graph part (extensions, tag, no roots),
// the transform, the limit, the bound flag.
void QueueRead(std::mt19937 &rng)
{
    sgfx::Reader &r = sgfx::gReader;
    r.Version(rng() % 16);
    Cfg::ReadResource(r, rng);
    for (int i = 0; i < 4; ++i) r.Version(rng() % 16);
    r.Version(4);
    const uint32_t extensions = rng() % 2;
    r.Word(extensions);
    if (extensions) r.Ref(&PilotP::w.ext[1]);
    r.Text(rng() % 2 ? "beta" : "");
    r.Word(0);
    for (int i = 0; i < 7; ++i) r.Float(sgtx::RandomFloat(rng));
    r.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
    r.Word(rng() % 2);
}

void OperateResource(difftest::Context &ctx, sgfx::Factory make, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = PilotP::w.nodes;
    cICompositionTreeNode *n = ns[rng() % ns.count];
    cISGResource *face = S(n)->GetResource();
    const unsigned op = rng() % 11;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface through any face
        const uint32_t iids[] = {cISceneNode::kIid, cIPartitionNode::kIid, cISGResource::kIid, 0x9b11bf70, 0x5b0aaa74,
                                 0xbb11a53e, sgfx::kIidTree, 0xdb01d4b2, sgfx::kIidGraph, 1, 0x3b253dc7, 0xfb859324,
                                 cGZPersistResource::kIid, cIBoundedNode::kIid, 0x12345678};
        const uint32_t iid = iids[rng() % 15];
        const unsigned via = rng() % 4;
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        const bool r = rng() % 8 ? FaceOf(n, via)->QueryInterface(iid, &out) : FaceOf(n, via)->QueryInterface(iid, nullptr);
        drawfx::Note("query", {via, iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        if (out && CountOf(n) != before) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: { // AddRef / Release through any face, the count through the part's
        const unsigned via = rng() % 4;
        const uint32_t a = FaceOf(n, via)->AddRef();
        const uint32_t c = PartFace(n)->GetRefCount();
        const uint32_t r = FaceOf(n, via)->Release();
        drawfx::Note("counts", {via, a, c, r, face->GetPersistResource()->GetRefCount()});
        break;
    }
    case 2: { // the virtual base: segment, flag, the three assignments; the name and key
        switch (rng() % 5) {
        case 0: drawfx::Note("set segment", uint8_t(face->SetSegment(Segment(rng)))); break;
        case 1: face->SetFlag(uint8_t(rng())); break;
        case 2: drawfx::Note("assign", face->AssignAndSubmit(rng() % 2 ? "lamp_scen" : "beta", Segment(rng))); break;
        case 3: {
            const char *name = rng() % 2 ? "lamp_scen" : "beta";
            const uint32_t group = rng();
            drawfx::Note("open assign", face->OpenAssignAndSubmit(name, group, Segment(rng)));
            break;
        }
        default: drawfx::Note("reset", face->ResetAndSubmit(rng(), rng())); break;
        }
        sgfx::NoteText("name", face->GetName());
        cGZPersistResourceKey key{};
        face->CopyKeyTo(&key);
        drawfx::Note("resource", {face->GetFlag(), face->HasKey(), drawfx::Norm(P(face->GetSegment())), face->ForwardKey(),
                                  key.type, key.group, key.instance, key.resource});
        break;
    }
    case 3: { // the records and the virtual base's other slots
        sgfx::NoteText("record 18", static_cast<const char *>(face->GetRecord18()));
        sgfx::NoteText("record 19", static_cast<const char *>(face->GetRecord19()));
        cISGResource *other = S(ns[rng() % ns.count])->GetResource();
        drawfx::Note("misc", {drawfx::Norm(P(face->GetSubResource(rng() % 2))), drawfx::Norm(P(face->GetPersistResourceConst())),
                              uint32_t(face->CompareIdentity(other)), face->SubmitKeyToService(),
                              drawfx::Norm(P(S(n)->GetResourceConst()))});
        break;
    }
    case 4: { // the part's key slots
        cIGZPersistResource *part = PartFace(n);
        part->SetGroup(rng());
        part->SetInstance(rng() % 2 ? rng() : 0);
        const cGZPersistResourceKey key = part->GetKey();
        drawfx::Note("key", {key.type, key.group, key.instance, key.resource});
        break;
    }
    case 5: // the owner, through both persist faces
        drawfx::Note("owner info", {drawfx::Norm(n->GetReferent()->GetOwnerInfo()), drawfx::Norm(face->GetOwnerInfo())});
        break;
    case 6: { // a type record, through either persist face
        cPersistTypeInfo info = rng() % 2 ? n->GetReferent()->GetTypeInfo() : face->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 7: // written, through either persist face
        if (rng() % 2)
            n->GetReferent()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        break;
    case 8: { // read into a fresh node of the same side, through either persist face
        const sgfx::Reader &q = sgfx::gReader; // room for one more stream (6 records, 11 words, 2 texts, a reference)
        if (ns.count >= sgfx::Nodes::kMax || q.versionCount + 6 > sgfx::Reader::kMax || q.wordCount + 11 > sgfx::Reader::kMax ||
            q.textCount + 2 > 8 || q.refCount + 1 > sgfx::Reader::kMax)
            break;
        cICompositionTreeNode *fresh = make();
        fresh->AddRef();
        drawfx::AddRange(fresh, Cfg::kSize, sgfx::kNodeTag | (ns.count << 8));
        ns.at[ns.count++] = fresh;
        QueueRead(rng);
        sgfx::Reader &r = sgfx::gReader;
        if (rng() % 2)
            fresh->GetReferent()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        else
            S(fresh)->GetResource()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        drawfx::Note("read", {r.wordAt, r.refAt, r.textAt, r.versionAt});
        break;
    }
    case 9: { // kinds
        const uint32_t iids[] = {cISceneNode::kIid, cIPartitionNode::kIid, cISGResource::kIid, 0x9b11bf70,
                                 cIBoundedNode::kIid, sgfx::kIidTree, 0x5b0aaa74, 0x25232b11, 1};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    default: { // the scene answers 31 / 32 with itself, for every node below it too
        drawfx::Note("scene", {drawfx::Norm(n->GetFromParent31()), drawfx::Norm(n->GetFromParent32())});
        break;
    }
    }
}

} // namespace

DIFF_TEST(SG_SceneNode_Factory, kFactory) { PilotP::Factory(ctx); }

// The class-id static 0xa4ea10 (cdecl, no arguments, no side effects: one register load and a return).
DIFF_TEST(SG_SceneNode_ClassId, 0xa4ea10)
{
    const uint32_t original = ctx.Original<uint32_t(__cdecl *)()>(0xa4ea10)();
    const uint32_t ours = cSceneNode::GetClassId();
    ctx.cases++;
    if (original != ours) ctx.Fail("class id: original %08x, ours %08x", original, ours);
}

namespace {
// The record the behaviour's slot 14 copies for an unknown event (0x106b120, this instantiation's) is all zeros in the
// unstarted image, so a wrong record address would read zeros as well: EventRecord fills it with a pattern (restored
// after), as sg_resource_node.cpp does for its instantiation's.
struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106b120;
    uint32_t saved[6];
    nGZEvent::cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    MissingRecordScope()
    {
        std::memcpy(saved, GameAddress<void>(kAt), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(kAt)[i] = 0x5ce4e000 + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ce4e000 + i; // ours reads its own record
        }
    }
    ~MissingRecordScope() { std::memcpy(GameAddress<void>(kAt), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};
} // namespace

DIFF_TEST(SG_SceneNode_EventRecord, kFactory)
{
    PilotP::Registries registries;
    MissingRecordScope missing;
    sgfx::CheckSides(ctx, "event record", 8, PilotP::Build, [&](bool original, std::mt19937 &rng) {
        PilotP::w.nodes.Make(PilotP::Maker(ctx, original), 1, Cfg::kSize);
        cICompositionTreeNode *n = PilotP::w.nodes[0];
        drawfx::Note("event count", n->GetEventCount());
        for (int k = 0; k < 4; ++k) {
            const nGZEvent::cEventAttribute a = n->GetEventAttribute(rng() % 2 ? cGraphConnChanged::kEventId : 0x5678u);
            drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        }
        PilotP::ReleaseAll();
    });
    PilotP::w.manager.Drop();
}
DIFF_TEST(SG_SceneNode_Slots, kFactory) { PilotP::Slots(ctx); }
DIFF_TEST(SG_SceneNode_Persist, 0xa4f0a0) { PilotP::Persist(ctx); }
DIFF_TEST(SG_SceneNode_Clone, kFactory) { PilotP::Clone(ctx); }
DIFF_TEST(SG_SceneNode_Events, kFactory) { PilotP::Events(ctx); }

DIFF_TEST(SG_SceneNode_Resource, 0xa4ea20)
{
    PilotP::Registries registries;
    sgfx::CheckSides(ctx, "resource", 64, PilotP::Build, [&](bool original, std::mt19937 &rng) {
        const sgfx::Factory make = PilotP::Maker(ctx, original);
        PilotP::MakeTree(make, rng);
        for (unsigned i = 0; i < PilotP::w.nodes.count; ++i)
            if (rng() % 3 == 0) S(PilotP::w.nodes[i])->GetResource()->AssignAndSubmit(rng() % 2 ? "lamp_scen" : "alpha", nullptr);
        PilotP::NoteNodes("built");
        for (int k = 0; k < 24; ++k) OperateResource(ctx, make, k, rng);
        PilotP::NoteNodes("after");
        sgfx::gReader.NoteNames();
        PilotP::ReleaseAll();
        sgfx::gResources.NoteRefs();
        drawfx::Note("system refs", uint32_t(sgfx::gResources.system.refs));
    });
    PilotP::w.manager.Drop();
}

// ---- the content ----
namespace {

#define SN_UNKNOWN                                                                                                     \
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""}
#define SN_TRANSFORM_BOUNDED                                                                                           \
    {1, Ret::Int, true, "SetLimit", "u"}, {2, Ret::Int, true, "SetTransformParts", "F3F4"},                         \
        {1, Ret::Int, true, "SetTransform", "F7"}, {1, Ret::Int, true, "SetTranslation", "F3"},                     \
        {1, Ret::Int, true, "SetRotation", "F4"}, {1, Ret::Int, true, "SetBoundFlag", "b"},                         \
        {2, Ret::Int, true, "SetHolder", "iu"}, {0, Ret::Int, true, "GetHolderConst", ""},                          \
        {0, Ret::Int, true, "GetHolder", ""}, {0, Ret::Int, true, "Slot70", ""},                                    \
        {2, Ret::Int, true, "GetBoundBoxIn", "iF13"}, {1, Ret::Int, true, "GetBoundBox", "i"},                      \
        {2, Ret::Int, true, "Slot73", "iu"}, {1, Ret::Int, true, "IntersectsLinear", "i"},                          \
        {2, Ret::Int, true, "FirstHitLinear", "F1i"}, {0, Ret::Int, true, "GetHolderData", ""}
inline constexpr Slot kContentTail[] = {
    SN_TRANSFORM_BOUNDED,                                                                       // 61-76
    {1, Ret::Int, true, "AddLayer", "u"},    {1, Ret::Int, true, "AddLayerBits", "u"},    // 77, 78
    {1, Ret::Int, true, "RemoveLayer", "u"}, {1, Ret::Int, true, "RemoveLayerBits", "u"}, // 79, 80
    {1, Ret::Int, true, "SetLayerTag", "u"},      {2, Ret::Int, true, "Draw", "iu"},           // 81, 82
    {1, Ret::Int, true, "DeletingDestructor", "u"},                                        // 83
};
inline constexpr Slot kPartitionTail[] = {
    SN_TRANSFORM_BOUNDED,                                                                           // 61-76
    {0, Ret::Int, true, "Clear", ""},         {1, Ret::Int, true, "RemovePartition", "i"},      // 77, 78
    {1, Ret::Int, true, "RemoveNode", "i"},   {1, Ret::Int, true, "RemovePartitionFalse", "i"}, // 79, 80
    {1, Ret::Int, true, "AddPartition", "i"}, {1, Ret::Int, true, "UpdateNode", "i"},           // 81, 82
    {1, Ret::Int, true, "InsertNode", "i"},   {3, Ret::Int, true, "QueryShape", "iiu"},         // 83, 84
    {2, Ret::Int, true, "QueryLinear", "ii"}, {4, Ret::Int, true, "RayCast", "F1iii"},          // 85, 86
    {1, Ret::Int, true, "Cull", "i"},         {1, Ret::Int, true, "DeletingDestructor", "u"},   // 87, 88
};
// 60's first argument points into the world transform refresh's record (0xa39b70: record + 4 in its table at 0x10f5fc4,
// a buffer the game allocates on each side when the cells are fresh, so at a different address): logged by the 7 floats
// there (the node's world transform), not by address.
template <size_t N> constexpr std::array<Slot, N> WithRecordFloats(std::array<Slot, N> slots)
{
    slots[60].args = "F7uu";
    return slots;
}
inline constexpr auto kContentSlots = WithRecordFloats(sgfx::detail::ExtendNode<84>(kContentTail));
inline constexpr auto kPartitionSlots = WithRecordFloats(sgfx::detail::ExtendNode<89>(kPartitionTail));
inline constexpr drawfx::Iface kContent{"RenderableNode", 84, kContentSlots.data()};
inline constexpr drawfx::Iface kPartition{"PartitionNode", 89, kPartitionSlots.data()};
inline constexpr Slot kStrategySlots[] = {SN_UNKNOWN, {3, Ret::Int, true, "VisitShape", "iiu"},
                                          {2, Ret::Int, true, "VisitLinear", "ii"}};
inline constexpr drawfx::Iface kStrategy{"QueryStrategy", 5, kStrategySlots};
inline constexpr Slot kNearerSlots[] = {SN_UNKNOWN, {4, Ret::Int, true, "Visit", "iF1ii"}};
inline constexpr drawfx::Iface kNearer{"NearerStrategy", 4, kNearerSlots};
inline constexpr Slot kFilterSlots[] = {SN_UNKNOWN, {2, Ret::Int, true, "Accept", "ii"}};
inline constexpr drawfx::Iface kFilter{"VisibilityFilter", 4, kFilterSlots};
#undef SN_UNKNOWN
#undef SN_TRANSFORM_BOUNDED

// A fake's state (Fake::owner): its holder words, the scene its slot 32 answers (the tree code queues it there), and the
// answers its yes / no slots give, one bit per call.
struct State {
    uint32_t holder, data, scene, bits;
};
State &St(Fake &f) { return *static_cast<State *>(f.owner); }
uint32_t Answer(Fake &f)
{
    State &s = St(f);
    const uint32_t b = s.bits & 1;
    s.bits = (s.bits >> 1) | (b << 31);
    return b;
}
uint32_t AnswerSlot(Fake &f, const uint32_t *) { return Answer(f); }
uint32_t SetHolder(Fake &f, const uint32_t *a)
{
    St(f).holder = a[0];
    St(f).data = a[1];
    return 0;
}
uint32_t GetHolder(Fake &f, const uint32_t *) { return St(f).holder; }
uint32_t GetData(Fake &f, const uint32_t *) { return St(f).data; }
uint32_t SceneOf(Fake &f, const uint32_t *) { return St(f).scene; }
// 57 (out, in): the transform to the parent (the world transform refresh 0xa39b70 asks it), every float written.
uint32_t ToParent(Fake &f, const uint32_t *a)
{
    const float *in = reinterpret_cast<const float *>(uintptr_t(a[1]));
    float *out = reinterpret_cast<float *>(uintptr_t(a[0]));
    for (int i = 0; i < 3; ++i) out[i] = in[i] + float(f.id);
    for (int i = 3; i < 7; ++i) out[i] = in[i];
    return a[0];
}

constexpr unsigned kContents = 12, kParts = 3;
constexpr uint32_t kViewerTag = 0x7a000000; // (0x7c is the nodes' tag)
constexpr uint32_t kBounded = 0x2, kPartitionFlag = 0x40, kDrawn = 0x420, kHidden = 0x4000;
constexpr uint32_t kBoundFlag = 0x800, kStale = 0x1000, kQueued = 0x2000;

struct World {
    Fake content[kContents], parts[kParts], shape, linear, strategy, nearer, filter;
    State states[kContents + kParts + 3];
    alignas(16) uint8_t viewer[0x50]; // the filter at +0x20, a ray context at +0x40
};
World gw;

Fake *FakeAt(uint32_t p)
{
    for (Fake &f : gw.content)
        if (P(&f) == p) return &f;
    for (Fake &f : gw.parts)
        if (P(&f) == p) return &f;
    return nullptr;
}
// A sub-partition's AddPartition / InsertNode (81 / 83): when its answer says so it takes the node (the node's holder
// words become this fake and 0x100 + its id).
uint32_t Take(Fake &f, const uint32_t *a)
{
    if (!Answer(f)) return 0;
    if (Fake *t = FakeAt(a[0])) {
        St(*t).holder = P(&f);
        St(*t).data = 0x100 + f.id;
    }
    return 1;
}
// The nearer strategy (context, t, node, linear): when its answer says so the node is nearer: t halved, the node's id in
// the context word (what the scene's slot 92 returns).
uint32_t NearerVisit(Fake &f, const uint32_t *a)
{
    if (!Answer(f)) return 0;
    *reinterpret_cast<float *>(uintptr_t(a[1])) *= 0.5f;
    if (const Fake *node = reinterpret_cast<const Fake *>(uintptr_t(a[2]))) *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = node->id;
    return 0;
}

template <const drawfx::Iface &I> void MakeBounded(Fake &f, State &s, uint32_t id, bool partition, std::mt19937 &rng)
{
    sgfx::MakeFlagNode<I>(f, id);
    f.owner = &s;
    s = State{};
    s.bits = uint32_t(rng());
    f.value[sgfx::kFlagsWord] = kBounded | (partition ? kPartitionFlag : 0) | (rng() % 4 ? kDrawn : 0x20) |
                                (rng() % 5 ? 0 : kHidden) | (rng() % 4 ? kBoundFlag : 0) | (rng() % 6 ? 0 : kStale);
    f.On(32, &SceneOf);
    f.On(57, &ToParent);
    f.On(67, &SetHolder);
    f.On(68, &GetHolder);
    f.On(69, &GetHolder);
    f.On(70, &AnswerSlot);
    f.On(76, &GetData);
}

void BuildContent(uint32_t seed, drawfx::Cells &cells)
{
    PilotP::Build(seed, cells);
    std::mt19937 rng(seed * 31u + 7u);
    unsigned k = 0;
    for (unsigned i = 0; i < kContents; ++i) MakeBounded<kContent>(gw.content[i], gw.states[k++], 120 + i, false, rng);
    for (unsigned i = 0; i < kParts; ++i) {
        MakeBounded<kPartition>(gw.parts[i], gw.states[k++], 140 + i, true, rng);
        for (unsigned s : {73u, 74u, 80u, 82u, 84u, 85u}) gw.parts[i].On(s, &AnswerSlot);
        gw.parts[i].On(81, &Take);
        gw.parts[i].On(83, &Take);
    }
    gw.shape.Reset<sgfx::kUnknown>(150);
    gw.linear.Reset<sgfx::kUnknown>(151);
    gw.strategy.Reset<kStrategy>(152);
    gw.strategy.owner = &gw.states[k++];
    St(gw.strategy) = State{0, 0, 0, uint32_t(rng())};
    gw.strategy.On(3, &AnswerSlot);
    gw.strategy.On(4, &AnswerSlot);
    gw.nearer.Reset<kNearer>(153);
    gw.nearer.owner = &gw.states[k++];
    St(gw.nearer) = State{0, 0, 0, uint32_t(rng())};
    gw.nearer.On(3, &NearerVisit);
    gw.filter.Reset<kFilter>(154);
    gw.filter.owner = &gw.states[k++];
    St(gw.filter) = State{0, 0, 0, uint32_t(rng())};
    gw.filter.On(3, &AnswerSlot);
    std::memset(gw.viewer, 0, sizeof(gw.viewer));
    *reinterpret_cast<uint32_t *>(gw.viewer + cSceneNode::kViewerFilterOffset) = rng() % 3 ? P(&gw.filter) : 0;
    drawfx::AddRange(gw.viewer, sizeof(gw.viewer), kViewerTag);
}

cISceneNode *Scene() { return S(PilotP::w.nodes[0]); }
bool Holds(Fake &f) { return St(f).holder == P(static_cast<cIBoundedNode *>(Scene())); }

void NoteFakes()
{
    for (unsigned i = 0; i < kContents + kParts; ++i) {
        Fake &f = i < kContents ? gw.content[i] : gw.parts[i - kContents];
        drawfx::Note("fake", {f.id, uint32_t(f.refs), drawfx::Norm(St(f).holder), drawfx::Norm(St(f).data),
                              f.value[sgfx::kFlagsWord]});
    }
}

// The free sub-partition indices all name a sub-partition. UpdateNode's original bug (src/sg/cSceneNode.cpp) puts node
// indices on that list; AddPartition then stores at such an index, past the end of the sub-partitions when there are
// fewer of them (a heap write out of bounds on both sides), so the steps that can reach AddPartition wait until the list
// is good again (a flush's compaction or a sub-partition's removal makes it so).
bool FreePartitionsGood()
{
    const void *n = Scene();
    const uint32_t size = (W(n, kVectorWord + 1) - W(n, kVectorWord)) / 4;
    for (uint32_t p = W(n, kVectorWord + 3); p != W(n, kVectorWord + 4); p += 4)
        if (*reinterpret_cast<const uint32_t *>(uintptr_t(p)) >= size) return false;
    return true;
}

using TreeFn = void(__cdecl *)(void *);

void ContentStep(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    cISceneNode *o = Scene();
    const unsigned op = rng() % 19;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    Fake &c = gw.content[rng() % kContents];
    auto *cn = reinterpret_cast<cIBoundedNode *>(&c);
    Fake &s = gw.parts[rng() % kParts];
    auto *sp = reinterpret_cast<cIPartitionNode *>(&s);
    Fake &any = rng() % 4 ? c : s;
    auto *shape = reinterpret_cast<nGZCompGeom::cIBound *>(&gw.shape);
    auto *linear = reinterpret_cast<nGZCompGeom::cILinear3 *>(&gw.linear);
    auto *strategy = reinterpret_cast<cIIntersectionQueryStrategy *>(&gw.strategy);
    auto *nearer = reinterpret_cast<cINearerIntersectionQueryStrategy *>(&gw.nearer);
    const bool good = FreePartitionsGood();
    switch (op) {
    case 0:
    case 1:
    case 2: // insert a node no one holds, or update one this scene holds
        if (!St(c).holder) {
            drawfx::Note("insert", uint8_t(o->InsertNode(cn)));
        } else if (Holds(c)) {
            drawfx::Note("update", uint8_t(o->UpdateNode(cn)));
        }
        break;
    case 3: // remove (the original returns what SetHolder left in eax: not compared, see the header)
        if (Holds(c)) o->RemoveNode(cn);
        break;
    case 4: // a sub-partition in or out
        if (!St(s).holder) {
            if (good) drawfx::Note("add partition", uint8_t(o->AddPartition(sp)));
        } else if (Holds(s)) {
            if (rng() % 2)
                o->RemovePartition(sp);
            else
                drawfx::Note("remove partition false", uint8_t(o->RemovePartitionFalse(sp)));
        }
        break;
    case 5:
        if (rng() % 3 == 0) o->Clear();
        break;
    case 6: drawfx::Note("query shape", o->QueryShape(strategy, shape, rng() % 4)); break;
    case 7: drawfx::Note("query linear", o->QueryLinear(strategy, linear)); break;
    case 8: {
        float t = float(rng() % 64) / 4.0f;
        o->RayCast(&t, &gw.viewer[0x40], nearer, linear);
        drawfx::Note("ray", {std::bit_cast<uint32_t>(t), W(gw.viewer, 0x10)});
        break;
    }
    case 9: o->Cull(gw.viewer); break;
    case 10:
    case 11: // the scene's own queries: the queues flushed first, the in-query flag around the query
        if (!good) break;
        switch (rng() % 4) {
        case 0: drawfx::Note("scene shape", o->SceneQueryShape(strategy, shape, rng() % 4)); break;
        case 1: drawfx::Note("scene linear", o->SceneQueryLinear(strategy, linear)); break;
        case 2: {
            float t = float(rng() % 64) / 4.0f;
            const uint32_t hit = o->SceneRayCast(&t, nearer, linear);
            drawfx::Note("scene ray", {drawfx::Norm(hit), std::bit_cast<uint32_t>(t)});
            break;
        }
        default: o->SceneCull(gw.viewer); break;
        }
        break;
    case 12: // the tree code queues a node whose bound changed on its scene (slot 32's answer) unless it is queued
        ctx.Original<TreeFn>(0xa39ae0)(&any);
        break;
    case 13: // the tree code over a node about to be disconnected: off its scene's queue, out of its holder
        ctx.Original<TreeFn>(0xa39710)(&any);
        break;
    case 14:
        if (good) o->FlushPending();
        break;
    case 15: { // the bound slots, the resource, the scene answers, the holder (a scene keeps none)
        sgtx::NoteFloats("box", o->GetBoundBox().mMin, 6);
        const cTransform34 frame = sgtx::RandomFrame(rng);
        sgtx::NoteFloats("box in", o->GetBoundBoxIn(&frame).mMin, 6);
        drawfx::Note("70", uint8_t(o->IsEverywhere()));
        drawfx::Note("73", uint8_t(o->IntersectsBound(P(shape), rng() % 4)));
        drawfx::Note("74", uint8_t(o->IntersectsLinear(linear)));
        float t = float(rng() % 64) / 4.0f;
        const bool hit = o->FirstHitLinear(&t, linear);
        drawfx::Note("75", {uint8_t(hit), std::bit_cast<uint32_t>(t)});
        o->SetHolder(rng() % 2 ? &s : nullptr, rng());
        drawfx::Note("scene", {drawfx::Norm(P(o->GetResource())), drawfx::Norm(P(o->GetResourceConst())),
                               drawfx::Norm(o->GetFromParent31()), drawfx::Norm(o->GetFromParent32()),
                               drawfx::Norm(P(o->GetHolder())), o->GetHolderData()});
        break;
    }
    case 16: // the fakes change: flags (not 1, 6 or 13: the tree code's), the answers
        any.value[sgfx::kFlagsWord] ^= (rng() % 2 ? kHidden : 0) | (rng() % 3 ? 0 : 0x400) | (rng() % 3 ? 0 : kBoundFlag) |
                                       (rng() % 3 ? 0 : kStale);
        St(any).bits ^= uint32_t(rng());
        St(gw.strategy).bits ^= uint32_t(rng());
        break;
    case 17: { // the kinds and the interfaces
        for (uint32_t iid : {cISceneNode::kIid, cIPartitionNode::kIid, cIBoundedNode::kIid, cISGResource::kIid})
            drawfx::Note("kind", {iid, uint8_t(o->IsKindOf(iid))});
        const uint32_t iids[] = {cISceneNode::kIid, cIPartitionNode::kIid, cISGResource::kIid, 0x12345678};
        void *out = nullptr;
        const uint32_t iid = iids[rng() % 4];
        const uint8_t r = uint8_t(o->QueryInterface(iid, &out));
        drawfx::Note("query", {iid, r, drawfx::Norm(P(out))});
        if (out) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    default: // a scene does not take itself
        if (good) drawfx::Note("add self", uint8_t(o->AddPartition(static_cast<cIPartitionNode *>(o))));
        break;
    }
    PilotP::NoteNodes("after");
    NoteFakes();
}

void Content(difftest::Context &ctx, const char *what, unsigned seeds, int steps)
{
    PilotP::Registries registries;
    sgfx::CheckSides(ctx, what, seeds, BuildContent, [&](bool original, std::mt19937 &rng) {
        PilotP::w.nodes.Make(PilotP::Maker(ctx, original), 1, Cfg::kSize);
        for (unsigned i = 0; i < kContents + kParts; ++i) {
            Fake &f = i < kContents ? gw.content[i] : gw.parts[i - kContents];
            St(f).scene = P(static_cast<cIBoundedNode *>(Scene()));
        }
        PilotP::NoteNodes("built");
        for (int k = 0; k < steps; ++k) ContentStep(ctx, k, rng);
        // The destructor: Clear (the nodes queued again by the tree code), the queues emptied through 0xa39710, the
        // system told (when there is one).
        sgfx::gResources.systemUp = rng() % 4 != 0;
        PilotP::ReleaseAll();
        NoteFakes();
        drawfx::Note("system refs", uint32_t(sgfx::gResources.system.refs));
    });
    PilotP::w.manager.Drop();
}

// The flush's compaction threshold at its float ties (lane OP; a coordinator mutant `>` -> `>=` survived the random
// steps): 3 free entries of 20 and 6 of 40, where free * 6.6666665f rounds to exactly the size and the original leaves the
// list alone; 4 of 20 and 7 of 40, where it compacts. One content fake is placed `size` times (its holder words cleared
// before each insert, so the scene appends it again), then removed at `free` indices (its holder data set to each), then
// a second fake (bound and stale flags clear: the flush only clears its queued flag) is queued so the flush runs.
unsigned gTieCase = 0;

void CompactTie(difftest::Context &ctx)
{
    static constexpr uint32_t kCases[][2] = {{20, 3}, {40, 6}, {20, 4}, {40, 7}}; // {size, free}
    PilotP::Registries registries;
    sgfx::CheckSides(
        ctx, "compact tie", 4,
        [](uint32_t seed, drawfx::Cells &cells) {
            BuildContent(seed, cells);
            gTieCase = seed % 4;
        },
        [&](bool original, std::mt19937 &) {
            PilotP::w.nodes.Make(PilotP::Maker(ctx, original), 1, Cfg::kSize);
            cISceneNode *o = Scene();
            Fake &placed = gw.content[0], &queued = gw.content[1];
            for (unsigned i = 0; i < kContents + kParts; ++i) {
                Fake &f = i < kContents ? gw.content[i] : gw.parts[i - kContents];
                St(f).scene = P(static_cast<cIBoundedNode *>(o));
            }
            const uint32_t size = kCases[gTieCase][0], free = kCases[gTieCase][1];
            drawfx::Note("case", {size, free});
            for (uint32_t i = 0; i < size; ++i) {
                St(placed).holder = 0;
                o->InsertNode(reinterpret_cast<cIBoundedNode *>(&placed));
            }
            for (uint32_t k = 0; k < free; ++k) {
                St(placed).data = 1 + k * 5;
                o->RemoveNode(reinterpret_cast<cIBoundedNode *>(&placed));
            }
            queued.value[sgfx::kFlagsWord] &= ~(kBoundFlag | kStale);
            o->QueueNode(reinterpret_cast<cIBoundedNode *>(&queued));
            PilotP::NoteNodes("before flush");
            o->FlushPending();
            PilotP::NoteNodes("after flush");
            NoteFakes();
            sgfx::gResources.systemUp = true;
            PilotP::ReleaseAll();
            NoteFakes();
        });
    PilotP::w.manager.Drop();
}

} // namespace

DIFF_TEST(SG_SceneNode_CompactTie, 0xa4f800) { CompactTie(ctx); }
DIFF_TEST(SG_SceneNode_Content, 0xa4f800) { Content(ctx, "content", 96, 40); }
DIFF_TEST(SG_SceneNode_ContentLong, 0xa4ec10) { Content(ctx, "content long", 24, 160); }
