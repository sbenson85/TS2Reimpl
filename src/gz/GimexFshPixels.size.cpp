// The FSH back end's pixel functions (lane MX, block 1's decode side; src/gz/GimexFshDecode.h): block-coded images to rows, the
// untiler, YUV 4:2:0 to BGRA, the per-pixel unpacker and the shape decoder that picks among them. Size-built in the original.
#include "gz/GimexFshDecode.h"

#include "game/original.h"
#include "gz/GimexFshBlocks.h"
#include "gz/GimexFshConvert.h"
#include "gz/GimexFshGm.h"
#include "gz/GimexFshLeaves.h"

#include <cstring>

namespace gimex {
namespace fsh {

namespace {

constexpr uint32_t kBigImageBytes = 0xf42400; // above this a block-coded header's size takes 4 bytes
constexpr int32_t kType60 = 0x60;
constexpr int32_t kType61 = 0x61;
constexpr int32_t kType62 = 0x62;
constexpr int32_t kType1e = 0x1e;

inline float Bits(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The record's 3-byte size, written big-endian at `where` (4 bytes above kBigImageBytes).
inline void PutImageSize(uint8_t *where, uint32_t size)
{
    if (static_cast<int32_t>(size) > static_cast<int32_t>(kBigImageBytes)) {
        PutBigEndian(where, size, 4);
    } else {
        where[2] = static_cast<uint8_t>(size);
        where[0] = static_cast<uint8_t>(size >> 16);
        where[1] = static_cast<uint8_t>(size >> 8);
    }
}

} // namespace

void __cdecl CopyRows(const uint8_t *source, uint8_t *dest, int32_t sourceStride, int32_t unused, int32_t width, int32_t rows)
{
    (void)unused;
    const int32_t sourceBytes = sourceStride << 2;
    const int32_t rowBytes = width << 2;
    for (; rows > 0; --rows) {
        std::memcpy(dest, source, static_cast<size_t>(rowBytes));
        dest += rowBytes;
        source += sourceBytes;
    }
}

int32_t __cdecl BlockIndex(int32_t x, int32_t y, int32_t width)
{
    const int32_t row = ((width + 1) >> 1) * (y >> 1) + (x >> 1);
    return ((row * 2 + (y & 1)) * 2) + (x & 1);
}

void __cdecl BlocksToRows(int32_t width, int32_t height, int32_t stride, const uint8_t *blocks, uint8_t *dest)
{
    for (int32_t y = 0; y < height; y += 4) {
        const int32_t rows = (height - y) < 4 ? (height - y) : 4;
        uint8_t *band = dest;
        for (int32_t x = 0; x < width; x += 4) {
            const int32_t columns = (width - x) < 4 ? (width - x) : 4;
            const uint8_t *block = blocks + (BlockIndex(x >> 2, y >> 2, static_cast<int32_t>(static_cast<uint32_t>(width) >> 2)) << 6);
            uint8_t *row = band;
            for (int32_t r = 0; r < rows; ++r) {
                std::memcpy(row, block, static_cast<size_t>(columns << 2));
                block += 0x10;
                row += stride;
            }
            band += 0x10;
        }
        dest += stride << 2;
    }
}

void __cdecl UnblockRows(int32_t width, int32_t height, int32_t stride, const uint8_t *blocks, uint8_t *dest)
{
    for (int32_t y = 0; y < height; y += 4) {
        const int32_t rows = (height - y) < 4 ? (height - y) : 4;
        uint8_t *band = dest;
        for (int32_t x = 0; x < width; x += 4) {
            const int32_t columns = (width - x) < 4 ? (width - x) : 4;
            uint8_t *row = band;
            for (int32_t r = 0; r < rows; ++r) {
                std::memcpy(row, blocks, static_cast<size_t>(columns << 2));
                row += stride;
                blocks += 0x10;
            }
            blocks += (4 - rows) << 4;
            band += 0x10;
        }
        dest += stride << 2;
    }
}

void __cdecl DecodeBlockImage(uint8_t *record, int32_t type, uint8_t *dest, int32_t copyHeader, int32_t pitch, int32_t width,
                              int32_t height)
{
    int32_t blockWidth = (width + 3) & ~3;
    int32_t blockHeight = (height + 3) & ~3;
    int32_t eightBlocks = 0;
    if (HasHeader28fb(record) != 0 || type == kType1e) {
        blockWidth = (width + 7) & ~7;
        blockHeight = (height + 7) & ~7;
        eightBlocks = 1;
    }
    const int32_t imageBytes = blockHeight * blockWidth * 4;
    uint8_t *copy = nullptr;
    int32_t codedWidth = blockWidth;
    if (copyHeader != 0) {
        const int32_t blockBytes = (type == kType60 || type == kType1e) ? 8 : 16;
        const int32_t coded = (blockHeight >> 2) * (blockWidth >> 2) * blockBytes;
        copy = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(coded + 8)));
        std::memcpy(copy + 8, record, static_cast<size_t>(coded));
    }
    codedWidth >>= 2;
    uint8_t *decoded = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(imageBytes)));
    if (decoded != nullptr) {
        std::memset(decoded, 0, static_cast<size_t>(imageBytes));
        const uint8_t *source;
        if (copyHeader != 0) {
            PutImageSize(copy + 2, static_cast<uint32_t>(imageBytes));
            const bool big = static_cast<int32_t>(imageBytes) > static_cast<int32_t>(kBigImageBytes);
            if (type == kType61) {
                *reinterpret_cast<uint16_t *>(copy) = big ? 0xfb9a : 0xfb1a;
            } else if (type == kType62) {
                *reinterpret_cast<uint16_t *>(copy) = big ? 0xfb9c : 0xfb1c;
            } else if (type == kType1e) {
                *reinterpret_cast<uint16_t *>(copy) = big ? 0xfba8 : 0xfb28;
            } else {
                *reinterpret_cast<uint16_t *>(copy) = big ? 0xfb98 : 0xfb18;
            }
            if (big) {
                copy[6] = 0;
            } else {
                copy[5] = 0;
                copy[6] = static_cast<uint8_t>(codedWidth);
                codedWidth = blockHeight >> 2;
            }
            copy[7] = static_cast<uint8_t>(codedWidth);
            source = copy;
        } else {
            PutImageSize(record + 2, static_cast<uint32_t>(imageBytes));
            source = record;
        }
        DecodeBlocks(decoded, source, nullptr);
        if (eightBlocks != 0) {
            if (copy != nullptr) FreeMemory(copy);
            copy = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(imageBytes)));
            BlocksToRows(blockWidth, blockHeight, blockWidth << 2, decoded, copy);
            CopyRows(copy, dest, blockWidth, blockHeight, width, height);
        } else {
            UnblockRows(width, height, pitch, decoded, dest);
        }
        FreeMemory(decoded);
    }
    if (copy != nullptr) FreeMemory(copy);
}

uint8_t *__cdecl Untile(const uint8_t *source, int32_t destPitch, int32_t rowStride, int32_t bits, int32_t kind, int32_t platform,
                        int32_t width, int32_t height)
{
    const int32_t allocBytes = (destPitch > rowStride ? destPitch : rowStride) * height;
    uint8_t *dest = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(allocBytes)));
    if (dest == nullptr) return nullptr;
    std::memset(dest, 0, static_cast<size_t>(destPitch * height));
    if (platform == 6 || platform == 0x10) {
        if (bits == 4) {
            Unswizzle4(source, dest, width, height);
        } else if (bits == 8) {
            Unswizzle8(source, dest, width, height);
        } else if (bits == 0x10 || bits == 0xf) {
            Unswizzle16(source, dest, width, height);
        } else {
            return nullptr; // ORIGINAL-BUG: `dest` leaks (the caller gets a null for a buffer it never frees)
        }
    }
    if (platform != 5 && platform != 8 && platform != 2 && platform != 0xf && platform != 0xc && platform != 0x12) return dest;

    int32_t bytesPerPixel;
    if (bits <= 8) {
        bytesPerPixel = 1;
    } else if (bits <= 0x10) {
        bytesPerPixel = 2;
    } else if (bits <= 0x18) {
        bytesPerPixel = 3;
    } else {
        bytesPerPixel = bits <= 0x20 ? 4 : 0;
    }
    const bool tiled = platform == 5 || platform == 0xf;
    int32_t rowBase = 0;
    for (int32_t y = 0; y < height; ++y) {
        int32_t sourceOffset = rowBase;
        int32_t destOffset = rowBase;
        for (int32_t x = 0; x < width; ++x) {
            bool done = false;
            if (tiled) {
                sourceOffset = TileOffset(x, y, width, bits);
                if (kind == 0x16 || kind == 0x1e) {
                    const uint8_t *from = source + sourceOffset;
                    uint8_t *to = dest + destOffset;
                    to[0] = from[0];
                    to[1] = from[1];
                    to[2] = from[0x20];
                    to[3] = from[0x21];
                    done = true;
                }
            } else {
                const int32_t index = InterleavedIndex(x, y, width, height);
                const int32_t quotient = index / width;
                const int32_t remainder = index % width;
                if (kind == 0x40 || kind == 0x79 || kind == 0x7a) {
                    destOffset = quotient * rowStride + remainder / 2;
                } else {
                    destOffset = remainder * bytesPerPixel + quotient * rowStride;
                }
            }
            if (!done) {
                if (kind == 0x40 || kind == 0x79) {
                    const uint8_t kept = (x & 1) != 0 ? (source[sourceOffset] & 0xf0) : (source[sourceOffset] & 0x0f);
                    dest[destOffset] = static_cast<uint8_t>(kept | dest[destOffset]);
                } else if (kind == 0x7a) {
                    const uint8_t kept = (x & 1) != 0 ? (source[sourceOffset] & 0x0f) : (source[sourceOffset] & 0xf0);
                    dest[destOffset] = static_cast<uint8_t>(kept | dest[destOffset]);
                } else {
                    std::memcpy(dest + destOffset, source + sourceOffset, static_cast<size_t>(bytesPerPixel));
                }
            }
            if (bits != 4 || (x & 1) != 0) {
                sourceOffset += bytesPerPixel;
                destOffset += bytesPerPixel;
            }
        }
        rowBase += rowStride;
    }
    return dest;
}

uint8_t *__cdecl AssemblePlanes(const uint8_t *source, int32_t pitch, int32_t kind, int32_t platform, int32_t width, int32_t height)
{
    uint8_t *lumaPlane = Untile(source, pitch, width, 8, kind, platform, width, height);
    if (lumaPlane == nullptr) return nullptr;
    const int32_t halfHeight = (height - (height >> 31)) >> 1;
    const int32_t area = width * height;
    const int32_t halfWidth = (width - (width >> 31)) >> 1;
    const int32_t halfPitch = (pitch - (pitch >> 31)) >> 1;
    uint8_t *uPlane = Untile(source + area, halfPitch, halfWidth, 8, kind, platform, halfWidth, halfHeight);
    if (uPlane == nullptr) {
        FreeMemory(lumaPlane);
        return nullptr;
    }
    const int32_t quarter = (area + ((area >> 31) & 3)) >> 2;
    uint8_t *vPlane = Untile(source + area + quarter, halfPitch, halfWidth, 8, kind, platform, halfWidth, halfHeight);
    uint8_t *planes = nullptr;
    if (vPlane != nullptr) {
        const int32_t total = (area * 3 - ((area * 3) >> 31)) >> 1;
        planes = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(total)));
        if (planes != nullptr) {
            std::memcpy(planes, lumaPlane, static_cast<size_t>(area));
            std::memcpy(planes + area, uPlane, static_cast<size_t>(quarter));
            std::memcpy(planes + area + quarter, vPlane, static_cast<size_t>(quarter));
        }
    }
    FreeMemory(lumaPlane);
    FreeMemory(uPlane);
    if (vPlane != nullptr) FreeMemory(vPlane);
    return planes;
}

void __cdecl YuvPixel(uint8_t *dest, int32_t luma, int32_t u, int32_t v)
{
    int32_t in[3];
    int32_t out[3];
    in[0] = luma;
    in[1] = static_cast<uint8_t>(u);
    in[2] = static_cast<uint8_t>(v);
    YCbCrToRgb(in, out);
    dest[2] = static_cast<uint8_t>(out[0]);
    dest[1] = static_cast<uint8_t>(out[1]);
    dest[3] = 0xff;
    dest[0] = static_cast<uint8_t>(out[2]);
}

void __cdecl YuvToRgba(uint8_t *dest, int32_t pitch, const uint8_t *planes, int32_t width, int32_t height, int32_t kind, int32_t platform,
                       int32_t untile)
{
    const uint8_t *base = planes;
    bool owned = false;
    if (untile != 0) {
        uint8_t *assembled = AssemblePlanes(planes, pitch, kind, platform, width, height);
        if (assembled != nullptr) {
            base = assembled;
            owned = true;
        }
    }
    if (height > 0) {
        const int32_t area = width * height;
        const int32_t evenWidth = (width - (width >> 31)) & ~1;
        const int32_t fiveQuarters = area * 5;
        const int32_t vDistance = ((fiveQuarters + ((fiveQuarters >> 31) & 3)) >> 2) - area;
        int32_t pairs = static_cast<int32_t>((static_cast<uint32_t>(height - 1) >> 1) + 1);
        int32_t chromaRows = 0; // the pair count so far times evenWidth
        int32_t lumaRow = 0;    // offset of the pair's top luma row
        uint8_t *rowDest = dest;
        for (; pairs != 0; --pairs) {
            const int32_t chromaOffset = (chromaRows - (chromaRows >> 31)) >> 1;
            const uint8_t *chroma = base + area + chromaOffset;
            const uint8_t *luma = base + lumaRow;
            uint8_t *top = rowDest;
            uint8_t *bottom = rowDest + (width << 2);
            for (int32_t x = 0; x < width; x += 2) {
                const int32_t u = *chroma;
                const int32_t v = chroma[vDistance];
                YuvPixel(top, luma[x], u, v);
                YuvPixel(top + 4, luma[x + 1], u, v);
                // ORIGINAL-BUG: both pixels of the bottom row take the luma at (x + 1), the right one's, not their own
                YuvPixel(bottom, luma[width + x + 1], u, v);
                YuvPixel(bottom + 4, luma[width + x + 1], u, v);
                top += 8;
                bottom += 8;
                ++chroma;
            }
            lumaRow += width << 1;
            chromaRows += evenWidth;
            rowDest += pitch << 1;
        }
    }
    if (untile != 0 && owned && base != nullptr) FreeMemory(const_cast<uint8_t *>(base));
}

void __cdecl YuvPairs(uint8_t *dest, int32_t unused, const uint8_t *source, int32_t width, int32_t rows)
{
    (void)unused;
    const bool odd = (width & 1) != 0;
    for (; rows > 0; --rows) {
        for (int32_t x = 0; x < width; x += 2) {
            YuyvPairToBgra(dest, source);
            dest += (odd && x == width - 1) ? 4 : 8;
            source += 4;
        }
    }
}

void __cdecl UnpackPixel(uint32_t type, int32_t *destOffset, int32_t *sourceOffset, uint8_t *dest, const uint8_t *source, uint32_t *column)
{
    type &= 0x7f;
    int32_t d = *destOffset;
    const uint32_t x = *column;
    int32_t s = *sourceOffset;
    uint8_t *out = dest + d;
    const uint8_t *in = source + s;
    auto bigWord = [&]() { return static_cast<uint32_t>((in[0] << 8) | in[1]); };
    auto littleWord = [&]() { return static_cast<uint32_t>(*reinterpret_cast<const uint16_t *>(in)); };
    auto store4 = [&](uint32_t value, int32_t sourceBytes) {
        *reinterpret_cast<uint32_t *>(out) = value;
        s += sourceBytes;
        d += 4;
    };
    auto copy1 = [&]() {
        *out = *in;
        ++d;
        ++s;
    };
    // `highWhenOdd`: which nibble an even / odd column takes; the source byte is consumed on an odd column.
    auto nibble = [&](bool highWhenOdd) {
        uint8_t value;
        if (highWhenOdd) {
            value = (x & 1) != 0 ? static_cast<uint8_t>(*in >> 4) : *in;
        } else {
            value = (x & 1) != 0 ? *in : static_cast<uint8_t>(*in >> 4);
        }
        *out = value & 0xf;
        ++d;
        if ((x & 1) != 0) ++s;
    };
    auto rgb3 = [&]() { // bytes 0, 1, 2 land at 2, 1, 0 with an opaque alpha
        out[3] = 0xff;
        out[2] = in[0];
        out[1] = in[1];
        out[0] = in[2];
        s += 3;
        d += 4;
    };
    auto reversed4 = [&]() { // 0, 1, 2, 3 land at 3, 2, 1, 0
        out[3] = in[0];
        out[2] = in[1];
        out[1] = in[2];
        out[0] = in[3];
        s += 4;
        d += 4;
    };
    auto straight4 = [&]() {
        out[3] = in[3];
        out[2] = in[2];
        out[1] = in[1];
        out[0] = in[0];
        s += 4;
        d += 4;
    };
    switch (type) {
    case 0x01:
    case 0x40:
    case 0x79:
        nibble(true);
        break;
    case 0x10:
    case 0x18:
    case 0x58:
    case 0x7a:
        nibble(false);
        break;
    case 0x02:
    case 0x11:
    case 0x12:
    case 0x19:
    case 0x41:
    case 0x59:
    case 0x64:
    case 0x7b:
        copy1();
        break;
    case 0x03:
        store4(Colour1555Swizzled(littleWord()), 2);
        break;
    case 0x04:
    case 0x43:
    case 0x5d:
        rgb3();
        break;
    case 0x05:
        out[2] = in[0];
        out[1] = in[1];
        out[0] = in[2];
        out[3] = in[3];
        s += 4;
        d += 4;
        break;
    case 0x13:
        store4(Colour88(bigWord()), 2);
        break;
    case 0x14:
    case 0x5b:
        store4(Colour565(bigWord()), 2);
        break;
    case 0x15:
        store4(Colour3555(bigWord()), 2);
        break;
    case 0x16:
    case 0x1e:
    case 0x5e:
        reversed4();
        break;
    case 0x42:
        store4(Colour1555Alt(littleWord()), 2);
        break;
    case 0x44: {
        const uint32_t bit = x & 7;
        uint8_t value = *in;
        if (bit != 7) value = static_cast<uint8_t>(value >> (7 - bit));
        *out = value & 1;
        ++d;
        if (bit == 7) ++s;
        break;
    }
    case 0x5a:
        store4(Colour1555(bigWord()), 2);
        break;
    case 0x5c:
        store4(Colour4444(bigWord()), 2);
        break;
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x7d:
        straight4();
        break;
    case 0x65:
        out[3] = in[1];
        out[2] = in[0];
        out[1] = in[0];
        out[0] = in[0];
        s += 2;
        d += 4;
        break;
    case 0x66:
        store4(Colour6666(ReadLittleEndian(in, 3)), 3);
        break;
    case 0x67:
        store4(Colour655(littleWord()), 2);
        break;
    case 0x6a:
        store4(Colour2101010(ReadLittleEndian(in, 3)), 4);
        break;
    case 0x6d:
        store4(Colour4444(littleWord()), 2);
        break;
    case 0x78:
        store4(Colour565(littleWord()), 2);
        break;
    case 0x7e:
        store4(Colour1555(littleWord()), 2);
        break;
    case 0x7f:
        out[3] = 0xff;
        out[2] = in[2];
        out[1] = in[1];
        out[0] = in[0];
        s += 3;
        d += 4;
        break;
    default:
        break;
    }
    *destOffset = d;
    *sourceOffset = s;
    *column = x;
}

int32_t __cdecl DecodeShape(uint8_t *record, uint8_t *dest, int32_t pitch, int32_t platform)
{
    uint8_t *untiled = nullptr;
    const int32_t bits = ShapeBits(record, platform);
    const uint32_t width = ReadWord(record + 0x18, 4, platform);
    const uint32_t height = ReadWord(record + 0x1c, 4, platform);
    const uint32_t type = ReadWord(record, 1, platform) & 0x7f;
    uint8_t *data = record + ReadWord(record + 8, 4, platform);
    const int32_t copyHeader = ((type > 0x5f && type < 99) || type == 0x1e) ? 1 : 0;
    int32_t rowBytes = RowBytes(static_cast<int32_t>(width), bits, platform);
    if (HasCodec(record, 3, platform) != 0 || copyHeader != 0) {
        DecodeBlockImage(data, static_cast<int32_t>(type), dest, copyHeader, pitch, static_cast<int32_t>(width),
                         static_cast<int32_t>(height));
        return 1;
    }
    if (HasCodec(record, 4, platform) != 0) {
        const uint32_t paddedWidth = (width + 0xf) & 0xfffffff0;
        const uint32_t imageBytes = ((height + 0xf) & 0xfffffff0) * paddedWidth * 4;
        uint8_t *decoded = static_cast<uint8_t *>(Allocate(imageBytes));
        uint32_t *sizeWord = reinterpret_cast<uint32_t *>(data + 4);
        *sizeWord = *sizeWord ^ ((*sizeWord ^ imageBytes) & 0xfffff);
        const uint32_t stride = paddedWidth << 2;
        DecodeGm(decoded, static_cast<int32_t>(stride), data, static_cast<int32_t>(width), static_cast<int32_t>(height), 0);
        const uint8_t *row = decoded;
        uint8_t *out = dest;
        for (int32_t remaining = static_cast<int32_t>(height); remaining > 0; --remaining) {
            std::memcpy(out, row, static_cast<size_t>(pitch));
            out += pitch;
            row += stride;
        }
        FreeMemory(decoded);
        return 1;
    }
    const uint8_t first = *record;
    if (first == 0x1c || first == 0x6c) {
        const uint32_t tiled = IsTiled(record, platform, width, height, bits);
        YuvToRgba(dest, pitch, data, static_cast<int32_t>(width), static_cast<int32_t>(height), static_cast<int32_t>(type), platform,
                  static_cast<int32_t>(tiled));
        return 1;
    }
    if (first == 0x68 || first == 0x63) {
        YuvPairs(dest, pitch, data, static_cast<int32_t>(width), static_cast<int32_t>(height));
        return 1;
    }
    const uint32_t tiled = IsTiled(record, platform, width, height, bits);
    int32_t sourceRow = 0;
    int32_t destRow = 0;
    if (tiled != 0) {
        if (platform == 5 || platform == 6) {
            uint32_t paddedWidth;
            uint32_t paddedHeight;
            if (platform == 5) {
                PaddedSize(&paddedWidth, &paddedHeight, static_cast<int32_t>(width), static_cast<int32_t>(height), bits);
            } else {
                PageSize(&paddedWidth, &paddedHeight, width, height, bits);
            }
            rowBytes = RowBytes(static_cast<int32_t>(paddedWidth), bits, platform);
            untiled = Untile(data, pitch, rowBytes, bits, static_cast<int32_t>(type), platform, static_cast<int32_t>(paddedWidth),
                             static_cast<int32_t>(paddedHeight));
        } else {
            untiled = Untile(data, pitch, rowBytes, bits, static_cast<int32_t>(type), platform, static_cast<int32_t>(width),
                             static_cast<int32_t>(height));
        }
    }
    const uint8_t *source = (tiled != 0 && untiled != nullptr) ? untiled : data;
    for (int32_t y = static_cast<int32_t>(height); y > 0; --y) {
        int32_t destOffset = destRow;
        int32_t sourceOffset = sourceRow;
        for (uint32_t x = 0; static_cast<int32_t>(x) < static_cast<int32_t>(width); ++x) {
            UnpackPixel(type, &destOffset, &sourceOffset, dest, source, &x);
        }
        sourceRow += rowBytes;
        destRow += pitch;
    }
    if (untiled != nullptr) FreeMemory(untiled);
    return 1;
}

} // namespace fsh
} // namespace gimex
