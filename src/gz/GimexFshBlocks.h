#pragma once
// The FSH back end's block decoders and the RefPack coder (lane MR, 0x894e0e..0x8953ff, 0x8955c7..0x895838, 0x896960..0x896e1b;
// src/gz/GimexFshBlocks.size.cpp, GimexFshRefPack.cpp, GimexFshRefPackEncode.size.cpp). RVAs 1.18.0.187. Names are tentative.
//
// The "block" records (headers 0x18fb, 0x1afb, 0x1cfb, 0x28fb and their 0x80 variants) are S3TC-style compressed textures: every
// 4 x 4 pixel block is a colour block (two 16-bit colours and 2-bit indices) with, for 0x1afb, an explicit 4-bit alpha block in
// front and, for 0x1cfb, an 8-bit interpolated alpha block. Decoded pixels are B, G, R, A bytes. The headers 0x10fb / 0x11fb (and
// 0x90fb / 0x91fb) are RefPack.
#include "gz/Gimex.h"

#include <cstdint>

namespace gimex {
namespace fsh {

// ---- header tests (all __stdcall, one pointer argument: the first two bytes, big-endian) ----
// 0x895381 / 0x8953ab / 0x8953d5: 100 when the header is 0x28fb / 0x1afb / 0x1cfb (or the same with the 0x80 flag), else 0.
uint32_t __stdcall HasHeader28fb(const uint8_t *header);
uint32_t __stdcall HasHeader1afb(const uint8_t *header);
uint32_t __stdcall HasHeader1cfb(const uint8_t *header);
// 0x8955c7: whether the header is one of the block kinds (0x18fb, 0x1afb, 0x1cfb, 0x28fb, each with or without 0x80). Only al is set.
bool __stdcall IsBlockCompressed(const uint8_t *header);
// 0x895802: whether the header is RefPack's (0x10fb, 0x11fb, 0x90fb, 0x91fb). Only al is set.
bool __stdcall IsRefPack(const uint8_t *header);

// ---- block decoders (__cdecl) ----
// 0x894e0e: one explicit 4-bit alpha block (8 bytes) into the alpha bytes of 4 x 4 pixels (byte 3 of each; `stride` bytes per row).
void __cdecl DecodeAlpha4(uint8_t *pixels, int32_t stride, const uint8_t *block);
// 0x894e67: one interpolated alpha block (8 bytes: two end values, 16 3-bit indices; 6 or 8 steps). The row stride is fixed at 0x10.
void __cdecl DecodeAlpha8(uint8_t *pixels, int32_t stride, const uint8_t *block);
// 0x894ff3: one colour block (8 bytes) into B, G, R, A of 4 x 4 pixels. `form1555`: the colours are 5-5-5 (else 5-6-5);
// `bigEndian`: the two colours and the index bytes are read the other way round (0x28fb).
void __cdecl DecodeColourBlock(uint8_t *pixels, int32_t stride, const uint8_t *block, uint32_t form1555, int32_t bigEndian);
// 0x8953ff: a block-compressed record (header 8 bytes: type, the size of the decoded bytes, flags) into `pixels`; returns the
// decoded size in bytes; `consumed` (may be null) gets size / 8 (the compressed bytes of a colour-only record).
// Flag bits of the byte after the size: 4 = 5-5-5 colours, 8 = divide the colours by the alpha afterwards (undo premultiplication).
int32_t __stdcall DecodeBlocks(uint8_t *pixels, const uint8_t *record, int32_t *consumed);

// ---- RefPack (0x895620 decoder, 0x896960 / 0x89698a / 0x896db9 the compressor) ----
// 0x895620: decodes a RefPack record (header 0x10fb / 0x11fb, or 0x90fb / 0x91fb with a 4-byte size) into `dest` with no room
// check; returns the decoded size (0 for a null source); `consumed` (may be null) gets the bytes read. cRZFastCompression3's decoder
// (RefPack::Decompress) is the same loop with a room check and no wide header: they share RefPack::DecodeBody.
int32_t __stdcall DecodeRefPack(uint8_t *dest, const uint8_t *source, int32_t *consumed);
// 0x89698a: RefPack-encodes `length` bytes into `dest`; returns the bytes written (0 when an allocation fails). Same coding as
// cRZFastCompression3's RefPack::Encode except that the last four bytes are never matched and go out as literals. `quick` is
// RefPack::Encode's (every caller passes 0); `window` is not read.
int32_t __cdecl RefPackEncode(const uint8_t *source, int32_t length, uint8_t *dest, int32_t window, int32_t quick);
// 0x896db9: writes the header (0x10fb and a 3-byte size, or 0x90fb and a 4-byte size from 16 MB) then the RefPack data; returns the
// total. The fourth argument is not read.
int32_t __stdcall RefPackEncodeWithHeader(uint8_t *dest, const uint8_t *source, int32_t length, int32_t unused);

} // namespace fsh
} // namespace gimex

// cRZFastCompression3's RefPack (src/rz/RefPack.size.cpp) shares these with the FSH back end.
namespace RefPack {
// The opcode loop of RefPack::Decompress: from `in` (after the header) to the end opcode, writing at `dst`; false when more than
// `room` bytes would be written (nothing of that opcode is written).
bool DecodeBody(const uint8_t *&in, uint8_t *&dst, int32_t room);
// How many leading bytes of a and b agree, up to `max`. Hooked at 0x896960 (the FSH compressor's helper, also called by
// cRZFastCompression3's original encoder at 0x9cf4f3).
uint32_t MatchLength(const uint8_t *a, const uint8_t *b, uint32_t max);
// The literal runs of RefPack::Encode: emits all but the last 0-3 pending literals as 4..112-byte runs.
void FlushLiterals(uint8_t *&out, const uint8_t *&run, uint32_t &literals);
// RefPack's hash of the three bytes at p.
uint32_t Hash(const uint8_t *p);
} // namespace RefPack
