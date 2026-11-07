// Differential tests of the nGZSceneGraph::cGeometryBuilder base bodies the hook table
// (src/hook/tables/cGeometryBuilder.cpp) puts at their RVAs (lane QM, scenegraph D2 part 5; src/sg/cGeometryBuilder.h,
// cGeometryBuilderAdapters).
//
// Every object here is the game's own, built by its factory in the unhooked image: the compactor (0xa83930), the bounding
// volume builder (0xa85090), the tangent space builder (0xa86bf0) and the indexed mesh builder (0xa7ffa0), four of the
// nine classes that derive from the base, with four different layouts (the cIGeometryBuilder face at +0x1c, +0x24, +0x20
// and +0x1d0). One side runs them as the game built them; the other runs them with every hooked RVA patched to its
// adapter (shape::CodePatch, what the hook table does in game), so the vtables, the vtordisp thunks and the derived
// classes' bodies of the game reach our adapters at the static `this` their thunks deliver. Random calls through the
// three faces (the cIGeometryBuilder behaviour slots, the persist face, the class's own face) with listeners on a real
// connection manager in the cell 0x10f60f4, the event registry's statics in place, and a writer fake; the object's words
// (count, the extra listener, vbtable entries, vtordisps) are compared after every few calls, and both sides' listener
// and name reference counts at the end.
//
// The other five classes (cAlphaSortBuilder, cOptimizedGeometryBuilder, cProcessDeformationsBuilder, nTSSG's
// cNHoodTerrainGeometryBuilder and cWaterGeometryBuilder) are not built here (their factories need services the harness
// has none of); their vtables and thunks reach the same bodies through the same static offsets, which the four layouts
// above cover (the offsets are per body, not per class).
//
// Left original and not tested as hooks: Read 0x9dc310 and AddRef 0x2582b0 (folded across unrelated classes), tested
// through our vtables by the classes' own tests (tests/sg_geometry_builders.cpp and the like).
//
// RVAs these tests reach (status entries cite them): QueryInterface 0xa45310, Release 0xa45530 (-> 0xa454c0), Write
// 0xa45790 (with GetTypeInfo 0xa456d0 behind it), OnEvent 0xa45240, AskListeners 0xa450f0, Connect 0xa44fd0,
// Disconnect 0xa450a0, GetEventCount 0xa452c0, GetEventIds 0xa44c60, GetEventAttribute 0xa44ee0 (the registry 0xa453a0
// behind them), NotifyListeners 0xa45660 (-> 0xa45540), SetListener 0xa456c0, the event manager getter 0xa45010, and
// the four classes' vtordisp thunks and factories.
#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"
#include "shape.h"

#include "hook/hooks.h"
#include "sg/cBoundingVolumeBuilder.h"
#include "sg/cCompactorBuilder.h"
#include "sg/cIndexedMeshBuilder.h"
#include "sg/cTangentSpaceBuilder.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

using FactoryFn = cIGZUnknown *(__cdecl *)();
constexpr uint32_t kBuilderTag = 0x6e000000;
constexpr uint32_t kConnectionChanged = 0x5b51e806;
constexpr sgfx::RegistryStatics kBuilderRegistry{0x10f60e8, 0x10f60f0, 0x10f60f8, 0x10f6100};
const uint32_t kEvents[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[1], sgfx::kRegistryIds[2], 0x5678};

// ---- the classes' facts ----
struct Kind {
    const char *name;
    uint32_t factory;
    uint32_t size;
    uint32_t face; // the cIGeometryBuilder subobject's offset
    uint32_t iid;  // the class's own interface
    cIGeometryBuilder *(*ownBase)(void *);
};
template <class I>
cIGeometryBuilder *BaseOf(void *own)
{
    return static_cast<cIGeometryBuilder *>(static_cast<I *>(own));
}
const Kind kKinds[] = {
    {"compactor", 0xa83930, 0x2c, 0x1c, cICompactorBuilder::kIid, &BaseOf<cICompactorBuilder>},
    {"bounding volume", 0xa85090, 0x34, 0x24, cIBoundingVolumeBuilder::kIid, &BaseOf<cIBoundingVolumeBuilder>},
    {"tangent space", 0xa86bf0, 0x30, 0x20, cITangentSpaceBuilder::kIid, &BaseOf<cITangentSpaceBuilder>},
    {"indexed mesh", 0xa7ffa0, 0x1e0, 0x1d0, cIIndexedMeshBuilder::kIid, &BaseOf<cIIndexedMeshBuilder>},
};

difftest::Context *gCtx;

// ---- a writer fake: the persist writer's slots the base's Write (and the derived classes') call ----
#define SGGB_BAD(n) \
    virtual uint32_t Bad##n() { difftest::NotCounted q; drawfx::Note("bad slot " #n, n); return 0; }

struct Writer {
    SGGB_BAD(0) SGGB_BAD(1) SGGB_BAD(2) SGGB_BAD(3)
    virtual uint32_t SetUint8(uint32_t v) { difftest::NotCounted q; drawfx::Note("w SetUint8", v & 0xff); return 1; } // 4
    SGGB_BAD(5) SGGB_BAD(6) SGGB_BAD(7) SGGB_BAD(8) SGGB_BAD(9)
    virtual uint32_t SetUint32(uint32_t v) { difftest::NotCounted q; drawfx::Note("w SetUint32", v); return 1; } // 10
    SGGB_BAD(11) SGGB_BAD(12) SGGB_BAD(13) SGGB_BAD(14) SGGB_BAD(15) SGGB_BAD(16) SGGB_BAD(17) SGGB_BAD(18)
    SGGB_BAD(19) SGGB_BAD(20) SGGB_BAD(21) SGGB_BAD(22)
    virtual uint32_t WriteRZCharStr(const char *text) // 23
    {
        difftest::NotCounted q;
        sgfx::NoteText("w WriteRZCharStr", text);
        return 1;
    }
    SGGB_BAD(24) SGGB_BAD(25) SGGB_BAD(26) SGGB_BAD(27)
    virtual uint32_t WriteTypeInfo(const cPersistTypeInfo *info) // 28
    {
        difftest::NotCounted q;
        sgfx::NoteTypeInfo("w WriteTypeInfo", info);
        return 1;
    }
};
#undef SGGB_BAD
Writer gWriter;
ifaces::cIGZPersistWriter *WriterFace() { return reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter); }

// ---- the world ----
struct World {
    Fake listener[2]; // ids 500, 501: connected through the manager
    Fake extra[2];    // ids 510, 511: the extra listener SetListener stores
    sgfx::Manager manager;
};
World w;

// The record GetEventAttribute copies for an unknown event: a pattern shows a wrong record address.
struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106ab04;
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

void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    sgfx::gPool.Reset(cells);
    for (std::string &text : sgfx::gPool.texts) std::string().swap(text);
    sgfx::gResources.Reset(cells);
    sgfx::gResources.manager.Returns(9, seed % 3);
    sgfx::gResources.manager.Returns(14, (seed + 1) % 3);
    sgfx::MakeListener(w.listener[0], 500, true);
    sgfx::MakeListener(w.listener[1], 501, seed % 2 != 0);
    sgfx::MakeListener(w.extra[0], 510, true);
    sgfx::MakeListener(w.extra[1], 511, false);
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cGeometryBuilderBehaviorTraits::kEventManagerCell);
}

void NoteWorld()
{
    for (const Fake &l : w.listener) drawfx::Note("listener refs", {l.id, uint32_t(l.refs)});
    for (const Fake &l : w.extra) drawfx::Note("extra listener refs", {l.id, uint32_t(l.refs)});
}

// ---- one builder, the game's, as its three faces (the test holds one reference on each) ----
struct Builder {
    const Kind *kind = nullptr;
    cIGeometryBuilder *base = nullptr;
    cIGZPersistSerializable2 *persist = nullptr;
    void *own = nullptr;
    uint8_t *object = nullptr;

    cIGeometryBuilder *OwnBase() const { return kind->ownBase(own); }
};

Builder Make(const Kind &kind)
{
    Builder b;
    b.kind = &kind;
    cIGZUnknown *const made = gCtx->Original<FactoryFn>(kind.factory)();
    b.base = reinterpret_cast<cIGeometryBuilder *>(made);
    b.object = reinterpret_cast<uint8_t *>(made) - kind.face;
    {
        difftest::NotCounted quiet;
        drawfx::AddRange(b.object, kind.size, kBuilderTag);
    }
    drawfx::Note("first count", b.base->AddRef());
    void *out = nullptr;
    drawfx::Note("query persist", {uint8_t(b.base->QueryInterface(cGeometryBuilder::kIidSerializable, &out)),
                                   drawfx::Norm(P(out))});
    b.persist = static_cast<cIGZPersistSerializable2 *>(out);
    out = nullptr;
    drawfx::Note("query own", {uint8_t(b.base->QueryInterface(kind.iid, &out)), drawfx::Norm(P(out))});
    b.own = out;
    return b;
}

void Drop(const Builder &b)
{
    drawfx::Note("persist release", b.persist->Release());
    drawfx::Note("own release", b.OwnBase()->Release());
    drawfx::Note("last release", b.base->Release());
}

// The object's words that are not vtables: the count, the extra listener, the vbtable's entries, the vtordisps.
void NoteWords(const Builder &b)
{
    auto word = [&](uint32_t off) { return *reinterpret_cast<const uint32_t *>(b.object + off); };
    drawfx::Call c;
    c.note = "words";
    c.words = {word(8), drawfx::Norm(word(0x10))};
    const auto *vbtable = reinterpret_cast<const int32_t *>(word(4));
    c.words.insert(c.words.end(), {uint32_t(vbtable[0]), uint32_t(vbtable[1]), uint32_t(vbtable[2])});
    const uint32_t ownFace = b.kind->face + 8;
    const auto *ownVbtable = reinterpret_cast<const int32_t *>(word(ownFace + 4));
    c.words.insert(c.words.end(), {uint32_t(ownVbtable[0]), uint32_t(ownVbtable[1])});
    c.words.push_back(word(b.kind->face - 4)); // the vtordisps
    c.words.push_back(word(ownFace - 4));
    drawfx::gLog.push_back(std::move(c));
}

const uint32_t kIids[] = {0,          1,          cGeometryBuilder::kIidSerializable, cGeometryBuilder::kIidBehavior,
                          0xfb859324, 0xbbb79c90, cICompactorBuilder::kIid,           cIBoundingVolumeBuilder::kIid,
                          cITangentSpaceBuilder::kIid, cIIndexedMeshBuilder::kIid,    0x12345678};
constexpr unsigned kIidCount = sizeof(kIids) / sizeof(kIids[0]);

// An interface a query answered, released by what it is: the class's own face through its base; the uncounted behaviour
// id not at all.
void ReleaseAnswer(const Builder &b, uint32_t iid, void *out)
{
    if (!out || iid == 0xfb859324) return;
    if (iid == b.kind->iid)
        drawfx::Note("query release own", b.kind->ownBase(out)->Release());
    else
        drawfx::Note("query release", static_cast<cIGZUnknown *>(out)->Release());
}

// Write over the persist face, its heap balance noted: the name record the base's record allocates is released again.
void WriteCounted(const Builder &b)
{
    long long blocks, bytes;
    {
        difftest::AllocationCounter heap;
        b.persist->Write(WriterFace());
        heap.Stop();
        blocks = heap.Blocks();
        bytes = heap.Bytes();
    }
    drawfx::Note("write heap", {uint32_t(blocks), uint32_t(bytes)});
}

// The bodies called at their RVAs with the static `this` their thunks deliver, the way a caller that holds no derived
// vtable would: a null out (QueryInterface 0xa45310, the persist face), and the pointers the two hidden-out registry slots
// (0xa44c60, 0xa44ee0; `this` unused) hand back.
void DirectQuery(const Builder &b, std::mt19937 &rng)
{
    using Fn = bool(__thiscall *)(void *, uint32_t, void **);
    const uint32_t iid = kIids[rng() % kIidCount];
    void *out = nullptr;
    void **const where = rng() % 2 ? &out : nullptr;
    const bool r = gCtx->Original<Fn>(0xa45310)(b.object + 0xc, iid, where);
    drawfx::Note("direct query", {iid, uint8_t(r), drawfx::Norm(P(out))});
    ReleaseAnswer(b, iid, out);
}

void DirectRegistry(const Builder &b, std::mt19937 &rng)
{
    using IdsFn = nGZEvent::cAttributeIDRange *(__thiscall *)(void *, nGZEvent::cAttributeIDRange *);
    using AttrFn = nGZEvent::cEventAttribute *(__thiscall *)(void *, nGZEvent::cEventAttribute *, uint32_t);
    void *const self = b.object + 0x10;
    nGZEvent::cEventAttribute a{};
    nGZEvent::cEventAttribute *const ra = gCtx->Original<AttrFn>(0xa44ee0)(self, &a, kEvents[rng() % 4]);
    drawfx::Note("direct attribute", {uint8_t(ra == &a), a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C});
    nGZEvent::cAttributeIDRange ids{};
    nGZEvent::cAttributeIDRange *const ri = gCtx->Original<IdsFn>(0xa44c60)(self, &ids);
    drawfx::Note("direct ids", {uint8_t(ri == &ids), drawfx::Norm(P(ids.mCurrent)) != 0, uint32_t(ids.mBegin != nullptr)});
    if (ids.mCurrent) ids.mCurrent->Release();
    if (ids.mBegin) ids.mBegin->Release();
    if (ids.mEnd) ids.mEnd->Release();
}

void Operate(const Builder &b, unsigned op, std::mt19937 &rng)
{
    switch (op) {
    case 0:
    case 1: { // QueryInterface through any face, a null out in some
        const uint32_t iid = kIids[rng() % kIidCount];
        void *out = nullptr;
        void **const where = rng() % 8 ? &out : nullptr;
        bool r;
        switch (rng() % 3) {
        case 0: r = b.base->QueryInterface(iid, where); break;
        case 1: r = b.persist->QueryInterface(iid, where); break;
        default: r = b.OwnBase()->QueryInterface(iid, where); break;
        }
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        ReleaseAnswer(b, iid, out);
        break;
    }
    case 2: // a reference taken and dropped through each face
        switch (rng() % 3) {
        case 0: drawfx::Note("addref/release persist", {b.persist->AddRef(), b.persist->Release()}); break;
        case 1: drawfx::Note("addref/release base", {b.base->AddRef(), b.base->Release()}); break;
        default: drawfx::Note("addref/release own", {b.OwnBase()->AddRef(), b.OwnBase()->Release()}); break;
        }
        break;
    case 3: {
        cPersistTypeInfo info = b.persist->GetTypeInfo();
        sgfx::NoteTypeInfo("type info", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 4:
    case 5: WriteCounted(b); break;
    case 6: {
        const unsigned k = rng() % 3;
        nGZEvent::cIAbstractBehaviorType *const listener =
            k < 2 ? reinterpret_cast<nGZEvent::cIAbstractBehaviorType *>(&w.extra[k]) : nullptr;
        cIGeometryBuilder *const face = rng() % 2 ? b.base : b.OwnBase();
        drawfx::Note("set listener", drawfx::Norm(P(face->SetListener(listener))));
        break;
    }
    case 7:
    case 8: {
        cIGZUnknown *const listener = reinterpret_cast<cIGZUnknown *>(&w.listener[rng() % 2]);
        cIGeometryBuilder *const face = rng() % 2 ? b.base : b.OwnBase();
        if (rng() % 2)
            face->Connect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        else
            face->Disconnect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        break;
    }
    case 9:
    case 10: {
        cIGeometryBuilder *const face = rng() % 2 ? b.base : b.OwnBase();
        if (rng() % 2)
            face->NotifyListeners(kEvents[rng() % 4], rng(), nullptr);
        else
            drawfx::Note("asked", uint8_t(face->AskListeners(kEvents[rng() % 4], rng(), nullptr)));
        break;
    }
    case 11: {
        cIGeometryBuilder *const face = rng() % 2 ? b.base : b.OwnBase();
        face->OnEvent(rng() % 2 ? kConnectionChanged : 0x1234, rng(), nullptr, rng());
        drawfx::Note("active", {face->IsActive(), face->GetEventCount()});
        break;
    }
    case 12: drawfx::Note("on query", uint8_t(b.base->OnQuery(kEvents[rng() % 4], rng(), nullptr))); break;
    case 16: DirectQuery(b, rng); break;
    case 17: DirectRegistry(b, rng); break;
    default: { // 13-15: the event attribute and the registry's ids (slots 12 and 13), through either face
        cIGeometryBuilder *const face = rng() % 2 ? b.base : b.OwnBase();
        const nGZEvent::cEventAttribute a = face->GetEventAttribute(kEvents[rng() % 4]);
        drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        nGZEvent::cAttributeIDRange ids = face->GetEventIds();
        unsigned steps = 0;
        while (steps < 20 && !ids.mCurrent->IsEqual(ids.mEnd)) {
            drawfx::Note("event id", ids.mCurrent->Get());
            ids.mCurrent->Next();
            ++steps;
        }
        drawfx::Note("event ids", {steps, ids.mCurrent->Release(), ids.mBegin->Release(), ids.mEnd->Release()});
        break;
    }
    }
}

// Every row of the hook table (src/hook/tables/cGeometryBuilder.cpp), as the hooks place them.
struct PatchRow {
    uint32_t rva;
    const void *target;
};
using A = cGeometryBuilderAdapters;
const PatchRow *PatchRows(size_t &n)
{
    static const PatchRow rows[] = {
        {0xa45310, reinterpret_cast<const void *>(&A::QueryInterface)},
        {0xa45530, reinterpret_cast<const void *>(&A::Release)},
        {0xa45790, reinterpret_cast<const void *>(&A::Write)},
        {0xa45240, reinterpret_cast<const void *>(&A::OnEvent)},
        {0xa450f0, reinterpret_cast<const void *>(&A::AskListeners)},
        {0xa44fd0, reinterpret_cast<const void *>(&A::Connect)},
        {0xa450a0, reinterpret_cast<const void *>(&A::Disconnect)},
        {0xa452c0, reinterpret_cast<const void *>(&A::GetEventCount)},
        {0xa44c60, reinterpret_cast<const void *>(&A::GetEventIds)},
        {0xa44ee0, reinterpret_cast<const void *>(&A::GetEventAttribute)},
        {0xa45660, reinterpret_cast<const void *>(&A::NotifyListeners)},
        {0xa456c0, reinterpret_cast<const void *>(&A::SetListener)},
    };
    n = sizeof(rows) / sizeof(rows[0]);
    return rows;
}

void Run(difftest::Context &ctx, const Kind &kind)
{
    gCtx = &ctx;
    sgfx::RegistryScope registry(kBuilderRegistry);
    MissingRecordScope missing;
    sgfx::CheckSides(ctx, kind.name, 64, BuildWorld, [&](bool original, std::mt19937 &rng) {
        std::optional<shape::CodePatch> patches[12];
        if (!original) {
            size_t n = 0;
            const PatchRow *rows = PatchRows(n);
            for (size_t i = 0; i < n; ++i) patches[i].emplace(rows[i].rva, rows[i].target);
        }
        const Builder b = Make(kind);
        const Builder other = Make(kKinds[rng() % 4]);
        NoteWords(b);
        for (int k = 0; k < 48; ++k) {
            const unsigned op = rng() % 18;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            Operate(rng() % 5 == 0 ? other : b, op, rng);
            if (k % 8 == 7) {
                NoteWords(b);
                NoteWords(other);
            }
        }
        NoteWords(b);
        WriteCounted(b);
        b.base->SetListener(nullptr);
        other.base->SetListener(nullptr);
        Drop(other);
        Drop(b);
        NoteWorld();
        for (auto &p : patches) p.reset();
    });
    w.manager.Drop();
}

} // namespace

DIFF_TEST(sg_geometry_builder_base_compactor, 0xa45790) { Run(ctx, kKinds[0]); }
DIFF_TEST(sg_geometry_builder_base_bounding, 0xa45790) { Run(ctx, kKinds[1]); }
DIFF_TEST(sg_geometry_builder_base_tangent, 0xa45790) { Run(ctx, kKinds[2]); }
DIFF_TEST(sg_geometry_builder_base_indexed, 0xa45790) { Run(ctx, kKinds[3]); }
