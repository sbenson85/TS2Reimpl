// FSH's GM image encoder (lane MV; src/gz/GimexFshGmEncode.h, 0x89d938..0x89e722): the bit writer, the code writers, the float DCT,
// the colour conversion and the stream driver. The arithmetic is single precision float with the original's evaluation order.
#include "gz/GimexFshGmEncode.h"

#include <bit>
#include <cmath>
#include <cstdio>

#include "game/game.h"

namespace gimex {

namespace fsh {

GmEncodeState gGmEncodeState = {};
uint32_t gGmEncodeWarnings = 0;

namespace {

// ---- tables in the original image (RVAs) ----
constexpr uint32_t kZigzag = 0x105e1f8;
constexpr uint32_t kQuantMatrix = 0x105e238;
constexpr uint32_t kLumaDcCodes = 0x105e2f8;       // 4 bytes per size: u16 code, s8 length
constexpr uint32_t kChromaDcCodes = 0x105e328;
constexpr uint32_t kShortRunCodes = 0x105e356;     // runs 0 and 1, levels 0..40: 2 bytes (code, length) each
constexpr uint32_t kLongRunCodes = 0x105e3e2;      // runs 2..31, levels 0..5
constexpr uint32_t kAddressCodes = 0x105e526;      // macroblock address increments 0..33
constexpr uint32_t kHeaderCodes = 0x105e530;       // 32 entries per group

template <class T> const T *Table(uint32_t rva) {
    return GameAddress<T>(rva);
}

// ---- the stream ----
constexpr uint32_t kMagic = 0x4d47;
constexpr int32_t kBitsPerByte = 8;
constexpr int32_t kWordBits = 32;
constexpr uint32_t kEndMarker = 0x134;
constexpr int32_t kPadMask = 0xf;
constexpr int32_t kTrailerWords = 4;
constexpr int32_t kMacroblock = 16;
constexpr int32_t kMacroblockShift = 4;
constexpr int32_t kBlockPixels = 8;
constexpr int32_t kBlocksPerMacroblock = 6;
constexpr uint32_t kFlagAlpha = 1;
constexpr int32_t kAlphaMode8 = 0;
constexpr int32_t kAlphaMode4 = 1;
constexpr int32_t kAlphaShift4 = 4;
constexpr int32_t kAlphaBits4 = 4;
constexpr int32_t kStrideUnitMask = 3;
constexpr int32_t kRowBlockShift = 6;
constexpr int32_t kStrideShift = 2;
constexpr int32_t kRightQuadrantBytes = 0x20;
constexpr int32_t kLowerQuadrantRowBytes = 0x20;
constexpr int32_t kQuadrantRows = 8;
constexpr int32_t kQuadrantStep = 4;
constexpr uint32_t kSizeMask = 0xfffff;
constexpr uint32_t kHeaderKeepMask = 0x7f00000;
constexpr int32_t kHeaderFlagShift = 27;
constexpr uint32_t kHeaderModeMask = 0x300000;
constexpr int32_t kHeaderModeShift = 20;
constexpr uint32_t kHeaderQualityMask = 0x7c00000;
constexpr int32_t kHeaderQualityShift = 22;
constexpr int32_t kHeaderWidthByte = 2;
constexpr int32_t kHeaderHeightByte = 3;
constexpr int32_t kHeaderWordsOffset = 4;
constexpr int32_t kBitsRoundUp = 7;

// ---- the codes ----
constexpr int32_t kFirstQuantiserBits = 5;
constexpr int32_t kHeaderGroup = 1;
constexpr int32_t kHeaderFirst = 0x11;
constexpr int32_t kHeaderNext = 1;
constexpr int32_t kNextIncrement = 1;
constexpr int32_t kBlockEndBits = 2;
constexpr uint32_t kBlockEndCode = 2;
constexpr int32_t kLumaBlocks = 4;
constexpr int32_t kDcMaxMagnitude = 0xff;
constexpr int32_t kAcMaxMagnitude = 0xff;
constexpr uint32_t kMaxRun = 0x3f;
constexpr int32_t kShortRuns = 2;
constexpr int32_t kShortRunLevels = 0x28;
constexpr int32_t kShortRunLimit = 0x29;
constexpr int32_t kLongRuns = 0x20;
constexpr int32_t kLongRunLevels = 5;
constexpr int32_t kLongRunLimit = 6;
constexpr int32_t kEscapeMarker = 1;
constexpr int32_t kEscapeMarkerBits = 6;
constexpr int32_t kEscapeRunBits = 6;
constexpr int32_t kEscapeLevelBits = 8;
constexpr int32_t kEscapeLevelMax = 0x7f;
constexpr uint32_t kEscapeWide = 0x80;
constexpr int32_t kSignBits = 1;
constexpr int32_t kAddressBitsPerStep = 11;
constexpr uint32_t kAddressEscapeCode = 8;
constexpr int32_t kAddressSteps = 0x21;
constexpr int32_t kAddressEscapeBase = 0x22;
constexpr int32_t kHeaderGroupShift = 5;
constexpr int32_t kBlockCoefficients = 64;
constexpr int32_t kChromaBlue = 4;
constexpr int32_t kChromaBlockMask = 1;

// ---- the quantiser ----
constexpr int32_t kDcDivisor = 8;
constexpr int32_t kLevelScaleShift = 5;
constexpr int32_t kRoundNumerator = 3;
constexpr int32_t kRoundOffset = 2;
constexpr int32_t kRoundShift = 2;
constexpr int32_t kQuantiserScale = 2;
constexpr int32_t kLevelClamp = 0xff;

// ---- the transform and the colour conversion ----
constexpr float kRound = 0.5f;
constexpr float kQuarter = 0.25f;
constexpr float kLumaFromGreen = 0.587f;
constexpr float kLumaFromRed = 0.299f;
constexpr float kLumaFromBlue = 0.114f;
constexpr float kLumaScale = 0.85546875f;
constexpr float kLumaBias = 16.5f;
constexpr float kLumaCenter = 128.0f;
constexpr float kBlueScale = 0.5643341f;
constexpr float kRedScale = 0.7132668f;
constexpr float kChromaScale = 0.875f;
constexpr int32_t kChannelMask = 0xff;
constexpr int32_t kRedShift = 16;
constexpr int32_t kGreenShift = 8;
constexpr int32_t kChromaPairStride = 8;

// The angles are single precision values widened to double (as the original's constants are).
constexpr uint64_t kAngleQuarterBits = 0x3fe921fb60000000ull;
constexpr uint64_t kAngleEighthBits = 0x3fd921fb60000000ull;
constexpr uint64_t kAngleThreeEighthsBits = 0x3ff2d97c88000000ull;
constexpr uint64_t kAngleSixteenthBits = 0x3fc921fb60000000ull;
constexpr double kSqrtArgument = 8.0;
constexpr double kScaleFour = 4.0;
constexpr float kOne = 1.0f;

// The float DCT of eight values `stride` floats apart (a row: 1, a column: 8).
template <int Stride> void Dct1d(const float *in, float *out) {
    const GmEncodeState &s = gGmEncodeState;
    const float c45 = s.rotate45;
    const float a0 = in[0 * Stride];
    const float a1 = in[1 * Stride];
    const float a2 = in[2 * Stride];
    const float a3 = in[3 * Stride];
    const float a4 = in[4 * Stride];
    const float a5 = in[5 * Stride];
    const float a6 = in[6 * Stride];
    const float a7 = in[7 * Stride];
    const float evenA = (a2 + a5) + (a1 + a6);
    const float evenB = (a3 + a4) + (a0 + a7);
    const float evenC = (a0 + a7) - (a3 + a4);
    out[0 * Stride] = evenA + evenB;
    const float evenD = (((a1 + a6) - (a2 + a5)) + evenC) * c45;
    out[4 * Stride] = evenB - evenA;
    out[2 * Stride] = evenD + evenC;
    out[6 * Stride] = evenC - evenD;
    const float oddA = a0 - a7;
    const float oddB = (a3 - a4) + (a2 - a5);
    const float oddC = oddA + (a1 - a6);
    const float oddD = c45 * ((a1 - a6) + (a2 - a5));
    const float oddE = (oddB - oddC) * s.rotateC;
    const float oddF = oddA - oddD;
    const float oddG = oddA + oddD;
    const float oddH = s.rotateA * oddB + oddE;
    const float oddI = s.rotateB * oddC + oddE;
    out[5 * Stride] = oddF + oddH;
    out[3 * Stride] = oddF - oddH;
    out[1 * Stride] = oddG + oddI;
    out[7 * Stride] = oddG - oddI;
}

} // namespace

// 0x89e45f
void __cdecl ResetBitWriter(uint8_t *output) {
    GmEncodeState &s = gGmEncodeState;
    s.output = output;
    s.accumulator = 0;
    s.bitCount = 0;
    s.bytePosition = 0;
}

// 0x89e3d7
void __cdecl PutBits(uint32_t value, int32_t count) {
    GmEncodeState &s = gGmEncodeState;
    uint32_t mask = 1u << ((count - 1) & 31);
    if (count <= 0) return;
    uint32_t accumulator = s.accumulator;
    int32_t bitCount = s.bitCount;
    int32_t bytePosition = s.bytePosition;
    do {
        accumulator = accumulator * 2;
        if ((value & mask) != 0) accumulator |= 1;
        mask >>= 1;
        s.accumulator = accumulator;
        bitCount++;
        s.bitCount = bitCount;
        if (bitCount == kBitsPerByte) {
            s.output[bytePosition] = static_cast<uint8_t>(accumulator);
            bytePosition++;
            bitCount = 0;
            s.bitCount = 0;
        }
        count--;
    } while (count != 0);
    s.bytePosition = bytePosition;
}

// 0x89e445
void __cdecl FlushBits() {
    if (gGmEncodeState.bitCount != 0) PutBits(0, kBitsPerByte - gGmEncodeState.bitCount);
}

// 0x89d938
int32_t __cdecl BitsWritten() {
    return gGmEncodeState.bitCount + gGmEncodeState.bytePosition * kBitsPerByte;
}

// 0x89e098
void __cdecl WriteDc(const void *table, int32_t difference) {
    int32_t magnitude = difference < 0 ? -difference : difference;
    int32_t size = 0;
    if (magnitude > kDcMaxMagnitude) {
        gGmEncodeWarnings++;
        std::printf("warning:: DC value out of range (%d)\n", difference);
        magnitude = kDcMaxMagnitude;
    }
    while (magnitude != 0) {
        size++;
        magnitude >>= 1;
    }
    const uint8_t *entry = static_cast<const uint8_t *>(table) + size * 4;
    PutBits(static_cast<uint32_t>(entry[0] | (entry[1] << 8)), static_cast<int8_t>(entry[2]));
    if (size != 0) {
        int32_t bits = difference;
        if (bits < 0) bits = bits - 1 + (1 << size);
        PutBits(static_cast<uint32_t>(bits), size);
    }
}

// 0x89dfd7
void __cdecl WriteRunLevel(uint32_t run, int32_t level) {
    int32_t magnitude = level < 0 ? -level : level;
    if (run > kMaxRun || magnitude == 0 || magnitude > kAcMaxMagnitude) {
        gGmEncodeWarnings++;
        std::printf("warning:: AC value out of range (run=%d, signed_level=%d)\n", run, level);
        magnitude = kAcMaxMagnitude;
    }
    const uint8_t *entry = nullptr;
    int32_t length = 0;
    bool useTables = true;
    if (static_cast<int32_t>(run) < kShortRuns) {
        if (magnitude < kShortRunLimit) {
            entry = Table<uint8_t>(kShortRunCodes) + (static_cast<int32_t>(run) * kShortRunLevels + magnitude) * 2;
        } else {
            useTables = false;
        }
    } else if (static_cast<int32_t>(run) < kLongRuns && magnitude < kLongRunLimit) {
        entry = Table<uint8_t>(kLongRunCodes) + (static_cast<int32_t>(run) * kLongRunLevels + magnitude) * 2;
    } else {
        useTables = false;
    }
    if (useTables) length = static_cast<int8_t>(entry[1]);
    if (useTables && length != 0) {
        PutBits(entry[0], length);
        PutBits(static_cast<uint32_t>(level) >> 31, kSignBits);
        return;
    }
    PutBits(kEscapeMarker, kEscapeMarkerBits);
    PutBits(run, kEscapeRunBits);
    if (level > kEscapeLevelMax) PutBits(0, kEscapeLevelBits);
    else if (level < -kEscapeLevelMax) PutBits(kEscapeWide, kEscapeLevelBits);
    PutBits(static_cast<uint32_t>(level), kEscapeLevelBits);
}

// 0x89e102
void __cdecl WriteAddressIncrement(int32_t count) {
    if (count > kAddressSteps) {
        int32_t steps = static_cast<int32_t>(static_cast<uint32_t>(count - kAddressEscapeBase) / kAddressSteps) + 1;
        count -= steps * kAddressSteps;
        do {
            PutBits(kAddressEscapeCode, kAddressBitsPerStep);
            steps--;
        } while (steps != 0);
    }
    const uint8_t *entry = Table<uint8_t>(kAddressCodes) + count * 2;
    PutBits(entry[0], static_cast<int8_t>(entry[1]));
}

// 0x89e14a
void __cdecl WriteHeaderCode(int32_t group, int32_t index) {
    const uint8_t *entry = Table<uint8_t>(kHeaderCodes) + (group * (1 << kHeaderGroupShift) + index) * 2;
    PutBits(entry[0], static_cast<int8_t>(entry[1]));
}

// 0x89d947
void __cdecl WriteBlock(const int32_t *levels, uint32_t block) {
    GmEncodeState &s = gGmEncodeState;
    const int32_t dc = levels[0];
    int32_t previous;
    const void *table;
    if (static_cast<int32_t>(block) < kLumaBlocks) {
        previous = s.dcPredictor[0];
        s.dcPredictor[0] = dc;
        table = Table<uint8_t>(kLumaDcCodes);
    } else {
        const int32_t slot = 1 + static_cast<int32_t>(block & kChromaBlockMask);
        previous = s.dcPredictor[slot];
        s.dcPredictor[slot] = dc;
        table = Table<uint8_t>(kChromaDcCodes);
    }
    WriteDc(table, dc - previous);
    uint32_t run = 0;
    const uint8_t *zigzag = Table<uint8_t>(kZigzag);
    for (int32_t i = 1; i < kBlockCoefficients; i++) {
        const int32_t level = levels[zigzag[i]];
        if (level == 0) {
            run++;
        } else {
            WriteRunLevel(run, level);
            run = 0;
        }
    }
    PutBits(kBlockEndCode, kBlockEndBits);
}

// 0x89dc7b
void __cdecl DctRow(const float *in, float *out) {
    Dct1d<1>(in, out);
}

// 0x89db37
void __cdecl DctColumn(const float *in, float *out) {
    Dct1d<8>(in, out);
}

// 0x89d9c9
void __cdecl DctBlock(const float *in, int32_t *out) {
    float columns[64];
    float rows[64];
    for (int32_t i = 0; i < 8; i++) DctColumn(in + i, columns + i);
    for (int32_t i = 0; i < 8; i++) DctRow(columns + i * 8, rows + i * 8);
    const GmEncodeState &s = gGmEncodeState;
    for (int32_t i = 0; i < kBlockCoefficients; i++) out[i] = static_cast<int32_t>(s.scale[i] * rows[i] + kRound);
}

// 0x89de18
void __cdecl InitEncodeTables() {
    GmEncodeState &s = gGmEncodeState;
    // The arguments go through volatile so that the compiler cannot fold the calls: the original calls the C library's cos and sqrt.
    volatile double quarter = std::bit_cast<double>(kAngleQuarterBits);
    volatile double eighth = std::bit_cast<double>(kAngleEighthBits);
    volatile double threeEighths = std::bit_cast<double>(kAngleThreeEighthsBits);
    volatile double sixteenth = std::bit_cast<double>(kAngleSixteenthBits);
    volatile double eight = kSqrtArgument;
    s.rotate45 = static_cast<float>(std::cos(quarter));
    const double cosEighth = std::cos(eighth);
    const double cosThreeEighths = std::cos(threeEighths);
    s.rotateA = static_cast<float>(cosEighth - cosThreeEighths);
    const double cosEighthAgain = std::cos(eighth);
    const double cosThreeEighthsAgain = std::cos(threeEighths);
    s.rotateB = static_cast<float>(cosEighthAgain + cosThreeEighthsAgain);
    s.rotateC = static_cast<float>(std::cos(threeEighths));
    for (int32_t row = 0; row < 8; row++) {
        for (int32_t column = 0; column < 8; column++) {
            float rowFactor;
            if (row == 0) rowFactor = kOne / static_cast<float>(std::sqrt(eight));
            else rowFactor = kOne / static_cast<float>(std::cos(static_cast<double>(row) * sixteenth) * kScaleFour);
            if (column == 0) s.scale[row * 8 + column] = rowFactor / static_cast<float>(std::sqrt(eight));
            else s.scale[row * 8 + column] = rowFactor / static_cast<float>(std::cos(static_cast<double>(column) * sixteenth) * kScaleFour);
        }
    }
}

// 0x89e16f
void __cdecl QuantiseBlock(const int32_t *source, int32_t *dest, uint8_t shift, const uint8_t *matrix, int32_t quantiser) {
    const int32_t divisor = kDcDivisor >> (shift & 31);
    const int32_t dc = source[0];
    if (dc < 0) dest[0] = -(((divisor >> 1) - dc) / divisor);
    else dest[0] = ((divisor >> 1) + dc) / divisor;
    for (int32_t i = 1; i < kBlockCoefficients; i++) {
        const int32_t value = source[i];
        const int32_t magnitude = value < 0 ? -value : value;
        const int32_t step = matrix[i];
        int32_t level = ((magnitude * (1 << kLevelScaleShift) + (step >> 1)) / step + ((quantiser * kRoundNumerator + kRoundOffset) >> kRoundShift)) /
                        (quantiser * kQuantiserScale);
        if (level > kLevelClamp) level = kLevelClamp;
        dest[i] = value >= 0 ? level : -level;
    }
}

// 0x89e20d
void __cdecl ConvertToBlocks(const uint32_t *pixels, int32_t stride, int32_t block, int32_t chromaRow, int32_t chromaColumn) {
    GmEncodeState &s = gGmEncodeState;
    float blue[64];
    float red[64];
    float *luma = s.pixels[block];
    int32_t index = 0;
    const uint8_t *row = reinterpret_cast<const uint8_t *>(pixels);
    do {
        for (int32_t x = 0; x < kBlockPixels; x++) {
            const uint32_t pixel = reinterpret_cast<const uint32_t *>(row)[x];
            const float r = static_cast<float>((pixel >> kRedShift) & kChannelMask);
            const float b = static_cast<float>(pixel & kChannelMask);
            const float g = static_cast<float>((pixel >> kGreenShift) & kChannelMask);
            const float y = g * kLumaFromGreen + r * kLumaFromRed + b * kLumaFromBlue;
            *luma++ = static_cast<float>(static_cast<int32_t>(((y * kLumaScale + kLumaBias) - kLumaCenter) + kRound));
            blue[index] = static_cast<float>(static_cast<int32_t>(((b - y) * kBlueScale) * kChromaScale + kRound));
            red[index] = static_cast<float>(static_cast<int32_t>(((r - y) * kRedScale) * kChromaScale + kRound));
            index++;
        }
        row += stride * 4;
    } while (index < kBlockCoefficients);
    float *chromaBlue = s.pixels[kChromaBlue] + chromaRow * kBlockPixels + chromaColumn;
    float *chromaRed = s.pixels[kChromaBlue + 1] + chromaRow * kBlockPixels + chromaColumn;
    for (int32_t y = 0; y < kQuadrantStep; y++) {
        for (int32_t x = 0; x < kQuadrantStep; x++) {
            const int32_t at = y * 2 * kBlockPixels + x * 2;
            chromaBlue[y * kBlockPixels + x] = (blue[at + 1] + blue[at] + blue[at + kChromaPairStride] + blue[at + kChromaPairStride + 1]) * kQuarter;
            chromaRed[y * kBlockPixels + x] = (red[at + 1] + red[at] + red[at + kChromaPairStride] + red[at + kChromaPairStride + 1]) * kQuarter;
        }
    }
}

// 0x89dd9b
void __cdecl WriteMacroblock(int32_t first, int32_t quantiser) {
    GmEncodeState &s = gGmEncodeState;
    WriteHeaderCode(kHeaderGroup, first == 0 ? kHeaderNext : kHeaderFirst);
    if (first == 0) WriteAddressIncrement(kNextIncrement);
    else PutBits(static_cast<uint32_t>(quantiser), kFirstQuantiserBits);
    for (int32_t block = 0; block < kBlocksPerMacroblock; block++) {
        int32_t *levels = s.levels[block];
        DctBlock(s.pixels[block], levels);
        QuantiseBlock(levels, levels, 0, Table<uint8_t>(kQuantMatrix), quantiser * kQuantiserScale);
        WriteBlock(levels, static_cast<uint32_t>(block));
    }
}

// 0x89e47a
uint32_t __cdecl EncodeGm(const uint32_t *pixels, uint16_t *output, int32_t width, int32_t height, uint32_t stride, uint32_t flags,
                          int32_t quantiser, int32_t alphaMode) {
    GmEncodeState &s = gGmEncodeState;
    if (s.tablesReady == 0) {
        InitEncodeTables();
        s.tablesReady = 1;
    }
    const int32_t alignedWidth = (width + kMacroblock - 1) & ~(kMacroblock - 1);
    const int32_t alignedHeight = (height + kMacroblock - 1) & ~(kMacroblock - 1);
    const uint8_t *base = reinterpret_cast<const uint8_t *>(pixels);
    ResetBitWriter(reinterpret_cast<uint8_t *>(output));
    PutBits(0, kWordBits);
    PutBits(0, kWordBits);
    int32_t first = 1;
    s.dcPredictor[2] = 0;
    s.dcPredictor[1] = 0;
    s.dcPredictor[0] = 0;
    // ORIGINAL-BUG: the conversions below take the aligned width as the row stride in pixels, and the loops step by the caller's
    // stride; the two agree only when the stride is the aligned width in pixels.
    for (int32_t x = 0; x < alignedWidth; x += kMacroblock) {
        if (alignedHeight <= 0) continue;
        const int32_t step = (static_cast<int32_t>(stride) >> kStrideShift) << kRowBlockShift;
        const uint8_t *upperLeft = base + x * 4;
        const uint8_t *lowerLeft = base + alignedWidth * kLowerQuadrantRowBytes + x * 4;
        const uint8_t *lowerRight = base + (x + (alignedWidth + 1) * kQuadrantRows) * 4;
        int32_t count = static_cast<int32_t>((static_cast<uint32_t>(alignedHeight) - 1) >> kMacroblockShift) + 1;
        do {
            ConvertToBlocks(reinterpret_cast<const uint32_t *>(upperLeft), alignedWidth, 0, 0, 0);
            ConvertToBlocks(reinterpret_cast<const uint32_t *>(lowerLeft - alignedWidth * kLowerQuadrantRowBytes + kRightQuadrantBytes),
                            alignedWidth, 1, 0, kQuadrantStep);
            ConvertToBlocks(reinterpret_cast<const uint32_t *>(lowerLeft), alignedWidth, 2, kQuadrantStep, 0);
            ConvertToBlocks(reinterpret_cast<const uint32_t *>(lowerRight), alignedWidth, 3, kQuadrantStep, kQuadrantStep);
            WriteMacroblock(first, quantiser);
            lowerLeft += step;
            first = 0;
            upperLeft += step;
            lowerRight += step;
            count--;
        } while (count != 0);
    }
    FlushBits();
    PutBits(kEndMarker, kWordBits);
    int32_t bits = BitsWritten();
    int32_t padding = (bits + ((bits >> 31) & 7)) >> 3;
    while ((padding & kPadMask) != 0) {
        PutBits(0, kBitsPerByte);
        padding++;
    }
    if ((flags & kFlagAlpha) != 0 && alignedHeight > 0) {
        const int32_t rowStep = static_cast<int32_t>(stride & ~static_cast<uint32_t>(kStrideUnitMask));
        const uint8_t *row = base + 3;
        for (int32_t y = alignedHeight; y > 0; y--) {
            const uint8_t *alpha = row;
            for (int32_t x = alignedWidth; x > 0; x--) {
                if (alphaMode == kAlphaMode8) PutBits(*alpha, kBitsPerByte);
                else if (alphaMode == kAlphaMode4) PutBits(static_cast<uint32_t>(*alpha >> kAlphaShift4), kAlphaBits4);
                alpha += 4;
            }
            row += rowStep;
        }
    }
    for (int32_t i = 0; i < kTrailerWords; i++) PutBits(0, kWordBits);
    bits = BitsWritten() + kBitsRoundUp;
    const int32_t size = (bits + ((bits >> 31) & 7)) >> 3;
    uint8_t *header = reinterpret_cast<uint8_t *>(output);
    *output = static_cast<uint16_t>(kMagic);
    uint32_t word = *reinterpret_cast<uint32_t *>(header + kHeaderWordsOffset);
    header[kHeaderWidthByte] = static_cast<uint8_t>(alignedWidth >> kMacroblockShift);
    word = (word & kHeaderKeepMask) ^ (static_cast<uint32_t>(size) & kSizeMask);
    word |= flags << kHeaderFlagShift;
    word = (((static_cast<uint32_t>(alphaMode) << kHeaderModeShift) ^ word) & kHeaderModeMask) ^ word;
    word = (((static_cast<uint32_t>(quantiser) << kHeaderQualityShift) ^ word) & kHeaderQualityMask) ^ word;
    *reinterpret_cast<uint32_t *>(header + kHeaderWordsOffset) = word;
    header[kHeaderHeightByte] = static_cast<uint8_t>(alignedHeight >> kMacroblockShift);
    return static_cast<uint32_t>(size);
}

} // namespace fsh

} // namespace gimex
