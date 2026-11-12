// Differential tests of row LD2h's bottom (lane 261110d; src/sg/cWallDiffusionManager.h): the reference test, the
// packed-colour writers (whole segment, one side, one row of mCurrent, the byte-wise max), the run's gradient, test
// colours and point along it, the neighbour search and border block, the walk over the references, and the manager's
// ambient colour and four simple passes. Records come from tests/sg_wall_diffusion.h (wallfx) alike on both sides; the
// colour channels are drawn at the packer's rounding points and clamp edges (wallfx::Channel). Every case runs at the
// x87's 53-bit and 24-bit precisions (the originals are SSE, so neither should matter).
#include "sg_wall_diffusion.h"

#include <climits>

using namespace wallfx;
using nTSSG::WallSegmentRef;

namespace {

// A manager ours constructed on a junk block, filled from the seed; destroyed by ours.
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

constexpr int32_t kSides[] = {0, 1, 0, 1, 0, 1, -1, 2};
constexpr int32_t kRows[] = {0, 1, 2, 3, 0, 1, 2, 3, -1, 4};

} // namespace

// ---- the reference ----------------------------------------------------------------------------------------------------

// IsValid 0x410250: run and segment words at 0, +-1, +-2, INT_MIN, INT_MAX and junk, the level word junk. Only al is
// compared (the original leaves eax's upper bytes as they came).
DIFF_TEST(sg_wall_diffusion_ref_valid, 0x410250)
{
    using Valid = uint32_t(__thiscall *)(const void *);
    auto valid = ctx.Original<Valid>(0x410250);
    static const int32_t kWords[] = {0, 1, -1, 2, -2, INT_MIN, INT_MAX};
    SidesAtBothPrecisions(ctx, "ref valid", 400, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        auto word = [&] { return gen() % 4 ? kWords[gen() % std::size(kWords)] : int32_t(gen()); };
        WallSegmentRef r{int32_t(gen()), 0, 0};
        r.mRun = word();
        r.mSegment = word();
        const uint32_t al = (side == 0 ? valid(&r) : uint32_t(r.IsValid())) & 0xffu;
        return Blob{uint32_t(r.mLevel), uint32_t(r.mRun), uint32_t(r.mSegment), al};
    });
}

// ---- the packed-colour writers ----------------------------------------------------------------------------------------

// SetPacked 0x40cf30 on a junk segment: the colour (wallfx::Channel's three) into the twelve packed words; the rest of the
// block as it was.
DIFF_TEST(sg_wall_diffusion_set_packed, 0x40cf30)
{
    using Set = void(__thiscall *)(void *, const float *);
    auto set = ctx.Original<Set>(0x40cf30);
    SidesAtBothPrecisions(ctx, "set packed", 4000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        float rgb[3];
        Channels(rgb, gen);
        Block<WallDiffusionSegment> b(seed);
        if (side == 0)
            set(b.Get(), rgb);
        else
            b.Get()->SetPacked(rgb);
        Blob o{U(rgb[0]), U(rgb[1]), U(rgb[2])};
        Words(o, b.bytes, sizeof b.bytes);
        return o;
    });
}

// SetSidePacked 0x40d180: sides 0 and 1, and -1 / 2 (unchecked: the words before and after mPacked, still in the block).
DIFF_TEST(sg_wall_diffusion_side_packed, 0x40d180)
{
    using Set = void(__thiscall *)(void *, const float *, int32_t);
    auto set = ctx.Original<Set>(0x40d180);
    SidesAtBothPrecisions(ctx, "side packed", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        float rgb[3];
        Channels(rgb, gen);
        const int32_t s = kSides[gen() % std::size(kSides)];
        Block<WallDiffusionSegment> b(seed);
        if (side == 0)
            set(b.Get(), rgb, s);
        else
            b.Get()->SetSidePacked(rgb, s);
        Blob o{U(rgb[0]), U(rgb[1]), U(rgb[2]), uint32_t(s)};
        Words(o, b.bytes, sizeof b.bytes);
        return o;
    });
}

// SetRow 0x40d3c0: rows 0-3 of mCurrent, and -1 / 4 (the end of mPacked, the start of mPrevious).
DIFF_TEST(sg_wall_diffusion_set_row, 0x40d3c0)
{
    using Set = void(__thiscall *)(void *, const float *, int32_t);
    auto set = ctx.Original<Set>(0x40d3c0);
    SidesAtBothPrecisions(ctx, "set row", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        float rgb[3];
        Channels(rgb, gen);
        const int32_t row = kRows[gen() % std::size(kRows)];
        Block<WallDiffusionSegment> b(seed);
        if (side == 0)
            set(b.Get(), rgb, row);
        else
            b.Get()->SetRow(rgb, row);
        Blob o{U(rgb[0]), U(rgb[1]), U(rgb[2]), uint32_t(row)};
        Words(o, b.bytes, sizeof b.bytes);
        return o;
    });
}

// MergeMax 0x40d260: the side's six words junk, the packed colour itself, or it with one byte (alpha too) one less or one
// more; a third of the cases only the colour or more, so nothing changes. al only (eax's upper bytes are movd junk,
// 0x40d39f).
DIFF_TEST(sg_wall_diffusion_merge_max, 0x40d260)
{
    using Merge = uint32_t(__thiscall *)(void *, const float *, int32_t);
    auto merge = ctx.Original<Merge>(0x40d260);
    SidesAtBothPrecisions(ctx, "merge max", 4000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        float rgb[3];
        Channels(rgb, gen);
        const int32_t s = kSides[gen() % std::size(kSides)];
        Block<WallDiffusionSegment> b(seed);
        const uint32_t color = nTSSG::PackWallColor(rgb);
        const bool noChange = gen() % 3 == 0;
        auto *words = reinterpret_cast<uint32_t *>(b.bytes + offsetof(WallDiffusionSegment, mPacked) + 24 * s);
        for (int k = 0; k < 6; ++k) {
            const uint32_t shift = 8 * (gen() % 4);
            const uint32_t byte = color >> shift & 0xffu;
            switch (noChange ? 1 + 2 * (gen() % 2) : gen() % 4) {
            case 0: words[k] = gen(); break;
            case 1: words[k] = color; break;
            case 2: words[k] = byte ? color - (1u << shift) : color; break;
            default: words[k] = byte < 0xff ? color + (1u << shift) : color; break;
            }
        }
        const uint32_t al = (side == 0 ? merge(b.Get(), rgb, s) : uint32_t(b.Get()->MergeMax(rgb, s))) & 0xffu;
        Blob o{U(rgb[0]), U(rgb[1]), U(rgb[2]), uint32_t(s), al};
        Words(o, b.bytes, sizeof b.bytes);
        return o;
    });
}

// ---- the run's writers ------------------------------------------------------------------------------------------------

// SetPacked 0x40cec0: runs of 0-5 segments.
DIFF_TEST(sg_wall_diffusion_run_set_packed, 0x40cec0)
{
    Fixture fixture;
    using Set = void(__thiscall *)(void *, const float *);
    auto set = ctx.Original<Set>(0x40cec0);
    SidesAtBothPrecisions(ctx, "run set packed", 600, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Shape shape;
        shape.maxSegments = 5;
        WallDiffusionRun r;
        FillRun(r, gen, shape, 0);
        float rgb[3];
        Channels(rgb, gen);
        if (side == 0)
            set(&r, rgb);
        else
            r.SetPacked(rgb);
        Blob o{U(rgb[0]), U(rgb[1]), U(rgb[2])};
        DumpRun(o, r, false);
        return o;
    });
}

// PaintGradient 0x40d090: runs of 0-6 segments, and counts whose 1 / 2n steps land near the packer's rounding points
// (13, 37, 100, 128, 255, 300), and 1792, the first count where the float step times 2i + side packs differently from
// the exact quotient (side index 91: 91 * step and 91 / 3584 rounded to float lie either side of a packer tie or on it).
DIFF_TEST(sg_wall_diffusion_run_gradient, 0x40d090)
{
    Fixture fixture;
    using Paint = void(__thiscall *)(void *);
    auto paint = ctx.Original<Paint>(0x40d090);
    static const uint32_t kCounts[] = {13, 37, 100, 128, 255, 300, 1792};
    SidesAtBothPrecisions(ctx, "run gradient", 150, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Shape shape;
        shape.minSegments = shape.maxSegments = gen() % 3 ? gen() % 7 : kCounts[gen() % std::size(kCounts)];
        WallDiffusionRun r;
        FillRun(r, gen, shape, 0);
        if (side == 0)
            paint(&r);
        else
            r.PaintGradient();
        Blob o;
        DumpRun(o, r, false);
        return o;
    });
}

// PaintTestColors 0x40d4b0: runs of 0-5 segments (rows 0, 1, 3 written, row 2 and the rest as they were).
DIFF_TEST(sg_wall_diffusion_run_test_colors, 0x40d4b0)
{
    Fixture fixture;
    using Paint = void(__thiscall *)(void *);
    auto paint = ctx.Original<Paint>(0x40d4b0);
    SidesAtBothPrecisions(ctx, "run test colors", 200, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Shape shape;
        shape.maxSegments = 5;
        WallDiffusionRun r;
        FillRun(r, gen, shape, 0);
        if (side == 0)
            paint(&r);
        else
            r.PaintTestColors();
        Blob o;
        DumpRun(o, r, false);
        return o;
    });
}

// PointAlong 0x410760: runs of 1-6 segments (and 37); the first segment's direction drawn wide, zero (end = start), a
// denormal (its squared length 0), or with infinities and NaNs; t at 0, -0, 1, 1-2 ulps around them, inside, outside,
// specials; the offset wide. The return, out[0..1] and out[2] (left as it was).
DIFF_TEST(sg_wall_diffusion_point_along, 0x410760)
{
    Fixture fixture;
    using Point = float *(__thiscall *)(void *, float *, float, float);
    auto point = ctx.Original<Point>(0x410760);
    static const float kTs[] = {0.0f, -0.0f, 1.0f, 0.5f, 0x1p-149f, -0x1p-149f, 0x1.fffffep-1f, 0x1.000002p+0f};
    SidesAtBothPrecisions(ctx, "point along", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Shape shape;
        shape.nans = true;
        shape.minSegments = 1;
        shape.maxSegments = 6;
        if (gen() % 8 == 0) shape.minSegments = shape.maxSegments = 37;
        WallDiffusionRun r;
        FillRun(r, gen, shape, 0);
        WallDiffusionSegment &first = r.mSegments[0];
        switch (gen() % 5) {
        case 0:
            for (int k = 0; k < 3; ++k) first.mEnd[k] = first.mStart[k];
            break;
        case 1:
            for (int k = 0; k < 2; ++k) first.mEnd[k] = first.mStart[k] + Fl(gen() % 0x400u * (gen() % 2 ? 1u : 0x80000001u));
            break;
        case 2:
            first.mEnd[gen() % 2] = gen() % 2 ? NaN(gen) : Fl(0x7f800000u | (gen() & 0x80000000u));
            break;
        default: break;
        }
        float t;
        switch (gen() % 4) {
        case 0: t = kTs[gen() % std::size(kTs)]; break;
        case 1: t = Wide(gen, 2.0f); break;
        default: t = float(int(gen() % 1201) - 100) / 1000.0f; break;
        }
        const float offset = Wide(gen, 4.0f);
        float out[3] = {Fl(gen()), Fl(gen()), Fl(gen())};
        float *ret = side == 0 ? point(&r, out, t, offset) : r.PointAlong(out, t, offset);
        return Blob{U(t), U(offset), uint32_t(ret == out), U(out[0]), U(out[1]), U(out[2])};
    });
}

// ---- neighbours and the border block ----------------------------------------------------------------------------------

// Neighbours 0x40fe70 on every segment of a manager of up to 5 runs of 0-5 segments, the references valid (any run, empty
// ones too, a segment index now and then one past the target's last) or not: the eight pointers as {run, index}
// (wallfx::SegmentId, the runs' blocks kept apart by wallfx::SeparateRuns), a ninth word left as it was, the manager
// unchanged.
DIFF_TEST(sg_wall_diffusion_neighbours, 0x40fe70)
{
    Fixture fixture;
    using Find = void(__thiscall *)(void *, void *, uint32_t, WallDiffusionSegment **);
    auto find = ctx.Original<Find>(0x40fe70);
    SidesAtBothPrecisions(ctx, "neighbours", 400, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = 5;
        shape.maxSegments = 5;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x40fe70u);
        ValidRefs(m, gen, false, true);
        std::vector<std::vector<WallDiffusionSegment>> keep;
        SeparateRuns(m, keep);
        Blob o;
        for (WallDiffusionRun &r : m.mRuns)
            for (uint32_t i = 0; i < r.mSegments.size(); ++i) {
                WallDiffusionSegment *out[9];
                for (auto &p : out) p = reinterpret_cast<WallDiffusionSegment *>(uintptr_t(gen()));
                const uint32_t canary = P(out[8]);
                if (side == 0)
                    find(&r, &m.mRuns, i, out);
                else
                    r.Neighbours(&m.mRuns, i, out);
                for (int k = 0; k < 8; ++k) o.push_back(SegmentId(m, out[k]));
                o.push_back(P(out[8]) == canary);
            }
        const Blob d = Dump(m);
        o.insert(o.end(), d.begin(), d.end());
        return o;
    });
}

// FillBlock 0x40da80 on a junk segment: each of the eight neighbours null or one of five drawn segments (now and then the
// segment itself), the null patterns from the case's low byte so all 256 come up; the second argument junk. The segment
// and the five, whole.
DIFF_TEST(sg_wall_diffusion_fill_block, 0x40da80)
{
    using FillFn = void(__thiscall *)(void *, WallDiffusionSegment *const *, void *);
    auto fill = ctx.Original<FillFn>(0x40da80);
    SidesAtBothPrecisions(ctx, "fill block", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Block<WallDiffusionSegment> self(seed);
        Block<WallDiffusionSegment> pool[5]{Block<WallDiffusionSegment>(seed + 1), Block<WallDiffusionSegment>(seed + 2),
                                            Block<WallDiffusionSegment>(seed + 3), Block<WallDiffusionSegment>(seed + 4),
                                            Block<WallDiffusionSegment>(seed + 5)};
        const uint32_t present = (seed & 0xffu) ^ (gen() & 0x100u ? 0u : gen() & 0xffu);
        WallDiffusionSegment *n[8];
        for (int k = 0; k < 8; ++k)
            n[k] = present >> k & 1 ? (gen() % 9 == 0 ? self.Get() : pool[gen() % 5].Get()) : nullptr;
        void *list = reinterpret_cast<void *>(uintptr_t(gen()));
        if (side == 0)
            fill(self.Get(), n, list);
        else
            self.Get()->FillBlock(n, list);
        Blob o{present};
        Words(o, self.bytes, sizeof self.bytes);
        for (auto &p : pool) Words(o, p.bytes, sizeof p.bytes);
        return o;
    });
}

// ---- the walk ---------------------------------------------------------------------------------------------------------

// Walk 0x4119f0 from a random run of a manager of up to 6 runs of 0-3 segments, the references valid only to later runs
// (so it ends) or invalid; the flag and direction bytes (0, 1, 2, 0x80, 0xff) with junk above them. The manager whole
// (every run's flags).
DIFF_TEST(sg_wall_diffusion_walk, 0x4119f0)
{
    Fixture fixture;
    using WalkFn = void(__thiscall *)(void *, void *, uint32_t, uint32_t);
    auto walk = ctx.Original<WalkFn>(0x4119f0);
    SidesAtBothPrecisions(ctx, "walk", 500, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = 6;
        shape.maxSegments = 3;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        std::mt19937 gen(seed ^ 0x4119f0u);
        ValidRefs(m, gen, true);
        Blob o;
        if (!m.mRuns.empty()) {
            const uint32_t start = gen() % 3 ? 0 : gen() % uint32_t(m.mRuns.size());
            const uint32_t flag = ByteArg(gen, Flag(gen));
            const uint32_t up = ByteArg(gen, Flag(gen));
            o = {start, flag, up};
            if (side == 0)
                walk(&m.mRuns[start], &m.mRuns, flag, up);
            else
                m.mRuns[start].Walk(&m.mRuns, uint8_t(flag), uint8_t(up));
        }
        const Blob d = Dump(m);
        o.insert(o.end(), d.begin(), d.end());
        return o;
    });
}

// ---- the manager ------------------------------------------------------------------------------------------------------

// AmbientFor 0x40c720 on every run of a manager (1-4 segments each; settings drawn with NaNs): the return, the three
// words, a fourth left as it was, and the stand-in's log (its answer's low half 0 for about half the runs).
DIFF_TEST(sg_wall_diffusion_ambient_for, 0x40c720)
{
    Fixture fixture;
    QueryModes modes;
    using Ambient = float *(__thiscall *)(void *, float *, void *);
    auto ambient = ctx.Original<Ambient>(0x40c720);
    SidesAtBothPrecisions(ctx, "ambient for", 400, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.minSegments = 1;
        shape.nans = true;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        gQuerySalt = seed;
        std::mt19937 gen(seed ^ 0x40c720u);
        Blob o;
        for (WallDiffusionRun &r : m.mRuns) {
            gLog.clear();
            float out[4] = {Fl(gen()), Fl(gen()), Fl(gen()), Fl(gen())};
            float *ret = side == 0 ? ambient(&m, out, &r) : m.AmbientFor(out, &r);
            o.push_back(ret == out);
            for (float f : out) o.push_back(U(f));
            o.insert(o.end(), gLog.begin(), gLog.end());
        }
        const Blob d = Dump(m);
        o.insert(o.end(), d.begin(), d.end());
        return o;
    });
}

// The four passes on a manager of up to 4 runs (1-4 segments each where the pass asks the world about every run): the
// manager whole and the stand-in's log. The border pass's references valid (segment indices in range: it reads the
// segments they name) or not.
namespace {
template <class Ours>
void PassCases(difftest::Context &ctx, const char *what, uint32_t rva, uint32_t cases, uint32_t minSegments, bool refs,
               Ours ours)
{
    Fixture fixture;
    QueryModes modes;
    using Pass = void(__thiscall *)(void *);
    auto original = ctx.Original<Pass>(rva);
    SidesAtBothPrecisions(ctx, what, cases, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.minSegments = minSegments;
        shape.nans = true;
        Manager mm(seed, shape);
        cWallDiffusionManager &m = *mm.m;
        if (refs) {
            std::mt19937 gen(seed ^ rva);
            ValidRefs(m, gen, false);
        }
        gQuerySalt = seed;
        gLog.clear();
        if (side == 0)
            original(&m);
        else
            (m.*ours)();
        Blob o = gLog;        const Blob d = Dump(m);
        o.insert(o.end(), d.begin(), d.end());
        return o;
    });
}
} // namespace

// AmbientPass 0x40d010: every run's segments to its ambient colour, its mFlag19 cleared.
DIFF_TEST(sg_wall_diffusion_ambient_pass, 0x40d010)
{
    PassCases(ctx, "ambient pass", 0x40d010, 400, 1, false, &cWallDiffusionManager::AmbientPass);
}

// GradientPass 0x411c60.
DIFF_TEST(sg_wall_diffusion_gradient_pass, 0x411c60)
{
    PassCases(ctx, "gradient pass", 0x411c60, 200, 0, false, &cWallDiffusionManager::GradientPass);
}

// TestColorPass 0x411c20.
DIFF_TEST(sg_wall_diffusion_test_color_pass, 0x411c20)
{
    PassCases(ctx, "test color pass", 0x411c20, 200, 0, false, &cWallDiffusionManager::TestColorPass);
}

// BorderPass 0x40f5a0.
DIFF_TEST(sg_wall_diffusion_border_pass, 0x40f5a0)
{
    PassCases(ctx, "border pass", 0x40f5a0, 400, 1, true, &cWallDiffusionManager::BorderPass);
}
