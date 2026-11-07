// The memory-texture routines of cImageManipDX (lane OW): src/gz/cImageManipDXMemory.h.
#include "gz/cImageManipDXMemory.h"

#include <algorithm>
#include <cstring>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/DxtBlocks.h"
#include "gz/GraphicServices.h"
#include "gz/MipKernels.h"
#include "gz/cImageManipDX.h"
#include "gz/cIGZUnknown.h"
#include "gz/cMemBlock.h"
#include "ifaces/cILockableSurface.h"
#include "ifaces/cIPixelFormat.h"
#include "ifaces/cIPixelFormatARGB.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "ifaces/cITexture.h"

namespace nGZGraphic4::memchain {

namespace {

using ifaces::cILockableSurface;
using ifaces::cIPixelFormat;
using ifaces::cIPixelFormatARGB;
using ifaces::cIPixelFormatsManager;
using ifaces::cITexture;

constexpr uint32_t kIidPixelFormatARGB = 0x9ac38991;

// A locked level (the engine's lock record, 0x24 bytes): cILockableSurface::Lock fills it and keeps the surface counted
// in it until the unlock. Destroyed it unlocks and lets the surface go, as the original's record does at the end of
// every routine (the records are destroyed in the reverse order of their construction).
struct LockedLevel {
    void *surface = nullptr; // +0x00 (counted)
    RECT rect{};             // +0x04
    uint8_t *bits = nullptr; // +0x14
    int32_t pitch = 0;       // +0x18
    uint32_t rowBytes = 0;   // +0x1c
    uint32_t rows = 0;       // +0x20

    LockedLevel() = default;
    LockedLevel(const LockedLevel &) = delete;
    LockedLevel &operator=(const LockedLevel &) = delete;
    ~LockedLevel() { Release(); }
    void Lock(void *lockable, bool readOnly) { As<cILockableSurface>(lockable)->Lock(this, readOnly); }

private:
    void Release()
    {
        if (!surface) return;
        As<cILockableSurface>(surface)->Unlock(this);
        if (void *const held = surface) {
            surface = nullptr;
            static_cast<cIGZUnknown *>(held)->Release();
        }
        bits = nullptr;
        rowBytes = 0;
        pitch = 0;
        rows = 0;
    }
};
static_assert(sizeof(LockedLevel) == 0x24, "lock record layout");

// nRZGraphic4::cMemBlockAsSurface (ours, src/gz/cMemBlock.h) on the stack: a surface over caller-owned memory, built
// and torn down through its hooked entry points (the tests stand in for them).
struct MemorySurface {
    alignas(nRZGraphic4::cMemBlockAsSurface) unsigned char storage[sizeof(nRZGraphic4::cMemBlockAsSurface)];

    MemorySurface(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch)
    {
        nRZGraphic4::cMemBlockAsSurface::Construct(storage, nullptr, bits, format, width, height, pitch);
        ++Get()->mRefs; // the AddRef 0x148fb0
    }
    ~MemorySurface() { nRZGraphic4::cMemBlockAsSurface::Destruct(Get(), nullptr); }
    MemorySurface(const MemorySurface &) = delete;
    MemorySurface &operator=(const MemorySurface &) = delete;
    nRZGraphic4::cMemBlockAsSurface *Get() { return reinterpret_cast<nRZGraphic4::cMemBlockAsSurface *>(storage); }
};
static_assert(sizeof(MemorySurface) == 12, "cMemBlockAsSurface layout");

uint32_t FormatIdOf(void *texture)
{
    return As<cIPixelFormat>(As<cITexture>(texture)->GetPixelFormat())->GetFormatId();
}

} // namespace

void __cdecl CopyLevel(void *target, void *source)
{
    LockedLevel to, from;
    to.Lock(target, false);
    from.Lock(source, true);
    uint8_t *destination = to.bits;
    const uint8_t *origin = from.bits;
    for (uint32_t row = to.rows; row != 0; --row) {
        std::memcpy(destination, origin, to.rowBytes);
        destination += to.pitch;
        origin += from.pitch;
    }
}

void __cdecl ConvertRgbToArgb(void *target, void *source)
{
    LockedLevel to, from;
    to.Lock(target, false);
    from.Lock(source, true);
    const uint32_t width = As<cILockableSurface>(target)->GetWidth();
    uint8_t *destination = to.bits;
    const uint8_t *origin = from.bits;
    for (uint32_t row = to.rows; row != 0; --row) {
        uint8_t *out = destination;
        const uint8_t *in = origin;
        for (uint32_t x = width; x != 0; --x) {
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            out[3] = 0xff;
            out += 4;
            in += 3;
        }
        destination += to.pitch;
        origin += from.pitch;
    }
}

LevelConverter __cdecl ChooseConverter(void *targetTexture, void *sourceTexture, bool allowConversion)
{
    void *const targetFormat = As<cITexture>(targetTexture)->GetPixelFormat();
    void *const sourceFormat = As<cITexture>(sourceTexture)->GetPixelFormat();
    const uint32_t sourceId = As<cIPixelFormat>(sourceFormat)->GetFormatId();
    const uint32_t targetId = As<cIPixelFormat>(targetFormat)->GetFormatId();
    if (targetId == sourceId) return &CopyLevel;
    if (!allowConversion) return nullptr;

    // Both formats must answer for the ARGB interface; the answers are let go at once (the formats are static objects).
    void *targetArgb = nullptr;
    static_cast<cIGZUnknown *>(targetFormat)->QueryInterface(kIidPixelFormatARGB, &targetArgb);
    if (targetArgb) static_cast<cIGZUnknown *>(targetArgb)->Release();
    void *sourceArgb = nullptr;
    static_cast<cIGZUnknown *>(sourceFormat)->QueryInterface(kIidPixelFormatARGB, &sourceArgb);
    if (!sourceArgb) return nullptr;
    static_cast<cIGZUnknown *>(sourceArgb)->Release();
    if (!targetArgb) return nullptr;

    cIPixelFormatARGB *const from = As<cIPixelFormatARGB>(sourceArgb);
    cIPixelFormatARGB *const to = As<cIPixelFormatARGB>(targetArgb);
    const uint32_t fromRed = from->GetRedBits(), fromGreen = from->GetGreenBits(), fromBlue = from->GetBlueBits();
    const uint32_t fromAlpha = from->GetAlphaBits(), fromBits = from->GetBitsPerPixel();
    const uint32_t toRed = to->GetRedBits(), toGreen = to->GetGreenBits(), toBlue = to->GetBlueBits();
    to->GetAlphaBits(); // asked, not compared
    const uint32_t toBits = to->GetBitsPerPixel();
    if (fromRed == 8 && fromGreen == 8 && fromBlue == 8 && fromAlpha == 0 && fromBits == 0x18 && toRed == 8 &&
        toGreen == 8 && toBlue == 8 && toBits == 0x20)
        return &ConvertRgbToArgb;
    return nullptr;
}

namespace {

// The compressed level decoders: the target gets the whole source decoded, the sizes are the source's.
template <void(__cdecl *Decode)(uint8_t *, int, const uint8_t *, int, int, int)>
void DecodeLevel(void *target, void *source)
{
    LockedLevel to, from;
    to.Lock(target, false);
    from.Lock(source, true);
    const uint32_t width = As<cILockableSurface>(source)->GetWidth();
    const uint32_t height = As<cILockableSurface>(source)->GetHeight();
    Decode(to.bits, to.pitch, from.bits, from.pitch, int((width + 3) >> 2), int((height + 3) >> 2));
}

} // namespace

void __cdecl DecodeDxt1Level(void *target, void *source) { DecodeLevel<&dxt::DecodeDxt1>(target, source); }
void __cdecl DecodeDxt3Level(void *target, void *source) { DecodeLevel<&dxt::DecodeDxt3>(target, source); }
void __cdecl DecodeDxt5Level(void *target, void *source) { DecodeLevel<&dxt::DecodeDxt5>(target, source); }

void __cdecl ReduceLevelTwoTap(void *target, int outWidth, int outRows, void *source, int sourceWidth, int sourceRows,
                               uint8_t *scratch, int rowBytes, RowKernel reducer, RowKernel combiner)
{
    LockedLevel to, from;
    to.Lock(target, false);
    from.Lock(source, true);
    uint8_t *destination = to.bits;
    const uint8_t *origin = from.bits;
    // The two scratch rows; the window the combiner sees lists the second one first.
    uint8_t *window[3] = {scratch, scratch + outWidth * rowBytes, scratch};
    auto fill = [&](uint8_t *row) {
        if (sourceWidth == 1)
            std::memcpy(row, origin, rowBytes);
        else
            reducer(row, origin, outWidth);
    };
    if (sourceRows == 1) {
        if (sourceWidth == 1)
            std::memcpy(destination, origin, rowBytes);
        else
            reducer(destination, origin, outWidth);
        return;
    }
    uint32_t slot = 0;
    do {
        for (int pair = 2; pair != 0; --pair) {
            fill(window[slot + 1]);
            origin += from.pitch;
            slot ^= 1;
        }
        combiner(destination, &window[slot], outWidth);
        destination += to.pitch;
        --outRows;
    } while (outRows != 0);
}

void __cdecl ReduceLevelFourTap(void *target, int outWidth, int outRows, void *source, int sourceWidth,
                                uint32_t sourceRows, uint8_t *scratch, int rowBytes, RowKernel reducer,
                                RowKernel combiner)
{
    LockedLevel to, from;
    to.Lock(target, false);
    from.Lock(source, true);
    uint8_t *destination = to.bits;
    const int32_t rowSpan = outWidth * rowBytes;
    // Four scratch rows used as a ring; the list repeats its first three so any four in a row are contiguous.
    uint8_t *ring[7] = {scratch,          scratch + rowSpan, scratch + 2 * rowSpan, scratch + 3 * rowSpan,
                        scratch,          scratch + rowSpan, scratch + 2 * rowSpan};
    uint32_t position = 0;
    int32_t next = -1; // the next source row (unmirrored)
    int32_t wanted = 1;
    do {
        wanted += 2;
        while (next < wanted) {
            uint32_t row = static_cast<uint32_t>(next);
            ++next;
            if (sourceRows == 1) {
                row = 0;
            } else {
                for (;;) {
                    while (static_cast<int32_t>(row) < 0) row = 0u - row;
                    if (row < sourceRows) break;
                    row = sourceRows * 2 - row - 2;
                }
            }
            const uint8_t *const line = from.bits + int32_t(row) * from.pitch;
            if (sourceWidth == 1)
                std::memcpy(ring[position], line, rowBytes);
            else
                reducer(ring[position], line, outWidth);
            position = (position + 1) & 3;
        }
        combiner(destination, &ring[position], outWidth);
        destination += to.pitch;
        --outRows;
    } while (outRows != 0);
}

void __cdecl ComputeNormalMapLevel(uint8_t *target, int targetPitch, const uint8_t *source, int sourcePitch, int width,
                                   int height, float amplitude)
{
    const float scale = 1020.0f / amplitude;
    const float scaleSquared = scale * scale;
    const uint8_t *current = source; // the row being made; rows above and below wrap
    for (int y = 0; y < height; ++y) {
        const uint8_t *const above = y == 0 ? source + (height - 1) * sourcePitch : current - sourcePitch;
        const uint8_t *const below = y < height - 1 ? current + sourcePitch : source;
        current += sourcePitch;
        for (int x = 0; x < width; ++x) {
            const int left = x == 0 ? width - 1 : x - 1;
            const int right = x == width - 1 ? 0 : x + 1;
            const int first = int(above[left]) - int(below[right]);
            const int second = int(above[right]) - int(below[left]);
            const int dx = first - second;
            const int dy = first + second;
            const float length = float(dy * dy + dx * dx) + scaleSquared;
            // The reciprocal square root estimate by the integer trick, one Newton step, times 63.75.
            int32_t lengthBits;
            std::memcpy(&lengthBits, &length, 4);
            const int32_t estimateBits = 0x5f400000 - (lengthBits >> 1);
            float estimate;
            std::memcpy(&estimate, &estimateBits, 4);
            const float factor = (3.0f - estimate * estimate * length) * estimate * 63.75f;
            // Adding 1.5 * 2^23 rounds to an integer in the low mantissa bits.
            const float bias = 12582912.0f;
            const float redValue = float(dx) * factor + bias;
            const float greenValue = float(dy) * factor + bias;
            const float blueValue = factor * scale + bias;
            uint32_t red, green, blue;
            std::memcpy(&red, &redValue, 4);
            std::memcpy(&green, &greenValue, 4);
            std::memcpy(&blue, &blueValue, 4);
            red += 0xb4c00080;
            green += 0xb4c00080;
            blue += 0xb4c00080;
            if (red > 0xff) red = uint32_t(int32_t(~red) >> 31) & 0xff;
            if (green > 0xff) green = uint32_t(int32_t(~green) >> 31) & 0xff;
            if (blue > 0xff) blue = uint32_t(int32_t(~blue) >> 31) & 0xff;
            const uint32_t pixel = ((red + 0xff00) * 0x100 + green) * 0x100 + blue;
            std::memcpy(target + x * 4, &pixel, 4);
        }
        target += targetPitch;
    }
}

bool __cdecl ComputeMemoryNormalMap(void *source, void *target, float amplitude)
{
    cIPixelFormatsManager *const formats = As<cIPixelFormatsManager>(GetPixelFormatsManagerCached());
    void *const x8r8g8b8 = formats->GetARGB(0x20, 0, 8, 8, 8);
    void *const a8r8g8b8 = formats->GetARGB(0x20, 8, 8, 8, 8);
    void *const luminance = formats->GetAlphaLuminance(8, 0, 8);
    const uint32_t sourceId = FormatIdOf(source);
    const uint32_t targetId = FormatIdOf(target);
    const uint32_t width = As<cITexture>(source)->GetWidth(0);
    const uint32_t height = As<cITexture>(source)->GetHeight(0);
    const int32_t levels = int32_t(As<cITexture>(source)->GetLevelCount());
    if (sourceId != As<cIPixelFormat>(luminance)->GetFormatId()) return false;
    if (targetId != As<cIPixelFormat>(x8r8g8b8)->GetFormatId() && targetId != As<cIPixelFormat>(a8r8g8b8)->GetFormatId())
        return false;
    if (As<cITexture>(target)->GetWidth(0) != width) return false;
    if (As<cITexture>(target)->GetHeight(0) != height) return false;
    if (As<cITexture>(target)->GetLevelCount() != uint32_t(levels)) return false;
    for (int32_t level = 0; level < levels; ++level) {
        void *const sourceLevel = As<cITexture>(source)->GetLockableSurface(uint32_t(level));
        void *const targetLevel = As<cITexture>(target)->GetTargetLockableSurface(uint32_t(level));
        const uint32_t levelWidth = As<cILockableSurface>(targetLevel)->GetWidth();
        const uint32_t levelHeight = As<cILockableSurface>(targetLevel)->GetHeight();
        LockedLevel from, to;
        from.Lock(sourceLevel, true);
        to.Lock(targetLevel, false);
        ComputeNormalMapLevel(to.bits, to.pitch, from.bits, from.pitch, int(levelWidth), int(levelHeight), amplitude);
    }
    return true;
}

bool __cdecl CopyMemoryTextures(void *source, void *target, bool filtered)
{
    cIPixelFormatsManager *const formats = As<cIPixelFormatsManager>(GetPixelFormatsManagerCached());
    void *const x8r8g8b8 = formats->GetARGB(0x20, 0, 8, 8, 8);
    void *const a8r8g8b8 = formats->GetARGB(0x20, 8, 8, 8, 8);
    void *const a2r10g10b10 = formats->GetARGB(0x20, 2, 10, 10, 10);
    void *const a8 = formats->GetARGB(8, 8, 0, 0, 0);
    void *const luminance = formats->GetAlphaLuminance(8, 0, 8);
    void *const dxt1 = formats->GetDXT(1);
    void *const dxt3 = formats->GetDXT(3);
    void *const dxt5 = formats->GetDXT(5);
    const uint32_t sourceId = FormatIdOf(source);
    const uint32_t targetId = FormatIdOf(target);
    auto idOf = [](void *format) { return As<cIPixelFormat>(format)->GetFormatId(); };

    // Which of the source's levels has the target's top size.
    uint32_t sourceLevel = 0;
    uint32_t targetLevel = 0;
    uint32_t halvings = 0;
    const uint32_t sourceLevels = As<cITexture>(source)->GetLevelCount();
    const uint32_t targetLevels = As<cITexture>(target)->GetLevelCount();
    const uint32_t topWidth = As<cITexture>(target)->GetWidth(0);
    const uint32_t topHeight = As<cITexture>(target)->GetHeight(0);
    // These two hold the size of the source level the new levels continue from.
    uint32_t width, height;
    if (sourceLevels != 0) {
        do {
            width = As<cITexture>(source)->GetWidth(sourceLevel);
            height = As<cITexture>(source)->GetHeight(sourceLevel);
            if (width == topWidth && height == topHeight) break;
            ++sourceLevel;
        } while (sourceLevel < sourceLevels);
    } else {
        // ORIGINAL-BUG: a source without levels takes the texture's own address for the size of its (missing) last level.
        width = height = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(source));
    }
    auto powerOfTwo = [](uint32_t value) { return value == 1 || (value & (value - 1)) == 0; };
    if (!powerOfTwo(width) || !powerOfTwo(height)) return false;

    uint32_t continueLevel; // the level of `from` the further levels are made from
    void *from = source;
    if (sourceLevel < sourceLevels) {
        if (source == target) {
            continueLevel = 0;
            targetLevel = 1;
        } else {
            const uint32_t remaining = sourceLevels - sourceLevel;
            const LevelConverter convert = ChooseConverter(target, source, remaining >= targetLevels);
            if (!convert) return false;
            while (targetLevel < targetLevels) {
                void *const sourceSurface = As<cITexture>(source)->GetLockableSurface(sourceLevel);
                void *const targetSurface = As<cITexture>(target)->GetTargetLockableSurface(targetLevel);
                convert(targetSurface, sourceSurface);
                ++sourceLevel;
                ++targetLevel;
                if (remaining < targetLevels || sourceLevel >= sourceLevels) break;
            }
            from = target;
            continueLevel = targetLevel - 1;
        }
        if (targetLevel >= targetLevels) return true;
        // The size of the level the new ones continue from.
        width = As<cITexture>(from)->GetWidth(continueLevel);
        height = As<cITexture>(from)->GetHeight(continueLevel);
    } else {
        continueLevel = sourceLevels - 1;
        uint32_t w = width, h = height;
        do {
            if (w < topWidth || h < topHeight) return false;
            ++halvings;
            w = (w + 1) >> 1;
            h = (h + 1) >> 1;
        } while (w != topWidth || h != topHeight);
    }
    if (sourceId != targetId) return false;

    uint32_t outWidth = (width + 1) >> 1;
    uint32_t outHeight = (height + 1) >> 1;
    uint32_t sourceWidth = width, sourceHeight = height;

    // The pixel layout of the source's format: the byte size, the row reducer and combiner, whether a compressed level
    // has to be decoded first.
    int pixelBytes;
    RowKernel reducer, combiner;
    bool decode = false;
    const uint32_t argbId = idOf(a8r8g8b8);
    if (sourceId == argbId || sourceId == idOf(x8r8g8b8)) {
        pixelBytes = 4;
        reducer = reinterpret_cast<RowKernel>(filtered ? &mip::FilterRow8888 : &mip::HalveRow8888);
        combiner = reinterpret_cast<RowKernel>(filtered ? &mip::CombineFourRows8888 : &mip::CombineTwoRows8888);
    } else if (sourceId == idOf(a2r10g10b10)) {
        pixelBytes = 4;
        reducer = reinterpret_cast<RowKernel>(filtered ? &mip::FilterRow2101010 : &mip::HalveRow2101010);
        combiner = reinterpret_cast<RowKernel>(filtered ? &mip::CombineFourRows2101010 : &mip::CombineTwoRows2101010);
    } else if (sourceId == idOf(a8) || sourceId == idOf(luminance)) {
        pixelBytes = 1;
        reducer = reinterpret_cast<RowKernel>(filtered ? &mip::FilterRow8 : &mip::HalveRow8);
        combiner = reinterpret_cast<RowKernel>(filtered ? &mip::CombineFourRows8 : &mip::CombineTwoRows8);
    } else if (sourceId == idOf(dxt1) || sourceId == idOf(dxt3) || sourceId == idOf(dxt5)) {
        pixelBytes = 4;
        decode = true;
        reducer = reinterpret_cast<RowKernel>(filtered ? &mip::FilterRow8888 : &mip::HalveRow8888);
        combiner = reinterpret_cast<RowKernel>(filtered ? &mip::CombineFourRows8888 : &mip::CombineTwoRows8888);
    } else {
        return false;
    }
    auto reduce = [&](void *to, uint32_t toWidth, uint32_t toHeight, void *fromSurface, uint32_t fromWidth,
                      uint32_t fromHeight, uint8_t *scratch) {
        if (filtered)
            ReduceLevelFourTap(to, int(toWidth), int(toHeight), fromSurface, int(fromWidth), fromHeight, scratch,
                               pixelBytes, reducer, combiner);
        else
            ReduceLevelTwoTap(to, int(toWidth), int(toHeight), fromSurface, int(fromWidth), int(fromHeight), scratch,
                              pixelBytes, reducer, combiner);
    };

    if (halvings < 2 && !decode) { // compressed levels always take the long way (they are decoded first)
        // One step from the level the new ones continue from: each new level from the one before.
        std::vector<uint8_t> scratch(size_t(pixelBytes) * outWidth * 4);
        void *current = As<cITexture>(from)->GetLockableSurface(continueLevel);
        while (targetLevel < targetLevels) {
            void *const level = As<cITexture>(target)->GetTargetLockableSurface(targetLevel);
            if (level != current) reduce(level, outWidth, outHeight, current, sourceWidth, sourceHeight, scratch.data());
            ++targetLevel;
            sourceWidth = outWidth;
            sourceHeight = outHeight;
            outWidth = (outWidth + 1) >> 1;
            outHeight = (outHeight + 1) >> 1;
            current = level;
        }
        return true;
    }

    // Several halvings from the last source level to the target's top, or compressed levels: two memory images (the
    // level at 4 x 4 at least, and its half) alternate as the intermediate steps.
    void *const imageManip = GetImageManipCached();
    const int32_t sourceWidthSigned = int32_t(sourceWidth), sourceHeightSigned = int32_t(sourceHeight);
    const int32_t firstWidth = std::max(4, sourceWidthSigned);
    const int32_t firstHeight = std::max(4, sourceHeightSigned);
    const uint32_t secondWidth = std::max(4u, outWidth);
    const uint32_t secondHeight = std::max(4u, outHeight);
    const uint32_t firstPixels = uint32_t(firstHeight) * uint32_t(firstWidth);
    const uint32_t totalPixels = secondHeight * secondWidth + firstPixels;
    std::vector<uint8_t> memory(size_t(totalPixels + outWidth * 4) * uint32_t(pixelBytes));
    MemorySurface first(memory.data(), a8r8g8b8, uint32_t(firstWidth), uint32_t(firstHeight),
                        uint32_t(firstWidth) * uint32_t(pixelBytes));
    MemorySurface second(memory.data() + size_t(pixelBytes) * firstPixels, a8r8g8b8, secondWidth, secondHeight,
                         secondWidth * uint32_t(pixelBytes));
    uint8_t *const scratch = memory.data() + size_t(totalPixels) * uint32_t(pixelBytes);

    void *const start = As<cITexture>(from)->GetLockableSurface(continueLevel);
    void *current = start;
    if (decode) {
        if (sourceId == idOf(dxt1))
            DecodeDxt1Level(&first, start);
        else if (sourceId == idOf(dxt3))
            DecodeDxt3Level(&first, start);
        else if (sourceId == idOf(dxt5))
            DecodeDxt5Level(&first, start);
        current = &first;
    }
    bool toggle = true;
    while (targetLevel < targetLevels) {
        void *const level = As<cITexture>(target)->GetTargetLockableSurface(targetLevel);
        void *matching = nullptr;
        const uint32_t levelWidth = As<cILockableSurface>(level)->GetWidth();
        const uint32_t levelHeight = As<cILockableSurface>(level)->GetHeight();
        if (levelWidth == outWidth && levelHeight == outHeight) {
            matching = level;
            ++targetLevel;
        }
        void *destination = matching;
        if (decode || !matching) {
            toggle = !toggle;
            destination = toggle ? static_cast<void *>(&first) : static_cast<void *>(&second);
        }
        reduce(destination, outWidth, outHeight, current, sourceWidth, sourceHeight, scratch);
        if (matching && matching != destination) {
            const RECT rect = {0, 0, LONG(outWidth), LONG(outHeight)};
            const uint32_t colorKey = 0;
            As<cIImageManip>(imageManip)->CopySurfaceRect(destination, &rect, matching, &rect, &colorKey);
        }
        sourceWidth = outWidth;
        sourceHeight = outHeight;
        outWidth = (outWidth + 1) >> 1;
        outHeight = (outHeight + 1) >> 1;
        current = destination;
    }
    return true;
}

} // namespace nGZGraphic4::memchain
