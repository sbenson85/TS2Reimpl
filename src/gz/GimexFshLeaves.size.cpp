// The leaves of FSH's back end (lane MQ; src/gz/GimexFshLeaves.h, 0x88374d..0x885849): page swizzles, words by
// platform, colour converters, tiled addressing and the record-type pickers. Pure helpers over buffers; the swizzles
// read their permutation tables from the image (32 and 256 entries, GameAddress).
#include "gz/GimexFshLeaves.h"

#include "game/game.h"

#include <cstring>

namespace gimex {

namespace fsh {

namespace {

// ---- swizzle tables (the image's, int32 each) ----
constexpr uint32_t kTableNibblesUnswizzle = 0x105cdc0;  // 256 entries: nibble index per output byte pair
constexpr uint32_t kTableNibblesSwizzle = 0x105c3c0;
constexpr uint32_t kTableBytesUnswizzle = 0x105c8c0;    // 256 entries
constexpr uint32_t kTableBytesSwizzle = 0x105c0c0;      // 128 entries, two halves alternating
constexpr uint32_t kTablePositions4Unswizzle = 0x105d240;  // the inverse of the block order: block -> (column, row)
constexpr uint32_t kTableOrder4Unswizzle = 0x105d1c0;
constexpr uint32_t kTablePositions8Unswizzle = 0x105cd40;
constexpr uint32_t kTableOrder8Unswizzle = 0x105ccc0;
constexpr uint32_t kTablePositions4Swizzle = 0x105c840;
constexpr uint32_t kTableOrder4Swizzle = 0x105c7c0;
constexpr uint32_t kTablePositions8Swizzle = 0x105c340;
constexpr uint32_t kTableOrder8Swizzle = 0x105c2c0;

const int32_t *Table(uint32_t rva)
{
    return GameAddress<const int32_t>(rva);
}

// Pixel (x, y) of a swizzled 16-bit image.
constexpr uint32_t kSwizzle16MinWidth = 0xf;     // narrower images are plain rows
constexpr uint32_t kSwizzle16PageMask = 0xffffffc0;
constexpr uint32_t kSwizzle16RowBits = 0x38;
constexpr uint32_t kSwizzle16ColumnBits = 7;
constexpr uint32_t kSwizzle16Bit3 = 8;
constexpr uint32_t kSwizzle16Bit3Scale = 2;      // bit 3 of x goes to bit 4
constexpr uint32_t kSwizzle16HighMask = 0x18;
constexpr uint32_t kSwizzle16RowShift = 6;
constexpr uint32_t kSwizzle16RowScale = 0x20;
constexpr uint32_t kSwizzle16BitsPerStep = 0x20;

// The permutations of one 0x100-byte block.
constexpr uint32_t kBlockGroups = 4;
constexpr uint32_t kBlockGroupBytes = 0x40;
constexpr uint32_t kBlockRows = 16;
constexpr uint32_t kBlockRowBytes = 4;
constexpr uint32_t kNibbleHalfTable = 128;       // entries per group, two groups alternating
constexpr uint32_t kByteHalfTable = 64;
constexpr uint32_t kBytesInBlock = 0x100;
constexpr uint32_t kNibbleLow = 0xf;

int32_t PermuteNibbles(const uint8_t *in, uint8_t *out, const int32_t *table)
{
    uint32_t outIndex = 0;
    for (uint32_t group = 0; group < kBlockGroups; ++group) {
        uint32_t entry = (group & 1) * kNibbleHalfTable;
        for (uint32_t row = 0; row < kBlockRows; ++row) {
            for (uint32_t column = 0; column < kBlockRowBytes; ++column) {
                const uint32_t lowIndex = static_cast<uint32_t>(table[entry]);
                const uint32_t highIndex = static_cast<uint32_t>(table[entry + 1]);
                entry += 2;
                const uint32_t lowShift = (lowIndex & 1) << 2;
                const uint32_t highShift = (highIndex & 1) << 2;
                const uint32_t low = (static_cast<uint32_t>(in[lowIndex >> 1]) & (kNibbleLow << lowShift)) >> lowShift;
                const uint32_t high =
                    (static_cast<uint32_t>(in[highIndex >> 1]) & (kNibbleLow << highShift)) >> highShift;
                out[outIndex++] = static_cast<uint8_t>(high << 4 | low);
            }
        }
        in += kBlockGroupBytes;
    }
    return 0;
}

// One page: pixels move between a linear image and 32 blocks. UnswizzlePage* gather a block's 8 rows, permute them and
// write 16 linear rows; SwizzlePage* do the opposite.
constexpr int32_t kPositionRows = 4;
constexpr int32_t kPositionColumns = 8;
constexpr uint32_t kPositionBytes = 0x80;        // 32 int32 entries
constexpr uint32_t kBlockSourceRows = 8;
constexpr uint32_t kBlockSourceRowBytes = 0x20;
constexpr uint32_t kBlockSourceStride = 0x100;
constexpr uint32_t kBlockRowStrideRows = 0x40;
constexpr uint32_t kBlockColumnBytes = 0x20;
constexpr uint32_t kBlockRowStep = 0x40;         // blocks of a row: (row * 0x40 + column) * 0x20 bytes
constexpr uint32_t kLinearRows = 16;
constexpr uint32_t kLinearRowBytes = 16;
constexpr uint32_t kBufferBytes = 0x100;

struct PageShape {
    uint32_t acrossShift;        // blocks across = width >> shift (32 or 16 pixels per block)
    uint32_t linearStride;       // bytes between the linear rows of a block
    uint32_t linearRowAdvance;   // bytes between the block rows in the linear image
    uint32_t positions;          // table: block -> column, row
    uint32_t order;              // table: block order
};

using Permute = int32_t(__cdecl *)(const uint8_t *, uint8_t *);

void BlockPositions(const PageShape &shape, int32_t *columns, int32_t *rows)
{
    const int32_t *positions = Table(shape.positions);
    uint32_t index = 0;
    for (int32_t row = 0; row < kPositionRows; ++row) {
        for (int32_t column = 0; column < kPositionColumns; ++column) {
            const int32_t block = positions[index++];
            columns[block] = column;
            rows[block] = row;
        }
    }
}

int32_t BlocksOf(int32_t extent, uint32_t shift)
{
    const uint32_t mask = (1u << shift) - 1;
    return static_cast<int32_t>((static_cast<uint32_t>(extent >> 31) & mask) + static_cast<uint32_t>(extent)) >> shift;
}

int32_t UnswizzlePage(const PageShape &shape, Permute permute, int32_t width, int32_t height, const uint8_t *source,
                      uint8_t *dest)
{
    int32_t *const columns = static_cast<int32_t *>(Allocate(kPositionBytes));
    int32_t *const rows = static_cast<int32_t *>(Allocate(kPositionBytes));
    BlockPositions(shape, columns, rows);
    const int32_t across = BlocksOf(width, shape.acrossShift);
    const int32_t down = BlocksOf(height, 4);
    uint8_t gathered[kBufferBytes] = {};
    uint8_t permuted[kBufferBytes] = {};
    const int32_t *order = Table(shape.order);
    for (int32_t blockRow = 0; blockRow < down; ++blockRow) {
        const int32_t *entry = order;
        uint8_t *linear = dest;
        for (int32_t block = 0; block < across; ++block) {
            const int32_t index = *entry;
            const uint8_t *from =
                source + (static_cast<uint32_t>(rows[index]) * kBlockRowStep + static_cast<uint32_t>(columns[index])) *
                             kBlockColumnBytes;
            for (uint32_t row = 0; row < kBlockSourceRows; ++row) {
                std::memcpy(gathered + row * kBlockSourceRowBytes, from, kBlockSourceRowBytes);
                from += kBlockSourceStride;
            }
            permute(gathered, permuted);
            uint8_t *to = linear;
            for (uint32_t row = 0; row < kLinearRows; ++row) {
                std::memcpy(to, permuted + row * kLinearRowBytes, kLinearRowBytes);
                to += shape.linearStride;
            }
            linear += kLinearRowBytes;
            ++entry;
        }
        dest += shape.linearRowAdvance;
        order += across;
    }
    FreeMemory(columns);
    FreeMemory(rows);
    return 0;
}

int32_t SwizzlePage(const PageShape &shape, Permute permute, int32_t width, int32_t height, const uint8_t *source,
                    uint8_t *dest)
{
    int32_t *const columns = static_cast<int32_t *>(Allocate(kPositionBytes));
    int32_t *const rows = static_cast<int32_t *>(Allocate(kPositionBytes));
    BlockPositions(shape, columns, rows);
    const int32_t across = BlocksOf(width, shape.acrossShift);
    const int32_t down = BlocksOf(height, 4);
    uint8_t gathered[kBufferBytes] = {};
    uint8_t permuted[kBufferBytes] = {};
    const int32_t *order = Table(shape.order);
    for (int32_t blockRow = 0; blockRow < down; ++blockRow) {
        const int32_t *entry = order;
        const uint8_t *linear = source;
        for (int32_t block = 0; block < across; ++block) {
            const int32_t index = *entry;
            const uint8_t *from = linear;
            for (uint32_t row = 0; row < kLinearRows; ++row) {
                std::memcpy(gathered + row * kLinearRowBytes, from, kLinearRowBytes);
                from += shape.linearStride;
            }
            permute(gathered, permuted);
            uint8_t *to =
                dest + (static_cast<uint32_t>(rows[index]) * kBlockRowStep + static_cast<uint32_t>(columns[index])) *
                           kBlockColumnBytes;
            for (uint32_t row = 0; row < kBlockSourceRows; ++row) {
                std::memcpy(to, permuted + row * kBlockSourceRowBytes, kBlockSourceRowBytes);
                to += kBlockSourceStride;
            }
            linear += kLinearRowBytes;
            ++entry;
        }
        source += shape.linearRowAdvance;
        order += across;
    }
    FreeMemory(columns);
    FreeMemory(rows);
    return 0;
}

constexpr PageShape kShape4Unswizzle = {5, 0x40, 0x400, kTablePositions4Unswizzle, kTableOrder4Unswizzle};
constexpr PageShape kShape8Unswizzle = {4, 0x80, 0x800, kTablePositions8Unswizzle, kTableOrder8Unswizzle};
constexpr PageShape kShape4Swizzle = {5, 0x40, 0x400, kTablePositions4Swizzle, kTableOrder4Swizzle};
constexpr PageShape kShape8Swizzle = {4, 0x80, 0x800, kTablePositions8Swizzle, kTableOrder8Swizzle};

// ---- whole images in pages ----
constexpr int32_t kPageWidth = 0x80;             // pixels per page row
constexpr int32_t kPageHeight4 = 0x80;
constexpr int32_t kPageHeight8 = 0x40;
constexpr int32_t kPageShift = 7;
constexpr int32_t kPageMask = 0x7f;
constexpr int32_t kHalfPageShift = 6;
constexpr int32_t kHalfPageMask = 0x3f;
constexpr int32_t kPageBytes4 = 0x40;            // bytes of a 4-bit page row
constexpr int32_t kPageColumns4 = 0x20;
constexpr int32_t kPageRowBytes = 0x100;         // bytes of a swizzled page row
constexpr int32_t kPageRows8 = 0x20;
constexpr uint32_t kPageBufferBytes = 0x2000;
constexpr int32_t kRowBytes8 = 0x80;

int32_t PagesOf(int32_t extent)
{
    const int32_t last = extent - 1;
    return ((last + (static_cast<int32_t>(static_cast<uint32_t>(last >> 31) & kPageMask))) >> kPageShift) + 1;
}

int32_t HalfPagesOf(int32_t extent)
{
    const int32_t last = extent - 1;
    return ((last + (static_cast<int32_t>(static_cast<uint32_t>(last >> 31) & kHalfPageMask))) >> kHalfPageShift) + 1;
}

int32_t QuarterOf(int32_t value)
{
    return (value + static_cast<int32_t>(static_cast<uint32_t>(value >> 31) & 3)) >> 2;
}

} // namespace

// 0x88374d
int32_t __cdecl Swizzle16Offset(uint32_t x, uint32_t y, int32_t width)
{
    int32_t extra = 0;
    if (width > static_cast<int32_t>(kSwizzle16MinWidth)) {
        const uint32_t half = static_cast<uint32_t>(static_cast<int32_t>(x) >> 1);
        extra = static_cast<int32_t>((x & kSwizzle16Bit3) * kSwizzle16Bit3Scale);
        x = (x & kSwizzle16PageMask) + (y & kSwizzle16RowBits) + (x & kSwizzle16ColumnBits);
        y = static_cast<uint32_t>(static_cast<int32_t>(y) >> kSwizzle16RowShift) * kSwizzle16RowScale +
            (half & kSwizzle16HighMask) + (y & kSwizzle16ColumnBits);
    }
    return static_cast<int32_t>((x + y * static_cast<uint32_t>(width)) * kSwizzle16BitsPerStep) + extra;
}

// 0x8837a5
int32_t __cdecl UnswizzleNibbles(const uint8_t *in, uint8_t *out)
{
    return PermuteNibbles(in, out, Table(kTableNibblesUnswizzle));
}

// 0x883bf0
int32_t __cdecl SwizzleNibbles(const uint8_t *in, uint8_t *out)
{
    return PermuteNibbles(in, out, Table(kTableNibblesSwizzle));
}

// 0x883851
int32_t __cdecl UnswizzleBytes(const uint8_t *in, uint8_t *out)
{
    const int32_t *const table = Table(kTableBytesUnswizzle);
    for (uint32_t index = 0; index < kBytesInBlock; ++index) out[index] = in[table[index]];
    return 0;
}

// 0x883c9c
int32_t __cdecl SwizzleBytes(const uint8_t *in, uint8_t *out)
{
    const int32_t *const table = Table(kTableBytesSwizzle);
    uint32_t outIndex = 0;
    for (uint32_t group = 0; group < kBlockGroups; ++group) {
        uint32_t entry = (group & 1) * kByteHalfTable;
        for (uint32_t byte = 0; byte < kBlockRows * kBlockRowBytes; ++byte) out[outIndex++] = in[table[entry++]];
        in += kBlockGroupBytes;
    }
    return 0;
}

// 0x883876
int32_t __cdecl UnswizzlePage4(int32_t width, int32_t height, const void *source, void *dest)
{
    return UnswizzlePage(kShape4Unswizzle, &UnswizzleNibbles, width, height, static_cast<const uint8_t *>(source),
                         static_cast<uint8_t *>(dest));
}

// 0x883a33
int32_t __cdecl UnswizzlePage8(int32_t width, int32_t height, const void *source, void *dest)
{
    return UnswizzlePage(kShape8Unswizzle, &UnswizzleBytes, width, height, static_cast<const uint8_t *>(source),
                         static_cast<uint8_t *>(dest));
}

// 0x883cf7
int32_t __cdecl SwizzlePage4(int32_t width, int32_t height, const void *source, void *dest)
{
    return SwizzlePage(kShape4Swizzle, &SwizzleNibbles, width, height, static_cast<const uint8_t *>(source),
                       static_cast<uint8_t *>(dest));
}

// 0x883eb9
int32_t __cdecl SwizzlePage8(int32_t width, int32_t height, const void *source, void *dest)
{
    return SwizzlePage(kShape8Swizzle, &SwizzleBytes, width, height, static_cast<const uint8_t *>(source),
                       static_cast<uint8_t *>(dest));
}

// 0x884b24
int32_t __cdecl Swizzle16(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height)
{
    int32_t linear = 0;
    for (int32_t y = 0; y < height; ++y) {
        const uint16_t *pixel = reinterpret_cast<const uint16_t *>(source + linear * 2);
        for (int32_t x = 0; x < width; ++x) {
            const int32_t offset = Swizzle16Offset(static_cast<uint32_t>(x), static_cast<uint32_t>(y), width);
            *reinterpret_cast<uint16_t *>(dest + (offset >> 3)) = *pixel++;
        }
        linear += width;
    }
    return 1;
}

// 0x884fa8
int32_t __cdecl Unswizzle16(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height)
{
    int32_t linear = 0;
    for (int32_t y = 0; y < height; ++y) {
        uint16_t *pixel = reinterpret_cast<uint16_t *>(dest + linear * 2);
        for (int32_t x = 0; x < width; ++x) {
            const int32_t offset = Swizzle16Offset(static_cast<uint32_t>(x), static_cast<uint32_t>(y), width);
            *pixel++ = *reinterpret_cast<const uint16_t *>(source + (offset >> 3));
        }
        linear += width;
    }
    return 1;
}

// 0x884b8b
int32_t __cdecl Swizzle4(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height)
{
    uint8_t linearPage[kPageBufferBytes];
    uint8_t swizzledPage[kPageBufferBytes];
    std::memset(linearPage, 0, sizeof(linearPage));
    std::memset(swizzledPage, 0, sizeof(swizzledPage));
    const int32_t pagesAcross = PagesOf(width);
    const int32_t pagesDown = PagesOf(height);
    int32_t rowBytes;
    int32_t outRows;
    if (pagesAcross == 1) {
        rowBytes = width / 2;
        outRows = QuarterOf(width);
    } else {
        rowBytes = kPageBytes4;
        outRows = kPageColumns4;
    }
    int32_t outBytes = height * 2;
    if (pagesDown != 1) {
        outBytes = kPageRowBytes;
        height = kPageHeight4;
    }
    for (int32_t pageRow = 0; pageRow < pagesDown; ++pageRow) {
        if (pagesAcross > 0) {
            uint8_t *dropTo = dest;
            const uint8_t *pickFrom = source;
            for (int32_t page = 0; page < pagesAcross; ++page) {
                uint8_t *row = linearPage;
                const uint8_t *from = pickFrom;
                for (int32_t line = 0; line < height; ++line) {
                    std::memcpy(row, from, static_cast<size_t>(rowBytes));
                    from += rowBytes * pagesAcross;
                    row += kPageBytes4;
                }
                SwizzlePage4(kPageWidth, kPageHeight4, linearPage, swizzledPage);
                const uint8_t *page16 = swizzledPage;
                uint8_t *to = dropTo;
                for (int32_t line = 0; line < outRows; ++line) {
                    std::memcpy(to, page16, static_cast<size_t>(outBytes));
                    to += outBytes * pagesAcross;
                    page16 += kPageRowBytes;
                }
                dropTo += outBytes;
                pickFrom += rowBytes;
            }
        }
        dest += outBytes * outRows * pagesAcross;
        source += height * rowBytes * pagesAcross;
    }
    return 0;
}

// 0x884d93
int32_t __cdecl Swizzle8(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height)
{
    uint8_t linearPage[kPageBufferBytes];
    uint8_t swizzledPage[kPageBufferBytes];
    std::memset(linearPage, 0, sizeof(linearPage));
    std::memset(swizzledPage, 0, sizeof(swizzledPage));
    int32_t outBytes = width * 2;
    const int32_t pagesAcross = PagesOf(width);
    const int32_t pagesDown = HalfPagesOf(height);
    if (pagesAcross != 1) {
        outBytes = kPageRowBytes;
        width = kPageWidth;
    }
    const int32_t outRows = pagesDown == 1 ? height / 2 : kPageRows8;
    if (pagesDown != 1) height = kPageHeight8;
    for (int32_t pageRow = 0; pageRow < pagesDown; ++pageRow) {
        if (pagesAcross > 0) {
            uint8_t *dropTo = dest;
            const uint8_t *pickFrom = source + height * width * pageRow * pagesAcross;
            for (int32_t page = 0; page < pagesAcross; ++page) {
                uint8_t *row = linearPage;
                const uint8_t *from = pickFrom;
                for (int32_t line = 0; line < height; ++line) {
                    std::memcpy(row, from, static_cast<size_t>(width));
                    from += width * pagesAcross;
                    row += kRowBytes8;
                }
                SwizzlePage8(kPageWidth, kPageHeight8, linearPage, swizzledPage);
                const uint8_t *page16 = swizzledPage;
                uint8_t *to = dropTo;
                for (int32_t line = 0; line < outRows; ++line) {
                    std::memcpy(to, page16, static_cast<size_t>(outBytes));
                    to += outBytes * pagesAcross;
                    page16 += kPageRowBytes;
                }
                dropTo += outBytes;
                pickFrom += width;
            }
        }
        dest += outRows * outBytes * pagesAcross;
    }
    return 0;
}

// 0x88500f
int32_t __cdecl Unswizzle4(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height)
{
    uint8_t swizzledPage[kPageBufferBytes];
    uint8_t linearPage[kPageBufferBytes];
    std::memset(swizzledPage, 0, sizeof(swizzledPage));
    std::memset(linearPage, 0, sizeof(linearPage));
    int32_t inBytes = height * 2;
    const int32_t pagesAcross = PagesOf(width);
    const int32_t pagesDown = PagesOf(height);
    if (pagesDown != 1) {
        inBytes = kPageRowBytes;
        height = kPageHeight4;
    }
    int32_t inRows;
    if (pagesAcross == 1) {
        inRows = QuarterOf(width);
        width = width / 2;
    } else {
        inRows = kPageColumns4;
        width = kPageBytes4;
    }
    for (int32_t pageRow = 0; pageRow < pagesDown; ++pageRow) {
        if (pagesAcross > 0) {
            uint8_t *dropTo = dest;
            const uint8_t *pickFrom = source;
            for (int32_t page = 0; page < pagesAcross; ++page) {
                uint8_t *row = swizzledPage;
                const uint8_t *from = pickFrom;
                if (inRows > 0) {
                    for (int32_t line = 0; line < inRows; ++line) {
                        std::memcpy(row, from, static_cast<size_t>(inBytes));
                        from += inBytes * pagesAcross;
                        row += kPageRowBytes;
                    }
                }
                UnswizzlePage4(kPageWidth, kPageHeight4, swizzledPage, linearPage);
                const uint8_t *linearRow = linearPage;
                uint8_t *to = dropTo;
                for (int32_t line = 0; line < height; ++line) {
                    std::memcpy(to, linearRow, static_cast<size_t>(width));
                    to += width * pagesAcross;
                    linearRow += kPageBytes4;
                }
                dropTo += width;
                pickFrom += inBytes;
            }
        }
        dest += height * width * pagesAcross;
        source += inRows * inBytes * pagesAcross;
    }
    return 0;
}

// 0x88521a
int32_t __cdecl Unswizzle8(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height)
{
    uint8_t swizzledPage[kPageBufferBytes];
    uint8_t linearPage[kPageBufferBytes];
    std::memset(swizzledPage, 0, sizeof(swizzledPage));
    std::memset(linearPage, 0, sizeof(linearPage));
    int32_t inBytes = width * 2;
    const int32_t pagesAcross = PagesOf(width);
    const int32_t pagesDown = HalfPagesOf(height);
    if (pagesAcross != 1) {
        inBytes = kPageRowBytes;
        width = kPageWidth;
    }
    const int32_t inRows = pagesDown == 1 ? height / 2 : kPageRows8;
    if (pagesDown != 1) height = kPageHeight8;
    for (int32_t pageRow = 0; pageRow < pagesDown; ++pageRow) {
        if (pagesAcross > 0) {
            uint8_t *dropTo = dest + height * width * pageRow * pagesAcross;
            const uint8_t *pickFrom = source;
            for (int32_t page = 0; page < pagesAcross; ++page) {
                uint8_t *row = swizzledPage;
                const uint8_t *from = pickFrom;
                if (inRows > 0) {
                    for (int32_t line = 0; line < inRows; ++line) {
                        std::memcpy(row, from, static_cast<size_t>(inBytes));
                        from += inBytes * pagesAcross;
                        row += kPageRowBytes;
                    }
                }
                UnswizzlePage8(kPageWidth, kPageHeight8, swizzledPage, linearPage);
                const uint8_t *linearRow = linearPage;
                uint8_t *to = dropTo;
                for (int32_t line = 0; line < height; ++line) {
                    std::memcpy(to, linearRow, static_cast<size_t>(width));
                    to += width * pagesAcross;
                    linearRow += kRowBytes8;
                }
                dropTo += width;
                pickFrom += inBytes;
            }
        }
        source += inRows * inBytes * pagesAcross;
    }
    return 0;
}

} // namespace fsh

} // namespace gimex
