// Differential tests of nGZSceneGraph::cExtension as a whole class (lane OY, scenegraph D1a; src/sg/cExtension.h).
//
// cExtension is abstract (Clone and slot 25 are pure): the game only builds it as a base part, its constructor 0xa3b7d0
// (or the copy constructor 0xa3b750) called by a derived constructor with the most-derived flag 0 after that constructor
// set the vbptr. The BasePart and Slots tests build it that way on both sides: a buffer whose vbptr (+4) points at a
// vbtable {-4, vbase - 4}, then the game's constructor or ours (the hook target, our compiler's constructor).
//
//   Extension_BasePart   the constructors and the destructor alone, at four placements of the virtual base: every word
//                        they write (vtables compared as the face they are: the game's 0xe694f4 / 0xe694fc / 0xe6951c
//                        against ours), the vbptr left alone, the vtordisp, the next extension released; half the seeds
//                        destroy through the deleting destructor (0xa3b910 / our primary slot 0) with flag 0.
//   Extension_Slots      the vtable route: random calls through both faces (the referent face +0xc and the virtual base
//                        cIExtension, every slot but the pure 15 / 25), on the game's object (its vtables, thunks and
//                        bodies) and on ours, with owner, next extensions, listeners on a real connection manager in the
//                        cell 0x10f6010, the event registry, a reader and a writer as fakes; the hook-shaped Write and
//                        GetTypeInfo bodies (0xa3c210 / 0xa3c160 against cExtensionAdapters') for their eax (the
//                        name's Release answer; the hidden out); the last Release through a stand-in
//                        deleting destructor (each side's own destructor behind it).
//   Extension_GameDerived the decision on a game-derived class: cTagExtension's constructor 0xa962d0 (flag 1) and
//                        destructor 0xa96390 run unpatched on one side and with every hooked RVA patched to our hook
//                        target on the other (what the hook table does in game); random calls through the tag's own
//                        vtables (its vtordisp thunks into cExtension's bodies, its QueryInterface and Write calling
//                        0xa3be60 / 0xa3c210, its Clone through the copy constructor 0xa3b750 and the deleting destructor).
//
// RVAs these tests reach (status entries cite them): the constructors 0xa3b7d0 / 0xa3b750 (with the behaviour template's
// constructor 0xa3b710 inside them on the game side; BasePart compares every word it leaves), the destructor 0xa3b880, the
// deleting destructor 0xa3b910; the referent face's QueryInterface 0xa3be60, Release 0xa3bf50 (-> 0xa3bee0), GetTypeInfo
// 0xa3c160, Write 0xa3c210, GetOwnerInfo 0xa3bf60; the behaviour bodies OnEvent 0xa3bd10, NotifyListeners 0xa3bfb0,
// AskListeners 0xa3bbc0, Connect 0xa3ba80, Disconnect 0xa3bb50, GetEventAttribute 0xa3b990 (the event manager getter
// 0xa3bac0 behind them); cIExtension's GetPersistReferent 0xa3b950, SetOwner 0xa3c0d0, SetNext 0xa3c0f0, IsKindOf
// 0xa3c130, GetDescription 0xa3bb90; and the game vtable 0xe6951c's own thunks (kGameThunks).
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "sgfx.h"
#include "shape.h"

#include "sg/cExtension.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kCtor = 0xa3b7d0, kCopyCtor = 0xa3b750, kDtor = 0xa3b880, kDeleting = 0xa3b910, kWrite = 0xa3c210,
                   kTypeInfo = 0xa3c160;
constexpr uint32_t kGamePrimary = 0xe694f4, kGameReferent = 0xe694fc, kGameBase = 0xe6951c;
constexpr uint32_t kObjectTag = 0x7b000000;
constexpr uint32_t kConnectionChanged = 0x5b51e806; // cConnectionChangedEvent: OnEvent syncs the active bit
// The plain behaviour type's event registry (0x1485f0: GetEventAttribute 0xa3b990 asks it, slots 12 / 13 count it).
constexpr sgfx::RegistryStatics kExtensionRegistry{0x10c94f4, 0x10c94fc, 0x10c9510, 0x10c9518};

// The game's virtual-base vtable (0xe6951c): its own vtordisp thunks, slot by slot (the shared ones, 0xa3b930 / 0xa3bd68 /
// 0xa3becc / 0xa3bd9b / 0x9e8839 / 0x9e7e7d / 0x2bd07d, are other families' too). Extension_Slots calls each through the
// game's vtable on one side and through ours on the other; it first checks this table against the game's vtable.
struct GameThunk {
    uint32_t slot, rva;
};
constexpr GameThunk kGameThunks[] = {
    {0, 0xa3be4b},  {2, 0xa3bf44},  {3, 0xa3bcf9},  {5, 0xa3bfa1},  {6, 0xa3bba9},  {8, 0xa3ba6d},  {9, 0xa3bb3e},
    {12, 0xa3bdbf}, {13, 0xa3b964}, {14, 0xa3b977}, {16, 0xa3b93b}, {21, 0xa3c0c3}, {22, 0xa3c0da}, {23, 0xa3c11f},
    {26, 0xa3bb87},
};

using CtorFn = void *(__thiscall *)(void *, uint32_t);
using CopyFn = void *(__thiscall *)(void *, const void *, uint32_t);
using DtorFn = void(__thiscall *)(void *);
using WriteFn = uint32_t(__thiscall *)(void *, void *);

// ---- our vtables ----
// Read off a concrete test class while it is built: the Snapshot base is constructed after the cExtension part and
// before the test class stores its own vtables, so it sees what our cExtension constructor stored.
struct OurVTables {
    uint32_t primary, referent, base;
};
uint8_t *gBuilding;
OurVTables gOurs;
struct Snapshot {
    Snapshot()
    {
        gOurs.primary = shape::Word(gBuilding, 0);
        gOurs.referent = shape::Word(gBuilding, 0xc);
        const int32_t *vbtable = reinterpret_cast<const int32_t *>(uintptr_t(shape::Word(gBuilding, 4)));
        gOurs.base = shape::Word(gBuilding, 4 + vbtable[1]);
    }
};
class TestExtension final : public cExtension, private Snapshot {
public:
    cIExtension *Clone() override { return nullptr; }
    const char *GetTypeName() override { return nullptr; }
};
const OurVTables &Ours()
{
    static bool done = false;
    if (!done) {
        alignas(8) static uint8_t buffer[sizeof(TestExtension)];
        gBuilding = buffer;
        TestExtension *t = ::new (buffer) TestExtension;
        t->~TestExtension();
        done = true;
    }
    return gOurs;
}

// A vtable word as the face it is, on its side.
uint32_t Code(uint32_t w, bool original)
{
    const OurVTables &o = Ours();
    if (w == (original ? P(GameAddress<void>(kGamePrimary)) : o.primary)) return 0xd0000000u;
    if (w == (original ? P(GameAddress<void>(kGameReferent)) : o.referent)) return 0xd0000001u;
    if (w == (original ? P(GameAddress<void>(kGameBase)) : o.base)) return 0xd0000002u;
    return w;
}

// ---- the object, built as a derived constructor builds its base part ----
struct Object {
    alignas(8) uint8_t bytes[0x60];
    int32_t vbtable[2];
    uint32_t vbase;
};
Object gObject;
Object gSource; // the copy constructor's argument (never read: 0xcd)

void Prepare(Object &o, uint32_t vbase)
{
    std::memset(o.bytes, 0xcd, sizeof(o.bytes));
    o.vbtable[0] = -4;
    o.vbtable[1] = int32_t(vbase - 4);
    o.vbase = vbase;
    const uint32_t vbptr = P(o.vbtable);
    std::memcpy(o.bytes + 4, &vbptr, 4);
}

void Build(difftest::Context &ctx, bool original, uint32_t vbase, bool copy = false)
{
    Prepare(gObject, vbase);
    drawfx::AddRange(gObject.bytes, sizeof(gObject.bytes), kObjectTag);
    void *r;
    if (copy) {
        Prepare(gSource, vbase);
        const CopyFn fn = original ? ctx.Original<CopyFn>(kCopyCtor)
                                   : reinterpret_cast<CopyFn>(const_cast<void *>(ExtensionBasePartCopyConstructor()));
        r = fn(gObject.bytes, gSource.bytes, 0);
    } else {
        const CtorFn fn = original ? ctx.Original<CtorFn>(kCtor)
                                   : reinterpret_cast<CtorFn>(const_cast<void *>(ExtensionBasePartConstructor()));
        r = fn(gObject.bytes, 0);
    }
    drawfx::Note("constructor returns", drawfx::Norm(P(r)));
}

void Destroy(difftest::Context &ctx, bool original, bool deleting = false)
{
    if (deleting && original)
        ctx.Original<void(__thiscall *)(void *, uint32_t)>(kDeleting)(gObject.bytes, 0);
    else if (deleting)
        reinterpret_cast<cExtension *>(gObject.bytes)->~cExtension(); // virtual: primary slot 0, flag 0
    else if (original)
        ctx.Original<DtorFn>(kDtor)(gObject.bytes);
    else
        cExtensionAdapters::Destruct(reinterpret_cast<cExtension *>(gObject.bytes), nullptr);
}

// After destruction the original's last code on the object is the behaviour template's destructor 0xa3b850, called
// last by 0xa3b880: three stores and `ret` (the template's vtables 0xe6947c at +0 and 0xe69484 at the virtual base, the
// vtordisp for the template's own placement, vbase offset - 0xc). Nothing reads them: 0xa3b880 returns to the derived
// destructor's tail-jump caller or to the deleting destructor 0xa3b910, which frees the block. Our compiler drops those
// dead stores (the template's destructor is inline and empty), so a destroyed object's three words are left out
// (kDead); every other word is compared.
constexpr uint32_t kDead = 0xdead0000u;
bool DeadAfterDestruction(uint32_t offset, uint32_t vbase) { return offset == 0 || offset == vbase - 4 || offset == vbase; }

// The object's words through the virtual base's vtable and two past it (left 0xcd): vtables coded, the vbptr as
// whether it is still the derived class's, pointers normalised.
void NoteObject(const char *label, bool original, bool destroyed = false)
{
    drawfx::Call c;
    c.note = label;
    const unsigned words = (gObject.vbase + 4) / 4 + 2;
    for (unsigned i = 0; i < words; ++i) {
        const uint32_t w = shape::Word(gObject.bytes, i * 4);
        if (destroyed && DeadAfterDestruction(i * 4, gObject.vbase))
            c.words.push_back(kDead);
        else
            c.words.push_back(i == 1 ? uint32_t(w == P(gObject.vbtable)) : drawfx::Norm(Code(w, original)));
    }
    drawfx::gLog.push_back(std::move(c));
}

cIGZPersistSerializableReferent *Referent() { return reinterpret_cast<cIGZPersistSerializableReferent *>(gObject.bytes + 0xc); }
cIExtension *Face() { return reinterpret_cast<cIExtension *>(gObject.bytes + gObject.vbase); }
uint32_t Count() { return shape::Word(gObject.bytes, 8); }

// ---- the world ----
// A cIGZString as GetDescription sees it: slot 4 FromChar(text), the text's place in the image noted.
inline constexpr drawfx::Slot kTextSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"}, {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},          {2, drawfx::Ret::Int, false, "", ""},
    {1, drawfx::Ret::Int, true, "FromChar", "s"},
};
inline constexpr drawfx::Iface kText{"Text", 5, kTextSlots};
uint32_t TextFrom(Fake &, const uint32_t *a)
{
    drawfx::Note("text at", a[0] - P(GameAddress<void>(0)));
    return 0;
}

struct World {
    Fake owner;     // id 300: a node whose referent face is `ownerRef`
    Fake ownerRef;  // id 310: its GetOwnerInfo (6) answers 0x600 + seed
    Fake next[3];   // ids 320-322: extensions for the list
    Fake listener[2];
    Fake text;      // id 330
    sgfx::Manager manager;
    uint32_t seed = 0;
};
World w;

// The writer's slot 28 (WriteTypeInfo), noted, a reference held on the record's name, and an answer of its own (Write's
// eax when there is no name): after the call the writer drops the name and notes what is left.
cIGZUnknown *gHeldTypeName;
uint32_t HoldTypeName(Fake &, const uint32_t *a)
{
    const auto *info = reinterpret_cast<const cPersistTypeInfo *>(uintptr_t(a[0]));
    sgfx::NoteTypeInfo("written type", info);
    if (info && info->mName) {
        gHeldTypeName = reinterpret_cast<cIGZUnknown *>(info->mName);
        gHeldTypeName->AddRef();
    }
    return 0x2800 + w.seed;
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
    sgfx::gPool.Handle("tag");
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::gWriter.fake.Returns(23, 0x2300u + seed);
    sgfx::gWriter.fake.On(28, &HoldTypeName);
    gHeldTypeName = nullptr;
    sgfx::MakeNode(w.owner, 300);
    w.ownerRef.Reset<sgfx::kReferent>(310);
    w.ownerRef.Returns(6, 0x600u + seed);
    sgfx::SetWord(w.owner, sgfx::kReferentWord, &w.ownerRef);
    for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.next[i], 320 + i);
    sgfx::MakeListener(w.listener[0], 500, true);
    sgfx::MakeListener(w.listener[1], 501, seed % 2 != 0);
    w.text.Reset<kText>(330);
    w.text.On(4, &TextFrom);
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cExtensionBehaviorTraits::kEventManagerCell);
}

void NoteWorld()
{
    for (const Fake &e : w.next) drawfx::Note("next refs", {e.id, uint32_t(e.refs)});
    for (const Fake &l : w.listener) drawfx::Note("listener refs", {l.id, uint32_t(l.refs)});
    drawfx::Note("owner refs", uint32_t(w.owner.refs));
    sgfx::gPool.NoteCounts();
}

// The record GetEventAttribute copies for an unknown event (0x106a48c) is all zeros in the unstarted image: a pattern
// shows a wrong record address.
struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106a48c;
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

// The stand-in deleting destructor: the primary vtable's slot 0 replaced before a last Release, so the release path
// (disconnect, then the virtual deleting destructor with flag 1) is seen without freeing a buffer. It notes the call and
// runs the side's own destructor (gDestroy).
void (*gDestroy)();
void __fastcall StandInDeleting(void *self, void *, uint32_t flag)
{
    drawfx::Note("deleting destructor", {drawfx::Norm(P(self)), flag});
    if (gDestroy) gDestroy();
}
void *gStandInVTable[1] = {reinterpret_cast<void *>(&StandInDeleting)};

const uint32_t kIids[] = {cIExtension::kIid, 0x5b0aaa74, 0xbb11a53e, 0x3b253dc7, 0xfb859324, 1, 0x9a80964e, 0x12345678};
const uint32_t kEvents[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[1], sgfx::kRegistryIds[2], 0x5678};

cIGZUnknown *Listener(std::mt19937 &rng) { return reinterpret_cast<cIGZUnknown *>(&w.listener[rng() % 2]); }

// The behaviour slots and cIExtension's own, through `face` (the virtual base of whatever object): ops 0-15.
void OperateFace(cIExtension *face, unsigned op, std::mt19937 &rng)
{
    switch (op) {
    case 0: { // QueryInterface
        const uint32_t iid = kIids[rng() % 8];
        void *out = nullptr;
        const bool r = rng() % 8 ? face->QueryInterface(iid, &out) : face->QueryInterface(iid, nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (out && iid != 0xfb859324) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: {
        const uint32_t a = face->AddRef();
        drawfx::Note("counts", {a, face->Release()});
        break;
    }
    case 2: face->Connect(kEvents[rng() % 4], Listener(rng), 0x500 + rng() % 2); break;
    case 3: face->Disconnect(kEvents[rng() % 4], Listener(rng), 0x500 + rng() % 2); break;
    case 4: face->NotifyListeners(kEvents[rng() % 4], rng(), nullptr); break;
    case 5: drawfx::Note("asked", uint8_t(face->AskListeners(kEvents[rng() % 4], rng(), nullptr))); break;
    case 6: { // OnEvent: the connection change syncs the active bit; anything else does nothing
        face->OnEvent(rng() % 2 ? kConnectionChanged : 0x1234, rng(), nullptr, rng());
        drawfx::Note("active", {face->IsActive(), face->GetEventCount()});
        break;
    }
    case 7: {
        const nGZEvent::cEventAttribute a = face->GetEventAttribute(kEvents[rng() % 4]);
        drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        break;
    }
    case 8: drawfx::Note("persist referent", drawfx::Norm(P(face->GetPersistReferent()))); break;
    case 9:
        drawfx::Note("links", {drawfx::Norm(P(face->GetOwner())), drawfx::Norm(P(face->GetOwnerConst())),
                               drawfx::Norm(P(face->GetNext())), drawfx::Norm(P(face->GetNextConst()))});
        break;
    case 10: {
        cIObjectGraphNode *owner = rng() % 3 ? reinterpret_cast<cIObjectGraphNode *>(&w.owner) : nullptr;
        drawfx::Note("set owner", drawfx::Norm(P(face->SetOwner(owner))));
        break;
    }
    case 11: {
        const unsigned k = rng() % 4;
        cIExtension *const next = k < 3 ? reinterpret_cast<cIExtension *>(&w.next[k]) : nullptr;
        // eax: the last count answer when the next one changes (unchanged, the original's is the caller's own).
        const bool changes = next != face->GetNext();
        const uint32_t r = face->SetNext(next);
        drawfx::Note("set next", {uint32_t(changes), changes ? r : 0});
        for (const Fake &e : w.next) drawfx::Note("next refs", {e.id, uint32_t(e.refs)});
        break;
    }
    case 12: {
        const uint32_t iid = kIids[rng() % 8];
        drawfx::Note("kind of", {iid, uint8_t(face->IsKindOf(iid))});
        break;
    }
    case 13: drawfx::Note("persistent", uint8_t(face->IsPersistent())); break;
    case 14: face->GetDescription(reinterpret_cast<cIGZString *>(&w.text)); break;
    default: drawfx::Note("asked query", uint8_t(face->OnQuery(kEvents[rng() % 4], rng(), nullptr))); break;
    }
}

// The referent face's slots: ops 16-21 (Write's eax through the hook-shaped body when `write` is given).
void OperateReferent(cIGZPersistSerializableReferent *referent, unsigned op, std::mt19937 &rng,
                     uint32_t (*write)(void *referent, void *writer),
                     cPersistTypeInfo *(*typeInfo)(void *referent, cPersistTypeInfo *out) = nullptr)
{
    switch (op) {
    case 16: { // QueryInterface
        const uint32_t iid = kIids[rng() % 8];
        void *out = nullptr;
        const bool r = rng() % 8 ? referent->QueryInterface(iid, &out) : referent->QueryInterface(iid, nullptr);
        drawfx::Note("referent query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (out && iid != 0xfb859324) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 17: {
        const uint32_t a = referent->AddRef();
        drawfx::Note("referent counts", {a, referent->Release()});
        break;
    }
    case 18: // Read: the type-info record only
        sgfx::gReader.Version(rng() % 4);
        referent->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&sgfx::gReader.fake));
        break;
    case 19: { // GetTypeInfo, through the slot or (its eax, the hidden out) the body
        cPersistTypeInfo info;
        if (typeInfo && rng() % 2)
            drawfx::Note("type info returns its out", uint32_t(typeInfo(referent, &info) == &info));
        else
            info = referent->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 20: // Write, through the slot or (its eax) the body
        if (write && rng() % 2)
            drawfx::Note("write returns", write(referent, &sgfx::gWriter.fake));
        else
            referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        NoteHeldTypeName();
        break;
    default: drawfx::Note("owner info", drawfx::Norm(referent->GetOwnerInfo())); break;
    }
}

void CheckGameThunks(difftest::Context &ctx)
{
    const uint32_t *game = GameAddress<uint32_t>(kGameBase);
    const uint32_t *ours = reinterpret_cast<const uint32_t *>(uintptr_t(Ours().base));
    for (const GameThunk &t : kGameThunks) {
        if (game[t.slot] != P(GameAddress<void>(t.rva)))
            ctx.Fail("game virtual-base slot %u is not the thunk 0x%x the status entries name", t.slot, t.rva);
        if (ours[t.slot] == game[t.slot]) ctx.Fail("our virtual-base slot %u is the game's thunk", t.slot);
    }
}

difftest::Context *gCtx;
bool gOriginal;
void DestroyObject() { Destroy(*gCtx, gOriginal); }

uint32_t OriginalWrite(void *referent, void *writer) { return gCtx->Original<WriteFn>(kWrite)(referent, writer); }
cPersistTypeInfo *OriginalTypeInfo(void *referent, cPersistTypeInfo *out)
{
    return gCtx->Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(kTypeInfo)(referent, out);
}
cPersistTypeInfo *OurTypeInfo(void *referent, cPersistTypeInfo *out) { return cExtensionAdapters::GetTypeInfo(referent, nullptr, out); }
uint32_t OurWrite(void *referent, void *writer)
{
    return cExtensionAdapters::Write(referent, nullptr, static_cast<ifaces::cIGZPersistWriter *>(writer));
}

// The last references released through the virtual base (the stand-in deleting destructor behind them), each answer
// noted; at most 8.
void ReleaseAll(cIExtension *face, void *object)
{
    void *const vt = gStandInVTable;
    std::memcpy(object, &vt, 4);
    for (int i = 0; i < 8; ++i) {
        const uint32_t r = face->Release();
        drawfx::Note("release", r);
        if (!r) break;
    }
}

} // namespace

// The constructors and the destructor alone: what each writes at four placements of the virtual base (cExtension alone
// +0x1c, cDataListExtension's +0x20, cTagExtension's +0x24, further out), with an owner and a next extension set between.
DIFF_TEST(Extension_BasePart, 0xa3b7d0)
{
    (void)Ours();
    sgfx::CheckSides(ctx, "base part", 32, BuildWorld, [&](bool original, std::mt19937 &rng) {
        const uint32_t placements[] = {0x1c, 0x20, 0x24, 0x40};
        Build(ctx, original, placements[w.seed % 4], w.seed / 8 % 2 != 0);
        NoteObject("built", original);
        const uint32_t owner = rng() % 2 ? P(&w.owner) : 0;
        std::memcpy(gObject.bytes + 0x10, &owner, 4);
        if (rng() % 3) {
            Fake &next = w.next[rng() % 3];
            ++next.refs; // counted, as SetNext leaves it
            const uint32_t p = P(&next);
            std::memcpy(gObject.bytes + 0x14, &p, 4);
        }
        NoteObject("set", original);
        Destroy(ctx, original, w.seed / 4 % 2 != 0);
        NoteObject("destroyed", original, true);
        NoteWorld();
    });
    w.manager.Drop();
}

// The vtable route: random calls through both faces of the game's object and of ours.
DIFF_TEST(Extension_Slots, 0xa3c0f0)
{
    (void)Ours();
    CheckGameThunks(ctx);
    sgfx::RegistryScope registry(kExtensionRegistry);
    shape::StandIn registryStand{0x1485f0, &nGZEvent::cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<pdfx::kBaseRegistryMap>};
    MissingRecordScope missing;
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "slots", 96, BuildWorld, [&](bool original, std::mt19937 &rng) {
        gOriginal = original;
        gDestroy = &DestroyObject;
        Build(ctx, original, w.seed % 2 ? 0x40 : 0x1c, w.seed % 5 == 0);
        Face()->AddRef(); // the test's reference
        NoteObject("built", original);
        for (int k = 0; k < 32; ++k) {
            const unsigned op = rng() % 22;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            if (op < 16)
                OperateFace(Face(), op, rng);
            else
                OperateReferent(Referent(), op, rng, original ? &OriginalWrite : &OurWrite,
                                original ? &OriginalTypeInfo : &OurTypeInfo);
            drawfx::Note("count", Count());
        }
        NoteObject("after", original);
        sgfx::gReader.NoteNames();
        if (w.seed % 3 == 0) {
            w.manager->DisconnectAll(reinterpret_cast<cIGZUnknown *>(Face()));
            Destroy(ctx, original);
        } else {
            ReleaseAll(Face(), gObject.bytes);
        }
        NoteObject("destroyed", original, true);
        NoteWorld();
        drawfx::Note("connected", {uint8_t(w.manager->HasConnectionsTo(reinterpret_cast<cIGZUnknown *>(&w.listener[0]))),
                                   uint8_t(w.manager->HasConnectionsTo(reinterpret_cast<cIGZUnknown *>(&w.listener[1])))});
        gDestroy = nullptr;
    });
    w.manager.Drop();
}

namespace {

// cTagExtension (0x34 bytes): +0 its vtable 0xe74bf0, +4 the vbptr, +0xc the referent face 0xe74bf8, the tag (a
// ServiceCell) +0x18, the vtordisp +0x20, cIExtension +0x24 (0xe74c18), the cITagExtension face +0x2c (0xe74c88).
constexpr uint32_t kTagCtor = 0xa962d0, kTagDtor = 0xa96390, kTagSize = 0x34, kTagTag = 0x7a000000;
constexpr uint32_t kTagVTables[] = {0xe74bf0, 0xe74bf8, 0xe74c18, 0xe74c88};
alignas(8) uint8_t gTag[kTagSize + 8];

cIExtension *TagFace() { return reinterpret_cast<cIExtension *>(gTag + 0x24); }
cIGZPersistSerializableReferent *TagReferent() { return reinterpret_cast<cIGZPersistSerializableReferent *>(gTag + 0xc); }

// A word of a tag extension: its vtables coded (the derived class's, the same on both sides; cExtension's as Code
// codes them, the game's or ours as the side is).
uint32_t TagCode(uint32_t w, bool original)
{
    for (uint32_t i = 0; i < 4; ++i)
        if (w == P(GameAddress<void>(kTagVTables[i]))) return 0xd1000000u + i;
    return drawfx::Norm(Code(w, original));
}
// Every word; a destroyed tag's dead words as
// NoteObject's (the virtual base at +0x24).
void NoteTag(const char *label, const uint8_t *tag, bool original, bool destroyed = false)
{
    drawfx::Call c;
    c.note = label;
    for (unsigned i = 0; i < kTagSize / 4; ++i) {
        if (destroyed && DeadAfterDestruction(i * 4, 0x24))
            c.words.push_back(kDead);
        else
            c.words.push_back(TagCode(shape::Word(tag, i * 4), original));
    }
    drawfx::gLog.push_back(std::move(c));
}

void DestroyTag() { gCtx->Original<DtorFn>(kTagDtor)(gTag); }

// Every row of the hook table (src/hook/tables/cExtension.cpp), as the hooks place them.
struct PatchRow {
    uint32_t rva;
    const void *target;
};
const PatchRow *PatchRows(size_t &n)
{
    static const PatchRow rows[] = {
        {0xa3b7d0, ExtensionBasePartConstructor()},
        {0xa3b750, ExtensionBasePartCopyConstructor()},
        {0xa3b880, reinterpret_cast<const void *>(&cExtensionAdapters::Destruct)},
        {0xa3be60, reinterpret_cast<const void *>(&cExtensionAdapters::QueryInterface)},
        {0xa3bf50, reinterpret_cast<const void *>(&cExtensionAdapters::Release)},
        {0xa3c160, reinterpret_cast<const void *>(&cExtensionAdapters::GetTypeInfo)},
        {0xa3c210, reinterpret_cast<const void *>(&cExtensionAdapters::Write)},
        {0xa3bf60, reinterpret_cast<const void *>(&cExtensionAdapters::GetOwnerInfo)},
        {0xa3bd10, reinterpret_cast<const void *>(&cExtensionAdapters::OnEvent)},
        {0xa3bfb0, reinterpret_cast<const void *>(&cExtensionAdapters::NotifyListeners)},
        {0xa3bbc0, reinterpret_cast<const void *>(&cExtensionAdapters::AskListeners)},
        {0xa3ba80, reinterpret_cast<const void *>(&cExtensionAdapters::Connect)},
        {0xa3bb50, reinterpret_cast<const void *>(&cExtensionAdapters::Disconnect)},
        {0xa3b990, reinterpret_cast<const void *>(&cExtensionAdapters::GetEventAttribute)},
        {0xa3b950, reinterpret_cast<const void *>(&cExtensionAdapters::GetPersistReferent)},
        {0xa3c0d0, reinterpret_cast<const void *>(&cExtensionAdapters::SetOwner)},
        {0xa3c0f0, reinterpret_cast<const void *>(&cExtensionAdapters::SetNext)},
        {0xa3c130, reinterpret_cast<const void *>(&cExtensionAdapters::IsKindOf)},
        {0xa3bb90, reinterpret_cast<const void *>(&cExtensionAdapters::GetDescription)},
    };
    n = sizeof(rows) / sizeof(rows[0]);
    return rows;
}

} // namespace

// The decision on a game-derived class, every hook in place on our side.
DIFF_TEST(Extension_GameDerived, 0xa3b7d0)
{
    sgfx::RegistryScope registry(kExtensionRegistry);
    shape::StandIn registryStand{0x1485f0, &nGZEvent::cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<pdfx::kBaseRegistryMap>};
    MissingRecordScope missing;
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "game derived", 64, BuildWorld, [&](bool original, std::mt19937 &rng) {
        std::optional<shape::CodePatch> patches[19];
        if (!original) {
            size_t n = 0;
            const PatchRow *rows = PatchRows(n);
            for (size_t i = 0; i < n; ++i) patches[i].emplace(rows[i].rva, rows[i].target);
        }
        gDestroy = &DestroyTag;
        std::memset(gTag, 0xcd, sizeof(gTag));
        drawfx::AddRange(gTag, sizeof(gTag), kTagTag);
        void *const r = ctx.Original<CtorFn>(kTagCtor)(gTag, 1);
        drawfx::Note("constructor returns", drawfx::Norm(P(r)));
        NoteTag("built", gTag, original);
        TagFace()->AddRef(); // the test's reference
        for (int k = 0; k < 24; ++k) {
            const unsigned op = rng() % 23;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            if (op < 16) {
                OperateFace(TagFace(), op, rng);
            } else if (op < 22) {
                // (Read, op 18, is the tag's own 0xa965a0, not cExtension's: skipped. The tag's Write calls 0xa3c210
                // and drops its eax.)
                if (op != 18) OperateReferent(TagReferent(), op, rng, nullptr);
            } else { // Clone: the tag's copy constructor through 0xa3b750, then released (its deleting destructor)
                cIExtension *clone = TagFace()->Clone();
                uint8_t *const object = reinterpret_cast<uint8_t *>(clone) - 0x24;
                drawfx::AddRange(object, kTagSize, 0x79000000);
                NoteTag("clone", object, original);
                drawfx::Note("clone counts", {clone->AddRef(), clone->Release() /* 0: deleted */});
            }
            drawfx::Note("count", shape::Word(gTag, 8));
        }
        NoteTag("after", gTag, original);
        if (w.seed % 3 == 0) {
            w.manager->DisconnectAll(reinterpret_cast<cIGZUnknown *>(TagFace()));
            DestroyTag();
        } else {
            ReleaseAll(TagFace(), gTag);
        }
        NoteTag("destroyed", gTag, original, true);
        NoteWorld();
        gDestroy = nullptr;
        for (auto &p : patches) p.reset();
    });
    w.manager.Drop();
}
