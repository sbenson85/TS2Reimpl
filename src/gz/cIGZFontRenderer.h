#pragma once
// cIGZFontRenderer (id 0xa8bc2a75, 8 slots): what draws a run of rasterised glyphs into a target buffer. Implemented by
// cGZBufferFontRenderer (vtable 0xe508f8: blits into a lockable pixel buffer; src/misc/ClassesRound4.h) and
// cGZDrawFontRenderer (vtable 0xe4f09c: draws through a draw context from a glyph atlas texture; src/gz/
// cGZDrawFontRenderer.h). Lane NM (docs/subsystems/rendering.md, R5). Slot names are tentative.
#include <cstdint>

#include "gz/cIGZBuffer.h"
#include "gz/cIGZUnknown.h"

// One rasterised glyph (0x24 bytes): where it goes, and its 8-bit coverage bitmap.
struct cGZFontGlyph {
    cGZRect rect;          // +0 the destination (right / bottom exclusive)
    const uint8_t *pixels; // +0x10 the bitmap's first row
    int32_t pitch;         // +0x14 bytes a bitmap row
    int32_t width;         // +0x18
    int32_t height;        // +0x1c
    uint32_t code;         // +0x20 the character
};
static_assert(sizeof(cGZFontGlyph) == 0x24, "cGZFontGlyph: 0x24 bytes");

class cIGZFontRenderer : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xa8bc2a75;

    virtual bool vf3(uint32_t font) = 0;         // slot 3 (both implementations: true)
    virtual bool ReleaseFont(uint32_t font) = 0; // slot 4: forget what is cached for the font
    // Slots 5 and 6 bracket the draws into `target`; slot 7 draws `count` glyphs of `font` in colours[0] (as the
    // target's native value), clipped to `clip` when given. `mode` and the third arguments are the buffer renderer's.
    virtual bool BeginDraw(uint32_t font, cIGZBuffer *target, uint32_t mode) = 0;
    virtual bool EndDraw(uint32_t font, cIGZBuffer *target, uint32_t mode) = 0;
    virtual bool DrawGlyphs(uint32_t font, cIGZBuffer *target, cGZFontGlyph *glyphs, uint32_t count, uint32_t mode,
                            const uint32_t *colours, const cGZRect *clip) = 0;
};
