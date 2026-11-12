// The queries around the heights of nTSWorld's helper module (lane RZ): src/world/WorldHeights.h. Built for speed (/O2).
#include "world/WorldHeights.h"

#include "game/original.h"
#include "game/vtable.h"
#include "misc/X87.h"
#include "rz/GameAllocator.h"
#include "rz/Singleton_20ddfb.h"
#include "world/WorldCatalog.h"
#include "world/WorldService.h"
#include "world/WorldTileMath.h"

#include <cstring>
#include <string>

#include <xmmintrin.h>

namespace worldheights {

namespace {

using nTSWorld::WorldPoint;
using nTSWorld::WorldTile;

constexpr float kQuarter = 0.25f;        // 0xda03e0
constexpr float kThreeQuarters = 0.75f;  // 0xdac534
constexpr float kPoolDeckRaise = 2.75f;  // 0xe037b8

// WU-2's predicates and lookups (0x7df000-0x7e3800), still the originals.
using PointPred = bool(__cdecl *)(const void *);

// A float's bits as a sign-magnitude integer (as WorldHeights.cpp's): the 31-ulp test against a and b.
int32_t Ordered(float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    const uint32_t sign = bits >> 31;
    return static_cast<int32_t>((((0u - sign) >> 1) ^ bits) + sign);
}
bool Near(float a, float b)
{
    const int32_t d = static_cast<int32_t>(static_cast<uint32_t>(Ordered(a)) - static_cast<uint32_t>(Ordered(b)));
    return ((d >> 26 ^ d) & ~31) == 0;
}

float Bits(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, sizeof(f));
    return f;
}

// The app's object manager (slot 26) gives an object for an id (slot 29), held by AddRef (slot 1) / Release (slot 2).
void *ObjectManager() { return RawSlot<26, void *>(GetCachedSingleton()); }

struct Held {
    void *object;
    explicit Held(void *o) : object(o) { if (object) RawSlot<1>(object); }
    ~Held() { if (object) RawSlot<2>(object); }
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
};

// Attribute `index` of an object (slot 13), a 16-bit value.
int16_t Attribute(void *object, uint32_t index) { return static_cast<int16_t>(RawSlot<13>(object, index)); }

// The quadrant record of the partitions' slot 76 vector.
struct TileRecord {
    int32_t x, y, level;
    uint8_t quadrants;
    uint8_t pad[3];
};
static_assert(sizeof(TileRecord) == 16, "TileRecord");

struct RecordVector {
    TileRecord *begin, *end, *capacity;
};

} // namespace

int32_t __cdecl RoomAreaInTiles(int32_t room, int32_t level)
{
    void *const partitions = worldsvc::CachedWorldDB()->GetPartitions();
    RecordVector records{};
    RawSlot<76>(partitions, room, &records, level);
    int32_t quarters = 0;
    const int32_t count = static_cast<int32_t>(records.end - records.begin);
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t q = records.begin[i].quadrants;
        if (q == 0xf) {
            quarters += 4;
            continue;
        }
        quarters += (q & 1) != 0;
        quarters += (q & 2) != 0;
        quarters += (q & 4) != 0;
        quarters += (q & 8) != 0;
    }
    const int32_t area = quarters / 4;
    if (records.begin) as2::FreeViaAllocatorService(records.begin, 0);
    return area;
}

bool __cdecl RoomMostlyFlagged(int32_t room)
{
    void *const partitions = worldsvc::CachedWorldDB()->GetPartitions();
    worldmath::TileQuadrant first{};
    RawSlot<77>(partitions, &first, room, 2);
    float centre[3];
    worldmath::QuadrantCentre(centre, &first);
    float snapped[6] = {-1.0f, -1.0f, 0.0f, -1.0f, -1.0f, 0.0f};
    OriginalFunction<bool(__cdecl *)(const float *, float *), 0x7e2de0>()(centre, snapped);

    void *const graph = RawSlot<40, void *>(partitions, 7);
    if (!graph) return false;
    void *const node = RawSlot<12, void *>(graph, snapped);
    if (!node) return false;
    void *start = RawSlot<2, void *>(node, 0);
    if (!start) return false;

    const float *origin = RawSlot<5, const float *>(RawSlot<35, void *>(start));
    int32_t originLevel, snappedLevel;
    std::memcpy(&originLevel, &origin[2], 4);
    std::memcpy(&snappedLevel, &snapped[2], 4);
    if (originLevel != snappedLevel || !Near(origin[0], snapped[0]) || !Near(origin[1], snapped[1]))
        start = RawSlot<11, void *>(start);

    int32_t flagged = 0, total = 0;
    void *edge = start;
    do {
        void *const from = RawSlot<35, void *>(edge);
        void *const to = RawSlot<36, void *>(edge);
        const float *a = RawSlot<5, const float *>(from);
        const float fx = a[0], fy = a[1];
        uint32_t fl;
        std::memcpy(&fl, &a[2], 4);
        const float *b = RawSlot<5, const float *>(to);
        float segment[6] = {fx, fy, 0, b[0], b[1], 0};
        std::memcpy(&segment[2], &fl, 4);
        std::memcpy(&segment[5], &b[2], 4);
        const uint32_t id = RawSlot<50>(partitions, segment);
        if (worldcatalog::ProductFlag11(&id)) ++flagged;
        ++total;
        edge = RawSlot<25, void *>(edge);
    } while (edge != start);
    return flagged > total / 2;
}

float __cdecl SurfaceHeightAt(const float *point)
{
    float height = static_cast<float>(GroundHeightAt(reinterpret_cast<const WorldPoint *>(point)));
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    worldmath::TileQuadrant quadrant;
    worldmath::PositionToTileQuadrant(&quadrant, point);
    const uint32_t deck = db->GetDeckFoundation(reinterpret_cast<const WorldTile *>(&quadrant));
    if (OriginalFunction<bool(__cdecl *)(const uint32_t *), 0x7e24f0>()(&deck)) {
        const float surface = RawSlot<17, float>(db->GetPools(), point);
        return static_cast<float>(x87::Rounded(static_cast<double>(surface) + height));
    }
    if (OriginalFunction<bool(__cdecl *)(const uint32_t *), 0x7e2520>()(&deck)) height += kThreeQuarters;
    return height;
}

double __cdecl HighestSurfaceAt(float x, float y, uint32_t roofArg, bool *onRoof, int32_t *level, bool *sunken)
{
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    void *const roofs = db->GetRoofs();
    const int32_t top = db->GetMaxLevel();
    const float roof = RawSlot<24, float>(roofs, x, y, level, roofArg);
    if (sunken) *sunken = false;
    if (onRoof) *onRoof = false;
    if (!Near(roof, 0.0f)) {
        if (onRoof) *onRoof = true;
        return roof;
    }
    for (int32_t l = top; l > 0; --l) {
        const WorldPoint at{x, y, l};
        if (OriginalFunction<bool(__cdecl *)(const WorldPoint *, uint32_t, uint32_t, uint32_t), 0x7e06e0>()(&at, 0, 0, 0)) {
            if (level) *level = l;
            return GroundHeightAt(&at);
        }
    }
    const WorldPoint at{x, y, 0};
    const WorldPoint position{x, y, 0};
    const float ground = static_cast<float>(GroundHeightAt(&at));
    // One record for the tile quadrant and then the tile, as the original's (the tile keeps the quadrant byte).
    worldmath::TileQuadrant cell;
    if (OriginalFunction<bool(__cdecl *)(const worldmath::TileQuadrant *), 0x7e0430>()(
            worldmath::PositionToTileQuadrant(&cell, &position.x))) {
        if (sunken) *sunken = true;
        // A float (the original's subss): MSVC would return `ground - kQuarter` as an unrounded x87 fsub, even assigned.
        return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(ground), _mm_set_ss(kQuarter)));
    }
    float result = ground;
    if (OriginalFunction<bool(__cdecl *)(const int32_t *), 0x7e2400>()(
            worldmath::PositionToTile(reinterpret_cast<int32_t *>(&cell), &position.x))) {
        if (void *const terrain = db->GetTerrain()) {
            const float t = RawSlot<11, float>(terrain, x, y);
            result = t > ground ? t : ground;
        }
    }
    if (level) *level = 0;
    return result;
}

uint32_t __cdecl ObjectQueryAt(uint32_t key, float f)
{
    void *const object = OriginalFunction<void *(__cdecl *)(uint32_t), 0x7e3730>()(key);
    if (!object) return 0;
    uint32_t scratch[3];
    void *const arg = RawSlot<42, void *>(object, scratch, f);
    return RawSlot<178>(object, arg);
}

bool __cdecl IsRaisedObject(void *object)
{
    if (!object) return false;
    const int32_t kind = static_cast<int32_t>(RawSlot<98>(object));
    if (kind == 9) return true;
    if (OriginalFunction<PointPred, 0x7e1820>()(object)) return true;
    if (kind == 10 || kind == 11 || kind == 12 || kind == 5) return true;
    if (Attribute(object, 0x3b) == 8) return true;
    if (OriginalFunction<PointPred, 0x7e20f0>()(object)) return true;
    if (OriginalFunction<PointPred, 0x7e15a0>()(object)) return true;
    if (static_cast<int32_t>(RawSlot<159>(object)) > 0) {
        switch (Attribute(object, 0x3b)) {
        case 1: case 4: case 5: case 6: case 28: case 70: case 148:
            return false;
        default:
            break;
        }
        if (RawSlot<28>(RawSlot<61, void *>(object)) == 0xb0860d98u) return false;
        if (!OriginalFunction<PointPred, 0x7e1d40>()(object)) return true;
    }
    void *const manager = RawSlot<24, void *>(GetCachedSingleton());
    void *const face = RawSlot<61, void *>(object);
    if (!manager) return false;
    return RawSlot<43, bool>(manager, face) || RawSlot<49, bool>(manager, face) || RawSlot<51, bool>(manager, face);
}

bool __cdecl TileClaimsObject(const float *point, void *other)
{
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    worldmath::TileQuadrant quadrant;
    worldmath::PositionToTileQuadrant(&quadrant, point);
    const auto *tile = reinterpret_cast<const WorldTile *>(&quadrant);
    if (!db->IsInside(tile)) return false;
    void *const app = GetCachedSingleton();
    void *const objects = RawSlot<26, void *>(app);
    const int32_t count = db->CountObjects(tile);
    for (int32_t i = 0; i < count; ++i) {
        Held held(RawSlot<29, void *>(objects, db->GetObjectAt(i, tile, true)));
        if (held.object && RawSlot<112, bool>(held.object, other)) return true;
    }
    const nTSWorld::WorldName pattern = db->GetFloorPatternName(tile);
    void *const catalog = RawSlot<16, void *>(app);
    if (!catalog) return false;
    void *const entry = RawSlot<42, void *>(catalog, 1, pattern.text, 1);
    if (!entry) return false;
    const char *name = RawSlot<9, const char *>(entry);
    if (!*name) return false;
    reinterpret_cast<std::string *>(static_cast<char *>(other) + 4)->assign(name);
    return true;
}

bool __cdecl AnyStructureAt(const void *a, uint32_t b)
{
    if (OriginalFunction<PointPred, 0x7dfdc0>()(a)) return true;
    if (OriginalFunction<PointPred, 0x7e0600>()(a)) return true;
    return OriginalFunction<bool(__cdecl *)(const void *, uint32_t), 0x7e00f0>()(a, b);
}

float __cdecl ObjectHeightAt(void *object, const float *point, const uint32_t *context)
{
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    int32_t pointLevel;
    std::memcpy(&pointLevel, &point[2], 4);
    if ((static_cast<uint32_t>(RawSlot<13>(object, 0x2a)) >> 14 & 1) != 0 && pointLevel == 0) {
        const float ground = db->GetGroundElevation(static_cast<int32_t>(point[0]), static_cast<int32_t>(point[1]));
        void *const terrain = db->GetTerrain();
        if (!terrain) return ground;
        const float t = RawSlot<11, float>(terrain, point[0], point[1]);
        return t > ground ? t : ground;
    }

    float height;
    const uint32_t flags = static_cast<uint16_t>(RawSlot<153>(object));
    if ((flags & 0x5f) != 0) {
        int32_t tile[3];
        worldmath::PositionToTile(tile, point);
        // ORIGINAL-BUG: when no allowed quadrant is inside the lot's corners (0x7e54d8 jumps to 0x7e54f9), the height stays
        // the flags word the original kept in the same stack slot, read as a float.
        height = Bits(flags);
        for (int32_t i = 0; i < 16; ++i) {
            const uint32_t bit = 1u << i;
            if ((bit & 0x5f) == 0 || (bit & static_cast<uint32_t>(static_cast<int16_t>(flags))) == 0) continue;
            float out[6];
            OriginalFunction<void(__cdecl *)(float *, uint32_t, const int32_t *, const uint32_t *), 0x7e29a0>()(out, bit, tile, context);
            WorldTile corner;
            corner.x = static_cast<int32_t>(out[0]);
            corner.y = static_cast<int32_t>(out[1]);
            std::memcpy(&corner.level, &out[2], 4);
            std::memcpy(&corner.segments, &tile[0], 4); // the original's tile record follows the corner on its stack
            if (db->IsInsideCorners(&corner)) {
                height = db->GetElevation(&corner);
                break;
            }
        }
    } else {
        height = static_cast<float>(GroundHeightAt(reinterpret_cast<const WorldPoint *>(point)));
    }

    worldmath::TileQuadrant quadrant;
    worldmath::PositionToTileQuadrant(&quadrant, point);
    const uint32_t deck = db->GetDeckFoundation(reinterpret_cast<const WorldTile *>(&quadrant));
    uint32_t placement[3];
    OriginalFunction<void(__cdecl *)(uint32_t *, void *, uint32_t, const float *, uint32_t), 0x7df3f0>()(placement, object, deck, point,
                                                                                            *context);
    if (OriginalFunction<bool(__cdecl *)(const uint32_t *), 0x7e24f0>()(&deck)) {
        height += kPoolDeckRaise;
    } else if (OriginalFunction<bool(__cdecl *)(void *, const uint32_t *), 0x7e2130>()(object, placement) && !IsRaisedObject(object)) {
        height += kThreeQuarters;
    }
    if ((static_cast<uint32_t>(RawSlot<13>(object, 0x52)) >> 9 & 1) != 0) {
        const float offset = RawSlot<45, float>(object);
        height = static_cast<float>(x87::Rounded(static_cast<double>(offset) + height));
    }
    return height;
}

float *__cdecl ObjectPositionAt(float *out, void *object, const float *point, const uint32_t *context)
{
    const float height = ObjectHeightAt(object, point, context);
    std::memcpy(&out[0], &point[0], 4);
    std::memcpy(&out[1], &point[1], 4);
    out[2] = height;
    return out;
}

bool __cdecl TileHasStructure(const WorldTile *tile)
{
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    if (!db->IsInside(tile)) return false;
    if (db->GetMaxLevel() < 0) return false;
    for (int32_t l = 0;; ++l) {
        WorldTile whole{};
        whole.x = tile->x;
        whole.y = tile->y;
        whole.level = l;
        whole.segments = 0xf;
        if (OriginalFunction<PointPred, 0x7dfc00>()(&whole) || OriginalFunction<PointPred, 0x7dfa70>()(&whole) || OriginalFunction<PointPred, 0x7e0920>()(&whole))
            return true;
        if (l + 1 > db->GetMaxLevel()) return false;
    }
}

bool __cdecl TileHasFlaggedObject(const WorldTile *tile)
{
    void *const objects = ObjectManager();
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    const int32_t count = db->CountObjects(tile);
    for (int32_t i = 0; i < count; ++i) {
        Held held(RawSlot<29, void *>(objects, db->GetObjectAt(i, tile, true)));
        if (held.object && (RawSlot<13>(held.object, 0x62) & 1) != 0) return true;
    }
    return false;
}

uint32_t __cdecl PartitionMaskRotated(const WorldTile *tile, uint32_t rotation)
{
    void *const partitions = worldsvc::CachedWorldDB()->GetPartitions();
    uint32_t mask = 0;
    for (uint32_t bit = 1; bit <= 0x20; bit += bit) {
        const int32_t query[4] = {tile->x, tile->y, tile->level, static_cast<int32_t>(bit)};
        if (RawSlot<43, bool>(partitions, query)) mask |= bit;
    }
    // Where each of the six bits goes after 1, 2 or 3 quarter turns.
    static constexpr uint8_t kTurned[3][6] = {
        {2, 4, 8, 1, 0x20, 0x10},
        {4, 8, 1, 2, 0x10, 0x20},
        {8, 1, 2, 4, 0x20, 0x10},
    };
    if (rotation == 0) return mask;
    if (rotation > 3) return 0;
    uint32_t turned = 0;
    for (int i = 0; i < 6; ++i)
        if (mask & (1u << i)) turned |= kTurned[rotation - 1][i];
    return turned;
}

float __cdecl SegmentEndHeight(const float *segment, bool first)
{
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    void *const roofs = db->GetRoofs();
    const float *end = first ? segment : segment + 3;
    WorldPoint at;
    std::memcpy(&at, end, sizeof(at));
    if (RawSlot<37, bool>(roofs, &at)) {
        const float roof = RawSlot<22, float>(roofs, &at, 1);
        return static_cast<float>(x87::Rounded(static_cast<double>(roof) - kQuarter));
    }
    const uint32_t id = RawSlot<50>(db->GetPartitions(), segment);
    if (worldcatalog::IsKind1Or3(&id) || worldcatalog::IsKind2(&id)) {
        worldcatalog::IdIs12d(&id); // asked and not used
        const float ground = static_cast<float>(GroundHeightAt(&at));
        return static_cast<float>(x87::Rounded(static_cast<double>(worldcatalog::PartitionValue(&id)) + ground));
    }
    if (worldcatalog::IdIs12d(&id))
        return static_cast<float>(x87::Rounded(GroundHeightAt(&at) + kThreeQuarters));
    ++at.level;
    return static_cast<float>(GroundHeightAt(&at));
}

int32_t *__cdecl Lookup9(uint32_t key) { return OriginalFunction<int32_t *(__cdecl *)(uint32_t, uint32_t), 0x7e37e0>()(key, 9); }

} // namespace worldheights
