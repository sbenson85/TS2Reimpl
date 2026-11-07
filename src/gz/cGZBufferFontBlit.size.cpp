#include "gz/cGZBufferFontBlit.h"

#include "game/game.h"
#include "gz/cIGZBuffer.h"

namespace {

constexpr uint32_t kSize = 32; // the table's three dimensions (5-bit components, alpha / 8)

uint8_t *Table() { return GameAddress<uint8_t>(cGZBufferFontRendererCode::kBlendTable); }

// Entry [colour][destination][alpha].
uint32_t Mix(uint32_t colour, uint32_t destination, uint32_t alpha)
{
    return Table()[(colour * kSize + destination) * kSize + alpha];
}

// The 1555 blend of formats 4 and 6 (the top bit left 0).
uint16_t Blend1555(uint32_t colour, uint32_t destination, uint32_t alpha)
{
    const uint32_t red = Mix(colour >> 10 & 0x1f, destination >> 10 & 0x1f, alpha);
    const uint32_t green = Mix(colour >> 5 & 0x1f, destination >> 5 & 0x1f, alpha);
    const uint32_t blue = Mix(colour & 0x1f, destination & 0x1f, alpha);
    return static_cast<uint16_t>((red << 5 | green) << 5 | blue);
}

// A 32-bit pixel: colour over destination at coverage 0..0xff, two channels at a time.
uint32_t Blend8888(uint32_t colour, uint32_t destination, uint32_t coverage)
{
    const uint32_t keep = 0x100 - coverage;
    const uint32_t redBlue =
        ((destination & 0xff00ff) * keep + coverage * (colour & 0xff00ff) + 0x800080) >> 8 & 0xff00ff;
    const uint32_t alphaGreen =
        (coverage * (colour >> 8 & 0xff00ff) + 0x800080 + (destination >> 8 & 0xff00ff) * keep) & 0xff00ff00;
    return redBlue + alphaGreen;
}

// One row of a 16-bit buffer.
void Row16(uint16_t *out, uint16_t *end, const uint8_t *src, uint32_t mode, uint32_t format, uint16_t colour,
           const uint32_t *colours)
{
    if (mode == 1) {
        for (; out < end; ++out, ++src)
            if (*src > 0x7f) *out = colour;
    } else if (mode == 2) {
        for (; out < end; ++out, ++src)
            if (*src != 0) *out = static_cast<uint16_t>(colours[*src]);
    } else if (format == 4 || format == 6) {
        for (; out < end; ++out, ++src)
            if (*src != 0) *out = Blend1555(colour, *out, *src >> 3);
    } else if (format == 5) {
        for (; out < end; ++out, ++src)
            if (*src != 0) *out = cGZBufferFontRendererCode::Blend565(colour, *out, *src >> 3);
    } else if (format == 7) {
        for (; out < end; ++out, ++src)
            if (*src != 0) *out = cGZBufferFontRendererCode::Blend4444(colour, *out, *src >> 3);
    }
}

// One row of a 24 / 32-bit buffer.
void Row32(uint32_t *out, uint32_t *end, const uint8_t *src, uint32_t mode, uint32_t colour, const uint32_t *colours)
{
    if (mode == 1) {
        for (; out < end; ++out, ++src)
            if (*src > 0x7f) *out = colour;
    } else if (mode == 2) {
        for (; out < end; ++out, ++src)
            if (*src != 0) *out = colours[*src];
    } else {
        for (; out < end; ++out, ++src) *out = Blend8888(colour, *out, *src);
    }
}

} // namespace

cGZBufferFontRenderer *__fastcall cGZBufferFontRendererCode::Construct(cGZBufferFontRenderer *self, void *)
{
    self->mRefCount = 0;
    self->mVTable = GameAddress<void>(kVTable);
    uint8_t *built = GameAddress<uint8_t>(kTableBuilt);
    if (*built == 0) {
        BuildBlendTable();
        *built = 1;
    }
    return self;
}

bool __cdecl cGZBufferFontRendererCode::BuildBlendTable()
{
    uint8_t *entry = Table();
    for (uint32_t colour = 0; colour < kSize; ++colour)
        for (uint32_t destination = 0; destination < kSize; ++destination)
            for (uint32_t alpha = 0; alpha < kSize; ++alpha) {
                const float weight = static_cast<float>(static_cast<int32_t>(alpha)) * 0.03125f;
                const float mixed = (1.0f - weight) * static_cast<float>(static_cast<int32_t>(destination)) +
                                    static_cast<float>(static_cast<int32_t>(colour)) * weight;
                *entry++ = static_cast<uint8_t>(static_cast<int32_t>(mixed));
            }
    return true;
}

bool cGZBufferFontRendererCode::Blit(uint32_t, cIGZBuffer *buffer, const int32_t *dest, uint32_t, const uint8_t *pixels,
                                     const int32_t *srcRect, int32_t pitch, uint32_t mode, const uint32_t *colours,
                                     const int32_t *clip)
{
    const int32_t width = static_cast<int32_t>(buffer->GetWidth());
    const int32_t height = static_cast<int32_t>(buffer->GetHeight());
    int32_t srcX = srcRect[0];
    int32_t srcY = srcRect[1];
    int32_t left = dest[0];
    int32_t top = dest[1];
    int32_t right = dest[2];
    int32_t bottom = dest[3];
    // The colour type is the "display's" (3) unless mode 3 asks the buffer (only the format is read).
    cGZColorType format(3);
    const uint32_t bits = buffer->GetBitsPerPixel();
    if (mode == 3) format = buffer->GetColorType();

    if (clip) {
        if (left < clip[0]) {
            const int32_t cut = clip[0] - left;
            srcX += cut;
            left += cut;
        }
        if (top < clip[1]) {
            const int32_t cut = clip[1] - top;
            srcY += cut;
            top += cut;
        }
        if (clip[2] < right) right = clip[2];
        if (clip[3] < bottom) bottom = clip[3];
    }
    if (left < 0) {
        srcX -= left;
        left = 0;
    }
    if (top < 0) {
        srcY -= top;
        top = 0;
    }
    if (width < right) right = width;
    if (height < bottom) bottom = height;
    if (left >= right || top >= bottom) return true;

    const uint8_t *srcRow = pixels + srcY * pitch + srcX;
    if (bits == 16) {
        const uint16_t colour = static_cast<uint16_t>(colours[0]);
        const uint32_t stride = buffer->GetColorSurfaceStride() >> 1;
        uint16_t *row = static_cast<uint16_t *>(buffer->GetColorSurfaceBits()) + (stride * top + left);
        uint16_t *const last = row + (bottom - top) * stride;
        for (; row < last; row += stride, srcRow += pitch)
            Row16(row, row + (right - left), srcRow, mode, format.format, colour, colours);
    } else if (bits == 32 || bits == 24) {
        const uint32_t colour = colours[0];
        const uint32_t stride = buffer->GetColorSurfaceStride() >> 2;
        uint32_t *row = static_cast<uint32_t *>(buffer->GetColorSurfaceBits()) + (stride * top + left);
        uint32_t *const last = row + (bottom - top) * stride;
        for (; row < last; row += stride, srcRow += pitch) Row32(row, row + (right - left), srcRow, mode, colour, colours);
    }
    return true;
}

uint16_t __cdecl cGZBufferFontRendererCode::Blend565(uint32_t colour, uint32_t destination, uint32_t alpha)
{
    // Green's low bit is dropped (5 of its 6 bits, put back one place up).
    const uint32_t red = Mix(colour >> 11, destination >> 11, alpha);
    const uint32_t green = Mix(colour >> 6 & 0x1f, destination >> 6 & 0x1f, alpha);
    const uint32_t blue = Mix(colour & 0x1f, destination & 0x1f, alpha);
    return static_cast<uint16_t>((red << 5 | green) << 6 | blue);
}

uint16_t __cdecl cGZBufferFontRendererCode::Blend4444(uint32_t colour, uint32_t destination, uint32_t alpha)
{
    // Alpha comes out 0xf.
    const uint32_t red = Mix(colour >> 8 & 0xf, destination >> 8 & 0xf, alpha);
    const uint32_t green = Mix(colour >> 4 & 0xf, destination >> 4 & 0xf, alpha);
    const uint32_t blue = Mix(colour & 0xf, destination & 0xf, alpha);
    return static_cast<uint16_t>(((green | (red | 0xf0) << 4) << 4) | blue);
}
