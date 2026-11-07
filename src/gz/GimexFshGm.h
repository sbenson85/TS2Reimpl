#pragma once
// The FSH back end's "GM" image decoder (lane MR, 0x895c47..0x89694e; src/gz/GimexFshGm.size.cpp, GimexFshGmBits.cpp). RVAs
// 1.18.0.187. Names are tentative.
//
// GM is an MPEG-1 style still-image (or frame) coder: a header ('GM', width / 16, height / 16, flags), then per 16 x 16
// macroblock six 8 x 8 blocks (four luma, Cb, Cr) of DC differences and run / level pairs from variable length code tables, the
// Chen-Wang integer IDCT, and a YCbCr to B, G, R, A conversion. All of it integer arithmetic; no floating point.
//
// The original keeps its decoder state in image globals (the stream pointer and bit position, the three DC predictors, the six
// decoded blocks, the quantiser scale, two clipping tables and an "initialised" flag). Ours keeps the same state in GmState; the
// tests (tests/gz_gimex_fsh_gm.cpp) set both sides from the same values and compare them after each call. The code and DC tables,
// the zigzag order and the quantiser matrix are still read from the original image (GameAddress); deriving them is a follow-up.
//
// Reading past the end: the bit reader loads four bytes at the current byte position (big-endian), so a stream needs three bytes
// of slack after its last byte; what lies beyond a stream is read as is (the tests pad with fixed bytes).
#include <cstdint>

namespace gimex {
namespace fsh {

struct GmState {
    const uint8_t *data;         // 0x10e6c6c: the bit stream (the decoder sets it to the data after the 8-byte header)
    int32_t bitPosition;         // 0x10e7070
    int32_t dcPredictor[3];      // 0x10e6c60 (luma), 0x10e6c64 (Cb), 0x10e6c68 (Cr)
    int16_t blocks[6][64];       // 0x10e6960: Y0..Y3, Cb, Cr (the Cb block is at 0x10e6b60, Cr at 0x10e6be0)
    int32_t quantiser;           // 0x105dc3c (an image global the decoder both reads and writes; the image holds 6 at start)
    int32_t tablesReady;         // 0x10e7878
    uint8_t byteClip[0x3ff];     // 0x10e6c70: 0 up to index 0x17f, then 0..255, then 255 from 0x280
    int16_t wordClip[1024];      // 0x10e7078, index -512..511 (wordClip[i + 512]): clamp(i, -256, 255)
    uint32_t clipOverruns;       // ours only: how many table lookups went outside a clipping table (the original reads whatever lies there)
};

// The decoder's state (the functions below all work on it; the tests set and read it).
extern GmState gGmState;

// ---- bit reader (__cdecl) ----
// 0x895ee0: the next `count` (1..16) bits of the stream as a number, without moving the position.
uint32_t __cdecl PeekBits(int32_t count);
// 0x895cdb: the same, moving the position on.
uint32_t __cdecl GetBits(int32_t count);

// ---- DC differences ----
// 0x895d8f (luma) and 0x895d1f (chroma): a code giving a size, then that many bits as a signed difference (0 for size 0).
int32_t __cdecl ReadLumaDc();
int32_t __cdecl ReadChromaDc();

// ---- blocks ----
// 0x89583b: an intra block: the DC (predictor chosen by `block`: 0..3 luma, 4 Cb, 5 Cr) and run / level pairs until the end code, dequantised
// into the (not yet inverse-transformed) block. Escape codes carry 6 bits of run and an 8 or 16 bit level.
void __cdecl DecodeIntraBlock(int32_t block);
// 0x895a44: the same for a predicted block: other code tables when `flag` is set, 12-bit escape levels.
void __cdecl DecodeInterBlock(int32_t block, int32_t flag);
// 0x895dff: the macroblock address increment (escape 0x21 steps, stuffing skipped); returns the increment. (A function of its own,
// reached by a tail jump from 0x8962e9.)
int32_t __cdecl ReadAddressIncrement();
// 0x8962e9: one macroblock: an optional new quantiser (a 0 bit then a 1 bit then 5 bits), the six blocks decoded and transformed,
// then, unless `isLast`, the next address increment (returned). For `isLast` the original returns whatever the last transform left in
// eax; ours returns 0.
int32_t __cdecl DecodeMacroblock(int32_t isLast, int32_t inter, int32_t flag);

// ---- inverse transform (blocks of 64 int16, row-major) ----
// 0x896137: one row (8 int16) in place. 0x895f18: one column (8 int16 with a stride of 8) in place, results clipped to -256..255.
void __cdecl IdctRow(int16_t *row);
void __cdecl IdctColumn(int16_t *column);
// 0x895c47: eight rows then eight columns.
void __cdecl Idct8x8(int16_t *block);

// ---- the decoder ----
// 0x895e7c: builds the two clipping tables.
void __cdecl InitClipTables();
// 0x8963bd: one 8 x 8 luma block (index `block`) with its chroma (rows `chromaRow`.., columns `chromaColumn`.. of the 8 x 8 Cb and Cr
// blocks, each chroma sample for 2 x 2 pixels) as B, G, R, A at `dest` (rows of (stride >> 2) * 4 bytes). The third argument is not read.
void __cdecl ConvertBlock(uint8_t *dest, int32_t stride, int32_t unused, int32_t block, int32_t chromaRow, int32_t chromaColumn);
// 0x89662e: decodes a GM stream (`data`, starting at the header) of width x height into `dest`. `flags`: 1 = macroblocks in rows
// (else in columns), 2 = predicted blocks, 4 = the predicted blocks' table flag. Returns 1, or 0 for a null `data`.
int32_t __cdecl DecodeGm(uint8_t *dest, int32_t stride, const uint8_t *data, int32_t width, int32_t height, uint32_t flags);
// 0x8968dd: reads the header of a GM stream; false when it does not start with 'GM'. `flags` gets 0, 4 or 8; `quality` bits 1..5 of byte 8.
int32_t __cdecl ParseGmHeader(const uint8_t *header, int32_t *width, int32_t *height, int32_t *flags, int32_t *quality, int32_t *size);
// 0x89694e: whether the first two bytes are 'GM'.
int32_t __cdecl IsGm(const uint8_t *header);

} // namespace fsh
} // namespace gimex
