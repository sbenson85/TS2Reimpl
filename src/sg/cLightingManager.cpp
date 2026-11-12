// nTSSG::cLightingManager (lane IR). See src/sg/cLightingManager.h.
#include "sg/cLightingManager.h"

#include "compgeom/ConvexMath.h"
#include "compgeom/Rigid.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "misc/X87.h"
#include "rz/GameAllocator.h"
#include "rz/StaticGetters2.h"
#include "sg/SGUtilities.h"
#include "simantics/ServiceRefVector.h"
#include "world/WorldHeights.h"
#include "world/WorldTileMath.h"
#include "world/cWorldDB.h"

#include <cmath>
#include <emmintrin.h>
#include <cstring>
#include <set>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

constexpr uint32_t kIidLightingManager = 0x6b390bfe;
constexpr uint32_t kIidMessageTarget2 = 0x452294aa;
// Game data: the default name, slot 20's switch and slot 34's luminance factors.
constexpr uint32_t kDefaultName = 0xdaefa0;
constexpr uint32_t kRefreshSwitch = 0xffde64;
constexpr uint32_t kLuminanceX = 0xdadf5c;
constexpr uint32_t kLuminanceY = 0xdb687c;
constexpr uint32_t kLuminanceZ = 0xdb6874;

float Factor(uint32_t rva) { return *GameAddress<const float>(rva); }

// The original's SSE operations with their operand order (of two NaNs the first operand's survives; MSVC may swap the
// operands of + and *), docs/lane/test.md "NaN operand order".
using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::BitsOf;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;
using nGZCompGeom::convex::X87Pick;

// An x87 subtraction between a register `reg` and a float in memory `mem`, at the thread's precision: of two NaNs the
// register's beats a signalling memory one, else the larger significand wins.
double PickNaN87(double reg, float mem)
{
    if (!(BitsOf(mem) & 0x00400000u)) return reg;
    return X87Pick(reg, static_cast<double>(mem));
}
double SubMem87(double reg, float mem) // fsub dword [mem]: reg - mem
{
    if (reg != reg && mem != mem) return x87::Rounded(PickNaN87(reg, mem));
    return x87::Rounded(reg - static_cast<double>(mem));
}
double SubFromMem87(float mem, double reg) // fsubr dword [mem]: mem - reg
{
    if (reg != reg && mem != mem) return x87::Rounded(PickNaN87(reg, mem));
    return x87::Rounded(static_cast<double>(mem) - reg);
}

// The original's inlined floor: (x - 0.5) + 1.5 * 2^23 leaves the integer at or below x in the low bits; one more when
// that integer plus one equals x (a halfway x rounded to even downwards). In 32-bit wrapping arithmetic, as there.
int32_t FloorToInt(float x)
{
    const float biased = AddS(SubS(x, 0.5f), 12582912.0f);
    uint32_t bits;
    std::memcpy(&bits, &biased, 4);
    const uint32_t result = bits - 0x4b400000u, next = result + 1;
    return static_cast<float>(static_cast<int32_t>(next)) == x ? static_cast<int32_t>(next) : static_cast<int32_t>(result);
}

int32_t Plus(int32_t value, int32_t step) { return static_cast<int32_t>(static_cast<uint32_t>(value) + static_cast<uint32_t>(step)); }

// An integer as the original converts it here: as unsigned, through a double.
double UnsignedToDouble(int32_t value) { return static_cast<double>(static_cast<uint32_t>(value)); }

// (float)sqrt((double)x), the original's sqrtsd (or the CRT's sqrt for negatives).
float Sqrt(float x) { return static_cast<float>(std::sqrt(static_cast<double>(x))); }

void Scale(float *v, float factor)
{
    v[0] = MulS(factor, v[0]);
    v[1] = MulS(factor, v[1]);
    v[2] = MulS(factor, v[2]);
}

// Image slots of an ambient map (receiver class not identified): 26 / 27 width and height of a level, 38 a texel's
// colour (returned through the first argument's buffer).
uint32_t MapWidth(const void *map) { return Call<26>(map, 0u); }
uint32_t MapHeight(const void *map) { return Call<27>(map, 0u); }
const float *MapTexel(const void *map, float *buffer, uint32_t x, uint32_t y)
{
    return Call<38, const float *>(map, buffer, x, y, 0u);
}

// --- vertex lighting helpers (LG2) ---

float Abs(float f) { return nGZCompGeom::convex::FloatOf(BitsOf(f) & 0x7fffffffu); } // andps with 0x7fffffff

// A colour component as a texel byte: (|c| + c) / 4 and 1 - (|h| + h) clamp it to [0, 1] (h = 0.5 - that); 49152 plus
// it leaves round(256 c) in the low bits of the float; 256 counts as 255. A NaN gives a byte from its payload.
uint32_t TexelByte(float c)
{
    const float half = SubS(0.5f, MulS(AddS(Abs(c), c), 0.25f));
    const float biased = AddS(SubS(1.0f, AddS(Abs(half), half)), 49152.0f);
    const uint32_t bits = BitsOf(biased) + 0xb8c00000u;
    return (bits - (bits >> 8)) & 0xffu;
}

uint32_t Saturated(uint32_t sum) { return sum >= 0xffu ? 0xffu : sum; }

// The texel plus bytes for red, green and blue, each sum held at 255; alpha kept.
uint32_t AddBytes(uint32_t texel, uint32_t red, uint32_t green, uint32_t blue)
{
    return (texel & 0xff000000u) | Saturated(red + (texel >> 16 & 0xffu)) << 16 | Saturated(green + (texel >> 8 & 0xffu)) << 8 |
           Saturated(blue + (texel & 0xffu));
}

// Texel (x, y) of a texture with rows `stride` words apart, the address arithmetic wrapping as the original's.
uint32_t *Texel(uint32_t *pixels, int32_t stride, uint32_t x, uint32_t y)
{
    return reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(pixels) + (y * static_cast<uint32_t>(stride) + x) * 4u);
}

// The colour (read through its pointer for each texel) added to a texel.
void AddColour(uint32_t *texel, const float *colour)
{
    const uint32_t red = TexelByte(colour[0]), green = TexelByte(colour[1]), blue = TexelByte(colour[2]);
    *texel = AddBytes(*texel, red, green, blue);
}

// numerator / span, the span an unsigned count made a float first, the quotient a float.
float TileStep(double numerator, uint32_t span)
{
    return static_cast<float>(numerator / static_cast<double>(static_cast<float>(UnsignedToDouble(static_cast<int32_t>(span)))));
}

// Slot 22 of the light source slot 39 asks for vertex normals (class not identified): the normal at vertex (x, y).
void VertexNormal(const void *normals, float *out, int32_t x, int32_t y) { Call<22, float *>(normals, out, x, y); }

// The shadowed lighters (0x2dd1b0 and its kin): the global up normal, a grid cell and an alpha byte.
constexpr uint32_t kUpNormal = 0xffd918;
const float &GridAt(const float *grid, uint32_t cell)
{
    return *reinterpret_cast<const float *>(reinterpret_cast<uintptr_t>(grid) + cell * 4u);
}
// a + 49152 rebased to an int: round(a * 256), negatives 0 and anything above 255 held at 255.
uint32_t AlphaByte(float a);

// The game's zero vector (0x2d7ad0 compares the sky colour with it) and the original's 32-ulp equality: both
// floats' bits made ordered integers, their difference in [-32, 31].
constexpr uint32_t kZeroVector = 0x10d9424;
bool NearlyEqual(float a, float b)
{
    const uint32_t ia = BitsOf(a), ib = BitsOf(b), sa = ia >> 31, sb = ib >> 31;
    const uint32_t oa = ((0u - sa) >> 1) ^ ia, ob = ((0u - sb) >> 1) ^ ib;
    const int32_t diff = static_cast<int32_t>(oa - ob - sb + sa);
    return (static_cast<uint32_t>((diff >> 26) ^ diff) & 0xffffffe0u) == 0;
}

uint32_t AlphaByte(float a)
{
    const uint32_t bits = BitsOf(AddS(a, 49152.0f)) + 0xb8c00000u;
    const uint32_t low = static_cast<int32_t>(bits) < 0 ? 0u : bits;
    const uint32_t high = static_cast<int32_t>(0xffu - bits) < 0 ? 0xffu : 0u;
    return (low | high) & 0xffu;
}

// --- lightmap walls and rooms (LG2, slots 39 to 44) ---

// A lightmap tile record (slots 39, 41, 43, 44): {x, y, level, face kind byte}.
struct Tile {
    int32_t x, y, level;
    uint8_t kind, pad[3];
};
static_assert(sizeof(Tile) == 16, "Tile layout");

// A wall segment {x0, y0, level, x1, y1, level} (the levels int32), as mWorld's slot 56 and mLitObject's slots 17 and
// 19 give them and the segment sets hold them.
struct Segment {
    float v[6];
};
static_assert(sizeof(Segment) == 24, "Segment layout");

struct SegmentOrder {
    bool operator()(const Segment &a, const Segment &b) const { return nTSSG::cLightingManager::SegmentLess(a.v, b.v); }
};

// Slots 40 and 43's sets: their nodes (0x28 bytes) come from the allocator service (0x2e5870, slot 3), the head is
// freed through 0x197100.
using SegmentSet = std::set<Segment, SegmentOrder, GZServiceAllocator<Segment>>;

// The tile lists mWorld's slot 76 fills: storage from the allocator service, freed by 0x195720 when the list goes.
struct TileList : ServiceBlockVector {
    TileList() { mFirst = mLast = mEnd = nullptr; }
    ~TileList() { FreeBlock(); }
    TileList(const TileList &) = delete;
    TileList &operator=(const TileList &) = delete;
};

// The global switch slot 39 reads per shaded tile: shadow masks (LightQuadShadowMask) when set, else sun shading.
constexpr uint32_t kShadowMaskSwitch = 0xffdeac;

// 0x7f2340 (__cdecl (wall, 0); not ours, nor a leaf): the room behind a wall side, in ax.
uint16_t RoomOfWall(const float *wall)
{
    return OriginalFunction<uint16_t(__cdecl *)(const float *, int32_t), 0x7f2340>()(wall, 0);
}

// A room's weight: the original indexes the weights with the sign-extended 16-bit room, unchecked.
float WeightAt(const std::vector<float> &weights, int16_t room)
{
    const uint32_t offset = static_cast<uint32_t>(int32_t{room}) * 4u;
    return *reinterpret_cast<const float *>(reinterpret_cast<uintptr_t>(weights.data()) + offset);
}

// The far side of a wall: its two ends swapped.
void SwapEnds(Segment &w)
{
    const Segment far = {{w.v[3], w.v[4], w.v[5], w.v[0], w.v[1], w.v[2]}};
    w = far;
}

} // namespace

namespace nTSSG {

bool cLightingManager::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == 1 || iid == kIidLightingManager) {
        *out = this;
    } else if (iid == kIidMessageTarget2) {
        *out = &mVTable4;
    } else {
        return false;
    }
    Call<1>(this);
    return true;
}

uint32_t cLightingManager::Release()
{
    if (--mRefCount) return mRefCount;
    Call<38>(this, 1u);
    return 0;
}

void cLightingManager::SetFlag44(uint8_t value)
{
    mFlag44 = value;
    mUseDefaultName = 1;
}

void cLightingManager::Refresh()
{
    RebuildAll(0);
    if (*GameAddress<uint8_t>(kRefreshSwitch)) EvaluateLerpLights(1.0f);
    mFlag81 = 1;
}

void cLightingManager::AddSample(const float *values, float weight)
{
    cLightSample sample;
    for (int i = 0; i < 4; ++i) reinterpret_cast<uint32_t *>(sample.mValues)[i] = reinterpret_cast<const uint32_t *>(values)[i];
    sample.mWeight = weight;
    mSamples.push_back(sample);
}

const char *cLightingManager::Name()
{
    if (mUseDefaultName) return GameAddress<const char>(kDefaultName);
    return mName.c_str();
}

uint32_t cLightingManager::Value404() { return *reinterpret_cast<const uint32_t *>(mRef2C + 0x404); }

const cLightSet &cLightingManager::CurrentSet() const
{
    return mCurrentSet < uint32_t(mLightSets.end() - mLightSets.begin()) ? mLightSets.begin()[mCurrentSet] : mDefaultSet;
}

cLightVector cLightingManager::Vector18C() { return CurrentSet().mVector18C; }

cLightVector cLightingManager::Vector19C() { return CurrentSet().mVector19C; }

cLightVector cLightingManager::Vector174() { return CurrentSet().mVector174; }

cLightVector cLightingManager::NegatedDirection()
{
    const cLightVector &d = CurrentSet().mLights[0].mDirection;
    return cLightVector{-d.x, -d.y, -d.z};
}

cLightVector cLightingManager::Vector2C4() { return mVector2C4; }

float cLightingManager::Luminance(uint32_t key)
{
    cLightVector colour;
    Call<16>(mRef24, &colour, key);
    return colour.x * Factor(kLuminanceX) + colour.y * Factor(kLuminanceY) + colour.z * Factor(kLuminanceZ);
}

cLightingManager *cLightingManager::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cLightingManager));
    return this;
}

// The lifecycle's small slots (lane 261108h).

namespace {

constexpr uint32_t kObjectLighting = 0xdb60bc;    // "ObjectLighting"
constexpr uint32_t kSimAnticsStrength = 0xdb61f4; // "SimAnticsStrength"
constexpr uint32_t kStrength = 0xdb61e8;          // "Strength"

// An object's "ObjectLighting" extension (0xaeb5b0, nGZSceneGraph::FindDataList), or null.
void *ObjectLighting(void *object)
{
    return nGZSceneGraph::FindDataList(static_cast<nGZSceneGraph::cICompositionTreeNode *>(object),
                                       GameAddress<const char>(kObjectLighting), nullptr);
}
void SetValue(void *extension, uint32_t name, float value)
{
    Call<3, void>(extension, GameAddress<const char>(name), value);
}
bool GetValue(void *extension, uint32_t name, float *value)
{
    return Call<15, bool>(extension, GameAddress<const char>(name), value);
}

} // namespace

void cLightingManager::ResetObjectLighting(void *object)
{
    uint8_t *extension = static_cast<uint8_t *>(ObjectLighting(object));
    if (!extension) return;
    Call<14, void>(this, object, 0u, 0.0f, 0u);
    // The extension's virtual base, through the vbptr at +4 (its second entry).
    const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(extension + 4);
    void *base = extension + 4 + vbtable[1];
    void *handler = Call<18, void *>(base);
    Call<23, void>(handler, base);
}

void cLightingManager::SetObjectStrength(void *object, uint32_t, float strength, uint32_t)
{
    if (!mWorld) return;
    void *extension = ObjectLighting(object);
    if (!extension) return;
    const bool keep = mFlag4D && (CurrentSet().mEnabled >> 12 & 1);
    SetValue(extension, kSimAnticsStrength, strength);
    if (!keep) strength = 1.0f;
    float current;
    // fabs(strength - current) as a double; an unordered difference counts as changed.
    if (GetValue(extension, kStrength, &current) && 0.001 > static_cast<double>(Abs(SubS(strength, current)))) return;
    SetValue(extension, kStrength, strength);
    Call<13, void>(this, object, 0u);
    mDirty = 1;
    const float raised = MulS(mField508, 1000.0f);
    mField500 = raised > mField500 ? raised : mField500;
}

void cLightingManager::UpdateObjectStrength(void *object, void *source, float strength, uint32_t)
{
    if (!mWorld) return;
    void *extension = ObjectLighting(object);
    if (!extension) return;
    const bool keep = mFlag4D && (CurrentSet().mEnabled >> 12 & 1);
    SetValue(extension, kSimAnticsStrength, strength);
    SetValue(extension, kStrength, keep ? strength : 1.0f);
    const uint32_t value = Call<5>(source);
    Call<13, void>(this, object, value);
    if (!Call<134, bool>(object)) return;
    mDirty = 1;
    const float raised = MulS(mField508, 1000.0f);
    mField500 = raised > mField500 ? raised : mField500;
}

bool cLightingManager::AmbientImageAt(const float *xy, float *out)
{
    const void *image = mAmbientImage;
    if (!image) return false;
    const int32_t x = FloorToInt(MulS(mTerrainScale, xy[0]));
    const int32_t y = FloorToInt(MulS(mTerrainScale, xy[1]));
    const uint32_t column = static_cast<uint32_t>(x < 0 ? 0 : x), row = static_cast<uint32_t>(y < 0 ? 0 : y);
    const uint32_t width = static_cast<uint32_t>(mTerrainWidth), height = static_cast<uint32_t>(mTerrainHeight);
    float buffer[3];
    const float *texel = MapTexel(image, buffer, column < width ? column : width - 1, row < height ? row : height - 1);
    std::memcpy(out, texel, 12);
    return true;
}

void cLightingManager::SelectLightSet(const char *name)
{
    if (std::strcmp(name, GameAddress<const char>(kDefaultName)) == 0) {
        mUseDefaultName = 1;
        SelectTimeOfDayLightSet();
        return;
    }
    mUseDefaultName = 0;
    if (!SelectHoodLightSet(name)) return;
    ApplyLightSet();
    mDirty = 1;
    mFlag83 = 1;
}

uint32_t cLightingManager::LotValue(uint32_t id) { return Call<55>(Call<9, void *>(GetCachedSlot23()), id); }

uint32_t cLightingManager::SetLotValue(uint32_t id, uint32_t value)
{
    return Call<56>(Call<10, void *>(GetCachedSlot23()), id, value);
}

// Light evaluation (lane 261108a).

void cLightingManager::EvaluateAmbient(const float *normal, float *out)
{
    const uint32_t current = mCurrentSet;
    const void *map = current < uint32_t(mAmbientMaps.end() - mAmbientMaps.begin()) ? mAmbientMaps.begin()[current] : nullptr;
    if (map) {
        const uint32_t width = MapWidth(map);
        const uint32_t height = MapHeight(map);
        const float u = static_cast<float>((static_cast<double>(normal[0]) + 1.0) * 0.5 * static_cast<double>(width));
        uint32_t x = static_cast<uint32_t>(FloorToInt(u));
        const float v = static_cast<float>((static_cast<double>(normal[1]) + 1.0) * 0.5 * static_cast<double>(height));
        uint32_t y = static_cast<uint32_t>(FloorToInt(v));
        if (x >= width) x = width - 1;
        if (y >= height) y = height - 1;
        const cLightSet &set = CurrentSet();
        float buffer[3];
        const float *texel = MapTexel(map, buffer, x, y);
        const float r = MulS(set.mTint.x, texel[0]), g = MulS(texel[1], set.mTint.y), b = MulS(texel[2], set.mTint.z);
        out[0] = r;
        out[1] = g;
        out[2] = b;
        return;
    }
    const cLightSet &set = CurrentSet();
    float weight[4];
    for (int i = 0; i < 4; ++i) {
        const float d = static_cast<float>(nGZCompGeom::Dot87(normal, &set.mLights[i].mDirection.x));
        weight[i] = MulS(0.0f > d ? 0.0f : d, set.mLights[i].mIntensity); // maxss (0, d): a NaN or -0 d stays
    }
    const float rest = SubS(1.0f, AddS(AddS(AddS(weight[1], weight[0]), weight[2]), weight[3]));
    const float *ambient = &set.mAmbient.x;
    float colour[3];
    for (int c = 0; c < 3; ++c) {
        float sum = MulS(ambient[c], rest);
        for (int i = 0; i < 4; ++i) sum = AddS(sum, MulS((&set.mLights[i].mColour.x)[c], weight[i]));
        colour[c] = sum;
    }
    out[0] = colour[0];
    out[1] = colour[1];
    out[2] = colour[2];
}

void cLightingManager::EvaluateAmbientShaded(const float *normal, float shade, float *out)
{
    const uint32_t current = mCurrentSet;
    const cLightSet &set = CurrentSet();
    const void *map = current < uint32_t(mAmbientMaps.end() - mAmbientMaps.begin()) ? mAmbientMaps.begin()[current] : nullptr;
    if (map) {
        float bent[3];
        mSunHeights.Blend(bent, normal, &set.mLights[1].mDirection.x, shade);
        const uint32_t width = MapWidth(map);
        const uint32_t height = MapHeight(map);
        const float u = static_cast<float>((static_cast<double>(bent[0]) + 1.0) * 0.5 * static_cast<double>(width));
        uint32_t x = static_cast<uint32_t>(FloorToInt(u));
        const float v = static_cast<float>((static_cast<double>(bent[1]) + 1.0) * 0.5 * static_cast<double>(height));
        uint32_t y = static_cast<uint32_t>(FloorToInt(v));
        if (x >= width) x = width - 1;
        if (y >= height) y = height - 1;
        const cLightSet &tinted = CurrentSet();
        float buffer[3];
        const float *texel = MapTexel(map, buffer, x, y);
        const float r = MulS(texel[0], tinted.mTint.x), g = MulS(texel[1], tinted.mTint.y), b = MulS(texel[2], tinted.mTint.z);
        out[0] = r;
        out[1] = g;
        out[2] = b;
        return;
    }
    float weight[4];
    for (int i = 0; i < 4; ++i) {
        const float d = static_cast<float>(nGZCompGeom::Dot87(normal, &set.mLights[i].mDirection.x));
        weight[i] = MulS(0.0f > d ? 0.0f : d, set.mLights[i].mIntensity); // maxss (0, d): a NaN or -0 d stays
    }
    const float lit = SubS(1.0f, MulS(shade, mSunFalloff));
    weight[0] = MulS(weight[0], lit > 0.0f ? lit : 0.0f); // maxss (lit, 0): a NaN or -0 gives +0
    const float rest = SubS(1.0f, AddS(AddS(AddS(weight[1], weight[0]), weight[2]), weight[3]));
    const float *ambient = &set.mAmbient.x;
    float colour[3];
    for (int c = 0; c < 3; ++c) {
        float sum = MulS(ambient[c], rest);
        for (int i = 0; i < 4; ++i) sum = AddS(sum, MulS((&set.mLights[i].mColour.x)[c], weight[i]));
        colour[c] = sum;
    }
    out[0] = colour[0];
    out[1] = colour[1];
    out[2] = colour[2];
}

void cLightingManager::EvaluateAmbientScaled(const float *normal, float *out)
{
    EvaluateAmbient(normal, out);
    Scale(out, mAmbientScale1EC);
}

void cLightingManager::AmbientForNormal(uint32_t, const float *normal, float *out)
{
    EvaluateAmbient(normal, out);
    Scale(out, mAmbientScale1F0);
}

void cLightingManager::AmbientAt(float x, float y, float *out)
{
    float normal[3];
    TerrainNormal(normal, x, y);
    EvaluateAmbient(normal, out);
    Scale(out, mAmbientScale1F0);
}

double cLightingManager::TerrainHeight(int32_t i, int32_t j)
{
    const int32_t vertex[3] = {i, j, 0};
    return Call<62, double>(mTerrain, vertex);
}

float *cLightingManager::TerrainNormal(float *out, float x, float y)
{
    const float sx = MulS(mTerrainScale, x), sy = MulS(mTerrainScale, y);
    const int32_t ix = FloorToInt(sx), iy = FloorToInt(sy);
    // ORIGINAL-BUG: the cell index is converted back as unsigned (2^32 added when negative), so for negative coordinates
    // the fractions are off by 2^32 instead of lying in [0, 1).
    const float fx = SubS(sx, static_cast<float>(UnsignedToDouble(ix)));
    const float fy = SubS(sy, static_cast<float>(UnsignedToDouble(iy)));
    const int32_t width = mTerrainWidth, height = mTerrainHeight;
    const int32_t x0 = Plus(ix, -1) > 0 ? Plus(ix, -1) : 0;
    const int32_t x2 = Plus(ix, 1) < width ? Plus(ix, 1) : width;
    const int32_t x3 = Plus(ix, 2) < width ? Plus(ix, 2) : width;
    const int32_t y0 = Plus(iy, -1) > 0 ? Plus(iy, -1) : 0;
    const int32_t y2 = Plus(iy, 1) < height ? Plus(iy, 1) : height;
    const int32_t y3 = Plus(iy, 2) < height ? Plus(iy, 2) : height;
    auto h = [this](int32_t i, int32_t j) { return static_cast<float>(TerrainHeight(i, j)); };
    // Two differences are taken on the x87 straight from the returned height: one subtraction at the thread's precision.
    auto below = [this](float minuend, int32_t i, int32_t j) {
        return static_cast<float>(SubFromMem87(minuend, TerrainHeight(i, j)));
    };
    // The heights in the original's call order; dy* are y differences along column ix (A) and ix + 1 (B), dx* x
    // differences along row iy and iy + 1.
    const float a0 = h(ix, y0), a1 = h(ix, iy), a2 = h(ix, y2);
    const float dyA0 = SubS(a2, a0);
    const float a3 = h(ix, y3);
    const float dyA = AddS(MulS(SubS(SubS(a3, a1), dyA0), fy), dyA0);
    const float b0 = h(x2, y0), b1 = h(x2, iy), b2 = h(x2, y2);
    const float dyB0 = SubS(b2, b0);
    const float b3 = h(x2, y3);
    const float dx0 = below(b1, x0, iy);
    const float c1 = h(x3, iy);
    const float dxA = AddS(MulS(SubS(SubS(c1, a1), dx0), fx), dx0);
    const float dx1 = below(b2, x0, y2);
    const float c2 = h(x3, y2);
    const float half = static_cast<float>(static_cast<double>(mTerrainScale) * 0.5);
    const float dyB = AddS(MulS(SubS(SubS(b3, b1), dyB0), fy), dyB0);
    const float gy = MulS(Negated(AddS(MulS(SubS(dyB, dyA), fx), dyA)), half);
    const float dxB = AddS(MulS(SubS(SubS(c2, a2), dx1), fx), dx1);
    const float gx = MulS(Negated(AddS(MulS(SubS(dxB, dxA), fy), dxA)), half);
    const float inverse = DivS(1.0f, Sqrt(AddS(AddS(MulS(gy, gy), MulS(gx, gx)), 1.0f)));
    out[0] = MulS(inverse, gx);
    out[1] = MulS(inverse, gy);
    out[2] = inverse;
    return out;
}

double cSunHeightMap::HeightAbove(const float *p) const
{
    const int32_t x = FloorToInt(MulS(AddS(p[0], 0.001f), mScale));
    const int32_t y = FloorToInt(MulS(AddS(p[1], 0.001f), mScale));
    const int32_t cell = static_cast<int32_t>(static_cast<uint32_t>(mWidth) * static_cast<uint32_t>(y) + static_cast<uint32_t>(x));
    return SubMem87(static_cast<double>(mHeights.data()[cell]), p[2]); // fld the cell, fsub the height
}

float *cSunHeightMap::Blend(float *out, const float *a, const float *b, float t) const
{
    const float step = MulS(t, mBlendScale);
    const float bx = b[0], by = b[1], bz = b[2];
    const float ax = a[0], ay = a[1], az = a[2];
    out[0] = AddS(MulS(bx, step), ax);
    out[1] = AddS(MulS(by, step), ay);
    out[2] = AddS(MulS(bz, step), az);
    const float x = out[0], y = out[1], z = out[2];
    const float lengthSquared = AddS(AddS(MulS(x, x), MulS(y, y)), MulS(z, z));
    if (lengthSquared > 0.0f) {
        const float length = Sqrt(lengthSquared);
        out[0] = DivS(x, length);
        out[1] = DivS(y, length);
        out[2] = DivS(z, length);
    }
    return out;
}

float cLightingManager::SunVisibility(const float *p)
{
    if (!mSunShadows) return 1.0f;
    const float scale = mSunHeights.mScale;
    const float x = MulS(scale, p[0]);
    if (!(x >= 0.0f)) return 1.0f;
    const float y = MulS(scale, p[1]);
    if (!(y >= 0.0f)) return 1.0f;
    if (!(static_cast<float>(Plus(mSunHeights.mWidth, -1)) >= x)) return 1.0f;
    if (!(static_cast<float>(Plus(mSunHeights.mHeight, -1)) >= y)) return 1.0f;
    const float depth = static_cast<float>(mSunHeights.HeightAbove(p));
    if (0.0f >= depth) return 1.0f; // a NaN depth goes on (and gives 0)
    const float visible = SubS(1.0f, MulS(depth, mSunFalloff));
    return visible > 0.0f ? visible : 0.0f; // maxss (visible, 0): NaN and -0 give 0
}

float cLightingManager::SampleLuminance(int16_t index)
{
    if (index == 0) {
        const cSetLight &light = CurrentSet().mLights[0];
        const float intensity = light.mIntensity;
        const float r = MulS(MulS(MulS(light.mColour.x, intensity), 0.25f), Factor(kLuminanceX));
        const float g = MulS(MulS(MulS(light.mColour.y, intensity), 0.25f), Factor(kLuminanceY));
        const float b = MulS(MulS(MulS(light.mColour.z, intensity), 0.25f), Factor(kLuminanceZ));
        return AddS(AddS(g, r), b);
    }
    if (index < 0) return 0.0f;
    const uint32_t count = SkySampleCount();
    if (uint32_t(int32_t(index)) >= count) return 0.0f;
    float average[3];
    AverageOfSix(average, SkySample(uint32_t(int32_t(index))).mVectors[0]);
    return AddS(AddS(MulS(average[0], Factor(kLuminanceX)), MulS(average[1], Factor(kLuminanceY))),
                MulS(average[2], Factor(kLuminanceZ)));
}

int32_t cLightingManager::FindLightSet(const cIGZString &name)
{
    for (uint32_t i = 0; i < uint32_t(mLightSets.end() - mLightSets.begin()); ++i) {
        const auto *setName = reinterpret_cast<const cIGZString *>(&mLightSets.begin()[i]);
        if (setName->IsEqual(name, false)) return int32_t(i);
    }
    return -1;
}

float *cLightingManager::OrderSegment(float *out, const float *in)
{
    auto *o = reinterpret_cast<uint32_t *>(out);
    const auto *s = reinterpret_cast<const uint32_t *>(in);
    for (int i = 0; i < 6; ++i) o[i] = s[i];
    bool inOrder;
    if (s[2] != s[5]) {
        inOrder = int32_t(s[2]) < int32_t(s[5]);
    } else if (in[0] == in[3]) {
        inOrder = in[4] > in[1];
    } else {
        inOrder = in[3] > in[0];
    }
    if (!inOrder) {
        const uint32_t x1 = s[3], y1 = s[4];
        o[0] = x1;
        o[1] = y1;
        o[2] = s[5];
        const uint32_t x0 = s[0], y0 = s[1];
        o[3] = x0;
        o[4] = y0;
        o[5] = s[2];
    }
    return out;
}

float *cLightingManager::EdgeNormal(float *out, const float *in)
{
    const float nx = SubS(in[4], in[1]);
    out[0] = nx;
    const float ny = Negated(SubS(in[3], in[0]));
    out[1] = ny;
    const float length = Sqrt(AddS(MulS(ny, ny), MulS(nx, nx)));
    if (length > 0.0f) {
        out[0] = DivS(nx, length);
        out[1] = DivS(ny, length);
    }
    return out;
}

void cLightingManager::FaceKind(char kind, float *sign, float *offset, bool *flag)
{
    switch (kind) {
    case 3: *sign = -1.0f, *offset = 0.0f, *flag = true; break;
    case 6: *sign = 1.0f, *offset = 1.0f, *flag = true; break;
    case 12: *sign = -1.0f, *offset = 0.0f, *flag = false; break;
    default: *sign = 1.0f, *offset = 1.0f, *flag = false;
    }
    *offset = SubS(*offset, 0.01f);
}

float *cLightingManager::SumOfSix(float *out, const float *vectors)
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    for (int i = 0; i < 6; ++i) {
        x = AddS(x, vectors[3 * i]);
        y = AddS(y, vectors[3 * i + 1]);
        z = AddS(z, vectors[3 * i + 2]);
        out[0] = x;
        out[1] = y;
        out[2] = z;
    }
    return out;
}

float *cLightingManager::AverageOfSix(float *out, const float *vectors)
{
    float sum[3];
    SumOfSix(sum, vectors);
    out[0] = sum[0];
    out[1] = sum[1];
    out[2] = sum[2];
    out[0] = DivS(out[0], 6.0f);
    out[1] = DivS(out[1], 6.0f);
    out[2] = DivS(out[2], 6.0f);
    return out;
}

// --- vertex lighting (lane 261108b, LG2) ---------------------------------------------------------------------------

void cLightingManager::AddFlatQuad(const float *colour, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                                   uint32_t *pixels, int32_t stride)
{
    for (uint32_t y = y0; y < y1; ++y)
        for (uint32_t x = x0; x < x1; ++x) AddColour(Texel(pixels, stride, x, y), colour);
}

void cLightingManager::AddFlatTriangle(char kind, const float *colour, uint32_t x0, uint32_t y0, uint32_t x1,
                                       uint32_t y1, uint32_t *pixels, int32_t stride)
{
    float sign, offset;
    bool flag;
    FaceKind(kind, &sign, &offset, &flag);
    const float du = TileStep(1.0, x1 - x0), dv = TileStep(-1.0, y1 - y0);
    float v = AddS(dv, 1.0f);
    for (uint32_t y = y0; y < y1; ++y, v = AddS(v, dv)) {
        float u = 0.0f;
        for (uint32_t x = x0; x < x1; ++x, u = AddS(u, du))
            if ((offset > AddS(MulS(u, sign), v)) != flag) AddColour(Texel(pixels, stride, x, y), colour);
    }
}

void cLightingManager::LightQuad(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1,
                                 uint32_t y1, uint32_t *pixels, int32_t stride, float scale)
{
    LightTile(normals, tile, x0, y0, x1, y1, pixels, stride, scale, false);
}

void cLightingManager::LightTriangle(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1,
                                     uint32_t y1, uint32_t *pixels, int32_t stride, float scale)
{
    LightTile(normals, tile, x0, y0, x1, y1, pixels, stride, scale, true);
}

void cLightingManager::LightTile(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1,
                                 uint32_t y1, uint32_t *pixels, int32_t stride, float scale, bool half)
{
    float sign = 0.0f, offset = 0.0f;
    bool flag = false;
    if (half) FaceKind(static_cast<char>(tile[3]), &sign, &offset, &flag);
    const int32_t x = tile[0], y = tile[1];
    float n00[3], n10[3], n01[3], n11[3];
    VertexNormal(normals, n00, x, y);
    VertexNormal(normals, n10, Plus(x, 1), y);
    VertexNormal(normals, n01, x, Plus(y, 1));
    VertexNormal(normals, n11, Plus(x, 1), Plus(y, 1));
    float c00[3] = {}, c10[3] = {}, c01[3] = {}, c11[3] = {};
    EvaluateAmbient(n00, c00);
    EvaluateAmbient(n10, c10);
    EvaluateAmbient(n01, c01);
    EvaluateAmbient(n11, c11);
    scale = MulS(scale, mAmbientScale1F0);
    const float du = TileStep(1.0, x1 - x0), dv = TileStep(-1.0, y1 - y0);
    float v = AddS(dv, 1.0f);
    for (uint32_t row = y0; row < y1; ++row, v = AddS(v, dv)) {
        const double v64 = v, above = 1.0 - v64;
        float u = 0.0f;
        for (uint32_t col = x0; col < x1; ++col, u = AddS(u, du)) {
            if (half && (offset > AddS(MulS(u, sign), v)) == flag) continue;
            const double u64 = u, right = 1.0 - u64;
            const float w10 = static_cast<float>(u64 * above), w00 = static_cast<float>(above * right);
            const float w01 = static_cast<float>(v64 * right), w11 = MulS(u, v);
            uint32_t bytes[3];
            for (int k = 0; k < 3; ++k) {
                const float sum = AddS(AddS(AddS(MulS(c10[k], w10), MulS(c00[k], w00)), MulS(c01[k], w01)), MulS(c11[k], w11));
                bytes[k] = TexelByte(MulS(sum, scale));
            }
            uint32_t *texel = Texel(pixels, stride, col, row);
            *texel = AddBytes(*texel, bytes[0], bytes[1], bytes[2]);
        }
    }
}

void cLightingManager::LightQuadShadowMask(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0,
                                           uint32_t x1, uint32_t y1, uint32_t *pixels, int32_t stride, float scale)
{
    LightTileShadowed(normals, tile, x0, y0, x1, y1, pixels, stride, scale, false, false);
}

void cLightingManager::LightTriangleShadowMask(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0,
                                               uint32_t x1, uint32_t y1, uint32_t *pixels, int32_t stride, float scale)
{
    LightTileShadowed(normals, tile, x0, y0, x1, y1, pixels, stride, scale, true, false);
}

void cLightingManager::LightQuadSunShaded(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0,
                                          uint32_t x1, uint32_t y1, uint32_t *pixels, int32_t stride, float scale)
{
    LightTileShadowed(normals, tile, x0, y0, x1, y1, pixels, stride, scale, false, true);
}

void cLightingManager::LightTriangleSunShaded(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0,
                                              uint32_t x1, uint32_t y1, uint32_t *pixels, int32_t stride, float scale)
{
    LightTileShadowed(normals, tile, x0, y0, x1, y1, pixels, stride, scale, true, true);
}

void cLightingManager::LightTileShadowed(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0,
                                         uint32_t x1, uint32_t y1, uint32_t *pixels, int32_t stride, float scale,
                                         bool half, bool shaded)
{
    const uint8_t kind = reinterpret_cast<const uint8_t *>(tile)[12];
    const bool perVertex = tile[2] == 0 && Call<61, bool>(mTerrain, tile, static_cast<uint32_t>(kind));
    float sign = 0.0f, offset = 0.0f;
    bool flag = false;
    if (half) FaceKind(static_cast<char>(kind), &sign, &offset, &flag);
    scale = MulS(scale, mAmbientScale1F0);
    const int32_t x = tile[0], y = tile[1];
    const uint32_t colOffset = static_cast<uint32_t>(x) * 8u - x0;
    const uint32_t rowBase = static_cast<uint32_t>(y) * 8u - 1u + y1;
    const uint32_t width = static_cast<uint32_t>(mSunHeights.mWidth);
    const float falloff = mSunFalloff;
    const float du = TileStep(1.0, x1 - x0), dv = TileStep(-1.0, y1 - y0);
    // The new alpha where the depth is above 0, a byte.
    auto alpha = [&](float depth) {
        if (shaded) {
            const float a = Sqrt(SubS(1.0f, MulS(depth, falloff)));
            return AlphaByte(a > 0.0f ? a : 0.0f); // maxss a, 0
        }
        const float a = MulS(depth, falloff);
        return AlphaByte(a < 1.0f ? a : 1.0f); // minss a, 1
    };
    // Sets the texel's alpha and leaves its colour (unscaled) in c: EvaluateAmbient's for `normal`, or `flat` when
    // given.
    auto shade = [&](uint32_t *texel, float depth, const float *normal, float *c, const float *flat) {
        if (shaded && depth > 0.0f) {
            *texel = alpha(depth) << 24 | (*texel & 0xffffffu);
            EvaluateAmbientShaded(normal, depth, c);
            return;
        }
        if (!shaded) {
            reinterpret_cast<uint8_t *>(texel)[3] = 0;
            if (depth > 0.0f) *texel |= alpha(depth) << 24;
        }
        if (flat) {
            std::memcpy(c, flat, sizeof(float) * 3);
        } else {
            EvaluateAmbient(normal, c);
        }
    };
    auto add = [&](uint32_t *texel, float *c) {
        uint32_t bytes[3];
        for (int k = 0; k < 3; ++k) {
            c[k] = MulS(c[k], scale);
            bytes[k] = TexelByte(c[k]);
        }
        *texel = AddBytes(*texel, bytes[0], bytes[1], bytes[2]);
    };
    float c[3] = {};
    if (perVertex) {
        float n00[3], n10[3], n01[3], n11[3];
        VertexNormal(normals, n00, x, y);
        VertexNormal(normals, n10, Plus(x, 1), y);
        VertexNormal(normals, n01, x, Plus(y, 1));
        VertexNormal(normals, n11, Plus(x, 1), Plus(y, 1));
        float v = AddS(dv, 1.0f);
        for (uint32_t row = y0; row < y1; ++row, v = AddS(v, dv)) {
            const double v64 = v, above = 1.0 - v64;
            float u = 0.0f;
            for (uint32_t col = x0; col < x1; ++col, u = AddS(u, du)) {
                if (half && (offset > AddS(MulS(u, sign), v)) == flag) continue;
                const double u64 = u, right = 1.0 - u64;
                const float w10 = static_cast<float>(u64 * above), w00 = static_cast<float>(above * right);
                const float w01 = static_cast<float>(v64 * right), w11 = MulS(u, v);
                // The triangles add the x components' first two terms the other way round.
                const float x01 = half ? AddS(MulS(n10[0], w10), MulS(n00[0], w00))
                                       : AddS(MulS(n00[0], w00), MulS(n10[0], w10));
                const float nx = AddS(AddS(x01, MulS(n01[0], w01)), MulS(n11[0], w11));
                const float ny =
                    AddS(AddS(AddS(MulS(n00[1], w00), MulS(n10[1], w10)), MulS(n01[1], w01)), MulS(n11[1], w11));
                const float nz =
                    AddS(AddS(AddS(MulS(n00[2], w00), MulS(n10[2], w10)), MulS(n01[2], w01)), MulS(n11[2], w11));
                // The shaded ones square y first.
                const float xx = MulS(nx, nx), yy = MulS(ny, ny);
                const float inv = DivS(1.0f, Sqrt(AddS(AddS(shaded ? yy : xx, shaded ? xx : yy), MulS(nz, nz))));
                float normal[3];
                if (!shaded) {
                    normal[0] = MulS(nx, inv), normal[1] = MulS(ny, inv), normal[2] = MulS(nz, inv);
                } else if (!half) {
                    normal[0] = MulS(inv, nx), normal[1] = MulS(inv, ny), normal[2] = MulS(inv, nz);
                } else {
                    normal[0] = MulS(nx, inv), normal[1] = MulS(inv, ny), normal[2] = MulS(nz, inv);
                }
                const uint32_t cell = width * (rowBase - row) + col + colOffset;
                const float depth = SubS(GridAt(mSunHeights.mHeights.data(), cell), GridAt(mSunHeights.mGround.data(), cell));
                uint32_t *texel = Texel(pixels, stride, col, row);
                shade(texel, depth, normal, c, nullptr);
                add(texel, c);
            }
        }
        return;
    }
    const int32_t vertex[3] = {x, y, tile[2]};
    const float height = static_cast<float>(Call<62, double>(mTerrain, vertex));
    const float *up = GameAddress<const float>(kUpNormal);
    float flat[3] = {};
    EvaluateAmbient(up, flat);
    float v = AddS(dv, 1.0f);
    for (uint32_t row = y0; row < y1; ++row, v = AddS(v, dv)) {
        float u = 0.0f;
        for (uint32_t col = x0; col < x1; ++col, u = AddS(u, du)) {
            if (half && (offset > AddS(MulS(u, sign), v)) == flag) continue;
            const uint32_t cell = width * (rowBase - row) + col + colOffset;
            const float depth = SubS(GridAt(mSunHeights.mHeights.data(), cell), height);
            uint32_t *texel = Texel(pixels, stride, col, row);
            shade(texel, depth, up, c, flat);
            add(texel, c);
        }
    }
}

bool cLightingManager::AddSkyLight(const uint8_t *lights, int32_t count, std::vector<float> *weights)
{
    if (mSkySamples.empty() || !mFlag44) return false;
    const cLightSet &set = CurrentSet();
    const float colour[3] = {set.mSkyColour.x, set.mSkyColour.y, set.mSkyColour.z};
    const float reach = set.mSkyReach;
    const float *zero = GameAddress<const float>(kZeroVector);
    if (NearlyEqual(colour[0], zero[0]) && NearlyEqual(colour[1], zero[1]) && NearlyEqual(colour[2], zero[2])) return false;
    const int32_t samples = static_cast<int32_t>(SkySampleCount());
    std::vector<float> &w = *weights;
    w.resize(static_cast<uint32_t>(samples));
    if (reach > 0.0f) {
        for (uint32_t i = 0; i < w.size(); ++i) {
            w[i] = 0.0f;
            if (static_cast<uint16_t>(i) == 0) continue; // ORIGINAL-BUG: a 16-bit test, so sample 65536 is skipped too
            float average[3];
            const float *a = AverageOfSix(average, SkySample(i).mVectors[0]);
            const float ax = Abs(a[0]), ay = Abs(a[1]), az = Abs(a[2]);
            const float yz = az > ay ? az : ay;
            const float m = yz > ax ? yz : ax; // maxss
            if (reach > m) w[i] = SubS(1.0f, DivS(m, reach));
        }
    } else {
        for (float &f : w) f = 1.0f;
        for (int32_t k = 0; k < count; ++k) {
            const int32_t index = *reinterpret_cast<const int16_t *>(lights + static_cast<uint32_t>(k) * 0x6cu + 0x68u);
            if (index >= 0 && index < samples) w[static_cast<uint32_t>(index)] = 0.0f;
        }
    }
    int32_t lit = 0;
    for (uint32_t i = 0; i < w.size(); ++i) {
        if (!(w[i] > 0.0f) || static_cast<uint16_t>(i) == 0) continue;
        ++lit;
        float *sample = SkySample(i).mVectors[0];
        for (int j = 0; j < 15; ++j) sample[j] = AddS(MulS(colour[j % 3], w[i]), sample[j]);
    }
    return lit > 0;
}

void cLightingManager::SendSkyLight(const uint8_t *lights, int32_t count)
{
    std::vector<float> weights;
    if (!AddSkyLight(lights, count, &weights)) return;
    const cLightSet &set = CurrentSet();
    const float colour[3] = {set.mSkyColour.x, set.mSkyColour.y, set.mSkyColour.z};
    const uint32_t samples = static_cast<uint32_t>(weights.size());
    for (uint32_t i = 0; i < samples; ++i) {
        if (!(weights[i] > 0.0f)) continue;
        const uint32_t index = static_cast<uint16_t>(i);
        if (index == 0) continue; // the 16-bit test AddSkyLight makes
        const int16_t percent = static_cast<int16_t>(Call<36>(this, index));
        const float w = weights[i];
        const float k = MulS(static_cast<float>(percent), 0.01f);
        const float light[3] = {MulS(MulS(colour[0], w), k), MulS(MulS(colour[1], w), k), MulS(MulS(colour[2], w), k)};
        Call<22>(mLitObject, index, light);
    }
}

void cLightingManager::LoadTexture(uint32_t level, uint32_t index, uint32_t *&pixels, int32_t &stride)
{
    const void *image = Call<22, const void *>(mRef24, level, index);
    uint32_t *texels = Call<9, uint32_t *>(image, 0u);
    const uint32_t pitch = Call<28>(image, 0u) >> 2;
    const uint32_t height = Call<27>(image, 0u);
    pixels = texels + (height - 1) * pitch;
    stride = -static_cast<int32_t>(pitch);
}

void cLightingManager::LightTiles()
{
    std::vector<Tile> tiles;
    Call<16>(mLitObject, &tiles);
    const float scale = Call<23, float>(mRef24, 1u);
    float ambient[3] = {0.0f, 0.0f, 0.0f};
    EvaluateAmbient(GameAddress<const float>(kUpNormal), ambient);
    const float s = MulS(scale, mAmbientScale1F0);
    for (float &a : ambient) a = MulS(a, s);
    const void *service = OriginalFunction<void *(__cdecl *)(), 0x20defb>()();
    const void *normals = Call<39, const void *>(service);
    int32_t texture = -1;
    uint32_t *pixels = nullptr;
    int32_t stride = 0;
    for (const Tile &tile : tiles) {
        uint32_t x0, y0, x1, y1;
        int32_t index;
        if (!Call<19, bool>(mRef24, &tile, &x0, &y0, &x1, &y1, &index)) continue;
        if (index != texture) {
            texture = index;
            LoadTexture(1u, static_cast<uint32_t>(index), pixels, stride);
        }
        const int32_t *record = &tile.x;
        if (tile.kind == 15) {
            if (mShadeTiles) LightQuadSunShaded(normals, record, x0, y0, x1, y1, pixels, stride, scale);
            else if (tile.level == 0 && Call<61, bool>(mTerrain, record, uint32_t{15}))
                LightQuad(normals, record, x0, y0, x1, y1, pixels, stride, scale);
            else AddFlatQuad(ambient, x0, y0, x1, y1, pixels, stride);
        } else {
            if (mShadeTiles) LightTriangleSunShaded(normals, record, x0, y0, x1, y1, pixels, stride, scale);
            else if (tile.level == 0 && Call<61, bool>(mTerrain, record, static_cast<uint32_t>(tile.kind)))
                LightTriangle(normals, record, x0, y0, x1, y1, pixels, stride, scale);
            else AddFlatTriangle(static_cast<char>(tile.kind), ambient, x0, y0, x1, y1, pixels, stride);
        }
    }
}


bool __stdcall cLightingManager::SegmentLess(const float *a, const float *b)
{
    const auto level = [](const float *s, int i) {
        int32_t value;
        std::memcpy(&value, s + i, 4);
        return value;
    };
    if (level(a, 2) != level(b, 2)) return level(a, 2) < level(b, 2);
    if (NearlyEqual(a[0], b[0]) && NearlyEqual(a[1], b[1])) {
        if (level(a, 5) != level(b, 5)) return level(a, 5) < level(b, 5);
        return a[3] == b[3] ? a[4] < b[4] : a[3] < b[3];
    }
    return a[0] == b[0] ? a[1] < b[1] : a[0] < b[0];
}

void cLightingManager::LightWallShaded(const float *wall, const float *normal, uint32_t x0, uint32_t y0, uint32_t x1,
                                       uint32_t y1, uint32_t *pixels, int32_t stride, float scale)
{
    float base[3] = {0.0f, 0.0f, 0.0f};
    EvaluateAmbientScaled(normal, base);
    // The wall side's corners (WallCorners): p0, p1 along the bottom, p2, p3 above them.
    float q[12];
    WallCorners(wall, q);
    const uint32_t width = x1 - x0, height = y1 - y0;
    scale = MulS(scale, mAmbientScale1F0);
    float u0, du, v, dv;
    if (width == 4 && height == 8) {
        // The game's 4 x 8 wall texels: half-texel steps from outside the corners (0xdb6888, 0xdb6884, 0xdb6878).
        u0 = -0.25f;
        du = 0.5f;
        v = nGZCompGeom::convex::FloatOf(0xbdaaaaabu);
        dv = nGZCompGeom::convex::FloatOf(0x3e2aaaabu);
    } else {
        u0 = 0.0f;
        v = 0.0f;
        du = static_cast<float>(1.0 / static_cast<double>(static_cast<float>(static_cast<int32_t>(width - 1))));
        dv = static_cast<float>(1.0 / static_cast<double>(static_cast<float>(static_cast<int32_t>(height - 1))));
    }
    float colour[3] = {0.0f, 0.0f, 0.0f}; // kept from texel to texel
    if (y0 >= y1) return;
    const float nudge = Factor(0xda20dc); // 0.001
    uint32_t *row = Texel(pixels, stride, x0, y0);
    for (uint32_t r = height; r != 0; --r) {
        float u = u0;
        if (x0 < x1) {
            uint32_t *texel = row;
            for (uint32_t c = width; c != 0; --c) {
                const float nx8 = MulS(normal[0], 0.125f), ny8 = MulS(normal[1], 0.125f), nz8 = MulS(normal[2], 0.125f);
                const float dx01 = SubS(q[3], q[0]), dy01 = SubS(q[4], q[1]), dz01 = SubS(q[5], q[2]);
                const float dx23 = SubS(q[9], q[6]), dy23 = SubS(q[10], q[7]), dz23 = SubS(q[11], q[8]);
                const float ax = AddS(MulS(u, dx01), q[0]), ay = AddS(MulS(u, dy01), q[1]), az = AddS(MulS(u, dz01), q[2]);
                const float bz = AddS(MulS(dz23, u), q[8]), by = AddS(MulS(dy23, u), q[7]), bx = AddS(MulS(u, dx23), q[6]);
                const float z = AddS(AddS(MulS(SubS(bz, az), v), az), nz8);
                const float y = AddS(AddS(MulS(SubS(by, ay), v), ay), ny8);
                const float x = AddS(AddS(MulS(SubS(bx, ax), v), ax), nx8);
                const float cells = mSunHeights.mScale;
                const int32_t col = FloorToInt(MulS(AddS(x, nudge), cells));
                const int32_t line = FloorToInt(MulS(AddS(y, nudge), cells));
                const uint32_t cell = static_cast<uint32_t>(mSunHeights.mWidth) * static_cast<uint32_t>(line) + static_cast<uint32_t>(col);
                const float depth = SubS(GridAt(mSunHeights.mHeights.data(), cell), z);
                if (depth > 0.0f) EvaluateAmbientShaded(normal, depth, colour);
                else std::memcpy(colour, base, sizeof(colour));
                for (float &k : colour) k = MulS(k, scale);
                *texel = AddBytes(*texel, TexelByte(colour[0]), TexelByte(colour[1]), TexelByte(colour[2]));
                ++texel;
                u = AddS(u, du);
            }
        }
        row = reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(row) + static_cast<uint32_t>(stride) * 4u);
        v = AddS(v, dv);
    }
}

bool cLightingManager::TileShown(const int32_t *tile)
{
    const uint32_t kind = reinterpret_cast<const Tile *>(tile)->kind;
    return Call<53, bool>(mTerrain, tile, kind, 1u) || Call<55, bool>(mTerrain, tile, kind, 1u);
}

void cLightingManager::LightOutdoorTiles()
{
    if (!mWorld) return;
    TileList tiles;
    Call<76>(mWorld, 0u, &tiles, 0u);
    const float scale = Call<23, float>(mRef24, 1u);
    float ambient[3] = {0.0f, 0.0f, 0.0f};
    EvaluateAmbient(GameAddress<const float>(kUpNormal), ambient);
    const float s = MulS(scale, mAmbientScale1F0);
    ambient[0] = MulS(s, ambient[0]);
    ambient[1] = MulS(ambient[1], s);
    ambient[2] = MulS(ambient[2], s);
    const void *service = OriginalFunction<void *(__cdecl *)(), 0x20defb>()();
    const void *normals = Call<39, const void *>(service);
    int32_t texture = -1;
    uint32_t *pixels = nullptr;
    int32_t stride = 0;
    for (const Tile *tile = static_cast<const Tile *>(tiles.mFirst); tile != tiles.mLast; ++tile) {
        const int32_t *record = &tile->x;
        if (!TileShown(record)) continue;
        uint32_t x0, y0, x1, y1;
        int32_t index;
        if (!Call<19, bool>(mRef24, record, &x0, &y0, &x1, &y1, &index)) continue;
        if (index != texture) {
            texture = index;
            LoadTexture(1u, static_cast<uint32_t>(index), pixels, stride);
        }
        // The switch is read per shaded tile, as the original.
        const auto masks = [] { return *GameAddress<const uint8_t>(kShadowMaskSwitch) != 0; };
        if (tile->kind == 15) {
            if (mShadeTiles) {
                if (masks()) LightQuadShadowMask(normals, record, x0, y0, x1, y1, pixels, stride, scale);
                else LightQuadSunShaded(normals, record, x0, y0, x1, y1, pixels, stride, scale);
            } else if (tile->level == 0 && Call<61, bool>(mTerrain, record, uint32_t{15})) {
                LightQuad(normals, record, x0, y0, x1, y1, pixels, stride, scale);
            } else {
                AddFlatQuad(ambient, x0, y0, x1, y1, pixels, stride);
            }
        } else {
            if (mShadeTiles) {
                if (masks()) LightTriangleShadowMask(normals, record, x0, y0, x1, y1, pixels, stride, scale);
                else LightTriangleSunShaded(normals, record, x0, y0, x1, y1, pixels, stride, scale);
            } else if (tile->level == 0 && Call<61, bool>(mTerrain, record, static_cast<uint32_t>(tile->kind))) {
                LightTriangle(normals, record, x0, y0, x1, y1, pixels, stride, scale);
            } else {
                AddFlatTriangle(static_cast<char>(tile->kind), ambient, x0, y0, x1, y1, pixels, stride);
            }
        }
    }
    if (!mDrawSunHeights) return;
    // The debug view: level 1's texture 0 overwritten top row first (rows mSunHeights.mWidth texels) with each value / 3
    // as three ramps: red 1 - |v|, green 1 - |v - 1|, blue 1 - |v - 2|, each clamped to [0, 1] and quartered.
    const void *image = Call<22, const void *>(mRef24, 1u, 0u);
    const uintptr_t texels = reinterpret_cast<uintptr_t>(Call<9, void *>(image, 0u));
    const uint32_t pitch = Call<28>(image, 0u) >> 2;
    const uintptr_t bottom = texels + (Call<27>(image, 0u) - 1) * pitch * 4u;
    const uint32_t rows = Call<27>(image, 0u);
    Call<26>(image, 0u);
    uintptr_t line = bottom + (4u - rows * 4u) * pitch;
    const auto ramp = [](float t) {
        const float h = SubS(0.5f, MulS(AddS(Abs(t), t), 0.25f));
        return AlphaByte(static_cast<float>(static_cast<double>(SubS(1.0f, AddS(Abs(h), h))) * 0.25));
    };
    for (int32_t r = 0; r < mSunHeights.mHeight; ++r) {
        uint32_t *out = reinterpret_cast<uint32_t *>(line);
        for (int32_t c = 0; c < mSunHeights.mWidth; ++c) {
            const uint32_t cell = static_cast<uint32_t>(mSunHeights.mWidth) * static_cast<uint32_t>(r) + static_cast<uint32_t>(c);
            const float value = DivS(GridAt(mSunHeights.mValues.data(), cell), 3.0f);
            const uint32_t red = ramp(SubS(value, 0.0f)), green = ramp(SubS(value, 1.0f)), blue = ramp(SubS(value, 2.0f));
            out[c] = 0xff000000u | red << 16 | green << 8 | blue;
        }
        line += pitch * 4u;
    }
}

void cLightingManager::LightWallSide(float *wall, int32_t kind, const float *sun, float scale, int32_t &texture,
                                     uint32_t *&pixels, int32_t &stride)
{
    const uint16_t room = RoomOfWall(wall);
    if (room != 0 && kind != 3) return;
    float edge[2];
    EdgeNormal(edge, wall);
    const float normal[3] = {edge[0], edge[1], 0.0f};
    uint32_t x0, y0, x1, y1;
    int32_t index;
    if (!Call<21, bool>(mRef24, wall, &x0, &y0, &x1, &y1, &index)) return;
    if (index != texture) {
        texture = index;
        LoadTexture(0u, static_cast<uint32_t>(index), pixels, stride);
    }
    if (kind == 3 && room != 0) {
        // A kind 3 wall with a room behind it: only the rows past three times its width.
        const int32_t skip = static_cast<int32_t>((x1 - x0) * 3u);
        if (static_cast<int32_t>(y1 - y0) <= skip) return;
        y0 += static_cast<uint32_t>(skip);
    }
    if (mShadeTiles && mShadeWalls && static_cast<float>(nGZCompGeom::Dot87(normal, sun)) > 0.0f) {
        LightWallShaded(wall, normal, x0, y0, x1, y1, pixels, stride, scale);
        return;
    }
    float ambient[3] = {0.0f, 0.0f, 0.0f};
    EvaluateAmbientScaled(normal, ambient);
    const float colour[3] = {MulS(ambient[0], scale), MulS(ambient[1], scale), MulS(ambient[2], scale)};
    AddFlatQuad(colour, x0, y0, x1, y1, pixels, stride);
}

void cLightingManager::LightWalls()
{
    if (!mWorld) return;
    const float scale = Call<23, float>(mRef24, 0u);
    const cLightVector light = CurrentSet().mLights[0].mDirection;
    const float sun[3] = {light.x, light.y, light.z};
    int32_t texture = -1;
    uint32_t *pixels = nullptr;
    int32_t stride = 0;
    SegmentSet done;
    for (uint32_t i = 0; i < Call<55>(mWorld); ++i) {
        Segment wall;
        Call<56>(mWorld, &wall, i);
        const int32_t kind = Call<50, int32_t>(mWorld, &wall);
        Segment key;
        OrderSegment(key.v, wall.v);
        if (done.find(key) != done.end()) continue;
        done.insert(key);
        for (int side = 0; side < 2; ++side) {
            if (side == 1) SwapEnds(wall);
            LightWallSide(wall.v, kind, sun, scale, texture, pixels, stride);
        }
    }
}

void cLightingManager::LightListedWalls()
{
    const float scale = Call<23, float>(mRef24, 0u);
    const cLightVector light = CurrentSet().mLights[0].mDirection;
    const float sun[3] = {light.x, light.y, light.z};
    int32_t texture = -1;
    uint32_t *pixels = nullptr;
    int32_t stride = 0;
    std::vector<Segment> walls;
    Call<17>(mLitObject, &walls);
    const int32_t count = static_cast<int32_t>(walls.size());
    for (int32_t i = 0; i < count; ++i) {
        Segment wall = walls[static_cast<uint32_t>(i)];
        const int32_t kind = Call<50, int32_t>(mWorld, &wall);
        LightWallSide(wall.v, kind, sun, scale, texture, pixels, stride);
    }
}

void cLightingManager::LightRoomsBySky(const uint8_t *lights, int32_t count)
{
    if (!mWorld) return;
    std::vector<float> weights;
    if (!AddSkyLight(lights, count, &weights)) return;
    const cLightVector sky = CurrentSet().mSkyColour;
    const float tileScale = Call<23, float>(mRef24, 1u);
    const float tileColour[3] = {MulS(sky.x, tileScale), MulS(sky.y, tileScale), MulS(sky.z, tileScale)};
    TileList tiles;
    int32_t texture = -1;
    uint32_t *pixels = nullptr;
    int32_t stride = 0;
    for (uint32_t i = 0; i < weights.size(); ++i) {
        if (!(weights[i] > 0.0f)) continue;
        const uint32_t room = static_cast<uint16_t>(i);
        if (room == 0) continue;
        tiles.mLast = tiles.mFirst;
        Call<76>(mWorld, room, &tiles, 0u);
        for (const Tile *tile = static_cast<const Tile *>(tiles.mFirst); tile != tiles.mLast; ++tile) {
            const int32_t *record = &tile->x;
            if (!TileShown(record)) continue;
            uint32_t x0, y0, x1, y1;
            int32_t index;
            if (!Call<19, bool>(mRef24, record, &x0, &y0, &x1, &y1, &index)) continue;
            if (index != texture) {
                texture = index;
                LoadTexture(1u, static_cast<uint32_t>(index), pixels, stride);
            }
            const float w = weights[i];
            const float colour[3] = {MulS(tileColour[0], w), MulS(tileColour[1], w), MulS(tileColour[2], w)};
            if (tile->kind == 15) AddFlatQuad(colour, x0, y0, x1, y1, pixels, stride);
            else AddFlatTriangle(static_cast<char>(tile->kind), colour, x0, y0, x1, y1, pixels, stride);
        }
    }
    const float wallScale = Call<23, float>(mRef24, 0u);
    const float wallColour[3] = {MulS(sky.x, wallScale), MulS(sky.y, wallScale), MulS(sky.z, wallScale)};
    texture = -1;
    SegmentSet done;
    for (uint32_t n = 0; n < Call<55>(mWorld); ++n) {
        Segment wall;
        Call<56>(mWorld, &wall, n);
        Segment key;
        OrderSegment(key.v, wall.v);
        if (done.find(key) != done.end()) continue;
        done.insert(key);
        for (int side = 0; side < 2; ++side) {
            if (side == 1) SwapEnds(wall);
            const uint16_t roomBits = RoomOfWall(wall.v);
            const float w = WeightAt(weights, static_cast<int16_t>(roomBits));
            if (!(w > 0.0f) || roomBits == 0) continue;
            uint32_t x0, y0, x1, y1;
            int32_t index;
            if (!Call<21, bool>(mRef24, wall.v, &x0, &y0, &x1, &y1, &index)) continue;
            if (index != texture) {
                texture = index;
                LoadTexture(0u, static_cast<uint32_t>(index), pixels, stride);
            }
            const float colour[3] = {MulS(wallColour[0], w), MulS(wallColour[1], w), MulS(wallColour[2], w)};
            AddFlatQuad(colour, x0, y0, x1, y1, pixels, stride);
        }
    }
}

void cLightingManager::LightListedBySky(const uint8_t *lights, int32_t count)
{
    if (!mWorld) return;
    std::vector<float> weights;
    if (!AddSkyLight(lights, count, &weights)) return;
    const cLightVector sky = CurrentSet().mSkyColour;
    const float tileScale = Call<23, float>(mRef24, 1u);
    const float tileColour[3] = {MulS(sky.x, tileScale), MulS(sky.y, tileScale), MulS(sky.z, tileScale)};
    int32_t texture = -1;
    uint32_t *pixels = nullptr;
    int32_t stride = 0;
    std::vector<Tile> tiles;
    Call<18>(mLitObject, &tiles);
    for (const Tile &tile : tiles) {
        const int16_t room = static_cast<int16_t>(Call<79>(mWorld, &tile, 0u, 1u));
        const float w = WeightAt(weights, room);
        if (!(w > 0.0f)) continue;
        uint32_t x0, y0, x1, y1;
        int32_t index;
        if (!Call<19, bool>(mRef24, &tile, &x0, &y0, &x1, &y1, &index)) continue;
        if (index != texture) {
            texture = index;
            LoadTexture(1u, static_cast<uint32_t>(index), pixels, stride);
        }
        const float colour[3] = {MulS(tileColour[0], w), MulS(tileColour[1], w), MulS(tileColour[2], w)};
        if (tile.kind == 15) AddFlatQuad(colour, x0, y0, x1, y1, pixels, stride);
        else AddFlatTriangle(static_cast<char>(tile.kind), colour, x0, y0, x1, y1, pixels, stride);
    }
    const float wallScale = Call<23, float>(mRef24, 0u);
    const float wallColour[3] = {MulS(sky.x, wallScale), MulS(sky.y, wallScale), MulS(sky.z, wallScale)};
    texture = -1;
    std::vector<Segment> walls;
    Call<19>(mLitObject, &walls);
    const int32_t walled = static_cast<int32_t>(walls.size());
    for (int32_t i = 0; i < walled; ++i) {
        const Segment wall = walls[static_cast<uint32_t>(i)];
        const uint16_t roomBits = RoomOfWall(wall.v);
        const float w = WeightAt(weights, static_cast<int16_t>(roomBits));
        if (!(w > 0.0f) || roomBits == 0) continue;
        uint32_t x0, y0, x1, y1;
        int32_t index;
        if (!Call<21, bool>(mRef24, wall.v, &x0, &y0, &x1, &y1, &index)) continue;
        if (index != texture) {
            texture = index;
            LoadTexture(0u, static_cast<uint32_t>(index), pixels, stride);
        }
        const float colour[3] = {MulS(wallColour[0], w), MulS(wallColour[1], w), MulS(wallColour[2], w)};
        AddFlatQuad(colour, x0, y0, x1, y1, pixels, stride);
    }
}

// ---- the sky samples' room tiles and wall portals, a wall side's corners (row LG4b) ------------------------------------

namespace {

// The 0x20e006 service: slot 3 (kind, key, &colour) gives a floor object's (kind 1) or a wall name's (kind 2) colour.
void *ColourService() { return OriginalFunction<void *(__cdecl *)(), 0x20e006>()(); }

// s * (v - g) + g towards the game's grey g (0xffd994, data), with the original's operand order: the first component
// (v - g) * s, the others s * (v - g).
void TowardsGrey(const float *v, float s, float *out)
{
    const float *const g = GameAddress<const float>(0xffd994);
    out[0] = AddS(MulS(SubS(v[0], g[0]), s), g[0]);
    out[1] = AddS(MulS(s, SubS(v[1], g[1])), g[1]);
    out[2] = AddS(MulS(s, SubS(v[2], g[2])), g[2]);
}

// A tile list's length: the byte span shifted right (signed), as the original counts it.
uint32_t TileCount(const TileList &tiles)
{
    const int32_t bytes = static_cast<int32_t>(reinterpret_cast<uintptr_t>(tiles.mLast) -
                                               reinterpret_cast<uintptr_t>(tiles.mFirst));
    return static_cast<uint32_t>(bytes >> 4);
}

} // namespace

void __cdecl cLightingManager::WallCorners(const float *wall, float *quad)
{
    nTSWorld::WorldPoint first{wall[0], wall[1], static_cast<int32_t>(BitsOf(wall[2]))};
    nTSWorld::WorldPoint second{wall[3], wall[4], static_cast<int32_t>(BitsOf(wall[5]))};
    const float ground0 = static_cast<float>(worldheights::GroundHeightAt(&first));
    const float ground1 = static_cast<float>(worldheights::GroundHeightAt(&second));
    // receiver: nTSWorld::cIWorldDB (0x20e0b2's object; slot 36): cWorldRoof
    void *const roofs = static_cast<nTSWorld::cIWorldDB *>(OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()())->GetRoofs();
    float top0;
    // receiver: nTSWorld::cIWorldRoof (vf37 the point under a roof; vf22 (ret 8) the roof's height there, in st0)
    if (roofs && RawSlot<37, bool>(roofs, &first)) {
        top0 = static_cast<float>(x87::Rounded(RawSlot<22, double>(roofs, &first, 1) - 0.25));
    } else {
        ++first.level;
        top0 = static_cast<float>(worldheights::GroundHeightAt(&first));
    }
    float top1;
    // receiver: nTSWorld::cIWorldRoof (vf37, vf22 as above)
    if (roofs && RawSlot<37, bool>(roofs, &second)) {
        top1 = static_cast<float>(x87::Rounded(RawSlot<22, double>(roofs, &second, 1) - 0.25));
    } else {
        ++second.level;
        top1 = static_cast<float>(worldheights::GroundHeightAt(&second));
    }
    const float low = ground1 < ground0 ? ground1 : ground0;
    const float high = top1 > top0 ? top1 : top0;
    const float span = SubS(high, low);
    // The span rounded half up to a whole number (1.5 * 2^23 added: the bits less 0x4b400000), one less where that one
    // less equals the span.
    const float biased = AddS(AddS(span, 0.5f), 12582912.0f);
    int32_t units = static_cast<int32_t>(BitsOf(biased) + 0xb4c00000u);
    if (static_cast<float>(units - 1) == span) units -= 1;
    const float top = AddS(static_cast<float>(units), low);
    const float corners[12] = {wall[0], wall[1], low, wall[3], wall[4], low, wall[0], wall[1], top, wall[3], wall[4], top};
    std::memcpy(quad, corners, sizeof(corners));
}

void cLightingManager::SampleRoomTiles()
{
    if (!mWorld) return;
    uint32_t unused[2];
    Call<40>(mTerrain, unused); // receiver: mTerrain (class not identified); what it fills is not read
    void *const service = ColourService();
    std::set<int16_t, std::less<int16_t>, GZServiceAllocator<int16_t>> rooms;
    Call<81>(mWorld, 0u, &rooms); // receiver: mWorld (class not identified): fills the set of rooms
    if (rooms.empty()) return;
    const int32_t largest = *rooms.rbegin();
    if (SkySampleCount() <= static_cast<uint32_t>(largest)) mSkySamples.resize(static_cast<uint32_t>(largest + 1));
    TileList tiles; // kept across the rooms: slot 76 adds to it
    for (const int16_t room : rooms) {
        const int32_t index = room;
        if (static_cast<uint32_t>(index) >= SkySampleCount()) continue;
        cSkySample &sample = mSkySamples.data()[static_cast<uint32_t>(index)];
        Call<76>(mWorld, static_cast<uint32_t>(static_cast<uint16_t>(room)), &tiles, 0u); // receiver: mWorld
        for (uint32_t i = 0; i < TileCount(tiles); ++i) {
            for (int32_t bit = 0; bit < 4; ++bit) {
                const Tile &tile = static_cast<const Tile *>(tiles.mFirst)[i];
                const uint8_t mask = static_cast<uint8_t>(1u << bit);
                if (!(tile.kind & mask)) continue;
                worldmath::TileQuadrant quadrant{tile.x, tile.y, tile.level, mask};
                void *object = nullptr;
                Call<43>(mTerrain, &object, &quadrant); // receiver: mTerrain: the floor object on the quadrant
                if (!object) continue;
                float colour[3] = {0.0f, 0.0f, 0.0f};
                Call<3>(service, 1u, object, colour); // receiver: the 0x20e006 service (class not identified)
                float centre[3];
                worldmath::QuadrantCentre(centre, &quadrant);
                float out[3];
                const float *const lit = Call<23, const float *>(mLitObject, out, centre); // receiver: mLitObject
                const float c0 = MulS(lit[0], 0.25f), c1 = MulS(lit[1], 0.25f), c2 = MulS(lit[2], 0.25f);
                float grey[3];
                TowardsGrey(colour, mField1D4, grey);
                float *const v4 = sample.mVectors[4];
                v4[1] = AddS(MulS(c1, grey[1]), v4[1]);
                v4[2] = AddS(MulS(c2, grey[2]), v4[2]);
                v4[0] = AddS(MulS(grey[0], c0), v4[0]);
                sample.mWeights[4] = AddS(sample.mWeights[4], 1.0f);
                const float t = mField1CC;
                float *const v5 = sample.mVectors[5];
                v5[0] = AddS(MulS(t, c0), v5[0]);
                v5[1] = AddS(MulS(t, c1), v5[1]);
                v5[2] = AddS(MulS(t, c2), v5[2]);
                sample.mWeights[5] = AddS(sample.mWeights[5], 1.0f);
                // ORIGINAL-BUG: a quarter added to the count and truncated, so it never grows.
                const float counted = AddS(static_cast<float>(static_cast<double>(sample.mTileCount)), 0.25f);
                sample.mTileCount = static_cast<uint32_t>(static_cast<int64_t>(counted));
            }
        }
    }
}

void cLightingManager::SampleWallPortals()
{
    if (!mWorld) return;
    void *const service = ColourService();
    SegmentSet done;
    const uint32_t inner = Call<22>(mWorld); // receiver: mWorld (class not identified)
    const uint32_t total = Call<55>(mWorld); // receiver: mWorld: the walls
    const uint32_t split = total - inner;    // the walls from here on are named by slot 52
    for (uint32_t i = 0; i < total; ++i) {
        Segment wall;
        Call<56>(mWorld, &wall, i); // receiver: mWorld
        Segment key;
        OrderSegment(key.v, wall.v);
        if (done.find(key) != done.end()) continue;
        done.insert(key);
        for (int side = 0; side < 2; ++side) {
            if (side == 1) SwapEnds(wall);
            const uint16_t roomBits = RoomOfWall(wall.v);
            if (roomBits == 0xffffu || roomBits == 0) continue;
            uint32_t record[4];
            // receiver: mWorld: slots 51 / 52 return a record whose first dword is the wall's name
            const char *const *const named = i < split ? Call<51, const char *const *>(mWorld, record, &wall)
                                                       : Call<52, const char *const *>(mWorld, record, &wall);
            const char *const name = *named;
            if (!name || !*name) continue;
            float colour[3] = {0.0f, 0.0f, 0.0f};
            Call<3>(service, 2u, name, colour); // receiver: the 0x20e006 service (class not identified)
            float out[3];
            const float *const lit = Call<24, const float *>(mLitObject, out, &wall); // receiver: mLitObject
            const float c0 = MulS(lit[0], 0.5f), c1 = MulS(lit[1], 0.5f), c2 = MulS(lit[2], 0.5f);
            float grey[3];
            TowardsGrey(colour, mField1D4, grey);
            const float a = MulS(grey[0], c0), b = MulS(c1, grey[1]), c = MulS(c2, grey[2]);
            const float dx = SubS(wall.v[4], wall.v[1]);
            const float dy = Negated(SubS(wall.v[3], wall.v[0]));
            const __m128d squared = _mm_cvtps_pd(_mm_set_ss(AddS(MulS(dx, dx), MulS(dy, dy))));
            const float length = _mm_cvtss_f32(_mm_cvtsd_ss(_mm_setzero_ps(), _mm_sqrt_sd(squared, squared)));
            const int32_t index = static_cast<int16_t>(roomBits);
            if (SkySampleCount() <= static_cast<uint32_t>(index)) mSkySamples.resize(static_cast<uint32_t>(index + 1));
            cSkySample &sample = mSkySamples.data()[static_cast<uint32_t>(index)];
            if (!(length > 0.0f)) continue;
            const float fx = DivS(dx, length), fy = DivS(dy, length);
            const float ax = MulS(a, fx), bx = MulS(b, fx), cx = MulS(c, fx);
            if (fx > 0.0f) {
                sample.mVectors[0][0] = AddS(ax, sample.mVectors[0][0]);
                sample.mVectors[0][1] = AddS(bx, sample.mVectors[0][1]);
                sample.mVectors[0][2] = AddS(cx, sample.mVectors[0][2]);
                sample.mWeights[0] = AddS(fx, sample.mWeights[0]);
            } else {
                sample.mVectors[1][0] = SubS(sample.mVectors[1][0], ax);
                sample.mVectors[1][1] = SubS(sample.mVectors[1][1], bx);
                sample.mVectors[1][2] = SubS(sample.mVectors[1][2], cx);
                sample.mWeights[1] = SubS(sample.mWeights[1], fx);
            }
            const float ay = MulS(fy, a), by = MulS(fy, b), cy = MulS(fy, c);
            if (fy > 0.0f) {
                sample.mVectors[2][0] = AddS(ay, sample.mVectors[2][0]);
                sample.mVectors[2][1] = AddS(by, sample.mVectors[2][1]);
                sample.mVectors[2][2] = AddS(cy, sample.mVectors[2][2]);
                sample.mWeights[2] = AddS(fy, sample.mWeights[2]);
            } else {
                sample.mVectors[3][0] = SubS(sample.mVectors[3][0], ay);
                sample.mVectors[3][1] = SubS(sample.mVectors[3][1], by);
                sample.mVectors[3][2] = SubS(sample.mVectors[3][2], cy);
                sample.mWeights[3] = SubS(sample.mWeights[3], fy);
            }
            ++sample.mWallCount;
        }
    }
}

} // namespace nTSSG
