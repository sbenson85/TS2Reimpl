#pragma once
#include "misc/ClassesRound4.h"

#include <cstdint>

class cIGZBuffer;

// cGZBufferFontRenderer's code outside its slots (the slots: src/misc/ClassesRound4.h): the constructor, the glyph
// blitter DrawGlyphs calls, the blend table and the two 16-bit blends. RVAs 1.18.0.187. Lane LS.
//
// The blend table (game global 0x10ecd10, 32 x 32 x 32 bytes, built once: the flag at 0x10ecd00): entry
// [colour][destination][alpha] is (1 - alpha / 32) * destination + colour * alpha / 32, truncated (5-bit components;
// every product is exact in float). The game's data, so the table is the one the original code reads too.
//
// The blitter draws an 8-bit coverage bitmap (a glyph) into a buffer:
//   destination rectangle `dest` (left, top, right, bottom; right / bottom exclusive), the source origin srcRect's
//   left / top (its right / bottom are not read), the bitmap `pixels` with `pitch` bytes a row;
//   clipped to `clip` (optional) and then to the buffer (left / top below 0 move the source origin);
//   mode 1: a pixel of coverage over 0x7f takes colours[0]; mode 2: a nonzero pixel takes colours[coverage] (a
//   palette); mode 3: blends colours[0] over the destination by the coverage. Other modes draw nothing on 16-bit
//   buffers, and blend as mode 3 on 24 / 32-bit ones (which the original treats alike: 4 bytes a pixel).
//   16-bit blends go by the buffer's colour type (slot 14, read only in mode 3): 4 and 6 (1555, the top bit left 0)
//   through the table inline, 5 (565: green's low bit dropped) and 7 (4444: alpha forced to 0xf) through the helpers;
//   other types draw nothing. 32-bit blends are the usual 0xff00ff pair arithmetic with alpha 0..0xff (0x100 - a).
// Buffer slots called, in order: GetWidth, GetHeight, GetBitsPerPixel, (mode 3) GetColorType, then (when anything is
// left to draw at 16, 24 or 32 bits) GetColorSurfaceStride and GetColorSurfaceBits. Returns true.
struct cGZBufferFontRendererCode {
    static constexpr uint32_t kVTable = 0xe508f8;      // the game's (cGZBufferFontRenderer is hook-shaped)
    static constexpr uint32_t kTableBuilt = 0x10ecd00; // byte
    static constexpr uint32_t kBlendTable = 0x10ecd10; // 0x8000 bytes

    // 0x90a7b5 (thiscall, returns this): the count 0, the game's vtable, the table built on first use.
    static cGZBufferFontRenderer *__fastcall Construct(cGZBufferFontRenderer *self, void *);
    // 0x90e1aa: fills the table; returns true.
    static bool __cdecl BuildBlendTable();
    // 0x90bbba (thiscall on the renderer, ret 0x28; the renderer itself is not read, so `this` is the renderer seen as
    // this code struct, which has no members).
    bool Blit(uint32_t unused, cIGZBuffer *buffer, const int32_t *dest, uint32_t code, const uint8_t *pixels,
              const int32_t *srcRect, int32_t pitch, uint32_t mode, const uint32_t *colours, const int32_t *clip);
    // 0x90c7ba / 0x90c82b (cdecl): one 565 / 4444 pixel, colour over destination at alpha 0..31.
    static uint16_t __cdecl Blend565(uint32_t colour, uint32_t destination, uint32_t alpha);
    static uint16_t __cdecl Blend4444(uint32_t colour, uint32_t destination, uint32_t alpha);
};
