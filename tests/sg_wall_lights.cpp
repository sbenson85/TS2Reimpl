// Differential tests of row LD2m (lane 261110k; src/sg/cWallDiffusionManager.h): ApplyLights 0x40dd30 (every segment's
// mCurrent into its rect of the wall light-map atlas: the 4 x 8 block scaled, the bilinear fills, the tail rows), its
// wrappers ApplyAllLights 0x40ed70 / ApplyChangedLights 0x40ed90 and the colour packer 0x4106a0 (PackLightColor).
// Original (by RVA) against ours, every case at the x87's 53-bit and 24-bit precisions; managers from
// tests/sg_wall_diffusion.h (wallfx) alike on both sides.
// - Fakes on both sides, logging into gLog (tests/sg_lighting_common.h) in call order:
//   - the world's level 0x7f2260 (wallfx::QueryModes: the location's words, the answer's low half zero now and then);
//   - the partitions' getter 0x20de97 and its slot 50 (the catalog id: the key's six words logged; 3, other kinds, and 3
//     in the low byte with junk above);
//   - the light map's GetWallRect (slot 21: the key's six words logged, a rect drawn per call from the case's generator
//     on one of three images, false a sixth of the time with junk above al) and GetImage (slot 22: group and index);
//   - the images' LevelData (slot 9), LevelPitch (28) and LevelHeight (27), each logged with the image and the level.
//   GetWallRect's real body (0x2c9bd0's caller's slot, ours cLightMapManager::GetWallRect) stores each out whole (4
//   bytes; the atlas only when it answers true): the fake writes the same and logs difftest::FillOut, capped at 4 (kOut,
//   below, says why).
// - Each image is a pixel buffer drawn from the seed with kMargin rows either side of the image proper (rects reach two
//   rows and columns past the edges: the writes past a row land in its neighbour, past the image in the margin), its
//   pitch 8, 12 or 16 words (a byte or three more now and then: the pitch / 4 is unsigned), its height 24 or 32.
// - Rects: the 4 x 8 block, h = 3w (w 1..5: u and v land on odd integers for w 5 and 3), multiples of w below and above 3w (0, 1, 2, 4, 5, 6 times), other heights
//   (skipped), and the odd ones (0 x 0, negative widths and heights: nothing written; left past right with right >= 0).
//   Never a width of 0 with a height other than 0 (ORIGINAL-BUG: the idiv faults) nor atlas -1 (rows from address 0).
// - Managers: up to 4 runs of 0-4 segments (empty runs given a block whose first slot holds a drawn segment: the level
//   query reads it unchecked), a quarter of the segments' locations copied from an earlier one (the local set's
//   duplicates), half of them with mPrevious equal to mCurrent (the changed-only skip); mOutsideAmbientEnabled, the outside colour and
//   threshold drawn (Channels' rounding points, NaNs, the threshold's -2.5 that zeroes 0.4 t + 1); a quarter of them
//   two-valued (every mCurrent word p or q, the outside colour p's exactly, half of them at scale 2^24: the floor and
//   ceiling's corrections at odd integers show).
// - The outcomes: the log, the manager whole (wallfx::Dump: both change lists rebuilt) and every image's buffer whole.
#include "sg_wall_diffusion.h"
#include "stack_out.h"
#include "ts_services_fixture.h"

#include <array>
#include <memory>

using namespace wallfx;
using nTSSG::cILightMapManager;

namespace {

constexpr uint32_t kMargin = 4; // rows either side of an image
constexpr uint32_t kImages = 3;

struct Image {
    FakeObject object;
    uint32_t id;
    uint32_t pitchWords, pitchBytes, height;
    std::vector<uint32_t> pixels;
    uint8_t *Data() { return reinterpret_cast<uint8_t *>(pixels.data() + kMargin * pitchWords); }
};

void *gImageVT[29];
void *gLightMapVT[23];
FakeObject gLightMap{gLightMapVT};
void *gPartsVT[51];
FakeObject gParts{gPartsVT};

std::array<Image, kImages> *gImages = nullptr;
std::mt19937 gRects;  // GetWallRect's answers, drawn per call
std::mt19937 gKinds;  // slot 50's

void LogKey(const uint32_t *key)
{
    for (int k = 0; k < 6; ++k) L(key[k]);
}

uint8_t *__fastcall LevelData(Image *self, void *, uint32_t level)
{
    L(9), L(self->id), L(level);
    return self->Data();
}
int32_t __fastcall LevelPitch(Image *self, void *, uint32_t level)
{
    L(28), L(self->id), L(level);
    return int32_t(self->pitchBytes);
}
int32_t __fastcall LevelHeight(Image *self, void *, uint32_t level)
{
    L(27), L(self->id), L(level);
    return int32_t(self->height);
}

// Rows a rect writes (ApplyLights' cases: 8 for the block, h when above 0, else none).
int32_t RowsWritten(int32_t w, int32_t h) { return w == 4 && h == 8 ? 8 : (h > 0 ? h : 0); }

// A rect on `image`: its width and height (the cases above), placed so its rows stay within two rows of the image and
// its columns within two columns.
void DrawRect(const Image &image, int32_t &left, int32_t &top, int32_t &right, int32_t &bottom)
{
    static const int32_t kTimes[] = {0, 1, 2, 4, 5, 6};
    static const int32_t kOdd[][2] = {{0, 0}, {-2, 4}, {-2, -6}, {-1, -3}, {2, -4}, {3, -6}, {-3, 6}, {1, 1}, {4, 7},
                                      {4, 9}, {4, 16}, {4, 4}, {8, 8}, {1, 0}};
    int32_t w, h;
    switch (gRects() % 9) {
    case 0:
    case 1: w = 4, h = 8; break;
    case 2:
    case 3: w = 1 + int32_t(gRects() % 5), h = 3 * w; break; // w 3: v at 5; w 5: u at 1
    case 4:
    case 5: w = 1 + int32_t(gRects() % 4), h = w * kTimes[gRects() % std::size(kTimes)]; break;
    case 6: w = 2 + int32_t(gRects() % 3), h = w * int32_t(gRects() % 5) + 1 + int32_t(gRects() % uint32_t(w - 1)); break;
    default: {
        const auto &o = kOdd[gRects() % std::size(kOdd)];
        w = o[0], h = o[1];
        break;
    }
    }
    const int32_t rows = RowsWritten(w, h);
    const int32_t height = int32_t(image.height);
    top = -2 + int32_t(gRects() % uint32_t(height + 4 - rows + 1));
    if (w > 0)
        left = -2 + int32_t(gRects() % uint32_t(int32_t(image.pitchWords) + 4 - w + 1));
    else
        left = -w + int32_t(gRects() % 4); // right = left + w stays >= 0: nothing written
    right = left + w;
    bottom = top + h;
}

// FillOut's cap: each out is a 4-byte slot on both sides. The real GetWallRect stores 4 bytes per out, and the
// original's slots are [ebp-0x50] / [ebp-0x94] / [ebp-0x34] / [ebp-0x8c] / [ebp-0xb8], each a whole int (its
// neighbours are other locals, e.g. the word at [ebp-0x8e] 0x40de70). The harness cannot analyse 0x40dd30's frame and
// its downward scan then finds stale return addresses an earlier test left below the stack pointer: after
// sg_wall_blend_light_map it measured 6 for the top and filled 6 bytes on our side, over our next local. The cap
// bounds that over-estimate; it does not mask a shrink (the fake writes whole ints, and the 2-byte-slot mutant dies).
constexpr size_t kOut = 4;

uint32_t __fastcall WallRect(FakeObject *self, void *, const uint32_t *key, int32_t *left, int32_t *top, int32_t *right,
                             int32_t *bottom, uint32_t *atlas)
{
    L(21), L(self == &gLightMap);
    LogKey(key);
    const uint32_t junk = gRects() & 0xffffff00u;
    if (gRects() % 6 == 0) {
        // The real body's false: the four sides zeroed, the atlas left.
        for (int32_t *out : {left, top, right, bottom}) {
            *out = 0;
            L(uint32_t(difftest::FillOut(out, 4, kOut)));
        }
        return junk;
    }
    const uint32_t index = gRects() % kImages;
    int32_t l, t, r, b;
    DrawRect((*gImages)[index], l, t, r, b);
    L(uint32_t(l)), L(uint32_t(t)), L(uint32_t(r)), L(uint32_t(b)), L(index);
    *left = l, *top = t, *right = r, *bottom = b, *atlas = index;
    for (void *out : {static_cast<void *>(left), static_cast<void *>(top), static_cast<void *>(right),
                      static_cast<void *>(bottom), static_cast<void *>(atlas)})
        L(uint32_t(difftest::FillOut(out, 4, kOut)));
    return junk | 1u;
}
void *__fastcall GetImage(FakeObject *self, void *, uint32_t group, uint32_t index)
{
    L(22), L(self == &gLightMap), L(group), L(index);
    return &(*gImages)[index % kImages];
}

uint32_t __fastcall Catalog(FakeObject *self, void *, const uint32_t *key)
{
    static const uint32_t kKinds[] = {3, 3, 3, 0, 8, 0xa, 0x103, 0xffffff03u, 0x80000003u};
    L(50), L(self == &gParts);
    LogKey(key);
    return kKinds[gKinds() % std::size(kKinds)];
}
void *__cdecl StandPartitions()
{
    L(0x20de97u);
    return &gParts;
}

struct Fakes {
    Fixture fixture;
    svcfx::AllocatorScope allocator; // the local set's nodes and head (the allocator service, both sides)
    QueryModes modes;
    shape::CodePatch parts{0x20de97u, shape::CodeOf(&StandPartitions)};
    Fakes()
    {
        gImageVT[9] = reinterpret_cast<void *>(&LevelData);
        gImageVT[27] = reinterpret_cast<void *>(&LevelHeight);
        gImageVT[28] = reinterpret_cast<void *>(&LevelPitch);
        gLightMapVT[21] = reinterpret_cast<void *>(&WallRect);
        gLightMapVT[22] = reinterpret_cast<void *>(&GetImage);
        gPartsVT[50] = reinterpret_cast<void *>(&Catalog);
    }
    Fakes(const Fakes &) = delete;
    Fakes &operator=(const Fakes &) = delete;
};

// The three images, their pixels drawn from `gen`.
void MakeImages(std::array<Image, kImages> &images, std::mt19937 &gen)
{
    static const uint32_t kPitches[] = {8, 12, 16};
    for (uint32_t i = 0; i < kImages; ++i) {
        Image &im = images[i];
        im.object.vt = gImageVT;
        im.id = 0x1a0 + i;
        im.pitchWords = kPitches[gen() % std::size(kPitches)];
        im.pitchBytes = im.pitchWords * 4 + (gen() % 4 == 0 ? 1 + gen() % 3 : 0);
        im.height = gen() % 2 ? 24 : 32;
        im.pixels.resize((im.height + 2 * kMargin) * im.pitchWords);
        for (uint32_t &p : im.pixels) p = gen();
    }
}

// A scale: 1, 16 (the block's factor 1.0: its fixed low half 0), the block factor's int16 edges (scale / 16 at
// 0.5 and 2^-16 steps), zeros, negatives, wide, infinities, NaNs of their own payloads, Nice values.
float Scale(std::mt19937 &gen)
{
    static const float kScales[] = {1.0f, 16.0f, 8.0f, 0.5f, 2.0f, 0.0f, -0.0f, -1.0f, 32.0f, 15.999f, 1.0f / 4096.0f, 0.75f};
    switch (gen() % 10) {
    case 0: return NaN(gen);
    case 1: return Fl(0x7f800000u | (gen() & 0x80000000u));
    case 2: return Wide(gen, 64.0f);
    case 3: return Nice(gen, 2.0f);
    default: return kScales[gen() % std::size(kScales)];
    }
}

// The manager: Fill's (the settings, up to 4 runs of 0-4 segments), then the cases' shapes (see the top of the file).
struct Manager {
    Block<cWallDiffusionManager> block{1};
    cWallDiffusionManager *m;
    bool bigScale = false; // the case's scale 2^24 (two-value cases)
    Manager(uint32_t seed, std::mt19937 &gen)
    {
        m = cWallDiffusionManager::Construct(block.bytes, nullptr);
        Shape shape;
        shape.maxRuns = 4;
        shape.maxSegments = 4;
        shape.nans = seed % 7 == 0;
        Fill(*m, seed, shape);
        if (gen() % 2) Color(m->mOutsideAmbientColor, gen);
        if (gen() % 3 == 0) {
            static const float kThresholds[] = {0.0f, 1.0f, -2.5f, 2.5f, -1.0f, 1e30f, -1e30f};
            m->mThreshold = kThresholds[gen() % std::size(kThresholds)];
        }
        static const float kAmbients[] = {1.0f, 0.5f, 2.0f, 0.0f};
        if (gen() % 2) m->mOutsideWallAmbient = kAmbients[gen() % std::size(kAmbients)];
        // A quarter of the cases: every mCurrent word one of two (p, q), the outside colour p's channels exactly (k / 255,
        // ambient 1, mOutsideAmbientEnabled set). A pixel read whole from a p word then comes out 0 (and 2^24 scales show any rounding
        // left), where weights of 1 and 0 on its neighbours ((p - q) * 1 + q) need not give p back.
        const bool twoValues = seed % 4 == 1;
        uint32_t words[2] = {gen(), gen()};
        if (twoValues) {
            for (int k = 0; k < 3; ++k)
                m->mOutsideAmbientColor[k] = static_cast<float>((words[0] >> (16 - 8 * k)) & 0xffu) / 255.0f;
            m->mOutsideWallAmbient = 1.0f;
            m->mOutsideAmbientEnabled = 1;
            bigScale = gen() % 2 != 0;
        }
        std::vector<WallDiffusionSegment *> all;
        for (WallDiffusionRun &r : m->mRuns) {
            if (r.mSegments.empty()) {
                r.mSegments.emplace_back();
                FillSegment(r.mSegments.back(), gen, shape);
                r.mSegments.pop_back();
            }
            for (WallDiffusionSegment &s : r.mSegments) {
                if (twoValues)
                    for (size_t k = 0; k < sizeof(s.mCurrent); k += 4)
                        std::memcpy(s.mCurrent + k, &words[gen() % 2], 4);
                if (!all.empty() && gen() % 4 == 0)
                    std::memcpy(s.mLocation, all[gen() % all.size()]->mLocation, sizeof(s.mLocation));
                if (gen() % 2) std::memcpy(s.mPrevious, s.mCurrent, sizeof(s.mCurrent));
                else if (gen() % 2) {
                    std::memcpy(s.mPrevious, s.mCurrent, sizeof(s.mCurrent));
                    s.mPrevious[gen() % sizeof(s.mPrevious)] ^= uint8_t(1u << (gen() % 8));
                }
                all.push_back(&s);
            }
        }
    }
    ~Manager() { m->~cWallDiffusionManager(); }
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;
};

// One case: the manager, images and fakes' generators from the seed, `call` run on them (the side's function); the log,
// the manager and the images.
template <class Call> Blob Case(uint32_t seed, uint32_t rva, Call &&call)
{
    std::mt19937 gen(seed ^ rva);
    Manager mm(seed, gen);
    auto images = std::make_unique<std::array<Image, kImages>>();
    MakeImages(*images, gen);
    gImages = images.get();
    gRects.seed(gen());
    gKinds.seed(gen());
    const float scale = mm.bigScale ? 16777216.0f : Scale(gen);
    gLog.clear();
    call(*mm.m, reinterpret_cast<cILightMapManager *>(&gLightMap), scale, gen);
    gImages = nullptr;
    Blob o = gLog;
    const Blob d = Dump(*mm.m);
    o.insert(o.end(), d.begin(), d.end());
    for (const Image &im : *images) o.insert(o.end(), im.pixels.begin(), im.pixels.end());
    return o;
}

} // namespace

// ApplyLights 0x40dd30 (thiscall, ret 0xc: the light map, the scale, the changed-only byte with junk above it).
DIFF_TEST(sg_wall_lights, 0x40dd30)
{
    Fakes fakes;
    using ApplyFn = void(__thiscall *)(void *, void *, float, uint32_t);
    auto original = ctx.Original<ApplyFn>(0x40dd30);
    SidesAtBothPrecisions(ctx, "apply lights", 2500, false, [&](uint32_t seed, int side) {
        return Case(seed, 0x40dd30u, [&](cWallDiffusionManager &m, cILightMapManager *lights, float scale, std::mt19937 &g) {
            const uint32_t changed = ByteArg(g, Flag(g));
            if (side == 0)
                original(&m, lights, scale, changed);
            else
                m.ApplyLights(lights, scale, uint8_t(changed));
        });
    });
}

// ApplyAllLights 0x40ed70 and ApplyChangedLights 0x40ed90 (thiscall, ret 8): ApplyLights with 0 and 1.
DIFF_TEST(sg_wall_lights_all, 0x40ed70)
{
    Fakes fakes;
    using ApplyFn = void(__thiscall *)(void *, void *, float);
    auto original = ctx.Original<ApplyFn>(0x40ed70);
    SidesAtBothPrecisions(ctx, "apply all lights", 800, false, [&](uint32_t seed, int side) {
        return Case(seed, 0x40ed70u, [&](cWallDiffusionManager &m, cILightMapManager *lights, float scale, std::mt19937 &) {
            if (side == 0)
                original(&m, lights, scale);
            else
                m.ApplyAllLights(lights, scale);
        });
    });
}

DIFF_TEST(sg_wall_lights_changed, 0x40ed90)
{
    Fakes fakes;
    using ApplyFn = void(__thiscall *)(void *, void *, float);
    auto original = ctx.Original<ApplyFn>(0x40ed90);
    SidesAtBothPrecisions(ctx, "apply changed lights", 800, false, [&](uint32_t seed, int side) {
        return Case(seed, 0x40ed90u, [&](cWallDiffusionManager &m, cILightMapManager *lights, float scale, std::mt19937 &) {
            if (side == 0)
                original(&m, lights, scale);
            else
                m.ApplyChangedLights(lights, scale);
        });
    });
}

// PackLightColor 0x4106a0 (cdecl: three floats in, the packed colour in eax): Channels' rounding points and edges.
DIFF_TEST(sg_wall_lights_pack, 0x4106a0)
{
    using PackFn = uint32_t(__cdecl *)(const float *);
    auto original = ctx.Original<PackFn>(0x4106a0);
    SidesAtBothPrecisions(ctx, "pack light color", 4000, false, [&](uint32_t seed, int side) {
        std::mt19937 gen(seed ^ 0x4106a0u);
        float rgb[3];
        Channels(rgb, gen);
        return Blob{side == 0 ? original(rgb) : nTSSG::PackLightColor(rgb)};
    });
}
