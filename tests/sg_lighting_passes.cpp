// Differential tests for nTSSG::cLightingManager's sky-sample passes and a wall side's corners (lane 261109k, scenegraph
// LG4b; src/sg/cLightingManager.cpp): SampleRoomTiles 0x2d8410, SampleWallPortals 0x2d8e00 and WallCorners 0x2d8120.
// Hook shape: the original runs on the same object image as ours, each case at the x87's 53-bit and 24-bit precision.
//
// The world is tests/sg_lighting_world.h's object with the sky samples over gSky (0 to 4 of 4, so growing them never
// reallocates; their reference fields null or fake lerp sets that log AddRef / Release), plus logging fakes: the world
// at +0x20 (22 the inner wall count, 51 / 52 a wall's name record, 55 / 56 its walls, 76 a room's floor tiles appended to
// a list on the allocator service, 81 the rooms into a std::set<short>), the terrain at +0x1c (40, 43 a quadrant's floor
// object or none), the listing object at +0x18 (23 / 24 the light at a point / on a wall, written to the buffer or a
// pool), the colour service 0x20e006 gives (slot 3) and the room behind a wall side 0x7f2340 (0, 0xffff, 1 to 3, eax's
// upper half noise). Rooms stay in -1 .. 3 (the original's resize of a larger room would reallocate, of a lower one
// throw). The game's grey 0xffd994 and the zero vector are drawn per case and restored. Walls come from a small grid,
// repeated, reversed or zero length. WallCorners runs over stand-ins for GroundHeightAt (0x7e4680, both sides) and the
// world 0x20e0b2 with its roof (slots 37 / 22, or none): heights from a pool with whole, half and huge values, NaNs and
// infinities, so the span's rounding meets ties, exact whole numbers and the 2^23 edge.
//
// Compared: the event log, the object's bytes, the light sets, the sky samples (all four) and, for WallCorners, the
// canary-filled quad. NaNs planted in the colours, the lights, mField1D4, mField1CC, the grey and the samples' sums.
#include "sg_lighting_world.h"

#include "app/AppServices2.h"
#include "code_patch.h"
#include "rz/GameAllocator.h"
#include "simantics/ServiceRefVector.h"
#include "world/WorldHeights.h"
#include "world/cWorldDB.h"

#include <set>
#include <vector>

namespace {

struct Wall {
    float v[6];
};
struct Tile {
    int32_t x, y, level;
    uint32_t kind; // the low byte the quadrant bits
};
using RoomSet = std::set<int16_t, std::less<int16_t>, GZServiceAllocator<int16_t>>;

uint32_t HashWords(const void *p, unsigned n, uint32_t salt)
{
    uint32_t h = salt, w;
    for (unsigned i = 0; i < n; ++i) std::memcpy(&w, static_cast<const uint8_t *>(p) + 4 * i, 4), h = Mix(h, w);
    return h;
}
void LogWords(const void *p, unsigned n)
{
    uint32_t w;
    for (unsigned i = 0; i < n; ++i) std::memcpy(&w, static_cast<const uint8_t *>(p) + 4 * i, 4), gLog.push_back(w);
}
float Level(int32_t level) { return Fl(uint32_t(level)); }

// --- the fakes ---------------------------------------------------------------------------------------------------------

struct FakeWorld {
    void **vtable;
    std::vector<Wall> walls;
    std::vector<int16_t> rooms;
    uint32_t inner, tileSalt, nameSalt;
} gWorld;
void *gWorldVT[90];

const char kWallA[] = "wall", kWallB[] = "brick", kWallC[] = "x", kEmpty[] = "";
const char *const kNames[] = {nullptr, kEmpty, kWallA, kWallB, kWallC, kWallA};
struct NameRecord {
    const char *name;
    uint32_t pad[3];
} gNameRecord;

uint32_t __fastcall InnerSlot(FakeWorld *self, void *)
{
    gLog.push_back(0x22);
    return self->inner;
}
const NameRecord *NameOf(FakeWorld *self, uint32_t tag, NameRecord *out, const Wall *wall)
{
    gLog.push_back(tag), LogWords(wall, 6);
    const uint32_t h = HashWords(wall, 6, self->nameSalt + tag);
    // the name in the caller's record, or in a pool record returned instead (the caller's then holds a decoy)
    const char *const name = kNames[h % std::size(kNames)];
    if (h / 8 % 2) {
        out->name = name;
        return out;
    }
    out->name = kWallC;
    gNameRecord.name = name;
    return &gNameRecord;
}
const NameRecord *__fastcall Name51Slot(FakeWorld *self, void *, NameRecord *out, const Wall *wall) { return NameOf(self, 0x51, out, wall); }
const NameRecord *__fastcall Name52Slot(FakeWorld *self, void *, NameRecord *out, const Wall *wall) { return NameOf(self, 0x52, out, wall); }
uint32_t __fastcall WallCountSlot(FakeWorld *self, void *)
{
    gLog.push_back(0x55);
    return uint32_t(self->walls.size());
}
Wall *__fastcall WallSlot(FakeWorld *self, void *, Wall *out, uint32_t i)
{
    gLog.push_back(0x56), gLog.push_back(i);
    *out = self->walls[i % self->walls.size()];
    return out;
}
void Append(ServiceBlockVector *list, const Tile &t)
{
    auto *first = static_cast<Tile *>(list->mFirst), *last = static_cast<Tile *>(list->mLast);
    if (list->mLast == list->mEnd) {
        const uint32_t n = uint32_t(last - first), capacity = n ? 2 * n : 4;
        auto *block = static_cast<Tile *>(as2::AllocateViaAllocatorService(capacity * sizeof(Tile), 0));
        if (n) std::memcpy(block, first, n * sizeof(Tile));
        if (first) as2::FreeViaAllocatorService(first, 0);
        list->mFirst = block, list->mLast = block + n, list->mEnd = block + capacity;
        last = block + n;
    }
    *last = t;
    list->mLast = last + 1;
}
ServiceBlockVector *__fastcall RoomTilesSlot(FakeWorld *self, void *, uint32_t room, ServiceBlockVector *list, uint32_t flag)
{
    const uint32_t before = uint32_t((P(list->mLast) - P(list->mFirst)) / sizeof(Tile));
    gLog.push_back(0x76), gLog.push_back(room), gLog.push_back(flag), gLog.push_back(before);
    const uint32_t h = Mix(self->tileSalt, room);
    const uint32_t count = h % 5 == 0 ? 0 : h % 5;
    for (uint32_t k = 0; k < count; ++k) {
        std::mt19937 gen(Mix(h, k));
        Tile t;
        t.x = int32_t(gen() % 6) - 1, t.y = int32_t(gen() % 6) - 1, t.level = gen() % 3 ? 1 : int32_t(gen() % 4);
        t.kind = gen() % 4 ? gen() : (gen() & 0xffffff00u) | 15u;
        Append(list, t);
    }
    return list;
}
void __fastcall RoomsSlot(FakeWorld *self, void *, uint32_t flag, RoomSet *rooms)
{
    gLog.push_back(0x81), gLog.push_back(flag), gLog.push_back(uint32_t(rooms->size()));
    for (const int16_t r : self->rooms) rooms->insert(r);
}

struct FakeTerrain2 {
    void **vtable;
    uint32_t salt;
} gTerrain2;
void *gTerrain2VT[45];
uint8_t gFloorObjects[4][8];

void __fastcall Terrain40Slot(FakeTerrain2 *self, void *, uint32_t *out)
{
    gLog.push_back(0x40);
    out[0] = self->salt, out[1] = ~self->salt;
}
void **__fastcall FloorObjectSlot(FakeTerrain2 *self, void *, void **out, const uint8_t *quadrant)
{
    gLog.push_back(0x43), LogWords(quadrant, 3), gLog.push_back(quadrant[12]);
    const uint32_t h = Mix(HashWords(quadrant, 3, self->salt), quadrant[12]);
    *out = h % 4 == 0 ? nullptr : gFloorObjects[h % 4];
    return out;
}

// The listing object's lights: the buffer filled, or a pool entry returned (so the result, not the buffer, is read).
struct FakeLit {
    void **vtable;
    uint32_t salt, nanPick;
    float pool[4][3];
} gLit;
void *gLitVT[25];

const float *Light(FakeLit *self, float *out, uint32_t h)
{
    std::mt19937 gen(h);
    float *const to = gen() % 4 ? out : self->pool[gen() % 4];
    for (int i = 0; i < 3; ++i) to[i] = gen() % 6 ? Nice(gen, 2.0f) : gen() % 2 ? 0.0f : -1.0f;
    if (self->nanPick % 16 == h % 16) to[h / 16 % 3] = NaN(gen);
    return to;
}
const float *__fastcall LightAtSlot(FakeLit *self, void *, float *out, const float *point)
{
    gLog.push_back(0x23), LogWords(point, 3);
    return Light(self, out, HashWords(point, 3, self->salt));
}
const float *__fastcall WallLightSlot(FakeLit *self, void *, float *out, const Wall *wall)
{
    gLog.push_back(0x24), LogWords(wall, 6);
    return Light(self, out, HashWords(wall, 6, self->salt + 1));
}

// The colour service (0x20e006): slot 3 (kind, key, &colour) writes the colour, leaves it, or plants a NaN.
struct FakeColours {
    void **vtable;
    uint32_t salt, nanPick;
} gColours;
void *gColoursVT[5];

uint32_t __fastcall ColourSlot(FakeColours *self, void *, uint32_t kind, const void *key, float *colour)
{
    gLog.push_back(0x3), gLog.push_back(kind), gLog.push_back(P(key)), LogWords(colour, 3);
    if (kind == 2) gLog.push_back(HashWords(key, 1, 0)); // the name's first bytes
    std::mt19937 gen(Mix(Mix(self->salt, kind), P(key)));
    if (gen() % 5 == 0) return 0;
    for (int i = 0; i < 3; ++i) colour[i] = gen() % 5 ? float(gen() % 256) / 255.0f : Nice(gen, 1.0f);
    if (self->nanPick % 8 == gen() % 8) colour[gen() % 3] = NaN(gen);
    return 1;
}
void *__cdecl StandColours()
{
    gLog.push_back(0x20e006);
    return &gColours;
}

uint32_t gRoomSalt;
uint32_t __cdecl StandRoom(const float *wall, int32_t flag)
{
    gLog.push_back(0x7f2340), LogWords(wall, 6), gLog.push_back(uint32_t(flag));
    const uint32_t h = HashWords(wall, 6, gRoomSalt);
    static const uint32_t kRooms[] = {0, 0xffff, 1, 2, 3, 1, 2, 3};
    return (h & 0xffff0000u) | kRooms[h / 7 % 8];
}

// A lerp set in a sample's reference fields: slot 1 Release, slot 2 AddRef, both logged.
struct FakeLerp {
    void **vtable;
} gLerps[2];
void *gLerpVT[3];
void __fastcall LerpRelease(FakeLerp *self, void *) { gLog.push_back(0x1e1), gLog.push_back(uint32_t(self - gLerps)); }
void __fastcall LerpAddRef(FakeLerp *self, void *) { gLog.push_back(0x1e2), gLog.push_back(uint32_t(self - gLerps)); }

void FillPassFakes()
{
    FillFakes();
    gWorld.vtable = gWorldVT, gTerrain2.vtable = gTerrain2VT, gLit.vtable = gLitVT, gColours.vtable = gColoursVT;
    gWorldVT[22] = reinterpret_cast<void *>(&InnerSlot);
    gWorldVT[51] = reinterpret_cast<void *>(&Name51Slot);
    gWorldVT[52] = reinterpret_cast<void *>(&Name52Slot);
    gWorldVT[55] = reinterpret_cast<void *>(&WallCountSlot);
    gWorldVT[56] = reinterpret_cast<void *>(&WallSlot);
    gWorldVT[76] = reinterpret_cast<void *>(&RoomTilesSlot);
    gWorldVT[81] = reinterpret_cast<void *>(&RoomsSlot);
    gTerrain2VT[40] = reinterpret_cast<void *>(&Terrain40Slot);
    gTerrain2VT[43] = reinterpret_cast<void *>(&FloorObjectSlot);
    gLitVT[23] = reinterpret_cast<void *>(&LightAtSlot);
    gLitVT[24] = reinterpret_cast<void *>(&WallLightSlot);
    gColoursVT[3] = reinterpret_cast<void *>(&ColourSlot);
    gLerpVT[1] = reinterpret_cast<void *>(&LerpRelease);
    gLerpVT[2] = reinterpret_cast<void *>(&LerpAddRef);
    for (auto &l : gLerps) l.vtable = gLerpVT;
}

// The image's data the passes read: the grey and the zero vector, drawn per case and put back when the test ends.
struct PassGlobals {
    float *grey = GameAddress<float>(0xffd994);
    float *zero = GameAddress<float>(0x10d9424);
    float savedGrey[3], savedZero[3];
    PassGlobals() { std::memcpy(savedGrey, grey, sizeof savedGrey), std::memcpy(savedZero, zero, sizeof savedZero); }
    ~PassGlobals() { std::memcpy(grey, savedGrey, sizeof savedGrey), std::memcpy(zero, savedZero, sizeof savedZero); }
    PassGlobals(const PassGlobals &) = delete;
    PassGlobals &operator=(const PassGlobals &) = delete;
};

Wall FreshWall(std::mt19937 &gen)
{
    Wall w;
    const auto coord = [&] { return gen() % 8 ? float(gen() % 5) : float(gen() % 9) * 0.5f; };
    w.v[0] = coord(), w.v[1] = coord();
    // Now and then an infinite y at both ends: dy is inf - inf, a NaN, and so is the length (the walls stay ordered).
    if (gen() % 16 == 0) w.v[1] = gen() % 2 ? HUGE_VALF : -HUGE_VALF;
    w.v[2] = Level(gen() % 4 ? 1 : int32_t(gen() % 3));
    static const int32_t kSteps[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {1, -1}, {0, 0}, {2, 0}, {-1, 2}};
    const auto &s = kSteps[gen() % 9];
    w.v[3] = w.v[0] + float(s[0]), w.v[4] = w.v[1] + float(s[1]);
    w.v[5] = gen() % 8 ? w.v[2] : Level(int32_t(gen() % 3));
    return w;
}

// One case's world, drawn from the seed: the object (Build), the fakes, the samples, the globals.
void SetUpCase(uint32_t seed, uint32_t salt)
{
    std::mt19937 gen(seed * 7919u + salt);
    Build(gen());
    gLog.clear();
    Put(0x18, P(&gLit)), Put(0x1c, P(&gTerrain2)), Put(0x20, gen() % 16 ? P(&gWorld) : 0u);
    // the sky samples' reference fields: null, or fake lerp sets at +0x78 / +0x88
    for (auto &s : gSky) {
        for (uint32_t i = 0x60 / 4; i < 0x8c / 4; ++i) s[i] = Fl(0);
        s[0x60 / 4] = Fl(gen() % 4 ? gen() % 3 : gen());
        s[0x64 / 4] = Fl(gen() % 4 ? gen() % 3 : gen());
        if (gen() % 4 == 0) s[0x78 / 4] = Fl(P(&gLerps[0]));
        if (gen() % 4 == 0) s[0x88 / 4] = Fl(P(&gLerps[1]));
        if (gen() % 8 == 0) s[gen() % 0x18] = NaN(gen);
    }
    float *const scale = reinterpret_cast<float *>(gObject + 0x1d4), *const light = reinterpret_cast<float *>(gObject + 0x1cc);
    *scale = gen() % 4 ? Nice(gen, 1.0f) : gen() % 2 ? 0.0f : 1.0f;
    *light = gen() % 4 ? Nice(gen, 1.0f) : 0.5f;
    float *const grey = GameAddress<float>(0xffd994), *const zero = GameAddress<float>(0x10d9424);
    for (int i = 0; i < 3; ++i) grey[i] = gen() % 3 ? 0.5f : Nice(gen, 1.0f), zero[i] = gen() % 2 ? 0.0f : Nice(gen, 1.0f);
    float *const places[] = {scale, light, grey, grey + 1, grey + 2};
    MaybeNaN(gen, places, gen() % 3 ? 0 : 5);
    // the world
    gWorld.walls.clear();
    const uint32_t walls = gen() % 8 ? gen() % 12 : 0;
    for (uint32_t i = 0; i < walls; ++i) {
        const unsigned roll = gWorld.walls.empty() ? 0 : gen() % 8;
        const Wall earlier = gWorld.walls.empty() ? Wall{} : gWorld.walls[gen() % gWorld.walls.size()];
        if (roll == 5) gWorld.walls.push_back(earlier);
        else if (roll == 6) gWorld.walls.push_back({{earlier.v[3], earlier.v[4], earlier.v[5], earlier.v[0], earlier.v[1], earlier.v[2]}});
        else gWorld.walls.push_back(FreshWall(gen));
    }
    gWorld.inner = gen() % 4 ? gen() % (walls + 1) : gen() % 3 ? 0 : walls + 1 + gen() % 3;
    gWorld.rooms.clear();
    const uint32_t rooms = gen() % 6;
    for (uint32_t i = 0; i < rooms; ++i) gWorld.rooms.push_back(int16_t(int32_t(gen() % 5) - 1));
    if (gen() % 8 == 0) gWorld.rooms.assign(1 + gen() % 2, int16_t(-1)); // the largest -1: a resize to none
    gWorld.tileSalt = gen(), gWorld.nameSalt = gen();
    gTerrain2.salt = gen();
    gLit.salt = gen(), gLit.nanPick = gen() % 3 ? ~0u : gen();
    for (auto &p : gLit.pool) for (auto &f : p) f = Fl(0xdeadbeefu);
    gColours.salt = gen(), gColours.nanPick = gen() % 3 ? ~0u : gen();
    gRoomSalt = gen();
}

Blob EndCase()
{
    Blob o = End(nullptr, 0);
    o.push_back(0xfeed);
    const auto *s = reinterpret_cast<const uint32_t *>(gSky);
    for (size_t i = 0; i < sizeof(gSky) / 4; ++i) o.push_back(s[i]);
    for (const auto &p : gLit.pool) for (float f : p) o.push_back(U(f));
    return o;
}

template <class Original, class Ours>
void PassTest(difftest::Context &ctx, uint32_t rva, const char *what, Original original, Ours ours)
{
    svcfx::AllocatorScope allocator;
    PassGlobals globals;
    FillPassFakes();
    shape::CodePatch colours(0x20e006, reinterpret_cast<const void *>(&StandColours));
    shape::CodePatch room(0x7f2340, reinterpret_cast<const void *>(&StandRoom));
    AtBothPrecisions(ctx, what, 4000, [&](uint32_t seed, const char *label) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            SetUpCase(seed, rva);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original();
            else ours();
            outcome[side] = EndCase();
        }
        ++ctx.cases;
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// --- WallCorners' stand-ins --------------------------------------------------------------------------------------------

constexpr double kHeights[] = {0.0, 1.0, 2.5, 3.0, -1.0, 0.25, 0.75, 1.5, 2.0000002, 1e7, 8388608.5, 8388607.0,
                               16777215.0, -2.5, 4.4, 1.0 / 3.0, 0.4999999701976776, 1e30};
uint32_t gHeightSalt, gRoofSalt, gHeightNaN;
double HeightFor(uint32_t h)
{
    std::mt19937 gen(h);
    const unsigned roll = gen() % 16;
    if (gHeightNaN && roll == 0) return std::nan("");
    if (gHeightNaN && roll == 1) return gen() % 2 ? HUGE_VAL : -HUGE_VAL;
    if (roll < 6) return double(Nice(gen, 4.0f)) + (gen() % 2 ? 0.0 : 1e-9);
    return kHeights[gen() % std::size(kHeights)];
}
double __cdecl StandGround(const nTSWorld::WorldPoint *point)
{
    gLog.push_back(0x7e4680), LogWords(point, 3);
    return HeightFor(HashWords(point, 3, gHeightSalt));
}

struct FakeRoofs {
    void **vtable;
} gRoofs;
void *gRoofsVT[40];
bool __fastcall UnderRoofSlot(FakeRoofs *, void *, const nTSWorld::WorldPoint *point)
{
    gLog.push_back(0x37), LogWords(point, 3);
    return HashWords(point, 3, gRoofSalt) % 3 != 0;
}
double __fastcall RoofHeightSlot(FakeRoofs *, void *, const nTSWorld::WorldPoint *point, uint32_t flag)
{
    gLog.push_back(0x22), LogWords(point, 3), gLog.push_back(flag);
    return HeightFor(HashWords(point, 3, gRoofSalt + 1));
}

struct FakeCornerWorld {
    void **vtable;
    void *roofs;
} gCornerWorld;
void *gCornerWorldVT[40];
void *__fastcall RoofsSlot(FakeCornerWorld *self, void *)
{
    gLog.push_back(0x36);
    return self->roofs;
}
void *__cdecl StandWorld()
{
    gLog.push_back(0x20e0b2);
    return &gCornerWorld;
}

} // namespace

DIFF_TEST(sg_lighting_passes_room_tiles, 0x2d8410)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d8410);
    PassTest(ctx, 0x2d8410, "room tiles", [&] { original(gObject); }, [] { Ours()->SampleRoomTiles(); });
}

DIFF_TEST(sg_lighting_passes_wall_portals, 0x2d8e00)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d8e00);
    PassTest(ctx, 0x2d8e00, "wall portals", [&] { original(gObject); }, [] { Ours()->SampleWallPortals(); });
}

DIFF_TEST(sg_lighting_passes_wall_corners, 0x2d8120)
{
    using Fn = void(__cdecl *)(const float *, float *);
    auto original = ctx.Original<Fn>(0x2d8120);
    gRoofs.vtable = gRoofsVT, gCornerWorld.vtable = gCornerWorldVT;
    gRoofsVT[37] = reinterpret_cast<void *>(&UnderRoofSlot);
    gRoofsVT[22] = reinterpret_cast<void *>(&RoofHeightSlot);
    gCornerWorldVT[36] = reinterpret_cast<void *>(&RoofsSlot);
    shape::CodePatch world(0x20e0b2, reinterpret_cast<const void *>(&StandWorld));
    shape::StandIn ground{0x7e4680u, &worldheights::GroundHeightAt, &StandGround};
    AtBothPrecisions(ctx, "wall corners", 30000, [&](uint32_t seed, const char *label) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0x2d8120u);
            gHeightSalt = gen(), gRoofSalt = gen(), gHeightNaN = gen() % 4 == 0;
            gCornerWorld.roofs = gen() % 4 ? &gRoofs : nullptr;
            float wall[6];
            for (int i : {0, 1, 3, 4}) wall[i] = gen() % 4 ? float(int32_t(gen() % 9) - 2) * 0.5f : Nice(gen, 8.0f);
            wall[2] = Level(int32_t(gen() % 5) - 1), wall[5] = gen() % 4 ? wall[2] : Level(int32_t(gen() % 5) - 1);
            float quad[14];
            for (auto &f : quad) f = Fl(0xcdcdcdcdu);
            gLog.clear();
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original(wall, quad);
            else cLightingManager::WallCorners(wall, quad);
            outcome[side] = gLog;
            outcome[side].push_back(0xeeee);
            for (float f : quad) outcome[side].push_back(U(f));
            for (float f : wall) outcome[side].push_back(U(f));
        }
        ++ctx.cases;
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}
