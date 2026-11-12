// Differential tests of row LD2e (lane 261110g; src/sg/cWallDiffusionManager.h): the manager's entry points
// cDiffusionManager calls: the two change-list collectors, Reset, Mark, the segment search and the wall colour. Managers
// come from tests/sg_wall_diffusion.h (wallfx) alike on both sides; the world's level 0x7f2260 is stood in (its locations
// logged by their words, its answer's low half zero for about half the runs) and its log is compared. Every case runs at
// the x87's 53-bit and 24-bit precisions (the originals are SSE).
#include "sg_wall_diffusion.h"

#include "sg/cDiffusionManager.h"
#include "world/WorldTileRects.h"

using namespace wallfx;
using nTSSG::DiffusionWall;
using nTSSG::WallSegmentRef;

namespace {

struct Manager {
    Block<cWallDiffusionManager> block{1};
    cWallDiffusionManager *m;
    Manager(uint32_t seed, const Shape &shape)
    {
        m = cWallDiffusionManager::Construct(block.bytes, nullptr);
        Fill(*m, seed, shape);
    }
    ~Manager() { m->~cWallDiffusionManager(); }
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;
};

void Append(Blob &o, const Blob &b) { o.insert(o.end(), b.begin(), b.end()); }

// Some segments' locations copied over others' (earlier and later, in the same run or another), so a search meets the
// same five words more than once, or the same level word with others different.
void ShareLocations(cWallDiffusionManager &m, std::mt19937 &gen)
{
    if (m.mRuns.empty()) return;
    const uint32_t n = gen() % 4;
    for (uint32_t k = 0; k < n; ++k) {
        WallDiffusionRun &a = m.mRuns[gen() % m.mRuns.size()];
        WallDiffusionRun &b = m.mRuns[gen() % m.mRuns.size()];
        if (a.mSegments.empty() || b.mSegments.empty()) continue;
        uint32_t *to = b.mSegments[gen() % b.mSegments.size()].mLocation;
        const uint32_t *from = a.mSegments[gen() % a.mSegments.size()].mLocation;
        if (gen() % 3 == 0)
            to[2] = from[2]; // the level word only
        else
            std::memcpy(to, from, 5 * sizeof(uint32_t));
    }
}

// A location to look for: a segment's (now and then one word off by one), or junk words with a run's level word, or
// all junk.
void Target(const cWallDiffusionManager &m, std::mt19937 &gen, uint32_t *location)
{
    for (int i = 0; i < 5; ++i) location[i] = gen() % 4 ? gen() % 64 : gen();
    if (m.mRuns.empty() || gen() % 6 == 0) return;
    const WallDiffusionRun &r = m.mRuns[gen() % m.mRuns.size()];
    if (r.mSegments.empty()) return;
    if (gen() % 5 == 0) {
        location[2] = r.mSegments[0].mLocation[2];
        return;
    }
    std::memcpy(location, r.mSegments[gen() % r.mSegments.size()].mLocation, 5 * sizeof(uint32_t));
    if (gen() % 5 == 0) location[gen() % 5] += gen() % 2 ? 1u : 0xffffffffu;
}

} // namespace

// ---- the collectors ---------------------------------------------------------------------------------------------------

// CollectWalls2 0x40c620 / CollectWalls 0x40c650: the list appended to a vector of 0-3 records (spare capacity now and
// then, so the insert both fits and grows): the vector's size, capacity and records, then the manager whole.
namespace {
template <class Ours> void CollectCases(difftest::Context &ctx, const char *what, uint32_t rva, Ours ours)
{
    Fixture fixture;
    using Collect = void(__thiscall *)(void *, std::vector<DiffusionWall> *);
    auto original = ctx.Original<Collect>(rva);
    SidesAtBothPrecisions(ctx, what, 600, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxChanges = 5;
        shape.nans = true;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ rva);
        std::vector<DiffusionWall> out;
        if (gen() % 3 == 0) out.reserve(gen() % 9);
        const uint32_t n = gen() % 4;
        for (uint32_t i = 0; i < n; ++i) {
            DiffusionWall w;
            for (uint32_t &x : w.words) x = gen();
            out.push_back(w);
        }
        if (side == 0)
            original(&m, &out);
        else
            (m.*ours)(&out);
        Blob o;
        Vector(o, out);
        Append(o, Dump(m));
        return o;
    });
}
} // namespace

DIFF_TEST(sg_wall_entry_collect_walls2, 0x40c620)
{
    CollectCases(ctx, "collect walls2", 0x40c620, &cWallDiffusionManager::CollectWalls2);
}

DIFF_TEST(sg_wall_entry_collect_walls, 0x40c650)
{
    CollectCases(ctx, "collect walls", 0x40c650, &cWallDiffusionManager::CollectWalls);
}

// ---- Reset ------------------------------------------------------------------------------------------------------------

// Reset 0x40eff0 on a manager of up to 5 runs of 0-4 segments: the manager whole (both colour blocks of every segment,
// the change lists' sizes and capacities).
DIFF_TEST(sg_wall_entry_reset, 0x40eff0)
{
    Fixture fixture;
    using Pass = void(__thiscall *)(void *);
    auto original = ctx.Original<Pass>(0x40eff0);
    SidesAtBothPrecisions(ctx, "reset", 400, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = 5;
        shape.maxChanges = 5;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        if (side == 0)
            original(&m);
        else
            m.Reset();
        return Dump(m);
    });
}

// ---- Mark -------------------------------------------------------------------------------------------------------------

// Mark 0x40c800 on a manager of up to 5 runs of 1-4 segments: the level a run's answer's low half (with junk above it),
// 0 with junk above (the outside runs'), or junk; the colour from wallfx::Channel (rounding points, the magic window's
// edges, NaNs, infinities). The stand-in's log, then the manager whole.
DIFF_TEST(sg_wall_entry_mark, 0x40c800)
{
    Fixture fixture;
    QueryModes modes;
    using MarkFn = void(__thiscall *)(void *, int32_t, const float *);
    auto original = ctx.Original<MarkFn>(0x40c800);
    SidesAtBothPrecisions(ctx, "mark", 1500, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = 5;
        shape.minSegments = 1;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x40c800u);
        gQuerySalt = seed;
        int32_t level = int32_t(gen());
        switch (gen() % 4) {
        case 0:
        case 1:
            if (!m.mRuns.empty()) {
                const uint32_t h = StandCellQuery(m.mRuns[gen() % m.mRuns.size()].mSegments[0].mLocation, 0);
                level = int32_t((gen() & 0xffff0000u) | (h & 0xffffu));
            }
            break;
        case 2: level = int32_t(gen() & 0xffff0000u); break;
        default: break;
        }
        float rgb[3];
        Channels(rgb, gen);
        if (gen() % 4 == 0) rgb[gen() % 3] = float(int(gen() % 520) - 260) / 256.0f;
        gLog.clear();
        if (side == 0)
            original(&m, level, rgb);
        else
            m.Mark(level, rgb);
        Blob o = gLog;
        for (float f : rgb) o.push_back(U(f));
        Append(o, Dump(m));
        return o;
    });
}

// ---- the segment search -----------------------------------------------------------------------------------------------

// FindSegment 0x40f960 on a manager of up to 5 runs of 1-4 segments, some locations shared (the first match wins), the
// target a segment's location, one word off, a run's level word only, or junk; `copy` null or a junk block. The return
// (as out or not), out, the copy, the manager whole.
DIFF_TEST(sg_wall_entry_find_segment, 0x40f960)
{
    Fixture fixture;
    using Find = WallSegmentRef *(__thiscall *)(void *, WallSegmentRef *, const uint32_t *, WallSegmentRef *);
    auto original = ctx.Original<Find>(0x40f960);
    SidesAtBothPrecisions(ctx, "find segment", 2000, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = 5;
        shape.minSegments = 1;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x40f960u);
        ShareLocations(m, gen);
        uint32_t location[5];
        Target(m, gen, location);
        WallSegmentRef out{int32_t(gen()), int32_t(gen()), int32_t(gen())};
        WallSegmentRef copyBlock{int32_t(gen()), int32_t(gen()), int32_t(gen())};
        WallSegmentRef *copy = gen() % 2 ? &copyBlock : nullptr;
        WallSegmentRef *ret = side == 0 ? original(&m, &out, location, copy) : m.FindSegment(&out, location, copy);
        Blob o{ret == &out, uint32_t(out.mLevel), uint32_t(out.mRun), uint32_t(out.mSegment), uint32_t(copyBlock.mLevel),
               uint32_t(copyBlock.mRun), uint32_t(copyBlock.mSegment)};
        Append(o, Dump(m));
        return o;
    });
}

// ---- the wall colour --------------------------------------------------------------------------------------------------

// GetColor 0x410320 on a manager of up to 5 runs of 1-4 segments (settings with NaNs, mOutsideAmbientEnabled drawn): a wall piece (two
// float triples) whose cell (worldrect::CellOfRect, ours and verified) is planted in a segment, its run's level word set
// to the cell's, or not planted (no segment: zeros). The return, the three words and a fourth left as it was, the
// stand-in's log, the manager whole.
DIFF_TEST(sg_wall_entry_get_color, 0x410320)
{
    Fixture fixture;
    QueryModes modes;
    using Color = float *(__thiscall *)(void *, float *, const float *);
    auto original = ctx.Original<Color>(0x410320);
    SidesAtBothPrecisions(ctx, "get color", 2000, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = 5;
        shape.minSegments = 1;
        shape.nans = true;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x410320u);
        gQuerySalt = seed;
        float wall[6];
        for (int i = 0; i < 6; ++i) {
            if (i % 3 == 2)
                wall[i] = Fl(gen() % 4);
            else
                wall[i] = gen() % 3 == 0 ? float(gen() % 64) + float(gen() % 3) * 0.5f : Nice(gen, 64.0f) + 64.0f;
        }
        int32_t cell[5];
        worldrect::CellOfRect(cell, wall);
        if (!m.mRuns.empty() && gen() % 5 != 0) {
            WallDiffusionRun &r = m.mRuns[gen() % m.mRuns.size()];
            std::memcpy(r.mSegments[gen() % r.mSegments.size()].mLocation, cell, sizeof(cell));
            r.mSegments[0].mLocation[2] = uint32_t(cell[2]);
            if (gen() % 4 == 0) ShareLocations(m, gen);
        }
        if (gen() % 3 == 0) m.mOutsideAmbientEnabled = uint8_t(gen() % 2);
        float out[4] = {Fl(gen()), Fl(gen()), Fl(gen()), Fl(gen())};
        gLog.clear();
        float *ret = side == 0 ? original(&m, out, wall) : m.GetColor(out, wall);
        Blob o{ret == out};
        for (float f : out) o.push_back(U(f));
        Append(o, gLog);
        Append(o, Dump(m));
        return o;
    });
}
