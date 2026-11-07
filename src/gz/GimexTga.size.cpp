// Gimex's TGA codec (row 6 of the codec table, lane MN; src/gz/Gimex.h, src/gz/GimexFormats.h): reads and writes Targa
// images (8-bit indexed or gray, 15 / 16, 24 and 32 bit true colour, run-length coded ones too).
#include "gz/GimexFormats.h"

#include "game/original.h"
#include "ifaces/cIGZFile.h"

#include <cstring>

namespace gimex {

namespace tga {

namespace {

constexpr uint32_t kFourcc = 0x2e544741; // "AGT." in memory
constexpr uint32_t kInfoVersion = 0x159;
constexpr uint32_t kScoreMatch = 100;
constexpr uint32_t kScoreLikely = 0x3c;
constexpr uint32_t kInstanceBytes = 0x18;

// The stream and memory helpers are Gimex's shared ones (src/gz/Gimex.h).

// About's descriptive words (immediates in the original's code).
constexpr uint32_t kAboutTag = 0x54504943;
constexpr uint32_t kAboutFlagsMask = 0xffff93d7;
constexpr uint32_t kAboutFlags = 0x93d7;

// The 18-byte file header.
constexpr uint32_t kHeaderBytes = 0x12;
constexpr uint32_t kOffsetIdLength = 0;
constexpr uint32_t kOffsetColorMapType = 1;
constexpr uint32_t kOffsetImageType = 2;
constexpr uint32_t kOffsetColorMapFirst = 3;
constexpr uint32_t kOffsetColorMapLength = 5;
constexpr uint32_t kOffsetColorMapEntryBits = 7;
constexpr uint32_t kOffsetOriginX = 8;
constexpr uint32_t kOffsetOriginY = 10;
constexpr uint32_t kOffsetWidth = 12;
constexpr uint32_t kOffsetHeight = 14;
constexpr uint32_t kOffsetDepth = 16;
constexpr uint32_t kOffsetDescriptor = 17;

constexpr uint8_t kDescriptorFlipX = 0x10; // pixels run right to left
constexpr uint8_t kDescriptorTopDown = 0x20;
constexpr uint8_t kDescriptorAlphaMask = 0xf;

// Image types (bit 3: run-length coded).
constexpr uint32_t kTypeColorMapped = 1;
constexpr uint32_t kTypeGray = 3;
constexpr uint32_t kTypeKindMask = 7;
constexpr uint8_t kTypeRunLength = 8;

constexpr uint32_t kPaletteEntries = 0x100;
constexpr int32_t kPaletteMaxBytes = 1024;
constexpr uint32_t kProbeMaxPaletteFirst = 0x100;
constexpr uint32_t kProbeMaxPaletteLength = 0x101;

uint32_t Load16(const uint8_t *header, uint32_t offset)
{
    uint16_t value;
    std::memcpy(&value, header + offset, sizeof(value));
    return value;
}

void Store16(uint8_t *header, uint32_t offset, uint32_t value)
{
    const uint16_t narrow = static_cast<uint16_t>(value);
    std::memcpy(header + offset, &narrow, sizeof(narrow));
}

int32_t RowBytes(int32_t bits, int32_t count)
{
    return static_cast<int32_t>((static_cast<uint32_t>(bits + 7) & ~7u) * static_cast<uint32_t>(count)) >> 3;
}

// A 15 / 16-bit pixel (5 bits each, the top bit an alpha) into B, G, R, A bytes.
void Expand16(uint8_t *dst, uint8_t low, uint8_t high, bool hasAlpha)
{
    dst[3] = hasAlpha ? static_cast<uint8_t>(static_cast<int8_t>(high) >> 7) : 0xff;
    dst[2] = static_cast<uint8_t>((((high >> 2) & 0x1fu) * 0xffu) / 0x1fu);
    dst[1] = static_cast<uint8_t>(((((high & 3u) << 3) | (low >> 5)) * 0xffu) / 0x1fu);
    dst[0] = static_cast<uint8_t>(((low & 0x1fu) * 0xffu) / 0x1fu);
}

// B, G, R, A bytes into a 16-bit pixel the way the writer packs them: the alpha is ORed in above the red and the fields
// are shifted up together.
uint16_t Pack16(const uint8_t *src, bool hasAlpha)
{
    const uint32_t alpha = hasAlpha ? static_cast<uint32_t>(src[3] >> 7) : 0xffu;
    uint16_t value = static_cast<uint16_t>((src[2] >> 3) | (alpha << 5));
    value = static_cast<uint16_t>((value << 5) | (src[1] >> 3));
    value = static_cast<uint16_t>((value << 5) | (src[0] >> 3));
    return value;
}

// 0x882380: reverses a row of `count` pixels, dwords when the decoded pixels are 32 bits, else bytes.
void FlipRow(uint8_t *row, int32_t count, int32_t bitsPerPixel)
{
    const int32_t half = count >> 1;
    if (bitsPerPixel == 0x20) {
        uint32_t *first = reinterpret_cast<uint32_t *>(row);
        uint32_t *last = first + count - 1;
        for (int32_t i = 0; i < half; ++i, ++first, --last) {
            uint32_t a;
            uint32_t b;
            std::memcpy(&a, first, sizeof(a));
            std::memcpy(&b, last, sizeof(b));
            std::memcpy(first, &b, sizeof(b));
            std::memcpy(last, &a, sizeof(a));
        }
    } else {
        uint8_t *first = row;
        uint8_t *last = row + count - 1;
        for (int32_t i = 0; i < half; ++i, ++first, --last) {
            const uint8_t a = *first;
            *first = *last;
            *last = a;
        }
    }
}

// One pixel of `bits` bits (8, 15 / 16, 24, 32) from `src` into `dst`: an index byte, or B, G, R, A. Returns the bytes
// the pixel takes in `dst`.
uint32_t DecodePixel(uint8_t *dst, const uint8_t *src, int32_t bits, bool hasAlpha)
{
    if (bits == 8) {
        *dst = *src;
        return 1;
    }
    if (bits == 0xf || bits == 0x10) {
        Expand16(dst, src[0], src[1], hasAlpha);
        return 4;
    }
    if (bits == 0x18) {
        dst[3] = 0xff;
        dst[2] = src[2];
        dst[1] = src[1];
        dst[0] = src[0];
        return 4;
    }
    if (bits == 0x20) {
        dst[3] = src[3];
        dst[2] = src[2];
        dst[1] = src[1];
        dst[0] = src[0];
        return 4;
    }
    return 0;
}

// 0x8823cf: one row of a file's pixels into `dst` (through `buffer`, a row's bytes), as the last read's count. A
// run-length coded row (info->jpegFlag) is a series of packets: a header byte (bit 7: one pixel repeated, else raw
// pixels; the low seven bits the count less one) up to the row's width.
uint32_t DecodeRow(ifaces::cIGZFile *stream, uint8_t *dst, uint8_t *buffer, const Info *info)
{
    const int32_t width = info->width;
    const int32_t bits = info->sourceBits;
    const bool hasAlpha = info->alphaBits != 0;
    uint32_t result;
    if (info->jpegFlag == 0) {
        result = StreamRead(stream, buffer, RowBytes(bits, width));
        const uint8_t *src = buffer;
        for (int32_t i = 0; i < width; ++i) {
            const uint32_t used = DecodePixel(dst, src, bits, hasAlpha);
            dst += used;
            src += bits == 8 ? 1 : bits == 0x18 ? 3 : bits == 0x20 ? 4 : 2;
        }
        return result;
    }
    int32_t placed = 0;
    const int32_t pixelBytes = RowBytes(bits, 1);
    do {
        uint8_t packet;
        result = StreamRead(stream, &packet, 1);
        const uint32_t count = (packet & 0x7fu) + 1;
        placed += static_cast<int32_t>(count);
        if ((packet & 0x80) != 0) {
            // One pixel, repeated.
            if (bits == 8 || bits == 0xf || bits == 0x10 || bits == 0x18 || bits == 0x20) {
                const int32_t take = bits == 8 ? 1 : bits == 0x18 ? 3 : bits == 0x20 ? 4 : 2;
                result = StreamRead(stream, buffer, take);
                for (uint32_t i = 0; i < count; ++i) dst += DecodePixel(dst, buffer, bits, hasAlpha);
            }
        } else {
            result = StreamRead(stream, buffer, RowBytes(bits, static_cast<int32_t>(count)));
            const uint8_t *src = buffer;
            for (uint32_t i = 0; i < count; ++i) {
                dst += DecodePixel(dst, src, bits, hasAlpha);
                src += pixelBytes;
            }
        }
    } while (placed < width);
    return result;
}

// 0x882fe3: how many pixels of `size` bytes from the first on are equal to the first, at most 128 and at most `limit`.
int32_t RunCount(const uint8_t *pixels, int32_t limit, int32_t size)
{
    const int32_t cap = limit < 0x81 ? limit : 0x80;
    int32_t count = 0;
    while (count < cap && std::memcmp(pixels + count * size, pixels, static_cast<size_t>(size)) == 0) ++count;
    return count;
}

// One pixel (B, G, R, A, or an index) into the file's `bits` (8, 16, 24, 32) bits; advances `out`.
uint8_t *EncodePixel(uint8_t *out, const uint8_t *src, int32_t bits, const uint8_t *map, bool hasAlpha)
{
    if (bits == 0x20) {
        out[0] = src[0];
        out[1] = src[1];
        out[2] = src[2];
        out[3] = src[3];
        return out + 4;
    }
    if (bits == 0x18) {
        out[0] = src[0];
        out[1] = src[1];
        out[2] = src[2];
        return out + 3;
    }
    if (bits == 0x10) {
        const uint16_t value = Pack16(src, hasAlpha);
        out[0] = static_cast<uint8_t>(value);
        out[1] = static_cast<uint8_t>(value >> 8);
        return out + 2;
    }
    if (bits == 8) {
        *out = map[src[0]];
        return out + 1;
    }
    return out;
}

// 0x88302f: one row of `width` pixels as the file stores it into `dst`; the bytes written. `map` turns an index into the
// byte stored (a gray palette's value, or the index itself).
int32_t EncodeRow(const uint8_t *src, uint8_t *dst, int32_t width, int32_t depth, const uint8_t *map, bool runLength,
                  bool hasAlpha)
{
    const int32_t bits = depth == 0xf ? 0x10 : depth;
    uint8_t *out = dst;
    if (!runLength) {
        for (int32_t i = 0; i < width; ++i) {
            out = EncodePixel(out, src, bits, map, hasAlpha);
            src += bits == 8 ? 1 : 4;
        }
        return static_cast<int32_t>(out - dst);
    }
    const int32_t stride = bits == 8 ? 1 : 4;
    int32_t remaining = width;
    while (remaining > 0) {
        const int32_t run = RunCount(src, remaining, stride);
        bool raw = run < 3;
        if (raw && run >= 2) raw = RunCount(src + run * stride, remaining - run, stride) < 2 && bits < 9;
        if (raw) {
            // A raw packet: pixels up to the next worthwhile run.
            int32_t length = 1;
            if (remaining > 1) {
                const uint8_t *probe = src;
                int32_t left = remaining;
                do {
                    if (length > 0x7f) break;
                    const int32_t next = RunCount(probe + stride, left - 1, stride);
                    if (next > 2 || (next > 1 && bits > 8)) break;
                    ++length;
                    --left;
                    probe += stride;
                } while (length < remaining);
            }
            int32_t take = remaining;
            if (length < remaining - 3) take = length;
            if (take > 0x80) take = 0x80;
            *out++ = static_cast<uint8_t>(take - 1);
            remaining -= take;
            for (int32_t i = 0; i < take; ++i) {
                out = EncodePixel(out, src, bits, map, hasAlpha);
                src += stride;
            }
        } else {
            *out++ = static_cast<uint8_t>((run - 1) | 0x80);
            out = EncodePixel(out, src, bits, map, hasAlpha);
            src += run * stride;
            remaining -= run;
        }
    }
    return static_cast<int32_t>(out - dst);
}

} // namespace

About *__stdcall NewAbout()
{
    About *about = static_cast<About *>(Allocate(sizeof(About)));
    if (about != nullptr) {
        std::memset(about, 0, sizeof(About));
        about->size = sizeof(About);
        about->fourcc = kFourcc;
        about->version = kInfoVersion;
        about->flags = (about->flags & kAboutFlagsMask) | kAboutFlags;
        about->defaultQuality = 100;
        about->unknown1c = kAboutTag;
        std::strcpy(about->extensions[0], ".tga");
        std::strcpy(about->extensions[1], ".vda");
        std::strcpy(about->extensions[2], ".icb");
        std::strcpy(about->extensions[3], ".vst");
        std::strcpy(about->author, "FrANK G. Barchard");
        std::strcpy(about->revision, "1.38");
        std::strcpy(about->shortName, "TGA");
        std::strcpy(about->typeName, "Targa");
        std::strcpy(about->description, "Targa");
    }
    return about;
}

int32_t __stdcall Probe(ifaces::cIGZFile *stream)
{
    StreamSeek(stream, 0, 0);
    // ORIGINAL-BUG: a stream of fewer than 18 bytes leaves the header as the stack held it; ours reads zeros.
    uint8_t header[kHeaderBytes] = {};
    uint32_t score = 0;
    if (StreamRead(stream, header, kHeaderBytes) != 0) {
        const uint32_t colorMapType = header[kOffsetColorMapType];
        const uint32_t imageType = header[kOffsetImageType];
        const uint32_t depth = header[kOffsetDepth];
        const bool typeOk = (imageType != 0 && imageType < 4) || imageType - 9 < 3;
        const bool depthOk = depth == 8 || depth == 0xf || depth == 0x10 || depth == 0x18 || depth == 0x20;
        if (colorMapType < 2 && typeOk && depthOk) {
            score = kScoreLikely;
            const uint32_t width = Load16(header, kOffsetWidth);
            const uint32_t height = Load16(header, kOffsetHeight);
            if (width - 1 < 0x1000 && height - 1 < 0x800 && Load16(header, kOffsetColorMapFirst) < kProbeMaxPaletteFirst &&
                Load16(header, kOffsetColorMapLength) < kProbeMaxPaletteLength && header[kOffsetColorMapEntryBits] < 0x21 &&
                Load16(header, kOffsetOriginX) < 0x1001 && Load16(header, kOffsetOriginY) < 0x801) {
                score = kScoreMatch;
            }
        }
    }
    return static_cast<int32_t>(score);
}

int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *, int32_t)
{
    Instance *instance = static_cast<Instance *>(Allocate(kInstanceBytes));
    if (instance == nullptr) return 0;
    instance->unknownC = 0;
    instance->unknown14 = 0;
    instance->stream = stream;
    instance->frameCount = 1;
    instance->fourcc = kFourcc;
    instance->size = kInstanceBytes;
    *out = instance;
    return 1;
}

Info *__stdcall GetInfo(Instance *instance, int32_t)
{
    ifaces::cIGZFile *const stream = instance->stream;
    StreamSeek(stream, 0, 0);
    const uint32_t length = StreamLength(stream);
    // ORIGINAL-BUG: a file of fewer than 18 bytes leaves the header as the stack held it; ours reads zeros.
    uint8_t header[kHeaderBytes] = {};
    if (StreamRead(stream, header, kHeaderBytes) == 0) return nullptr;
    const uint32_t width = Load16(header, kOffsetWidth);
    const uint32_t height = Load16(header, kOffsetHeight);
    const uint32_t colorMapLength = Load16(header, kOffsetColorMapLength);
    const uint32_t colorMapFirst = Load16(header, kOffsetColorMapFirst);
    const uint32_t imageType = header[kOffsetImageType];
    const uint8_t descriptor = header[kOffsetDescriptor];
    const uint32_t entryBits = header[kOffsetColorMapEntryBits];
    const bool hasColorMap = header[kOffsetColorMapType] != 0;
    const int32_t originX = static_cast<int16_t>(Load16(header, kOffsetOriginX));
    const int32_t originY = static_cast<int16_t>(Load16(header, kOffsetOriginY));
    uint32_t depth = header[kOffsetDepth];
    const int32_t flippedY = (descriptor & kDescriptorTopDown) != 0 ? originY : -originY;
    const int32_t flippedX = (descriptor & kDescriptorFlipX) != 0 ? -originX : originX;
    uint32_t alphaDepth = depth - 0x18;
    if (depth == 0xf || depth == 0x10 || (depth == 8 && hasColorMap && (entryBits == 0xf || entryBits == 0x10))) {
        alphaDepth = (descriptor & kDescriptorAlphaMask) == 1 ? 1 : 0;
    }
    uint32_t alphaBits = 0;
    if (0 < static_cast<int32_t>(alphaDepth)) alphaBits = alphaDepth;
    if (8 < static_cast<int32_t>(alphaBits)) alphaBits = 8;
    // Past the identification field is the colour map.
    uint32_t status = StreamSeek(stream, header[kOffsetIdLength] + kHeaderBytes, 0) ? 1 : 0;
    uint32_t mapCount = colorMapLength;
    if ((imageType & kTypeKindMask) != kTypeColorMapped) {
        mapCount = 0;
        if ((imageType & kTypeKindMask) == kTypeGray) mapCount = kPaletteEntries;
    }
    Info *info = static_cast<Info *>(Allocate(kWriteInfoSize));
    if (info == nullptr) return nullptr;
    std::memset(info, 0, kWriteInfoSize);
    info->width = static_cast<int32_t>(width);
    info->height = static_cast<int32_t>(height);
    info->imageBytes = length;
    info->jpegFlag = (imageType >> 3) & 1;
    info->alphaBits = alphaBits;
    info->originX = flippedX;
    info->originY = flippedY;
    info->redBits = 8;
    info->greenBits = 8;
    info->blueBits = 8;
    info->signature = kFourcc;
    info->size = kWriteInfoSize;
    info->version = kInfoVersion;
    info->quality = 100;
    if (depth < 0xf) {
        if (depth == 0) depth = 8;
        info->sourceBits = static_cast<int32_t>(depth);
        info->unknown20 = colorMapFirst;
        uint32_t count = 1u << (depth & 31);
        if (mapCount != 0) count = mapCount;
        if (static_cast<int32_t>(count) > static_cast<int32_t>(kPaletteEntries)) count = kPaletteEntries;
        info->bitsPerPixel = 8;
        info->paletteCount = count;
        // The first entry the map loads, and how many a table of `count` entries holds.
        uint8_t table[kPaletteMaxBytes];
        if (!hasColorMap) {
            info->alphaBits = 0;
            for (uint32_t i = 0; i < kPaletteEntries; ++i) {
                info->palette[i][0] = static_cast<uint8_t>(i);
                info->palette[i][1] = static_cast<uint8_t>(i);
                info->palette[i][2] = static_cast<uint8_t>(i);
                info->palette[i][3] = 0xff;
            }
        } else if (entryBits == 0xf || entryBits == 0x10 || entryBits == 0x18 || entryBits == 0x20) {
            const uint32_t entryBytes = entryBits == 0x18 ? 3 : entryBits == 0x20 ? 4 : 2;
            if (entryBits == 0x20) {
                info->alphaBits = 8;
            } else {
                info->alphaBits = 0;
                if (entryBytes == 2) info->redBits = info->greenBits = info->blueBits = 5;
            }
            status = static_cast<uint32_t>(StreamRead(stream, table, static_cast<int32_t>(count * entryBytes)));
            // ORIGINAL-BUG: the original loads colorMapLength entries from `colorMapFirst` on whatever the counts, past
            // the palette (and past the table it read); ours stops at the palette and at what the table holds.
            for (uint32_t i = 0; i < colorMapLength && i < count && colorMapFirst + i < kPaletteEntries; ++i) {
                uint8_t *entry = info->palette[colorMapFirst + i];
                const uint8_t *packed = table + i * entryBytes;
                if (entryBytes == 2) {
                    Expand16(entry, packed[0], packed[1], false);
                    // The 15 / 16-bit entry's alpha comes from the table's top bit when the info has alpha bits, else opaque.
                    entry[3] = alphaBits == 0 ? 0xff : static_cast<uint8_t>(static_cast<int8_t>(packed[1]) >> 7);
                } else {
                    entry[0] = packed[0];
                    entry[1] = packed[1];
                    entry[2] = packed[2];
                    entry[3] = entryBytes == 4 ? packed[3] : 0xff;
                }
            }
        }
    } else {
        info->paletteCount = 0;
        if (depth == 0x10) depth = 0xf;
        info->sourceBits = static_cast<int32_t>(depth);
        info->bitsPerPixel = 0x20;
        if (depth < 0x11) {
            info->redBits = info->greenBits = info->blueBits = 5;
            info->alphaBits = 0;
        } else if (depth < 0x20) {
            info->alphaBits = 0;
        }
    }
    if (status == 0) {
        FreeMemory(info);
        info = nullptr;
    }
    return info;
}

int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch)
{
    ifaces::cIGZFile *const stream = instance->stream;
    int32_t row = 0;
    StreamSeek(stream, 0, 0);
    uint8_t header[kHeaderBytes] = {};
    if (StreamRead(stream, header, kHeaderBytes) == 0) return 0;
    const int32_t width = info->width;
    const int32_t height = info->height;
    const int32_t bits = info->sourceBits;
    int32_t dataOffset = header[kOffsetIdLength] + static_cast<int32_t>(kHeaderBytes);
    if (header[kOffsetColorMapType] != 0) {
        dataOffset += static_cast<int32_t>((header[kOffsetColorMapEntryBits] + 7u) >> 3) *
                      static_cast<int32_t>(Load16(header, kOffsetColorMapLength));
    }
    uint32_t result = StreamSeek(stream, static_cast<uint32_t>(dataOffset), 0) ? 1 : 0;
    uint8_t *dst = static_cast<uint8_t *>(pixels);
    int32_t step = pitch;
    if ((header[kOffsetDescriptor] & kDescriptorTopDown) == 0) {
        dst += (height - 1) * pitch;
        step = -pitch;
    }
    uint8_t *const buffer = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(RowBytes(bits, width))));
    if (buffer != nullptr) {
        for (; row < height; ++row) {
            if (result == 0) break;
            result = DecodeRow(stream, dst, buffer, info);
            if ((header[kOffsetDescriptor] & kDescriptorFlipX) != 0) FlipRow(dst, width, info->bitsPerPixel);
            dst += step;
        }
        // The original answers 0 once a row has failed, whatever is left.
        FreeMemory(buffer);
    }
    return static_cast<int32_t>(result);
}

int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch)
{
    ifaces::cIGZFile *const stream = instance->stream;
    const int32_t height = info->height;
    const int32_t width = info->width;
    const int32_t alphaBits = static_cast<int32_t>(info->alphaBits);
    int32_t paletteCount = static_cast<int32_t>(info->paletteCount);
    int32_t depth = 0x20;
    int32_t entryBits = 0x20;
    int32_t attribute = alphaBits;
    if (info->sourceBits < 9) {
        depth = 8;
    } else if (alphaBits < 2 && info->redBits < 6 && info->greenBits < 6 && info->blueBits < 6) {
        depth = 0x10;
    } else if (alphaBits == 0) {
        depth = 0x18;
        attribute = 0;
    }
    bool gray = false;
    const int32_t rowBytes = static_cast<int32_t>(static_cast<int32_t>(static_cast<uint32_t>(depth) * static_cast<uint32_t>(width)) +
                                                  (static_cast<int32_t>(static_cast<uint32_t>(depth) * static_cast<uint32_t>(width)) >> 31 & 7)) >> 3;
    bool hasColorMap = paletteCount != 0;
    uint32_t imageType = depth > 8 ? 2 : 1;
    uint8_t map[256];
    uint8_t table[kPaletteMaxBytes];
    std::memset(map, 0, sizeof(map));
    if (depth < 9) {
        gray = true;
        bool plain = true;
        for (int32_t i = 0; i < paletteCount; ++i) {
            const uint8_t *entry = info->palette[i];
            if (entry[3] != 0xff || entry[2] != entry[1] || entry[2] != entry[0]) {
                gray = false;
                plain = false;
            }
            map[i] = entry[1];
        }
        if (!plain) {
            for (int32_t i = 0; i < paletteCount; ++i) map[i] = static_cast<uint8_t>(i);
        } else {
            imageType = 3;
            hasColorMap = false;
        }
    }
    if (info->jpegFlag != 0) imageType |= kTypeRunLength;
    if (hasColorMap) {
        const uint8_t *first = &info->palette[info->unknown20][0];
        if (info->redBits == 5 && info->greenBits == 5 && info->blueBits == 5 && alphaBits < 2) {
            entryBits = 0x10;
            for (int32_t i = 0; i < paletteCount; ++i) {
                const uint8_t *entry = first + i * 4;
                const uint32_t alpha = attribute == 0 ? 0xffu : static_cast<uint32_t>(entry[2] >> 7);
                uint16_t value = static_cast<uint16_t>((entry[2] >> 3) | (alpha << 5));
                value = static_cast<uint16_t>((value << 5) | (entry[1] >> 3));
                value = static_cast<uint16_t>((value << 5) | (entry[0] >> 3));
                table[i * 2] = static_cast<uint8_t>(value);
                table[i * 2 + 1] = static_cast<uint8_t>(value >> 8);
            }
        } else if (alphaBits == 8) {
            entryBits = 0x20;
            for (int32_t i = 0; i < paletteCount; ++i) {
                const uint8_t *entry = first + i * 4;
                table[i * 4] = entry[0];
                table[i * 4 + 1] = entry[1];
                table[i * 4 + 2] = entry[2];
                table[i * 4 + 3] = entry[3];
            }
        } else {
            entryBits = 0x18;
            for (int32_t i = 0; i < paletteCount; ++i) {
                const uint8_t *entry = first + i * 4;
                table[i * 3] = entry[0];
                table[i * 3 + 1] = entry[1];
                table[i * 3 + 2] = entry[2];
            }
        }
    }
    uint8_t header[kHeaderBytes];
    header[kOffsetIdLength] = 0;
    header[kOffsetColorMapType] = hasColorMap ? 1 : 0;
    header[kOffsetImageType] = static_cast<uint8_t>(imageType);
    Store16(header, kOffsetColorMapFirst, info->unknown20);
    Store16(header, kOffsetColorMapLength, static_cast<uint32_t>(paletteCount));
    header[kOffsetColorMapEntryBits] = static_cast<uint8_t>(entryBits);
    Store16(header, kOffsetOriginX, static_cast<uint32_t>(info->originX));
    Store16(header, kOffsetOriginY, static_cast<uint32_t>(-info->originY));
    Store16(header, kOffsetWidth, static_cast<uint32_t>(width));
    Store16(header, kOffsetHeight, static_cast<uint32_t>(height));
    header[kOffsetDepth] = static_cast<uint8_t>(depth);
    header[kOffsetDescriptor] = static_cast<uint8_t>(attribute);
    int32_t written = StreamWrite(stream, header, kHeaderBytes);
    const int32_t tableBits = entryBits * paletteCount;
    const int32_t tableBytes = (tableBits + (tableBits >> 31 & 7)) >> 3;
    if (written != 0 && !gray && tableBytes != 0) written = StreamWrite(stream, table, tableBytes);
    uint8_t *const buffer = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(rowBytes * 3 + 0x10)));
    if (buffer != nullptr) {
        if (height > 0) {
            const uint8_t *row = static_cast<const uint8_t *>(pixels) + (height - 1) * pitch;
            int32_t done = 0;
            do {
                const bool failed = written == 0;
                written = 0;
                if (failed) break;
                const int32_t length = EncodeRow(row, buffer, width, depth, map, info->jpegFlag != 0, attribute != 0);
                written = StreamWrite(stream, buffer, length);
                row -= pitch;
                ++done;
            } while (done < height);
        }
        FreeMemory(buffer);
    }
    return written > 0 ? 1 : 0;
}

} // namespace tga

} // namespace gimex
