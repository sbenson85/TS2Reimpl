// The grid world (lane RY's, moved here by lane RZ so the ground-height tests share it): the fake cIWorldDB of
// tests/worldfx.h programmed as a size x size grid of tiles on the levels minLevel..maxLevel, with an elevation per
// tile corner, plus what one call left behind (Outcome / Run / Same) and CHECK_SAME. Tests that use it run the
// original ground height (0x7e4680) or ours against the same fakes and compare the call log as well as the outputs.
#pragma once

#include "difftest.h"
#include "worldfx.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace worldgrid {

using worldfx::Fake;
using worldfx::gLog;
using worldfx::gWorldDb;

inline uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}
inline float FromBits(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

// The programmed world. rough: elevations with long significands (so rounding shows), else multiples of 0.3125.
struct World {
    int32_t minLevel = 0, maxLevel = 2, size = 12;
    float wall = 3.0f;
    bool rough = false;
};
inline World gWorld;

inline uint32_t MinLevel(Fake *, const uint32_t *) { return uint32_t(gWorld.minLevel); }
inline uint32_t MaxLevel(Fake *, const uint32_t *) { return uint32_t(gWorld.maxLevel); }
inline uint32_t MaxCoord(Fake *, const uint32_t *) { return uint32_t(gWorld.size - 1); }
inline uint32_t MinCoord(Fake *, const uint32_t *) { return 0; }
inline uint32_t IsInWorld(Fake *, const uint32_t *a)
{
    const float x = FromBits(a[0]), y = FromBits(a[1]);
    return (x >= 0.0f && x < float(gWorld.size) && y >= 0.0f && y < float(gWorld.size)) ? 1u : 0u;
}
// The tile slots take a pointer to {x, y, level}; the pointers differ between the two sides, so the tile's content goes into
// the log after the call's own entry, and Run() clears the pointer argument.
inline const int32_t *LoggedTile(const uint32_t *a)
{
    const int32_t *t = reinterpret_cast<const int32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 3; ++i) gLog.push_back(uint32_t(t[i]));
    return t;
}
inline uint32_t IsInside(Fake *, const uint32_t *a)
{
    const int32_t *t = LoggedTile(a);
    return (t[0] >= 0 && t[0] < gWorld.size && t[1] >= 0 && t[1] < gWorld.size && t[2] >= gWorld.minLevel &&
            t[2] <= gWorld.maxLevel)
               ? 1u
               : 0u;
}
inline uint32_t IsInsideCorners(Fake *, const uint32_t *a)
{
    const int32_t *t = LoggedTile(a);
    return (t[0] >= 0 && t[0] <= gWorld.size && t[1] >= 0 && t[1] <= gWorld.size && t[2] >= gWorld.minLevel &&
            t[2] <= gWorld.maxLevel)
               ? 1u
               : 0u;
}
inline float Elevation(Fake *, const uint32_t *a)
{
    const int32_t *t = LoggedTile(a);
    if (gWorld.rough) {
        const uint32_t h = uint32_t(t[0] * 73856093) ^ uint32_t(t[1] * 19349663) ^ uint32_t(t[2] * 83492791);
        return float(h % 100003u) * 0.0001372f - 3.1f;
    }
    return float((t[0] * 31 + t[1] * 17 + t[2] * 7) & 15) * 0.3125f - 1.0f;
}
inline float WallHeight(Fake *, const uint32_t *) { return gWorld.rough ? 2.7182817f : gWorld.wall; }

inline void ProgramWorld()
{
    gWorldDb.Reset(100);
    gWorldDb.Set(88, 0, 0, &MinLevel);
    gWorldDb.Set(89, 0, 0, &MaxLevel);
    gWorldDb.Set(83, 0, 0, &MaxCoord); // 0x14c
    gWorldDb.Set(81, 0, 0, &MinCoord); // 0x144
    gWorldDb.Set(84, 0, 0, &MaxCoord); // 0x150
    gWorldDb.Set(82, 0, 0, &MinCoord); // 0x148
    gWorldDb.Set(72, 2, 0, &IsInWorld);
    gWorldDb.Set(71, 1, 0, &IsInside);
    gWorldDb.Set(70, 1, 0, &IsInsideCorners);
    gWorldDb.SetFloatFn(62, 1, &Elevation);
    gWorldDb.SetFloatFn(95, 0, &WallHeight);
}

inline void RandomWorld(std::mt19937 &r)
{
    gWorld.minLevel = int32_t(r() % 4) - 1;
    gWorld.maxLevel = gWorld.minLevel + int32_t(r() % 4); // max < min would make the original's two level branches chase each other
    gWorld.size = 2 + int32_t(r() % 14);
    const float walls[] = {3.0f, 0.0f, 2.5f, -1.0f, 0.1f};
    gWorld.wall = walls[r() % 5];
}

inline float RandomCoord(std::mt19937 &r)
{
    const float size = float(gWorld.size);
    switch (r() % 9) {
    case 0: return float(int32_t(r() % uint32_t(gWorld.size + 4)) - 2);
    case 1: return float(int32_t(r() % uint32_t(2 * gWorld.size + 8)) - 4) * 0.5f;
    case 2: return float(int32_t(r() % uint32_t(gWorld.size + 2))) + 1.0f / 1024.0f * float(int32_t(r() % 3));
    case 3: return float(int32_t(r() % uint32_t(gWorld.size + 2))) + 1.0f - 1.0f / 1048576.0f * float(r() % 3);
    case 4: return -2.0f + (size + 4.0f) * float(r() % 10001) / 10000.0f;
    case 5: return FromBits(r() % 64 == 0 ? 0x7fc00000u : r());
    default: return -1.0f + (size + 2.0f) * float(r() % 1000001) / 1000000.0f;
    }
}

inline int32_t RandomLevel(std::mt19937 &r) { return int32_t(r() % 10) - 3; }

// What one call left behind: four output words, a return token and the fake's call log.
struct Outcome {
    uint32_t w[4];
    uint32_t ret;
    std::vector<uint32_t> log;
};

// The tile-pointer slots of the log (their pointer argument is cleared, the logged tile content follows the entry).
inline bool TileSlot(uint32_t slot) { return slot == 62 || slot == 70 || slot == 71 || slot == 52; }

template <class F> Outcome Run(F &&f)
{
    Outcome o;
    for (auto &w : o.w) w = 0xc5c5c5c5u;
    gLog.clear();
    o.ret = f(o.w);
    o.log = gLog;
    // Walk the entries {id, slot, argc, args..., [tile content]} and clear the tile pointers.
    for (size_t i = 0; i + 3 < o.log.size();) {
        const uint32_t slot = o.log[i + 1], argc = o.log[i + 2];
        const bool tile = TileSlot(slot);
        if (tile) o.log[i + 3] = 0;
        i += 3 + argc + (tile ? 3 : 0);
    }
    return o;
}

inline bool Same(const Outcome &a, const Outcome &b)
{
    for (int i = 0; i < 4; ++i)
        if (a.w[i] != b.w[i]) return false;
    return a.ret == b.ret && a.log == b.log;
}

inline uint32_t Token(const void *returned, const void *out) { return returned == out ? 1u : 0u; }

} // namespace worldgrid

#define CHECK_SAME(a, b, ...)                                                                                          \
    do {                                                                                                               \
        ++ctx.cases;                                                                                                   \
        if (!worldgrid::Same(a, b)) {                                                                                  \
            ctx.Fail(__VA_ARGS__);                                                                                     \
        }                                                                                                              \
    } while (0)
