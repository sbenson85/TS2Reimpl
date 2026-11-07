#pragma once
// The FSH back end's "GM" image encoder (lane MV, 0x89d938..0x89e722; src/gz/GimexFshGmEncode.size.cpp). RVAs 1.18.0.187. Names are
// tentative.
//
// The inverse of lane MR's decoder (GimexFshGm.h): a BGRA image goes to 16 x 16 macroblocks of six 8 x 8 blocks (four luma, Cb and
// Cr averaged over 2 x 2 pixels), each block through a float DCT, scaled and rounded to integers, quantised, and written as DC
// differences and run / level codes from the same code tables the decoder reads. The transform and the colour conversion are
// single precision float arithmetic (SSE scalar), so the order of every operation matters and is kept.
//
// The original keeps the encoder state in image globals (0x10ebee8..0x10ecc1c): the three DC predictors, the output pointer, the
// six blocks of pixels and of levels, the bit accumulator, bit count and byte position, the DCT's four rotation constants and the
// 64 scale factors built once from cos and sqrt. A relocation scan found no other reader of that range, so ours keeps all of it in
// GmEncodeState (same layout, checked below); the tests set both sides from the same values and compare them after each call. The
// code tables, the zigzag order and the quantiser matrix are still read from the original image (GameAddress).
#include <cstddef>
#include <cstdint>

namespace gimex {
namespace fsh {

struct GmEncodeState {
    int32_t dcPredictor[3];       // 0x10ebee8 (luma), 0x10ebeec (Cb), 0x10ebef0 (Cr)
    uint8_t *output;              // 0x10ebef4: where PutBits writes
    float pixels[6][64];          // 0x10ebef8: Y0..Y3, Cb, Cr (Cb is at 0x10ec2f8, Cr at 0x10ec3f8)
    int32_t levels[6][64];        // 0x10ec4f8: the quantised blocks
    uint32_t accumulator;         // 0x10ecaf8: the bits not yet stored (the low bitCount of them)
    int32_t bitCount;             // 0x10ecafc: bits in the current byte
    int32_t bytePosition;         // 0x10ecb00: bytes stored
    float rotate45;               // 0x10ecb04: cos(pi / 4)
    float rotateA;                // 0x10ecb08: cos(pi / 8) - cos(3 pi / 8)
    float rotateB;                // 0x10ecb0c: cos(pi / 8) + cos(3 pi / 8)
    float rotateC;                // 0x10ecb10: cos(3 pi / 8)
    uint32_t reserved;            // 0x10ecb14: not used by the encoder
    float scale[64];              // 0x10ecb18: 1 / (4 c(i) c(j)) with c(0) = sqrt(8) / 4 folded in
    int32_t tablesReady;          // 0x10ecc18
};

static_assert(offsetof(GmEncodeState, output) == 0x0c);
static_assert(offsetof(GmEncodeState, pixels) == 0x10);
static_assert(offsetof(GmEncodeState, levels) == 0x610);
static_assert(offsetof(GmEncodeState, accumulator) == 0xc10);
static_assert(offsetof(GmEncodeState, rotate45) == 0xc1c);
static_assert(offsetof(GmEncodeState, scale) == 0xc30);
static_assert(offsetof(GmEncodeState, tablesReady) == 0xd30);

extern GmEncodeState gGmEncodeState;
// Ours only: how many times a code writer printed its out-of-range warning (the tests skip inputs that would, as the original cannot print in the harness).
extern uint32_t gGmEncodeWarnings;

// ---- bit writer (__cdecl) ----
// 0x89e45f: starts a stream at `output`.
void __cdecl ResetBitWriter(uint8_t *output);
// 0x89e3d7: appends the low `count` bits of `value`, most significant first (nothing for count <= 0).
void __cdecl PutBits(uint32_t value, int32_t count);
// 0x89e445: pads the current byte with zero bits.
void __cdecl FlushBits();
// 0x89d938: bits written so far.
int32_t __cdecl BitsWritten();

// ---- code writers ----
// 0x89e098: a DC difference: its size from `table` (entries of u16 code, s8 length; 4 bytes) then the bits of the difference.
void __cdecl WriteDc(const void *table, int32_t difference);
// 0x89dfd7: one (run, level) pair; an escape (6 bits, run, level) when the pair is not in the tables. Prints a warning for a
// run over 63 or a level of 0 or over 255 in size, and codes level +-255 then.
void __cdecl WriteRunLevel(uint32_t run, int32_t level);
// 0x89e102: the macroblock address increment `count` (escape codes for each 33 over 33).
void __cdecl WriteAddressIncrement(int32_t count);
// 0x89e14a: one entry of the header code table.
void __cdecl WriteHeaderCode(int32_t group, int32_t index);
// 0x89d947: one quantised block: the DC difference (predictor by `block`) then the run / level pairs in zigzag order, then the end code.
void __cdecl WriteBlock(const int32_t *levels, uint32_t block);

// ---- transform ----
// 0x89dc7b: one row (8 floats) of the DCT. 0x89db37: one column (stride of 8 floats).
void __cdecl DctRow(const float *in, float *out);
void __cdecl DctColumn(const float *in, float *out);
// 0x89d9c9: the 8 x 8 DCT (columns, then rows) scaled and rounded to integers.
void __cdecl DctBlock(const float *in, int32_t *out);
// 0x89de18: builds the rotation constants and the scale table.
void __cdecl InitEncodeTables();
// 0x89e16f: quantises a block in place or between buffers: the DC rounded by 8 >> shift, the others by `matrix`[i] and `quantiser`.
void __cdecl QuantiseBlock(const int32_t *source, int32_t *dest, uint8_t shift, const uint8_t *matrix, int32_t quantiser);

// ---- the encoder ----
// 0x89e20d: 8 x 8 pixels (BGRA words, rows of `stride` pixels) into luma block `block` and chroma samples (averaged over 2 x 2)
// at chroma rows `chromaRow`.., columns `chromaColumn`.. of the Cb and Cr blocks.
void __cdecl ConvertToBlocks(const uint32_t *pixels, int32_t stride, int32_t block, int32_t chromaRow, int32_t chromaColumn);
// 0x89dd9b: one macroblock: the header codes (the quantiser the first time, the address increment 1 later), then the six blocks
// from the pixels the conversions left, transformed, quantised and written.
void __cdecl WriteMacroblock(int32_t first, int32_t quantiser);
// 0x89e47a: encodes `width` x `height` BGRA pixels (the `stride` is in bytes) as a GM stream at `output`; `flags` bit 0 appends the
// alpha plane as 8 bits (`alphaMode` 0) or 4 bits (1) per pixel. Returns the stream's length in bytes. See the .cpp for the original's
// stride quirks.
uint32_t __cdecl EncodeGm(const uint32_t *pixels, uint16_t *output, int32_t width, int32_t height, uint32_t stride, uint32_t flags,
                          int32_t quantiser, int32_t alphaMode);

} // namespace fsh
} // namespace gimex
