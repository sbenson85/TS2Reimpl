// DXT1 / DXT3 / DXT5 block decoders (lane OW): src/gz/DxtBlocks.h.
#include "gz/DxtBlocks.h"

#include <cstring>

namespace nGZGraphic4::dxt {

namespace {

uint32_t Load16(const uint8_t *p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
uint32_t Load32(const uint8_t *p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
void Store32(uint8_t *p, uint32_t v) { std::memcpy(p, &v, 4); }

// R5G6B5 to 00RRGGBB by bit replication.
uint32_t Expand565(uint32_t packed)
{
    const uint32_t redBlue = (packed & 0xf800) * 0x20 + (packed & 0x1f);
    const uint32_t green = (packed & 0x7e0) * 0x20 + ((packed & 0x7e0) >> 1);
    return (green & 0xff00) + (((redBlue >> 2) + redBlue * 8) & 0xffff00ff);
}

// The colour 2/3 of the way from `a` to `b`'s mirror: (a * 0xab + b * 0x55 + rounding) / 256 per channel, the channels split
// in two halves (red and blue; alpha and green) so the products do not overlap.
uint32_t Blend(uint32_t a, uint32_t b)
{
    const uint32_t aOuter = a & 0xff00ff, bOuter = b & 0xff00ff;
    const uint32_t aInner = (a >> 8) & 0xff00ff, bInner = (b >> 8) & 0xff00ff;
    return (((aOuter * 0xab + bOuter * 0x55 + 0x800080) >> 8) & 0xff00ff) +
           ((aInner * 0xab + bInner * 0x55 + 0x800080) & 0xff00ff00);
}

// The four-colour table of DXT3 / DXT5 (no alpha) from the two endpoints.
void FourColours(uint32_t packed0, uint32_t packed1, uint32_t table[4])
{
    table[0] = Expand565(packed0);
    table[1] = Expand565(packed1);
    table[2] = Blend(table[0], table[1]);
    table[3] = Blend(table[1], table[0]);
}

} // namespace

void __stdcall PaintBlock(uint8_t *target, int pitch, uint32_t indices, const uint32_t *palette)
{
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) Store32(target + column * 4, palette[(indices >> (2 * column)) & 3]);
        indices >>= 8;
        target += pitch;
    }
}

void __cdecl DecodeDxt1(uint8_t *target, int targetPitch, const uint8_t *source, int sourcePitch, int blocksWide,
                        int blocksHigh)
{
    if (blocksWide == 0 || blocksHigh == 0) return;
    for (int by = blocksHigh; by != 0; --by) {
        for (int bx = blocksWide; bx != 0; --bx) {
            const uint32_t packed0 = Load16(source), packed1 = Load16(source + 2);
            uint32_t palette[4];
            palette[0] = Expand565(packed0) | 0xff000000;
            palette[1] = Expand565(packed1) | 0xff000000;
            if (packed1 < packed0) {
                // Four colours: the opaque blends.
                palette[2] = Blend(palette[0] & 0xffffff | 0xff000000, palette[1] & 0xffffff | 0xff000000);
                palette[3] = Blend(palette[1] & 0xffffff | 0xff000000, palette[0] & 0xffffff | 0xff000000);
            } else {
                // Three colours: the half-way colour and transparent black.
                palette[2] = (palette[1] | palette[0]) - (((palette[1] ^ palette[0]) >> 1) & 0x7f7f7f7f);
                palette[3] = 0;
            }
            PaintBlock(target, targetPitch, Load32(source + 4), palette);
            target += 16;
            source += 8;
        }
        source += sourcePitch - blocksWide * 8;
        target += (targetPitch - blocksWide * 4) * 4;
    }
}

void __cdecl DecodeDxt3(uint8_t *target, int targetPitch, const uint8_t *source, int sourcePitch, int blocksWide,
                        int blocksHigh)
{
    if (blocksWide == 0 || blocksHigh == 0) return;
    for (int by = blocksHigh; by != 0; --by) {
        for (int bx = blocksWide; bx != 0; --bx) {
            uint32_t palette[4];
            FourColours(Load16(source + 8), Load16(source + 10), palette);
            const uint32_t indices = Load32(source + 12);
            for (int pixel = 0; pixel < 16; ++pixel) {
                const uint32_t nibble = (Load32(source + (pixel >> 3) * 4) >> ((pixel & 7) * 4)) & 0xf;
                const uint32_t colour = palette[(indices >> (2 * pixel)) & 3];
                Store32(target + (pixel >> 2) * targetPitch + (pixel & 3) * 4, nibble * 0x11 * 0x1000000 + colour);
            }
            target += 16;
            source += 16;
        }
        source += sourcePitch - blocksWide * 16;
        target += (targetPitch - blocksWide * 4) * 4;
    }
}

void __cdecl DecodeDxt5(uint8_t *target, int targetPitch, const uint8_t *source, int sourcePitch, int blocksWide,
                        int blocksHigh)
{
    if (blocksWide == 0 || blocksHigh == 0) return;
    for (int by = blocksHigh; by != 0; --by) {
        for (int bx = blocksWide; bx != 0; --bx) {
            uint32_t palette[4];
            FourColours(Load16(source + 8), Load16(source + 10), palette);
            const uint32_t indices = Load32(source + 12);

            // The alpha table in the top byte of each entry.
            uint32_t alpha[8];
            const uint32_t scaled0 = uint32_t(source[0]) * 0x1000000 + 0x800000;
            const uint32_t scaled1 = uint32_t(source[1]) * 0x1000000 + 0x800000;
            alpha[0] = uint32_t(source[0]) * 0x1000000;
            alpha[1] = uint32_t(source[1]) * 0x1000000;
            if (scaled1 < scaled0) {
                const uint32_t step = (scaled0 - scaled1) / 7;
                for (uint32_t k = 2; k < 8; ++k) alpha[k] = (scaled0 - (k - 1) * step) & 0xff000000;
            } else {
                const uint32_t step = (scaled1 - scaled0) / 5;
                for (uint32_t k = 2; k < 6; ++k) alpha[k] = (scaled0 + (k - 1) * step) & 0xff000000;
                alpha[6] = 0;
                alpha[7] = 0xff000000;
            }

            // 16 three-bit alpha indices in bytes 2..7 (little-endian, pixel 0 lowest), then the two-bit colour indices.
            const uint64_t alphaIndices = uint64_t(Load32(source + 2)) | uint64_t(Load16(source + 6)) << 32;
            for (int pixel = 0; pixel < 16; ++pixel) {
                const uint32_t a = alpha[(alphaIndices >> (3 * pixel)) & 7];
                const uint32_t colour = palette[(indices >> (2 * pixel)) & 3];
                Store32(target + (pixel >> 2) * targetPitch + (pixel & 3) * 4, a + colour);
            }
            target += 16;
            source += 16;
        }
        source += sourcePitch - blocksWide * 16;
        target += (targetPitch - blocksWide * 4) * 4;
    }
}

} // namespace nGZGraphic4::dxt
