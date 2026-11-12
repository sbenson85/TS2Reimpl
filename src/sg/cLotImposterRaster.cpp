// The lot imposter's raster helpers (lane 261111q; sg/cLotImposterRaster.h).
#include "sg/cLotImposterRaster.h"

#include "sg/cImageData.h"

namespace nTSSG {

using nGZSceneGraph::cIImageData;

namespace {

constexpr int32_t kTopLevel = 0;

// One run along an axis of `size` pixels. `across` is the coordinate the run stays on, `point[along]` where it starts;
// `vertical` runs paint (across, r), horizontal ones (r, across).
// ORIGINAL-BUG: both coordinates are taken modulo (and checked against) the one size the caller read: the height for a
// column, the width for a row. The imposter textures are square, so it never shows.
void PaintRun(cIImageData *image, int32_t size, int32_t across, const int32_t *point, int along, int32_t length,
              const float *colour, bool wrap, bool vertical)
{
    const auto paint = [&](int32_t fixed, int32_t r) {
        if (vertical)
            image->SetPixel(fixed, r, colour, kTopLevel);
        else
            image->SetPixel(r, fixed, colour, kTopLevel);
    };
    if (wrap) {
        int32_t fixed = across % size;
        if (fixed < 0) fixed += size;
        const int32_t span = length < 0 ? -length : length;
        if (span >= size) {
            for (int32_t r = 0; r < size; ++r) paint(fixed, r);
            return;
        }
        int32_t base = point[along];
        if (length <= 0) base += 1 + length;
        int32_t start = base % size;
        if (start < 0) start += size;
        const int32_t end = span + start;
        if (end <= size) {
            for (int32_t r = start; r < end; ++r) paint(fixed, r);
            return;
        }
        for (int32_t r = start; r < size; ++r) paint(fixed, r);
        const int32_t rest = span - size + start;
        for (int32_t r = 0; r < rest; ++r) paint(fixed, r);
        return;
    }
    if (across < 0 || across >= size) return;
    const int32_t from = point[along];
    int32_t first = from < 0 ? 0 : from;
    const int32_t stop = length + first;
    int32_t last = (size - 1 < stop - 1 ? size : stop) - 1;
    if (length < 0) {
        first = stop + 1;
        const int32_t end = first - length;
        last = (size <= end - 1 ? size : end) - 1;
    }
    for (int32_t r = first; r <= last; ++r) paint(across, r);
}

// Scales the pixel at (x, y) by `scale`, through the image's own read.
void ScalePixel(cIImageData *image, int32_t x, int32_t y, float scale)
{
    float read[4];
    const float *const pixel = image->GetPixel(read, x, y, kTopLevel);
    float scaled[4];
    for (int k = 0; k < 4; ++k) scaled[k] = scale * pixel[k];
    image->SetPixel(x, y, scaled, kTopLevel);
}

} // namespace

void __cdecl PaintColumn(cIImageData *image, const int32_t *point, int32_t length, const float *colour, bool wrap)
{
    const int32_t size = image->LevelHeight(kTopLevel);
    PaintRun(image, size, point[0], point, 1, length, colour, wrap, true);
}

void __cdecl PaintRow(cIImageData *image, const int32_t *point, int32_t length, const float *colour, bool wrap)
{
    const int32_t size = image->LevelWidth(kTopLevel);
    PaintRun(image, size, point[1], point, 0, length, colour, wrap, false);
}

void __cdecl PaintRect(cIImageData *image, const int32_t *rect, const float *colour, bool wrap)
{
    if (rect[2] - rect[0] >= rect[3] - rect[1]) {
        const int32_t low = rect[1] <= rect[3] ? rect[1] : rect[3];
        const int32_t high = rect[1] <= rect[3] ? rect[3] : rect[1];
        for (int32_t y = low; y <= high; ++y) {
            const int32_t point[2] = {rect[0], y};
            PaintRow(image, point, rect[2] - rect[0] + 1, colour, wrap);
        }
        return;
    }
    const int32_t low = rect[0] <= rect[2] ? rect[0] : rect[2];
    const int32_t high = rect[0] <= rect[2] ? rect[2] : rect[0];
    for (int32_t x = low; x <= high; ++x) {
        const int32_t point[2] = {x, rect[1]};
        PaintColumn(image, point, rect[3] - rect[1] + 1, colour, wrap);
    }
}

void __cdecl PaintFrame(cIImageData *image, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t thickness,
                        const float *colour, bool wrap)
{
    if (thickness == 0) return;
    int32_t a = thickness;
    if (thickness < 0) {
        a = -thickness;
        if (x1 - x0 <= 2 * a || y1 - y0 <= 2 * a) {
            const int32_t whole[4] = {x0, y0, x1, y1};
            PaintRect(image, whole, colour, wrap);
            return;
        }
        x0 += a;
        x1 -= a;
        y0 += a;
        y1 -= a;
    }
    const int32_t top[4] = {x0 - a, y0 - a, x1 + a, y0 - 1};
    PaintRect(image, top, colour, wrap);
    const int32_t bottom[4] = {x0 - a, y1 + 1, x1 + a, y1 + a};
    PaintRect(image, bottom, colour, wrap);
    const int32_t left[4] = {x0 - a, y0, x0 - 1, y1};
    PaintRect(image, left, colour, wrap);
    const int32_t right[4] = {x1 + 1, y0, x1 + a, y1};
    PaintRect(image, right, colour, wrap);
}

void __cdecl FadeBorder(int32_t rings, cIImageData *image, int32_t width, int32_t height)
{
    if (!image || rings <= 0) return;
    const float count = static_cast<float>(rings);
    for (int32_t i = 0; i < rings; ++i) {
        const float scale = static_cast<float>(i) / count;
        const int32_t top = image->LevelHeight(kTopLevel) - height + i;
        const int32_t bottom = image->LevelHeight(kTopLevel) - i - 1;
        const int32_t right = width - 1 - i;
        for (int32_t x = i; x <= right; ++x) {
            ScalePixel(image, x, top, scale);
            ScalePixel(image, x, bottom, scale);
        }
        for (int32_t y = top + 1; y < bottom; ++y) {
            ScalePixel(image, i, y, scale);
            ScalePixel(image, right, y, scale);
        }
    }
}

} // namespace nTSSG
