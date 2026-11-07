// The FSH back end's encode side, pixel packing and compression (lane MY; src/gz/GimexFshWrite.h, 0x88bd83..0x88c0e9): one pixel of the frame
// into a shape's record type, the whole frame row by row, and the block and GM compressors that feed block 2's and block 1's encoders.
#include "gz/GimexFshWrite.h"

#include <cstring>

#include "gz/GimexFshCluster.h"
#include "gz/GimexFshConvert.h"
#include "gz/GimexFshEncode.h"
#include "gz/GimexFshGmEncode.h"
#include "gz/GimexFshLeaves.h"

namespace gimex {

namespace fsh {

namespace {

constexpr uint32_t kInfoWidth = 0x10;
constexpr uint32_t kInfoHeight = 0x14;
constexpr uint32_t kInfoBitsPerPixel = 0x18;
constexpr uint32_t kInfoPalette = 0x28;
constexpr uint32_t kInfoQuality = 0x430;
constexpr uint32_t kInfoAlphaBits = 0x438;
constexpr uint32_t kInfoRedBits = 0x43c;
constexpr uint32_t kInfoGreenBits = 0x440;
constexpr uint32_t kInfoBlueBits = 0x444;
constexpr uint32_t kInfoCompression = 0x42c;
constexpr uint32_t kFlagReorder = 0x2000;
constexpr int32_t kPlatformTiled = 5;
constexpr int32_t kPlatformPaged = 6;

int32_t InfoInt(const Info *info, uint32_t offset)
{
    int32_t value;
    std::memcpy(&value, reinterpret_cast<const uint8_t *>(info) + offset, sizeof value);
    return value;
}

// The palette entry (B, G, R, A) for index `index` of an 8-bit frame.
const uint8_t *PaletteEntry(const Info *info, uint8_t index)
{
    return reinterpret_cast<const uint8_t *>(info) + kInfoPalette + static_cast<uint32_t>(index) * 4;
}

} // namespace

// 0x88c0e9
void __cdecl PackPixel(const Info *info, const uint8_t *source, uint8_t *dest, int32_t *sourceOffset, int32_t *destOffset, int32_t type, int32_t *column,
                       uint32_t *row, int32_t width)
{
    const uint32_t savedRow = *row;
    int32_t x = *column;
    int32_t s = *sourceOffset;
    int32_t d = *destOffset;
    const uint8_t *src = source + s;
    const int32_t depth = InfoInt(info, kInfoBitsPerPixel);
    auto big16 = [&](uint32_t value) {
        dest[d] = static_cast<uint8_t>((value & 0xffff) >> 8);
        dest[d + 1] = static_cast<uint8_t>(value);
        d += 2;
        s += 4;
    };
    auto little16 = [&](uint32_t value) {
        const uint16_t word = static_cast<uint16_t>(value);
        std::memcpy(dest + d, &word, 2);
        d += 2;
        s += 4;
    };
    auto rgb3 = [&] {
        dest[d] = src[2];
        dest[d + 1] = src[1];
        dest[d + 2] = src[0];
        d += 3;
        s += 4;
    };
    // The two pixels of a byte, the first in the low nibble (0x01, 0x40, 0x79) or in the high one (0x18, 0x58, 0x7a, 0x62, 0x63); the second is
    // left out at the end of a row.
    auto lowFirst = [&] {
        uint8_t value = src[0] & 0xf;
        s += 1;
        if (x + 1 < width) {
            value |= static_cast<uint8_t>(src[1] << 4);
            s += 1;
        }
        dest[d++] = value;
        x += 1;
    };
    auto highFirst = [&] {
        uint8_t value = static_cast<uint8_t>(src[0] << 4);
        s += 1;
        if (x + 1 < width) {
            value |= src[1] & 0xf;
            s += 1;
        }
        dest[d++] = value;
        x += 1;
    };
    switch (type) {
    case 0x5a: big16(Pack1555(src)); break;
    case 0x15: big16(Pack3555(src)); break;
    case 0x14:
    case 0x5b: big16(Pack565(src)); break;
    case 0x5c: big16(Pack4444(src)); break;
    case 0x42: little16(Pack1555Alt(src)); break;
    case 0x03: little16(Pack1555Swapped(src)); break;
    case 0x7e: little16(Pack1555(src)); break;
    case 0x67: little16(Pack655(src)); break;
    case 0x6d: little16(Pack4444(src)); break;
    case 0x78: little16(Pack565(src)); break;
    case 0x6a: {
        const uint32_t word = Pack2101010(src);
        std::memcpy(dest + d, &word, 4);
        d += 4;
        s += 4;
        break;
    }
    case 0x66:
        PutLittleEndian(dest + d, Pack6666(src), 3);
        d += 3;
        s += 4;
        break;
    case 0x43:
    case 0x5d:
    case 0x04: rgb3(); break;
    case 0x7f:
        dest[d] = src[0];
        dest[d + 1] = src[1];
        dest[d + 2] = src[2];
        d += 3;
        s += 4;
        break;
    case 0x16:
    case 0x5e:
        dest[d] = src[3];
        dest[d + 1] = src[2];
        dest[d + 2] = src[1];
        dest[d + 3] = src[0];
        d += 4;
        s += 4;
        break;
    case 0x05:
        dest[d] = src[2];
        dest[d + 1] = src[1];
        dest[d + 2] = src[0];
        dest[d + 3] = src[3];
        d += 4;
        s += 4;
        break;
    case 0x7d:
        dest[d] = src[0];
        dest[d + 1] = src[1];
        dest[d + 2] = src[2];
        dest[d + 3] = src[3];
        d += 4;
        s += 4;
        break;
    case 0x02:
    case 0x19:
    case 0x41:
    case 0x59:
    case 0x7b:
        dest[d] = src[0];
        d += 1;
        s += 1;
        break;
    case 0x01:
    case 0x40:
    case 0x79: lowFirst(); break;
    case 0x18:
    case 0x58:
    case 0x7a:
    case 0x62:
    case 0x63: highFirst(); break;
    case 0x10: {
        uint8_t value;
        if (depth == 8) {
            value = PaletteEntry(info, src[0])[2] & 0xf0;
            s += 1;
            if (x + 1 < width) {
                value |= PaletteEntry(info, src[1])[2] >> 4;
                s += 1;
            }
        } else {
            value = static_cast<uint8_t>((static_cast<uint32_t>(Luma(src)) & 0xff) << 4);
            s += 4;
            if (x + 1 < width) {
                value |= static_cast<uint8_t>((static_cast<uint32_t>(Luma(src + 4)) & 0xff) >> 4);
                s += 4;
            }
        }
        dest[d++] = value;
        x += 1;
        break;
    }
    case 0x61: {
        uint8_t value = PaletteEntry(info, src[0])[2] & 0xf0;
        s += 1;
        if (x + 1 < width) {
            value |= PaletteEntry(info, src[1])[2] >> 4;
            s += 1;
        }
        dest[d++] = value;
        x += 1;
        break;
    }
    case 0x11:
    case 0x12: {
        const bool indexed = depth == 8;
        const uint8_t *pixel = indexed ? PaletteEntry(info, src[0]) : src;
        dest[d++] = type == 0x11 ? static_cast<uint8_t>(Luma(pixel)) : PackGrey44(pixel);
        s += indexed ? 1 : 4;
        break;
    }
    case 0x13: {
        const bool indexed = depth < 9;
        const uint16_t value = PackGrey88(indexed ? PaletteEntry(info, src[0]) : src);
        dest[d] = static_cast<uint8_t>(value >> 8);
        dest[d + 1] = static_cast<uint8_t>(value);
        d += 2;
        s += indexed ? 1 : 4;
        break;
    }
    case 0x44:
    case 0x60: {
        // Eight 1-bit pixels, the first in the top bit; the row's end leaves the rest zero. The column ends one short of the pixels used.
        uint32_t bits = 0;
        int32_t shift = 7;
        uint8_t value;
        do {
            value = static_cast<uint8_t>(bits);
            if (width <= x) break;
            bits |= static_cast<uint32_t>(src[s - *sourceOffset] & 1) << (shift & 0x1f);
            value = static_cast<uint8_t>(bits);
            ++s;
            ++x;
            --shift;
        } while (-1 < shift);
        dest[d++] = value;
        x -= 1;
        break;
    }
    case 0x64: {
        // 0x64: the alpha byte only.
        if (depth == 8) {
            dest[d++] = PaletteEntry(info, src[0])[3];
            s += 1;
        } else {
            dest[d++] = src[3];
            s += 4;
        }
        break;
    }
    case 0x65: {
        // 0x65: grey (or one colour channel) and alpha.
        int32_t step = 1;
        if (depth < 9) {
            if (InfoInt(info, kInfoRedBits) < 2) {
                if (InfoInt(info, kInfoGreenBits) > 1) dest[d] = PaletteEntry(info, src[0])[1];
                else if (InfoInt(info, kInfoBlueBits) > 1) dest[d] = PaletteEntry(info, src[0])[0];
            } else {
                dest[d] = PaletteEntry(info, src[0])[2];
            }
            dest[d + 1] = PaletteEntry(info, src[0])[3];
        } else {
            if (InfoInt(info, kInfoRedBits) < 2) {
                if (InfoInt(info, kInfoGreenBits) > 1) dest[d] = src[1];
                else if (InfoInt(info, kInfoBlueBits) > 1) dest[d] = src[0];
            } else {
                dest[d] = src[2];
            }
            step = 4;
            dest[d + 1] = src[3];
        }
        d += 2;
        s += step;
        break;
    }
    default: break;
    }
    *column = x;
    *row = savedRow;
    *sourceOffset = s;
    *destOffset = d;
}

// 0x88bd83
void __cdecl PackShapePixels(const uint8_t *shape, uint8_t *dest, const Info *info, const uint8_t *source, int32_t sourceStride, int32_t platform,
                             uint32_t flags, int32_t reorderWidth, int32_t reorderHeight)
{
    const int32_t bits = EncodeShapeBits(shape, platform);
    const int32_t width = InfoInt(info, kInfoWidth);
    const int32_t height = InfoInt(info, kInfoHeight);
    int32_t stride = EncodeRowBytes(width, bits, platform);
    const bool reordered = (flags & kFlagReorder) != 0;
    const bool consoleSize = platform == kPlatformTiled || platform == kPlatformPaged;
    size_t bytes;
    if (!reordered || !consoleSize) {
        bytes = static_cast<size_t>(stride) * static_cast<size_t>(height);
    } else {
        stride = EncodeRowBytes(reorderWidth, bits, platform);
        bytes = static_cast<size_t>(stride) * static_cast<size_t>(reorderHeight);
    }
    std::memset(dest, 0, bytes);
    const int32_t type = static_cast<int8_t>(shape[0]);
    int32_t sourceRow = 0;
    int32_t destRow = 0;
    uint32_t row = 0;
    for (int32_t y = 0; y < height; ++y) {
        int32_t column = 0;
        int32_t sourceOffset = sourceRow;
        int32_t destOffset = destRow;
        while (column < width) {
            PackPixel(info, source, dest, &sourceOffset, &destOffset, type, &column, &row, width);
            ++column;
        }
        sourceRow += sourceStride;
        destRow += stride;
        ++row;
    }
    if (reordered) {
        int32_t w = width, h = height;
        if (consoleSize) {
            w = reorderWidth;
            h = reorderHeight;
        }
        uint8_t *image = ReorderImage(dest, w, h, bits, type, platform, stride);
        if (image != nullptr) {
            if (!consoleSize) reorderHeight = height;
            std::memcpy(dest, image, static_cast<size_t>(stride) * static_cast<size_t>(reorderHeight));
            FreeMemory(image);
        }
    }
}

// 0x88bf12
uint32_t __cdecl BlockCompress(const Info *info, int32_t stride, const uint8_t *source, uint8_t *dest, int32_t platform, int32_t dropHeader)
{
    const int32_t width = InfoInt(info, kInfoWidth);
    const int32_t height = InfoInt(info, kInfoHeight);
    const uint32_t paddedWidth8 = static_cast<uint32_t>(width + 7) & ~7u;
    const uint32_t paddedHeight8 = static_cast<uint32_t>(height + 7) & ~7u;
    uint32_t blocksWidth = paddedWidth8;
    uint32_t blocksHeight = paddedHeight8;
    if (platform != kPlatformTiled) {
        blocksHeight = static_cast<uint32_t>(height + 3) & ~3u;
        blocksWidth = static_cast<uint32_t>(width + 3) & ~3u;
    }
    const int32_t imageBytes = static_cast<int32_t>(blocksHeight * blocksWidth * 4);
    uint8_t *image = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(imageBytes + 8)));
    uint32_t result = 0;
    if (image != nullptr) {
        std::memset(image, 0, static_cast<size_t>(imageBytes) + 8);
        const int32_t alpha = InfoInt(info, kInfoAlphaBits);
        uint32_t mode;
        if (alpha < 2) mode = alpha == 1 ? 2 : 0;
        else mode = alpha != 3 ? 0x100 : 0x200;
        if (platform == kPlatformTiled) {
            mode = alpha < 1 ? 8 : 10;
            TileBlocks(paddedWidth8, static_cast<int32_t>(paddedHeight8), width, height, stride, source, image);
        } else {
            TilePixels(width, height, stride, source, image);
        }
        int32_t written;
        if (InfoInt(info, kInfoCompression) == 3) written = EncodeRecord(dest, image, imageBytes, &mode);
        else written = EncodeColourRecord(dest, reinterpret_cast<const uint32_t *>(image), imageBytes, &mode);
        const uint32_t padded = static_cast<uint32_t>(written + 0xf) & ~0xfu;
        const int32_t area = height * width * 2;
        dest[4] = static_cast<uint8_t>(area);
        dest[2] = static_cast<uint8_t>(static_cast<uint32_t>(area) >> 16);
        dest[3] = static_cast<uint8_t>(static_cast<uint32_t>(area) >> 8);
        dest[6] = static_cast<uint8_t>(static_cast<uint32_t>(width) >> 8);
        dest[7] = static_cast<uint8_t>(width);
        FreeMemory(image);
        result = padded;
        if (dropHeader != 0) {
            result = padded - 8;
            std::memmove(dest, dest + 8, result);
            std::memset(dest + result, 0, 4);
            std::memset(dest + (padded - 4), 0, 4);
        }
    }
    return result;
}

// 0x88c070
uint32_t __cdecl GmCompress(const Info *info, uint32_t stride, const uint32_t *pixels, uint16_t *output)
{
    const int32_t alpha = InfoInt(info, kInfoAlphaBits);
    uint32_t alphaMode = 0;
    if (alpha > 0) alphaMode = alpha < 5 ? 1u : 0u;
    int32_t quantiser = static_cast<int32_t>(static_cast<float>(100 - InfoInt(info, kInfoQuality)) / 6.25f + 1.5f);
    if (quantiser < 0x12) {
        if (quantiser <= 0) quantiser = 1;
    } else {
        quantiser = 0x11;
    }
    return EncodeGm(pixels, output, InfoInt(info, kInfoWidth), InfoInt(info, kInfoHeight), stride, alpha > 0 ? 1u : 0u, quantiser,
                    static_cast<int32_t>(alphaMode));
}

} // namespace fsh

} // namespace gimex
