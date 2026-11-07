#pragma once
// The FSH back end's block encoders, first half (lane MS, 0x896e1e..0x8995d9; src/gz/GimexFshColourBlock.size.cpp,
// GimexFshAlphaFit.size.cpp, GimexFshColourFit.size.cpp, GimexFshEncodeBlocks.size.cpp). RVAs 1.18.0.187. Names are tentative.
//
// These write the "block" records the decoders in GimexFshBlocks.h read (S3TC-style 4 x 4 blocks: two 16-bit colours and 2-bit
// indices, with an explicit 4-bit alpha block for 0x1afb and an interpolated 8-bit one for 0x1cfb).
//
// Two encoders live here:
//  - the "colour record" encoder 0x897a29 (kinds 0x18fb, 0x1afb, 0x1cfb, 0x28fb with flags from the caller) cuts the pixels into 4 x 4
//    blocks and picks two end colours per block by trying every pair of the block's distinct colours (0x896e1e);
//  - the "block" encoder 0x8988f3 uses least-squares colour fitting (0x897e3b, the single-colour tables 0x8990a2) and the alpha
//    fitter 0x897b56, the callees past 0x8995da (the second half: ordering, clustering, the endpoint solver) are still original calls.
//
// Uninitialised memory (the cause of lane MP's nondeterminism, found by lane MS): see GimexFshColourBlock.size.cpp. Ours starts every
// local table at zero, which is what the original sees when its stack is zero (the tests clear it).
#include "gz/Gimex.h"

#include <cstdint>

namespace gimex {
namespace fsh {

// ---- the colour record encoder (0x896e1e..0x897a29) ----

// 0x896e1e: encodes one block of width x height (each 1..4) 32-bit pixels (B, G, R, A in memory order; `stride` pixels per row) into
// two 16-bit 5-6-5 colours followed by one index byte per row (2 bits per pixel, the first pixel in the low bits; the row's index
// bytes are laid out four to a row whatever the width). Pixels with alpha below 0x40 are transparent. Tries every pair of the
// block's distinct colours as end points in the 4-colour mode and (also for transparent blocks) the 3-colour mode, and keeps the
// one with the least weighted error (0x897987).
void __cdecl EncodeColourBlock(const uint32_t *pixels, uint16_t *out, int32_t stride, int32_t width, int32_t height);
// 0x8976f6: a width x rows pixel area into blocks (8 bytes each, left to right, top to bottom) at `out`.
void __cdecl EncodeColourBlocks(const uint32_t *pixels, uint8_t *out, uint32_t width, uint32_t rows);
// 0x89779a: which of four colours (end 0, end 1, then the 2/3 and 1/3 points) is nearest to `entry`, by the sum of the absolute
// differences of the 5-6-5 parts; each colour is passed as its three bytes in the low bytes of a stack slot. Only al is set.
uint8_t __cdecl NearestOfFour(uint32_t end0, uint32_t end1, uint32_t twoThirds, uint32_t oneThird, uint32_t entry);
// 0x8978a7: the same for three colours (end 0, end 1, the middle): 0, 1 or 2.
uint32_t __cdecl NearestOfThree(uint32_t end0, uint32_t end1, uint32_t middle, uint32_t entry);
// 0x897987: the weighted squared error of `count` palette entries (4 bytes: three colour bytes and the pixel count) against the
// colours chosen for them (3 bytes each); the parts are scaled by 8, 4 and 8 before squaring.
uint32_t __cdecl ColourError(const uint8_t *palette, const uint8_t *chosen, uint8_t count);
// 0x897a29: a whole record. Writes the 8-byte header (type from the flags word `flags` points to, may be null: 0x100 explicit alpha
// 0x1afb, 0x200 0x1cfb, 8 0x28fb, else 0x18fb; the 0x80 flag when `size` is above 16,000,000; the size in 3 or 4 bytes; the flag byte)
// and the blocks of `size / 16` rows of 4 pixels from `source`. Returns the bytes written (8 + 8 per block row group).
int32_t __stdcall EncodeColourRecord(uint8_t *dest, const uint32_t *source, int32_t size, const uint32_t *flags);

// ---- the least-squares encoder (0x897b56..0x8990a2) ----

// 0x897b56: input and output of the alpha end-point search. `count` values in 0..1; the search picks the cheapest of five
// configurations of 0x899d55 (an original call) and writes the end points and a code (0..5, 6 for exact zero, 7 for exact one)
// per value.
struct AlphaFit {
    int32_t count;          // +0x00
    float values[16];       // +0x04
    int32_t allowZero;      // +0x44 the zero end point may be used (code 6)
    int32_t allowOne;       // +0x48 (code 7)
    int32_t levels;         // +0x4c out: 6 or 8
    int32_t endpoints[2];   // +0x50 out
    int32_t codes[16];      // +0x58 out
};
static_assert(sizeof(AlphaFit) == 0x98, "AlphaFit");
void __cdecl FitAlpha(AlphaFit *fit);

// 0x897e3b / 0x8990a2: input and output of the colour end-point search. Up to 16 colours of three doubles in 0..1 and three weights.
struct ColourFit {
    int32_t count;          // +0x000 (0 or a null fit: nothing happens)
    int32_t reserved;       // +0x004
    double colours[16][3];  // +0x008
    double weights[3];      // +0x188
    int32_t maxLevels;      // +0x1a0 how many end-point steps to search (3 or 4)
    int32_t levels;         // +0x1a4 out
    int32_t endpoints[2][3];// +0x1a8 out: the two end colours, 5-6-5 parts (or 4-4-4 in the other form)
    int32_t indices[16];    // +0x1c0 out: a code for each colour
};
static_assert(sizeof(ColourFit) == 0x200, "ColourFit");
void __cdecl FitColours(ColourFit *fit);
// 0x8990a2: the best end points for colours all within 1/2048 of each other: from a table (built on first use from the image's
// 5-6-5 tables at 0x105dea0..) of the nearest representable pairs. Returns the error (x87 st0).
double __cdecl FitSingleColour(int32_t *levels, int32_t count, const double *colours, const double *weights, int32_t *endpoints,
                               int32_t *indices);

// 0x8988f3: encodes `width` x `rows` pixels (B, G, R, A; rows `pitch` bytes apart) into 4 x 4 blocks at `dest`; flags 0x100 / 0x200 /
// 0x10000 / 0x20000 / low byte 0..2 choose the alpha form and the colour search mode (see the source). Returns the bytes written.
int32_t __cdecl EncodeBlocks(int32_t width, int32_t rows, uint32_t pitch, const uint8_t *source, uint8_t *dest, uint32_t flags,
                             const float *weights, int32_t form4444, int32_t bigEndian);

} // namespace fsh
} // namespace gimex
