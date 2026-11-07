// cTSNHoodTerrain (0xc0 bytes, four vtables): differential tests of the persistence, lifetime and leaf functions. Every case
// builds the same terrain twice from one seed (our class stores the game's vtables), runs the original on one and ours on the
// other, and compares the return, the whole object (fields, vectors, both arrays, the id set) and the log of every fake call
// in order: the stream, the neighbourhood / service chain, the persist service, the cheat and notification services and the
// message server. The face vtable the objects carry is a copy whose slot 21 (SetTerrainType: static type table) is a
// recording fake. LoadTerrainTypes (file I/O) is never reached: its "loaded" byte is set.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "heap_poison.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/cRZString.h"
#include "shape.h"
#include "std/StdHash.h"
#include "ts/cTSNHoodTerrain.h"
#include "ts_nhood_fakes.h"
#include "ts_services_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <vector>

namespace {
using namespace nhoodfx;
using Blob = std::vector<uint32_t>;
using nTSWorld::c2DArray;

// ---- fakes ----

struct Fake { void **vt; uint32_t id; };
void *gAppVT[16]{}, *gNbhdVT[64]{}, *gServiceVT[8]{}, *gCheatMgrVT[8]{}, *gNotifierVT[8]{}, *gPersistVT[40]{}, *gSegVT[16]{}, *gServerVT[8]{};
Fake gApp{gAppVT, 10}, gNbhd{gNbhdVT, 11}, gService{gServiceVT, 12}, gCheatMgr{gCheatMgrVT, 13}, gNotifier{gNotifierVT, 14},
    gPersist{gPersistVT, 15}, gSeg{gSegVT, 16}, gServer{gServerVT, 17};
cTSNHoodTerrain *gBase;           // the object under test: pointers into it are logged as offsets
float gCellSize = 10.0f;
uint32_t gGridW = 128, gGridH = 128;
bool gHaveService, gHaveCheatMgr;
uint8_t gSaveRet, gFindRet, gFlushRet, gSetTypeRet;
bool gOutErrorWanted;
size_t gLastStateStart;
cRZString gTypes[3];
const void *gTypeEntries[4];
uint8_t *gEntries; // the fake type-entry vector's block (SetTerrainType)

uint32_t Norm(const void *p)
{
    const auto u = reinterpret_cast<uintptr_t>(p);
    if (!p) return 0;
    for (Fake *f : {&gApp, &gNbhd, &gService, &gCheatMgr, &gNotifier, &gPersist, &gSeg, &gServer}) if (p == f) return 0x100 + f->id;
    if (gBase && u >= reinterpret_cast<uintptr_t>(gBase) && u < reinterpret_cast<uintptr_t>(gBase) + 0xc0) return 0x1000 + uint32_t(u - reinterpret_cast<uintptr_t>(gBase));
    return 0xbad00000;
}
void Ev(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) { Note(evMisc, a, b); gLog.push_back(c); gLog.push_back(d); }

void *__fastcall AppSlot6(Fake *, void *) { Ev(6); return &gNbhd; }
float __fastcall NbhdCell(Fake *, void *) { Ev(50); return gCellSize; }
uint32_t __fastcall NbhdW(Fake *, void *) { Ev(52); return gGridW; }
uint32_t __fastcall NbhdH(Fake *, void *) { Ev(53); return gGridH; }
uint32_t __fastcall ServiceQuery(Fake *, void *, uint32_t a, uint32_t b, void **out)
{
    Ev(5, a, b);
    *out = nullptr;
    if (a == 0x2a4915ea) { if (gHaveCheatMgr) *out = &gCheatMgr; }
    else *out = &gNotifier;
    return 1;
}
uint32_t __fastcall CheatRegister(Fake *, void *, uint32_t *cheat)
{
    Ev(4, cheat ? cheat[0] == uint32_t(reinterpret_cast<uintptr_t>(GameAddress<void>(0xdcc280))) : 9, cheat ? cheat[1] : 0, cheat ? cheat[2] : 0);
    return 1;
}
uint32_t __fastcall FakeRelease(Fake *self, void *) { Ev(2, self->id); return 1; }
uint32_t __fastcall NotifierAdd(Fake *, void *, const void *arg, uint32_t key) { Ev(5, Norm(arg), key); return 1; }
uint32_t __fastcall NotifierRemove(Fake *, void *, const void *arg, uint32_t key) { Ev(6, Norm(arg), key); return 1; }
uint8_t __fastcall PersistSave(Fake *, void *, const void *resource, const void *segment) { Ev(16, Norm(resource), Norm(segment)); return gSaveRet; }
uint8_t __fastcall PersistFind(Fake *, void *, const uint32_t *key, uint32_t iid, void **out)
{
    Ev(34, key[0], key[1], key[2]);
    Ev(35, key[3], iid);
    if (gFindRet) *out = &gSeg;
    return gFindRet;
}
uint8_t __fastcall SegFlush(Fake *, void *) { Ev(10); return gFlushRet; }
void Message(const uint8_t *m, uint32_t how, uint32_t extra)
{
    Ev(how, extra, m[8], *reinterpret_cast<const uint32_t *>(m + 0xc));
    Ev(*reinterpret_cast<const uint32_t *>(m + 0x10), *reinterpret_cast<const uint32_t *>(m + 0x14), *reinterpret_cast<const uint32_t *>(m + 0x18), *reinterpret_cast<const uint32_t *>(m + 4));
}
uint8_t __fastcall ServerSend(Fake *, void *, const uint8_t *m) { Message(m, 3, 0); return 1; }
uint8_t __fastcall ServerPost(Fake *, void *, const uint8_t *m, uint32_t flag) { Message(m, 4, flag); return 1; }
uint8_t __fastcall FaceSetTerrainType(void *face, void *, cIGZString *name)
{
    Ev(21, Norm(face), Hash(static_cast<cRZString *>(static_cast<void *>(name))->mString));
    return gSetTypeRet;
}
// Slot 10 (GetHeightAt) logged, then the game's 0x4afc10: the edit slots' reads through the vtable are compared by count and
// arguments (ResampleGrid's column loop reading one row group too many changed nothing else).
float __fastcall FaceGetHeightAt(void *face, void *, int32_t x, int32_t y)
{
    Ev(10, uint32_t(x), uint32_t(y));
    return reinterpret_cast<float(__fastcall *)(void *, void *, int32_t, int32_t)>(GameAddress<void>(0x4afc10))(face, nullptr, x, y);
}

void SetupFakes()
{
    gAppVT[6] = reinterpret_cast<void *>(&AppSlot6);
    gNbhdVT[50] = reinterpret_cast<void *>(&NbhdCell);
    gNbhdVT[52] = reinterpret_cast<void *>(&NbhdW);
    gNbhdVT[53] = reinterpret_cast<void *>(&NbhdH);
    gServiceVT[5] = reinterpret_cast<void *>(&ServiceQuery);
    gCheatMgrVT[4] = reinterpret_cast<void *>(&CheatRegister);
    gCheatMgrVT[2] = gNotifierVT[2] = gSegVT[2] = reinterpret_cast<void *>(&FakeRelease);
    gNotifierVT[5] = reinterpret_cast<void *>(&NotifierAdd);
    gNotifierVT[6] = reinterpret_cast<void *>(&NotifierRemove);
    gPersistVT[16] = reinterpret_cast<void *>(&PersistSave);
    gPersistVT[34] = reinterpret_cast<void *>(&PersistFind);
    gSegVT[10] = reinterpret_cast<void *>(&SegFlush);
    gServerVT[3] = reinterpret_cast<void *>(&ServerSend);
    gServerVT[4] = reinterpret_cast<void *>(&ServerPost);
    for (int i = 0; i < 3; ++i) {
        gTypes[i].mVTable = GameAddress<void>(cRZString::kVTable);
        new (&gTypes[i].mString) std::string(i == 0 ? "Temperate" : i == 1 ? "Desert" : "a name longer than the small string buffer holds");
        gTypes[i].mRefCount = 1;
    }
    gTypeEntries[0] = nullptr;
    for (int i = 0; i < 3; ++i) gTypeEntries[i + 1] = &gTypes[i];
}

// The game cells the code under test reads, saved and restored around a test.
struct Cells {
    uint32_t app, persist, service, server, loaded, list;
    uint8_t table[0x1c], entries[0xc]; // the type table 0x10dab00 and the entry vector 0x10daae8
    Cells()
    {
        std::memcpy(table, GameAddress<void>(0x10dab00), sizeof(table));
        std::memcpy(entries, GameAddress<void>(0x10daae8), sizeof(entries));
        app = *GameAddress<uint32_t>(0x10cc288);
        persist = *GameAddress<uint32_t>(0x10faf6c);
        service = *GameAddress<uint32_t>(0x10c9378);
        server = *GameAddress<uint32_t>(0x10f519c);
        loaded = *GameAddress<uint8_t>(0x10daad4);
        list = *GameAddress<uint32_t>(0x10dab04);
        SetupFakes();
        *GameAddress<void *>(0x10cc288) = &gApp;
        *GameAddress<void *>(0x10faf6c) = &gPersist;
        *GameAddress<void *>(0x10c9378) = &gService;
        *GameAddress<void *>(0x10f519c) = &gServer;
        *GameAddress<uint8_t>(0x10daad4) = 1;
        *GameAddress<uint8_t>(0x10daad5) = 9;
    }
    ~Cells()
    {
        *GameAddress<uint32_t>(0x10cc288) = app;
        *GameAddress<uint32_t>(0x10faf6c) = persist;
        *GameAddress<uint32_t>(0x10c9378) = service;
        *GameAddress<uint32_t>(0x10f519c) = server;
        *GameAddress<uint8_t>(0x10daad4) = uint8_t(loaded);
        *GameAddress<uint32_t>(0x10dab04) = list;
        std::memcpy(GameAddress<void>(0x10dab00), table, sizeof(table));
        std::memcpy(GameAddress<void>(0x10daae8), entries, sizeof(entries));
    }
};

// ---- routes (docs/standalone-shape.md "Tests", tests/shape.h) ----
//
// Every case runs the original once and our side twice: the vtable route (the object carries copies of our vtables, the
// function under test is called through our pristine vtable's slot where the game's vtable of that face holds its RVA) and the
// adapter route (copies of the game's vtables, as when the Create hook is off; the RVA hook's adapter is called, with an
// arity-matched trap wherever the game's vtable holds that RVA: an adapter that dispatched virtually lands there). Both
// copies carry the fixture's fakes in face slots 10 and 21. Vtable words are the only bytes coded (VtCode).

using shape::P;
using shape::Word;

constexpr unsigned kFaces = 4;
constexpr uint32_t kFaceOffset[kFaces] = {0, nhoodterrain::kTerrainFace, nhoodterrain::kPersistFace, nhoodterrain::kMessageFace};
constexpr uint32_t kGameVTable[kFaces] = {cTSNHoodTerrain::kVTable, cTSNHoodTerrain::kTerrainVTable, cTSNHoodTerrain::kPersistVTable,
                                          cTSNHoodTerrain::kMessageVTable};
constexpr unsigned kSlotCount[kFaces] = {7, 44, 12, 4};
constexpr uint32_t kGameUnknownVTable = 0xda2fd0; // cRZUnknown's: what the destructor leaves at +0

enum Route { kOriginalRoute, kVTableRoute, kAdapterRoute };
Route gRoute = kOriginalRoute;
void *gCopies[kFaces][44];
int gTrapHits = 0;

const shape::OurTables<kFaces> &OurVTables() { return shape::Ours<cTSNHoodTerrain>(kFaceOffset); }

// The object's vtables: copies of the route's (the game's on the original and adapter routes, ours on the vtable route) with
// the fixture's recording fakes in face slots 21 (SetTerrainType) and 10 (GetHeightAt).
void InstallVTables(void *object)
{
    for (unsigned f = 0; f < kFaces; ++f) {
        void *const *from = gRoute == kVTableRoute ? OurVTables().face[f] : shape::GameVTable(kGameVTable[f]);
        for (unsigned s = 0; s < kSlotCount[f]; ++s) gCopies[f][s] = from[s];
        shape::VTableAt(object, kFaceOffset[f]) = gCopies[f];
    }
    gCopies[1][21] = reinterpret_cast<void *>(&FaceSetTerrainType);
    gCopies[1][10] = reinterpret_cast<void *>(&FaceGetHeightAt);
}

uint32_t VtCode(uint32_t word)
{
    const unsigned f = shape::FaceOf(word, kFaces, shape::GameTables{kGameVTable}, OurVTables().face, gCopies);
    if (f != ~0u) return shape::kFaceCode | f;
    if (word == P(GameAddress<void>(kGameUnknownVTable)) || word == P(OurVTables().destroyed)) return 0xd1000000u;
    return word;
}

// Replaces every slot of the copies where the game's vtable holds `rva` by a trap popping `words` stack words.
void InstallTrap(uint32_t rva, unsigned words)
{
    const void *target = GameAddress<void>(rva);
    for (unsigned f = 0; f < kFaces; ++f) {
        void *const *game = shape::GameVTable(kGameVTable[f]);
        for (unsigned s = 0; s < kSlotCount[f]; ++s)
            if (game[s] == target) gCopies[f][s] = shape::TrapFor<shape::Count<gTrapHits>>(words);
    }
}

// The slot function at `rva` of face `face`, called the route's way: the original RVA; our pristine vtable's slot; the adapter.
template <class Fn, class... A>
auto TCall(difftest::Context &ctx, cTSNHoodTerrain *t, unsigned face, uint32_t rva, void *adapter, A... args)
{
    void *self = reinterpret_cast<uint8_t *>(t) + kFaceOffset[face];
    if (gRoute == kOriginalRoute) return ctx.Original<Fn>(rva)(self, args...);
    if (gRoute == kAdapterRoute) {
        InstallTrap(rva, shape::Words<A...>());
        return reinterpret_cast<Fn>(adapter)(self, args...);
    }
    unsigned slot = shape::SlotOf(kGameVTable[face], kSlotCount[face], rva);
    if (slot == ~0u) {
        ctx.Fail("rva %x is in no slot of face %u", rva, face);
        slot = 0;
    }
    return reinterpret_cast<Fn>(OurVTables().face[face][slot])(self, args...);
}

#define ADAPTER(f) reinterpret_cast<void *>(&cTSNHoodTerrainAdapters::f)

// ---- objects ----

uint32_t Bits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }

template <class T> c2DArray<T> *MakeArray(std::mt19937 &rng, uint32_t w, uint32_t h, uint32_t refs)
{
    auto *a = static_cast<c2DArray<T> *>(::operator new(sizeof(c2DArray<T>)));
    a->mVTable = GameAddress<void>(c2DArray<T>::kVTable);
    a->mRefCount = refs;
    a->mWidth = w;
    a->mHeight = h;
    new (&a->mData) std::vector<T>();
    a->mData.reserve(w * h + rng() % 4);
    for (uint32_t i = 0; i < w * h; ++i) a->mData.push_back(std::is_same_v<T, float> ? T(float(int(rng() % 2001) - 1000) / 8.0f) : T(rng()));
    // Spare capacity holds nonzero bytes (fresh heap is often zero): a grow within capacity must store its own values (DL).
    std::memset(a->mData.data() + a->mData.size(), 0xa5, (a->mData.capacity() - a->mData.size()) * sizeof(T));
    return a;
}

template <class T> void CaptureArray(Blob &b, const c2DArray<T> *a)
{
    if (!a) { b.push_back(0xdeadbeef); return; }
    b.insert(b.end(), {uint32_t(a->mVTable == GameAddress<void>(c2DArray<T>::kVTable)), a->mRefCount, a->mWidth, a->mHeight,
        uint32_t(a->mData.size()), uint32_t(a->mData.capacity())});
    for (T v : a->mData) b.push_back(std::is_same_v<T, float> ? Bits(float(v)) : uint32_t(v));
}

struct Shape { uint32_t w, h; bool heights, flags; uint32_t type; };

// A terrain in a defined state: every field set from the seed; arrays and vectors sized for (w, h).
cTSNHoodTerrain *MakeTerrain(uint32_t seed, Shape s)
{
    std::mt19937 rng(seed);
    auto *raw = static_cast<uint8_t *>(::operator new(sizeof(cTSNHoodTerrain)));
    for (size_t i = 0; i < sizeof(cTSNHoodTerrain); ++i) raw[i] = uint8_t(rng());
    auto *t = reinterpret_cast<cTSNHoodTerrain *>(raw);
    InstallVTables(t);
    t->mRefCount = 1 + seed % 3;
    t->mWidth = s.w;
    t->mHeight = s.h;
    t->mVertsX = t->mVertsX2 = s.w + 1;
    t->mVertsY = t->mVertsY2 = s.h + 1;
    t->mCellSize = gCellSize;
    t->mInvCellSize = 1.0f / gCellSize;
    t->mWorldSizeX = float(s.w) * gCellSize;
    t->mWorldSizeY = float(s.h) * gCellSize;
    t->mWaterLevel = float(int(rng() % 400)) - 100.0f + 0.5f;
    const uint32_t vertices = (s.w + 1) * (s.h + 1);
    new (&t->mPaint) std::vector<cTSNHoodTerrainVertexPaint>(rng() % 3 ? vertices : 3);
    for (auto &paint : t->mPaint)
        for (auto &layer : paint.mLayers) layer = {uint8_t(rng()), uint8_t(rng()), uint16_t(rng())};
    new (&t->mNormals) std::vector<cTSNHoodTerrainVec3>();
    for (uint32_t i = 0; i < vertices; ++i) t->mNormals.push_back({float(i), float(rng() % 9), -1.0f});
    t->mNormalsValid = uint8_t(rng() & 1);
    new (&t->mFaceRowA) std::vector<cTSNHoodTerrainVec3>(s.w * 2 + rng() % 2);
    new (&t->mFaceRowB) std::vector<cTSNHoodTerrainVec3>(3);
    new (&t->mShoreRows) std::vector<std::vector<uint8_t>>(s.h + 1, std::vector<uint8_t>(s.w + 1, uint8_t(rng())));
    new (&t->mLotRows) std::vector<std::vector<uint8_t>>(2, std::vector<uint8_t>(5, 7));
    t->mTerrainType = gTypeEntries[s.type % 4];
    t->mHeights = s.heights ? MakeArray<float>(rng, s.w + 1, s.h + 1, 1) : nullptr;
    t->mVertexFlags = s.flags ? MakeArray<uint8_t>(rng, s.w + 1, s.h + 1, 1) : nullptr;
    new (&t->mMessageIds) std::set<uint32_t>{0xaf, 0xb1};
    for (auto &k : t->mKey) k = rng();
    return t;
}

// Sizes only: SetSize resizes these without initialising the new elements, so their bytes are whatever the allocator gave.
// Checked (DL): SetSize 0x4b2200 resizes all three through 0x3b58f0, which only moves the end pointer (or reallocates via
// 0x299710, copying the old elements) and never stores into new ones; Read 0x4b0980 and SetHeightAt 0x4b2540 never write them.
void CaptureVec3(Blob &b, const std::vector<cTSNHoodTerrainVec3> &v)
{
    b.insert(b.end(), {uint32_t(v.size()), uint32_t(v.capacity())});
}
void CaptureRows(Blob &b, const std::vector<std::vector<uint8_t>> &rows)
{
    b.insert(b.end(), {uint32_t(rows.size()), uint32_t(rows.capacity())});
    for (auto &r : rows) {
        b.insert(b.end(), {uint32_t(r.size()), uint32_t(r.capacity())});
        for (uint8_t c : r) b.push_back(c);
    }
}
uint32_t TypeIndex(const void *p)
{
    for (int i = 0; i < 4; ++i) if (gTypeEntries[i] == p) return uint32_t(i);
    for (uint32_t i = 0; gEntries && i < 4; ++i) if (gEntries + 0x828 * i == p) return 100 + i;
    return 99;
}

Blob Capture(const cTSNHoodTerrain *t)
{
    Blob b{VtCode(Word(t, 0)), VtCode(Word(t, kFaceOffset[1])), VtCode(Word(t, kFaceOffset[2])), VtCode(Word(t, kFaceOffset[3])),
        t->mRefCount, t->mWidth, t->mHeight,
        t->mVertsX, t->mVertsY, Bits(t->mCellSize), Bits(t->mInvCellSize), Bits(t->mWorldSizeX), Bits(t->mWorldSizeY),
        Bits(t->mWaterLevel), t->mVertsX2, t->mVertsY2, t->mNormalsValid, TypeIndex(t->mTerrainType)};
    b.insert(b.end(), {uint32_t(t->mPaint.size()), uint32_t(t->mPaint.capacity())});
    // Each layer's type and weight (SetSize zeroes them in new elements); the pad byte is never written.
    for (const auto &paint : t->mPaint)
        for (const auto &layer : paint.mLayers) b.push_back(layer.mType | uint32_t(layer.mWeight) << 16);
    CaptureVec3(b, t->mNormals);
    CaptureVec3(b, t->mFaceRowA);
    CaptureVec3(b, t->mFaceRowB);
    CaptureRows(b, t->mShoreRows);
    CaptureRows(b, t->mLotRows);
    CaptureArray(b, t->mHeights);
    CaptureArray(b, t->mVertexFlags);
    b.push_back(uint32_t(t->mMessageIds.size()));
    for (uint32_t id : t->mMessageIds) b.push_back(id);
    b.insert(b.end(), std::begin(t->mKey), std::end(t->mKey));
    return b;
}

void Destroy(difftest::Context &ctx, cTSNHoodTerrain *t)
{
    ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x4ad290)(t, 1);
}

// The static terrain-type list (head cell 0x10dab04): a sentinel and a few nodes, each with a weight map at +0x6c.
struct TypeList {
    std::vector<cTSNHoodTerrainTypeNode *> nodes; // [0] is the sentinel
    explicit TypeList(uint32_t seed)
    {
        std::mt19937 rng(seed + 991);
        const uint32_t count = 1 + rng() % 3;
        for (uint32_t i = 0; i <= count; ++i) {
            auto *node = static_cast<cTSNHoodTerrainTypeNode *>(::operator new(0x90));
            std::memset(node, 0, 0x90);
            node->mWeightMap.mVTable = GameAddress<void>(c2DArray<uint8_t>::kVTable);
            new (&node->mWeightMap.mData) std::vector<uint8_t>();
            if (i) {
                node->mWeightMap.mWidth = 2;
                node->mWeightMap.mHeight = 1 + rng() % 3;
                node->mWeightMap.mData.assign(node->mWeightMap.mWidth * node->mWeightMap.mHeight, uint8_t(rng() | 1));
            }
            nodes.push_back(node);
        }
        // Ids, names and flags (slots 29-33): ids 1..4 apart by 4, so some share a bucket of the four.
        for (uint32_t i = 1; i <= count; ++i) {
            nodes[i]->mId = uint8_t(1 + rng() % 4 + 4 * (i - 1));
            nodes[i]->mFlag = uint8_t(rng());
            static const char *const kNames[] = {"Temperate", "Desert", "a name longer than the small string buffer holds", "Ice"};
            MakeString(nodes[i]->mName, kNames[(i + seed) % 4]);
            MakeString(nodes[i]->mAltName, kNames[(i + seed + 1) % 4]);
        }
        // The hash table at 0x10dab00 (StdHash.h): the list holds each bucket's nodes together, buckets of (FNV-1a(id) & 15)
        // (16 buckets: a wrong hash multiplier differs from the right one only above the low two bits).
        std::vector<cTSNHoodTerrainTypeNode *> order(nodes.begin() + 1, nodes.end());
        std::stable_sort(order.begin(), order.end(), [](auto *a, auto *b) { return Bucket(a->mId) < Bucket(b->mId); });
        order.insert(order.begin(), nodes[0]);
        for (size_t i = 0; i < order.size(); ++i) {
            order[i]->mNext = order[(i + 1) % order.size()];
            order[(i + 1) % order.size()]->mPrev = order[i];
        }
        for (auto &bucket : buckets) bucket = {reinterpret_cast<StdHashNode *>(nodes[0]), reinterpret_cast<StdHashNode *>(nodes[0])};
        for (size_t i = order.size() - 1; i >= 1; --i) buckets[Bucket(order[i]->mId)].first = reinterpret_cast<StdHashNode *>(order[i]);
        for (size_t i = 1; i < order.size(); ++i) buckets[Bucket(order[i]->mId)].last = reinterpret_cast<StdHashNode *>(order[i]);
        auto *table = GameAddress<StdHashTable>(0x10dab00);
        table->mHead = reinterpret_cast<StdHashNode *>(nodes[0]);
        table->mBuckets = buckets;
        table->mMask = 15;
        // The type entries (SetTerrainType): 0x828 bytes each, a name first.
        entryCount = rng() % 4;
        entries = static_cast<uint8_t *>(::operator new(0x828 * 4));
        static const char *const kEntryNames[] = {"Desert", "temperate", "Ice", "Temperate"};
        for (uint32_t i = 0; i < entryCount; ++i) MakeString(entries + 0x828 * i, kEntryNames[(i + seed / 2) % 4]);
        uint8_t **vec = GameAddress<uint8_t *>(0x10daae8);
        vec[0] = entries;
        vec[1] = vec[2] = entries + 0x828 * entryCount;
        gEntries = entries;
    }
    static uint32_t Bucket(uint8_t id) { return ((id ^ 0x811c9dc5u) * 0x1000193u) & 15; }
    static void MakeString(void *at, const char *text)
    {
        auto *s = static_cast<cRZString *>(at);
        s->mVTable = GameAddress<void>(cRZString::kVTable);
        new (&s->mString) std::string(text);
        s->mRefCount = 1;
    }
    void Capture(Blob &b) const
    {
        for (size_t i = 1; i < nodes.size(); ++i) CaptureArray(b, &nodes[i]->mWeightMap);
    }
    ~TypeList()
    {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (i) {
                reinterpret_cast<cRZString *>(nodes[i]->mName)->mString.~basic_string();
                reinterpret_cast<cRZString *>(nodes[i]->mAltName)->mString.~basic_string();
            }
            nodes[i]->mWeightMap.mData.~vector();
            ::operator delete(nodes[i], 0x90);
        }
        for (uint32_t i = 0; i < entryCount; ++i) reinterpret_cast<cRZString *>(entries + 0x828 * i)->mString.~basic_string();
        ::operator delete(entries, 0x828 * 4);
        gEntries = nullptr;
    }
    StdHashBucket buckets[16];
    uint8_t *entries;
    uint32_t entryCount;
};

// One run on a fresh terrain; `call` does the work and returns the result word.
using Call = uint32_t (*)(difftest::Context &, bool ours, cTSNHoodTerrain *);
Blob Run(difftest::Context &ctx, Call call, bool ours, uint32_t seed, Shape shape, const std::vector<Item> &script, int fail, int *total)
{
    Begin(script, fail, gOutErrorWanted);
    cTSNHoodTerrain *t = MakeTerrain(seed, shape);
    TypeList list(seed);
    gBase = t;
    uint32_t returned = 0;
    bool threw = false;
    gTrapHits = 0;
    try { returned = call(ctx, ours, t); } catch (int) { threw = true; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw), uint32_t(gCursor), uint32_t(gTrapHits)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    gLastStateStart = blob.size();
    Blob state = Capture(t);
    blob.insert(blob.end(), state.begin(), state.end());
    list.Capture(blob);
    gBase = nullptr;
    Destroy(ctx, t);
    return blob;
}

const Shape kShapes[] = {{4, 4, true, true, 1}, {2, 3, true, false, 2}, {3, 2, false, true, 3}, {5, 5, false, false, 0}, {1, 1, true, true, 1},
    {8, 8, true, true, 3}, {0, 0, true, true, 1}, {6, 2, false, false, 2}};

// Both sides over every shape (and, with `fail`, a throw at every stream call).
void Sweep(difftest::Context &ctx, Call call, const std::vector<std::vector<Item>> &scripts, const char *name, bool inject = true)
{
    uint64_t before = gThrown;
    size_t index = 0;
    for (const auto &script : scripts) {
        for (size_t s = 0; s < std::size(kShapes); ++s) {
            int total = 0;
            for (int fail = -1; fail < (inject ? total : 0); fail += (fail < 0 || total <= 256) ? 1 : total / 256) { // a throw at each call; past 256 calls, at 256 spread over them
                const uint32_t seed = uint32_t(index * 53 + s);
                gCellSize = (seed % 3 == 0) ? 10.0f : (seed % 3 == 1) ? 0.5f : 7.25f;
                gRoute = kOriginalRoute;
                Blob original = Run(ctx, call, false, seed, kShapes[s], script, fail, fail < 0 ? &total : nullptr); // only the clean run counts the calls
                for (Route route : {kVTableRoute, kAdapterRoute}) {
                gRoute = route;
                Blob ours = Run(ctx, call, true, seed, kShapes[s], script, fail, nullptr);
                gRoute = kOriginalRoute;
                if (original != ours) {
                    size_t at = 0;
                    while (at < original.size() && at < ours.size() && original[at] == ours[at]) ++at;
                    auto w = [&](const Blob &b, size_t i) { return i < b.size() ? b[i] : 0xffffffffu; };
                    ctx.Fail("%s route %d script %zu shape %zu fail %d: result / state / fake calls differ at word %zu of %zu/%zu (state starts at %zu): "
                             "orig %08x %08x [%08x] %08x vs ours %08x %08x [%08x] %08x", name, int(route), index, s, fail, at, original.size(), ours.size(),
                        gLastStateStart, w(original, at - 2), w(original, at - 1), w(original, at), w(original, at + 1), w(ours, at - 2),
                        w(ours, at - 1), w(ours, at), w(ours, at + 1));
                }
                ++ctx.cases;
                }
            }
        }
        ++index;
    }
    if (inject) std::printf("  %s: exceptions injected %llu\n", name, gThrown - before);
}

std::vector<Item> ArrayRecord(uint32_t w, uint32_t h, uint32_t extra = 0)
{
    std::vector<Item> s{U(0x6b943b43), U(1), S("c2DArray"), U(w), U(h)};
    for (uint32_t i = 0; i < w * h + extra; ++i) s.push_back(F(float(i) * 0.5f + 1.0f));
    return s;
}
std::vector<Item> TerrainRecord(uint32_t classId, uint32_t version, uint32_t w, uint32_t h, const char *type, bool withArray, bool shortArray = false)
{
    std::vector<Item> s{U(classId), U(version), U(w), U(h), F(312.5f)};
    if (version >= 2) s.push_back(S(type));
    if (withArray) {
        auto a = ArrayRecord(w + 1, h + 1);
        if (shortArray) a.resize(a.size() - 3);
        s.insert(s.end(), a.begin(), a.end());
    }
    return s;
}
constexpr uint32_t kClass = 0xabcb5da4;
} // namespace

#define TERRAIN_TEST(name, rva)                                                                                         \
    static void name##Body(difftest::Context &ctx);                                                                      \
    DIFF_TEST(name, rva)                                                                                                 \
    {                                                                                                                    \
        svcfx::AllocatorScope allocator;                                                                                 \
        Cells cells;                                                                                                     \
        gHaveService = gHaveCheatMgr = true;                                                                             \
        gSaveRet = gFindRet = gFlushRet = gSetTypeRet = 1;                                                               \
        name##Body(ctx);                                                                                                 \
    }                                                                                                                    \
    static void name##Body(difftest::Context &ctx)

TERRAIN_TEST(NHoodTerrain_Read, 0x4b0980)
{
    std::vector<std::vector<Item>> scripts{{}, {U(kClass)}, {U(kClass + 1), U(4)}, {U(0), U(0)}};
    for (uint32_t version = 0; version <= 5; ++version) {
        scripts.push_back(TerrainRecord(kClass, version, 4, 4, "Temperate", true));
        scripts.push_back(TerrainRecord(kClass, version, 3, 2, "Desert", true));
    }
    scripts.push_back(TerrainRecord(kClass, 4, 4, 4, "Temperate", true, true));   // heights stream ends early: the array stays zero-filled
    scripts.push_back(TerrainRecord(kClass, 3, 2, 2, "", false));                 // no array at all: Read fails, ResetHeights
    auto badArray = TerrainRecord(kClass, 4, 2, 2, "Temperate", true);
    badArray[6].bits = 0;                                                         // the array's class id is wrong
    scripts.push_back(badArray);
    scripts.push_back(TerrainRecord(kClass, 4, 0, 0, "Temperate", true));
    for (uint32_t cut : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 9u, 12u}) {
        auto s = TerrainRecord(kClass, 4, 3, 3, "Temperate", true);
        s.resize(cut);
        scripts.push_back(s);
    }
    for (uint32_t setType : {0u, 1u}) {
        gSetTypeRet = uint8_t(setType);
        Sweep(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            auto *stream = reinterpret_cast<ifaces::cIGZIStream *>(&gIn);
            return TCall<uint8_t(__thiscall *)(void *, void *)>(c, t, 1, 0x4b0980, ADAPTER(Read), stream);
        }, scripts, "Read");
    }
}

TERRAIN_TEST(NHoodTerrain_Write, 0x4b2fb0)
{
    for (bool outError : {false, true}) {
        gOutErrorWanted = outError;
        Sweep(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            auto *stream = reinterpret_cast<ifaces::cIGZOStream *>(&gOut);
            return TCall<uint8_t(__thiscall *)(void *, void *)>(c, t, 1, 0x4b2fb0, ADAPTER(Write), stream);
        }, {{}}, outError ? "Write (stream error)" : "Write");
    }
    gOutErrorWanted = false;
}

// ---- the rest of lane 1 ----

namespace {
uint32_t gA, gB, gC;
float gF;
bool gSame, gNotify;
const std::vector<Item> kNoScript;

uint32_t Fold(uint32_t acc, uint32_t v) { return (acc ^ v) * 16777619u + 0x9e3779b9u; }
uint32_t FoldF(uint32_t acc, float v) { return Fold(acc, Bits(v)); }

Blob RunSimple(difftest::Context &ctx, Call call, bool ours, uint32_t seed, Shape shape)
{
    return Run(ctx, call, ours, seed, shape, kNoScript, -1, nullptr);
}

// Both sides over every shape and a few seeds. `needHeights` / `needFlags` skip shapes without the array the function reads.
void Each(difftest::Context &ctx, Call call, const char *name, bool needHeights = false, bool needFlags = false, bool needNormals = false)
{
    (void)needNormals;
    for (size_t s = 0; s < std::size(kShapes); ++s) {
        if ((needHeights && !kShapes[s].heights) || (needFlags && !kShapes[s].flags)) continue;
        for (uint32_t seed = 0; seed < 4; ++seed) {
            gCellSize = (seed % 3 == 0) ? 10.0f : (seed % 3 == 1) ? 0.5f : 7.25f;
            gRoute = kOriginalRoute;
            Blob original = RunSimple(ctx, call, false, seed + uint32_t(s) * 17, kShapes[s]);
            for (Route route : {kVTableRoute, kAdapterRoute}) {
                gRoute = route;
                Blob ours = RunSimple(ctx, call, true, seed + uint32_t(s) * 17, kShapes[s]);
                gRoute = kOriginalRoute;
                if (original != ours) {
                    size_t at = 0;
                    while (at < original.size() && at < ours.size() && original[at] == ours[at]) ++at;
                    ctx.Fail("%s route %d shape %zu seed %u: result / state / fake calls differ at word %zu of %zu/%zu (state at %zu)",
                             name, int(route), s, seed, at, original.size(), ours.size(), gLastStateStart);
                }
                ++ctx.cases;
            }
        }
    }
}
} // namespace

#define LEAF_TEST(name, rva, ...)                                                                                       \
    TERRAIN_TEST(name, rva)                                                                                              \
    {                                                                                                                    \
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t { __VA_ARGS__ }, #name, kNeeds_##name##_H, kNeeds_##name##_F); \
    }

namespace {
constexpr bool kNeeds_NHoodTerrain_GetHeightAt_H = true, kNeeds_NHoodTerrain_GetHeightAt_F = false;
constexpr bool kNeeds_NHoodTerrain_GetWaterDepthAt_H = true, kNeeds_NHoodTerrain_GetWaterDepthAt_F = false;
constexpr bool kNeeds_NHoodTerrain_GetVertexFlag_H = false, kNeeds_NHoodTerrain_GetVertexFlag_F = true;
constexpr bool kNeeds_NHoodTerrain_IsVertexUnderWater_H = true, kNeeds_NHoodTerrain_IsVertexUnderWater_F = false;
constexpr bool kNeeds_NHoodTerrain_IsCellUnderWater_H = true, kNeeds_NHoodTerrain_IsCellUnderWater_F = false;
constexpr bool kNeeds_NHoodTerrain_IsCellAboveWater_H = true, kNeeds_NHoodTerrain_IsCellAboveWater_F = false;
constexpr bool kNeeds_NHoodTerrain_IsInWorld_H = false, kNeeds_NHoodTerrain_IsInWorld_F = false;
constexpr bool kNeeds_NHoodTerrain_WorldToVertex_H = false, kNeeds_NHoodTerrain_WorldToVertex_F = false;
constexpr bool kNeeds_NHoodTerrain_GetVertexNormal_H = false, kNeeds_NHoodTerrain_GetVertexNormal_F = false;
constexpr bool kNeeds_NHoodTerrain_GetTerrainTypeName_H = false, kNeeds_NHoodTerrain_GetTerrainTypeName_F = false;
constexpr bool kNeeds_NHoodTerrain_GetMaxTypeId_H = false, kNeeds_NHoodTerrain_GetMaxTypeId_F = false;
constexpr bool kNeeds_NHoodTerrain_GetGZCLSID_H = false, kNeeds_NHoodTerrain_GetGZCLSID_F = false;
constexpr bool kNeeds_NHoodTerrain_Key_H = false, kNeeds_NHoodTerrain_Key_F = false;
} // namespace

LEAF_TEST(NHoodTerrain_GetHeightAt, 0x4afc10, {
    uint32_t acc = 0;
    for (int32_t y = 0; y <= int32_t(t->mHeight); ++y) for (int32_t x = 0; x <= int32_t(t->mWidth); ++x)
        acc = FoldF(acc, TCall<float(__thiscall *)(void *, int32_t, int32_t)>(c, t, 1, 0x4afc10, ADAPTER(GetHeightAt), x, y));
    return acc;
})
LEAF_TEST(NHoodTerrain_GetWaterDepthAt, 0x4b2d50, {
    uint32_t acc = 0;
    for (uint32_t y = 0; y <= t->mHeight; ++y) for (uint32_t x = 0; x <= t->mWidth; ++x)
        acc = FoldF(acc, TCall<float(__thiscall *)(void *, uint32_t, uint32_t)>(c, t, 1, 0x4b2d50, ADAPTER(GetWaterDepthAt), x, y));
    return acc;
})
LEAF_TEST(NHoodTerrain_GetVertexFlag, 0x4b00e0, {
    uint32_t acc = 0;
    for (uint32_t layer = 0; layer < 3; ++layer) for (uint32_t y = 0; y <= t->mHeight; ++y) for (uint32_t x = 0; x <= t->mWidth; ++x) {
        const uint32_t vertex[3] = {x, y, layer};
        acc = Fold(acc, TCall<uint8_t(__thiscall *)(void *, const uint32_t *)>(c, t, 1, 0x4b00e0, ADAPTER(GetVertexFlag), vertex));
    }
    return acc;
})
// The cell and vertex water tests: x and y run past the counts (the tests stop at the cell counts / vertex counts).
LEAF_TEST(NHoodTerrain_IsVertexUnderWater, 0x4b0110, {
    uint32_t acc = 0;
    for (uint32_t y = 0; y <= t->mVertsY + 1; ++y) for (uint32_t x = 0; x <= t->mVertsX + 1; ++x)
        acc = Fold(acc, TCall<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(c, t, 1, 0x4b0110, ADAPTER(IsVertexUnderWater), x, y));
    return acc;
})
LEAF_TEST(NHoodTerrain_IsCellUnderWater, 0x4b0030, {
    uint32_t acc = 0;
    for (uint32_t y = 0; y <= t->mHeight + 1; ++y) for (uint32_t x = 0; x <= t->mWidth + 1; ++x)
        acc = Fold(acc, TCall<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(c, t, 1, 0x4b0030, ADAPTER(IsCellUnderWater), x, y));
    return acc;
})
LEAF_TEST(NHoodTerrain_IsCellAboveWater, 0x4affc0, {
    uint32_t acc = 0;
    for (uint32_t y = 0; y <= t->mHeight + 1; ++y) for (uint32_t x = 0; x <= t->mWidth + 1; ++x)
        acc = Fold(acc, TCall<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(c, t, 1, 0x4affc0, ADAPTER(IsCellAboveWater), x, y));
    return acc;
})
LEAF_TEST(NHoodTerrain_IsInWorld, 0x4b00a0, {
    uint32_t acc = 0;
    const float values[] = {-1.0f, -0.0f, 0.0f, 0.5f, t->mWorldSizeX, t->mWorldSizeX + 0.001f, t->mWorldSizeY, t->mWorldSizeY * 0.5f,
        t->mWorldSizeY + 1.0f, 1.0e9f, std::numeric_limits<float>::quiet_NaN()};
    for (float x : values) for (float z : values)
        acc = Fold(acc, TCall<uint8_t(__thiscall *)(void *, float, float)>(c, t, 1, 0x4b00a0, ADAPTER(IsInWorld), x, z));
    return acc;
})
LEAF_TEST(NHoodTerrain_WorldToVertex, 0x4ae780, {
    uint32_t acc = 0;
    const float values[] = {-1.0f, 0.0f, 0.5f, 3.3f, 12.0f, t->mWorldSizeX, t->mWorldSizeY * 0.7f, 1.0e5f, -4.5e4f};
    for (float x : values) for (float z : values) {
        int32_t ax = 7, az = 7;
        TCall<void(__thiscall *)(void *, float, float, int32_t *, int32_t *)>(c, t, 1, 0x4ae780, ADAPTER(WorldToVertex), x, z, &ax, &az);
        acc = Fold(Fold(acc, uint32_t(ax)), uint32_t(az));
    }
    return acc;
})
LEAF_TEST(NHoodTerrain_GetVertexNormal, 0x4af700, {
    uint32_t acc = 0;
    for (uint32_t y = 0; y <= t->mVertsY; ++y) for (uint32_t x = 0; x <= t->mVertsX; ++x) {
        float out[4] = {-7.0f, -7.0f, -7.0f, -7.0f};
        TCall<void(__thiscall *)(void *, uint32_t, uint32_t, float *)>(c, t, 1, 0x4af700, ADAPTER(GetVertexNormal), x, y, out);
        for (float v : out) acc = FoldF(acc, v);
    }
    return acc;
})
LEAF_TEST(NHoodTerrain_GetTerrainTypeName, 0x4afbf0, {
    cRZString out;
    out.mVTable = GameAddress<void>(cRZString::kVTable);
    new (&out.mString) std::string("untouched");
    out.mRefCount = 0;
    const uint8_t r = TCall<uint8_t(__thiscall *)(void *, cIGZString *)>(c, t, 1, 0x4afbf0, ADAPTER(GetTerrainTypeName), out.AsInterface());
    const uint32_t result = Fold(Fold(0, r), Hash(out.mString));
    out.mString.~basic_string();
    return result;
})
LEAF_TEST(NHoodTerrain_GetMaxTypeId, 0x4b0290, {
    return TCall<uint8_t(__thiscall *)(void *)>(c, t, 1, 0x4b0290, ADAPTER(GetMaxTypeId));
})
LEAF_TEST(NHoodTerrain_GetGZCLSID, 0x4af2d0, {
    return TCall<uint32_t(__thiscall *)(void *)>(c, t, 1, 0x4af2d0, ADAPTER(GetGZCLSID));
})
// GetKey / SetKey / SetType (the persist face): the key is 16 bytes, the type the first word.
LEAF_TEST(NHoodTerrain_Key, 0x4af5d0, {
    uint32_t acc = 0;
    uint32_t out[5] = {1, 2, 3, 4, 0x5a5a5a5a};
    uint32_t *r = TCall<uint32_t *(__thiscall *)(void *, uint32_t *)>(c, t, 2, 0x4af5d0, ADAPTER(GetKey), out);
    acc = Fold(uint32_t(r == out), out[0]); acc = Fold(Fold(Fold(Fold(acc, out[1]), out[2]), out[3]), out[4]);
    const uint32_t in[4] = {0x11, 0x22, 0x33, 0x44};
    const uint8_t s = TCall<uint8_t(__thiscall *)(void *, const uint32_t *)>(c, t, 2, 0x4b21d0, ADAPTER(SetKey), in);
    const uint8_t u = TCall<uint8_t(__thiscall *)(void *, uint32_t)>(c, t, 2, 0x4b2630, ADAPTER(SetType), 0xabcdef01);
    return Fold(Fold(acc, s), u);
})

// ---- construction, destruction, interfaces ----

TERRAIN_TEST(NHoodTerrain_Create, 0x4af120)
{
    // The constructor never stores 0x14-0x33, 0x38-0x4f and 0x69-0x6b (DL: 0x4af120 stores the vtables, 0x34 (20.0f),
    // 0x50-0xa7 (0x68 as a byte) and 0xb0-0xbf, nothing else). Every heap block both sides allocate is poisoned
    // (heap_poison.h, DT), so those bytes hold the pattern on both sides and are compared too, under two patterns. Our side
    // is the Create hook's entry (our constructor, our vtables): the four vtable words are coded, every other byte raw.
    for (int round = 0; round < 6; ++round) {
        heappoison::Scope poison(ctx, round % 2 ? 0x5a : 0xcd);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            void *face = side ? static_cast<void *>(cTSNHoodTerrain::Create()) : ctx.Original<void *(__cdecl *)()>(0x4af120)();
            auto *t = reinterpret_cast<cTSNHoodTerrain *>(static_cast<uint8_t *>(face) - 8);
            const auto *raw = reinterpret_cast<const uint8_t *>(t);
            blobs[side] = {uint32_t(static_cast<uint8_t *>(face) - raw)};
            for (unsigned f = 0; f < kFaces; ++f) blobs[side].push_back(VtCode(Word(t, kFaceOffset[f])));
            for (size_t i = 0x14; i < 0xc0; ++i) {
                const bool heap = i >= 0xa8 && i < 0xb0; // the id set: compared below
                if (!heap) blobs[side].push_back(raw[i]);
            }
            blobs[side].push_back(Word(t, 4)); // the count
            blobs[side].push_back(uint32_t(t->mMessageIds.size()));
            for (uint32_t id : t->mMessageIds) blobs[side].push_back(id);
            ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x4ad290)(t, 1);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("Create round %d: the constructed object differs", round);
        ++ctx.cases;
    }
}

TERRAIN_TEST(NHoodTerrain_DeletingDestructor, 0x4ad290)
{
    // Primary slot 3 on each route; with flags 0 the object is left for inspection: the +0 vtable (cRZUnknown's), the +8 one
    // (the class's, coded), the emptied vectors, the kept array pointer.
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (const Shape &sh : kShapes) {
        Blob blobs[3];
        for (int route = 0; route < 3; ++route) {
            gRoute = Route(route);
            cTSNHoodTerrain *t = MakeTerrain(flags * 11 + sh.w, sh);
            const uint32_t keep = sh.heights ? 1 : 0;
            if (t->mHeights) t->mHeights->mRefCount = 1 + (flags & 1); // shared: survives the release
            gTrapHits = 0;
            difftest::FreeRecorder rec;
            void *r = TCall<void *(__thiscall *)(void *, uint32_t)>(ctx, t, 0, 0x4ad290, ADAPTER(DeletingDestructor), flags);
            rec.Stop();
            size_t sizes[64]{};
            const size_t freedCount = rec.SortedSizes(sizes, 64);
            blobs[route] = {uint32_t(r == t), uint32_t(rec.Freed(t)), uint32_t(freedCount), keep, uint32_t(gTrapHits)};
            for (size_t i = 0; i < freedCount && i < 64; ++i) blobs[route].push_back(uint32_t(sizes[i]));
            // What is left of an object that was not freed: the vtables and the emptied vectors.
            if (!rec.Freed(t)) {
                for (unsigned f = 0; f < kFaces; ++f) blobs[route].push_back(VtCode(Word(t, kFaceOffset[f])));
                blobs[route].push_back(uint32_t(t->mPaint.data() == nullptr && t->mNormals.data() == nullptr && t->mShoreRows.data() == nullptr &&
                    t->mLotRows.data() == nullptr && t->mFaceRowA.data() == nullptr && t->mFaceRowB.data() == nullptr));
                blobs[route].push_back(uint32_t(t->mHeights != nullptr));
                ::operator delete(t, sizeof(cTSNHoodTerrain));
            }
        }
        gRoute = kOriginalRoute;
        for (int route = 1; route < 3; ++route)
            if (blobs[0] != blobs[route]) ctx.Fail("DeletingDestructor route %d flags %u shape w%u: frees / left-over state differ", route, flags, sh.w);
        ctx.cases += 2;
    }
}

TERRAIN_TEST(NHoodTerrain_QueryInterface, 0x4b05b0)
{
    // Primary (this = object) and the three face thunks (this = face): the ids and a null out.
    const uint32_t ids[] = {1, 0x456b8f1d, 0xabcb5d7a, 0xe4fda3d4, 0x452294aa, 0, 2, 0xffffffff, 0x456b8f1c, 0xabcb5d7b};
    struct Entry { uint32_t rva; unsigned face; void *adapter; };
    const Entry entries[] = {{0x4b05b0, 0, ADAPTER(QueryInterface)}, {0x4b0602, 1, ADAPTER(TerrainQueryInterface)},
                             {0x4b0612, 2, ADAPTER(PersistQueryInterface)}, {0x4b060a, 3, ADAPTER(MessageQueryInterface)}};
    for (const Entry &e : entries) for (uint32_t id : ids) for (bool nullOut : {false, true}) {
        Blob blobs[3];
        for (int route = 0; route < 3; ++route) {
            gRoute = Route(route);
            cTSNHoodTerrain *t = MakeTerrain(id, kShapes[3]);
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            void **outp = nullOut ? nullptr : &out;
            gTrapHits = 0;
            const uint8_t r = TCall<uint8_t(__thiscall *)(void *, uint32_t, void **)>(ctx, t, e.face, e.rva, e.adapter, id, outp);
            blobs[route] = {r, out == reinterpret_cast<void *>(0x5a5a5a5a) ? 0xfffffff0u : uint32_t(static_cast<uint8_t *>(out) - reinterpret_cast<uint8_t *>(t)),
                t->mRefCount, uint32_t(gTrapHits)};
            Destroy(ctx, t);
        }
        gRoute = kOriginalRoute;
        for (int route = 1; route < 3; ++route)
            if (blobs[0] != blobs[route]) ctx.Fail("QueryInterface %x route %d id %08x nullOut %d: differs", e.rva, route, id, nullOut);
        ctx.cases += 2;
    }
}

// The slots no other test calls, each through each side's own vtable (the original's object carries the game's, ours ours):
// cRZUnknown's AddRef / Release / RemoveRef / RefCount (primary 1, 2, 4, 5), the faces' AddRef / Release thunks, the water level
// accessors (slots 15 / 16), AsPersistResource (40), the persist face's folded key accessors (5-7, 9, 10) and GetRefCount (11).
// The result, the out values, the count and the key are compared, and the vtable words (coded).
TERRAIN_TEST(NHoodTerrain_Slots, 0x4af9d0)
{
    struct SlotCase { unsigned face, slot; int kind; uint32_t arg; };
    // kind: 0 no argument, dword result; 1 one dword argument, byte result; 2 float argument (slot 15); 3 float result (16)
    const SlotCase cases[] = {
        {0, 1, 0, 0}, {0, 2, 0, 0}, {0, 4, 0, 0}, {0, 5, 0, 0},
        {1, 1, 0, 0}, {1, 2, 0, 0}, {2, 1, 0, 0}, {2, 2, 0, 0}, {3, 1, 0, 0}, {3, 2, 0, 0},
        {1, 15, 2, 0}, {1, 16, 3, 0}, {1, 40, 0, 0},
        {2, 5, 0, 0}, {2, 6, 0, 0}, {2, 7, 0, 0}, {2, 9, 1, 0x1234}, {2, 10, 1, 0xfedc}, {2, 11, 0, 0},
    };
    for (const SlotCase &k : cases) for (uint32_t seed : {1u, 2u, 3u, 7u}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gRoute = side ? kVTableRoute : kOriginalRoute;
            cTSNHoodTerrain *t = MakeTerrain(seed * 5 + k.slot, kShapes[1]);
            if (k.face == 0 && k.slot == 2) t->mRefCount = 2 + (seed & 1); // Release: keep the object (a count above 1)
            if (k.face != 0 && k.slot == 2) t->mRefCount = 3;
            void *self = reinterpret_cast<uint8_t *>(t) + kFaceOffset[k.face];
            void *fn = (*reinterpret_cast<void ***>(self))[k.slot];
            uint32_t r = 0;
            switch (k.kind) {
            case 0: r = reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self); break;
            case 1: r = reinterpret_cast<uint8_t(__thiscall *)(void *, uint32_t)>(fn)(self, k.arg); break;
            case 2: reinterpret_cast<void(__thiscall *)(void *, float)>(fn)(self, 12.5f + float(seed)); break;
            case 3: r = Bits(reinterpret_cast<float(__thiscall *)(void *)>(fn)(self)); break;
            }
            // Pointers into the object as offsets (AsPersistResource answers the +0xc face).
            if (k.face == 1 && k.slot == 40) r = r - P(t);
            blobs[side] = {r};
            Blob state = Capture(t);
            blobs[side].insert(blobs[side].end(), state.begin(), state.end());
            Destroy(ctx, t);
        }
        gRoute = kOriginalRoute;
        if (blobs[0] != blobs[1]) ctx.Fail("Slots face %u slot %u seed %u: differs", k.face, k.slot, seed);
        ++ctx.cases;
    }
    // The thunk 0x4af9d0 (persist slot 11) and the face QI thunks are the game's slots of those faces.
    if (shape::SlotOf(kGameVTable[2], kSlotCount[2], 0x4af9d0) != 11) ctx.Fail("0x4af9d0 is not persist slot 11");
    // Our faces sit where the original's do.
    cTSNHoodTerrain *t = new cTSNHoodTerrain;
    auto at = [&](const void *face) { return uint32_t(static_cast<const uint8_t *>(face) - reinterpret_cast<uint8_t *>(t)); };
    if (at(static_cast<rz::cRZUnknown *>(t)) != 0 || at(static_cast<cITSNHoodTerrain *>(t)) != kFaceOffset[1] ||
        at(static_cast<cIGZPersistResource *>(t)) != kFaceOffset[2] || at(static_cast<cIGZMessageTarget2 *>(t)) != kFaceOffset[3])
        ctx.Fail("faces not at the original's offsets");
    delete t;
}

// ---- save, sizes, resets, registration, initialise, shutdown, set height, messages ----

TERRAIN_TEST(NHoodTerrain_Save, 0x4b1e40)
{
    for (int save : {0, 1, 0x41}) for (int find : {0, 1}) for (int flush : {0, 1, 0x40, 0x80}) {
        gSaveRet = uint8_t(save); gFindRet = uint8_t(find); gFlushRet = uint8_t(flush);
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            return TCall<uint8_t(__thiscall *)(void *)>(c, t, 1, 0x4b1e40, ADAPTER(Save));
        }, "Save");
    }
}

TERRAIN_TEST(NHoodTerrain_SetSize, 0x4b2200)
{
    const uint32_t sizes[][2] = {{0, 0}, {1, 1}, {3, 2}, {4, 4}, {7, 5}, {16, 16}, {2, 9}};
    for (auto &size : sizes) {
        gA = size[0]; gB = size[1];
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            if (ours) t->SetSize(gA, gB);
            else c.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0x4b2200)(t, gA, gB);
            return 0;
        }, "SetSize");
    }
}

TERRAIN_TEST(NHoodTerrain_ResetHeights, 0x4afd90)
{
    Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
        if (ours) t->ResetHeights(); else c.Original<void(__thiscall *)(void *)>(0x4afd90)(t);
        return 0;
    }, "ResetHeights");
}

TERRAIN_TEST(NHoodTerrain_ResetVertexFlags, 0x4aff00)
{
    Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
        if (ours) t->ResetVertexFlags(); else c.Original<void(__thiscall *)(void *)>(0x4aff00)(t);
        return 0;
    }, "ResetVertexFlags");
}

TERRAIN_TEST(NHoodTerrain_RegisterCheatAndMessages, 0x4afcd0)
{
    // The service is always present: with none, the notification walk calls through a null pointer in the original too
    // (ORIGINAL-BUG noted at KeyMapSlot5Walker). Without the cheat manager the new cheat is leaked by both.
    for (bool manager : {true, false}) {
        gHaveCheatMgr = manager;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            if (ours) t->RegisterCheatAndMessages(); else c.Original<void(__thiscall *)(void *)>(0x4afcd0)(t);
            return 0;
        }, "RegisterCheatAndMessages");
    }
}

TERRAIN_TEST(NHoodTerrain_Initialize, 0x4afc30)
{
    const uint32_t grids[][2] = {{128, 128}, {6, 3}, {0, 0}, {1, 9}};
    for (auto &grid : grids) {
        gGridW = grid[0]; gGridH = grid[1];
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            return TCall<uint8_t(__thiscall *)(void *)>(c, t, 1, 0x4afc30, ADAPTER(Initialize));
        }, "Initialize");
    }
    gGridW = gGridH = 128;
}

TERRAIN_TEST(NHoodTerrain_Shutdown, 0x4b2640)
{
    Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
        return TCall<uint8_t(__thiscall *)(void *)>(c, t, 1, 0x4b2640, ADAPTER(Shutdown));
    }, "Shutdown");
}

TERRAIN_TEST(NHoodTerrain_SetHeightAt, 0x4b2540)
{
    const uint32_t coords[] = {0, 1, 3, 4, 5, 8, 9};
    for (uint32_t x : coords) for (uint32_t y : coords) for (int mode = 0; mode < 3; ++mode) for (bool notify : {false, true}) {
        gA = x; gB = y; gSame = mode == 0; gF = mode == 1 ? 123.5f : -9.25f; gNotify = notify;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            float value = gF;
            if (gSame && t->mHeights && gA < t->mVertsX && gB < t->mVertsY) value = t->mHeights->mData[t->mHeights->mWidth * gB + gA];
            TCall<void(__thiscall *)(void *, uint32_t, uint32_t, float, uint8_t)>(c, t, 1, 0x4b2540, ADAPTER(SetHeightAt), gA, gB, value, gNotify);
            return 0;
        }, "SetHeightAt", true);
    }
}

TERRAIN_TEST(NHoodTerrain_PostTerrainMessage, 0x4b04f0)
{
    for (uint32_t type : {0x6c09445cu, 0u, 0xffffffffu, 0xac3a02d9u}) for (uint32_t v : {0u, 5u, 0x80000000u}) {
        gA = type; gB = v;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *) -> uint32_t {
            if (ours) cTSNHoodTerrain::PostTerrainMessage(gA, gB, gB + 1, gB ^ 0xff);
            else c.Original<void(__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t)>(0x4b04f0)(gA, gB, gB + 1, gB ^ 0xff);
            return 0;
        }, "PostTerrainMessage");
    }
}

TERRAIN_TEST(NHoodTerrain_SendTerrainMessage, 0x4b1ef0)
{
    for (uint32_t type : {0x6c09445cu, 0u, 0xffffffffu, 0xac3a02d9u}) for (uint32_t v : {0u, 5u, 0x80000000u}) {
        gA = type; gB = v;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *) -> uint32_t {
            if (ours) cTSNHoodTerrain::SendTerrainMessage(gA, gB, gB + 1, gB ^ 0xff);
            else c.Original<void(__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t)>(0x4b1ef0)(gA, gB, gB + 1, gB ^ 0xff);
            return 0;
        }, "SendTerrainMessage");
    }
}

// ---- lane FO: the queries over paint, flags and the static type table, UpdateNormals, SetTerrainType ----

namespace {
uint32_t gRect[4];

// Small layer types (0..4, so lists end early and types repeat) over the paint, from the terrain's own random key. False when
// the paint vector is not one per vertex (MakeTerrain makes some with 3 entries): the slots then read past it.
bool SmallPaint(cTSNHoodTerrain *t)
{
    if (t->mPaint.size() != size_t(t->mVertsX) * t->mVertsY) return false;
    uint32_t mix = t->mKey[0];
    for (auto &paint : t->mPaint)
        for (auto &layer : paint.mLayers) {
            mix = mix * 1103515245u + 12345u;
            layer.mType = uint8_t((mix >> 16) % 5);
            layer.mWeight = uint16_t(mix >> 8);
        }
    return true;
}

uint32_t FoldBytes(uint32_t acc, const TerrainTypeIdVector &v)
{
    acc = Fold(Fold(acc, uint32_t(v.size())), uint32_t(v.capacity()));
    for (uint8_t b : v) acc = Fold(acc, b);
    return acc;
}

TerrainTypeIdVector Prefilled(uint32_t n)
{
    TerrainTypeIdVector v;
    for (uint32_t i = 0; i < n; ++i) v.push_back(uint8_t(9 + i));
    return v;
}

cRZString *NewString(const char *text)
{
    auto *s = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    s->mVTable = GameAddress<void>(cRZString::kVTable);
    new (&s->mString) std::string(text);
    s->mRefCount = 1;
    return s;
}
void FreeString(cRZString *s)
{
    s->mString.~basic_string();
    ::operator delete(s, sizeof(cRZString));
}
} // namespace

TERRAIN_TEST(NHoodTerrain_GetHeightRange, 0x4af650)
{
    // Inside, the corners, one past each edge, upside down in y and in x.
    const uint32_t rects[][4] = {{0, 0, 0, 0}, {0, 0, 1, 1}, {1, 0, 3, 2}, {0, 0, 4, 4}, {0, 0, 5, 5}, {2, 3, 2, 3}, {0, 2, 0, 1},
        {3, 0, 1, 2}, {0, 0, 9, 0}, {9, 0, 0, 0}, {0, 9, 0, 9}, {1, 1, 8, 8}, {0, 0, 2, 9}};
    for (auto &rect : rects) {
        std::memcpy(gRect, rect, sizeof(gRect));
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            float lo = -7.0f, hi = -7.0f;
            TCall<void(__thiscall *)(void *, const uint32_t *, float *, float *)>(c, t, 1, 0x4af650, ADAPTER(GetHeightRange), gRect, &lo, &hi);
            return FoldF(FoldF(0, lo), hi);
        }, "GetHeightRange", true);
    }
}

TERRAIN_TEST(NHoodTerrain_UpdateNormals, 0x4b29c0)
{
    // RecomputeNormals (0x4ade70, the original on both sides) writes the face-row buffers (2 * width each) and the normals;
    // the result word folds every normal (Capture keeps only the normals' sizes).
    for (bool force : {false, true}) for (int valid : {0, 1, 0x80}) {
        gA = force; gB = uint32_t(valid);
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            t->mFaceRowA.resize(t->mWidth * 2);
            t->mFaceRowB.resize(t->mWidth * 2);
            t->mNormalsValid = uint8_t(gB);
            TCall<void(__thiscall *)(void *, uint32_t)>(c, t, 1, 0x4b29c0, ADAPTER(UpdateNormals), gA);
            uint32_t acc = 0;
            for (const auto &n : t->mNormals) acc = FoldF(FoldF(FoldF(acc, n.x), n.y), n.z);
            return acc;
        }, "UpdateNormals", true);
    }
}

TERRAIN_TEST(NHoodTerrain_SetTerrainType, 0x4b24b0)
{
    // The current type (null or one of three names), the entry list (0..3 entries from the seed), names that match the current
    // type, an entry (in another case) or nothing.
    for (const char *name : {"Temperate", "TEMPERATE", "desert", "Ice", "", "Nothing", "a name longer than the small string buffer holds"}) {
        gA = Hash(name);
        static const char *sName;
        sName = name;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            cRZString *s = NewString(sName);
            const uint8_t r = TCall<uint8_t(__thiscall *)(void *, cIGZString *)>(c, t, 1, 0x4b24b0, ADAPTER(SetTerrainType), s->AsInterface());
            const uint32_t result = Fold(Fold(r, Hash(s->mString)), s->mRefCount);
            FreeString(s);
            return result;
        }, "SetTerrainType");
    }
}

TERRAIN_TEST(NHoodTerrain_SetVertexFlag, 0x4b25a0)
{
    // Every vertex, each layer (2 counts as layer 1), set and clear, sent or posted: a change is announced, no change is not.
    for (uint32_t layer : {0u, 1u, 2u}) for (bool set : {false, true}) for (int send : {0, 1, 0x40}) {
        gA = layer; gSame = set; gC = uint32_t(send);
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            for (uint32_t y = 0; y < t->mVertsY; ++y) for (uint32_t x = 0; x < t->mVertsX; ++x) {
                const uint32_t vertex[3] = {x, y, gA};
                TCall<void(__thiscall *)(void *, const uint32_t *, uint32_t, uint32_t)>(c, t, 1, 0x4b25a0, ADAPTER(SetVertexFlag), vertex, gSame, gC);
            }
            return 0;
        }, "SetVertexFlag", false, true);
    }
}

TERRAIN_TEST(NHoodTerrain_GetVertexTerrainTypes, 0x4af870)
{
    for (uint32_t pre : {0u, 3u}) {
        gA = pre;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            if (!SmallPaint(t)) return 0x5e;
            uint32_t acc = 0;
            for (uint32_t y = 0; y < t->mVertsY; ++y) for (uint32_t x = 0; x < t->mVertsX; ++x) {
                TerrainTypeIdVector out = Prefilled(gA);
                TCall<void(__thiscall *)(void *, uint32_t, uint32_t, void *)>(c, t, 1, 0x4af870, ADAPTER(GetVertexTerrainTypes), x, y, &out);
                acc = FoldBytes(acc, out);
            }
            return acc;
        }, "GetVertexTerrainTypes");
    }
}

TERRAIN_TEST(NHoodTerrain_GetRectTerrainTypes, 0x4af8f0)
{
    // Rectangles inside the vertex grid of every shape (no bounds check), single vertices, empty ones (x or y upside down).
    const uint32_t rects[][4] = {{0, 0, 0, 0}, {0, 0, 1, 1}, {1, 0, 2, 1}, {0, 1, 1, 1}, {1, 1, 0, 1}, {0, 1, 1, 0}, {0, 0, 2, 2}};
    for (auto &rect : rects) for (uint32_t pre : {0u, 2u}) {
        std::memcpy(gRect, rect, sizeof(gRect));
        gA = pre;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            if (!SmallPaint(t) || gRect[2] >= t->mVertsX || gRect[3] >= t->mVertsY) return 0x5e;
            TerrainTypeIdVector out = Prefilled(gA);
            TCall<void(__thiscall *)(void *, const uint32_t *, void *)>(c, t, 1, 0x4af8f0, ADAPTER(GetRectTerrainTypes), gRect, &out);
            return FoldBytes(0, out);
        }, "GetRectTerrainTypes");
    }
}

TERRAIN_TEST(NHoodTerrain_GetVertexTypeWeight, 0x4af7b0)
{
    Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
        if (!SmallPaint(t)) return 0x5e;
        uint32_t acc = 0;
        for (uint32_t type : {0u, 1u, 2u, 3u, 4u, 5u, 0x101u}) for (uint32_t y = 0; y <= t->mVertsY; ++y) for (uint32_t x = 0; x <= t->mVertsX; ++x)
            acc = Fold(acc, TCall<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(c, t, 1, 0x4af7b0, ADAPTER(GetVertexTypeWeight), type, x, y));
        return acc;
    }, "GetVertexTypeWeight");
}

// Slots 29-33 over every id 0..17 (the table holds 1..3 of 1..12), and the id list.
TERRAIN_TEST(NHoodTerrain_TypeTable, 0x4af760)
{
    Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
        uint32_t acc = 0;
        auto *table = GameAddress<StdHashTable>(0x10dab00);
        for (uint32_t id = 0; id < 18; ++id) {
            const uint32_t arg = id | 0xabcd00u; // the original reads only the byte
            uint8_t *map = TCall<uint8_t *(__thiscall *)(void *, uint32_t)>(c, t, 1, 0x4af760, ADAPTER(GetTypeWeightMap), arg);
            uint32_t where = 0;
            for (auto *node = table->mHead->next; node != table->mHead; node = node->next, ++where)
                if (map && map == reinterpret_cast<cTSNHoodTerrainTypeNode *>(node)->mWeightMap.mData.data()) break;
            acc = Fold(Fold(acc, uint32_t(map != nullptr)), where);
            acc = Fold(acc, TCall<uint8_t(__thiscall *)(void *, uint32_t)>(c, t, 1, 0x4b04a0, ADAPTER(GetTypeFlag), arg));
            for (int alt = 0; alt < 2; ++alt) {
                cRZString *s = NewString("untouched");
                uint8_t r;
                if (alt) r = TCall<uint8_t(__thiscall *)(void *, uint32_t, cIGZString *)>(c, t, 1, 0x4af5f0, ADAPTER(GetTypeAltName), arg, s->AsInterface());
                else r = TCall<uint8_t(__thiscall *)(void *, uint32_t, cIGZString *)>(c, t, 1, 0x4af810, ADAPTER(GetTypeName), arg, s->AsInterface());
                acc = Fold(Fold(acc, r), Hash(s->mString));
                FreeString(s);
            }
        }
        for (uint32_t pre : {0u, 5u, 40u}) {
            TerrainTypeIdVector out = Prefilled(pre);
            TCall<void(__thiscall *)(void *, void *)>(c, t, 1, 0x4af580, ADAPTER(GetTerrainTypeIds), &out);
            acc = FoldBytes(acc, out);
        }
        return acc;
    }, "TypeTable");
}

// ---- lane FO: edits and derived maps ----

namespace {
uint32_t FoldHeights(uint32_t acc, const cTSNHoodTerrain *t)
{
    if (t->mHeights) for (float h : t->mHeights->mData) acc = FoldF(acc, h);
    return acc;
}

// Import fakes (slot 36): the source and its heights object.
struct ImportFake { void **vt; };
void *gSourceVT[32]{}, *gSamplesVT[32]{};
ImportFake gSource{gSourceVT}, gSamples{gSamplesVT};
uint32_t gLastX, gLastY, gRotation, gFill, gExtra;
int32_t gCornerX, gCornerY;
void *__fastcall SourceSamples(ImportFake *, void *) { Ev(0x404); return &gSamples; }
void __fastcall SourceCorner(ImportFake *, void *, int32_t *x, int32_t *y) { Ev(0x40a); *x = gCornerX; *y = gCornerY; }
uint32_t __fastcall SourceRotation(ImportFake *, void *) { Ev(0x417); return 0xabcd00u | gRotation; }
float __fastcall SourceBase(ImportFake *, void *) { Ev(0x410); return 2.5f; }
uint8_t __fastcall SamplesFill(ImportFake *, void *, TerrainHeightVector *out)
{
    Ev(0x510, uint32_t(out->size()));
    for (uint32_t i = 0; i < (gLastX + 1) * (gLastY + 1) + gExtra; ++i) out->push_back(float(i) * 0.75f - 3.0f);
    return uint8_t(gFill);
}
void __fastcall SamplesSize(ImportFake *, void *, uint32_t *x, uint32_t *y) { Ev(0x505); *x = gLastX; *y = gLastY; }

// DoMessage fakes: a message answering the standard-message id with a fake standard message.
struct MessageFake { void **vt; };
void *gMessageVT[8]{}, *gStandardVT[16]{};
MessageFake gMessage{gMessageVT}, gStandard{gStandardVT};
uint32_t gMessageType, gAnswer;
uint8_t __fastcall MessageQuery(MessageFake *, void *, uint32_t iid, void **out)
{
    Ev(0x600, iid);
    if (gAnswer) *out = &gStandard;
    return uint8_t(gAnswer);
}
uint32_t __fastcall StandardRelease(MessageFake *, void *) { Ev(0x602); return 0; }
uint32_t __fastcall StandardType(MessageFake *, void *) { Ev(0x604); return gMessageType; }
uint32_t __fastcall StandardData1(MessageFake *, void *) { Ev(0x609); return 0x1234; }
void *__fastcall NbhdGetLot(Fake *, void *, uint32_t id) { Ev(57, id); return nullptr; }
void *__fastcall AppLotQuery(Fake *, void *) { Ev(12); return nullptr; }

void SetupEditFakes()
{
    gSourceVT[4] = reinterpret_cast<void *>(&SourceSamples);
    gSourceVT[10] = reinterpret_cast<void *>(&SourceCorner);
    gSourceVT[23] = reinterpret_cast<void *>(&SourceRotation);
    gSourceVT[16] = reinterpret_cast<void *>(&SourceBase);
    gSamplesVT[16] = reinterpret_cast<void *>(&SamplesFill);
    gSamplesVT[5] = reinterpret_cast<void *>(&SamplesSize);
    gMessageVT[0] = reinterpret_cast<void *>(&MessageQuery);
    gStandardVT[2] = reinterpret_cast<void *>(&StandardRelease);
    gStandardVT[4] = reinterpret_cast<void *>(&StandardType);
    gStandardVT[9] = reinterpret_cast<void *>(&StandardData1);
    gNbhdVT[57] = reinterpret_cast<void *>(&NbhdGetLot);
    gAppVT[12] = reinterpret_cast<void *>(&AppLotQuery);
}
} // namespace

TERRAIN_TEST(NHoodTerrain_ResampleGrid, 0x4b1fb0)
{
    // Steps that divide both cell counts (the original reads past the row otherwise: GetHeightAt has no bounds check).
    for (uint32_t step : {1u, 2u, 4u}) {
        gA = step;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            if (t->mWidth % gA || t->mHeight % gA) return 0x5e;
            TCall<void(__thiscall *)(void *, uint32_t)>(c, t, 1, 0x4b1fb0, ADAPTER(ResampleGrid), gA);
            return 0;
        }, "ResampleGrid", true);
    }
}

TERRAIN_TEST(NHoodTerrain_GetInterpolatedHeight, 0x4af9e0)
{
    Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
        if (t->mWidth == 0 || t->mHeight == 0) return 0x5e; // clamps to cell -1: reads before the array
        uint32_t acc = 0;
        std::vector<float> xs{-1.0f, -0.0f, 0.0f, t->mWorldSizeX, t->mWorldSizeX + 0.25f, t->mCellSize, t->mCellSize * 0.5f,
            t->mCellSize * 2.0f - 0.001f};
        for (int i = 1; i < 9; ++i) xs.push_back(t->mWorldSizeX * float(i) / 9.0f);
        std::vector<float> zs = xs;
        for (int i = 1; i < 6; ++i) zs.push_back(t->mWorldSizeY * float(i) / 6.0f + 0.03f);
        for (float x : xs) for (float z : zs)
            acc = FoldF(acc, TCall<float(__thiscall *)(void *, float, float)>(c, t, 1, 0x4af9e0, ADAPTER(GetInterpolatedHeight), x, z));
        return acc;
    }, "GetInterpolatedHeight", true);
}

TERRAIN_TEST(NHoodTerrain_RebuildDerivedMaps, 0x4b2a00)
{
    // Square grids only (the shore map is indexed [x][y]). The lot pass 0x4ae410 finds no lot service (the app's slot 12 answers
    // null); the paint pass 0x4ae7f0 reads a zeroed type entry. Both are the original on both sides; the normals, shore rows,
    // paint weights and the types' weight maps (sized to the grid here, as SetSize does) are compared.
    SetupEditFakes();
    // Water: none, everywhere, a mix, and only the lowest vertex (the shore distance then spreads over the whole grid, so the
    // spread's step count matters).
    for (int water : {0, 1, 2, 3}) {
        gA = uint32_t(water);
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            if (t->mWidth != t->mHeight || !SmallPaint(t) || !gEntries) return 0x5e;
            t->mWaterLevel = gA == 0 ? -1.0e6f : gA == 1 ? 1.0e6f : 10.0f;
            if (gA == 3) t->mWaterLevel = *std::min_element(t->mHeights->mData.begin(), t->mHeights->mData.end()) + 0.0625f;
            t->mFaceRowA.resize(t->mWidth * 2);
            t->mFaceRowB.resize(t->mWidth * 2);
            t->mLotRows.assign(t->mVertsX, std::vector<uint8_t>(t->mVertsY, 3));
            std::memset(gEntries, 0, 0x828 * 4);
            t->mTerrainType = gEntries;
            for (auto &paint : t->mPaint) for (auto &layer : paint.mLayers) layer.mWeight |= 1;
            auto *head = *GameAddress<cTSNHoodTerrainTypeNode *>(0x10dab04);
            for (auto *node = head->mNext; node != head; node = node->mNext) {
                node->mWeightMap.mWidth = t->mVertsX;
                node->mWeightMap.mHeight = t->mVertsY;
                node->mWeightMap.mData.assign(t->mVertsX * t->mVertsY, 0x77);
            }
            TCall<void(__thiscall *)(void *)>(c, t, 1, 0x4b2a00, ADAPTER(RebuildDerivedMaps));
            uint32_t acc = 0;
            for (const auto &n : t->mNormals) acc = FoldF(FoldF(FoldF(acc, n.x), n.y), n.z);
            return acc;
        }, "RebuildDerivedMaps", true);
    }
}

TERRAIN_TEST(NHoodTerrain_ImportHeights, 0x4b02a0)
{
    SetupEditFakes();
    struct Case { uint32_t lastX, lastY, rotation, fill, extra; int32_t x, y; bool source; };
    const Case cases[] = {{1, 1, 0, 1, 0, 0, 0, true}, {2, 1, 1, 1, 0, 1, 0, true}, {1, 2, 2, 1, 0, 0, 1, true}, {2, 2, 3, 1, 0, 1, 1, true},
        {1, 2, 4, 1, 0, 0, 0, true}, {3, 0, 0x7f, 1, 0, 0, 2, true}, {1, 1, 0, 0, 0, 0, 0, true}, {1, 1, 0, 1, 1, 0, 0, true},
        {0, 0, 1, 1, 0, 3, 3, true}, {1, 1, 0, 1, 0, 0, 0, false}, {2, 3, 2, 0x80, 0, 2, 0, true}, {3, 3, 1, 1, 0, -1, 2, true},
        {3, 1, 3, 1, 0, 0, 1, true}, {1, 3, 3, 1, 0, 1, 0, true}, {3, 1, 1, 1, 0, 0, 0, true}}; // rotations 1 and 3 of non-square blocks
    for (const Case &k : cases) {
        gLastX = k.lastX; gLastY = k.lastY; gRotation = k.rotation; gFill = k.fill; gExtra = k.extra; gCornerX = k.x; gCornerY = k.y;
        gSame = k.source;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            void *source = gSame ? &gSource : nullptr;
            TCall<void(__thiscall *)(void *, void *)>(c, t, 1, 0x4b02a0, ADAPTER(ImportHeights), source);
            return 0;
        }, "ImportHeights", true);
    }
}

TERRAIN_TEST(NHoodTerrain_RectEdits, 0x4b0620)
{
    // RaiseRect (slot 41) and SetRectHeight (slot 42) over rectangles inside the vertex grid of the smallest shapes, empty ones
    // included; the rectangle is invalidated through the primary vtable (the original InvalidateRect on both sides).
    const uint32_t rects[][4] = {{0, 0, 0, 0}, {0, 0, 1, 1}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 1, 0}, {1, 1, 1, 1}};
    for (auto &rect : rects) for (int which : {0, 1}) for (float v : {0.0f, 3.25f, -40.0f}) {
        std::memcpy(gRect, rect, sizeof(gRect));
        gA = uint32_t(which); gF = v;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            if (gA == 0) {
                TCall<void(__thiscall *)(void *, const uint32_t *, float)>(c, t, 1, 0x4b0620, ADAPTER(RaiseRect), gRect, gF);
            } else {
                TCall<void(__thiscall *)(void *, const uint32_t *, float)>(c, t, 1, 0x4b0150, ADAPTER(SetRectHeight), gRect, gF);
            }
            return 0;
        }, "RectEdits", true);
    }
}

TERRAIN_TEST(NHoodTerrain_InvalidateRect, 0x4b2c00)
{
    const uint32_t rects[][4] = {{0, 0, 0, 0}, {1, 1, 1, 1}, {1, 1, 2, 3}, {2, 1, 1, 2}, {0, 2, 3, 3}, {3, 0, 3, 2}, {1, 2, 1, 1}};
    for (auto &rect : rects) {
        std::memcpy(gRect, rect, sizeof(gRect));
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            TCall<void(__thiscall *)(void *, const uint32_t *)>(c, t, 0, 0x4b2c00, ADAPTER(InvalidateRect), gRect);
            return 0;
        }, "InvalidateRect");
    }
}

TERRAIN_TEST(NHoodTerrain_DoMessage, 0x4aecc0)
{
    // A null message, a message that is not a standard one, and standard ones of the two lot types and others. A lot message
    // reaches FlattenUnderLot (the original on both sides), which asks the neighbourhood for the lot (data 1): none.
    SetupEditFakes();
    for (int which = 0; which < 6; ++which) {
        gA = uint32_t(which);
        gAnswer = which != 1;
        gMessageType = which == 2 ? 0xafu : which == 3 ? 0xb1u : which == 4 ? 0xb0u : 0x1afu;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            void *message = gA == 0 ? nullptr : &gMessage;
            return TCall<uint8_t(__thiscall *)(void *, void *)>(c, t, 3, 0x4aecc0, ADAPTER(DoMessage), message);
        }, "DoMessage");
    }
}

TERRAIN_TEST(NHoodTerrain_GetRectTypeWeights, 0x4af2e0)
{
    // Types 0..4 in the paint; the largest type id 9 (all counted) or 2 (types 3 and 4 skipped); 0..7 places asked for.
    const uint32_t rects[][4] = {{0, 0, 0, 0}, {0, 0, 1, 1}, {1, 0, 2, 1}, {1, 1, 0, 1}, {0, 0, 2, 2}, {2, 2, 2, 2}};
    // With `ties`, every vertex holds types 3, 1, 2 at weight 5 each: equal totals keep the lower type id first.
    for (auto &rect : rects) for (uint32_t maxId : {9u, 2u}) for (uint32_t count : {0u, 1u, 2u, 3u, 7u}) for (uint32_t ties : {0u, 1u}) {
        std::memcpy(gRect, rect, sizeof(gRect));
        gB = maxId; gC = count; gA = ties;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            if (!SmallPaint(t) || gRect[2] >= t->mVertsX || gRect[3] >= t->mVertsY) return 0x5e;
            if (gA) for (auto &paint : t->mPaint) for (int i = 0; i < 3; ++i) paint.mLayers[i] = {uint8_t(i == 0 ? 3 : i), 0, 5};
            *GameAddress<uint8_t>(0x10daad5) = uint8_t(gB);
            TerrainTypeIdVector out = Prefilled(2);
            TCall<void(__thiscall *)(void *, const uint32_t *, void *, uint32_t)>(c, t, 1, 0x4af2e0, ADAPTER(GetRectTypeWeights), gRect, &out, gC);
            *GameAddress<uint8_t>(0x10daad5) = 9;
            return FoldBytes(0, out);
        }, "GetRectTypeWeights");
    }
}

namespace {
// The lot services the lot-marking pass 0x4ae5c0 asks (SlopeRect with lockLots): a lot query (the app's slot 12) whose slot 10
// answers for some vertices, and the neighbourhood's slot 59 (no lot otherwise).
void *gLotVT[16]{};
Fake gLots{gLotVT, 18};
bool gLotsOn;
uint8_t __fastcall LotsCover(Fake *, void *, const int32_t *rect, uint32_t a, uint32_t b)
{
    Ev(10, uint32_t(rect[0]) | uint32_t(rect[1]) << 8 | uint32_t(rect[2]) << 16 | uint32_t(rect[3]) << 24, a, b);
    return uint8_t((rect[0] * 3 + rect[1]) % 4 == 0);
}
uint32_t __fastcall NbhdLotAt(Fake *, void *, const int32_t *) { Ev(59); return 0; }
void *__fastcall AppLots(Fake *, void *) { Ev(12); return gLotsOn ? &gLots : nullptr; }
} // namespace

TERRAIN_TEST(NHoodTerrain_SlopeRect, 0x4b2700)
{
    SetupEditFakes();
    gLotVT[10] = reinterpret_cast<void *>(&LotsCover);
    gNbhdVT[59] = reinterpret_cast<void *>(&NbhdLotAt);
    gAppVT[12] = reinterpret_cast<void *>(&AppLots);
    const int32_t rects[][4] = {{0, 0, 0, 0}, {0, 0, 1, 1}, {0, 0, 2, 2}, {1, 0, 3, 2}, {2, 1, 1, 2}, {0, 0, 4, 4}};
    struct Case { float angle; int32_t passes; float strength; uint32_t lock; };
    const Case cases[] = {{30.0f, 1, 0.5f, 0}, {0.0f, 2, 1.0f, 0}, {45.0f, 3, 0.25f, 1}, {89.0f, 1, 0.5f, 0}, {10.0f, 0, 0.5f, 0},
        {20.0f, -1, 0.5f, 0}, {5.0f, 2, 0.75f, 0x100}, {60.0f, 1, 2.0f, 1}};
    for (auto &rect : rects) for (const Case &k : cases) {
        static int32_t sRect[4];
        static Case sCase;
        std::memcpy(sRect, rect, sizeof(sRect));
        sCase = k;
        gLotsOn = k.lock != 0;
        Each(ctx, [](difftest::Context &c, bool ours, cTSNHoodTerrain *t) -> uint32_t {
            if (sRect[2] >= int32_t(t->mVertsX) || sRect[3] >= int32_t(t->mVertsY)) return 0x5e;
            TCall<void(__thiscall *)(void *, const int32_t *, float, int32_t, float, uint32_t)>(c, t, 1, 0x4b2700, ADAPTER(SlopeRect),
                sRect, sCase.angle, sCase.passes, sCase.strength, sCase.lock);
            return 0;
        }, "SlopeRect", true);
    }
    gLotsOn = false;
    gAppVT[12] = reinterpret_cast<void *>(&AppLotQuery);
}
