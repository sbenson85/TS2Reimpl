// Differential test for nTSWorld::cWorldDB (lane JC, src/world/cWorldDB.h): the class in the standalone shape.
//
// WorldDB_Run: a database built by the original constructor (0xd0b6c0, game vtables 0xebb4b0 / 0xebb504 / 0xebb520 /
// 0xebb6b8) against one built by ours (the constructor hook's adapter), each driven through its own vtables by the same
// random sequence of operations, then destroyed by the last Release (the deleting destructor 0xd0c030 behind the +4 thunk
// 0xd0bd57, the destructor 0xd0b960). Every slot of every face is called through its slot number, so a slot-order mistake
// calls the wrong function. Operations: Init (0xd0e6b0), Shutdown (0xd133c0), DoMessage (0xd0db70), QueryInterface
// (0xd11360; thunks 0xd113b9 / 0xd113c1 / 0xd113c9) / AddRef / Release on every face, the service accessors, Write
// (0xd143e0), Read (0xd11ce0), SetBounds (0xd13310) with ResizeGrids (0xd11e80), SetLevels (0xd13180) with 0xd13230, Save
// (0xd12360), Reset (0xd138b0), slot 12 (0xd0eea0), the floor patterns (0xd12c70, 0xd12630, 0xd0ef50, 0xd12df0, 0xd127b0,
// 0xd138d0, 0xd0df10, 0xd0e1d0, primary 0xd12f50 / 0xd12910 / 0xd0e130 / 0xd0d9c0 / 0xd12e70), rotations (0xd12e80, 0xd12830,
// 0xd0df30, 0xd0d8f0), elevations (0xd12a50, 0xd0ef30, 0xd12bd0, 0xd12c50, 0xd11150), visibility (0xd12fe0, 0xd0e200), the
// objects on tiles (0xd0d3f0 / 0xd0d400, 0xd0d320 / 0xd0d330, 0xd120c0, 0xd0e2d0, 0xd12000, 0xd0d3d0, 0xd120a0, 0xd10fa0,
// 0xd0e620, 0xd110a0, 0xd11120), the byte / deck / awning grids (0xd13280 / 0xd13290 / 0xd13300, 0xd0f7a0, 0xd129a0,
// 0xd0da60, 0xd12530, 0xd125b0, 0xd0d7a0, 0xd0d820), the floor queries (0xd0e4f0 behind 0xd0e4d0, 0xd0e420, 0xd0e5f0,
// 0xd0e440, 0xd0e470, 0xd0e3c0; 0xd0fae0, 0xd0fa60), names (0xd13350, 0xd10d40, 0xd10f50, 0xd0db30, 0xd0dee0, 0xd0d8c0,
// 0xd0e000, 0xd0f630), heights (0xd0de50, 0xd13ac0, 0xd13700), bounds tests (0xd0f7f0, 0xd0f8f0, 0xd0f850, 0xd0f9a0,
// 0xd0f690, 0xd0f700), the getters (0xd10ef0, 0xd10f20, 0xd10e40, 0xd10e80, 0xd10d80, 0xd10db0, 0xd11040, 0xd11070,
// 0xd10f70, 0xd10de0, 0xd10ed0, 0xd10e00, 0xd10ec0, 0xd0e2c0, 0xd0db20, 0xd13160, 0xd10d40 and the folded one-liners), and
// the forwarders of the slots still game code (Load 0xd0fb30, Create 0xd0ef80).
//
// Every object the database reaches is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject: the
// message registry, the settings service), the director's CreateInstance (terrain, roofs, pools, curved pools, partitions,
// outside support, proxy manager), the app singleton, the persist system, the message server (0x9c5a89 stood in for: posted
// and sent messages logged by content, with the change record they carry), streams, strings. Recorders stand in for the
// original callees nobody has replaced (0xd15080, 0xd17090, 0xd0d040, 0x81e800, 0x81ea20, 0x801530, 0xd0fb30, 0xd0ef80);
// the pattern table (0x813b30), cRZString and the tile of a point (0x7dd9d0) are the game's own code on both sides; the
// object index is the game's cVertexLayer (0xd146c0, its Add / Remove / Contains / ValueOf) on the original side and ours
// (world/cVertexLayer.h) on ours, its maps compared as trees;
// the grids are the game's on the original side and ours (world/c3DArray.h, tested in tests/world_c3darray.cpp) on ours, their
// vtable words compared as "a known grid vtable". Compared after every
// operation: the result (the low byte where the original returns a bool in al, floats by their bits), the event log, every
// field (the grids element by element with their sizes and capacities, the pattern table's maps, the object cells, the name)
// and every fake's count. The original has unchecked grid accesses (the getters of patterns, rotations, bytes, deck markers,
// FindObject, the heights' clamp against absolute bounds); the test calls those only with tiles whose cell the original's
// indexing keeps inside the grid, and keeps the origin at -1 or 0 (AddObjectRects lists cells relative to the origin and
// checks them against the absolute bounds).
#include "rz/RZMessageServer.h"
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cIGZPropertySet.h"
#include "rz/cRZString.h"
#include "world/cWorldDB.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace worlddb_test {

using namespace nTSWorld;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cWorldDB;

constexpr uint32_t kTag = 0x7d000000; // the database
constexpr uint32_t kGridTag = 0x70000000; // grid i: kGridTag + (i << 20)

enum : unsigned {
    kService = 0, kRegistry, kSettings, kTerrain, kRoofs, kPools, kCurved, kPartitions, kOutside, kProxies, kPersist,
    kServer, kApp, kHolder, kLotLink, kLot, kCatalog, kEntry, kMessage, kStd, kStream, kName, kOut, kWriter,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); }

struct World {
    bool settings;   // the settings service answers
    bool proxies;    // the director makes a proxy manager
    bool entry;      // the catalog has a default floor
    float values[4]; // the setting values handed to DoMessage
};
World gWorld;

uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}

void LogText(const char *text, size_t n)
{
    Ev(uint32_t(n));
    for (size_t i = 0; i < n; ++i) Ev(uint8_t(text[i]));
}
void LogRZString(uint32_t at)
{
    const auto *s = reinterpret_cast<const cRZString *>(uintptr_t(at));
    LogText(s->mString.data(), s->mString.size());
}

// ---- the pattern names (static: the table keeps the pointers) ----
const char *const kPatternNames[] = {"Floor_Wood", "floor_wood", "Tile_Blue", "Carpet", "A Long Pattern Name, Over Fifteen", "x"};
constexpr unsigned kPatternCount = sizeof(kPatternNames) / sizeof(kPatternNames[0]);
const char *PatternName(uint32_t k) { return k >= kPatternCount ? nullptr : kPatternNames[k]; }

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void *__cdecl StandServer() { return &F(kServer); }

// Record's arguments by the stood-in function, as logged (svcfx::detail::DumpKinds letters, one per argument).
constexpr const char *RecordKinds(uint32_t rva)
{
    switch (rva) {
    case 0x81e800: return "pd"; // the object, a word
    case 0xd0d040: return "dd";
    case 0xd0ef80: return "ddd";
    case 0xd0fb30: return "dd";
    default: return "";
    }
}
template <uint32_t Rva, unsigned... I> uint32_t __fastcall Record(void *self, void *, shape::StackWord<I>... w)
{
    const uint32_t words[] = {0u, w...};
    Ev(0xf0000000u | Rva);
    Ev(svcfx::Norm(P(self)));
    svcfx::detail::DumpKinds(svcfx::gEv, sizeof...(I), RecordKinds(Rva), [&](size_t i) { return words[i + 1]; }, [](size_t i) {
        char text[80];
        std::snprintf(text, sizeof(text), "Record %#x argument %u", Rva, unsigned(i));
        return std::string(text);
    });
    if constexpr (Rva == 0x81e800) return P(self);
    else return gRng();
}
template <uint32_t Rva, unsigned... I> const void *RecordAt(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<const void *>(&Record<Rva, I...>);
}
template <uint32_t Rva, unsigned N> const void *RecordFor() { return RecordAt<Rva>(std::make_integer_sequence<unsigned, N>()); }
// The __cdecl callees: one argument (the database) or none.
void __cdecl SetUpStatics()
{
    Ev(0xf0d15080u);
}
void __cdecl RebuildDerived(uint32_t world)
{
    Ev(0xf0d17090u);
    Ev(svcfx::Norm(world));
}

// ---- fake handlers ----

// The framework's slot 5 (class, iid, out): the message registry, the settings service.
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    *reinterpret_cast<void **>(uintptr_t(a[2])) = nullptr;
    if (a[0] == 0x04fa845b) return HandOut(a[2], F(kRegistry));
    if (a[0] == 0x0ac1887e) return gWorld.settings ? HandOut(a[2], F(kSettings)) : 0;
    return 0;
}
// The director framework's slot 3 CreateInstance (class, iid, out).
uint32_t CreateInstance(Obj *, uint32_t *a)
{
    switch (a[0]) {
    case 0x4b58975b: return HandOut(a[2], F(kTerrain));
    case 0xab9406aa: return HandOut(a[2], F(kRoofs));
    case 0x0c900fdb: return HandOut(a[2], F(kPools));
    case 0x916da14d: return HandOut(a[2], F(kCurved));
    case 0x6ca2a642: return HandOut(a[2], F(kPartitions));
    case 0xb21f9b20: return HandOut(a[2], F(kOutside));
    case 0x8ce45d10: return gWorld.proxies ? HandOut(a[2], F(kProxies)) : 0;
    default: return 0;
    }
}
// The settings' watch (target, id, key, value) and unwatch (target, id, key): the key by content, the value's words.
void LogKey(uint32_t at)
{
    const auto *key = reinterpret_cast<const GZServiceString *>(uintptr_t(at));
    LogText(key->data(), key->size());
}
uint32_t Watch(Obj *, uint32_t *a)
{
    LogKey(a[2]);
    const auto *value = reinterpret_cast<const uint32_t *>(uintptr_t(a[3]));
    Ev(value[0] - P(GameAddress<void>(0)));
    Ev(value[1] & 0xff);
    return Bool();
}
uint32_t Unwatch(Obj *, uint32_t *a)
{
    LogKey(a[2]);
    return Bool();
}

// A posted or sent message: the fake incoming message by identity; a standard message by content and its change record.
void LogRecord(const uint32_t *record)
{
    const uint32_t vt = record[0] - P(GameAddress<void>(0));
    Ev(vt);
    Ev(record[1]);
    unsigned words = 0;
    switch (vt) {
    case 0xebb6cc: words = 5; break; // pattern change: name, x, y, level, segments (low byte)
    case 0xebb6dc: words = 4; break; // elevation change
    case 0xebb6ec: words = 3; break; // terrain change
    case 0xebb6fc: words = 4; break; // visibility change
    default: break;
    }
    for (unsigned i = 0; i < words; ++i) Ev(record[2 + i] & (vt == 0xebb6cc && i == 4 ? 0xffu : vt == 0xebb6fc && i == 3 ? 0xffu : ~0u));
}
void LogPosted(uint32_t message)
{
    Ev(svcfx::evMessage);
    if (!message || message == P(&F(kMessage))) {
        Ev(svcfx::Norm(message));
        return;
    }
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(message));
    Ev(words[0] - P(GameAddress<void>(0)));
    Ev(words[1]);
    Ev(words[2] & 0xff);
    Ev(words[3]);
    for (int i = 4; i < 8; ++i) Ev(words[i]);
    for (int i = 8; i < 11; ++i) {
        Ev(words[i] != 0);
        if (words[i]) LogRecord(reinterpret_cast<const uint32_t *>(uintptr_t(words[i])));
    }
}
uint32_t __fastcall ServerPost(Obj *self, void *, uint32_t message)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(3);
    LogPosted(message);
    return Bool();
}
uint32_t __fastcall ServerSend(Obj *self, void *, uint32_t message, uint32_t flag)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(4);
    LogPosted(message);
    Ev(flag);
    return Bool();
}

// The incoming message's QueryInterface: the standard message face, or nothing.
uint32_t MessageQuery(Obj *, uint32_t *a)
{
    if (a[0] != 0x65297976 || Draw(6) == 0) return 0;
    return HandOut(a[1], F(kStd));
}
uint32_t gSettingValue[2];
uint32_t StdValue(Obj *, uint32_t *) { return Draw(5) == 0 ? 0u : P(gSettingValue); }

// The terrain's height (slot 12, a float in st0) and its slot 13 (a bool).
float __fastcall TerrainHeight(Obj *self, void *, int32_t x, int32_t y)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(12);
    Ev(uint32_t(x));
    Ev(uint32_t(y));
    return float(x * 3 - y) * 0.25f;
}

// Streams: the out-stream logs what it is given; the in-stream hands out a scripted sequence.
std::vector<uint32_t> gReadWords;
size_t gReadAt;
uint32_t NextRead() { return gReadAt < gReadWords.size() ? gReadWords[gReadAt++] : gRng(); }
uint32_t StreamString(Obj *, uint32_t *a)
{
    LogRZString(a[0]);
    return Bool();
}
uint32_t StreamReadWord(Obj *, uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = NextRead();
    return Bool();
}
const char *const kStrings[] = {"Lot Name", "clear", "", "A Lot Name Longer Than Fifteen Characters"};
uint32_t StreamReadString(Obj *, uint32_t *a)
{
    reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString.assign(kStrings[Draw(4)]);
    return Bool();
}

// A cIGZString: slot 5 the text, slot 7 the length, slot 10 the compare, slot 4 FromChar (logged by content).
char gNameText[64];
uint32_t NameFromChar(Obj *, uint32_t *a)
{
    const char *text = reinterpret_cast<const char *>(uintptr_t(a[0]));
    LogText(text, std::strlen(text));
    return 0;
}

// ---- building the fakes ----

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 7);
    svcfx::ResetAll();
    gWorld.settings = true;
    gWorld.proxies = rng() % 4 != 0;
    gWorld.entry = rng() % 4 != 0;
    gSettingValue[0] = P(GameAddress<void>(0xd9f534));
    gSettingValue[1] = rng() % 3 == 0 ? 0x100u : rng() % 4 == 0 ? 0x80000000u | rng() % 9 : rng() % 9;

    F(kService).Set(5, "ddp", 0, &GetSystemService);
    F(kService).Set(3, "ddp", 0, &CreateInstance);
    svcfx::gFrameworkIndex = kService;
    F(kRegistry).Set(5, "pd", 0x55);
    F(kRegistry).Set(6, "pd", 0x66);
    F(kSettings).Set(4, "pdpp", 0, &Watch);
    F(kSettings).Set(5, "pdp", 0, &Unwatch);
    for (unsigned k : {kTerrain, kRoofs, kPools, kCurved, kPartitions, kOutside, kProxies}) {
        F(k).Set(3, 0, 0x33);
        F(k).Set(4, 0, 0x44);
    }
    for (unsigned k : {kTerrain, kRoofs, kPools, kCurved}) F(k).Set(5, "p", 0x55);
    F(kTerrain).Set(9, "dd", 0x99);
    F(kTerrain).table[12] = reinterpret_cast<void *>(&TerrainHeight);
    F(kTerrain).Set(13, "dd", 0, [](Obj *, uint32_t *) { return Bool(); });
    F(kPartitions).Set(5, "dddd", 0x55);
    F(kPartitions).Set(6, "dd", 0x66);
    F(kPartitions).Set(7, "p", 0x77);
    F(kPartitions).Set(13, 0, 0x1313);
    F(kProxies).Set(7, "p", 0x77);
    F(kProxies).Set(5, "p", 0x55);
    F(kPersist).Set(72, "ppd", 0, [](Obj *, uint32_t *) { return Bool(); });
    F(kServer).table[3] = reinterpret_cast<void *>(&ServerPost);
    F(kServer).table[4] = reinterpret_cast<void *>(&ServerSend);
    F(kApp).Set(6, 0, &F(kHolder));
    F(kApp).Set(16, 0, &F(kCatalog));
    F(kHolder).Set(57, "d", &F(kLotLink));
    F(kLotLink).Set(4, 0, &F(kLot));
    F(kLot).Set(24, "d", 0, [](Obj *, uint32_t *) { return Bool(); });
    F(kCatalog).Set(43, "d", gWorld.entry ? &F(kEntry) : nullptr);
    F(kEntry).Set(7, 0, kPatternNames[rng() % kPatternCount]);
    F(kMessage).Set(0, "dp", 0, &MessageQuery);
    F(kMessage).Set(5, "d", 0u);
    F(kStd).Set(4, 0, 0u);
    F(kStd).Set(13, 0, 0, [](Obj *, uint32_t *) { return Bool(); });
    F(kStd).Set(15, 0, 0, &StdValue);
    for (unsigned s : {8u, 9u}) F(kStream).Set(s, "d", 0, [](Obj *, uint32_t *) { return Bool(); });
    F(kStream).Set(15, "p", 0, &StreamString);
    F(kName).Set(5, 0, gNameText);
    F(kName).Set(7, 0, 0u);
    F(kName).Set(10, "p-", 0, [](Obj *, uint32_t *) { return Bool(); });
    F(kOut).Set(4, "p", 0, &NameFromChar);

    *GameAddress<void *>(svcfx::kAppCell) = &F(kApp);
    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(svcfx::kPersistCell) = &F(kPersist);
}

// ---- driving a database ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

void *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(cWorldDBAdapters::Construct(memory, nullptr))
                   : ctx.Original<void *(__thiscall *)(void *)>(0xd0b6c0)(memory);
    b.push_back(r == memory);
    return memory;
}

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

template <class T> const c3DArray<T> *GridAt(const uint8_t *m, uint32_t off)
{
    return reinterpret_cast<const c3DArray<T> *>(uintptr_t(Word(m, off)));
}

template <class T, class Each> void DumpGrid(Blob &b, const uint8_t *m, uint32_t off, Each each)
{
    const c3DArray<T> *g = GridAt<T>(m, off);
    b.push_back(g != nullptr);
    if (!g) return;
    // The grid's vtable word: the game's (the original's Init) and ours (our Init builds our c3DArray) are one code.
    static const uint32_t kFace[] = {0};
    void *const *vtable = *reinterpret_cast<void *const *const *>(g);
    const bool known = vtable == shape::GameVTable(c3DArrayElement<T>::kVTable) || vtable == shape::Ours<c3DArray<T>>(kFace).face[0];
    b.push_back(known ? c3DArrayElement<T>::kVTable : P(vtable));
    b.push_back(g->mRefCount);
    b.push_back(uint32_t(g->mSizeX));
    b.push_back(uint32_t(g->mSizeY));
    b.push_back(uint32_t(g->mSizeZ));
    b.push_back(uint32_t(g->mData.size()));
    b.push_back(uint32_t(g->mData.capacity()));
    for (const T &e : g->mData) each(e);
}

// A map of 0x20-byte nodes (the index's two) as a tree: size, the head's flags, the tree in preorder (colour, parent
// link, the payload's 4 words), whether the head's links name the extremes. The head's payload is never dumped (never
// written).
struct RawNode {
    RawNode *left, *parent, *right;
    uint8_t color, isnil;
    uint8_t pad[2];
    uint32_t payload[4];
};
static_assert(sizeof(RawNode) == 0x20);
void DumpTreeNode(Blob &out, const RawNode *node, const RawNode *parent)
{
    if (node->isnil) {
        out.push_back(0xeeee0000u | node->color);
        return;
    }
    out.insert(out.end(), {uint32_t(node->color), uint32_t(node->parent == parent), node->payload[0], node->payload[1],
        node->payload[2], node->payload[3]});
    DumpTreeNode(out, node->left, node);
    DumpTreeNode(out, node->right, node);
}
void DumpTreeMap(Blob &out, const void *map)
{
    const RawNode *head = *static_cast<RawNode *const *>(map);
    out.insert(out.end(), {uint32_t(static_cast<const size_t *>(map)[1]), head->color, head->isnil});
    const RawNode *root = head->parent;
    DumpTreeNode(out, root, head);
    const RawNode *least = head, *most = head;
    if (!root->isnil) {
        for (least = root; !least->left->isnil;) least = least->left;
        for (most = root; !most->right->isnil;) most = most->right;
    }
    out.push_back(uint32_t(head->left == least) | uint32_t(head->right == most) << 1 | uint32_t(root->isnil ? root == head : 1) << 2);
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[4][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kWorldVTable}, {0x20, Sys::kMessageVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x1c; off += 4) b.push_back(Word(m, off));
    for (uint32_t off = 0x24; off < 0x4c; off += 4) b.push_back(Word(m, off)); // bounds, grades, height, 0x48 with its padding
    const auto &ids = *reinterpret_cast<const std::set<uint32_t> *>(m + 0x4c);
    b.push_back(uint32_t(ids.size()));
    for (uint32_t id : ids) b.push_back(id);
    b.push_back(Word(m, 0x54));
    b.push_back(Word(m, 0x58) - P(GameAddress<void>(0)));
    const auto *name = reinterpret_cast<const std::string *>(m + 0x5c);
    b.push_back(uint32_t(name->size()));
    b.push_back(uint32_t(name->capacity()));
    for (char c : *name) b.push_back(uint8_t(c));
    b.push_back(Word(m, 0x74));
    b.push_back(Word(m, 0x78));
    for (uint32_t off = 0x7c; off <= 0x90; off += 4) b.push_back(svcfx::Norm(Word(m, off)));
    DumpGrid<FloorPatternTile>(b, m, 0x94, [&](const FloorPatternTile &t) { for (uint16_t s : t.segment) b.push_back(s); });
    DumpGrid<FloorRotationTile>(b, m, 0x98, [&](const FloorRotationTile &t) { b.push_back(t); });
    DumpGrid<FloorPatternTile>(b, m, 0x9c, [&](const FloorPatternTile &t) { for (uint16_t s : t.segment) b.push_back(s); });
    DumpGrid<FloorRotationTile>(b, m, 0xa0, [&](const FloorRotationTile &t) { b.push_back(t); });
    DumpGrid<float>(b, m, 0xa4, [&](const float &t) { b.push_back(Bits(t)); });
    DumpGrid<FloorVisibilityTile>(b, m, 0xa8, [&](const FloorVisibilityTile &t) { for (uint8_t s : t.segment) b.push_back(s); });
    DumpGrid<uint8_t>(b, m, 0xac, [&](const uint8_t &t) { b.push_back(t); });
    DumpGrid<ObjectList>(b, m, 0xb0, [&](const ObjectList &t) {
        b.push_back(uint32_t(t.size()));
        b.push_back(uint32_t(t.capacity()));
        for (int32_t v : t) b.push_back(uint32_t(v));
    });
    b.push_back(Word(m, 0xb4));
    const auto &cells = *reinterpret_cast<const std::map<int32_t, ObjectCellSet> *>(m + 0xb8);
    b.push_back(uint32_t(cells.size()));
    for (const auto &entry : cells) {
        b.push_back(uint32_t(entry.first));
        b.push_back(uint32_t(entry.second.size()));
        for (const WorldCell &c : entry.second) b.push_back(uint32_t(c.x)), b.push_back(uint32_t(c.y)), b.push_back(uint32_t(c.level));
    }
    const auto *table = reinterpret_cast<const PatternTable *>(uintptr_t(Word(m, 0xc0)));
    b.push_back(table != nullptr);
    if (table) {
        // The game's cStringMap (the original's Init) or ours (our Init): its vtable word compared as "a cStringMap vtable".
        static const uint32_t kTableFace[] = {0};
        void *const *tableVTable = *reinterpret_cast<void *const *const *>(table);
        const bool tableKnown = tableVTable == shape::GameVTable(cStringMap::kVTable) || tableVTable == shape::Ours<cStringMap>(kTableFace).face[0];
        b.push_back(tableKnown ? cStringMap::kVTable : P(tableVTable));
        b.push_back(table->mRefCount);
        b.push_back(table->mWord8);
        b.push_back(table->mByteC);
        b.push_back(uint32_t(table->mUses.size()));
        for (const auto &u : table->mUses) b.push_back(P(u.first)), b.push_back(u.second.id), b.push_back(u.second.count);
        b.push_back(uint32_t(table->mNames.size()));
        for (const auto &n : table->mNames) b.push_back(n.first), b.push_back(P(n.second));
    }
    // The object index: the game's cVertexLayer (the original's Init) or ours (our Init), its vtable word compared as "a
    // cVertexLayer vtable", then both maps as trees.
    const auto *index = reinterpret_cast<const cVertexLayer *>(uintptr_t(Word(m, 0xc4)));
    b.push_back(index != nullptr);
    if (index) {
        static const uint32_t kFace[] = {0};
        void *const *vtable = *reinterpret_cast<void *const *const *>(index);
        const bool known = vtable == shape::GameVTable(cVertexLayer::kVTable) || vtable == shape::Ours<cVertexLayer>(kFace).face[0];
        b.push_back(known ? cVertexLayer::kVTable : P(vtable));
        b.push_back(index->mRefCount);
        DumpTreeMap(b, &index->mMap);
        DumpTreeMap(b, &index->mIds);
    }
    DumpGrid<DeckFoundationTile>(b, m, 0xc8, [&](const DeckFoundationTile &t) { for (uint32_t s : t.segment) b.push_back(s); });
    DumpGrid<AwningTile>(b, m, 0xcc, [&](const AwningTile &t) { b.push_back(t.first), b.push_back(t.second); });
    b.push_back(svcfx::Norm(Word(m, 0xd0)));
}

// The ranges the event log names: the database and the heap objects it holds.
void AddRanges(void *object)
{
    svcfx::gRanges.clear();
    svcfx::AddRange(object, Sys::kSize, kTag);
    const auto *m = static_cast<const uint8_t *>(object);
    unsigned i = 0;
    for (uint32_t off : {0x94u, 0x98u, 0x9cu, 0xa0u, 0xa4u, 0xa8u, 0xacu, 0xb0u, 0xc0u, 0xc4u, 0xc8u, 0xccu}) {
        if (const uint32_t p = Word(m, off)) svcfx::AddRange(reinterpret_cast<void *>(uintptr_t(p)), 0x20, kGridTag + (++i << 20));
    }
    const auto *name = reinterpret_cast<const std::string *>(m + 0x5c);
    if (name->capacity() > 15) svcfx::AddRange(const_cast<char *>(name->data()), uint32_t(name->capacity() + 1), kGridTag + (++i << 20));
}

// ---- tiles ----

struct Bounds {
    int32_t minX, minY, minLevel, maxX, maxY, maxLevel;
};
Bounds Current(const void *object)
{
    const auto *m = static_cast<const uint8_t *>(object);
    return {int32_t(Word(m, 0x24)), int32_t(Word(m, 0x28)), int32_t(Word(m, 0x2c)),
            int32_t(Word(m, 0x30)), int32_t(Word(m, 0x34)), int32_t(Word(m, 0x38))};
}
Bounds DrawBounds()
{
    Bounds b;
    b.minX = -int32_t(Draw(2));
    b.minY = -int32_t(Draw(2));
    b.maxX = b.minX + int32_t(Draw(6)); // a lot with no area too
    b.maxY = b.minY + int32_t(Draw(6));
    b.minLevel = -int32_t(Draw(2));
    b.maxLevel = int32_t(Draw(3));
    return b;
}
// Half the tiles repeat the last one's cell (new segments), so operations meet on the same tiles: two patterns on one
// tile, a footprint where an object is looked for, a name set twice. Reset for each side of each seed.
WorldTile gLastTile;
bool gHaveLast;
WorldTile DrawTile(const Bounds &b)
{
    WorldTile t{};
    t.x = b.minX - 2 + int32_t(Draw(uint32_t(b.maxX - b.minX + 5)));
    t.y = b.minY - 2 + int32_t(Draw(uint32_t(b.maxY - b.minY + 5)));
    t.level = b.minLevel - 1 + int32_t(Draw(uint32_t(b.maxLevel - b.minLevel + 3)));
    if (gHaveLast && Draw(2)) t.x = gLastTile.x, t.y = gLastTile.y, t.level = gLastTile.level;
    gLastTile = t;
    gHaveLast = true;
    t.segments = Draw(6) == 0 ? int8_t(0xf0 | Draw(16)) : int8_t(Draw(16));
    t.pad[0] = uint8_t(gRng());
    t.pad[1] = uint8_t(gRng());
    t.pad[2] = uint8_t(gRng());
    return t;
}
// Whether the tile's cell (relative to the origin) lies inside the grid at `off` (unchecked accesses).
bool InGrid(const void *object, uint32_t off, int32_t x, int32_t y, int32_t level)
{
    const auto *g = GridAt<uint8_t>(static_cast<const uint8_t *>(object), off);
    return g && x >= 0 && y >= 0 && level >= 0 && x < g->mSizeX && y < g->mSizeY && level < g->mSizeZ;
}
bool TileInGrid(const void *object, uint32_t off, const WorldTile &t)
{
    const Bounds b = Current(object);
    return InGrid(object, off, t.x - b.minX, t.y - b.minY, t.level - b.minLevel);
}

const float kFloats[] = {0.0f, 1.5f, -2.25f, 3.0f, 0.1875f, 7.75f, -0.5f, 1e6f};
float DrawFloat() { return kFloats[Draw(8)]; }
// Coordinates for points and rectangles: whole and half tiles, ties at .5, just under whole, off the lot.
float DrawCoord(int32_t low, int32_t high)
{
    const float base = float(low - 1 + int32_t(Draw(uint32_t(high - low + 3))));
    static const float kFrac[] = {0.0f, 0.5f, 0.25f, 0.999f, -0.0001f, 0.75f};
    return base + kFrac[Draw(6)];
}

// ---- operations ----

// The side being driven (the helpers' operation calls the original or the adapter).
bool gOurs;
difftest::Context *gCtx;

struct Op {
    unsigned kind;
    uint32_t arg;
};
constexpr unsigned kKinds = 32;

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 5);
    std::vector<Op> ops;
    ops.push_back({0, uint32_t(rng())});
    const unsigned n = 6 + rng() % 30;
    for (unsigned i = 0; i < n; ++i) ops.push_back({1 + unsigned(rng() % (kKinds - 1)), uint32_t(rng())});
    return ops;
}

void SetBounds(void *world, const Bounds &b) { Slot<uint32_t>(world, 6, b.minX, b.minY, b.maxX, b.maxY); }
void SetLevels(void *world, const Bounds &b) { Slot<uint32_t>(world, 7, b.minLevel, b.maxLevel); }

static const uint32_t kIids[] = {1, Sys::kIid, Sys::kClassId, 0x287fb697, 0x4a037ee3, 0x12345678};
static const uint32_t kMessageTypes[] = {0xad, 0xad, 0x0c02efce, 0x2c02eff6, 0, 0x12345678};
static const int32_t kObjectIds[] = {1, 2, 7, 0x123};

uint32_t CallPattern(void *world, unsigned slot, const WorldTile &t)
{
    WorldName out{reinterpret_cast<const char *>(uintptr_t(0x5e5e5e5e))};
    const uint32_t r = Slot<uint32_t>(world, slot, &out, &t);
    return (r == P(&out) ? 0x10000000u : 0u) ^ P(out.text);
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const world = Face(object, 0x1c);
    void *const target = Face(object, 0x20);
    auto *const m = static_cast<uint8_t *>(object);
    const uint32_t a = op.arg;
    const Bounds bounds = Current(object);
    WorldTile t = DrawTile(bounds);
    switch (op.kind) {
    case 0: { // Init, the levels and the bounds; half the seeds post their changes
        b.push_back(Slot<uint32_t>(primary, 8) & 0xff);
        m[0x55] = uint8_t(a % 2);
        const Bounds nb = DrawBounds();
        SetLevels(world, nb);
        SetBounds(world, nb);
        break;
    }
    case 1: { // new levels and bounds (SetLevels shifts the grids through 0xd0d040, SetBounds resizes them)
        if (a % 3 == 0) {
            // A level more on top before the grids are resized: the awning getters check the grid's height.
            Bounds up = bounds;
            ++up.maxLevel;
            SetLevels(world, up);
            WorldTile top = t;
            top.level = up.maxLevel;
            b.push_back(Slot<uint32_t>(world, 59, &top));
            WorldPair out{0x5e5e5e5e, 0x5e5e5e5e};
            Slot<uint32_t>(world, 60, &out, &top);
            b.push_back(out.first);
            b.push_back(out.second);
        }
        const Bounds nb = DrawBounds();
        SetLevels(world, nb);
        SetBounds(world, nb);
        break;
    }
    case 2: // Shutdown (sometimes without the settings service), Init, the bounds again
        gWorld.settings = a % 4 != 0;
        b.push_back(Slot<uint32_t>(primary, 9) & 0xff);
        gWorld.settings = true;
        b.push_back(Slot<uint32_t>(primary, 8) & 0xff);
        SetBounds(world, bounds);
        break;
    case 3: // Reset, the bounds again
        b.push_back(Slot<uint32_t>(world, 10) & 0xff);
        SetBounds(world, bounds);
        break;
    case 4: { // DoMessage (null too)
        F(kStd).ret[4] = kMessageTypes[a % 6];
        void *message = a % 11 == 0 ? nullptr : static_cast<void *>(&F(kMessage));
        b.push_back(Slot<uint32_t>(target, 3, message) & 0xff);
        break;
    }
    case 5: { // QueryInterface on a face (a null out too), AddRef / Release through others
        void *faces[] = {primary, Face(object, 4), world, target};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const bool nullOut = (a >> 10) % 9 == 0;
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, kIids[(a >> 4) % 6], nullOut ? nullptr : &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 14) % 4], 2));
        if (ok && !nullOut) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 6: // the service accessors, OnTick / OnIdle, the +4 face's RefCount, GetGZCLSID, slot 42
        b.push_back(Slot<uint32_t>(primary, 3));
        b.push_back(Slot<uint32_t>(primary, 5));
        b.push_back(Slot<uint32_t>(primary, 6) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 12));
        Slot<uint32_t>(primary, 7, a % 2);
        b.push_back(Slot<uint32_t>(primary, 10, a) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 11, a) & 0xff);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        b.push_back(Slot<uint32_t>(world, 5));
        {
            uint32_t stored = 0x5e5e5e5e;
            b.push_back(Slot<uint32_t>(world, 42, &stored) == P(&stored));
            b.push_back(stored - P(GameAddress<void>(0)));
        }
        break;
    case 7: // Write
        b.push_back(Slot<uint32_t>(world, 3, static_cast<void *>(&F(kStream))) & 0xff);
        break;
    case 8: { // Read: a class id, a version, the bounds (version 1: the sizes), the name; then the levels and bounds set
        Obj &in = F(kStream);
        in.Set(9, "p", 0, &StreamReadWord);
        in.Set(8, "p", 0, &StreamReadWord);
        in.Set(15, "p", 0, &StreamReadString);
        const Bounds nb = DrawBounds();
        gReadWords.clear();
        gReadAt = 0;
        gReadWords.push_back(a % 9 == 0 ? 0x12345678u : Sys::kClassId);
        static const uint32_t kVersions[] = {1, 2, 3, 4, 5, 7, 7, 0, 8, 0xffffffff};
        const uint32_t version = kVersions[(a >> 4) % 10];
        gReadWords.push_back(version);
        if (version == 1) {
            gReadWords.push_back(uint32_t(nb.maxX + 1));
            gReadWords.push_back(uint32_t(nb.maxY + 1));
            gReadWords.push_back(uint32_t(nb.maxLevel + 1));
        } else {
            for (int32_t v : {nb.minX, nb.minY, nb.minLevel, nb.maxX, nb.maxY, nb.maxLevel}) gReadWords.push_back(uint32_t(v));
        }
        gReadWords.push_back((a >> 8) % 3 == 0 ? 2u : (a >> 8) % 3);
        b.push_back(Slot<uint32_t>(world, 4, static_cast<void *>(&in)) & 0xff);
        in.Set(9, "d", 0, [](Obj *, uint32_t *) { return Bool(); });
        in.Set(8, "d", 0, [](Obj *, uint32_t *) { return Bool(); });
        in.Set(15, "p", 0, &StreamString);
        const Bounds read = Current(object);
        // The grids follow the bounds read (the levels already stored: SetLevels shifts nothing).
        SetLevels(world, read);
        SetBounds(world, read);
        break;
    }
    case 9: { // the floor patterns: slots 13 / 15 / 17, primary 14 (the getters unchecked: a tile in the grid)
        WorldName name{PatternName(a % (kPatternCount + 2))};
        const unsigned which = (a >> 4) % 4;
        if (which == 2) {
            Slot<uint32_t>(world, 17, &name, &t);
        } else if (TileInGrid(object, which == 1 ? 0x9c : 0x94, t)) {
            // A third of the time twice: the second finds the name already there (no message).
            for (unsigned n = (a >> 9) % 3 == 0 ? 2 : 1; n; --n) {
                if (which == 3) Slot<uint32_t>(primary, 14, &name, &t);
                else Slot<uint32_t>(world, which == 0 ? 13 : 15, &name, &t);
            }
        }
        break;
    }
    case 10: { // rotations (slots 14 / 16)
        const uint16_t rotation = uint16_t((a >> 2) % 7 == 0 ? 0x1234 + (a >> 8) % 3 : (a >> 8) % 6);
        const bool second = a % 2;
        if (TileInGrid(object, second ? 0xa0 : 0x98, t)) {
            Slot<uint32_t>(world, second ? 16 : 14, &rotation, &t);
            // A third of the time rotation 1 after it: the getter reads 0 (and mixed segments) as 1, so nothing is stored.
            if ((a >> 11) % 3 == 0) {
                const uint16_t one = 1;
                Slot<uint32_t>(world, second ? 16 : 14, &one, &t);
            }
        }
        break;
    }
    case 11: { // elevations (slots 18 / 19, primary 15)
        const float e = DrawFloat();
        static const unsigned kSlots[] = {18, 19};
        if (a % 3 == 2) Slot<uint32_t>(primary, 15, Bits(e), &t);
        else Slot<uint32_t>(world, kSlots[a % 3], Bits(e), &t);
        break;
    }
    case 12: // visibility (slot 20)
        Slot<uint32_t>(world, 20, uint32_t(a % 3 == 0 ? 0 : 0x100 | (a >> 2) % 3), &t);
        break;
    case 13: { // AddObjectRects (slot 21)
        static std::vector<WorldRect> rects;
        rects.clear();
        const unsigned n = (a >> 2) % 4;
        for (unsigned i = 0; i < n; ++i) {
            WorldRect r;
            r.x0 = DrawCoord(bounds.minX, bounds.maxX);
            r.y0 = DrawCoord(bounds.minY, bounds.maxY);
            r.x1 = Draw(4) == 0 ? r.x0 - 1.0f : DrawCoord(bounds.minX, bounds.maxX);
            r.y1 = DrawCoord(bounds.minY, bounds.maxY);
            rects.push_back(r);
        }
        Slot<uint32_t>(world, 21, kObjectIds[a % 4], &rects, t.level);
        break;
    }
    case 14: { // AddObjectAt / RemoveObjectAt (slots 22 / 25)
        static WorldPoint point;
        point.x = Draw(2) ? float(t.x) + 0.5f : DrawCoord(bounds.minX, bounds.maxX);
        point.y = Draw(2) ? float(t.y) + 0.25f : DrawCoord(bounds.minY, bounds.maxY);
        point.level = t.level;
        const int32_t id = (a >> 3) % 5 == 0 ? -kObjectIds[a % 4] : kObjectIds[a % 4];
        Slot<uint32_t>(world, (a >> 2) % 3 == 0 ? 25 : 22, id, &point);
        break;
    }
    case 15: { // RemoveObject / GetObjectCells (slots 23 / 24)
        const int32_t id = (a >> 3) % 7 == 0 ? 99 : kObjectIds[a % 4];
        if ((a >> 2) % 2) {
            Slot<uint32_t>(world, 23, id);
        } else {
            WorldCellList cells; // its buffer from the allocator service
            cells.assign((a >> 6) % 3, WorldCell{9, 9, 9});
            b.push_back(Slot<uint32_t>(world, 24, id, &cells) & 0xff);
            b.push_back(uint32_t(cells.size()));
            for (const WorldCell &c : cells) b.push_back(uint32_t(c.x)), b.push_back(uint32_t(c.y)), b.push_back(uint32_t(c.level));
        }
        break;
    }
    case 16: { // the object index (slots 26 / 27 / 68: cVertexLayer Add / Remove, Contains + ValueOf; the index's maps in Dump)
        // A few calls on vertices from a small set (so ids move between vertices, vertices collide, removals match or
        // not). Slots 26 / 27 return no value (the original leaves its callee's eax: a node or head pointer).
        std::mt19937 rng(a);
        if (!Word(m, 0xc4)) break; // no index before Init
        for (unsigned n = 1 + rng() % 6; n; --n) {
            static const float kCoords[] = {0.0f, 1.5f, -2.0f};
            const LayerVertex v{kCoords[rng() % 3], kCoords[rng() % 3], int32_t(rng() % 2) - 1};
            const int32_t id = rng() % 5 == 0 ? -kObjectIds[rng() % 4] : kObjectIds[rng() % 4];
            switch (rng() % 3) {
            case 0: Slot<uint32_t>(world, 26, id, &v); break;
            case 1: Slot<uint32_t>(world, 27, id, &v); break;
            default: b.push_back(Slot<uint32_t>(world, 68, &v)); break;
            }
        }
        break;
    }
    case 17: { // byte tiles (28, primary 16, 57), deck markers (29, 58), awnings (30, 31, 59, 60)
        const uint32_t value = (a >> 3) % 4 == 0 ? 0xffffffffu : (a >> 5) % 5;
        switch (a % 8) {
        case 0:
        case 1: // set, then read back
            if (a % 8 == 0) Slot<uint32_t>(world, 28, uint32_t(0x100 | value), &t);
            else Slot<uint32_t>(primary, 16, uint32_t(0x200 | value), &t);
            if (TileInGrid(object, 0xac, t)) b.push_back(Slot<uint32_t>(world, 57, &t) & 0xff);
            break;
        case 2:
            if (TileInGrid(object, 0xac, t)) b.push_back(Slot<uint32_t>(world, 57, &t) & 0xff);
            break;
        case 3:
            if (TileInGrid(object, 0xc8, t)) Slot<uint32_t>(world, 29, &value, &t);
            break;
        case 4: b.push_back(Slot<uint32_t>(world, 58, &t)); break;
        case 5: Slot<uint32_t>(world, 30, &value, &t); break;
        case 6: {
            const AwningTile pair{value, (a >> 9) % 3};
            Slot<uint32_t>(world, 31, &pair, &t);
            break;
        }
        default: {
            b.push_back(Slot<uint32_t>(world, 59, &t));
            WorldPair out{0x5e5e5e5e, 0x5e5e5e5e};
            const uint32_t r = Slot<uint32_t>(world, 60, &out, &t);
            b.push_back(r == P(&out));
            b.push_back(out.first);
            b.push_back(out.second);
            break;
        }
        }
        break;
    }
    case 18: { // SetMinGrade, SetName (a non-empty name; "clear"), GetName
        static const int32_t kGrades[] = {0, 3, 4, 5, 9, -1, INT32_MIN};
        Slot<uint32_t>(world, 32, kGrades[a % 7]);
        const char *text = kStrings[(a >> 4) % 4];
        std::memcpy(gNameText, text, std::strlen(text) + 1);
        F(kName).ret[7] = (a >> 6) % 5 == 0 ? 0u : uint32_t(std::strlen(text)) + (a >> 9) % 2;
        Slot<uint32_t>(world, 33, static_cast<void *>(&F(kName)));
        AddRanges(object); // the name's new buffer
        b.push_back(Slot<uint32_t>(world, 99, static_cast<void *>(&F(kOut))) & 0xff);
        break;
    }
    case 19: { // the getters (slots 34-39, 75-98, 100)
        for (unsigned s : {34u, 97u}) b.push_back(Slot<uint32_t>(world, s) & 0xff);
        for (unsigned s : {35u, 36u, 37u, 38u, 39u, 100u}) b.push_back(svcfx::Norm(Slot<uint32_t>(world, s)));
        for (unsigned s = 81; s <= 98; ++s) {
            if (s == 87 || (s >= 91 && s <= 97)) continue;
            b.push_back(Slot<uint32_t>(world, s));
        }
        for (unsigned s : {75u, 76u, 77u, 78u, 79u, 80u, 87u, 91u, 92u, 93u, 94u, 95u, 96u}) b.push_back(Bits(Slot<float>(world, s)));
        break;
    }
    case 20: { // pattern names and ids (40, 41, 43-48, primary 13 / 17 / 18 / 19)
        for (unsigned s : {40u, 41u}) {
            WorldName out{nullptr};
            const uint32_t r = Slot<uint32_t>(world, s, &out);
            b.push_back(r == P(&out));
            b.push_back(P(out.text));
        }
        if (TileInGrid(object, 0x94, t)) {
            b.push_back(CallPattern(world, 43, t));
            b.push_back(Slot<uint32_t>(primary, 18, &t) & 0xffff);
            if ((a >> 3) % 4 == 0) Slot<uint32_t>(primary, 13, uint32_t((a >> 5) % 5), &t);
        }
        if (TileInGrid(object, 0x9c, t)) {
            b.push_back(CallPattern(world, 45, t));
            b.push_back(Slot<uint32_t>(primary, 19, &t) & 0xffff);
            if ((a >> 3) % 4 == 1) Slot<uint32_t>(primary, 17, uint32_t((a >> 5) % 5), &t);
        }
        if (TileInGrid(object, 0x98, t)) b.push_back(Slot<uint32_t>(world, 44, &t));
        if (TileInGrid(object, 0xa0, t)) b.push_back(Slot<uint32_t>(world, 46, &t));
        if (a % 2) {
            PatternNameSet names;
            names.clear();
            if ((a >> 9) % 3 == 0) names.insert(kPatternNames[3]);
            Slot<uint32_t>(world, 47, (a >> 11) % 6 == 0 ? nullptr : static_cast<void *>(&names));
            b.push_back(uint32_t(names.size()));
            for (const char *n : names) b.push_back(P(n));
        }
        const WorldName name{PatternName((a >> 1) % kPatternCount)};
        b.push_back(Slot<uint32_t>(world, 48, &name) & 0xff);
        break;
    }
    case 21: { // floors: 49-56, 61
        const int8_t mask = int8_t(a >> 4);
        const uint32_t check = 0x100 | (a >> 12) % 2;
        uint8_t visible = 0xcc;
        b.push_back(Slot<uint32_t>(world, 49, &t, (a >> 3) % 4 == 0 ? nullptr : &visible) & 0xff);
        b.push_back(visible);
        for (unsigned s = 50; s <= 55; ++s) b.push_back(Slot<uint32_t>(world, s, &t, uint32_t(uint8_t(mask)), check) & 0xff);
        b.push_back(Slot<uint32_t>(world, 56, &t) & 0xff);
        WorldTile masked = t;
        masked.segments = mask;
        if (TileInGrid(object, 0x94, masked)) b.push_back(Slot<uint32_t>(world, 61, &t, uint32_t(uint8_t(mask))) & 0xff);
        break;
    }
    case 22: { // heights (62-64): only where the original's clamp keeps the cell in the grid
        const Bounds &bb = bounds;
        auto clamp = [&](int32_t &x, int32_t &y) {
            if (x < bb.minX || y < bb.minY || x > bb.maxX + 1 || y > bb.maxY + 1) {
                x = (std::min)((std::max)(x, bb.minX), bb.maxX + 1);
                y = (std::min)((std::max)(y, bb.minY), bb.maxY + 1);
            }
        };
        int32_t x = t.x - bb.minX, y = t.y - bb.minY;
        clamp(x, y);
        if (InGrid(object, 0xa4, x, y, t.level - bb.minLevel)) b.push_back(Bits(Slot<float>(world, 62, &t)));
        int32_t tx = t.x, ty = t.y;
        b.push_back(Bits(Slot<float>(world, 63, tx, ty)));
        clamp(tx, ty);
        const auto *g = GridAt<float>(m, 0xa4);
        if (g) {
            const int32_t at = (tx - g->mSizeX * bb.minLevel) * g->mSizeY + ty;
            if (at >= 0 && at < int32_t(g->mData.size())) b.push_back(Bits(Slot<float>(world, 64, t.x, t.y)));
        }
        break;
    }
    case 23: { // objects on a tile: 65, 66 (unchecked), 67
        const uint32_t footprints = 0x100 | (a >> 2) % 2;
        b.push_back(Slot<uint32_t>(world, 65, &t));
        if (TileInGrid(object, 0xb0, t)) b.push_back(Slot<uint32_t>(world, 66, kObjectIds[a % 4], &t, footprints));
        static const int32_t kIndexes[] = {0, 1, 2, -1, 5};
        b.push_back(Slot<uint32_t>(world, 67, kIndexes[(a >> 4) % 5], &t, footprints));
        break;
    }
    case 24: { // bounds tests 69-74 (NaN points too)
        static WorldPoint point;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        point.x = (a >> 2) % 9 == 0 ? nan : DrawCoord(bounds.minX, bounds.maxX + 1);
        point.y = (a >> 5) % 9 == 0 ? nan : DrawCoord(bounds.minY, bounds.maxY + 1);
        point.level = t.level;
        b.push_back(Slot<uint32_t>(world, 69, &point) & 0xff);
        for (unsigned s : {70u, 71u, 73u}) b.push_back(Slot<uint32_t>(world, s, &t) & 0xff);
        b.push_back(Slot<uint32_t>(world, 72, Bits(point.x), Bits(point.y)) & 0xff);
        b.push_back(Slot<uint32_t>(world, 74, Bits(point.x), Bits(point.y)) & 0xff);
        break;
    }
    case 25: // Save
        b.push_back(Slot<uint32_t>(world, 8, static_cast<void *>(&F(kWriter)), 0x100u | (a % 2)) & 0xff);
        break;
    case 26: // Load / Create (still game code: the forwarders)
        b.push_back(Slot<uint32_t>(world, 9, a & 0xfffff, (a >> 8) & 0xfffff));
        b.push_back(Slot<uint32_t>(world, 11, (a >> 4) & 0xfffff, a >> 12, a >> 16));
        break;
    case 27: // slot 12
        Slot<uint32_t>(world, 12, a & 0xfffff);
        break;
    case 28: // state the code reads: posting, the re-post flag, the uneven tiles setting, the levels' height
        m[0x55] = uint8_t(a % 3 != 0);
        m[0xb4] = uint8_t((a >> 2) % 4 == 0);
        m[0x48] = uint8_t((a >> 4) % 2);
        if ((a >> 5) % 4 == 0) *reinterpret_cast<float *>(m + 0x44) = DrawFloat();
        *reinterpret_cast<uint32_t *>(m + 0x40) = (a >> 7) % 5 == 0 ? 0x80000001u : (a >> 7) % 7;
        break;
    case 30: { // objects meeting on one tile: one at its centre, one whose footprint covers it, then the queries
        const int32_t id = kObjectIds[a % 4];
        const int32_t other = kObjectIds[(a >> 2) % 4];
        WorldPoint point{float(t.x) + 0.5f, float(t.y) + 0.5f, t.level};
        Slot<uint32_t>(world, 22, id, &point);
        if ((a >> 4) % 3) {
            static std::vector<WorldRect> rects;
            rects.assign(1, WorldRect{float(t.x) - 0.5f, float(t.y), float(t.x) + 0.25f, float(t.y) + 0.5f});
            Slot<uint32_t>(world, 21, other, &rects, t.level);
        }
        if (TileInGrid(object, 0xb0, t)) {
            for (uint32_t footprints : {0x100u, 0x101u}) {
                b.push_back(Slot<uint32_t>(world, 66, id, &t, footprints));
                b.push_back(Slot<uint32_t>(world, 66, other, &t, footprints));
                for (int32_t index = -1; index < 4; ++index) b.push_back(Slot<uint32_t>(world, 67, index, &t, footprints));
            }
        }
        b.push_back(Slot<uint32_t>(world, 65, &t));
        if ((a >> 6) % 2) Slot<uint32_t>(world, 25, (a >> 7) % 3 == 0 ? other : id, &point);
        if ((a >> 8) % 3 == 0) Slot<uint32_t>(world, 23, other);
        break;
    }
    case 31: { // the helpers Load / Create call directly: the original on the original's object, the adapter on ours
        WorldName name{PatternName(a % (kPatternCount + 2))};
        switch ((a >> 4) % 5) {
        case 0: {
            using Fn = uint16_t(__fastcall *)(void *, void *, const WorldName *);
            const Fn fn = gOurs ? reinterpret_cast<Fn>(&cWorldDBAdapters::PatternId) : gCtx->Original<Fn>(0xd0e1d0);
            b.push_back(fn(primary, nullptr, &name));
            break;
        }
        case 1: {
            using Fn = void(__fastcall *)(void *, void *, const WorldName *, const WorldTile *);
            const Fn fn = gOurs ? reinterpret_cast<Fn>(&cWorldDBAdapters::SetPatternByNameB) : gCtx->Original<Fn>(0xd127b0);
            if (TileInGrid(object, 0x9c, t)) fn(primary, nullptr, &name, &t);
            break;
        }
        case 2: {
            using Fn = void(__fastcall *)(void *, void *, int32_t, int32_t);
            const Fn fn = gOurs ? reinterpret_cast<Fn>(&cWorldDBAdapters::SetLevels) : gCtx->Original<Fn>(0xd13230);
            const Bounds nb = DrawBounds();
            fn(primary, nullptr, nb.minLevel, nb.maxLevel);
            SetBounds(world, nb); // the grids follow
            break;
        }
        case 3: {
            using Fn = void(__fastcall *)(void *, void *, uint32_t, const WorldTile *);
            const Fn fn = gOurs ? reinterpret_cast<Fn>(&cWorldDBAdapters::SetByteTile) : gCtx->Original<Fn>(0xd13290);
            fn(primary, nullptr, 0x300u | (a >> 8) % 5, &t);
            break;
        }
        default: {
            using Fn = void(__fastcall *)(void *, void *, const WorldName *, const WorldName *, uint32_t);
            const Fn fn = gOurs ? reinterpret_cast<Fn>(&cWorldDBAdapters::UsePatternName) : gCtx->Original<Fn>(0xd138d0);
            const WorldName old{PatternName((a >> 8) % (kPatternCount + 2))};
            fn(primary, nullptr, &old, &name, 0x400u | (a >> 12) % 256);
            break;
        }
        }
        break;
    }
    default: { // the primary face's own slots
        WorldName name{PatternName(a % (kPatternCount + 2))};
        switch ((a >> 4) % 4) {
        case 0:
            if (TileInGrid(object, 0x94, t)) Slot<uint32_t>(primary, 14, &name, &t);
            break;
        case 1: Slot<uint32_t>(primary, 15, Bits(DrawFloat()), &t); break;
        case 2: Slot<uint32_t>(primary, 16, uint32_t((a >> 8) % 3), &t); break;
        default:
            if (TileInGrid(object, 0x9c, t)) Slot<uint32_t>(primary, 17, uint32_t((a >> 8) % 4), &t);
            break;
        }
        break;
    }
    }
}

} // namespace worlddb_test

using namespace worlddb_test;

DIFF_TEST(WorldDB_Run, 0xd0b6c0)
{
    // Also reached (see the file comment): 0xd0b960 (destructor), 0xd0c030 (deleting destructor), 0xd0bd57 (its +4 thunk),
    // 0xd11360, 0xd113b9, 0xd113c1, 0xd113c9 (QueryInterface and thunks), 0xd0e6b0 (Init), 0xd133c0 (Shutdown), 0xd0db70
    // (DoMessage), 0xd143e0, 0xd11ce0, 0xd0e2c0, 0xd13310, 0xd11e80, 0xd13180, 0xd13230, 0xd12360, 0xd138b0, 0xd0eea0,
    // 0xd12c70, 0xd12630, 0xd0ef50, 0xd12df0, 0xd127b0, 0xd138d0, 0xd0df10, 0xd0e1d0, 0xd12f50, 0xd12910, 0xd0e130, 0xd0d9c0,
    // 0xd12e70, 0xd12e80, 0xd12830, 0xd0df30, 0xd0d8f0, 0xd12a50, 0xd0ef30, 0xd12bd0, 0xd12c50, 0xd11150, 0xd12fe0, 0xd0e200,
    // 0xd0d3f0, 0xd0d400, 0xd0d320, 0xd0d330, 0xd120c0, 0xd0e2d0, 0xd12000, 0xd0d3d0, 0xd120a0, 0xd10fa0, 0xd0e620, 0xd110a0,
    // 0xd11120, 0xd13280, 0xd13290, 0xd13300, 0xd0f7a0, 0xd129a0, 0xd0da60, 0xd12530, 0xd125b0, 0xd0d7a0, 0xd0d820, 0xd0e4f0,
    // 0xd0e4d0, 0xd0e420, 0xd0e5f0, 0xd0e440, 0xd0e470, 0xd0e3c0, 0xd0fae0, 0xd0fa60, 0xd13350, 0xd10d40, 0xd10f50, 0xd0db30,
    // 0xd0dee0, 0xd0d8c0, 0xd0e000, 0xd0f630, 0xd0de50, 0xd13ac0, 0xd13700, 0xd0f7f0, 0xd0f8f0, 0xd0f850, 0xd0f9a0, 0xd0f690,
    // 0xd0f700, 0xd10ef0, 0xd10f20, 0xd10e40, 0xd10e80, 0xd10d80, 0xd10db0, 0xd11040, 0xd11070, 0xd10f70, 0xd10de0, 0xd10ed0,
    // 0xd10e00, 0xd10ec0, 0xd0db20, 0xd13160, and the folded one-liners of slots 34-39, 81-84, 87-89, 94, 95, 97, 98, 100.
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {svcfx::kAppCell, 0x10c9378u, svcfx::kPersistCell}) globals.SetWord(cell, 0);

    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::StandIn server(0x9c5a89, &GetCachedMessageServer, &StandServer);
    shape::CodePatch statics(0xd15080, shape::CodeOf(&SetUpStatics));
    shape::CodePatch derived(0xd17090, shape::CodeOf(&RebuildDerived));
    shape::CodePatch shift(0xd0d040, RecordFor<0xd0d040, 2>());
    shape::CodePatch helper1(0x81e800, RecordFor<0x81e800, 2>());
    shape::CodePatch helper2(0x81ea20, RecordFor<0x81ea20, 0>());
    shape::CodePatch helper3(0x801530, RecordFor<0x801530, 0>());
    shape::CodePatch load(0xd0fb30, RecordFor<0xd0fb30, 2>());
    shape::CodePatch create(0xd0ef80, RecordFor<0xd0ef80, 3>());
    // The forwarded slots are where the game's cIWorldDB vtable has them.
    ++ctx.cases;
    if (GameAddress<void *>(Sys::kWorldVTable)[9] != GameAddress<void>(0xd0fb30) ||
        GameAddress<void *>(Sys::kWorldVTable)[11] != GameAddress<void>(0xd0ef80))
        ctx.Fail("game world slots 9 / 11 are not Load / Create");

    constexpr uint32_t kSeeds = 20000;
    // Tracing: a fault prints the stack words that point into the image (debugging only).
    void *faultHandler = nullptr;
    if (svcfx::gTrace)
        faultHandler = AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS *e) -> LONG {
            if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
            const uint32_t base = P(GameAddress<void>(0));
            fprintf(stderr, "fault at %08x\n", uint32_t(e->ContextRecord->Eip) - base);
            const auto *sp = reinterpret_cast<const uint32_t *>(uintptr_t(e->ContextRecord->Esp));
            for (int i = 0; i < 400; ++i)
                if (sp[i] - base < 0x1000000u) fprintf(stderr, "  [%3d] %08x\n", i, sp[i] - base);
            return EXCEPTION_CONTINUE_SEARCH;
        });
    char firstSeed[16] = "1"; // TS2_TEST_SEED: start at a seed (debugging a failure)
    GetEnvironmentVariableA("TS2_TEST_SEED", firstSeed, sizeof(firstSeed));
    for (uint32_t seed = uint32_t(strtoul(firstSeed, nullptr, 10)); seed <= kSeeds; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gHaveLast = false;
            gOurs = side == 1;
            gCtx = &ctx;
            Blob first;
            gRng.seed(seed * 1000 + 3);
            void *object = Build(ctx, first, side == 1);
            AddRanges(object);
            Dump(first, object, side == 1);
            first.insert(first.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                AddRanges(object);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "WorldDB_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                const size_t op = i ? i - 1 : 0;
                ctx.Fail("  at step %u of %u (kind %u)", unsigned(i), unsigned(ops.size() + 2),
                         i && op < ops.size() ? ops[op].kind : 99u);
                seed = kSeeds + 1;
                break;
            }
        }
    }
    if (faultHandler) RemoveVectoredExceptionHandler(faultHandler);
}
