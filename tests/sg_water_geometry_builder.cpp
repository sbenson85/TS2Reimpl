// Differential tests of nTSSG::cWaterGeometryBuilder as a whole class (lane 261108f, scenegraph LT1b;
// src/sg/cWaterGeometryBuilder.h), on our cGeometryBuilder base. The fixtures follow tests/sg_geometry_builders.cpp (the
// allocator service, the COM director's stand-in answering the geometry library's triangle sets, the tape writer and
// reader, the listeners and the event manager).
//
// Each side's builder is made as cTerrain makes it: the original's 0x150 bytes from the image's operator new 0x824632 and
// its constructor 0x3b5970 (mostDerived 1), ours with new. The test holds one reference (through the cIGeometryBuilder
// face) and its last Release runs the deleting destructor.
//
//   sg_water_builder_slots  random calls through the builder's faces, as sg_geometry_builders' slots tests: Connect /
//                           Disconnect, Notify / Ask, OnEvent, OnQuery, the event attribute and ids, SetListener,
//                           QueryInterface over the iids through either face, the persist face's type info, Write and Read
//                           of a tape the builder wrote (versions changed, short tapes), GetName, slots 7 and 10 (one
//                           empty body), and Build of a builder never set up into a recording container (no vertices, no
//                           shape).
//   sg_water_builder_grid   a terrain fake (cTerrain's words +0x3c, the grid, and +0x68..+0x74, the lot; slot 13
//                           RedrawRegion), its grid fake (+0x10: the vertices GridHeight 0x3b58c0 reads), the placement
//                           service in its cell 0x10cc298 (slot 35, the terrain painter: slots 18, 22, 23, 27), the depth
//                           curve 0xc0a6a1 and cTerrain's lot-subset function 0x297ad0 stood in for (the latter logs and
//                           calls the side's SetTriangleSubset with case-varied names); grids of 1-4 x 1-4 tiles, 1-3 paint
//                           subdivisions, the paint thresholds set per seed (their .data defaults changed in some). Random
//                           SetVertexHeight (-0, unchanged and repeated heights included), GrowDirtyRect, Update,
//                           SetTriangleSubset, RemoveTriangle, IsTriangleDry, GetVertexHeight, ClearSubsetsChanged, terrain
//                           heights moved under the water, Setup again (the other terrain or the same, other sizes), the
//                           collision flag cleared in some seeds (nothing in the game clears it: poked at +0xd9), Build into
//                           a recording container; the builder's state dumped every 8 steps and at the end (vertices,
//                           normals, colours, depths, texture coordinates, triangle subsets, each subset's name, count,
//                           triangles and corners, the name map, the refit set, the flags, sizes and tables, the collision
//                           mesh's vertices and corners), and the fakes' counts.
//
// Left out, with the original's reasons: a width or height of 0 (BuildCollision's unsigned loops never end: an
// ORIGINAL-BUG in the source), vertices or tiles outside the grid (unchecked vector indices on both sides), the
// generator at +0x68 (seeded from the clock, never drawn from: cRZRandom's constructor is stood in on both sides and the
// seed it is given logged), and the fields the constructor leaves for Setup (the sizes, tables, layers) before Setup.
//
// RVAs these tests reach (status entries cite them): the constructor 0x3b5970, the destructor 0x3b5b80 and the deleting
// destructor 0x3b5f70, cSubset's deleting destructor 0x3b5e60, GetTypeInfo 0x3b94e0, Write 0x3b95c0, GetName 0x3b63b6,
// Build 0x3b75b4 + 0x3b75c0, Clear 0x3b8e30, Setup 0x3b6c50, Update 0x3b7850, SetVertexHeight 0x3b9140, SetTriangleSubset
// 0x3b9060, RemoveTriangle 0x3b6510, IsTriangleDry 0x3b7080, GrowDirtyRect 0x3b7110, ClearSubsetsChanged 0x3b7190,
// GetVertexHeight 0x3b8e10, and the helpers 0x3b5fb0, 0x3b6240, 0x3b63d0, 0x3b6450, 0x3b6470, 0x3b6550, 0x3b65b0,
// 0x3b6610, 0x3b6650, 0x3b66f0, 0x3b67e0, 0x3b6840, 0x3b6940, 0x3b6a00, 0x3b6bd0, 0x3b71a0, 0x3b71e0, 0x3b8d80,
// 0x3b8fe0, 0x3b90f0, 0x3b93a0, 0x3b93d0. The cIGeometryBuilder face's vtordisp thunks the slots test calls (ours the
// compiler's, in our vtables): 0x3b7ba5, 0x3b5f93, 0x3b8d63, 0x3b6920, 0x3b692e, 0x3b8fc8, 0x3b663e, 0x3b7bb3, 0x3b6502,
// 0x3b6542, 0x3b71d0, 0x3b639a, 0x3b63a8, 0x3b904d.
#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"

#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cWaterGeometryBuilder.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using nGZCompGeom::cTriangleSetData;
using nTSSG::cWaterGeometryBuilder;
using Coord = cWaterGeometryBuilder::TriangleCoord;
using Vec3 = nGZCompGeom::Vec3;

constexpr uint32_t kMeshFactory = 0x196560;
using MeshFactoryFn = cIGZUnknown *(__cdecl *)();
using GameNewFn = void *(__cdecl *)(size_t);
using CtorFn = void *(__thiscall *)(void *, uint32_t);
using SetupFn = void(__thiscall *)(void *, uint32_t, uint32_t, void *);
using UpdateFn = void(__thiscall *)(void *);
using SetVertexHeightFn = void(__thiscall *)(void *, uint32_t, uint32_t, float);
using SetTriangleSubsetFn = void(__thiscall *)(void *, const Coord *, const char *);
using RemoveTriangleFn = void(__thiscall *)(void *, const Coord *);
using IsTriangleDryFn = uint32_t(__thiscall *)(void *, Coord);
using GrowDirtyRectFn = void(__thiscall *)(void *, int32_t, int32_t);
using ClearSubsetsChangedFn = void(__thiscall *)(void *);
using GetVertexHeightFn = float(__thiscall *)(void *, uint32_t, uint32_t);

constexpr uint32_t kSize = 0x150;
constexpr uint32_t kFace = 0x14c;    // the cIGeometryBuilder virtual base
constexpr uint32_t kPersist = 0x0c;  // the cIGZPersistSerializable2 face
constexpr uint32_t kMeshTag = 0x6d000000;
constexpr uint32_t kBuilderTag = 0x6e000000;

difftest::Context *gCtx;
bool gOriginal;
uint8_t *gMade;   // the builder being constructed (the generator's stand-in logs its offset)
uint8_t *gObject; // the side's builder (the stand-in for 0x297ad0 calls its SetTriangleSubset)

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
void Push(drawfx::Call &&c)
{
    difftest::NotCounted quiet;
    drawfx::gLog.push_back(std::move(c));
}

// ---- the triangle sets a side made ----
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

// ---- the allocator service the triangle sets' vectors and the refit set use ----
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
    Push(std::move(c));
}

// ---- a tape writer and reader (the persist writer's and reader's slots the builders call) ----
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
    Push(std::move(c));
    difftest::NotCounted quiet;
    gTape.push_back(std::move(t));
}

#define SGWB_BAD(n) \
    virtual uint32_t Bad##n() { drawfx::Note("bad slot " #n, n); return 0; }

struct TapeWriter {
    SGWB_BAD(0) SGWB_BAD(1) SGWB_BAD(2) SGWB_BAD(3)
    virtual uint32_t SetUint8(uint32_t v) { Put("w SetUint8", {'w', {v & 0xff}, {}}); return 1; } // 4
    SGWB_BAD(5) SGWB_BAD(6) SGWB_BAD(7) SGWB_BAD(8) SGWB_BAD(9)
    virtual uint32_t SetUint32(uint32_t v) { Put("w SetUint32", {'w', {v}, {}}); return 1; } // 10
    SGWB_BAD(11) SGWB_BAD(12) SGWB_BAD(13) SGWB_BAD(14) SGWB_BAD(15) SGWB_BAD(16) SGWB_BAD(17) SGWB_BAD(18)
    SGWB_BAD(19) SGWB_BAD(20) SGWB_BAD(21) SGWB_BAD(22)
    virtual uint32_t WriteRZCharStr(const char *text) // 23
    {
        Put("w WriteRZCharStr", {'t', {}, text ? text : "<null>"});
        return 1;
    }
    SGWB_BAD(24) SGWB_BAD(25) SGWB_BAD(26) SGWB_BAD(27)
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
    SGWB_BAD(0) SGWB_BAD(1) SGWB_BAD(2) SGWB_BAD(3)
    virtual bool GetBool() { return WordOf(Take("r GetBool")) != 0; } // 4
    SGWB_BAD(5) SGWB_BAD(6) SGWB_BAD(7) SGWB_BAD(8) SGWB_BAD(9)
    virtual uint32_t GetUint32() { return WordOf(Take("r GetUint32")); } // 10
    SGWB_BAD(11) SGWB_BAD(12) SGWB_BAD(13) SGWB_BAD(14) SGWB_BAD(15) SGWB_BAD(16) SGWB_BAD(17) SGWB_BAD(18)
    SGWB_BAD(19) SGWB_BAD(20) SGWB_BAD(21)
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
    SGWB_BAD(23) SGWB_BAD(24) SGWB_BAD(25)
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

ifaces::cIGZPersistWriter *Writer() { return reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter); }
ifaces::cIGZPersistReader *Reader() { return reinterpret_cast<ifaces::cIGZPersistReader *>(&gReader); }

void ClearTape()
{
    difftest::NotCounted quiet;
    gTape.clear();
    gReader.next = 0;
}

// ---- a recording geometry container: the slots Build calls (9, 12, 13, 14, 16, 20, 21, 26), each logged ----
struct FakeContainer {
    SGWB_BAD(0) SGWB_BAD(1) SGWB_BAD(2) SGWB_BAD(3) SGWB_BAD(4) SGWB_BAD(5) SGWB_BAD(6) SGWB_BAD(7) SGWB_BAD(8)
    virtual uint32_t AddElement(uint32_t identity, uint32_t repetition, uint32_t blockFormat, uint32_t setFormat) // 9
    {
        drawfx::Note("AddElement", {identity, repetition, blockFormat, setFormat});
        return elements++;
    }
    SGWB_BAD(10) SGWB_BAD(11)
    virtual void SetElementData(uint32_t index, uint32_t number, const void *data, uint32_t size, const uint16_t *indices,
                                uint32_t count) // 12
    {
        drawfx::Call c;
        c.note = "SetElementData";
        c.words = {index, number, uint32_t(data != nullptr), size, uint32_t(indices != nullptr), count};
        if (data)
            for (uint32_t i = 0; i < size / 4; ++i) c.words.push_back(static_cast<const uint32_t *>(data)[i]);
        Push(std::move(c));
    }
    virtual uint32_t AddLinkage() { drawfx::Note("AddLinkage", 0); return 0; } // 13
    virtual uint32_t AddLinkageElement(uint32_t index, uint32_t element)     // 14 (the element a uint16_t)
    {
        drawfx::Note("AddLinkageElement", {index, element & 0xffff});
        return 0;
    }
    SGWB_BAD(15)
    virtual void SetLinkageReferencedSize(uint32_t index, uint32_t value) // 16
    {
        drawfx::Note("SetLinkageReferencedSize", {index, value});
    }
    SGWB_BAD(17) SGWB_BAD(18) SGWB_BAD(19)
    virtual uint32_t AddGroup(uint32_t primType, const char *name, uint32_t linkIndex, int32_t word10) // 20
    {
        drawfx::Call c;
        c.note = "AddGroup";
        c.words = {primType, uint32_t(name != nullptr), linkIndex, uint32_t(word10)};
        if (name) c.text = name;
        Push(std::move(c));
        return groups++;
    }
    virtual void SetGroupVertexIndices(uint32_t index, const uint16_t *data, uint32_t count) // 21
    {
        drawfx::Call c;
        c.note = "SetGroupVertexIndices";
        c.words = {index, uint32_t(data != nullptr), count};
        if (data)
            for (uint32_t i = 0; i < count; ++i) c.words.push_back(data[i]);
        Push(std::move(c));
    }
    SGWB_BAD(22) SGWB_BAD(23) SGWB_BAD(24) SGWB_BAD(25)
    virtual void SetBoundingMesh(cITriangleSetShape *mesh) { drawfx::Note("SetBoundingMesh", MeshIndex(mesh)); } // 26
    uint32_t elements = 0, groups = 0;
};

// cRZRandom's constructor 0x1b8bc8 (ours), stood in on both sides: the builder's generator is seeded from the C runtime's
// clock (the image's, which the harness never initialises), and nothing draws from it. The seed is logged and a fixed
// state written.
cRZRandom *__fastcall StandRandom(cRZRandom *self, void *, uint32_t seed)
{
    drawfx::Note("generator", {uint32_t(reinterpret_cast<uint8_t *>(self) - gMade), seed});
    self->mIntegerSeed = 0x2468ace0;
    self->mRealSeed = 0x2468ace0;
    self->mUnknown = 0;
    return self;
}
// Ours seeds through ConstructAt (seed control's call-site key; it may have Construct inlined): stood in the same way.
cRZRandom *__fastcall StandRandomAt(cRZRandom *self, void *, uint32_t seed, uint32_t)
{
    return StandRandom(self, nullptr, seed);
}

// ---- the world of the slots test: listeners and the event manager ----
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
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ec0de00 + i;
        }
    }
    ~MissingRecordScope()
    {
        std::memcpy(GameAddress<void>(kAt), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};

void BuildSlotsWorld(uint32_t seed, drawfx::Cells &cells)
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

// ---- a side's builder ----
cIGeometryBuilder *Face(uint8_t *object) { return reinterpret_cast<cIGeometryBuilder *>(object + kFace); }
cIGZPersistSerializable2 *Persist(uint8_t *object)
{
    return reinterpret_cast<cIGZPersistSerializable2 *>(object + kPersist);
}
cWaterGeometryBuilder *State(uint8_t *object) { return reinterpret_cast<cWaterGeometryBuilder *>(object); }

// cTerrain's construction: the image's operator new and the constructor (original), or ours; one reference taken.
uint8_t *Make()
{
    uint8_t *object;
    if (gOriginal) {
        void *const memory = gCtx->Original<GameNewFn>(0x824632)(kSize);
        gMade = static_cast<uint8_t *>(memory);
        object = static_cast<uint8_t *>(gCtx->Original<CtorFn>(0x3b5970)(memory, 1));
    } else {
        void *const memory = ::operator new(kSize);
        gMade = static_cast<uint8_t *>(memory);
        cWaterGeometryBuilder *const ours = ::new (memory) cWaterGeometryBuilder;
        object = reinterpret_cast<uint8_t *>(ours);
        const uint32_t face = uint32_t(reinterpret_cast<uint8_t *>(static_cast<cIGeometryBuilder *>(ours)) - object);
        const uint32_t persist = uint32_t(reinterpret_cast<uint8_t *>(static_cast<cIGZPersistSerializable2 *>(ours)) - object);
        if (face != kFace || persist != kPersist) drawfx::Note("faces moved", {face, persist});
    }
    {
        difftest::NotCounted quiet;
        drawfx::AddRange(object, kSize, kBuilderTag);
    }
    drawfx::Note("first count", Face(object)->AddRef());
    return object;
}

// The object's behaviour words (the count, the extra listener, the vbtable's entries, the vtordisp).
void NoteWords(uint8_t *object)
{
    auto word = [&](uint32_t off) { return *reinterpret_cast<const uint32_t *>(object + off); };
    drawfx::Call c;
    c.note = "words";
    c.words = {word(8), drawfx::Norm(word(0x10))};
    const auto *vbtable = reinterpret_cast<const int32_t *>(word(4));
    c.words.insert(c.words.end(), {uint32_t(vbtable[0]), uint32_t(vbtable[1]), word(0x148)});
    Push(std::move(c));
}

// A tape the builder writes, read back by it (the type-info versions changed and the tape cut short in some seeds).
void WriteRead(uint8_t *object, std::mt19937 &rng)
{
    ClearTape();
    Persist(object)->Write(Writer());
    drawfx::Note("tape", uint32_t(gTape.size()));
    {
        difftest::NotCounted quiet;
        const uint32_t versions[] = {0, 1, 2, 3, 0xffffffffu};
        if (rng() % 3 == 0 && !gTape.empty()) gTape[0].words[1] = versions[rng() % 5];
        if (rng() % 4 == 0 && gTape.size() > 1) gTape[1].words[1] = versions[rng() % 5];
        if (rng() % 6 == 0) gTape.resize(gTape.size() > 0 ? rng() % gTape.size() : 0); // a short tape
    }
    Persist(object)->Read(Reader());
    drawfx::Note("read to", uint32_t(gReader.next));
}

const uint32_t kIids[] = {0,          1,          cGeometryBuilder::kIidSerializable, cGeometryBuilder::kIidBehavior,
                          0xfb859324, 0xbbb79c90, 0x2b6efb38,                         0x12345678};
constexpr unsigned kIidCount = sizeof(kIids) / sizeof(kIids[0]);

void OperateSlot(uint8_t *object, unsigned op, std::mt19937 &rng)
{
    cIGeometryBuilder *const base = Face(object);
    cIGZPersistSerializable2 *const persist = Persist(object);
    switch (op) {
    case 0: { // QueryInterface through either face, a null out in some
        const uint32_t iid = kIids[rng() % kIidCount];
        void *out = nullptr;
        void **const where = rng() % 8 ? &out : nullptr;
        const bool r = rng() % 2 ? base->QueryInterface(iid, where) : persist->QueryInterface(iid, where);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (!out || iid == 0xfb859324) break;
        drawfx::Note("query release", static_cast<cIGZUnknown *>(out)->Release());
        break;
    }
    case 1:
        drawfx::Note("addref", persist->AddRef());
        drawfx::Note("release", base->Release());
        break;
    case 2: {
        cPersistTypeInfo info = persist->GetTypeInfo();
        sgfx::NoteTypeInfo("type info", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 3:
        ClearTape();
        persist->Write(Writer());
        break;
    case 4: WriteRead(object, rng); break;
    case 5: sgfx::NoteText("name", base->GetName()); break;
    case 6: {
        const unsigned k = rng() % 3;
        nGZEvent::cIAbstractBehaviorType *const listener =
            k < 2 ? reinterpret_cast<nGZEvent::cIAbstractBehaviorType *>(&w.extra[k]) : nullptr;
        drawfx::Note("set listener", drawfx::Norm(P(base->SetListener(listener))));
        break;
    }
    case 7: {
        cIGZUnknown *const listener = reinterpret_cast<cIGZUnknown *>(&w.listener[rng() % 2]);
        if (rng() % 2)
            base->Connect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        else
            base->Disconnect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        break;
    }
    case 8:
        if (rng() % 2)
            base->NotifyListeners(kEvents[rng() % 4], rng(), nullptr);
        else
            drawfx::Note("asked", uint8_t(base->AskListeners(kEvents[rng() % 4], rng(), nullptr)));
        break;
    case 9:
        base->OnEvent(rng() % 2 ? kConnectionChanged : 0x1234, rng(), nullptr, rng());
        drawfx::Note("active", {base->IsActive(), base->GetEventCount()});
        break;
    case 10: drawfx::Note("on query", uint8_t(base->OnQuery(kEvents[rng() % 4], rng(), nullptr))); break;
    case 11: {
        const nGZEvent::cEventAttribute a = base->GetEventAttribute(kEvents[rng() % 4]);
        drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        nGZEvent::cAttributeIDRange ids = base->GetEventIds();
        unsigned steps = 0;
        while (steps < 20 && !ids.mCurrent->IsEqual(ids.mEnd)) {
            drawfx::Note("event id", ids.mCurrent->Get());
            ids.mCurrent->Next();
            ++steps;
        }
        drawfx::Note("event ids", {steps, ids.mCurrent->Release(), ids.mBegin->Release(), ids.mEnd->Release()});
        break;
    }
    case 12: // slots 7 and 10 (one empty body)
        if (rng() % 2)
            base->SetAttribute(rng(), rng(), rng());
        else
            base->GetAttribute(rng(), rng(), rng());
        break;
    default: { // a builder never set up: no vertices, no subsets, no shape
        FakeContainer box;
        base->Build(reinterpret_cast<cIGeometryDataContainer *>(&box));
        break;
    }
    }
}

// ---- a side's begin and end ----
struct SideScope {
    difftest::AllocationCounter counter;
    SideScope(difftest::Context &ctx, bool original)
    {
        gCtx = &ctx;
        gOriginal = original;
        difftest::NotCounted quiet;
        gMeshes.clear();
    }
    void End()
    {
        for (cITriangleSetShape *m : gMeshes) drawfx::Note("triangle set released", m->Release());
        drawfx::Note("blocks left", uint32_t(counter.Blocks()));
    }
};

void RunSlots(difftest::Context &ctx)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    MissingRecordScope missing;
    ServiceFake service(ctx); // the refit set's head node
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn random{0x1b8bc8, &cRZRandom::Construct, &StandRandom};
    shape::CodePatch randomAt{shape::CodeOf(&cRZRandom::ConstructAt), shape::CodeOf(&StandRandomAt)};
    sgfx::CheckSides(ctx, "water builder slots", 48, BuildSlotsWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        uint8_t *const object = Make();
        NoteWords(object);
        for (int k = 0; k < 48; ++k) {
            const unsigned op = rng() % 14;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            OperateSlot(object, op, rng);
            if (k % 8 == 7) NoteWords(object);
        }
        NoteWords(object);
        ClearTape();
        Persist(object)->Write(Writer());
        Face(object)->SetListener(nullptr);
        drawfx::Note("last release", Face(object)->Release());
        for (const Fake &l : w.listener) drawfx::Note("listener refs", {l.id, uint32_t(l.refs)});
        for (const Fake &l : w.extra) drawfx::Note("extra listener refs", {l.id, uint32_t(l.refs)});
        for (const Fake &n : gNames) drawfx::Note("type name refs", {n.id, uint32_t(n.refs)});
        side.End();
    });
    w.manager.Drop();
}

// ---- the grid test's fakes: the terrain, its grid, the placement service and its terrain painter ----
constexpr uint32_t kMaxTiles = 4;
constexpr uint32_t kMaxVertices = (kMaxTiles + 1) * (2 * kMaxTiles + 2);

// cTerrainGeometryData: GridHeight 0x3b58c0 reads the z of vertex i of the array at +0x10.
struct FakeGrid {
    virtual uint32_t QueryInterface(uint32_t, void **) { drawfx::Note("grid query", id); return 0; } // 0
    virtual uint32_t AddRef() { drawfx::Note("grid addref", {id, uint32_t(++refs)}); return uint32_t(refs); } // 1
    virtual uint32_t Release() { drawfx::Note("grid release", {id, uint32_t(--refs)}); return uint32_t(refs); } // 2
    uint32_t pad[3];
    Vec3 *vertices; // +0x10
    uint32_t id;
    int32_t refs;
    Vec3 storage[kMaxVertices];
};

#define SGWB_BADT(who, n) \
    virtual uint32_t Bad##n() { drawfx::Note(who " bad slot", n); return 0; }

// cTerrain: +0x3c its grid, +0x68..+0x74 the lot's tiles (unsigned, inclusive); slot 13 RedrawRegion.
struct FakeTerrain {
    virtual uint32_t QueryInterface(uint32_t, void **) { drawfx::Note("terrain query", id()); return 0; } // 0
    virtual uint32_t AddRef() { drawfx::Note("terrain addref", {id(), uint32_t(++refs)}); return uint32_t(refs); }
    virtual uint32_t Release() { drawfx::Note("terrain release", {id(), uint32_t(--refs)}); return uint32_t(refs); }
    SGWB_BADT("terrain", 3) SGWB_BADT("terrain", 4) SGWB_BADT("terrain", 5) SGWB_BADT("terrain", 6)
    SGWB_BADT("terrain", 7) SGWB_BADT("terrain", 8) SGWB_BADT("terrain", 9) SGWB_BADT("terrain", 10)
    SGWB_BADT("terrain", 11) SGWB_BADT("terrain", 12)
    virtual void RedrawRegion(const int32_t *tiles, const int32_t *texels) // 13
    {
        drawfx::Note("RedrawRegion", {uint32_t(tiles[0]), uint32_t(tiles[1]), uint32_t(tiles[2]), uint32_t(tiles[3]),
                                      uint32_t(texels[0]), uint32_t(texels[1]), uint32_t(texels[2]),
                                      uint32_t(texels[3])});
    }
    uint32_t words[31]; // +0x04..+0x7f
    int32_t refs;

    uint32_t id() const { return words[0]; } // +0x04: the fake's id (nothing reads it)
    void SetGrid(FakeGrid *grid) { words[(0x3c - 4) / 4] = reinterpret_cast<uint32_t>(grid); }
    uint32_t *Lot() { return &words[(0x68 - 4) / 4]; }
};

// The terrain painter: slots 18 GetLayerIndex (by the name's address: the image's three layer-name strings),
// 22 GetSubdivisions, 23 BlendLayer, 27 SetLayerValue (the byte arguments' low bytes logged).
struct FakePainter {
    virtual uint32_t QueryInterface(uint32_t, void **) { drawfx::Note("painter query", 0); return 0; } // 0
    virtual uint32_t AddRef() { drawfx::Note("painter addref", uint32_t(++refs)); return uint32_t(refs); }
    virtual uint32_t Release() { drawfx::Note("painter release", uint32_t(--refs)); return uint32_t(refs); }
    SGWB_BADT("painter", 3) SGWB_BADT("painter", 4) SGWB_BADT("painter", 5) SGWB_BADT("painter", 6)
    SGWB_BADT("painter", 7) SGWB_BADT("painter", 8) SGWB_BADT("painter", 9) SGWB_BADT("painter", 10)
    SGWB_BADT("painter", 11) SGWB_BADT("painter", 12) SGWB_BADT("painter", 13) SGWB_BADT("painter", 14)
    SGWB_BADT("painter", 15) SGWB_BADT("painter", 16) SGWB_BADT("painter", 17)
    virtual uint32_t GetLayerIndex(void *name) // 18
    {
        const uint32_t names[3] = {0x10da1f8, 0x10da218, 0x100d414};
        uint32_t which = 0xff;
        for (uint32_t i = 0; i < 3; ++i)
            if (name == GameAddress<void>(names[i])) which = i;
        drawfx::Note("GetLayerIndex", which);
        return 0x5a5a5a00u | (which == 0xff ? 0xeeu : layers[which]); // high bytes a register's leftovers
    }
    SGWB_BADT("painter", 19) SGWB_BADT("painter", 20) SGWB_BADT("painter", 21)
    virtual uint32_t GetSubdivisions() { drawfx::Note("GetSubdivisions", subdivisions); return subdivisions; } // 22
    virtual void BlendLayer(uint32_t layer, int32_t x, int32_t y, uint32_t alpha, uint32_t mode) // 23
    {
        drawfx::Note("BlendLayer", {layer & 0xff, uint32_t(x), uint32_t(y), alpha & 0xff, mode});
    }
    SGWB_BADT("painter", 24) SGWB_BADT("painter", 25) SGWB_BADT("painter", 26)
    virtual void SetLayerValue(uint32_t layer, int32_t x, int32_t y, uint32_t value) // 27
    {
        drawfx::Note("SetLayerValue", {layer & 0xff, uint32_t(x), uint32_t(y), value & 0xff});
    }
    int32_t refs;
    uint32_t subdivisions;
    uint8_t layers[3];
};

struct FakePlacement {
    virtual uint32_t QueryInterface(uint32_t, void **) { drawfx::Note("service query", 0); return 0; } // 0
    virtual uint32_t AddRef() { drawfx::Note("service addref", uint32_t(++refs)); return uint32_t(refs); }
    virtual uint32_t Release() { drawfx::Note("service release", uint32_t(--refs)); return uint32_t(refs); }
    SGWB_BADT("service", 3) SGWB_BADT("service", 4) SGWB_BADT("service", 5) SGWB_BADT("service", 6)
    SGWB_BADT("service", 7) SGWB_BADT("service", 8) SGWB_BADT("service", 9) SGWB_BADT("service", 10)
    SGWB_BADT("service", 11) SGWB_BADT("service", 12) SGWB_BADT("service", 13) SGWB_BADT("service", 14)
    SGWB_BADT("service", 15) SGWB_BADT("service", 16) SGWB_BADT("service", 17) SGWB_BADT("service", 18)
    SGWB_BADT("service", 19) SGWB_BADT("service", 20) SGWB_BADT("service", 21) SGWB_BADT("service", 22)
    SGWB_BADT("service", 23) SGWB_BADT("service", 24) SGWB_BADT("service", 25) SGWB_BADT("service", 26)
    SGWB_BADT("service", 27) SGWB_BADT("service", 28) SGWB_BADT("service", 29) SGWB_BADT("service", 30)
    SGWB_BADT("service", 31) SGWB_BADT("service", 32) SGWB_BADT("service", 33) SGWB_BADT("service", 34)
    virtual void *GetTerrainPainter() { drawfx::Note("GetTerrainPainter", 0); return painter; } // 35
    int32_t refs;
    FakePainter *painter;
};

#undef SGWB_BADT
#undef SGWB_BAD

FakeGrid gGrids[2];
FakeTerrain gTerrains[2];
FakePainter gPainter;
FakePlacement gPlacement;
float gCurveScale;
uint32_t gSubsetSalt;

// The paint thresholds (owned statics shared with the image) and their .data defaults, restored after the test.
const uint32_t kThresholds[] = {0x10da1b8, 0x10da1bc, 0x10da1c0, 0x10da1c4, 0x10da1c8, 0x10da1cc, 0x10da1d0,
                                0x10da1d4, 0x10da1d8, 0x10da1dc, 0x10da1e0, 0x10da1e4, 0x10da1e8, 0x100d138,
                                0x100d13c, 0x100d140, 0x100d168, 0x100d16c, 0x100d170};
constexpr unsigned kThresholdCount = sizeof(kThresholds) / sizeof(kThresholds[0]);
struct ThresholdScope {
    uint32_t saved[kThresholdCount];
    ThresholdScope()
    {
        for (unsigned i = 0; i < kThresholdCount; ++i) saved[i] = *GameAddress<uint32_t>(kThresholds[i]);
    }
    ~ThresholdScope()
    {
        for (unsigned i = 0; i < kThresholdCount; ++i) *GameAddress<uint32_t>(kThresholds[i]) = saved[i];
    }
};
void SetFloat(uint32_t rva, float value) { *GameAddress<float>(rva) = value; }

// The placement service's cell (0x20e0b2 answers it when set).
struct PlacementCellScope {
    void *saved = *GameAddress<void *>(0x10cc298);
    PlacementCellScope() { *GameAddress<void *>(0x10cc298) = &gPlacement; }
    ~PlacementCellScope() { *GameAddress<void *>(0x10cc298) = saved; }
};

// Terrain heights: small values in tenths, with some -0 and repeats.
float Height(std::mt19937 &rng)
{
    switch (rng() % 10) {
    case 0: return -0.0f;
    case 1: return 0.0f;
    default: return float(int(rng() % 31) - 15) / 10.0f;
    }
}

void BuildGridWorld(uint32_t seed, drawfx::Cells &)
{
    std::mt19937 rng(seed * 104729u + 7u);
    for (uint32_t g = 0; g < 2; ++g) {
        FakeGrid &grid = gGrids[g];
        grid.id = 700 + g;
        grid.refs = 1;
        grid.vertices = grid.storage;
        for (Vec3 &v : grid.storage) v = Vec3{float(rng() % 7), float(rng() % 7), Height(rng)};
        FakeTerrain &terrain = gTerrains[g];
        std::memset(terrain.words, 0, sizeof(terrain.words));
        terrain.words[0] = 710 + g;
        terrain.refs = 1;
        terrain.SetGrid(&grid);
        uint32_t *const lot = terrain.Lot();
        switch (rng() % 4) {
        case 0: lot[0] = 0, lot[1] = 0, lot[2] = 0xffffffffu, lot[3] = 0xffffffffu; break; // everything
        case 1: lot[0] = 3, lot[1] = 3, lot[2] = 1, lot[3] = 1; break;                     // nothing
        default:
            lot[0] = rng() % 3, lot[1] = rng() % 3;
            lot[2] = lot[0] + rng() % 3, lot[3] = lot[1] + rng() % 3;
            break;
        }
    }
    gPainter.refs = 1;
    gPainter.subdivisions = 1 + rng() % 3;
    gPainter.layers[0] = uint8_t(rng() % 8);
    gPainter.layers[1] = uint8_t(rng() % 8);
    gPainter.layers[2] = uint8_t(rng() % 4 == 0 ? 0xff : rng() % 8);
    gPlacement.refs = 1;
    gPlacement.painter = &gPainter;
    gCurveScale = float(rng() % 200) - 50.0f;
    gSubsetSalt = rng() % 5;
    // The thresholds: depths are about -3 to 3 here.
    SetFloat(0x10da1b8, rng() % 4 == 0 ? -0.2f : 0.05f);              // shallow lowest
    SetFloat(0x10da1bc, 300.0f + float(rng() % 200));                 // shallow ramp
    SetFloat(0x10da1c0, 250.0f + float(rng() % 300));                 // shallow fade
    SetFloat(0x10da1c4, 200.0f + float(rng() % 300));                 // deep ramp
    SetFloat(0x10da1c8, -1.0f - float(rng() % 10) / 10.0f);           // beach lowest
    SetFloat(0x10da1cc, rng() % 3 == 0 ? -0.3f : 0.1f);               // beach peak
    SetFloat(0x10da1d0, 0.2f + float(rng() % 5) / 10.0f);             // beach highest
    SetFloat(0x10da1d4, 150.0f + float(rng() % 200));                 // beach up
    SetFloat(0x10da1d8, 400.0f + float(rng() % 400));                 // beach down
    SetFloat(0x10da1dc, 1.0f + float(rng() % 30) / 10.0f);            // wet-corner distance limit
    SetFloat(0x10da1e0, 0.2f + float(rng() % 6) / 10.0f);             // wet-corner distance scale
    *GameAddress<int32_t>(0x10da1e4) = int32_t(rng() % 3);            // wet-corner radius
    SetFloat(0x10da1e8, 0.5f + float(rng() % 20) / 10.0f);            // wet-corner alpha scale
    SetFloat(0x100d138, 0.6f);
    SetFloat(0x100d13c, 0.5f);
    SetFloat(0x100d140, 1.2f);
    SetFloat(0x100d168, 0.0f);
    SetFloat(0x100d16c, 0.0f);
    SetFloat(0x100d170, 1.0f);
    if (rng() % 4 == 0) { // the .data defaults changed
        SetFloat(0x100d138, 0.9f);
        SetFloat(0x100d13c, 0.3f);
        SetFloat(0x100d140, 2.0f);
        SetFloat(0x100d168, 0.25f);
        SetFloat(0x100d16c, -0.0f);
        SetFloat(0x100d170, 0.75f);
    }
}

// 0xc0a6a1 (thiscall on the curve 0x10da238, ret 4): the depth curve, a line here.
float __fastcall StandCurve(void *curve, void *, float depth)
{
    drawfx::Note("curve", {uint32_t(curve == GameAddress<void>(0x10da238)), Bits(depth)});
    return depth * gCurveScale + 100.0f;
}

const char *const kSubsetNames[] = {"lot_1", "LOT_1", "Lot_2", "lot_3", "", "a_lot_name_longer_than_sixteen"};
constexpr unsigned kSubsetCount = sizeof(kSubsetNames) / sizeof(kSubsetNames[0]);

void SideSetTriangleSubset(const Coord *coord, const char *name)
{
    if (gOriginal)
        gCtx->Original<SetTriangleSubsetFn>(0x3b9060)(gObject, coord, name);
    else
        State(gObject)->SetTriangleSubset(coord, name);
}

// 0x297ad0 (cTerrain, thiscall, ret 8): the lot subset of a wet triangle; logged, then the side's SetTriangleSubset with
// a name of the tile.
void __fastcall StandLotSubset(void *terrain, void *, const Coord *coord, const char *name)
{
    drawfx::Call c;
    c.note = "lot subset";
    c.words = {reinterpret_cast<FakeTerrain *>(terrain)->id(), coord->x, coord->y, coord->k};
    c.text = name ? name : "<null>";
    Push(std::move(c));
    SideSetTriangleSubset(coord, kSubsetNames[(coord->x * 3 + coord->y + gSubsetSalt) % kSubsetCount]);
}

// ---- the builder's state ----
uint32_t RefId(const void *p)
{
    for (uint32_t g = 0; g < 2; ++g) {
        if (p == &gGrids[g]) return gGrids[g].id;
        if (p == &gTerrains[g]) return gTerrains[g].id();
    }
    if (p == &gPainter) return 720;
    if (p == &gPlacement) return 721;
    return p ? 0xffffu : 0;
}

// The fields the constructor leaves (the sizes, tables, layers and the skip-paint flag, which Setup sets) only once set up;
// the dirty rectangle (set by the first GrowDirtyRect after an Update or Setup) only while dirty.
void DumpState(uint8_t *object, bool setUp = true)
{
    cWaterGeometryBuilder *const b = State(object);
    drawfx::Note("flags", {b->mWidth, b->mHeight, b->mSubsetsChanged, b->mRebuildAll, b->mCollisionEnabled, b->mDirty,
                           uint32_t(b->mUnused120.size())});
    if (b->mDirty)
        drawfx::Note("dirty rect", {uint32_t(b->mDirtyRect.xmin), uint32_t(b->mDirtyRect.ymin),
                                    uint32_t(b->mDirtyRect.xmax), uint32_t(b->mDirtyRect.ymax)});
    if (setUp) {
        drawfx::Note("sizes", {uint32_t(b->mStride), uint32_t(b->mTrianglesPerColumn), uint32_t(b->mBounds.xmin),
                               uint32_t(b->mBounds.ymin), uint32_t(b->mBounds.xmax), uint32_t(b->mBounds.ymax),
                               uint32_t(b->mSizeX), uint32_t(b->mSizeY), b->mPaintSubdivisions, Bits(b->mPaintStep),
                               b->mSkipPaint, b->mShallowLayer, b->mDeepLayer, b->mBeachLayer});
        drawfx::Call c;
        c.note = "tables";
        for (int32_t v : b->mCornerTable) c.words.push_back(uint32_t(v));
        for (int32_t v : b->mTileCorners) c.words.push_back(uint32_t(v));
        Push(std::move(c));
    }
    drawfx::Note("references", {MeshIndex(b->mShape), RefId(b->mService), RefId(b->mPainter), RefId(b->mTerrain),
                                RefId(b->mGrid)});
    {
        drawfx::Call c;
        c.note = "vertices";
        c.words = {uint32_t(b->mPositions.size()), uint32_t(b->mNormals.size()), uint32_t(b->mTexCoords.size()),
                   uint32_t(b->mColours.size()), uint32_t(b->mDepths.size())};
        for (const Vec3 &v : b->mPositions) c.words.insert(c.words.end(), {Bits(v.x), Bits(v.y), Bits(v.z)});
        for (const Vec3 &v : b->mNormals) c.words.insert(c.words.end(), {Bits(v.x), Bits(v.y), Bits(v.z)});
        for (const auto &t : b->mTexCoords) c.words.insert(c.words.end(), {Bits(t.x), Bits(t.y)});
        for (uint32_t colour : b->mColours) c.words.push_back(colour);
        for (float d : b->mDepths) c.words.push_back(Bits(d));
        Push(std::move(c));
    }
    {
        drawfx::Call c;
        c.note = "triangle subsets";
        for (uint8_t s : b->mTriangleSubsets) c.words.push_back(s);
        Push(std::move(c));
    }
    for (const cWaterGeometryBuilder::cSubset *s : b->mSubsets) {
        drawfx::Call c;
        c.note = "subset";
        c.words.push_back(s != nullptr);
        if (s) {
            c.words.push_back(s->mRefCount);
            c.words.push_back(uint32_t(s->mTriangles.size()));
            for (uint32_t t : s->mTriangles) c.words.push_back(t);
            c.words.push_back(uint32_t(s->mIndices.size()));
            for (uint16_t i : s->mIndices) c.words.push_back(i);
            c.text = s->mName.Text();
        }
        Push(std::move(c));
    }
    {
        drawfx::Call c;
        c.note = "subset index";
        c.words.push_back(uint32_t(b->mSubsetIndex.size()));
        for (const auto &entry : b->mSubsetIndex) {
            c.words.push_back(entry.second);
            c.text += entry.first;
            c.text += '\x1f';
        }
        Push(std::move(c));
    }
    {
        drawfx::Call c;
        c.note = "refit set";
        c.words.push_back(uint32_t(b->mChangedTriangles.size()));
        for (uint32_t t : b->mChangedTriangles) c.words.push_back(t);
        Push(std::move(c));
    }
    NoteMesh("collision mesh", b->mShape);
}

// ---- the operations, on the side's builder ----
void Setup(uint32_t width, uint32_t height, FakeTerrain *terrain)
{
    drawfx::Note("setup", {width, height, terrain->id()});
    if (gOriginal)
        gCtx->Original<SetupFn>(0x3b6c50)(gObject, width, height, terrain);
    else
        State(gObject)->Setup(width, height, terrain);
}
void Update()
{
    if (gOriginal)
        gCtx->Original<UpdateFn>(0x3b7850)(gObject);
    else
        State(gObject)->Update();
}
void SetVertexHeight(uint32_t x, uint32_t y, float z)
{
    if (gOriginal)
        gCtx->Original<SetVertexHeightFn>(0x3b9140)(gObject, x, y, z);
    else
        State(gObject)->SetVertexHeight(x, y, z);
}
void RemoveTriangle(const Coord *coord)
{
    if (gOriginal)
        gCtx->Original<RemoveTriangleFn>(0x3b6510)(gObject, coord);
    else
        State(gObject)->RemoveTriangle(coord);
}
uint32_t IsTriangleDry(Coord coord)
{
    if (gOriginal) return gCtx->Original<IsTriangleDryFn>(0x3b7080)(gObject, coord) & 0xff;
    return State(gObject)->IsTriangleDry(coord);
}
void GrowDirtyRect(int32_t x, int32_t y)
{
    if (gOriginal)
        gCtx->Original<GrowDirtyRectFn>(0x3b7110)(gObject, x, y);
    else
        State(gObject)->GrowDirtyRect(x, y);
}
void ClearSubsetsChanged()
{
    if (gOriginal)
        gCtx->Original<ClearSubsetsChangedFn>(0x3b7190)(gObject);
    else
        State(gObject)->ClearSubsetsChanged();
}
float GetVertexHeight(uint32_t x, uint32_t y)
{
    if (gOriginal) return gCtx->Original<GetVertexHeightFn>(0x3b8e10)(gObject, x, y);
    return State(gObject)->GetVertexHeight(x, y);
}
void Build()
{
    FakeContainer box;
    Face(gObject)->Build(reinterpret_cast<cIGeometryDataContainer *>(&box));
}

float WaterHeight(std::mt19937 &rng)
{
    switch (rng() % 12) {
    case 0: return -0.0f;
    case 1: return 0.0f;
    default: return float(int(rng() % 41) - 20) / 10.0f;
    }
}

void OperateGrid(unsigned op, std::mt19937 &rng, FakeTerrain *&terrain)
{
    cWaterGeometryBuilder *const b = State(gObject);
    const uint32_t width = b->mWidth, height = b->mHeight;
    switch (op) {
    case 0:
    case 1:
    case 2:
    case 3: { // a grid vertex moved (unchanged in some)
        const uint32_t x = rng() % (width + 1), y = rng() % (height + 1);
        float z = WaterHeight(rng);
        if (rng() % 8 == 0) z = GetVertexHeight(x, y);
        drawfx::Note("set height", {x, y, Bits(z)});
        SetVertexHeight(x, y, z);
        break;
    }
    case 4: // a rise over a block of vertices
    {
        const uint32_t x0 = rng() % (width + 1), y0 = rng() % (height + 1);
        const float z = WaterHeight(rng);
        drawfx::Note("set block", {x0, y0, Bits(z)});
        for (uint32_t x = x0; x <= width && x < x0 + 2; ++x)
            for (uint32_t y = y0; y <= height && y < y0 + 2; ++y) SetVertexHeight(x, y, z);
        break;
    }
    case 5:
    case 6: drawfx::Note("update", 0); Update(); break;
    case 7: {
        const Coord coord{rng() % width, rng() % height, rng() % 4};
        const char *const name = kSubsetNames[rng() % kSubsetCount];
        drawfx::Note("set subset", {coord.x, coord.y, coord.k});
        SideSetTriangleSubset(&coord, name);
        break;
    }
    case 8: {
        const Coord coord{rng() % width, rng() % height, rng() % 4};
        drawfx::Note("remove triangle", {coord.x, coord.y, coord.k});
        RemoveTriangle(&coord);
        break;
    }
    case 9: {
        const Coord coord{rng() % width, rng() % height, rng() % 4};
        drawfx::Note("dry", {coord.x, coord.y, coord.k, IsTriangleDry(coord)});
        break;
    }
    case 10: {
        const int32_t x = int32_t(rng() % (width + 1)), y = int32_t(rng() % (height + 1));
        drawfx::Note("grow dirty", {uint32_t(x), uint32_t(y)});
        GrowDirtyRect(x, y);
        break;
    }
    case 11: drawfx::Note("clear changed", 0); ClearSubsetsChanged(); break;
    case 12: {
        const uint32_t x = rng() % (width + 1), y = rng() % (height + 1);
        drawfx::Note("height", {x, y, Bits(GetVertexHeight(x, y))});
        break;
    }
    case 13: { // the terrain under a vertex moved, then the vertex marked dirty
        const uint32_t x = rng() % (width + 1), y = rng() % (height + 1);
        FakeGrid *const grid = reinterpret_cast<FakeGrid *>(terrain->words[(0x3c - 4) / 4]);
        const float z = Height(rng);
        drawfx::Note("terrain height", {x, y, Bits(z)});
        grid->storage[uint32_t(b->mStride) * x + 2 * y].z = z;
        GrowDirtyRect(int32_t(x), int32_t(y));
        break;
    }
    case 14: drawfx::Note("build", 0); Build(); break;
    case 15: { // a triangle set to the subset it is already in (the changed flag cleared first in some)
        std::vector<uint32_t> placed;
        for (uint32_t t = 0; t < b->mTriangleSubsets.size(); ++t)
            if (b->mTriangleSubsets[t] != cWaterGeometryBuilder::kNoSubset) placed.push_back(t);
        if (placed.empty()) {
            drawfx::Note("no placed triangle", 0);
            break;
        }
        const uint32_t t = placed[rng() % placed.size()];
        const Coord coord{t / 4 / height, t / 4 % height, t % 4};
        const std::string name = b->mSubsets[b->mTriangleSubsets[t]]->mName.Text();
        if (rng() % 2) ClearSubsetsChanged();
        drawfx::Note("same subset", {coord.x, coord.y, coord.k});
        SideSetTriangleSubset(&coord, name.c_str());
        break;
    }
    case 16: { // rarely: subsets up to index 255 (kNoSubset) and past it, through one wet triangle
        if (rng() % 3) break;
        Coord coord{0, 0, 0};
        bool wet = false;
        for (uint32_t t = 0; t < width * height * 4 && !wet; ++t) {
            coord = Coord{t / 4 / height, t / 4 % height, t % 4};
            wet = !IsTriangleDry(coord);
        }
        drawfx::Note("many subsets", {coord.x, coord.y, coord.k, uint32_t(wet)});
        if (!wet) break;
        char name[32];
        uint32_t n = 0;
        while (b->mSubsets.size() < 255) {
            std::snprintf(name, sizeof(name), "many_%u", n++);
            SideSetTriangleSubset(&coord, name);
        }
        RemoveTriangle(&coord);
        for (int k = 0; k < 2; ++k) { // index 255 (no triangle added), then 256 (subset 0)
            ClearSubsetsChanged();
            std::snprintf(name, sizeof(name), "many_%u", n++ + 1000);
            SideSetTriangleSubset(&coord, name);
            drawfx::Note("changed", b->mSubsetsChanged);
        }
        break;
    }
    default: { // set up again, rarely
        if (rng() % 4) break;
        terrain = &gTerrains[rng() % 2];
        Setup(1 + rng() % kMaxTiles, 1 + rng() % kMaxTiles, terrain);
        break;
    }
    }
}

void RunGrid(difftest::Context &ctx)
{
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn random{0x1b8bc8, &cRZRandom::Construct, &StandRandom};
    shape::CodePatch randomAt{shape::CodeOf(&cRZRandom::ConstructAt), shape::CodeOf(&StandRandomAt)};
    ServiceFake service(ctx);
    ThresholdScope thresholds;
    PlacementCellScope placement;
    shape::CodePatch curve{0xc0a6a1, reinterpret_cast<const void *>(&StandCurve)};
    shape::CodePatch lotSubset{0x297ad0, reinterpret_cast<const void *>(&StandLotSubset)};
    sgfx::CheckSides(ctx, "water builder grid", 96, BuildGridWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        gObject = Make();
        FakeTerrain *terrain = &gTerrains[rng() % 2];
        if (rng() % 8 == 0) { // never set up: Build and Update do nothing much
            DumpState(gObject, false);
            Build();
            Update();
        }
        Setup(1 + rng() % kMaxTiles, 1 + rng() % kMaxTiles, terrain);
        if (rng() % 6 == 0) gObject[0xd9] = 0; // the collision flag: nothing in the game clears it
        DumpState(gObject);
        const int steps = 24 + int(rng() % 40);
        for (int k = 0; k < steps; ++k) {
            const unsigned op = rng() % 18;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            OperateGrid(op, rng, terrain);
            if (k % 8 == 7) DumpState(gObject);
        }
        Update();
        DumpState(gObject);
        Build();
        drawfx::Note("last release", Face(gObject)->Release());
        for (const FakeGrid &g : gGrids) drawfx::Note("grid refs", {g.id, uint32_t(g.refs)});
        for (const FakeTerrain &t : gTerrains) drawfx::Note("terrain refs", {t.id(), uint32_t(t.refs)});
        drawfx::Note("service refs", {uint32_t(gPlacement.refs), uint32_t(gPainter.refs)});
        side.End();
        gObject = nullptr;
    });
}

} // namespace

DIFF_TEST(sg_water_builder_slots, 0x3b5970) { RunSlots(ctx); }

DIFF_TEST(sg_water_builder_grid, 0x3b6c50) { RunGrid(ctx); }
