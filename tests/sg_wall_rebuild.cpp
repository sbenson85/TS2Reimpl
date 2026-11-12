// Differential tests of row LD2k (lane 261110h; src/sg/cWallDiffusionManager.h): the manager's rebuild from the wall
// graph. AddSegment 0x40c680, BuildRuns 0x411ca0, BuildAll 0x4120d0, LinkLevels 0x40d850 and Rebuild 0x412240, original
// (by RVA) against ours, every case at the x87's 53-bit and 24-bit precisions.
// - The wall graph is real: our cWallVertex / cWallSection objects (the original calls them through our vtables, as the
//   game would after WL-0d), sections between lattice points (sides, uprights, one diagonal a cell), the positions now
//   and then an ulp or a hundredth off (atan2 angles, near misses) and each vertex's edges spliced counter-clockwise
//   (Guibas-Stolfi), so the chains walk real faces. Each side builds its own from the seed; edge flags drawn.
// - Stand-ins on both sides, logging into worldfx::gLog in call order: the world's level 0x7f2260 (the location's words
//   and the flag; the answer the level word, now and then another, with junk above the low half), the partitions'
//   service 0x20de97 (a worldfx::Fake: layers of kinds 3, 8, 0xa, 0xb and 0 with their sections, the 0x18-byte segment
//   records and their catalog ids, every slot logged), the world 0x20e0b2 (the worldgrid db of tests/worldgrid.h, its
//   levels drawn, slot 52 answering from a hash). Ground heights (WithGroundHeight 0x7dddc0, ours and the original both
//   verified) run against the worldgrid db, whose tile slots log what they were asked.
// - The outcome: the log, the manager (runs' size and capacity, each run's segments' size, capacity and fields, the
//   colour blocks of segments the case did not make: a new segment's mPacked / mCurrent / mPrevious are the constructor's
//   stack junk on both sides, and the padding never), and every edge's flag.
#include "sg_wall_diffusion.h"
#include "worldgrid.h"

#include "world/cWallSection.h"
#include "world/cWallVertex.h"

#include <cmath>
#include <memory>
#include <utility>

using namespace wallfx;
using nTSWorld::cIWallEdge;
using nTSWorld::cIWallSection;
using nTSWorld::cWallSection;
using nTSWorld::cWallVertex;
using worldfx::Fake;

namespace {

void Log(uint32_t w) { worldfx::gLog.push_back(w); }
void Append(Blob &o, const Blob &b) { o.insert(o.end(), b.begin(), b.end()); }

// ---- the wall graph ---------------------------------------------------------------------------------------------------

struct Graph {
    std::vector<std::unique_ptr<cWallVertex>> vertices;
    std::vector<std::unique_ptr<cWallSection>> sections; // destroyed first
    std::vector<std::vector<std::pair<double, cIWallEdge *>>> rings;
    cIWallSection *Section(size_t i) { return sections[i % sections.size()].get(); }
};

void Splice(cIWallEdge *a, cIWallEdge *b)
{
    cIWallEdge *const alpha = a->Onext()->Rot();
    cIWallEdge *const beta = b->Onext()->Rot();
    cIWallEdge *const an = a->Onext(), *const bn = b->Onext(), *const alphan = alpha->Onext(), *const betan = beta->Onext();
    a->SetOnext(bn);
    b->SetOnext(an);
    alpha->SetOnext(betan);
    beta->SetOnext(alphan);
}

// The isolated edge e put into vertex v's ring after its counter-clockwise predecessor.
void Attach(Graph &g, size_t v, cIWallEdge *e, double angle)
{
    auto &ring = g.rings[v];
    if (!ring.empty()) {
        const std::pair<double, cIWallEdge *> *pred = nullptr;
        for (const auto &p : ring)
            if (p.first < angle && (!pred || p.first > pred->first)) pred = &p;
        if (!pred)
            for (const auto &p : ring)
                if (!pred || p.first > pred->first) pred = &p;
        Splice(pred->second, e);
    }
    ring.push_back({angle, e});
}

void AddSection(Graph &g, size_t u, size_t v, bool flip)
{
    if (flip) std::swap(u, v);
    auto s = std::make_unique<cWallSection>();
    cIWallEdge *const e0 = s->EdgeAt(0);
    cIWallEdge *const e2 = s->EdgeAt(2);
    e0->SetVertex(g.vertices[u].get());
    e0->SetDestVertex(g.vertices[v].get());
    const float *a = g.vertices[u]->mPos, *b = g.vertices[v]->mPos;
    Attach(g, u, e0, std::atan2(double(b[1]) - a[1], double(b[0]) - a[0]));
    Attach(g, v, e2, std::atan2(double(a[1]) - b[1], double(a[0]) - b[0]));
    s->SetId(int32_t(g.sections.size()));
    g.sections.push_back(std::move(s));
}

float Jitter(std::mt19937 &gen, float x)
{
    switch (gen() % 12) {
    case 0: return std::nextafter(x, 1e30f);
    case 1: return std::nextafter(x, -1e30f);
    case 2: return x + 0.01f;
    case 3: return x - 0.02f;
    default: return x;
    }
}

// An n x n cell lattice (n 1-3) at a drawn origin and level: each side and upright a section (two in three), each cell
// one diagonal or none, each drawn either way round; at least one section. Edge flags: now and then 1, 2 or 0x80 on a
// section's edge 0 or 2.
void Build(Graph &g, std::mt19937 &gen)
{
    const int n = 1 + int(gen() % 3);
    const float ox = float(int(gen() % 5) - 1), oy = float(int(gen() % 5) - 1);
    const int32_t level = int32_t(gen() % 4) - 1;
    // One lattice in three bent: row i rises by bend * i (i - 1) / 2, so neighbouring sides' angles differ by about
    // `bend` (within the 0.03 pi tolerance) and sides two apart by twice that (beyond it): the walk back compares each
    // edge with the side's own angle, not the last edge taken.
    const bool bent = gen() % 3 == 0;
    const float bend = bent ? 0.06f : 0.0f;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            float p[3] = {Jitter(gen, ox + float(i)), Jitter(gen, oy + float(j) + bend * float(i * (i - 1) / 2)), 0.0f};
            const int32_t lv = gen() % 10 == 0 ? level + 1 : level;
            std::memcpy(&p[2], &lv, 4);
            g.vertices.push_back(std::make_unique<cWallVertex>(p));
        }
    g.rings.resize(g.vertices.size());
    auto at = [&](int i, int j) { return size_t(j * (n + 1) + i); };
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            if (i < n && gen() % 3) AddSection(g, at(i, j), at(i + 1, j), gen() % 2);
            if (j < n && gen() % 3) AddSection(g, at(i, j), at(i, j + 1), gen() % 2);
            if (i < n && j < n) switch (gen() % 4) {
                case 0: AddSection(g, at(i, j), at(i + 1, j + 1), gen() % 2); break;
                case 1: AddSection(g, at(i + 1, j), at(i, j + 1), gen() % 2); break;
                default: break;
                }
        }
    if (g.sections.empty()) AddSection(g, at(0, 0), at(1, 0), gen() % 2);
    static const uint8_t kFlags[] = {1, 2, 0x80};
    for (auto &s : g.sections)
        for (uint32_t k : {0u, 2u})
            if (gen() % 8 == 0) s->EdgeAt(k)->SetFlag(kFlags[gen() % 3]);
}

void Flags(Blob &b, const Graph &g)
{
    for (const auto &s : g.sections)
        for (const auto &e : s->mEdges) b.push_back(e.mFlag);
}

// ---- the stand-ins ----------------------------------------------------------------------------------------------------

// The world's level 0x7f2260: the location's level word in the low half (now and then another), junk above.
uint32_t gLevelSalt = 0;
uint32_t __cdecl StandLevel(const uint32_t *location, uint32_t flag)
{
    Log(0x7f2260u);
    uint32_t h = gLevelSalt ^ 0x7f2260u;
    for (int i = 0; i < 5; ++i) {
        Log(location[i]);
        h = Mix(h, location[i]);
    }
    Log(flag);
    const uint32_t low = h % 5 == 0 ? (h >> 7) & 0xffffu : location[2] & 0xffffu;
    return (h & 0xffff0000u) | low;
}

// The partitions: five layers (kinds 3, 8, 0xa, 0xb, then 0), each its section count (slot 15, with negative ones) and
// sections (slot 11); the segment records {catalog id, section} (slots 55, 56, 50), layer 0's section of a record
// (slot 12, null for an index past the graph's).
struct LayerPlan {
    uint32_t count = 0;
    std::vector<uint32_t> sections;
};
struct PartsPlan {
    LayerPlan layers[5];
    std::vector<std::pair<uint32_t, uint32_t>> segments;
};
PartsPlan gPlan;
Graph *gGraph = nullptr;
Fake gParts, gLayer[5];

void DrawPlan(std::mt19937 &gen, uint32_t sections)
{
    for (LayerPlan &l : gPlan.layers) {
        switch (gen() % 6) {
        case 0: l.count = 0; break;
        case 1: l.count = gen() % 2 ? 0xffffffffu : 0x80000000u; break;
        default: l.count = 1 + gen() % 3; break;
        }
        l.sections.clear();
        for (uint32_t i = 0; int32_t(i) < int32_t(l.count); ++i) l.sections.push_back(gen() % sections);
    }
    static const uint32_t kIds[] = {0x1d, 0x1d, 0x1d, 0x1c, 0x1e, 0x11d, 0};
    gPlan.segments.clear();
    const uint32_t n = gen() % 6;
    for (uint32_t i = 0; i < n; ++i) gPlan.segments.push_back({kIds[gen() % std::size(kIds)], gen() % (sections + 1)});
}

uint32_t LayerIndex(uint32_t kind)
{
    switch (kind) {
    case 3: return 0;
    case 8: return 1;
    case 0xa: return 2;
    case 0xb: return 3;
    default: return 4;
    }
}

void *__cdecl StandPartitions()
{
    Log(0x20de97u);
    return &gParts;
}
uint32_t PartsLayer(Fake *, const uint32_t *a)
{
    Log(40);
    Log(a[0]);
    return P(&gLayer[LayerIndex(a[0])]);
}
uint32_t PartsCount(Fake *, const uint32_t *)
{
    Log(55);
    return uint32_t(gPlan.segments.size());
}
uint32_t PartsSegment(Fake *, const uint32_t *a)
{
    Log(56);
    Log(a[1]);
    auto *buf = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    const auto &s = gPlan.segments[a[1] % gPlan.segments.size()];
    buf[0] = s.first;
    buf[1] = s.second;
    // The game's slot 56 (0x81a130) writes six words: a caller that reserves fewer loses its frame.
    for (uint32_t k = 2; k < 6; ++k) buf[k] = 0x56000000u | (a[1] << 8) | k;
    return 0xdead0056u;
}
uint32_t PartsCatalog(Fake *, const uint32_t *a)
{
    const auto *buf = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    Log(50);
    for (uint32_t k = 0; k < 6; ++k) Log(buf[k]);
    return buf[0];
}
template <unsigned kSlot> uint32_t LayerLogged(Fake *self, const uint32_t *)
{
    Log(self->id);
    Log(kSlot);
    return 0x5a5a0000u | kSlot;
}
uint32_t LayerCount(Fake *self, const uint32_t *)
{
    Log(self->id);
    Log(15);
    return gPlan.layers[self->id - 10].count;
}
uint32_t LayerSection(Fake *self, const uint32_t *a)
{
    Log(self->id);
    Log(11);
    Log(a[0]);
    const LayerPlan &l = gPlan.layers[self->id - 10];
    return P(gGraph->Section(l.sections.empty() ? 0 : l.sections[a[0] % l.sections.size()]));
}
uint32_t LayerSegment(Fake *self, const uint32_t *a)
{
    const auto *buf = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    Log(self->id);
    Log(12);
    for (uint32_t k = 0; k < 6; ++k) Log(buf[k]);
    return buf[1] < gGraph->sections.size() ? P(gGraph->Section(buf[1])) : 0u;
}

// The world 0x20e0b2: the worldgrid db, its slot 52 (HasFloorOrGround: tile, quadrant byte, flag) logged and answered
// from a hash (0, 1 or 0x80 in the low byte, junk above).
uint32_t gFloorSalt = 0;
void *__cdecl StandWorld()
{
    Log(0x20e0b2u);
    return &worldfx::gWorldDb;
}
uint32_t FloorOrGround(Fake *, const uint32_t *a)
{
    const auto *t = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    const uint32_t q[] = {52u, t[0], t[1], t[2], t[3] & 0xffu, a[1] & 0xffu, a[2] & 0xffu};
    uint32_t h = gFloorSalt ^ 0x52u;
    for (uint32_t w : q) {
        Log(w);
        h = Mix(h, w);
    }
    const uint32_t low = h % 2 ? 0u : (h & 0x100u ? 1u : 0x80u);
    return (h & 0xffffff00u) | low;
}

// Everything the cases need, on both sides: the game heap (wallfx::Fixture), the partitions, the level, the world (and
// the worldgrid db in the ground heights' cell). worldfx's own call log is off (its entries hold pointers).
struct Rig {
    Fixture fixture;
    worldfx::Scope scope;
    shape::CodePatch parts{0x20de97u, shape::CodeOf(&StandPartitions)};
    shape::CodePatch level{0x7f2260u, shape::CodeOf(&StandLevel)};
    shape::CodePatch world{0x20e0b2u, shape::CodeOf(&StandWorld)};
    bool logging;
    Rig()
    {
        logging = worldfx::gLogging;
        worldfx::gLogging = false;
        gParts.Reset(1);
        gParts.Set(40, 1, 0, &PartsLayer);
        gParts.Set(55, 0, 0, &PartsCount);
        gParts.Set(56, 2, 0, &PartsSegment);
        gParts.Set(50, 1, 0, &PartsCatalog);
        for (uint32_t k = 0; k < 5; ++k) {
            Fake &l = gLayer[k];
            l.Reset(10 + k);
            l.Set(1, 0, 0, &LayerLogged<1>);
            l.Set(2, 0, 0, &LayerLogged<2>);
            l.Set(14, 0, 0, &LayerLogged<14>);
            l.Set(15, 0, 0, &LayerCount);
            l.Set(11, 1, 0, &LayerSection);
            l.Set(12, 1, 0, &LayerSegment);
        }
        worldgrid::ProgramWorld();
        worldfx::gWorldDb.Set(52, 3, 0, &FloorOrGround);
        scope.Set(worldfx::kWorldDbCell, &worldfx::gWorldDb);
    }
    ~Rig()
    {
        worldfx::gLogging = logging;
        gGraph = nullptr;
    }
    Rig(const Rig &) = delete;
    Rig &operator=(const Rig &) = delete;
};

// A case's draws common to the sides: the world, the salts.
void DrawWorld(std::mt19937 &gen, uint32_t seed)
{
    worldgrid::RandomWorld(gen);
    gLevelSalt = seed;
    gFloorSalt = seed * 3u + 1u;
}

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

// ---- dumps ------------------------------------------------------------------------------------------------------------

// A segment: its ends, location, references and flag; the colour blocks with `blocks` (segments the case did not make).
void DumpKnown(Blob &b, const WallDiffusionSegment &s, bool blocks)
{
    Words(b, &s, offsetof(WallDiffusionSegment, mPacked));
    if (blocks) Words(b, s.mPacked, offsetof(WallDiffusionSegment, mAbove) - offsetof(WallDiffusionSegment, mPacked));
    Words(b, &s.mAbove, offsetof(WallDiffusionSegment, mFlag) - offsetof(WallDiffusionSegment, mAbove));
    b.push_back(s.mFlag);
}

// A run: its segments' size and capacity, the segments (the first `old` with their blocks), then prev, next, angle, flags.
void DumpRunKnown(Blob &b, const WallDiffusionRun &r, size_t old)
{
    b.push_back(uint32_t(r.mSegments.size()));
    b.push_back(uint32_t(r.mSegments.capacity()));
    for (size_t i = 0; i < r.mSegments.size(); ++i) DumpKnown(b, r.mSegments[i], i < old);
    Words(b, &r.mPrev, sizeof(WallDiffusionRun) - offsetof(WallDiffusionRun, mPrev));
}

// The manager: the runs (the first `oldRuns` whole), then its vtable class, count, settings, key set size, change lists.
Blob DumpBuilt(const cWallDiffusionManager &m, size_t oldRuns)
{
    Blob b;
    b.push_back(uint32_t(m.mRuns.size()));
    b.push_back(uint32_t(m.mRuns.capacity()));
    for (size_t r = 0; r < m.mRuns.size(); ++r) DumpRunKnown(b, m.mRuns[r], r < oldRuns ? m.mRuns[r].mSegments.size() : 0);
    b.push_back(VTableClass(&m));
    b.push_back(m.mRefCount);
    Words(b, &m.mThreshold, offsetof(cWallDiffusionManager, mMarkedRuns) - offsetof(cWallDiffusionManager, mThreshold));
    b.push_back(uint32_t(m.mMarkedRuns.size()));
    Vector(b, m.mChanges);
    Vector(b, m.mChanges2);
    return b;
}

} // namespace

// ---- AddSegment -------------------------------------------------------------------------------------------------------

// AddSegment 0x40c680 (ret 0xc): a run of 0-5 segments (spare capacity now and then) given 1-6 more, the ends any floats
// (NaNs of their own payloads), the location any words: the log (one 0x20de97 a call), the run (its segments' size,
// capacity and fields, the old ones' blocks too).
DIFF_TEST(sg_wall_rebuild_add_segment, 0x40c680)
{
    Rig rig;
    using Add = void(__thiscall *)(void *, const float *, const float *, const uint32_t *);
    auto original = ctx.Original<Add>(0x40c680);
    SidesAtBothPrecisions(ctx, "add segment", 500, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x40c680u);
        Shape shape;
        shape.maxSegments = 5;
        shape.nans = true;
        WallDiffusionRun run;
        FillRun(run, gen, shape, 4);
        const size_t old = run.mSegments.size();
        worldfx::gLog.clear();
        const uint32_t n = 1 + gen() % 6;
        for (uint32_t k = 0; k < n; ++k) {
            float start[3], end[3];
            uint32_t location[5];
            for (float &f : start) f = Value(gen, shape);
            for (float &f : end) f = Value(gen, shape);
            for (uint32_t &w : location) w = gen() % 3 ? gen() % 64 : gen();
            if (side == 0)
                original(&run, start, end, location);
            else
                run.AddSegment(start, end, location);
        }
        Blob o = worldfx::gLog;
        DumpRunKnown(o, run, old);
        return o;
    });
}

// ---- BuildRuns --------------------------------------------------------------------------------------------------------

// BuildRuns 0x411ca0 (ret 4): a manager of 0-3 runs (linked, angles near the directions), then 1-4 sections of a drawn
// graph (a section again now and then: its edges are flagged by then) built in turn.
DIFF_TEST(sg_wall_rebuild_build_runs, 0x411ca0)
{
    Rig rig;
    using BuildFn = void(__thiscall *)(void *, cIWallSection *);
    auto original = ctx.Original<BuildFn>(0x411ca0);
    SidesAtBothPrecisions(ctx, "build runs", 500, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x411ca0u);
        DrawWorld(gen, seed);
        Shape shape;
        shape.maxRuns = 3;
        shape.linked = true;
        shape.directions = true;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        Graph g;
        Build(g, gen);
        gGraph = &g;
        const size_t oldRuns = m.mRuns.size();
        worldfx::gLog.clear();
        const uint32_t n = 1 + gen() % 4;
        for (uint32_t k = 0; k < n; ++k) {
            cIWallSection *const section = g.Section(gen());
            if (side == 0)
                original(&m, section);
            else
                m.BuildRuns(section);
        }
        Blob o = worldfx::gLog;
        Append(o, DumpBuilt(m, oldRuns));
        Flags(o, g);
        return o;
    });
}

// ---- BuildAll ---------------------------------------------------------------------------------------------------------

// BuildAll 0x4120d0: a manager of 0-3 runs, a drawn graph and partitions plan (each layer 0-3 sections or a negative
// count; 0-5 segment records, most of them walls, some past the graph).
DIFF_TEST(sg_wall_rebuild_build_all, 0x4120d0)
{
    Rig rig;
    using Pass = void(__thiscall *)(void *);
    auto original = ctx.Original<Pass>(0x4120d0);
    SidesAtBothPrecisions(ctx, "build all", 500, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x4120d0u);
        DrawWorld(gen, seed);
        Shape shape;
        shape.maxRuns = 3;
        shape.linked = true;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        Graph g;
        Build(g, gen);
        gGraph = &g;
        DrawPlan(gen, uint32_t(g.sections.size()));
        const size_t oldRuns = m.mRuns.size();
        worldfx::gLog.clear();
        if (side == 0)
            original(&m);
        else
            m.BuildAll();
        Blob o = worldfx::gLog;
        Append(o, DumpBuilt(m, oldRuns));
        Flags(o, g);
        return o;
    });
}

// ---- LinkLevels -------------------------------------------------------------------------------------------------------

// LinkLevels 0x40d850: a manager of 0-5 runs of 0-4 segments whose locations lie in a small space (x, y 0-2, the level
// one below the world's least to one above its greatest, the masks the sides and diagonals, now and then a junk word), so
// the searches one level up and down find segments; the world's floors from the hash. The log and the manager whole.
DIFF_TEST(sg_wall_rebuild_link_levels, 0x40d850)
{
    Rig rig;
    using Pass = void(__thiscall *)(void *);
    auto original = ctx.Original<Pass>(0x40d850);
    SidesAtBothPrecisions(ctx, "link levels", 800, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x40d850u);
        DrawWorld(gen, seed);
        Shape shape;
        shape.maxRuns = 5;
        shape.minSegments = 1; // the search reads each run's first segment unchecked (as BuildRuns leaves no run empty)
        shape.linked = true;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        static const uint32_t kMasks[] = {1, 2, 4, 8, 0x10, 0x20, 0, 0x30};
        const int32_t span = worldgrid::gWorld.maxLevel - worldgrid::gWorld.minLevel + 3;
        for (WallDiffusionRun &r : m.mRuns)
            for (WallDiffusionSegment &s : r.mSegments) {
                s.mLocation[0] = gen() % 3;
                s.mLocation[1] = gen() % 3;
                s.mLocation[2] = uint32_t(worldgrid::gWorld.minLevel - 1 + int32_t(gen() % uint32_t(span)));
                s.mLocation[3] = kMasks[gen() % std::size(kMasks)];
                s.mLocation[4] = gen() % 3;
                if (gen() % 12 == 0) s.mLocation[gen() % 5] = gen();
            }
        worldfx::gLog.clear();
        if (side == 0)
            original(&m);
        else
            m.LinkLevels();
        Blob o = worldfx::gLog;
        Append(o, Dump(m));
        return o;
    });
}

// ---- Rebuild ----------------------------------------------------------------------------------------------------------

// Rebuild 0x412240: a manager of 0-4 runs (cleared: the vector's capacity stays), a drawn graph and partitions plan, the
// world's levels and floors; the runs built and linked across levels.
DIFF_TEST(sg_wall_rebuild_rebuild, 0x412240)
{
    Rig rig;
    using Pass = void(__thiscall *)(void *);
    auto original = ctx.Original<Pass>(0x412240);
    SidesAtBothPrecisions(ctx, "rebuild", 500, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x412240u);
        DrawWorld(gen, seed);
        Shape shape;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        Graph g;
        Build(g, gen);
        gGraph = &g;
        DrawPlan(gen, uint32_t(g.sections.size()));
        worldfx::gLog.clear();
        if (side == 0)
            original(&m);
        else
            m.Rebuild();
        Blob o = worldfx::gLog;
        Append(o, DumpBuilt(m, 0));
        Flags(o, g);
        return o;
    });
}
