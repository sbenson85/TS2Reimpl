// The world DB stood in for nTSSG::cFloorColorGrid's tests (moved out of tests/sg_floor_color_grid_step.cpp, lane
// 261110b, scenegraph LD2g; LD2c wrote it): the world 0x20e0b2 is a worldfx::Fake whose slots (39 the partitions, 52
// HasFloorOrGround, 53 HasAnyFloorOrGround, 55 HasAnyFloorAbove, 71 IsInside; the partitions' 80) answer from a hash of
// the case's seed and what they are asked, with junk above the bool's byte, and log what they were asked. The
// stopwatch's Restart 0x1c95ec stands in, counted (floorfx::StandRestart).
//
// Its Mix is its own (neither of tests/sg_lighting_common.h's): the step tests' cases were drawn with it.
#pragma once

#include "sg_floor_color_grid.h"
#include "worldfx.h"

#include <initializer_list>
#include <random>

namespace floorworld {

using floorfx::Blob;

struct WorldState {
    worldfx::Fake db, parts;
    uint32_t seed = 0;
    uint32_t floors = 50; // per cent of the floor queries answered yes
    uint32_t inside = 70;
    Blob log;
    // LD2g (CellWorld): the partitions' 80 answers drawn level ids, and the questions' running count (so a question
    // asked twice may be answered differently).
    bool drawnLevels = false;
    uint32_t calls = 0;
};
inline WorldState *gWorld = nullptr;

inline uint32_t Mix(uint32_t h, uint32_t v)
{
    h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
    h *= 0x85ebca6bu;
    return h ^ (h >> 13);
}

// The answer to a question: a bool from the hash (yes `percent` of the time) with junk above its byte.
inline uint32_t Answer(std::initializer_list<uint32_t> question, uint32_t percent)
{
    uint32_t h = gWorld->seed;
    for (uint32_t v : question) h = Mix(h, v);
    const uint32_t junk = Mix(h, 0x5eed) & 0xffffff00u;
    return junk | (h % 100 < percent ? 1u : 0u);
}

inline const int32_t *Tile(const uint32_t *args) { return reinterpret_cast<const int32_t *>(args[0]); }

inline uint32_t FloorOrGround(worldfx::Fake *, const uint32_t *args)
{
    const int32_t *t = Tile(args);
    const uint32_t q = uint32_t(t[3]) & 0xff; // the quadrant byte (the three after it are not set)
    const std::initializer_list<uint32_t> question = {52u, uint32_t(t[0]), uint32_t(t[1]), uint32_t(t[2]), q,
                                                      args[1] & 0xff, args[2] & 0xff};
    gWorld->log.insert(gWorld->log.end(), question);
    return Answer(question, gWorld->floors);
}

template <uint32_t kSlot> uint32_t AnyFloor(worldfx::Fake *, const uint32_t *args)
{
    const int32_t *t = Tile(args); // the segment byte is not set by MarkFloorTiles: not read
    const std::initializer_list<uint32_t> question = {kSlot, uint32_t(t[0]), uint32_t(t[1]), uint32_t(t[2]),
                                                      args[1] & 0xff, args[2] & 0xff};
    gWorld->log.insert(gWorld->log.end(), question);
    return Answer(question, gWorld->floors);
}

inline uint32_t Inside(worldfx::Fake *, const uint32_t *args)
{
    const int32_t *t = Tile(args);
    const std::initializer_list<uint32_t> question = {71u, uint32_t(t[0]), uint32_t(t[1]), uint32_t(t[2])};
    gWorld->log.insert(gWorld->log.end(), question);
    return Answer(question, gWorld->inside);
}

inline uint32_t Partitions(worldfx::Fake *, const uint32_t *)
{
    gWorld->log.push_back(39u);
    return uint32_t(reinterpret_cast<uintptr_t>(&gWorld->parts));
}

inline uint32_t LevelQuery(worldfx::Fake *, const uint32_t *args)
{
    const uint32_t *p = reinterpret_cast<const uint32_t *>(args[0]);
    const std::initializer_list<uint32_t> question = {80u, p[0], p[1], p[2], args[1], args[2]};
    gWorld->log.insert(gWorld->log.end(), question);
    const uint32_t h = Mix(Mix(Mix(gWorld->seed, p[0]), p[1]), p[2]);
    if (!gWorld->drawnLevels) return h; // LD2c's cases: the whole hash
    // LD2g: level ids around the 16-bit zero test (0, junk above a zero low half, small, either sign's extreme, any),
    // differing between calls.
    const uint32_t v = Mix(h, ++gWorld->calls);
    switch (v % 8) {
    case 0:
    case 1: return 0;
    case 2: return (v & 0xffff0000u) | 0x10000u;
    case 3: return 1 + v % 3;
    case 4: return (v & 0xffff0000u) | 0x8000u;
    case 5: return (v & 0xffff0000u) | 0xffffu;
    default: return v;
    }
}

inline void *__cdecl StandWorld()
{
    gWorld->log.push_back(0x20e0b2u);
    return &gWorld->db;
}

struct World : WorldState {
    shape::CodePatch patch{0x20e0b2u, shape::CodeOf(&StandWorld)};
    shape::CodePatch restart{0x1c95ecu, shape::CodeOf(&floorfx::StandRestart)};
    World()
    {
        worldfx::gLogging = false;
        db.Reset(1);
        parts.Reset(2);
        db.Set(39, 0, 0, &Partitions);
        db.Set(52, 3, 0, &FloorOrGround);
        db.Set(53, 3, 0, &AnyFloor<53>);
        db.Set(55, 3, 0, &AnyFloor<55>);
        db.Set(71, 1, 0, &Inside);
        parts.Set(80, 3, 0, &LevelQuery);
        gWorld = this;
    }
    ~World()
    {
        gWorld = nullptr;
        worldfx::gLogging = true;
    }
    void Case(uint32_t s, std::mt19937 &gen)
    {
        seed = s;
        static const uint32_t kRates[] = {0, 100, 50, 20, 80};
        floors = kRates[gen() % std::size(kRates)];
        inside = kRates[gen() % std::size(kRates)];
        log.clear();
    }
};

// ---- LD2g: what BuildCells asks besides (lane 261110b) ----
//
// CellWorld adds to World: the same db fake plugged into the cached world DB cell (worldfx::kWorldDbCell) with the
// tile box (slots 81-84 GetMinX, GetMinY, GetMaxX, GetMaxY); the partitions' 43 (a partition on a tile side: yes `cuts`
// per cent) and 50 (the catalog id of a side's segment, from kCutIds); a worldfx::Catalog whose entries answer slot 38
// (0x98 / 4: yes `fixed` per cent); and 0x7e37e0, the world's answer table (still the original's; its inputs are the
// world's own tables, which the test cannot build), standing in as null `walls` per cent. Every answer is hashed from
// the case's seed, the question and the running count of questions, and every question is logged. The partitions' 80
// answers drawn level ids (WorldState::drawnLevels).

inline const int32_t *gBox = nullptr; // minX, minY, maxX, maxY
inline uint32_t gCuts = 50, gFixed = 50, gWalls = 50;

template <uint32_t kSlot> uint32_t Bound(worldfx::Fake *, const uint32_t *)
{
    gWorld->log.push_back(kSlot);
    return uint32_t(gBox[kSlot - 81]);
}

inline uint32_t Cut(worldfx::Fake *, const uint32_t *args)
{
    const uint32_t *k = reinterpret_cast<const uint32_t *>(args[0]);
    const std::initializer_list<uint32_t> question = {43u, k[0], k[1], k[2], k[3]};
    gWorld->log.insert(gWorld->log.end(), question);
    return Answer({43u, k[0], k[1], k[2], k[3], ++gWorld->calls}, gCuts);
}

// The ids: 0x1d (passes whatever its entry says), 0x11d (0x1d in the low byte only), 1..6, and 0x77 with no entry.
constexpr uint32_t kCutIds[] = {0x1d, 0x11d, 1, 2, 3, 4, 5, 6, 0x77};

inline uint32_t CutId(worldfx::Fake *, const uint32_t *args)
{
    const uint32_t *seg = reinterpret_cast<const uint32_t *>(args[0]);
    gWorld->log.push_back(50u);
    gWorld->log.insert(gWorld->log.end(), seg, seg + 6);
    uint32_t h = Mix(gWorld->seed, ++gWorld->calls);
    for (int i = 0; i < 6; ++i) h = Mix(h, seg[i]);
    return kCutIds[h % std::size(kCutIds)];
}

inline uint32_t Fixed(worldfx::Fake *self, const uint32_t *)
{
    gWorld->log.push_back(38u);
    gWorld->log.push_back(self->id);
    return Answer({38u, self->id, ++gWorld->calls}, gFixed);
}

inline void *__cdecl StandAnswers(const uint32_t *key, uint32_t mode)
{
    const std::initializer_list<uint32_t> question = {0x7e37e0u, key[0], key[1], key[2], key[3], mode};
    gWorld->log.insert(gWorld->log.end(), question);
    uint32_t h = gWorld->seed;
    for (uint32_t v : question) h = Mix(h, v);
    h = Mix(h, ++gWorld->calls);
    if (h % 100 < gWalls) return nullptr;
    // Non-null, sometimes with a zero low half (the callers test the whole word).
    return reinterpret_cast<void *>(uintptr_t(h % 3 ? (h | 1u) : ((h & 0xffff0000u) | 0x10000u)));
}

struct CellWorld : World {
    worldfx::Scope scope;
    worldfx::Catalog catalog;
    shape::CodePatch answers{0x7e37e0u, shape::CodeOf(&StandAnswers)};
    int32_t box[4] = {};
    CellWorld()
    {
        drawnLevels = true;
        db.Set(81, 0, 0, &Bound<81>);
        db.Set(82, 0, 0, &Bound<82>);
        db.Set(83, 0, 0, &Bound<83>);
        db.Set(84, 0, 0, &Bound<84>);
        parts.Set(43, 1, 0, &Cut);
        parts.Set(50, 1, 0, &CutId);
        for (unsigned i = 0; i < worldfx::Catalog::kEntries; ++i) catalog.Entry(i).Set(38, 0, 0, &Fixed);
        catalog.Map(0x1d, 7);
        catalog.Map(0x11d, 0);
        for (uint32_t id = 1; id <= 6; ++id) catalog.Map(id, id);
        scope.Set(worldfx::kWorldDbCell, &db);
        catalog.Plug(scope);
        gBox = box;
    }
    ~CellWorld() { gBox = nullptr; }
    // A case on a w x h grid: the floor rates as World::Case, the side rates, and the tile box: the whole grid when
    // `whole`, otherwise each bound anywhere in the grid (the minimum past the maximum included).
    void Case(uint32_t s, std::mt19937 &gen, uint32_t w, uint32_t h, bool whole)
    {
        World::Case(s, gen);
        static const uint32_t kRates[] = {0, 100, 50, 30, 70};
        gCuts = kRates[gen() % std::size(kRates)];
        gFixed = kRates[gen() % std::size(kRates)];
        gWalls = kRates[gen() % std::size(kRates)];
        if (whole) {
            box[0] = box[1] = 0;
            box[2] = int32_t(w) - 1;
            box[3] = int32_t(h) - 1;
        } else {
            box[0] = int32_t(gen() % w);
            box[1] = int32_t(gen() % h);
            box[2] = gen() % 4 ? int32_t(gen() % w) : box[0];
            box[3] = gen() % 4 ? int32_t(gen() % h) : box[1];
        }
    }
    void Side() // before each side: the log and the question count afresh
    {
        log.clear();
        calls = 0;
    }
};

} // namespace floorworld
