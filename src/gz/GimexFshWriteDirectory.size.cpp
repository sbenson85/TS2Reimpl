// The FSH back end's encode side, the directory writer (lane MZ; src/gz/GimexFshWrite.h, 0x88c719): serialises a directory the encoder
// (EncodeShape, 0x88905f) filled into the stream. It first prepares the directory with the helpers of lane MY (merge the mip levels into their
// shapes, expand cube maps, pad the shapes, chain the blocks), then writes, per entry, the blocks in the order shape, palette, marker,
// user data, pairs, text. Two layouts: 0, a 16-byte header and a table of (offset, size, name) first (the real file; the header is written
// again at the end once the length is known), and 1, a chunk header of 8 bytes before each entry.
#include "gz/GimexFshWrite.h"

#include <cstring>

#include "gz/GimexFshDecode.h"
#include "gz/GimexFshLeaves.h"

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kLayoutDirectory = 0;
constexpr int32_t kLayoutChunks = 1;
constexpr uint32_t kHeaderBytes = 0x10;
constexpr uint32_t kChunkHeaderBytes = 8;
constexpr uint32_t kTableEntryBytes = 9; // an offset and a size (4 bytes each) around the NUL-terminated name, less its terminator counted
constexpr uint32_t kEntryFlagMerged = 0x200; // the first entry's flag: its mip levels are in the shape already
constexpr uint32_t kHeaderTableExtra = 0x18;
constexpr int32_t kRecordHeaderBytes = 0x20;
constexpr int32_t kCodecRecordHeaderBytes = 0x28;
// The filler written before a record to reach its alignment: the original keeps these two words of text in the block it writes.
constexpr uint32_t kFillerFirst = 0x20797542;  // "Buy "
constexpr uint32_t kFillerSecond = 0x53545245; // "ERTS"

// The bytes of an entry's blocks a table entry and a chunk header count (the user data is not among them).
uint32_t CountedBytes(const Entry &entry)
{
    return entry.shapeBytes + entry.paletteBytes + entry.markerBytes + entry.pairsBytes + entry.textBytes;
}

} // namespace

// 0x88c719. ORIGINAL-QUIRKS: the placeholder header of the directory layout is 16 bytes of the original's uninitialised stack (always
// overwritten at the end when the writing succeeds); ours writes zeros. A failed write of an entry's block does not stop the loop: the next
// entry's filler write (layout 0) resets the status, and layout 1's chunk header sets it to the byte count it wrote (8), which is what the
// function returns when nothing is written after it.
uint32_t __cdecl WriteDirectory(ifaces::cIGZFile *stream, Directory *directory, int32_t platform, int32_t layout, int32_t writeHeaders,
                                uint32_t fourcc, uint32_t alignBase, uint32_t padMask, uint32_t unused, int32_t cubeAlign, uint32_t codecAlign)
{
    Entry *entries = EntriesOf(directory);
    if (directory->count > 0 && (entries[0].flags & kEntryFlagMerged) == 0) MergeMipmaps(directory, platform, unused, cubeAlign);
    ExpandCubeMaps(directory, platform, unused, cubeAlign);
    PadShapes(directory, platform, padMask);
    ChainEntryBlocks(directory, platform);

    const bool tabled = layout == kLayoutDirectory && writeHeaders != 0;
    uint8_t header[kHeaderBytes] = {};
    uint8_t *table = nullptr;
    uint32_t tableBytes = 0;
    uint32_t status = 1;
    if (tabled) {
        if (StreamWrite(stream, header, kHeaderBytes) != static_cast<int32_t>(kHeaderBytes)) return 0;
        for (int32_t i = 0; i < directory->count; ++i) tableBytes += static_cast<uint32_t>(std::strlen(entries[i].name)) + kTableEntryBytes;
        if (tableBytes != 0) {
            table = static_cast<uint8_t *>(Allocate(tableBytes));
            if (table == nullptr) return 0;
            std::memset(table, 0, tableBytes);
            status = StreamWrite(stream, table, static_cast<int32_t>(tableBytes)) == static_cast<int32_t>(tableBytes);
            if (!status) {
                FreeMemory(table);
                return 0;
            }
        }
    }

    uint8_t *cursor = table;
    for (int32_t i = 0; i < directory->count; ++i) {
        const Entry &entry = entries[i];
        uint32_t position = static_cast<uint32_t>(StreamLength(stream));
        const uint32_t align = entry.frameDepth != 0 ? entry.frameDepth : alignBase;
        if (layout == kLayoutDirectory) {
            if (writeHeaders != 0) {
                int32_t gap;
                if (RecordCodec(entry.shape, platform) != 0)
                    gap = static_cast<int32_t>(((codecAlign + kCodecRecordHeaderBytes + position) & ~codecAlign) - position - kCodecRecordHeaderBytes);
                else
                    gap = static_cast<int32_t>(((align + kRecordHeaderBytes + position) & ~align) - position - kRecordHeaderBytes);
                if (gap > 0) {
                    uint8_t *filler = static_cast<uint8_t *>(Allocate(align + 1));
                    std::memset(filler, 0, align + 1);
                    std::memcpy(filler, &kFillerFirst, 4);
                    std::memcpy(filler + 4, &kFillerSecond, 4);
                    status = StreamWrite(stream, filler, gap) == gap;
                    FreeMemory(filler);
                    position += static_cast<uint32_t>(gap);
                }
                PutBigEndian(cursor, position, 4);
                PutBigEndian(cursor + 4, CountedBytes(entry), 4);
                const uint32_t nameBytes = static_cast<uint32_t>(std::strlen(entry.name)) + 1;
                std::memcpy(cursor + 8, entry.name, nameBytes);
                cursor += 8 + nameBytes;
            }
        } else if (layout == kLayoutChunks && writeHeaders != 0) {
            uint8_t chunk[kChunkHeaderBytes];
            PutBigEndian(chunk, fourcc, 4);
            const uint32_t chunkBytes = CountedBytes(entry) + kChunkHeaderBytes;
            std::memcpy(chunk + 4, &chunkBytes, 4);
            status = static_cast<uint32_t>(StreamWrite(stream, chunk, kChunkHeaderBytes));
        }
        // The blocks, in the chain's order; the first that is short ends this entry.
        struct Block {
            const uint8_t *bytes;
            uint32_t count;
        };
        const Block blocks[] = {{entry.shape, entry.shapeBytes}, {entry.palette, entry.paletteBytes}, {entry.marker, entry.markerBytes},
                                {entry.userData, entry.userDataBytes}, {entry.pairs, entry.pairsBytes}, {entry.text, entry.textBytes}};
        for (const Block &block : blocks) {
            if (status == 0) break;
            if (block.bytes != nullptr) status = StreamWrite(stream, block.bytes, static_cast<int32_t>(block.count)) == static_cast<int32_t>(block.count);
        }
        alignBase = align;
    }

    if (status != 0 && tabled) {
        StreamSeek(stream, 0, 0);
        PutBigEndian(header, fourcc, 4);
        const uint32_t length = static_cast<uint32_t>(StreamLength(stream));
        std::memcpy(header + 4, &length, 4);
        PutBigEndian(header + 8, static_cast<uint32_t>(directory->count), 4);
        PutBigEndian(header + 12, tableBytes + kHeaderTableExtra, 4);
        StreamWrite(stream, header, kHeaderBytes);
        status = StreamWrite(stream, table, static_cast<int32_t>(tableBytes)) == static_cast<int32_t>(tableBytes);
    }
    if (table != nullptr) FreeMemory(table);
    return status;
}

} // namespace fsh

} // namespace gimex
