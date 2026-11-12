// Differential tests of nTSSG::cWallDiffusionManager's records and storage (lane 261110c, scenegraph LD2d;
// src/sg/cWallDiffusionManager.h): the manager's constructor and slot 0, its settings, the segment's and run's
// constructors, the segment's copy, the run's accessors, Clear, SetAngle and Length, the Fixed16 helpers and the angle
// tests. Records are built alike on both sides from a seed (tests/sg_wall_diffusion.h); every case runs at the x87's
// 53-bit and 24-bit precisions (the originals are SSE, so neither should matter; _CIfmod's fprem is exact).
#include "sg_wall_diffusion.h"

#include <cmath>
#include <limits>

using namespace wallfx;

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

float Up(float f, int ulps = 1)
{
    for (int i = 0; i < ulps; ++i) f = std::nextafter(f, kInf);
    return f;
}
float Down(float f, int ulps = 1)
{
    for (int i = 0; i < ulps; ++i) f = std::nextafter(f, -kInf);
    return f;
}

// A float near `edge`: the edge itself, 1-2 ulps either side, or a Value.
float Near(std::mt19937 &gen, float edge, const Shape &shape)
{
    switch (gen() % 6) {
    case 0: return edge;
    case 1: return Up(edge);
    case 2: return Down(edge);
    case 3: return Up(edge, 2);
    case 4: return Down(edge, 2);
    default: return Value(gen, shape, 4.0f);
    }
}

// Special floats every float test draws now and then: zeros, infinities, NaNs (quiet and signalling), denormals.
float Special(std::mt19937 &gen)
{
    switch (gen() % 7) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return kInf;
    case 3: return -kInf;
    case 4: return Fl(0x7fc00000u | (gen() & 0x803fffffu));        // quiet NaN
    case 5: return Fl(0x7f800000u | (gen() & 0x80000000u) | (1 + gen() % 0x3fffffu)); // signalling NaN
    default: return Fl((gen() & 0x80000000u) | (1 + gen() % 0x7fffffu)); // denormal
    }
}

uint32_t Offset(const void *p, const void *base) { return P(p) - P(base); }

} // namespace

// ---- the manager's constructor and slot 0 -----------------------------------------------------------------------------

// The constructor 0x40be50 on blocks of junk: it returns the block, makes the set's head (one 0x14 block) and sets every
// field; the vtable word is each side's own class's.
DIFF_TEST(sg_wall_diffusion_life, 0x40be50)
{
    Fixture fixture;
    using Ctor = void *(__thiscall *)(void *);
    auto construct = ctx.Original<Ctor>(0x40be50);
    SidesAtBothPrecisions(ctx, "construct", 40, false, [&](uint32_t seed, int side) {
        Block<cWallDiffusionManager> block(seed);
        difftest::AllocationCounter count;
        void *r = side == 0 ? construct(block.bytes) : cWallDiffusionManager::Construct(block.bytes, nullptr);
        count.Stop();
        Blob o = Dump(*block.Get(), true);
        o.push_back(r == block.bytes);
        o.push_back(uint32_t(count.Blocks()));
        o.push_back(uint32_t(count.Bytes()));
        block.Get()->~cWallDiffusionManager();
        return o;
    });
}

// Slot 0, the scalar deleting destructor 0x40bff0, on managers built alike in heap blocks, with flag words whose bit 0
// is set or clear among other bits: it returns this, frees the lists', the set's and the runs' blocks in that order, then
// the manager's own 0x60 block when bit 0 is set.
DIFF_TEST(sg_wall_diffusion_deleting, 0x40bff0)
{
    Fixture fixture;
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    auto deleting = ctx.Original<Deleting>(0x40bff0);
    static constexpr uint32_t kFlags[] = {0, 1, 2, 3, 0xfffffffe, 0xffffffff, 0x80000001};
    SidesAtBothPrecisions(ctx, "deleting", 120, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = seed % 5;
        const uint32_t flags = kFlags[seed % std::size(kFlags)];
        void *block = ::operator new(sizeof(cWallDiffusionManager));
        cWallDiffusionManager::Construct(block, nullptr);
        Fill(*static_cast<cWallDiffusionManager *>(block), seed, shape);
        Blob o;
        auto *m = static_cast<cWallDiffusionManager *>(block);
        o.push_back(uint32_t(m->mChanges2.capacity() != 0) | uint32_t(m->mChanges.capacity() != 0) << 1 |
                    uint32_t(m->mRuns.capacity() != 0) << 2);
        {
            shape::DeleteRecorder deletes;
            deletes.Begin();
            void *r = side == 0 ? deleting(block, flags) : m->DeletingDestructor(flags);
            deletes.End();
            o.push_back(r == block);
            o.push_back(uint32_t(deletes.Count()));
            o.push_back(deletes.Overflowed());
            for (int i = 0; i < deletes.Count(); ++i) {
                o.push_back(deletes.At(i).size);
                o.push_back(deletes.At(i).block == block);
            }
            if (!deletes.Deleted(block)) ::operator delete(block);
        }
        return o;
    });
}

// ---- the settings -----------------------------------------------------------------------------------------------------

// The two hooked setters 0x411b30 / 0x411b60 and our four whose game bodies are folded with other classes' (0x20c6f0,
// 0x229f60, 0x4099f0, 0x411b70; called here through their RVAs, never hooked), on managers built alike, with values of
// every family (signalling NaNs too: the colours and floats are copied as bits).
DIFF_TEST(sg_wall_diffusion_settings, 0x411b30)
{
    Fixture fixture;
    using SetColor = void(__thiscall *)(void *, const float *);
    using SetFloat = void(__thiscall *)(void *, float);
    using SetInt = void(__thiscall *)(void *, int32_t);
    auto insideColor = ctx.Original<SetColor>(0x411b30);
    auto outsideWall = ctx.Original<SetFloat>(0x411b60);
    auto steps = ctx.Original<SetInt>(0x20c6f0);
    auto insideWall = ctx.Original<SetFloat>(0x229f60);
    auto threshold = ctx.Original<SetFloat>(0x4099f0);
    auto outsideColor = ctx.Original<SetColor>(0x411b70);
    SidesAtBothPrecisions(ctx, "settings", 200, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.nans = true;
        Block<cWallDiffusionManager> block(1);
        auto *m = cWallDiffusionManager::Construct(block.bytes, nullptr);
        Fill(*m, seed, shape);
        std::mt19937 gen(seed);
        auto f = [&] { return gen() % 4 == 0 ? Special(gen) : Value(gen, shape, 1.0f); };
        float colour[3] = {f(), f(), f()};
        const uint32_t which = seed % 6;
        const float v = f();
        const int32_t n = int32_t(gen());
        switch (which) {
        case 0: side == 0 ? insideColor(m, colour) : m->SetInsideAmbientColor(colour); break;
        case 1: side == 0 ? outsideWall(m, v) : m->SetOutsideWallAmbient(v); break;
        case 2: side == 0 ? steps(m, n) : m->SetPropagationSteps(n); break;
        case 3: side == 0 ? insideWall(m, v) : m->SetInsideWallAmbient(v); break;
        case 4: side == 0 ? threshold(m, v) : m->SetThreshold(v); break;
        default: side == 0 ? outsideColor(m, colour) : m->SetOutsideAmbientColor(colour); break;
        }
        Blob o = Dump(*m);
        LF(colour, 3);
        m->~cWallDiffusionManager();
        return o;
    });
}

// ---- the records' constructors and the segment's copy -----------------------------------------------------------------

// The segment's constructor 0x40bd90 and copy 0x40bcd0, the run's constructor 0x40bc90, on blocks of junk: each returns
// the block; every byte compared (the segment's padding +0x175..+0x177 stays the block's: the constructor and copy write
// the fields alone, 0x40bcd0 ending with the byte at +0x174).
DIFF_TEST(sg_wall_diffusion_records, 0x40bd90)
{
    Fixture fixture;
    using Ctor = void *(__thiscall *)(void *);
    using Copy = void *(__thiscall *)(void *, const void *);
    auto segment = ctx.Original<Ctor>(0x40bd90);
    auto copy = ctx.Original<Copy>(0x40bcd0);
    auto run = ctx.Original<Ctor>(0x40bc90);
    SidesAtBothPrecisions(ctx, "records", 120, false, [&](uint32_t seed, int side) {
        Blob o;
        {
            Block<WallDiffusionSegment> b(seed);
            void *r = side == 0 ? segment(b.bytes) : ::new (b.bytes) WallDiffusionSegment;
            o.push_back(r == b.bytes);
            Words(o, b.bytes, sizeof(b.bytes));
        }
        {
            Shape shape;
            shape.nans = true;
            std::mt19937 gen(seed);
            Block<WallDiffusionSegment> source(seed + 1), b(seed + 2);
            FillSegment(*source.Get(), gen, shape);
            void *r = side == 0 ? copy(b.bytes, source.Get()) : ::new (b.bytes) WallDiffusionSegment(*source.Get());
            o.push_back(r == b.bytes);
            Words(o, b.bytes, sizeof(b.bytes));
            Words(o, source.bytes, sizeof(source.bytes));
        }
        {
            Block<WallDiffusionRun> b(seed + 3);
            void *r = side == 0 ? run(b.bytes) : ::new (b.bytes) WallDiffusionRun;
            o.push_back(r == b.bytes);
            Words(o, b.bytes, sizeof(b.bytes));
        }
        return o;
    });
}

// ---- the run's accessors ----------------------------------------------------------------------------------------------

// On runs built alike (some empty): the counts 0x410680 / 0x410660, the address accessors 0x410270, 0x411420, 0x4114e0,
// 0x411480 / 0x4114b0 (the segment's 0x4114a0 / 0x4114d0 inside; returned as offsets from the run's first segment, with
// indices past the end and wrapping ones: nothing is read), the location word 0x410310 (non-empty runs), the reference
// setters 0x411bc0 / 0x411c00 and 0x411ba0 / 0x411be0 (in range), the flag setters 0x4119e0 / 0x411b50 (junk above the byte) and Clear 0x40d570.
DIFF_TEST(sg_wall_diffusion_run, 0x410680)
{
    Fixture fixture;
    using Int = int32_t(__thiscall *)(void *);
    using Ptr = void *(__thiscall *)(void *);
    using PtrAt = void *(__thiscall *)(void *, uint32_t);
    using Word = uint32_t(__thiscall *)(void *);
    using SetAt = void(__thiscall *)(void *, uint32_t, const void *);
    using SetColor = void(__thiscall *)(void *, const void *);
    using SetByte = void(__thiscall *)(void *, uint32_t);
    using Plain = void(__thiscall *)(void *);
    const Int counts[2] = {ctx.Original<Int>(0x410680), ctx.Original<Int>(0x410660)};
    const Ptr ends[2] = {ctx.Original<Ptr>(0x410270), ctx.Original<Ptr>(0x411420)};
    const PtrAt ats[3] = {ctx.Original<PtrAt>(0x4114e0), ctx.Original<PtrAt>(0x411480), ctx.Original<PtrAt>(0x4114b0)};
    const Ptr segColors[2] = {ctx.Original<Ptr>(0x4114a0), ctx.Original<Ptr>(0x4114d0)};
    const SetColor segSets[2] = {ctx.Original<SetColor>(0x411bc0), ctx.Original<SetColor>(0x411c00)};
    auto word2 = ctx.Original<Word>(0x410310);
    const SetAt sets[2] = {ctx.Original<SetAt>(0x411ba0), ctx.Original<SetAt>(0x411be0)};
    const SetByte flags[2] = {ctx.Original<SetByte>(0x4119e0), ctx.Original<SetByte>(0x411b50)};
    auto clear = ctx.Original<Plain>(0x40d570);
    const SetByte ourFlags[2] = {reinterpret_cast<SetByte>(shape::MemberCode(&WallDiffusionRun::SetFlag19Hook)),
                                 reinterpret_cast<SetByte>(shape::MemberCode(&WallDiffusionRun::SetFlag1AHook))};
    SidesAtBothPrecisions(ctx, "run", 300, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = 1 + seed % 3;
        shape.maxSegments = 5;
        shape.nans = true;
        Block<cWallDiffusionManager> block(1);
        auto *m = cWallDiffusionManager::Construct(block.bytes, nullptr);
        Fill(*m, seed, shape);
        std::mt19937 gen(seed ^ 0x5eed);
        Blob o;
        for (WallDiffusionRun &r : m->mRuns) {
            const void *base = r.mSegments.data();
            const uint32_t n = uint32_t(r.mSegments.size());
            for (int k = 0; k < 2; ++k)
                o.push_back(uint32_t(side == 0 ? counts[k](&r) : k == 0 ? r.SegmentCount() : r.SideCount()));
            for (int k = 0; k < 2; ++k)
                o.push_back(Offset(side == 0 ? ends[k](&r) : k == 0 ? r.FrontLocation() : r.BackLocation(), base));
            for (int t = 0; t < 4; ++t) {
                static const uint32_t kFar[] = {0x7fffffffu, 0x80000000u, 0xffffffffu, 0xadfu};
                const uint32_t i = gen() % 3 ? gen() % (n + 2) : kFar[gen() % 4];
                for (int k = 0; k < 3; ++k) {
                    void *p = side == 0 ? ats[k](&r, i)
                              : k == 0  ? static_cast<void *>(r.LocationAt(i))
                              : k == 1  ? static_cast<void *>(r.AboveAt(i))
                                        : static_cast<void *>(r.BelowAt(i));
                    o.push_back(Offset(p, base));
                }
            }
            if (n) {
                o.push_back(side == 0 ? word2(&r) : r.FrontLocationWord2());
                WallDiffusionSegment &s = r.mSegments[gen() % n];
                for (int k = 0; k < 2; ++k)
                    o.push_back(Offset(side == 0 ? segColors[k](&s) : k == 0 ? s.Above() : s.Below(), &s));
                const nTSSG::WallSegmentRef ref = Ref(gen);
                const int k = int(gen() % 2);
                if (side == 0)
                    segSets[k](&s, &ref);
                else
                    k == 0 ? s.SetAbove(&ref) : s.SetBelow(&ref);
                const uint32_t i = gen() % n;
                const nTSSG::WallSegmentRef ref2 = Ref(gen);
                const int k2 = int(gen() % 2);
                if (side == 0)
                    sets[k2](&r, i, &ref2);
                else
                    k2 == 0 ? r.SetAboveAt(i, &ref2) : r.SetBelowAt(i, &ref2);
            }
            for (int k = 0; k < 2; ++k) {
                const uint32_t arg = ByteArg(gen, Flag(gen));
                (side == 0 ? flags[k] : ourFlags[k])(&r, arg);
            }
            if (gen() % 3 == 0) side == 0 ? clear(&r) : r.Clear();
        }
        const Blob d = Dump(*m);
        o.insert(o.end(), d.begin(), d.end());
        m->~cWallDiffusionManager();
        return o;
    });
}

// FrontCellQuery 0x411460 on non-empty runs, the world's level 0x7f2260 stood in (StandCellQuery; both sides call it at its RVA):
// it asks about the first segment's location with flag 0 and returns the answer as it is.
DIFF_TEST(sg_wall_diffusion_front_query, 0x411460)
{
    Fixture fixture;
    shape::CodePatch level{0x7f2260u, shape::CodeOf(&StandCellQuery)};
    using Word = uint32_t(__thiscall *)(void *);
    auto frontLevel = ctx.Original<Word>(0x411460);
    SidesAtBothPrecisions(ctx, "front query", 120, false, [&](uint32_t seed, int side) {
        Shape shape;
        shape.maxRuns = 3;
        shape.minSegments = 1;
        Block<cWallDiffusionManager> block(1);
        auto *m = cWallDiffusionManager::Construct(block.bytes, nullptr);
        Fill(*m, seed, shape);
        gQuerySalt = seed;
        Blob o;
        for (WallDiffusionRun &r : m->mRuns) {
            gLog.clear();
            gQueryBase = r.mSegments.data();
            o.push_back(side == 0 ? frontLevel(&r) : r.FrontCellQuery());
            o.insert(o.end(), gLog.begin(), gLog.end());
        }
        m->~cWallDiffusionManager();
        return o;
    });
}

// SetAngle 0x411960: the angle stored as bits, then diagonal (stored and returned in al) inside the four closed ranges.
// Angles at each bound's nearest float, 1 and 2 ulps either side, in between, specials. pi/8 (0xdc2558 / 0xdc2598) is
// the double of a float, so that float itself is on the bound; the 3pi/8, 5pi/8 and 7pi/8 doubles carry bits below a
// float's (3, 5 and 7 times the float pi/8), so no float lies on them and closed or open ends there are the same for
// every input (the angle reaches the compares as cvtps2pd of a float, 0x411966). Only al is compared: the original
// leaves eax's upper bytes as they came.
DIFF_TEST(sg_wall_diffusion_set_angle, 0x411960)
{
    using Set = uint32_t(__thiscall *)(void *, float);
    auto setAngle = ctx.Original<Set>(0x411960);
    static const float kBounds[] = {0x1.921fb6p-2f, 0x1.2d97c88p+0f, 0x1.f6a7a38p+0f, 0x1.5fdbbf4p+1f};
    SidesAtBothPrecisions(ctx, "set angle", 3000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Shape shape;
        float a;
        switch (gen() % 4) {
        case 0: a = Special(gen); break;
        case 1: a = Value(gen, shape, 4.0f); break;
        default: {
            const float b = kBounds[gen() % 4];
            a = Near(gen, gen() % 2 ? b : -b, shape);
        }
        }
        Block<WallDiffusionRun> b(seed);
        ::new (b.bytes) WallDiffusionRun;
        RandomBytes(b.bytes + 0xc, 0x10, gen);
        Blob o;
        o.push_back(U(a));
        o.push_back((side == 0 ? setAngle(b.Get(), a) : uint32_t(b.Get()->SetAngle(a))) & 0xff);
        Words(o, b.bytes + 0xc, 0x10);
        b.Get()->~WallDiffusionRun();
        return o;
    });
}

// Length 0x410280: the segment count times 1 or sqrt(2) by the diagonal byte (0, 1, 2, 0x80, 0xff), returned in st0.
// Counts 0..6, and counts where the product with the double sqrt(2) rounds to another float than with the float's
// (9, 13, 18, 26, 31, 94, 1044...): the step is the float's.
DIFF_TEST(sg_wall_diffusion_length, 0x410280)
{
    using Len = float(__thiscall *)(void *);
    auto length = ctx.Original<Len>(0x410280);
    static const uint32_t kCounts[] = {9, 13, 18, 26, 31, 94, 104, 1044};
    SidesAtBothPrecisions(ctx, "length", 200, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        WallDiffusionRun r;
        r.mSegments.resize(gen() % 2 ? gen() % 7 : kCounts[gen() % std::size(kCounts)]);
        r.mDiagonal = Flag(gen);
        const float f = side == 0 ? length(&r) : r.Length();
        return Blob{U(f), r.mDiagonal, uint32_t(r.mSegments.size())};
    });
}

// ---- Fixed16 ----------------------------------------------------------------------------------------------------------

// SetFromFloat 0x40bc60 and SetFalloff 0x40bc00 (the scaled flag's byte with junk above it) on a word of junk: both
// return this. Values at the rounding edges ((k + 0.5) / 65536, 1-2 ulps either side), at the edge of the magic range
// (|f * 65536| near 2^22 and 2^23, past it the sum leaves the integer window), the falloff's poles (f = -1, -1 / 1.4),
// specials.
DIFF_TEST(sg_wall_diffusion_fixed, 0x40bc60)
{
    using FromFloat = void *(__thiscall *)(void *, float);
    using Falloff = void *(__thiscall *)(void *, uint32_t, float);
    auto fromFloat = ctx.Original<FromFloat>(0x40bc60);
    auto falloff = ctx.Original<Falloff>(0x40bc00);
    auto ourFalloff = reinterpret_cast<Falloff>(shape::MemberCode(&Fixed16::SetFalloff));
    SidesAtBothPrecisions(ctx, "fixed", 4000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Shape shape;
        auto draw = [&]() -> float {
            switch (gen() % 7) {
            case 0: return Special(gen);
            case 1: return Value(gen, shape, 4.0f);
            case 2: {
                const float k = float(int(gen() % 2001) - 1000);
                return Near(gen, (k + (gen() % 2 ? 0.5f : 0.0f)) / 65536.0f, shape);
            }
            case 3: {
                const float e = gen() % 2 ? 4194304.0f / 65536.0f : 8388608.0f / 65536.0f;
                return Near(gen, gen() % 2 ? e : -e, shape);
            }
            case 4: return Near(gen, gen() % 2 ? -1.0f : -1.0f / 1.4f, shape);
            case 5: return Fl((gen() & 0x807fffffu) | ((100u + gen() % 60) << 23));
            default: return float(int(gen() % 65)) / 64.0f;
            }
        };
        const float a = draw();
        float b = draw();
        uint32_t flag = ByteArg(gen, Flag(gen));
        if (seed % 8 == 0) {
            // Falloffs where rounding the denominator to float before dividing would move the result across a
            // half-integer of 1/65536 (found by search): the division is the double's.
            static const struct { uint32_t scaled, bits; } kEdges[] = {
                {1, 3207573835u}, {1, 3207733731u}, {0, 1097891905u}, {1, 1087154351u},
                {0, 1066741339u}, {0, 1064956551u}, {0, 1068746085u}, {0, 1098866401u}};
            const auto &e = kEdges[(seed / 8) % 8];
            b = Fl(e.bits);
            flag = ByteArg(gen, uint8_t(e.scaled));
        }
        Blob o{U(a), U(b), flag};
        Fixed16 x{int32_t(gen())}, y{int32_t(gen())};
        void *r1 = side == 0 ? fromFloat(&x, a) : x.SetFromFloat(a);
        void *r2 = side == 0 ? falloff(&y, flag, b) : ourFalloff(&y, flag, b);
        o.push_back(uint32_t(x.mValue));
        o.push_back(uint32_t(y.mValue));
        o.push_back(r1 == &x);
        o.push_back(r2 == &y);
        return o;
    });
}

// ---- the angle tests --------------------------------------------------------------------------------------------------

// WallAnglesNear 0x4101f0 and WallAnglesQuarterApart 0x411430 (__cdecl, al): pairs whose difference (plus pi/2) sits at
// the tolerance 0.03 pi and 1-2 ulps either side, equal, specials; only al compared. WallOctant 0x410170 (al): angles at
// the octant edges (pi/8 - k pi/4, 1-2 ulps either side), far turns (fmod), specials.
DIFF_TEST(sg_wall_diffusion_angles, 0x4101f0)
{
    using Pair2 = uint32_t(__cdecl *)(float, float);
    using One = uint32_t(__cdecl *)(float);
    auto anglesNear = ctx.Original<Pair2>(0x4101f0);
    auto quarter = ctx.Original<Pair2>(0x411430);
    auto octant = ctx.Original<One>(0x410170);
    constexpr float kTol = 0x1.8209f6p-4f;
    constexpr float kHalfPi = 0x1.921fb6p+0f;
    SidesAtBothPrecisions(ctx, "angles", 5000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed);
        Shape shape;
        shape.directions = true;
        const float a = gen() % 8 == 0 ? Special(gen) : Angle(gen, shape);
        float b;
        switch (gen() % 6) {
        case 0: b = Special(gen); break;
        case 1: b = a; break;
        case 2: b = Value(gen, shape, 4.0f); break;
        default: {
            // b - a (+ pi/2 for the quarter test) at the tolerance's edge, either sign.
            const float edge = gen() % 2 ? kTol : -kTol;
            const float shift = gen() % 2 ? -kHalfPi : 0.0f;
            b = Near(gen, a + edge + shift, shape);
        }
        }
        float c;
        switch (gen() % 5) {
        case 0: c = Special(gen); break;
        case 1: c = Value(gen, shape, 40.0f); break;
        case 2: c = Fl((gen() & 0x807fffffu) | ((140u + gen() % 100) << 23)); break; // far turns, up to the largest
        default: c = Near(gen, 0x1.921fb6p-2f - float(int(gen() % 17) - 8) * 0x1.921fb6p-1f, shape);
        }
        Blob o{U(a), U(b), U(c)};
        o.push_back((side == 0 ? anglesNear(a, b) : uint32_t(nTSSG::WallAnglesNear(a, b))) & 0xff);
        o.push_back((side == 0 ? quarter(a, b) : uint32_t(nTSSG::WallAnglesQuarterApart(a, b))) & 0xff);
        o.push_back((side == 0 ? octant(c) : uint32_t(nTSSG::WallOctant(c))) & 0xff);
        return o;
    });
}
