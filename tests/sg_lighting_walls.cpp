// Differential tests for nTSSG::cLightingManager's lightmap walls and rooms (lane 261108e, scenegraph LG2;
// src/sg/cLightingManager.cpp): slots 39, 40, 42, 43 and 44, LightWallShaded (0x2de610) and SegmentLess (0x257cf0).
// Hook shape: the original runs on the same object image as ours, each case at the x87's 53-bit and 24-bit precision.
//
// The world is tests/sg_lighting_tiles.h's (light sets, ambient maps, terrain, sun grids, lightmaps, textures, the
// normals service), plus the world object at +0x20 (slots 55 / 56 / 50: its walls, one, a wall's kind; 76: a room's
// floor tiles, appended to a list the allocator service holds; 79: a tile's room), the terrain's slots 53 and 55 (is a
// tile shown), the lightmaps' slot 21 (a wall side's texel rectangle), a texture's slot 26, a listing object at +0x18
// (slots 17 and 19: walls; 18: tiles) and stand-ins both sides call: 0x7f2340 (the image's room behind a wall side:
// 0 half the time, the upper half of eax noise) and 0x2d8120 (WallCorners, ours and the image's: a wall side's
// corners, x and y in [1, 3] so every texel's sun-grid cell lies in the grid, z anywhere).
// Walls lie on a small integer grid, now and then repeated, reversed, or moved by 1, 31, 32, 33 ulps (SegmentLess's
// 32-ulp test), zero length, at other levels.
//
// Compared: the fakes' and stand-ins' event log, the whole texture buffer, the object's and light sets' bytes, and per
// test the up normal or the sky samples. NaNs planted one or two at a time (scales, mAmbientScale1F0, light-set words,
// corner z, grid cells, the falloff), never in what picks a grid cell (x, y, mScale: the original reads unchecked).
#include "sg_lighting_tiles.h"

#include "app/AppServices2.h"
#include "code_patch.h"
#include "simantics/ServiceRefVector.h"

#include <algorithm>
#include <vector>

namespace {

struct Wall {
    float v[6];
};
static_assert(sizeof(Wall) == 24, "Wall layout");

uint32_t HashWords(const float *v, unsigned n, uint32_t salt)
{
    uint32_t h = salt;
    for (unsigned i = 0; i < n; ++i) h = Mix(h, U(v[i]));
    return h;
}

void LogWords(const float *v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) gLog.push_back(U(v[i]));
}

// k ulps away from f on the ordered integer line (through the signed zeros).
float Ulps(float f, int32_t k)
{
    const uint32_t b = U(f);
    const int32_t ordered = (b >> 31) ? -int32_t(b & 0x7fffffffu) : int32_t(b);
    const int32_t moved = ordered + k;
    return Fl(moved < 0 ? (0x80000000u | uint32_t(-moved)) : uint32_t(moved));
}

float Level(int32_t level) { return Fl(uint32_t(level)); }

// Up to 3 ulps from 3 * (2k + 1) / 128 (exact), k < 192: a third of it is near a ramp byte's rounding tie.
float NearTie(std::mt19937 &gen)
{
    const float base = float(int32_t(6 * (gen() % 192) + 3)) / 128.0f;
    return Ulps(base, int32_t(gen() % 7) - 3);
}

Wall FreshWall(std::mt19937 &gen)
{
    Wall w;
    const auto coord = [&] { return gen() % 8 ? float(gen() % 5) : float(gen() % 9) * 0.5f; };
    w.v[0] = coord(), w.v[1] = coord();
    const int32_t level = gen() % 4 ? 0 : int32_t(gen() % 3);
    w.v[2] = Level(level);
    static const int32_t kSteps[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {1, -1}, {0, 0}, {2, 0}};
    const auto &s = kSteps[gen() % 8];
    w.v[3] = w.v[0] + float(s[0]), w.v[4] = w.v[1] + float(s[1]);
    w.v[5] = gen() % 8 ? w.v[2] : Level(int32_t(gen() % 3));
    return w;
}

Wall Reversed(const Wall &w) { return {{w.v[3], w.v[4], w.v[5], w.v[0], w.v[1], w.v[2]}}; }

Wall Near(std::mt19937 &gen, Wall w)
{
    static const int32_t kUlps[] = {1, 31, 32, 33, -1, -31, -32, -33, 0};
    for (int i : {0, 1, 3, 4})
        if (gen() % 2) w.v[i] = Ulps(w.v[i], kUlps[gen() % 9]);
    return w;
}

std::vector<Wall> WallList(std::mt19937 &gen, uint32_t count)
{
    std::vector<Wall> walls;
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned roll = walls.empty() ? 0 : gen() % 10;
        const Wall &earlier = walls.empty() ? Wall{} : walls[gen() % walls.size()];
        switch (roll) {
        case 5: walls.push_back(earlier); break;
        case 6: walls.push_back(Reversed(earlier)); break;
        case 7: walls.push_back(Near(gen, earlier)); break;
        case 8: walls.push_back(Near(gen, Reversed(earlier))); break;
        default: walls.push_back(FreshWall(gen)); break;
        }
    }
    return walls;
}

// --- the stand-ins over 0x7f2340 and 0x2d8120 -----------------------------------------------------------------------

uint32_t gRoomSalt, gRooms = 1; // rooms 0 .. gRooms - 1
uint32_t RoomOf(uint32_t h) { return h % 2 ? 0u : h / 2 % gRooms; }
uint32_t __cdecl RoomStand(const float *wall, int32_t flag)
{
    gLog.push_back(0x7f2340), LogWords(wall, 6), gLog.push_back(uint32_t(flag));
    const uint32_t h = HashWords(wall, 6, gRoomSalt);
    return (h & 0xffff0000u) | RoomOf(h);
}

uint32_t gCornerSalt, gCornerNaN = ~0u; // a z (2, 5, 8 or 11) every side gets as a NaN, else ~0
void __cdecl CornersStand(const float *wall, float *q)
{
    gLog.push_back(0x2d8120), LogWords(wall, 6);
    std::mt19937 gen(HashWords(wall, 6, gCornerSalt));
    for (uint32_t k = 0; k < 12; ++k) q[k] = k % 3 == 2 ? Nice(gen, 2.0f) : 1.0f + float(gen() % 129) / 64.0f;
    if (gCornerNaN < 12) q[gCornerNaN] = Fl(0x7fc0beefu);
}

// --- the world at +0x20, the listing object at +0x18, the extra slots ---------------------------------------------

struct FakeWorld {
    void **vtable;
    std::vector<Wall> walls;
    uint32_t kindSalt, tileSalt;
} gWorld;
void *gWorldVT[80];

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
int32_t __fastcall WallKindSlot(FakeWorld *self, void *, const float *wall)
{
    gLog.push_back(0x50), LogWords(wall, 6);
    static const int32_t kKinds[] = {3, 3, 0, 1, 2, 3, 5, -1};
    return kKinds[HashWords(wall, 6, self->kindSalt) % 8];
}

LitTile TileFrom(uint32_t h)
{
    static const uint8_t kKinds[] = {15, 15, 15, 3, 6, 12, 0, 9};
    std::mt19937 gen(h);
    LitTile t;
    t.x = int32_t(gen() % 4), t.y = int32_t(gen() % 4), t.level = gen() % 3 ? 0 : int32_t(1 + gen() % 3);
    t.kind = (gen() & 0xffffff00u) | kKinds[gen() % 8];
    return t;
}

// Slot 76 appends (never clears: the caller's reset shows), growing the block through the allocator service.
void Append(ServiceBlockVector *list, const LitTile &t)
{
    auto *first = static_cast<LitTile *>(list->mFirst), *last = static_cast<LitTile *>(list->mLast);
    if (list->mLast == list->mEnd) {
        const uint32_t n = uint32_t(last - first), capacity = n ? 2 * n : 4;
        auto *block = static_cast<LitTile *>(as2::AllocateViaAllocatorService(capacity * sizeof(LitTile), 0));
        if (n) std::memcpy(block, first, n * sizeof(LitTile));
        if (first) as2::FreeViaAllocatorService(first, 0);
        list->mFirst = block, list->mLast = block + n, list->mEnd = block + capacity;
        last = block + n;
    }
    *last = t;
    list->mLast = last + 1;
}
ServiceBlockVector *__fastcall RoomTilesSlot(FakeWorld *self, void *, uint32_t room, ServiceBlockVector *list, uint32_t flag)
{
    const uint32_t before = uint32_t((P(list->mLast) - P(list->mFirst)) / sizeof(LitTile));
    gLog.push_back(0x76), gLog.push_back(room), gLog.push_back(flag), gLog.push_back(before);
    const uint32_t h = Mix(self->tileSalt, room);
    const uint32_t count = h % 7 == 0 ? 0 : h % (room ? 6 : 9);
    for (uint32_t k = 0; k < count; ++k) Append(list, TileFrom(Mix(h, k)));
    return list;
}
uint32_t __fastcall TileRoomSlot(FakeWorld *, void *, const LitTile *tile, uint32_t a, uint32_t b)
{
    gLog.push_back(0x79), gLog.push_back(uint32_t(tile->x)), gLog.push_back(uint32_t(tile->y)), gLog.push_back(uint32_t(tile->level));
    gLog.push_back(a), gLog.push_back(b);
    const uint32_t h = Mix(Mix(Mix(gRoomSalt, uint32_t(tile->x)), uint32_t(tile->y)), tile->kind);
    return (h & 0xffff0000u) | RoomOf(h);
}

uint32_t gShownSalt;
bool __fastcall Shown53Slot(FakeTerrain *, void *, const int32_t *tile, uint32_t kind, uint32_t flag)
{
    gLog.push_back(0x53), gLog.push_back(uint32_t(tile[0])), gLog.push_back(uint32_t(tile[1])), gLog.push_back(uint32_t(tile[2]));
    gLog.push_back(kind), gLog.push_back(flag);
    return Mix(Mix(gShownSalt, uint32_t(tile[0])), uint32_t(tile[1]) + 0x53) % 3 != 0;
}
bool __fastcall Shown55Slot(FakeTerrain *, void *, const int32_t *tile, uint32_t kind, uint32_t flag)
{
    gLog.push_back(0x155), gLog.push_back(uint32_t(tile[0])), gLog.push_back(uint32_t(tile[1])), gLog.push_back(uint32_t(tile[2]));
    gLog.push_back(kind), gLog.push_back(flag);
    return Mix(Mix(gShownSalt, uint32_t(tile[0])), uint32_t(tile[1]) + 0x55) % 2 != 0;
}

bool __fastcall WallRectSlot(FakeLightmaps *self, void *, const float *wall, uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1,
                             int32_t *texture)
{
    gLog.push_back(0x21), LogWords(wall, 6);
    const Rect &r = self->rects[self->next++ % 8];
    *x0 = r.x0, *y0 = r.y0, *x1 = r.x1, *y1 = r.y1, *texture = r.texture;
    return r.found;
}

uint32_t __fastcall LockSlot(FakeTexture *self, void *, uint32_t level)
{
    gLog.push_back(0x26), gLog.push_back(uint32_t(self - gTextures)), gLog.push_back(level);
    return 0x2600u;
}

struct FakeListing {
    void **vtable;
    std::vector<Wall> walls17, walls19;
    std::vector<LitTile> tiles18;
} gListing;
void *gListingVT[23];
void __fastcall Walls17Slot(FakeListing *self, void *, std::vector<Wall> *out)
{
    gLog.push_back(0x17), gLog.push_back(uint32_t(out->size()));
    *out = self->walls17;
}
void __fastcall Tiles18Slot(FakeListing *self, void *, std::vector<LitTile> *out)
{
    gLog.push_back(0x18), gLog.push_back(uint32_t(out->size()));
    *out = self->tiles18;
}
void __fastcall Walls19Slot(FakeListing *self, void *, std::vector<Wall> *out)
{
    gLog.push_back(0x19), gLog.push_back(uint32_t(out->size()));
    *out = self->walls19;
}

// The sky samples (rooms) and light records slots 43 and 44 hand AddSkyLight.
constexpr uint32_t kSampleFloats = 0x8c / 4;
constexpr uint32_t kMaxSamples = 13;
alignas(16) float gSamples[kMaxSamples * kSampleFloats];
alignas(16) uint8_t gLights[16 * 0x6c];
constexpr uint32_t kZeroVector = 0x10d9424;

void FillAllFakes()
{
    FillFakes();
    gNormalsVT[22] = reinterpret_cast<void *>(&NormalAt);
    gTerrainVT[53] = reinterpret_cast<void *>(&Shown53Slot);
    gTerrainVT[55] = reinterpret_cast<void *>(&Shown55Slot);
    gTerrainVT[61] = reinterpret_cast<void *>(&TileKindSlot);
    gTerrainVT[62] = reinterpret_cast<void *>(&TileHeightSlot);
    gTextureVT[9] = reinterpret_cast<void *>(&TexelsSlot);
    gTextureVT[26] = reinterpret_cast<void *>(&LockSlot);
    gTextureVT[27] = reinterpret_cast<void *>(&HeightSlot);
    gTextureVT[28] = reinterpret_cast<void *>(&PitchSlot);
    gLightmapsVT[19] = reinterpret_cast<void *>(&RectSlot);
    gLightmapsVT[21] = reinterpret_cast<void *>(&WallRectSlot);
    gLightmapsVT[22] = reinterpret_cast<void *>(&TextureSlot);
    gLightmapsVT[23] = reinterpret_cast<void *>(&ScaleSlot);
    gServiceVT[39] = reinterpret_cast<void *>(&NormalsSourceSlot);
    gWorldVT[50] = reinterpret_cast<void *>(&WallKindSlot);
    gWorldVT[55] = reinterpret_cast<void *>(&WallCountSlot);
    gWorldVT[56] = reinterpret_cast<void *>(&WallSlot);
    gWorldVT[76] = reinterpret_cast<void *>(&RoomTilesSlot);
    gWorldVT[79] = reinterpret_cast<void *>(&TileRoomSlot);
    gListingVT[17] = reinterpret_cast<void *>(&Walls17Slot);
    gListingVT[18] = reinterpret_cast<void *>(&Tiles18Slot);
    gListingVT[19] = reinterpret_cast<void *>(&Walls19Slot);
}

// The image's globals the cases set: the up normal, the cached service, the shadow-mask switch, the zero vector;
// put back when the test ends.
struct Globals {
    float *up = GameAddress<float>(0xffd918);
    void **service = GameAddress<void *>(kServiceGlobal);
    uint8_t *masks = GameAddress<uint8_t>(0xffdeac);
    float *zero = GameAddress<float>(kZeroVector);
    float savedUp[3], savedZero[3];
    void *savedService;
    uint8_t savedMasks;
    Globals() : savedService(*service), savedMasks(*masks)
    {
        std::memcpy(savedUp, up, sizeof(savedUp)), std::memcpy(savedZero, zero, sizeof(savedZero));
    }
    ~Globals()
    {
        std::memcpy(up, savedUp, sizeof(savedUp)), std::memcpy(zero, savedZero, sizeof(savedZero));
        *service = savedService, *masks = savedMasks;
        gTerrainVT[62] = reinterpret_cast<void *>(&TerrainHeightSlot);
    }
    Globals(const Globals &) = delete;
    Globals &operator=(const Globals &) = delete;
};

// One case's world (both sides set up from the same draw).
struct WallCase {
    uint32_t world, grids, texture, salt, nanPick, sampleSeed, lightsSeed;
    FakeTexture textures[3];
    Rect rects[8];
    float scale, falloff, upNormal[3];
    uint32_t width, sunRows;
    double tileHeight;
    bool perVertex, hasWorld, drawHeights;
    uint8_t shadeTiles, shadeWalls, masks, flag44;
    uint32_t roomSalt, rooms, cornerSalt, kindSalt, tileSalt, shownSalt, samples;
    std::vector<Wall> walls, walls17, walls19;
    std::vector<LitTile> tiles18;
    int32_t lightCount;
    unsigned mode, zeroKind;

    WallCase(uint32_t seed, uint32_t rva)
    {
        std::mt19937 gen(seed * 7919u + rva);
        world = gen(), grids = gen(), texture = gen(), salt = gen(), nanPick = gen(), sampleSeed = gen(), lightsSeed = gen();
        for (unsigned i = 0; i < 3; ++i) {
            static const uint32_t kPitches[] = {16, 24, 40};
            const uint32_t pitch = kPitches[gen() % 3];
            textures[i] = {gTextureVT, gTexture + 16 * i, pitch * 4 + (gen() % 4 ? 0 : gen() % 4), 14 + gen() % 35};
        }
        for (auto &r : rects) {
            // inside every texture: x1 <= 15 words, y1 <= 14 rows; 4 x 8 (the fixed steps) and tall ones (kind 3's skip)
            static const uint32_t kShapes[][2] = {{4, 8}, {4, 8}, {4, 8}, {1, 13}, {2, 8}, {1, 4}, {0, 5}, {5, 0},
                                                  {1, 1}, {2, 3}, {8, 8}, {13, 13}, {3, 12}, {1, 2}, {2, 1}, {4, 4}};
            const auto &s = kShapes[gen() % 16];
            r.x0 = gen() % 3, r.y0 = gen() % 2;
            r.x1 = r.x0 + s[0], r.y1 = r.y0 + s[1];
            if (gen() % 16 == 0) std::swap(r.x0, r.x1), r.x0 += 1;
            if (gen() % 24 == 0) std::swap(r.y0, r.y1), r.y0 += 1;
            r.texture = gen() % 3 ? int32_t(gen() % 2) : int32_t(gen() % 5);
            r.found = gen() % 6 != 0;
        }
        scale = gen() % 4 ? float(1 + gen() % 64) / 32.0f : Nice(gen, 2.0f);
        static const float kFalloffs[] = {0.5f, 1.0f, 2.0f, 0.25f, 0.0f, -0.5f, 4.0f, 0.1f};
        falloff = kFalloffs[gen() % 8];
        for (auto &f : upNormal) f = gen() % 4 ? float(int32_t(gen() % 2600) - 1300) / 1000.0f : 0.0f;
        if (gen() % 3 == 0) upNormal[0] = 0.0f, upNormal[1] = 0.0f, upNormal[2] = 1.0f;
        static const uint32_t kWidths[] = {13, 16, 24, 40};
        width = kWidths[gen() % 4];
        sunRows = 1 + gen() % 12;
        tileHeight = gen() % 2 ? 0.0 : double(int32_t(gen() % 3000) - 1000) / 1000.0;
        perVertex = gen() % 4 != 0;
        hasWorld = gen() % 16 != 0;
        drawHeights = gen() % 5 == 0;
        shadeTiles = uint8_t(gen() % 3 ? 1 + gen() % 255 : 0);
        shadeWalls = uint8_t(gen() % 4 ? 1 + gen() % 255 : 0);
        masks = uint8_t(gen() % 2 ? 0 : 1 + gen() % 255);
        flag44 = uint8_t(gen() % 8 ? 1 + gen() % 255 : 0);
        roomSalt = gen(), cornerSalt = gen(), kindSalt = gen(), tileSalt = gen(), shownSalt = gen();
        samples = gen() % 10 ? 1 + gen() % kMaxSamples : 0;
        rooms = samples ? samples : 1 + gen() % 6;
        walls = WallList(gen, gen() % 8 ? gen() % 11 : 0);
        walls17 = WallList(gen, gen() % 8 ? gen() % 9 : 0);
        walls19 = WallList(gen, gen() % 8 ? gen() % 9 : 0);
        tiles18.resize(gen() % 6 ? gen() % 9 : 0);
        for (auto &t : tiles18) t = TileFrom(gen());
        lightCount = gen() % 7 ? int32_t(gen() % 14) : -int32_t(1 + gen() % 3);
        mode = gen() % 12;
        zeroKind = gen() % 12;
    }

    // The world for one side (the image's globals: SetGlobalsFor).
    void SetUp()
    {
        Build(world);
        gLog.clear();
        gTexelNaN = ~0u;
        FillTexture(texture);
        {
            std::mt19937 g(grids);
            for (uint32_t i = 0; i < kGridCells; ++i) {
                gHeights[i] = GridValue(g);
                // Every fifth other value is up to 3 ulps from 3 * (2k + 1) / 128: slot 39's debug view shows value / 3,
                // whose ramp bytes then sit at a rounding tie, so the division's last bit counts (x * (1 / 3.0f)
                // differs there for about 1 in 30 of them).
                gGround[i] = g() % 6 ? (g() % 5 ? GridValue(g) : NearTie(g)) : gHeights[i];
            }
        }
        Put(kSunMap + 0x00, P(gGround)), Put(kSunMap + 0x0c, P(gGround)), Put(kSunMap + 0x18, P(gHeights));
        Put(kSunMap + 0x24, width), Put(kSunMap + 0x28, sunRows);
        PutF(0x2ac, falloff);
        gPerVertex = perVertex;
        gTileHeight = tileHeight;
        gNormals = {gNormalsVT, salt, {~0u, ~0u}, false};
        for (unsigned i = 0; i < 3; ++i) gTextures[i] = textures[i];
        gLightmaps.vtable = gLightmapsVT, gLightmaps.scale = scale, gLightmaps.next = 0;
        std::memcpy(gLightmaps.rects, rects, sizeof(rects));
        gService.vtable = gServiceVT;
        gWorld.vtable = gWorldVT, gWorld.walls = walls, gWorld.kindSalt = kindSalt, gWorld.tileSalt = tileSalt;
        gListing.vtable = gListingVT, gListing.walls17 = walls17, gListing.walls19 = walls19, gListing.tiles18 = tiles18;
        gRoomSalt = roomSalt, gRooms = rooms, gCornerSalt = cornerSalt, gCornerNaN = ~0u, gShownSalt = shownSalt;
        Put(0x18, P(&gListing)), Put(0x20, hasWorld ? P(&gWorld) : 0u), Put(0x24, P(&gLightmaps));
        gObject[0x44] = flag44, gObject[0x5c] = shadeTiles, gObject[0x5d] = shadeWalls, gObject[0x68] = drawHeights;
        {
            std::mt19937 fill(sampleSeed);
            for (auto &f : gSamples) f = Nice(fill, 4.0f);
        }
        {
            std::mt19937 fill(lightsSeed);
            for (auto &b : gLights) b = uint8_t(fill());
            for (unsigned k = 0; k < 16; ++k) {
                const int16_t index = int16_t(fill() % 4 ? fill() % (samples + 2) : uint32_t(int32_t(fill() % 3) - 1));
                std::memcpy(gLights + k * 0x6c + 0x68, &index, 2);
            }
        }
        Put(0x1f4, P(gSamples)), Put(0x1f8, P(gSamples) + samples * 0x8c), Put(0x1fc, P(gSamples) + sizeof(gSamples));
        float *zero = GameAddress<float>(kZeroVector);
        auto &set = const_cast<cLightSet &>(Current());
        if (zeroKind == 0) zero[0] = set.mSkyColour.x, zero[1] = set.mSkyColour.y, zero[2] = set.mSkyColour.z; // no sky light
        else zero[0] = zero[1] = zero[2] = 0.0f;
        if (zeroKind == 1) set.mSkyReach = -std::fabs(set.mSkyReach); // every unlisted sample weighs 1
    }
};

void SetGlobalsFor(const WallCase &c)
{
    *GameAddress<void *>(kServiceGlobal) = &gService;
    *GameAddress<uint8_t>(0xffdeac) = c.masks;
    float *up = GameAddress<float>(0xffd918);
    up[0] = c.upNormal[0], up[1] = c.upNormal[1], up[2] = c.upNormal[2];
}

Blob SamplesEnd()
{
    Blob o = TextureEnd();
    o.push_back(0xcccc);
    for (float f : gSamples) o.push_back(U(f));
    return o;
}

} // namespace

// 0x257cf0 SegmentLess: the wall-segment sets' strict order. Pairs: random, equal, one start coordinate moved by up to
// 33 ulps (the 32-ulp test, through the signed zeros), other levels, other ends, NaNs; both orders of each pair.
DIFF_TEST(sg_lighting_segment_less, 0x257cf0)
{
    using LessFn = bool(__thiscall *)(void *, const float *, const float *);
    auto less = ctx.Original<LessFn>(0x257cf0);
    AtBothPrecisions(ctx, "segment less", 20000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x257cf0u);
        Wall a = FreshWall(gen);
        if (gen() % 4 == 0)
            for (int i : {0, 1, 3, 4}) a.v[i] = Nice(gen, 4.0f);
        if (gen() % 6 == 0) a.v[0] = gen() % 2 ? 0.0f : -0.0f, a.v[1] = gen() % 2 ? 0.0f : -0.0f;
        Wall b = a;
        switch (gen() % 8) {
        case 0: b = FreshWall(gen); break;
        case 1: break;
        case 2: b = Near(gen, a); break;
        case 3: b = Near(gen, a), b.v[5] = Level(int32_t(gen() % 3) - 1); break;
        case 4: b.v[2] = Level(int32_t(gen() % 3) - 1); break;
        case 5: b.v[3] = Ulps(b.v[3], int32_t(gen() % 5) - 2), b.v[4] = Ulps(b.v[4], int32_t(gen() % 5) - 2); break;
        case 6: b = Reversed(a); break;
        default: b.v[0] = Ulps(a.v[0], int32_t(gen() % 80) - 40), b.v[1] = Ulps(a.v[1], int32_t(gen() % 80) - 40); break;
        }
        const unsigned nan = gen() % 8;
        if (nan == 0) a.v[gen() % 2 ? 0 : 3 + gen() % 2] = NaN(gen);
        else if (nan == 1) b.v[gen() % 2] = NaN(gen), a.v[4] = NaN(gen);
        const bool swap = gen() % 2 != 0;
        const Wall &x = swap ? b : a, &y = swap ? a : b;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Wall p = x, q = y;
            ctx.Mark(side ? "ours" : "original");
            const bool r = side == 0 ? less(nullptr, p.v, q.v) : cLightingManager::SegmentLess(p.v, q.v);
            outcome[side].push_back(r ? 1u : 0u);
            for (int i = 0; i < 6; ++i) outcome[side].push_back(U(p.v[i])), outcome[side].push_back(U(q.v[i]));
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2de610 LightWallShaded: a wall side's texels, each its corner-interpolated point pushed out by normal / 8, shaded
// by its depth under the sun grid or lit by the plain ambient colour. Normals along the axes, diagonals and anywhere
// in the plane (z now and then); rectangles 4 x 8 (the fixed half-texel steps) and others, spans 0 and 1, swapped.
DIFF_TEST(sg_lighting_wall_shaded, 0x2de610)
{
    FillAllFakes();
    shape::StandIn corners{0x2d8120u, &cLightingManager::WallCorners, &CornersStand};
    using ShadedFn = void(__thiscall *)(void *, const float *, const float *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int32_t, float);
    auto shaded = ctx.Original<ShadedFn>(0x2de610);
    Globals globals;
    AtBothPrecisions(ctx, "wall shaded", 3000, [&](uint32_t seed, const char *label) {
        const WallCase c(seed, 0x2de610u);
        std::mt19937 gen(seed ^ 0x2de6u);
        const Wall wall = FreshWall(gen);
        float normal[3];
        static const float kNormals[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {0.70710677f, 0.70710677f}, {0.70710677f, -0.70710677f}};
        if (gen() % 3) normal[0] = kNormals[gen() % 6][0], normal[1] = kNormals[gen() % 6][1];
        else normal[0] = float(int32_t(gen() % 2000) - 1000) / 1000.0f, normal[1] = float(int32_t(gen() % 2000) - 1000) / 1000.0f;
        normal[2] = gen() % 4 ? 0.0f : Nice(gen, 1.0f);
        const Rect r = c.rects[gen() % 8];
        const unsigned layout = gen() % 2; // bottom-up as the callers pass it, or top-down
        const FakeTexture &t = c.textures[gen() % 3];
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            WallCase setup = c;
            setup.SetUp();
            SetGlobalsFor(c);
            float n[3] = {normal[0], normal[1], normal[2]};
            float s = c.scale;
            const int32_t pitch = int32_t(t.pitchBytes >> 2);
            uint32_t *pixels = layout ? t.texels + (t.height - 1) * uint32_t(pitch) : t.texels;
            const int32_t stride = layout ? -pitch : pitch;
            {
                std::mt19937 pick(c.nanPick);
                auto &set = const_cast<cLightSet &>(Current());
                float *f1f0 = reinterpret_cast<float *>(gObject + 0x1f0), *fall = reinterpret_cast<float *>(gObject + 0x2ac);
                switch (c.mode) {
                case 0: s = NaN(pick), *f1f0 = NaN(pick); break;
                case 1: n[2] = NaN(pick), set.mAmbient.x = NaN(pick); break;
                case 2: gCornerNaN = 2 + 3 * (pick() % 4), *fall = NaN(pick); break;
                case 3: gHeights[pick() % kGridCells] = NaN(pick), set.mLights[0].mIntensity = NaN(pick); break;
                default: {
                    float *places[] = {&set.mLights[0].mColour.x, &set.mLights[1].mIntensity, &set.mAmbient.y, &set.mTint.x,
                                       f1f0, &s, fall, &n[2], &gHeights[pick() % kGridCells]};
                    MaybeNaN(pick, places, 9);
                }
                }
            }
            Wall w = wall;
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) shaded(gObject, w.v, n, r.x0, r.y0, r.x1, r.y1, pixels, stride, s);
            else Ours()->LightWallShaded(w.v, n, r.x0, r.y0, r.x1, r.y1, pixels, stride, s);
            outcome[side] = TextureEnd();
            LogWords(w.v, 6), LogWords(n, 3);
            outcome[side].insert(outcome[side].end(), gLog.end() - 9, gLog.end());
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

namespace {

// The NaN plantings the slot tests share: scales, mAmbientScale1F0, light-set words (the sun direction among them),
// corner z, grid cells, the falloff; for the sky tests the sky colour and reach too.
void PlantWallNaNs(const WallCase &c, bool sky)
{
    std::mt19937 pick(c.nanPick);
    auto &set = const_cast<cLightSet &>(Current());
    float *f1f0 = reinterpret_cast<float *>(gObject + 0x1f0), *fall = reinterpret_cast<float *>(gObject + 0x2ac);
    float *up = GameAddress<float>(0xffd918);
    switch (c.mode) {
    case 0: gLightmaps.scale = NaN(pick), *f1f0 = NaN(pick); break;
    case 1: (&set.mLights[0].mDirection.x)[pick() % 3] = NaN(pick); break;
    case 2: gCornerNaN = 2 + 3 * (pick() % 4), *fall = NaN(pick); break;
    case 3:
        if (sky) (&set.mSkyColour.x)[pick() % 3] = NaN(pick), gSamples[pick() % (kMaxSamples * kSampleFloats)] = NaN(pick);
        else up[pick() % 3] = NaN(pick), set.mAmbient.x = NaN(pick);
        break;
    default: {
        float *places[] = {&set.mLights[0].mColour.x, &set.mLights[0].mDirection.y, &set.mAmbient.y, &set.mTint.x, f1f0,
                           &gLightmaps.scale, fall, &gHeights[pick() % kGridCells], sky ? &set.mSkyReach : &up[2],
                           sky ? &set.mSkyColour.y : &set.mLights[1].mIntensity};
        MaybeNaN(pick, places, 10);
    }
    }
}

} // namespace

// 0x2cbd90 slot 39 LightOutdoorTiles: room 0's tiles (mWorld's slot 76) the terrain shows (slots 53, 55), lit as slot
// 41 (shadow masks when 0xffdeac is set), then the sun heights debug view over texture 0 now and then. No world:
// nothing. The list's block comes from the allocator service and goes back through 0x195720.
DIFF_TEST(sg_lighting_walls_outdoor, 0x2cbd90)
{
    svcfx::AllocatorScope allocator;
    FillAllFakes();
    Globals globals;
    using SlotFn = void(__thiscall *)(void *);
    auto outdoor = ctx.Original<SlotFn>(0x2cbd90);
    AtBothPrecisions(ctx, "outdoor", 2000, [&](uint32_t seed, const char *label) {
        const WallCase c(seed, 0x2cbd90u);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            WallCase setup = c;
            setup.SetUp();
            SetGlobalsFor(c);
            PlantWallNaNs(c, false);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) outdoor(gObject);
            else Ours()->LightOutdoorTiles();
            outcome[side] = TextureEnd();
            const float *up = GameAddress<float>(0xffd918);
            outcome[side].insert(outcome[side].end(), {U(up[0]), U(up[1]), U(up[2])});
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2cc660 slot 40 LightWalls: each wall of mWorld once (repeats, reversed and near copies make the set's find and
// insert matter), both sides, through the room stand-in, slot 21's rectangle and LightWallShaded or the flat colour.
DIFF_TEST(sg_lighting_walls_all, 0x2cc660)
{
    svcfx::AllocatorScope allocator;
    FillAllFakes();
    Globals globals;
    shape::CodePatch rooms(0x7f2340, reinterpret_cast<const void *>(&RoomStand));
    shape::StandIn corners{0x2d8120u, &cLightingManager::WallCorners, &CornersStand};
    using SlotFn = void(__thiscall *)(void *);
    auto walls = ctx.Original<SlotFn>(0x2cc660);
    AtBothPrecisions(ctx, "walls", 2000, [&](uint32_t seed, const char *label) {
        const WallCase c(seed, 0x2cc660u);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            WallCase setup = c;
            setup.SetUp();
            SetGlobalsFor(c);
            PlantWallNaNs(c, false);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) walls(gObject);
            else Ours()->LightWalls();
            outcome[side] = TextureEnd();
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2ccb80 slot 42 LightListedWalls: the near side of each wall the listing object's slot 17 gives.
DIFF_TEST(sg_lighting_walls_listed, 0x2ccb80)
{
    FillAllFakes();
    Globals globals;
    shape::CodePatch rooms(0x7f2340, reinterpret_cast<const void *>(&RoomStand));
    shape::StandIn corners{0x2d8120u, &cLightingManager::WallCorners, &CornersStand};
    using SlotFn = void(__thiscall *)(void *);
    auto listed = ctx.Original<SlotFn>(0x2ccb80);
    AtBothPrecisions(ctx, "listed walls", 2000, [&](uint32_t seed, const char *label) {
        WallCase c(seed, 0x2ccb80u);
        c.hasWorld = true; // slot 42 asks the world for kinds without checking it
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            WallCase setup = c;
            setup.SetUp();
            SetGlobalsFor(c);
            PlantWallNaNs(c, false);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) listed(gObject);
            else Ours()->LightListedWalls();
            outcome[side] = TextureEnd();
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2d1720 slot 43 LightRoomsBySky: AddSkyLight's weights per room (sample), the sky colour added flat to each sky-lit
// room's shown tiles and to every wall side (once per segment) whose room is sky-lit. Rooms: as many as samples (the
// room stand-ins answer within them).
DIFF_TEST(sg_lighting_walls_rooms_sky, 0x2d1720)
{
    svcfx::AllocatorScope allocator;
    FillAllFakes();
    Globals globals;
    shape::CodePatch rooms(0x7f2340, reinterpret_cast<const void *>(&RoomStand));
    using SkyFn = void(__thiscall *)(void *, const uint8_t *, int32_t);
    auto sky = ctx.Original<SkyFn>(0x2d1720);
    AtBothPrecisions(ctx, "rooms by sky", 2000, [&](uint32_t seed, const char *label) {
        const WallCase c(seed, 0x2d1720u);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            WallCase setup = c;
            setup.SetUp();
            SetGlobalsFor(c);
            PlantWallNaNs(c, true);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) sky(gObject, gLights, c.lightCount);
            else Ours()->LightRoomsBySky(gLights, c.lightCount);
            outcome[side] = SamplesEnd();
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2d1ec0 slot 44 LightListedBySky: slot 43 over the listing object's tiles (slot 18, rooms from mWorld's slot 79)
// and walls (slot 19, near sides).
DIFF_TEST(sg_lighting_walls_listed_sky, 0x2d1ec0)
{
    FillAllFakes();
    Globals globals;
    shape::CodePatch rooms(0x7f2340, reinterpret_cast<const void *>(&RoomStand));
    using SkyFn = void(__thiscall *)(void *, const uint8_t *, int32_t);
    auto sky = ctx.Original<SkyFn>(0x2d1ec0);
    AtBothPrecisions(ctx, "listed by sky", 2000, [&](uint32_t seed, const char *label) {
        const WallCase c(seed, 0x2d1ec0u);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            WallCase setup = c;
            setup.SetUp();
            SetGlobalsFor(c);
            PlantWallNaNs(c, true);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) sky(gObject, gLights, c.lightCount);
            else Ours()->LightListedBySky(gLights, c.lightCount);
            outcome[side] = SamplesEnd();
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}
