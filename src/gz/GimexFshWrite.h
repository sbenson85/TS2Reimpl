#pragma once
// The FSH back end's encode side (lane MY, 0x889917..0x88a9ad and 0x88b405..0x88c0e9; lane MZ, the top three 0x88905f EncodeShape,
// 0x88aa6e CompressShape and 0x88c719 WriteDirectory; src/gz/GimexFshWrite*.size.cpp): the directory writer's helpers (padding, chaining,
// sizes, mip-level merging, cube-map tiles), the palette and attachment builders, the block and GM compressors, the pixel packer, and
// the encoder, compressor and directory writer over them. All __cdecl, RVAs 1.18.0.187. Names are tentative.
//
// The directory the writer builds (0x88905f fills it, 0x88c719 writes it): a count, then 0x4c-byte entries (Entry). Every block an
// entry owns (the shape, the palette, the marker, the user data, the pairs, the text and the name) was made by the codecs' allocator
// (gimex::Allocate) and is freed with gimex::FreeMemory; each block has its byte count in the word after its pointer (the name has none).
//
// The platform argument is the FSH codec's (2 for every call the game makes); it picks the byte order of the words (ReadWord / PutWord),
// the pixel layout of the palette builder and the alignment of a row.
#include "gz/Gimex.h"
#include "gz/GimexFshDecode.h" // kDirectoryHead

#include <cstddef>
#include <cstdint>

namespace gimex {
namespace fsh {

// ---- sizes (0x8899a3, 0x8899fc, 0x88a187, 0x88a1ed) ----
// 0x8899a3: the bytes of a row of `width` pixels at `bits` bits (15 counts as 16), rounded up to 8 bits on platforms 2..5, 7 and 8 and to
// 16 on the others.
int32_t __cdecl EncodeRowBytes(int32_t width, int32_t bits, int32_t platform);
// 0x8899fc: EncodeRowBytes times height, both dimensions first rounded up to a multiple of 4 when `pad` is set.
int32_t __cdecl PlaneBytes(uint32_t width, uint32_t height, int32_t bits, int32_t platform, int32_t pad);
// 0x88a187: the mip levels a shape header declares (bits 20..23 of its attribute word, 0 and 15 mean 1), 1 when the width cannot hold
// that many halvings; 0 for a null shape.
int32_t __cdecl EncodeMipLevels(const uint8_t *shape, int32_t platform);
// 0x88a1ed: the bits per pixel of a shape header's type (the first byte masked to 7 bits; on platform 3 only the type bytes 0x22 and 0x24
// are 24-bit and the rest 8): 1, 4, 8, 12, 15 (0xf), 16, 24, 32 as the original answers them.
int32_t __cdecl EncodeShapeBits(const uint8_t *shape, int32_t platform);

// ---- the directory's blocks ----
// 0x88a105: frees every block of every entry (shape, palette, the blocks, text, name) and then the directory itself.
void __cdecl FreeWrittenDirectory(Directory *directory);
// 0x889917 (`unused` is not read): for every entry that has a shape, a shape size and a palette size, grows the shape block to the next
// multiple of align + 1 (`mask` is the low bits to clear: size + mask & ~mask) with zero fill.
void __cdecl PadShapes(Directory *directory, int32_t unused, uint32_t mask);
// 0x88a962: appends `block` (when not null) to the chain whose last block is *tail: the old tail's word at +4 gets *tailBytes, the new
// block's gets 0; *tail and *tailBytes then name the new block and its size.
void __cdecl ChainBlock(uint8_t **tail, uint32_t *tailBytes, uint8_t *block, uint32_t bytes, int32_t platform);
// 0x88a9ad: chains each entry's blocks in the order shape, palette, block 0x24, block 0x2c, block 0x14, text (each block's +4 word is
// the size of the next block, 0 for the last). Returns 0.
int32_t __cdecl ChainEntryBlocks(Directory *directory, int32_t platform);
// 0x88a8f6: writes the mip level count into the shape header (bits 20..23 of the attribute word get count - 1, 0 counts as 1) and, for a
// compressed shape (0x88888f with kind 3), the level count in the high nibble of byte 0x25.
void __cdecl SetMipLevels(uint8_t *shape, int32_t count, int32_t platform);

// ---- the attachment builders (GimexFshWritePalette.size.cpp) ----
// Each fills *out with a new block (the codecs' allocator, zeroed, a 0x10-aligned size in *outBytes) or null and 0. `info` is the writer's
// frame description (the 0x2a70-byte Gimex Info and what follows it); offsets below are in it.
// 0x88b405: the palette block (type byte, attributes, 0x20 header bytes, then count entries from the BGRA `entries` in the platform's
// layout: 1: 1555, 2: by the info's channel bits, 3: 6-6-6, 4: 556 (not for a grey ramp), 5: 4444-ish by bits, 6: PS2 with the entry order of
// 0x885429, 7: by bits, 8: 8888, 9: 1555 swapped; another platform: the type byte is the count and no entries are written). Nothing when
// count < 1 or `skip` is set.
void __cdecl BuildPalette(const Info *info, uint32_t count, const uint8_t *entries, uint8_t **out, uint32_t *outBytes, int32_t skip,
                          int32_t platform);
// 0x88b9e1: the 0x6f text block of the NUL-terminated string at info + 0x65c (nothing when it is empty or `skip` is set).
void __cdecl BuildTextBlock(const Info *info, uint8_t **out, uint32_t *outBytes, int32_t skip, int32_t platform);
// 0x88ba81: the 0x6b user-data block, info + 0x2a78 bytes of the game's filler text (only when info + 4 > 0x2a7b and the count > 0).
void __cdecl BuildUserDataBlock(const Info *info, uint8_t **out, uint32_t *outBytes, int32_t platform);
// 0x88bb19: the 0x7c block of the info's word pairs (count at info + 0x458, pairs from info + 0xa5c, two words at 0x448 / 0x44c first).
void __cdecl BuildPairsBlock(const Info *info, uint8_t **out, uint32_t *outBytes, int32_t skip, int32_t platform);
// 0x88bc6b: the 0x69 metal-bin attachment (when bit 0x200000 of the variant word is set): a note from the game's text on platforms other than 5.
void __cdecl BuildMetalBinBlock(const Info *info, uint8_t **out, uint32_t *outBytes, int32_t platform);
// 0x88bd45: a copy of the NUL-terminated name at info + 0x45c.
void __cdecl CopyShapeName(const Info *info, char **out);

// ---- pixel packing and compression (GimexFshWritePixels.size.cpp) ----
// 0x88c0e9: packs the frame pixel at *sourceOffset (BGRA bytes, or an index into the info's palette when its bits per pixel are 8) into the
// record type `type` at *destOffset of `dest`, advancing both offsets (and *column for the types that take two or eight pixels to a byte; the
// caller steps it by one more). *row is read and written back. `width` is the frame's width; the original also takes the height (unused). Types
// the table does not know write nothing.
void __cdecl PackPixel(const Info *info, const uint8_t *source, uint8_t *dest, int32_t *sourceOffset, int32_t *destOffset, int32_t type, int32_t *column,
                       uint32_t *row, int32_t width);
// 0x88bd83: the whole frame as the shape's pixels: clears the shape's pixel area, packs every pixel row by row with PackPixel and, with
// flag 0x2000, reorders the result (ReorderImage) at the platform's console size (reorderWidth x reorderHeight on platforms 5 and 6).
void __cdecl PackShapePixels(const uint8_t *shape, uint8_t *dest, const Info *info, const uint8_t *source, int32_t sourceStride, int32_t platform,
                             uint32_t flags, int32_t reorderWidth, int32_t reorderHeight);
// 0x88bf12: the frame (BGRA, `stride` bytes a row) as a block compressed record at `dest` (EncodeRecord when the info's compression word is 3,
// else EncodeColourRecord): pads the image to 4 x 4 blocks (8 x 8 and the console order on platform 5), fixes the header's size and width
// bytes and rounds the length to 16. With `dropHeader` the 8 header bytes are removed. Returns the length (0 when out of memory).
uint32_t __cdecl BlockCompress(const Info *info, int32_t stride, const uint8_t *source, uint8_t *dest, int32_t platform, int32_t dropHeader);
// 0x88c070: the frame as a GM stream (EncodeGm) with the quantiser from the info's quality (100 - quality over 6.25, 1 to 17) and the alpha
// mode from its alpha bits. Returns the stream's length.
uint32_t __cdecl GmCompress(const Info *info, uint32_t stride, const uint32_t *pixels, uint16_t *output);

// ---- shape assembly (GimexFshWriteMip.size.cpp) ----
// 0x88a5b2: copies a width x height image of 4-byte pixels (stride in bytes) into 4 x 4 tiles in row order, each tile row padded with zero
// pixels past the right edge and each band padded with zero rows past the bottom.
void __cdecl TilePixels(int32_t width, int32_t height, int32_t stride, const uint8_t *source, uint8_t *dest);
// 0x88a731: the same into the Morton order of 4 x 4 blocks of 16 bytes (the index from the decode side's 0x88828f) of an image padded to
// paddedWidth x paddedHeight; the pad pixels repeat the tile's first byte (also for a tile wholly right of the image; a tile wholly below it is zero).
void __cdecl TileBlocks(uint32_t paddedWidth, int32_t paddedHeight, int32_t width, int32_t height, int32_t stride, const uint8_t *source,
                        uint8_t *dest);
// 0x88a3c9: for each entry with the cube flag (0x40000 in the flags word) and more than one mip level, rebuilds the shape as a chain of the six
// faces of every level, aligned by `align`.
void __cdecl ExpandCubeMaps(Directory *directory, int32_t platform, uint32_t unused, uint32_t align);
// 0x889a2d: merges each run of mip-named entries into the shape before them (decoding or encoding the RefPack form as needed), frees the
// merged entries' blocks and drops the emptied entries from the directory.
void __cdecl MergeMipmaps(Directory *directory, int32_t platform, uint32_t unused, uint32_t align);

// ---- the top three (lane MZ: GimexFshWriteShape.size.cpp, GimexFshWriteDirectory.size.cpp) ----
// 0x88aa6e: the frame as a shape record in `out` (the 0x20-byte header, then the pixels), whose size it answers rounded as the caller
// stores it (length + 0x2f & ~0xf). Picks the record type from the platform, the pixel bits and the info's channel bits, packs the pixels
// (PackShapePixels, a block-compressed form, YUV, or GM), and for compression kind 2 tries the RefPack form and keeps it when smaller.
// `effectiveBits` and `aux8` / `aux9` are the encoder's choices (bits per pixel of the record; aux9 8: the record is 8-bit grey alpha, 4: a
// 4-bit plane; aux8 8 or 4: the same for the second plane); `noPack` (the info's flag 2) goes nowhere.
uint32_t __cdecl CompressShape(uint8_t *out, const Info *info, int32_t bits, const uint8_t *pixels, uint32_t stride, uint32_t noPack,
                               int32_t platform, int32_t aux8, int32_t aux9);
// 0x88905f: encodes frame `index` into the directory the FSH row's wopen made: picks the record bits for the platform, builds the shape (a
// buffer sized from PlaneBytes, then CompressShape; none when the info's flag 2 is set), the palette, metal-bin, user-data, pairs and text
// blocks and the name, and stores them with the info's variant word and a four-character tag in entry `index`. Answers 0 when the shape's
// buffer could not be allocated, else 1. (The original takes a seventh argument, an option it never reads.)
int32_t __cdecl EncodeShape(Directory *directory, const Info *info, const uint8_t *pixels, int32_t pitch, int32_t index, int32_t platform);
// 0x88c719: writes the directory to the stream. `layout` 0 is the file (a 16-byte header and a table of offsets, sizes and names, then the
// entries, the header written again at the end), 1 a chunk header per entry; `writeHeaders` 0 writes the blocks only. `alignBase` is the
// alignment the records are put on (an entry's own frameDepth wins and carries on to the next), `codecAlign` the same for the coded records
// (those with a header of 0x28 bytes); `padMask` goes to PadShapes, `unused` and `cubeAlign` to MergeMipmaps and ExpandCubeMaps. Answers
// whether everything was written (layout 1: the chunk header's byte count when no block follows it).
uint32_t __cdecl WriteDirectory(ifaces::cIGZFile *stream, Directory *directory, int32_t platform, int32_t layout, int32_t writeHeaders,
                                uint32_t fourcc, uint32_t alignBase, uint32_t padMask, uint32_t unused, int32_t cubeAlign, uint32_t codecAlign);

} // namespace fsh
} // namespace gimex
