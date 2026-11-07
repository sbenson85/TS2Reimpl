// The FSH back end's directory reader (lane MX, block 1's decode side; src/gz/GimexFshDecode.h): one record copied out of the
// file, an entry parsed from its chain of records, the directory builder that gives each mipmap level an entry of its own, and the
// reader that finds the entries in the stream. Size-built in the original.
#include "gz/GimexFshDecode.h"

#include "game/game.h"
#include "gz/GimexFshBlocks.h"
#include "gz/GimexFshLeaves.h"

#include <cstdio>
#include <cstring>

namespace gimex {
namespace fsh {

namespace {

constexpr uint32_t kRecordHeader = 0x20;
constexpr uint32_t kPaletteTail = 0x400;     // the room a coded record is given beyond its stored size
constexpr uint32_t kFlagNoPalette = 1;       // the entry's flags: no palette came with the shape
constexpr uint32_t kFlagFilledShape = 2;     // ... the shape was made up (a palette-only entry)
constexpr uint32_t kFlagPaletteBlocks = 0x100000;
constexpr uint32_t kEntryDwords = 0x13;

inline size_t Length(const char *text) { return std::strlen(text); }

} // namespace

uint8_t *__cdecl ExtractRecord(const uint8_t *record, const uint8_t *end, int32_t *bytes, int32_t platform)
{
    uint8_t *copy = nullptr;
    const int32_t codec = RecordCodec(record, platform);
    int32_t size = RecordSize(record, platform);
    if (codec != 0) {
        if (codec == 2) {
            size += static_cast<int32_t>(kPaletteTail);
        } else {
            size = (size * 3) / 2 + static_cast<int32_t>(kPaletteTail);
            const int32_t available = static_cast<int32_t>(end - record);
            if (available < size) size = available;
        }
    }
    *bytes = size;
    if (size > 0) {
        copy = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(size)));
        if (copy != nullptr) {
            if (codec == 2) {
                std::memcpy(copy, record, kRecordHeader);
                DecodeRefPack(copy + kRecordHeader, record + kRecordHeader, nullptr);
            } else {
                std::memcpy(copy, record, static_cast<size_t>(size));
            }
        }
    }
    return copy;
}

int32_t __cdecl ParseEntry(const uint8_t *start, const uint8_t *base, const uint8_t *end, int32_t fileBytes,
                           const uint8_t *defaultPalette, Entry *entry, int32_t platform)
{
    int32_t bytes = 0;
    uint32_t seen = 0; // 1 shape, 2 text, 4 palette, 8 pairs, 0x10 extra, 0x20 marker
    entry->shape = nullptr;
    entry->palette = nullptr;
    entry->paletteBytes = 0;
    entry->pairs = nullptr;
    entry->text = nullptr;
    entry->marker = nullptr;
    entry->userData = nullptr;
    entry->name = nullptr;
    entry->flags = 0;
    entry->kind = 0;
    entry->imageBytes = 0;
    const uint8_t *current = start;
    for (;;) {
        uint8_t *copy = ExtractRecord(current, end, &bytes, platform);
        if (copy != nullptr) {
            entry->imageBytes += static_cast<uint32_t>(bytes);
            bool keep = false;
            switch (ClassifyRecord(copy, platform)) {
            case kRecordShape:
                if ((seen & 1) == 0) {
                    const int32_t codec = RecordCodec(current, platform);
                    entry->kind = 0;
                    entry->shape = copy;
                    if (codec != 0) {
                        entry->kind = (codec == 4 || codec == 3 || codec == 2) ? static_cast<uint32_t>(codec) : 0;
                    } else {
                        const uint8_t type = *current;
                        if (type == 0x60 || type == 0x61 || type == 0x62 || type == 0x1e) entry->kind = 3;
                    }
                    seen |= 1;
                    keep = true;
                }
                break;
            case kRecordPalette:
                if ((seen & 4) == 0) {
                    entry->palette = copy;
                    entry->paletteBytes = static_cast<uint32_t>(bytes);
                    seen |= 4;
                    keep = true;
                }
                break;
            case kRecordPairs:
                if ((seen & 8) == 0) {
                    entry->pairs = copy;
                    seen |= 8;
                    keep = true;
                }
                break;
            case kRecordText:
                if ((seen & 2) == 0) {
                    entry->text = copy;
                    seen |= 2;
                    keep = true;
                }
                break;
            case kRecordMarker:
                if ((seen & 0x20) == 0) {
                    entry->marker = copy;
                    seen |= 0x20;
                    keep = true;
                }
                break;
            case kRecordExtra:
                if ((seen & 0x10) == 0) {
                    entry->userData = copy;
                    seen |= 0x10;
                    keep = true;
                }
                break;
            default:
                break;
            }
            if (!keep) FreeMemory(copy);
        }
        const uint32_t length = RecordLength(current, platform);
        const uint8_t *next = current + length;
        if (length == 0 || next <= base) break;
        const uint8_t *fileEnd = base + fileBytes;
        if (next >= fileEnd) break;
        if (ClassifyRecord(current, platform) == 0 || platform == 3) break;
        if (ClassifyRecord(next, platform) == 0) break;
        const uint32_t nextLength = RecordLength(next, platform);
        const uint8_t *after = next + nextLength;
        if (nextLength != 0 && (after <= base || after >= fileEnd)) break;
        current = next;
    }

    uint8_t *shape = entry->shape;
    if (shape == nullptr && entry->palette == nullptr && entry->pairs == nullptr && entry->marker == nullptr && entry->userData == nullptr &&
        entry->text == nullptr) {
        return 0;
    }
    uint8_t *palette = entry->palette;
    if (palette == nullptr) {
        if (shape != nullptr && *shape != 0x64) entry->flags |= kFlagNoPalette;
        if (defaultPalette != nullptr) {
            palette = ExtractRecord(defaultPalette, end, &bytes, platform);
            entry->palette = palette;
            entry->paletteBytes = static_cast<uint32_t>(bytes);
        }
    }
    shape = entry->shape;
    if (shape == nullptr) {
        int32_t entries = 0x100;
        int32_t unused;
        if (palette != nullptr) entries = PaletteEntries(palette, &unused, 8, platform);
        entry->flags |= kFlagFilledShape;
        shape = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(entries + 0x10)));
        int32_t type = -1;
        switch (platform) {
        case 1: type = 0x41; break;
        case 2:
        case 3:
        case 8:
        case 0xc:
        case 0x12: type = 0x7b; break;
        case 5:
        case 0xf: type = 0x19; break;
        case 6:
        case 9:
        case 0x10: type = 2; break;
        case 7:
        case 0x11: type = 0x59; break;
        default: break;
        }
        if (type >= 0) PutWord(shape, static_cast<uint32_t>(type), 1, platform);
        PutWord(shape + 0x18, static_cast<uint32_t>(entries), 4, platform);
        PutWord(shape + 0x1c, 1, 4, platform);
        PutWord(shape + 0x10, 0, 4, platform);
        PutWord(shape + 0x14, 0, 4, platform);
        for (int32_t i = 0; i < entries; ++i) shape[0x10 + i] = static_cast<uint8_t>(i);
        entry->shape = shape;
    }
    const int8_t first = static_cast<int8_t>(*shape);
    if ((first < 0x60 || first > 0x62) && first != 0x1e && entry->kind == 3) entry->flags |= kFlagPaletteBlocks;
    return 1;
}

Directory *__cdecl BuildDirectory(Directory *directory, int32_t platform, uint32_t alignMask)
{
    char *nameCopy = nullptr;
    bool coded = false;  // a block-coded shape was seen (its header is kept for the levels)
    bool eights = false; // a shape of the 8-byte block types was seen
    uint32_t savedHeader[2] = {0, 0};
    int32_t total = 0;
    for (int32_t i = 0; i < directory->count; ++i) total += MipLevels(directory->entries[i].shape, 0, platform);
    if (total == directory->count) return directory;
    Directory *result = static_cast<Directory *>(Allocate(static_cast<uint32_t>(total * static_cast<int32_t>(sizeof(Entry)) + 4)));
    if (result == nullptr) return directory;
    result->count = total;
    int32_t outIndex = 0;
    int32_t bits = 0;
    for (int32_t i = 0; i < directory->count; ++i) {
        const Entry &source = directory->entries[i];
        uint8_t *record = source.shape;
        const uint8_t *data = record + kRecordHeader;
        int32_t width = static_cast<int32_t>(ReadWord(record + 0x18, 4, platform));
        int32_t height = static_cast<int32_t>(ReadWord(record + 0x1c, 4, platform));
        bits = ShapeBits(record, platform);
        const uint8_t type = *record;
        if (type == 0x1e || type == 0x60) {
            eights = true;
            bits = 4;
        } else if (type == 0x61 || type == 0x62) {
            bits = 8;
            eights = true;
        }
        const int32_t levels = MipLevels(record, 0, platform);
        for (int32_t level = 0; level < levels; ++level) {
            Entry &out = result->entries[outIndex];
            std::memcpy(&out, &source, sizeof(Entry));
            if (level == 0) {
                if (HasCodec(record, 3, platform) != 0) {
                    bits = (HasHeader1afb(data) != 0 || HasHeader1cfb(data) != 0) ? 8 : 4;
                    savedHeader[0] = reinterpret_cast<const uint32_t *>(data)[0];
                    savedHeader[1] = reinterpret_cast<const uint32_t *>(data)[1];
                    coded = true;
                }
                if (nameCopy != nullptr) FreeMemory(nameCopy);
                nameCopy = static_cast<char *>(Allocate(static_cast<uint32_t>(Length(source.name) + 1)));
                std::memcpy(nameCopy, source.name, Length(source.name) + 1);
            } else {
                out.name = static_cast<char *>(Allocate(static_cast<uint32_t>(Length(nameCopy) + 6)));
                std::sprintf(out.name, GameAddress<char>(0xe4a6b8), nameCopy, level);
                if (source.palette != nullptr && source.paletteBytes != 0) {
                    out.palette = static_cast<uint8_t *>(Allocate(source.paletteBytes));
                    std::memcpy(out.palette, source.palette, source.paletteBytes);
                }
                out.pairs = nullptr;
                out.pairsBytes = 0;
                out.text = nullptr;
                out.textBytes = 0;
                out.marker = nullptr;
                out.markerBytes = 0;
                out.userData = nullptr;
            }
            int32_t levelWidth = width;
            int32_t levelHeight = height;
            if (coded || eights) {
                if (levelWidth < 4) levelWidth = 4;
                if (levelHeight < 4) levelHeight = 4;
            }
            int32_t imageBytes;
            if (platform == 5 || platform == 6) {
                if (IsSwizzled(record, platform) && !coded && !eights) {
                    uint32_t paddedWidth;
                    uint32_t paddedHeight;
                    if (platform == 5) {
                        PaddedSize(&paddedWidth, &paddedHeight, levelWidth, levelHeight, bits);
                    } else {
                        PageSize(&paddedWidth, &paddedHeight, static_cast<uint32_t>(levelWidth), static_cast<uint32_t>(levelHeight), bits);
                    }
                    imageBytes = ImageBytes(static_cast<int32_t>(paddedWidth), static_cast<int32_t>(paddedHeight), bits, platform);
                } else if (platform == 5 && (coded || eights)) {
                    imageBytes = ImageBytes((levelWidth + 7) & ~7, (levelHeight + 7) & ~7, bits, 5);
                } else {
                    imageBytes = ImageBytes(levelWidth, levelHeight, bits, platform);
                }
            } else {
                imageBytes = ImageBytes(levelWidth, levelHeight, bits, platform);
            }
            const uint32_t aligned = (static_cast<uint32_t>(imageBytes) + alignMask) & ~alignMask;
            const uint32_t shapeBytes = aligned + kRecordHeader;
            uint32_t allocBytes = shapeBytes;
            if (coded) allocBytes += level != 0 ? 8u : 0u;
            uint8_t *shape = static_cast<uint8_t *>(Allocate(allocBytes));
            out.shapeBytes = shapeBytes;
            out.shape = shape;
            std::memcpy(shape, record, kRecordHeader);
            PutWord(shape + 0x18, static_cast<uint32_t>(width), 4, platform);
            PutWord(shape + 0x1c, static_cast<uint32_t>(height), 4, platform);
            if (level == 0 || !coded) {
                std::memcpy(shape + kRecordHeader, data, aligned);
                if (level == 0 && coded) data += 8;
            } else {
                reinterpret_cast<uint32_t *>(shape + kRecordHeader)[0] = savedHeader[0];
                reinterpret_cast<uint32_t *>(shape + kRecordHeader)[1] = savedHeader[1];
                std::memcpy(shape + kRecordHeader + 8, data, aligned);
            }
            data += aligned;
            ++outIndex;
            width = width > 1 ? width >> 1 : width;
            height = height > 1 ? height >> 1 : height;
        }
        FreeMemory(source.shape);
    }
    if (nameCopy != nullptr) FreeMemory(nameCopy);
    FreeMemory(directory);
    return result;
}

int32_t __cdecl ReadDirectoryStream(ifaces::cIGZFile *stream, Directory **out, int32_t scanChunks, int32_t platform, uint32_t fourcc,
                                    uint32_t fontFourcc, uint32_t alignMask)
{
    Directory *directory = nullptr;
    bool chunks = false;
    uint8_t head[16] = {};
    StreamSeek(stream, 0, 0);
    if (StreamRead(stream, head, 0x10) == 0x10 && IsCode(head, static_cast<int32_t>(fontFourcc))) chunks = true;
    StreamSeek(stream, 0, 0);
    bool scan = chunks;
    if (!chunks) {
        if (scanChunks == 0) goto Finish;
        if (platform / 10 == 0) {
            // The older layout: a 16-byte head with the entry count, then offset / name pairs.
            uint8_t counts[16] = {};
            const int32_t got = StreamRead(stream, counts, 0x10);
            const int32_t count = static_cast<int32_t>(ReadBigEndian(counts + 8, 4));
            if (got == 0x10) {
                const int32_t length = static_cast<int32_t>(StreamLength(stream));
                const int32_t size = length - 0x10;
                uint8_t *buffer = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(size)));
                if (buffer != nullptr) {
                    if (StreamRead(stream, buffer, size) == size) {
                        directory = static_cast<Directory *>(Allocate(static_cast<uint32_t>(count * static_cast<int32_t>(sizeof(Entry)) + 4)));
                        if (directory != nullptr) {
                            directory->count = 0;
                            const uint8_t *cursor = buffer;
                            const uint8_t *base = buffer - 0x10;
                            for (int32_t remaining = count; remaining > 0; --remaining) {
                                const uint32_t offset = ReadBigEndian(cursor, 4);
                                Entry local{};
                                if (ParseEntry(base + offset, base, buffer + size, length, nullptr, &local, platform) != 0) {
                                    Entry &slot = directory->entries[directory->count];
                                    slot = local;
                                    const char *name = reinterpret_cast<const char *>(cursor + 8);
                                    slot.name = static_cast<char *>(Allocate(static_cast<uint32_t>(Length(name) + 1)));
                                    std::memcpy(slot.name, name, Length(name) + 1);
                                    cursor += 8 + Length(name) + 1;
                                    ++directory->count;
                                }
                            }
                        }
                    }
                    FreeMemory(buffer);
                }
            }
        }
        scan = static_cast<uint32_t>(platform - 10) < 10;
    }
    if (scan) {
        const uint32_t length = static_cast<uint32_t>(StreamLength(stream));
        uint8_t *buffer = static_cast<uint8_t *>(Allocate(length));
        if (buffer != nullptr) {
            const int32_t fileBytes = static_cast<int32_t>(StreamLength(stream));
            const uint8_t *end = buffer + fileBytes;
            if (StreamRead(stream, buffer, fileBytes) == fileBytes) {
                const int32_t limit = fileBytes - 8;
                int32_t position = 0;
                int32_t count = 0;
                const uint8_t *cursor = buffer;
                bool single = false;
                while (position < limit) {
                    if (IsCode(cursor, static_cast<int32_t>(fourcc)) || IsCode(cursor, static_cast<int32_t>(fontFourcc))) {
                        ++count;
                    } else if (position == 0 && count == 0 && IsKnownRecord(cursor, platform)) {
                        count = 1;
                        single = true;
                        break;
                    }
                    position += *reinterpret_cast<const int32_t *>(cursor + 4);
                    if (position < 0) break;
                    if (static_cast<int64_t>(position) > static_cast<int64_t>(StreamLength(stream)) - 8) break;
                    cursor = buffer + position;
                }
                (void)single;
                if (count != 0) {
                    directory = static_cast<Directory *>(Allocate(static_cast<uint32_t>(count * static_cast<int32_t>(sizeof(Entry)) + 4)));
                    if (directory != nullptr) {
                        int32_t index = 0;
                        position = 0;
                        cursor = buffer;
                        directory->count = count;
                        if (count > 0) {
                            Entry *slot = directory->entries;
                            for (;;) {
                                Entry local{};
                                bool parsed = false;
                                if (IsCode(cursor, static_cast<int32_t>(fourcc))) {
                                    parsed = ParseEntry(cursor + 8, buffer, end, fileBytes, nullptr, &local, platform) != 0;
                                } else if (IsCode(cursor, static_cast<int32_t>(fontFourcc))) {
                                    const uint32_t offset = ReadWord(cursor + 0x30, 4, platform);
                                    parsed = ParseEntry(cursor + offset, buffer, end, fileBytes, nullptr, &local, platform) != 0;
                                } else if (IsKnownRecord(cursor, platform)) {
                                    parsed = ParseEntry(cursor, buffer, end, fileBytes, nullptr, &local, platform) != 0;
                                }
                                if (parsed) *slot = local;
                                position += *reinterpret_cast<const int32_t *>(cursor + 4);
                                if (position < 0) break;
                                if (static_cast<int64_t>(position) > static_cast<int64_t>(StreamLength(stream)) - 8) break;
                                cursor = buffer + position;
                                ++index;
                                ++slot;
                                if (index >= count) break;
                            }
                        }
                    }
                }
            }
            FreeMemory(buffer);
        }
    }
Finish:
    const int32_t split = SplitFaces(directory, platform, alignMask);
    Directory *built = BuildDirectory(directory, platform, alignMask);
    *out = built;
    return built != nullptr ? 1 : split;
}

} // namespace fsh
} // namespace gimex
