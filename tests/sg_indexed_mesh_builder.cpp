// Differential tests of nGZSceneGraph::cIndexedMeshBuilder as a whole class (lane QH, scenegraph D2 part 3;
// src/sg/cIndexedMeshBuilder.h), on our cGeometryBuilder base (src/sg/cGeometryBuilder.h). The harness follows
// tests/sg_tangent_builder.cpp (lane QG): each side's builder comes from its own factory (the game's 0xa7ffa0 and ours
// Create), the containers are ours on both sides (lane PA's cGeometryDataContainer, verified), and persistence runs over
// a tape writer and reader (every list slot of the persist interfaces, typed so that a tape read with another version
// cannot read a count from the wrong kind of token).
//
//   sg_indexed_mesh_builder_collect   random meshes through the cIIndexedMeshBuilder face: a transform, positions,
//                              normals, texture coordinates, colours (values past 0 and 1 for the clamp), the two word
//                              lists, bones with influences, morph targets (every one of the eight add forms), and
//                              groups made with every index form, closed by CommitGroup, then Reset(keep) and more
//                              groups; the object's lists dumped after each stage (positions, groups, runs, names,
//                              bones, morph targets, the cursors, the transform and normal matrix) and the faces'
//                              counts. Finish is called on the half of the cases whose helpers (still the game's) have
//                              data they can take.
//   sg_indexed_mesh_builder_build     the same meshes built into a container (elements, linkages with their element
//                              lists and aliases, groups with their indices, transforms and name pairs dumped).
//   sg_indexed_mesh_builder_persist   Write of a builder (the tape, token by token), Read of a tape into a fresh builder
//                              (the tape cut short, its record's version changed, and a tape of the old bone format
//                              made from a current one), the builder read then written again.
//   sg_indexed_mesh_builder_faces     QueryInterface over the iids through either face, the type info, GetName,
//                              the reference counts of the faces, the listeners Finish notifies.
//
// Left out, with the original's reasons: indices past a list's size (unchecked vector indexing on both sides), a
// morph add past the position or normal count, a bone number past the bone count.
//
// RVAs these tests reach (status entries cite them): the factory 0xa7ffa0, the constructor 0xa7b1d0, the destructors
// 0xa7b8d0 / 0xa7c2f0, QueryInterface 0xa80c30, Read 0xa80c90, GetTypeInfo 0xa81fc0, Write 0xa826c0, GetName 0xa7e368,
// Build 0xa80200 and every slot body of the cIIndexedMeshBuilder face, with the helpers 0xa7bba0, 0xa7bd30, 0xa836c0,
// 0xa83740 and 0x1a7570 (the thunks the tests call are the compiler's, in our vtables).
#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"

#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/cGeometryDataContainerClass.h"
// RVAs these tests reach (the status entries cite them): 0xa7c2f0, 0xa80c30, 0xa81a60, 0xa80c90, 0xa81fc0, 0xa826c0,
// 0xa80c1a, 0xa7db25, 0xa81a51, 0xa80026, 0xa80034, 0xa81a65, 0xa7ff88, 0xa80c7c, 0xa7f8a1, 0xa7f924, 0xa800bf,
// 0xa7dd58, 0xa7dd66, 0xa81b60, 0xa801f6, 0xa7e368, 0xa7dd74, 0xa7f932, 0xa81ebf, 0xa7da42, 0xa7d772, 0xa7dc90,
// 0xa7c670, 0xa7dcc7, 0xa7db33, 0xa7d94b, 0xa7d57b, 0xa7dbc8, 0xa7c4b4, 0xa81afb, 0xa81b6e, 0xa7c395, 0xa7c313,
// 0xa81ae1, 0xa81a73, 0xa7d42c, 0xa7cfcd, 0xa7d383, 0xa7cdc5, 0xa7d1cb, 0xa7cadc, 0xa7d2e0, 0xa7cca5, 0xa81bb3,
// 0xa7e34d, 0xa7fa02, 0xa7d9ea, 0xa7d71a, 0xa7dc34, 0xa7c614, 0xa7c957, 0xa7d9b5, 0xa7d6e1, 0xa7dbf1, 0xa7c5cc,
// 0xa7c7ec, 0xa80050, 0xa80067, 0xa80163, 0xa80103, 0xa80192, 0xa80132, 0xa801c3, 0xa800cd, 0xa7b1d0, 0xa7b8d0,
// 0xa7ffa0, 0xa7caf0, 0xa7ccb0, 0xa7cfe0, 0xa7d1e0, 0xa7d2f0, 0xa7d440, 0xa7d590, 0xa7d780, 0xa7d960, 0xa7da50,
// 0xa7dd80, 0xa7f940, 0xa7fa10, 0xa80200, 0xa81b10, 0xa81bc0, and Finish's helpers 0xa7e380, 0xa7f4a0, 0xa820a0,
// 0xa7ed00, 0xa7f0b0 (Read reaches 0xa820a0 and 0x1a7570 too; 0x1a7570 has its own test, sg_matrix_to_quaternion).

#include "sg/cIndexedMeshBuilder.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

using FactoryFn = cIGZUnknown *(__cdecl *)();

constexpr uint32_t kContainerTag = 0x6c000000;
constexpr uint32_t kBuilderTag = 0x6e000000;
constexpr uint32_t kFace = 0x2c;          // the container's face in its object
constexpr uint32_t kBuilderFace = 0x1d0;  // the cIGeometryBuilder subobject's offset
constexpr uint32_t kOwnFace = 0x1d8;      // the cIIndexedMeshBuilder subobject's offset
constexpr uint32_t kObjectSize = 0x1e0;
constexpr uint32_t kFinishEvent = 0xbbb112f6;

difftest::Context *gCtx;
bool gOriginal;

std::string TextOf(const void *str)
{
    return str ? reinterpret_cast<const cRZString *>(str)->ToChar() : std::string("<null>");
}


std::vector<cIGeometryDataContainer *> gBoxes;

// Registers a new object's memory. A registered range that overlaps it belonged to an object that has been freed (the
// heap handed its memory out again), so it is dropped first: the range lookup takes the first match, and a stale range
// left in front would name the new object by the dead one's tag on whichever side the heap reused the address (VB's
// validation 2026-11-02: "query persist" orig 0x6c000004, ours 0x6e00000c).
void AddLiveRange(const void *p, size_t size, uint32_t tag, std::source_location where = std::source_location::current())
{
    difftest::NotCounted quiet;
    const uintptr_t lo = uintptr_t(p), hi = lo + size;
    std::erase_if(static_cast<std::vector<drawfx::Range> &>(drawfx::gRanges), [&](const drawfx::Range &r) { return r.lo < hi && lo < r.hi; });
    drawfx::AddRange(p, size, tag, where);
}

cIGeometryDataContainer *NewBox()
{
    cISGResource *r = cGeometryDataContainer::Create();
    void *out = nullptr;
    r->QueryInterface(cIGeometryDataContainer::kIid, &out);
    auto *face = static_cast<cIGeometryDataContainer *>(out);
    AddLiveRange(reinterpret_cast<uint8_t *>(face) - kFace, cGeometryDataContainer::kSize,
                 kContainerTag | uint32_t(gBoxes.size()) << 12);
    gBoxes.push_back(face);
    return face;
}

// The framework object the behaviour slots reach (GetClassObject is never asked here).
struct FakeCom {
    virtual uint32_t QueryInterface(uint32_t, void **) { return 0; }
    virtual uint32_t AddRef() { return 1; }
    virtual uint32_t Release() { return 1; }
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **out)
    {
        drawfx::Note("COM", {classId, iid});
        *out = nullptr;
        return 0;
    }
};
FakeCom gCom;
void *__cdecl StandFramework() { return &gCom; }

// ---- a tape writer and reader ----
// kind: 'w' a word (SetUint32), 'f' a float (SetFloat32), 'a' an array (elem 'u' unsigned, 's' signed, 'f' float, 'v'
// bytes), 't' a text (elem 'g' GZ string, 'r' char string), 'i' a type-info record.
struct Token {
    char kind;
    char elem;
    std::vector<uint32_t> words;
    std::string text;
};
std::vector<Token> gTape;

void Put(const char *label, Token t)
{
    drawfx::Call c;
    c.note = label;
    c.words = t.words;
    c.words.push_back(uint32_t(t.kind) << 8 | uint32_t(t.elem));
    c.text = t.text;
    drawfx::gLog.push_back(std::move(c));
    difftest::NotCounted quiet;
    gTape.push_back(std::move(t));
}

std::vector<uint32_t> WordsOf(const void *data, uint32_t count, uint32_t bytesEach)
{
    std::vector<uint32_t> v;
    const uint32_t n = data ? count * bytesEach / 4 : 0;
    const uint32_t *p = static_cast<const uint32_t *>(data);
    for (uint32_t i = 0; i < n; ++i) v.push_back(p[i]);
    return v;
}

#define SGGB_BAD(n) \
    virtual uint32_t Bad##n() { drawfx::Note("bad slot " #n, n); return 0; }

struct TapeWriter {
    SGGB_BAD(0) SGGB_BAD(1) SGGB_BAD(2) SGGB_BAD(3)
    virtual uint32_t SetUint8(uint32_t v) { Put("w SetUint8", {'w', 'b', {v & 0xff}, {}}); return 1; } // 4
    SGGB_BAD(5) SGGB_BAD(6) SGGB_BAD(7) SGGB_BAD(8) SGGB_BAD(9)
    virtual uint32_t SetUint32(uint32_t v) { Put("w SetUint32", {'w', 'u', {v}, {}}); return 1; } // 10
    virtual uint32_t SetFloat32(float v) { Put("w SetFloat32", {'f', 'f', {std::bit_cast<uint32_t>(v)}, {}}); return 1; } // 11
    SGGB_BAD(12) SGGB_BAD(13) SGGB_BAD(14)
    virtual uint32_t SetVoid(const void *data, uint32_t size) // 15
    {
        Put("w SetVoid", {'a', 'v', WordsOf(data, size, 1), {}});
        return 1;
    }
    SGGB_BAD(16) SGGB_BAD(17)
    virtual uint32_t SetSint32Array(const void *data, uint32_t count) // 18
    {
        Put("w SetSint32Array", {'a', 's', WordsOf(data, count, 4), {}});
        return 1;
    }
    virtual uint32_t SetUint32Array(const void *data, uint32_t count) // 19
    {
        Put("w SetUint32Array", {'a', 'u', WordsOf(data, count, 4), {}});
        return 1;
    }
    virtual uint32_t SetFloat32Array(const void *data, uint32_t count) // 20
    {
        Put("w SetFloat32Array", {'a', 'f', WordsOf(data, count, 4), {}});
        return 1;
    }
    SGGB_BAD(21) SGGB_BAD(22)
    virtual uint32_t WriteRZCharStr(const char *text) // 23
    {
        Put("w WriteRZCharStr", {'t', 'r', {}, text ? text : "<null>"});
        return 1;
    }
    virtual uint32_t WriteGZStr(const cIGZString *str) // 24
    {
        Put("w WriteGZStr", {'t', 'g', {}, TextOf(str)});
        return 1;
    }
    SGGB_BAD(25) SGGB_BAD(26) SGGB_BAD(27)
    virtual uint32_t WriteTypeInfo(const cPersistTypeInfo *info) // 28
    {
        Put("w WriteTypeInfo", {'i', 'i', {info->mWord4, info->mVersion}, TextOf(info->mName)});
        return 1;
    }
};
TapeWriter gWriter;

Fake gNames[4];
unsigned gNameAt;
struct TapeReader {
    size_t next = 0;
    const Token *Take(const char *kind)
    {
        drawfx::Note(kind, uint32_t(next));
        return next < gTape.size() ? &gTape[next++] : nullptr;
    }
    // A scalar of the kind asked, else 0 (the token is used up either way).
    static uint32_t Scalar(const Token *t, char kind)
    {
        return t && t->kind == kind && !t->words.empty() ? t->words[0] : 0;
    }
    // An array of the element kind asked, else zeros; a short one zero-filled.
    static void Array(const Token *t, char elem, void *data, uint32_t count, uint32_t bytesEach)
    {
        std::memset(data, 0, size_t(count) * bytesEach);
        if (!t || t->kind != 'a' || t->elem != elem) return;
        const size_t bytes = std::min<size_t>(size_t(count) * bytesEach, t->words.size() * 4);
        std::memcpy(data, t->words.data(), bytes);
    }
    SGGB_BAD(0) SGGB_BAD(1) SGGB_BAD(2) SGGB_BAD(3)
    virtual bool GetBool() { return Scalar(Take("r GetBool"), 'w') != 0; } // 4
    SGGB_BAD(5) SGGB_BAD(6) SGGB_BAD(7) SGGB_BAD(8) SGGB_BAD(9)
    virtual uint32_t GetUint32() { return Scalar(Take("r GetUint32"), 'w'); } // 10
    virtual float GetFloat32() { return std::bit_cast<float>(Scalar(Take("r GetFloat32"), 'f')); } // 11
    SGGB_BAD(12) SGGB_BAD(13) SGGB_BAD(14)
    virtual bool GetVoid(void *data, uint32_t size) // 15
    {
        const Token *t = Take("r GetVoid");
        Array(t, 'v', data, size, 1);
        return t != nullptr;
    }
    SGGB_BAD(16) SGGB_BAD(17)
    virtual void GetSint32Array(int32_t *data, uint32_t count) { Array(Take("r GetSint32Array"), 's', data, count, 4); } // 18
    virtual void GetUint32Array(uint32_t *data, uint32_t count) { Array(Take("r GetUint32Array"), 'u', data, count, 4); } // 19
    virtual void GetFloat32Array(float *data, uint32_t count) { Array(Take("r GetFloat32Array"), 'f', data, count, 4); } // 20
    SGGB_BAD(21)
    virtual bool GetGZStr(cIGZString *str) // 22
    {
        const Token *t = Take("r GetGZStr");
        std::string text;
        {
            difftest::NotCounted quiet;
            text = t && t->kind == 't' ? t->text : std::string("tail");
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
        out->mWord4 = t && t->kind == 'i' && t->words.size() > 0 ? t->words[0] : 0;
        out->mVersion = t && t->kind == 'i' && t->words.size() > 1 ? t->words[1] : 0;
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

struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106ab04;
    static constexpr uint32_t kPlanted = 0x5ec0de00; // word i holds kPlanted + i
    uint32_t saved[6];
    nGZEvent::cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    MissingRecordScope()
    {
        std::memcpy(saved, GameAddress<void>(kAt), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(kAt)[i] = kPlanted + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = kPlanted + i; // ours reads its own record
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
    w.manager.Drop();
    w.manager.Use(cells, cGeometryBuilderBehaviorTraits::kEventManagerCell);
}

void NoteWorld()
{
    for (const Fake &l : w.listener) drawfx::Note("listener refs", {l.id, uint32_t(l.refs)});
    for (const Fake &l : w.extra) drawfx::Note("extra listener refs", {l.id, uint32_t(l.refs)});
    for (const Fake &n : gNames) drawfx::Note("type name refs", {n.id, uint32_t(n.refs)});
}

difftest::AllocationCounter *gCounter = nullptr;
long long gWarmBlocks = 0; // blocks the game's lazy one-offs left in the side that warmed up (the kept ones)
struct SideScope {
    difftest::AllocationCounter counter;
    SideScope(difftest::Context &ctx, bool original)
    {
        gCtx = &ctx;
        gCounter = &counter;
        gOriginal = original;
        difftest::NotCounted quiet;
        gBoxes.clear();
    }
    void End()
    {
        for (cIGeometryDataContainer *f : gBoxes) drawfx::Note("container released", f->Release());
        NoteWorld();
        drawfx::Note("blocks left", uint32_t(counter.Blocks() - gWarmBlocks));
        gWarmBlocks = 0;
    }
};

// ---- one builder of the side being run, as its three faces (the test holds one reference on each) ----
struct Builder {
    cIGeometryBuilder *base = nullptr;
    cIGZPersistSerializable2 *persist = nullptr;
    cIIndexedMeshBuilder *own = nullptr;
    uint8_t *object = nullptr;

    cIGeometryBuilder *OwnBase() const { return static_cast<cIGeometryBuilder *>(own); }
};

bool gLoose = false; // the morphs may name vertices past the lists (the Finish helpers are not run then)
bool gKept = false; // a mesh was kept over another (Reset(true)): the game's Finish helpers are not run on such data
bool gFinished = false; // the builder was finished (the game helper run for versions below 10 needs finished data)

Builder Make();
void Drop(const Builder &b);
void Warm();
Builder Make()
{
    Builder b;
    gKept = false;
    gFinished = false;
    Warm();
    cIGZUnknown *const made = gOriginal ? gCtx->Original<FactoryFn>(0xa7ffa0)() : cIndexedMeshBuilder::Create();
    b.base = reinterpret_cast<cIGeometryBuilder *>(made);
    b.object = reinterpret_cast<uint8_t *>(made) - kBuilderFace;
    AddLiveRange(b.object, kObjectSize, kBuilderTag);
    drawfx::Note("first count", b.base->AddRef());
    void *out = nullptr;
    drawfx::Note("query persist", {uint8_t(b.base->QueryInterface(cGeometryBuilder::kIidSerializable, &out)),
                                   drawfx::Ptr(out)});
    b.persist = static_cast<cIGZPersistSerializable2 *>(out);
    out = nullptr;
    drawfx::Note("query own", {uint8_t(b.base->QueryInterface(cIIndexedMeshBuilder::kIid, &out)), drawfx::Ptr(out)});
    b.own = static_cast<cIIndexedMeshBuilder *>(out);
    return b;
}

void Drop(const Builder &b)
{
    drawfx::Note("persist release", b.persist->Release());
    drawfx::Note("own release", b.OwnBase()->Release());
    drawfx::Note("last release", b.base->Release());
}

// ---- dumping the object ----
struct RawVec {
    const uint8_t *begin, *end, *cap;
};

void DumpVec(const char *label, const uint8_t *object, uint32_t offset, uint32_t elem)
{
    const RawVec &v = *reinterpret_cast<const RawVec *>(object + offset);
    drawfx::Call c;
    c.note = label;
    const uintptr_t bytes = uintptr_t(v.end) - uintptr_t(v.begin);
    c.words.push_back(uint32_t(bytes / elem));
    c.words.push_back(v.begin != nullptr);
    const uint32_t *p = reinterpret_cast<const uint32_t *>(v.begin);
    for (uint32_t i = 0; i < bytes / 4 && i < 8192; ++i) c.words.push_back(p[i]);
    drawfx::gLog.push_back(std::move(c));
}

void DumpWordsAt(const char *label, const uint8_t *object, uint32_t offset, uint32_t count)
{
    drawfx::Call c;
    c.note = label;
    const uint32_t *p = reinterpret_cast<const uint32_t *>(object + offset);
    for (uint32_t i = 0; i < count; ++i) c.words.push_back(p[i]);
    drawfx::gLog.push_back(std::move(c));
}

// The collected mesh and the cursors, read from the object's memory (the layout is the game's, which is the point).
void NoteObject(const Builder &b, const char *when)
{
    const uint8_t *o = b.object;
    drawfx::Note("object", {uint32_t(when[0]), uint32_t(when[1])});
    DumpVec("positions", o, 0x14, 12);
    DumpVec("normals", o, 0x20, 12);
    for (uint32_t k = 0; k < 4; ++k) DumpVec("texcoords", o, 0x2c + 12 * k, 8);
    for (uint32_t k = 0; k < 2; ++k) DumpVec("colours", o, 0x5c + 12 * k, 4);
    DumpWordsAt("word74", o, 0x74, 1);
    DumpVec("words A", o, 0x78, 4);
    DumpVec("words B", o, 0x84, 4);
    const RawVec &groups = *reinterpret_cast<const RawVec *>(o + 0x90);
    const uint32_t groupCount = uint32_t((uintptr_t(groups.end) - uintptr_t(groups.begin)) / 0x80);
    drawfx::Note("group count", groupCount);
    for (uint32_t g = 0; g < groupCount && g < 64; ++g) {
        const uint8_t *group = groups.begin + 0x80 * g;
        DumpWordsAt("group head", group, 0, 2);
        static const uint32_t kLists[] = {8, 0x14, 0x20, 0x2c, 0x38, 0x44, 0x50, 0x5c, 0x68, 0x74};
        for (uint32_t list : kLists) {
            drawfx::Note("group list at", list + 0x100 * g + 0x10000 * uint32_t(when[0]) + 0x1000000 * uint32_t(when[1])); // which list, for the failure message
            DumpVec("group list", group, list, 4);
        }
    }
    DumpVec("runs", o, 0x9c, 16);
    const auto &names = *reinterpret_cast<const std::vector<std::string> *>(o + 0xa8);
    drawfx::Note("name count", uint32_t(names.size()));
    for (const std::string &name : names) sgfx::NoteText("name", name.c_str());
    DumpVec("bones", o, 0xb4, 0x1c);
    const RawVec &influences = *reinterpret_cast<const RawVec *>(o + 0xc0);
    const uint32_t influenceCount = uint32_t((uintptr_t(influences.end) - uintptr_t(influences.begin)) / 0x18);
    drawfx::Note("influence count", influenceCount);
    for (uint32_t i = 0; i < influenceCount && i < 64; ++i) {
        DumpVec("influence vertices", influences.begin + 0x18 * i, 0, 4);
        DumpVec("influence weights", influences.begin + 0x18 * i, 0xc, 4);
    }
    const RawVec &morphs = *reinterpret_cast<const RawVec *>(o + 0xcc);
    const uint32_t morphCount = uint32_t((uintptr_t(morphs.end) - uintptr_t(morphs.begin)) / 0x70);
    drawfx::Note("morph count", morphCount);
    for (uint32_t i = 0; i < morphCount && i < 64; ++i) {
        const uint8_t *m = morphs.begin + 0x70 * i;
        sgfx::NoteText("morph name 1", reinterpret_cast<const std::string *>(m + 4)->c_str());
        sgfx::NoteText("morph name 2", reinterpret_cast<const std::string *>(m + 0x24)->c_str());
        drawfx::Note("morph refs", {*reinterpret_cast<const uint32_t *>(m + 0x1c), *reinterpret_cast<const uint32_t *>(m + 0x3c)});
        DumpVec("morph position vertices", m, 0x40, 4);
        DumpVec("morph position deltas", m, 0x4c, 12);
        DumpVec("morph normal vertices", m, 0x58, 4);
        DumpVec("morph normal deltas", m, 0x64, 12);
    }
    DumpWordsAt("transform and normal matrix", o, 0xe0, 22);
    drawfx::Note("flags", {o[0x138], o[0x139]});
    DumpWordsAt("cursors", o, 0x13c, 12);
    for (uint32_t k = 0; k < 8; ++k) DumpVec("index list", o, 0x16c + 12 * k, 4);
    // The base's words: the count, the extra listener, the vbtables' entries, the vtordisps.
    auto word = [&](uint32_t off) { return *reinterpret_cast<const uint32_t *>(o + off); };
    drawfx::Call c;
    c.note = "base words";
    c.words = {word(8), drawfx::Ptr(word(0x10))};
    const auto *vbtable = reinterpret_cast<const int32_t *>(word(4));
    c.words.insert(c.words.end(), {uint32_t(vbtable[0]), uint32_t(vbtable[1]), uint32_t(vbtable[2])});
    const auto *ownVbtable = reinterpret_cast<const int32_t *>(word(kOwnFace + 4));
    c.words.insert(c.words.end(), {uint32_t(ownVbtable[0]), uint32_t(ownVbtable[1])});
    c.words.push_back(word(kBuilderFace - 4));
    c.words.push_back(word(kOwnFace - 4));
    drawfx::gLog.push_back(std::move(c));
}

void NoteCounts(const Builder &b)
{
    cIIndexedMeshBuilder *m = b.own;
    drawfx::Note("counts", {m->CountBones(), m->CountMorphTargets(), m->CountPositions(), m->CountNormals(),
                            uint32_t(m->IsCollecting()), uint32_t(m->GetUnknown())});
    drawfx::Note("set counts", {m->CountTexCoords(0), m->CountTexCoords(1), m->CountTexCoords(2), m->CountTexCoords(3),
                                m->CountColours(0), m->CountColours(1)});
}

// ---- synthetic meshes ----
const char *const kNames[] = {"body", "BODY", "Body", "hair_lod0", "", "tops", "a_group_name_longer_than_sixteen", "x"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);

float SmallFloat(std::mt19937 &rng)
{
    const int32_t i = int32_t(rng() % 41) - 20;
    return rng() % 2 ? float(i) : float(i) * 0.25f + 0.125f;
}

// A colour channel: mostly 0..1, sometimes past either end, and values at the rounding points.
float ChannelValue(std::mt19937 &rng)
{
    switch (rng() % 10) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 2: return -0.4f;
    case 3: return 1.7f;
    case 4: return 0.5f / 255.0f;
    case 5: return 254.5f / 255.0f;
    case 6: return 0.5f;
    default: return float(rng() % 1001) / 1000.0f;
    }
}

// A weight or delta part: often the given value, else one around the thresholds the classification tests (1e-4 for a weight;
// a delta whose squared length passes 9.999999e-9, 1e-4 along one axis does by a hair).
float Weight(std::mt19937 &rng, float plain)
{
    switch (rng() % 8) {
    case 0: return 1.0e-4f;
    case 1: return std::nextafter(1.0e-4f, 1.0f);
    case 2: return std::nextafter(1.0e-4f, 0.0f);
    case 3: return 5.0e-5f;
    case 4: return 0.0f;
    default: return plain;
    }
}

std::vector<float> Floats(uint32_t n, std::mt19937 &rng)
{
    std::vector<float> v(n);
    for (float &x : v) x = SmallFloat(rng);
    return v;
}

void RandomTransform(cIIndexedMeshBuilder *m, std::mt19937 &rng)
{
    AffineTransform3 t{};
    t.mUnknown0 = rng() % 4;
    for (int i = 0; i < 9; ++i) t.mMatrix[i] = (i % 4 == 0) ? 0.5f + float(rng() % 7) * 0.25f : float(int(rng() % 5) - 2) * 0.125f;
    for (float &x : t.mOffset) x = SmallFloat(rng);
    m->SetTransform(&t);
}

void AddAttributes(cIIndexedMeshBuilder *m, std::mt19937 &rng)
{
    const uint32_t np = rng() % 8, nn = rng() % 8;
    if (np) {
        const std::vector<float> data = Floats(np * 3, rng);
        if (rng() % 2) m->AddPositions(np, data.data());
        else
            for (uint32_t i = 0; i < np; ++i) m->AddPosition(&data[3 * i]);
    }
    if (nn) {
        std::vector<float> data = Floats(nn * 3, rng);
        if (rng() % 4 == 0) std::fill(data.begin(), data.begin() + 3, 0.0f); // a zero normal: 1/0
        if (rng() % 2) m->AddNormals(nn, data.data());
        else
            for (uint32_t i = 0; i < nn; ++i) m->AddNormal(&data[3 * i]);
    }
    for (uint32_t set = 0; set < 4; ++set) {
        const uint32_t n = rng() % 3 == 0 ? 0 : rng() % 6;
        if (!n) continue;
        const std::vector<float> data = Floats(n * 2, rng);
        if (rng() % 2) m->AddTexCoords(n, data.data(), set);
        else
            for (uint32_t i = 0; i < n; ++i) m->AddTexCoord(&data[2 * i], set);
    }
    for (uint32_t set = 0; set < 2; ++set) {
        const uint32_t n = rng() % 2 == 0 ? 0 : rng() % 5;
        if (!n) continue;
        std::vector<float> data(n * 4);
        for (float &x : data) x = ChannelValue(rng);
        if (rng() % 2) m->AddColours(n, data.data(), set);
        else
            for (uint32_t i = 0; i < n; ++i) m->AddColour(&data[4 * i], set);
    }
    if (rng() % 3 == 0) {
        const uint32_t wn = 1 + rng() % 4;
        std::vector<uint32_t> words(gLoose ? wn : m->CountPositions()); // one a vertex when the helpers will read them
        for (uint32_t &x : words) x = rng() % 3 ? 0 : rng() % 100;
        m->AddWordsA(uint32_t(words.size()), words.data());
    }
    if (rng() % 3 == 0) {
        const uint32_t wn = 1 + rng() % 4;
        std::vector<uint32_t> words(gLoose ? wn : m->CountPositions()); // one a vertex when the helpers will read them
        for (uint32_t &x : words) x = rng() % 3 ? 0 : rng() % 100;
        m->AddWordsB(uint32_t(words.size()), words.data());
    }
}

void AddBones(cIIndexedMeshBuilder *m, std::mt19937 &rng)
{
    const uint32_t np = m->CountPositions();
    if (!np || rng() % 2) return;
    const uint32_t nb = 1 + rng() % 3;
    m->SetBoneCount(nb);
    for (uint32_t b = 0; b < nb; ++b) {
        if (rng() % 3) {
            const std::vector<float> data = Floats(7, rng);
            m->SetBoneData(b, data.data());
        }
        if (rng() % 2) {
            const uint32_t k = 1 + rng() % 4;
            std::vector<uint32_t> vertices(k);
            for (uint32_t &v : vertices) v = rng() % np;
            std::vector<float> weights = Floats(k, rng);
            for (float &w : weights) w = Weight(rng, w);
            m->AddInfluences(b, k, vertices.data(), weights.data());
        }
        for (uint32_t k = rng() % 3; k; --k) m->AddInfluence(b, rng() % np, Weight(rng, SmallFloat(rng)));
    }
}

void AddMorphs(cIIndexedMeshBuilder *m, std::mt19937 &rng)
{
    const uint32_t np = m->CountPositions(), nn = m->CountNormals();
    if (!np || !nn || rng() % 2) return;
    const uint32_t nm = 1 + rng() % 2;
    m->SetMorphTargetCount(nm);
    for (uint32_t i = 0; i < nm; ++i) {
        m->SetMorphNames(i, kNames[rng() % kNameCount], kNames[rng() % kNameCount]);
        for (uint32_t k = 1 + rng() % 5; k; --k) {
            std::vector<float> data = Floats(3 * 8, rng);
            if (rng() % 2)
                for (float &x : data) x = Weight(rng, rng() % 2 ? 0.0f : x);
            std::vector<int32_t> vertices(8);
            const unsigned kind = rng() % 8;
            const uint32_t limit = kind % 2 == 0 ? np : nn; // the position kinds are the even ones, the normal ones the odd
            for (int32_t &v : vertices) v = int32_t(rng() % (gLoose ? 4 : limit));
            switch (kind) {
            case 0: m->AddMorphPositions(i, 1 + rng() % np, data.data()); break;
            case 1: m->AddMorphNormals(i, 1 + rng() % nn, data.data()); break;
            case 2: m->SetMorphPositions(i, rng() % 5, data.data(), vertices.data()); break;
            case 3: m->SetMorphNormals(i, rng() % 5, data.data(), vertices.data()); break;
            case 4: m->AddMorphPosition(i, int32_t(rng() % np), data.data()); break;
            case 5: m->AddMorphNormal(i, int32_t(rng() % nn), data.data()); break;
            case 6: m->AddMorphPositionAbsolute(i, int32_t(rng() % (gLoose ? 6 : np)), data.data()); break;
            default: m->AddMorphNormalRaw(i, int32_t(rng() % (gLoose ? 6 : nn)), data.data()); break;
            }
        }
    }
}

// One vertex index list: `count` entries, below `below`, by one of the forms; attribute 0 positions, 1 normals, 2-5
// texture coordinate sets, 6-7 colour sets.
void AddIndexList(cIIndexedMeshBuilder *m, std::mt19937 &rng, uint32_t attribute, uint32_t count, uint32_t below)
{
    const int32_t offset = (below > 1 && rng() % 3 == 0) ? (rng() % 2 ? 1 : -1) : 0;
    std::vector<uint32_t> values(count);
    for (uint32_t &v : values) v = offset < 0 ? 1 + rng() % (below - 1) : rng() % (below - offset);
    switch (rng() % 3) {
    case 0: // the batch slots
        switch (attribute) {
        case 0: m->AddPositionIndices(count, values.data(), offset); break;
        case 1: m->AddNormalIndices(count, values.data(), offset); break;
        case 2: case 3: case 4: case 5: m->AddTexCoordIndices(count, values.data(), attribute - 2, offset); break;
        default: m->AddColourIndices(count, values.data(), attribute - 6, offset); break;
        }
        break;
    case 1: // the flag form, one kind
        m->AddIndices(uint8_t(1u << attribute), count, values.data(), offset);
        break;
    default: // singles (the offset added here)
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t v = values[i] + offset;
            if (rng() % 2) {
                m->AddIndex(uint8_t(1u << attribute), v);
            } else {
                switch (attribute) {
                case 0: m->AddPositionIndex(v); break;
                case 1: m->AddNormalIndex(v); break;
                case 2: case 3: case 4: case 5: m->AddTexCoordIndex(v, attribute - 2); break;
                default: m->AddColourIndex(v, attribute - 6); break;
                }
            }
        }
        break;
    }
}

// One group: a name, the vertices' index lists (a list per attribute chosen, one entry per vertex or one for all), and
// CommitGroup.
void AddGroup(cIIndexedMeshBuilder *m, std::mt19937 &rng)
{
    const uint32_t np = m->CountPositions();
    if (!np) return;
    if (rng() % 5) m->SetGroupName(kNames[rng() % kNameCount]);
    const uint32_t nv = 1 + rng() % 6;
    uint32_t below[8] = {np, m->CountNormals(), m->CountTexCoords(0), m->CountTexCoords(1), m->CountTexCoords(2),
                         m->CountTexCoords(3), m->CountColours(0), m->CountColours(1)};
    AddIndexList(m, rng, 0, nv, np);
    for (uint32_t a = 1; a < 8; ++a) {
        if (!below[a] || rng() % 6 == 0) continue; // a group may leave an attribute out: its flags (the mask) differ
        AddIndexList(m, rng, a, nv > 1 && rng() % 4 == 0 ? 1 : nv, below[a]);
    }
    if (rng() % 8 == 0) m->MarkUnknown();
    drawfx::Note("unknown", m->GetUnknown());
    m->CommitGroup();
    drawfx::Note("unknown after", m->GetUnknown());
}

// The whole of a mesh: stages noted as they go.
void MakeMesh(const Builder &b, std::mt19937 &rng, bool keep)
{
    cIIndexedMeshBuilder *m = b.own;
    m->Reset(keep);
    if (keep) gKept = true;
    gLoose = rng() % 4 == 0;
    NoteObject(b, "Rs");
    NoteCounts(b);
    if (rng() % 3 == 0) RandomTransform(m, rng);
    AddAttributes(m, rng);
    NoteObject(b, "At");
    NoteCounts(b);
    AddBones(m, rng);
    AddMorphs(m, rng);
    NoteObject(b, "BM");
    NoteCounts(b);
    for (uint32_t g = rng() % 5; g; --g) AddGroup(m, rng);
    NoteObject(b, "Gr");
}

// ---- the container, dumped ----
void DumpRange32(const char *label, cGeometryRange<uint32_t> &r)
{
    drawfx::Call c;
    c.note = label;
    uint32_t n = 0;
    while (!r.mCurrent->IsEqual(r.mEnd) && n < 4096) {
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

void DumpRange16(const char *label, cGeometryRange<uint16_t> &r)
{
    drawfx::Call c;
    c.note = label;
    uint32_t n = 0;
    while (!r.mCurrent->IsEqual(r.mEnd) && n < 4096) {
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

void DumpContainer(cIGeometryDataContainer *f)
{
    const uint32_t ne = f->CountElements(), nl = f->CountLinkages(), ng = f->CountGroups();
    drawfx::Note("counts", {ne, nl, ng, f->CountTransforms(), f->CountNamePairs()});
    for (uint32_t i = 0; i < ne; ++i) {
        cGeometryElementHeader h{};
        f->GetElementHeader(&h, i);
        drawfx::Note("element", {h.mIndex, h.mNumber, h.mIdentity, h.mRepetition, h.mBlockFormat, h.mSetFormat});
        cGeometryRange<uint32_t> r;
        f->GetElementWords(&r, i);
        DumpRange32("element words", r);
        cGeometryRange<uint16_t> x;
        f->GetElementIndices(&x, i);
        DumpRange16("element indices", x);
    }
    for (uint32_t i = 0; i < nl; ++i) {
        cGeometryLinkageHeader h{};
        f->GetLinkageHeader(&h, i);
        drawfx::Note("linkage", {h.mIndex, h.mActiveCount, h.mReferencedSize});
        cGeometryRange<uint16_t> r;
        f->GetLinkageElements(&r, i);
        DumpRange16("linkage elements", r);
        for (uint32_t which = 0; which < 3; ++which) {
            drawfx::Note("has aliases", f->LinkageHas(i, which));
            cGeometryRange<uint16_t> a;
            f->GetLinkageAliases(&a, i, which);
            DumpRange16("linkage aliases", a);
        }
    }
    for (uint32_t i = 0; i < ng; ++i) {
        cGeometryGroupInfo g{};
        f->GetGroupInfo(&g, i);
        drawfx::Note("group", {g.mIndex, g.mPrimType, g.mIndexCount, g.mLinkIndex, uint32_t(g.mWord10), g.mBoneCount});
        sgfx::NoteText("group name", g.mName);
        cGeometryRange<uint16_t> r;
        f->GetGroupVertexIndices(&r, i);
        DumpRange16("group indices", r);
    }
    for (uint32_t i = 0; i < f->CountTransforms(); ++i) {
        cGeometryTransform t{};
        f->GetTransform(&t, i);
        uint32_t words[7];
        std::memcpy(words, &t, sizeof words);
        drawfx::gLog.push_back([&] {
            drawfx::Call c;
            c.note = "transform";
            c.words.assign(words, words + 7);
            return c;
        }());
    }
    for (uint32_t i = 0; i < f->CountNamePairs(); ++i) {
        sgfx::NoteText("name first", f->GetNameFirst(i));
        sgfx::NoteText("name second", f->GetNameSecond(i));
    }
}

// ---- the runs ----
// Finish is the half of the cases whose data the helpers (the game's) take: the lists they index are consistent.
void FinishSometimes(const Builder &b, std::mt19937 &rng)
{
    if (rng() % 2 || gKept || gLoose) return;
    sgfx::MarkStep(*gCtx, "finish");
    b.own->Finish();
    gFinished = true;
    NoteObject(b, "Fi");
    NoteCounts(b);
}

// What the game's Build can walk: every run closed, naming a group and a name that exist, over triangles that exist (a
// builder not finished, or read from a cut tape, can hold runs that do not; Build on those runs off the lists).
bool BuildSafe(const Builder &b)
{
    const uint8_t *o = b.object;
    const RawVec &gs = *reinterpret_cast<const RawVec *>(o + 0x90);
    const RawVec &rs = *reinterpret_cast<const RawVec *>(o + 0x9c);
    const auto &names = *reinterpret_cast<const std::vector<std::string> *>(o + 0xa8);
    const uint32_t groups = uint32_t((gs.end - gs.begin) / 0x80);
    for (const uint8_t *r = rs.begin; r != rs.end; r += 16) {
        const int32_t name = *reinterpret_cast<const int32_t *>(r), group = *reinterpret_cast<const int32_t *>(r + 4);
        const int32_t start = *reinterpret_cast<const int32_t *>(r + 8), end = *reinterpret_cast<const int32_t *>(r + 12);
        if (name < -1 || name >= int32_t(names.size()) || group < 0 || uint32_t(group) >= groups) return false;
        const RawVec &tri = *reinterpret_cast<const RawVec *>(gs.begin + 0x80 * group + 0x74);
        if (start < 0 || end < start || end > int32_t((tri.end - tri.begin) / 4)) return false;
    }
    return true;
}

void BuildAndDump(const Builder &b, std::mt19937 &rng)
{
    if (!BuildSafe(b)) {
        drawfx::Note("build skipped", 0);
        return;
    }
    if (rng() % 3 == 0) {
        // Group flags 10 / 11 (the extra word lists) are set by the game's finalize helpers; set here on both sides so
        // Build's linkage numbering after the morph elements is walked with and without them.
        const RawVec &gs = *reinterpret_cast<const RawVec *>(b.object + 0x90);
        for (const uint8_t *g = gs.begin; g != gs.end; g += 0x80)
            if (rng() % 2) *reinterpret_cast<uint32_t *>(const_cast<uint8_t *>(g)) |= (rng() % 3 + 1) << 10;
        drawfx::Note("flags poked", 0);
    }
    cIGeometryDataContainer *f = NewBox();
    sgfx::MarkStep(*gCtx, "build");
    b.base->Build(f);
    DumpContainer(f);
    if (rng() % 4 == 0) { // built again over the same container
        sgfx::MarkStep(*gCtx, "second build");
        b.base->Build(f);
        DumpContainer(f);
    }
}

// The game's code keeps a few blocks it takes the first time (its lazy one-offs): a whole first object, mesh, Finish and
// Build, on each side, before the counted runs.
void Warm()
{
    static bool warmedSide[2] = {false, false};
    bool &warmed = warmedSide[gOriginal ? 1 : 0];
    if (warmed) return;
    warmed = true;
    difftest::NotCounted quiet;
    const size_t notes = drawfx::gLog.size(), boxes = gBoxes.size();
    std::mt19937 rng(12345);
    const Builder w1 = Make();
    MakeMesh(w1, rng, false);
    if (!gLoose) w1.own->Finish();
    BuildAndDump(w1, rng);
    Drop(w1);
    drawfx::gLog.erase(drawfx::gLog.begin() + notes, drawfx::gLog.end());
    for (size_t i = boxes; i < gBoxes.size(); ++i) gBoxes[i]->Release();
    gBoxes.resize(boxes);
    gWarmBlocks = gCounter->Blocks();
}

void RunCollect(difftest::Context &ctx)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    MissingRecordScope missing;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "indexed mesh collect", 200, BuildWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        const Builder b = Make();
        sgfx::MarkStep(ctx, "first mesh");
        MakeMesh(b, rng, false);
        FinishSometimes(b, rng);
        for (unsigned again = rng() % 3; again; --again) {
            sgfx::MarkStep(ctx, "next mesh");
            MakeMesh(b, rng, rng() % 2 == 0);
            FinishSometimes(b, rng);
        }
        Drop(b);
        side.End();
    });
    w.manager.Drop();
}

void RunBuild(difftest::Context &ctx)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    MissingRecordScope missing;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "indexed mesh build", 200, BuildWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        const Builder b = Make();
        MakeMesh(b, rng, false);
        FinishSometimes(b, rng);
        BuildAndDump(b, rng);
        if (rng() % 3 == 0) { // a second mesh kept after the first
            MakeMesh(b, rng, true);
            FinishSometimes(b, rng);
            BuildAndDump(b, rng);
        }
        if (rng() % 6 == 0) { // an empty builder
            const Builder e = Make();
            cIGeometryDataContainer *f = NewBox();
            e.base->Build(f);
            DumpContainer(f);
            Drop(e);
        }
        Drop(b);
        side.End();
    });
    w.manager.Drop();
}

// ---- persistence ----
// The current tape made one of the old bone format (a matrix, then the position as three floats, per bone) with the
// record's version set to `version` (below 9).
void MakeLegacy(uint32_t version, std::mt19937 &rng)
{
    difftest::NotCounted quiet;
    size_t at = 0;
    while (at < gTape.size() && at < 4 && gTape[at].kind == 'i') ++at;
    if (at == 0 || at >= gTape.size()) return;
    gTape[0].words[1] = version;
    auto skipList = [&](size_t &i) {
        const bool any = i < gTape.size() && gTape[i].words[0] != 0;
        ++i;
        if (any) ++i;
    };
    skipList(at); // positions
    skipList(at); // normals
    for (int k = 0; k < 6; ++k) skipList(at); // texture coordinates and colours
    ++at;                                      // the word at 0x74
    if (at >= gTape.size()) return;
    const uint32_t bones = gTape[at].words[0];
    ++at;
    if (bones == 0) return;
    at += 3 * size_t(bones); // the influences (all written when there are bones: the counts agree)
    std::vector<Token> legacy;
    for (uint32_t i = 0; i < bones && at + 1 < gTape.size(); ++i) {
        Token matrix{'a', 'f', std::vector<uint32_t>(9), {}};
        for (uint32_t &x : matrix.words) x = std::bit_cast<uint32_t>(SmallFloat(rng) * 0.1f);
        legacy.push_back({'w', 'u', {rng() % 5}, {}});
        legacy.push_back(std::move(matrix));
        for (uint32_t k = 0; k < 3; ++k) legacy.push_back({'f', 'f', {gTape[at + 1].words.size() > k ? gTape[at + 1].words[k] : 0}, {}});
        at += 2;
    }
    // Replace the bones' tokens (2 each) by the legacy ones (5 each).
    const size_t first = at - 2 * size_t(bones);
    gTape.erase(gTape.begin() + first, gTape.begin() + at);
    gTape.insert(gTape.begin() + first, legacy.begin(), legacy.end());
}

// A tape of an older version: the group flags a version below 10 has not classified yet. The current writer puts the
// classified bits (7 to 11: 0x80, 0x100, 0x200, 0x400, 0x800) in the tape, so reading it back with ClassifyGroups skipped
// would give the same flags; cleared, a read that skips the classification (or runs it on the wrong versions) shows. The
// group section is found from the end of the tape (the runs and the names after it), with the sizes read from `from`.
// Whether ClassifyGroups can walk the builder's data: it indexes lists of the vertices the groups, influences and morphs
// name without a check, so a tape claiming a version below 10 is only made from data where every index is in range.
bool ClassifySafe(const Builder &from)
{
    auto count = [](const uint8_t *vec, size_t each) {
        const RawVec &v = *reinterpret_cast<const RawVec *>(vec);
        return size_t(v.end - v.begin) / each;
    };
    auto at = [](const uint8_t *vec, size_t i) { return reinterpret_cast<const uint32_t *>(reinterpret_cast<const RawVec *>(vec)->begin)[i]; };
    const uint8_t *const o = from.object;
    const size_t np = count(o + 0x14, 12), nn = count(o + 0x20, 12);
    const RawVec &groups = *reinterpret_cast<const RawVec *>(o + 0x90);
    for (const uint8_t *g = groups.begin; g != groups.end; g += 0x80) {
        const size_t n = *reinterpret_cast<const uint32_t *>(g + 4);
        if (count(g + 8, 4) != n) return false;
        for (size_t i = 0; i < n; ++i)
            if (at(g + 8, i) >= np) return false;
        // The word lists at 0x78 and 0x84 (when not empty) are indexed by the same positions (the 0x400 / 0x800 bits):
        // past their end the game reads heap garbage, and the bits then differ by address (VH: persist seed 10, group
        // head 0x13d against 0x53d, backlog 2026-10-30).
        for (uint32_t list : {0x78u, 0x84u})
            if (const size_t words = count(o + list, 4))
                for (size_t i = 0; i < n; ++i)
                    if (at(g + 8, i) >= words) return false;
        if (count(g + 0x14, 4) && n) {
            if (count(g + 0x14, 4) < n) return false;
            for (size_t i = 0; i < n; ++i)
                if (at(g + 0x14, i) >= nn) return false;
        }
    }
    const RawVec &influences = *reinterpret_cast<const RawVec *>(o + 0xc0);
    for (const uint8_t *in = influences.begin; in != influences.end; in += 0x18) {
        const size_t n = count(in, 4);
        if (count(in + 0xc, 4) < n) return false;
        for (size_t i = 0; i < n; ++i)
            if (at(in, i) >= np) return false;
    }
    const RawVec &morphs = *reinterpret_cast<const RawVec *>(o + 0xcc);
    for (const uint8_t *m = morphs.begin; m != morphs.end; m += 0x70) {
        const size_t pn = count(m + 0x40, 4), nv = count(m + 0x58, 4);
        if (count(m + 0x4c, 12) < pn || count(m + 0x64, 12) < nv) return false;
        for (size_t i = 0; i < pn; ++i)
            if (at(m + 0x40, i) >= np) return false;
        for (size_t i = 0; i < nv; ++i)
            if (at(m + 0x58, i) >= nn) return false;
    }
    return true;
}

void ClearClassifierBits(const Builder &from)
{
    difftest::NotCounted quiet;
    auto size = [](const uint8_t *vec) { return size_t(reinterpret_cast<const RawVec *>(vec)->end - reinterpret_cast<const RawVec *>(vec)->begin) / 4; };
    const uint8_t *const o = from.object;
    const RawVec &groups = *reinterpret_cast<const RawVec *>(o + 0x90);
    const size_t groupCount = size_t(groups.end - groups.begin) / 0x80;
    const size_t runBytes = size_t(reinterpret_cast<const RawVec *>(o + 0x9c)->end - reinterpret_cast<const RawVec *>(o + 0x9c)->begin);
    const auto &names = *reinterpret_cast<const std::vector<std::string> *>(o + 0xa8);
    const size_t tail = (runBytes ? 2 : 1) + 1 + names.size();
    size_t tokens = 0;
    for (size_t g = 0; g < groupCount; ++g) {
        const uint8_t *group = groups.begin + 0x80 * g;
        tokens += 2 + (size(group + 8) ? 1 : 0);
        for (uint32_t list : {0x14u, 0x68u, 0x74u}) tokens += 1 + (size(group + list) ? 1 : 0);
    }
    if (gTape.size() < tail + tokens) return;
    size_t at = gTape.size() - tail - tokens;
    for (size_t g = 0; g < groupCount; ++g) {
        const uint8_t *group = groups.begin + 0x80 * g;
        if (gTape[at].kind != 'w' || gTape[at].words.empty()) return; // not where the layout says: leave the tape alone
        if (gTape[at].words[0] > 0xfff || gTape[at + 1].words.empty() || gTape[at + 1].words[0] != size(group + 8)) return;
        gTape[at].words[0] &= ~0xf80u;
        at += 2 + (size(group + 8) ? 1 : 0);
        for (uint32_t list : {0x14u, 0x68u, 0x74u}) at += 1 + (size(group + list) ? 1 : 0);
    }
}

void WriteRead(const Builder &from, const Builder &into, std::mt19937 &rng)
{
    ClearTape();
    from.persist->Write(Writer());
    drawfx::Note("tape", uint32_t(gTape.size()));
    for (const Token &t : gTape) {
        drawfx::Call c;
        c.note = "tape token";
        c.words.push_back(uint32_t(t.kind));
        c.words.push_back(uint32_t(t.elem));
        c.words.insert(c.words.end(), t.words.begin(), t.words.begin() + std::min<size_t>(t.words.size(), 64));
        c.text = t.text;
        drawfx::gLog.push_back(std::move(c));
    }
    {
        difftest::NotCounted quiet;
        const bool whole = gFinished && ClassifySafe(from); // (what the classification the game runs for versions below 10 can walk)
        switch (rng() % 8) {
        case 0: case 5: case 6: gTape[0].words[1] = whole ? 9 : 11; break;
        case 1: if (whole) MakeLegacy(8, rng); break; // (the game helper run for versions below 10 needs whole data)
        case 2: if (whole) MakeLegacy(1 + rng() % 7, rng); break;
        case 3: gTape[0].words[1] = (whole ? 9 : 10) + rng() % 3; break;
        case 4: gTape[0].words[1] = whole ? 9 : 10; break;
        default: break;
        }
        if (gTape[0].words[1] < 10) ClearClassifierBits(from);
        // A short tape (the helper the game runs after reading a version below 10 needs whole lists, so only past that).
        const size_t cut = rng() % (gTape.size() + 1);
        if (rng() % 8 == 0 && cut > 0 && gTape[0].words[1] >= 10) gTape.resize(cut);
    }
    into.persist->Read(Reader());
    drawfx::Note("read to", uint32_t(gReader.next));
}

void RunPersist(difftest::Context &ctx)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    MissingRecordScope missing;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "indexed mesh persist", 200, BuildWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        const Builder b = Make();
        const Builder c = Make();
        MakeMesh(b, rng, false);
        if (ClassifySafe(b)) FinishSometimes(b, rng); // (Finish classifies the groups, trusting the indices)
        ClearTape();
        b.persist->Write(Writer());
        drawfx::Note("tape", uint32_t(gTape.size()));
        for (unsigned round = 1 + rng() % 3; round; --round) {
            sgfx::MarkStep(ctx, "read");
            WriteRead(b, c, rng);
            NoteObject(c, "Rd");
            NoteCounts(c);
            ClearTape();
            c.persist->Write(Writer());
            drawfx::Note("tape of the read", uint32_t(gTape.size()));
        }
        BuildAndDump(c, rng);
        Drop(c);
        Drop(b);
        side.End();
    });
    w.manager.Drop();
}

// ---- the faces ----
const uint32_t kIids[] = {0,          1,          cGeometryBuilder::kIidSerializable, cGeometryBuilder::kIidBehavior,
                          0xfb859324, 0xbbb79c90, cIIndexedMeshBuilder::kIid, 0x9bffc102, 0x12345678};

void RunFaces(difftest::Context &ctx)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    MissingRecordScope missing;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "indexed mesh faces", 48, BuildWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        const Builder b = Make();
        NoteObject(b, "Ne");
        NoteCounts(b);
        for (int k = 0; k < 24; ++k) {
            const unsigned op = rng() % 8;
            drawfx::Note("op", op);
            switch (op) {
            case 0: {
                const uint32_t iid = kIids[rng() % 9];
                void *out = nullptr;
                void **const where = rng() % 8 ? &out : nullptr;
                const bool r = rng() % 2 ? b.base->QueryInterface(iid, where) : b.persist->QueryInterface(iid, where);
                drawfx::Note("query", {iid, uint8_t(r), drawfx::Ptr(out)});
                if (!out || iid == 0xfb859324) break;
                if (iid == cIIndexedMeshBuilder::kIid)
                    static_cast<cIGeometryBuilder *>(static_cast<cIIndexedMeshBuilder *>(out))->Release();
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
            case 3: sgfx::NoteText("name", b.base->GetName()); break;
            case 4: {
                const unsigned which = rng() % 3;
                nGZEvent::cIAbstractBehaviorType *const listener =
                    which < 2 ? reinterpret_cast<nGZEvent::cIAbstractBehaviorType *>(&w.extra[which]) : nullptr;
                drawfx::Note("set listener", drawfx::Ptr(b.base->SetListener(listener)));
                break;
            }
            case 5: { // the slots the thunks reach on their own: OnQuery, AskListeners, Slot7
                const uint32_t event = rng() % 3 ? kFinishEvent : 0x1234;
                drawfx::Note("on query", uint8_t(b.base->OnQuery(event, rng(), nullptr)));
                drawfx::Note("asked", uint8_t(b.base->AskListeners(event, rng(), nullptr)));
                b.base->SetAttribute(rng(), rng(), rng());
                break;
            }
            case 6: { // the registry slots: count, ids, the attribute of a known and an unknown event
                drawfx::Note("event count", b.base->GetEventCount());
                nGZEvent::cAttributeIDRange ids = b.base->GetEventIds();
                unsigned steps = 0;
                while (steps < 20 && !ids.mCurrent->IsEqual(ids.mEnd)) {
                    drawfx::Note("event id", ids.mCurrent->Get());
                    ids.mCurrent->Next();
                    ++steps;
                }
                drawfx::Note("event ids", {steps, ids.mCurrent->Release(), ids.mBegin->Release(), ids.mEnd->Release()});
                const nGZEvent::cEventAttribute a = b.base->GetEventAttribute(rng() % 2 ? kFinishEvent : 0x1234);
                // The missing record (MissingRecordScope's planted words, data) or a registered event's, whose name is a pointer.
                const bool missing = a.mId == MissingRecordScope::kPlanted;
                drawfx::Note("event attribute", {a.mId, missing ? P(a.mName) : drawfx::Ptr(a.mName), a.mWord08, a.mWord0C,
                                                 a.mWord10, a.mWord14});
                break;
            }
            default: { // listeners on the finish event, then a mesh and Finish
                cIGZUnknown *const listener = reinterpret_cast<cIGZUnknown *>(&w.listener[rng() % 2]);
                if (rng() % 3) b.base->Connect(kFinishEvent, listener, 0x500 + rng() % 2);
                else
                    b.base->Disconnect(kFinishEvent, listener, 0x500 + rng() % 2);
                MakeMesh(b, rng, rng() % 2 == 0);
                sgfx::MarkStep(ctx, "finish");
                if (!gKept && !gLoose) b.own->Finish();
                NoteObject(b, "Fi");
                break;
            }
            }
        }
        b.base->SetListener(nullptr);
        Drop(b);
        side.End();
    });
    w.manager.Drop();
}

} // namespace

DIFF_TEST(sg_indexed_mesh_builder_collect, 0xa7ffa0) { RunCollect(ctx); }

DIFF_TEST(sg_indexed_mesh_builder_build, 0xa80200) { RunBuild(ctx); }

DIFF_TEST(sg_indexed_mesh_builder_persist, 0xa80c90) { RunPersist(ctx); }

DIFF_TEST(sg_indexed_mesh_builder_faces, 0xa80c30) { RunFaces(ctx); }
