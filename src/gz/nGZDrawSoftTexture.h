#pragma once
// The texture slot of the software span renderers (vf5 of cSpanRender8 / 1555 / 565 / 4444 / 8888;
// docs/subsystems/rendering.md, R5b step 1). The original's slot picks a function from a table by the texture's
// format and key (params.formatIndex * 2 + hasKey) and the mode (0..14); every renderer has 14 x 15 such span
// functions (about 900 distinct bodies). One template per destination pixel format covers them: the texel is
// decoded to ARGB8888 (src formats below), then the mode's operation runs on the destination pixel.
//   modes 0..4 (plain: texel at the span's own coordinates), +5 scaled (TexelSpans from ScaledSpans), +10 tiled
//   (TexelSpans from TiledSpans); 0 copy, 1 blend by the colour's alpha, 2 modulate by the colour, 3 modulate and
//   blend by the modulated alpha, 4 modulate and blend premultiplied (the engine's flag 0x42).
#include <cstdint>

#include "gz/nGZDrawSoftRender.h"

namespace nGZDrawSoftRender {

enum class DestPixel { Palette8, Argb1555, Rgb565, Argb4444, Argb8888 };

void DrawTextureSpans(DestPixel destFormat, const void *spans, uint32_t count, const cGZDrawSurface *dest,
                      const cGZDrawSurface *texture, const TextureParams *params, uint32_t mode);

} // namespace nGZDrawSoftRender
