// FSH's block-compressed records (lane MR; src/gz/GimexFshBlocks.h, 0x894e0e..0x8955c4): the colour and alpha block decoders,
// the record decoder and the header tests.
#include "gz/GimexFshBlocks.h"

namespace gimex {

namespace fsh {

namespace {

constexpr uint32_t kRecognised = 100;       // what the header tests answer for a match
constexpr uint16_t kHeader28fb = 0x28fb;
constexpr uint16_t kHeader18fb = 0x18fb;
constexpr uint16_t kHeader1afb = 0x1afb;
constexpr uint16_t kHeader1cfb = 0x1cfb;
constexpr uint16_t kHeader10fb = 0x10fb;
constexpr uint16_t kHeader11fb = 0x11fb;
constexpr uint16_t kWideFlag = 0x8000;      // the 0x80 variant of a header (a 4-byte size)

constexpr int32_t kBlockPixels = 16;        // pixels in a block
constexpr int32_t kBlockRows = 4;
constexpr int32_t kPixelBytes = 4;          // B, G, R, A
constexpr int32_t kBlockBytes = 64;         // decoded bytes of a block
constexpr int32_t kBlockStride = 0x10;      // the stride the record decoder passes
constexpr int32_t kAlphaOffset = 3;         // the alpha byte of a pixel

constexpr uint32_t kFull = 0xff;
constexpr uint32_t kMax5 = 0x1f;
constexpr uint32_t kHalf5 = 0x10;
constexpr uint32_t kMax6 = 0x3f;
constexpr uint32_t kHalf6 = 0x20;

constexpr uint8_t kRecordWide = 0x80;       // the first header byte's flag: 4-byte size
constexpr int32_t kHeaderBytes = 8;
constexpr uint8_t kFlagForm1555 = 4;
constexpr uint8_t kFlagUnpremultiply = 8;
constexpr int32_t kSizeOffsetNarrow = 2;
constexpr int32_t kFlagsOffsetNarrow = 5;
constexpr int32_t kFlagsOffsetWide = 6;
constexpr int32_t kNarrowSizeBytes = 3;
constexpr int32_t kWideSizeBytes = 4;
constexpr int32_t kColourBlockBytes = 8;
constexpr int32_t kSizeToBytesShift = 3;    // the compressed bytes are an eighth of the decoded size
constexpr int32_t kSizeToBytesBias = 7;     // added for negative sizes so the shift rounds toward zero
constexpr int32_t kSignShift = 31;
constexpr int32_t kSizeToBlocksShift = 6;
constexpr int32_t kSizeToPixelsShift = 2;
constexpr uint32_t kUnpremultiplyRound = 0x80;

constexpr uint32_t kSevenSteps = 7;
constexpr uint32_t kFiveSteps = 5;
constexpr uint32_t kThreeBitMask = 7;
constexpr uint32_t kTwoBitMask = 3;

uint16_t Header(const uint8_t *header)
{
    return static_cast<uint16_t>(header[0] << 8 | header[1]);
}

uint32_t Recognise(const uint8_t *header, uint16_t type)
{
    const uint16_t value = Header(header);
    return value == type || value == (type | kWideFlag) ? kRecognised : 0;
}

// A 5-bit field scaled to 0..255 (rounded).
uint32_t Scale5(uint32_t value)
{
    return (value * kFull + kHalf5) / kMax5;
}

uint32_t Scale6(uint32_t value)
{
    return (value * kFull + kHalf6) / kMax6;
}

} // namespace

uint32_t __stdcall HasHeader28fb(const uint8_t *header)
{
    return Recognise(header, kHeader28fb);
}

uint32_t __stdcall HasHeader1afb(const uint8_t *header)
{
    return Recognise(header, kHeader1afb);
}

uint32_t __stdcall HasHeader1cfb(const uint8_t *header)
{
    return Recognise(header, kHeader1cfb);
}

bool __stdcall IsBlockCompressed(const uint8_t *header)
{
    const uint16_t value = Header(header);
    const uint16_t kind = value & ~kWideFlag;
    return kind == kHeader18fb || kind == kHeader1afb || kind == kHeader1cfb || kind == kHeader28fb;
}

bool __stdcall IsRefPack(const uint8_t *header)
{
    const uint16_t value = Header(header);
    const uint16_t kind = value & ~kWideFlag;
    return kind == kHeader10fb || kind == kHeader11fb;
}

void __cdecl DecodeAlpha4(uint8_t *pixels, int32_t stride, const uint8_t *block)
{
    for (int32_t row = 0; row < kBlockRows; ++row) {
        const uint8_t low = block[row * 2];
        const uint8_t high = block[row * 2 + 1];
        uint8_t *alpha = pixels + kAlphaOffset;
        alpha[0] = static_cast<uint8_t>((low & 0xf) | low << 4);
        alpha[4] = static_cast<uint8_t>((low & 0xf0) | low >> 4);
        alpha[8] = static_cast<uint8_t>((high & 0xf) | high << 4);
        alpha[12] = static_cast<uint8_t>((high & 0xf0) | high >> 4);
        pixels += stride;
    }
}

void __cdecl DecodeAlpha8(uint8_t *pixels, int32_t, const uint8_t *block)
{
    const uint32_t first = block[0];
    const uint32_t second = block[1];
    uint8_t steps[8];
    steps[0] = block[0];
    steps[1] = block[1];
    if (second < first) {
        // six steps between: indices 2..7 interpolate in sevenths
        steps[2] = static_cast<uint8_t>((first * 6 + 3 + second) / kSevenSteps);
        steps[3] = static_cast<uint8_t>((first * 5 + 3 + second * 2) / kSevenSteps);
        steps[4] = static_cast<uint8_t>((first * 4 + 3 + second * 3) / kSevenSteps);
        steps[5] = static_cast<uint8_t>((first * 3 + 3 + second * 4) / kSevenSteps);
        steps[6] = static_cast<uint8_t>((second * 5 + first * 2 + 3) / kSevenSteps);
        steps[7] = static_cast<uint8_t>((second * 6 + 3 + first) / kSevenSteps);
    } else {
        // four steps between, then 0 and 255
        steps[2] = static_cast<uint8_t>((first * 4 + 2 + second) / kFiveSteps);
        steps[3] = static_cast<uint8_t>((first * 3 + 2 + second * 2) / kFiveSteps);
        steps[4] = static_cast<uint8_t>((first * 2 + 2 + second * 3) / kFiveSteps);
        steps[5] = static_cast<uint8_t>((first + 2 + second * 4) / kFiveSteps);
        steps[6] = 0;
        steps[7] = 0xff;
    }
    // two 3-byte groups of eight 3-bit indices, four pixels per row, two rows per group
    const uint8_t *indices = block + 2;
    uint8_t *row = pixels + kAlphaOffset;
    for (int32_t group = 0; group < 2; ++group) {
        uint32_t bits = indices[0] | indices[1] << 8 | indices[2] << 16;
        indices += 3;
        for (int32_t half = 0; half < 2; ++half) {
            for (int32_t x = 0; x < 4; ++x) {
                row[x * kPixelBytes] = steps[bits & kThreeBitMask];
                bits >>= 3;
            }
            row += kBlockStride;
        }
    }
}

void __cdecl DecodeColourBlock(uint8_t *pixels, int32_t stride, const uint8_t *block, uint32_t form1555, int32_t bigEndian)
{
    uint16_t first;
    uint16_t second;
    if (bigEndian == 0) {
        first = static_cast<uint16_t>(block[0] | block[1] << 8);
        second = static_cast<uint16_t>(block[2] | block[3] << 8);
    } else {
        first = static_cast<uint16_t>(block[0] << 8 | block[1]);
        second = static_cast<uint16_t>(block[2] << 8 | block[3]);
    }
    // The four colours, B, G, R, A each. In the 5-5-5 form byte 0 takes the field at bits 10..14 and byte 2 the one at bits 0..4.
    uint8_t colours[4][4];
    uint32_t b0, g0, r0, b1, g1, r1;
    if (form1555 == 0) {
        b0 = Scale5(first & kMax5);
        g0 = Scale6(first >> 5 & kMax6);
        r0 = Scale5(first >> 11);
        b1 = Scale5(second & kMax5);
        g1 = Scale6(second >> 5 & kMax6);
        r1 = Scale5(second >> 11);
    } else {
        r0 = Scale5(first & kMax5);
        g0 = Scale5(first >> 5 & kMax5);
        b0 = Scale5(first >> 10 & kMax5);
        r1 = Scale5(second & kMax5);
        g1 = Scale5(second >> 5 & kMax5);
        b1 = Scale5(second >> 10 & kMax5);
    }
    // For the 5-5-5 form the names above hold the bytes in the order the colour bytes are stored: b = byte 0, r = byte 2.
    colours[0][0] = static_cast<uint8_t>(b0);
    colours[0][1] = static_cast<uint8_t>(g0);
    colours[0][2] = static_cast<uint8_t>(r0);
    colours[0][3] = 0xff;
    colours[1][0] = static_cast<uint8_t>(b1);
    colours[1][1] = static_cast<uint8_t>(g1);
    colours[1][2] = static_cast<uint8_t>(r1);
    colours[1][3] = 0xff;
    if (first > second) {
        colours[2][0] = static_cast<uint8_t>((b1 * 3 + b0 * 5) >> 3);
        colours[2][1] = static_cast<uint8_t>((g1 * 3 + g0 * 5) >> 3);
        colours[2][2] = static_cast<uint8_t>((r1 * 3 + r0 * 5) >> 3);
        colours[2][3] = 0xff;
        colours[3][0] = static_cast<uint8_t>((b1 * 5 + b0 * 3) >> 3);
        colours[3][1] = static_cast<uint8_t>((g1 * 5 + g0 * 3) >> 3);
        colours[3][2] = static_cast<uint8_t>((r1 * 5 + r0 * 3) >> 3);
        colours[3][3] = 0xff;
    } else {
        colours[2][0] = static_cast<uint8_t>((b0 + b1) >> 1);
        colours[2][1] = static_cast<uint8_t>((g0 + g1) >> 1);
        colours[2][2] = static_cast<uint8_t>((r0 + r1) >> 1);
        colours[2][3] = 0xff;
        colours[3][0] = 0;
        colours[3][1] = 0;
        colours[3][2] = 0;
        colours[3][3] = 0;
    }
    const uint8_t *indices = block + 4;
    for (int32_t row = 0; row < kBlockRows; ++row) {
        const uint32_t byte = indices[row];
        for (int32_t x = 0; x < 4; ++x) {
            const uint32_t pair = bigEndian == 0 ? byte >> (2 * x) : byte >> (2 * (3 - x));
            const uint8_t *colour = colours[pair & kTwoBitMask];
            uint8_t *pixel = pixels + x * kPixelBytes;
            pixel[0] = colour[0];
            pixel[1] = colour[1];
            pixel[2] = colour[2];
            pixel[3] = colour[3];
        }
        pixels += stride;
    }
}

int32_t __stdcall DecodeBlocks(uint8_t *pixels, const uint8_t *record, int32_t *consumed)
{
    const bool wide = (record[0] & kRecordWide) != 0;
    const int32_t size = static_cast<int32_t>(
        ReadBigEndian(record + kSizeOffsetNarrow, wide ? kWideSizeBytes : kNarrowSizeBytes));
    const uint8_t flags = record[wide ? kFlagsOffsetWide : kFlagsOffsetNarrow];
    const uint32_t form1555 = flags & kFlagForm1555;
    const bool unpremultiply = (flags & kFlagUnpremultiply) != 0;
    const uint32_t is1afb = HasHeader1afb(record);
    const uint32_t is1cfb = HasHeader1cfb(record);
    const uint32_t is28fb = HasHeader28fb(record);
    if (consumed != nullptr) *consumed = (size + (size >> kSignShift & kSizeToBytesBias)) >> kSizeToBytesShift;
    const uint8_t *block = record + kHeaderBytes;
    if (size > 0) {
        int32_t blocks = ((size - 1) >> kSizeToBlocksShift) + 1;
        uint8_t *out = pixels;
        for (; blocks != 0; --blocks, out += kBlockBytes) {
            if (is1afb != 0) {
                DecodeColourBlock(out, kBlockStride, block + kColourBlockBytes, form1555, static_cast<int32_t>(is28fb));
                DecodeAlpha4(out, kBlockStride, block);
                block += 2 * kColourBlockBytes;
            } else if (is1cfb != 0) {
                DecodeColourBlock(out, kBlockStride, block + kColourBlockBytes, form1555, static_cast<int32_t>(is28fb));
                DecodeAlpha8(out, kBlockStride, block);
                block += 2 * kColourBlockBytes;
            } else {
                DecodeColourBlock(out, kBlockStride, block, form1555, static_cast<int32_t>(is28fb));
                block += kColourBlockBytes;
            }
        }
    }
    if (unpremultiply && size > 0) {
        uint8_t *pixel = pixels;
        for (int32_t count = ((size - 1) >> kSizeToPixelsShift) + 1; count != 0; --count, pixel += kPixelBytes) {
            const uint32_t alpha = pixel[3];
            if (alpha != 0) {
                pixel[2] = static_cast<uint8_t>((pixel[2] * kFull + kUnpremultiplyRound) / alpha);
                pixel[1] = static_cast<uint8_t>((pixel[1] * kFull + kUnpremultiplyRound) / alpha);
                pixel[0] = static_cast<uint8_t>((pixel[0] * kFull + kUnpremultiplyRound) / alpha);
            }
        }
    }
    return size;
}

} // namespace fsh

} // namespace gimex
