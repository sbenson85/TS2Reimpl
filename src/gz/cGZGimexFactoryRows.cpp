// cGZGimexFactory's row converters (lane LX; speed-built in the game, so not a size file). Declared in
// src/gz/cGZGimexFactory.h. A 32-bit pixel is B, G, R, A from its low byte up.
#include "gz/cGZGimexFactory.h"

namespace gimexrows {

namespace {

// A 5- or 6-bit field scaled to 0..255, rounding down.
uint32_t Scale5(uint32_t field)
{
    return field * 0xff / 31;
}

uint32_t Scale6(uint32_t field)
{
    return field * 0xff / 63;
}

} // namespace

void __fastcall To1555Opaque(const uint32_t *source, uint16_t *destination, uint32_t count)
{
    for (; count != 0; --count) {
        const uint32_t v = *source++;
        *destination++ = static_cast<uint16_t>((v >> 9 & 0x7c00) | (v >> 6 & 0x3e0) | (v >> 3 & 0x1f) | 0x8000);
    }
}

void __fastcall To565(const uint32_t *source, uint16_t *destination, uint32_t count)
{
    for (; count != 0; --count) {
        const uint32_t v = *source++;
        *destination++ = static_cast<uint16_t>((v >> 8 & 0xf800) | (v >> 6 & 0x3e0) | (v >> 3 & 0x1f));
    }
}

void __fastcall To1555(const uint32_t *source, uint16_t *destination, uint32_t count)
{
    for (; count != 0; --count) {
        const uint32_t v = *source++;
        *destination++ =
            static_cast<uint16_t>((v >> 9 & 0x7c00) | (v >> 6 & 0x3e0) | (v >> 3 & 0x1f) | (v >> 16 & 0x8000));
    }
}

void __fastcall To4444(const uint32_t *source, uint16_t *destination, uint32_t count)
{
    for (; count != 0; --count) {
        const uint32_t v = *source++;
        *destination++ = static_cast<uint16_t>((v >> 16 & 0xf000) | (v >> 12 & 0xf00) | (v >> 8 & 0xf0) | (v >> 4 & 0xf));
    }
}

void __fastcall Copy32(const uint32_t *source, uint32_t *destination, uint32_t count)
{
    for (; count != 0; --count) *destination++ = *source++;
}

void __fastcall From1555Opaque(const uint16_t *source, uint32_t *destination, uint32_t count)
{
    for (; count != 0; --count) {
        const uint32_t v = *source++;
        *destination++ = 0xff000000u | Scale5(v >> 10 & 0x1f) << 16 | Scale5(v >> 5 & 0x1f) << 8 | Scale5(v & 0x1f);
    }
}

void __fastcall From565(const uint16_t *source, uint32_t *destination, uint32_t count)
{
    for (; count != 0; --count) {
        const uint32_t v = *source++;
        *destination++ = 0xff000000u | Scale5(v >> 11) << 16 | Scale6(v >> 5 & 0x3f) << 8 | Scale5(v & 0x1f);
    }
}

void __fastcall From1555(const uint16_t *source, uint32_t *destination, uint32_t count)
{
    for (; count != 0; --count) {
        const uint32_t v = *source++;
        *destination++ = (v >> 15) * 0xff000000u | Scale5(v >> 10 & 0x1f) << 16 | Scale5(v >> 5 & 0x1f) << 8 |
                         Scale5(v & 0x1f);
    }
}

void __fastcall From4444(const uint16_t *source, uint32_t *destination, uint32_t count)
{
    for (; count != 0; --count) {
        const uint32_t v = *source++;
        // ORIGINAL-BUG: not a conversion (cGZGimexFactory.h, 0x87c6f8).
        *destination++ = (v >> 4 & 0xf00) | (v & 0xf0);
    }
}

} // namespace gimexrows
