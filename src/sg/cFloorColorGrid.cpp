// nTSSG::cFloorColorGrid, rows LD2a (lane 261109i: the grid's storage and its leaves), LD2b (lane 261109l: the light
// cells), LD2c (lane 261109q: the step and the rebuild's leaves) and LD2g (lane 261110b: the rebuild). See the header.
//
// Still the original's, called through OriginalFunction: the stopwatch's Restart 0x1c95ec, the placement service
// 0x20e0b2 (the world DB) and the wall answer 0x7defd0.
#include "sg/cFloorColorGrid.h"

#include "game/original.h"
#include "rz/StopwatchMore.h"
#include "sg/cImageData.h"
#include "sg/cLightMapManager.h" // cILightMapManager, FloorUse, LightMapStopwatch
#include "world/WorldCatalog.h"
#include "world/WorldHeights.h"
#include "world/WorldIterators.h"
#include "world/WorldTileMath.h"
#include "world/WorldTileRects.h"
#include "world/cWorldDB.h"
#include "game/vtable.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <new>

TS2_OWNED_STATIC(const OwnedVTable, dbeb74); // nTSSG::cFloorColorGrid (slot 0: the deleting destructor 0x3c4870)
TS2_OWNED_STATIC(const float, 10da6fc)[3]; // GetColor's colour off the lit floor (.bss, zero; only read)

namespace nTSSG {

FloorColorSample::FloorColorSample() : mColor{}, mOther{}, m18(0xff), m19(0), m1a(1), mLit(0), m1c(0), m1e(0) {}

FloorColorLink::FloorColorLink() : m0(0), m4(0), mCells{} {}

cFloorColorGrid::cFloorColorGrid()
    : mVTable(&ts2_rva_dbeb74), m04(std::bit_cast<float>(0x3f7f7ceeu)), m0c(std::bit_cast<float>(0x3f4ed917u)),
      mThreshold(std::bit_cast<float>(0x3e0c49bau))
{
}

cFloorColorGrid::cFloorColorGrid(const cFloorColorGrid &other)
    : mVTable(&ts2_rva_dbeb74), m04(other.m04), m08(other.m08), m0c(other.m0c), mInsideAmbient(other.mInsideAmbient),
      mOutsideAmbient(other.mOutsideAmbient), m18{other.m18[0], other.m18[1], other.m18[2]},
      mInsideAmbientColor{other.mInsideAmbientColor[0], other.mInsideAmbientColor[1], other.mInsideAmbientColor[2]},
      mOutsideAmbientColor{other.mOutsideAmbientColor[0], other.mOutsideAmbientColor[1], other.mOutsideAmbientColor[2]},
      mThreshold(other.mThreshold), m40(other.m40), mLevel(other.mLevel), mWidth(other.mWidth), mHeight(other.mHeight),
      mCells(other.mCells), mLinks(other.mLinks), m68(other.m68), mLights(other.mLights), mLevels(other.mLevels),
      mSteps(other.mSteps)
{
}

__declspec(noinline) cFloorColorGrid::~cFloorColorGrid()
{
    // The vtable is stored again (the original's destructor does), then the members go in reverse order.
    *static_cast<const OwnedVTable *volatile *>(static_cast<void *>(&mVTable)) = &ts2_rva_dbeb74;
}

// The scalar deleting destructor: the destructor (kept out of line, as the original calls it), then the block.
cFloorColorGrid *cFloorColorGrid::DeletingDestructor(uint32_t flags)
{
    this->~cFloorColorGrid();
    if (flags & 1) ::operator delete(this, sizeof(cFloorColorGrid));
    return this;
}

__declspec(noinline) cFloorColorGrid *__fastcall cFloorColorGrid::Construct(void *memory, void *)
{
    return new (memory) cFloorColorGrid;
}

__declspec(noinline) cFloorColorGrid *__fastcall cFloorColorGrid::CopyConstruct(void *memory, void *,
                                                                                const cFloorColorGrid *other)
{
    return new (memory) cFloorColorGrid(*other);
}

__declspec(noinline) void __fastcall cFloorColorGrid::Destroy(cFloorColorGrid *self, void *)
{
    self->~cFloorColorGrid();
}

__declspec(noinline) void cFloorColorGrid::SetSize(int32_t level, uint32_t width, uint32_t height)
{
    mLevel = level;
    mHeight = height;
    mWidth = width;
    const uint32_t tiles = width * height;
    mCells.resize(tiles * 4);
    mLinks.resize(tiles);
}

__declspec(noinline) FloorColorCell *cFloorColorGrid::Cell(uint32_t index) { return mCells.data() + index; }

__declspec(noinline) FloorColorLink *cFloorColorGrid::Link(uint32_t index) { return mLinks.data() + index; }

__declspec(noinline) uint32_t cFloorColorGrid::CellIndex(int32_t x, int32_t y)
{
    uint32_t column = static_cast<uint32_t>(x);
    if (x < 0)
        column = 0;
    else if (column >= mWidth)
        column = mWidth - 1;
    uint32_t row = static_cast<uint32_t>(y);
    if (y < 0)
        row = 0;
    else if (row >= mHeight)
        row = mHeight - 1;
    return mWidth * row + column;
}

__declspec(noinline) void cFloorColorGrid::SetInsideAmbientColor(const float *color)
{
    std::memcpy(mInsideAmbientColor, color, sizeof mInsideAmbientColor);
}

__declspec(noinline) void cFloorColorGrid::SetOutsideAmbientColor(const float *color)
{
    std::memcpy(mOutsideAmbientColor, color, sizeof mOutsideAmbientColor);
}

namespace {

// One record pushed: the tile, the level and the flag byte (the last word's other three bytes left unset).
void PushTile(std::vector<DiffusionTile> *out, const FloorLightCell &cell, int32_t level, uint8_t flag)
{
    DiffusionTile tile;
    tile.words[0] = static_cast<uint32_t>(cell.mX);
    tile.words[1] = static_cast<uint32_t>(cell.mY);
    tile.words[2] = static_cast<uint32_t>(level);
    std::memcpy(reinterpret_cast<uint8_t *>(&tile) + 12, &flag, 1);
    out->push_back(tile);
}

} // namespace

__declspec(noinline) void cFloorColorGrid::CollectTiles(std::vector<DiffusionTile> *out)
{
    for (uint32_t i = 0; i < mLights.size(); ++i) {
        const FloorLightCell &cell = mLights[i];
        if (cell.mActive) PushTile(out, cell, mLevel, static_cast<uint8_t>(cell.mFlagB | cell.mFlagA));
    }
}

__declspec(noinline) void cFloorColorGrid::CollectTilesB(std::vector<DiffusionTile> *out)
{
    for (uint32_t i = 0; i < mLights.size(); ++i) {
        const FloorLightCell &cell = mLights[i];
        if (cell.mFlagB && cell.mActive) PushTile(out, cell, mLevel, cell.mFlagB);
    }
}

__declspec(noinline) void cFloorColorGrid::CollectTilesA(std::vector<DiffusionTile> *out)
{
    for (uint32_t i = 0; i < mLights.size(); ++i) {
        const FloorLightCell &cell = mLights[i];
        if (cell.mFlagA && cell.mActive) PushTile(out, cell, mLevel, cell.mFlagA);
    }
}

__declspec(noinline) void cFloorColorGrid::Reset()
{
    for (uint32_t i = 0; i < mLights.size(); ++i) {
        for (FloorLightHalf &half : mLights[i].mHalves)
            if (half.mSave) std::memcpy(half.mSaved, half.mCurrent, sizeof half.mSaved);
    }
}

namespace {

void AddHalf(FloorColorSample &sample, int16_t level, const float *half)
{
    if (!sample.mLit || sample.mLevel != level) return;
    for (int k = 0; k < 3; ++k) sample.mColor[k] = half[k] + sample.mColor[k];
}

} // namespace

__declspec(noinline) void cFloorColorGrid::Mark(uint32_t level, const float *amount)
{
    const float half[3] = {amount[0] * 0.5f, amount[1] * 0.5f, amount[2] * 0.5f};
    const int16_t key = static_cast<int16_t>(level);
    if (mLevels.find(key) == mLevels.end()) return;
    for (uint32_t i = 0; i < mLights.size(); ++i) {
        const FloorLightCell &cell = mLights[i];
        const FloorColorLink *const link = Link(CellIndex(cell.mX, cell.mY));
        for (uint32_t j = 0; j < 4; ++j) {
            FloorColorCell *const c = Cell(link->mCells[j]);
            AddHalf(c->mSamples[0], key, half);
            AddHalf(c->mSamples[1], key, half);
        }
    }
}

// ---- LD2b: the light cells ----

namespace {

// The eight directions (dx, dy): 0 up (y - 1), then counter-clockwise; odd directions are the corners.
constexpr int32_t kDirX[8] = {0, -1, -1, -1, 0, 1, 1, 1};
constexpr int32_t kDirY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// The blob weights: a 2x2 and a 4x4 falloff (float bits).
constexpr uint32_t kBlob2[4] = {0x3f39b9bau, 0x3e909091u, 0x3e909091u, 0x3d40c0c1u};
constexpr uint32_t kBlob4[16] = {1063839977u, 1061010878u, 1057194884u, 1049398413u, 1061010878u, 1057194884u,
                                 1049398413u, 1033937057u, 1057194884u, 1049398413u, 1040746633u, 1019265217u,
                                 1049398413u, 1033937057u, 1019265217u, 0u};

// SSE arithmetic with the NaN rule spelt out: when the first operand is a NaN it is the result (quieted), whatever the
// second; otherwise the hardware's (which returns the second's NaN, quieted). Kept so the compiler cannot swap operands.
float Quiet(float a) { return std::bit_cast<float>(std::bit_cast<uint32_t>(a) | 0x400000u); }
double Quiet(double a) { return std::bit_cast<double>(std::bit_cast<uint64_t>(a) | 0x8000000000000ull); }
template <class T> T Add(T a, T b) { return a != a ? Quiet(a) : a + b; }
template <class T> T Sub(T a, T b) { return a != a ? Quiet(a) : a - b; }
template <class T> T Mul(T a, T b) { return a != a ? Quiet(a) : a * b; }

// An unsigned count as the original converts it: through double, then float.
float CountToFloat(uint32_t n) { return static_cast<float>(static_cast<double>(n)); }

// The integer part of x >= 0 by the magic-number trick (round to nearest of x - 0.5, then one up when that lands
// exactly on x), and the fraction left.
uint32_t FloorIndex(float x)
{
    const float t = (x - 0.5f) + 12582912.0f;
    uint32_t i = std::bit_cast<uint32_t>(t) + 0xb4c00000u;
    if (static_cast<float>(static_cast<int32_t>(i + 1)) == x) ++i;
    return i;
}

// One colour channel to a byte: (sum * scale2 + 49152) read as an integer, clamped to 0..255.
uint32_t Channel(float sum, float scale2)
{
    const float biased = Add(Mul(sum, scale2), 49152.0f);
    const uint32_t v = std::bit_cast<uint32_t>(biased) + 0xb8c00000u;
    const int32_t s = static_cast<int32_t>(v);
    const uint32_t low = static_cast<uint32_t>(~(s >> 31) & s);
    const uint32_t high = static_cast<uint32_t>(static_cast<int32_t>(0xffu - v) >> 31);
    return (low | high) & 0xffu;
}

// The 4x4 grid sampled at (uu, vv) (grid units, 0.5 the first entry's centre): the four neighbours weighted
// bilinearly, each channel scaled and packed.
uint32_t Bilinear(const FloorColorVec *grid, float uu, float vv, float scale2)
{
    const uint32_t ix = FloorIndex(uu);
    const float fu = uu - static_cast<float>(static_cast<double>(ix));
    const uint32_t iy = FloorIndex(vv);
    const float fv = vv - static_cast<float>(static_cast<double>(iy));
    const double a = 1.0 - static_cast<double>(fu);
    const double b = 1.0 - static_cast<double>(fv);
    const float w11 = Mul(fv, fu);
    const float w01 = static_cast<float>(Mul(static_cast<double>(fv), a));
    const float w00 = static_cast<float>(Mul(a, b));
    const float w10 = static_cast<float>(Mul(static_cast<double>(fu), b));
    const uint32_t idx = ix + iy * 4;
    const FloorColorVec &c00 = grid[idx];
    const FloorColorVec &c10 = grid[idx + 1];
    const FloorColorVec &c01 = grid[idx + 4];
    const FloorColorVec &c11 = grid[idx + 5];
    const auto sum = [&](float FloorColorVec::*k) {
        return Add(Add(Add(Mul(c10.*k, w10), Mul(c00.*k, w00)), Mul(c01.*k, w01)), Mul(c11.*k, w11));
    };
    return 0xff000000u | Channel(sum(&FloorColorVec::x), scale2) << 16 | Channel(sum(&FloorColorVec::y), scale2) << 8 |
           Channel(sum(&FloorColorVec::z), scale2);
}

// u or v (0..1 across the rectangle) to grid units.
float GridUnits(float t) { return static_cast<float>(static_cast<double>(t) + static_cast<double>(t) + 0.5); }

} // namespace

__declspec(noinline) void FloorLightHalf::ShadeQuad(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom,
                                                    uint32_t *pixels, int32_t pitch, float scale, bool levelZero)
{
    const uint32_t width = right - left;
    const uint32_t height = bottom - top;
    float du, dv, v, u0;
    if (levelZero) {
        du = static_cast<float>(1.0 / static_cast<double>(CountToFloat(width)));
        dv = static_cast<float>(-1.0 / static_cast<double>(CountToFloat(height)));
        v = static_cast<float>(static_cast<double>(dv) * 0.5 + 1.0);
        u0 = static_cast<float>(static_cast<double>(du) * 0.5);
    } else {
        du = static_cast<float>(1.0 / static_cast<double>(CountToFloat(width - 1)));
        dv = static_cast<float>(-1.0 / static_cast<double>(CountToFloat(height - 1)));
        v = 1.0f;
        u0 = 0.0f;
    }
    if (top >= bottom) return;
    uint32_t *row = pixels + (top * static_cast<uint32_t>(pitch) + left);
    for (uint32_t r = height; r != 0; --r) {
        if (left < right) {
            const float vv = GridUnits(v);
            const float scale2 = scale + scale;
            float u = u0;
            uint32_t *out = row;
            for (uint32_t n = width; n != 0; --n) {
                *out++ = Bilinear(mCurrent, GridUnits(u), vv, scale2);
                u = Add(u, du);
            }
        }
        v = Add(v, dv);
        row += pitch;
    }
}

__declspec(noinline) void FloorLightHalf::ShadeTriangle(float slope, float threshold, uint8_t side, uint32_t left,
                                                        uint32_t top, uint32_t right, uint32_t bottom,
                                                        uint32_t *pixels, int32_t pitch, float scale, bool levelZero)
{
    const uint32_t width = right - left;
    const uint32_t height = bottom - top;
    float dv, v;
    uint32_t steps;
    if (levelZero) {
        dv = static_cast<float>(-1.0 / static_cast<double>(CountToFloat(height)));
        v = static_cast<float>(static_cast<double>(dv) * 0.5 + 1.0);
        steps = width;
    } else {
        v = 1.0f;
        dv = static_cast<float>(-1.0 / static_cast<double>(CountToFloat(height - 1)));
        steps = width - 1;
    }
    const float du = static_cast<float>(1.0 / static_cast<double>(CountToFloat(steps)));
    if (top >= bottom) return;
    uint32_t *row = pixels + (top * static_cast<uint32_t>(pitch) + left);
    for (uint32_t r = height; r != 0; --r) {
        float u = 0.0f;
        if (left < right) {
            uint32_t *out = row;
            for (uint32_t n = width; n != 0; --n, ++out) {
                // Unordered compares count as "not above".
                const uint32_t above = threshold > u * slope + v ? 1u : 0u;
                if (above != side) *out = Bilinear(mCurrent, GridUnits(u), GridUnits(v), scale + scale);
                u = Add(u, du);
            }
        }
        v = Add(v, dv);
        row += pitch;
    }
}

__declspec(noinline) void FloorLightCell::Shade(uint32_t kind, uint32_t left, uint32_t top, uint32_t right,
                                                uint32_t bottom, uint32_t *pixels, int32_t pitch, float scale,
                                                bool levelZero)
{
    if (kind == 0) {
        if (mHalves[0].mSave) mHalves[0].ShadeQuad(left, top, right, bottom, pixels, pitch, scale, levelZero);
        return;
    }
    float slope, threshold;
    if (kind == 1) {
        slope = 1.0f;
        threshold = std::bit_cast<float>(0x3f7d70a4u); // 0.99
    } else if (kind == 2) {
        slope = -1.0f;
        threshold = std::bit_cast<float>(0xbc23d70au); // -0.01
    } else {
        return;
    }
    if (mHalves[0].mSave)
        mHalves[0].ShadeTriangle(slope, threshold, 0, left, top, right, bottom, pixels, pitch, scale, levelZero);
    if (mHalves[1].mSave)
        mHalves[1].ShadeTriangle(slope, threshold, 1, left, top, right, bottom, pixels, pitch, scale, levelZero);
}

__declspec(noinline) FloorColorSample *cFloorColorGrid::NeighbourSample(int32_t x, int32_t y, uint32_t dir, bool first)
{
    const uint32_t stride = mWidth * 2;
    FloorColorCell *const cells = mCells.data();
    const FloorColorSample &self =
        cells[static_cast<uint32_t>(x) + static_cast<uint32_t>(y) * stride].mSamples[first ? 0 : 1];
    const uint8_t bit = static_cast<uint8_t>(1u << (dir & 31));
    if (!(self.m18 & bit)) return nullptr;
    FloorColorCell &next = cells[static_cast<uint32_t>(kDirX[dir]) +
                                 static_cast<uint32_t>(kDirY[dir] + y) * stride + static_cast<uint32_t>(x)];
    return (self.m19 & bit) ? &next.mSamples[1] : &next.mSamples[0];
}

namespace {

void CopyColor(FloorColorVec *out, const float *color) { std::memcpy(out, color, sizeof *out); }

void SubColor(FloorColorVec *out, const float *color, const float *minus)
{
    out->x = Sub(color[0], minus[0]);
    out->y = Sub(color[1], minus[1]);
    out->z = Sub(color[2], minus[2]);
}

void SubColor(FloorColorVec *entry, const float *minus)
{
    entry->x = Sub(entry->x, minus[0]);
    entry->y = Sub(entry->y, minus[1]);
    entry->z = Sub(entry->z, minus[2]);
}

// A lit neighbour's colour as an entry: less the outside ambient when the grid subtracts it and the sample is outside.
void NeighbourColor(FloorColorVec *out, const FloorColorSample &sample, bool subtract, const float *outside)
{
    if (subtract && sample.m1a)
        SubColor(out, sample.mColor, outside);
    else
        CopyColor(out, sample.mColor);
}

// The direction of ring entry (i, j) from the tile's centre (i the column 0..3, j the row).
uint32_t RingDir(uint32_t i, uint32_t j)
{
    if (i == 0) return j == 0 ? 1 : j == 3 ? 3 : 2;
    if (i == 3) return j == 0 ? 7 : j == 3 ? 5 : 6;
    return j == 0 ? 0 : j == 3 ? 4 : 0;
}

} // namespace

__declspec(noinline) void cFloorColorGrid::PrepareLight(int32_t x, int32_t y, FloorLightCell *cell)
{
    const FloorColorLink *const link = Link(CellIndex(x, y));
    FloorColorCell *corners[4];
    for (uint32_t j = 0; j < 4; ++j) corners[j] = Cell(link->mCells[j]);
    const uint8_t kind = link->Kind();

    FloorLightHalf *const halves = cell->mHalves;
    halves[0].mSave = 0;
    halves[1].mSave = 0;
    for (uint32_t j = 0; j < 4; ++j) {
        if (corners[j]->mSamples[0].mLit) halves[0].mSave = 1;
        if (corners[j]->mSamples[1].mLit) halves[1].mSave = 1;
    }

    float inside[3], outside[3];
    for (int k = 0; k < 3; ++k) {
        inside[k] = Mul(mInsideAmbientColor[k], mInsideAmbient);
        outside[k] = Mul(mOutsideAmbientColor[k], mOutsideAmbient);
    }

    static constexpr uint32_t kCentre[4] = {5, 6, 9, 10};
    const uint32_t halfCount = kind ? 2 : 1;
    for (uint32_t h = 0; h < halfCount; ++h) {
        FloorLightHalf &half = halves[h];
        for (uint32_t j = 0; j < 4; ++j) {
            const FloorColorSample &sample = corners[j]->mSamples[h];
            FloorColorVec *const entry = &half.mCurrent[kCentre[j]];
            if (sample.mLit)
                CopyColor(entry, sample.mColor);
            else
                CopyColor(entry, sample.m1a ? outside : inside);
        }
        if (m40) {
            for (uint32_t j = 0; j < 4; ++j)
                if (corners[j]->mSamples[h].m1a) SubColor(&half.mCurrent[kCentre[j]], outside);
        }
    }

    const bool subtract = m40 != 0;
    for (uint32_t i = 0; i < 4; ++i) {
        for (uint32_t j = 0; j < 4; ++j) {
            if ((i == 1 || i == 2) && (j == 1 || j == 2)) continue;
            const uint32_t ix = i >= 2 ? 1 : 0;
            const uint32_t jx = j >= 2 ? 1 : 0;
            const uint32_t dir = RingDir(i, j);
            const int32_t sx = 2 * x + static_cast<int32_t>(ix);
            const int32_t sy = 2 * y + static_cast<int32_t>(jx);
            for (uint32_t h = 0; h < halfCount; ++h) {
                FloorLightHalf &half = halves[h];
                const bool first = h == 0;
                FloorColorVec *const entry = &half.mCurrent[i + j * 4];
                const FloorColorSample *const n = NeighbourSample(sx, sy, dir, first);
                if (n && n->mLit) {
                    NeighbourColor(entry, *n, subtract, outside);
                } else if (dir & 1) {
                    const FloorColorSample *a = NeighbourSample(sx, sy, (dir - 1) & 0xff, first);
                    const FloorColorSample *b = NeighbourSample(sx, sy, (dir + 1) & 7, first);
                    if (a && !a->mLit) a = nullptr;
                    if (b && !b->mLit) b = nullptr;
                    if (a && b) {
                        FloorColorVec ca, cb;
                        NeighbourColor(&ca, *a, subtract, outside);
                        NeighbourColor(&cb, *b, subtract, outside);
                        entry->x = Mul(Add(cb.x, ca.x), 0.5f);
                        entry->y = Mul(Add(ca.y, cb.y), 0.5f);
                        entry->z = Mul(Add(ca.z, cb.z), 0.5f);
                    } else if (a) {
                        NeighbourColor(entry, *a, subtract, outside);
                    } else if (b) {
                        NeighbourColor(entry, *b, subtract, outside);
                    } else {
                        std::memcpy(entry, &half.mCurrent[ix + jx * 4 + 5], sizeof *entry);
                    }
                } else {
                    std::memcpy(entry, &half.mCurrent[ix + jx * 4 + 5], sizeof *entry);
                }
            }
        }
    }
    cell->m308 = kind != 0;
}

namespace {

void Restart(cRZStopwatch *watch) { OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(watch); }

bool HalfChanged(const FloorLightHalf &half)
{
    return half.mSave && std::memcmp(half.mCurrent, half.mSaved, sizeof half.mCurrent) != 0;
}

// The shading pass both light passes end with: each (active) cell drawn into its rectangle of the light map image,
// the image's pixels (bottom row first, so the pitch is negative) fetched again only when the image changes.
template <bool kActiveOnly> void ShadeCells(cFloorColorGrid *grid, cILightMapManager *lights, float scale)
{
    uint32_t lastImage = 0xffffffffu;
    uint32_t *pixels = nullptr;
    int32_t pitch = 0;
    for (uint32_t i = 0; i < grid->mLights.size(); ++i) {
        const FloorLightCell &cell = grid->mLights[i];
        if constexpr (kActiveOnly) {
            if (!cell.mActive) continue;
        }
        const int32_t x = cell.mX;
        const int32_t y = cell.mY;
        FloorUse use; // the padding left as it is
        use.key.x = x;
        use.key.y = y;
        use.key.level = grid->mLevel;
        use.flags = 0xf;
        int32_t left, top, right, bottom;
        uint32_t image;
        if (!lights->GetFloorRect(&use, &left, &top, &right, &bottom, &image)) continue; // receiver: nTSSG::cILightMapManager
        if (image != lastImage) {
            lastImage = image;
            nGZSceneGraph::cIImageData *const data = lights->GetImage(1, image); // receiver: nTSSG::cILightMapManager
            uint8_t *const bytes = data->LevelData(0);          // receiver: nGZSceneGraph::cIImageData
            const uint32_t words = static_cast<uint32_t>(data->LevelPitch(0)) >> 2;
            const int32_t height = data->LevelHeight(0);        // receiver: nGZSceneGraph::cIImageData
            pixels = reinterpret_cast<uint32_t *>(bytes) + static_cast<uint32_t>(height - 1) * words;
            pitch = -static_cast<int32_t>(words);
        }
        const uint32_t kind = grid->Link(grid->mWidth * static_cast<uint32_t>(y) + static_cast<uint32_t>(x))->Kind();
        (grid->mLights.data() + i)
            ->Shade(kind, static_cast<uint32_t>(left), static_cast<uint32_t>(top), static_cast<uint32_t>(right),
                    static_cast<uint32_t>(bottom), pixels, pitch, scale, grid->mLevel == 0);
    }
}

} // namespace

__declspec(noinline) void cFloorColorGrid::ShadeLights(cILightMapManager *lights, float scale)
{
    LightMapStopwatch watch;
    Restart(&watch.value);
    for (uint32_t i = 0; i < mLights.size(); ++i) {
        FloorLightCell *const cell = &mLights[i];
        PrepareLight(cell->mX, cell->mY, cell);
    }
    Restart(&watch.value);
    ShadeCells<false>(this, lights, scale);
}

__declspec(noinline) void cFloorColorGrid::ShadeChangedLights(cILightMapManager *lights, float scale)
{
    LightMapStopwatch watch;
    Restart(&watch.value);
    for (uint32_t i = 0; i < mLights.size(); ++i) {
        FloorLightCell *cell = &mLights[i];
        PrepareLight(cell->mX, cell->mY, cell);
        cell = &mLights[i];
        cell->mActive = HalfChanged(cell->mHalves[0]) || HalfChanged(cell->mHalves[1]);
    }
    Restart(&watch.value);
    ShadeCells<true>(this, lights, scale);
}

__declspec(noinline) FloorKey *FloorBlob::Key(FloorKey *out) const
{
    out->x = mX;
    out->y = mY;
    out->level = mLevel;
    return out;
}

namespace {

// The sample blended toward its ambient colour (outside or inside by its m1a) by `weight`.
void Blend(FloorColorSample &sample, float weight, const cFloorColorGrid &grid)
{
    float ambient[3];
    for (int k = 0; k < 3; ++k)
        ambient[k] = sample.m1a ? Mul(grid.mOutsideAmbientColor[k], grid.mOutsideAmbient)
                                : Mul(grid.mInsideAmbientColor[k], grid.mInsideAmbient);
    const float keep = 1.0f - weight;
    for (int k = 0; k < 3; ++k) sample.mColor[k] = Add(Mul(weight, ambient[k]), Mul(sample.mColor[k], keep));
}

// Colour cell (x, y) of the 2w x 2h grid, or null outside it.
FloorColorCell *CellAt(cFloorColorGrid &grid, int32_t x, int32_t y)
{
    if (x < 0 || y < 0) return nullptr;
    const uint32_t stride = grid.mWidth * 2;
    if (static_cast<uint32_t>(x) >= stride || static_cast<uint32_t>(y) >= grid.mHeight * 2) return nullptr;
    return grid.mCells.data() + (static_cast<uint32_t>(x) + static_cast<uint32_t>(y) * stride);
}

} // namespace

__declspec(noinline) void cFloorColorGrid::TakeBlobs(const std::vector<FloorBlob> *blobs)
{
    for (uint32_t b = 0; b < blobs->size(); ++b) {
        const FloorBlob &blob = (*blobs)[b];
        FloorKey key;
        if (blob.Key(&key)->level != mLevel) continue;
        const uint8_t dir = blob.mDir;
        if (!(dir & 1)) continue;
        // The corner's sub-cell, and the two directions either side of the corner.
        const int32_t x0 = blob.mX * 2 + ((dir == 1 || dir == 3) ? 1 : 0);
        const int32_t y0 = blob.mY * 2 + ((dir == 1 || dir == 7) ? 1 : 0);
        const uint32_t along = static_cast<uint8_t>(dir + 1) & 7u;
        const uint32_t across = static_cast<uint8_t>(dir - 1) & 7u;
        const int32_t ax = kDirX[along], ay = kDirY[along];
        const int32_t cx = kDirX[across], cy = kDirY[across];
        if (blob.mMode) {
            FloorColorCell *quad[4];
            for (int32_t i = 0; i < 2; ++i) {
                quad[i] = CellAt(*this, x0 + ax * i, y0 + ay * i);
                quad[2 + i] = CellAt(*this, x0 + ax * i + cx, y0 + ay * i + cy);
            }
            for (uint32_t k = 0; k < 4; ++k) {
                FloorColorCell *const cell = quad[k];
                if (!cell) continue;
                const float weight = std::bit_cast<float>(kBlob2[k]);
                if (cell->mSamples[0].mLit) Blend(cell->mSamples[0], weight, *this);
                if (cell->mSamples[1].mLit) Blend(cell->mSamples[1], weight, *this);
            }
        } else {
            FloorColorCell *grid[16];
            bool blocked[16];
            for (int32_t k = 0; k < 4; ++k) {
                for (int32_t i = 0; i < 4; ++i) {
                    FloorColorCell *const cell = CellAt(*this, x0 + ax * i + k * cx, y0 + ay * i + k * cy);
                    grid[k * 4 + i] = cell;
                    blocked[k * 4 + i] = cell == nullptr;
                }
            }
            // ORIGINAL-BUG: the original walks each of the first seven cells four steps along a direction table,
            // meaning to stop the falloff at walls (the m18 bits), but every step looks at the same cell and the flag
            // it sets is never read. What survives: those seven cells are skipped when unlit; the last nine are
            // blended lit or not.
            for (uint32_t s = 0; s < 7; ++s)
                if (grid[s] && !grid[s]->mSamples[0].mLit) blocked[s] = true;
            for (uint32_t s = 0; s < 16; ++s)
                if (!blocked[s]) Blend(grid[s]->mSamples[0], std::bit_cast<float>(kBlob4[s]), *this);
        }
    }
}


// ---- LD2c: the step and the rebuild's leaves ----

namespace {

nTSWorld::cIWorldDB *World()
{
    return static_cast<nTSWorld::cIWorldDB *>(OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()());
}

// A direction's bit as the original's shifts make it: the count taken mod 32 (shl, bts and btr on a register), then
// the low byte.
uint8_t Bit(uint32_t n) { return static_cast<uint8_t>(1u << (n & 31)); }

// The link's lit flag: the low byte of its first word.
uint8_t &LinkLit(FloorColorLink &link) { return reinterpret_cast<uint8_t *>(&link)[0]; }

// The upper end of a light's box to sub-cells: (2t - 0.5) in double, then float.
float GridUnitsBelow(float t) { return static_cast<float>(static_cast<double>(t) + static_cast<double>(t) - 0.5); }

// The light's box in sub-cells (signed; either corner may be the larger).
struct LightBox {
    int32_t loX, hiX, loY, hiY;
};

LightBox BoxOf(const FloorLight &light)
{
    const int32_t x0 = static_cast<int32_t>(FloorIndex(GridUnits(light.mMin[0])));
    const int32_t y0 = static_cast<int32_t>(FloorIndex(GridUnits(light.mMin[1])));
    const int32_t x1 = static_cast<int32_t>(FloorIndex(GridUnitsBelow(light.mMax[0])));
    const int32_t y1 = static_cast<int32_t>(FloorIndex(GridUnitsBelow(light.mMax[1])));
    LightBox box;
    box.loX = x0 <= x1 ? x0 : x1;
    box.hiX = x0 <= x1 ? x1 : x0;
    box.loY = y0 <= y1 ? y0 : y1;
    box.hiY = y0 <= y1 ? y1 : y0;
    return box;
}

// The sub-cell (x, y), or null off the grid.
FloorColorCell *BoxCell(cFloorColorGrid &grid, int32_t x, int32_t y)
{
    if (x < 0 || y < 0) return nullptr;
    const uint32_t width = grid.mWidth;
    if (static_cast<uint32_t>(x) >= width * 2 || static_cast<uint32_t>(y) >= grid.mHeight * 2) return nullptr;
    return grid.mCells.data() + (static_cast<uint32_t>(x) + width * static_cast<uint32_t>(y) * 2);
}

bool LightsSample(const FloorColorSample &sample, int16_t id) { return sample.mLit && sample.mLevel == id; }

bool LightsCell(const FloorColorCell &cell, int16_t id)
{
    return LightsSample(cell.mSamples[0], id) || LightsSample(cell.mSamples[1], id);
}

// Whether the world DB has a floor (or the ground) in the quadrant under the position (x, y and the level's bits).
bool FloorAt(nTSWorld::cIWorldDB *world, const float *position)
{
    worldmath::TileQuadrant quadrant;
    worldmath::PositionToTileQuadrant(&quadrant, position);
    return world->HasFloorOrGround(reinterpret_cast<const nTSWorld::WorldTile *>(&quadrant),
                                   static_cast<int8_t>(quadrant.quadrant), true);
}

// The light's colour times the share into a sample it lights, each channel only raised; m1c set when one was.
bool Raise(FloorColorSample &sample, const FloorLight &light, float share)
{
    if (!LightsSample(sample, light.mLevelId)) return false;
    const float v[3] = {Mul(light.mColor[0], share), Mul(light.mColor[1], share), Mul(light.mColor[2], share)};
    bool changed = false;
    for (int k = 0; k < 3; ++k) {
        if (v[k] > sample.mColor[k]) {
            sample.mColor[k] = v[k];
            changed = true;
        }
    }
    if (changed) sample.m1c = 1;
    return changed;
}

// The sub-cell a direction away from (x, y), by the direction table (callers pass directions 0..7; the original
// indexes its table with the whole byte).
FloorColorCell &Toward(cFloorColorGrid &grid, int32_t x, int32_t y, uint32_t dir)
{
    const uint32_t d = dir & 7;
    return grid.mCells.data()[static_cast<uint32_t>(kDirX[d]) +
                              static_cast<uint32_t>(kDirY[d] + y) * grid.mWidth * 2 + static_cast<uint32_t>(x)];
}

// One side of a corner: true unless the sample links along `side` to a lit partner that links on around the corner
// (`around`) to the sample's corner partner's other choice (e: the sample's m19 bit for the corner direction).
bool OpenSide(cFloorColorGrid &grid, int32_t x, int32_t y, uint8_t m18, uint8_t m19, uint32_t side, uint32_t around,
              uint32_t e)
{
    const uint32_t bit = 1u << (side & 31);
    if (!(m18 & bit)) return true;
    FloorColorCell &next = Toward(grid, x, y, side);
    const FloorColorSample &partner = (m19 & bit) ? next.mSamples[1] : next.mSamples[0];
    const uint8_t back = Bit(around);
    if (!partner.mLit || !(partner.m18 & back)) return true;
    return (partner.m19 & back) ? e == 0 : e != 0;
}

} // namespace

__declspec(noinline) int32_t __stdcall cFloorColorGrid::Spread(FloorColorSample *to, const FloorColorSample *from,
                                                              uint8_t dir, float rate)
{
    if (!to || !to->mLit || !from || !from->mLit) return 0;
    double scaled = rate;
    if (dir & 1) scaled = Mul(scaled, 1.4);
    const float r = static_cast<float>(scaled);
    const float inverse = 1.0f / Add(r, 1.0f);
    int32_t count = 0;
    for (int k = 0; k < 3; ++k) {
        const float f = from->mColor[k];
        const float t = to->mColor[k];
        if (Sub(f, t) > Mul(t, r)) {
            to->mColor[k] = Add(Mul(f, inverse), 0.01f);
            ++count;
        }
    }
    return count;
}

__declspec(noinline) uint32_t __cdecl cFloorColorGrid::LevelAt(void *world, float x, float y, int32_t level)
{
    float position[3] = {x, y, std::bit_cast<float>(level)};
    void *const partitions = static_cast<nTSWorld::cIWorldDB *>(world)->GetPartitions();
    // receiver: nTSWorld::cPartitionManager
    return RawSlot<80, uint32_t>(partitions, static_cast<float *>(position), 0, 0);
}

__declspec(noinline) void cFloorColorGrid::ResetColors()
{
    const float inside[3] = {Mul(mInsideAmbientColor[0], mInsideAmbient), Mul(mInsideAmbientColor[1], mInsideAmbient),
                             Mul(mInsideAmbientColor[2], mInsideAmbient)};
    const float outside[3] = {Mul(mOutsideAmbientColor[0], mOutsideAmbient),
                              Mul(mOutsideAmbientColor[1], mOutsideAmbient),
                              Mul(mOutsideAmbientColor[2], mOutsideAmbient)};
    for (uint32_t y = 0; y < mHeight * 2; ++y) {
        for (uint32_t x = 0; x < mWidth * 2; ++x) {
            FloorColorCell &cell = mCells.data()[x + mWidth * y * 2];
            for (FloorColorSample &sample : cell.mSamples) {
                if (!sample.mLit) continue;
                std::memcpy(sample.mColor, sample.m1a ? outside : inside, sizeof sample.mColor);
                sample.m1c = 0;
            }
        }
    }
}

__declspec(noinline) bool cFloorColorGrid::LightReaches(const FloorLight *light)
{
    nTSWorld::cIWorldDB *const world = World();
    const LightBox box = BoxOf(*light);
    for (int32_t x = box.loX; x <= box.hiX; ++x) {
        for (int32_t y = box.loY; y <= box.hiY; ++y) {
            const FloorColorCell *const cell = BoxCell(*this, x, y);
            if (!cell || !LightsCell(*cell, light->mLevelId)) continue;
            int32_t level = light->mLevel;
            if (mLevel > level) continue;
            if (mLevel == level) return true;
            // Down from the light's level to this grid's, each level's floor under the sub-cell blocking it.
            float position[3] = {static_cast<float>(x * 0.5), static_cast<float>(y * 0.5), std::bit_cast<float>(level)};
            bool blocked = false;
            do {
                if (FloorAt(world, position)) {
                    blocked = true;
                    break;
                }
                --level;
                position[2] = std::bit_cast<float>(level);
            } while (level > mLevel);
            if (!blocked) return true;
        }
    }
    return false;
}

__declspec(noinline) bool cFloorColorGrid::ApplyLight(const FloorLight *light, int32_t *min, int32_t *max)
{
    nTSWorld::cIWorldDB *const world = World();
    bool result = false;
    const LightBox box = BoxOf(*light);
    for (int32_t x = box.loX; x <= box.hiX; ++x) {
        for (int32_t y = box.loY; y <= box.hiY; ++y) {
            FloorColorCell *const cell = BoxCell(*this, x, y);
            if (!cell) continue;
            float share = light->mScale;
            if (!LightsCell(*cell, light->mLevelId)) continue;
            int32_t level = light->mLevel;
            if (mLevel > level) continue;
            if (mLevel < level) {
                // Unordered counts as below 1.
                if (light->mFalloff >= 1.0f) continue;
                float position[3] = {static_cast<float>(x * 0.5), static_cast<float>(y * 0.5),
                                     std::bit_cast<float>(level)};
                bool blocked = false;
                do {
                    if (FloorAt(world, position)) {
                        blocked = true;
                        break;
                    }
                    --level;
                    position[2] = std::bit_cast<float>(level);
                    share = static_cast<float>(
                        Mul(Sub(1.0, static_cast<double>(light->mFalloff)), static_cast<double>(share)));
                } while (level > mLevel);
                if (blocked) continue;
            }
            const bool first = Raise(cell->mSamples[0], *light, share);
            const bool second = Raise(cell->mSamples[1], *light, share);
            if (!first && !second) continue;
            result = true;
            if (min[0] > max[0]) {
                min[0] = x;
                max[0] = x;
                min[1] = y;
                max[1] = y;
            } else {
                if (x < min[0]) min[0] = x;
                if (y < min[1]) min[1] = y;
                if (x > max[0]) max[0] = x;
                if (y > max[1]) max[1] = y;
            }
        }
    }
    return result;
}

__declspec(noinline) void cFloorColorGrid::Propagate(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY,
                                                     uint32_t passes, float rate)
{
    const uint32_t width = mWidth;
    if (minX < 0) minX = 0;
    if (static_cast<int32_t>(width * 2 - 1) < maxX) maxX = static_cast<int32_t>(width * 2 - 1);
    if (minY < 0) minY = 0;
    const uint32_t height = mHeight;
    if (static_cast<int32_t>(height * 2 - 1) < maxY) maxY = static_cast<int32_t>(height * 2 - 1);
    for (uint32_t y = 0; y < mHeight * 2; ++y) {
        for (uint32_t x = 0; x < mWidth * 2; ++x) {
            FloorColorCell &cell = mCells.data()[x + mWidth * y * 2];
            cell.mSamples[0].m1d = 0;
            cell.mSamples[1].m1d = 0;
        }
    }
    int32_t count = 0;
    // A sub-cell's lit samples pulled toward their lit neighbours in directions first..last (taken mod 8), only
    // neighbours changed last pass (m1c) when `changedOnly`; true when the count grew.
    const auto pull = [&](int32_t x, int32_t y, uint32_t first, uint32_t last, bool changedOnly) {
        FloorColorCell &cell = mCells.data()[static_cast<uint32_t>(x) + mWidth * static_cast<uint32_t>(y) * 2];
        const int32_t before = count;
        for (uint32_t s = 0; s < 2; ++s) {
            FloorColorSample *const sample = &cell.mSamples[s];
            if (!sample->mLit) continue;
            for (uint32_t d = first; d <= last; ++d) {
                const FloorColorSample *const next = NeighbourSample(x, y, d & 7, s == 0);
                if (!next || !next->mLit || (changedOnly && !next->m1c)) continue;
                const int32_t r = Spread(sample, next, static_cast<uint8_t>(d & 7), rate);
                if (r > 0) {
                    count += r;
                    sample->m1c = 1;
                    sample->m1d = 1;
                }
            }
        }
        return count != before;
    };
    uint32_t pass = 0;
    while (pass < passes) {
        ++pass;
        count = 0;
        int32_t seenMinX = static_cast<int32_t>(mWidth * 2), seenMinY = static_cast<int32_t>(mHeight * 2);
        int32_t seenMaxX = 0, seenMaxY = 0;
        const auto seen = [&](int32_t x, int32_t y) {
            if (x < seenMinX) seenMinX = x;
            if (seenMaxX < x) seenMaxX = x;
            if (y < seenMinY) seenMinY = y;
            if (seenMaxY < y) seenMaxY = y;
        };
        // Along x: rightwards (the box growing right), then leftwards (growing left).
        for (int32_t y = minY; y <= maxY; ++y) {
            for (int32_t x = minX; x <= maxX + 1; ++x) {
                if (static_cast<uint32_t>(x) >= mWidth * 2) break;
                if (pull(x, y, 1, 3, true)) {
                    if (x > maxX) maxX = x;
                    seen(x, y);
                }
            }
            for (int32_t x = maxX; x >= minX - 1; --x) {
                if (x < 0) break;
                if (pull(x, y, 5, 7, true)) {
                    if (x < minX) minX = x;
                    seen(x, y);
                }
            }
        }
        // Along y: downwards, then upwards.
        for (int32_t x = minX; x <= maxX; ++x) {
            for (int32_t y = minY; y <= maxY + 1; ++y) {
                if (static_cast<uint32_t>(y) >= mHeight * 2) break;
                // ORIGINAL-BUG: this sweep alone takes any lit neighbour, changed last pass or not (0x4095bc-0x4095c0
                // test the neighbour's +0x1b only; the other three sweeps also test +0x1c).
                if (pull(x, y, 7, 9, false)) {
                    if (y > maxY) maxY = y;
                    seen(x, y);
                }
            }
            for (int32_t y = maxY; y >= minY - 1; --y) {
                if (y < 0) break;
                if (pull(x, y, 3, 5, true)) {
                    if (y < minY) minY = y;
                    seen(x, y);
                }
            }
        }
        // This pass's changes become the next pass's sources.
        // ORIGINAL-BUG: the box's last row and column are left out (0x4097e7-0x409840 loop while y < maxY and x < maxX),
        // so their samples keep m1d set and m1c as it was.
        for (int32_t y = minY; y < maxY; ++y) {
            for (int32_t x = minX; x < maxX; ++x) {
                FloorColorCell &cell = mCells.data()[static_cast<uint32_t>(x) + mWidth * static_cast<uint32_t>(y) * 2];
                cell.mSamples[0].m1c = cell.mSamples[0].m1d;
                cell.mSamples[1].m1c = cell.mSamples[1].m1d;
                cell.mSamples[0].m1d = 0;
                cell.mSamples[1].m1d = 0;
            }
        }
        minX = seenMinX;
        maxX = seenMaxX;
        minY = seenMinY;
        maxY = seenMaxY;
        if (count == 0) break;
    }
}

__declspec(noinline) void cFloorColorGrid::Step(const std::vector<FloorLight> *lights)
{
    LightMapStopwatch watch;
    Restart(&watch.value);
    ResetColors();
    std::vector<uint16_t> order;
    for (uint32_t i = 0; i < lights->size(); ++i) {
        const FloorLight *const light = lights->data() + i;
        if (light->mLevel >= mLevel && LightReaches(light)) order.push_back(static_cast<uint16_t>(i));
    }
    // Brightest key first (an exchange sort, repeated until a round swaps nothing).
    const size_t n = order.size();
    if (n != 0) {
        bool swapped;
        do {
            swapped = false;
            for (size_t i = 0; i + 1 < n; ++i) {
                for (size_t j = i + 1; j < n; ++j) {
                    if ((*lights)[order[j]].mKey > (*lights)[order[i]].mKey) {
                        const uint16_t t = order[i];
                        order[i] = order[j];
                        order[j] = t;
                        swapped = true;
                    }
                }
            }
        } while (swapped);
    }
    // From the dimmest: each run of keys within 0.01 of its first applied together, then spread.
    while (!order.empty()) {
        bool changed = false;
        int32_t min[2] = {0, 0};
        int32_t max[2] = {-1, -1};
        const float key = (*lights)[order.back()].mKey;
        while (order.size() > 1 && 0.01f > std::fabs(Sub((*lights)[order[order.size() - 2]].mKey, key))) {
            changed |= ApplyLight(&(*lights)[order.back()], min, max);
            order.pop_back();
        }
        const uint16_t last = order.back();
        order.pop_back();
        changed |= ApplyLight(&(*lights)[last], min, max);
        if (changed) Propagate(min[0], min[1], max[0], max[1], static_cast<uint32_t>(mSteps), Mul(key, mThreshold));
    }
}

__declspec(noinline) float *cFloorColorGrid::GetColor(float *out, const float *position)
{
    const uint32_t ix = FloorIndex(position[0]);
    const uint32_t iy = FloorIndex(position[1]);
    FloorColorLink *const link = Link(CellIndex(static_cast<int32_t>(ix), static_cast<int32_t>(iy)));
    if (LinkLit(*link)) {
        const float fx = Sub(Sub(position[0], static_cast<float>(static_cast<int32_t>(ix))), 0.5f);
        const float fy = Sub(Sub(position[1], static_cast<float>(static_cast<int32_t>(iy))), 0.5f);
        // The tile's half the position is in (unordered compares count as false), then its corner.
        const uint8_t kind = link->Kind();
        const bool first = kind == 0 || (kind == 1 && -fx > fy) || (kind == 2 && fx > fy);
        const uint32_t corner = 0.0f > fy ? (0.0f > fx ? 0 : 1) : (0.0f > fx ? 2 : 3);
        const FloorColorSample &sample = Cell(link->mCells[corner])->mSamples[first ? 0 : 1];
        if (sample.mLit) {
            float c[3] = {sample.mColor[0], sample.mColor[1], sample.mColor[2]};
            if (m40 && sample.m1a) {
                for (int k = 0; k < 3; ++k) c[k] = Sub(c[k], Mul(mOutsideAmbientColor[k], mOutsideAmbient));
            }
            for (int k = 0; k < 3; ++k) out[k] = c[k] + c[k];
            return out;
        }
    }
    std::memcpy(out, ts2_rva_10da6fc, 3 * sizeof(float));
    return out;
}

__declspec(noinline) void cFloorColorGrid::MarkFloorTiles()
{
    nTSWorld::cIWorldDB *const world = World();
    for (uint32_t y = 0; y < mHeight; ++y) {
        for (uint32_t x = 0; x < mWidth; ++x) {
            const uint32_t index = CellIndex(static_cast<int32_t>(x), static_cast<int32_t>(y));
            FloorColorLink *const link = Link(index);
            nTSWorld::WorldTile tile; // the segment mask is not set (the slots called ignore it or take their own)
            tile.x = static_cast<int32_t>(x);
            tile.y = static_cast<int32_t>(y);
            tile.level = mLevel;
            if (world->IsInside(&tile) &&
                (world->HasAnyFloorOrGround(&tile, 0xf, true) || world->HasAnyFloorAbove(&tile, 0xf, true))) {
                LinkLit(*link) = 1;
                const uint32_t c = x + mWidth * y * 2;
                link->mCells[0] = c * 2;
                link->mCells[1] = c * 2 + 1;
                link->mCells[2] = (mWidth + c) * 2;
                link->mCells[3] = (mWidth + c) * 2 + 1;
                const int16_t tag = static_cast<int16_t>(index);
                for (uint32_t k = 0; k < 4; ++k) mCells.data()[link->mCells[k]].mSamples[0].m1e = tag;
                for (uint32_t k = 0; k < 4; ++k) mCells.data()[link->mCells[k]].mSamples[1].m1e = tag;
                for (uint32_t k = 0; k < 4; ++k) mCells.data()[link->mCells[k]].mSamples[0].mLit = LinkLit(*link);
                for (uint32_t k = 0; k < 4; ++k) mCells.data()[link->mCells[k]].mSamples[1].mLit = 0;
            } else {
                LinkLit(*link) = 0;
            }
        }
    }
}

__declspec(noinline) void __stdcall cFloorColorGrid::LinkSamples(FloorColorCell *a, FloorColorCell *b, uint8_t dir)
{
    const uint8_t bit = Bit(dir);
    const uint8_t back = Bit((dir - 4) & 7);
    FloorColorSample &a0 = a->mSamples[0], &a1 = a->mSamples[1];
    FloorColorSample &b0 = b->mSamples[0], &b1 = b->mSamples[1];
    // Which samples link across: a's toward dir, b's back.
    const bool fromA0 = a0.mLit && (a0.m18 & bit);
    const bool fromA1 = a1.mLit && (a1.m18 & bit);
    const bool fromB0 = b0.mLit && (b0.m18 & back);
    const bool fromB1 = b1.mLit && (b1.m18 & back);
    if (fromA0 && !fromA1 && !fromB0 && fromB1) {
        // a's first sample to b's second; a's second keeps no link.
        a0.m19 |= bit;
        b1.m19 &= static_cast<uint8_t>(~back);
        a1.m18 &= static_cast<uint8_t>(~bit);
        return;
    }
    bool firsts = false;   // a's first sample to b's first
    bool seconds = false;  // a's second to b's second (after the firsts' pairing)
    bool crossed = false;  // a's second to b's first, a's first unlinked
    if (fromA0) {
        firsts = fromB0;
        if (fromA1) seconds = fromB1;
    } else if (fromA1) {
        if (fromB0)
            crossed = true;
        else
            seconds = fromB1;
    }
    if (firsts) {
        a0.m19 &= static_cast<uint8_t>(~bit);
        b0.m19 &= static_cast<uint8_t>(~back);
    } else {
        a0.m18 &= static_cast<uint8_t>(~bit);
        if (crossed) {
            a1.m19 &= static_cast<uint8_t>(~bit);
            b0.m19 |= back;
            return;
        }
    }
    if (seconds) {
        a1.m19 |= bit;
        b1.m19 |= back;
    } else {
        a1.m18 &= static_cast<uint8_t>(~bit);
    }
}

__declspec(noinline) void cFloorColorGrid::PruneCorner(int32_t x, int32_t y, uint8_t dir)
{
    FloorColorCell *const cell = mCells.data() + (static_cast<uint32_t>(x) + mWidth * static_cast<uint32_t>(y) * 2);
    const uint32_t bit = 1u << (dir & 31);
    const uint8_t before = static_cast<uint8_t>(dir - 1);
    const uint8_t after = static_cast<uint8_t>(dir + 1) & 7;
    for (uint32_t s = 0; s < 2; ++s) {
        FloorColorSample &sample = cell->mSamples[s];
        if (!sample.mLit) continue;
        const uint8_t links = sample.m18;
        if (!(bit & links)) continue;
        const uint8_t partners = sample.m19;
        const uint32_t e = partners & bit;
        if (!OpenSide(*this, x, y, links, partners, before, after, e) ||
            !OpenSide(*this, x, y, links, partners, after, before, e))
            continue;
        sample.m18 &= static_cast<uint8_t>(~Bit(dir));
        FloorColorCell &next = Toward(*this, x, y, dir);
        const uint8_t back = Bit((dir - 4) & 7);
        if (sample.m19 & Bit(dir))
            next.mSamples[1].m18 &= static_cast<uint8_t>(~back);
        else
            next.mSamples[0].m18 &= static_cast<uint8_t>(~back);
    }
}

// ---- LD2g: the rebuild ----

namespace {

// 0x7defd0 (cdecl, still the original's): the world's answer 8 for a tile side (0x7e37e0(key, 8)); BuildCells tests the
// whole word for null.
void *WallAnswer(const worlditer::TileCoordBit *side)
{
    return OriginalFunction<void *(__cdecl *)(const worlditer::TileCoordBit *), 0x7defd0>()(side);
}

// A tile's four corner cells (sub-cells (2x, 2y), (2x + 1, 2y), (2x, 2y + 1), (2x + 1, 2y + 1)) as BuildCells names
// them: a and a + 1 the upper row, b and b + 1 the lower.
struct TileCells {
    FloorColorCell *a;
    FloorColorCell *b;
    void Split() const // every corner's second sample a copy of its first
    {
        for (FloorColorCell *row : {a, b})
            for (uint32_t k = 0; k < 2; ++k) row[k].mSamples[1] = row[k].mSamples[0];
    }
    // A half's level id into the corners' sample `sample`: mLevel and m1a (the id is 0).
    void SetLevel(uint32_t sample, uint16_t id) const
    {
        for (FloorColorCell *row : {a, b})
            for (uint32_t k = 0; k < 2; ++k) {
                row[k].mSamples[sample].mLevel = static_cast<int16_t>(id);
                row[k].mSamples[sample].m1a = id == 0;
            }
    }
};

} // namespace

// The level id of one half at (x, y) in tiles: its quadrants' bit into the link's ground or upper mask, a non-zero id
// into the level set, then into the four corners' sample `sample`.
static void TakeHalfLevel(cFloorColorGrid &grid, FloorColorLink &link, const TileCells &cells, void *world, float x,
                          float y, uint8_t quads, uint32_t sample)
{
    // LevelAt's answer as a 16-bit id (the original tests and stores ax alone).
    const uint16_t id = static_cast<uint16_t>(cFloorColorGrid::LevelAt(world, x, y, grid.mLevel));
    if (id == 0) {
        link.mGroundQuads |= quads;
    } else {
        link.mUpperQuads |= quads;
        grid.mLevels.insert(static_cast<int16_t>(id));
    }
    cells.SetLevel(sample, id);
}

__declspec(noinline) void cFloorColorGrid::BuildCells()
{
    nTSWorld::cIWorldDB *const world = World();
    mLevels.clear();
    worlditer::TileBoxIterator it;
    it.ConstructLevel(mLevel, nullptr); // the cached world DB's box
    while (!it.Done()) {
        worlditer::TileCoord tile;
        it.Get(&tile);
        FloorColorLink *const link = Link(CellIndex(tile.x, tile.y));
        if (LinkLit(*link)) {
            reinterpret_cast<uint8_t *>(link)[1] = 0; // the kind
            const uint32_t x = static_cast<uint32_t>(tile.x), y = static_cast<uint32_t>(tile.y);
            const TileCells cells{mCells.data() + (2 * x + 4 * mWidth * y), mCells.data() + ((4 * y + 2) * mWidth + 2 * x)};
            FloorColorCell *const a = cells.a;
            FloorColorCell *const b = cells.b;
            for (uint32_t bit = 1; bit <= 0x20; bit += bit) {
                worlditer::TileCoord at;
                it.Get(&at);
                const worlditer::TileCoordBit side{at.x, at.y, at.level, bit};
                // receiver: nTSWorld::cPartitionManager (slot 43 0x819f90, ret 4: a partition on the side)
                if (!RawSlot<43, bool>(world->GetPartitions(), &side)) continue;
                const int32_t cut[5] = {side.x, side.y, side.level, static_cast<int32_t>(side.bit), 0};
                void *const partitions = world->GetPartitions();
                float segment[6];
                // receiver: nTSWorld::cPartitionManager (slot 50 0x81a670, ret 4: the segment's catalog id)
                const uint32_t id = RawSlot<50, uint32_t>(partitions, worldrect::CellToSegment(segment, cut));
                if (!worldcatalog::FlagSlot98OrTrue(&id) && id != 0x1d) continue;
                const bool open = WallAnswer(&side) == nullptr;
                worldheights::Lookup9(reinterpret_cast<uint32_t>(&side)); // asked, the answer not used
                uint8_t wall = 0;
                switch (bit) {
                case 2:
                    a[0].mSamples[0].m18 &= 0xf1;
                    b[0].mSamples[0].m18 &= 0xf1;
                    wall = 1;
                    break;
                case 4:
                    b[0].mSamples[0].m18 &= 0xc7;
                    b[1].mSamples[0].m18 &= 0xc7;
                    wall = 8;
                    break;
                case 1:
                    a[0].mSamples[0].m18 &= 0x7c;
                    a[1].mSamples[0].m18 &= 0x7c;
                    wall = 4;
                    break;
                case 8:
                    a[1].mSamples[0].m18 &= 0x1f;
                    b[1].mSamples[0].m18 &= 0x1f;
                    wall = 2;
                    break;
                case 0x20: // the diagonal from (x, y) to (x + 1, y + 1): kind 2
                    cells.Split();
                    a[0].mSamples[0].m18 &= 0xf3;
                    b[0].mSamples[0].m18 &= 0xc1;
                    b[1].mSamples[0].m18 &= 0xe7;
                    a[0].mSamples[1].m18 &= 0x7e;
                    a[1].mSamples[1].m18 &= 0x1c;
                    b[1].mSamples[1].m18 &= 0x3f;
                    wall = 0x10;
                    reinterpret_cast<uint8_t *>(link)[1] = 2;
                    break;
                default: // 0x10, the other diagonal: kind 1
                    cells.Split();
                    b[0].mSamples[0].m18 &= 0xcf;
                    b[1].mSamples[0].m18 &= 0x07;
                    a[1].mSamples[0].m18 &= 0x9f;
                    b[0].mSamples[1].m18 &= 0xf9;
                    a[0].mSamples[1].m18 &= 0x70;
                    a[1].mSamples[1].m18 &= 0xfc;
                    wall = 0x20;
                    reinterpret_cast<uint8_t *>(link)[1] = 1;
                    break;
                }
                if (open) link->m4 |= wall;
            }
            link->mGroundQuads = 0;
            link->mUpperQuads = 0;
            // The tile's corner (x, y) in floats: unsigned to double, then float.
            const float fx = static_cast<float>(static_cast<double>(x));
            const float fy = static_cast<float>(static_cast<double>(y));
            switch (link->Kind()) {
            case 0:
                TakeHalfLevel(*this, *link, cells, world, fx + 0.5f, fy + 0.5f, 0xf, 0);
                break;
            case 1:
                TakeHalfLevel(*this, *link, cells, world, fx + 0.25f, fy + 0.25f, 9, 0);
                TakeHalfLevel(*this, *link, cells, world, fx + 0.75f, fy + 0.75f, 6, 1);
                break;
            case 2:
                TakeHalfLevel(*this, *link, cells, world, fx + 0.75f, fy + 0.25f, 0xc, 0);
                TakeHalfLevel(*this, *link, cells, world, fx + 0.25f, fy + 0.75f, 3, 1);
                break;
            default:
                break;
            }
        }
        it.Next();
    }

    // Every sub-cell's links: toward a neighbour on the grid LinkSamples, off it the direction's bit cleared.
    for (uint32_t y = 0; y < mHeight * 2; ++y) {
        for (uint32_t x = 0; x < mWidth * 2; ++x) {
            FloorColorCell *const cell = mCells.data() + (x + y * mWidth * 2);
            for (uint32_t dir = 0; dir < 8; ++dir) {
                const int32_t nx = static_cast<int32_t>(x) + kDirX[dir];
                const int32_t ny = static_cast<int32_t>(y) + kDirY[dir];
                if (nx < 0 || ny < 0 || static_cast<uint32_t>(nx) >= mWidth * 2 ||
                    static_cast<uint32_t>(ny) >= mHeight * 2) {
                    cell->mSamples[0].m18 &= static_cast<uint8_t>(~Bit(dir));
                    cell->mSamples[1].m18 &= static_cast<uint8_t>(~Bit(dir));
                } else {
                    LinkSamples(cell, mCells.data() + (static_cast<uint32_t>(nx) + static_cast<uint32_t>(ny) * mWidth * 2),
                                static_cast<uint8_t>(dir));
                }
            }
        }
    }
    // Then the corner directions pruned.
    for (uint32_t y = 0; y < mHeight * 2; ++y)
        for (uint32_t x = 0; x < mWidth * 2; ++x)
            for (uint8_t dir = 1; dir < 8; dir += 2) PruneCorner(static_cast<int32_t>(x), static_cast<int32_t>(y), dir);
}

__declspec(noinline) void cFloorColorGrid::Rebuild()
{
    LightMapStopwatch watch; // restarted, never read
    Restart(&watch.value);
    // Value-initialised (zeroed, then the samples' constructor); FloorColorCell{} would skip the zeroing.
    const FloorColorCell blank = FloorColorCell();
    for (FloorColorCell &cell : mCells) cell = blank;
    const FloorColorLink fresh; // the constructor alone: the masks and padding are whatever the stack held
    for (FloorColorLink &link : mLinks) link = fresh;
    MarkFloorTiles();
    BuildCells();
    mLights.clear();
    uint32_t count = 0;
    for (uint32_t y = 0; y < mHeight; ++y) {
        for (uint32_t x = 0; x < mWidth; ++x) {
            FloorColorLink *const link = Link(y * mWidth + x);
            if (!LinkLit(*link)) continue;
            // Value-initialised (zeroed, then the halves' constructor), so the original's clearing of both halves'
            // mSaved (0x40addf, 0x40adf4) and of mActive (0x40ae02) after it leaves the cell as it was: not repeated.
            mLights.resize(++count);
            FloorLightCell &cell = mLights.back();
            cell.mX = static_cast<int32_t>(x);
            cell.mY = static_cast<int32_t>(y);
            cell.mFlagA = link->mGroundQuads;
            cell.mFlagB = link->mUpperQuads;
        }
    }
}

} // namespace nTSSG
