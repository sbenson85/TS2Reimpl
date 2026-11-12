// Differential tests of nTSSG::cFloorColorGrid's rebuild (lane 261110b, scenegraph LD2g; src/sg/cFloorColorGrid.h):
// BuildCells 0x409a20 and Rebuild 0x40ac20, the second through the first and LD2c's MarkFloorTiles.
//
// The world stands in as tests/sg_floor_color_world.h's CellWorld: the world DB 0x20e0b2 (also the cached world DB
// cell, for the tile box), its partitions (39; 43 a partition on a tile side, 50 the side's catalog id, 80 the level id
// at a point), the catalog's entries (slot 38) and the answer table 0x7e37e0 (behind 0x7defd0 and Lookup9), every
// answer hashed from the case, the question and the question count, every question logged. The stopwatch's Restart
// 0x1c95ec stands in, counted. Each side builds its grid from the seed (tests/sg_floor_color_grid.h's light-pass
// shape, a third of the links unlit), runs, and gives its grid, its links' every byte that is set, the world's log and
// the restarts; the two are compared, at the x87's 53-bit and 24-bit precision.
//
// The Rebuild test also covers what Rebuild alone reaches (in effect through callers): the element constructors
// 0x405810 (FloorColorSample), 0x405850 (FloorLightHalf) and 0x4058b0 (FloorColorLink), and the light vector's growth
// 0x4054c0 (_Resize_reallocate) and 0x405680 (_Uninitialized_value_construct_n).
#include "sg_floor_color_grid.h"
#include "worldfx.h"
#include "sg_floor_color_world.h"
#include "sg_lighting_common.h"

#include "sg/cFloorColorGrid.h"

using floorfx::Fixture;
using floorfx::Shape;
using floorworld::CellWorld;
using nTSSG::cFloorColorGrid;
using nTSSG::FloorColorLink;

namespace {

// One side's grid: constructed by ours, filled from the seed with the light-pass shape, destroyed by ours.
struct Grid {
    floorfx::Storage st{1};
    explicit Grid(uint32_t seed)
    {
        Shape shape;
        shape.lightable = true;
        cFloorColorGrid::Construct(st.bytes, nullptr);
        floorfx::Fill(*st.Get(), seed, shape);
    }
    ~Grid() { cFloorColorGrid::Destroy(st.Get(), nullptr); }
    cFloorColorGrid &operator*() { return *st.Get(); }
};

// A third of the links unlit (the fill leaves the lit byte random), so BuildCells skips some tiles.
void Unlight(cFloorColorGrid &g, std::mt19937 &gen)
{
    for (FloorColorLink &l : g.mLinks)
        if (gen() % 3 == 0) l.m0 = int16_t(uint16_t(l.m0) & 0xff00u);
}

// The links' quadrant masks (+2, +3, which floorfx::Dump leaves out with the padding): every link's when `all`,
// otherwise the lit links' (Rebuild's new links have whatever its stack held there until BuildCells sets them).
void Quads(svcfx::Blob &b, const cFloorColorGrid &g, bool all)
{
    for (const FloorColorLink &l : g.mLinks)
        if (all || (uint16_t(l.m0) & 0xff)) b.push_back(uint32_t(l.mGroundQuads) | uint32_t(l.mUpperQuads) << 8);
}

svcfx::Blob Outcome(const cFloorColorGrid &g, const CellWorld &world, bool all)
{
    svcfx::Blob b = floorfx::Dump(g);
    Quads(b, g, all);
    b.push_back(0x10c0u); // the world's log after this
    b.insert(b.end(), world.log.begin(), world.log.end());
    b.push_back(uint32_t(floorfx::gRestarts));
    return b;
}

} // namespace

// ---- BuildCells 0x409a20 ----

DIFF_TEST(sg_floor_color_grid_build_cells, 0x409a20)
{
    Fixture fixture;
    CellWorld world;
    using Fn = void(__thiscall *)(void *);
    auto original = ctx.Original<Fn>(0x409a20);
    SidesAtBothPrecisions(ctx, "BuildCells", 1500, false, [&](uint32_t draw, int s) {
        std::mt19937 gen(draw ^ 0xb11dc);
        Grid grid(draw);
        Unlight(*grid, gen);
        world.Case(draw, gen, (*grid).mWidth, (*grid).mHeight, gen() % 4 == 0);
        world.Side();
        floorfx::gRestarts = 0;
        if (s == 0)
            original(&*grid);
        else
            (*grid).BuildCells();
        return Outcome(*grid, world, true);
    });
}

// ---- Rebuild 0x40ac20 ----

DIFF_TEST(sg_floor_color_grid_rebuild, 0x40ac20)
{
    Fixture fixture;
    CellWorld world;
    using Fn = void(__thiscall *)(void *);
    auto original = ctx.Original<Fn>(0x40ac20);
    SidesAtBothPrecisions(ctx, "Rebuild", 800, false, [&](uint32_t draw, int s) {
        std::mt19937 gen(draw ^ 0x4eb1d);
        Grid grid(draw);
        // The whole grid's box (the game's: the world's tiles), so every tile MarkFloorTiles lights BuildCells visits.
        world.Case(draw, gen, (*grid).mWidth, (*grid).mHeight, true);
        world.Side();
        floorfx::gRestarts = 0;
        if (s == 0)
            original(&*grid);
        else
            (*grid).Rebuild();
        return Outcome(*grid, world, false);
    });
}
