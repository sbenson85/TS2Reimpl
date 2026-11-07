// Differential tests of nGZSceneGraph::cSGResource as a whole class (lane OE, scenegraph N1; src/sg/cSGResource.h).
//
// cSGResource is abstract: the game only ever builds it as a base part, its constructor 0xa3c270 called by a derived
// constructor with the most-derived flag 0 after that constructor set the vbptr. Every test builds it that way on both
// sides: a buffer whose vbptr points at a vbtable {-0x1c, vbase - 0x1c} (the virtual base where cSGResource alone puts
// it, +0x30, or further out as a derived class puts it), then the game's constructor or ours (the hook target, the
// compiler's own constructor: SGResourceBasePartConstructor).
//
//   SGResource_BasePart   the constructor and destructor alone, at four placements of the virtual base: every word the
//                         two write (vtables compared as the face they are: the game's 0xe695a4 / 0xe695dc / 0xe603ac
//                         against ours), the vbptr left alone, the vtordisp, the releases; half the seeds destroy
//                         through the deleting destructor (0xa3c380 / our primary slot 12) with flag 0.
//   SGResource_Slots      the vtable route: random calls through both faces (the primary cIGZPersistResource and the
//                         virtual base cISGResource, slots 0-24 but the derived class's 18 / 19), on the game's object
//                         (its vtables and bodies) and on ours (our vtables, our overrides and the hook-shaped bodies
//                         they call), with the name pool, the reader, the writer and the resource manager as fakes.
//   SGResource_GameDerived the decision on a game-derived class: cResourceNode's constructor 0xa4fd10 (flag 1) and
//                         destructor 0xa4fe50 run unpatched on one side and with 0xa3c270 / 0xa3c2f0 patched to our
//                         constructor / destructor adapter on the other (what the hooks do in game), and the adapters
//                         of 0xa3c4d0 / 0xa3c540 / 0xa3ced0 called on that object as the derived classes call them
//                         (QueryInterface also through cResourceNode's own QueryInterface 0xa50620, patched).
#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"
#include "shape.h"

#include "sg/cSGResource.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kCtor = 0xa3c270, kDtor = 0xa3c2f0, kDeleting = 0xa3c380, kQuery = 0xa3c4d0, kRead = 0xa3c540, kWrite = 0xa3ced0;
constexpr uint32_t kGamePrimary = 0xe695a4, kGameSecondary = 0xe695dc, kGameBase = 0xe603ac; // cGZPersistResource's
constexpr uint32_t kObjectTag = 0x7b000000;
// GetTypeInfo 0xa3ce10 (unhooked, in effect through callers) is reached through the virtual base's slot 4 (Operate op 3)
// and from Write (op 4, and GameDerived's write).

// The game's virtual-base vtable (0xe695dc): its vtordisp thunks, slot by slot (slots 18 to 21 are the derived class's).
// SGResource_Slots calls every one of these slots through the game's vtable on one side and through ours (whose slots
// are our compiler's own thunks, verified unhooked in their place) on the other; it first checks this table against
// the game's vtable, so the status entries name the thunks the test really reaches.
struct GameThunk {
    uint32_t slot, rva;
};
constexpr GameThunk kGameThunks[] = {
    {0, 0xa3c4b8},  {1, 0xa3c3a0},  {2, 0xa3c706},  {3, 0xa3c534},  {4, 0xa3ce01},  {5, 0xa3cebe},  {6, 0xa3c741},
    {7, 0xa3c3ab},  {8, 0xa3c3ab},  {9, 0xa3c711},  {10, 0xa3c784}, {11, 0xa3c3c4}, {12, 0xa3cbe5}, {13, 0xa3c87a},
    {14, 0xa3c9f2}, {15, 0xa3cdc8}, {16, 0xa3c860}, {17, 0xa3c4a4}, {22, 0xa3c764}, {23, 0xa3c48f}, {24, 0xa3c6c2},
};

using CtorFn = void *(__thiscall *)(void *, uint32_t);
using DtorFn = void(__thiscall *)(void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using ReadFn = void(__thiscall *)(void *, void *);
using WriteFn = uint32_t(__thiscall *)(void *, void *);

// ---- our vtables ----
// Read off a concrete test class while it is built: the Snapshot base is constructed after the cSGResource part and
// before the test class stores its own vtables, so it sees what our cSGResource constructor stored. The +0 word after
// the test class is destroyed is our cGZPersistResource's vtable.
struct OurVTables {
    uint32_t primary, secondary, base;
};
uint8_t *gBuilding;
OurVTables gOurs;
struct Snapshot {
    Snapshot()
    {
        gOurs.primary = shape::Word(gBuilding, 0);
        const int32_t *vbtable = reinterpret_cast<const int32_t *>(uintptr_t(shape::Word(gBuilding, 0x1c)));
        gOurs.secondary = shape::Word(gBuilding, 0x1c + vbtable[1]);
    }
};
class TestResource final : public cSGResource, private Snapshot {
public:
    const void *GetRecord18() override { return nullptr; }
    const void *GetRecord19() override { return nullptr; }
};
const OurVTables &Ours()
{
    static bool done = false;
    if (!done) {
        alignas(8) static uint8_t buffer[sizeof(TestResource)];
        gBuilding = buffer;
        TestResource *t = ::new (buffer) TestResource;
        t->~TestResource();
        gOurs.base = shape::Word(buffer, 0);
        done = true;
    }
    return gOurs;
}

// A vtable word as the face it is, on its side: the game's object must carry the game's vtables and ours ours.
uint32_t Code(uint32_t w, bool original)
{
    const OurVTables &o = Ours();
    const uint32_t primary = original ? P(GameAddress<void>(kGamePrimary)) : o.primary;
    const uint32_t secondary = original ? P(GameAddress<void>(kGameSecondary)) : o.secondary;
    const uint32_t base = original ? P(GameAddress<void>(kGameBase)) : o.base;
    if (w == primary) return 0xd0000000u;
    if (w == secondary) return 0xd0000001u;
    if (w == base) return 0xd0000002u;
    return w;
}

// ---- the object, built as a derived constructor builds its base part ----
struct Object {
    alignas(8) uint8_t bytes[0xa0];
    int32_t vbtable[2];
    uint32_t vbase;
};
Object gObject;

CtorFn Constructor(difftest::Context &ctx, bool original)
{
    return original ? ctx.Original<CtorFn>(kCtor) : reinterpret_cast<CtorFn>(const_cast<void *>(SGResourceBasePartConstructor()));
}

void Build(difftest::Context &ctx, bool original, uint32_t vbase)
{
    std::memset(gObject.bytes, 0xcd, sizeof(gObject.bytes));
    gObject.vbtable[0] = -0x1c;
    gObject.vbtable[1] = int32_t(vbase - 0x1c);
    gObject.vbase = vbase;
    const uint32_t vbptr = P(gObject.vbtable);
    std::memcpy(gObject.bytes + 0x1c, &vbptr, 4);
    drawfx::AddRange(gObject.bytes, sizeof(gObject.bytes), kObjectTag);
    void *const r = Constructor(ctx, original)(gObject.bytes, 0);
    drawfx::Note("constructor returns", drawfx::Norm(P(r)));
}

// The destructor as the game's derived destructors call it, or (deleting) the primary vtable's slot 12 with flag 0 (no
// delete), what an explicit virtual destructor call makes: the game's 0xa3c380 against our compiler's.
void Destroy(difftest::Context &ctx, bool original, bool deleting = false)
{
    if (deleting && original)
        ctx.Original<void(__thiscall *)(void *, uint32_t)>(kDeleting)(gObject.bytes, 0);
    else if (deleting)
        reinterpret_cast<cSGResource *>(gObject.bytes)->~cSGResource(); // virtual: slot 12, flag 0
    else if (original)
        ctx.Original<DtorFn>(kDtor)(gObject.bytes);
    else
        cSGResourceAdapters::Destruct(reinterpret_cast<cSGResource *>(gObject.bytes), nullptr);
}

// The object's words through the virtual base's vtable and two words past it (left 0xcd): vtables coded, the vbptr as
// whether it is still the derived class's, pointers normalised.
void NoteObject(const char *label, bool original)
{
    drawfx::Call c;
    c.note = label;
    const unsigned words = (gObject.vbase + 4) / 4 + 2;
    for (unsigned i = 0; i < words; ++i) {
        const uint32_t w = shape::Word(gObject.bytes, i * 4);
        c.words.push_back(i == 7 ? uint32_t(w == P(gObject.vbtable)) : drawfx::Norm(Code(w, original)));
    }
    drawfx::gLog.push_back(std::move(c));
}

cIGZPersistResource *Primary() { return reinterpret_cast<cIGZPersistResource *>(gObject.bytes); }
cISGResource *Face() { return reinterpret_cast<cISGResource *>(gObject.bytes + gObject.vbase); }

// ---- the world ----
struct World {
    Fake twin;     // id 480: answers cISGResource with itself
    Fake stranger; // id 481: answers nothing
    uint32_t seed = 0;
};
World w;

// The writer's slot 28 (WriteTypeInfo) as sgfx's, plus a reference held on the record's name: after the call the
// writer drops it and notes what is left, 0 when the caller released its own (Write releases the name it made).
cIGZUnknown *gHeldTypeName;
uint32_t HoldTypeName(Fake &, const uint32_t *a)
{
    const auto *info = reinterpret_cast<const cPersistTypeInfo *>(uintptr_t(a[0]));
    sgfx::NoteTypeInfo("written type", info);
    if (info && info->mName) {
        gHeldTypeName = reinterpret_cast<cIGZUnknown *>(info->mName);
        gHeldTypeName->AddRef();
    }
    return 1;
}
void NoteHeldTypeName()
{
    if (cIGZUnknown *const name = gHeldTypeName) {
        gHeldTypeName = nullptr;
        drawfx::Note("type name refs left", name->Release());
    }
}

void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    w.seed = seed;
    sgfx::gPool.Reset(cells);
    sgfx::gPool.Handle("lamp");
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::gWriter.fake.Returns(23, 0x2300u + seed); // what Write (0xa3ced0) hands back
    sgfx::gWriter.fake.On(28, &HoldTypeName);
    gHeldTypeName = nullptr;
    sgfx::gResources.Reset(cells);
    sgfx::gResources.manager.Returns(9, seed % 3);
    sgfx::gResources.manager.Returns(14, (seed + 1) % 3);
    w.twin.Reset<sgfx::kUnknown>(480);
    w.twin.AddFace(cISGResource::kIid, w.twin);
    w.stranger.Reset<sgfx::kUnknown>(481);
}

const char *const kNames[] = {"lamp", "##0x1c050000!floor_txmt", "", "beta_cres", "##0x00000001!0x12345678"};
const char *Name(std::mt19937 &rng) { return kNames[rng() % 5]; }
cIGZUnknown *Segment(std::mt19937 &rng)
{
    const unsigned k = rng() % 4;
    return k < 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[k]) : nullptr;
}
uint32_t Count() { return shape::Word(gObject.bytes, 4); }

void NoteState(const char *label, bool original)
{
    NoteObject(label, original);
    sgfx::gResources.NoteRefs();
    sgfx::gPool.NoteCounts();
    drawfx::Note("twin refs", uint32_t(w.twin.refs));
}

// One random call through a face, its results noted.
void Operate(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    cISGResource *face = Face();
    cIGZPersistResource *primary = Primary();
    const unsigned op = rng() % 24;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface, either face
        const uint32_t iids[] = {cISGResource::kIid, 0x5b0aaa74, 0x9b11bf70, 0xbb11a53e, 1, 0x456b8f1d, 0x12345678};
        const uint32_t iid = iids[rng() % 7];
        void *out = nullptr;
        const uint32_t before = Count();
        cIGZUnknown *on = rng() % 2 ? static_cast<cIGZUnknown *>(face) : static_cast<cIGZUnknown *>(primary);
        const bool r = rng() % 8 ? on->QueryInterface(iid, &out) : on->QueryInterface(iid, nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out)), Count() - before});
        if (out) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: { // AddRef / Release, either face
        cIGZUnknown *on = rng() % 2 ? static_cast<cIGZUnknown *>(face) : static_cast<cIGZUnknown *>(primary);
        const uint32_t a = on->AddRef();
        const uint32_t r = on->Release();
        drawfx::Note("counts", {a, r});
        break;
    }
    case 2: { // Read
        sgfx::gReader.Version(rng() % 4);
        sgfx::gReader.Text(Name(rng));
        face->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&sgfx::gReader.fake));
        break;
    }
    case 3: { // GetTypeInfo
        cPersistTypeInfo info = face->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 4:
        face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        NoteHeldTypeName();
        break;
    case 5: drawfx::Note("owner info", drawfx::Norm(face->GetOwnerInfo())); break;
    case 6:
        drawfx::Note("persist resource", drawfx::Norm(P(rng() % 2 ? face->GetPersistResource() : face->GetPersistResourceConst())));
        break;
    case 7: {
        const uint32_t index = rng() % 3 ? 0 : 1 + rng() % 3;
        drawfx::Note("sub resource", {index, drawfx::Norm(P(face->GetSubResource(index)))});
        break;
    }
    case 8: drawfx::Note("submit", face->SubmitKeyToService()); break;
    case 9: {
        const unsigned which = rng() % 3;
        cIGZUnknown *other = which == 0   ? static_cast<cIGZUnknown *>(face)
                             : which == 1 ? reinterpret_cast<cIGZUnknown *>(&w.twin)
                                          : reinterpret_cast<cIGZUnknown *>(&w.stranger);
        drawfx::Note("identity", {which, uint32_t(face->CompareIdentity(other))});
        break;
    }
    case 10: {
        const char *name = Name(rng);
        cIGZUnknown *segment = Segment(rng);
        drawfx::Note("assign", face->AssignAndSubmit(name, segment));
        break;
    }
    case 11: {
        const uint32_t group = rng(), instance = rng();
        drawfx::Note("reset", face->ResetAndSubmit(group, instance));
        break;
    }
    case 12: {
        const char *name = Name(rng);
        const uint32_t group = rng();
        cIGZUnknown *segment = Segment(rng);
        drawfx::Note("open assign", face->OpenAssignAndSubmit(name, group, segment));
        break;
    }
    case 13: drawfx::Note("set segment", uint8_t(face->SetSegment(Segment(rng)))); break;
    case 14: face->SetFlag(uint8_t(rng())); break;
    case 15: sgfx::NoteText("name", face->GetName()); break;
    case 16: drawfx::Note("segment", drawfx::Norm(P(face->GetSegment()))); break;
    case 17: drawfx::Note("has key", face->HasKey()); break;
    case 18: {
        cGZPersistResourceKey key{};
        cGZPersistResourceKey *r = face->CopyKeyTo(&key);
        drawfx::Note("key copy", {uint32_t(r == &key), key.type, key.group, key.instance, key.resource});
        break;
    }
    case 19: drawfx::Note("flag", face->GetFlag()); break;
    case 20: drawfx::Note("forward key", face->ForwardKey()); break;
    case 21: { // the key through the primary face
        const uint32_t v = rng() % 4 ? rng() : 0;
        switch (rng() % 3) {
        case 0: primary->SetType(v); break;
        case 1: primary->SetGroup(v); break;
        default: primary->SetInstance(v); break;
        }
        const cGZPersistResourceKey key = primary->GetKey();
        drawfx::Note("key", {key.type, key.group, key.instance, key.resource});
        break;
    }
    case 22: drawfx::Note("ref count", {face->GetPersistResource()->GetRefCount(), primary->GetRefCount()}); break;
    default: { // the name and flag words, raw
        drawfx::Note("words", {shape::Word(gObject.bytes, 0x20), shape::Word(gObject.bytes, 0x28)});
        break;
    }
    }
}

void CheckGameThunks(difftest::Context &ctx)
{
    const uint32_t *game = GameAddress<uint32_t>(kGameSecondary);
    const uint32_t *ours = reinterpret_cast<const uint32_t *>(uintptr_t(Ours().secondary));
    for (const GameThunk &t : kGameThunks) {
        if (game[t.slot] != P(GameAddress<void>(t.rva)))
            ctx.Fail("game virtual-base slot %u is not the thunk 0x%x the status entries name", t.slot, t.rva);
        if (ours[t.slot] == game[t.slot]) ctx.Fail("our virtual-base slot %u is the game's thunk", t.slot);
    }
}

} // namespace

// The constructor and destructor alone (the decision's first half): what each writes at four placements of the virtual
// base (cSGResource alone, 12 bytes out, cResourceNode's +0x60, cGeometryDataContainer's +0x80), with a name and a
// segment set between them.
DIFF_TEST(SGResource_BasePart, 0xa3c270)
{
    sgfx::CheckSides(ctx, "base part", 16, BuildWorld, [&](bool original, std::mt19937 &rng) {
        const uint32_t placements[] = {0x30, 0x3c, 0x60, 0x80};
        Build(ctx, original, placements[w.seed % 4]);
        NoteObject("built", original);
        if (rng() % 2) {
            // A name and a segment, as Read or the setters leave them (counted the way they count them).
            ServiceCell temp;
            reinterpret_cast<ServiceCell *>(gObject.bytes + 0x20)->Assign(&temp.Open(P("lamp"), 1)->mValue);
            temp.Close();
        }
        if (cIGZUnknown *segment = Segment(rng)) {
            segment->AddRef();
            std::memcpy(gObject.bytes + 0x24, &segment, 4);
        }
        gObject.bytes[0x28] = uint8_t(rng() % 2);
        gObject.bytes[0x29] = uint8_t(rng());
        NoteState("set", original);
        Destroy(ctx, original, w.seed / 4 % 2 != 0);
        NoteState("destroyed", original);
    });
}

// The vtable route: random calls through both faces of the game's object and of ours.
DIFF_TEST(SGResource_Slots, 0xa3c540)
{
    (void)Ours();
    CheckGameThunks(ctx);
    sgfx::CheckSides(ctx, "slots", 96, BuildWorld, [&](bool original, std::mt19937 &rng) {
        Build(ctx, original, w.seed % 2 ? 0x60 : 0x30);
        Primary()->AddRef(); // the test's reference (never released: the object is a buffer)
        NoteObject("built", original);
        for (int k = 0; k < 24; ++k) Operate(ctx, k, rng);
        NoteState("after", original);
        sgfx::gReader.NoteNames();
        Destroy(ctx, original);
        NoteState("destroyed", original);
    });
}

namespace {

// cResourceNode (0x64 bytes; its cSGResource part at +0x28, the virtual base at +0x60): built by its own constructor
// with the most-derived flag, as its factory 0xa503a0 does.
constexpr uint32_t kNodeCtor = 0xa4fd10, kNodeDtor = 0xa4fe50, kNodeQuery = 0xa50620;
constexpr uint32_t kNodeSize = 0x64, kPart = 0x28, kNodeTag = 0x7a000000;
alignas(8) uint8_t gNode[kNodeSize + 8];

void NoteNode(const char *label, bool original)
{
    drawfx::Call c;
    c.note = label;
    for (unsigned i = 0; i < sizeof(gNode) / 4; ++i) c.words.push_back(drawfx::Norm(Code(shape::Word(gNode, i * 4), original)));
    drawfx::gLog.push_back(std::move(c));
}

} // namespace

// The decision on a game-derived class, and the adapters on its object.
DIFF_TEST(SGResource_GameDerived, 0xa3c270)
{
    (void)Ours();
    sgfx::RegistryScope registry(sgfx::kNodeRegistry);
    sgfx::CheckSides(
        ctx, "game derived", 48, BuildWorld,
        [&](bool original, std::mt19937 &rng) {
            // Ours: the hooks of the constructor, the destructor and QueryInterface (cResourceNode's own QueryInterface
            // falls back to 0xa3c4d0), as in game.
            struct Patches {
                shape::CodePatch ctor, dtor, query;
            };
            alignas(Patches) uint8_t patchSpace[sizeof(Patches)];
            Patches *patches = nullptr;
            if (!original)
                patches = ::new (patchSpace) Patches{
                    {kCtor, SGResourceBasePartConstructor()},
                    {kDtor, reinterpret_cast<const void *>(&cSGResourceAdapters::Destruct)},
                    {kQuery, reinterpret_cast<const void *>(&cSGResourceAdapters::QueryInterface)}};

            std::memset(gNode, 0xcd, sizeof(gNode));
            drawfx::AddRange(gNode, sizeof(gNode), kNodeTag);
            void *const r = ctx.Original<CtorFn>(kNodeCtor)(gNode, 1);
            drawfx::Note("constructor returns", drawfx::Norm(P(r)));
            NoteNode("built", original);
            reinterpret_cast<cIGZUnknown *>(gNode)->AddRef(); // the test's reference (the node is a buffer)

            void *const part = gNode + kPart;
            void *const secondary = gNode + kPart + cSGResource::kSecondaryOffset;
            for (int k = 0; k < 16; ++k) {
                const unsigned op = rng() % 5;
                drawfx::Note("op", op);
                char step[32];
                std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
                sgfx::MarkStep(ctx, step);
                switch (op) {
                case 0:
                case 1: { // QueryInterface: 0xa3c4d0 directly, or through cResourceNode's 0xa50620
                    const uint32_t iids[] = {cISGResource::kIid, 0x5b0aaa74, 0x9b11bf70, 0xbb11a53e, 0x456b8f1d, 0x4512dd13,
                                             0xe519c92c, 0x12345678};
                    const uint32_t iid = iids[rng() % 8];
                    void *out = nullptr;
                    void **at = rng() % 8 ? &out : nullptr;
                    bool got;
                    if (op == 1)
                        got = ctx.Original<QueryFn>(kNodeQuery)(gNode, iid, at);
                    else if (original)
                        got = ctx.Original<QueryFn>(kQuery)(part, iid, at);
                    else
                        got = cSGResourceAdapters::QueryInterface(static_cast<cSGResource *>(part), nullptr, iid, at);
                    drawfx::Note("query", {iid, uint8_t(got), drawfx::Norm(P(out))});
                    if (out) static_cast<cIGZUnknown *>(out)->Release();
                    break;
                }
                case 2: { // Read
                    sgfx::gReader.Version(rng() % 4);
                    sgfx::gReader.Text(Name(rng));
                    void *const reader = &sgfx::gReader.fake;
                    if (original)
                        ctx.Original<ReadFn>(kRead)(secondary, reader);
                    else
                        cSGResourceAdapters::Read(secondary, nullptr, static_cast<ifaces::cIGZPersistReader *>(reader));
                    break;
                }
                case 3: { // Write: the writer's answer to the name comes back
                    void *const writer = &sgfx::gWriter.fake;
                    const uint32_t got = original ? ctx.Original<WriteFn>(kWrite)(secondary, writer)
                                                  : cSGResourceAdapters::Write(secondary, nullptr,
                                                                               static_cast<ifaces::cIGZPersistWriter *>(writer));
                    drawfx::Note("write returns", got);
                    NoteHeldTypeName();
                    break;
                }
                default: { // a slot of the node's virtual base (cResourceNode's vtable, cSGResource's bodies)
                    cISGResource *face = reinterpret_cast<cISGResource *>(gNode + 0x60);
                    drawfx::Note("node face", {face->HasKey(), drawfx::Norm(P(face->GetSegment())), face->ForwardKey()});
                    sgfx::NoteText("node name", face->GetName());
                    break;
                }
                }
            }
            NoteNode("after", original);
            sgfx::gResources.NoteRefs();
            sgfx::gPool.NoteCounts();
            sgfx::gReader.NoteNames();
            ctx.Original<DtorFn>(kNodeDtor)(gNode);
            NoteNode("destroyed", original);
            sgfx::gResources.NoteRefs();
            sgfx::gPool.NoteCounts();
            if (patches) patches->~Patches();
        });
}
