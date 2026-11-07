#pragma once
// The decode side of the FSH back end's block 1 (lane MX, 0x88584d..0x8888e7; src/gz/GimexFshRecords.size.cpp, GimexFshRecords.cpp,
// GimexFshPixels.size.cpp, GimexFshDirectory.size.cpp). RVAs 1.18.0.187, all __cdecl. Names are tentative.
//
// A shape file is a directory of entries, each a chain of records: the shape itself, then its attachments (a palette, a block of pairs,
// a text, a marker). A record starts with a 0x20-byte header: the type byte (the top bit is a flag), a 3-byte attribute word at +1, the
// offset of the pixels at +8, the shape's width and height at +0x18 / +0x1c, and at +0x20 the pixel bytes (raw, or coded: the first
// bytes there say RefPack, block codes or GM). `ClassifyRecord` says which kind of record a type byte is. `platform` is the back end's
// byte order / layout selector (2 for every FSH call).
#include "gz/Gimex.h"

#include <cstdint>

namespace gimex {
namespace fsh {

// One shape of the directory (0x4c bytes), for the decode side (MX) and the encode side (MY, MZ) alike. Every block an entry owns was made
// by the codecs' allocator (gimex::Allocate) and is freed with gimex::FreeMemory. On the encode side each block's byte count is the word
// after its pointer (the name has none); the decode side fills them in only where a record was copied (shapeBytes, paletteBytes).
struct Entry {
    uint8_t *shape;         // 0x00 the shape record (header and pixels)
    uint32_t shapeBytes;    // 0x04 the record's size when the directory builder or the face splitter allocated it
    uint32_t tag;           // 0x08 the encoder: the four characters of the info's name field from byte 4 on, big-endian (0x21 when none)
    uint8_t *palette;       // 0x0c the attached palette record (header and entries), or null
    uint32_t paletteBytes;  // 0x10
    uint8_t *pairs;         // 0x14 the attached block of pairs (0x8866ae decodes it, 0x88bb19 builds it: type 0x7c)
    uint32_t pairsBytes;    // 0x18
    uint8_t *text;          // 0x1c a text record (0x886652 decodes it, 0x88b9e1 builds it: type 0x6f)
    uint32_t textBytes;     // 0x20
    uint8_t *marker;        // 0x24 the marker record (0x88675f: its presence sets info flag 0x200000; 0x88bc6b builds it: type 0x69)
    uint32_t markerBytes;   // 0x28
    uint8_t *userData;      // 0x2c the user-data record (0x886680 reads its word at +0xc; 0x88ba81 builds it: type 0x6b)
    uint32_t userDataBytes; // 0x30
    char *name;             // 0x34
    uint32_t flags;         // 0x38 the info's variant (bit 0x40000: the shape carries a cube map)
    uint32_t kind;          // 0x3c the info's jpegFlag word (the compression kind, tentative)
    uint32_t imageBytes;    // 0x40 the bytes the entry's records took in the file
    uint32_t frameDepth;    // 0x44 the encoder: the info's word at +0x2a70 less one, for an info of the FSH row's size (else 0)
    uint32_t unknown48;     // 0x48
};
static_assert(sizeof(Entry) == 0x4c, "FSH directory entry: 0x4c bytes");
static_assert(offsetof(Entry, name) == 0x34, "Entry::name");
static_assert(offsetof(Entry, flags) == 0x38, "Entry::flags");

// The directory: the count, then the entries.
struct Directory {
    int32_t count;
    Entry entries[1]; // `count` of them
};
constexpr uint32_t kDirectoryHead = 4;

inline Entry *EntriesOf(Directory *directory)
{
    return reinterpret_cast<Entry *>(reinterpret_cast<uint8_t *>(directory) + kDirectoryHead);
}

// What a record's type byte says it is (0x88677e).
enum RecordClass : int32_t {
    kRecordNone = 0,
    kRecordShape = 1,
    kRecordPalette = 2,
    kRecordPairs = 3,
    kRecordText = 4,
    kRecordMarker = 8,
    kRecordExtra = 9, // no type gives it
};

// ---- small arithmetic ----
// 0x88584d: copies `rows` rows of `width` 4-byte pixels from a source `sourceStride` pixels wide to a packed destination (the 4th
// argument is not read).
void __cdecl CopyRows(const uint8_t *source, uint8_t *dest, int32_t sourceStride, int32_t unused, int32_t width, int32_t rows);
// 0x885d30 (value + 0.5 truncated, limited to 0..255) is ClampRound in gz/GimexFshConvert.h.
// 0x885e88: the bytes of one row of `width` pixels of `bits` bits (bits 0xf counts as 16), rounded up to 8 bits on the platforms 1..0x12
// that tile, else to 16.
int32_t __cdecl RowBytes(int32_t width, int32_t bits, int32_t platform);
// 0x885eee: RowBytes * rows.
int32_t __cdecl ImageBytes(int32_t width, int32_t rows, int32_t bits, int32_t platform);
// 0x88828f: the position of block (x, y) in an image of `width` blocks: x and y interleaved bit by bit (bit 0 of x, bit 0 of y, then
// the rest in rows of (width + 1) / 2).
int32_t __cdecl BlockIndex(int32_t x, int32_t y, int32_t width);
// 0x886466: copies an image of 4 x 4 blocks of 32-bit pixels (in BlockIndex order, 0x40 bytes each) to rows: `width` x `height`
// pixels, the destination `stride` bytes per row.
void __cdecl BlocksToRows(int32_t width, int32_t height, int32_t stride, const uint8_t *blocks, uint8_t *dest);
// 0x887f0a: the same with the blocks read one after another (each four rows of 16 bytes); edge blocks copy only what fits.
void __cdecl UnblockRows(int32_t width, int32_t height, int32_t stride, const uint8_t *blocks, uint8_t *dest);

// ---- records ----
// 0x88677e: the record's class by its type byte (platform 3 has its own letters).
int32_t __cdecl ClassifyRecord(const uint8_t *record, int32_t platform);
// 0x886956: the bits per pixel of a shape record by its type byte (platform 3: 0x22 and 0x24 are 24 bits, the rest 8).
int32_t __cdecl ShapeBits(const uint8_t *record, int32_t platform);
// 0x88888f: whether the attribute word has bit 2 and the pixels (at +0x20) are coded the way `codec` says: 2 RefPack, 3 block codes,
// 4 GM (another codec: 0).
uint32_t __cdecl HasCodec(const uint8_t *record, int32_t codec, int32_t platform);
// 0x8888e7: which codec the record's pixels use (3 block codes, 2 RefPack, 4 GM, else 0; a null record: 0).
int32_t __cdecl RecordCodec(const uint8_t *record, int32_t platform);
// 0x88659e: the number of mipmap levels of a shape (1 when the width is too small for the count its attribute word gives); a non-zero
// `setLevels` first stores (setLevels - 1) as the count in the attribute word. 0 for a null record.
int32_t __cdecl MipLevels(uint8_t *record, int32_t setLevels, int32_t platform);
// 0x887ea7: the same count read from bits 12.. of the word at +0x14 (0 for a null record).
int32_t __cdecl MipLevelsAt14(const uint8_t *record, int32_t platform);
// 0x88663b: the 4-byte word at +4 (a record's length in the file; 0 ends the chain).
uint32_t __cdecl RecordLength(const uint8_t *record, int32_t platform);
// 0x88824b: attribute bit 14 (the palette is in the PS2's order). Only al is meaningful.
bool __cdecl IsSwizzled(const uint8_t *record, int32_t platform);
// 0x888265 / 0x88827a: whether ClassifyRecord is not 0 / whether the record's first big-endian word equals `code`.
bool __cdecl IsKnownRecord(const uint8_t *record, int32_t platform);
bool __cdecl IsCode(const uint8_t *record, int32_t code);
// 0x8881f8: the entries of a palette record (its count word at +0x18, 0 for 1 << bits; 0x100 at most) and `*start` = 0.
int32_t __cdecl PaletteEntries(const uint8_t *palette, int32_t *start, int32_t bits, int32_t platform);
// 0x886b4e: the bytes a record takes in the file (header, pixels or entries; every mipmap level and the compressed forms included).
int32_t __cdecl RecordSize(const uint8_t *record, int32_t platform);

// ---- the attachments, read into an info block (the FSH row's info) ----
// 0x886652: copies the text record's string (at +0x10) to info + 0x65c (an empty string for none).
void __cdecl ReadShapeText(const Directory *directory, int32_t frame, uint8_t *info);
// 0x886680 (speed-built): info + 0x2a78 = the extra record's word at +0xc, when there is one.
void __cdecl ReadShapeExtra(const Directory *directory, int32_t frame, uint8_t *info, int32_t platform);
// 0x8866ae: the block of pairs into info + 0x448 / 0x44c / 0x458 and the pairs from info + 0xa5c; 0 when the count is above 0x400.
int32_t __cdecl ReadShapePairs(const Directory *directory, int32_t frame, uint8_t *info, int32_t platform);
// 0x88675f: sets info flag 0x200000 (at +0x428) when the entry has a marker record.
void __cdecl ReadShapeMarker(const Directory *directory, int32_t frame, uint8_t *info);

// ---- mipmaps stored face by face (six faces of `levelCount` levels) ----
// 0x885d54: the offset of face `face` at level `level` in the six-face layout (levels one after the other, each 6 faces, aligned
// by `alignMask` between levels); `*faceBytes` gets that level's bytes for one face.
int32_t __cdecl FaceOffset(int32_t level, int32_t face, uint32_t alignMask, int32_t levelCount, int32_t *faceBytes, int32_t width,
                           int32_t height, int32_t bits);
// 0x885e69: the next level's offset in the flat layout: offset + bytes, aligned unless this was the last of `levelCount`.
uint32_t __cdecl NextLevelOffset(int32_t level, int32_t offset, uint32_t alignMask, int32_t levelCount, int32_t bytes);
// 0x885c25: allocates the six-face buffer for `levels` levels of `width` x `height / 6` pixels (header included); `*bytes` = its size.
void *__cdecl AllocateFaces(int32_t width, int32_t height, int32_t bits, int32_t *bytes, int32_t levels, uint32_t alignMask);
// 0x885cba: for a block-coded record (or when `forced`) sets *bits to 8 (1afb / 1cfb coded, or type 0x61 / 0x62 at `typeSource`) or 4.
void __cdecl BlockBits(const uint8_t *record, const uint8_t *typeSource, int32_t forced, int32_t *bits, int32_t platform);
// 0x885def: the shape's attribute bit 14 (0x4000), or 1 when its platform and size qualify it as tiled.
uint32_t __cdecl IsTiled(const uint8_t *record, int32_t platform, uint32_t width, uint32_t height, int32_t bits);
// 0x88800e: for every entry whose attribute word has bit 8 and whose shape has more than one level (0x887ea7), rebuilds the record face
// by face (AllocateFaces, FaceOffset) and frees the old one; 0 when an allocation fails, else 1.
int32_t __cdecl SplitFaces(Directory *directory, int32_t platform, uint32_t alignMask);

// ---- the directory ----
// 0x885f08: frees every entry's blocks (shape, palette, pairs, marker, extra, text, name) and the directory.
void __cdecl FreeDirectory(Directory *directory);
// 0x887967: a copy of the record chain's record at `record` (RefPack records are inflated into it); `*bytes` its size.
uint8_t *__cdecl ExtractRecord(const uint8_t *record, const uint8_t *end, int32_t *bytes, int32_t platform);
// 0x8871b5: reads the records of one entry of the file (shape, palette, pairs, text, marker) into `entry`; 0 when it has none.
int32_t __cdecl ParseEntry(const uint8_t *start, const uint8_t *base, const uint8_t *end, int32_t fileBytes,
                           const uint8_t *defaultPalette, Entry *entry, int32_t platform);
// 0x885f9b: expands each entry's mipmaps into entries of their own (named "<name>_mm<NN>") and frees the old directory.
Directory *__cdecl BuildDirectory(Directory *directory, int32_t platform, uint32_t alignMask);
// 0x887a06: reads a shape file (or chunk file) from the stream into a directory; 0 when nothing was read.
int32_t __cdecl ReadDirectoryStream(ifaces::cIGZFile *stream, Directory **out, int32_t scanChunks, int32_t platform, uint32_t fourcc,
                                    uint32_t fontFourcc, uint32_t alignMask);

// ---- the pixels (GimexFshPixels.size.cpp) ----
// 0x88588e: a block-coded image (`record` is the coded data) into rows of `width` x `height` BGRA pixels, `pitch` bytes apart. The
// size of the coded data is written into its header (a copy of it when `copyHeader`), the decoder's blocks are reordered by
// BlocksToRows or UnblockRows.
void __cdecl DecodeBlockImage(uint8_t *record, int32_t type, uint8_t *dest, int32_t copyHeader, int32_t pitch, int32_t width,
                              int32_t height);
// 0x8882bc: a new buffer with the image read in the platform's tile / swizzle order: `destPitch` x `height` bytes cleared, `rowStride`
// the bytes per row written; null when out of memory (or for a swizzled platform with another bit depth, leaking the buffer).
uint8_t *__cdecl Untile(const uint8_t *source, int32_t destPitch, int32_t rowStride, int32_t bits, int32_t kind, int32_t platform,
                        int32_t width, int32_t height);
// 0x885add: the three planes of a YUV 4:2:0 image untiled and stored one after the other in a new buffer (null on any failure).
uint8_t *__cdecl AssemblePlanes(const uint8_t *source, int32_t pitch, int32_t kind, int32_t platform, int32_t width, int32_t height);
// 0x88855e: one BGRA pixel (alpha 0xff) from a Y, U, V triple (calls the original's 0x888939 until lane MV's merge).
void __cdecl YuvPixel(uint8_t *dest, int32_t luma, int32_t u, int32_t v);
// 0x8885a1 (red, green and blue floats from Y, U, V) is YuvToRgbFloat in gz/GimexFshConvert.h.
// 0x888623: YUV 4:2:0 planes (untiled first when `untile`) to BGRA rows `pitch` bytes apart, two rows and two columns at a time.
void __cdecl YuvToRgba(uint8_t *dest, int32_t pitch, const uint8_t *planes, int32_t width, int32_t height, int32_t kind, int32_t platform,
                       int32_t untile);
// 0x888826: packed YUV 4:2:2 pairs (4 bytes per 2 pixels, through the original's 0x888a19) to BGRA, rows one after the other.
void __cdecl YuvPairs(uint8_t *dest, int32_t unused, const uint8_t *source, int32_t width, int32_t rows);
// 0x887507: one pixel of the record type `type` (the top bit ignored) from `source + *sourceOffset` to `dest + *destOffset`, both
// offsets advanced by what was read and written; `*column` picks the nibble / bit of the packed types.
void __cdecl UnpackPixel(uint32_t type, int32_t *destOffset, int32_t *sourceOffset, uint8_t *dest, const uint8_t *source, uint32_t *column);
// 0x886e83: a shape record's pixels as BGRA (or the record's own format for the paletted types) into `dest`, `pitch` bytes per row;
// always 1.
int32_t __cdecl DecodeShape(uint8_t *record, uint8_t *dest, int32_t pitch, int32_t platform);

} // namespace fsh
} // namespace gimex
