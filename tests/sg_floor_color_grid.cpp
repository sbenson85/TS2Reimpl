// Differential tests of nTSSG::cFloorColorGrid (lane 261109i, scenegraph LD2a; src/sg/cFloorColorGrid.h): the
// constructor, the copy, the destructor, SetSize, the accessors, the colour setters, the three collectors, Reset and
// Mark. Each case builds the same grid on both sides from a seed (random scalars, cells, links, light cells, levels and
// spare capacity) on a block filled alike, runs the image's function on one and ours on the other, and compares the
// grid: its first 0x50 bytes (the vtable word included), each vector's size, capacity and elements, +0x68, the level
// set's tree (each node's value and colour, in shape) and +0x80. The game's operator new / delete go to ours, so either
// side frees the other's blocks; the destructor and constructor compare the heap traffic (difftest::AllocationCounter).
#include "sg_floor_color_grid.h"

#include "sg/cLightMapManager.h"

using namespace floorfx;

// ---- the constructor, the copy and the destructor --------------------------------------------------------------------

DIFF_TEST(sg_floor_color_grid_life, 0x4056d0)
{
    Fixture fixture;
    using Ctor = void *(__thiscall *)(void *);
    using Copy = void *(__thiscall *)(void *, const void *);
    using Dtor = void(__thiscall *)(void *);
    auto construct = ctx.Original<Ctor>(0x4056d0);
    auto copy = ctx.Original<Copy>(0x3c42c0);
    auto destroy = ctx.Original<Dtor>(0x4058e0);

    for (uint32_t seed = 0; seed < 64; ++seed) {
        Storage s[2]{Storage(seed), Storage(seed)};
        long long blocks[2], bytes[2];
        void *r[2];
        {
            difftest::AllocationCounter count;
            r[0] = construct(s[0].bytes);
            count.Stop();
            blocks[0] = count.Blocks();
            bytes[0] = count.Bytes();
        }
        {
            difftest::AllocationCounter count;
            r[1] = cFloorColorGrid::Construct(s[1].bytes, nullptr);
            count.Stop();
            blocks[1] = count.Blocks();
            bytes[1] = count.Bytes();
        }
        ++ctx.cases;
        if (r[0] != s[0].bytes || r[1] != s[1].bytes) ctx.Fail("construct seed %u: returns", seed);
        ++ctx.cases;
        if (blocks[0] != blocks[1] || bytes[0] != bytes[1])
            ctx.Fail("construct seed %u: heap %lld / %lld vs %lld / %lld", seed, blocks[0], bytes[0], blocks[1], bytes[1]);
        Compare(ctx, "construct", seed, Dump(*s[0].Get()), Dump(*s[1].Get()));
        for (auto &st : s) cFloorColorGrid::Destroy(st.Get(), nullptr);
    }

    for (uint32_t seed = 0; seed < 300; ++seed) {
        Shape shape;
        shape.key = int16_t(seed % 5) - 1;
        Storage source(seed);
        cFloorColorGrid::Construct(source.bytes, nullptr);
        Fill(*source.Get(), seed, shape);
        const Blob before = Dump(*source.Get());
        Storage s[2]{Storage(seed + 7), Storage(seed + 7)};
        void *r0 = copy(s[0].bytes, source.Get());
        void *r1 = cFloorColorGrid::CopyConstruct(s[1].bytes, nullptr, source.Get());
        ++ctx.cases;
        if (r0 != s[0].bytes || r1 != s[1].bytes) ctx.Fail("copy seed %u: returns", seed);
        Compare(ctx, "copy", seed, Dump(*s[0].Get()), Dump(*s[1].Get()));
        Compare(ctx, "copy source", seed, before, Dump(*source.Get()));
        for (auto &st : s) cFloorColorGrid::Destroy(st.Get(), nullptr);
        cFloorColorGrid::Destroy(source.Get(), nullptr);
    }

    // The destructor: on grids built alike, the vtable word junk first; it is stored again, and the same blocks go.
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Shape shape;
        Storage s[2]{Storage(seed), Storage(seed)};
        uint32_t junk = 0x5a5a0000u | seed;
        long long blocks[2], bytes[2];
        size_t frees[2];
        for (int side = 0; side < 2; ++side) {
            cFloorColorGrid::Construct(s[side].bytes, nullptr);
            Fill(*s[side].Get(), seed, shape);
            std::memcpy(s[side].bytes, &junk, 4);
            difftest::AllocationCounter count;
            if (side == 0)
                destroy(s[side].bytes);
            else
                cFloorColorGrid::Destroy(s[side].Get(), nullptr);
            count.Stop();
            blocks[side] = count.Blocks();
            bytes[side] = count.Bytes();
            frees[side] = count.Frees();
        }
        uint32_t vt[2];
        std::memcpy(&vt[0], s[0].bytes, 4);
        std::memcpy(&vt[1], s[1].bytes, 4);
        ++ctx.cases;
        if (vt[0] != vt[1] || vt[0] == junk) ctx.Fail("destroy seed %u: vtable word %#x vs %#x", seed, vt[0], vt[1]);
        ++ctx.cases;
        if (blocks[0] != blocks[1] || bytes[0] != bytes[1] || frees[0] != frees[1])
            ctx.Fail("destroy seed %u: heap %lld / %lld / %zu vs %lld / %lld / %zu", seed, blocks[0], bytes[0], frees[0],
                     blocks[1], bytes[1], frees[1]);
    }
}

// The scalar deleting destructor 0x3c4870 (the vtable's slot 0; coordinator, after 261109i moved it here): on grids built
// alike in heap blocks, the vtable word junk first, with flag words whose bit 0 is set or clear among other bits; it
// returns this, stores the vtable again, and frees the members' blocks, plus the grid's own 0x84 block when bit 0 is set.
DIFF_TEST(sg_floor_color_grid_deleting, 0x3c4870)
{
    Fixture fixture;
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    auto deleting = ctx.Original<Deleting>(0x3c4870);
    static constexpr uint32_t kFlags[] = {0, 1, 2, 3, 0xfffffffe, 0xffffffff, 0x80000001};
    for (uint32_t seed = 0; seed < 140; ++seed) {
        const uint32_t flags = kFlags[seed % std::size(kFlags)];
        Shape shape;
        shape.key = int16_t(seed % 5) - 1;
        const uint32_t junk = 0x5a5a0000u | seed;
        long long blocks[2], bytes[2];
        size_t frees[2];
        uint32_t vt[2] = {0, 0};
        void *r[2];
        void *block[2];
        for (int side = 0; side < 2; ++side) {
            block[side] = ::operator new(sizeof(cFloorColorGrid));
            std::mt19937 g(seed);
            RandomBytes(block[side], sizeof(cFloorColorGrid), g);
            cFloorColorGrid::Construct(block[side], nullptr);
            Fill(*static_cast<cFloorColorGrid *>(block[side]), seed, shape);
            std::memcpy(block[side], &junk, 4);
            difftest::AllocationCounter count;
            if (side == 0)
                r[side] = deleting(block[side], flags);
            else
                r[side] = static_cast<cFloorColorGrid *>(block[side])->DeletingDestructor(flags);
            count.Stop();
            blocks[side] = count.Blocks();
            bytes[side] = count.Bytes();
            frees[side] = count.Frees();
            if (!(flags & 1)) {
                std::memcpy(&vt[side], block[side], 4);
                ::operator delete(block[side]);
            }
        }
        ++ctx.cases;
        if (r[0] != block[0] || r[1] != block[1]) ctx.Fail("deleting seed %u flags %#x: returns", seed, flags);
        ++ctx.cases;
        if (!(flags & 1) && (vt[0] != vt[1] || vt[0] == junk))
            ctx.Fail("deleting seed %u flags %#x: vtable word %#x vs %#x", seed, flags, vt[0], vt[1]);
        ++ctx.cases;
        if (blocks[0] != blocks[1] || bytes[0] != bytes[1] || frees[0] != frees[1])
            ctx.Fail("deleting seed %u flags %#x: heap %lld / %lld / %zu vs %lld / %lld / %zu", seed, flags, blocks[0],
                     bytes[0], frees[0], blocks[1], bytes[1], frees[1]);
    }
}

// ---- SetSize, the accessors and the colour setters -------------------------------------------------------------------

// SetSize's resizes run the image's STL instantiations on the original side (the links' and cells' _Resize_reallocate
// 0x405210 / 0x405370, their value-initialisers 0x405610 / 0x405640) and our std::vector on ours: growing from an
// empty or smaller grid reaches all four, and the dump compares what they leave.

DIFF_TEST(sg_floor_color_grid_size, 0x4098d0)
{
    Fixture fixture;
    using SizeFn = void(__thiscall *)(void *, int32_t, uint32_t, uint32_t);
    auto setSize = ctx.Original<SizeFn>(0x4098d0);
    for (uint32_t seed = 0; seed < 500; ++seed) {
        Pair p(seed, Shape{});
        std::mt19937 gen(seed ^ 0x51ce);
        const int32_t level = gen() % 4 ? int32_t(gen() % 7) - 3 : int32_t(gen());
        const uint32_t w = gen() % 7, h = gen() % 7;
        const int rounds = 1 + gen() % 3;
        for (int i = 0; i < rounds; ++i) {
            const uint32_t ww = i ? gen() % 7 : w, hh = i ? gen() % 7 : h;
            setSize(p[0], level + i, ww, hh);
            p[1]->SetSize(level + i, ww, hh);
        }
        Compare(ctx, "SetSize", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

DIFF_TEST(sg_floor_color_grid_access, 0x40abe0)
{
    Fixture fixture;
    using At = void *(__thiscall *)(void *, uint32_t);
    using Index = uint32_t(__thiscall *)(void *, int32_t, int32_t);
    using SetColor = void(__thiscall *)(void *, const float *);
    auto cell = ctx.Original<At>(0x409880);
    auto link = ctx.Original<At>(0x40abd0);
    auto index = ctx.Original<Index>(0x40abe0);
    auto inside = ctx.Original<SetColor>(0x4098b0);
    auto outside = ctx.Original<SetColor>(0x409a00);
    static const int32_t kEdges[] = {0, -1, 1, 2, 3, 4, 5, 6, INT32_MIN, INT32_MAX, -2, 0x7ffffffe};
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Pair p(seed, Shape{});
        std::mt19937 gen(seed ^ 0xacce55);
        if (gen() % 6 == 0) p[0]->mWidth = p[1]->mWidth = gen() % 3 ? 0 : gen();
        if (gen() % 6 == 0) p[0]->mHeight = p[1]->mHeight = gen() % 3 ? 0 : gen();
        for (int i = 0; i < 40; ++i) {
            const uint32_t at = gen() % 3 ? gen() % 40 : gen();
            ++ctx.cases;
            if (cell(p[0], at) != p[0]->Cell(at) || link(p[0], at) != p[0]->Link(at))
                ctx.Fail("Cell / Link seed %u: index %#x", seed, at);
            const int32_t x = gen() % 2 ? kEdges[gen() % std::size(kEdges)] : int32_t(gen() % 9) - 2;
            const int32_t y = gen() % 2 ? kEdges[gen() % std::size(kEdges)] : int32_t(gen() % 9) - 2;
            ++ctx.cases;
            const uint32_t a = index(p[0], x, y), b = p[0]->CellIndex(x, y);
            if (a != b) ctx.Fail("CellIndex seed %u: (%d, %d) in %u x %u: %#x vs %#x", seed, x, y, p[0]->mWidth, p[0]->mHeight, a, b);
        }
        float colour[3] = {Nice(gen), Nice(gen), Nice(gen)};
        if (gen() % 2) {
            inside(p[0], colour);
            p[1]->SetInsideAmbientColor(colour);
        }
        for (float &f : colour) f = Nice(gen);
        if (gen() % 2) {
            outside(p[0], colour);
            p[1]->SetOutsideAmbientColor(colour);
        }
        Compare(ctx, "colour setters", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

// ---- the collectors, Reset and Mark -----------------------------------------------------------------------------------

namespace {

// The out list: its size, capacity and records, each record's padding (+0xd..+0xf) left out: the original's push
// writes the four fields alone into a free slot, and its reallocation (0x30d610) moves every record field by field, so
// the padding is whatever the block held.
Blob Tiles(const std::vector<DiffusionTile> &v)
{
    Blob b{uint32_t(v.size()), uint32_t(v.capacity())};
    for (const DiffusionTile &t : v) {
        b.insert(b.end(), t.words, t.words + 3);
        b.push_back(t.words[3] & 0xff);
    }
    return b;
}

} // namespace

DIFF_TEST(sg_floor_color_grid_collect, 0x405910)
{
    Fixture fixture;
    using Collect = void(__thiscall *)(void *, std::vector<DiffusionTile> *);
    using Pass = void(__thiscall *)(void *);
    const uint32_t kRvas[] = {0x405910, 0x405a00, 0x405ae0};
    void (cFloorColorGrid::*const kOurs[])(std::vector<DiffusionTile> *) = {
        &cFloorColorGrid::CollectTiles, &cFloorColorGrid::CollectTilesB, &cFloorColorGrid::CollectTilesA};
    auto reset = ctx.Original<Pass>(0x406f40);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Pair p(seed, Shape{});
        std::mt19937 gen(seed ^ 0xc011);
        const unsigned which = gen() % 3;
        auto original = ctx.Original<Collect>(kRvas[which]);
        std::vector<DiffusionTile> out[2];
        const size_t held = gen() % 5;
        const size_t spare = gen() % 3 ? 0 : gen() % 6;
        for (auto &o : out) {
            std::mt19937 fill(seed);
            o.reserve(held + spare);
            for (size_t i = 0; i < held; ++i) o.push_back({{fill(), fill(), fill(), fill()}});
        }
        original(p[0], &out[0]);
        (p[1]->*kOurs[which])(&out[1]);
        static const char *const kNames[] = {"CollectTiles", "CollectTilesB", "CollectTilesA"};
        Compare(ctx, kNames[which], seed, Tiles(out[0]), Tiles(out[1]));
        Compare(ctx, kNames[which], seed, Dump(*p[0]), Dump(*p[1]));
        if (gen() % 2) {
            reset(p[0]);
            p[1]->Reset();
            Compare(ctx, "Reset", seed, Dump(*p[0]), Dump(*p[1]));
        }
    }
}

DIFF_TEST(sg_floor_color_grid_mark, 0x405bc0)
{
    Fixture fixture;
    using MarkFn = void(__thiscall *)(void *, uint32_t, const float *);
    auto mark = ctx.Original<MarkFn>(0x405bc0);
    static const uint32_t kLevels[] = {0, 1, 0xffffffffu, 2, 0x10001u, 0xffff8000u, 0x7fff, 0x8000, 0x12340003u, 3};
    for (uint32_t seed = 0; seed < 800; ++seed) {
        std::mt19937 gen(seed ^ 0x3a7c);
        const uint32_t level = gen() % 4 ? kLevels[gen() % std::size(kLevels)] : gen();
        Shape shape;
        shape.markable = true;
        shape.key = gen() % 8 ? int16_t(level) : int16_t(gen());
        shape.nans = gen() % 4 == 0;
        Pair p(seed, shape);
        const int calls = 1 + gen() % 3;
        for (int i = 0; i < calls; ++i) {
            float amount[3];
            for (float &f : amount) f = shape.nans && gen() % 2 ? NaNOf(gen()) : Nice(gen);
            const uint32_t at = i ? (gen() % 2 ? level : kLevels[gen() % std::size(kLevels)]) : level;
            mark(p[0], at, amount);
            p[1]->Mark(at, amount);
        }
        Compare(ctx, "Mark", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

// ---- LD2b (lane 261109l): the light cells -----------------------------------------------------------------------------
//
// Bottom-up: the blob key, the neighbour lookup, the three shaders into a pixel buffer, PrepareLight, TakeBlobs, then
// the two light passes against a fake light map manager and fake images. Byte arguments (the shaders' levelZero and
// side, NeighbourSample's first) get junk upper bytes, and ours is called through the original's own signature
// (shape::MemberCode). Colours, ambients and scales carry NaNs with differing payloads, often in pairs; the
// coordinates stay where the original reads inside its vectors (u and v come from the rectangle, never from data).

namespace {

// A rectangle in a 16 x 16 pixel buffer, rows `stride` pixels apart, top-down or bottom-up; now and then empty or
// inverted (the shaders then draw nothing).
constexpr uint32_t kMapW = 16, kMapH = 16, kMaxStride = 18;
struct Rect {
    uint32_t left, top, right, bottom, stride;
    bool up;
    explicit Rect(std::mt19937 &gen)
    {
        left = gen() % kMapW;
        right = gen() % 10 == 0 && left ? gen() % left : left + gen() % (kMapW - left + 1);
        top = gen() % kMapH;
        bottom = gen() % 10 == 0 && top ? gen() % top : top + gen() % (kMapH - top + 1);
        stride = kMapW + gen() % 3;
        up = gen() % 2;
    }
};

struct Map {
    uint32_t pixels[kMaxStride * kMapH];
    uint32_t *base;
    int32_t pitch;
    Map(const Rect &r, uint32_t junk)
    {
        std::mt19937 g(junk);
        RandomBytes(pixels, sizeof pixels, g);
        base = r.up ? pixels + (kMapH - 1) * r.stride : pixels;
        pitch = r.up ? -int32_t(r.stride) : int32_t(r.stride);
    }
    Blob Words() const { return Blob(pixels, pixels + std::size(pixels)); }
};

Blob Words(const void *p, size_t n)
{
    const auto *w = static_cast<const uint32_t *>(p);
    return Blob(w, w + n / 4);
}

} // namespace

DIFF_TEST(sg_floor_color_grid_blob_key, 0x3c3a50)
{
    using Fn = FloorKey *(__thiscall *)(const void *, FloorKey *);
    auto original = ctx.Original<Fn>(0x3c3a50);
    auto ours = reinterpret_cast<Fn>(shape::MemberCode(&FloorBlob::Key));
    for (uint32_t seed = 0; seed < 100; ++seed) {
        std::mt19937 gen(seed ^ 0xb10b);
        FloorBlob blob;
        RandomBytes(&blob, sizeof blob, gen);
        FloorKey out[2];
        RandomBytes(&out[0], sizeof out[0], gen);
        out[1] = out[0];
        FloorKey *const r0 = original(&blob, &out[0]);
        FloorKey *const r1 = ours(&blob, &out[1]);
        ++ctx.cases;
        if (r0 != &out[0] || r1 != &out[1]) ctx.Fail("Key seed %u: returns", seed);
        ++ctx.cases;
        if (std::memcmp(&out[0], &out[1], sizeof out[0]) != 0 || out[1].x != blob.mX)
            ctx.Fail("Key seed %u: %d %d %d vs %d %d %d", seed, out[0].x, out[0].y, out[0].level, out[1].x, out[1].y,
                     out[1].level);
    }
}

DIFF_TEST(sg_floor_color_grid_neighbour, 0x4087e0)
{
    Fixture fixture;
    using Fn = void *(__thiscall *)(void *, int32_t, int32_t, uint32_t, uint32_t);
    auto original = ctx.Original<Fn>(0x4087e0);
    auto ours = reinterpret_cast<Fn>(shape::MemberCode(&cFloorColorGrid::NeighbourSample));
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Shape shape;
        shape.lightable = true;
        Storage s(seed);
        cFloorColorGrid::Construct(s.bytes, nullptr);
        Fill(*s.Get(), seed, shape);
        cFloorColorGrid *const g = s.Get();
        std::mt19937 gen(seed ^ 0x4e16);
        for (int i = 0; i < 64; ++i) {
            const int32_t x = int32_t(gen() % (g->mWidth * 2)), y = int32_t(gen() % (g->mHeight * 2));
            const uint32_t dir = gen() % 8;
            const uint32_t first = ByteArg(gen, uint8_t(gen() % 2));
            void *const a = original(g, x, y, dir, first);
            void *const b = ours(g, x, y, dir, first);
            ++ctx.cases;
            if (a != b)
                ctx.Fail("NeighbourSample seed %u: (%d, %d) dir %u first %#x: %p vs %p", seed, x, y, dir, first, a, b);
        }
        cFloorColorGrid::Destroy(g, nullptr);
    }
}

DIFF_TEST(sg_floor_color_grid_shade_quad, 0x4080c0)
{
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int32_t, float, uint32_t);
    auto original = ctx.Original<Fn>(0x4080c0);
    auto ours = reinterpret_cast<Fn>(shape::MemberCode(&FloorLightHalf::ShadeQuad));
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed ^ 0x5a0d);
        FloorLightHalf half[2];
        FillHalf(half[0], gen, seed % 3 == 0);
        half[1] = half[0];
        const Rect r(gen);
        Map m[2]{Map(r, seed), Map(r, seed)};
        const float scale = Scale(gen);
        const uint32_t levelZero = ByteArg(gen, uint8_t(gen() % 2));
        original(&half[0], r.left, r.top, r.right, r.bottom, m[0].base, m[0].pitch, scale, levelZero);
        ours(&half[1], r.left, r.top, r.right, r.bottom, m[1].base, m[1].pitch, scale, levelZero);
        Compare(ctx, "ShadeQuad pixels", seed, m[0].Words(), m[1].Words());
        Compare(ctx, "ShadeQuad half", seed, Words(&half[0], sizeof half[0]), Words(&half[1], sizeof half[1]));
    }
}

DIFF_TEST(sg_floor_color_grid_shade_triangle, 0x4072a0)
{
    using Fn = void(__thiscall *)(void *, float, float, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *,
                                  int32_t, float, uint32_t);
    auto original = ctx.Original<Fn>(0x4072a0);
    auto ours = reinterpret_cast<Fn>(shape::MemberCode(&FloorLightHalf::ShadeTriangle));
    static const uint8_t kSides[] = {0, 1, 0, 1, 2, 0xff};
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed ^ 0x7a1e);
        FloorLightHalf half[2];
        FillHalf(half[0], gen, seed % 3 == 0);
        half[1] = half[0];
        const Rect r(gen);
        Map m[2]{Map(r, seed), Map(r, seed)};
        float slope, threshold;
        switch (gen() % 5) {
        case 0: slope = 1.0f, threshold = std::bit_cast<float>(0x3f7d70a4u); break;
        case 1: slope = -1.0f, threshold = std::bit_cast<float>(0xbc23d70au); break;
        case 2: slope = Nice(gen), threshold = Nice(gen); break;
        default: slope = float(int(gen() % 9) - 4) / 2.0f, threshold = float(int(gen() % 9) - 4) / 4.0f; break;
        }
        const uint32_t side = ByteArg(gen, kSides[gen() % std::size(kSides)]);
        const float scale = Scale(gen);
        const uint32_t levelZero = ByteArg(gen, uint8_t(gen() % 2));
        original(&half[0], slope, threshold, side, r.left, r.top, r.right, r.bottom, m[0].base, m[0].pitch, scale,
                 levelZero);
        ours(&half[1], slope, threshold, side, r.left, r.top, r.right, r.bottom, m[1].base, m[1].pitch, scale,
             levelZero);
        Compare(ctx, "ShadeTriangle pixels", seed, m[0].Words(), m[1].Words());
        Compare(ctx, "ShadeTriangle half", seed, Words(&half[0], sizeof half[0]), Words(&half[1], sizeof half[1]));
    }
}

DIFF_TEST(sg_floor_color_grid_shade_cell, 0x4085e0)
{
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int32_t, float,
                                  uint32_t);
    auto original = ctx.Original<Fn>(0x4085e0);
    auto ours = reinterpret_cast<Fn>(shape::MemberCode(&FloorLightCell::Shade));
    static const uint32_t kKinds[] = {0, 0, 1, 1, 2, 2, 3, 0x100, 0x101, 0xffffffffu};
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed ^ 0x5ce1);
        FloorLightCell cell[2];
        RandomBytes(&cell[0], sizeof cell[0], gen);
        for (auto &h : cell[0].mHalves) {
            FillHalf(h, gen, seed % 3 == 0);
            h.mSave = Flag(gen);
        }
        cell[1] = cell[0];
        const Rect r(gen);
        Map m[2]{Map(r, seed), Map(r, seed)};
        const uint32_t kind = gen() % 8 ? kKinds[gen() % std::size(kKinds)] : gen();
        const float scale = Scale(gen);
        const uint32_t levelZero = ByteArg(gen, uint8_t(gen() % 2));
        original(&cell[0], kind, r.left, r.top, r.right, r.bottom, m[0].base, m[0].pitch, scale, levelZero);
        ours(&cell[1], kind, r.left, r.top, r.right, r.bottom, m[1].base, m[1].pitch, scale, levelZero);
        Compare(ctx, "Shade pixels", seed, m[0].Words(), m[1].Words());
        Compare(ctx, "Shade cell", seed, Words(&cell[0], sizeof cell[0]), Words(&cell[1], sizeof cell[1]));
    }
}

DIFF_TEST(sg_floor_color_grid_prepare, 0x4077a0)
{
    Fixture fixture;
    using Fn = void(__thiscall *)(void *, int32_t, int32_t, void *);
    auto original = ctx.Original<Fn>(0x4077a0);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Shape shape;
        shape.lightable = true;
        shape.nans = seed % 3 == 0;
        Pair p(seed, shape);
        std::mt19937 gen(seed ^ 0x9e9a);
        Ambients(p[0], p[1], gen, shape.nans);
        FloorLightCell cell[2];
        RandomBytes(&cell[0], sizeof cell[0], gen);
        cell[1] = cell[0];
        const int32_t x = int32_t(gen() % p[0]->mWidth), y = int32_t(gen() % p[0]->mHeight);
        original(p[0], x, y, &cell[0]);
        p[1]->PrepareLight(x, y, &cell[1]);
        Compare(ctx, "PrepareLight cell", seed, Words(&cell[0], sizeof cell[0]), Words(&cell[1], sizeof cell[1]));
        Compare(ctx, "PrepareLight grid", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

DIFF_TEST(sg_floor_color_grid_take_blobs, 0x4065b0)
{
    Fixture fixture;
    using Fn = void(__thiscall *)(void *, const void *);
    auto original = ctx.Original<Fn>(0x4065b0);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Shape shape;
        shape.lightable = true;
        shape.nans = seed % 3 == 0;
        Pair p(seed, shape);
        std::mt19937 gen(seed ^ 0xb10b5);
        Ambients(p[0], p[1], gen, shape.nans);
        std::vector<FloorBlob> blobs(gen() % 12);
        for (FloorBlob &b : blobs) {
            RandomBytes(&b, sizeof b, gen);
            b.mX = gen() % 8 == 0 ? int32_t(gen()) / 4 : int32_t(gen() % (p[0]->mWidth + 4)) - 2;
            b.mY = gen() % 8 == 0 ? int32_t(gen()) / 4 : int32_t(gen() % (p[0]->mHeight + 4)) - 2;
            b.mLevel = gen() % 5 ? p[0]->mLevel : int32_t(gen() % 7) - 3;
            b.mDir = gen() % 4 ? uint8_t(gen() % 8) : uint8_t(gen());
            b.mMode = Flag(gen);
        }
        original(p[0], &blobs);
        p[1]->TakeBlobs(&blobs);
        Compare(ctx, "TakeBlobs", seed, Dump(*p[0]), Dump(*p[1]));
    }
}

// ---- the two light passes, against a fake light map manager ------------------------------------------------------------

namespace {

// Three fake images (slots 9, 27, 28 of nGZSceneGraph::cIImageData) of 12 x 10 pixels, pitch 48..60 bytes, and a fake
// manager (slots 19 and 22 of nTSSG::cILightMapManager) whose answers come from its own generator, seeded alike on both
// sides. Every call is logged with its arguments (the FloorUse without its padding).
constexpr uint32_t kImgW = 12, kImgH = 10, kImgMaxWords = 15;

struct FakeLights;

struct FakeImage {
    const void *const *vtable;
    FakeLights *owner;
    int32_t pitchBytes;
    uint32_t pixels[kImgMaxWords * kImgH];
};

struct FakeLights {
    const void *const *vtable;
    std::mt19937 gen;
    FakeImage images[3];
    Blob log;
    explicit FakeLights(uint32_t seed);
};


uint32_t __fastcall FakeFloorRect(FakeLights *self, void *, const FloorUse *use, int32_t *left, int32_t *top,
                                  int32_t *right, int32_t *bottom, uint32_t *image)
{
    self->log.insert(self->log.end(), {19u, uint32_t(use->key.x), uint32_t(use->key.y), uint32_t(use->key.level),
                                       uint32_t(use->flags)});
    std::mt19937 &gen = self->gen;
    if (gen() % 6 == 0) return gen() & 0xffffff00u;
    const uint32_t l = gen() % kImgW, t = gen() % kImgH;
    *left = int32_t(l);
    *right = int32_t(l + gen() % (kImgW - l + 1));
    *top = int32_t(t);
    *bottom = int32_t(t + gen() % (kImgH - t + 1));
    *image = gen() % 3 + (gen() % 4 ? 0 : 3 * (gen() % 5));
    return (gen() & 0xffffff00u) | 1;
}

void *__fastcall FakeGetImage(FakeLights *self, void *, uint32_t group, uint32_t index)
{
    self->log.insert(self->log.end(), {22u, group, index});
    return &self->images[index % 3];
}

uint8_t *__fastcall FakeLevelData(FakeImage *self, void *, uint32_t level)
{
    self->owner->log.insert(self->owner->log.end(), {9u, level, uint32_t(self - self->owner->images)});
    return reinterpret_cast<uint8_t *>(self->pixels);
}

int32_t __fastcall FakeLevelHeight(FakeImage *self, void *, uint32_t level)
{
    self->owner->log.insert(self->owner->log.end(), {27u, level});
    return int32_t(kImgH);
}

int32_t __fastcall FakeLevelPitch(FakeImage *self, void *, uint32_t level)
{
    self->owner->log.insert(self->owner->log.end(), {28u, level});
    return self->pitchBytes;
}

const void *const kLightsVt[23] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, reinterpret_cast<const void *>(&FakeFloorRect),
                                   nullptr, nullptr, reinterpret_cast<const void *>(&FakeGetImage)};
const void *const kImageVt[29] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                  reinterpret_cast<const void *>(&FakeLevelData), nullptr, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr,
                                  reinterpret_cast<const void *>(&FakeLevelHeight),
                                  reinterpret_cast<const void *>(&FakeLevelPitch)};

FakeLights::FakeLights(uint32_t seed) : vtable(kLightsVt), gen(seed)
{
    for (FakeImage &img : images) {
        img.vtable = kImageVt;
        img.owner = this;
        img.pitchBytes = int32_t(kImgW * 4 + gen() % 13);
        RandomBytes(img.pixels, sizeof img.pixels, gen);
    }
    log.reserve(4096);
}

Blob Pixels(const FakeLights &l)
{
    Blob b;
    for (const FakeImage &img : l.images) b.insert(b.end(), img.pixels, img.pixels + std::size(img.pixels));
    return b;
}

// A few samples' colours changed alike on both sides (between rounds: some cells then change, some not).
void Touch(cFloorColorGrid *a, cFloorColorGrid *b, std::mt19937 &gen)
{
    const uint32_t n = gen() % 4;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t c = gen() % uint32_t(a->mCells.size()), s = gen() % 2, k = gen() % 3;
        a->mCells[c].mSamples[s].mColor[k] = b->mCells[c].mSamples[s].mColor[k] = Colour(gen, true);
    }
}

} // namespace

// Both passes (the case's seed picks one), each run one to three rounds on the same grids with the original's Reset
// between (some cells unchanged the next round, some changed by Touch); the stopwatch's Restart 0x1c95ec stands in,
// counted.
DIFF_TEST(sg_floor_color_grid_shade_lights, 0x4060d0)
{
    Fixture fixture;
    shape::CodePatch restart{0x1c95ecu, shape::CodeOf(&StandRestart)};
    using Fn = void(__thiscall *)(void *, void *, float);
    using Pass = void(__thiscall *)(void *);
    const uint32_t kRvas[] = {0x4060d0, 0x406300};
    void (cFloorColorGrid::*const kOurs[])(nTSSG::cILightMapManager *, float) = {&cFloorColorGrid::ShadeLights,
                                                                                &cFloorColorGrid::ShadeChangedLights};
    static const char *const kNames[] = {"ShadeLights", "ShadeChangedLights"};
    auto reset = ctx.Original<Pass>(0x406f40);
    for (uint32_t seed = 0; seed < 800; ++seed) {
        const unsigned which = seed % 2;
        auto original = ctx.Original<Fn>(kRvas[which]);
        Shape shape;
        shape.lightable = true;
        shape.nans = seed % 3 == 0;
        Pair p(seed, shape);
        std::mt19937 gen(seed ^ 0x116475);
        Ambients(p[0], p[1], gen, shape.nans);
        if (gen() % 3 == 0) p[0]->mLevel = p[1]->mLevel = 0;
        FakeLights lights[2]{FakeLights(seed), FakeLights(seed)};
        const int rounds = 1 + gen() % 3;
        for (int round = 0; round < rounds; ++round) {
            const float scale = Scale(gen);
            int restarts[2];
            gRestarts = 0;
            original(p[0], &lights[0], scale);
            restarts[0] = gRestarts;
            gRestarts = 0;
            (p[1]->*kOurs[which])(reinterpret_cast<nTSSG::cILightMapManager *>(&lights[1]), scale);
            restarts[1] = gRestarts;
            ++ctx.cases;
            if (restarts[0] != restarts[1])
                ctx.Fail("%s seed %u round %d: %d vs %d restarts", kNames[which], seed, round, restarts[0], restarts[1]);
            Compare(ctx, kNames[which], seed, lights[0].log, lights[1].log);
            Compare(ctx, kNames[which], seed, Pixels(lights[0]), Pixels(lights[1]));
            Compare(ctx, kNames[which], seed, Dump(*p[0]), Dump(*p[1]));
            reset(p[0]);
            p[1]->Reset();
            Touch(p[0], p[1], gen);
        }
    }
}
