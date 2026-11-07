// cTSNHoodTerrain: the queries of the +8 face over the paint layers, the vertex flags and the static terrain-type table
// (lane FO; DD's "lane 2" table). All /O2.
#include "ts/cTSNHoodTerrain.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cITSNHoodTerrain.h"
#include "std/StdHash.h"

#include <unordered_set>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace {
constexpr uint32_t kTypeTable = 0x10dab00;      // StdHashTable: the type nodes by id
constexpr uint32_t kTypeEntries = 0x10daae8;    // std::vector of 0x828-byte type entries (a cRZString name first)
constexpr uint32_t kTypeEntrySize = 0x828;
constexpr uint32_t kMessageTypeChanged = 0xac3a02d9;
constexpr uint32_t kMessageFlagSet = 0x2bfb06fa;
constexpr uint32_t kMessageFlagCleared = 0x0bfb0707;

constexpr uint32_t kMessageRectEdited = 0x0fb96150;
constexpr uint32_t kShoreDistanceBase = 0x10131ac; // byte (2): the shore map's "far" value is this + 2
constexpr uint32_t kMessage2StandardIid = 0x65297976;
constexpr uint8_t kWeightTotal = 250;

constexpr uint32_t kMaxTypeId = 0x10daad5;         // byte: the largest terrain type id
// The eight neighbours SlopeRect walks (0xdcc02c / 0xdcc04c): odd directions are the diagonals.
constexpr int32_t kNeighbourDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int32_t kNeighbourDy[8] = {0, -1, -1, -1, 0, 1, 1, 1};

// A std::vector<bool> as the original's (MSVC: the word vector, then the bit count), filled by the lot pass 0x4ae5c0 (which
// resizes it through the library's vector<bool>::resize 0x4b3750): one bit per vertex of SlopeRect's rectangle, row by row.
struct TerrainLockBits {
    std::vector<uint32_t> words;
    uint32_t size = 0;
};
static_assert(sizeof(TerrainLockBits) == 16);

using MarkLockedFn = void(__thiscall *)(cTSNHoodTerrain *, const int32_t *, TerrainLockBits *, uint32_t);
using RecomputeNormalsFn =void(__thiscall *)(cTSNHoodTerrain *, const uint32_t *);
using SpreadDistanceFn = void(__cdecl *)(std::vector<std::vector<uint8_t>> *, uint32_t start, uint32_t steps);
using TerrainPassFn = void(__thiscall *)(cTSNHoodTerrain *);
using FlattenUnderLotFn = void(__thiscall *)(cTSNHoodTerrain *, uint32_t);

// The bits of a float, as the original's round-down (x - 0.5 + 1.5 * 2^23, then the mantissa) reads them.
int32_t RoundDown(float value)
{
    const float biased = (value - 0.5f) + 12582912.0f;
    int32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    int32_t result = bits - 0x4b400000;
    if (static_cast<float>(result + 1) == value) result = result + 1;
    return result;
}

// Adds each layer type of one vertex's paint (up to the first empty layer) that `out` does not hold yet.
void AddLayerTypes(const cTSNHoodTerrainVertexPaint &paint, TerrainTypeIdVector *out)
{
    for (int layer = 0; layer < 3; ++layer) {
        const uint8_t type = paint.mLayers[layer].mType;
        if (type == 0) return;
        if (std::find(out->begin(), out->end(), type) == out->end()) out->push_back(type);
    }
}
} // namespace

// The type node with this id, or null. The library's find (its _Find_last is 0x4ac4e0), through a view of the game's
// table by its key: the key is the first thing in a node's value, so a key's address is 8 past its node.
cTSNHoodTerrainTypeNode *cTSNHoodTerrain::FindType(uint8_t type)
{
    auto *table = GameAddress<std::unordered_set<uint8_t>>(kTypeTable);
    const auto found = table->find(type);
    if (found == table->end()) return nullptr;
    return reinterpret_cast<cTSNHoodTerrainTypeNode *>(const_cast<uint8_t *>(&*found) - offsetof(cTSNHoodTerrainTypeNode, mId));
}

// 0x4af650 (slot 12, ret 0xc): the lowest and highest height over a vertex rectangle {x0, y0, x1, y1} (inclusive), through
// the vtable's GetHeightAt. A rectangle reaching outside the vertex grid, or upside down in y, leaves the extremes at
// +/- FLT_MAX.
void cTSNHoodTerrain::GetHeightRange(const uint32_t *rect, float *minHeight, float *maxHeight)
{
    cTSNHoodTerrain *terrain = this;
    *minHeight = FLT_MAX;
    *maxHeight = -FLT_MAX;
    if (rect[0] >= terrain->mVertsX || rect[1] >= terrain->mVertsY || rect[2] >= terrain->mVertsX || rect[3] >= terrain->mVertsY ||
        rect[1] > rect[3])
        return;
    for (uint32_t y = rect[1]; y <= rect[3]; ++y) {
        for (uint32_t x = rect[0]; x <= rect[2]; ++x) {
            const float height = GetHeightAt(x, y);
            if (*minHeight > height) *minHeight = height;
            if (height > *maxHeight) *maxHeight = height;
        }
    }
}

// 0x4b29c0 (slot 18, ret 4): recomputes every normal when asked to, or when they are not valid.
void cTSNHoodTerrain::UpdateNormals(bool force)
{
    cTSNHoodTerrain *terrain = this;
    if (!force && terrain->mNormalsValid) return;
    const uint32_t rect[4] = {0, 0, terrain->mWidth, terrain->mHeight};
    OriginalFunction<RecomputeNormalsFn, 0x4ade70>()(terrain, rect);
}

// 0x4b24b0 (slot 21, ret 4): the current type when its name matches (case-insensitive), otherwise the first entry of the
// type list whose name does, announced. False when no entry matches (the type is kept).
bool cTSNHoodTerrain::SetTerrainType(cIGZString *name)
{
    cTSNHoodTerrain *terrain = this;
    if (terrain->mTerrainType && static_cast<const cIGZString *>(terrain->mTerrainType)->IsEqual(*name, false)) return true;
    uint8_t *const *entries = GameAddress<uint8_t *>(kTypeEntries);
    uint8_t *const end = entries[1];
    for (uint8_t *entry = entries[0]; entry != end; entry += kTypeEntrySize) {
        if (name->IsEqual(*reinterpret_cast<const cIGZString *>(entry), false)) {
            terrain->mTerrainType = entry;
            cTSNHoodTerrain::PostTerrainMessage(kMessageTypeChanged, 0, 0, 0);
            return true;
        }
    }
    return false;
}

// 0x4b25a0 (slot 23, ret 0xc): sets or clears a vertex's flag bit {x, y, layer} (layer 0: bit 1, otherwise bit 2); a change
// is announced, sent at once or posted. No bounds check.
void cTSNHoodTerrain::SetVertexFlag(const uint32_t *vertex, bool set, bool sendNow)
{
    nTSWorld::c2DArray<uint8_t> *flags = this->mVertexFlags;
    const uint8_t bit = (vertex[2] != 0) + 1;
    uint8_t &cell = flags->mData.data()[flags->mWidth * vertex[1] + vertex[0]];
    if (((bit & cell) != 0) == set) return;
    cell = set ? cell | bit : cell & ~bit;
    const uint32_t type = set ? kMessageFlagSet : kMessageFlagCleared;
    if (sendNow)
        cTSNHoodTerrain::SendTerrainMessage(type, vertex[0], vertex[1], vertex[2]);
    else
        cTSNHoodTerrain::PostTerrainMessage(type, vertex[0], vertex[1], vertex[2]);
}

// 0x4af870 (slot 25, ret 0xc): the distinct layer types of one vertex. No bounds check.
void cTSNHoodTerrain::GetVertexTerrainTypes(uint32_t x, uint32_t y, TerrainTypeIdVector *out)
{
    out->clear();
    cTSNHoodTerrain *terrain = this;
    AddLayerTypes(terrain->mPaint.data()[terrain->mVertsX * y + x], out);
}

// 0x4af8f0 (slot 26, ret 8): the distinct layer types over a vertex rectangle {x0, y0, x1, y1} (inclusive; rows compared
// signed, columns unsigned). No bounds check.
void cTSNHoodTerrain::GetRectTerrainTypes(const uint32_t *rect, TerrainTypeIdVector *out)
{
    out->clear();
    cTSNHoodTerrain *terrain = this;
    for (int32_t y = int32_t(rect[1]); y <= int32_t(rect[3]); ++y) {
        const uint32_t row = terrain->mVertsX * uint32_t(y);
        for (uint32_t index = rect[0] + row; index <= rect[2] + row; ++index) AddLayerTypes(terrain->mPaint.data()[index], out);
    }
}

// 0x4af7b0 (slot 27, ret 0xc): the weight's low byte of a type's layer at a vertex; 0 outside the grid or without that layer.
// The type is compared as a whole dword (movzx of the layer's byte, cmp with the argument): 0x101 matches no layer.
uint8_t cTSNHoodTerrain::GetVertexTypeWeight(uint32_t type, uint32_t x, uint32_t y)
{
    cTSNHoodTerrain *terrain = this;
    if (x >= terrain->mVertsX || y >= terrain->mVertsY) return 0;
    const cTSNHoodTerrainVertexPaint &paint = terrain->mPaint.data()[terrain->mVertsX * y + x];
    for (int layer = 0; layer < 3; ++layer) {
        const cTSNHoodTerrainPaintLayer &entry = paint.mLayers[layer];
        if (entry.mType == 0) break;
        if (entry.mType == type) return static_cast<uint8_t>(entry.mWeight);
    }
    return 0;
}

// 0x4af760 (slot 29, ret 4): the type's weight map data, null for an unknown type.
uint8_t *cTSNHoodTerrain::GetTypeWeightMap(uint8_t type)
{
    cTSNHoodTerrainTypeNode *node = cTSNHoodTerrain::FindType(type);
    return node ? node->mWeightMap.mData.data() : nullptr;
}

// 0x4af580 (slot 30, ret 4): every type id, in the table's list order.
void cTSNHoodTerrain::GetTerrainTypeIds(TerrainTypeIdVector *out)
{
    out->clear();
    auto *head = reinterpret_cast<cTSNHoodTerrainTypeNode *>(GameAddress<StdHashTable>(kTypeTable)->mHead);
    for (cTSNHoodTerrainTypeNode *node = head->mNext; node != head; node = node->mNext) out->push_back(node->mId);
}

// 0x4af810 (slot 31, ret 8): the type's name copied out; false for an unknown type.
bool cTSNHoodTerrain::GetTypeName(uint8_t type, cIGZString *out)
{
    cTSNHoodTerrainTypeNode *node = cTSNHoodTerrain::FindType(type);
    if (!node) return false;
    out->Copy(*reinterpret_cast<const cIGZString *>(node->mName));
    return true;
}

// 0x4af5f0 (slot 32, ret 8): the type's second name assigned out; false for an unknown type.
bool cTSNHoodTerrain::GetTypeAltName(uint8_t type, cIGZString *out)
{
    cTSNHoodTerrainTypeNode *node = cTSNHoodTerrain::FindType(type);
    if (!node) return false;
    *out = *reinterpret_cast<const cIGZString *>(node->mAltName);
    return true;
}

// 0x4b04a0 (slot 33, ret 4): the type's flag byte; 0 for an unknown type.
uint8_t cTSNHoodTerrain::GetTypeFlag(uint8_t type)
{
    cTSNHoodTerrainTypeNode *node = cTSNHoodTerrain::FindType(type);
    return node ? node->mFlag : 0;
}

// ---- edits and derived maps ----

// 0x4b1fb0 (slot 9, ret 4): keeps every `step`-th height of each row and column and fills the vertices between with a straight
// line (or the same height when both ends are equal), through the vtable's GetHeightAt / SetHeightAt (no notification). Rows
// first (every step-th row), then every column. The step counts as unsigned.
void cTSNHoodTerrain::ResampleGrid(int32_t step)
{
    cTSNHoodTerrain *terrain = this;
    cTSNHoodTerrain *self = this;
    const uint32_t stride = static_cast<uint32_t>(step);
    uint32_t y = 0;
    do {
        float previous = self->GetHeightAt(0, y);
        uint32_t x = 0;
        if (terrain->mWidth > x) {
            do {
                const uint32_t next = x + stride;
                const float height = self->GetHeightAt(next, y);
                uint32_t between = x + 1;
                if (height == previous) {
                    for (; between < next; ++between) self->SetHeightAt(between, y, previous, 0);
                } else {
                    const float slope = (height - previous) / static_cast<float>(stride);
                    for (; between < next; ++between) {
                        previous += slope;
                        self->SetHeightAt(between, y, previous, 0);
                    }
                }
                previous = height;
                x = next;
            } while (x < terrain->mWidth);
        }
        y += stride;
    } while (y <= terrain->mHeight);

    uint32_t column = 0;
    do {
        float previous = self->GetHeightAt(column, 0);
        if (terrain->mHeight > 0) {
            uint32_t start = 1;
            do {
                const uint32_t end = start - 1 + stride;
                const float height = self->GetHeightAt(column, end);
                if (height == previous) {
                    for (uint32_t row = start; row < end; ++row) self->SetHeightAt(column, row, previous, 0);
                } else {
                    const float slope = (height - previous) / static_cast<float>(stride);
                    for (uint32_t row = start; row < end; ++row) {
                        previous += slope;
                        self->SetHeightAt(column, row, previous, 0);
                    }
                }
                previous = height;
                start += stride;
            } while (start - 1 < terrain->mHeight);
        }
        ++column;
    } while (column <= terrain->mWidth);
}

// 0x4af9e0 (slot 11, ret 8): the height at a world position, on the triangle of its cell (split along the diagonal) through
// the vtable's GetHeightAt; 0 outside the world. The cell is clamped to the last one.
float cTSNHoodTerrain::GetInterpolatedHeight(float x, float z)
{
    cTSNHoodTerrain *self = this;
    if (!self->IsInWorld(x, z)) return 0.0f;
    cTSNHoodTerrain *terrain = this;
    const float cellsX = terrain->mInvCellSize * x;
    const float cellsZ = terrain->mInvCellSize * z;
    uint32_t cellX = static_cast<uint32_t>(RoundDown(cellsX));
    uint32_t cellZ = static_cast<uint32_t>(RoundDown(cellsZ));
    if (cellX >= terrain->mWidth) cellX = terrain->mWidth - 1;
    if (cellZ >= terrain->mHeight) cellZ = terrain->mHeight - 1;
    const float localX = x - static_cast<float>(cellX) * terrain->mCellSize;
    const float localZ = z - static_cast<float>(cellZ) * terrain->mCellSize;
    const float corner00 = self->GetHeightAt(cellX, cellZ);
    const float corner11 = self->GetHeightAt(cellX + 1, cellZ + 1);
    if (localZ >= localX) {
        const float corner01 = self->GetHeightAt(cellX, cellZ + 1);
        return ((corner11 - corner01) * localX + (corner01 - corner00) * localZ) * terrain->mInvCellSize + corner00;
    }
    const float corner10 = self->GetHeightAt(cellX + 1, cellZ);
    return ((corner11 - corner10) * localZ + (corner10 - corner00) * localX) * terrain->mInvCellSize + corner00;
}

// 0x4b2a00 (slot 19, ret 0): every normal, the shore map (0 under water, otherwise the distance spread from the water by
// 0x4aefe0), the lot-distance and paint passes, then each vertex's layer weights normalised to sum 250 and copied into the
// type's weight map. The shore map is indexed [x][y].
void cTSNHoodTerrain::RebuildDerivedMaps()
{
    cTSNHoodTerrain *terrain = this;
    const uint32_t rect[4] = {0, 0, terrain->mWidth, terrain->mHeight};
    OriginalFunction<RecomputeNormalsFn, 0x4ade70>()(terrain, rect);
    cTSNHoodTerrain *self = this;
    uint8_t nearest = 0xff;
    const uint8_t far = static_cast<uint8_t>(*GameAddress<uint8_t>(kShoreDistanceBase) + 2);
    for (uint32_t y = 0; y < terrain->mVertsY; ++y) {
        for (uint32_t x = 0; x < terrain->mVertsX; ++x) {
            const float depth = self->GetWaterDepthAt(x, y);
            uint8_t &cell = terrain->mShoreRows.data()[x].data()[y];
            if (depth > 0.0f) {
                cell = 0;
                nearest = 0;
            } else {
                cell = far;
            }
        }
    }
    if (nearest < far) OriginalFunction<SpreadDistanceFn, 0x4aefe0>()(&terrain->mShoreRows, nearest, static_cast<uint8_t>(far - nearest - 1));
    OriginalFunction<TerrainPassFn, 0x4ae410>()(terrain);
    OriginalFunction<TerrainPassFn, 0x4ae7f0>()(terrain);
    for (uint32_t y = 0; y < terrain->mVertsY; ++y) {
        for (uint32_t x = 0; x < terrain->mVertsX; ++x) {
            cTSNHoodTerrainPaintLayer *layers = terrain->mPaint.data()[terrain->mVertsX * y + x].mLayers;
            uint32_t total = 0;
            for (int i = 0; i < 3 && layers[i].mType != 0; ++i) total += layers[i].mWeight;
            for (int i = 0; i < 3 && layers[i].mType != 0; ++i)
                layers[i].mWeight = static_cast<uint16_t>(layers[i].mWeight * uint32_t(kWeightTotal) / total);
            for (int i = 0; i < 3; ++i) {
                const uint8_t type = layers[i].mType;
                if (type == 0) break;
                const uint8_t weight = static_cast<uint8_t>(layers[i].mWeight);
                if (cTSNHoodTerrainTypeNode *node = cTSNHoodTerrain::FindType(type))
                    node->mWeightMap.mData.data()[node->mWeightMap.mWidth * y + x] = weight;
            }
        }
    }
}

// 0x4b02a0 (slot 36, ret 4): copies a height block from an import source (a lot's terrain): the source's slot 4 gives the
// heights object, which fills a vector (slot 16) and gives its size (slot 5, the last x and y); the source gives the
// destination corner (slot 10), a rotation 0-3 (slot 23; any other value keeps the previous position) and a base height
// added to each sample (slot 16). Each vertex goes through the vtable's SetHeightAt, announced. Nothing without a source or
// when the vector's length is not the block's.
void cTSNHoodTerrain::ImportHeights(void *source)
{
    if (!source) return;
    using GetObjectFn = void *(__thiscall *)(void *);
    using FillFn = uint8_t(__thiscall *)(void *, TerrainHeightVector *);
    using SizeFn = void(__thiscall *)(void *, uint32_t *, uint32_t *);
    using CornerFn = void(__thiscall *)(void *, int32_t *, int32_t *);
    using ByteFn = uint8_t(__thiscall *)(void *);
    using FloatFn = float(__thiscall *)(void *);
    void *heights = VirtualCall<GetObjectFn, 4>(source);
    TerrainHeightVector samples;
    if (!VirtualCall<FillFn, 16>(heights, &samples)) return;
    uint32_t lastX, lastY;
    VirtualCall<SizeFn, 5>(heights, &lastX, &lastY);
    if (samples.size() != (lastX + 1) * (lastY + 1)) return;
    int32_t cornerX, cornerY;
    VirtualCall<CornerFn, 10>(source, &cornerX, &cornerY);
    const uint32_t rotation = VirtualCall<ByteFn, 23>(source);
    uint32_t index = 0;
    const float base = VirtualCall<FloatFn, 16>(source);
    cTSNHoodTerrain *self = this;
    uint32_t dx = 0, dy = 0;
    for (uint32_t j = 0; j <= lastY; ++j) {
        for (uint32_t i = 0; i <= lastX; ++i) {
            const float height = base + samples.data()[index];
            switch (rotation) {
            case 0: dx = i; dy = j; break;
            case 1: dx = j; dy = lastX - i; break;
            case 2: dx = lastX - i; dy = lastY - j; break;
            case 3: dx = lastY - j; dy = i; break;
            default: break;
            }
            self->SetHeightAt(cornerX + dx, cornerY + dy, height, 1);
            ++index;
        }
    }
}

// 0x4b0620 (slot 41, ret 8): raises every vertex of the rectangle (inclusive) by `delta` through the vtable, announced; then
// the rectangle is invalidated (primary slot 6) and the terrain-changed and rect-edited messages are posted.
void cTSNHoodTerrain::RaiseRect(const uint32_t *rect, float delta)
{
    cTSNHoodTerrain *self = this;
    const uint32_t right = rect[2], top = rect[1], bottom = rect[3];
    for (uint32_t x = rect[0]; x <= right; ++x)
        for (uint32_t y = top; y <= bottom; ++y) self->SetHeightAt(x, y, self->GetHeightAt(x, y) + delta, 1);
    InvalidateRect(rect);
    cTSNHoodTerrain::PostTerrainMessage(kMessageTypeChanged, 0, 0, 0);
    cTSNHoodTerrain::PostTerrainMessage(kMessageRectEdited, 0, 0, 0);
}

// 0x4b0150 (slot 42, ret 8): the same with one height for every vertex.
void cTSNHoodTerrain::SetRectHeight(const uint32_t *rect, float height)
{
    cTSNHoodTerrain *self = this;
    const uint32_t right = rect[2], top = rect[1], bottom = rect[3];
    for (uint32_t x = rect[0]; x <= right; ++x)
        for (uint32_t y = top; y <= bottom; ++y) self->SetHeightAt(x, y, height, 1);
    InvalidateRect(rect);
    cTSNHoodTerrain::PostTerrainMessage(kMessageTypeChanged, 0, 0, 0);
    cTSNHoodTerrain::PostTerrainMessage(kMessageRectEdited, 0, 0, 0);
}

// 0x4b2c00 (primary slot 6, ret 4): for each vertex of the rectangle grown by one to the left and top (unsigned: a rectangle
// at 0 starts past the end and posts nothing), both flag layers posted as set and as cleared.
void cTSNHoodTerrain::InvalidateRect(const uint32_t *rect)
{
    const uint32_t right = rect[2], top = rect[1], bottom = rect[3];
    for (uint32_t x = rect[0] - 1; x <= right; ++x) {
        for (uint32_t y = top - 1; y <= bottom; ++y) {
            PostTerrainMessage(kMessageFlagSet, x, y, 0);
            PostTerrainMessage(kMessageFlagSet, x, y, 1);
            PostTerrainMessage(kMessageFlagCleared, x, y, 0);
            PostTerrainMessage(kMessageFlagCleared, x, y, 1);
        }
    }
}

// 0x4af2e0 (slot 28, ret 0xc): the `count` types with the largest total weight over a vertex rectangle (inclusive), heaviest
// first, without those whose total is not positive. The ranking inserts each type id 1..max in turn into a list of `count`
// slots that starts as `count` copies of type 0's total (always 0: no layer of type 0 is ever added).
void cTSNHoodTerrain::GetRectTypeWeights(const uint32_t *rect, TerrainTypeIdVector *out, uint32_t count)
{
    out->clear();
    cTSNHoodTerrain *terrain = this;
    const uint32_t typeCount = *GameAddress<uint8_t>(kMaxTypeId) + 1u;
    std::vector<float> totals(typeCount, 0.0f);
    for (uint32_t y = rect[1]; y <= rect[3]; ++y) {
        for (uint32_t x = rect[0]; x <= rect[2]; ++x) {
            const cTSNHoodTerrainPaintLayer *layers = terrain->mPaint.data()[terrain->mVertsX * y + x].mLayers;
            for (int i = 0; i < 3; ++i) {
                const uint8_t type = layers[i].mType;
                if (type == 0) break;
                if (type < typeCount) totals[type] += static_cast<float>(layers[i].mWeight);
            }
        }
    }
    TerrainTypeIdVector topTypes;
    std::vector<float> topWeights;
    if (count != 0) {
        topTypes.resize(count, 0);
        topWeights.resize(count, totals[0]);
    }
    for (uint8_t type = 1; type < typeCount; ++type) {
        float weight = totals[type];
        uint8_t carried = type;
        for (uint32_t j = 0; j < count; ++j) {
            const float held = topWeights[j];
            if (weight > held) {
                const uint8_t heldType = topTypes[j];
                topTypes[j] = carried;
                topWeights[j] = weight;
                weight = held;
                carried = heldType;
            }
        }
    }
    for (uint32_t j = 0; j < count; ++j) {
        const uint8_t type = topTypes[j];
        if (0.0f >= totals[type]) break;
        out->push_back(type);
    }
}

// 0x4b2700 (slot 35, ret 0x14): `passes` rounds of slope limiting over a vertex rectangle (inclusive, signed): wherever a vertex
// stands higher above a neighbour (8 directions, inside the rectangle) than tan(angle) * 10 (times 1.414 diagonally), the excess
// times `strength` moves from it to the neighbour, through the vtable's SetHeightAt, announced. Vertices the lot pass 0x4ae5c0
// marks (with `lockLots`) keep their height.
void cTSNHoodTerrain::SlopeRect(const int32_t *rect, float angleDegrees, int32_t passes, float strength, uint32_t lockLots)
{
    cTSNHoodTerrain *terrain = this;
    TerrainLockBits locked;
    OriginalFunction<MarkLockedFn, 0x4ae5c0>()(terrain, rect, &locked, lockLots);
    const float straight = static_cast<float>(std::tan(static_cast<double>(angleDegrees * 3.14f / 180.0f))) * 10.0f;
    const float diagonal = straight * 1.414f;
    const int32_t width = rect[2] - rect[0] + 1;
    cTSNHoodTerrain *self = this;
    if (passes <= 0) return;
    do {
        uint32_t bit = 0;
        for (int32_t y = rect[1]; y <= rect[3]; ++y) {
            for (int32_t x = rect[0]; x <= rect[2]; ++x) {
                nTSWorld::c2DArray<float> *heights = terrain->mHeights;
                float here = heights->mData.data()[heights->mWidth * y + x];
                const uint32_t lockedHere = (1u << (bit & 0x1f)) & locked.words.data()[bit >> 5];
                for (uint32_t direction = 0; direction < 8; ++direction) {
                    const int32_t nx = kNeighbourDx[direction] + x;
                    const int32_t ny = kNeighbourDy[direction] + y;
                    if (nx < rect[0] || ny < rect[1] || nx > rect[2] || ny > rect[3]) continue;
                    heights = terrain->mHeights;
                    const float there = heights->mData.data()[heights->mWidth * ny + nx];
                    const float excess = here - there - ((direction & 1) ? diagonal : straight);
                    if (excess > 0.0f) {
                        const float moved = excess * strength;
                        if (lockedHere == 0) {
                            here = here - moved;
                            self->SetHeightAt(x, y, here, 1);
                        }
                        const uint32_t index = (ny - rect[1]) * width - rect[0] + nx;
                        if ((locked.words.data()[index >> 5] & (1u << (index & 0x1f))) == 0) self->SetHeightAt(nx, ny, moved + there, 1);
                    }
                }
                ++bit;
            }
        }
    } while (--passes != 0);
}

// 0x4aecc0 (message face slot 3, ret 4): a lot placed or changed (0xaf / 0xb1, a standard message) flattens the terrain under
// that lot (data 1). True when handled.
bool cTSNHoodTerrain::DoMessage(void *message)
{
    if (!message) return false;
    struct Standard {
        void *object = nullptr;
        ~Standard() noexcept(false)
        {
            if (object) As<ifaces::cIGZMessage2Standard>(object)->Release();
        }
    } standard;
    bool handled;
    if (!As<cIGZUnknown>(message)->QueryInterface(kMessage2StandardIid, &standard.object)) {
        handled = false;
    } else {
        const uint32_t type = As<ifaces::cIGZMessage2Standard>(standard.object)->GetType();
        if (type == 0xaf || type == 0xb1) {
            OriginalFunction<FlattenUnderLotFn, 0x4aee30>()(this, As<ifaces::cIGZMessage2Standard>(standard.object)->GetData1());
            handled = true;
        } else {
            handled = false;
        }
    }
    return handled;
}

// ---- adapters (the RVA hooks): bodies of this file ----

using Adapters = cTSNHoodTerrainAdapters;

void __fastcall Adapters::InvalidateRect(Self *self, void *, const uint32_t *rect)
{
    [[msvc::forceinline_calls]] return self->Self::InvalidateRect(rect);
}

void __fastcall Adapters::ResampleGrid(cITSNHoodTerrain *face, void *, int32_t step)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::ResampleGrid(step);
}

float __fastcall Adapters::GetInterpolatedHeight(cITSNHoodTerrain *face, void *, float x, float z)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetInterpolatedHeight(x, z);
}

void __fastcall Adapters::GetHeightRange(cITSNHoodTerrain *face, void *, const uint32_t *rect, float *minHeight, float *maxHeight)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetHeightRange(rect, minHeight, maxHeight);
}

void __fastcall Adapters::UpdateNormals(cITSNHoodTerrain *face, void *, bool force)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::UpdateNormals(force);
}

void __fastcall Adapters::RebuildDerivedMaps(cITSNHoodTerrain *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::RebuildDerivedMaps();
}

bool __fastcall Adapters::SetTerrainType(cITSNHoodTerrain *face, void *, cIGZString *name)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetTerrainType(name);
}

void __fastcall Adapters::SetVertexFlag(cITSNHoodTerrain *face, void *, const uint32_t *vertex, bool set, bool sendNow)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetVertexFlag(vertex, set, sendNow);
}

void __fastcall Adapters::GetVertexTerrainTypes(cITSNHoodTerrain *face, void *, uint32_t x, uint32_t y, TerrainTypeIdVector *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetVertexTerrainTypes(x, y, out);
}

void __fastcall Adapters::GetRectTerrainTypes(cITSNHoodTerrain *face, void *, const uint32_t *rect, TerrainTypeIdVector *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetRectTerrainTypes(rect, out);
}

uint8_t __fastcall Adapters::GetVertexTypeWeight(cITSNHoodTerrain *face, void *, uint32_t type, uint32_t x, uint32_t y)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetVertexTypeWeight(type, x, y);
}

void __fastcall Adapters::GetRectTypeWeights(cITSNHoodTerrain *face, void *, const uint32_t *rect, TerrainTypeIdVector *out, uint32_t count)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetRectTypeWeights(rect, out, count);
}

uint8_t *__fastcall Adapters::GetTypeWeightMap(cITSNHoodTerrain *face, void *, uint8_t type)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetTypeWeightMap(type);
}

void __fastcall Adapters::GetTerrainTypeIds(cITSNHoodTerrain *face, void *, TerrainTypeIdVector *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetTerrainTypeIds(out);
}

bool __fastcall Adapters::GetTypeName(cITSNHoodTerrain *face, void *, uint8_t type, cIGZString *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetTypeName(type, out);
}

bool __fastcall Adapters::GetTypeAltName(cITSNHoodTerrain *face, void *, uint8_t type, cIGZString *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetTypeAltName(type, out);
}

uint8_t __fastcall Adapters::GetTypeFlag(cITSNHoodTerrain *face, void *, uint8_t type)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetTypeFlag(type);
}

void __fastcall Adapters::SlopeRect(cITSNHoodTerrain *face, void *, const int32_t *rect, float angleDegrees, int32_t passes, float strength, uint32_t lockLots)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SlopeRect(rect, angleDegrees, passes, strength, lockLots);
}

void __fastcall Adapters::ImportHeights(cITSNHoodTerrain *face, void *, void *source)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::ImportHeights(source);
}

void __fastcall Adapters::RaiseRect(cITSNHoodTerrain *face, void *, const uint32_t *rect, float delta)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::RaiseRect(rect, delta);
}

void __fastcall Adapters::SetRectHeight(cITSNHoodTerrain *face, void *, const uint32_t *rect, float height)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetRectHeight(rect, height);
}

bool __fastcall Adapters::DoMessage(cIGZMessageTarget2 *face, void *, void *message)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::DoMessage(message);
}
