// The footprint of a cTSObject: slot 65 (0x5426a0) and the functions that build the rectangles it turns (field map:
// src/ts/cTSObject.h, the cache layout: src/ts/cTSObjectFootprint.h).
#include "ts/cTSObjectFootprint.h"
#include "game/game.h"
#include "game/original.h"
#include "ifaces/AttributeTable.h"
#include "ifaces/cTSObject.h"
#include "rz/cGZAllocatorService.h"
#include "ts/cTSObject.h"
#include "ts/cTSObjectHeld.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace tsobject_held;

namespace {

// The object's base rectangle list (+0x1e4): the cache the base footprint is built into.
constexpr intptr_t kBaseFootprintOffset = 0x1e4;
constexpr intptr_t kFootprintOffset = 0x200;
constexpr intptr_t kBaseBuiltOffset = 0x293;

constexpr float kGrid = 0.125f;         // the snapping grid (game constant 0xda74f4)
constexpr float kGridMargin = 0.01f;    // kept inside the snapped edge (0xda74e8)
constexpr float kNarrowMargin = 0.02f;  // 0xda74ec
constexpr float kNarrowLimit = 0.0625f; // 0xda7698
constexpr float kPerCell = 8.0f;        // 1 / kGrid (0xdb5d18)
constexpr float kHalf = 0.5f;           // 0xd9c694
constexpr float kMarginStep = 0.0625f;  // one step of a margin nibble (0xda7698)
constexpr float kFloatMax = 3.4028235e+38f; // 0xda7660; its negative is 0xda1eb8
constexpr float kRoundingShift = 12582912.0f; // 1.5 * 2^23 (0xd9ff00): adding it leaves the rounded integer in the low mantissa bits

// SSE maxss/minss with the destination first: the second operand wins unless the first is strictly larger/smaller (NaN and ties
// give the second).
float MaxSS(float a, float b)
{
    return a > b ? a : b;
}
float MinSS(float a, float b)
{
    return a < b ? a : b;
}

// The game's floor and ceil take a double and give the float back (the value is converted to double and back).
float FloorF(float x)
{
    return static_cast<float>(std::floor(static_cast<double>(x)));
}
float CeilF(float x)
{
    return static_cast<float>(std::ceil(static_cast<double>(x)));
}

// One axis: the interval [low, high] snapped inwards to the 1/8 grid with the margin, never leaving the interval; one that ends
// up narrower than 1/16 (from a real interval, low < high) is replaced by one of 0.105 at the snapped low edge.
void SnapInterval(float low, float high, float *snappedLow, float *snappedHigh)
{
    const float scaledLow = low * kPerCell + kHalf;
    const float lowEdge = FloorF(scaledLow) * kGrid + kGridMargin;
    const float scaledHigh = high * kPerCell - kHalf;
    const float highEdge = CeilF(scaledHigh) * kGrid - kGridMargin;
    const float left = MaxSS(lowEdge, low);
    float right = MinSS(highEdge, high);
    if (high > low && kNarrowLimit > right - left) right = (left + kGrid) - kNarrowMargin;
    *snappedLow = left;
    *snappedHigh = right;
}

// Empties the cache and resets its extent.
void ClearFootprint(FootprintCache *cache)
{
    if (cache->mBegin != cache->mEnd) cache->mEnd = cache->mBegin;
    cache->mMinX = kFloatMax; // 0x7f7fffff
    cache->mMinY = kFloatMax;
    cache->mMaxX = -kFloatMax; // 0xff7fffff
    cache->mMaxY = -kFloatMax;
}

// Stores a rectangle at the end of the cache; when it is full (end == capacity) the vector's growing insert (std::vector's, 0x255f70,
// thiscall: vector, where = end, value) does it.
void PushRect(FootprintCache *cache, float x1, float y1, float x2, float y2)
{
    using GrowFn = void(__thiscall *)(void *, float *, const float *);
    const float rect[4] = {x1, y1, x2, y2};
    if (cache->mEnd != cache->mCapacity) {
        cache->mEnd[0] = x1;
        cache->mEnd[1] = y1;
        cache->mEnd[2] = x2;
        cache->mEnd[3] = y2;
        cache->mEnd += 4;
    } else {
        OriginalFunction<GrowFn, 0x255f70>()(cache, cache->mEnd, rect);
    }
}

// One rectangle of the base footprint, turned by (*notch & 7) (0/1 none, then a quarter turn per pair), moved to the position,
// snapped, counted into the extent (strict compares: NaN never counts) and stored.
void AppendSnappedRect(FootprintCache *cache, const float *position, const uint32_t *notch, const float *rect)
{
    const float x = position[0];
    const float y = position[1];
    float x1, x2, y1, y2;
    switch (*notch & 7) {
    default: // 0, 1
        y1 = y + rect[1];
        y2 = y + rect[3];
        x2 = x + rect[2];
        x1 = x + rect[0];
        break;
    case 2:
    case 3:
        y1 = y + rect[0];
        y2 = y + rect[2];
        x2 = x - rect[1];
        x1 = x - rect[3];
        break;
    case 4:
    case 5:
        y1 = y - rect[3];
        y2 = y - rect[1];
        x2 = x - rect[0];
        x1 = x - rect[2];
        break;
    case 6:
    case 7:
        y1 = y - rect[2];
        y2 = y - rect[0];
        x2 = x + rect[3];
        x1 = x + rect[1];
        break;
    }
    float left, right, bottom, top;
    SnapInterval(x1, x2, &left, &right);
    SnapInterval(y1, y2, &bottom, &top);

    if (cache->mMinX > left) cache->mMinX = left;
    if (cache->mMinY > bottom) cache->mMinY = bottom;
    if (right > cache->mMaxX) cache->mMaxX = right;
    if (top > cache->mMaxY) cache->mMaxY = top;
    PushRect(cache, left, bottom, right, top);
}

// A footprint cache a call fills in (the rectangle vector, then the extent, which starts as (1, 1, -1, -1): 0xdc8800): the
// storage goes back through the allocator service when the frame ends, normally or by an exception.
struct RectBuffer {
    float *mBegin = nullptr;
    float *mEnd = nullptr;
    float *mCapacity = nullptr;
    float mMinX = 1.0f;
    float mMinY = 1.0f;
    float mMaxX = -1.0f;
    float mMaxY = -1.0f;

    RectBuffer() = default;
    RectBuffer(const RectBuffer &) = delete;
    RectBuffer &operator=(const RectBuffer &) = delete;
    ~RectBuffer() { Release(); }
    void Release()
    {
        if (!mBegin) return;
        FreeThroughAllocatorService(mBegin, static_cast<uint32_t>((reinterpret_cast<intptr_t>(mCapacity) - reinterpret_cast<intptr_t>(mBegin)) >> 4));
        mBegin = mEnd = mCapacity = nullptr;
    }
};
static_assert(sizeof(RectBuffer) == sizeof(FootprintCache), "slot 183 fills a whole footprint cache");

// A tile coordinate the way the game rounds a float position: add 0.5, then the 1.5 * 2^23 trick (the integer is in the low bits
// of the float), then one more if that next integer equals the shifted value. `bits` gets the float's bit pattern of the trick.
int32_t TileOfCoordinate(float value, uint32_t *bits)
{
    const float shifted = value + kHalf;
    const float tricked = (shifted - kHalf) + kRoundingShift;
    uint32_t pattern;
    std::memcpy(&pattern, &tricked, sizeof(pattern));
    *bits = pattern;
    int32_t tile = static_cast<int32_t>(pattern + 0xb4c00000u); // pattern - 0x4b400000
    const int32_t next = tile + 1;
    if (static_cast<float>(next) == shifted) tile = next;
    return tile;
}

float NibbleSteps(uint32_t packed, int nibble)
{
    return static_cast<float>(static_cast<int32_t>((packed >> (nibble * 4)) & 0xf)) * kMarginStep;
}

} // namespace

float __cdecl FootprintSize(cTSObject *object)
{
    ifaces::cTSObject *const self = As<ifaces::cTSObject>(object);
    float size = 1.0f;
    if (self->ObjectKind() == 2) return 0.2f; // 0x3e4ccccd
    ifaces::AttributeTable *const table = self->GetAttributeTable();
    HeldRef held; // the table is counted for the length of the lookup
    if (table) {
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(table);
        held.Adopt(table);
    }
    const uint32_t steps = table->GetFlags(0x43); // the table is not checked for null (as in the original)
    if (steps != 0) size = static_cast<float>(static_cast<int32_t>(steps)) * kMarginStep;
    return size;
}

int32_t __cdecl FacingPointerToNotch(const int32_t *facing)
{
    return static_cast<int32_t>(10u - static_cast<uint32_t>(*facing)) % 8;
}

void __cdecl BuildFootprint(cTSObject *object, const float *position, const uint32_t *notch, FootprintCache *cache)
{
    using TwoArgs = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    using ReadFn = uint32_t(__thiscall *)(void *, uint32_t);
    using OriginFn = void(__thiscall *)(void *, float *);
    using RectsFn = uint8_t(__thiscall *)(void *, int32_t, int32_t, RectBuffer *, uint32_t, uint32_t);
    constexpr uint32_t kMarginAttribute = 0x4d;
    constexpr uint32_t kPersonInterface = 0x5a69d3d5;
    constexpr uint32_t kPersonMarginEntry = 0x49;
    constexpr int32_t kPersonKind = 2;

    ifaces::cTSObject *const self = As<ifaces::cTSObject>(object);
    // The footprint square (x1, y1, x2, y2) around the position.
    const float half = FootprintSize(object) * kHalf;
    const float x = position[0];
    const float tile[4] = {x - half, position[1] - half, x + half, position[1] + half};

    // The margins (left, bottom, right, top) of the attribute table's entry 0x4d: four nibbles of 1/16.
    float margin[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    void *const table = self->GetAttributeTable();
    const uint32_t packed = static_cast<uint16_t>(VirtualCall<ReadFn, 5>(table, kMarginAttribute));
    if (packed != 0)
        for (int i = 0; i < 4; ++i) margin[i] = NibbleSteps(packed, i);

    if (self->ObjectKind() == kPersonKind) {
        // A person with the helper interface takes its own margins off (not for odd notches).
        HeldRef helper;
        if (self->QueryInterface(kPersonInterface, helper.Out()) != 0 && (*notch & 1) == 0) {
            void *const iface = helper.Get();
            const uint32_t entry = static_cast<uint16_t>(VirtualCall<TwoArgs, 10>(iface, kPersonMarginEntry, 1));
            if (entry != 0)
                for (int i = 0; i < 4; ++i) margin[i] = margin[i] - NibbleSteps(entry, i);
        }
    } else if (static_cast<int8_t>(self->GetAttribute(8)) >= 0) {
        // The rectangles the object reports for its tile.
        float origin[3];
        VirtualCall<OriginFn, 85>(object, origin);
        uint32_t ignored;
        uint32_t yBits;
        const int32_t tileX = TileOfCoordinate(origin[0], &ignored);
        const int32_t tileY = TileOfCoordinate(origin[1], &yBits);
        const uint8_t level = static_cast<uint8_t>(VirtualCall<NoArgRet32, 107>(object));
        // The last argument is the dword the second coordinate's rounding left on the stack, with its low byte replaced.
        const uint32_t levelWord = (yBits & 0xffffff00u) | level;
        RectBuffer found;
        if (VirtualCall<RectsFn, 183>(object, tileX, tileY, &found, 0, levelWord) != 0) {
            ClearFootprint(cache);
            for (const float *rect = found.mBegin; rect != found.mEnd; rect += 4) AppendSnappedRect(cache, position, notch, rect);
            found.Release();
            return;
        }
        found.Release();
    }

    // One square: the position's footprint square, shrunk by the margins when any is non-zero (turned with the notch).
    float rect[4] = {tile[0], tile[1], tile[2], tile[3]};
    if (!(margin[0] == 0.0f) || !(margin[1] == 0.0f) || !(margin[2] == 0.0f) || !(margin[3] == 0.0f)) {
        const int32_t turns = (8 - static_cast<int16_t>(FacingPointerToNotch(reinterpret_cast<const int32_t *>(notch)))) / 2;
        rect[0] = tile[0] + margin[turns % 4];
        rect[3] = tile[3] - margin[(turns + 1) % 4];
        rect[2] = tile[2] - margin[(turns + 2) % 4];
        rect[1] = tile[1] + margin[(turns + 3) % 4];
    }
    ClearFootprint(cache);
    if (kFloatMax > rect[0]) cache->mMinX = rect[0];
    if (kFloatMax > rect[1]) cache->mMinY = rect[1];
    if (rect[2] > -kFloatMax) cache->mMaxX = rect[2];
    if (rect[3] > -kFloatMax) cache->mMaxY = rect[3];
    PushRect(cache, rect[0], rect[1], rect[2], rect[3]);
}

void *cTSObject::vf65(const float *position, const uint32_t *facingNotch)
{
    FootprintCache *const base = &At<FootprintCache>(this, kBaseFootprintOffset);
    if (At<uint8_t>(this, kBaseBuiltOffset) == 0) {
        // The base footprint: at position (0, 0, 0) and notch 0, into the cache at +0x1e4 (zeroed locals as the game passes them).
        float origin[3] = {};
        uint32_t notch = 0;
        BuildFootprint(this, origin, &notch, base);
        At<uint8_t>(this, kBaseBuiltOffset) = 1;
    }
    FootprintCache *const cache = &At<FootprintCache>(this, kFootprintOffset);
    ClearFootprint(cache);
    for (const float *rect = base->mBegin; rect != base->mEnd; rect += 4) AppendSnappedRect(cache, position, facingNotch, rect);
    return cache;
}
