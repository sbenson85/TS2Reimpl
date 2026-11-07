// The FSH back end's encode side, helpers (lane MY; src/gz/GimexFshWrite.h, 0x889917..0x88a1ed, 0x88a8f6..0x88a9ad): sizes of rows and planes,
// the pixel width of a shape type, the mip level count, and the loops over a directory's blocks (padding, chaining, freeing).
#include "gz/GimexFshWrite.h"

#include <cstring>

#include "game/original.h"
#include "gz/GimexFshDecode.h"
#include "gz/GimexFshLeaves.h"

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kBits15 = 15;
constexpr int32_t kBitsRowFor15 = 16;
constexpr uint32_t kRowAlign8 = 7;
constexpr uint32_t kRowAlign16 = 15;
constexpr int32_t kPadMask = 3;
constexpr uint32_t kMipLevelsShift = 20;
constexpr uint32_t kMipLevelsMask = 0xf;
constexpr uint32_t kMipLevelsUnset = 0xf;
constexpr int32_t kCompressedKind = 3;
constexpr uint32_t kAttributeBytes = 3;
constexpr uint32_t kShapeAttributes = 1;
constexpr uint32_t kShapeWidth = 0x18;
constexpr uint32_t kLevelNibble = 0x25;

// Platforms whose rows are 8 bits aligned (the others 16): 2..5, 7 and 8.
bool ByteAlignedRows(int32_t platform)
{
    switch (platform) {
    case 2:
    case 3:
    case 4:
    case 5:
    case 7:
    case 8: return true;
    default: return false;
    }
}

} // namespace

// 0x8899a3
int32_t __cdecl EncodeRowBytes(int32_t width, int32_t bits, int32_t platform)
{
    const uint32_t perPixel = static_cast<uint32_t>(bits == kBits15 ? kBitsRowFor15 : bits);
    const uint32_t row = perPixel * static_cast<uint32_t>(width);
    const int32_t aligned = static_cast<int32_t>(ByteAlignedRows(platform) ? (row + kRowAlign8) & ~kRowAlign8 : (row + kRowAlign16) & ~kRowAlign16);
    return (aligned + ((aligned >> 31) & 7)) >> 3;
}

// 0x8899fc
int32_t __cdecl PlaneBytes(uint32_t width, uint32_t height, int32_t bits, int32_t platform, int32_t pad)
{
    if (pad != 0) {
        width = (width + kPadMask) & ~static_cast<uint32_t>(kPadMask);
        height = (height + kPadMask) & ~static_cast<uint32_t>(kPadMask);
    }
    return static_cast<int32_t>(static_cast<uint32_t>(EncodeRowBytes(static_cast<int32_t>(width), bits, platform)) * height);
}

// 0x88a187
int32_t __cdecl EncodeMipLevels(const uint8_t *shape, int32_t platform)
{
    if (shape == nullptr) return 0;
    const int32_t width = static_cast<int32_t>(ReadWord(shape + kShapeWidth, 4, platform));
    const uint32_t declared = (ReadWord(shape + kShapeAttributes, kAttributeBytes, platform) >> kMipLevelsShift) & kMipLevelsMask;
    int32_t levels = 1;
    if (declared != kMipLevelsUnset && declared != 0) levels = static_cast<int32_t>(declared) + 1;
    if (width < (1 << ((levels - 1) & 31))) levels = 1;
    return levels;
}

// 0x88a1ed
int32_t __cdecl EncodeShapeBits(const uint8_t *shape, int32_t platform)
{
    if (platform == 3) {
        const int32_t type = static_cast<int8_t>(shape[0]);
        return (type == 0x22 || type == 0x24) ? 24 : 8;
    }
    switch (shape[0] & 0x7f) {
    case 0x01:
    case 0x10:
    case 0x18:
    case 0x1e:
    case 0x40:
    case 0x58:
    case 0x60:
    case 0x63:
    case 0x79:
    case 0x7a: return 4;
    case 0x03:
    case 0x15:
    case 0x20:
    case 0x32:
    case 0x39:
    case 0x5a:
    case 0x7e: return 15;
    case 0x04:
    case 0x22:
    case 0x24:
    case 0x43:
    case 0x5d:
    case 0x66:
    case 0x7f: return 24;
    case 0x05:
    case 0x16:
    case 0x21:
    case 0x2a:
    case 0x2c:
    case 0x2e:
    case 0x38:
    case 0x5e:
    case 0x6a:
    case 0x7d: return 32;
    case 0x13:
    case 0x14:
    case 0x1a:
    case 0x23:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2d:
    case 0x30:
    case 0x31:
    case 0x42:
    case 0x5b:
    case 0x5c:
    case 0x65:
    case 0x67:
    case 0x68:
    case 0x6d:
    case 0x78: return 16;
    case 0x1c:
    case 0x6c: return 12;
    case 0x44: return 1;
    default: return 8;
    }
}

// 0x88a105
void __cdecl FreeWrittenDirectory(Directory *directory)
{
    Entry *entries = EntriesOf(directory);
    for (int32_t i = 0; i < directory->count; ++i) {
        Entry &entry = entries[i];
        if (entry.shape != nullptr) FreeMemory(entry.shape);
        if (entry.palette != nullptr) FreeMemory(entry.palette);
        if (entry.marker != nullptr) FreeMemory(entry.marker);
        if (entry.userData != nullptr) FreeMemory(entry.userData);
        if (entry.pairs != nullptr) FreeMemory(entry.pairs);
        if (entry.text != nullptr) FreeMemory(entry.text);
        if (entry.name != nullptr) FreeMemory(entry.name);
    }
    FreeMemory(directory);
}

// 0x889917
void __cdecl PadShapes(Directory *directory, int32_t /*unused*/, uint32_t mask)
{
    Entry *entries = EntriesOf(directory);
    for (int32_t i = 0; i < directory->count; ++i) {
        Entry &entry = entries[i];
        if (entry.shape == nullptr || entry.shapeBytes == 0 || entry.paletteBytes == 0) continue;
        const uint32_t size = entry.shapeBytes;
        const uint32_t padded = (size + mask) & ~mask;
        const int32_t extra = static_cast<int32_t>(padded - size);
        if (extra <= 0) continue;
        uint8_t *block = static_cast<uint8_t *>(Allocate(padded));
        std::memset(block + size, 0, static_cast<uint32_t>(extra));
        std::memcpy(block, entry.shape, size);
        FreeMemory(entry.shape);
        entry.shape = block;
        entry.shapeBytes = padded;
    }
}

// 0x88a962
void __cdecl ChainBlock(uint8_t **tail, uint32_t *tailBytes, uint8_t *block, uint32_t bytes, int32_t platform)
{
    if (block == nullptr) return;
    if (*tail != nullptr) PutWord(*tail + 4, *tailBytes, 4, platform);
    PutWord(block + 4, 0, 4, platform);
    *tail = block;
    *tailBytes = bytes;
}

// 0x88a9ad
int32_t __cdecl ChainEntryBlocks(Directory *directory, int32_t platform)
{
    Entry *entries = EntriesOf(directory);
    for (int32_t i = 0; i < directory->count; ++i) {
        Entry &entry = entries[i];
        uint8_t *tail = nullptr;
        uint32_t tailBytes = 0;
        ChainBlock(&tail, &tailBytes, entry.shape, entry.shapeBytes, platform);
        ChainBlock(&tail, &tailBytes, entry.palette, entry.paletteBytes, platform);
        ChainBlock(&tail, &tailBytes, entry.marker, entry.markerBytes, platform);
        ChainBlock(&tail, &tailBytes, entry.userData, entry.userDataBytes, platform);
        ChainBlock(&tail, &tailBytes, entry.pairs, entry.pairsBytes, platform);
        ChainBlock(&tail, &tailBytes, entry.text, entry.textBytes, platform);
    }
    return 0;
}

// 0x88a8f6
void __cdecl SetMipLevels(uint8_t *shape, int32_t count, int32_t platform)
{
    if (count == 0) count = 1;
    if (shape == nullptr) return;
    const uint32_t attributes = ReadWord(shape + kShapeAttributes, kAttributeBytes, platform);
    PutWord(shape + kShapeAttributes, (attributes & 0xff0fffffu) + (static_cast<uint32_t>(count) + 0xfffu) * 0x100000u, kAttributeBytes, platform);
    if (HasCodec(shape, kCompressedKind, platform) != 0) {
        shape[kLevelNibble] = static_cast<uint8_t>(static_cast<uint8_t>(count << 4) - 0x10u) | (shape[kLevelNibble] & 0xf);
    }
}

} // namespace fsh

} // namespace gimex
