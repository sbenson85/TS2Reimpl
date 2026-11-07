#pragma once
// DXT1 / DXT3 / DXT5 block decoders into A8R8G8B8 (lane OW, rendering): the memory mip chain decodes a compressed level
// before it filters it. All three are cdecl and decode blocksWide x blocksHigh blocks of 4 x 4 pixels from `source`
// (8-byte DXT1 blocks, 16-byte DXT3 and DXT5 blocks; rows `sourcePitch` bytes apart) into `target` (rows `targetPitch`
// bytes apart; 16 bytes of target per block row segment). Either count 0 does nothing.
//
// The colour endpoints (R5G6B5) expand by bit replication; the two interpolated colours weigh 2/3 and 1/3 with rounding
// (+0x80 per channel, 8.8 fixed point); DXT1 with colour0 <= colour1 has one half-way colour and a transparent black.
// DXT3 alpha is the 4-bit value times 0x11. DXT5 interpolates its alpha endpoints in 0x1000000 units (the quotient
// of the scaled difference, unsigned, so the table is the original's, not the usual rounding).
#include <cstdint>

namespace nGZGraphic4::dxt {

void __cdecl DecodeDxt1(uint8_t *target, int targetPitch, const uint8_t *source, int sourcePitch, int blocksWide,
                        int blocksHigh); // 0x1837f0
void __cdecl DecodeDxt3(uint8_t *target, int targetPitch, const uint8_t *source, int sourcePitch, int blocksWide,
                        int blocksHigh); // 0x1839e0
void __cdecl DecodeDxt5(uint8_t *target, int targetPitch, const uint8_t *source, int sourcePitch, int blocksWide,
                        int blocksHigh); // 0x183dd0

// 0x186420: four rows of four pixels at `target` (rows `pitch` apart) from the 16 two-bit indices of `indices`
// (low bits first, row by row) into `palette` (four entries).
void __stdcall PaintBlock(uint8_t *target, int pitch, uint32_t indices, const uint32_t *palette);

} // namespace nGZGraphic4::dxt
