// The FSH back end's encode side, shape assembly (lane MY; src/gz/GimexFshWrite.h, 0x889a2d, 0x88a3c9, 0x88a5b2, 0x88a731): the merge of the
// mip-level entries of a directory into their base shape, the six-face expansion of a cube map, and the two pixel tilings the block
// compressor feeds (4 x 4 blocks in row order, and in Morton order of 8 x 8-aligned images).
#include "gz/GimexFshWrite.h"

#include <cstring>

#include "game/original.h"
#include "gz/GimexFshBlocks.h"
#include "gz/GimexFshConvert.h"
#include "gz/GimexFshDecode.h"
#include "gz/GimexFshLeaves.h"

namespace gimex {

namespace fsh {

namespace {

constexpr uint32_t kShapeAttributes = 1;
constexpr uint32_t kShapeWidth = 0x18;
constexpr uint32_t kShapeHeight = 0x1c;
constexpr uint32_t kShapeHeaderBytes = 0x20;
constexpr uint32_t kShapeExtraHeader = 8;          // a block compressed shape of the detected kind carries 8 more header bytes
constexpr uint32_t kAttributeBytes = 3;
constexpr uint32_t kAttributeTiled = 0x4000;
constexpr uint32_t kAttributeCube = 0x100;
constexpr uint32_t kEntryCubeFlag = 0x40000;
constexpr int32_t kPlatformTiled = 5;
constexpr int32_t kPlatformPaged = 6;
constexpr int32_t kRefPackKind = 2;
constexpr uint32_t kRoundTo8 = 7;
constexpr int32_t kCubeFaces = 6;
constexpr uint32_t kMergedSlack = 0x400;
constexpr uint8_t kTypeFirstBlock = 0x60;
constexpr uint8_t kTypeLastBlock = 0x62;
constexpr uint8_t kTypeBlockGrey = 0x1e;

uint32_t RoundUp8(uint32_t v)
{
    return (v + kRoundTo8) & ~kRoundTo8;
}

bool IsTiledPlatform(int32_t platform)
{
    return platform == kPlatformTiled || platform == kPlatformPaged;
}

// The padded size of a w x h image for the tiled platforms (5: PaddedSize, 6: PageSize); (0, 0) for another platform.
void PaddedDimensions(int32_t platform, uint32_t w, uint32_t h, int32_t bits, uint32_t *outW, uint32_t *outH)
{
    *outW = 0;
    *outH = 0;
    if (platform == kPlatformTiled) PaddedSize(outW, outH, static_cast<int32_t>(w), static_cast<int32_t>(h), bits);
    else if (platform == kPlatformPaged) PageSize(outW, outH, w, h, bits);
}

// The bytes of a level of w x h pixels in the merged shape: on a tiled platform with the tiled attribute and no detected kind the padded size
// of the tiling; else the level's own size (rounded up to 8 on platform 5 when a kind was detected), padded to blocks of 4 for a kind.
int32_t LevelBytes(uint32_t w, uint32_t h, int32_t bits, int32_t platform, int32_t kind, bool tiledAttribute)
{
    if (IsTiledPlatform(platform) && tiledAttribute) {
        if (kind == 0) {
            uint32_t paddedW, paddedH;
            PaddedDimensions(platform, w, h, bits, &paddedW, &paddedH);
            return PlaneBytes(paddedW, paddedH, bits, platform, 0);
        }
    } else if (kind == 0) {
        return PlaneBytes(w, h, bits, platform, 0);
    }
    if (platform == kPlatformTiled) {
        w = RoundUp8(w);
        h = RoundUp8(h);
    }
    return PlaneBytes(w, h, bits, platform, kind);
}

} // namespace

// 0x88a5b2
void __cdecl TilePixels(int32_t width, int32_t height, int32_t stride, const uint8_t *source, uint8_t *dest)
{
    uint8_t *out = dest;
    int32_t rowsDone = 0;
    if (0 < height) {
        int32_t padRows = -height;
        int32_t remainingRows = height;
        do {
            padRows += 4;
            rowsDone += 4;
            int32_t bandPad = padRows;
            int32_t bandRows = remainingRows;
            if (rowsDone <= height) {
                bandPad = 0;
                bandRows = 4;
            }
            const uint8_t *column = source;
            if (0 < width) {
                int32_t columnsDone = 0;
                int32_t padColumns = -width;
                int32_t remainingColumns = width;
                do {
                    padColumns += 4;
                    columnsDone += 4;
                    int32_t copyColumns = 4;
                    if (width < columnsDone) copyColumns = remainingColumns;
                    int32_t tilePad = padColumns;
                    if (columnsDone <= width) tilePad = 0;
                    if (0 < bandRows) {
                        const uint8_t *row = column;
                        int32_t left = bandRows;
                        do {
                            std::memcpy(out, row, static_cast<size_t>(copyColumns) * 4);
                            if (tilePad != 0) std::memset(out + copyColumns * 4, 0, static_cast<size_t>(tilePad) << 2);
                            row += stride;
                            out += 0x10;
                        } while (--left != 0);
                    }
                    if (0 < bandPad) {
                        std::memset(out, 0, static_cast<size_t>(bandPad) * 0x10);
                        out += bandPad * 0x10;
                    }
                    column += 0x10;
                    remainingColumns -= 4;
                } while (columnsDone < width);
            }
            source += stride * 4;
            remainingRows -= 4;
        } while (rowsDone < height);
    }
}

// 0x88a731
void __cdecl TileBlocks(uint32_t paddedWidth, int32_t paddedHeight, int32_t width, int32_t height, int32_t stride, const uint8_t *source,
                        uint8_t *dest)
{
    const int32_t signedWidth = static_cast<int32_t>(paddedWidth);
    if (0 >= paddedHeight) return;
    int32_t remainingRows = height;
    int32_t y = 0;
    do {
        const int32_t nextY = y + 4;
        int32_t bandRows = 4;
        if (height < nextY) {
            int32_t clipped = remainingRows;
            if (4 < clipped) clipped = 4;
            bandRows = 0;
            if (-1 < clipped) bandRows = clipped;
        }
        if (0 < signedWidth) {
            int32_t x = 0;
            int32_t remainingColumns = width;
            const uint8_t *tileSource = source;
            do {
                const int32_t nextX = x + 4;
                int32_t columns = 4;
                int32_t pad = 0;
                if (width < nextX) {
                    int32_t clipped = remainingColumns;
                    if (4 < clipped) clipped = 4;
                    columns = 0;
                    if (-1 < clipped) columns = clipped;
                    pad = 4 - columns;
                }
                const int32_t index = BlockIndex(static_cast<int32_t>(x >> 2), static_cast<int32_t>(y >> 2), static_cast<int32_t>(paddedWidth >> 2));
                uint8_t *out = dest + static_cast<uint32_t>(index) * 0x40;
                const uint8_t *rowSource = tileSource;
                for (int32_t row = 0; row < 4; ++row) {
                    if (row < bandRows) {
                        std::memcpy(out, rowSource, static_cast<size_t>(columns) << 2);
                        if (pad != 0) std::memset(out + columns * 4, *tileSource, static_cast<size_t>(pad) << 2);
                    } else if (bandRows == 0) {
                        std::memset(out, 0, 16);
                    } else {
                        std::memset(out, *tileSource, 16);
                    }
                    out += 16;
                    rowSource += stride;
                }
                tileSource += 0x10;
                remainingColumns -= 4;
                x = nextX;
            } while (x < signedWidth);
        }
        source += stride * 4;
        remainingRows -= 4;
        y = nextY;
    } while (y < paddedHeight);
}

// 0x88a3c9
void __cdecl ExpandCubeMaps(Directory *directory, int32_t platform, uint32_t unused, uint32_t align)
{
    Entry *entries = EntriesOf(directory);
    uint32_t chainBytes = 0;
    for (int32_t i = 0; i < directory->count; ++i) {
        Entry &entry = entries[i];
        uint8_t *header = entry.shape;
        const int32_t levels = EncodeMipLevels(header, platform);
        if ((entry.flags & kEntryCubeFlag) == 0 || levels <= 1) continue;
        uint32_t levelSize = 0;
        int32_t offset = static_cast<int32_t>(kShapeHeaderBytes);
        const int32_t width = static_cast<int32_t>(ReadWord(header + kShapeWidth, 4, platform));
        const int32_t height = static_cast<int32_t>(ReadWord(header + kShapeHeight, 4, platform));
        int32_t bits = EncodeShapeBits(header, platform);
        int32_t kind;
        const uint32_t type = ReadWord(header, 1, platform);
        const bool force = (type >= kTypeFirstBlock && type <= kTypeLastBlock) || type == kTypeBlockGrey;
        DetectPixelFormat(header, header, force ? 1 : 0, &bits, &kind, platform);
        uint8_t *chain = static_cast<uint8_t *>(
            AllocateMipChain(width, height, bits, reinterpret_cast<int32_t *>(&chainBytes), levels, align));
        std::memset(chain, 0, chainBytes);
        std::memcpy(chain, header, kShapeHeaderBytes);
        PutWord(chain + kShapeAttributes, ReadWord(chain + kShapeAttributes, kAttributeBytes, platform) | kAttributeCube, kAttributeBytes,
                platform);
        for (int32_t face = 0; face < kCubeFaces; ++face) {
            for (int32_t level = 0; level < levels; ++level) {
                const int32_t source = MipChainSize(level, face, unused, align, levels, reinterpret_cast<int32_t *>(&levelSize), width, height, bits);
                std::memcpy(chain + offset, header + source + kShapeHeaderBytes, levelSize);
                offset = AlignedOffset(level, offset, unused, align, levels, static_cast<int32_t>(levelSize));
            }
        }
        if (entry.shape != nullptr) FreeMemory(entry.shape);
        entry.shape = chain;
        entry.shapeBytes = chainBytes;
    }
}

// 0x889a2d
void __cdecl MergeMipmaps(Directory *directory, int32_t platform, uint32_t /*unused*/, uint32_t align)
{
    Entry *entries = EntriesOf(directory);
    uint8_t *merged = nullptr;     // the base shape's replacement, built when its first mip level is found
    uint32_t position = 0;         // where the next level goes in `merged`
    uint32_t levelCount = 0;
    int32_t base = 0;              // the entry the merged levels belong to
    bool mergedHasHeader8 = false;
    (void)mergedHasHeader8;
    if (directory->count > 1) {
        for (int32_t i = 1; i < directory->count; ++i) {
            Entry &current = entries[i];
            uint8_t *currentShape = current.shape;
            uint8_t *baseShape = entries[base].shape;
            bool endsChain = false;
            if (!IsMipmapName(current.name, 0) || EncodeShapeBits(baseShape, platform) != EncodeShapeBits(currentShape, platform)) {
                endsChain = true;
            } else {
                int32_t kind = 0;
                const uint32_t currentWidth = ReadWord(currentShape + kShapeWidth, 4, platform);
                const uint32_t currentHeight = ReadWord(currentShape + kShapeHeight, 4, platform);
                int32_t bits = EncodeShapeBits(currentShape, platform);
                const uint32_t type = ReadWord(baseShape, 1, platform);
                const bool force = (type >= kTypeFirstBlock && type <= kTypeLastBlock) || type == kTypeBlockGrey;
                DetectPixelFormat(baseShape, currentShape, force ? 1 : 0, &bits, &kind, platform);
                const bool tiledAttribute = (ReadWord(baseShape + kShapeAttributes, kAttributeBytes, platform) & kAttributeTiled) != 0;
                const bool extraHeader = kind != 0 && !force;
                if (levelCount == 0) {
                    // The first level of this chain: size the merged shape for every level and copy the base level in.
                    uint32_t baseWidth = ReadWord(baseShape + kShapeWidth, 4, platform);
                    uint32_t baseHeight = ReadWord(baseShape + kShapeHeight, 4, platform);
                    bool viaAttribute;
                    if (kind != 0) {
                        if (platform == kPlatformTiled) {
                            baseWidth = RoundUp8(baseWidth);
                            baseHeight = RoundUp8(baseHeight);
                            viaAttribute = true;
                        } else {
                            viaAttribute = platform == kPlatformPaged;
                        }
                    } else {
                        viaAttribute = IsTiledPlatform(platform);
                    }
                    int32_t baseBytes;
                    if (viaAttribute && tiledAttribute && kind == 0) {
                        uint32_t paddedW, paddedH;
                        PaddedDimensions(platform, baseWidth, baseHeight, bits, &paddedW, &paddedH);
                        baseBytes = PlaneBytes(paddedW, paddedH, bits, platform, 0);
                    } else {
                        baseBytes = PlaneBytes(baseWidth, baseHeight, bits, platform, kind);
                    }
                    uint32_t total = static_cast<uint32_t>(baseBytes);
                    uint32_t w = baseWidth, h = baseHeight;
                    while (static_cast<int32_t>(w) > 1 || static_cast<int32_t>(h) > 1) {
                        if (static_cast<int32_t>(w) > 1) w = static_cast<uint32_t>(static_cast<int32_t>(w) >> 1);
                        if (static_cast<int32_t>(h) > 1) h = static_cast<uint32_t>(static_cast<int32_t>(h) >> 1);
                        const int32_t levelBytes = LevelBytes(w, h, bits, platform, kind, tiledAttribute);
                        total += static_cast<uint32_t>(levelBytes) + ((static_cast<uint32_t>(baseBytes) + align) & ~align);
                    }
                    total += kShapeHeaderBytes;
                    if (extraHeader) total += kShapeExtraHeader;
                    const uint32_t allocation = total * 2 + kMergedSlack;
                    merged = static_cast<uint8_t *>(Allocate(allocation));
                    std::memset(merged, 0, allocation);
                    std::memcpy(merged, baseShape, kShapeHeaderBytes);
                    uint32_t copied;
                    if (HasCodec(baseShape, kRefPackKind, platform) != 0) {
                        DecodeRefPack(merged + kShapeHeaderBytes, baseShape + kShapeHeaderBytes, nullptr);
                        copied = static_cast<uint32_t>(baseBytes);
                    } else {
                        copied = static_cast<uint32_t>(baseBytes) + (extraHeader ? kShapeExtraHeader : 0);
                        std::memcpy(merged + kShapeHeaderBytes, baseShape + kShapeHeaderBytes, copied);
                    }
                    position = copied + kShapeHeaderBytes;
                    levelCount = 1;
                }
                const uint32_t headerBytes = extraHeader ? kShapeHeaderBytes + kShapeExtraHeader : kShapeHeaderBytes;
                const uint32_t placed = ((position + (align - headerBytes)) & ~align) + headerBytes;
                const int32_t currentBytes = LevelBytes(currentWidth, currentHeight, bits, platform, kind, tiledAttribute);
                if (HasCodec(currentShape, kRefPackKind, platform) != 0) {
                    DecodeRefPack(merged + placed, currentShape + kShapeHeaderBytes, nullptr);
                } else {
                    std::memcpy(merged + placed, currentShape + headerBytes, static_cast<size_t>(currentBytes));
                }
                if (current.shape != nullptr) FreeMemory(current.shape);
                if (current.palette != nullptr) FreeMemory(current.palette);
                if (current.marker != nullptr) FreeMemory(current.marker);
                if (current.userData != nullptr) FreeMemory(current.userData);
                if (current.pairs != nullptr) FreeMemory(current.pairs);
                if (current.text != nullptr) FreeMemory(current.text);
                if (current.name != nullptr) FreeMemory(current.name);
                position = placed + static_cast<uint32_t>(currentBytes);
                ++levelCount;
                current.shapeBytes = 0;
                current.paletteBytes = 0;
                current.markerBytes = 0;
                current.pairsBytes = 0;
                current.textBytes = 0;
                current.name = nullptr;
            }
            if (i == directory->count - 1 || endsChain) {
                if (merged != nullptr) {
                    uint32_t finalBytes = (position + 0xf) & ~0xfu;
                    uint8_t *result = merged;
                    if (HasCodec(baseShape, kRefPackKind, platform) != 0) {
                        result = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(static_cast<int32_t>(finalBytes) * 3 / 2 + static_cast<int32_t>(kMergedSlack))));
                        std::memcpy(result, merged, kShapeHeaderBytes);
                        const int32_t packed = RefPackEncodeWithHeader(result + kShapeHeaderBytes, merged + kShapeHeaderBytes,
                                                                       static_cast<int32_t>(finalBytes - kShapeHeaderBytes), 0);
                        finalBytes = static_cast<uint32_t>(packed) + kShapeHeaderBytes;
                        FreeMemory(merged);
                    }
                    FreeMemory(entries[base].shape);
                    entries[base].shape = result;
                    entries[base].shapeBytes = finalBytes;
                    SetMipLevels(result, static_cast<int32_t>(levelCount), platform);
                }
                merged = nullptr;
                levelCount = 0;
                base = i;
                position = 0;
            }
        }
    }
    // Drop the entries that are left with nothing in them.
    int32_t kept = 0;
    for (int32_t i = 0; i < directory->count; ++i) {
        const Entry &entry = entries[i];
        if (entry.shapeBytes != 0 || entry.paletteBytes != 0 || entry.markerBytes != 0 || entry.pairsBytes != 0 || entry.textBytes != 0) {
            if (i != kept) entries[kept] = entry;
            ++kept;
        }
    }
    directory->count = kept;
}

} // namespace fsh

} // namespace gimex
