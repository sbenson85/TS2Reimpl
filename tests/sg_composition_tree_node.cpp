// Differential tests of nGZSceneGraph::cCompositionTreeNode, the first node pilot (lane NZ, scenegraph N0): the game's
// node (its factory 0xa37910, its vtables 0xe68b2c / 0xe68c28 and the template bodies behind them) against ours (Create,
// our vtables), each side building its own nodes with its own factory. The tests drive whole trees through the slots
// and the +8 persist face and compare the calls the nodes make on the fakes (sgfx.h), the notes, every node's words but
// its two vtable words (the game's vtables on one side, ours on the other: the words at +0 and +8), and the blocks
// freed when the test lets go of the nodes.
#include "difftest.h"
#include "sgfx.h"

#include "sg/cCompositionTreeNode.h"

#include <algorithm>
#include <bit>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kFactory = 0xa37910;
constexpr uint32_t kSize = cCompositionTreeNode::kSize;
constexpr uint32_t kSkipVtables = 0b101; // words 0 and 2
constexpr unsigned kSeeds = 64;

cICompositionTreeNode *__cdecl MakeOurs() { return cCompositionTreeNode::Create(); }
sgfx::Factory Maker(difftest::Context &ctx, bool original)
{
    return original ? ctx.Original<sgfx::Factory>(kFactory) : &MakeOurs;
}

struct World {
    sgfx::Nodes nodes;
    Fake ext[3]; // ids 200-202
    Fake traversal;
    Fake context;
    Fake parent; // id 110: a parent whose transforms are not the identity (sgfx::MakeNode), linked in for a moment
    sgfx::Manager manager; // in the behaviour's cell, as in game (null, the original asks the framework for it)
};
World w;

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gPool.Handle("alpha");
    sgfx::gPool.Handle("beta");
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 2);
    sgfx::MakeTraversal(w.traversal, 450, rng() % 3);
    sgfx::MakeNode(w.parent, 110);
    w.nodes = sgfx::Nodes{};
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cCompositionTreeNodeBehaviorTraits::kEventManagerCell);
}

// A random forest of 1-6 nodes from `make`.
void MakeTree(sgfx::Factory make, std::mt19937 &rng)
{
    const unsigned n = 1 + rng() % 6;
    int parents[sgfx::Nodes::kMax];
    parents[0] = -1;
    for (unsigned i = 1; i < n; ++i) parents[i] = rng() % 5 ? int(rng() % i) : -1;
    w.nodes.Make(make, n, kSize);
    w.nodes.Link(parents);
}

uint32_t CountOf(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }

bool InSubtree(cICompositionTreeNode *root, cICompositionTreeNode *n)
{
    for (; n; n = n->GetParent())
        if (n == root) return true;
    return false;
}

// The nodes under `root` the test does not know yet (a deep clone's) join the held nodes, each named and counted.
void HoldSubtree(cICompositionTreeNode *root)
{
    for (cICompositionTreeNode *c = root->GetFirstChild(); c; c = c->GetNextSibling()) {
        if (drawfx::Norm(P(c)) == P(c) && w.nodes.count < sgfx::Nodes::kMax) {
            drawfx::AddRange(c, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
            c->AddRef();
            w.nodes.at[w.nodes.count++] = c;
        }
        HoldSubtree(c);
    }
}

void NoteTransform(const char *label, const cRigidTransform &t)
{
    drawfx::Call c;
    c.note = label;
    for (float f : t.mValues) c.words.push_back(std::bit_cast<uint32_t>(f));
    drawfx::gLog.push_back(std::move(c));
}

// One random operation on node i through the slots, its results noted.
void Operate(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    cICompositionTreeNode *n = ns[rng() % ns.count];
    const unsigned op = rng() % 14;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface
        const uint32_t iids[] = {0x4512dd13, 0xdb01d4b2, 0x5aef607b, 0x5b0aaa74, 0xbb11a53e, 1,
                                 0x3b253dc7, 0xfb859324, 0x12345678, 0xfb06dba0};
        const uint32_t iid = iids[rng() % 10];
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        const bool r = rng() % 8 ? n->QueryInterface(iid, &out) : n->QueryInterface(iid, nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        if (CountOf(n) != before) n->Release();
        break;
    }
    case 1: { // move a node: removed from its parent, added to another node outside its subtree
        cICompositionTreeNode *c = ns[rng() % ns.count];
        if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
        cICompositionTreeNode *to = ns[rng() % ns.count];
        if (!InSubtree(c, to)) to->AddChild(c);
        break;
    }
    case 2: { // flags
        // Not bit 1: it marks a bounded node, whose slot 69 the tree propagation (0xa39820) calls on a connection
        // change; a plain composition node (62 slots) never carries it.
        uint32_t bit = rng() % 40;
        if ((bit & 31) == 1) bit = 0;
        n->SetFlag(bit, rng() % 2);
        drawfx::Note("flags", {uint8_t(n->TestFlag(bit)), uint8_t(n->TestFlag(rng() % 40)), n->GetFlags()});
        break;
    }
    case 3: { // the tag
        const char *texts[] = {"alpha", "beta", "", "gamma", nullptr};
        if (rng() % 2)
            n->SetTag(texts[rng() % 5]);
        else
            n->SetTagHandle(rng() % 3 ? sgfx::Pool::kFirstHandle + rng() % 2 : sgfx::kNoHandle);
        sgfx::NoteText("tag", n->GetTag());
        drawfx::Note("tag handle", n->GetTagHandle());
        break;
    }
    case 4: { // kinds
        const uint32_t iids[] = {0x4512dd13, 0x5aef607b, 0x3b253dc7, 0xbb11a53e, 0x5b0aaa74, 1, 0, 0xdb01d4b2};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 5: { // children ranges, links
        const cChildRange a = n->GetChildren(), b = n->GetChildrenConst();
        drawfx::Note("children", {drawfx::Norm(P(a.mCurrent)), drawfx::Norm(P(a.mBegin)), drawfx::Norm(P(a.mEnd)),
                                  drawfx::Norm(P(b.mCurrent)), drawfx::Norm(P(b.mBegin)), drawfx::Norm(P(b.mEnd)),
                                  uint8_t(n->HasChildren())});
        drawfx::Note("links", {drawfx::Norm(P(n->GetParentConst())), drawfx::Norm(P(n->GetPrevSiblingConst())),
                               drawfx::Norm(P(n->GetNextSiblingConst())), drawfx::Norm(P(n->GetFirstChildConst()))});
        break;
    }
    case 6: { // transforms
        NoteTransform("local", n->GetLocalTransform());
        NoteTransform("world", n->GetWorldTransform());
        if (rng() % 2) {
            // Composition nodes' world transforms are all the identity: under a fake parent (as under a transform
            // node) the climb to the parent shows. Slot 47's parent link is a plain pointer, restored after.
            cICompositionTreeNode *const old = n->GetParent();
            n->SetLink(cICompositionTreeNode::kParentLink, reinterpret_cast<cICompositionTreeNode *>(&w.parent));
            NoteTransform("world under a fake", n->GetWorldTransform());
            n->SetLink(cICompositionTreeNode::kParentLink, old);
        }
        cRigidTransform in{}, out{};
        for (float &f : in.mValues) f = float(int(rng() % 200) - 100) / 8.0f;
        n->ToParent1c(&out, &in);
        NoteTransform("to parent", out);
        n->FromParent1c(&out, &in);
        NoteTransform("from parent", out);
        cTransform34 in34{}, out34{};
        for (float &f : in34.mValues) f = float(int(rng() % 200) - 100) / 8.0f;
        n->ToParent34(&out34, &in34);
        n->FromParent34(&out34, &out34);
        drawfx::Call c;
        c.note = "34";
        for (float f : out34.mValues) c.words.push_back(std::bit_cast<uint32_t>(f));
        drawfx::gLog.push_back(std::move(c));
        break;
    }
    case 7: // the parent's answers, the limit, the behaviour's
        drawfx::Note("misc", {n->GetFromParent31(), n->GetFromParent32(), n->GetLimit55(), n->IsActive(),
                              n->GetEventCount()});
        break;
    case 8: { // traversals
        auto *t = reinterpret_cast<cICompositionTreeTraversal *>(&w.traversal);
        drawfx::Note("accept", {n->Accept(t), n->AcceptReadOnly(t)});
        break;
    }
    case 9: { // extensions
        Fake &e = w.ext[rng() % 3];
        auto *ext = reinterpret_cast<cIExtension *>(&e);
        if (n->HasExtension(ext)) {
            n->RemoveExtension(ext);
        } else if (!e.value[sgfx::kExtOwnerWord]) {
            n->AddExtension(ext);
        }
        void *out = nullptr;
        const bool found = n->FindExtension(0x7777, &out, nullptr);
        drawfx::Note("extensions", {drawfx::Norm(P(n->GetFirstExtension())), drawfx::Norm(P(n->GetFirstExtensionConst())),
                                    uint8_t(n->HasExtension(ext)), uint8_t(found)});
        break;
    }
    case 10: // content changed
        n->OnContentChanged(rng(), rng(), rng());
        drawfx::Note("flags", n->GetFlags());
        break;
    case 11: { // the +8 face
        drawfx::Note("referent", {drawfx::Norm(P(n->GetReferent())), drawfx::Norm(P(n->GetReferentConst())),
                                  n->GetReferent()->GetOwnerInfo()});
        break;
    }
    case 12: { // events up the tree: a connection change heard by the parent, then the node's listeners
        cICompositionTreeNode *sub = ns[rng() % ns.count];
        cGraphConnChanged body(rng() % 3, n, sub, rng() % 2 ? sgfx::kIidTree : sgfx::kIidGraph);
        n->NotifyListeners(cGraphConnChanged::kEventId, rng(), &body);
        drawfx::Note("asked", uint8_t(n->AskListeners(0x1234, rng(), &body)));
        drawfx::Note("query", uint8_t(n->OnQuery(0x1234, 0, nullptr)));
        break;
    }
    case 13: { // an event to the node itself
        cICompositionTreeNode *sub = ns[rng() % ns.count];
        cGraphConnChanged body(rng() % 3, n, sub, sgfx::kIidTree);
        n->OnEvent(rng() % 2 ? cGraphConnChanged::kEventId : 0x777, 0, &body, 0);
        break;
    }
    }
}

// Releases the test's references, noting the sizes of the blocks freed (the nodes and whatever they held).
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
}

} // namespace

DIFF_TEST(SG_CompositionTreeNode_Factory, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    sgfx::CheckSides(ctx, "factory", 4, Build, [&](bool original, std::mt19937 &) {
        sgfx::Factory make = Maker(ctx, original);
        cICompositionTreeNode *n = make();
        drawfx::Note("count", CountOf(n));
        n->AddRef();
        w.nodes.at[0] = n;
        w.nodes.count = 1;
        w.nodes.size = kSize;
        drawfx::AddRange(n, kSize, sgfx::kNodeTag);
        w.nodes.NoteWords("new node", kSkipVtables);
        w.nodes.NoteShape("new node");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_CompositionTreeNode_Slots, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    sgfx::CheckSides(ctx, "slots", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(Maker(ctx, original), rng);
        w.nodes.NoteShape("built");
        w.nodes.NoteWords("built", kSkipVtables);
        for (int k = 0; k < 16; ++k) Operate(ctx, k, rng);
        w.nodes.NoteShape("after");
        w.nodes.NoteWords("after", kSkipVtables);
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_CompositionTreeNode_Persist, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    sgfx::CheckSides(ctx, "persist", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(Maker(ctx, original), rng);
        cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
        cIGZPersistSerializableReferent *face = nullptr;
        n->QueryInterface(0x5b0aaa74, reinterpret_cast<void **>(&face));
        drawfx::Note("face", drawfx::Norm(P(face)));
        if (rng() % 2) n->SetTag("alpha");
        if (rng() % 2) n->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[0]));
        cPersistTypeInfo info = face->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        // Read into a fresh node of the same side: its own record, the object-graph part, then children (roots of the
        // tree, which have no parent).
        cICompositionTreeNode *fresh = Maker(ctx, original)();
        fresh->AddRef();
        w.nodes.at[w.nodes.count] = fresh;
        drawfx::AddRange(fresh, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
        ++w.nodes.count;
        sgfx::Reader &r = sgfx::gReader;
        r.Version(rng() % 16);
        r.Version(4);
        const uint32_t extensions = rng() % 2;
        r.Word(extensions);
        if (extensions) r.Ref(&w.ext[1]);
        r.Text(rng() % 2 ? "beta" : "");
        uint32_t roots = 0;
        for (unsigned i = 0; i + 1 < w.nodes.count; ++i)
            if (!w.nodes[i]->GetParent() && rng() % 2) ++roots;
        r.Word(roots);
        for (unsigned i = 0, k = 0; i + 1 < w.nodes.count && k < roots; ++i)
            if (!w.nodes[i]->GetParent()) r.Ref(w.nodes[i]), ++k;
        cIGZPersistSerializableReferent *freshFace = nullptr;
        fresh->QueryInterface(0xbb11a53e, reinterpret_cast<void **>(&freshFace));
        freshFace->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        freshFace->Release();
        face->Release();
        w.nodes.NoteShape("after");
        w.nodes.NoteWords("after", kSkipVtables);
        sgfx::gReader.NoteNames();
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_CompositionTreeNode_Clone, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    sgfx::CheckSides(ctx, "clone", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        sgfx::Factory make = Maker(ctx, original);
        MakeTree(make, rng);
        cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
        if (rng() % 2) n->SetTag("beta");
        if (rng() % 2) n->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[0]));
        const uint32_t mode = rng() % 3;
        // A context that answers a node made beforehand, or none (the node makes its clone). The fake answers that node
        // for every source, so it stands in only when no children are cloned: mode 1 asks it for each child's clone too
        // (0xa384f0) and would make the prepared node its own child.
        cINodeCloneContext *context = nullptr;
        if (rng() % 3 == 0 && mode != 1) {
            cICompositionTreeNode *prepared = make();
            prepared->AddRef();
            w.nodes.at[w.nodes.count] = prepared;
            drawfx::AddRange(prepared, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
            ++w.nodes.count;
            sgfx::MakeCloneContext(w.context, prepared);
            context = reinterpret_cast<cINodeCloneContext *>(&w.context);
        }
        const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, 0x12345678};
        void *out = nullptr;
        n->Clone(mode, iids[rng() % 3], &out, context);
        // The clone (counted by Clone's QueryInterface): a new node joins the nodes the test holds (named by its index
        // there before it is noted); the prepared one is held already.
        const bool isNew = out && drawfx::Norm(P(out)) == P(out);
        if (isNew) {
            drawfx::AddRange(out, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
            w.nodes.at[w.nodes.count++] = static_cast<cICompositionTreeNode *>(out);
        }
        drawfx::Note("clone", {out != nullptr, uint8_t(isNew), drawfx::Norm(P(out))});
        if (out && !isNew) static_cast<cICompositionTreeNode *>(out)->Release();
        if (out) HoldSubtree(static_cast<cICompositionTreeNode *>(out));
        w.nodes.NoteShape("after");
        w.nodes.NoteWords("after", kSkipVtables);
        ReleaseAll();
    });
    w.manager.Drop();
}

// The behaviour with a connection manager (ours, in the instantiation's cell 0x10f5f98): connections made and dropped
// through the nodes' slots 8 / 9 (the manager tells the node, whose OnEvent keeps the active bit), events sent up the
// tree and to the listeners, queries, and the last Release disconnecting everything.
DIFF_TEST(SG_CompositionTreeNode_Events, kFactory)
{
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
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
            const unsigned op = rng() % 5;
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
            }
        }
        w.nodes.NoteShape("after");
        w.nodes.NoteWords("after", kSkipVtables);
        ReleaseAll();
        drawfx::Note("connected", {uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[0]))),
                                   uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[1])))});
    });
    w.manager.Drop();
}
