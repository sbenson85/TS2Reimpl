// The primitive builders of cGZDrawContext (docs/subsystems/rendering.md, R5b step 2). Dispatch (cGZDrawContext.cpp) asks
// the engine for its route for a kind of primitive and calls one of these: route 1 builds quads and triangles for the
// engine to draw itself, route 2 builds spans (DrawSpans: word pairs of row << 16 | x, the end exclusive). The geometry is
// float arithmetic in the order the game does it (each operation rounded to float, or to double where the game does),
// because the engine's calls and the vectors' contents are what the differential test compares.
#include "gz/cGZDrawContext.h"

#include "gz/GZScanConvert.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <emmintrin.h>
#include <stdexcept>
#include <vector>

namespace {

// The original's float-to-int (cvttsd2si / cvttss2si, _ftoi2): truncation, 0x80000000 when it does not fit or is NaN.
int32_t ToInt(double value) { return _mm_cvttsd_si32(_mm_set_sd(value)); }
int32_t ToInt(float value) { return _mm_cvttss_si32(_mm_set_ss(value)); }

// ceil on the double, stored back to float, then truncated (the game's ceil call and its float store).
int32_t CeilToInt(float value) { return ToInt(static_cast<float>(std::ceil(static_cast<double>(value)))); }

// floor(value * 256 + 0.5) as an int: a coordinate in 1/256 pixels.
int32_t Subpixels(float value) { return ToInt(std::floor(static_cast<double>(value) * 256.0 + 0.5)); }

// The number of segments a curve of the given half-size needs (at least 1): ceil(sqrt(size)) when the size is over 1.
int32_t Segments(float size)
{
    if (!(size > 1.0f)) return 1;
    const float root = static_cast<float>(std::sqrt(static_cast<double>(size)));
    return CeilToInt(root);
}

constexpr double kFullCircle = 6.28318; // the game's constant, not 2 * pi
const float kBezierCircle = std::bit_cast<float>(0x3f0d6289u); // 0.5522848 (the control point distance of a quarter circle)

// Half a width and the centre of a span of two floats: |a - b| * 0.5 and (a + b) * 0.5.
float HalfDistance(float a, float b) { return std::fabs(a - b) * 0.5f; }
float Middle(float a, float b) { return (a + b) * 0.5f; }

int32_t Max(int32_t a, int32_t b) { return a >= b ? a : b; }
int32_t Min(int32_t a, int32_t b) { return a <= b ? a : b; }

// A row of polygon spans cut to the clip (left, top, right, bottom as the context's sent clip).
void AddClippedRows(std::vector<int32_t> &spans, const std::vector<PolygonSpan> &rows, const cGZRect &clip)
{
    for (const PolygonSpan &row : rows) {
        const int32_t y = row.y;
        if (y < clip.top) continue;
        if (y >= clip.bottom) break;
        const int32_t left = Max(clip.left, row.x0); // the clip as the larger left (cmovge)
        const int32_t right = clip.right <= row.x1 ? clip.right : row.x1;
        if (left < right) {
            const uint32_t base = static_cast<uint32_t>(y) << 16;
            spans.push_back(static_cast<int32_t>(base + static_cast<uint32_t>(left)));
            spans.push_back(static_cast<int32_t>(base + static_cast<uint32_t>(right)));
        }
    }
}

} // namespace

// ---- kind 0: points ----

void cGZDrawContext::BuildPoints(const float *points, int32_t count) // 0x8bc290
{
    switch (mEngine.p->ChooseRoute(0, points, count)) {
    case 1: {
        // Each point as the quad of the pixel around it.
        mQuads.resize(static_cast<uint32_t>(count) * 8);
        float *quad = mQuads.data();
        const float *const limit = quad + mQuads.size();
        for (int32_t i = 0; i < count; ++i, points += 2, quad += 12) {
            const float x = points[0], y = points[1];
            const float left = x - 0.5f, top = y - 0.5f, right = x + 0.5f, bottom = y + 0.5f;
            const float quadValues[8] = {left, top, right, top, right, bottom, left, bottom};
            // ORIGINAL-BUG: the game sizes the vector for 8 floats a point but steps 12 floats a point, so from the second
            // point on it writes past the vector's end (a heap overrun). Ours leaves out the writes past the end; the
            // floats inside are the same (the engine is handed only the vector's own range).
            for (int k = 0; k < 8; ++k)
                if (quad + k < limit) quad[k] = quadValues[k];
        }
        SwapQuads();
        DrawQuadList(mQuadsOut.data(), static_cast<int32_t>(mQuadsOut.size() >> 1));
        return;
    }
    case 2: {
        // Each point as the rectangle of its pixel, a rectangle list for kind 3.
        std::vector<float> rects(static_cast<size_t>(static_cast<uint32_t>(count)) * 4);
        float *rect = rects.data();
        for (int32_t i = 0; i < count; ++i, points += 2, rect += 4) {
            const float x = points[0], y = points[1];
            rect[0] = x - 0.5f;
            rect[1] = y - 0.5f;
            rect[2] = x + 0.5f;
            rect[3] = y + 0.5f;
        }
        Dispatch(3, rects.data(), count * 2);
        return;
    }
    }
}

// ---- kind 1: lines ----

void cGZDrawContext::AppendLineQuad(const float *line) // 0x8bc8f0
{
    mQuads.resize(mQuads.size() + 8);
    float *const quad = mQuads.data() + mQuads.size() - 8;
    const float x0 = line[0];
    const float dx = line[2] - x0;
    const float dy = line[3] - line[1];
    if (mState.byte0 != 0) {
        // Antialiased: the quad is as wide as the line, perpendicular to it.
        const float lengthSquared = dy * dy + dx * dx;
        if (lengthSquared > 1e-08f) {
            const float half = mState.lineWidth * 0.5f;
            const float root = static_cast<float>(std::sqrt(static_cast<double>(lengthSquared)));
            const float scale = half / root;
            const float sy = scale * dy;
            const float sx = scale * dx;
            quad[0] = x0 + sy;
            quad[1] = line[1] - sx;
            quad[2] = line[0] - sy;
            quad[3] = sx + line[1];
            quad[4] = line[2] - sy;
            quad[5] = sx + line[3];
            quad[6] = sy + line[2];
            quad[7] = line[3] - sx;
        }
        return; // (a line of no length leaves its quad zero)
    }
    float half = static_cast<float>(mState.lineWidthRounded) * 0.5f;
    float cap = 0.5f;
    if (std::fabs(dx) >= std::fabs(dy)) {
        // Mostly horizontal.
        if (!(dx > 0.0f)) {
            half = -half;
            cap = -0.5f;
        }
        quad[0] = x0 - cap;
        quad[1] = line[1] - half;
        quad[2] = line[0] - cap;
        quad[3] = half + line[1];
        quad[4] = cap + line[2];
        quad[5] = half + line[3];
        quad[6] = cap + line[2];
        quad[7] = line[3] - half;
    } else {
        if (!(dy > 0.0f)) {
            half = -half;
            cap = -0.5f;
        }
        quad[0] = x0 + half;
        quad[1] = line[1] - cap;
        quad[2] = line[0] - half;
        quad[3] = line[1] - cap;
        quad[4] = line[2] - half;
        quad[5] = cap + line[3];
        quad[6] = half + line[2];
        quad[7] = cap + line[3];
    }
}

void cGZDrawContext::BuildLines(const float *points, int32_t count) // 0x8bc5f0
{
    const uint32_t route = mEngine.p->ChooseRoute(1, points, count);
    if (route != 1 && route != 2) return;
    int32_t lines = count >> 1;
    if (route == 1) {
        const uint32_t floats = static_cast<uint32_t>(lines) * 12;
        if (floats > mQuads.capacity()) {
            if (floats > 0x3fffffff) throw std::length_error("vector too long");
            mQuads.reserve(floats);
        }
        mQuads.clear();
        for (; lines > 0; --lines, points += 4) AppendLineQuad(points);
        SwapQuads();
        DrawQuadList(mQuadsOut.data(), static_cast<int32_t>(mQuadsOut.size() >> 1));
        return;
    }
    mSpans.clear();
    for (; lines > 0; --lines, points += 4)
        BuildLineSpans(static_cast<uint32_t>(Subpixels(points[0])), static_cast<uint32_t>(Subpixels(points[1])),
                       static_cast<uint32_t>(Subpixels(points[2])), static_cast<uint32_t>(Subpixels(points[3])));
    if (!mSpans.empty()) mEngine.p->DrawSpans(mSpans.data(), static_cast<uint32_t>(mSpans.size() >> 1));
}

// One line of the span builder, in 1/256 pixels (x0, y0, x1, y1; the low bit set on each, the line walked along its longer
// axis with an error term, clipped to the sent clip on both axes). A width over one pixel makes each step cover that many
// pixels across the line.
void cGZDrawContext::BuildLineSpans(uint32_t param_1, uint32_t param_2, uint32_t param_3, uint32_t param_4) // 0x8bccd0
{
    const int32_t clipLeft = mSentClip.left, clipTop = mSentClip.top, clipRight = mSentClip.right,
                  clipBottom = mSentClip.bottom;
    auto absolute = [](uint32_t v) {
        const uint32_t sign = static_cast<uint32_t>(static_cast<int32_t>(v) >> 31);
        return static_cast<int32_t>((v ^ sign) - sign);
    };
    auto push = [this](int32_t value) { mSpans.push_back(value); };
    // The distance of a subpixel position from the middle of its pixel, both coordinates summed (the corner test).
    auto cornerFar = [&](uint32_t a, uint32_t b) {
        return absolute((a & 0xff) - 0x80) + absolute((b & 0xff) - 0x80) >= 0x81;
    };

    const int32_t iVar5 = absolute((param_3 | 1) - (param_1 | 1)); // |dx|
    const int32_t local_4 = absolute((param_4 | 1) - (param_2 | 1)); // |dy|
    int32_t local_34 = local_4; // the shorter axis
    int32_t local_10 = iVar5;   // the longer
    if (iVar5 < local_4) {
        local_34 = iVar5;
        local_10 = local_4;
    }
    int32_t iVar7 = clipRight - 1;
    int32_t iVar9 = clipBottom - 1;
    int32_t iVar14 = iVar7;
    int32_t iVar13 = clipLeft;
    if (static_cast<int32_t>(param_1 | 1) <= static_cast<int32_t>(param_3 | 1)) {
        iVar14 = clipLeft;
        iVar13 = iVar7;
    }
    int32_t iVar10 = iVar9;
    iVar7 = clipTop;
    if (static_cast<int32_t>(param_2 | 1) <= static_cast<int32_t>(param_4 | 1)) {
        iVar10 = clipTop;
        iVar7 = iVar9;
    }
    uint32_t uVar4 = param_2;
    int32_t local_c = iVar14;
    iVar9 = iVar13;
    if (local_4 <= iVar5) {
        uVar4 = param_1;
        local_c = iVar10;
        iVar9 = iVar7;
        iVar10 = iVar14;
        iVar7 = iVar13;
    }
    if (local_4 <= iVar5) param_1 = param_2;
    uint32_t uVar8 = param_4;
    if (local_4 <= iVar5) uVar8 = param_3;
    if (local_4 <= iVar5) param_3 = param_4;
    uint32_t local_30 = param_1 | 1;
    iVar13 = iVar9;
    param_2 = uVar8 | 1;
    uint32_t local_2c = param_3 | 1;
    int32_t local_20 = iVar10;
    int32_t local_1c = iVar7;
    uint32_t local_14 = uVar4 | 1;
    if (iVar7 < iVar10) {
        local_30 = param_3 | 1;
        iVar13 = local_c;
        param_2 = uVar4 | 1;
        local_2c = param_1 | 1;
        local_20 = iVar7;
        local_1c = iVar10;
        local_14 = uVar8 | 1;
        local_c = iVar9;
    }
    iVar14 = mState.lineWidthRounded;
    if (1 < iVar14) {
        local_30 = local_30 + static_cast<uint32_t>(iVar14) * static_cast<uint32_t>(-0x80) + 0x80;
        local_2c = local_2c + static_cast<uint32_t>(iVar14) * static_cast<uint32_t>(-0x80) + 0x80;
    }
    int32_t local_24 = static_cast<int32_t>(local_14) >> 8;
    iVar9 = static_cast<int32_t>(local_30) >> 8;
    iVar14 = (local_c < iVar13 ? 1 : 0) * 2 - 1;
    int32_t iVar6 = static_cast<int32_t>(param_2) >> 8;
    const int32_t local_8 = static_cast<int32_t>(local_2c) >> 8;
    bool bVar2, bVar3;
    if (iVar7 < iVar10) {
        bVar2 = (local_14 & 0x80) == 0 && cornerFar(local_14, local_30);
        bVar3 = (param_2 & 0x80) != 0 || !cornerFar(param_2, local_2c);
    } else {
        bVar2 = (local_14 & 0x80) == 0 || !cornerFar(local_14, local_30);
        bVar3 = (param_2 & 0x80) != 0 && cornerFar(param_2, local_2c);
    }
    local_34 = static_cast<int32_t>(static_cast<uint32_t>(local_34) * static_cast<uint32_t>(-2));
    iVar7 = static_cast<int32_t>(static_cast<uint32_t>(local_10) * 2u);
    {
        const uint32_t a = static_cast<uint32_t>(iVar9 * 0x100 - static_cast<int32_t>(local_30) + 0x80);
        const uint32_t b = static_cast<uint32_t>(local_24 * 0x100 - static_cast<int32_t>(local_14) + 0x80);
        const uint32_t sum = a * static_cast<uint32_t>(iVar7) * static_cast<uint32_t>(iVar14) +
                             b * static_cast<uint32_t>(local_34);
        param_2 = static_cast<uint32_t>((static_cast<int32_t>(sum) >> 8) + local_10);
    }
    if (!bVar2) {
        local_24 = local_24 + 1;
        param_2 = param_2 + static_cast<uint32_t>(local_34);
        if (static_cast<int32_t>(param_2) < 0) {
            param_2 = param_2 + static_cast<uint32_t>(iVar7);
            iVar9 = iVar9 + iVar14;
        }
    }
    if (!bVar3) iVar6 = iVar6 - 1;
    if (static_cast<int32_t>(param_2) < 0) {
        iVar9 = iVar9 + iVar14;
        param_2 = param_2 + static_cast<uint32_t>(iVar7);
    }
    if (iVar7 <= static_cast<int32_t>(param_2)) {
        iVar9 = iVar9 - iVar14;
        param_2 = param_2 + static_cast<uint32_t>(local_10) * static_cast<uint32_t>(-2);
    }
    if (local_24 < local_20) {
        param_2 = param_2 + static_cast<uint32_t>(local_20 - local_24) * static_cast<uint32_t>(local_34);
        for (; static_cast<int32_t>(param_2) < 0; param_2 = param_2 + static_cast<uint32_t>(iVar7)) iVar9 = iVar9 + iVar14;
        local_24 = local_20;
    }
    if (local_1c < iVar6) iVar6 = local_1c;
    if (iVar6 < local_24) return;

    // ORIGINAL-BUG: the game divides by the shorter axis' doubled length (local_34) in the clipping below, which is zero for
    // a line along an axis that needs it; reached only by a line the clip splits across its short axis, which a line of
    // one pixel row or column never is. Ours stops there instead of trapping.
    auto divide = [&](int32_t numerator, int32_t divisor, bool *ok) {
        if (divisor == 0 || (numerator == INT32_MIN && divisor == -1)) {
            *ok = false;
            return 0;
        }
        return numerator / divisor;
    };
    bool ok = true;

    if (local_c < iVar13) {
        if (iVar9 < local_c) {
            if (local_8 < local_c) return;
            param_2 = param_2 + static_cast<uint32_t>(local_c - iVar9) * static_cast<uint32_t>(iVar7);
            iVar9 = local_c;
            if (iVar7 <= static_cast<int32_t>(param_2)) {
                iVar9 = static_cast<int32_t>(1u - static_cast<uint32_t>(divide(
                            static_cast<int32_t>(param_2 + static_cast<uint32_t>(local_10) * static_cast<uint32_t>(-2)),
                            local_34, &ok)));
                if (!ok) return;
                local_24 = local_24 + iVar9;
                param_2 = param_2 + static_cast<uint32_t>(iVar9) * static_cast<uint32_t>(local_34);
                iVar9 = local_c;
            }
        }
        if (iVar13 < local_8) {
            if (iVar13 < iVar9) return;
            param_4 = static_cast<uint32_t>(local_24 -
                      divide(static_cast<int32_t>(param_2 + static_cast<uint32_t>(iVar13 - iVar9) *
                                                                static_cast<uint32_t>(iVar7)),
                             local_34, &ok));
            if (!ok) return;
            if (static_cast<int32_t>(param_4) < iVar6) goto emit;
        }
    } else {
        if (local_c < iVar9) {
            if (local_c < local_8) return;
            param_2 = param_2 + static_cast<uint32_t>(iVar9 - local_c) * static_cast<uint32_t>(iVar7);
            iVar9 = local_c;
            if (iVar7 <= static_cast<int32_t>(param_2)) {
                iVar9 = static_cast<int32_t>(1u - static_cast<uint32_t>(divide(
                            static_cast<int32_t>(param_2 + static_cast<uint32_t>(local_10) * static_cast<uint32_t>(-2)),
                            local_34, &ok)));
                if (!ok) return;
                local_24 = local_24 + iVar9;
                param_2 = param_2 + static_cast<uint32_t>(iVar9) * static_cast<uint32_t>(local_34);
                iVar9 = local_c;
            }
        }
        if (local_8 < iVar13) {
            if (iVar9 < iVar13) return;
            const int32_t reach = local_24 - divide(static_cast<int32_t>(static_cast<uint32_t>(iVar9 - iVar13) *
                                                                          static_cast<uint32_t>(iVar7) + param_2),
                                                    local_34, &ok);
            if (!ok) return;
            param_4 = static_cast<uint32_t>(iVar6);
            if (reach < iVar6) param_4 = static_cast<uint32_t>(reach);
            goto emit;
        }
    }
    param_4 = static_cast<uint32_t>(iVar6);
emit:
    iVar13 = static_cast<int32_t>(param_2);
    if (local_24 > static_cast<int32_t>(param_4)) return;
    {
        uint32_t room = static_cast<uint32_t>(mSpans.size()) +
                        static_cast<uint32_t>(static_cast<int32_t>(param_4) - local_24) * 2u + 2u;
        if (room > mSpans.capacity()) {
            if (room > 0x3fffffff) throw std::length_error("vector too long");
            mSpans.reserve(room);
        }
    }
    local_c = mState.lineWidthRounded;
    if (local_c < 2) {
        uint32_t param_3_ = static_cast<uint32_t>(iVar9) << 16;
        int32_t rowStart = static_cast<int32_t>(static_cast<uint32_t>(local_24) << 16);
        const int32_t step = static_cast<int32_t>(static_cast<uint32_t>(iVar14) * 0x10000u);
        do {
            int32_t word;
            if (iVar5 < local_4)
                word = static_cast<int32_t>(static_cast<uint32_t>(rowStart) + static_cast<uint32_t>(iVar9));
            else
                word = static_cast<int32_t>(param_3_ + static_cast<uint32_t>(local_24));
            push(word);
            push(word + 1);
            local_24 = local_24 + 1;
            rowStart = static_cast<int32_t>(static_cast<uint32_t>(rowStart) + 0x10000u);
            iVar13 = static_cast<int32_t>(static_cast<uint32_t>(iVar13) + static_cast<uint32_t>(local_34));
            if (iVar13 < 0) {
                iVar13 = static_cast<int32_t>(static_cast<uint32_t>(iVar13) + static_cast<uint32_t>(iVar7));
                iVar9 = iVar9 + iVar14;
                param_3_ += static_cast<uint32_t>(step);
            }
        } while (local_24 <= static_cast<int32_t>(param_4));
        return;
    }
    if (local_4 <= iVar5) {
        // Along x: each step a column of the width's pixels (down to the clip's bottom).
        int32_t rowBits = static_cast<int32_t>(static_cast<uint32_t>(iVar9) << 16);
        const int32_t bottomBits = static_cast<int32_t>(static_cast<uint32_t>(clipBottom) << 16);
        for (;;) {
            int32_t count = 0;
            int32_t word = static_cast<int32_t>(static_cast<uint32_t>(local_24) + static_cast<uint32_t>(rowBits));
            int32_t row = rowBits;
            do {
                if (bottomBits <= row) break;
                push(word);
                push(word + 1);
                ++count;
                row = static_cast<int32_t>(static_cast<uint32_t>(row) + 0x10000u);
                word = static_cast<int32_t>(static_cast<uint32_t>(word) + 0x10000u);
            } while (count < local_c);
            local_24 = local_24 + 1;
            param_2 = param_2 + static_cast<uint32_t>(local_34);
            if (static_cast<int32_t>(param_2) < 0) {
                param_2 = param_2 + static_cast<uint32_t>(iVar7);
                rowBits = static_cast<int32_t>(static_cast<uint32_t>(rowBits) +
                                               static_cast<uint32_t>(iVar14) * 0x10000u);
            }
            if (static_cast<int32_t>(param_4) < local_24) return;
        }
    }
    // Along y: each step a row of the width's pixels (across, to the clip's right).
    param_4 = param_4 * 0x10000u;
    int32_t y = static_cast<int32_t>(static_cast<uint32_t>(local_24) * 0x10000u);
    if (y <= static_cast<int32_t>(param_4)) {
        uint32_t right = static_cast<uint32_t>(local_c) + static_cast<uint32_t>(iVar9);
        do {
            push(static_cast<int32_t>(static_cast<uint32_t>(y) + static_cast<uint32_t>(iVar9)));
            uint32_t end = right;
            if (static_cast<uint32_t>(clipRight) < right) end = static_cast<uint32_t>(clipRight);
            push(static_cast<int32_t>(end + static_cast<uint32_t>(y)));
            y = static_cast<int32_t>(static_cast<uint32_t>(y) + 0x10000u);
            iVar13 = static_cast<int32_t>(static_cast<uint32_t>(iVar13) + static_cast<uint32_t>(local_34));
            if (iVar13 < 0) {
                iVar13 = static_cast<int32_t>(static_cast<uint32_t>(iVar13) + static_cast<uint32_t>(iVar7));
                iVar9 = iVar9 + iVar14;
                right = right + static_cast<uint32_t>(iVar14);
            }
        } while (y <= static_cast<int32_t>(param_4));
    }
}

// ---- kind 2: curves ----

void cGZDrawContext::BuildCurveQuads(const float *points, int32_t count) // 0x8bd430
{
    // Each cubic (four points) as 16 line segments of the width, their quads drawn as a list.
    mQuads.clear();
    for (int32_t curves = count >> 2; curves > 0; --curves, points += 8) {
        const float ax = points[0], ay = points[1], bx = points[2], by = points[3];
        const float cx = points[4], cy = points[5], dx = points[6], dy = points[7];
        // The cubic as a polynomial in t: ((A t + B) t + C) t + start.
        const float qx = (dx - ax) - (cx - bx) * 3.0f;
        const float rx = (cx + ax) * 3.0f - bx * 6.0f;
        const float sx = (bx - ax) * 3.0f;
        const float qy = (dy - ay) - (cy - by) * 3.0f;
        const float ry = (cy + ay) * 3.0f - by * 6.0f;
        const float sy = (by - ay) * 3.0f;
        float lastX = ax, lastY = ay;
        for (int32_t step = 1; step <= 16; ++step) {
            const float t = static_cast<float>(static_cast<double>(step) * 0.0625);
            const float x = ((qx * t + rx) * t + sx) * t + ax;
            const float y = ((qy * t + ry) * t + sy) * t + ay;
            const float line[4] = {lastX, lastY, x, y};
            AppendLineQuad(line);
            lastX = x;
            lastY = y;
        }
    }
    SwapQuads();
    DrawQuadList(mQuadsOut.data(), static_cast<int32_t>(mQuadsOut.size() >> 1));
}

void cGZDrawContext::BuildCurveSpans(const float *points, int32_t count) // 0x8bd660
{
    // Each cubic as line segments of fixed point forward differences (1/256 pixels, 64 bit sums scaled by 2^30), the number
    // of segments a power of two by how sharply the curve bends.
    mSpans.clear();
    for (int32_t curves = count >> 2; curves > 0; --curves, points += 8) {
        const int32_t a0 = Subpixels(points[0]), a1 = Subpixels(points[1]);
        const int32_t b0 = Subpixels(points[2]), b1 = Subpixels(points[3]);
        const int32_t c0 = Subpixels(points[4]), c1 = Subpixels(points[5]);
        const int32_t d0 = Subpixels(points[6]), d1 = Subpixels(points[7]);
        auto wrap = [](uint32_t v) { return static_cast<int32_t>(v); };
        auto u = [](int32_t v) { return static_cast<uint32_t>(v); };
        // The polynomial's coefficients per axis: cubic, quadratic / 3's third (as the game's sums), linear.
        const int32_t cubicX = wrap(u(d0) + (u(b0) - u(c0)) * 3u - u(a0));
        const int32_t curveX = wrap(u(c0) + u(b0) * u(-2) + u(a0)); // a - 2 b + c
        const int32_t quadX = wrap(u(curveX) * 3u);
        const int32_t linearX = wrap((u(b0) - u(a0)) * 3u);
        const int32_t cubicY = wrap(u(d1) + (u(b1) - u(c1)) * 3u - u(a1));
        const int32_t curveY = wrap(u(c1) + u(b1) * u(-2) + u(a1));
        const int32_t quadY = wrap(u(curveY) * 3u);
        const int32_t linearY = wrap((u(b1) - u(a1)) * 3u);

        // The bend: the larger of the second differences' magnitudes, at most 1024 segments.
        auto absolute = [](uint32_t v) {
            const uint32_t sign = static_cast<uint32_t>(static_cast<int32_t>(v) >> 31);
            return static_cast<int32_t>((v ^ sign) - sign);
        };
        int32_t bendX = absolute((u(quadX) + u(cubicX) * 3u) * 2u);
        int32_t bendX2 = absolute(u(curveX) * 6u);
        if (bendX2 < bendX) bendX2 = bendX;
        int32_t bendY = absolute((u(quadY) + u(cubicY) * 3u) * 2u);
        int32_t bendY2 = absolute(u(curveY) * 6u);
        if (bendY2 < bendY) bendY2 = bendY;
        int32_t bend = bendX2;
        if (bend < bendY2) bend = bendY2;
        uint32_t divisor = 0x400; // 1024 / segments
        int32_t segments = 1;
        {
            int32_t reach = 4;
            if (4 < bend) {
                int32_t doubled = 1;
                for (;;) {
                    segments = doubled;
                    if (divisor < 2) break;
                    reach *= 4;
                    doubled *= 2;
                    divisor = static_cast<uint32_t>(static_cast<int32_t>(divisor) >> 1);
                    segments = doubled;
                    if (!(reach < bend)) break;
                }
            }
        }
        const int64_t d = static_cast<int64_t>(divisor);
        const int64_t square = static_cast<int64_t>(static_cast<uint32_t>(divisor << 10)) * d; // 1024 * d^2
        const int64_t cube = (square >> 10) * d;                                                // d^3
        const int64_t linearScale = static_cast<int64_t>(static_cast<uint32_t>(divisor << 20));
        auto mul = [](int32_t a, int64_t b) { return static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(a)) *
                                                                          static_cast<uint64_t>(b)); };
        // First difference (position step), second and third differences, per axis, wrapping like the game's 64 bit sums.
        auto add = [](int64_t a, int64_t b) { return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b)); };
        int64_t firstX = add(add(mul(cubicX, cube), mul(quadX, square)), mul(linearX, linearScale));
        const int64_t thirdX = mul(wrap(u(cubicX) * 6u), cube);
        int64_t secondX = add(mul(wrap(u(curveX) * 6u), square), thirdX);
        int64_t firstY = add(add(mul(cubicY, cube), mul(quadY, square)), mul(linearY, linearScale));
        const int64_t thirdY = mul(wrap(u(cubicY) * 6u), cube);
        int64_t secondY = add(mul(wrap(u(curveY) * 6u), square), thirdY);
        int64_t positionX = add(mul(a0, 0x40000000), 0x20000000);
        int64_t positionY = add(mul(a1, 0x40000000), 0x20000000);
        uint32_t lastX = static_cast<uint32_t>(a0), lastY = static_cast<uint32_t>(a1);
        // One step more than the segments: the first difference runs one ahead (the game's loop).
        for (int32_t left = segments + 1; left > 0; --left) {
            positionX = add(positionX, firstX);
            firstX = add(firstX, secondX);
            secondX = add(secondX, thirdX);
            positionY = add(positionY, firstY);
            firstY = add(firstY, secondY);
            secondY = add(secondY, thirdY);
            const uint32_t x = static_cast<uint32_t>(static_cast<uint64_t>(positionX) >> 30);
            const uint32_t y = static_cast<uint32_t>(static_cast<uint64_t>(positionY) >> 30);
            BuildLineSpans(lastX, lastY, x, y);
            lastX = x;
            lastY = y;
        }
    }
    if (!mSpans.empty()) mEngine.p->DrawSpans(mSpans.data(), static_cast<uint32_t>(mSpans.size() >> 1));
}

// ---- kind 4: ellipse outlines ----

void cGZDrawContext::BuildEllipseOutlineCurves(const float *points, int32_t count) // 0x8be3d0
{
    // Each ellipse as four cubic Beziers (16 points) for the curve kind.
    const int32_t ellipses = count >> 1;
    std::vector<float> curves(static_cast<size_t>(static_cast<uint32_t>(ellipses) << 5));
    float *out = curves.data();
    for (int32_t left = ellipses; left > 0; --left, points += 4, out += 32) {
        const float ry = HalfDistance(points[1], points[3]);
        const float cy = Middle(points[1], points[3]);
        const float rx = HalfDistance(points[0], points[2]);
        const float cx = Middle(points[0], points[2]);
        const float cyPlus = ry + cy;
        const float cxPlus = rx + cx;
        const float ryK = ry * kBezierCircle;
        const float rxK = rx * kBezierCircle;
        const float cyK = ryK + cy;
        const float cxK = rxK + cx;
        const float cxKMinus = cx - rxK;
        const float cyKMinus = cy - ryK;
        const float cxMinus = cx - rx;
        const float cyMinus = cy - ry;
        const float values[32] = {
            cxPlus, cy,       cxPlus,   cyK,       cxK,      cyPlus,   cx,       cyPlus,
            cx,     cyPlus,   cxKMinus, cyPlus,    cxMinus,  cyK,      cxMinus,  cy,
            cxMinus, cy,      cxMinus,  cyKMinus,  cxKMinus, cyMinus,  cx,       cyMinus,
            cx,     cyMinus,  cxK,      cyMinus,   cxPlus,   cyKMinus, cxPlus,   cy,
        };
        for (int k = 0; k < 32; ++k) out[k] = values[k];
    }
    Dispatch(2, curves.data(), static_cast<int32_t>(curves.size() >> 1));
}

void cGZDrawContext::BuildEllipseOutlineQuads(const float *points, int32_t count) // 0x8bdc50
{
    if (mState.byte0 != 0) {
        BuildEllipseOutlineCurves(points, count);
        return;
    }
    // An outline of one pixel: each ellipse as a strip of quads along a polygon of 8 * ceil(sqrt(size) / 2) corners, the
    // strip's offset (1, 0) turning a quarter every quarter of the corners.
    mQuads.clear();
    for (int32_t left = count >> 1; left > 0; --left, points += 4) {
        const float rx = HalfDistance(points[0], points[2]);
        const float ry = HalfDistance(points[1], points[3]);
        const float cx = Middle(points[0], points[2]);
        const float cy = Middle(points[1], points[3]);
        const float larger = rx > ry ? rx : ry;
        const int32_t segments = Segments(larger);
        const int32_t half = static_cast<int32_t>(static_cast<uint32_t>(segments + 1) >> 0) >> 1;
        const int32_t quarter = half; // corners per quarter, in pairs (twice the pair count below)
        const int32_t pairs = static_cast<int32_t>(static_cast<uint32_t>(quarter) * 2u);
        const int32_t corners = static_cast<int32_t>(static_cast<uint32_t>(quarter) * 8u);
        const float step = static_cast<float>(kFullCircle / static_cast<double>(corners));
        float offsetX = 1.0f, offsetY = 0.0f;
        float angle = 0.0f;
        float quad[8] = {};
        if (corners < 0) continue;
        int32_t phase = quarter - 1;
        const double cxd = cx, cyd = cy, rxd = rx, ryd = ry;
        for (int32_t remaining = corners + 1; remaining != 0; --remaining) {
            const double cosv = static_cast<double>(static_cast<float>(std::cos(static_cast<double>(angle))));
            const double sinv = static_cast<double>(static_cast<float>(std::sin(static_cast<double>(angle))));
            quad[0] = quad[6];
            quad[1] = quad[7];
            quad[2] = quad[4];
            quad[3] = quad[5];
            const double ax = rxd * cosv + cxd;
            const double bx = static_cast<double>(offsetX) + ax;
            const double ay = ryd * sinv + cyd;
            const double by = static_cast<double>(offsetY) + ay;
            const float fax = static_cast<float>(ax), fbx = static_cast<float>(bx);
            const float fay = static_cast<float>(ay), fby = static_cast<float>(by);
            quad[4] = fax;
            quad[5] = fay;
            quad[6] = fbx;
            quad[7] = fby;
            if (!(angle == 0.0f))
                for (int k = 0; k < 8; ++k) mQuads.push_back(quad[k]);
            ++phase;
            if (!(phase < pairs)) {
                // A quarter of the way round: turn the offset and close the strip with a quad of the corner.
                phase = 0;
                const float newOffsetX = -offsetY;
                const float newOffsetY = offsetX;
                quad[0] = fbx;
                quad[1] = fby;
                quad[2] = fax;
                quad[3] = fay;
                quad[4] = fax;
                quad[5] = fay;
                quad[6] = static_cast<float>(static_cast<double>(newOffsetX) + ax);
                quad[7] = static_cast<float>(static_cast<double>(newOffsetY) + ay);
                offsetX = newOffsetX;
                offsetY = newOffsetY;
                for (int k = 0; k < 8; ++k) mQuads.push_back(quad[k]);
            }
            angle = angle + step;
        }
    }
    SwapQuads();
    DrawQuadList(mQuadsOut.data(), static_cast<int32_t>(mQuadsOut.size() >> 1));
}

// ---- kind 3: rectangles (route 2) ----

void cGZDrawContext::BuildRectSpans(const float *points, int32_t count) // 0x8be790
{
    mSpans.clear();
    for (int32_t rects = count >> 1; rects > 0; --rects, points += 4) {
        const int32_t left = CeilToInt(points[0] - 0.5f);
        const int32_t top = CeilToInt(points[1] - 0.5f);
        const int32_t right = CeilToInt(points[2] - 0.5f);
        const int32_t bottom = CeilToInt(points[3] - 0.5f);
        const int32_t l = Max(mSentClip.left, left);
        int32_t row = Max(mSentClip.top, top);
        const int32_t r = Min(mSentClip.right, right);
        const int32_t end = Min(mSentClip.bottom, bottom);
        if (end > row && r > l) {
            const uint32_t oldSize = static_cast<uint32_t>(mSpans.size());
            const uint32_t newSize = oldSize + static_cast<uint32_t>(end - row) * 2u;
            if (newSize < oldSize) {
                mSpans.resize(newSize);
                continue; // (the game then writes past the end; not reachable with a clip of a real surface)
            }
            mSpans.resize(newSize);
            int32_t *out = mSpans.data() + oldSize;
            uint32_t base = static_cast<uint32_t>(row) << 16;
            for (; row < end; ++row, out += 2, base += 0x10000u) {
                out[0] = static_cast<int32_t>(base + static_cast<uint32_t>(l));
                out[1] = static_cast<int32_t>(base + static_cast<uint32_t>(r));
            }
        }
    }
    if (!mSpans.empty()) mEngine.p->DrawSpans(mSpans.data(), static_cast<uint32_t>(mSpans.size() >> 1));
}

// ---- kind 5: ellipses ----

void cGZDrawContext::BuildEllipsePolygons(const float *points, int32_t count) // 0x8bea10
{
    // Each ellipse as a polygon of 4 * ceil(sqrt(size)) corners (clockwise from the angle just before zero) through kind 8.
    for (int32_t left = count >> 1; left > 0; --left) {
        // ORIGINAL-BUG: `points` is never advanced, so every ellipse of the list is drawn from the first one's corners.
        const float rx = HalfDistance(points[0], points[2]);
        const float cx = Middle(points[0], points[2]);
        const float ry = HalfDistance(points[1], points[3]);
        const float cy = Middle(points[1], points[3]);
        const float larger = rx > ry ? rx : ry;
        const int32_t corners = static_cast<int32_t>(static_cast<uint32_t>(Segments(larger)) * 4u);
        const float step = static_cast<float>(kFullCircle / static_cast<double>(corners));
        float angle = -step;
        mQuads.resize(static_cast<uint32_t>(corners) * 2u);
        float *out = mQuads.data();
        for (int32_t i = 0; i < corners; ++i, out += 2) {
            out[0] = static_cast<float>(std::cos(static_cast<double>(angle))) * rx + cx;
            out[1] = static_cast<float>(std::sin(static_cast<double>(angle))) * ry + cy;
            angle = angle - step;
        }
        Dispatch(8, mQuads.data(), static_cast<int32_t>(mQuads.size() >> 1));
    }
}

void cGZDrawContext::BuildEllipseSpans(const float *points, int32_t count) // 0x8bec30
{
    mSpans.clear();
    for (int32_t left = count >> 1; left > 0; --left, points += 4) {
        float lowX = points[0], highX = points[2];
        float lowY = points[1], highY = points[3];
        if (lowX > highX) std::swap(lowX, highX);
        if (lowY > highY) std::swap(lowY, highY);
        const int32_t top = Max(mSentClip.top, CeilToInt(lowY - 0.5f));
        const int32_t bottom = Min(mSentClip.bottom, CeilToInt(highY - 0.5f));
        if (top >= bottom) continue;
        const float ry = (highY - lowY) * 0.5f;
        const float centreY = (highY + lowY) * 0.5f;
        const float centreX = (highX + lowX) * 0.5f;
        const float ryy = ry * ry;
        if (ryy <= 0.0f) continue;
        const float ratio = ((highX - lowX) * 0.5f) / ry;
        {
            const uint32_t room = static_cast<uint32_t>(mSpans.size()) + static_cast<uint32_t>(bottom - top) * 2u;
            if (room > mSpans.capacity()) {
                if (room > 0x3fffffff) throw std::length_error("vector too long");
                mSpans.reserve(room);
            }
        }
        const float cx = centreX - 0.5f;
        const float cy = centreY - 0.5f;
        uint32_t rowBits = static_cast<uint32_t>(top) << 16;
        for (int32_t row = top; row < bottom; ++row, rowBits += 0x10000u) {
            const float d = static_cast<float>(row) - cy;
            const float t = ryy - d * d;
            if (t > 0.0f) {
                const float halfWidth = static_cast<float>(std::sqrt(static_cast<double>(t))) * ratio;
                const int32_t l = Max(mSentClip.left, CeilToInt(cx - halfWidth));
                const int32_t r = Min(mSentClip.right, CeilToInt(cx + halfWidth));
                if (l < r) {
                    mSpans.push_back(static_cast<int32_t>(static_cast<uint32_t>(l) + rowBits));
                    mSpans.push_back(static_cast<int32_t>(static_cast<uint32_t>(r) + rowBits));
                }
            }
        }
    }
    if (!mSpans.empty()) mEngine.p->DrawSpans(mSpans.data(), static_cast<uint32_t>(mSpans.size() >> 1));
}

// ---- kinds 6 to 8: triangles, quads, polygons ----

void cGZDrawContext::BuildTriangleSpans(const float *points, int32_t count) // 0x8beff0
{
    mSpans.clear();
    for (int32_t triangles = count / 3; triangles > 0; --triangles, points += 6) {
        std::vector<PolygonSpan> rows;
        GZScanConvertPolygon(&rows, points, 3);
        AddClippedRows(mSpans, rows, mSentClip);
    }
    if (!mSpans.empty()) mEngine.p->DrawSpans(mSpans.data(), static_cast<uint32_t>(mSpans.size() >> 1));
}

void cGZDrawContext::DrawQuadList(const float *points, int32_t count) // 0x8bf130
{
    switch (mEngine.p->ChooseRoute(7, points, count)) {
    case 1: {
        // Each quad as two triangles (corners 0 1 2 and 0 2 3).
        int32_t quads = count >> 2;
        mQuads.resize(static_cast<uint32_t>(quads) * 12);
        float *out = mQuads.data();
        for (; quads > 0; --quads, points += 8, out += 12) {
            out[0] = points[0];
            out[1] = points[1];
            out[2] = points[2];
            out[3] = points[3];
            out[4] = points[4];
            out[5] = points[5];
            out[6] = points[0];
            out[7] = points[1];
            out[8] = points[4];
            out[9] = points[5];
            out[10] = points[6];
            out[11] = points[7];
        }
        SwapQuads();
        const float *const triangles = mQuadsOut.data();
        const int32_t triangleCount = static_cast<int32_t>(mQuadsOut.size() >> 1);
        if (mEngine.p->ChooseRoute(6, triangles, triangleCount) == 2) BuildTriangleSpans(triangles, triangleCount);
        return;
    }
    case 2: BuildQuadSpans(points, count); return;
    }
}

void cGZDrawContext::BuildQuadSpans(const float *points, int32_t count) // 0x8bf250
{
    mSpans.clear();
    for (int32_t quads = count >> 2; quads > 0; --quads) {
        std::vector<PolygonSpan> rows;
        GZScanConvertPolygon(&rows, points, 4);
        // ORIGINAL-BUG: the next quad is 8 floats on, but the game steps 4 (0x10 bytes), so each quad after the first starts
        // in the middle of the one before.
        points += 4;
        AddClippedRows(mSpans, rows, mSentClip);
    }
    if (!mSpans.empty()) mEngine.p->DrawSpans(mSpans.data(), static_cast<uint32_t>(mSpans.size() >> 1));
}

void cGZDrawContext::BuildPolygon(const float *points, int32_t count) // 0x8bf480
{
    switch (mEngine.p->ChooseRoute(8, points, count)) {
    case 1: {
        // A fan of triangles from the first point. The game's vector can be the one the points are in (ellipses, kind 5,
        // build theirs in the quad vector): ours copies the points first.
        // ORIGINAL-BUG: the game reads its points from the vector it resizes, freed when the resize moves it.
        std::vector<float> copy;
        if (!mQuads.empty() && points >= mQuads.data() && points < mQuads.data() + mQuads.size())
            copy.assign(points, points + static_cast<size_t>(count) * 2), points = copy.data();
        mQuads.resize(static_cast<uint32_t>(count) * 6u - 12u);
        float *out = mQuads.data();
        for (int32_t k = count - 2; k > 0; --k, out += 6) {
            const float *p = points + 2 * (count - 2 - k);
            out[0] = points[0];
            out[1] = points[1];
            out[2] = p[2];
            out[3] = p[3];
            out[4] = p[4];
            out[5] = p[5];
        }
        // ORIGINAL-BUG: the answer is dropped (a route 2 for triangles draws nothing here), and below two points the resize
        // above throws.
        mEngine.p->ChooseRoute(6, mQuads.data(), static_cast<int32_t>(mQuads.size() >> 1));
        return;
    }
    case 2: {
        mSpans.clear();
        std::vector<PolygonSpan> rows;
        GZScanConvertPolygon(&rows, points, count);
        AddClippedRows(mSpans, rows, mSentClip);
        if (!mSpans.empty()) mEngine.p->DrawSpans(mSpans.data(), static_cast<uint32_t>(mSpans.size() >> 1));
        return;
    }
    }
}
