// Differential tests of the geometry builders nGZSceneGraph::cCompactorBuilder and cBoundingVolumeBuilder as whole
// classes (lane QE, scenegraph D2 part 1; src/sg/cCompactorBuilder.h, src/sg/cBoundingVolumeBuilder.h), on our
// cGeometryBuilder base (src/sg/cGeometryBuilder.h).
//
// Each side's builder comes from its own factory: the game's 0xa83930 / 0xa85090 and ours Create. The containers they
// build are ours on both sides (lane PA's cGeometryDataContainer, verified), filled with synthetic geometry through its
// face. The triangle sets the bounding volume builder makes are the geometry library's on both sides: the COM director's
// framework object 0x1ae18d (ours: GetDirectorFrameworkObject) is stood in for, and its GetClassObject (slot 3) logs the
// ids and answers a new triangle set from the library's factory 0x196560, counted for the caller and once more for the
// test. At the end of a side the test drops its references (the containers, then the triangle sets, in the order they
// were made) and notes each answer and the side's net count of heap blocks (difftest::AllocationCounter).
//
// Persistence runs over a tape writer and reader (the persist writer's and reader's slots the builders call), as
// tests/sg_data_list_extension.cpp's.
//
//   sg_compactor_builder_slots        random calls through the builder's three faces (cIGeometryBuilder's behaviour
//   sg_bounding_volume_builder_slots  slots: Connect / Disconnect with listeners on our connection manager, Notify /
//                                     Ask with and without the extra listener SetListener stores, OnEvent, OnQuery, the
//                                     event attribute with the registry's statics in place; QueryInterface over the iids
//                                     through either face; the persist face's type info, Write and Read of a tape the
//                                     builder wrote, its versions and name count changed; GetName; the class's own
//                                     setters and HasName), the object's words dumped every 8 calls and at the end.
//   sg_compactor_builder_build        synthetic containers (1-3 linkages with or without alias lists, groups of each
//                                     linkage and orphans), the per-group reset on or off, the enabled byte cleared in
//                                     some seeds (nothing in the game writes it: poked at +0x15 on both sides), built
//                                     once or twice, then every linkage and group dumped through the container.
//   sg_bounding_volume_builder_build  synthetic containers (positions per linkage with decoy elements, alias lists or
//                                     none, groups of every primitive type and none, bone weights with NaN, ties and
//                                     weights at or under -1, transforms), named-only on or off over case-varied names,
//                                     built once or twice; the bounding and subset meshes dumped (vertices, corners).
//
// Left out, with the original's reasons: groups with no indices for the compactor (0xa83a60 reads the first index of
// every group of a linkage before walking it: an empty group reads past its range); indices past a linkage's alias
// lists or its referenced size, bones past the transforms, weight indices past the first weight element's count, and
// index counts that are not multiples of 3 for the bounding volume builder (each is an unchecked vector index or a read
// past a range on both sides); weight elements with fewer weights than indices (the walk follows the indices).
//
// RVAs these tests reach (status entries cite them): the factories 0xa83930 / 0xa85090, the constructors 0xa837e0 /
// 0xa84b20, the destructors 0xa83870 / 0xa84c10 and deleting destructors 0xa838c0 / 0xa84c80, QueryInterface 0xa83a00 /
// 0xa85f30, Read 0x3b7bd0 / 0xa85f80, GetTypeInfo 0xa84480 / 0xa861f0, Write 0xa84550 / 0xa862d0, Build 0xa839cc +
// 0xa83a60 / 0xa85d46 + 0xa85d50 (with 0xa84dc0, 0xa85120, 0xaeda40), GetName 0xa838f6 / 0xa84d89, SetPerGroupReset and
// SetNamedOnly 0x9e0f30, AddName 0xa84cc0, HasName 0xa85c60. The thunks the slots tests call through each face (ours
// the compiler's, in our vtables): the compactor's QueryInterface 0xa839f1; the bounding volume builder's
// cIGeometryBuilder-face vtordisp thunks QueryInterface 0xa85f1c, Release 0xa861c1, OnEvent 0xa85c3e, NotifyListeners
// 0xa861cc, AskListeners 0xa85080, Connect 0xa84da6, Disconnect 0xa84db1, GetEventCount 0xa85d3b, GetEventIds 0xa84d73,
// GetEventAttribute 0xa84d7e, SetListener 0xa861d7, and its own face's AddName 0xa84cab and HasName 0xa85c49.
#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"

#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/cBoundingVolumeBuilder.h"
#include "sg/cCompactorBuilder.h"
#include "sg/cGeometryDataContainerClass.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using nGZCompGeom::cTriangleSetData;

constexpr uint32_t kMeshFactory = 0x196560;
using FactoryFn = cIGZUnknown *(__cdecl *)();
using MeshFactoryFn = cIGZUnknown *(__cdecl *)();

constexpr uint32_t kContainerTag = 0x6c000000;
constexpr uint32_t kMeshTag = 0x6d000000;
constexpr uint32_t kBuilderTag = 0x6e000000;
constexpr uint32_t kFace = 0x2c; // the container's face in its object

difftest::Context *gCtx;
bool gOriginal;

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
std::string TextOf(const void *str)
{
    return str ? reinterpret_cast<const cRZString *>(str)->ToChar() : std::string("<null>");
}

// ---- the builders: the two classes' facts ----
struct Kind {
    const char *name;
    uint32_t factory;
    FactoryFn ours;
    uint32_t size;
    uint32_t face; // the cIGeometryBuilder subobject's offset
    uint32_t iid;  // the class's own interface
};
const Kind kCompactor{"compactor", 0xa83930, &cCompactorBuilder::Create, 0x2c, 0x1c, cICompactorBuilder::kIid};
const Kind kBounding{"bounding volume", 0xa85090, &cBoundingVolumeBuilder::Create, 0x34, 0x24,
                     cIBoundingVolumeBuilder::kIid};

// ---- the containers and triangle sets a side made ----
std::vector<cIGeometryDataContainer *> gBoxes;
std::vector<cITriangleSetShape *> gMeshes;

uint32_t MeshIndex(const void *mesh)
{
    if (!mesh) return 0xfffffffeu;
    for (size_t i = 0; i < gMeshes.size(); ++i)
        if (gMeshes[i] == mesh) return uint32_t(i);
    return 0xffffu;
}

void Track(cITriangleSetShape *mesh)
{
    difftest::NotCounted quiet;
    drawfx::AddRange(mesh, 0x30, kMeshTag | uint32_t(gMeshes.size()) << 12);
    gMeshes.push_back(mesh);
}

// A new container (ours on both sides), as its face, counted once (the test's reference).
cIGeometryDataContainer *NewBox()
{
    cISGResource *r = cGeometryDataContainer::Create();
    void *out = nullptr;
    r->QueryInterface(cIGeometryDataContainer::kIid, &out);
    auto *face = static_cast<cIGeometryDataContainer *>(out);
    difftest::NotCounted quiet;
    drawfx::AddRange(reinterpret_cast<uint8_t *>(face) - kFace, cGeometryDataContainer::kSize,
                     kContainerTag | uint32_t(gBoxes.size()) << 12);
    gBoxes.push_back(face);
    return face;
}

// ---- the allocator service the triangle sets' vectors use (as tests/sg_geometry_data_class.cpp's) ----
uint32_t __fastcall ServiceNop(void *, void *) { return 0; }
void *__fastcall ServiceAlloc(void *, void *, uint32_t n) { return std::malloc(n ? n : 1); }
void __fastcall ServiceFree(void *, void *, void *p) { std::free(p); }
void *gServiceVTable[5] = {reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
                           reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceAlloc),
                           reinterpret_cast<void *>(&ServiceFree)};
void *gServiceObject = gServiceVTable;

struct ServiceFake {
    void **cache;
    int32_t *guard;
    void *savedCache;
    int32_t savedGuard;
    explicit ServiceFake(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int32_t>(0x10c95d8)), savedCache(*cache),
          savedGuard(*guard)
    {
        *cache = &gServiceObject;
        *guard = INT32_MIN;
    }
    ~ServiceFake()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

// ---- the COM director: new triangle sets, each held once more by the test ----
struct FakeCom {
    virtual uint32_t QueryInterface(uint32_t, void **) { return 0; }
    virtual uint32_t AddRef() { return 1; }
    virtual uint32_t Release() { return 1; }
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **out)
    {
        drawfx::Note("COM", {classId, iid});
        if (classId != cITriangleSetShape::kClassId) {
            *out = nullptr;
            return 0;
        }
        cIGZUnknown *u = gCtx->Original<MeshFactoryFn>(kMeshFactory)();
        const bool ok = u->QueryInterface(iid, out);
        auto *mesh = static_cast<cITriangleSetShape *>(*out);
        mesh->AddRef();
        Track(mesh);
        return ok;
    }
};
FakeCom gCom;
void *__cdecl StandFramework() { return &gCom; }

// A triangle set: its identity (the test's list), its word +0x2c, its vertices and corners.
void NoteMesh(const char *label, cITriangleSetShape *mesh)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(MeshIndex(mesh));
    if (mesh) {
        c.words.push_back(*reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(mesh) + 0x2c));
        const cTriangleSetData *data = mesh->GetTriangleSet();
        c.words.push_back(uint32_t(data->mVertexEnd - data->mVertexBegin));
        c.words.push_back(uint32_t(data->mIndexEnd - data->mIndexBegin));
        for (const auto *v = data->mVertexBegin; v != data->mVertexEnd; ++v)
            c.words.insert(c.words.end(), {Bits(v->x), Bits(v->y), Bits(v->z)});
        for (const uint16_t *i = data->mIndexBegin; i != data->mIndexEnd; ++i) c.words.push_back(*i);
    }
    drawfx::gLog.push_back(std::move(c));
}

// ---- a tape writer and reader ----
struct Token {
    char kind; // 'w' a word, 't' a text, 'i' a type-info record
    std::vector<uint32_t> words;
    std::string text;
};
std::vector<Token> gTape;

void Put(const char *kind, Token t)
{
    drawfx::Call c;
    c.note = kind;
    c.words = t.words;
    c.text = t.text;
    drawfx::gLog.push_back(std::move(c));
    difftest::NotCounted quiet;
    gTape.push_back(std::move(t));
}

#define SGGB_BAD(n) \
    virtual uint32_t Bad##n() { drawfx::Note("bad slot " #n, n); return 0; }

struct TapeWriter {
    SGGB_BAD(0) SGGB_BAD(1) SGGB_BAD(2) SGGB_BAD(3)
    virtual uint32_t SetUint8(uint32_t v) { Put("w SetUint8", {'w', {v & 0xff}, {}}); return 1; } // 4
    SGGB_BAD(5) SGGB_BAD(6) SGGB_BAD(7) SGGB_BAD(8) SGGB_BAD(9)
    virtual uint32_t SetUint32(uint32_t v) { Put("w SetUint32", {'w', {v}, {}}); return 1; } // 10
    SGGB_BAD(11) SGGB_BAD(12) SGGB_BAD(13) SGGB_BAD(14) SGGB_BAD(15) SGGB_BAD(16) SGGB_BAD(17) SGGB_BAD(18)
    SGGB_BAD(19) SGGB_BAD(20) SGGB_BAD(21) SGGB_BAD(22)
    virtual uint32_t WriteRZCharStr(const char *text) // 23
    {
        Put("w WriteRZCharStr", {'t', {}, text ? text : "<null>"});
        return 1;
    }
    SGGB_BAD(24) SGGB_BAD(25) SGGB_BAD(26) SGGB_BAD(27)
    virtual uint32_t WriteTypeInfo(const cPersistTypeInfo *info) // 28
    {
        Put("w WriteTypeInfo", {'i', {info->mWord4, info->mVersion}, TextOf(info->mName)});
        return 1;
    }
};
TapeWriter gWriter;

// The reader answers from gTape in order, one token a call whatever its kind. ReadTypeInfo answers a name fake,
// counted (each noted at the end).
Fake gNames[4];
unsigned gNameAt;
struct TapeReader {
    size_t next = 0;
    const Token *Take(const char *kind)
    {
        drawfx::Note(kind, uint32_t(next));
        return next < gTape.size() ? &gTape[next++] : nullptr;
    }
    static uint32_t WordOf(const Token *t) { return t && !t->words.empty() ? t->words[0] : 0; }
    SGGB_BAD(0) SGGB_BAD(1) SGGB_BAD(2) SGGB_BAD(3)
    virtual bool GetBool() { return WordOf(Take("r GetBool")) != 0; } // 4
    SGGB_BAD(5) SGGB_BAD(6) SGGB_BAD(7) SGGB_BAD(8) SGGB_BAD(9)
    virtual uint32_t GetUint32() { return WordOf(Take("r GetUint32")); } // 10
    SGGB_BAD(11) SGGB_BAD(12) SGGB_BAD(13) SGGB_BAD(14) SGGB_BAD(15) SGGB_BAD(16) SGGB_BAD(17) SGGB_BAD(18)
    SGGB_BAD(19) SGGB_BAD(20) SGGB_BAD(21)
    virtual bool GetGZStr(cIGZString *str) // 22
    {
        const Token *t = Take("r GetGZStr");
        std::string text;
        {
            difftest::NotCounted quiet;
            text = t ? t->text : std::string("tail");
        }
        str->FromChar(text.c_str());
        return t != nullptr;
    }
    SGGB_BAD(23) SGGB_BAD(24) SGGB_BAD(25)
    virtual bool ReadTypeInfo(cPersistTypeInfo *out) // 26
    {
        const Token *t = Take("r ReadTypeInfo");
        Fake &name = gNames[gNameAt++ % 4];
        ++name.refs;
        out->mName = reinterpret_cast<cIGZUnknown *>(&name);
        out->mWord4 = t && t->words.size() > 0 ? t->words[0] : 0;
        out->mVersion = t && t->words.size() > 1 ? t->words[1] : 0;
        return t != nullptr;
    }
};
TapeReader gReader;

#undef SGGB_BAD

ifaces::cIGZPersistWriter *Writer() { return reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter); }
ifaces::cIGZPersistReader *Reader() { return reinterpret_cast<ifaces::cIGZPersistReader *>(&gReader); }

void ClearTape()
{
    difftest::NotCounted quiet;
    gTape.clear();
    gReader.next = 0;
}

// ---- the world ----
struct World {
    Fake listener[2]; // ids 500, 501: connected through the manager
    Fake extra[2];    // ids 510, 511: the extra listener SetListener stores
    sgfx::Manager manager;
};
World w;

constexpr sgfx::RegistryStatics kBuilderRegistry{0x10f60e8, 0x10f60f0, 0x10f60f8, 0x10f6100};
constexpr uint32_t kConnectionChanged = 0x5b51e806;
const uint32_t kEvents[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[1], sgfx::kRegistryIds[2], 0x5678};

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
    for (unsigned i = 0; i < 4; ++i) gNames[i].Reset<sgfx::kUnknown>(440 + i);
    gNameAt = 0;
    ClearTape();
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
    for (const Fake &n : gNames) drawfx::Note("type name refs", {n.id, uint32_t(n.refs)});
}

// ---- a side's begin and end ----
struct SideScope {
    difftest::AllocationCounter counter;
    SideScope(difftest::Context &ctx, bool original)
    {
        gCtx = &ctx;
        gOriginal = original;
        difftest::NotCounted quiet;
        gBoxes.clear();
        gMeshes.clear();
    }
    void End()
    {
        for (cIGeometryDataContainer *f : gBoxes) drawfx::Note("container released", f->Release());
        for (cITriangleSetShape *m : gMeshes) drawfx::Note("triangle set released", m->Release());
        NoteWorld();
        drawfx::Note("blocks left", uint32_t(counter.Blocks()));
    }
};

// ---- one builder of the side being run, as its three faces (the test holds one reference on each) ----
struct Builder {
    const Kind *kind = nullptr;
    cIGeometryBuilder *base = nullptr;
    cIGZPersistSerializable2 *persist = nullptr;
    void *own = nullptr; // the class's own interface (slots of its own first, its vbptr at +4)
    uint8_t *object = nullptr;

    cICompactorBuilder *Compactor() const { return static_cast<cICompactorBuilder *>(own); }
    cIBoundingVolumeBuilder *Bounding() const { return static_cast<cIBoundingVolumeBuilder *>(own); }
    // The own face's references go through its virtual base (the face has no IUnknown slots of its own).
    cIGeometryBuilder *OwnBase() const
    {
        return kind == &kCompactor ? static_cast<cIGeometryBuilder *>(Compactor())
                                   : static_cast<cIGeometryBuilder *>(Bounding());
    }
};

Builder Make(const Kind &kind)
{
    Builder b;
    b.kind = &kind;
    cIGZUnknown *const made = gOriginal ? gCtx->Original<FactoryFn>(kind.factory)() : kind.ours();
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

// The object's words that are not vtables: the count, the extra listener, the vbtables' entries, the vtordisps, the
// class's own fields.
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
    if (b.kind == &kCompactor) {
        c.words.push_back(b.object[0x14]);
        c.words.push_back(b.object[0x15]);
    } else {
        c.words.push_back(word(0x18)); // the name set's size
        c.words.push_back(b.object[0x1c]);
    }
    drawfx::gLog.push_back(std::move(c));
}

const char *const kNames[] = {"body", "BODY", "Body", "hair_lod0", "", "tops", "a_group_name_longer_than_sixteen", "x"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);

// The bounding volume builder's names through HasName.
void NoteNames(const Builder &b)
{
    if (b.kind != &kBounding) return;
    drawfx::Call c;
    c.note = "has names";
    for (const char *name : kNames) c.words.push_back(uint8_t(b.Bounding()->HasName(name)));
    c.words.push_back(uint8_t(b.Bounding()->HasName("HAIR_LOD0")));
    c.words.push_back(uint8_t(b.Bounding()->HasName("hair")));
    drawfx::gLog.push_back(std::move(c));
}

void Dump(const Builder &b)
{
    NoteWords(b);
    NoteNames(b);
}

// A tape the builder writes, read back by `into` (the type-info versions and the name count changed in some seeds).
void WriteRead(const Builder &from, const Builder &into, std::mt19937 &rng)
{
    ClearTape();
    from.persist->Write(Writer());
    drawfx::Note("tape", uint32_t(gTape.size()));
    {
        difftest::NotCounted quiet;
        const uint32_t versions[] = {0, 1, 2, 3, 0xffffffffu};
        if (rng() % 3 == 0 && !gTape.empty()) gTape[0].words[1] = versions[rng() % 5];
        if (rng() % 4 == 0 && gTape.size() > 1) gTape[1].words[1] = versions[rng() % 5];
        if (rng() % 5 == 0 && gTape.size() > 3) gTape[3].words[0] = rng() % 2 ? 0x80000000u : 0xffffffffu;
        if (rng() % 6 == 0 && gTape.size() > 3) gTape[3].words[0] += 1 + rng() % 2; // past the tape: "tail" names
        if (rng() % 6 == 0) gTape.resize(gTape.size() > 0 ? rng() % gTape.size() : 0); // a short tape
    }
    into.persist->Read(Reader());
    drawfx::Note("read to", uint32_t(gReader.next));
}

const uint32_t kIids[] = {0,          1,          cGeometryBuilder::kIidSerializable,   cGeometryBuilder::kIidBehavior,
                          0xfb859324, 0xbbb79c90, cICompactorBuilder::kIid, cIBoundingVolumeBuilder::kIid, 0x12345678};

void Operate(const Builder &b, const Builder &other, unsigned op, std::mt19937 &rng)
{
    switch (op) {
    case 0: { // QueryInterface through either face, a null out in some
        const uint32_t iid = kIids[rng() % 9];
        void *out = nullptr;
        void **const where = rng() % 8 ? &out : nullptr;
        const bool r = rng() % 2 ? b.base->QueryInterface(iid, where) : b.persist->QueryInterface(iid, where);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (!out || iid == 0xfb859324) break;
        if (iid == cICompactorBuilder::kIid)
            static_cast<cIGeometryBuilder *>(static_cast<cICompactorBuilder *>(out))->Release();
        else if (iid == cIBoundingVolumeBuilder::kIid)
            static_cast<cIGeometryBuilder *>(static_cast<cIBoundingVolumeBuilder *>(out))->Release();
        else
            drawfx::Note("query release", static_cast<cIGZUnknown *>(out)->Release());
        break;
    }
    case 1:
        drawfx::Note("addref", b.persist->AddRef());
        drawfx::Note("release", b.base->Release());
        break;
    case 2: {
        cPersistTypeInfo info = b.persist->GetTypeInfo();
        sgfx::NoteTypeInfo("type info", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 3:
        ClearTape();
        b.persist->Write(Writer());
        break;
    case 4: WriteRead(rng() % 2 ? other : b, b, rng); break;
    case 5: sgfx::NoteText("name", b.base->GetName()); break;
    case 6: {
        const unsigned k = rng() % 3;
        nGZEvent::cIAbstractBehaviorType *const listener =
            k < 2 ? reinterpret_cast<nGZEvent::cIAbstractBehaviorType *>(&w.extra[k]) : nullptr;
        drawfx::Note("set listener", drawfx::Norm(P(b.base->SetListener(listener))));
        break;
    }
    case 7: {
        cIGZUnknown *const listener = reinterpret_cast<cIGZUnknown *>(&w.listener[rng() % 2]);
        if (rng() % 2)
            b.base->Connect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        else
            b.base->Disconnect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        break;
    }
    case 8:
        if (rng() % 2)
            b.base->NotifyListeners(kEvents[rng() % 4], rng(), nullptr);
        else
            drawfx::Note("asked", uint8_t(b.base->AskListeners(kEvents[rng() % 4], rng(), nullptr)));
        break;
    case 9:
        b.base->OnEvent(rng() % 2 ? kConnectionChanged : 0x1234, rng(), nullptr, rng());
        drawfx::Note("active", {b.base->IsActive(), b.base->GetEventCount()});
        break;
    case 10: drawfx::Note("on query", uint8_t(b.base->OnQuery(kEvents[rng() % 4], rng(), nullptr))); break;
    case 11: {
        const nGZEvent::cEventAttribute a = b.base->GetEventAttribute(kEvents[rng() % 4]);
        drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        // The registry's ids (slot 13): each id in order, then the three iterators' Release answers.
        nGZEvent::cAttributeIDRange ids = b.base->GetEventIds();
        unsigned steps = 0;
        while (steps < 20 && !ids.mCurrent->IsEqual(ids.mEnd)) {
            drawfx::Note("event id", ids.mCurrent->Get());
            ids.mCurrent->Next();
            ++steps;
        }
        drawfx::Note("event ids", {steps, ids.mCurrent->Release(), ids.mBegin->Release(), ids.mEnd->Release()});
        break;
    }
    default: // 12-15: the class's own
        if (b.kind == &kCompactor) {
            b.Compactor()->SetPerGroupReset(rng() % 2 != 0);
        } else if (op == 12 || op == 13) {
            b.Bounding()->AddName(kNames[rng() % kNameCount]);
        } else if (op == 14) {
            b.Bounding()->SetNamedOnly(rng() % 2 != 0);
        } else {
            drawfx::Note("has name", uint8_t(b.Bounding()->HasName(kNames[rng() % kNameCount])));
        }
        break;
    }
}

void RunSlots(difftest::Context &ctx, const Kind &kind)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    MissingRecordScope missing;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, kind.name, 48, BuildWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        const Builder b = Make(kind);
        const Builder other = Make(rng() % 2 ? kCompactor : kBounding);
        if (kind.iid == kBounding.iid)
            for (unsigned k = rng() % 5; k; --k) b.Bounding()->AddName(kNames[rng() % kNameCount]);
        if (other.kind == &kBounding)
            for (unsigned k = rng() % 5; k; --k) other.Bounding()->AddName(kNames[rng() % kNameCount]);
        Dump(b);
        for (int k = 0; k < 48; ++k) {
            const unsigned op = rng() % 16;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            Operate(b, other, op, rng);
            if (k % 8 == 7) Dump(b);
        }
        Dump(b);
        ClearTape();
        b.persist->Write(Writer());
        b.base->SetListener(nullptr);
        Drop(other);
        Drop(b);
        side.End();
    });
    w.manager.Drop();
}

// ---- synthetic geometry ----
float SmallFloat(std::mt19937 &rng)
{
    const int32_t i = int32_t(rng() % 41) - 20;
    return rng() % 2 ? float(i) : float(i) * 0.25f + 0.125f;
}

std::vector<uint16_t> Words(uint32_t n, uint32_t below, std::mt19937 &rng)
{
    std::vector<uint16_t> v(n);
    for (uint16_t &x : v) x = uint16_t(rng() % below);
    return v;
}

// A range's values from its cursor to its end, then what each Release answers.
void NoteRange(const char *label, cGeometryRange<uint16_t> &r)
{
    drawfx::Call c;
    c.note = label;
    uint32_t n = 0;
    while (!r.mCurrent->IsEqual(r.mEnd) && n < 512) {
        c.words.push_back(r.mCurrent->Get());
        r.mCurrent->Next();
        ++n;
    }
    c.words.push_back(n);
    c.words.push_back(r.mCurrent->Release());
    c.words.push_back(r.mBegin->Release());
    c.words.push_back(r.mEnd->Release());
    drawfx::gLog.push_back(std::move(c));
}

// Linkages (headers, the three alias lists) and groups (info, vertex indices) through the container.
void DumpLinkagesAndGroups(cIGeometryDataContainer *f)
{
    const uint32_t nl = f->CountLinkages(), ng = f->CountGroups();
    drawfx::Note("counts", {nl, ng});
    for (uint32_t i = 0; i < nl; ++i) {
        cGeometryLinkageHeader h{};
        f->GetLinkageHeader(&h, i);
        drawfx::Note("linkage", {h.mIndex, h.mActiveCount, h.mReferencedSize});
        for (uint32_t which = 0; which < 3; ++which) {
            cGeometryRange<uint16_t> r;
            f->GetLinkageAliases(&r, i, which);
            NoteRange("aliases", r);
        }
    }
    for (uint32_t i = 0; i < ng; ++i) {
        cGeometryGroupInfo g{};
        f->GetGroupInfo(&g, i);
        drawfx::Note("group", {g.mIndex, g.mPrimType, g.mIndexCount, g.mLinkIndex, uint32_t(g.mWord10), g.mBoneCount});
        cGeometryRange<uint16_t> r;
        f->GetGroupVertexIndices(&r, i);
        NoteRange("group indices", r);
    }
}

// The compactor's input: 0-3 linkages (a referenced size, each alias list present or not), groups of each linkage with
// 1-12 indices below its size, and groups of a linkage that is not there.
void MakeCompactorGeometry(cIGeometryDataContainer *f, std::mt19937 &rng)
{
    const uint32_t links = rng() % 8 == 0 ? 0 : 1 + rng() % 3;
    std::vector<uint32_t> sizes;
    for (uint32_t l = 0; l < links; ++l) {
        f->AddLinkage();
        const uint32_t size = 1 + rng() % 12;
        sizes.push_back(size);
        f->SetLinkageReferencedSize(l, size);
        for (uint32_t which = 0; which < 3; ++which) {
            if (rng() % 3 == 0) continue;
            const std::vector<uint16_t> list = Words(size + rng() % 3, 60000, rng);
            f->SetLinkageAliases(l, which, list.data(), uint32_t(list.size()));
        }
    }
    const uint32_t groups = 1 + rng() % 6;
    for (uint32_t g = 0; g < groups; ++g) {
        const uint32_t link = rng() % 8 == 0 ? links + rng() % 2 : (links ? rng() % links : 0);
        f->AddGroup(rng() % 4, kNames[rng() % kNameCount], link, int32_t(rng() % 4));
        const uint32_t below = link < links ? sizes[link] : 4;
        // Indices clustered in part of the range, so groups share some vertices and leave others unused.
        const uint32_t count = 1 + rng() % 12;
        std::vector<uint16_t> list = Words(count, below, rng);
        if (link >= links && rng() % 2) continue; // an orphan group may stay empty (nothing reads it)
        f->SetGroupVertexIndices(g, list.data(), count);
    }
}

void RunCompactorBuild(difftest::Context &ctx)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "compactor build", 96, BuildWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        cIGeometryDataContainer *f = NewBox();
        MakeCompactorGeometry(f, rng);
        DumpLinkagesAndGroups(f);
        const Builder b = Make(kCompactor);
        if (rng() % 3 == 0) b.Compactor()->SetPerGroupReset(false);
        if (rng() % 8 == 0) b.object[0x15] = 0; // the enabled byte: nothing in the game clears it
        NoteWords(b);
        const unsigned builds = 1 + rng() % 2;
        for (unsigned k = 0; k < builds; ++k) {
            sgfx::MarkStep(ctx, k ? "second build" : "build");
            b.base->Build(f);
            DumpLinkagesAndGroups(f);
        }
        Drop(b);
        side.End();
    });
    w.manager.Drop();
}

constexpr uint32_t kPosition = 0x5b830781;
constexpr uint32_t kWeights = 0x5c4afc5c;

float Weight(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return std::numeric_limits<float>::quiet_NaN();
    case 1: return -1.0f;
    case 2: return -2.0f;
    case 3: return 0.5f; // ties between bones
    default: return float(rng() % 1000) / 1000.0f;
    }
}

void AddVec3Element(cIGeometryDataContainer *f, uint32_t identity, uint32_t repetition, uint32_t blockFormat,
                    uint32_t setFormat, uint32_t count, std::mt19937 &rng, uint16_t *index)
{
    const uint32_t e = f->AddElement(identity, repetition, blockFormat, setFormat);
    std::vector<float> data(count * 3);
    for (float &x : data) x = SmallFloat(rng);
    f->SetElementData(e, count, data.data(), uint32_t(data.size() * 4), nullptr, 0);
    if (index) *index = uint16_t(e);
}

// The bounding volume builder's input: `nv` vertices per linkage (a position element each, repetition 0, block format
// 2, a set format 0-2, decoys of the same identity around it), alias lists or none (values below nv), groups of every
// primitive type (indices below nv, counts multiples of 3, none in some), bone weights in some seeds (a weight element
// per bone, the indices below nv; decoys of other formats), and transforms (at least one when there are weights).
void MakeBoundingGeometry(cIGeometryDataContainer *f, std::mt19937 &rng)
{
    const uint32_t nv = 3 + rng() % 10;
    const bool weights = rng() % 2 != 0;
    const uint32_t transforms = weights ? 1 + rng() % 4 : rng() % 3;
    for (uint32_t t = 0; t < transforms; ++t) {
        cGeometryTransform tr;
        for (float &x : tr.mTranslation) x = SmallFloat(rng);
        for (float &x : tr.mRotation) x = SmallFloat(rng) * 0.125f;
        if (rng() % 3 == 0) tr.mRotation[0] = tr.mRotation[1] = tr.mRotation[2] = 0.0f, tr.mRotation[3] = 1.0f;
        f->AddTransform(&tr);
    }
    const uint32_t links = 1 + rng() % 2;
    for (uint32_t l = 0; l < links; ++l) {
        f->AddLinkage();
        f->SetLinkageReferencedSize(l, nv);
        if (rng() % 2) { // a decoy first: another repetition (block format 2), or another block format
            uint16_t decoy;
            const bool otherRepetition = rng() % 2 != 0;
            AddVec3Element(f, kPosition, otherRepetition ? 1 + rng() % 2 : 0, otherRepetition ? 2 : 1 + 2 * (rng() % 2),
                           0, nv, rng, &decoy);
            f->AddLinkageElement(l, decoy);
        }
        if (rng() % 6) {
            uint16_t position;
            AddVec3Element(f, kPosition, 0, 2, rng() % 3, nv, rng, &position);
            f->AddLinkageElement(l, position);
        }
        if (rng() % 3 == 0) { // a later position element the search never reaches
            uint16_t later;
            AddVec3Element(f, kPosition, 0, 2, rng() % 3, nv, rng, &later);
            f->AddLinkageElement(l, later);
        }
        for (uint32_t which = 0; which < 3; ++which) {
            if (rng() % 2) continue;
            const std::vector<uint16_t> list = Words(nv, nv, rng);
            f->SetLinkageAliases(l, which, list.data(), nv);
        }
    }
    if (weights) {
        bool first = true;
        const uint32_t elements = 1 + rng() % 4;
        for (uint32_t k = 0; k < elements; ++k) {
            const bool decoy = rng() % 5 == 0;
            const uint32_t decoyKind = 1 + rng() % 3; // a decoy's block format, set format or both not 0
            const uint32_t e = f->AddElement(kWeights, rng() % transforms, decoy && (decoyKind & 1) ? 1 : 0,
                                             decoy && (decoyKind & 2) ? 1 : 0);
            const bool qualifies = !decoy;
            const uint32_t m = 1 + rng() % (nv + 2);
            std::vector<uint16_t> indices = Words(m, nv, rng);
            std::vector<float> values(m + rng() % 2);
            for (float &x : values) x = Weight(rng);
            const uint32_t number = qualifies && first ? nv : (rng() % 2 ? nv : 1 + rng() % nv);
            if (qualifies) first = false;
            f->SetElementData(e, number, values.data(), uint32_t(values.size() * 4), indices.data(), m);
        }
        // A weight element that qualifies, so the tables are sized to nv (a decoy's count is not read).
        if (first) {
            const uint32_t e = f->AddElement(kWeights, rng() % transforms, 0, 0);
            std::vector<uint16_t> indices = Words(nv, nv, rng);
            std::vector<float> values(nv);
            for (float &x : values) x = Weight(rng);
            f->SetElementData(e, nv, values.data(), uint32_t(values.size() * 4), indices.data(), nv);
        }
    }
    const uint32_t groups = 1 + rng() % 5;
    for (uint32_t g = 0; g < groups; ++g) {
        const uint32_t prim = rng() % 5 ? 2 : rng() % 4;
        f->AddGroup(prim, kNames[rng() % kNameCount], rng() % links, int32_t(rng() % 4));
        const uint32_t count = rng() % 6 == 0 ? 0 : 3 * (1 + rng() % 4);
        if (count == 0) continue;
        const std::vector<uint16_t> list = Words(count, nv, rng);
        f->SetGroupVertexIndices(g, list.data(), count);
    }
}

void DumpMeshes(cIGeometryDataContainer *f)
{
    NoteMesh("bounding", f->GetBoundingMesh());
    const uint32_t n = f->CountTransforms();
    for (uint32_t i = 0; i <= n; ++i) NoteMesh("subset", f->GetSubsetMesh(i));
}

void RunBoundingBuild(difftest::Context &ctx)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    ServiceFake service(ctx);
    sgfx::CheckSides(ctx, "bounding volume build", 128, BuildWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        cIGeometryDataContainer *f = NewBox();
        MakeBoundingGeometry(f, rng);
        const Builder b = Make(kBounding);
        for (unsigned k = rng() % 4; k; --k) b.Bounding()->AddName(kNames[rng() % kNameCount]);
        if (rng() % 3 == 0) b.Bounding()->SetNamedOnly(true);
        NoteWords(b);
        const unsigned builds = 1 + (rng() % 4 == 0);
        for (unsigned k = 0; k < builds; ++k) {
            sgfx::MarkStep(ctx, k ? "second build" : "build");
            b.base->Build(f);
            DumpMeshes(f);
        }
        if (rng() % 4 == 0) { // a container without linkages or groups: nothing happens
            cIGeometryDataContainer *empty = NewBox();
            if (rng() % 2) empty->AddLinkage();
            b.base->Build(empty);
            DumpMeshes(empty);
        }
        Drop(b);
        side.End();
    });
    w.manager.Drop();
}

} // namespace

DIFF_TEST(sg_compactor_builder_slots, 0xa83930) { RunSlots(ctx, kCompactor); }

DIFF_TEST(sg_bounding_volume_builder_slots, 0xa85090) { RunSlots(ctx, kBounding); }

DIFF_TEST(sg_compactor_builder_build, 0xa83a60) { RunCompactorBuild(ctx); }

DIFF_TEST(sg_bounding_volume_builder_build, 0xa85d50) { RunBoundingBuild(ctx); }
