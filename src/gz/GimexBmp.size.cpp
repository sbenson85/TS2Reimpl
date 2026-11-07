// Gimex's BMP codec (row 5 of the codec table, lane MN; src/gz/Gimex.h, src/gz/GimexFormats.h): reads and writes Windows
// and OS/2 bitmaps (1, 4, 8, 15 / 16, 24 and 32 bits, bit-fielded and run-length coded ones).
#include "gz/GimexFormats.h"

#include "game/original.h"
#include "ifaces/cIGZFile.h"

#include <cstring>

namespace gimex {

namespace bmp {

namespace {

constexpr uint32_t kFourcc = 0x2e424d50; // "PMB." in memory
constexpr uint32_t kInfoVersion = 0x159;
constexpr uint32_t kScoreMatch = 100;
constexpr uint32_t kInstanceBytes = 0x18;

// The stream and memory helpers are Gimex's shared ones (src/gz/Gimex.h).

// About's descriptive words (immediates in the original's code).
constexpr uint32_t kAboutTag = 0x42494e41;
constexpr uint32_t kAboutFlagsMask = 0xffff93d7;
constexpr uint32_t kAboutFlags = 0x93d7;

// The file header (14 bytes) and the info header that follows (Windows: BITMAPINFOHEADER 40 bytes, V4 108; OS/2: 12).
constexpr uint32_t kSignature = 0x4d42; // "BM"
constexpr uint32_t kFileHeaderBytes = 14;
constexpr uint32_t kCoreHeaderBytes = 12;
constexpr uint32_t kInfoHeaderBytes = 40;
constexpr uint32_t kV4HeaderBytes = 108;
constexpr uint32_t kHeaderBufferBytes = 0x7a; // the file header and a V4 header
constexpr uint32_t kHeaderFirstRead = 0x12;   // the file header and the info header's size word

// Offsets in the header buffer.
constexpr uint32_t kOffsetFileSize = 2;
constexpr uint32_t kOffsetDataOffset = 10;
constexpr uint32_t kOffsetHeaderSize = 14;
constexpr uint32_t kOffsetWidth = 0x12;
constexpr uint32_t kOffsetHeight = 0x16;
constexpr uint32_t kOffsetPlanes = 0x1a;
constexpr uint32_t kOffsetBitCount = 0x1c;
constexpr uint32_t kOffsetCompression = 0x1e;
constexpr uint32_t kOffsetImageSize = 0x22;
constexpr uint32_t kOffsetXPixelsPerMetre = 0x26;
constexpr uint32_t kOffsetYPixelsPerMetre = 0x2a;
constexpr uint32_t kOffsetColorsUsed = 0x2e;
constexpr uint32_t kOffsetColorsImportant = 0x32;
constexpr uint32_t kOffsetRedMask = 0x36;
constexpr uint32_t kOffsetGreenMask = 0x3a;
constexpr uint32_t kOffsetBlueMask = 0x3e;
constexpr uint32_t kOffsetAlphaMask = 0x42;
constexpr uint32_t kOffsetColorSpaceType = 0x46;

// Info.variant: which header the file has.
constexpr uint32_t kVariantInfo = 0;      // 40 bytes without bit fields (or any size but 12 and 108)
constexpr uint32_t kVariantCore = 1;      // 12 bytes
constexpr uint32_t kVariantV4 = 2;        // 108 bytes
constexpr uint32_t kVariantBitfields = 3; // 40 bytes and three colour masks (compression 3)
constexpr uint32_t kCompressionBitfields = 3;
constexpr uint32_t kCompressionRle8 = 1;
constexpr uint32_t kCompressionRle4 = 2;

constexpr float kPixelsPerMetreToInch = 39.37008f;

uint32_t Load16(const uint8_t *header, uint32_t offset)
{
    uint16_t value;
    std::memcpy(&value, header + offset, sizeof(value));
    return value;
}

uint32_t Load32(const uint8_t *header, uint32_t offset)
{
    uint32_t value;
    std::memcpy(&value, header + offset, sizeof(value));
    return value;
}

void Store16(uint8_t *header, uint32_t offset, uint32_t value)
{
    const uint16_t narrow = static_cast<uint16_t>(value);
    std::memcpy(header + offset, &narrow, sizeof(narrow));
}

void Store32(uint8_t *header, uint32_t offset, uint32_t value)
{
    std::memcpy(header + offset, &value, sizeof(value));
}

// 0x880c8e: the run of set bits that starts at the mask's lowest set bit (the width of a colour channel); 0 for 0.
int32_t ChannelBits(uint32_t mask)
{
    int32_t bits = 0;
    if (mask != 0) {
        while ((mask & 1) == 0) mask >>= 1;
        do {
            mask >>= 1;
            ++bits;
        } while ((mask & 1) != 0);
    }
    return bits;
}

// 0x880caa: the number of zero bits below the mask's lowest set bit (a channel's shift); 0 for 0.
int32_t ChannelShift(uint32_t mask)
{
    int32_t shift = 0;
    if (mask != 0) {
        while ((mask & 1) == 0) {
            ++shift;
            mask >>= 1;
        }
    }
    return shift;
}

// The bytes of one pixel row in the file: a bit depth of 15 is stored like 16.
int32_t StoredBits(int32_t bits)
{
    return bits == 0xf ? 0x10 : bits;
}

// 0x880cbf (__cdecl): one row of a file's pixels into `dst` (8 bits per index for 1, 4 and 8, B, G, R, A for the rest),
// through `buffer`, which holds a row's bytes. The last read's count. Run-length coded rows (info->jpegFlag) end at the
// row's last pixel, or an end-of-line / end-of-file / delta code once the row has a pixel.
uint32_t DecodeRow(ifaces::cIGZFile *stream, uint8_t *dst, uint8_t *buffer, int32_t bits, const Info *info,
                   uint32_t alphaMask, uint32_t redMask, uint32_t greenMask, uint32_t blueMask)
{
    const int32_t width = info->width;
    uint32_t result = 0;
    int32_t consumed = 0;
    if (info->jpegFlag == 0) {
        consumed = (StoredBits(bits) * width + 0x1f) >> 3 & ~3;
        result = StreamRead(stream, buffer, consumed);
        const uint8_t *src = buffer;
        if (bits == 1) {
            for (int32_t i = 0; i < width; ++i) {
                *dst++ = (*src >> (7 - (i & 7))) & 1;
                if ((i & 7) == 7) ++src;
            }
        } else if (bits == 4) {
            for (int32_t i = 0; i < width; ++i) {
                *dst++ = (i & 1) == 0 ? *src >> 4 : *src & 0xf;
                if ((i & 1) != 0) ++src;
            }
        } else if (bits == 8) {
            for (int32_t i = 0; i < width; ++i) *dst++ = *src++;
        } else if (bits == 0x18) {
            for (int32_t i = 0; i < width; ++i) {
                dst[3] = 0xff;
                dst[2] = src[2];
                dst[1] = src[1];
                dst[0] = src[0];
                src += 3;
                dst += 4;
            }
        } else if (bits == 0xf || bits == 0x10 || bits == 0x20) {
            const int32_t alphaMax = 1 << (info->alphaBits & 31);
            const int32_t redMax = 1 << (info->redBits & 31);
            const int32_t greenMax = 1 << (info->greenBits & 31);
            const int32_t blueMax = 1 << (info->blueBits & 31);
            const int32_t pixelBytes = bits > 0x10 ? 4 : 2;
            const int32_t alphaShift = ChannelShift(alphaMask);
            const int32_t redShift = ChannelShift(redMask);
            const int32_t greenShift = ChannelShift(greenMask);
            const int32_t blueShift = ChannelShift(blueMask);
            for (int32_t i = 0; i < width; ++i) {
                // The original reads a whole word even for a 16-bit pixel; the masks of a 16-bit file stay in the low half.
                uint32_t pixel = 0;
                std::memcpy(&pixel, src, pixelBytes);
                uint8_t alpha = 0xff;
                if (alphaMask != 0) {
                    const uint32_t value = (alphaMask & pixel) >> (alphaShift & 31);
                    alpha = alphaMax == 1 ? static_cast<uint8_t>(0u - value)
                                          : static_cast<uint8_t>((value * 0xffu + (alphaMax >> 1)) / (alphaMax - 1u));
                }
                dst[3] = alpha;
                dst[2] = static_cast<uint8_t>((((pixel & redMask) >> (redShift & 31)) * 0xffu + (redMax >> 1)) / (redMax - 1u));
                dst[1] = static_cast<uint8_t>((((pixel & greenMask) >> (greenShift & 31)) * 0xffu + (greenMax >> 1)) /
                                              (greenMax - 1u));
                dst[0] = static_cast<uint8_t>((((pixel & blueMask) >> (blueShift & 31)) * 0xffu + (blueMax >> 1)) /
                                              (blueMax - 1u));
                src += pixelBytes;
                dst += 4;
            }
        }
    } else {
        int32_t placed = 0;
        do {
            StreamRead(stream, buffer, 1);
            const uint32_t count = buffer[0];
            int32_t next = consumed + 2;
            if (count == 0) {
                result = StreamRead(stream, buffer, 1);
                const uint32_t length = buffer[0];
                if (length < 3) {
                    consumed = next;
                    if (placed != 0) break;
                    continue;
                }
                // An absolute run of `length` pixels, padded to a whole number of words.
                placed += length;
                const int32_t bytes = static_cast<int32_t>(length * bits + 0xf) >> 3 & ~1;
                result = StreamRead(stream, buffer, bytes);
                next += bytes;
                const uint8_t *src = buffer;
                if (bits == 1) {
                    for (uint32_t i = 0; i < length; ++i) {
                        *dst++ = (*src >> (7 - (i & 7))) & 1;
                        if ((i & 7) == 7) ++src;
                    }
                } else if (bits == 4) {
                    for (uint32_t i = 0; i < length; ++i) {
                        *dst++ = (i & 1) == 0 ? *src >> 4 : *src & 0xf;
                        if ((i & 1) != 0) ++src;
                    }
                } else if (bits == 8) {
                    for (uint32_t i = 0; i < length; ++i) *dst++ = *src++;
                } else if (bits == 0x18) {
                    for (uint32_t i = 0; i < length; ++i) {
                        dst[3] = 0xff;
                        dst[2] = src[2];
                        dst[1] = src[1];
                        dst[0] = src[0];
                        src += 3;
                        dst += 4;
                    }
                }
            } else {
                // An encoded run: `count` pixels of one value (a 24-bit one has three bytes).
                placed += count;
                result = StreamRead(stream, buffer, 1);
                const uint8_t value = buffer[0];
                if (bits == 1) {
                    for (uint32_t i = 0; i < count; ++i) *dst++ = (value >> (7 - (i & 7))) & 1;
                } else if (bits == 4) {
                    for (uint32_t i = 0; i < count; ++i) *dst++ = (i & 1) == 0 ? value >> 4 : value & 0xf;
                } else if (bits == 8) {
                    for (uint32_t i = 0; i < count; ++i) *dst++ = value;
                } else if (bits == 0x18) {
                    result = StreamRead(stream, buffer + 1, 2);
                    const uint32_t color = ReadLittleEndian(buffer, 3) | 0xff000000u;
                    next = consumed + 4;
                    for (uint32_t i = 0; i < count; ++i) {
                        std::memcpy(dst, &color, sizeof(color));
                        dst += 4;
                    }
                }
            }
            consumed = next;
        } while (placed < width);
    }
    if ((consumed & 3) != 0) StreamRead(stream, buffer, -consumed & 3);
    return result;
}

// 0x8819dd: the length of the run of bytes equal to the first, at most 255 and at most `limit`.
int32_t RunLength(const uint8_t *bytes, int32_t limit)
{
    const int32_t cap = limit < 0x100 ? limit : 0xff;
    int32_t length = 0;
    while (length < cap && bytes[0] == bytes[length]) ++length;
    return length;
}

// (x * (2^n - 1) + 128) / 255 shifted into place: a channel of `bits` bits.
int32_t Scaled(uint8_t value, uint8_t bits)
{
    return static_cast<int32_t>(((static_cast<uint32_t>(value) << (bits & 31)) - value) + 0x80) / 0xff;
}

// 0x881a04 (__cdecl): one row of B, G, R, A pixels (or indices) as the file stores it into `dst`; the bytes written
// (padded to a word, or the run-length coding's end-of-line code).
int32_t EncodeRow(const uint8_t *src, uint8_t *dst, int32_t width, int32_t bits, bool runLength, uint8_t alphaBits,
                  uint8_t redBits, uint8_t greenBits, uint8_t blueBits)
{
    const int32_t stored = StoredBits(bits);
    uint8_t *out = dst;
    if (!runLength) {
        const uint8_t redShift = static_cast<uint8_t>(0x20 - alphaBits - redBits);
        const uint8_t greenShift = static_cast<uint8_t>(redShift - greenBits);
        for (int32_t i = 0; i < width; ++i) {
            if (stored == 0x10 || stored == 0x20) {
                const uint32_t pixel = static_cast<uint32_t>(Scaled(src[0], blueBits)) << ((greenShift - blueBits) & 31) |
                                       static_cast<uint32_t>(Scaled(src[3], alphaBits)) << ((0x20 - alphaBits) & 31) |
                                       static_cast<uint32_t>(Scaled(src[2], redBits)) << (redShift & 31) |
                                       static_cast<uint32_t>(Scaled(src[1], greenBits)) << (greenShift & 31);
                if (stored == 0x20) {
                    out[0] = static_cast<uint8_t>(pixel);
                    out[1] = static_cast<uint8_t>(pixel >> 8);
                    out[2] = static_cast<uint8_t>(pixel >> 16);
                    out[3] = static_cast<uint8_t>(pixel >> 24);
                    out += 4;
                } else {
                    out[0] = static_cast<uint8_t>(pixel >> 16);
                    out[1] = static_cast<uint8_t>(pixel >> 24);
                    out += 2;
                }
                src += 4;
            } else if (stored == 0x18) {
                out[0] = src[0];
                out[1] = src[1];
                out[2] = src[2];
                out += 3;
                src += 4;
            } else if (stored == 8) {
                *out++ = *src++;
            } else if (stored == 4) {
                if ((i & 1) == 0) {
                    *out = static_cast<uint8_t>(*src << 4);
                    if (i == width - 1) ++out;
                } else {
                    *out = static_cast<uint8_t>(*out | (*src & 0xf));
                    ++out;
                }
                ++src;
            } else if (stored == 1) {
                const int32_t bit = i & 7;
                if (bit == 0) *out = 0;
                *out = static_cast<uint8_t>(*out | (*src & 1) << (7 - bit));
                if (bit == 7 || i == width - 1) ++out;
                ++src;
            }
        }
        while (((out - dst) & 3) != 0) *out++ = 0;
        return static_cast<int32_t>(out - dst);
    }
    // 8-bit run-length coding: encoded runs (count, value) and absolute runs (0, count, bytes, padded to a word).
    int32_t remaining = width;
    while (remaining > 0) {
        int32_t run;
        bool encoded = false;
        do {
            run = RunLength(src, remaining);
            if (remaining < 3) {
                run = 1;
                if (remaining == 2 && src[0] == src[1]) run = 2;
                encoded = true;
                break;
            }
            if (RunLength(src + run, remaining - run) + run > 3) {
                encoded = true;
                break;
            }
            run = 4;
            if (4 < remaining) {
                int32_t room = remaining - 4;
                do {
                    if (run > 0xfd) break;
                    const int32_t first = RunLength(src + run, room);
                    if (first > 2 && RunLength(src + first, remaining - first - run) > 1) break;
                    if (first > 4) break;
                    run += 2;
                    room -= 2;
                } while (run < remaining);
            }
            if (remaining - 2 <= run) run = remaining;
            if (run > 0xfe) {
                run = 0xfe;
                break;
            }
        } while (run < 3);
        if (encoded) {
            out[0] = static_cast<uint8_t>(run);
            out[1] = src[0];
            src += run;
            remaining -= run;
            out += 2;
        } else {
            out[0] = 0;
            out[1] = static_cast<uint8_t>(run);
            remaining -= run;
            out += 2;
            for (int32_t i = 0; i < run; ++i) *out++ = *src++;
            if (((out - dst) & 1) != 0) *out++ = 0;
        }
    }
    out[0] = 0;
    out[1] = 0;
    out += 2;
    return static_cast<int32_t>(out - dst);
}

} // namespace

About *__stdcall NewAbout()
{
    About *about = static_cast<About *>(Allocate(sizeof(About)));
    if (about != nullptr) {
        std::memset(about, 0, sizeof(About));
        about->size = sizeof(About);
        about->unknown1c = kAboutTag;
        about->version = kInfoVersion;
        about->flags = (about->flags & kAboutFlagsMask) | kAboutFlags;
        about->unknown20 = kFourcc;
        about->fourcc = kFourcc;
        about->defaultQuality = 100;
        std::strcpy(about->extensions[0], ".bmp");
        std::strcpy(about->author, "FrANK G. Barchard");
        std::strcpy(about->revision, "1.27");
        std::strcpy(about->shortName, "BMP");
        std::strcpy(about->typeName, "BMP");
        std::strcpy(about->description, "Windows BMP");
    }
    return about;
}

int32_t __stdcall Probe(ifaces::cIGZFile *stream)
{
    uint32_t score = 0;
    StreamSeek(stream, 0, 0);
    // ORIGINAL-BUG: a stream of one byte leaves the second signature byte as the stack held it; ours reads zero.
    uint8_t signature[2] = {0, 0};
    if (StreamRead(stream, signature, 2) != 0 && signature[0] == 'B' && signature[1] == 'M') score = kScoreMatch;
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
    int32_t colorsUsed = 0;
    uint32_t masks[4] = {0, 0, 0, 0}; // red, green, blue, alpha
    StreamSeek(stream, 0, 0);
    uint8_t header[kHeaderBufferBytes];
    std::memset(header, 0, sizeof(header));
    if (StreamRead(stream, header, kHeaderFirstRead) == 0) return nullptr;
    const int32_t headerSize = static_cast<int32_t>(Load32(header, kOffsetHeaderSize));
    // ORIGINAL-BUG: the original reads the rest of the info header into the stack buffer whatever its size says; ours
    // stops at the buffer's end.
    int32_t restBytes = headerSize - 4;
    if (restBytes > static_cast<int32_t>(kHeaderBufferBytes - kHeaderFirstRead)) {
        restBytes = static_cast<int32_t>(kHeaderBufferBytes - kHeaderFirstRead);
    }
    int32_t lastRead = StreamRead(stream, header + kHeaderFirstRead, restBytes);
    uint32_t width;
    uint32_t height;
    uint32_t bits;
    uint32_t compression;
    uint32_t variant;
    if (headerSize == static_cast<int32_t>(kCoreHeaderBytes)) {
        width = Load16(header, kOffsetWidth);
        height = Load16(header, kOffsetWidth + 2);
        bits = Load16(header, kOffsetWidth + 6);
        colorsUsed = 1 << (bits & 31);
        compression = 0;
        variant = kVariantCore;
    } else {
        width = Load32(header, kOffsetWidth);
        height = Load32(header, kOffsetHeight);
        bits = Load16(header, kOffsetBitCount);
        compression = Load32(header, kOffsetCompression);
        colorsUsed = static_cast<int32_t>(Load32(header, kOffsetColorsUsed));
        if (compression == kCompressionBitfields && headerSize == static_cast<int32_t>(kInfoHeaderBytes)) {
            lastRead = StreamRead(stream, header + kOffsetRedMask, 12);
            variant = kVariantBitfields;
        } else {
            variant = headerSize == static_cast<int32_t>(kV4HeaderBytes) ? kVariantV4 : kVariantInfo;
        }
        masks[0] = Load32(header, kOffsetRedMask);
        masks[1] = Load32(header, kOffsetGreenMask);
        masks[2] = Load32(header, kOffsetBlueMask);
        masks[3] = Load32(header, kOffsetAlphaMask);
    }
    if (lastRead == 0) return nullptr;
    const uint32_t pixelsPerMetre = headerSize == static_cast<int32_t>(kCoreHeaderBytes) ? 0 : Load32(header, kOffsetXPixelsPerMetre);
    Info *info = static_cast<Info *>(Allocate(kWriteInfoSize));
    if (info == nullptr) return nullptr;
    std::memset(info, 0, kWriteInfoSize);
    info->width = static_cast<int32_t>(width);
    info->signature = kFourcc;
    info->size = kWriteInfoSize;
    const int32_t signedHeight = static_cast<int32_t>(height);
    info->height = signedHeight < 0 ? -signedHeight : signedHeight;
    info->version = kInfoVersion;
    info->variant = variant;
    info->jpegFlag = compression == kCompressionRle8 || compression == kCompressionRle4 ? 1 : 0;
    info->quality = 100;
    info->redBits = 8;
    info->greenBits = 8;
    info->blueBits = 8;
    info->dotsPerInch = static_cast<float>(static_cast<int32_t>(pixelsPerMetre)) / kPixelsPerMetreToInch;
    if (bits > 8) {
        info->paletteCount = 0;
        info->sourceBits = static_cast<int32_t>(bits);
        info->bitsPerPixel = 32;
        if (variant >= kVariantV4) {
            if (bits == 0x20 && variant == kVariantV4) info->alphaBits = ChannelBits(masks[3]);
            info->redBits = ChannelBits(masks[0]);
            info->greenBits = ChannelBits(masks[1]);
            info->blueBits = ChannelBits(masks[2]);
        } else if (bits == 0x10) {
            info->sourceBits = 0xf;
            info->blueBits = 5;
            info->greenBits = 5;
            info->redBits = 5;
        }
        return info;
    }
    if (bits == 0) bits = 8;
    int32_t colors = static_cast<int32_t>(1u << (bits & 31));
    if (colorsUsed != 0) colors = colorsUsed;
    int32_t entries = 0x100;
    bool hasPalette = true;
    if (colors <= 0x100) {
        entries = colors;
        hasPalette = colors != 0;
    }
    if (hasPalette) {
        uint8_t table[1024];
        if (variant == kVariantCore) {
            lastRead = StreamRead(stream, table, entries * 3);
            for (int32_t i = 0; i < entries; ++i) {
                info->palette[i][0] = table[i * 3];
                info->palette[i][1] = table[i * 3 + 1];
                info->palette[i][2] = table[i * 3 + 2];
                info->palette[i][3] = 0xff;
            }
        } else {
            lastRead = StreamRead(stream, table, entries * 4);
            bool allClear = true;
            bool allOpaque = true;
            for (int32_t i = 0; i < entries; ++i) {
                info->palette[i][0] = table[i * 4];
                info->palette[i][1] = table[i * 4 + 1];
                info->palette[i][2] = table[i * 4 + 2];
                info->palette[i][3] = table[i * 4 + 3];
                allClear = allClear && table[i * 4 + 3] == 0;
                allOpaque = allOpaque && table[i * 4 + 3] == 0xff;
            }
            if (entries > 0 && !allClear) {
                info->alphaBits = allOpaque ? 0 : 8;
            } else {
                // No palette entry has an alpha: the format's unused byte becomes opaque.
                for (int32_t i = 0; i < entries; ++i) info->palette[i][3] = 0xff;
                info->alphaBits = 0;
            }
        }
    }
    info->bitsPerPixel = 8;
    info->sourceBits = static_cast<int32_t>(bits);
    info->paletteCount = static_cast<uint32_t>(entries);
    if (lastRead == 0) {
        FreeMemory(info);
        info = nullptr;
    }
    return info;
}

int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch)
{
    ifaces::cIGZFile *const stream = instance->stream;
    const int32_t paletteCount = static_cast<int32_t>(info->paletteCount);
    int32_t rows = info->height;
    const int32_t variant = static_cast<int32_t>(info->variant);
    const int32_t width = info->width;
    uint32_t alphaMask = 0;
    uint32_t masks[3] = {0, 0, 0}; // red, green, blue
    StreamSeek(stream, 0, 0);
    uint8_t header[0x7c];
    std::memset(header, 0, sizeof(header));
    if (StreamRead(stream, header, variant < 2 ? 0x1a : 0x46) == 0) return 0;
    const int32_t bits = info->sourceBits;
    if (variant < 2) {
        if (bits == 0xf) {
            masks[0] = 0x7c00;
            masks[1] = 0x3e0;
            masks[2] = 0x1f;
        } else if (bits == 0x20) {
            masks[0] = 0xff0000;
            masks[1] = 0xff00;
            masks[2] = 0xff;
        }
    } else {
        masks[0] = Load32(header, kOffsetRedMask);
        masks[1] = Load32(header, kOffsetGreenMask);
        masks[2] = Load32(header, kOffsetBlueMask);
        if (bits == 0x20 && variant == static_cast<int32_t>(kVariantV4)) alphaMask = Load32(header, kOffsetAlphaMask);
    }
    int32_t dataOffset = static_cast<int32_t>(Load32(header, kOffsetDataOffset));
    if (dataOffset == 0) {
        if (variant == static_cast<int32_t>(kVariantCore)) {
            dataOffset = 0x1a;
            if (bits <= 8) dataOffset = paletteCount * 3 + 0x1a;
        } else if (variant == static_cast<int32_t>(kVariantV4)) {
            dataOffset = 0x7a;
        } else if (variant == static_cast<int32_t>(kVariantBitfields)) {
            dataOffset = 0x42;
        } else {
            dataOffset = 0x36;
            if (bits <= 8) dataOffset = paletteCount * 4 + 0x36;
        }
    }
    uint32_t result = StreamSeek(stream, static_cast<uint32_t>(dataOffset), dataOffset >> 31) ? 1 : 0;
    const int32_t rowBytes = (StoredBits(bits) * width + 0x1f) >> 3 & ~3;
    uint8_t *const buffer = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(rowBytes)));
    if (buffer != nullptr) {
        uint8_t *dst = static_cast<uint8_t *>(pixels);
        const bool topDown = static_cast<int32_t>(Load32(header, kOffsetHeight)) < 0;
        if (!topDown) dst += (rows - 1) * pitch;
        const int32_t step = topDown ? pitch : -pitch;
        for (; rows > 0; --rows) {
            if (result != 0) {
                result = DecodeRow(stream, dst, buffer, bits, info, alphaMask, masks[0], masks[1], masks[2]);
            }
            dst += step;
        }
        FreeMemory(buffer);
    }
    return static_cast<int32_t>(result);
}

int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch)
{
    ifaces::cIGZFile *const stream = instance->stream;
    const int32_t width = info->width;
    const int32_t height = info->height;
    int32_t alphaBits = static_cast<int32_t>(info->alphaBits);
    int32_t redBits = static_cast<int32_t>(info->redBits);
    int32_t greenBits = static_cast<int32_t>(info->greenBits);
    int32_t blueBits = static_cast<int32_t>(info->blueBits);
    const int32_t variant = static_cast<int32_t>(info->variant);
    int32_t channelTotal;
    if (info->bitsPerPixel < 9) {
        channelTotal = info->sourceBits < 8 ? info->sourceBits : 8;
    } else {
        channelTotal = alphaBits + redBits + greenBits + blueBits;
    }
    int32_t bits = 1;
    if (channelTotal > 1) {
        if (channelTotal < 5) {
            bits = 4;
        } else if (channelTotal < 9) {
            bits = 8;
        } else if (channelTotal < 0x11) {
            bits = 0x10;
        } else {
            bits = channelTotal < 0x19 ? 0x18 : 0x20;
        }
    }
    int32_t headerSize = kInfoHeaderBytes;
    int32_t paletteCount = bits < 9 ? static_cast<int32_t>(info->paletteCount) : 0;
    int32_t extraBytes = 0;
    bool runLength = false;
    uint32_t compression = 0;
    uint8_t header[kHeaderBufferBytes];
    std::memset(header, 0, sizeof(header));
    uint32_t redMask = 0;
    uint32_t greenMask = 0;
    uint32_t blueMask = 0;
    uint32_t alphaMask = 0;
    if (variant == static_cast<int32_t>(kVariantV4)) {
        headerSize = kV4HeaderBytes;
        if (alphaBits == 0) {
            if (bits == 0x10) {
                int32_t green = 5;
                if (redBits < 6 && blueBits < 6 && greenBits < 7) {
                    compression = kCompressionBitfields;
                    redBits = 5;
                    blueMask = 0x1f;
                    alphaMask = 0;
                    if (greenBits > 5) {
                        green = 6;
                        redMask = 0xf800;
                        greenMask = 0x7e0;
                    } else {
                        redMask = 0x7c00;
                        greenMask = 0x3e0;
                    }
                    alphaBits = greenBits <= 5 ? 1 : 0;
                    blueBits = 5;
                    greenBits = green;
                }
            } else if (bits == 0x18) {
                blueBits = 8;
                redBits = 8;
                alphaBits = 0;
                redMask = 0xff0000;
                greenMask = 0xff00;
                blueMask = 0xff;
                alphaMask = 0;
                greenBits = 8;
            }
        } else if (bits > 8) {
            bits = 0x20;
            alphaBits = 8;
            blueBits = 8;
            redBits = 8;
            redMask = 0xff0000;
            greenMask = 0xff00;
            blueMask = 0xff;
            alphaMask = 0xff000000;
            greenBits = 8;
        }
        Store32(header, kOffsetColorSpaceType, 1);
    } else if (variant == static_cast<int32_t>(kVariantBitfields)) {
        compression = kCompressionBitfields;
        extraBytes = 12;
        if (bits == 0x10 && channelTotal > 0x10) bits = 0x20;
        const int32_t alpha = alphaBits < 8 ? alphaBits : 8;
        const int32_t red = redBits < 8 ? redBits : 8;
        const int32_t green = greenBits < 8 ? greenBits : 8;
        const int32_t blue = blueBits < 8 ? blueBits : 8;
        const uint8_t greenAndBlue = static_cast<uint8_t>(green + blue);
        alphaMask = static_cast<uint32_t>((1 << (alpha & 31)) - 1) << ((greenAndBlue + red) & 31);
        redMask = static_cast<uint32_t>((1 << (red & 31)) - 1) << (greenAndBlue & 31);
        greenMask = static_cast<uint32_t>((1 << (green & 31)) - 1) << (blue & 31);
        blueMask = static_cast<uint32_t>((1 << (blue & 31)) - 1);
        greenBits = green;
        blueBits = blue;
        redBits = red;
        alphaBits = alpha;
    } else if (bits == 0x10) {
        alphaBits = variant != 4 ? 1 : 0;
        blueBits = 5;
        greenBits = (variant == 4 ? 1 : 0) + 5;
        redBits = 5;
    } else if (bits == 0x20) {
        greenBits = 8;
        alphaBits = 8;
        blueBits = 8;
        redBits = 8;
    } else if (bits == 8 && info->jpegFlag != 0) {
        runLength = true;
        compression = kCompressionRle8;
    }
    Store32(header, kOffsetRedMask, redMask);
    Store32(header, kOffsetGreenMask, greenMask);
    Store32(header, kOffsetBlueMask, blueMask);
    Store32(header, kOffsetAlphaMask, alphaMask);
    int32_t pixelBytes = 0;
    const uint32_t fullRow = static_cast<uint32_t>(bits * width + 0x1f);
    int32_t rowLength = static_cast<int32_t>((fullRow & 0xffffffe0u) + (static_cast<int32_t>(fullRow) >> 31 & 7u)) >> 3;
    const int32_t bufferBytes = rowLength * 3 + 0x1c;
    uint8_t *const buffer = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(bufferBytes)));
    const uint8_t *const base = static_cast<const uint8_t *>(pixels);
    if (buffer != nullptr && height > 0) {
        const uint8_t *row = base + (height - 1) * pitch;
        for (int32_t i = height; i > 0; --i) {
            rowLength = EncodeRow(row, buffer, width, bits, runLength, static_cast<uint8_t>(alphaBits),
                                  static_cast<uint8_t>(redBits), static_cast<uint8_t>(greenBits),
                                  static_cast<uint8_t>(blueBits));
            pixelBytes += rowLength;
            row -= pitch;
        }
    }
    const int32_t paletteBytes = paletteCount * 4;
    const int32_t dataStart = extraBytes + paletteBytes + 0xe + headerSize;
    Store16(header, 0, kSignature);
    Store32(header, kOffsetFileSize, static_cast<uint32_t>(pixelBytes + dataStart));
    Store32(header, 6, 0);
    Store32(header, kOffsetDataOffset, static_cast<uint32_t>(dataStart));
    Store32(header, kOffsetHeaderSize, static_cast<uint32_t>(headerSize));
    Store32(header, kOffsetWidth, static_cast<uint32_t>(width));
    Store32(header, kOffsetHeight, static_cast<uint32_t>(height));
    Store16(header, kOffsetPlanes, 1);
    Store16(header, kOffsetBitCount, static_cast<uint32_t>(bits));
    Store32(header, kOffsetCompression, compression);
    Store32(header, kOffsetImageSize, static_cast<uint32_t>(pixelBytes));
    const uint32_t pixelsPerMetre = static_cast<uint32_t>(static_cast<int32_t>(info->dotsPerInch * kPixelsPerMetreToInch + 0.5f));
    Store32(header, kOffsetXPixelsPerMetre, pixelsPerMetre);
    Store32(header, kOffsetYPixelsPerMetre, pixelsPerMetre);
    Store32(header, kOffsetColorsUsed, static_cast<uint32_t>(paletteCount));
    Store32(header, kOffsetColorsImportant, 0);
    int32_t written = StreamWrite(stream, header, extraBytes + headerSize + static_cast<int32_t>(kFileHeaderBytes));
    if (bits < 9 && paletteCount != 0) {
        uint8_t table[1024];
        for (int32_t i = 0; i < paletteCount; ++i) {
            table[i * 4] = info->palette[i][0];
            table[i * 4 + 1] = info->palette[i][1];
            table[i * 4 + 2] = info->palette[i][2];
            table[i * 4 + 3] = info->alphaBits == 0 ? 0xff : info->palette[i][3];
        }
        written = StreamWrite(stream, table, paletteBytes);
    }
    if (buffer != nullptr) {
        // ORIGINAL-BUG: the original clears three times the last encoded row's length plus 16, which for a run-length
        // coded row longer than the plain row runs past the buffer; ours stops at its end (the rows rewrite what they use).
        const int32_t clearBytes = rowLength * 3 + 0x10;
        std::memset(buffer, 0, static_cast<size_t>(clearBytes < bufferBytes ? clearBytes : bufferBytes));
        if (height > 0) {
            const uint8_t *row = base + (height - 1) * pitch;
            int32_t done = 0;
            do {
                const bool failed = written == 0;
                written = 0;
                if (failed) break;
                const int32_t length = EncodeRow(row, buffer, width, bits, runLength, static_cast<uint8_t>(alphaBits),
                                                 static_cast<uint8_t>(redBits), static_cast<uint8_t>(greenBits),
                                                 static_cast<uint8_t>(blueBits));
                written = StreamWrite(stream, buffer, length);
                row -= pitch;
                ++done;
            } while (done < height);
        }
        FreeMemory(buffer);
    }
    return written;
}

} // namespace bmp

} // namespace gimex
