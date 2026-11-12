// Lane 261111g (row group SU6): nGZSceneGraph's generated textures (src/sg/SGUtilitiesTexture.cpp, declared in
// src/sg/SGUtilities.h).
//
//   SG_UtilitiesTextureConstants  the image's constants the code uses (360, 120, 60, 0.5 double and float, 1.5 * 2^23, 1,
//       0, 49152, 255), read from the image.
//   SG_UtilitiesCubeDirection  0x18e0b0 on faces -1..7 and random, sizes 0, 1, 2, odd, negative and large, texels inside
//       and outside the face: the three out floats (pre-filled, so the ones not written show) at both x87 precisions.
//   SG_UtilitiesPow  0xaebd00 on NaNs of both signs, zeros, infinities, denormals, negatives and powers -40..40, large and
//       INT_MIN: the double's bits.
//   SG_UtilitiesHueColour  0xc631e0 on hues at the sector boundaries (0, 60, ..., 360 and their neighbours in the last
//       place), negatives and random hues under 1e5 (a NaN or huge hue writes outside the original's array: never given),
//       saturations and values with NaNs of both signs: the three floats and the returned pointer.
//   SG_UtilitiesTexture<Name>  each generator on fakes, at both x87 precisions: the COM director (0x1ae18d stood in on both
//       sides) hands out an image fake (class 0x1c4a276c), a texture fake (0x2a956dad, or the cube class 0x4a956dd2) and a
//       noise object (0x9d332114, a C++ cIPerlinNoise whose Fractal2 answers from a table with NaNs, infinities, zeros and
//       denormals). Every fake call is logged in order with its arguments (Init, SetCurrentEntry, LevelData, FillOpaque's
//       colour, SetPixelChannel's value, LevelPitch, LevelCount, Load, AddRef / Release), so the order of calls shows.
//       LevelPitch answers a larger pitch on each call (which of two calls gives the size and which the destination
//       shows); LevelCount answers 0..12 or more. Each image's arena (a face per entry) is pre-filled from the seed and
//       compared whole after the call, with *out (pre-set to a stale value) and every fake's count.
//   SG_UtilitiesTextureFromBuffer  0xaece50 on a cIGZBuffer fake (sizes 0..40, 0..4 bytes a pixel, strides below and
//       above the row, lock answers with junk above the byte, a huge height with a failed lock), a null buffer or out, a
//       refused image, a null, empty or named name and the add flag with junk above its byte; the texture manager
//       (0xae8750) stood in on both sides, its Add logged with the name's text. The source pixels are random.
#include "sgfx.h"

#include "code_patch.h"

#include "numerics/PerlinNoise.h"
#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"
#include "sg/cTextureManager.h"

#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>

using namespace nGZSceneGraph;

namespace {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

const float kQNan = std::numeric_limits<float>::quiet_NaN();
const float kNegNan = std::bit_cast<float>(0xffc00000u);
const float kPayloadNan = std::bit_cast<float>(0x7fc12345u);
const float kInfinity = std::numeric_limits<float>::infinity();

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

// nGZSceneGraph::cIImageData (src/sg/cImageData.h): 6 Init, 7 SetCurrentEntry, 9 LevelData, 17 FillOpaque, 21
// SetPixelChannel, 28 LevelPitch, 31 LevelCount.
constexpr At kImageAt[] = {{6, {6, Ret::Int, true, "Init", "uuuufu"}},
                           {7, {1, Ret::Int, true, "SetCurrentEntry", "u"}},
                           {9, {1, Ret::Int, true, "LevelData", "u"}},
                           {13, {1, Ret::Int, true, "SetName", "s"}},
                           {17, {2, Ret::Int, true, "FillOpaque", "F3u"}},
                           {21, {4, Ret::Int, true, "SetPixelChannel", "uufu"}},
                           {28, {1, Ret::Int, true, "LevelPitch", "u"}},
                           {31, {0, Ret::Int, true, "LevelCount", ""}}};
constexpr auto kImageSlots = Table<39>(kImageAt);
constexpr drawfx::Iface kImage{"ImageData", 39, kImageSlots.data(), false, drawfx::kTestTable};

// nGZSceneGraph::cITextureImage (src/sg/cTextureImage.h): 9 Load.
constexpr At kTextureAt[] = {{9, {2, Ret::Int, true, "Load", "au"}}};
constexpr auto kTextureSlots = Table<11>(kTextureAt);
constexpr drawfx::Iface kTexture{"TextureImage", 11, kTextureSlots.data(), false, drawfx::kTestTable};

// cIGZCOM: 3 GetClassObject (class, iid, out).
constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data(), false, drawfx::kTestTable};

// cIGZBuffer (src/gz/cIGZBuffer.h): 6 Lock, 7 Unlock, 9 GetWidth, 10 GetHeight, 17 GetBytesPerPixel, 34
// GetColorSurfaceBits, 35 GetColorSurfaceStride.
constexpr At kBufferAt[] = {{6, {1, Ret::Int, true, "Lock", "u"}},
                            {7, {1, Ret::Int, true, "Unlock", "u"}},
                            {9, {0, Ret::Int, true, "GetWidth", ""}},
                            {10, {0, Ret::Int, true, "GetHeight", ""}},
                            {17, {0, Ret::Int, true, "GetBytesPerPixel", ""}},
                            {34, {0, Ret::Int, true, "GetColorSurfaceBits", ""}},
                            {35, {0, Ret::Int, true, "GetColorSurfaceStride", ""}}};
constexpr auto kBufferSlots = Table<40>(kBufferAt);
constexpr drawfx::Iface kBuffer{"Buffer", 40, kBufferSlots.data(), false, drawfx::kTestTable};

// cITextureManager (src/sg/cTextureManager.h): 20 Add (texture, name, flags).
constexpr At kManagerAt[] = {{20, {3, Ret::Int, true, "Add", "asu"}}};
constexpr auto kManagerSlots = Table<29>(kManagerAt);
constexpr drawfx::Iface kManager{"TextureManager", 29, kManagerSlots.data(), false, drawfx::kTestTable};

constexpr uint32_t kImageClass = 0x1c4a276c, kImageIid = 0xfc49f6ee;
constexpr uint32_t kTextureClass = 0x2a956dad, kCubeTextureClass = 0x4a956dd2, kTextureIid = 0x2a956da8;
constexpr uint32_t kNoiseClass = 0x9d332114, kNoiseIid = 0xbd33210e;

constexpr unsigned kImages = 2, kTextures = 2;
constexpr size_t kFace = 6144;           // bytes per entry (face) of an image's arena
constexpr size_t kArena = kFace * 6;     // six entries

const float kNoiseValues[] = {0.5f, -1.0f, 1.0f, 0.0f, -0.0f, kQNan, kNegNan, kPayloadNan, kInfinity, -kInfinity,
                              std::bit_cast<float>(1u), std::bit_cast<float>(0x807fffffu), 0.3f, -0.75f, 1e30f, 0.999f,
                              -0.5f, 0.25f, 0.125f, 2.0f};

// The noise object the director hands out (a C++ cIPerlinNoise: our compiler's layout, as the game's): its calls are
// notes; Fractal2 answers from kNoiseValues, or random values.
class TestNoise final : public nGZNumerics::cIPerlinNoise {
public:
    int32_t refs = 0;
    uint32_t next = 0, salt = 0;

    bool QueryInterface(uint32_t iid, void **) override
    {
        drawfx::Note("noise QueryInterface", iid);
        return false;
    }
    uint32_t AddRef() override
    {
        drawfx::Note("noise AddRef", uint32_t(refs));
        return uint32_t(++refs);
    }
    uint32_t Release() override
    {
        drawfx::Note("noise Release", uint32_t(refs));
        return uint32_t(--refs);
    }
    void Init(uint32_t dimensions, uint32_t seed) override { drawfx::Note("noise Init", {dimensions, seed}); }
    bool IsValidDimension(uint32_t) override { return Unexpected("IsValidDimension"), false; }
    float Noise1Cell(uint32_t, float) override { return Unexpected("Noise1Cell"); }
    float Noise1(float) override { return Unexpected("Noise1"); }
    float Noise2Cell(uint32_t, float, int32_t, float) override { return Unexpected("Noise2Cell"); }
    float Noise2(float, float) override { return Unexpected("Noise2"); }
    float Noise3Cell(uint32_t, float, int32_t, float, int32_t, float) override { return Unexpected("Noise3Cell"); }
    float Noise3(float, float, float) override { return Unexpected("Noise3"); }
    float Fractal1Cell(int32_t, float, float, float, int32_t) override { return Unexpected("Fractal1Cell"); }
    float Fractal1(float, float, float, int32_t) override { return Unexpected("Fractal1"); }
    float Fractal2Cell(int32_t, float, int32_t, float, float, float, int32_t) override
    {
        return Unexpected("Fractal2Cell");
    }
    float Fractal2(float x, float y, float amplitude, float gain, int32_t octaves) override
    {
        drawfx::Note("noise Fractal2", {std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(y),
                                        std::bit_cast<uint32_t>(amplitude), std::bit_cast<uint32_t>(gain),
                                        uint32_t(octaves)});
        const uint32_t k = next++ * 2654435761u + salt;
        if ((k >> 8) % 4 == 0) return kNoiseValues[(k >> 12) % std::size(kNoiseValues)];
        return float(int32_t((k >> 10) % 4001) - 2000) / 1000.0f;
    }
    float Fractal3Cell(int32_t, float, int32_t, float, int32_t, float, float, float, int32_t) override
    {
        return Unexpected("Fractal3Cell");
    }
    float Fractal3(float, float, float, float, float, int32_t) override { return Unexpected("Fractal3"); }
    int32_t OctaveCount(float, float, float, float, float) override { return int32_t(Unexpected("OctaveCount")); }

private:
    float Unexpected(const char *what)
    {
        drawfx::gErrors.push_back(std::string("noise slot called: ") + what);
        return 0.0f;
    }
};

struct World {
    Fake com;
    Fake images[kImages];
    Fake textures[kTextures];
    TestNoise noise;
    unsigned nextImage = 0, nextTexture = 0, noiseMade = 0;
    uint32_t levelCount = 0; // what LevelCount answers
    uint32_t pitchStep = 1;  // LevelPitch grows by this on each call
    bool refuseImage = false; // the director answers no image
    Fake buffer, manager;     // 0xaece50's source buffer and the texture manager
    uint32_t bufferWidth = 0, bufferHeight = 0, bytesPerPixel = 0, stride = 0, lockAnswer = 0, sourceOffset = 0;
};
World gw;
alignas(16) uint8_t gArena[kImages][kArena];
constexpr size_t kSource = 8192;
alignas(16) uint8_t gSource[kSource]; // the buffer's pixels

uint32_t BufferLock(Fake &, const uint32_t *) { return gw.lockAnswer; }
uint32_t BufferWidth(Fake &, const uint32_t *) { return gw.bufferWidth; }
uint32_t BufferHeight(Fake &, const uint32_t *) { return gw.bufferHeight; }
uint32_t BufferBytesPerPixel(Fake &, const uint32_t *) { return gw.bytesPerPixel; }
uint32_t BufferBits(Fake &, const uint32_t *) { return drawfx::P(gSource + gw.sourceOffset); }
uint32_t BufferStride(Fake &, const uint32_t *) { return gw.stride; }

void *__cdecl StandManager()
{
    drawfx::Note("texture manager", 0);
    return &gw.manager;
}

// Image fakes: value[0] width, [1] height, [2] the pitch's base (3 * width and a slack), [4] the pool index, [5] the
// current entry, [6] LevelPitch's calls so far, [7] the slack.
uint32_t ImageInit(Fake &self, const uint32_t *a)
{
    self.value[0] = a[0];
    self.value[1] = a[1];
    self.value[2] = (a[0] & 63) * 3 + self.value[7];
    return 1;
}
uint32_t ImageEntry(Fake &self, const uint32_t *a)
{
    self.value[5] = a[0];
    return 0;
}
uint32_t ImageData(Fake &self, const uint32_t *)
{
    return drawfx::P(gArena[self.value[4]] + (self.value[5] % 6) * kFace);
}
uint32_t ImagePitch(Fake &self, const uint32_t *) { return self.value[2] + gw.pitchStep * self.value[6]++; }
uint32_t ImageLevels(Fake &, const uint32_t *) { return gw.levelCount; }

Fake *HandOutImage()
{
    difftest::NotCounted bookkeeping;
    const unsigned i = gw.nextImage++ % kImages;
    if (gw.nextImage > kImages) drawfx::Note("image pool reused", i);
    Fake &f = gw.images[i];
    const uint32_t slack = f.value[7];
    f.Reset<kImage>(700 + i);
    f.On(6, &ImageInit);
    f.On(7, &ImageEntry);
    f.On(9, &ImageData);
    f.On(28, &ImagePitch);
    f.On(31, &ImageLevels);
    f.value[4] = i;
    f.value[7] = slack;
    ++f.refs;
    return &f;
}
// A texture's AddRef notes whether the caller's result cell already holds it (the store comes before the count).
void **gResultCell;
uint32_t TextureAddRef(Fake &self, const uint32_t *)
{
    if (gResultCell) drawfx::Note("cell holds texture", uint32_t(*gResultCell == &self));
    return uint32_t(++self.refs);
}

Fake *HandOutTexture()
{
    difftest::NotCounted bookkeeping;
    const unsigned i = gw.nextTexture++ % kTextures;
    if (gw.nextTexture > kTextures) drawfx::Note("texture pool reused", i);
    Fake &t = gw.textures[i];
    t.Reset<kTexture>(900 + i);
    t.On(1, &TextureAddRef);
    ++t.refs;
    return &t;
}
uint32_t ComGetClassObject(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    void *made = nullptr;
    if (a[0] == kImageClass && a[1] == kImageIid) made = gw.refuseImage ? nullptr : HandOutImage();
    else if ((a[0] == kTextureClass || a[0] == kCubeTextureClass) && a[1] == kTextureIid) made = HandOutTexture();
    else if (a[0] == kNoiseClass && a[1] == kNoiseIid) {
        ++gw.noiseMade;
        ++gw.noise.refs;
        made = static_cast<nGZNumerics::cIPerlinNoise *>(&gw.noise);
    }
    if (out) *out = made;
    return made != nullptr;
}

void *__cdecl StandCom()
{
    drawfx::Note("com", 0);
    return &gw.com;
}

struct Stands {
    shape::StandIn com{0x1ae18du, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn manager{0xae8750u, &nGZSceneGraph::TextureManager, &StandManager};
};

void Build(uint32_t seed, drawfx::Cells &)
{
    gw.com.Reset<kCom>(400);
    gw.com.On(3, &ComGetClassObject);
    gw.nextImage = gw.nextTexture = gw.noiseMade = 0;
    gw.refuseImage = false;
    gw.buffer.Reset<kBuffer>(500);
    gw.buffer.On(6, &BufferLock);
    gw.buffer.On(9, &BufferWidth);
    gw.buffer.On(10, &BufferHeight);
    gw.buffer.On(17, &BufferBytesPerPixel);
    gw.buffer.On(34, &BufferBits);
    gw.buffer.On(35, &BufferStride);
    gw.manager.Reset<kManager>(600);
    gw.noise.refs = 0;
    gw.noise.next = 0;
    gw.noise.salt = seed * 0x9e3779b9u;
    std::mt19937 fill(seed ^ 0x51ed270bu);
    for (unsigned i = 0; i < kImages; ++i) {
        gw.images[i].value[7] = 0;
        uint32_t *const words = reinterpret_cast<uint32_t *>(gArena[i]);
        for (size_t k = 0; k < kArena / 4; ++k) words[k] = fill();
    }
    uint32_t *const source = reinterpret_cast<uint32_t *>(gSource);
    for (size_t k = 0; k < kSource / 4; ++k) source[k] = fill();
}

// After a generator: *out, the counts, each handed-out image's arena (whole).
void NoteAfter(void *out)
{
    if (out == reinterpret_cast<void *>(uintptr_t(0x5a5a0001u))) drawfx::Note("out untouched", 0);
    else drawfx::Note("out", drawfx::Ptr(out));
    drawfx::Note("made", {gw.nextImage, gw.nextTexture, gw.noiseMade});
    drawfx::Note("com refs", uint32_t(gw.com.refs));
    for (unsigned i = 0; i < kImages && i < gw.nextImage; ++i) {
        drawfx::Note("image refs", {i, uint32_t(gw.images[i].refs), gw.images[i].value[6]});
        drawfx::Call c;
        c.note = "arena";
        const uint32_t *const words = reinterpret_cast<const uint32_t *>(gArena[i]);
        c.words.assign(words, words + kArena / 4);
        drawfx::gLog.push_back(std::move(c));
    }
    for (unsigned i = 0; i < kTextures && i < gw.nextTexture; ++i)
        drawfx::Note("texture refs", {i, uint32_t(gw.textures[i].refs)});
    drawfx::Note("noise refs", uint32_t(gw.noise.refs));
}

// A size: mostly small, with the 0 / 1 / 2 / 3 boundaries often.
int32_t Size(std::mt19937 &rng, uint32_t most)
{
    const uint32_t r = rng() % 8;
    if (r < 3) return int32_t(r == 0 ? 0 : r == 1 ? 1 : 2 + rng() % 2);
    return int32_t(rng() % (most + 1));
}

float Channel(std::mt19937 &rng)
{
    static const float kOdd[] = {0.0f, -0.0f, 1.0f, 255.0f, 255.5f, 256.0f, -1.0f, 127.5f, 128.5f, 0.5f, 1e10f, -1e10f,
                                 kInfinity, -kInfinity, kQNan, kNegNan, kPayloadNan, 254.5f, 0.49999997f};
    const uint32_t r = rng();
    if (r % 4 == 0) return kOdd[(r / 4) % std::size(kOdd)];
    return float(int32_t(rng() % 4000) - 1000) * 0.125f;
}

enum Generator { kDensity, kMipTest, kNormalCube, kNoise, kRampH, kRampV, kFalloff, kFromBuffer };

const char kNameA[] = "texture_from_buffer", kNameB[] = "";

void RunGenerator(difftest::Context &ctx, Generator which, bool original, std::mt19937 &rng)
{
    void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a0001u)); // stale: never released
    gResultCell = &out;
    for (Fake &f : gw.images) f.value[7] = rng() % 3 == 0 ? 3 * (1 + rng() % 2) : 0;
    gw.levelCount = rng() % 6 == 0 ? 13 + rng() % 30 : rng() % 13;
    gw.pitchStep = 1 + rng() % 3;
    switch (which) {
    case kDensity: {
        const int32_t w = Size(rng, 300), h = Size(rng, 300);
        static const float kHues[] = {0.0f, 60.0f, 120.0f, 180.0f, 240.0f, 300.0f, 360.0f, -0.0f, -360.0f, 720.0f,
                                      -1e-6f, 359.99997f, 119.99999f, 59.999996f, 60.000004f};
        const float hue = rng() % 3 == 0 ? kHues[rng() % std::size(kHues)] : float(int32_t(rng() % 200000) - 100000) * 0.01f;
        static const float kSteps[] = {0.0f, 60.0f, 120.0f, 30.0f, -60.0f, 1e-3f, 7.5f, 360.0f, -0.0f};
        const float step = rng() % 2 ? kSteps[rng() % std::size(kSteps)] : float(int32_t(rng() % 2000) - 1000) * 0.1f;
        drawfx::Note("density", {uint32_t(w), uint32_t(h), std::bit_cast<uint32_t>(hue), std::bit_cast<uint32_t>(step)});
        if (original) ctx.Original<void(__cdecl *)(int32_t, int32_t, float, float, void **)>(0xaec1a0)(w, h, hue, step, &out);
        else MakeDensityTexture(w, h, hue, step, &out);
        break;
    }
    case kMipTest: {
        const int32_t w = Size(rng, 300), h = Size(rng, 300);
        drawfx::Note("mipTest", {uint32_t(w), uint32_t(h)});
        if (original) ctx.Original<void(__cdecl *)(int32_t, int32_t, void **)>(0xaec310)(w, h, &out);
        else MakeMipTestTexture(w, h, &out);
        break;
    }
    case kNormalCube: {
        const uint32_t size = rng() % 5 == 0 ? rng() : uint32_t(Size(rng, 300));
        drawfx::Note("normal cube", size);
        if (original) ctx.Original<void(__cdecl *)(uint32_t, void **)>(0xaec4a0)(size, &out);
        else MakeNormalCubeTexture(size, &out);
        break;
    }
    case kNoise: {
        const int32_t w = Size(rng, 12), h = Size(rng, 12);
        const int32_t seed = int32_t(rng());
        static const float kScales[] = {1.0f, 0.5f, 3.0f, 0.0f, -0.0f, -2.0f, 1e-30f, 1e30f, kInfinity, kQNan, kNegNan,
                                        kPayloadNan, 7.0f};
        const float sx = rng() % 3 == 0 ? kScales[rng() % std::size(kScales)] : float(1 + rng() % 64) * 0.25f;
        const float sy = rng() % 3 == 0 ? kScales[rng() % std::size(kScales)] : float(1 + rng() % 64) * 0.25f;
        drawfx::Note("noise", {uint32_t(w), uint32_t(h), uint32_t(seed), std::bit_cast<uint32_t>(sx),
                               std::bit_cast<uint32_t>(sy)});
        if (original)
            ctx.Original<void(__cdecl *)(int32_t, int32_t, int32_t, float, float, void **)>(0xaec6b0)(w, h, seed, sx, sy,
                                                                                                      &out);
        else MakeNoiseTexture(w, h, seed, sx, sy, &out);
        break;
    }
    case kRampH:
    case kRampV: {
        const int32_t w = Size(rng, 33), h = Size(rng, 33);
        float from[3], to[3];
        for (float &f : from) f = Channel(rng);
        for (float &f : to) f = Channel(rng);
        drawfx::Note(which == kRampH ? "rampH" : "rampV", {uint32_t(w), uint32_t(h)});
        drawfx::Note("from", {std::bit_cast<uint32_t>(from[0]), std::bit_cast<uint32_t>(from[1]), std::bit_cast<uint32_t>(from[2])});
        drawfx::Note("to", {std::bit_cast<uint32_t>(to[0]), std::bit_cast<uint32_t>(to[1]), std::bit_cast<uint32_t>(to[2])});
        using Ramp = void(__cdecl *)(int32_t, int32_t, const float *, const float *, void **);
        if (original) ctx.Original<Ramp>(which == kRampH ? 0xaec8e0 : 0xaecba0)(w, h, from, to, &out);
        else if (which == kRampH) MakeRampHTexture(w, h, from, to, &out);
        else MakeRampVTexture(w, h, from, to, &out);
        break;
    }
    case kFalloff: {
        const uint32_t size = uint32_t(Size(rng, 20));
        float range[2], colour[3];
        static const float kRanges[] = {0.0f, 255.0f, 128.0f, -10.0f, 300.0f, 0.5f, 254.5f, kQNan, kNegNan, kPayloadNan,
                                        kInfinity, -kInfinity, -0.0f, 255.99998f};
        for (float &f : range) f = rng() % 2 ? kRanges[rng() % std::size(kRanges)] : float(int32_t(rng() % 4000) - 1000) * 0.125f;
        static const float kColours[] = {0.0f, 1.0f, -1.0f, 0.57735026f, 0.70710677f, kQNan, kNegNan, kInfinity, 2.0f, -0.0f};
        for (float &f : colour) f = rng() % 2 ? kColours[rng() % std::size(kColours)] : float(int32_t(rng() % 2001) - 1000) * 1e-3f;
        static const int32_t kPowers[] = {0, 1, 2, 3, 4, 8, 16, -1, -2, 31, 1000, -1000, INT_MIN, INT_MAX};
        const int32_t power = kPowers[rng() % std::size(kPowers)];
        drawfx::Note("falloff", {size, uint32_t(power)});
        drawfx::Note("range", {std::bit_cast<uint32_t>(range[0]), std::bit_cast<uint32_t>(range[1])});
        drawfx::Note("colour", {std::bit_cast<uint32_t>(colour[0]), std::bit_cast<uint32_t>(colour[1]), std::bit_cast<uint32_t>(colour[2])});
        using Falloff = void(__cdecl *)(uint32_t, const float *, const float *, int32_t, void **);
        if (original) ctx.Original<Falloff>(0xaebd70)(size, range, colour, power, &out);
        else MakeFalloffCubeTexture(size, range, colour, power, &out);
        break;
    }
    case kFromBuffer: {
        // Sizes keep the copies inside the arenas (rows of up to 4 x 40 bytes, 40 rows at a pitch under 200); a huge
        // height only with a failed lock (nothing copied). The lock's answer has junk above its byte (bool: al only).
        static const uint32_t kLocks[] = {1, 0, 0x100, 0xff01, 0xffffff00u, 0x80};
        gw.lockAnswer = kLocks[rng() % std::size(kLocks)];
        gw.bufferWidth = uint32_t(Size(rng, 40));
        gw.bufferHeight = uint32_t(Size(rng, 40));
        if ((gw.lockAnswer & 0xff) == 0 && rng() % 4 == 0) gw.bufferHeight = 0x80000000u + rng() % 3;
        gw.bytesPerPixel = rng() % 5;
        gw.stride = rng() % 4 == 0 ? rng() % 8 : rng() % 200;
        gw.sourceOffset = rng() % 32;
        gw.refuseImage = rng() % 10 == 0;
        const uint32_t pick = rng() % 12;
        cIGZBuffer *const buffer = pick == 0 ? nullptr : reinterpret_cast<cIGZBuffer *>(&gw.buffer);
        void **const target = pick == 1 ? nullptr : &out;
        const char *const name = rng() % 4 == 0 ? nullptr : rng() % 3 == 0 ? kNameB : kNameA;
        const uint32_t add = rng() % 3 == 0 ? 0u : rng() % 3 == 0 ? 0xffffff00u + rng() % 2 : 1u + (rng() % 2) * 0x100u;
        drawfx::Note("from buffer", {pick, gw.lockAnswer, gw.bufferWidth, gw.bufferHeight, gw.bytesPerPixel, gw.stride,
                                     gw.sourceOffset, uint32_t(gw.refuseImage), add});
        using FromBuffer = void(__cdecl *)(cIGZBuffer *, void **, const char *, uint32_t);
        if (original) ctx.Original<FromBuffer>(0xaece50)(buffer, target, name, add);
        else reinterpret_cast<FromBuffer>(&MakeTextureFromBuffer)(buffer, target, name, add); // add's byte only
        break;
    }
    }
    gResultCell = nullptr;
    NoteAfter(out);
}

void CheckGenerator(difftest::Context &ctx, Generator which, const char *what, unsigned seeds)
{
    Stands stands;
    for (int precision = 0; precision < 2; ++precision) {
        difftest::X87Precision pc(precision == 0 ? difftest::X87Pc::Bits53 : difftest::X87Pc::Bits24);
        sgfx::CheckSides(ctx, what, seeds, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
                         [&](bool original, std::mt19937 &rng) {
                             RunGenerator(ctx, which, original, rng);
                             ++ctx.cases;
                         });
    }
}

// ---- the direct callees ----

float OddFloat(std::mt19937 &rng)
{
    static const float kOdd[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 2.0f, kQNan, kNegNan, kPayloadNan, kInfinity, -kInfinity,
                                 std::bit_cast<float>(1u), std::bit_cast<float>(0x80000001u), 1e30f, -1e30f, 1e-30f,
                                 3.4028235e38f, 0.99999994f, 1.0000001f};
    const uint32_t r = rng();
    if (r % 4 == 0) return kOdd[(r / 4) % std::size(kOdd)];
    return float(int32_t(rng() % 20001) - 10000) * 1e-3f;
}

void RunCubeDirection(difftest::Context &ctx, const char *precision)
{
    const auto theirs = ctx.Original<void(__cdecl *)(float *, int32_t, int32_t, int32_t, int32_t)>(0x18e0b0);
    std::mt19937 rng(0x18e0b0);
    static const int32_t kSizes[] = {0, 1, 2, 3, 4, 5, 16, 17, 31, -1, -2, -3, 0x7fffffff, INT_MIN, 1 << 25, (1 << 25) + 1};
    for (unsigned n = 0; n < 200000; ++n) {
        const int32_t face = n % 10 == 9 ? int32_t(rng()) : int32_t(n % 10) - 1;
        const int32_t size = rng() % 3 == 0 ? kSizes[rng() % std::size(kSizes)] : int32_t(rng() % 64);
        const int32_t range = size > 0 && size < 1000 ? size + 4 : 40;
        const int32_t x = rng() % 8 == 0 ? int32_t(rng()) : int32_t(rng() % uint32_t(range)) - 2;
        const int32_t y = rng() % 8 == 0 ? int32_t(rng()) : int32_t(rng() % uint32_t(range)) - 2;
        float a[3] = {kPayloadNan, -3.5f, 7.25f}, b[3] = {kPayloadNan, -3.5f, 7.25f};
        theirs(a, face, x, y, size);
        CubeDirection(b, face, x, y, size);
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof a) != 0) {
            ctx.Fail("%s CubeDirection(face %d, %d, %d, size %d): %08x %08x %08x, ours %08x %08x %08x", precision, face, x,
                     y, size, std::bit_cast<uint32_t>(a[0]), std::bit_cast<uint32_t>(a[1]), std::bit_cast<uint32_t>(a[2]),
                     std::bit_cast<uint32_t>(b[0]), std::bit_cast<uint32_t>(b[1]), std::bit_cast<uint32_t>(b[2]));
            return;
        }
    }
}

void RunPow(difftest::Context &ctx, const char *precision)
{
    const auto theirs = ctx.Original<double(__cdecl *)(float, int32_t)>(0xaebd00);
    std::mt19937 rng(0xaebd00);
    static const int32_t kPowers[] = {0, 1, -1, 2, -2, 3, 31, 32, 127, 128, 149, 150, -126, -127, -149, -150, 1000, -1000,
                                      INT_MAX, INT_MIN, INT_MIN + 1, 1 << 24, (1 << 24) + 1};
    for (unsigned n = 0; n < 200000; ++n) {
        const float x = OddFloat(rng);
        const int32_t y = rng() % 3 == 0 ? kPowers[rng() % std::size(kPowers)] : int32_t(rng() % 81) - 40;
        const double a = theirs(x, y), b = Pow(x, y);
        ++ctx.cases;
        if (std::bit_cast<uint64_t>(a) != std::bit_cast<uint64_t>(b)) {
            ctx.Fail("%s Pow(%08x, %d): %016llx, ours %016llx", precision, std::bit_cast<uint32_t>(x), y,
                     static_cast<unsigned long long>(std::bit_cast<uint64_t>(a)),
                     static_cast<unsigned long long>(std::bit_cast<uint64_t>(b)));
            return;
        }
    }
}

void RunHueColour(difftest::Context &ctx, const char *precision)
{
    const auto theirs = ctx.Original<float *(__cdecl *)(float *, float, float, float, float)>(0xc631e0);
    std::mt19937 rng(0xc631e0);
    static const float kBounds[] = {0.0f, 60.0f, 120.0f, 180.0f, 240.0f, 300.0f, 360.0f, 420.0f, 480.0f, 720.0f, -60.0f,
                                    -120.0f, -360.0f, 99999.0f, -99999.0f, 30.0f, 90.0f, 1e-30f, -1e-30f, -0.0f};
    for (unsigned n = 0; n < 300000; ++n) {
        float hue;
        const uint32_t pick = rng() % 4;
        if (pick == 0) hue = float(int32_t(rng() % 2000000) - 1000000) * 0.1f;
        else if (pick == 1) hue = float(int32_t(rng() % 36001) - 18000) * 0.01f;
        else {
            hue = kBounds[rng() % std::size(kBounds)];
            static const int32_t kUlps[] = {0, 1, -1, 2, -2, 3};
            if (hue != 0.0f) hue = std::bit_cast<float>(std::bit_cast<uint32_t>(hue) + uint32_t(kUlps[rng() % std::size(kUlps)]));
        }
        const float s = rng() % 4 == 0 ? OddFloat(rng) : float(rng() % 1001) * 1e-3f;
        const float v = rng() % 4 == 0 ? OddFloat(rng) : float(rng() % 1001) * 1e-3f;
        const float unused = OddFloat(rng);
        float a[3] = {kPayloadNan, -3.5f, 7.25f}, b[3] = {kPayloadNan, -3.5f, 7.25f};
        float *const ra = theirs(a, hue, s, v, unused);
        float *const rb = HueColour(b, hue, s, v, unused);
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof a) != 0 || ra != a || rb != b) {
            ctx.Fail("%s HueColour(%08x, %08x, %08x): %08x %08x %08x, ours %08x %08x %08x (returned %s / %s)", precision,
                     std::bit_cast<uint32_t>(hue), std::bit_cast<uint32_t>(s), std::bit_cast<uint32_t>(v),
                     std::bit_cast<uint32_t>(a[0]), std::bit_cast<uint32_t>(a[1]), std::bit_cast<uint32_t>(a[2]),
                     std::bit_cast<uint32_t>(b[0]), std::bit_cast<uint32_t>(b[1]), std::bit_cast<uint32_t>(b[2]),
                     ra == a ? "out" : "other", rb == b ? "out" : "other");
            return;
        }
    }
}

} // namespace

// Checks the image's constants SGUtilitiesTexture.cpp writes as literals.
DIFF_TEST(SG_UtilitiesTextureConstants, 0xaebd70)
{
    struct F {
        uint32_t rva;
        float value;
    };
    static const F kFloats[] = {{0xdeb66c, 360.0f}, {0xd9c694, 0.5f}, {0xd9ff00, 12582912.0f}, {0xd9c698, 1.0f},
                                {0xd9e82c, 0.0f},   {0xd9c6a0, 49152.0f}, {0xd9c79c, 255.0f}};
    struct D {
        uint32_t rva;
        double value;
    };
    static const D kDoubles[] = {{0xe9df30, 120.0}, {0xe9df28, 60.0}, {0xda1870, 0.5}};
    for (const F &f : kFloats) {
        ++ctx.cases;
        const float at = *ctx.image.At<float>(f.rva);
        if (std::bit_cast<uint32_t>(at) != std::bit_cast<uint32_t>(f.value))
            ctx.Fail("float at %#x is %08x, ours %08x", f.rva, std::bit_cast<uint32_t>(at), std::bit_cast<uint32_t>(f.value));
    }
    for (const D &d : kDoubles) {
        ++ctx.cases;
        const double at = *ctx.image.At<double>(d.rva);
        if (std::bit_cast<uint64_t>(at) != std::bit_cast<uint64_t>(d.value))
            ctx.Fail("double at %#x is %g, ours %g", d.rva, at, d.value);
    }
    ++ctx.cases;
    const uint32_t *const mask = ctx.image.At<uint32_t>(0xda1880);
    if (mask[0] != 0x7fffffffu) ctx.Fail("abs mask at 0xda1880 is %08x", mask[0]);
}

// Verifies CubeDirection 0x18e0b0.
DIFF_TEST(SG_UtilitiesCubeDirection, 0x18e0b0)
{
    RunCubeDirection(ctx, "53-bit");
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    RunCubeDirection(ctx, "24-bit");
}

// Verifies Pow 0xaebd00.
DIFF_TEST(SG_UtilitiesPow, 0xaebd00)
{
    RunPow(ctx, "53-bit");
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    RunPow(ctx, "24-bit");
}

// Verifies HueColour 0xc631e0.
DIFF_TEST(SG_UtilitiesHueColour, 0xc631e0)
{
    RunHueColour(ctx, "53-bit");
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    RunHueColour(ctx, "24-bit");
}

// Verifies MakeDensityTexture 0xaec1a0.
DIFF_TEST(SG_UtilitiesTextureDensity, 0xaec1a0) { CheckGenerator(ctx, kDensity, "density texture", 1500); }

// Verifies MakeMipTestTexture 0xaec310.
DIFF_TEST(SG_UtilitiesTextureMipTest, 0xaec310) { CheckGenerator(ctx, kMipTest, "mip test texture", 1500); }

// Verifies MakeNormalCubeTexture 0xaec4a0.
DIFF_TEST(SG_UtilitiesTextureNormalCube, 0xaec4a0) { CheckGenerator(ctx, kNormalCube, "normal cube texture", 500); }

// Verifies MakeNoiseTexture 0xaec6b0.
DIFF_TEST(SG_UtilitiesTextureNoise, 0xaec6b0) { CheckGenerator(ctx, kNoise, "noise texture", 1500); }

// Verifies MakeRampHTexture 0xaec8e0.
DIFF_TEST(SG_UtilitiesTextureRampH, 0xaec8e0) { CheckGenerator(ctx, kRampH, "rampH texture", 1500); }

// Verifies MakeRampVTexture 0xaecba0.
DIFF_TEST(SG_UtilitiesTextureRampV, 0xaecba0) { CheckGenerator(ctx, kRampV, "rampV texture", 1500); }

// Verifies MakeFalloffCubeTexture 0xaebd70 (with CubeDirection 0x18e0b0 and Pow 0xaebd00 as each side's).
DIFF_TEST(SG_UtilitiesTextureFalloffCube, 0xaebd70) { CheckGenerator(ctx, kFalloff, "falloff cube texture", 1500); }

// 0xaece50: the texture of a buffer's pixels.
DIFF_TEST(SG_UtilitiesTextureFromBuffer, 0xaece50) { CheckGenerator(ctx, kFromBuffer, "texture from buffer", 1500); }
