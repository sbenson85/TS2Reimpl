// Differential tests of nGZSceneGraph::cViewer_LEGACY (lane VD, scenegraph N3r; src/sg/cViewer_LEGACY.h): the game's
// legacy viewer (its factory 0xa66010, constructor 0xa64520, vtables 0xe7066c / 0xe706a4 / 0xe70720 / 0xe70740 and the
// bodies behind them) against ours (Create, Construct, our vtables), each side building its own objects. The tests drive
// the objects through all four faces (the cSGResource part +0, the node +0x2c, the persist face +0x34, the virtual base
// +0x110) and compare the calls they make on the fakes (sgfx.h, a material manager in 0x10f8fcc), the notes, every
// object's words but its vtable words and vbptr (words 0, 7, 11, 13, 68: the vbtable's entries are compared instead), the
// padding (the upper half of word 10 after the part's two flags, the view's bytes after its fixed-aspect flag (word 17)
// and its dirty byte (word 66)) and the view's material vector (words 61-63: heap addresses; its indices are noted), the
// name pool's counts, the resource manager's and segments' references, and the blocks freed when the objects go.
//
//   SG_ViewerLegacy_Factory  the factory 0xa66010 (hooked) against Create, the constructor 0xa64520 (hooked) in place
//                            against Construct, the destructor 0xa64860 (hooked) against Destruct, the deleting
//                            destructor 0xa64c00 (hooked) against DeletingDestructor: words, frees, the fakes.
//   SG_ViewerLegacy_Slots    random operations through every face.
//   SG_ViewerLegacy_Read     Read 0xa68a00 through the persist face or the virtual base (its vtordisp thunk 0xa689e3):
//                            records of versions below, at and above 0x11, materials named in the stream's group or
//                            none, the orthographic flag.
//   SG_ViewerLegacy_Events   the behaviour with a connection manager in the cell 0x10f63dc.
//
// The game's bodies these tests run on the original side, with ours (the same slots through our vtables) on the other;
// db/status/cViewer_LEGACY.jsonl has an entry for each.
// The class's own bodies:
//   0xa64860 ~cViewer_LEGACY, 0xa64c00 scalar_deleting_destructor, 0xa68930 QueryInterface, 0xa68f70 Release (-> the
//   template's jump stub 0xa68f30, no entry), 0xa68a00 Read, 0xa69750 GetTypeInfo, 0xa68720 GetRecord18, 0xa68710 GetRecord19
// The template-level bodies of cObjectGraphNodeT<cIObjectGraphNode> and its behaviour (cAbstractBehaviorTypeT<
// cIObjectGraphNode>), reached through our vtables:
//   0xa68780 GetEventCount, 0xa64cb0 GetEventIds (both in _Events), 0xa649d3 the node face's deleting
//   destructor thunk (slot 29)
// Adjustor thunks (ours are the compiler's, reached through the node and persist faces):
//   0xa6898b / 0xa68993 QueryInterface, 0xa68f78 / 0xa68f80 Release
// Vtordisp thunks of the virtual base (ours are the compiler's, reached through the virtual base):
//   0xa6891a QueryInterface, 0xa64c44 AddRef, 0xa68f5d Release, 0xa689e3 Read, 0xa69735 GetTypeInfo, 0xa69a83 Write,
//   0xa69036 GetOwnerInfo, 0xa64c99 GetPersistResource(Const), 0xa69028 GetSubResource, 0xa692ef SubmitKeyToService,
//   0xa686b4 CompareIdentity, 0xa69447 AssignAndSubmit, 0xa693f8 ResetAndSubmit, 0xa69439 OpenAssignAndSubmit,
//   0xa695cd SetSegment, 0xa693c1 SetFlag, 0xa686f6 GetName, 0xa68716 GetRecord18, 0xa68704 GetRecord19, 0xa69052
//   HasKey, 0xa6618d GetSegment, 0xa69044 CopyKeyTo, 0xa686e8 GetFlag, 0xa68d95 ForwardKey
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "sg_viewer_ref_fx.h"
#include "sg_viewer_ref_tape.h"
#include "sgfx.h"

#include "sg/cViewer_LEGACY.h"

#include <bit>
#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;

using Factory = cIObjectGraphNode *(__cdecl *)();
constexpr uint32_t kFactory = 0xa66010;
constexpr uint32_t kConstruct = 0xa64520;
constexpr uint32_t kDestruct = 0xa64860;
constexpr uint32_t kDeleting = 0xa64c00;
constexpr uint32_t kRead = 0xa68a00;
constexpr uint32_t kSize = cViewer_LEGACY::kSize;
constexpr uint32_t kNodeFace = 0x2c, kPersistFace = 0x34, kVBase = 0x110, kVbptr = 0x1c;
constexpr uint32_t kEventCell = 0x10f63dc;
constexpr uint32_t kMaterialCell = 0x10f8fcc; // GetCachedQueriedObject's cache: the material manager
constexpr uint32_t kMissingRecord = 0x106bee0; // this behaviour copy's record for an unknown event
constexpr sgfx::RegistryStatics kObjectGraph{0x10cc9f4, 0x10cc9fc, 0x10cca1c, 0x10cca24};
constexpr unsigned kMax = 6;
constexpr unsigned kSeeds = 64;
// Left out of the word dump (see the header): vtables and vbptr, the view's material vector.
constexpr uint64_t kSkipWords = 1ull << 0 | 1ull << 7 | 1ull << 11 | 1ull << 13 | 7ull << 61;

constexpr auto kMaterialSlots = svx::Sparse<34>({{19, {1, Ret::Int, true, "FindOrLoad", "s"}},
                                                 {20, {1, Ret::Int, true, "AddRefIndex", "u"}},
                                                 {21, {1, Ret::Int, true, "ReleaseIndex", "u"}}});
constexpr drawfx::Iface kMaterials{"MaterialManager", 34, kMaterialSlots.data()};

uint32_t MaterialFind(Fake &, const uint32_t *a)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(a[0]));
    return name[0] == 'x' ? 0xffffffffu : 40u + uint32_t(std::strlen(name));
}

struct World {
    uint8_t *obj[kMax] = {};
    unsigned count = 0;
    Fake ext[3]; // ids 200-202
    Fake context;
    Fake materials;
    sgfx::Manager manager; // in the behaviour's cell 0x10f63dc
};
World w;

cIGZPersistResource *Part(unsigned i) { return reinterpret_cast<cIGZPersistResource *>(w.obj[i]); }
cIObjectGraphNode *Node(unsigned i) { return reinterpret_cast<cIObjectGraphNode *>(w.obj[i] + kNodeFace); }
cIGZPersistSerializableReferent *Persist(unsigned i)
{
    return reinterpret_cast<cIGZPersistSerializableReferent *>(w.obj[i] + kPersistFace);
}
cISGResource *VBase(unsigned i) { return reinterpret_cast<cISGResource *>(w.obj[i] + kVBase); }
uint32_t CountOf(unsigned i) { return reinterpret_cast<const uint32_t *>(w.obj[i])[0x30 / 4]; }
cIGZUnknown *Face(unsigned i, unsigned via)
{
    switch (via) {
    case 0: return Part(i);
    case 1: return Node(i);
    case 2: return Persist(i);
    default: return VBase(i);
    }
}

cIObjectGraphNode *__cdecl MakeOurs() { return cViewer_LEGACY::Create(); }
Factory Maker(difftest::Context &ctx, bool original) { return original ? ctx.Original<Factory>(kFactory) : &MakeOurs; }

// A new object held at the next index (registered as a range, counted by the test).
void Hold(uint8_t *base)
{
    drawfx::AddRange(base, kSize, sgfx::kNodeTag | (w.count << 12));
    w.obj[w.count++] = base;
    Node(w.count - 1)->AddRef();
}
void HoldNew(Factory make) { Hold(reinterpret_cast<uint8_t *>(make()) - kNodeFace); }

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gPool.Handle("alpha");
    sgfx::gPool.Handle("cam_view");
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::gWriter.fake.Returns(23, 0x2300u + seed);
    sgfx::gResources.Reset(cells);
    sgfx::gResources.manager.Returns(9, seed % 3);
    sgfx::gResources.manager.Returns(14, (seed + 1) % 3);
    for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 2);
    w.materials.Reset<kMaterials>(707);
    w.materials.On(19, &MaterialFind);
    cells.Set(kMaterialCell, static_cast<const void *>(&w.materials));
    w.count = 0;
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, kEventCell);
    vtape::Install();
}

// Each object's words (kSkipWords left out, the padding masked), its vbtable's entries, its view's materials, and what
// its slots say of it.
void NoteObjects(const char *label)
{
    for (unsigned i = 0; i < w.count; ++i) {
        const uint32_t *words = reinterpret_cast<const uint32_t *>(w.obj[i]);
        drawfx::Call c;
        c.note = label;
        for (unsigned k = 0; k < kSize / 4; ++k) {
            if (k < 64 && ((kSkipWords >> k) & 1)) continue;
            if (k == 68) continue; // the virtual base's vtable
            uint32_t v = words[k];
            if (k == 10) v &= 0xffff; // the part's two flags
            if (k == 17 || k == 66) v &= 0xff;
            c.words.push_back(drawfx::Norm(v));
        }
        drawfx::gLog.push_back(std::move(c));
        const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(w.obj[i] + kVbptr);
        const cViewerRefEntry &view = reinterpret_cast<const cViewer_LEGACY *>(w.obj[i])->mView;
        drawfx::Call m;
        m.note = "materials";
        m.words = {i, uint32_t(vbtable[0]), uint32_t(vbtable[1]), uint32_t(view.mMaterials.size())};
        for (const auto &h : view.mMaterials) m.words.push_back(uint32_t(h.mIndex));
        drawfx::gLog.push_back(std::move(m));
        cIObjectGraphNode *n = Node(i);
        sgfx::NoteText("tag", n->GetTag());
        drawfx::Note("node", {n->GetTagHandle(), drawfx::Norm(P(n->GetFirstExtension())), n->IsActive(), CountOf(i)});
    }
}

// Releases the test's references, noting the sizes of the blocks freed and what the objects held.
void ReleaseAll()
{
    difftest::FreeRecorder freed;
    for (unsigned i = w.count; i-- > 0;) Node(i)->Release();
    w.count = 0;
    freed.Stop();
    size_t sizes[64];
    const size_t n = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    for (const Fake &e : w.ext)
        drawfx::Note("extension", {e.id, uint32_t(e.refs), drawfx::Norm(e.value[0]), drawfx::Norm(e.value[1])});
    drawfx::Note("material refs", uint32_t(w.materials.refs));
    sgfx::gPool.NoteCounts();
    sgfx::gResources.NoteRefs();
}

cIGZUnknown *Segment(std::mt19937 &rng)
{
    const unsigned k = rng() % 4;
    return k < 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[k]) : nullptr;
}

float RandomFloat(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return std::bit_cast<float>(uint32_t(rng())); // any bits (NaNs, denormals)
    case 1: return 0.0f;
    default: return float(int(rng() % 2001) - 1000) / 64.0f;
    }
}

using vtape::BoolWord; // (a word GetBool reads: sg_viewer_ref_tape.h)

// Scopes every test needs: the object-graph registry (0x264010, both the image's function and ours on one planted map)
// and a pattern in the missing records (the image's 0x106bee0 is all zeros unstarted, so a wrong address would read
// zeros as well).
struct Scopes {
    sgfx::RegistryScope registry{kObjectGraph};
    shape::StandIn stand{0x264010, &cObjectGraphBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kObjectGraph.map>};
    uint32_t saved[6];
    nGZEvent::cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    Scopes()
    {
        std::memcpy(saved, GameAddress<void>(kMissingRecord), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(kMissingRecord)[i] = 0x5ec0de00 + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ec0de00 + i;
        }
    }
    ~Scopes()
    {
        std::memcpy(GameAddress<void>(kMissingRecord), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};

// One random operation on a random object through one of its faces, its results noted.
void Operate(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    const unsigned i = rng() % w.count;
    cIObjectGraphNode *n = Node(i);
    cISGResource *face = VBase(i);
    const unsigned op = rng() % 15;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface through any face
        const uint32_t iids[] = {cViewer_LEGACY::kIid, cISGResource::kIid, 0x9b11bf70, 0x5b0aaa74, 0xbb11a53e,
                                 sgfx::kIidTree, 0xdb01d4b2, sgfx::kIidGraph, 1, 0x3b253dc7, 0xfb859324,
                                 cGZPersistResource::kIid, 0x12345678, cViewer_LEGACY::kClassId};
        const uint32_t iid = iids[rng() % 14];
        const unsigned via = rng() % 4;
        void *out = nullptr;
        const uint32_t before = CountOf(i);
        const bool r = rng() % 8 ? Face(i, via)->QueryInterface(iid, &out) : Face(i, via)->QueryInterface(iid, nullptr);
        drawfx::Note("query", {via, iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(i) - before});
        if (out && CountOf(i) != before) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: { // AddRef / Release through any face, the count through the part and the virtual base
        const unsigned via = rng() % 4;
        const uint32_t a = Face(i, via)->AddRef();
        const uint32_t c = Part(i)->GetRefCount();
        const uint32_t r = Face(i, via)->Release();
        drawfx::Note("counts", {via, a, c, r, face->GetPersistResource()->GetRefCount()});
        break;
    }
    case 2: { // the tag
        const char *texts[] = {"alpha", "cam_view", "", "gamma", nullptr};
        if (rng() % 4)
            n->SetTag(texts[rng() % 5]);
        else
            n->SetTagHandle(rng() % 2 ? n->GetTagHandle() : 0);
        sgfx::NoteText("tag", n->GetTag());
        drawfx::Note("tag handle", n->GetTagHandle());
        break;
    }
    case 3: { // kinds
        const uint32_t iids[] = {cViewer_LEGACY::kIid, cISGResource::kIid, 0x9b11bf70, sgfx::kIidTree, sgfx::kIidGraph,
                                 0x3b253dc7, 0x5b0aaa74, 1, 0, 0xdb01d4b2, cViewer_LEGACY::kClassId};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 4: { // extensions
        Fake &e = w.ext[rng() % 3];
        auto *ext = reinterpret_cast<cIExtension *>(&e);
        if (n->HasExtension(ext))
            n->RemoveExtension(ext);
        else if (!e.value[sgfx::kExtOwnerWord])
            n->AddExtension(ext);
        void *found = nullptr;
        const bool r = n->FindExtension(rng() % 2 ? 0x1234u : 1u, &found, nullptr);
        drawfx::Note("extensions", {drawfx::Norm(P(n->GetFirstExtension())), drawfx::Norm(P(n->GetFirstExtensionConst())),
                                    uint8_t(n->HasExtension(ext)), uint8_t(r), drawfx::Norm(P(found))});
        if (found) static_cast<cIGZUnknown *>(found)->Release();
        break;
    }
    case 5: { // the virtual base: segment, flag, the three assignments, name, key
        switch (rng() % 5) {
        case 0: drawfx::Note("set segment", uint8_t(face->SetSegment(Segment(rng)))); break;
        case 1: face->SetFlag(uint8_t(rng())); break;
        case 2: drawfx::Note("assign", face->AssignAndSubmit(rng() % 2 ? "cam_view" : "beta", Segment(rng))); break;
        case 3: {
            const char *name = rng() % 2 ? "cam_view" : "beta";
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
    case 6: { // the virtual base's other slots
        sgfx::NoteText("record 18", static_cast<const char *>(face->GetRecord18()));
        sgfx::NoteText("record 19", static_cast<const char *>(face->GetRecord19()));
        cISGResource *other = VBase(rng() % w.count);
        drawfx::Note("misc", {drawfx::Norm(P(face->GetSubResource(rng() % 2))), drawfx::Norm(P(face->GetPersistResourceConst())),
                              uint32_t(face->CompareIdentity(other)), face->SubmitKeyToService()});
        break;
    }
    case 7: { // the part's key slots
        cIGZPersistResource *part = Part(i);
        part->SetGroup(rng());
        part->SetInstance(rng() % 2 ? rng() : 0);
        if (rng() % 4 == 0) part->SetType(rng());
        const cGZPersistResourceKey key = part->GetKey();
        drawfx::Note("key", {key.type, key.group, key.instance, key.resource, part->GetType(), part->GetGroup(),
                             part->GetInstance()});
        break;
    }
    case 8: // the owner, through both persist faces
        drawfx::Note("owner info", {drawfx::Norm(Persist(i)->GetOwnerInfo()), drawfx::Norm(face->GetOwnerInfo())});
        break;
    case 9: { // a type-info record, through either persist face
        cPersistTypeInfo info = rng() % 2 ? Persist(i)->GetTypeInfo() : face->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 10: // written (nothing is), through either persist face
        if (rng() % 2)
            Persist(i)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        vtape::gTape.NotePosition("written");
        break;
    case 11: { // a clone (nothing: *out untouched)
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a0000u + k));
        sgfx::MakeCloneContext(w.context, nullptr);
        n->Clone(rng() % 3, rng() % 2 ? sgfx::kIidGraph : cViewer_LEGACY::kIid, &out,
                 rng() % 2 ? reinterpret_cast<cINodeCloneContext *>(&w.context) : nullptr);
        drawfx::Note("clone", P(out));
        break;
    }
    case 12: // the referents
        drawfx::Note("referents", {drawfx::Norm(P(n->GetReferent())), drawfx::Norm(P(n->GetReferentConst()))});
        break;
    case 13: { // events to the object and up to its listeners (none here)
        cGraphConnChanged body(rng() % 3, n, Node(rng() % w.count), sgfx::kIidGraph);
        switch (rng() % 3) {
        case 0: n->NotifyListeners(cGraphConnChanged::kEventId, rng(), &body); break;
        case 1: drawfx::Note("asked", uint8_t(n->AskListeners(0x1234, rng(), &body))); break;
        default: n->OnEvent(rng() % 2 ? cGraphConnChanged::kEventId : 0x777, 0, &body, 0); break;
        }
        break;
    }
    default: { // the behaviour's no-op slots and the query slot
        n->SetAttribute(rng(), rng(), rng());
        n->GetAttribute(rng(), rng(), rng());
        drawfx::Note("query event", uint8_t(n->OnQuery(rng() % 2 ? cGraphConnChanged::kEventId : 0x1234, rng(), nullptr)));
        break;
    }
    }
}

// A record for Read: the type-info version, the cSGResource part, the node part (extensions, the tag), before 0x11 the
// seven dropped words, the view's materials and fields, the flag, the orthographic box.
void TapeRecord(uint32_t version, std::mt19937 &rng)
{
    sgfx::Reader &r = sgfx::gReader;
    vtape::Tape &t = vtape::gTape;
    r.Version(version);
    r.Version(rng() % 3); // cSGResource's record
    t.Text(rng() % 2 ? "cam_view" : "");
    r.Version(4); // the object-graph part's
    const uint32_t extensions = rng() % 3 == 0;
    t.Word(extensions);
    if (extensions) t.Ref(&w.ext[1]);
    t.Text(rng() % 2 ? "alpha" : "");
    if (version < cViewer_LEGACY::kVersion) { // the words the old records hold
        t.Word(rng());
        for (int i = 0; i < 5; ++i) t.Float(RandomFloat(rng));
        t.Word(rng());
    }
    const uint32_t materials = rng() % 4;
    t.Word(materials);
    static const char *const kNames[] = {"mat_a", "*grp!mat_b", "x_missing", "", "##0x1c0532fa!mat_c", "lambert"};
    for (uint32_t m = 0; m < materials; ++m) {
        const uint32_t present = BoolWord(rng);
        t.Word(present);
        if (present & 0xff) t.Text(kNames[rng() % 6]);
    }
    for (int i = 0; i < 16; ++i) t.Float(RandomFloat(rng));
    t.Word(BoolWord(rng));
    for (int i = 0; i < 4; ++i) t.Float(RandomFloat(rng));
    if (rng() % 8 == 0) t.Word(0x5a5a5a5a); // more than the record holds
}

} // namespace

// The factory, the constructor in place, the destructor and the deleting destructor (all four hooked), on objects that
// hold a tag, an extension and a name.
DIFF_TEST(SG_ViewerLegacy_Factory, kFactory)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "factory", 32, Build, [&](bool original, std::mt19937 &rng) {
        const unsigned variant = rng() % 4;
        drawfx::Note("variant", variant);
        uint8_t *base;
        if (variant == 1) { // the constructor in place (the most-derived flag 1, as the factory passes it)
            void *mem = ::operator new(kSize);
            std::memset(mem, 0xcd, kSize);
            void *r = original ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kConstruct)(mem, 1)
                               : cViewer_LEGACY::Construct(static_cast<cViewer_LEGACY *>(mem), nullptr, 1);
            drawfx::Note("constructed", uint8_t(r == mem));
            base = static_cast<uint8_t *>(mem);
        } else {
            base = reinterpret_cast<uint8_t *>(Maker(ctx, original)()) - kNodeFace;
        }
        drawfx::Note("count", reinterpret_cast<const uint32_t *>(base)[0x30 / 4]);
        Hold(base);
        NoteObjects("new");
        if (rng() % 2) Node(0)->SetTag("alpha");
        if (rng() % 2) Node(0)->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[0]));
        if (rng() % 2) VBase(0)->AssignAndSubmit("cam_view", Segment(rng));
        NoteObjects("held");
        if (variant < 2) {
            ReleaseAll();
            return;
        }
        // The destructors called directly (the test's count left in the object: the destructors do not read it).
        w.count = 0;
        difftest::FreeRecorder freed;
        if (variant == 2) {
            if (original)
                ctx.Original<void(__thiscall *)(void *)>(kDestruct)(base);
            else
                cViewer_LEGACY::Destruct(reinterpret_cast<cViewer_LEGACY *>(base), nullptr);
            ::operator delete(base);
        } else {
            const uint32_t flags = rng() % 2;
            void *r = original ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(base, flags)
                               : cViewer_LEGACY::DeletingDestructor(reinterpret_cast<cViewer_LEGACY *>(base), nullptr, flags);
            drawfx::Note("deleted", {flags, uint8_t(r == base)});
            if (!(flags & 1)) ::operator delete(base);
        }
        freed.Stop();
        size_t sizes[16];
        const size_t n = freed.SortedSizes(sizes, 16);
        drawfx::Call c;
        c.note = "freed";
        for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
        drawfx::gLog.push_back(std::move(c));
        for (const Fake &e : w.ext) drawfx::Note("extension", {e.id, uint32_t(e.refs), drawfx::Norm(e.value[0])});
        sgfx::gPool.NoteCounts();
        sgfx::gResources.NoteRefs();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ViewerLegacy_Slots, kFactory)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "slots", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        const Factory make = Maker(ctx, original);
        const unsigned n = 1 + rng() % 4;
        for (unsigned i = 0; i < n; ++i) HoldNew(make);
        NoteObjects("built");
        for (int k = 0; k < 24; ++k) Operate(ctx, k, rng);
        NoteObjects("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_ViewerLegacy_Read, kRead)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "read", 128, Build, [&](bool original, std::mt19937 &rng) {
        HoldNew(Maker(ctx, original));
        if (rng() % 3 == 0) Node(0)->SetTag("cam_view");
        static const uint32_t kGroups[] = {0, 0x1c0532fau, 0x7f00a5c3u};
        sgfx::gReader.fake.value[0] = kGroups[rng() % 3];
        const unsigned reads = 1 + rng() % 2; // a second read over the first one's materials
        for (unsigned k = 0; k < reads; ++k) {
            vtape::gTape.Clear();
            const uint32_t version = rng() % 2 ? cViewer_LEGACY::kVersion : rng() % 3 ? rng() % 0x11 : 0x12;
            TapeRecord(version, rng);
            const bool viaBase = rng() % 2;
            drawfx::Note("read", {version, uint8_t(viaBase)});
            auto *reader = reinterpret_cast<ifaces::cIGZPersistReader *>(&sgfx::gReader.fake);
            if (viaBase)
                VBase(0)->Read(reader);
            else
                Persist(0)->Read(reader);
            vtape::gTape.NotePosition("tape read");
            drawfx::Note("reader", {sgfx::gReader.versionAt});
            NoteObjects("read");
        }
        sgfx::gReader.NoteNames();
        ReleaseAll();
    });
    w.manager.Drop();
}

// The behaviour with a connection manager (ours, in the cell): connections made and dropped through slots 8 / 9, events
// to the listeners, queries, the registry's slots 12-14, and the last Release disconnecting everything.
DIFF_TEST(SG_ViewerLegacy_Events, 0xa68780)
{
    Scopes scopes;
    static Fake listeners[2];
    sgfx::Manager &manager = w.manager;
    auto build = [&](uint32_t seed, drawfx::Cells &cells) {
        Build(seed, cells);
        sgfx::MakeListener(listeners[0], 500, true);
        sgfx::MakeListener(listeners[1], 501, seed % 2 != 0);
    };
    sgfx::CheckSides(ctx, "events", 48, build, [&](bool original, std::mt19937 &rng) {
        const Factory make = Maker(ctx, original);
        const unsigned count = 1 + rng() % 3;
        for (unsigned i = 0; i < count; ++i) HoldNew(make);
        const uint32_t events[] = {cGraphConnChanged::kEventId, 0x1234, sgfx::kRegistryIds[1]};
        for (int k = 0; k < 12; ++k) {
            const unsigned i = rng() % w.count;
            cIObjectGraphNode *n = Node(i);
            Fake &l = listeners[rng() % 2];
            const uint32_t event = events[rng() % 3];
            const unsigned op = rng() % 7;
            drawfx::Note("op", op);
            switch (op) {
            case 0: n->Connect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 1: n->Disconnect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 2: {
                cGraphConnChanged body(rng() % 2, n, Node(rng() % w.count), sgfx::kIidGraph);
                n->NotifyListeners(event, rng(), &body);
                break;
            }
            case 3: drawfx::Note("asked", uint8_t(n->AskListeners(event, rng(), nullptr))); break;
            case 4: // (the part's count masks the active bit the connections set)
                drawfx::Note("active", {n->IsActive(), CountOf(i), Part(i)->GetRefCount()});
                break;
            case 5: { // the registry's ids (slot 13)
                drawfx::Note("event count", n->GetEventCount());
                nGZEvent::cAttributeIDRange range = n->GetEventIds();
                unsigned steps = 0;
                while (steps < 12 && !range.mCurrent->IsEqual(range.mEnd)) {
                    drawfx::Note("id", range.mCurrent->Get());
                    range.mCurrent->Next();
                    ++steps;
                }
                nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
                break;
            }
            default: { // a record (0x5678 is not registered: the missing one)
                const nGZEvent::cEventAttribute a = n->GetEventAttribute(rng() % 3 ? event : 0x5678u);
                drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
                break;
            }
            }
        }
        NoteObjects("after");
        ReleaseAll();
        drawfx::Note("connected", {uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[0]))),
                                   uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[1])))});
    });
    w.manager.Drop();
}
