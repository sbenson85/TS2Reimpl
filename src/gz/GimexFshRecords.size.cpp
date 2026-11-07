// The FSH back end's record helpers (lane MX, block 1's decode side; src/gz/GimexFshDecode.h): the record classes and sizes, the
// attribute word's fields, the attachments read into an info block, the six-face mipmap layout and the directory's free. Size-built
// in the original; 0x885d30 and 0x886680 (speed-built) are in GimexFshRecords.cpp.
#include "gz/GimexFshDecode.h"

#include "gz/GimexFshBlocks.h"
#include "gz/GimexFshGm.h"
#include "gz/GimexFshLeaves.h"

#include <cstring>
#include <emmintrin.h>

namespace gimex {
namespace fsh {

namespace {

constexpr uint32_t kHeaderBytes = 0x20;
constexpr uint32_t kAttributeOffset = 1;
constexpr uint32_t kAttributeBytes = 3;
constexpr uint32_t kWidthOffset = 0x18;
constexpr uint32_t kHeightOffset = 0x1c;
constexpr uint32_t kCodedBit = 2;       // the attribute word's bit 1: the pixels are coded
constexpr uint32_t kSwizzledBit = 14;   // the attribute word's bit 14
constexpr int32_t kBlocksCodec = 3;
constexpr int32_t kRefPackCodec = 2;
constexpr int32_t kGmCodec = 4;
constexpr uint32_t kMipShift = 20;      // the attribute word's mipmap count (bits 20..23: levels - 1, 0 and 0xf mean one)
constexpr uint32_t kMipKeep = 0xff0fffff;
constexpr uint32_t kMipBias = 0xfff;
constexpr int32_t kTypeFour = 0x1e;
constexpr int32_t kTypeBlocks1 = 0x60;
constexpr int32_t kTypeBlocksLast = 0x62;
constexpr int32_t kTypeBlocks61 = 0x61;
constexpr int32_t kTypeBlocks62 = 0x62;
constexpr int32_t kFaces = 6;

// Info block offsets the attachments are read into.
constexpr uint32_t kInfoVariant = 0x428;
constexpr uint32_t kInfoPairsA = 0x448;
constexpr uint32_t kInfoPairsB = 0x44c;
constexpr uint32_t kInfoPairCount = 0x458;
constexpr uint32_t kInfoText = 0x65c;
constexpr uint32_t kInfoPairs = 0xa5c;
constexpr uint32_t kInfoExtra = 0x2a78;
constexpr uint32_t kMarkerFlag = 0x200000;
constexpr int32_t kMaxPairs = 0x400;

template <class T> T &At(uint8_t *base, uint32_t offset)
{
    return *reinterpret_cast<T *>(base + offset);
}

const Entry &EntryOf(const Directory *directory, int32_t frame)
{
    return directory->entries[frame];
}

// The size of one mipmap level of a block-coded shape, rounded to its block.
int32_t BlockLevelBytes(int32_t width, int32_t height, int32_t bits, int32_t platform)
{
    int32_t pixelsWide;
    int32_t pixelsHigh;
    if (platform == 5) {
        pixelsHigh = (height + 7) & ~7;
        pixelsWide = (width + 7) & ~7;
    } else {
        pixelsHigh = (height + 3) & ~3;
        pixelsWide = (width + 3) & ~3;
    }
    if (pixelsWide < 4) pixelsWide = 4;
    if (pixelsHigh < 4) pixelsHigh = 4;
    const int32_t product = pixelsHigh * pixelsWide * bits;
    return (((product + ((product >> 31) & 7)) >> 3) + 0xf) & ~0xf;
}

} // namespace

int32_t __cdecl RowBytes(int32_t width, int32_t bits, int32_t platform)
{
    const int32_t depth = bits == 0xf ? 0x10 : bits;
    uint32_t aligned;
    switch (platform) {
    case 2:
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 0xc:
    case 0xf:
    case 0x11:
    case 0x12:
        aligned = (static_cast<uint32_t>(depth * width) + 7) & 0xfffffff8;
        break;
    default:
        aligned = (static_cast<uint32_t>(depth * width) + 0xf) & 0xfffffff0;
        break;
    }
    return static_cast<int32_t>(aligned + ((static_cast<int32_t>(aligned) >> 31) & 7)) >> 3;
}

int32_t __cdecl ImageBytes(int32_t width, int32_t rows, int32_t bits, int32_t platform)
{
    return RowBytes(width, bits, platform) * rows;
}

int32_t __cdecl ClassifyRecord(const uint8_t *record, int32_t platform)
{
    const char first = static_cast<char>(record[0]);
    if (platform == 3) {
        if (first == '"') return kRecordPalette;
        if (first == 'o') return kRecordText;
        if (first == '|') return kRecordPairs;
        return kRecordShape;
    }
    switch (first & 0x7f) {
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x18:
    case 0x19:
    case 0x1a:
    case 0x1c:
    case 0x1e:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x58:
    case 0x59:
    case 0x5a:
    case 0x5b:
    case 0x5c:
    case 0x5d:
    case 0x5e:
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x6a:
    case 0x6c:
    case 0x6d:
    case 0x78:
    case 0x79:
    case 0x7a:
    case 0x7b:
    case 0x7d:
    case 0x7e:
    case 0x7f:
        return kRecordShape;
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2c:
    case 0x2d:
    case 0x2e:
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x38:
    case 0x39:
        return kRecordPalette;
    case 0x69:
        return kRecordMarker;
    case 0x6f:
        return kRecordText;
    case 0x7c:
        return kRecordPairs;
    default:
        return kRecordNone;
    }
}

int32_t __cdecl ShapeBits(const uint8_t *record, int32_t platform)
{
    const int32_t first = static_cast<int8_t>(record[0]);
    if (platform == 3) return first == 0x22 || first == 0x24 ? 0x18 : 8;
    switch (first & 0x7f) {
    case 0x01: return 4;
    case 0x02: return 8;
    case 0x03: return 0xf;
    case 0x04: return 0x18;
    case 0x05: return 0x20;
    case 0x10: return 4;
    case 0x13: return 0x10;
    case 0x14: return 0x10;
    case 0x15: return 0xf;
    case 0x16: return 0x20;
    case 0x18: return 4;
    case 0x1a: return 0x10;
    case 0x1c: return 0xc;
    case 0x1e: return 0x10;
    case 0x20: return 0xf;
    case 0x21: return 0x20;
    case 0x22: return 0x18;
    case 0x23: return 0x10;
    case 0x24: return 0x18;
    case 0x27: return 0x10;
    case 0x28: return 0x10;
    case 0x29: return 0x10;
    case 0x2a: return 0x20;
    case 0x2c: return 0x20;
    case 0x2d: return 0x10;
    case 0x2e: return 0x20;
    case 0x30: return 0x10;
    case 0x31: return 0x10;
    case 0x32: return 0xf;
    case 0x33: return 0x20;
    case 0x38: return 0x20;
    case 0x39: return 0xf;
    case 0x40: return 4;
    case 0x42: return 0x10;
    case 0x43: return 0x18;
    case 0x44: return 1;
    case 0x58: return 4;
    case 0x5a: return 0xf;
    case 0x5b: return 0x10;
    case 0x5c: return 0x10;
    case 0x5d: return 0x18;
    case 0x5e: return 0x20;
    case 0x60: return 0x10;
    case 0x61: return 0x20;
    case 0x62: return 0x20;
    case 0x63: return 0x10;
    case 0x65: return 0x10;
    case 0x66: return 0x18;
    case 0x67: return 0x10;
    case 0x68: return 0x10;
    case 0x6a: return 0x20;
    case 0x6c: return 0xc;
    case 0x6d: return 0x10;
    case 0x78: return 0x10;
    case 0x79: return 4;
    case 0x7a: return 4;
    case 0x7d: return 0x20;
    case 0x7e: return 0xf;
    case 0x7f: return 0x18;
    default: return 8;
    }
}

uint32_t __cdecl HasCodec(const uint8_t *record, int32_t codec, int32_t platform)
{
    const uint32_t attributes = ReadWord(record + kAttributeOffset, kAttributeBytes, platform);
    if ((attributes & kCodedBit) == 0) return 0;
    if (codec == kBlocksCodec) return IsBlockCompressed(record + kHeaderBytes);
    if (codec == kRefPackCodec) return IsRefPack(record + kHeaderBytes);
    if (codec == kGmCodec) return static_cast<uint32_t>(IsGm(record + kHeaderBytes));
    return 0;
}

int32_t __cdecl RecordCodec(const uint8_t *record, int32_t platform)
{
    if (record != nullptr && (ReadWord(record + kAttributeOffset, kAttributeBytes, platform) & kCodedBit) != 0) {
        const uint8_t *pixels = record + kHeaderBytes;
        if (IsBlockCompressed(pixels)) return kBlocksCodec;
        if (IsRefPack(pixels)) return kRefPackCodec;
        if (IsGm(pixels) != 0) return kGmCodec;
    }
    return 0;
}

int32_t __cdecl MipLevels(uint8_t *record, int32_t setLevels, int32_t platform)
{
    if (record == nullptr) return 0;
    const int32_t width = static_cast<int32_t>(ReadWord(record + kWidthOffset, 4, platform));
    const uint32_t attributes = ReadWord(record + kAttributeOffset, kAttributeBytes, platform);
    const int32_t count = static_cast<int32_t>(attributes) >> kMipShift & 0xf;
    int32_t levels = 1;
    if (count != 0xf && count != 0) levels = count + 1;
    if (setLevels != 0) {
        PutWord(record + kAttributeOffset, (static_cast<uint32_t>(setLevels + kMipBias) << kMipShift) + (attributes & kMipKeep),
                kAttributeBytes, platform);
    }
    if (1 << ((levels - 1) & 0x1f) > width) levels = 1;
    return levels;
}

int32_t __cdecl MipLevelsAt14(const uint8_t *record, int32_t platform)
{
    if (record == nullptr) return 0;
    const int32_t width = static_cast<int32_t>(ReadWord(record + kWidthOffset, 4, platform));
    const int32_t count = static_cast<int32_t>(ReadWord(record + 0x14, 4, platform)) >> 12;
    int32_t levels = 1;
    if (count != 0xf && count != 0) levels = count + 1;
    if (width < 1 << ((levels - 1) & 0x1f)) levels = 1;
    return levels;
}

uint32_t __cdecl RecordLength(const uint8_t *record, int32_t platform)
{
    return ReadWord(record + 4, 4, platform);
}

bool __cdecl IsSwizzled(const uint8_t *record, int32_t platform)
{
    return (ReadWord(record + kAttributeOffset, kAttributeBytes, platform) >> kSwizzledBit & 1) != 0;
}

bool __cdecl IsKnownRecord(const uint8_t *record, int32_t platform)
{
    return ClassifyRecord(record, platform) != 0;
}

bool __cdecl IsCode(const uint8_t *record, int32_t code)
{
    return static_cast<int32_t>(ReadBigEndian(record, 4)) == code;
}

int32_t __cdecl PaletteEntries(const uint8_t *palette, int32_t *start, int32_t bits, int32_t platform)
{
    int32_t count = static_cast<int32_t>(ReadWord(palette + kWidthOffset, 4, platform));
    const int32_t full = 1 << (bits & 0x1f);
    int32_t limit = 0xff;
    if (static_cast<uint32_t>(bits - 1) < 8) limit = full;
    if (count == 0) count = full;
    int32_t result = 0x100;
    *start = 0;
    if (count <= 0x100) {
        result = count;
        if (count < 0) return full;
    }
    if (result > limit) result = full;
    return result;
}

int32_t __cdecl RecordSize(const uint8_t *record, int32_t platform)
{
    int32_t total = 0;
    switch (ClassifyRecord(record, platform)) {
    case kRecordShape: {
        int32_t width = static_cast<int32_t>(ReadWord(record + kWidthOffset, 4, platform));
        int32_t height = static_cast<int32_t>(ReadWord(record + kHeightOffset, 4, platform));
        int32_t bits = ShapeBits(record, platform);
        const int8_t type = static_cast<int8_t>(record[0]);
        const bool blockType = type == kTypeFour || (type >= kTypeBlocks1 && type <= kTypeBlocksLast);
        bool blockForced = false;
        if (HasCodec(record, kBlocksCodec, platform) != 0 || blockType) {
            if (blockType && (type == kTypeBlocks61 || type == kTypeBlocks62)) {
                bits = 8;
            } else if (HasHeader1afb(record + kHeaderBytes) != 0 || HasHeader1cfb(record + kHeaderBytes) != 0) {
                bits = 8;
            } else {
                bits = 4;
            }
            blockForced = true;
        }
        int32_t levels = MipLevels(const_cast<uint8_t *>(record), 0, platform);
        total = kHeaderBytes;
        while (levels != 0) {
            --levels;
            if (HasCodec(record, kBlocksCodec, platform) == 0 && !blockType) {
                if (HasCodec(record, kGmCodec, platform) == 0) {
                    uint32_t pixelsWide = static_cast<uint32_t>(width);
                    uint32_t pixelsHigh = static_cast<uint32_t>(height);
                    if ((platform == 5 || platform == 6) && IsSwizzled(record, platform) && !blockForced) {
                        if (platform == 5) PaddedSize(&pixelsWide, &pixelsHigh, width, height, bits);
                        else PageSize(&pixelsWide, &pixelsHigh, static_cast<uint32_t>(width), static_cast<uint32_t>(height), bits);
                    }
                    total += ImageBytes(static_cast<int32_t>(pixelsWide), static_cast<int32_t>(pixelsHigh), bits, platform);
                } else {
                    int32_t scratch[4];
                    if (ParseGmHeader(record + kHeaderBytes, &scratch[0], &scratch[1], &scratch[2], &scratch[3], &total) != 0) {
                        total += kHeaderBytes;
                    }
                }
            } else {
                total += BlockLevelBytes(width, height, bits, platform);
                if (levels == 0 && !blockType) total += 8;
                blockForced = true;
            }
            width = width < 2 ? width : width >> 1;
            height = height < 2 ? height : height >> 1;
        }
        return total;
    }
    case kRecordPalette: {
        int32_t start;
        int32_t entries = PaletteEntries(record, &start, 8, platform);
        if ((platform == 6 || platform == 0x10) && IsSwizzled(record, platform)) {
            entries = static_cast<int32_t>(PaletteOrder(static_cast<uint32_t>(entries)));
        }
        const int32_t bytes = RowBytes(entries, ShapeBits(record, platform), platform);
        if (record[0] != '3') return bytes + 0x20;
        return ((entries * 2 + 0x1f) & ~0x1f) + (entries + 0x10) * 2;
    }
    case kRecordPairs:
        return static_cast<int32_t>(ReadWord(record + 0x1c, 4, platform)) * 8 + 0x20;
    case kRecordText:
        return static_cast<int32_t>(ReadWord(record + 0xc, 4, platform)) + 0x10;
    case kRecordMarker:
        return 0x10;
    default:
        return 0;
    }
}

void __cdecl ReadShapeText(const Directory *directory, int32_t frame, uint8_t *info)
{
    info[kInfoText] = 0;
    const uint8_t *text = EntryOf(directory, frame).text;
    if (text != nullptr) {
        // ORIGINAL-BUG: the text is copied with no bound (a text longer than the info's 0x400 bytes from here runs into the pairs).
        const uint8_t *from = text + 0x10;
        uint8_t *to = info + kInfoText;
        uint8_t c;
        do {
            c = *from++;
            *to++ = c;
        } while (c != 0);
    }
}

int32_t __cdecl ReadShapePairs(const Directory *directory, int32_t frame, uint8_t *info, int32_t platform)
{
    At<uint32_t>(info, kInfoPairCount) = 0;
    const uint8_t *pairs = EntryOf(directory, frame).pairs;
    if (pairs != nullptr) {
        At<uint32_t>(info, kInfoPairsA) = ReadWord(pairs + 0x10, 4, platform);
        At<uint32_t>(info, kInfoPairsB) = ReadWord(pairs + 0x14, 4, platform);
        const uint32_t count = ReadWord(pairs + 0x1c, 4, platform);
        At<uint32_t>(info, kInfoPairCount) = count;
        if (static_cast<int32_t>(count) > kMaxPairs) return 0;
        for (int32_t i = 0; i < static_cast<int32_t>(count); ++i) {
            At<uint32_t>(info, kInfoPairs + 8 * i) = ReadWord(pairs + 0x20 + 8 * i, 4, platform);
            At<uint32_t>(info, kInfoPairs + 4 + 8 * i) = ReadWord(pairs + 0x24 + 8 * i, 4, platform);
        }
    }
    return 1;
}

void __cdecl ReadShapeMarker(const Directory *directory, int32_t frame, uint8_t *info)
{
    if (EntryOf(directory, frame).marker != nullptr) At<uint32_t>(info, kInfoVariant) |= kMarkerFlag;
}

int32_t __cdecl FaceOffset(int32_t level, int32_t face, uint32_t alignMask, int32_t levelCount, int32_t *faceBytes, int32_t width,
                           int32_t height, int32_t bits)
{
    uint32_t offset = 0;
    for (int32_t i = 0; i < level; ++i) {
        offset += static_cast<uint32_t>(width * bits / 8 * height);
        if (i != levelCount - 1) offset = (offset + alignMask) & ~alignMask;
        width >>= 1;
        height >>= 1;
    }
    const double perFace = static_cast<double>(static_cast<float>(bits)) * 0.125 * static_cast<double>(height / kFaces) *
                           static_cast<double>(width);
    const int32_t bytes = _mm_cvttsd_si32(_mm_set_sd(perFace));
    *faceBytes = bytes;
    return static_cast<int32_t>(static_cast<uint32_t>(bytes * face) + offset);
}

uint32_t __cdecl NextLevelOffset(int32_t level, int32_t offset, uint32_t alignMask, int32_t levelCount, int32_t bytes)
{
    uint32_t next = static_cast<uint32_t>(offset + bytes);
    if (level < levelCount - 1) next = (next + alignMask) & ~alignMask;
    return next;
}

void *__cdecl AllocateFaces(int32_t width, int32_t height, int32_t bits, int32_t *bytes, int32_t levels, uint32_t alignMask)
{
    int32_t used = 0;
    int32_t faceHeight = height / kFaces;
    const double perPixel = static_cast<double>(static_cast<float>(bits)) * 0.125;
    for (int32_t i = levels; i > 0; --i) {
        const double level = static_cast<double>(width) * perPixel * static_cast<double>(faceHeight) +
                             static_cast<double>(static_cast<int32_t>((static_cast<uint32_t>(used) + alignMask) & ~alignMask));
        used += _mm_cvttsd_si32(_mm_set_sd(level));
        width >>= 1;
        faceHeight >>= 1;
        if (faceHeight < 1) faceHeight = 1;
        if (width < 1) width = 1;
    }
    const int32_t total = used * kFaces + 0x20;
    *bytes = total;
    return Allocate(static_cast<uint32_t>(total));
}

void __cdecl BlockBits(const uint8_t *record, const uint8_t *typeSource, int32_t forced, int32_t *bits, int32_t platform)
{
    if (HasCodec(record, kBlocksCodec, platform) == 0 && forced == 0) return;
    if (HasHeader1afb(record + kHeaderBytes) != 0 || ReadWord(typeSource, 1, platform) == kTypeBlocks61 ||
        HasHeader1cfb(record + kHeaderBytes) != 0 || ReadWord(typeSource, 1, platform) == kTypeBlocks62) {
        *bits = 8;
    } else {
        *bits = 4;
    }
}

uint32_t __cdecl IsTiled(const uint8_t *record, int32_t platform, uint32_t width, uint32_t height, int32_t bits)
{
    uint32_t tiled = ReadWord(record + kAttributeOffset, kAttributeBytes, platform) & 0x4000;
    if (tiled != 0) {
        if (platform == 6 || platform == 0x10) {
            if ((width & (width - 1)) == 0 && (height & (height - 1)) == 0) tiled = 1;
        } else if (platform == 5 || platform == 0xf) {
            if (bits == 4 || bits == 8 || bits == 0xc || bits == 0xf || bits == 0x10 || bits == 0x20) tiled = 1;
        }
    }
    return tiled;
}

int32_t __cdecl SplitFaces(Directory *directory, int32_t platform, uint32_t alignMask)
{
    for (int32_t index = 0; index < directory->count; ++index) {
        Entry &entry = directory->entries[index];
        uint8_t *record = entry.shape;
        const int32_t levels = MipLevelsAt14(record, platform);
        if ((ReadWord(record + kAttributeOffset, kAttributeBytes, platform) & 0x100) != 0 && levels > 1) {
            int32_t faceBytes = 0;
            int32_t sourceOffset = kHeaderBytes;
            const int32_t width = static_cast<int32_t>(ReadWord(record + kWidthOffset, 4, platform));
            const int32_t height = static_cast<int32_t>(ReadWord(record + kHeightOffset, 4, platform));
            int32_t bits = ShapeBits(record, platform);
            const uint32_t type = ReadWord(record, 1, platform);
            const int32_t blockType = (type >= 0x60 && type <= 0x62) || type == 0x1e ? 1 : 0;
            BlockBits(record, record, blockType, &bits, platform);
            int32_t newBytes;
            uint8_t *rebuilt = static_cast<uint8_t *>(AllocateFaces(width, height, bits, &newBytes, levels, alignMask));
            if (rebuilt == nullptr || newBytes < 1) return 0;
            std::memset(rebuilt, 0, static_cast<size_t>(newBytes));
            std::memcpy(rebuilt, record, kHeaderBytes);
            for (int32_t face = 0; face < kFaces; ++face) {
                for (int32_t level = 0; level < levels; ++level) {
                    const int32_t at = FaceOffset(level, face, alignMask, levels, &faceBytes, width, height, bits);
                    std::memcpy(rebuilt + at + kHeaderBytes, record + sourceOffset, static_cast<size_t>(faceBytes));
                    sourceOffset = static_cast<int32_t>(NextLevelOffset(level, sourceOffset, alignMask, levels, faceBytes));
                }
            }
            if (entry.shape != nullptr) FreeMemory(entry.shape);
            entry.shape = rebuilt;
            entry.shapeBytes = static_cast<uint32_t>(newBytes);
        }
    }
    return 1;
}

void __cdecl FreeDirectory(Directory *directory)
{
    for (int32_t i = 0; i < directory->count; ++i) {
        Entry &entry = directory->entries[i];
        if (entry.shape != nullptr) FreeMemory(entry.shape);
        if (entry.palette != nullptr) FreeMemory(entry.palette);
        if (entry.pairs != nullptr) FreeMemory(entry.pairs);
        if (entry.marker != nullptr) FreeMemory(entry.marker);
        if (entry.userData != nullptr) FreeMemory(entry.userData);
        if (entry.text != nullptr) FreeMemory(entry.text);
        if (entry.name != nullptr) FreeMemory(entry.name);
        entry.shape = nullptr;
        entry.palette = nullptr;
        entry.pairs = nullptr;
        entry.marker = nullptr;
        entry.userData = nullptr;
        entry.text = nullptr;
    }
    FreeMemory(directory);
}

} // namespace fsh
} // namespace gimex
