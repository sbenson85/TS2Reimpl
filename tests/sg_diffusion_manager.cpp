// Differential tests of nTSSG::cDiffusionManager (lane 261109f, scenegraph LD1; src/sg/cDiffusionManager.h).
//
//   SG_DiffusionManagerLife  one manager's life on each side: built by the constructor 0x3c4140 on a block filled alike,
//       then random calls through its three vtables: Init / Shutdown / Start / Stop / SetSettings (3-7), the passes
//       Diffuse / DiffuseUnlit (12, 13; the debug switches 0x100eb20-23 and 0x10da3dc-df set alike to random bytes, and
//       the dirty flag both ways), ReadLightMaps (14), ReadLightMapsAndCollect (15), the collectors (16-19, into vectors
//       holding junk), PassChanges (20), Reset (21), Mark (22), GetFloorColor (23, levels in and out of range) and
//       GetWallColor (24), DoMessage (+4 slot 3) with the levels message (the grids resized from the world's level count
//       and lowest level, each grid sized from its floor or skipped for a missing one), the world-loaded message (the
//       holder's property set read by the settings readers, then, when started, the settings applied and the change
//       message posted), the change message with each data1 and another type, QueryInterface through each face, AddRef /
//       Release. Between steps the world's levels and the floors' sizes change alike. After each call the object is noted
//       word by word (vtables left out), then the message ids, each grid's words, the two collected lists, the wall
//       manager's words and the grid counts. At the end the manager is deleted (the +8 face's deleting destructor
//       0x3c482c / 0x3c4840 with the destructor 0x3c4730, or the last Release), then the fakes' counts and the heap.
//
// Row LD2's engine is stood in on both sides (code patches over the image's functions, which our code calls through
// OriginalFunction, and, for rows LD2a-LD2c's cFloorColorGrid members our code now calls, over ours as well): the floor colour grid's constructor 0x4056d0 (a test vtable whose slot 0, the destructor, counts),
// its copy 0x3c42c0, setters and passes; the wall diffusion manager's constructor 0x40be50 (a counted test object in the
// 0x60 block: Release slot 1 frees it at zero, AddRef slot 2), setters and passes. Each stand-in notes its RVA, its
// receiver (the wall manager's serial, or the grid's offset from the grid vector's start) and its arguments; the setters
// store their values in the object, the collectors push records, the colour getters write their out. Also stood in: the
// stopwatch's Restart 0x1c95ec and Stop 0x1c9842 (each watch tagged at its first Restart), the settings readers
// 0xaf7190 / 0xaf7210 / 0xaf7290, the services 0x20e0b2 (the world DB fake: 39 the partitions, 88 the lowest level, 90
// the level count) and 0x20defb (the scene graph system fake: 34 the wall manager, 38 the floor manager, whose 19 gives
// floor fakes, 48 the light map manager, whose 23 gives the scales), RZMemory's Malloc / Free and the game's operator
// new / delete / free. Cells: the framework 0x10c9378, the message server cache 0x10f519c, the allocator 0x10c95d4 /
// 0x10c95d8.
#include "sgfx.h"

#include "code_patch.h"

#include "rz/RZMemory.h"
#include "rz/StopwatchMore.h"
#include "rz/cRZStopwatch.h"
#include "sg/SGUtilities.h"
#include "sg/cDiffusionManager.h"
#include "sg/cWallDiffusionManager.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <set>
#include <vector>

namespace {

using namespace nTSSG;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

// cIGZFrameWork: 5 GetSystemService.
constexpr At kFrameworkAt[] = {{5, {3, Ret::Int, true, "GetSystemService", "uua"}}};
constexpr auto kFrameworkSlots = Table<6>(kFrameworkAt);
constexpr drawfx::Iface kFramework{"FrameWork", 6, kFrameworkSlots.data(), false, drawfx::kTestTable};

// cIGZMessageServer2: 4 MessagePost, 5 AddNotification, 6 RemoveNotification.
constexpr At kServerAt[] = {{4, {2, Ret::Int, true, "MessagePost", "ab"}},
                            {5, {2, Ret::Int, true, "AddNotification", "au"}},
                            {6, {2, Ret::Int, true, "RemoveNotification", "au"}}};
constexpr auto kServerSlots = Table<9>(kServerAt);
constexpr drawfx::Iface kServer{"MessageServer2", 9, kServerSlots.data(), false, drawfx::kTestTable};

// The allocator service: 3 allocates, 4 frees.
constexpr At kAllocatorAt[] = {{3, {1, Ret::Int, true, "Allocate", "u"}}, {4, {1, Ret::Int, true, "Free", "a"}}};
constexpr auto kAllocatorSlots = Table<5>(kAllocatorAt);
constexpr drawfx::Iface kAllocator{"AllocatorService", 5, kAllocatorSlots.data(), false, drawfx::kTestTable};

// nTSWorld::cIWorldDB: 39 GetPartitions, 88 GetMinLevel, 90 GetLevelCount.
constexpr At kWorldAt[] = {{39, {0, Ret::Int, true, "GetPartitions", ""}},
                           {88, {0, Ret::Int, true, "GetMinLevel", ""}},
                           {90, {0, Ret::Int, true, "GetLevelCount", ""}}};
constexpr auto kWorldSlots = Table<91>(kWorldAt);
constexpr drawfx::Iface kWorld{"WorldDB", 91, kWorldSlots.data(), false, drawfx::kTestTable};

constexpr At kUnknownAt[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
constexpr auto kUnknownSlots = Table<3>(kUnknownAt);
constexpr drawfx::Iface kUnknownFace{"Unknown", 3, kUnknownSlots.data(), false, drawfx::kTestTable};

// cIGZMessage2Standard: 4 GetType, 9 GetData1, 32 GetIGZUnknown.
constexpr At kMessageAt[] = {{4, {0, Ret::Int, true, "GetType", ""}},
                             {9, {0, Ret::Int, true, "GetData1", ""}},
                             {32, {0, Ret::Int, true, "GetIGZUnknown", ""}}};
constexpr auto kMessageSlots = Table<37>(kMessageAt);
constexpr drawfx::Iface kMessage{"Message2Standard", 37, kMessageSlots.data(), false, drawfx::kTestTable};

// nTSSG::cTSSGSystem: 34 the wall manager, 38 the floor manager, 48 the light map manager.
constexpr At kSystemAt[] = {{34, {0, Ret::Int, true, "WallManager", ""}},
                            {38, {0, Ret::Int, true, "FloorManager", ""}},
                            {48, {0, Ret::Int, true, "LightMapManager", ""}}};
constexpr auto kSystemSlots = Table<49>(kSystemAt);
constexpr drawfx::Iface kSystem{"TSSGSystem", 49, kSystemSlots.data(), false, drawfx::kTestTable};

// nTSSG::cFloorManager: 18 the changed tiles handed over (the handler notes the list), 19 a level's floor.
constexpr At kFloorsAt[] = {{18, {1, Ret::Int, true, "PassTiles", "a"}}, {19, {1, Ret::Int, true, "Floor", "u"}}};
constexpr auto kFloorsSlots = Table<20>(kFloorsAt);
constexpr drawfx::Iface kFloors{"FloorManager", 20, kFloorsSlots.data(), false, drawfx::kTestTable};

// nTSSG::cFloor: 20, 22, 23 the grid's sizes.
constexpr At kFloorAt[] = {{20, {0, Ret::Int, true, "Size20", ""}},
                           {22, {0, Ret::Int, true, "Size22", ""}},
                           {23, {0, Ret::Int, true, "Size23", ""}}};
constexpr auto kFloorSlots = Table<24>(kFloorAt);
constexpr drawfx::Iface kFloor{"Floor", 24, kFloorSlots.data(), false, drawfx::kTestTable};

// nTSSG::cWallManager: 25 the changed walls handed over (the handler notes the list).
constexpr At kWallManagerAt[] = {{25, {1, Ret::Int, true, "PassWalls", "a"}}};
constexpr auto kWallManagerSlots = Table<26>(kWallManagerAt);
constexpr drawfx::Iface kWallManager{"WallManager", 26, kWallManagerSlots.data(), false, drawfx::kTestTable};

// nTSSG::cILightMapManager: 23 GetScale(which), in st0.
constexpr At kLightsAt[] = {{23, {1, Ret::Float, true, "GetScale", "u"}}};
constexpr auto kLightsSlots = Table<24>(kLightsAt);
constexpr drawfx::Iface kLights{"LightMapManager", 24, kLightsSlots.data(), false, drawfx::kTestTable};

template <unsigned kSlot, class R, class... Args> R Virtual(void *self, Args... args)
{
    using F = R(__thiscall *)(void *, Args...);
    return (*reinterpret_cast<F *const *>(self))[kSlot](self, args...);
}
template <unsigned kSlot, class R = uint32_t, class... Args> R Slot0(void *o, Args... args)
{
    return Virtual<kSlot, R>(o, args...);
}

constexpr uint32_t kFrameworkCell = 0x10c9378, kServerCell = 0x10f519c, kAllocatorCell = 0x10c95d4,
                   kAllocatorGuard = 0x10c95d8, kSwitchesA = 0x100eb20, kSwitchesB = 0x10da3dc;
constexpr uint32_t kServerId = 0x04fa845b;
constexpr uint32_t kHolderIid = 0xab7cc174;
constexpr uint32_t kSize = sizeof(cDiffusionManager);
constexpr uint32_t kBufferTag = 0x7d000000, kPropsTag = 0x7c000000;
constexpr unsigned kFloorFakes = 6;

const float kNan = std::bit_cast<float>(0x7fc00000u);
const float kInf = std::bit_cast<float>(0x7f800000u);

struct Holder {
    uint32_t w0, w1;
    const int32_t *vbptr; // +8
    uint32_t pad;
    Fake unknown;
    uint8_t props[16];
};
const int32_t kHolderBases[3] = {0, int32_t(offsetof(Holder, unknown)) - 8, int32_t(offsetof(Holder, props)) - 8};

// What the calls are given (one block, registered, so its pointers log alike).
struct Buffers {
    float settings[12];
    float position[3];
    uint32_t out[3];
};

// The wall diffusion manager stood in (the 0x60 block 0x40be50 is given): words[0] its serial, words[1..] what the
// setters store; refs is its count (cRZRefCount: Release slot 1, AddRef slot 2).
struct TestWalls {
    void *const *vt;
    uint32_t words[22];
    int32_t refs;
};
static_assert(sizeof(TestWalls) == 0x60);

struct World {
    Fake framework, server, allocator, world, partitions, message, object, system, floors, wallManager, lights;
    Fake floor[kFloorFakes];
    Holder holder;
    Buffers buf;
    std::vector<DiffusionTile> tilesOut;
    std::vector<DiffusionWall> wallsOut;
    uint32_t queryMode;
    uint32_t gridsMade, gridsCopied, gridsDestroyed, wallsMade, wallsFreed, watchTag;
    uint32_t lcg;
    uint32_t Next() { return lcg = lcg * 1664525u + 1013904223u, lcg >> 8; }
};
World gw;
void *gObject; // the side's manager

uint8_t *Bytes(void *o) { return static_cast<uint8_t *>(o); }
uint32_t W(const void *o, uint32_t at) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(o) + at); }

// ---- the engine stood in ----

// A receiver: the wall manager's serial, or a grid's offset from the grid vector's start (no read: GetFloorColor's
// grid can be out of range), or 0.
uint32_t Recv(const void *self)
{
    if (!self) return 0;
    if (drawfx::P(self) == W(gObject, 0x68)) return 0x3a110000 | static_cast<const TestWalls *>(self)->words[0];
    return 0x6d000000 | ((drawfx::P(self) - W(gObject, 0x6c)) & 0xffffff);
}
void Engine(uint32_t rva, const void *self, std::initializer_list<uint32_t> args)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "engine";
    c.words = {rva, Recv(self)};
    c.words.insert(c.words.end(), args.begin(), args.end());
    drawfx::gLog.push_back(std::move(c));
}
// The words after an engine object's vtable (the grid's body, the wall manager's words).
uint32_t *Body(void *self) { return static_cast<uint32_t *>(self) + 1; }

template <uint32_t kRva> void __fastcall Pass(void *self, void *) { Engine(kRva, self, {}); }
template <uint32_t kRva> void __fastcall Data1(void *self, void *, uint32_t a) { Engine(kRva, self, {a}); }
template <uint32_t kRva> void __fastcall Data2(void *self, void *, uint32_t a, uint32_t b) { Engine(kRva, self, {a, b}); }
// A setter of one word: kept at the body's word kAt.
template <uint32_t kRva, unsigned kAt> void __fastcall SetWord(void *self, void *, uint32_t a)
{
    Engine(kRva, self, {a});
    if (self) Body(self)[kAt] = a;
}
// A colour setter (three floats): their bits noted and kept from kAt.
template <uint32_t kRva, unsigned kAt> void __fastcall SetColor(void *self, void *, const uint32_t *c)
{
    Engine(kRva, self, {drawfx::Ptr(c), c[0], c[1], c[2]});
    if (self) std::memcpy(Body(self) + kAt, c, 12);
}
// The grid's size (0x4098d0).
void __fastcall GridSize(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Engine(0x4098d0, self, {a, b, c});
    uint32_t *const body = Body(self);
    body[20] = a, body[21] = b, body[22] = c;
}
// The light maps read (the manager and a scale).
template <uint32_t kRva> void __fastcall Lights(void *self, void *, void *lights, uint32_t scale)
{
    Engine(kRva, self, {drawfx::Ptr(lights), scale});
}
void __fastcall BlendUnlit(void *self, void *, uint32_t bright, uint32_t dark, void *manager)
{
    Engine(0x40f800, self, {bright, dark, drawfx::Ptr(manager)});
}
// The collectors: 0-2 records pushed (from the generator, the receiver and the RVA).
template <uint32_t kRva> void __fastcall PushTiles(void *self, void *, std::vector<DiffusionTile> *out)
{
    Engine(kRva, self, {drawfx::Ptr(out), uint32_t(out->size())});
    for (uint32_t n = gw.Next() % 3; n; --n) out->push_back({gw.Next(), Recv(self), kRva, n});
}
template <uint32_t kRva> void __fastcall PushWalls(void *self, void *, std::vector<DiffusionWall> *out)
{
    Engine(kRva, self, {drawfx::Ptr(out), uint32_t(out->size())});
    for (uint32_t n = gw.Next() % 3; n; --n) out->push_back({gw.Next(), gw.Next(), Recv(self), kRva, n, 0x77});
}
void __fastcall PushBlobs(void *self, void *, std::vector<DiffusionBlob44> *out)
{
    Engine(0x40fa40, self, {drawfx::Ptr(out), uint32_t(out->size())});
    for (uint32_t n = gw.Next() % 4; n; --n) {
        DiffusionBlob44 b;
        for (uint32_t &w : b.words) w = gw.Next();
        out->push_back(b);
    }
}
void __fastcall TakeBlobs(void *self, void *, std::vector<DiffusionBlob44> *blobs)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "engine";
    c.words = {0x4065b0, Recv(self), drawfx::Ptr(blobs), uint32_t(blobs->size())};
    for (const DiffusionBlob44 &b : *blobs) c.words.insert(c.words.end(), std::begin(b.words), std::end(b.words));
    drawfx::gLog.push_back(std::move(c));
}
// The colour getters (by value: the out back).
uint32_t *__fastcall FloorColor(void *self, void *, uint32_t *out, const uint32_t *position)
{
    Engine(0x408e30, self, {drawfx::Ptr(out), drawfx::Ptr(position), position[0], position[1], position[2]});
    out[0] = Recv(self), out[1] = position[0] ^ position[1], out[2] = gw.Next();
    return out;
}
uint32_t *__fastcall WallColor(void *self, void *, uint32_t *out, uint32_t wall)
{
    Engine(0x410320, self, {drawfx::Ptr(out), wall});
    out[0] = Recv(self), out[1] = wall * 3, out[2] = gw.Next();
    return out;
}

// The grids: a test vtable whose slot 0 (the scalar deleting destructor, never freeing here) counts.
void *__fastcall GridDestroy(uint32_t *self, void *, uint32_t flags)
{
    ++gw.gridsDestroyed;
    if (flags) drawfx::Note("grid destroyed with flags", flags);
    return self;
}
void *const kGridVt[1] = {reinterpret_cast<void *>(&GridDestroy)};
void *__fastcall GridConstruct(uint32_t *self, void *)
{
    self[0] = drawfx::P(kGridVt);
    std::memset(self + 1, 0, 0x80);
    self[1] = 0x6000 + ++gw.gridsMade;
    return self;
}
void *__fastcall GridCopy(uint32_t *self, void *, const uint32_t *other)
{
    ++gw.gridsCopied;
    std::memcpy(self, other, 0x84);
    return self;
}

// The wall diffusion manager.
uint32_t __fastcall WallsRelease(TestWalls *self, void *)
{
    const int32_t refs = --self->refs;
    drawfx::Note("walls release", {self->words[0], uint32_t(refs)});
    if (refs == 0) {
        ++gw.wallsFreed;
        ::operator delete(self);
    }
    return uint32_t(refs);
}
uint32_t __fastcall WallsAddRef(TestWalls *self, void *)
{
    drawfx::Note("walls addref", {self->words[0], uint32_t(self->refs + 1)});
    return uint32_t(++self->refs);
}
void *const kWallsVt[3] = {nullptr, reinterpret_cast<void *>(&WallsRelease), reinterpret_cast<void *>(&WallsAddRef)};
void *__fastcall WallsConstruct(TestWalls *self, void *)
{
    self->vt = kWallsVt;
    std::memset(self->words, 0, sizeof self->words);
    self->words[0] = 0x100 + ++gw.wallsMade;
    self->refs = 0;
    drawfx::Note("walls made", self->words[0]);
    return self;
}

// The stopwatch: each watch tagged at its first Restart (its start word; the constructor zeroes it).
void __fastcall StandRestart(cRZStopwatch *watch, void *)
{
    uint32_t *const w = reinterpret_cast<uint32_t *>(watch);
    if (!w[2]) w[2] = ++gw.watchTag;
    drawfx::Note("restart", {w[2], w[0]});
}
void __fastcall StandStop(cRZStopwatch *watch, void *)
{
    const uint32_t *const w = reinterpret_cast<const uint32_t *>(watch);
    drawfx::Note("stop", {w[2], w[0]});
}

// ---- fakes ----

uint32_t SystemService(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Fake *answer = a[0] == kServerId ? &gw.server : nullptr;
    *out = answer;
    if (answer) ++answer->refs;
    return answer != nullptr;
}
uint32_t Allocate(Fake &, const uint32_t *a) { return drawfx::P(::operator new(a[0] ? a[0] : 1)); }
uint32_t FreeBlock(Fake &, const uint32_t *a)
{
    ::operator delete(reinterpret_cast<void *>(uintptr_t(a[0])));
    return 0;
}
uint32_t Post(Fake &, const uint32_t *a)
{
    void *const message = reinterpret_cast<void *>(uintptr_t(a[0]));
    drawfx::Note("posted", {Virtual<4, uint32_t>(message), Virtual<9, uint32_t>(message)}); // receiver: cRZMessage2Standard
    return 1;
}
uint32_t ObjectQuery(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    drawfx::Note("object query", {a[0], gw.queryMode});
    const bool give = gw.queryMode & 1, ok = (gw.queryMode & 2) != 0;
    if (give) {
        *out = &gw.holder;
        ++gw.holder.unknown.refs;
    }
    return ok;
}
// A level's floor: a fake, or none one time in 5.
uint32_t FloorOf(Fake &, const uint32_t *a)
{
    if (gw.Next() % 5 == 0) return 0;
    return drawfx::P(&gw.floor[(a[0] + 2) % kFloorFakes]);
}
template <class T> void NoteList(const char *label, const std::vector<T> *list)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = label;
    c.words.push_back(uint32_t(list->size()));
    for (const T &t : *list) c.words.insert(c.words.end(), std::begin(t.words), std::end(t.words));
    drawfx::gLog.push_back(std::move(c));
}
uint32_t PassTiles(Fake &, const uint32_t *a)
{
    NoteList("tiles handed over", reinterpret_cast<const std::vector<DiffusionTile> *>(uintptr_t(a[0])));
    return 0;
}
uint32_t PassWalls(Fake &, const uint32_t *a)
{
    NoteList("walls handed over", reinterpret_cast<const std::vector<DiffusionWall> *>(uintptr_t(a[0])));
    return 0;
}
uint32_t Scale(Fake &, const uint32_t *a)
{
    return std::bit_cast<uint32_t>(a[0] == 1 ? 0.75f : a[0] == 0 ? 1.5f : -2.0f);
}

// ---- the readers ----

void ReaderNote(const char *what, void *properties, const char *name, void *field, uint8_t *changed)
{
    drawfx::Call c;
    c.note = what;
    c.words = {drawfx::Ptr(properties), uint32_t(Bytes(field) - Bytes(gObject)), *changed};
    c.text = name;
    drawfx::gLog.push_back(std::move(c));
}
// The reader's effect: 0 none, 1 changed (value and flag), 2 the flag cleared, 3 the value alone.
uint32_t ReaderMode() { return gw.Next() % 4; }
uint32_t __cdecl StandReadFloat(void *properties, const char *name, float *value, uint8_t *changed)
{
    ReaderNote("read float", properties, name, value, changed);
    static const float kValues[] = {20.0f, 1.0f, 0.5f, 2.0f, 0.0f, -1.0f, 0.1f, kNan, 0.75f};
    const uint32_t mode = ReaderMode();
    if (mode == 1 || mode == 3) *value = kValues[gw.Next() % std::size(kValues)];
    if (mode == 1) *changed = 1;
    if (mode == 2) *changed = 0;
    return mode;
}
uint32_t __cdecl StandReadInt(void *properties, const char *name, int32_t *value, uint8_t *changed)
{
    ReaderNote("read int", properties, name, value, changed);
    static const int32_t kSteps[] = {60, 30, 0, 7, -4, 1, 4, 3, -1};
    const uint32_t mode = ReaderMode();
    if (mode == 1 || mode == 3) *value = kSteps[gw.Next() % std::size(kSteps)];
    if (mode == 1) *changed = 1;
    if (mode == 2) *changed = 0;
    return mode;
}
uint32_t __cdecl StandReadVector(void *properties, const char *name, float *value, uint8_t *changed)
{
    ReaderNote("read vector", properties, name, value, changed);
    static const float kValues[] = {0.25f, 1.0f, 0.5f, 0.0f, kNan, 0.07f};
    const uint32_t mode = ReaderMode();
    if (mode == 1 || mode == 3)
        for (int i = 0; i < 3; ++i) value[i] = kValues[gw.Next() % std::size(kValues)];
    if (mode == 1) *changed = 1;
    if (mode == 2) *changed = 0;
    return mode;
}

void *__cdecl StandWorld()
{
    drawfx::Note("world db", 0);
    return &gw.world;
}
void *__cdecl StandSystem()
{
    drawfx::Note("system", 0);
    return &gw.system;
}
void *__cdecl TestRzMalloc(uint32_t bytes) { return ::operator new(bytes ? bytes : 1); }
void __cdecl TestRzFree(void *p) { ::operator delete(p); }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

using shape::CodeOf;
using shape::CodePatch;

struct Stands {
    shape::StandIn readFloat{0xaf7190u, &nGZSceneGraph::ReadFloatProperty, &StandReadFloat};
    shape::StandIn readInt{0xaf7210u, &nGZSceneGraph::ReadIntProperty, &StandReadInt};
    shape::StandIn readVector{0xaf7290u, &nGZSceneGraph::ReadVector3Property, &StandReadVector};
    CodePatch world{0x20e0b2u, CodeOf(&StandWorld)};
    CodePatch system{0x20defbu, CodeOf(&StandSystem)};
    shape::StandIn rzMalloc{0x1be3b0u, &RZMemory::Malloc, &TestRzMalloc};
    shape::StandIn rzFree{0x1be3a1u, &RZMemory::Free, &TestRzFree};
    CodePatch gameNew{0x824632u, CodeOf(&GameNew)};
    CodePatch gameDelete{0x824624u, CodeOf(&GameDelete)};
    CodePatch gameFree{0x82540fu, CodeOf(&GameFree)};
    CodePatch restart{0x1c95ecu, CodeOf(&StandRestart)};
    shape::StandIn stop{0x1c9842u, &stopwatch_more::Stop, &StandStop};
    // The grids.
    shape::StandIn gridConstruct{0x4056d0u, &cFloorColorGrid::Construct, &GridConstruct};
    shape::StandIn gridCopy{0x3c42c0u, &cFloorColorGrid::CopyConstruct, &GridCopy};
    shape::StandIn gridSize{0x4098d0u, &cFloorColorGrid::SetSize, &GridSize};
    CodePatch gridSteps{0x239840u, CodeOf(&SetWord<0x239840, 1>)};
    CodePatch gridInside{0x4098a0u, CodeOf(&SetWord<0x4098a0, 2>)};
    CodePatch shared4099f0{0x4099f0u, CodeOf(&SetWord<0x4099f0, 3>)};
    CodePatch gridThreshold{0x409890u, CodeOf(&SetWord<0x409890, 4>)};
    shape::StandIn gridInsideColor{0x4098b0u, &cFloorColorGrid::SetInsideAmbientColor, &SetColor<0x4098b0, 5>};
    shape::StandIn gridOutsideColor{0x409a00u, &cFloorColorGrid::SetOutsideAmbientColor, &SetColor<0x409a00, 8>};
    shape::StandIn gridRebuild{0x40ac20u, &cFloorColorGrid::Rebuild, &Pass<0x40ac20>};
    shape::StandIn gridStep{0x405d70u, &cFloorColorGrid::Step, &Data1<0x405d70>};
    shape::StandIn gridLights{0x4060d0u, &cFloorColorGrid::ShadeLights, &Lights<0x4060d0>};
    shape::StandIn gridLights2{0x406300u, &cFloorColorGrid::ShadeChangedLights, &Lights<0x406300>};
    shape::StandIn gridBlobs{0x4065b0u, &cFloorColorGrid::TakeBlobs, &TakeBlobs};
    shape::StandIn gridTiles{0x405910u, &cFloorColorGrid::CollectTiles, &PushTiles<0x405910>};
    shape::StandIn gridTilesA{0x405ae0u, &cFloorColorGrid::CollectTilesA, &PushTiles<0x405ae0>};
    shape::StandIn gridTilesB{0x405a00u, &cFloorColorGrid::CollectTilesB, &PushTiles<0x405a00>};
    shape::StandIn gridReset{0x406f40u, &cFloorColorGrid::Reset, &Pass<0x406f40>};
    shape::StandIn gridMark{0x405bc0u, &cFloorColorGrid::Mark, &Data2<0x405bc0>};
    shape::StandIn gridColor{0x408e30u, &cFloorColorGrid::GetColor, &FloorColor};
    // The wall diffusion manager.
    // (Its constructor and setters are ours, lane 261110c: stood in on both sides. 0x4099f0, the threshold's, is
    // shared4099f0 above on the image's side.)
    shape::StandIn wallsConstruct{0x40be50u, &cWallDiffusionManager::Construct, &WallsConstruct};
    shape::StandIn wallSteps{0x20c6f0u, &cWallDiffusionManager::SetPropagationSteps, &SetWord<0x20c6f0, 11>};
    shape::StandIn wallInside{0x229f60u, &cWallDiffusionManager::SetInsideWallAmbient, &SetWord<0x229f60, 12>};
    shape::StandIn wallOutside{0x411b60u, &cWallDiffusionManager::SetOutsideWallAmbient, &SetWord<0x411b60, 13>};
    CodePatch wallThreshold{CodeOf(&cWallDiffusionManager::SetThreshold), CodeOf(&SetWord<0x4099f0, 3>)};
    shape::StandIn wallInsideColor{0x411b30u, &cWallDiffusionManager::SetInsideAmbientColor, &SetColor<0x411b30, 14>};
    shape::StandIn wallOutsideColor{0x411b70u, &cWallDiffusionManager::SetOutsideAmbientColor, &SetColor<0x411b70, 17>};
    // (Rebuild is ours, lane 261110h: stood in on both sides.)
    shape::StandIn wallRebuild{0x412240u, &cWallDiffusionManager::Rebuild, &Pass<0x412240>};
    // (The gradient, border, ambient and test-colour passes are ours, lane 261110d: stood in on both sides.)
    shape::StandIn wallShort{0x411c60u, &cWallDiffusionManager::GradientPass, &Pass<0x411c60>};
    shape::StandIn wall40f5a0{0x40f5a0u, &cWallDiffusionManager::BorderPass, &Pass<0x40f5a0>};
    shape::StandIn wall40d010{0x40d010u, &cWallDiffusionManager::AmbientPass, &Pass<0x40d010>};
    // (Step, the blob pass and CollectBlobs are ours, lane 261110i: stood in on both sides.)
    shape::StandIn wallStep{0x40c2e0u, &cWallDiffusionManager::Step, &Data1<0x40c2e0>};
    shape::StandIn wallBlend{0x40f6a0u, &cWallDiffusionManager::Blend, &Data2<0x40f6a0>};
    shape::StandIn wallBlendUnlit{0x40f800u, &cWallDiffusionManager::BlendUnlit, &BlendUnlit};
    shape::StandIn wall40f0f0{0x40f0f0u, &cWallDiffusionManager::DiffuseMarked, &Pass<0x40f0f0>};
    shape::StandIn wall40edb0{0x40edb0u, &cWallDiffusionManager::BlobPass, &Pass<0x40edb0>};
    shape::StandIn wallBlobs{0x40fa40u, &cWallDiffusionManager::CollectBlobs, &PushBlobs};
    shape::StandIn wall411c20{0x411c20u, &cWallDiffusionManager::TestColorPass, &Pass<0x411c20>};
    shape::StandIn wallLights{0x40ed70u, &cWallDiffusionManager::ApplyAllLights, &Lights<0x40ed70>};
    shape::StandIn wallLights2{0x40ed90u, &cWallDiffusionManager::ApplyChangedLights, &Lights<0x40ed90>};
    // (The collectors, Reset, Mark and GetColor are ours, lane 261110g: stood in on both sides.)
    shape::StandIn wallList{0x40c650u, &cWallDiffusionManager::CollectWalls, &PushWalls<0x40c650>};
    shape::StandIn wallList2{0x40c620u, &cWallDiffusionManager::CollectWalls2, &PushWalls<0x40c620>};
    shape::StandIn wallReset{0x40eff0u, &cWallDiffusionManager::Reset, &Pass<0x40eff0>};
    shape::StandIn wallMark{0x40c800u, &cWallDiffusionManager::Mark, &Data2<0x40c800>};
    shape::StandIn wallColor{0x410320u, &cWallDiffusionManager::GetColor, &WallColor};
};

// ---- the world ----

void SetLevels(std::mt19937 &rng)
{
    static const uint32_t kCounts[] = {0, 1, 2, 3, 4, 6, 2};
    static const int32_t kMins[] = {0, 0, -1, 1, -2};
    gw.world.Returns(90, kCounts[rng() % std::size(kCounts)]);
    gw.world.Returns(88, uint32_t(kMins[rng() % std::size(kMins)]));
}
void SetFloors(std::mt19937 &rng)
{
    static const uint32_t kSizes[] = {0, 1, 8, 64, 3, 0x40000};
    for (Fake &f : gw.floor)
        for (unsigned slot : {20u, 22u, 23u}) f.Returns(slot, kSizes[rng() % std::size(kSizes)]);
}
void SetSwitches(std::mt19937 &rng)
{
    uint32_t a = 0, b = 0;
    for (int i = 0; i < 4; ++i) a |= uint32_t(rng() % 3 != 0) << (8 * i), b |= uint32_t(rng() % 3 == 0) << (8 * i);
    *GameAddress<uint32_t>(kSwitchesA) = a;
    *GameAddress<uint32_t>(kSwitchesB) = b;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 7;
    gw.framework.Reset<kFramework>(460);
    gw.framework.On(5, &SystemService);
    gw.server.Reset<kServer>(461);
    gw.server.On(4, &Post);
    gw.allocator.Reset<kAllocator>(463);
    gw.allocator.On(3, &Allocate);
    gw.allocator.On(4, &FreeBlock);
    gw.world.Reset<kWorld>(464);
    gw.partitions.Reset<kUnknownFace>(465);
    gw.world.Returns(39, static_cast<const void *>(&gw.partitions));
    gw.message.Reset<kMessage>(466);
    gw.object.Reset<kUnknownFace>(467);
    gw.object.On(0, &ObjectQuery);
    gw.holder.unknown.Reset<kUnknownFace>(469);
    gw.holder.vbptr = kHolderBases;
    gw.holder.w0 = gw.holder.w1 = gw.holder.pad = 0;
    std::memset(gw.holder.props, 0, sizeof gw.holder.props);
    gw.system.Reset<kSystem>(470);
    gw.floors.Reset<kFloors>(471);
    gw.floors.On(18, &PassTiles);
    gw.floors.On(19, &FloorOf);
    gw.wallManager.Reset<kWallManager>(472);
    gw.wallManager.On(25, &PassWalls);
    gw.lights.Reset<kLights>(473);
    gw.lights.On(23, &Scale);
    gw.system.Returns(34, static_cast<const void *>(&gw.wallManager));
    gw.system.Returns(38, static_cast<const void *>(&gw.floors));
    gw.system.Returns(48, static_cast<const void *>(&gw.lights));
    for (unsigned i = 0; i < kFloorFakes; ++i) gw.floor[i].Reset<kFloor>(480 + i);
    gw.gridsMade = gw.gridsCopied = gw.gridsDestroyed = gw.wallsMade = gw.wallsFreed = gw.watchTag = 0;
    std::vector<DiffusionTile>().swap(gw.tilesOut);
    std::vector<DiffusionWall>().swap(gw.wallsOut);
    std::mt19937 rng(seed ^ 0x5bd1e995u);
    SetLevels(rng);
    SetFloors(rng);
    std::memset(&gw.buf, 0, sizeof gw.buf);
    drawfx::AddRange(&gw.buf, sizeof gw.buf, kBufferTag);
    drawfx::AddRange(gw.holder.props, sizeof gw.holder.props, kPropsTag);
    cells.Set(kFrameworkCell, &gw.framework);
    cells.Set(kServerCell, &gw.server);
    cells.Set(kAllocatorCell, &gw.allocator);
    cells.Set(kAllocatorGuard, 0x80000000u);
    cells.Set(kSwitchesA, 0x01010101u);
    cells.Set(kSwitchesB, 0u);
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.framework, &gw.server, &gw.allocator, &gw.world, &gw.partitions, &gw.message, &gw.object,
                    &gw.holder.unknown, &gw.system, &gw.floors, &gw.wallManager, &gw.lights})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.floor) c.words.push_back(uint32_t(f.refs));
    c.words.insert(c.words.end(), {gw.gridsMade, gw.gridsCopied, gw.gridsDestroyed, gw.wallsMade, gw.wallsFreed});
    drawfx::gLog.push_back(std::move(c));
}

// ---- the object ----

// Each word's kind: the vtables left out; the message ids' head, the world and partitions, the walls and the three
// vectors' pointers strict.
constexpr char kObjectKinds[] = "---uuauuaa"
                                "uuuuuuuuuuuuuuuu"
                                "aaaa"
                                "u"
                                "aaaaaa";
static_assert(sizeof(kObjectKinds) - 1 == kSize / 4);

void NoteObject(void *o)
{
    difftest::NotCounted bookkeeping;
    {
        drawfx::Call c;
        c.note = "object";
        drawfx::Words(c.words, o, kSize / 4, kObjectKinds);
        drawfx::gLog.push_back(std::move(c));
    }
    {
        drawfx::Call c;
        c.note = "message ids";
        for (uint32_t id : *reinterpret_cast<const std::set<uint32_t> *>(Bytes(o) + 0x14)) c.words.push_back(id);
        drawfx::gLog.push_back(std::move(c));
    }
    {
        drawfx::Call c;
        c.note = "grids";
        for (const DiffusionGrid &g : *reinterpret_cast<const std::vector<DiffusionGrid> *>(Bytes(o) + 0x6c)) {
            c.words.push_back(uint32_t(g.mVTable == kGridVt));
            drawfx::Words(c.words, g.mBody, 0x80 / 4, "u");
        }
        drawfx::gLog.push_back(std::move(c));
    }
    NoteList("tiles", reinterpret_cast<const std::vector<DiffusionTile> *>(Bytes(o) + 0x7c));
    NoteList("wall list", reinterpret_cast<const std::vector<DiffusionWall> *>(Bytes(o) + 0x88));
    if (const auto *walls = reinterpret_cast<const TestWalls *>(uintptr_t(W(o, 0x68)))) {
        drawfx::Call c;
        c.note = "walls";
        drawfx::Words(c.words, walls->words, 22, "u");
        c.words.push_back(uint32_t(walls->refs));
        drawfx::gLog.push_back(std::move(c));
    }
    drawfx::Note("counts", {gw.gridsMade, gw.gridsCopied, gw.gridsDestroyed, gw.wallsMade, gw.wallsFreed});
}

struct Run {
    difftest::Context &ctx;
    bool original;
    void *o;

    void Query(std::mt19937 &rng)
    {
        static const uint32_t kIids[] = {cIDiffusionManager::kIid, cDiffusionManager::kClass, 1u, 0x0bf077fdu, 0u,
                                         kHolderIid};
        const uint32_t iid = kIids[rng() % std::size(kIids)];
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        void **const where = rng() % 6 == 0 ? nullptr : &out;
        const uint32_t face = (rng() % 3) * 4;
        const bool found = Virtual<0, uint8_t>(Bytes(o) + face, iid, where) != 0;
        drawfx::Note("query", {iid, face, uint32_t(found), found && where ? drawfx::Ptr(out) : 0x5a5a, W(o, 0xc)});
        if (found) Slot0<2>(o);
    }
    void Message(std::mt19937 &rng)
    {
        static const uint32_t kTypes[] = {cDiffusionManager::kLevels, cDiffusionManager::kWorldLoaded,
                                          cDiffusionManager::kChanged, cDiffusionManager::kWorldLoaded,
                                          cDiffusionManager::kChanged, 0x12345678};
        static const uint32_t kData[] = {1, 4, 0, 2, 5, 0x101, 0x104};
        const uint32_t type = kTypes[rng() % std::size(kTypes)];
        gw.message.Returns(4, type);
        gw.message.Returns(9, kData[rng() % std::size(kData)]);
        gw.message.Returns(32, static_cast<const void *>(&gw.object)); // never null (the original reads through it)
        gw.queryMode = rng() % 4;
        // receiver: our cIGZMessageTarget2 face (+4), slot 3 DoMessage
        const uint32_t r = Virtual<3, uint8_t>(Bytes(o) + 4, static_cast<void *>(&gw.message));
        drawfx::Note("message", {type, r});
    }
    template <class T> void Junk(std::vector<T> &out, std::mt19937 &rng)
    {
        for (uint32_t n = rng() % 3; n; --n) {
            T t;
            for (uint32_t &w : t.words) w = rng();
            out.push_back(t);
        }
    }
    void NoteColor(const char *label, const float *r)
    {
        drawfx::Note(label, {uint32_t(r == reinterpret_cast<const float *>(gw.buf.out)), gw.buf.out[0], gw.buf.out[1],
                             gw.buf.out[2]});
    }

    void Step(std::mt19937 &rng)
    {
        static const uint32_t kArgs[] = {0, 1, 2, 7, 0xffffffffu, 0x100};
        const unsigned what = rng() % 50;
        if (what < 2) {
            drawfx::Note("init", Slot0<3, uint8_t>(o));
        } else if (what < 3) {
            drawfx::Note("shutdown", Slot0<4, uint8_t>(o));
        } else if (what < 6) {
            Slot0<5, void>(o);
        } else if (what < 7) {
            Slot0<6, void>(o);
        } else if (what < 9) {
            static const float kValues[] = {0.5f, 1.0f, 0.0f, -1.0f, kNan, kInf, 0.137f, 2.5f};
            for (float &f : gw.buf.settings) f = kValues[rng() % std::size(kValues)];
            Slot0<7, void>(o, static_cast<const float *>(gw.buf.settings));
        } else if (what < 13) {
            if (rng() % 3 == 0) Bytes(o)[0x1d] = uint8_t(rng() % 2);
            Slot0<12, void>(o, kArgs[rng() % std::size(kArgs)]);
        } else if (what < 16) {
            if (rng() % 3 == 0) Bytes(o)[0x1d] = uint8_t(rng() % 2);
            Slot0<13, void>(o, kArgs[rng() % std::size(kArgs)]);
        } else if (what < 18) {
            Slot0<14, void>(o);
        } else if (what < 20) {
            Slot0<15, void>(o);
        } else if (what < 24) {
            const unsigned slot = 16 + (what - 20);
            if (slot % 2 == 0) {
                Junk(gw.tilesOut, rng);
                if (slot == 16) Slot0<16, void>(o, &gw.tilesOut);
                else Slot0<18, void>(o, &gw.tilesOut);
                NoteList("tiles out", &gw.tilesOut);
            } else {
                Junk(gw.wallsOut, rng);
                if (slot == 17) Slot0<17, void>(o, &gw.wallsOut);
                else Slot0<19, void>(o, &gw.wallsOut);
                NoteList("walls out", &gw.wallsOut);
            }
        } else if (what < 26) {
            Slot0<20, void>(o);
        } else if (what < 27) {
            Slot0<21, void>(o);
        } else if (what < 29) {
            Slot0<22, void>(o, kArgs[rng() % std::size(kArgs)], kArgs[rng() % std::size(kArgs)]);
        } else if (what < 32) {
            static const float kCoords[] = {0.0f, 0.5f, 3.25f, -1.5f, 63.9f, 1e9f, kNan};
            gw.buf.position[0] = kCoords[rng() % std::size(kCoords)];
            gw.buf.position[1] = kCoords[rng() % std::size(kCoords)];
            const int32_t base = int32_t(W(o, 0x78));
            const uint32_t count = (W(o, 0x70) - W(o, 0x6c)) / 0x84;
            const int32_t level = rng() % 8 == 0 ? int32_t(rng() % 5) - 2 : base + int32_t(rng() % (count + 1));
            std::memcpy(&gw.buf.position[2], &level, 4);
            for (uint32_t &w : gw.buf.out) w = 0xa5a5a5a5u;
            NoteColor("floor color", Slot0<23, float *>(o, reinterpret_cast<float *>(gw.buf.out),
                                                         static_cast<const float *>(gw.buf.position)));
        } else if (what < 34) {
            for (uint32_t &w : gw.buf.out) w = 0xa5a5a5a5u;
            NoteColor("wall color", Slot0<24, float *>(o, reinterpret_cast<float *>(gw.buf.out), uint32_t(rng() % 50)));
        } else if (what < 40) {
            Message(rng);
        } else if (what < 42) {
            Query(rng);
        } else if (what < 43) {
            drawfx::Note("addref, release", {Slot0<1>(o), Slot0<2>(o)});
        } else if (what < 45) {
            SetLevels(rng);
        } else if (what < 47) {
            SetSwitches(rng);
        } else {
            SetFloors(rng);
        }
    }
};

void RunLife(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *const o = ::operator new(kSize);
    for (uint32_t i = 0; i < kSize; ++i) Bytes(o)[i] = uint8_t(rng());
    if (original) ctx.Original<void *(__thiscall *)(void *)>(0x3c4140)(o);
    else cDiffusionManager::Construct(o, nullptr);
    drawfx::Note("made", drawfx::Ptr(o));
    gObject = o;
    Run run{ctx, original, o};
    NoteObject(o);
    Slot0<1>(o);
    const unsigned steps = 4 + rng() % 60;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        run.Step(rng);
        NoteObject(o);
    }
    sgfx::MarkStep(ctx, "delete");
    if (rng() % 2) {
        drawfx::Note("released", Slot0<2>(o));
    } else {
        // receiver: the +8 rz::cRZUnknown face, slot 3 (the deleting destructor's +8 thunk)
        void *const result = Virtual<3, void *>(Bytes(o) + 8, 1u);
        drawfx::Note("deleted", uint32_t(Bytes(result) - Bytes(o)));
    }
    std::vector<DiffusionTile>().swap(gw.tilesOut);
    std::vector<DiffusionWall>().swap(gw.wallsOut);
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the constructor 0x3c4140, the destructor 0x3c4730 with the deleting destructor 0x3c4840 and its +8 thunk
// 0x3c482c, Init 0x3c5c90, Shutdown 0x3c6050, Start 0x3c5dc0, Stop 0x3c5e60, SetSettings 0x3c6010, Diffuse 0x3c5470,
// DiffuseUnlit 0x3c5710, ReadLightMaps 0x3c51b0, ReadLightMapsAndCollect 0x3c52a0, CollectTiles 0x3c5ba0, CollectWalls
// 0x3c5c10, CollectTiles2 0x3c5b10, CollectWalls2 0x3c5b80, PassChanges 0x3c5d20, Reset 0x3c5410, Mark 0x3c4ab0,
// GetFloorColor 0x3c5d60, GetWallColor 0x3c5da0, DoMessage 0x3c5920, ApplySettings 0x3c60f0, ResizeGrids 0x3c5eb0 and
// ReadSettings 0x3c6220.
DIFF_TEST(SG_DiffusionManagerLife, 0x3c4140)
{
    Stands stands;
    sgfx::CheckSides(
        ctx, "diffusion manager", 2000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, original, rng);
            ++ctx.cases;
        });
}
