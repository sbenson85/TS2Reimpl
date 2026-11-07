// Differential tests of nGZSceneGraph::cGeometryDataContainer as a whole class (lane PA, scenegraph D1b;
// src/sg/cGeometryDataContainerClass.h).
//
// Each side's containers come from its own factory: the game's 0xa70aa0 (new 0x84, the constructor 0xa6ed50) and ours
// Create. The triangle sets they hold are the geometry library's on both sides (the director's factory 0x196560 and the
// library's fill functions, called as originals): the COM director's framework object 0x1ae18d (ours:
// GetDirectorFrameworkObject) is stood in for on both sides, and its GetClassObject (slot 3) logs the ids and answers a
// new triangle set, counted for the caller and once more for the test. At the end of a side the test drops its own
// reference on every container and triangle set, in the order they were made, and notes each answer: anything leaked or
// released once too often shows there, and so does the side's net count of heap blocks (difftest::AllocationCounter).
//
// Persistence runs through our cGZPersistBinaryReader and cGZPersistBinaryWriter (verified, src/gz), with vtables of
// their own members, over a memory stream (MemIStream / MemOStream: the stream slots the reader and writer call, the
// values as little-endian bytes, a text as its length and its bytes). Written bytes are compared between the sides.
//
//   sg_geometry_data_factory   a new container: its words, every getter, the virtual base's slots, released.
//   sg_geometry_data_class_id  the class-id static 0xa70b10 (hooked) against GetClassId.
//   sg_geometry_data_slots     random calls through the face (every setter with indexes in range, removals, clears,
//                              Clone with each answer, CopyLinkageTo / CopyGroupTo between two containers, triangle
//                              sets set and replaced) and the virtual base (flags, segment, names and keys, identity,
//                              type info, interfaces); every 8 calls and at the end both containers are dumped through
//                              every getter, each range walked through its iterators (Get / Next to the end, then a
//                              clone's At, Advance, Previous, QueryInterface and the counts each Release answers).
//   sg_geometry_data_persist   a random container written (through the face or the virtual base), its bytes read into
//                              a fresh container or a populated one, dumped, written again.
//   sg_geometry_data_read      synthetic buffers of versions 0-5 (index lists 32-bit before version 3, triangle corners
//                              32-bit before version 4, no bone counts before version 2) read, dumped and written.
//
// Left out, with the original's reasons: short buffers (the reader's scalar getters 0x9c08a0 etc. read into the stack
// slot of their own `this` and answer its bytes when the stream fails, so a count read past the end is the reader's
// address, and Read 0xa711b0 resizes a vector to that many records: both sides die of the allocation); element data
// whose size is not a multiple of the range's stride (the range slots 0xa70920 / 0xa730c0 / 0xa73240 / 0xa733c0 /
// 0xa70bc0 step from begin by the stride and compare pointers with end, so a walk never meets end and reads past the
// block); indexes past the end for every slot but GetElementIndices 0xa70750 and GetSubsetMesh 0xa70000 (the others
// index their vector unchecked); triangle sets with vertices and no triangles (Read builds each set through 0x196300 ->
// 0x195c80 -> 0x195d90, which sizes its node vector to 2 * triangles - 1 through 0x196650: 0xffffffff nodes, and the
// vector throws its length error 0x1443b0 on both sides; so every set the tests make or generate has 1 to 3); Clone with an interface the copy does not answer is run once in 16 clones only (the
// copy is leaked on both sides: 0xa702e0 never counts it itself).
//
// RVAs these tests reach (status entries cite them): the factory 0xa70aa0, the class id 0xa70b10, the constructor
// 0xa6ed50, the destructor 0xa6f4c0 and the deleting destructor 0xa6f7d0, QueryInterface 0xa71150 (and its thunk
// 0xa71199), Read 0xa711b0 and its reader helpers 0xa71fc0 / 0xa72320 / 0xa720e0 / 0xa72200, GetTypeInfo 0xa72fe0,
// Write 0xa73920, the face's bodies 0xa6f800 0xa72520 0xa72d20 0xa72d40 0xa6f930 0xa6ff70 0xa72fb0 0xa72dd0 0xa72f00
// 0xa700b0 0xa72c30 0xa6fdc0 0xa72e30 0xa72e70 0xa72ea0 0xa6fd80 0xa6fc30 0xa72cc0 0xa724c0 0xa72490 0xa724f0 0xa70e10
// 0xa708d0 0xa70920 0xa730c0 0xa73240 0xa733c0 0xa70bc0 0xa70750 0xa70e30 0xa70d50 0xa73820 0xa73540 0xa70b30 0xa70e90
// 0xa71000 0xa71050 0xa70f10 0xa70e70 0xa70eb0 0xa70e50 0xa70da0 0xa70d80 0xa70000 0xa702e0 0xa70570 0xa704c0, the
// virtual base's vtordisp thunks 0xa71134 0xa6ff54 0xa72456 0xa711a1 0xa72fd2 0xa73909 0xa72c81 0xa6ffec 0xa72c76
// 0xa72cae 0xa70bac 0xa72e17 0xa72db9 0xa72e0c 0xa72eee 0xa72d0f 0xa70db8 0xa70dd6 0xa70dc3 0xa70b16 0xa72c98 0xa70d3b
// 0xa7244b, the iterators' bodies (cVector<4>'s 0xa70270 0xa70650 0xa70710 0xa6f770; the null iterator's 0xa702b0
// 0xa70660 0xa70730 0xa6f7a0; cUnitIterator's 0xa72470 0xa70480 0xa70900 0xa70e00 0xa70f00 0xa6ffe0 0xa70740).
#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"

#include "gz/cGZPersistBinaryReader.h"
#include "gz/cGZPersistBinaryWriter.h"
#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/cGeometryDataContainerClass.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::P;
using nGZCompGeom::cTriangleSetData;

constexpr uint32_t kFactory = 0xa70aa0;
constexpr uint32_t kMeshFactory = 0x196560;
using FactoryFn = cISGResource *(__cdecl *)();
using MeshFactoryFn = cIGZUnknown *(__cdecl *)();
using ReserveFn = void(__thiscall *)(cTriangleSetData *, uint32_t, uint32_t);
using SetVertexFn = void(__thiscall *)(cTriangleSetData *, uint32_t, const float *);
using SetTriangleFn = void(__thiscall *)(cTriangleSetData *, int32_t, uint16_t, uint16_t, uint16_t);

constexpr uint32_t kContainerTag = 0x6c000000; // | index << 12 | offset
constexpr uint32_t kMeshTag = 0x6d000000;
constexpr uint32_t kFace = 0x2c;               // the face's offset in the object
constexpr uint32_t kPattern = 0xa5a5a5a5u;     // what the outs hold before a getter fills them

difftest::Context *gCtx;
bool gOriginal;

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
uint32_t N(const void *p) { return drawfx::Norm(P(p)); }
std::string TextOf(const void *str)
{
    return str ? reinterpret_cast<const cRZString *>(str)->ToChar() : std::string("<null>");
}

// ---- the containers and triangle sets a side made ----
std::vector<cIGeometryDataContainer *> gBoxes;  // one test reference each
std::vector<cITriangleSetShape *> gMeshes;      // one test reference each

uint32_t BoxIndex(const void *face)
{
    for (size_t i = 0; i < gBoxes.size(); ++i)
        if (gBoxes[i] == face) return uint32_t(i);
    return 0xffffu;
}
uint32_t MeshIndex(const void *mesh)
{
    if (!mesh) return 0xfffffffeu;
    for (size_t i = 0; i < gMeshes.size(); ++i)
        if (gMeshes[i] == mesh) return uint32_t(i);
    return 0xffffu;
}

void Register(cIGeometryDataContainer *face)
{
    difftest::NotCounted quiet;
    drawfx::AddRange(reinterpret_cast<uint8_t *>(face) - kFace, cGeometryDataContainer::kSize,
                     kContainerTag | uint32_t(gBoxes.size()) << 12);
    gBoxes.push_back(face);
}

void Track(cITriangleSetShape *mesh)
{
    difftest::NotCounted quiet;
    drawfx::AddRange(mesh, 0x30, kMeshTag | uint32_t(gMeshes.size()) << 12);
    gMeshes.push_back(mesh);
}

// A new container from the side's factory, as its face (counted once: the test's reference).
cIGeometryDataContainer *Make()
{
    cISGResource *r = gOriginal ? gCtx->Original<FactoryFn>(kFactory)() : cGeometryDataContainer::Create();
    void *out = nullptr;
    const bool ok = r->QueryInterface(cIGeometryDataContainer::kIid, &out);
    auto *face = static_cast<cIGeometryDataContainer *>(out);
    Register(face);
    drawfx::Note("made", {uint32_t(ok), N(r), N(face)});
    return face;
}

// A new triangle set from the library's factory, counted once (the test's reference).
cITriangleSetShape *NewMesh()
{
    cIGZUnknown *u = gCtx->Original<MeshFactoryFn>(kMeshFactory)();
    void *out = nullptr;
    u->QueryInterface(cITriangleSetShape::kClassId, &out);
    auto *mesh = static_cast<cITriangleSetShape *>(out);
    Track(mesh);
    return mesh;
}

float SmallFloat(std::mt19937 &rng)
{
    const int32_t i = int32_t(rng() % 41) - 20;
    return rng() % 2 ? float(i) : float(i) * 0.25f + 0.125f;
}

// Vertices and triangles through the library's functions (the same calls Read makes).
void FillMesh(cITriangleSetShape *mesh, std::mt19937 &rng)
{
    const uint32_t vertices = 1 + rng() % 6, triangles = 1 + rng() % 3;
    cTriangleSetData *data = mesh->GetTriangleSet();
    gCtx->Original<ReserveFn>(0x195af0)(data, vertices, triangles);
    for (uint32_t i = 0; i < vertices; ++i) {
        const float v[3] = {SmallFloat(rng), SmallFloat(rng), SmallFloat(rng)};
        gCtx->Original<SetVertexFn>(0x196d00)(data, i, v);
    }
    for (uint32_t i = 0; i < triangles; ++i)
        gCtx->Original<SetTriangleFn>(0x196cc0)(data, int32_t(i), uint16_t(rng() % vertices), uint16_t(rng() % vertices),
                                                uint16_t(rng() % vertices));
}

// A triangle set by identity (the test's list), count, vertices and corners.
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

// ---- the allocator service the triangle sets' vectors use ----
// The library's vector code (0x195af0 -> 0x1953a0, the set's release -> 0x195c80) frees through
// FreeThroughAllocatorService 0x197100, whose function-local static (cache 0x10c95d4, guard 0x10c95d8) would on first use
// fetch the service and register an atexit destructor in the game's uninitialised CRT (heap corruption in the harness).
// While alive the cache holds this fake, the guard as after its first use: slot 3 allocates, slot 4 frees (host heap).
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

// ---- the memory stream ----
std::vector<uint8_t> gBytes; // what the writer wrote, what the reader reads
size_t gAt;                  // the reader's position
std::string gKinds;          // a letter per stream call

void Kind(char k)
{
    difftest::NotCounted quiet;
    gKinds += k;
}
void PutBytes(const void *p, size_t n)
{
    difftest::NotCounted quiet;
    const auto *b = static_cast<const uint8_t *>(p);
    gBytes.insert(gBytes.end(), b, b + n);
}
void Put32(uint32_t v) { PutBytes(&v, 4); }
void Put16(uint16_t v) { PutBytes(&v, 2); }
void PutF(float f) { PutBytes(&f, 4); }
void PutText(const std::string &s)
{
    Put32(uint32_t(s.size()));
    PutBytes(s.data(), s.size());
}
bool TakeBytes(void *p, size_t n)
{
    if (gBytes.size() - gAt < n) {
        drawfx::Note("short read", uint32_t(gAt));
        return false;
    }
    std::memcpy(p, gBytes.data() + gAt, n);
    gAt += n;
    return true;
}

#define PA_BAD(n) \
    virtual void Bad##n() { drawfx::Note("bad slot " #n, n); }

// A cIGZOStream: the slots the writer forwards to (cGZPersistBinaryWriter.h).
struct MemOStream {
    PA_BAD(0) PA_BAD(1) PA_BAD(2) PA_BAD(3) PA_BAD(4)
    virtual uint32_t SetUint8(uint32_t v) // 5
    {
        Kind('b');
        const uint8_t b = uint8_t(v);
        PutBytes(&b, 1);
        return 1;
    }
    PA_BAD(6)
    virtual uint32_t SetUint16(uint32_t v) { Kind('h'); Put16(uint16_t(v)); return 1; } // 7
    virtual uint32_t SetSint32(uint32_t v) { Kind('i'); Put32(v); return 1; }          // 8
    virtual uint32_t SetUint32(uint32_t v) { Kind('u'); Put32(v); return 1; }          // 9
    PA_BAD(10) PA_BAD(11)
    virtual uint32_t SetFloat32(float f) { Kind('f'); PutF(f); return 1; } // 12
    PA_BAD(13)
    virtual uint32_t SetRZCharStr(const char *s) // 14
    {
        Kind('s');
        difftest::NotCounted quiet;
        PutText(s ? s : "<null>");
        return 1;
    }
    virtual uint32_t SetGZStr(const void *str) // 15
    {
        Kind('g');
        difftest::NotCounted quiet;
        PutText(TextOf(str));
        return 1;
    }
    PA_BAD(16)
    virtual uint32_t SetVoid(const void *p, uint32_t n) { Kind('v'); PutBytes(p, n); return 1; } // 17
};
MemOStream gOStream;

// A cIGZIStream: the slots the reader calls (cGZPersistBinaryReader.h). Texts go into the string through its FromChar
// (slot 3: text, length).
struct MemIStream {
    PA_BAD(0) PA_BAD(1) PA_BAD(2) PA_BAD(3) PA_BAD(4)
    virtual bool GetUint8(uint8_t *d) { Kind('b'); return TakeBytes(d, 1); } // 5
    PA_BAD(6)
    virtual bool GetUint16(uint16_t *d) { Kind('h'); return TakeBytes(d, 2); } // 7
    virtual bool GetSint32(int32_t *d) { Kind('i'); return TakeBytes(d, 4); }  // 8
    virtual bool GetUint32(uint32_t *d) { Kind('u'); return TakeBytes(d, 4); } // 9
    PA_BAD(10) PA_BAD(11)
    virtual bool GetFloat32(float *d) { Kind('f'); return TakeBytes(d, 4); } // 12
    PA_BAD(13) PA_BAD(14)
    virtual bool GetGZStr(void *str) // 15
    {
        Kind('g');
        uint32_t n = 0;
        if (!TakeBytes(&n, 4)) return false;
        if (gBytes.size() - gAt < n) {
            drawfx::Note("short text", uint32_t(gAt));
            return false;
        }
        const char *text = reinterpret_cast<const char *>(gBytes.data() + gAt);
        gAt += n;
        using Fn = void(__thiscall *)(void *, const char *, uint32_t);
        reinterpret_cast<Fn>((*reinterpret_cast<void ***>(str))[3])(str, text, n);
        return true;
    }
    PA_BAD(16)
    virtual bool GetVoid(void *d, uint32_t n) { Kind('v'); return TakeBytes(d, n); } // 17
};
MemIStream gIStream;

// The writer's deque element: its slot 29 gives the stream.
struct WriteContext {
    virtual uint32_t QueryInterface(uint32_t, void **) { return 0; } // 0
    virtual uint32_t AddRef() { return 1; }                          // 1
    virtual uint32_t Release() { return 1; }                         // 2
    PA_BAD(3) PA_BAD(4) PA_BAD(5) PA_BAD(6) PA_BAD(7) PA_BAD(8) PA_BAD(9) PA_BAD(10) PA_BAD(11) PA_BAD(12) PA_BAD(13)
    PA_BAD(14) PA_BAD(15) PA_BAD(16) PA_BAD(17) PA_BAD(18) PA_BAD(19) PA_BAD(20) PA_BAD(21) PA_BAD(22) PA_BAD(23)
    PA_BAD(24) PA_BAD(25) PA_BAD(26) PA_BAD(27) PA_BAD(28)
    virtual void *GetStream() { return &gOStream; } // 29
};
WriteContext gContext;

#undef PA_BAD

// ---- our reader and writer, with vtables of their own members (a slot nobody calls is null: a call faults) ----
struct NoCount {
    uint32_t AddRef() { return 1; }
    uint32_t Release() { return 1; }
};
void *gReaderSlots[30];
void *gWriterSlots[32];

void MakeTables()
{
    using R = cGZPersistBinaryReader;
    void **r = gReaderSlots;
    r[0] = MemberAddress(&R::QueryInterface);
    r[1] = MemberAddress(&NoCount::AddRef);
    r[2] = MemberAddress(&NoCount::Release);
    r[3] = MemberAddress(&R::ReadRoot);
    r[4] = MemberAddress(&R::GetBool);
    r[5] = MemberAddress(&R::GetSint8);
    r[6] = MemberAddress(&R::GetUint8);
    r[7] = MemberAddress(&R::GetSint16);
    r[8] = MemberAddress(&R::GetUint16);
    r[9] = MemberAddress(&R::GetSint32);
    r[10] = MemberAddress(&R::GetUint32);
    r[11] = MemberAddress(&R::GetFloat32);
    r[12] = MemberAddress(&R::GetFloat64);
    r[13] = MemberAddress(&R::GetBoolArray);
    r[14] = MemberAddress(&R::GetVoid);
    r[15] = MemberAddress(&R::GetVoid);
    r[16] = MemberAddress(&R::GetSint16Array);
    r[17] = MemberAddress(&R::GetUint16Array);
    r[18] = MemberAddress(&R::GetSint32Array);
    r[19] = MemberAddress(&R::GetUint32Array);
    r[20] = MemberAddress(&R::GetFloat32Array);
    r[21] = MemberAddress(&R::GetFloat64Array);
    r[22] = MemberAddress(&R::GetGZStr);
    r[23] = MemberAddress(&R::Skip);
    r[24] = MemberAddress(&R::ReadObject);
    r[25] = MemberAddress(&R::GetObjectRef);
    r[26] = MemberAddress(&R::ReadStringRecord);
    r[29] = MemberAddress(&R::DeletingDestructor);

    using W = cGZPersistBinaryWriter;
    void **w = gWriterSlots;
    w[0] = MemberAddress(&W::QueryInterface);
    w[1] = MemberAddress(&NoCount::AddRef);
    w[2] = MemberAddress(&NoCount::Release);
    w[3] = MemberAddress(&W::WriteRoot);
    w[4] = MemberAddress(&W::SetUint8);
    w[5] = MemberAddress(&W::SetSint8);
    w[6] = MemberAddress(&W::SetUint8);
    w[7] = MemberAddress(&W::SetSint16);
    w[8] = MemberAddress(&W::SetUint16);
    w[9] = MemberAddress(&W::SetSint32);
    w[10] = MemberAddress(&W::SetUint32);
    w[11] = MemberAddress(&W::SetFloat32);
    w[12] = MemberAddress(&W::SetFloat64);
    w[13] = MemberAddress(&W::SetUint8Array);
    w[14] = MemberAddress(&W::SetVoid);
    w[15] = MemberAddress(&W::SetVoid);
    w[16] = MemberAddress(&W::SetSint16Array);
    w[17] = MemberAddress(&W::SetUint16Array);
    w[18] = MemberAddress(&W::SetSint32Array);
    w[19] = MemberAddress(&W::SetUint32Array);
    w[20] = MemberAddress(&W::SetFloat32Array);
    w[21] = MemberAddress(&W::SetFloat64Array);
    w[22] = MemberAddress(&W::SetVoid);
    w[23] = MemberAddress(&W::WriteRZCharStr);
    w[24] = MemberAddress(&W::WriteGZStr);
    w[25] = MemberAddress(&W::WriteSerialized);
    w[27] = MemberAddress(&W::WriteFlaggedObject);
    w[28] = MemberAddress(&W::WriteRecord);
    w[29] = MemberAddress(&W::GetCurrent);
    w[31] = MemberAddress(&W::DeletingDestructor);
}

cGZPersistBinaryReader gReaderObject;

ifaces::cIGZPersistReader *Reader()
{
    gReaderObject = cGZPersistBinaryReader{};
    gReaderObject.mVTable = gReaderSlots;
    gReaderObject.mRefCount = 1;
    gReaderObject.mStream = &gIStream;
    return reinterpret_cast<ifaces::cIGZPersistReader *>(&gReaderObject);
}

// Our writer with one deque element (the write context), for a scope.
struct WriterScope {
    alignas(8) uint8_t storage[sizeof(cGZPersistBinaryWriter)] = {};
    cGZPersistBinaryWriter *w;
    WriterScope()
    {
        difftest::NotCounted quiet;
        w = reinterpret_cast<cGZPersistBinaryWriter *>(storage);
        w->mVTable = gWriterSlots;
        ::new (&w->Ring()) cGZPersistBinaryWriter::ElementRing();
        w->Ring().emplace_back(&gContext);
    }
    ~WriterScope()
    {
        difftest::NotCounted quiet;
        std::destroy_at(&w->Ring());
    }
    ifaces::cIGZPersistWriter *Get() { return reinterpret_cast<ifaces::cIGZPersistWriter *>(w); }
};

void NoteBytes(const char *label)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(uint32_t(gBytes.size()));
    for (size_t i = 0; i < gBytes.size(); i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, gBytes.data() + i, gBytes.size() - i < 4 ? gBytes.size() - i : 4);
        c.words.push_back(w);
    }
    c.text = gKinds;
    drawfx::gLog.push_back(std::move(c));
}

// ---- dumps ----
void NoteValue(drawfx::Call &c, float v) { c.words.push_back(Bits(v)); }
void NoteValue(drawfx::Call &c, uint32_t v) { c.words.push_back(v); }
void NoteValue(drawfx::Call &c, uint16_t v) { c.words.push_back(v); }
template <int K> void NoteValue(drawfx::Call &c, const cGeometryVector<K> &v)
{
    for (int i = 0; i < K; ++i) c.words.push_back(Bits(v.v[i]));
}

// The first empty element range of each value type in a dump (counted; released by ReleaseEmptyBegins): every later
// empty range's begin is compared with it, so the null pointers the original gives an empty range (MemRange) show.
// Only the MemPtr-only types: a uint16 range may hold a cUnitIterator, whose +8 a MemPtr's IsEqual would read.
template <typename T> cIGeometryIterator<T> *gEmptyBegin = nullptr;

template <typename T> void ReleaseEmptyBegin()
{
    if (gEmptyBegin<T>) gEmptyBegin<T>->Release();
    gEmptyBegin<T> = nullptr;
}

void ReleaseEmptyBegins()
{
    ReleaseEmptyBegin<float>();
    ReleaseEmptyBegin<cGeometryVector<2>>();
    ReleaseEmptyBegin<cGeometryVector<3>>();
    ReleaseEmptyBegin<cGeometryVector<4>>();
    ReleaseEmptyBegin<uint32_t>();
}

// A range: the values from the cursor to the end, end against begin, then a clone of begin through At, Advance,
// Previous and QueryInterface, and what each Release answers. An empty range: a clone of begin advanced by one against
// end (a MemPtr moves, a null iterator does not), and for `probeNull` (only the null iterators GetElementIndices gives
// past the end) begin's Get and At.
template <typename T>
void NoteRange(const char *label, cGeometryRange<T> *out, cGeometryRange<T> &r, std::mt19937 &rng, bool probeNull = false)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(out == &r);
    cIGeometryIterator<T> *it = r.mCurrent;
    uint32_t n = 0;
    while (!it->IsEqual(r.mEnd) && n < 512) {
        NoteValue(c, it->Get());
        it->Next();
        ++n;
    }
    c.words.push_back(n);
    c.words.push_back(r.mEnd->IsEqual(r.mBegin));
    if (n == 0) {
        cIGeometryIterator<T> *k = r.mBegin->Clone();
        k->AddRef();
        k->Advance(1);
        c.words.push_back(k->IsEqual(r.mEnd));
        c.words.push_back(k->Release());
        if (probeNull) {
            NoteValue(c, r.mBegin->Get());
            NoteValue(c, r.mBegin->At(5));
        }
        if constexpr (!std::is_same_v<T, uint16_t>) {
            if (gEmptyBegin<T>) {
                c.words.push_back(gEmptyBegin<T>->IsEqual(r.mBegin));
            } else {
                gEmptyBegin<T> = r.mBegin;
                r.mBegin->AddRef();
            }
        }
    }
    if (n) {
        cIGeometryIterator<T> *k = r.mBegin->Clone();
        k->AddRef();
        NoteValue(c, k->At(int32_t(rng() % n)));
        k->Advance(int32_t(n));
        c.words.push_back(k->IsEqual(r.mEnd));
        k->Previous();
        NoteValue(c, k->Get());
        k->Advance(-int32_t(n - 1));
        c.words.push_back(k->IsEqual(r.mBegin));
        void *q = nullptr;
        c.words.push_back(k->QueryInterface(1, &q));
        c.words.push_back(q == k);
        c.words.push_back(k->QueryInterface(0x5bafb147, &q));
        c.words.push_back(k->Release());
        c.words.push_back(k->Release());
    }
    c.words.push_back(r.mCurrent->Release());
    c.words.push_back(r.mBegin->Release());
    c.words.push_back(r.mEnd->Release());
    drawfx::gLog.push_back(std::move(c));
}

void NoteOut(const char *label, const uint32_t *words, unsigned count, const void *ret, const void *out)
{
    drawfx::Call c;
    c.note = label;
    c.words.assign(words, words + count);
    c.words.push_back(ret == out);
    drawfx::gLog.push_back(std::move(c));
}

// The object's words that are not pointers into the heap or vtables: the count, the cSGResource part's key, name,
// segment and flags (the low half of +0x28: +0x2a / +0x2b are padding no constructor writes), the vbtable's entries, the
// vtordisp, each vector's size and capacity in bytes, the bounding set.
void NoteWords(cIGeometryDataContainer *f)
{
    const auto *b = reinterpret_cast<const uint8_t *>(f) - kFace;
    auto word = [&](uint32_t off) { return *reinterpret_cast<const uint32_t *>(b + off); };
    drawfx::Call c;
    c.note = "words";
    c.words = {word(4), word(8), word(0xc), word(0x10), word(0x14), word(0x18), word(0x20), drawfx::Norm(word(0x24)),
               word(0x28) & 0xffff, word(0x7c), MeshIndex(reinterpret_cast<void *>(word(0x78)))};
    const auto *vbtable = reinterpret_cast<const int32_t *>(word(0x1c));
    c.words.push_back(uint32_t(vbtable[0]));
    c.words.push_back(uint32_t(vbtable[1]));
    for (uint32_t v = 0x30; v <= 0x6c; v += 0xc) {
        c.words.push_back(word(v + 4) - word(v));
        c.words.push_back(word(v + 8) - word(v));
    }
    drawfx::gLog.push_back(std::move(c));
}

void Dump(const char *label, cIGeometryDataContainer *f, std::mt19937 &rng)
{
    drawfx::Note(label, BoxIndex(f));
    NoteWords(f);
    const uint32_t ne = f->CountElements(), nl = f->CountLinkages(), ng = f->CountGroups(), nt = f->CountTransforms(),
                   nn = f->CountNamePairs();
    drawfx::Note("counts", {ne, nl, ng, nt, nn});
    for (uint32_t i = 0; i < ne; ++i) {
        uint32_t h[7];
        std::fill(h, h + 7, kPattern);
        const void *ret = f->GetElementHeader(reinterpret_cast<cGeometryElementHeader *>(h), i);
        NoteOut("element header", h, 7, ret, h);
        cGeometryRange<uint16_t> ri;
        NoteRange("element indices", f->GetElementIndices(&ri, i), ri, rng);
        cGeometryRange<float> rf;
        NoteRange("element floats", f->GetElementFloats(&rf, i), rf, rng);
        cGeometryRange<cGeometryVector<2>> r2;
        NoteRange("element vectors 2", f->GetElementVectors2(&r2, i), r2, rng);
        cGeometryRange<cGeometryVector<3>> r3;
        NoteRange("element vectors 3", f->GetElementVectors3(&r3, i), r3, rng);
        cGeometryRange<cGeometryVector<4>> r4;
        NoteRange("element vectors 4", f->GetElementVectors4(&r4, i), r4, rng);
        cGeometryRange<uint32_t> rw;
        NoteRange("element words", f->GetElementWords(&rw, i), rw, rng);
    }
    {
        cGeometryRange<uint16_t> ri; // past the end: the null iterators
        NoteRange("element indices past the end", f->GetElementIndices(&ri, ne), ri, rng, true);
    }
    for (uint32_t i = 0; i < nl; ++i) {
        uint32_t h[4];
        std::fill(h, h + 4, kPattern);
        const void *ret = f->GetLinkageHeader(reinterpret_cast<cGeometryLinkageHeader *>(h), i);
        NoteOut("linkage header", h, 4, ret, h);
        cGeometryRange<uint16_t> re;
        NoteRange("linkage elements", f->GetLinkageElements(&re, i), re, rng);
        for (uint32_t which = 0; which < 4; ++which) {
            cGeometryRange<uint16_t> ra;
            NoteRange("linkage aliases", f->GetLinkageAliases(&ra, i, which), ra, rng);
        }
        for (uint32_t which = 0; which < 5; ++which) drawfx::Note("linkage has", {which, f->LinkageHas(i, which)});
    }
    for (uint32_t i = 0; i < ng; ++i) {
        uint32_t h[8];
        std::fill(h, h + 8, kPattern);
        const void *ret = f->GetGroupInfo(reinterpret_cast<cGeometryGroupInfo *>(h), i);
        sgfx::NoteText("group name", reinterpret_cast<const char *>(uintptr_t(h[5])));
        h[5] = 0;
        NoteOut("group info", h, 8, ret, h);
        cGeometryRange<uint16_t> rv;
        NoteRange("group vertex indices", f->GetGroupVertexIndices(&rv, i), rv, rng);
        cGeometryRange<uint16_t> rb;
        NoteRange("group bones", f->GetGroupBones(&rb, i), rb, rng);
    }
    for (uint32_t i = 0; i < nt; ++i) {
        uint32_t h[8];
        std::fill(h, h + 8, kPattern);
        const void *ret = f->GetTransform(reinterpret_cast<cGeometryTransform *>(h), i);
        NoteOut("transform", h, 8, ret, h);
    }
    for (uint32_t i = 0; i < nn; ++i) {
        sgfx::NoteText("name first", f->GetNameFirst(i));
        sgfx::NoteText("name second", f->GetNameSecond(i));
    }
    NoteMesh("bounding", f->GetBoundingMesh());
    for (uint32_t i = 0; i < 6; ++i) NoteMesh("subset", f->GetSubsetMesh(i));
    ReleaseEmptyBegins();
}

// ---- random calls ----
const char *const kNames[] = {"", "body", "hair_lod0", "a_group_name_longer_than_sixteen", "tops", "x"};
const char *PickName(std::mt19937 &rng) { return kNames[rng() % (sizeof(kNames) / sizeof(kNames[0]))]; }

uint32_t SmallWord(std::mt19937 &rng) { return rng() % 3 ? rng() % 8 : rng(); }

void Fill16(uint16_t *p, uint32_t n, std::mt19937 &rng)
{
    for (uint32_t i = 0; i < n; ++i) p[i] = uint16_t(rng() % 3 ? rng() % 8 : rng());
}

cITriangleSetShape *PickMesh(std::mt19937 &rng)
{
    if (rng() % 4 == 0) return nullptr;
    if (gMeshes.empty() || rng() % 3 == 0) {
        cITriangleSetShape *mesh = NewMesh();
        FillMesh(mesh, rng);
        return mesh;
    }
    return gMeshes[rng() % gMeshes.size()];
}

void TypeInfo(const char *label, cIGZPersistSerializableReferent *referent)
{
    const cPersistTypeInfo info = referent->GetTypeInfo();
    sgfx::NoteText(label, TextOf(info.mName).c_str());
    drawfx::Note("type info words", {info.mWord4, info.mVersion});
    if (info.mName) info.mName->Release();
}

const uint32_t kIids[] = {1, cIGeometryDataContainer::kIid, cISGResource::kIid, 0x5b0aaa74, 0xbb11a53e, 0x9b11bf70,
                          0x12345678};

void Query(const char *label, cIGZUnknown *u, std::mt19937 &rng)
{
    const uint32_t iid = kIids[rng() % (sizeof(kIids) / sizeof(kIids[0]))];
    void *out = reinterpret_cast<void *>(uintptr_t(kPattern));
    const bool ok = rng() % 8 ? u->QueryInterface(iid, &out) : u->QueryInterface(iid, nullptr);
    drawfx::Note(label, {iid, uint32_t(ok), N(out)});
    if (ok && out != reinterpret_cast<void *>(uintptr_t(kPattern)))
        drawfx::Note("released", static_cast<cIGZUnknown *>(out)->Release());
}

// The virtual base's slots (the vtordisp thunks of 0xe73314).
void OperateResource(cIGeometryDataContainer *f, cIGeometryDataContainer *other, std::mt19937 &rng)
{
    cISGResource *r = f->GetResource();
    drawfx::Note("resource", {N(r), N(f->GetResourceConst())});
    switch (rng() % 14) {
    case 0: Query("resource QI", r, rng); break;
    case 1: {
        const uint32_t a = r->AddRef();
        drawfx::Note("resource AddRef / Release", {a, r->Release()});
        break;
    }
    case 2: TypeInfo("resource type info", r); break;
    case 3: drawfx::Note("persist resource", {N(r->GetPersistResourceConst()), N(r->GetPersistResource())}); break;
    case 4: drawfx::Note("sub resource", N(r->GetSubResource(rng() % 3))); break;
    case 5: drawfx::Note("submit key", r->SubmitKeyToService()); break;
    case 6: {
        cISGResource *o = rng() % 2 ? r : other->GetResource();
        drawfx::Note("compare identity", uint32_t(r->CompareIdentity(o)));
        break;
    }
    case 7: {
        cIGZUnknown *segment = rng() % 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[rng() % 3]) : nullptr;
        const uint32_t which = rng() % 3;
        const char *name = PickName(rng);
        uint32_t ret = 0;
        if (which == 0) ret = r->AssignAndSubmit(name, segment);
        else if (which == 1) ret = r->ResetAndSubmit(rng() % 4, rng() % 4);
        else ret = r->OpenAssignAndSubmit(name, rng() % 4, segment);
        drawfx::Note("assign", {which, ret});
        sgfx::NoteText("name", r->GetName());
        break;
    }
    case 8: {
        cIGZUnknown *segment = rng() % 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[rng() % 3]) : nullptr;
        drawfx::Note("set segment", uint32_t(r->SetSegment(segment)));
        drawfx::Note("segment", N(r->GetSegment()));
        break;
    }
    case 9: {
        r->SetFlag(uint8_t(rng() % 3));
        drawfx::Note("flag", r->GetFlag());
        break;
    }
    case 10: drawfx::Note("key", {r->HasKey(), r->ForwardKey()}); break;
    case 11: {
        uint32_t key[5];
        std::fill(key, key + 5, kPattern);
        const void *ret = r->CopyKeyTo(reinterpret_cast<cGZPersistResourceKey *>(key));
        NoteOut("copy key", key, 5, ret, key);
        break;
    }
    case 12: {
        sgfx::NoteText("record 18", static_cast<const char *>(r->GetRecord18()));
        sgfx::NoteText("record 19", static_cast<const char *>(r->GetRecord19()));
        break;
    }
    default: drawfx::Note("owner info", {drawfx::Norm(r->GetOwnerInfo()), drawfx::Norm(f->GetOwnerInfo())}); break;
    }
}

cIGeometryDataContainer *CloneOf(cIGeometryDataContainer *f, std::mt19937 &rng)
{
    const uint32_t pick = rng() % 16;
    const uint32_t iid = pick == 0 ? 0x12345678u : pick % 3 == 0 ? cISGResource::kIid : pick % 3 == 1 ? 0x5b0aaa74u
                                                                                                     : cIGeometryDataContainer::kIid;
    void *out = reinterpret_cast<void *>(uintptr_t(kPattern));
    const bool ok = f->Clone(iid, &out);
    drawfx::Note("clone", {iid, uint32_t(ok)});
    if (!ok) return nullptr;
    auto *u = static_cast<cIGZUnknown *>(out);
    void *face = nullptr;
    u->QueryInterface(cIGeometryDataContainer::kIid, &face);
    drawfx::Note("clone released", u->Release());
    Register(static_cast<cIGeometryDataContainer *>(face));
    drawfx::Note("clone out", N(out));
    return static_cast<cIGeometryDataContainer *>(face);
}

void Operate(cIGeometryDataContainer *f, cIGeometryDataContainer *other, std::mt19937 &rng)
{
    const uint32_t ne = f->CountElements(), nl = f->CountLinkages(), ng = f->CountGroups();
    uint16_t list[16];
    uint16_t bones[16];
    const uint32_t op = rng() % 30;
    drawfx::Note("op", op);
    {
        char step[32];
        std::snprintf(step, sizeof(step), "op kind %u", op);
        sgfx::MarkStep(*gCtx, step);
    }
    switch (op) {
    case 0:
    case 1:
        drawfx::Note("add element", f->AddElement(rng() % 4, rng() % 3, rng() % 3, rng() % 3));
        break;
    case 2:
    case 3:
        if (ne) {
            uint8_t data[96];
            for (uint8_t &b : data) b = uint8_t(rng());
            const uint32_t size = 48 * (rng() % 3);
            const uint32_t count = rng() % 8;
            Fill16(list, count, rng);
            f->SetElementData(rng() % ne, rng(), size ? data : nullptr, size, rng() % 4 ? list : nullptr, count);
        }
        break;
    case 4:
        if (ne && rng() % 2) {
            // An existing element's words, so something is removed (and the linkages renumbered) more often.
            uint32_t h[6];
            f->GetElementHeader(reinterpret_cast<cGeometryElementHeader *>(h), rng() % ne);
            f->RemoveElements(h[2], h[4], h[5]);
        } else {
            f->RemoveElements(rng() % 4, rng() % 3, rng() % 3);
        }
        break;
    case 5:
        if (ne) f->SetElementSetFormat(rng() % ne, rng());
        break;
    case 6:
        f->AddLinkage();
        break;
    case 7:
    case 8:
        if (nl) drawfx::Note("add linkage element", f->AddLinkageElement(rng() % nl, uint16_t(rng() % (ne + 2))));
        break;
    case 9:
        if (nl) {
            const uint32_t i = rng() % nl;
            uint32_t h[3];
            f->GetLinkageHeader(reinterpret_cast<cGeometryLinkageHeader *>(h), i);
            cGeometryRange<uint16_t> r;
            f->GetLinkageElements(&r, i);
            uint32_t size = 0;
            for (cIGeometryIterator<uint16_t> *it = r.mCurrent; !it->IsEqual(r.mEnd); it->Next()) ++size;
            r.mCurrent->Release();
            r.mBegin->Release();
            r.mEnd->Release();
            if (size) f->SetLinkageElement(i, rng() % size, uint16_t(rng()));
        }
        break;
    case 10:
        if (nl) f->SetLinkageReferencedSize(rng() % nl, rng() % 12);
        break;
    case 11:
    case 12:
        if (nl) {
            const uint32_t count = rng() % 6;
            Fill16(list, count, rng);
            f->SetLinkageAliases(rng() % nl, rng() % 4, list, count);
        }
        break;
    case 13:
        if (nl) f->ClearLinkageAliases(rng() % nl, rng() % 5);
        break;
    case 14:
        f->ReserveGroups(rng() % 12);
        break;
    case 15:
    case 16:
        f->AddGroup(rng() % 4, PickName(rng), rng() % 4, int32_t(SmallWord(rng)));
        break;
    case 17:
        if (ng) {
            const uint32_t count = rng() % 9;
            Fill16(list, count, rng);
            f->SetGroupVertexIndices(rng() % ng, list, count);
        }
        break;
    case 18:
        if (ng) {
            const uint32_t count = rng() % 9;
            Fill16(list, count, rng);
            f->SetGroupVertexIndicesAndWord(rng() % ng, list, count, int32_t(SmallWord(rng)));
        }
        break;
    case 19:
        if (ng) {
            const uint32_t count = rng() % 9, boneCount = rng() % 5;
            Fill16(list, count, rng);
            Fill16(bones, boneCount, rng);
            f->SetGroupVertexIndicesAndBones(rng() % ng, list, count, bones, boneCount);
        }
        break;
    case 20: {
        cGeometryTransform t;
        for (float &v : t.mTranslation) v = SmallFloat(rng);
        for (float &v : t.mRotation) v = SmallFloat(rng);
        f->AddTransform(&t);
        break;
    }
    case 21:
        f->AddNamePair(PickName(rng), PickName(rng));
        break;
    case 22:
        f->SetBoundingMesh(PickMesh(rng));
        break;
    case 23:
        f->SetSubsetMesh(rng() % 5, PickMesh(rng));
        break;
    case 24: {
        const uint32_t which = rng() % 3;
        if (rng() % 3 == 0) {
            if (which == 0) f->ClearLinkages();
            else if (which == 1) f->ClearElements();
            else f->ClearGroups();
        }
        break;
    }
    case 25:
        if (cIGeometryDataContainer *copy = CloneOf(f, rng)) Dump("clone", copy, rng);
        break;
    case 26: {
        const uint32_t on = other->CountLinkages();
        if (nl && on) {
            const uint32_t source = rng() % nl;
            if (rng() % 2) f->SetLinkageReferencedSize(source, 1 + rng() % 11); // a size the copy must carry
            f->CopyLinkageTo(source, other, rng() % on);
        }
        break;
    }
    case 27:
        if (ng) f->CopyGroupTo(rng() % ng, other, rng() % 4);
        break;
    case 28:
        f->Slot55();
        Query("face QI", f, rng);
        TypeInfo("face type info", f);
        break;
    default:
        OperateResource(f, other, rng);
        break;
    }
}

// ---- a side's begin and end ----
struct SideScope {
    difftest::AllocationCounter counter;
    SideScope(difftest::Context &ctx, bool original)
    {
        gCtx = &ctx;
        gOriginal = original;
        gAt = 0;
        difftest::NotCounted quiet;
        gBoxes.clear();
        gMeshes.clear();
        gBytes.clear();
        gKinds.clear();
    }
    // The test's references dropped in the order they were made: the containers, then the triangle sets.
    void End()
    {
        for (cIGeometryDataContainer *f : gBoxes) drawfx::Note("container released", f->Release());
        for (cITriangleSetShape *m : gMeshes) drawfx::Note("triangle set released", m->Release());
        sgfx::gResources.NoteRefs();
        sgfx::gPool.NoteCounts();
        drawfx::Note("blocks left", uint32_t(counter.Blocks()));
    }
};

void Build(uint32_t seed, drawfx::Cells &cells)
{
    sgfx::gPool.Reset(cells);
    // Pool::Reset clears its texts but keeps their buffers: the side that interns a long text first would allocate a
    // block the other side reuses, and the sides' block counts would differ. Each side starts from empty buffers.
    for (std::string &text : sgfx::gPool.texts) std::string().swap(text);
    sgfx::gResources.Reset(cells);
    sgfx::gResources.manager.Returns(9, seed % 3);
    sgfx::gResources.manager.Returns(14, (seed + 1) % 3);
}

void Write(cIGeometryDataContainer *f, std::mt19937 &rng, const char *label)
{
    {
        difftest::NotCounted quiet;
        gBytes.clear();
        gKinds.clear();
    }
    {
        WriterScope w;
        if (rng() % 2) f->Write(w.Get());
        else f->GetResource()->Write(w.Get());
    }
    NoteBytes(label);
}

void Read(cIGeometryDataContainer *f, std::mt19937 &rng)
{
    {
        difftest::NotCounted quiet;
        gKinds.clear();
    }
    gAt = 0;
    if (rng() % 2) f->Read(Reader());
    else f->GetResource()->Read(Reader());
    drawfx::Note("read to", {uint32_t(gAt), uint32_t(gBytes.size())});
    sgfx::NoteText("read kinds", gKinds.c_str());
}

// ---- synthetic buffers ----
void GenIndexList(std::mt19937 &rng, uint32_t version, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (version < 3) Put32(rng() % 3 ? rng() % 8 : rng());
        else Put16(uint16_t(rng() % 3 ? rng() % 8 : rng()));
    }
}

void GenMesh(std::mt19937 &rng, uint32_t version)
{
    const uint32_t vertices = rng() % 3 == 0 ? 0 : 1 + rng() % 5;
    Put32(vertices);
    if (!vertices) return;
    const uint32_t triangles = 1 + rng() % 3;
    Put32(triangles * 3);
    for (uint32_t i = 0; i < vertices * 3; ++i) PutF(SmallFloat(rng));
    for (uint32_t i = 0; i < triangles * 3; ++i) {
        if (version < 4) Put32(rng() % vertices | (rng() % 4 == 0 ? rng() << 16 : 0));
        else Put16(uint16_t(rng() % vertices));
    }
}

std::string GenText(std::mt19937 &rng) { return PickName(rng); }

void Generate(std::mt19937 &rng, uint32_t version)
{
    difftest::NotCounted quiet;
    gBytes.clear();
    PutText("cGeometryDataContainer");
    Put32(cGeometryDataContainer::kClassId);
    Put32(version);
    PutText("cSGResource");
    Put32(0);
    Put32(rng() % 3);
    PutText(rng() % 2 ? "geom_gmdc" : "");

    const uint32_t elements = rng() % 4;
    Put32(elements);
    for (uint32_t i = 0; i < elements; ++i) {
        for (int k = 0; k < 5; ++k) Put32(SmallWord(rng));
        const uint32_t size = 48 * (rng() % 3);
        Put32(size);
        for (uint32_t b = 0; b < size; ++b) {
            const uint8_t v = uint8_t(rng());
            PutBytes(&v, 1);
        }
        const uint32_t count = rng() % 6;
        Put32(count);
        GenIndexList(rng, version, count);
    }
    const uint32_t linkages = rng() % 4;
    Put32(linkages);
    for (uint32_t i = 0; i < linkages; ++i) {
        const uint32_t count = rng() % 5;
        Put32(count);
        GenIndexList(rng, version, count);
        Put32(rng() % 10);
        Put32(rng() % 6);
        for (int which = 0; which < 3; ++which) {
            const uint32_t n = rng() % 2 ? 0 : rng() % 5;
            Put32(n);
            GenIndexList(rng, version, n);
        }
    }
    const uint32_t groups = rng() % 4;
    Put32(groups);
    for (uint32_t i = 0; i < groups; ++i) {
        Put32(rng() % 4);
        Put32(rng() % 4);
        PutText(GenText(rng));
        const uint32_t count = rng() % 7;
        Put32(count);
        GenIndexList(rng, version, count);
        Put32(SmallWord(rng));
        if (version > 1) {
            const uint32_t boneCount = rng() % 2 ? 0 : rng() % 5;
            Put32(boneCount);
            GenIndexList(rng, version, boneCount);
        }
    }
    const uint32_t transforms = rng() % 4;
    Put32(transforms);
    for (uint32_t i = 0; i < transforms * 7; ++i) PutF(SmallFloat(rng));
    const uint32_t names = rng() % 3;
    Put32(names);
    for (uint32_t i = 0; i < names * 2; ++i) PutText(GenText(rng));
    GenMesh(rng, version);
    const uint32_t subsets = rng() % 4;
    Put32(subsets);
    for (uint32_t i = 0; i < subsets; ++i) GenMesh(rng, version);
}

constexpr unsigned kSeeds = 48;

} // namespace

DIFF_TEST(sg_geometry_data_factory, 0xa70aa0)
{
    MakeTables();
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    ServiceFake service(ctx);
    sgfx::CheckSides(ctx, "factory", 32, Build, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        cIGeometryDataContainer *a = Make();
        cIGeometryDataContainer *b = Make();
        Dump("new", a, rng);
        TypeInfo("face type info", a);
        for (int k = 0; k < 14; ++k) OperateResource(a, b, rng);
        side.End();
    });
}

// The class-id static 0xa70b10 (cdecl, no arguments: one register load and a return).
DIFF_TEST(sg_geometry_data_class_id, 0xa70b10)
{
    const uint32_t original = ctx.Original<uint32_t(__cdecl *)()>(0xa70b10)();
    const uint32_t ours = cGeometryDataContainer::GetClassId();
    ctx.cases++;
    if (original != ours) ctx.Fail("class id: original %08x, ours %08x", original, ours);
}

DIFF_TEST(sg_geometry_data_slots, 0xa6f800)
{
    MakeTables();
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    ServiceFake service(ctx);
    sgfx::CheckSides(ctx, "slots", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        cIGeometryDataContainer *box[2] = {Make(), Make()};
        for (int k = 0; k < 60; ++k) {
            char step[32];
            std::snprintf(step, sizeof(step), "op %d", k);
            sgfx::MarkStep(ctx, step);
            const uint32_t which = rng() % 2;
            Operate(box[which], box[rng() % 2], rng);
            if (k % 8 == 7) Dump("dump", box[which], rng);
        }
        Dump("end a", box[0], rng);
        Dump("end b", box[1], rng);
        side.End();
    });
}

DIFF_TEST(sg_geometry_data_persist, 0xa73920)
{
    MakeTables();
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    ServiceFake service(ctx);
    sgfx::CheckSides(ctx, "persist", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        cIGeometryDataContainer *box[2] = {Make(), Make()};
        for (int k = 0; k < 40; ++k) {
            const uint32_t which = rng() % 2;
            Operate(box[which], box[rng() % 2], rng);
        }
        sgfx::MarkStep(ctx, "write");
        Write(box[0], rng, "written");
        cIGeometryDataContainer *target = rng() % 3 ? Make() : box[1];
        sgfx::MarkStep(ctx, "read");
        Read(target, rng);
        Dump("read", target, rng);
        sgfx::MarkStep(ctx, "write again");
        Write(target, rng, "written again");
        side.End();
    });
}

DIFF_TEST(sg_geometry_data_read, 0xa711b0)
{
    MakeTables();
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    ServiceFake service(ctx);
    sgfx::CheckSides(ctx, "read", kSeeds * 2, Build, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        cIGeometryDataContainer *target = Make();
        if (rng() % 4 == 0) {
            cIGeometryDataContainer *other = Make();
            for (int k = 0; k < 30; ++k) Operate(target, other, rng);
        }
        const uint32_t version = rng() % 6;
        drawfx::Note("version", version);
        Generate(rng, version);
        sgfx::MarkStep(ctx, "read");
        Read(target, rng);
        Dump("read", target, rng);
        sgfx::MarkStep(ctx, "write");
        Write(target, rng, "written");
        side.End();
    });
}

