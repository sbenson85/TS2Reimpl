#pragma once
// The lot imposter's raster helpers (lane 261111q, scenegraph row LT2b2): free __cdecl functions the generation path
// (0x363b90, 0x365670, 0x366180, 0x369460) uses to paint lines, rectangles and frames into a texture's top level and
// to darken its border. Pixels go through cIImageData::SetPixel (slot 20) at level 0; sizes come from level 0.
//
// A run is a point and a signed length: a positive length paints forward from the point, a negative one paints the
// |length| pixels that end at the point. With `wrap` the coordinates wrap around the level (a run as long as the level
// paints all of it); without it pixels outside the level are skipped.
#include <cstdint>

namespace nGZSceneGraph {
class cIImageData;
}

namespace nTSSG {

// 0x3637d0: a vertical run at column point[0] from row point[1].
void __cdecl PaintColumn(nGZSceneGraph::cIImageData *image, const int32_t *point, int32_t length, const float *colour,
                         bool wrap);
// 0x363a10: a horizontal run on row point[1] from column point[0].
void __cdecl PaintRow(nGZSceneGraph::cIImageData *image, const int32_t *point, int32_t length, const float *colour,
                      bool wrap);
// 0x363950: the rectangle {x0, y0, x1, y1} (inclusive, either corner order), as rows when it is at least as wide as
// tall, else as columns. The rectangle is read again for each run.
void __cdecl PaintRect(nGZSceneGraph::cIImageData *image, const int32_t *rect, const float *colour, bool wrap);
// 0x363680: a frame `thickness` wide around {x0, y0, x1, y1} (outside it); a negative thickness puts the frame inside,
// and a rectangle too small for that is filled.
void __cdecl PaintFrame(nGZSceneGraph::cIImageData *image, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        int32_t thickness, const float *colour, bool wrap);
// 0x363490: darkens `rings` rings of the region of columns 0..width-1 and the level's last `height` rows; ring i (from
// the outside) is scaled by i / rings (the outermost goes black). Each pixel is read (GetPixel, slot 39) and written.
void __cdecl FadeBorder(int32_t rings, nGZSceneGraph::cIImageData *image, int32_t width, int32_t height);

} // namespace nTSSG
