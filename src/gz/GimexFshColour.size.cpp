// FSH's back end, the small leaves (lane MQ; src/gz/GimexFshLeaves.h, 0x88407b..0x885849): words by platform, the colour
// converters between colour words and B, G, R, A bytes, sizes and tiled addressing, and the helpers that pick a record type.
#include "gz/GimexFshLeaves.h"

namespace gimex {

namespace fsh {

namespace {

// ---- words ----
constexpr int32_t kPlatformBig5 = 5;
constexpr int32_t kPlatformBig7 = 7;
constexpr int32_t kPlatformBigF = 0xf;
constexpr int32_t kPlatformBig11 = 0x11;
constexpr int32_t kOneByte = 1;
constexpr int32_t kTwoBytes = 2;
constexpr int32_t kThreeBytes = 3;
constexpr int32_t kFourBytes = 4;

// ---- colours ----
constexpr uint32_t kFull = 0xff;
constexpr uint32_t kMax2 = 3;
constexpr uint32_t kHalf2 = 2;
constexpr uint32_t kMax3 = 7;
constexpr uint32_t kHalf3 = 4;
constexpr uint32_t kMax4 = 0xf;
constexpr uint32_t kHalf4 = 8;
constexpr uint32_t kMax5 = 0x1f;
constexpr uint32_t kHalf5 = 0x10;
constexpr uint32_t kMax6 = 0x3f;
constexpr uint32_t kHalf6 = 0x20;
constexpr uint32_t kMax10 = 0x3ff;
constexpr uint32_t kHalf10 = 0x200;
constexpr uint32_t kPackHalf = 0x80;             // rounding when scaling 0..255 down
constexpr uint32_t kMask3 = 0xfffffff8;          // keep the top 5 bits of a byte
constexpr uint32_t kAlphaBit1555 = 0x8000;
constexpr uint32_t kAlphaThresholdLow = 0x3f;
constexpr uint32_t kAlphaThresholdHigh = 0xbf;
constexpr uint32_t kAlphaThreshold3555 = 0xec;   // above it the 1555 form is used
constexpr uint32_t kAlpha1555Visible = 0xff;
constexpr uint32_t kAlpha1555Half = 0x80;
constexpr uint32_t kRedBlack1555 = 0x400;        // the opaque black the packer avoids (all zero means transparent)
constexpr uint32_t kForm3555Flag = 0x40000;      // sets bit 15 after the shift by 3
constexpr double kLumaRed = 0.299;
constexpr double kLumaGreen = 0.587;
constexpr double kLumaBlue = 0.114;
constexpr double kRoundUp = 0.5;
constexpr uint32_t kNibbleMask = 0xf;
constexpr uint32_t kHighNibbleMask = 0xf0;

// A field of `maximum` + 1 levels, scaled to 0..255 and rounded.
uint32_t Scale(uint32_t value, uint32_t maximum, uint32_t half)
{
    return (value * kFull + half) / maximum;
}

uint32_t Bytes(uint32_t blue, uint32_t green, uint32_t red, uint32_t alpha)
{
    return (blue & kFull) | (green & kFull) << 8 | (red & kFull) << 16 | (alpha & kFull) << 24;
}

// The scaled-down byte of a packer (levels * value, rounded).
uint32_t Reduce(uint32_t byte, uint32_t maximum)
{
    return (byte * maximum + kPackHalf) / kFull;
}

// A packer's alpha that keeps a visible alpha at least 1.
uint32_t ReduceAlpha(uint32_t byte, uint32_t maximum)
{
    uint32_t alpha = Reduce(byte, maximum);
    if (byte != 0 && alpha == 0) alpha = 1;
    return alpha;
}

double LumaOf(const uint8_t *bgra)
{
    return static_cast<double>(bgra[1]) * kLumaGreen + static_cast<double>(bgra[2]) * kLumaRed +
           static_cast<double>(bgra[0]) * kLumaBlue;
}

// ---- tiled addressing ----
constexpr int32_t kBits4 = 4;
constexpr int32_t kBits8 = 8;
constexpr int32_t kBits15 = 0xf;
constexpr int32_t kBits16 = 0x10;
constexpr int32_t kBits32 = 0x20;

// ---- sizes ----
constexpr uint32_t kRound8 = 7;
constexpr uint32_t kRound4 = 3;
constexpr uint32_t kRound32Blocks = 0x1f;
constexpr uint32_t kRound16Blocks = 0xf;

// ---- palettes ----
constexpr uint32_t kPaletteGroupMask = 0x1f;
constexpr uint32_t kPaletteSecondFrom = 8;
constexpr uint32_t kPaletteThirdFrom = 0x10;
constexpr uint32_t kPaletteSpan = 8;
constexpr uint32_t kPaletteUp = 8;
constexpr uint32_t kPaletteThirdTo = 0x18;

// ---- record types (palette types of FSH) ----
constexpr int32_t kType1555 = 0x2d;
constexpr int32_t kType565 = 0x29;
constexpr int32_t kType666 = 0x22;
constexpr int32_t kType888 = 0x24;
constexpr int32_t kType8888 = 0x2a;
constexpr int32_t kType8555 = 0x2e;
constexpr int32_t kType8565 = 0x2c;
constexpr int32_t kType88 = 0x30;
constexpr int32_t kType565Alt = 0x31;
constexpr int32_t kType3555 = 0x32;
constexpr int32_t kTypeSplit = 0x33;
constexpr int32_t kTypeArgb = 0x38;
constexpr int32_t kType1555Other = 0x39;
constexpr int32_t kType1555Swizzled = 0x20;
constexpr int32_t kType8888Swizzled = 0x21;

constexpr int32_t kStep5 = 5;
constexpr int32_t kStep6 = 6;
constexpr int32_t kStep7 = 7;
constexpr int32_t kStep8 = 8;

// ---- names ----
constexpr char kMipmapLetter = 'm';
constexpr char kMipmapSeparator = '_';
constexpr char kDigitLow = '0';
constexpr uint32_t kDigitCount = 10;
constexpr int32_t kShortestScanned = 5;
constexpr int32_t kScanBack = 3;

} // namespace

// 0x884193
bool __cdecl IsBigEndianPlatform(int32_t platform)
{
    if (platform != kPlatformBig5 && platform != kPlatformBig7 && platform != kPlatformBigF) {
        return platform == kPlatformBig11;
    }
    return true;
}

// 0x88407b
uint32_t __cdecl ReadWord(const void *bytes, int32_t width, int32_t platform)
{
    if (IsBigEndianPlatform(platform)) return ReadBigEndian(bytes, width);
    return ReadLittleEndian(bytes, width);
}

// 0x8840a3
void __cdecl PutLittleEndian(void *dest, uint32_t value, int32_t width)
{
    uint8_t *const bytes = static_cast<uint8_t *>(dest);
    if (width == kOneByte) {
        bytes[0] = static_cast<uint8_t>(value);
    } else if (width == kTwoBytes) {
        *reinterpret_cast<uint16_t *>(bytes) = static_cast<uint16_t>(value);
    } else if (width == kFourBytes) {
        *reinterpret_cast<uint32_t *>(bytes) = value;
    } else if (width == kThreeBytes) {
        bytes[0] = static_cast<uint8_t>(value);
        bytes[1] = static_cast<uint8_t>(value >> 8);
        bytes[2] = static_cast<uint8_t>(value >> 16);
    }
}

// 0x8840f5
void __cdecl PutBigEndian(void *dest, uint32_t value, int32_t width)
{
    uint8_t *const bytes = static_cast<uint8_t *>(dest);
    if (width == kOneByte) {
        bytes[0] = static_cast<uint8_t>(value);
    } else if (width == kTwoBytes) {
        bytes[0] = static_cast<uint8_t>(value >> 8);
        bytes[1] = static_cast<uint8_t>(value);
    } else if (width == kFourBytes) {
        bytes[0] = static_cast<uint8_t>(value >> 24);
        bytes[1] = static_cast<uint8_t>(value >> 16);
        bytes[2] = static_cast<uint8_t>(value >> 8);
        bytes[3] = static_cast<uint8_t>(value);
    } else if (width == kThreeBytes) {
        bytes[0] = static_cast<uint8_t>(value >> 16);
        bytes[1] = static_cast<uint8_t>(value >> 8);
        bytes[2] = static_cast<uint8_t>(value);
    }
}

// 0x884167
void __cdecl PutWord(void *dest, uint32_t value, int32_t width, int32_t platform)
{
    if (IsBigEndianPlatform(platform)) {
        PutBigEndian(dest, value, width);
    } else {
        PutLittleEndian(dest, value, width);
    }
}

// 0x8841b0
uint32_t __cdecl Colour2101010(uint32_t word)
{
    return Bytes(Scale(word & kMax10, kMax10, kHalf10), Scale(word >> 10 & kMax10, kMax10, kHalf10),
                 Scale(word >> 20 & kMax10, kMax10, kHalf10), Scale(word >> 30, kMax2, kHalf2));
}

// 0x88421e
uint32_t __cdecl Colour4444(uint32_t word)
{
    return Bytes(Scale(word & kMax4, kMax4, kHalf4), Scale(word >> 4 & kMax4, kMax4, kHalf4),
                 Scale(word >> 8 & kMax4, kMax4, kHalf4), Scale(word >> 12 & kMax4, kMax4, kHalf4));
}

// 0x884285
uint32_t __cdecl Colour565(uint32_t word)
{
    return Bytes(Scale(word & kMax5, kMax5, kHalf5), Scale(word >> 5 & kMax6, kMax6, kHalf6),
                 Scale(word >> 11 & kMax5, kMax5, kHalf5), kFull);
}

// 0x8842df
uint32_t __cdecl Colour1555(uint32_t word)
{
    return Bytes(Scale(word & kMax5, kMax5, kHalf5), Scale(word >> 5 & kMax5, kMax5, kHalf5),
                 Scale(word >> 10 & kMax5, kMax5, kHalf5), 0u - (word >> 15 & 1));
}

// 0x88433b
uint32_t __cdecl Colour655(uint32_t word)
{
    return Bytes(Scale(word & kMax5, kMax5, kHalf5), Scale(word >> 5 & kMax5, kMax5, kHalf5),
                 Scale(word >> 10 & kMax6, kMax6, kHalf6), kFull);
}

// 0x884393
uint32_t __cdecl Colour6666(uint32_t word)
{
    return Bytes(Scale(word & kMax6, kMax6, kHalf6), Scale(word >> 6 & kMax6, kMax6, kHalf6),
                 Scale(word >> 12 & kMax6, kMax6, kHalf6), Scale(word >> 18 & kMax6, kMax6, kHalf6));
}

// 0x8846b7
uint32_t __cdecl Colour3555(uint32_t word)
{
    if ((word & kAlphaBit1555) == 0) {
        return Bytes(Scale(word & kMax4, kMax4, kHalf4), Scale(word >> 4 & kMax4, kMax4, kHalf4),
                     Scale(word >> 8 & kMax4, kMax4, kHalf4), Scale(word >> 12 & kMax3, kMax3, kHalf3));
    }
    return Bytes(Scale(word & kMax5, kMax5, kHalf5), Scale(word >> 5 & kMax5, kMax5, kHalf5),
                 Scale(word >> 10 & kMax5, kMax5, kHalf5), kFull);
}

// 0x884767
uint32_t __cdecl Colour88(uint32_t word)
{
    const uint32_t grey = word & kFull;
    return Bytes(grey, grey, grey, word >> 8);
}

// 0x8849c7
uint32_t __cdecl Colour1555Alt(uint32_t word)
{
    uint32_t alpha;
    if ((word & kAlphaBit1555) == 0) {
        if (word == 0) return 0;
        alpha = kAlpha1555Visible;
    } else {
        alpha = kAlpha1555Half;
    }
    return Bytes(Scale(word >> 10 & kMax5, kMax5, kHalf5), Scale(word >> 5 & kMax5, kMax5, kHalf5),
                 Scale(word & kMax5, kMax5, kHalf5), alpha);
}

// 0x88551d
uint32_t __cdecl Colour1555Swizzled(uint32_t word)
{
    return Bytes(Scale(word >> 10 & kMax5, kMax5, kHalf5), Scale(word >> 5 & kMax5, kMax5, kHalf5),
                 Scale(word & kMax5, kMax5, kHalf5), 0u - (word >> 15 & 1));
}

// 0x8843fa
uint32_t __cdecl Pack2101010(const uint8_t *bgra)
{
    return Reduce(bgra[0], kMax10) |
           (((Reduce(bgra[3], kMax2) << 10 | Reduce(bgra[2], kMax10)) << 10 | Reduce(bgra[1], kMax10)) << 10);
}

// 0x88445c
uint32_t __cdecl Pack4444(const uint8_t *bgra)
{
    const uint32_t alpha = ReduceAlpha(bgra[3], kMax4);
    return Reduce(bgra[0], kMax4) |
           (((alpha << 4 | Reduce(bgra[2], kMax4)) << 4 | Reduce(bgra[1], kMax4)) << 4);
}

// 0x8844da
uint16_t __cdecl Pack1555(const uint8_t *bgra)
{
    return static_cast<uint16_t>((((bgra[2] >> 3) | (bgra[3] >> 7) << 5) << 5 | (bgra[1] >> 3)) << 5 | (bgra[0] >> 3));
}

// 0x884519
uint16_t __cdecl Pack565(const uint8_t *bgra)
{
    return static_cast<uint16_t>(((bgra[2] >> 3) << 6 | (bgra[1] >> 2)) << 5 | (bgra[0] >> 3));
}

// 0x884546
uint16_t __cdecl Pack655(const uint8_t *bgra)
{
    return static_cast<uint16_t>(((bgra[2] >> 2) << 5 | (bgra[1] >> 3)) << 5 | (bgra[0] >> 3));
}

// 0x884573
uint32_t __cdecl Pack6666(const uint8_t *bgra)
{
    const uint32_t alpha = ReduceAlpha(bgra[3], kMax6);
    return Reduce(bgra[0], kMax6) |
           (((alpha << 6 | Reduce(bgra[2], kMax6)) << 6 | Reduce(bgra[1], kMax6)) << 6);
}

// 0x884781
uint8_t __cdecl PackGrey44(const uint8_t *bgra)
{
    const double luma = LumaOf(bgra);
    uint8_t level = static_cast<uint8_t>(static_cast<int32_t>(luma));
    if (luma - static_cast<double>(static_cast<int32_t>(luma)) >= kRoundUp) ++level;
    return static_cast<uint8_t>((bgra[3] & kHighNibbleMask) | (level >> 4));
}

// 0x8847eb
uint16_t __cdecl Pack3555(const uint8_t *bgra)
{
    uint32_t packed;
    if (bgra[3] <= kAlphaThreshold3555) {
        packed = ((((Reduce(bgra[3], kMax3) << 4 | Reduce(bgra[2], kMax4)) << 4 | Reduce(bgra[1], kMax4)) << 4) |
                  Reduce(bgra[0], kMax4));
    } else {
        packed = ((((bgra[2] & kMask3) << 5 | bgra[1]) & kMask3) << 2) | ((bgra[0] | kForm3555Flag) >> 3);
    }
    return static_cast<uint16_t>(packed);
}

// 0x88486e
int32_t __cdecl Luma(const uint8_t *bgra)
{
    const double luma = LumaOf(bgra);
    int32_t level = static_cast<int32_t>(luma);
    if (luma - static_cast<double>(level) >= kRoundUp) {
        level = static_cast<int32_t>((static_cast<uint32_t>(level) & ~kFull) | ((static_cast<uint32_t>(level) + 1) & kFull));
    }
    return level;
}

// 0x8848ce
uint16_t __cdecl PackGrey88(const uint8_t *bgra)
{
    const double luma = LumaOf(bgra);
    int32_t level = static_cast<int32_t>(luma);
    if (luma - static_cast<double>(level) >= kRoundUp) ++level;
    return static_cast<uint16_t>(static_cast<uint16_t>(bgra[3]) << 8 | static_cast<uint16_t>(level));
}

// 0x884989
uint16_t __cdecl Pack556(const uint8_t *bgra)
{
    if (bgra[3] < kAlphaThresholdLow) return 0;
    return static_cast<uint16_t>(((bgra[2] & 0xfff8u) << 5 | (bgra[1] & 0xfff8u)) << 3 |
                                 static_cast<uint8_t>((bgra[0] | 4) >> 2));
}

// 0x885579
uint16_t __cdecl Pack1555Swapped(const uint8_t *bgra)
{
    const uint32_t alpha = (bgra[3] + kPackHalf) / kFull;
    return static_cast<uint16_t>((((alpha << 5 | (bgra[0] >> 3)) << 5 | (bgra[1] >> 3)) << 5) | (bgra[2] >> 3));
}

// 0x884a34
uint32_t __cdecl Pack1555Alt(const uint8_t *bgra)
{
    uint32_t packed = (((bgra[0] & kMask3) << 5 | (bgra[1] & kMask3)) << 2) | static_cast<uint32_t>(bgra[2] >> 3);
    if (bgra[3] < kAlphaThresholdLow) {
        packed = 0;
    } else if (bgra[3] < kAlphaThresholdHigh) {
        packed |= kAlphaBit1555;
    } else if (packed == 0) {
        packed = kRedBlack1555;
    }
    return packed;
}

// 0x884938
int32_t __cdecl IsGreyRamp(const uint8_t *entries, int32_t count)
{
    if (entries != nullptr && count > 0) {
        int32_t previous = -1;
        for (int32_t index = 0; index < count; ++index, entries += 4) {
            const int32_t level = entries[2];
            if (entries[2] != entries[1] || entries[2] != entries[0] || entries[1] != entries[0] || level <= previous) {
                return 0;
            }
            previous = level;
        }
    }
    return 1;
}

// 0x884a7e
void __cdecl PaddedSize(uint32_t *width, uint32_t *height, int32_t sourceWidth, int32_t sourceHeight, int32_t bits)
{
    uint32_t paddedWidth = 0;
    uint32_t paddedHeight = 0;
    const uint32_t wide = static_cast<uint32_t>(sourceWidth);
    const uint32_t high = static_cast<uint32_t>(sourceHeight);
    if (bits == kBits4) {
        paddedWidth = (wide + kRound8) & ~kRound8;
        paddedHeight = (high + kRound8) & ~kRound8;
    } else if (bits == kBits8) {
        paddedWidth = (wide + kRound8) & ~kRound8;
        paddedHeight = (high + kRound4) & ~kRound4;
    } else if (bits == kBits15 || bits == kBits16 || bits == kBits32) {
        paddedWidth = (wide + kRound4) & ~kRound4;
        paddedHeight = (high + kRound4) & ~kRound4;
    }
    *width = paddedWidth;
    *height = paddedHeight;
}

// 0x884ade
void __cdecl PageSize(uint32_t *width, uint32_t *height, uint32_t sourceWidth, uint32_t sourceHeight, int32_t bits)
{
    if (bits == kBits4) {
        sourceWidth = (sourceWidth + kRound32Blocks) & ~kRound32Blocks;
        sourceHeight = (sourceHeight + kRound32Blocks) & ~kRound32Blocks;
    } else if (bits == kBits8 || bits == kBits15 || bits == kBits16) {
        sourceWidth = (sourceWidth + kRound16Blocks) & ~kRound16Blocks;
        sourceHeight = (sourceHeight + kRound16Blocks) & ~kRound16Blocks;
    }
    *width = sourceWidth;
    *height = sourceHeight;
}

// 0x8845f1
int32_t __cdecl TileOffset16(int32_t x, int32_t y, int32_t width)
{
    return ((x & 3) + ((y & 3) + (((width + 3) >> 2) * (y >> 2) + (x >> 2)) * 4) * 4) * 2;
}

// 0x884623
int32_t __cdecl TileOffset32(int32_t x, int32_t y, int32_t width)
{
    return ((x & 3) + ((y & 3) + (((width + 3) >> 2) * (y >> 2) + (x >> 2)) * 8) * 4) * 2;
}

// 0x884655
int32_t __cdecl TileOffset4(int32_t x, int32_t y, int32_t width)
{
    return (x >> 1 & 3) + ((y & 7) + (((width + 7) >> 3) * (y >> 3) + (x >> 3)) * 8) * 4;
}

// 0x884687
int32_t __cdecl TileOffset8(int32_t x, int32_t y, int32_t width)
{
    return (x & 7) + ((y & 3) + (((width + 7) >> 3) * (y >> 2) + (x >> 3)) * 4) * 8;
}

// 0x885450
int32_t __cdecl TileOffset(int32_t x, int32_t y, int32_t width, int32_t bits)
{
    if (bits == kBits4) return TileOffset4(x, y, width);
    if (bits == kBits8) return TileOffset8(x, y, width);
    if (bits == kBits15 || bits == kBits16) return TileOffset16(x, y, width);
    if (bits == kBits32) return TileOffset32(x, y, width);
    return 0;
}

// 0x8854b6
int32_t __cdecl InterleavedIndex(int32_t x, int32_t y, int32_t width, int32_t height)
{
    int32_t step = 1;
    uint32_t horizontal = 0;
    uint32_t vertical = 0;
    uint32_t horizontalBits = 0;
    uint32_t verticalBits = 0;
    int32_t rest = y * width + x;
    for (;;) {
        if (step < width) {
            horizontal |= (static_cast<uint32_t>(rest) & 1) << (horizontalBits & 0x1f);
            ++horizontalBits;
            rest >>= 1;
        } else if (height <= step) {
            return static_cast<int32_t>(horizontal + vertical * static_cast<uint32_t>(width));
        }
        if (step < height) {
            vertical |= (static_cast<uint32_t>(rest) & 1) << (verticalBits & 0x1f);
            ++verticalBits;
            rest >>= 1;
        }
        step *= 2;
    }
}

// 0x885429
uint32_t __cdecl PaletteOrder(uint32_t index)
{
    if ((index & kPaletteGroupMask) - kPaletteSecondFrom < kPaletteSpan) {
        index += kPaletteUp;
    } else if ((index & kPaletteGroupMask) - kPaletteThirdFrom < kPaletteSpan) {
        // ORIGINAL-BUG: entries 16..23 all map to 24 (a swap with 8..15 would map each to index - 8).
        index = (index & ~kPaletteGroupMask) + kPaletteThirdTo;
    }
    return index;
}

// 0x8855c0
int32_t __cdecl PaletteTypeForBitsA(int32_t alpha, int32_t red, int32_t green, int32_t blue)
{
    if (alpha > 2) goto maybeWithAlpha;
    if (red < kStep6) {
        if (green < kStep6) {
            if (blue < kStep6) return kType1555;
        } else {
            if (green > kStep6) goto greenWide;
            if (blue < kStep6) return kType565;
        }
    smallBlue:
        if (blue < kStep7) return kType666;
    } else {
        if (red < kStep7) {
            if (green < kStep7) goto smallBlue;
        } else if (red > kStep8) {
            return kType8888;
        }
    greenWide:
        if (green > kStep8) goto maybeWithAlpha;
    }
    if (blue < kStep8 + 1) return kType888;
maybeWithAlpha:
    if (red < kStep6) {
        if (green < kStep6) {
            if (blue < kStep6) return kType8555;
        } else if (green < kStep7 && blue < kStep6) {
            return kType8565;
        }
    }
    return kType8888;
}

// 0x88563c
int32_t __cdecl PaletteTypeForBitsB(int32_t alpha, int32_t red, int32_t green, int32_t blue)
{
    if (alpha < 3 && red < kStep6) {
        if (green < kStep6) {
            if (blue < kStep6) return kType1555;
        } else if (green < kStep7 && blue < kStep6) {
            return kType565;
        }
    }
    if (alpha < 1 && red < kStep8 + 1 && green < kStep8 + 1 && blue < kStep8 + 1) return kType888;
    return kType8888;
}

// 0x88568b
int32_t __cdecl PaletteTypeForBitsF(int32_t alpha, int32_t red, int32_t green, int32_t blue)
{
    if (green == 0 && blue == 0) return kType88;
    if (alpha == 0) {
        if (red > kStep5) return kTypeSplit;
        if (green < kStep7 && blue < kStep6) return kType565Alt;
    } else {
        if (alpha > 3) return kTypeSplit;
        if (red > kStep5) return kTypeSplit;
    }
    if (green < kStep6 && blue < kStep6) return kType3555;
    return kTypeSplit;
}

// 0x8856d9
int32_t __cdecl PaletteTypeForBitsC(int32_t alpha, int32_t red, int32_t green, int32_t blue)
{
    if (green == 0 && blue == 0) return kType88;
    if (alpha == 0 && red < kStep6 && green < kStep7 && blue < kStep6) return kType565Alt;
    return kType3555;
}

// 0x885801
int32_t __cdecl PaletteTypeForBitsD(int32_t alpha, int32_t red, int32_t green, int32_t blue)
{
    if (alpha < 2 && red < kStep6 && green < kStep6 && blue < kStep6) return kType1555Other;
    return kTypeArgb;
}

// 0x885825
int32_t __cdecl PaletteTypeForBitsE(int32_t alpha, int32_t red, int32_t green, int32_t blue)
{
    if (alpha < 3 && red < kStep6 && green < kStep6 && blue < kStep6) return kType1555Swizzled;
    return kType8888Swizzled;
}

// 0x88570d
bool __cdecl IsGreyImage(const uint8_t *pixels, const Info *info)
{
    bool grey = true;
    if (info->bitsPerPixel == kBits8) {
        const int32_t first = static_cast<int32_t>(info->unknown20);
        const int32_t end = static_cast<int32_t>(info->paletteCount);
        if (first < end) {
            const uint8_t *entry = &info->palette[0][0] + first * 4;
            for (int32_t count = end - first; count != 0; --count, entry += 4) {
                if (entry[2] != entry[1] || entry[1] != entry[0]) grey = false;
            }
        }
    } else {
        const int32_t width = info->width;
        for (int32_t y = 0; y < info->height; ++y) {
            if (width > 0) {
                const uint8_t *pixel = pixels + width * y * 4;
                for (int32_t x = 0; x < width; ++x, pixel += 4) {
                    if (pixel[2] != pixel[1] || pixel[1] != pixel[0]) grey = false;
                }
            }
        }
    }
    return grey;
}

// 0x88578d
bool __cdecl IsMipmapName(const char *name, int32_t length)
{
    if (name[0] == kMipmapLetter && name[1] == kMipmapLetter &&
        static_cast<uint8_t>(name[2] - kDigitLow) < kDigitCount) {
        return true;
    }
    int32_t scanned = length;
    if (length != 0) {
        if (length < kShortestScanned) return false;
    } else {
        const char *end = name;
        while (*end != '\0') ++end;
        scanned = static_cast<int32_t>(end - name) - kScanBack;
    }
    for (int32_t index = 0; index < scanned; ++index) {
        if (name[index] == kMipmapSeparator && name[index + 1] == kMipmapLetter &&
            name[index + 2] == kMipmapLetter && name[index + 3] > kDigitLow - 1 && name[index + 3] < kDigitLow + 10) {
            return true;
        }
    }
    return false;
}

} // namespace fsh

} // namespace gimex
