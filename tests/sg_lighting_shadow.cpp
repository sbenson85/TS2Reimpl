// Differential test for nTSSG::cLightingManager::BuildSunShadowTexture 0x2d2a70 (lane 261110a, scenegraph LG4h;
// src/sg/cLightingManagerShadow.cpp). Hook shape: the original runs on the same manager as ours.
//
// The world: the manager is 0x548 bytes of junk with the system's terrain (0x20defb, slot 40: the fake grid or null)
// of a drawn grid size (0, 1, 2, 3, ... 65: the power-of-two sizes and their degenerate cases), the shade image
// (+0x38: null, so both images are made through the framework's GetClassObject, or a pool image with +0x3c another),
// the ambient maps (+0xa4 over zero to four entries, null or a fake map of drawn size, 0 and 2^31 and up included), the
// current set (+0xd0 in or out of range of the maps and of the light sets +0x98; the default set at +0x2d0) with drawn
// sun direction (+0x34) and tint (+0xec), the shadow blend scale (+0x244), mSunHeights' four vectors (+0x248, empty or
// heap junk), the samples (+0x2b0, int32 bounds around the cell edges, the extremes, drawn weights), the sample scale
// and margin (+0x2bc, +0x2c0) and the texture handle (+0x34). The grid's heights and normals and the maps' texels are
// hashed from salts with NaNs, zeros and the byte packing's rounding edges among them. The handle answers an object or
// null; its QueryInterface answers yes or no and writes the face or not; the face's current image (asked twice) is the
// shade image or not, each time. The game's fill colour 0xffd964 is drawn and restored.
//
// Fakes on both sides: the system getter 0x20defb (image only; ours calls it), the framework 0x1ae18d, the handle's Get
// 0xae8730 and the cached object 0xae8900, each logging. Compared: the log, the manager's bytes (vectors as size,
// capacity and contents), the images' fields and pixels (hashed per 64 words), the face's call count and the heap
// blocks each side made or freed; at the x87's 53-bit and 24-bit precision.
#include "sg_lighting_fakes.h"

#include <memory>

namespace {

using namespace lightfx;
using nTSSG::cLightSample;

constexpr int kMaps = 3;
constexpr int kMapSlots = 4;
constexpr int kSets = 2;
constexpr int kSampleCount = 8;

FakeMap gMaps[kMaps];
const void *gMapSlots[kMapSlots];
cLightSample gSampleArray[kSampleCount];
alignas(16) uint8_t gManager[0x548];
alignas(16) uint8_t gSets[kSets][0x214];

constexpr uint32_t kFill = 0xffd964;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kShadowTextures; ++i)
        if (p == &gShadowTextures[i]) return 0x900 + i;
    for (int i = 0; i < kMaps; ++i)
        if (p == &gMaps[i]) return 0xa00 + i;
    if (p == &gGrid) return 0x5000;
    if (p == &gSystem) return 0x20de;
    if (p == &gHandleObject) return 0xae00;
    if (p == &gTextureFace) return 0xfa00;
    if (p == &gCached) return 0xca00;
    if (p == &gCom) return 0xc000;
    if (p == GameAddress<const void>(kFill)) return 0xd964;
    return 0xbad00000u | (P(p) & 0xfffff);
}

void *Make(uint32_t cls) { return cls == 0x1c4a276c ? MakeTexture() : nullptr; }

struct Globals : SavedGlobals {
    Globals() { Save(kFill, 16); }
};

// ---- the drawn world ----

float Component(std::mt19937 &gen)
{
    static const float kPool[] = {0.0f, -0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 0.5f, 3.0f, 1.005f, 0.995f, 0.25f, -0.5f};
    if (gen() % 2) return kPool[gen() % std::size(kPool)];
    return Wide(gen, 4.0f);
}

// A heap float vector: empty (all zero words) or `capacity` floats of junk, `size` of them in use.
void DrawFloatVector(std::mt19937 &gen, uint8_t *at)
{
    if (gen() % 3 == 0) {
        PutWord(at, 0, 0), PutWord(at, 4, 0), PutWord(at, 8, 0);
        return;
    }
    const uint32_t capacity = 1 + (gen() % 4 == 0 ? gen() % 5000 : gen() % 300);
    const uint32_t size = gen() % 3 == 0 ? capacity : gen() % (capacity + 1);
    uint32_t *const buffer = reinterpret_cast<uint32_t *>(std::allocator<float>().allocate(capacity));
    for (uint32_t i = 0; i < capacity; ++i) buffer[i] = gen();
    PutWord(at, 0, P(buffer)), PutWord(at, 4, P(buffer) + size * 4), PutWord(at, 8, P(buffer) + capacity * 4);
}

constexpr uint32_t kFloatVectors[] = {0x248, 0x254, 0x260, 0x2a0}; // mSunHeights' mValues, mGround, mHeights, mRun

void DrawSet(std::mt19937 &gen, uint8_t *set)
{
    for (int i = 0; i < 0x214; ++i) set[i] = uint8_t(gen());
    for (uint32_t o : {0x34u, 0x38u, 0x3cu, 0xecu, 0xf0u, 0xf4u}) PutFloat(set, o, Component(gen));
    if (gen() % 3 == 0) PutFloat(set, 0x34, 0.0f), PutFloat(set, 0x38, gen() % 2 ? 0.0f : -0.0f); // straight overhead
    if (gen() % 4 == 0) // a unit direction towards a drawn side
        PutFloat(set, 0x34, 0.6f * (gen() % 2 ? 1 : -1)), PutFloat(set, 0x38, 0.48f * (gen() % 2 ? 1 : -1)),
            PutFloat(set, 0x3c, 0.64f * (gen() % 3 ? 1 : -1));
    if (gen() % 3 == 0) PutFloat(set, 0xec, 1.0f), PutFloat(set, 0xf0, 1.0f), PutFloat(set, 0xf4, 1.0f);
}

uint32_t GridSide(std::mt19937 &gen)
{
    static const uint32_t kSides[] = {0, 1, 2, 3, 4, 5, 8, 9, 16, 17, 33, 65};
    if (gen() % 3 == 0) return kSides[gen() % std::size(kSides)];
    return 2 + gen() % 14;
}

uint32_t MapSide(std::mt19937 &gen)
{
    static const uint32_t kSides[] = {0, 1, 2, 3, 4, 16, 64, 100, 0x80000000u, 0xffffffffu, 0x80000010u};
    if (gen() % 2) return kSides[gen() % std::size(kSides)];
    return 1u << gen() % 8;
}

int32_t Bound(std::mt19937 &gen)
{
    static const int32_t kBounds[] = {0, 10, 15, 20, 5, -5, -10, 100, 155, 160, 640, 650, 0x7fffffff, int32_t(0x80000000),
                                      16777217, -16777217};
    if (gen() % 3 == 0) return kBounds[gen() % std::size(kBounds)];
    return int32_t(gen() % 220) - 30;
}

void DrawSample(std::mt19937 &gen, cLightSample &s)
{
    int32_t bounds[4];
    for (int32_t &b : bounds) b = Bound(gen);
    if (gen() % 2) { // ordered corners
        if (bounds[0] > bounds[2]) std::swap(bounds[0], bounds[2]);
        if (bounds[1] > bounds[3]) std::swap(bounds[1], bounds[3]);
    }
    std::memcpy(s.mValues, bounds, sizeof bounds);
    static const float kWeights[] = {0.0f, -0.0f, 1.0f, 3.0f, -2.0f, 0.5f, 100.0f};
    s.mWeight = gen() % 2 ? kWeights[gen() % std::size(kWeights)] : Wide(gen, 8.0f);
}

uint32_t gPixelSeed;
void DrawTexture(std::mt19937 &gen, int i)
{
    FakeTexture &t = gShadowTextures[i];
    t.vt = gTextureObjectVT, t.id = 0x900u + i, t.refs = 1 + gen() % 3;
    t.width = 1 + gen() % 64, t.height = gen(), t.levels = gen(), t.format = gen(), t.flags = gen();
    t.scale = Fl(gen()), t.locks = 1 + gen() % 2;
    static const uint32_t kJunk[] = {0, 0, 0, 0, 4, 8, 16, 1, 2, 6}; // the stride is the pitch rounded down to words
    t.pitchJunk = kJunk[gen() % std::size(kJunk)];
    uint32_t x = gen() | 1;
    for (uint32_t &p : t.pixels) x = x * 1664525u + 1013904223u, p = x;
}

struct World {
    uint32_t seed;
};

World Draw(uint32_t seed)
{
    std::mt19937 gen(seed * 2654435761u + 0x2d2a70u);
    // the system's terrain
    gGrid = {gGridVT, 0x5000, GridSide(gen), GridSide(gen), gen()};
    if (gen() % 4 == 0) gGrid.width = gGrid.height; // square grids now and then
    gSystemTerrain = gen() % 12 == 0 ? nullptr : &gGrid;
    // the images
    for (int i = 0; i < kShadowTextures; ++i) DrawTexture(gen, i);
    gNextShadowTexture = gen() % kShadowTextures;
    // the ambient maps
    for (int i = 0; i < kMaps; ++i) {
        FakeMap &m = gMaps[i];
        m = {gMapVT, 0xa00u + i, MapSide(gen), MapSide(gen), gen(), gen() % 4 == 0, {}};
        if (gen() % 2) m.height = m.width;
        for (float &c : m.other) c = Component(gen);
    }
    for (const void *&slot : gMapSlots) slot = gen() % 5 == 0 ? nullptr : &gMaps[gen() % kMaps];
    // the samples
    for (cLightSample &s : gSampleArray) DrawSample(gen, s);
    // the manager
    for (auto &b : gManager) b = uint8_t(gen());
    PutWord(gManager, 0, P(gManagerVT));
    PutWord(gManager, 0x34, gen() % 4 == 0 ? 0xffffffffu : gen() % 64);
    const bool made = gen() % 2;
    if (made) {
        PutWord(gManager, 0x38, 0);
        PutWord(gManager, 0x3c, gen() % 3 ? 0 : P(&gShadowTextures[gen() % kShadowTextures]));
    } else {
        const uint32_t shade = gen() % kShadowTextures;
        PutWord(gManager, 0x38, P(&gShadowTextures[shade]));
        PutWord(gManager, 0x3c, P(&gShadowTextures[gen() % 4 ? (shade + 1 + gen() % 3) % kShadowTextures : shade]));
    }
    for (int i = 0; i < kSets; ++i) DrawSet(gen, gSets[i]);
    DrawSet(gen, gManager + 0x2d0);
    const uint32_t sets = gen() % (kSets + 1);
    PutWord(gManager, 0x98, P(gSets)), PutWord(gManager, 0x9c, P(gSets) + sets * 0x214);
    PutWord(gManager, 0xa0, P(gSets) + sets * 0x214);
    const uint32_t maps = gen() % 6 == 0 ? 0 : 1 + gen() % kMapSlots;
    PutWord(gManager, 0xa4, P(gMapSlots)), PutWord(gManager, 0xa8, P(gMapSlots) + maps * 4);
    PutWord(gManager, 0xac, P(gMapSlots) + kMapSlots * 4);
    static const uint32_t kCurrent[] = {0, 1, 2, 3, 4, 0xffffffffu, 0x80000000u};
    PutWord(gManager, 0xd0, gen() % 4 ? gen() % 3 : kCurrent[gen() % std::size(kCurrent)]);
    static const float kBlend[] = {0.0f, -0.0f, 1.0f, 0.5f, 0.02f, 10.0f, -1.0f};
    PutFloat(gManager, 0x244, gen() % 2 ? kBlend[gen() % std::size(kBlend)] : Wide(gen, 2.0f));
    for (uint32_t o : kFloatVectors) DrawFloatVector(gen, gManager + o);
    const uint32_t samples = gen() % 5 == 0 ? 0 : gen() % (kSampleCount + 1);
    PutWord(gManager, 0x2b0, P(gSampleArray)), PutWord(gManager, 0x2b4, P(gSampleArray) + samples * 20);
    PutWord(gManager, 0x2b8, P(gSampleArray) + kSampleCount * 20);
    static const float kScales[] = {0.0f, 1.0f, 0.5f, 2.0f, -1.0f, 0.1f};
    PutFloat(gManager, 0x2bc, gen() % 2 ? kScales[gen() % std::size(kScales)] : Wide(gen, 4.0f));
    static const float kMargins[] = {0.0f, -0.0f, 5.0f, 10.0f, 2.5f, -5.0f, 0.05f};
    PutFloat(gManager, 0x2c0, gen() % 2 ? kMargins[gen() % std::size(kMargins)] : Wide(gen, 20.0f));
    // the texture handle, its face and the face's current image (the shade image or not, at each of the two asks)
    gHandleAnswer = gen() % 5 == 0 ? nullptr : &gHandleObject;
    static const uint32_t kAnswers[] = {0, 1, 4, 5, 0x100, 0x101, 0xffffff00u, 0x12345605u};
    gFaceAnswer = kAnswers[gen() % std::size(kAnswers)];
    const uint32_t shadeImage = made ? P(&gShadowTextures[(gNextShadowTexture) % kShadowTextures]) : Word<uint32_t>(gManager, 0x38);
    for (uint32_t &c : gFaceCurrent) {
        const uint32_t pick = gen() % 4;
        c = pick < 2 ? shadeImage : pick == 2 ? 0 : P(&gShadowTextures[gen() % kShadowTextures]);
    }
    gFaceCalls = 0;
    // the game's fill colour
    for (int k = 0; k < 4; ++k) GameAddress<float>(kFill)[k] = Component(gen);
    return {seed};
}

void AppendFloatVector(Blob &o, const uint8_t *at)
{
    const uint32_t begin = Word<uint32_t>(at, 0), end = Word<uint32_t>(at, 4), cap = Word<uint32_t>(at, 8);
    o.push_back((end - begin) / 4), o.push_back((cap - begin) / 4), o.push_back(begin ? 1 : 0);
    for (uint32_t p = begin; p != end; p += 4) o.push_back(*reinterpret_cast<const uint32_t *>(uintptr_t(p)));
}

bool InFloatVector(uint32_t offset)
{
    for (uint32_t o : kFloatVectors)
        if (offset >= o && offset < o + 12) return true;
    return false;
}

Blob Snapshot(const Globals &globals, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    for (uint32_t i = 0; i < sizeof gManager; i += 4)
        if (!InFloatVector(i)) o.push_back(Word<uint32_t>(gManager, i));
    for (uint32_t v : kFloatVectors) AppendFloatVector(o, gManager + v);
    for (const FakeTexture &t : gShadowTextures) {
        o.push_back(t.refs), o.push_back(t.width), o.push_back(t.height), o.push_back(t.levels), o.push_back(t.format);
        o.push_back(t.flags), o.push_back(U(t.scale)), o.push_back(t.locks);
        for (uint32_t b = 0; b < kTexturePixels; b += 64) {
            uint32_t h = 0x2d2a70;
            for (uint32_t k = 0; k < 64; ++k) h = Mix(h, t.pixels[b + k]);
            o.push_back(h);
        }
    }
    o.push_back(gNextShadowTexture), o.push_back(gFaceCalls);
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

void FreeAll()
{
    // through the STL's allocator: blocks of 4 KB and more are over-aligned with the real pointer stored before them
    for (uint32_t o : kFloatVectors) {
        const uint32_t begin = Word<uint32_t>(gManager, o), cap = Word<uint32_t>(gManager, o + 8);
        if (begin) std::allocator<float>().deallocate(reinterpret_cast<float *>(uintptr_t(begin)), (cap - begin) / 4);
    }
}

struct Scope {
    svcfx::AllocatorScope allocator;
    Globals globals;
    ShadowServices services;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &GetFramework};
    Scope()
    {
        FillShared();
        gIdOf = &Id, gMake = &Make;
        gLog.reserve(1 << 18);
    }
};

} // namespace

// 0x2d2a70: the terrain's grid to the two shade images (made when missing), the sun heights with the samples' raised
// rectangles and their shadow, each texel the ambient map at the bent normal times the tint; then the texture's image
// loaded or set.
DIFF_TEST(sg_lighting_shadow, 0x2d2a70)
{
    Scope scope;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d2a70);
    SidesAtBothPrecisions(ctx, "shadow", 1500, false, [&](uint32_t draw, int side) {
        Draw(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        if (side == 0) original(gManager);
        else reinterpret_cast<nTSSG::cLightingManager *>(gManager)->BuildSunShadowTexture();
        heap.Stop();
        Blob outcome = Snapshot(scope.globals, heap.Blocks(), heap.Bytes());
        FreeAll();
        return outcome;
    });
}
