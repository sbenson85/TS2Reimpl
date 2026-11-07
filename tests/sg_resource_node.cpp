// Differential tests of nGZSceneGraph::cResourceNode (lane OG, scenegraph N1; src/sg/cResourceNode.h): the game's node (its
// factory 0xa503a0, constructor 0xa4fd10, vtables 0xe6d698 / 0xe6d7a4 / 0xe6d7c4 / 0xe6d7fc and the bodies behind them)
// against ours (Create, our vtables), each side building its own nodes with its own factory. The tests drive whole trees
// through the node's slots, the +8 persist face, the cSGResource part's face (+0x28) and the virtual base (+0x60), and
// compare the calls the nodes make on the fakes (sgfx.h), the notes, every node's words but its vtable words and its
// vbptr (the game's tables on one side, ours on the other: words 0, 2, 10, 17 and 24; the vbtable's two entries are
// compared instead) and the padding at +0x52 (kSkipWords), the name pool's counts, the resource manager's and segments'
// references, and the blocks freed when the test lets go of the nodes.
//
//   SG_ResourceNode_Factory  a new node: its words, vbtable, shape, faces.
//   SG_ResourceNode_ClassId  the class-id static 0xa50400 (hooked) against GetClassId.
//   SG_ResourceNode_Slots    random operations through every face of random nodes in a random forest, sources made by
//                            cloning (the clone instances the node it was cloned from, or that node's source).
//   SG_ResourceNode_Persist  Write through the +8 face or the virtual base (with or without a source), then Read into a
//                            fresh node or a clone with a source: both branches of the flag, a source whose children are
//                            cloned in, old versions.
//   SG_ResourceNode_Clone    chains of clones (a clone of a clone instances the first node's source), with and without a
//                            clone context, in every mode.
//   SG_ResourceNode_Events   the behaviour with a connection manager in this instantiation's cell 0x10f61fc.
//
// The game's bodies these tests run on the original side, with ours (the same slots through our vtables) on the
// other; db/status/cResourceNode.jsonl has an entry for each.
// The class's own bodies (reached through our vtables):
//   0xa4fe50 ~cResourceNode, 0xa4ff50 scalar_deleting_destructor, 0xa50620 QueryInterface, 0xa508c0 Release, 0xa50090
//   Clone, 0xa504b0 CopyContentTo, 0xa50af0 IsKindOf, 0xa50b20 Accept, 0xa4ff90 GetResource, 0xa50690 Read, 0xa50b40
//   GetTypeInfo, 0xa50c00 Write, 0xa508f0 GetOwnerInfo, 0xa50ac0 SetSegment, 0xa505f6 GetRecord18, 0xa505e7
//   GetRecord19
// This instantiation's template-level bodies (reached through our vtables, the behaviour in _Events):
//   0xa4fc80 cAbstractBehaviorTypeT (stores the base vtable 0xe6d334; called by 0xa4fcb0 only), 0xa4fcb0
//   cObjectGraphNodeT (stores 0xe6d440 / 0xe6d54c; called by 0xa4fc90 only), 0xa4fc90 cCompositionTreeNodeT (stores
//   0xe6d56c / 0xe6d678; called by the constructor 0xa4fd10 only), 0xa4fdc0 ~cAbstractBehaviorTypeT (called by
//   0xa4fe10, 0xa4fef0 and the constructor's unwind funclet 0xd723e3), 0xa4fe10 ~cObjectGraphNodeT (called by 0xa4fdd0
//   and 0xa4ff30), 0xa4fdd0 ~cCompositionTreeNodeT (called by the destructor 0xa4fe50, 0xa4ff10 and the constructor's
//   unwind funclet 0xd72403), 0xa4fef0 scalar_deleting_destructor (slot 65 of 0xe6d334), 0xa4ff30
//   scalar_deleting_destructor (slot 65 of 0xe6d440), 0xa4ff10 scalar_deleting_destructor (slot 65 of 0xe6d56c),
//   0xa50850 Release, 0xa508a0 Release (-> 0xa50850), 0xa50420 OnEvent, 0xa50460 OnEvent (-> 0xa50420), 0xa50910
//   NotifyListeners, 0xa50a50 NotifyListeners (-> 0xa50910), 0xa50a00 NotifyListeners (-> 0xa50910), 0xa50240
//   AskListeners, 0xa50360 AskListeners (-> 0xa50240), 0xa50180 Connect, 0xa50220 Disconnect, 0xa501a0 GetEventManager
//   (the cell 0x10f61fc), 0xa4ffb0 GetEventAttribute (the missing record 0x106b328)
// Adjustor thunks (ours are the compiler's, reached through the +8 / +0x28 faces):
//   0xa5066e QueryInterface, 0xa508c5 Release, 0xa50676 QueryInterface, 0xa4ff7b AddRef, 0xa508cd Release, 0xa50410
//   GetRefCount, 0xa4fee5 scalar_deleting_destructor, 0xa508a5 Release
// Vtordisp thunks of the virtual base (ours are the compiler's, reached through the virtual base):
//   0xa50606 QueryInterface, 0xa4ff70 AddRef, 0xa508ad Release, 0xa5067e Read, 0xa50b2f GetTypeInfo, 0xa50bec Write,
//   0xa508e0 GetOwnerInfo, 0xa4ff83 GetPersistResourceConst, 0xa508d5 GetSubResource, 0xa50a7f SubmitKeyToService,
//   0xa505c6 CompareIdentity, 0xa50aab AssignAndSubmit, 0xa50a95 ResetAndSubmit, 0xa50aa0 OpenAssignAndSubmit,
//   0xa50ab6 SetSegment, 0xa50a8a SetFlag, 0xa505dc GetName, 0xa5049d HasKey, 0xa50902 CopyKeyTo, 0xa505d1 GetFlag,
//   0xa5083b ForwardKey
#include "difftest.h"
#include "sgfx.h"

#include "sg/cResourceNode.h"

#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kFactory = 0xa503a0;
constexpr uint32_t kSize = cResourceNode::kSize;
// Left out of the word dump: the vtables and the vbptr (words 0, 2, 10, 17, 24), and word 20 (+0x50), whose upper half
// is padding after the cSGResource part's two flags (the heap's leftovers on each side): its low half is noted instead.
// Why the upper half is padding: the base constructor 0xa3c270 clears the two flags with one 16-bit store at the part's
// +0x28 and writes nothing at +0x2a / +0x2b, and cResourceNode's constructor 0xa4fd10 stores only the dwords at +0x54
// and +0x58 after it, so both sides keep whatever the heap left there.
constexpr uint32_t kSkipWords = (1u << 0) | (1u << 2) | (1u << 10) | (1u << 17) | (1u << 20) | (1u << 24);
constexpr uint32_t kPart = 0x28;     // the cSGResource part (cIGZPersistResource face)
constexpr uint32_t kVbptr = 0x44;    // the part's vbptr
constexpr unsigned kSeeds = 64;

cICompositionTreeNode *__cdecl MakeOurs() { return cResourceNode::Create(); }
sgfx::Factory Maker(difftest::Context &ctx, bool original)
{
    return original ? ctx.Original<sgfx::Factory>(kFactory) : &MakeOurs;
}

cIResourceNode *R(cICompositionTreeNode *n) { return static_cast<cIResourceNode *>(n); }
cIGZPersistResource *PartFace(cICompositionTreeNode *n)
{
    return reinterpret_cast<cIGZPersistResource *>(reinterpret_cast<uint8_t *>(n) + kPart);
}
cIGZPersistSerializableReferent *PersistFace(cICompositionTreeNode *n) { return n->GetReferent(); }
uint32_t CountOf(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }

struct World {
    sgfx::Nodes nodes;
    Fake ext[3]; // ids 200-202
    Fake traversal;
    Fake context;
    sgfx::Manager manager; // in the behaviour's cell 0x10f61fc
    unsigned unheld = 0;   // nodes named past kMax (Hold)
};
World w;

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gPool.Handle("alpha");
    sgfx::gPool.Handle("lamp_cres");
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::gWriter.fake.Returns(23, 0x2300u + seed);
    sgfx::gResources.Reset(cells);
    sgfx::gResources.manager.Returns(9, seed % 3);
    sgfx::gResources.manager.Returns(14, (seed + 1) % 3);
    for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 2);
    sgfx::MakeTraversal(w.traversal, 450, rng() % 3);
    w.nodes = sgfx::Nodes{};
    w.unheld = 0;
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cResourceNodeBehaviorTraits::kEventManagerCell);
}

// A held node at index i: named and counted by the test. False when the test holds kMax nodes already: then a counted
// node is released (a new clone is gone after that), and an uncounted one (a node in a held tree) is left to its holders,
// only named (index kMax and up), so the words pointing at it are normalised on both sides.
bool Hold(cICompositionTreeNode *n, bool counted)
{
    if (w.nodes.count >= sgfx::Nodes::kMax) {
        if (counted)
            n->Release();
        else
            drawfx::AddRange(n, kSize, sgfx::kNodeTag | ((sgfx::Nodes::kMax + w.unheld++) << 8));
        return false;
    }
    drawfx::AddRange(n, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
    if (!counted) n->AddRef();
    w.nodes.at[w.nodes.count++] = n;
    return true;
}

// The nodes under `root` the test does not know yet (a deep clone's, a read's) join the held nodes.
void HoldSubtree(cICompositionTreeNode *root)
{
    for (cICompositionTreeNode *c = root->GetFirstChild(); c; c = c->GetNextSibling()) {
        if (drawfx::Norm(P(c)) == P(c)) Hold(c, false);
        HoldSubtree(c);
    }
}

// A random forest of 1-6 nodes from `make`, some named.
void MakeTree(sgfx::Factory make, std::mt19937 &rng)
{
    const unsigned n = 1 + rng() % 6;
    int parents[sgfx::Nodes::kMax];
    parents[0] = -1;
    for (unsigned i = 1; i < n; ++i) parents[i] = rng() % 5 ? int(rng() % i) : -1;
    w.nodes.Make(make, n, kSize);
    w.nodes.Link(parents);
    for (unsigned i = 0; i < n; ++i)
        if (rng() % 3 == 0) R(w.nodes[i])->GetResource()->AssignAndSubmit(rng() % 2 ? "lamp_cres" : "alpha", nullptr);
}

// Each node's words (kSkipWords left out), the cSGResource part's flags, its vbtable's entries and its source and word
// through the slots.
void NoteNodes(const char *label)
{
    w.nodes.NoteWords(label, kSkipWords);
    w.nodes.NoteShape(label);
    for (unsigned i = 0; i < w.nodes.count; ++i) {
        const uint8_t *b = reinterpret_cast<const uint8_t *>(w.nodes[i]);
        const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(b + kVbptr);
        cIResourceNode *r = R(w.nodes[i]);
        const uint32_t flags = reinterpret_cast<const uint32_t *>(b)[20] & 0xffff; // +0x50 / +0x51
        drawfx::Note(label, {i, uint32_t(vbtable[0]), uint32_t(vbtable[1]), flags, r->Slot64(),
                             drawfx::Norm(P(r->GetResource())), drawfx::Norm(P(r->GetResourceConst()))});
    }
}

// Releases the test's references, noting the sizes of the blocks freed and what the nodes held.
void ReleaseAll()
{
    difftest::FreeRecorder freed;
    w.nodes.Release();
    freed.Stop();
    size_t sizes[64];
    const size_t n = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    for (const Fake &e : w.ext) drawfx::Note("extension", {e.id, uint32_t(e.refs), drawfx::Norm(e.value[0]), drawfx::Norm(e.value[1])});
    sgfx::gPool.NoteCounts();
    sgfx::gResources.NoteRefs();
}

bool InSubtree(cICompositionTreeNode *root, cICompositionTreeNode *n)
{
    for (; n; n = n->GetParent())
        if (n == root) return true;
    return false;
}

cIGZUnknown *Segment(std::mt19937 &rng)
{
    const unsigned k = rng() % 4;
    return k < 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[k]) : nullptr;
}

// A clone of `n` (mode, iid, a context answering `prepared` or none), held when new; returns it (or null).
cICompositionTreeNode *CloneOf(cICompositionTreeNode *n, uint32_t mode, uint32_t iid, cICompositionTreeNode *prepared)
{
    cINodeCloneContext *context = nullptr;
    if (prepared) {
        sgfx::MakeCloneContext(w.context, prepared);
        context = reinterpret_cast<cINodeCloneContext *>(&w.context);
    }
    void *out = nullptr;
    n->Clone(mode, iid, &out, context);
    // Every iid asked for here is answered with the node itself (offset 0), or not at all.
    auto *clone = static_cast<cICompositionTreeNode *>(out);
    const bool isNew = clone && drawfx::Norm(P(clone)) == P(clone);
    if (isNew && !Hold(clone, true)) { // the test holds kMax nodes: the clone was released (and destroyed)
        drawfx::Note("clone dropped", {mode, iid});
        return nullptr;
    }
    drawfx::Note("clone", {mode, iid, clone != nullptr, uint8_t(isNew), drawfx::Norm(P(clone))});
    if (clone && !isNew) clone->Release();
    if (clone) HoldSubtree(clone);
    return clone;
}

// One random operation on a random node through one of its faces, its results noted.
void Operate(difftest::Context &ctx, sgfx::Factory make, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    cICompositionTreeNode *n = ns[rng() % ns.count];
    cISGResource *face = R(n)->GetResource();
    const unsigned op = rng() % 20;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface through any face
        const uint32_t iids[] = {cIResourceNode::kIid, cISGResource::kIid, 0x9b11bf70, 0x5b0aaa74, 0xbb11a53e,
                                 sgfx::kIidTree, 0xdb01d4b2, sgfx::kIidGraph, 1, 0x3b253dc7, 0xfb859324,
                                 cGZPersistResource::kIid, 0x12345678};
        const uint32_t iid = iids[rng() % 13];
        const unsigned via = rng() % 4;
        cIGZUnknown *on = via == 0   ? static_cast<cIGZUnknown *>(R(n))
                          : via == 1 ? static_cast<cIGZUnknown *>(PersistFace(n))
                          : via == 2 ? static_cast<cIGZUnknown *>(PartFace(n))
                                     : static_cast<cIGZUnknown *>(face);
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        const bool r = rng() % 8 ? on->QueryInterface(iid, &out) : on->QueryInterface(iid, nullptr);
        drawfx::Note("query", {via, iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        // Some answers are not counted (kIidRawComposition, kIidBehaviorType: the node itself): released only when counted.
        if (out && CountOf(n) != before) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: { // AddRef / Release through any face, the count through the part's
        const unsigned via = rng() % 4;
        cIGZUnknown *on = via == 0   ? static_cast<cIGZUnknown *>(R(n))
                          : via == 1 ? static_cast<cIGZUnknown *>(PersistFace(n))
                          : via == 2 ? static_cast<cIGZUnknown *>(PartFace(n))
                                     : static_cast<cIGZUnknown *>(face);
        const uint32_t a = on->AddRef();
        const uint32_t c = PartFace(n)->GetRefCount();
        const uint32_t r = on->Release();
        drawfx::Note("counts", {via, a, c, r, face->GetPersistResource()->GetRefCount()});
        break;
    }
    case 2: { // move a node: removed from its parent, added to another node outside its subtree
        cICompositionTreeNode *c = ns[rng() % ns.count];
        if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
        cICompositionTreeNode *to = ns[rng() % ns.count];
        if (!InSubtree(c, to)) to->AddChild(c);
        break;
    }
    case 3: { // flags (not bit 1: it marks a bounded node for the tree code)
        uint32_t bit = rng() % 40;
        if ((bit & 31) == 1) bit = 0;
        n->SetFlag(bit, rng() % 2);
        drawfx::Note("flags", {uint8_t(n->TestFlag(bit)), uint8_t(n->TestFlag(rng() % 40)), n->GetFlags()});
        break;
    }
    case 4: { // the tag
        const char *texts[] = {"alpha", "lamp_cres", "", "gamma", nullptr};
        n->SetTag(texts[rng() % 5]);
        sgfx::NoteText("tag", n->GetTag());
        drawfx::Note("tag handle", n->GetTagHandle());
        break;
    }
    case 5: { // kinds
        const uint32_t iids[] = {cIResourceNode::kIid, cISGResource::kIid, 0x9b11bf70, sgfx::kIidTree, sgfx::kIidGraph,
                                 0x3b253dc7, 0x5b0aaa74, 1, 0, 0xdb01d4b2, 0xe519c933};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 6: { // children, links, the composition defaults
        const cChildRange a = n->GetChildren(), b = n->GetChildrenConst();
        drawfx::Note("children", {drawfx::Norm(P(a.mCurrent)), drawfx::Norm(P(a.mEnd)), drawfx::Norm(P(b.mBegin)),
                                  uint8_t(n->HasChildren()), n->GetLimit55(), n->GetFromParent31()});
        break;
    }
    case 7: { // traversals: the visitor's slot 12
        auto *t = reinterpret_cast<cICompositionTreeTraversal *>(&w.traversal);
        drawfx::Note("accept", {n->Accept(t), n->AcceptReadOnly(t)});
        break;
    }
    case 8: { // extensions
        Fake &e = w.ext[rng() % 3];
        auto *ext = reinterpret_cast<cIExtension *>(&e);
        if (n->HasExtension(ext))
            n->RemoveExtension(ext);
        else if (!e.value[sgfx::kExtOwnerWord])
            n->AddExtension(ext);
        drawfx::Note("extensions", {drawfx::Norm(P(n->GetFirstExtension())), uint8_t(n->HasExtension(ext))});
        break;
    }
    case 9: // the word
        if (rng() % 2) R(n)->Slot63(rng());
        drawfx::Note("word", R(n)->Slot64());
        break;
    case 10: { // the virtual base: segment (the node's own releases the source first), flag, the three assignments,
               // name, key
        switch (rng() % 5) {
        case 0: drawfx::Note("set segment", uint8_t(face->SetSegment(Segment(rng)))); break;
        case 1: face->SetFlag(uint8_t(rng())); break;
        case 2:
            drawfx::Note("assign", face->AssignAndSubmit(rng() % 2 ? "lamp_cres" : "beta", Segment(rng)));
            break;
        case 3: {
            const char *name = rng() % 2 ? "lamp_cres" : "beta";
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
    case 11: { // the virtual base's other slots
        sgfx::NoteText("record 18", static_cast<const char *>(face->GetRecord18()));
        sgfx::NoteText("record 19", static_cast<const char *>(face->GetRecord19()));
        cISGResource *other = R(ns[rng() % ns.count])->GetResource();
        drawfx::Note("misc", {drawfx::Norm(P(face->GetSubResource(rng() % 2))), drawfx::Norm(P(face->GetPersistResourceConst())),
                              uint32_t(face->CompareIdentity(other)), face->SubmitKeyToService()});
        break;
    }
    case 12: { // the part's key slots
        cIGZPersistResource *part = PartFace(n);
        part->SetGroup(rng());
        part->SetInstance(rng() % 2 ? rng() : 0);
        const cGZPersistResourceKey key = part->GetKey();
        drawfx::Note("key", {key.type, key.group, key.instance, key.resource});
        break;
    }
    case 13: // the owner, through both persist faces
        drawfx::Note("owner info", {drawfx::Norm(PersistFace(n)->GetOwnerInfo()), drawfx::Norm(face->GetOwnerInfo())});
        break;
    case 14: { // a type-info record, through either persist face
        cPersistTypeInfo info = rng() % 2 ? PersistFace(n)->GetTypeInfo() : face->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 15: // written, through either persist face
        if (rng() % 2)
            PersistFace(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        break;
    case 16: { // a clone (the clone instances this node, or this node's source)
        const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, cIResourceNode::kIid, 0x12345678};
        CloneOf(n, rng() % 3, iids[rng() % 4], nullptr);
        break;
    }
    case 17: { // a clone into a node a context answers (shallow: the context answers that node for every source)
        if (ns.count >= sgfx::Nodes::kMax) break; // the prepared node must be held
        cICompositionTreeNode *prepared = make();
        Hold(prepared, false);
        CloneOf(n, rng() % 2 ? 0 : 2, cIResourceNode::kIid, prepared);
        break;
    }
    case 18: { // events up the tree
        cICompositionTreeNode *sub = ns[rng() % ns.count];
        cGraphConnChanged body(rng() % 3, n, sub, rng() % 2 ? sgfx::kIidTree : sgfx::kIidGraph);
        n->NotifyListeners(cGraphConnChanged::kEventId, rng(), &body);
        drawfx::Note("asked", uint8_t(n->AskListeners(0x1234, rng(), &body)));
        break;
    }
    default: { // an event to the node itself
        cICompositionTreeNode *sub = ns[rng() % ns.count];
        cGraphConnChanged body(rng() % 3, n, sub, sgfx::kIidTree);
        n->OnEvent(rng() % 2 ? cGraphConnChanged::kEventId : 0x777, 0, &body, 0);
        break;
    }
    }
}

} // namespace

DIFF_TEST(SG_ResourceNode_Factory, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    sgfx::CheckSides(ctx, "factory", 4, Build, [&](bool original, std::mt19937 &) {
        cICompositionTreeNode *n = Maker(ctx, original)();
        drawfx::Note("count", CountOf(n));
        w.nodes.size = kSize;
        Hold(n, false);
        NoteNodes("new node");
        ReleaseAll();
    });
    w.manager.Drop();
}

// The class-id static 0xa50400 (cdecl, no arguments, no side effects: one register load and a return).
DIFF_TEST(SG_ResourceNode_ClassId, 0xa50400)
{
    const uint32_t original = ctx.Original<uint32_t(__cdecl *)()>(0xa50400)();
    const uint32_t ours = cResourceNode::GetClassId();
    ctx.cases++;
    if (original != ours) ctx.Fail("class id: original %08x, ours %08x", original, ours);
}

DIFF_TEST(SG_ResourceNode_Slots, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    sgfx::CheckSides(ctx, "slots", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        sgfx::Factory make = Maker(ctx, original);
        MakeTree(make, rng);
        NoteNodes("built");
        for (int k = 0; k < 20; ++k) Operate(ctx, make, k, rng);
        NoteNodes("after");
        sgfx::gReader.NoteNames();
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ResourceNode_Persist, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    sgfx::CheckSides(ctx, "persist", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        sgfx::Factory make = Maker(ctx, original);
        MakeTree(make, rng);
        // The source a node instances: a node of the forest (its children are what a read copies in).
        cICompositionTreeNode *source = w.nodes[rng() % w.nodes.count];
        cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
        if (rng() % 2) // a clone instances the source (mode 2: no source after all)
            if (cICompositionTreeNode *clone = CloneOf(source, rng() % 2 ? 0 : 2, sgfx::kIidTree, nullptr)) n = clone;
        if (rng() % 2) n->SetTag("alpha");
        if (rng() % 2) n->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[0]));
        if (rng() % 2) R(n)->Slot63(rng());
        cISGResource *face = R(n)->GetResource();
        if (rng() % 2)
            n->GetReferent()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));

        // Read into a fresh node of the same side, through either persist face; a third of the reads go into a mode-0
        // clone, which instances a node already (the read lets go of that source first).
        cICompositionTreeNode *fresh =
            rng() % 3 ? nullptr : CloneOf(w.nodes[rng() % w.nodes.count], 0, sgfx::kIidTree, nullptr);
        drawfx::Note("read into", {fresh != nullptr});
        if (!fresh) {
            fresh = make();
            Hold(fresh, false);
        }
        sgfx::Reader &r = sgfx::gReader;
        const uint32_t version = rng() % 2 ? 7 : rng() % 9;
        const bool whole = rng() % 2; // the flag: the resource and composition parts, or the object-graph part alone
        r.Version(version);
        r.Word(whole);
        if (whole) {
            r.Version(rng() % 3);     // cSGResource's record
            r.Text(rng() % 2 ? "lamp_cres" : "");
            r.Version(rng() % 16);    // the composition part's
        }
        r.Version(4); // the object-graph part's
        const uint32_t extensions = rng() % 2;
        r.Word(extensions);
        if (extensions) r.Ref(&w.ext[1]);
        r.Text(rng() % 2 ? "alpha" : "");
        if (whole) {
            uint32_t roots = 0;
            for (unsigned i = 0; i < w.nodes.count; ++i)
                if (!w.nodes[i]->GetParent() && w.nodes[i] != source && w.nodes[i] != fresh && rng() % 2) ++roots;
            r.Word(roots);
            for (unsigned i = 0, k = 0; i < w.nodes.count && k < roots; ++i)
                if (!w.nodes[i]->GetParent() && w.nodes[i] != source && w.nodes[i] != fresh) r.Ref(w.nodes[i]), ++k;
        }
        const bool withSource = rng() % 3 != 0;
        r.Ref(withSource ? static_cast<void *>(R(source)) : nullptr);
        r.Word(0x5800 + rng() % 16); // the word
        r.Text("dropped");          // read before version 7
        if (rng() % 2)
            fresh->GetReferent()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        else
            R(fresh)->GetResource()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        drawfx::Note("read", {r.wordAt, r.refAt, r.textAt, r.versionAt});
        HoldSubtree(fresh);
        NoteNodes("after");
        sgfx::gReader.NoteNames();
        // The fresh node written back: with a source the object-graph part and the source's resource.
        fresh->GetReferent()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ResourceNode_Clone, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    sgfx::CheckSides(ctx, "clone", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        sgfx::Factory make = Maker(ctx, original);
        MakeTree(make, rng);
        const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, cIResourceNode::kIid, 0x12345678};
        for (int k = 0; k < 4; ++k) {
            cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
            if (rng() % 2) n->SetTag("lamp_cres");
            if (rng() % 3 == 0) n->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[k % 3]));
            if (rng() % 2) R(n)->Slot63(rng());
            const uint32_t mode = rng() % 3;
            // A context answering a held node (not n), or none; only when no children are cloned (mode 1 asks the
            // context for every child's clone, and the fake answers the same node each time).
            cICompositionTreeNode *prepared = nullptr;
            if (mode != 1 && rng() % 3 == 0) {
                prepared = w.nodes[rng() % w.nodes.count];
                if (prepared == n || InSubtree(n, prepared) || InSubtree(prepared, n)) prepared = nullptr;
            }
            CloneOf(n, mode, iids[rng() % 4], prepared);
        }
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

// The behaviour with a connection manager (ours, in the instantiation's cell): connections made and dropped through the
// nodes' slots 8 / 9, events up the tree and to the listeners, queries, and the last Release disconnecting everything.
// The record GetEventAttribute copies for an unknown event (0x106b328, this instantiation's) is all zeros in the unstarted
// image, so a wrong record address would read zeros as well: the Events test fills it with a pattern (restored after).
namespace {
struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106b328;
    uint32_t saved[6];
    nGZEvent::cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    MissingRecordScope()
    {
        std::memcpy(saved, GameAddress<void>(kAt), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(kAt)[i] = 0x5ec0de00 + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ec0de00 + i; // ours reads its own record
        }
    }
    ~MissingRecordScope() { std::memcpy(GameAddress<void>(kAt), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};
} // namespace

DIFF_TEST(SG_ResourceNode_Events, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    MissingRecordScope missing;
    static Fake listeners[2];
    sgfx::Manager &manager = w.manager;
    auto build = [&](uint32_t seed, drawfx::Cells &cells) {
        Build(seed, cells);
        sgfx::MakeListener(listeners[0], 500, true);
        sgfx::MakeListener(listeners[1], 501, seed % 2 != 0);
    };
    sgfx::CheckSides(ctx, "events", 48, build, [&](bool original, std::mt19937 &rng) {
        MakeTree(Maker(ctx, original), rng);
        const uint32_t events[] = {cGraphConnChanged::kEventId, 0x1234};
        for (int k = 0; k < 12; ++k) {
            cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
            Fake &l = listeners[rng() % 2];
            const uint32_t event = events[rng() % 2];
            const unsigned op = rng() % 6;
            drawfx::Note("op", op);
            switch (op) {
            case 0: n->Connect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 1: n->Disconnect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 2: {
                cGraphConnChanged body(rng() % 2, n, w.nodes[rng() % w.nodes.count],
                                       rng() % 2 ? sgfx::kIidTree : sgfx::kIidGraph);
                n->NotifyListeners(event, rng(), &body);
                break;
            }
            case 3: drawfx::Note("asked", uint8_t(n->AskListeners(event, rng(), nullptr))); break;
            case 4: drawfx::Note("active", {n->IsActive(), CountOf(n)}); break;
            default: { // the event registry's slots: the count, and a record (sgfx registers both events; 0x5678 is not
                       // registered, so its record is the missing one, 0x106b328)
                drawfx::Note("event count", n->GetEventCount());
                const nGZEvent::cEventAttribute a = n->GetEventAttribute(rng() % 3 ? event : 0x5678u);
                drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
                break;
            }
            }
        }
        NoteNodes("after");
        ReleaseAll();
        drawfx::Note("connected", {uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[0]))),
                                   uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[1])))});
    });
    w.manager.Drop();
}
