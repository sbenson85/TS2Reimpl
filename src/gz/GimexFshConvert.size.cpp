// The FSH back end's image conversion (lane MV; src/gz/GimexFshConvert.h, 0x885d30, 0x8885a1..0x888f97, 0x88cc20..0x88d5e8): the
// YCbCr / RGB converters, the planar and packed writers, the plane reorderer and the size helpers. The arithmetic keeps the
// original's precision and evaluation order (the float converters in single, the others in double).
#include "gz/GimexFshConvert.h"
#include "gz/GimexFshDecode.h"

#include <bit>
#include <cstring>
#include <emmintrin.h>

#include "game/original.h"
#include "gz/GimexFshBlocks.h"
#include "gz/GimexFshLeaves.h"

namespace gimex {

namespace fsh {

namespace {

// ---- converter weights (double: the original's constants, bit for bit) ----
constexpr double kLumaFromRed = std::bit_cast<double>(0x3fd06f38bd35d5fbull);     // 0.25678843
constexpr double kLumaFromGreen = std::bit_cast<double>(0x3fe021d480f634a5ull);   // 0.50412965
constexpr double kLumaFromBlue = std::bit_cast<double>(0x3fb9105bc2c968bfull);    // 0.09790586
constexpr double kCbFromRed = std::bit_cast<double>(0xbfc2f8f83962215dull);       // -0.14822295
constexpr double kCbFromGreen = std::bit_cast<double>(0x3fd29fa043d7c625ull);     // 0.2909928
constexpr double kCbFromBlue = std::bit_cast<double>(0x3fdc1c1c6b459e45ull);      // 0.43921576
constexpr double kCrFromRed = std::bit_cast<double>(0x3fdc1c1b9f42d0e0ull);       // 0.43921557
constexpr double kCrFromGreen = std::bit_cast<double>(0x3fd789d7c523602cull);     // 0.36778826
constexpr double kCrFromBlue = std::bit_cast<double>(0x3fb2490f687dc2ceull);      // 0.07142731
constexpr double kLumaOffset = 16.0;
constexpr double kChromaOffset = 128.0;
constexpr double kIntLumaFromGreen = std::bit_cast<double>(0x3fe2c8b439581062ull); // 0.587
constexpr double kIntLumaFromRed = std::bit_cast<double>(0x3fd322d0e5604189ull);   // 0.299
constexpr double kIntLumaFromBlue = std::bit_cast<double>(0x3fbd2f1a9fbe76c9ull);  // 0.114
constexpr double kIntCbFromRed = std::bit_cast<double>(0xbfc597f62b6ae7d5ull);     // -0.1687
constexpr double kIntCbFromGreen = std::bit_cast<double>(0x3fd53404ea4a8c15ull);   // 0.3313
constexpr double kIntCrFromGreen = std::bit_cast<double>(0x3fdacbfb15b573ebull);   // 0.4187
constexpr double kIntCrFromBlue = std::bit_cast<double>(0x3fb4d013a92a3055ull);    // 0.0813
constexpr double kHalf = 0.5;
constexpr double kRedFromCr = std::bit_cast<double>(0x3ff66e978d4fdf3bull);        // 1.402
constexpr double kGreenFromCb = std::bit_cast<double>(0x3fd605bc01a36e2full);      // 0.3441
constexpr double kGreenFromCr = std::bit_cast<double>(0x3fe6d9e83e425aeeull);      // 0.7141
constexpr double kBlueFromCb = std::bit_cast<double>(0x3ffc5a1cac083127ull);       // 1.772
constexpr double kZero = 0.0;
constexpr double kMaxChannel = 255.0;
constexpr double kEighth = 0.125;

// ---- converter weights (float) ----
constexpr float kFloatRedFromV = std::bit_cast<float>(0x3fcc4a9du);    // 1.5960270
constexpr float kFloatLumaScale = std::bit_cast<float>(0x3f950a81u);   // 1.1643831
constexpr float kFloatGreenFromU = std::bit_cast<float>(0x3ec89507u);  // 0.39176199
constexpr float kFloatGreenFromV = std::bit_cast<float>(0x3f501eacu);  // 0.81296802
constexpr float kFloatBlueFromU = std::bit_cast<float>(0x40011a54u);   // 2.0172319
constexpr float kFloatLumaBlack = 16.0f;
constexpr float kFloatChromaCentre = 128.0f;
constexpr float kFloatHalf = 0.5f;

constexpr int32_t kLumaScaleNumerator = 255;
constexpr int32_t kLumaBase = 0xf83;
constexpr int32_t kLumaRange = 0xdb;
constexpr int32_t kChromaCentre = 0x80;
constexpr int32_t kChannelMax = 0xff;
constexpr int32_t kLumaWeightBias = 0x32;
constexpr int32_t kLumaWeightMask = 0x3f;
constexpr int32_t kLumaWeightShift = 6;
constexpr uint32_t kAlphaPlanes = 0x2000;
constexpr int32_t kModeTiled = 5;
constexpr int32_t kModeSwizzled = 6;
constexpr int32_t kModeInterleaved = 2;
constexpr int32_t kModeInterleavedB = 8;
constexpr int32_t kFormatTile4 = 0x16;
constexpr int32_t kFormatTile4B = 0x1e;
constexpr int32_t kFormatNibble = 0x40;
constexpr int32_t kFormatNibbleB = 0x79;
constexpr int32_t kFormatNibbleC = 0x7a;
constexpr uint32_t kHeader1afbWord = 0x61;
constexpr uint32_t kHeader1cfbWord = 0x62;
constexpr int32_t kHeaderOffset = 0x20;
constexpr int32_t kBufferPadding = 0x20;
constexpr int32_t kSixthShare = 6;

int32_t Truncate(double v) {
    return _mm_cvttsd_si32(_mm_set_sd(v));
}

// The channel clamp of 0x888939: 255 when above it, 0 when below zero, else truncated.
int32_t ClampChannel(double v) {
    double m = kMaxChannel < v ? kMaxChannel : v;
    if (kZero > m) {
        m = kZero;
    }
    return Truncate(m);
}

bool IsNibbleFormat(int32_t format) {
    return format == kFormatNibble || format == kFormatNibbleB || format == kFormatNibbleC;
}

uint8_t ClampByte(int32_t v) {
    if (v > kChannelMax) {
        return static_cast<uint8_t>(kChannelMax);
    }
    if (v < 0) {
        return 0;
    }
    return static_cast<uint8_t>(v);
}

// The luma weight of a 2x2 block's pixel: (Y + 0x32) / 64, rounded toward zero.
int32_t LumaWeight(int32_t luma) {
    const int32_t t = luma + kLumaWeightBias;
    return (t + ((t >> 31) & kLumaWeightMask)) >> kLumaWeightShift;
}

// One chroma byte of a 2x2 block (the pixels' B, G, R bytes read as red = byte 2): the luma-weighted average of the
// component, rounded to a multiple of d.
template <int Component>
uint8_t *AverageChroma(uint8_t *dst, const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3, int32_t d) {
    const uint8_t *const pixels[4] = {p0, p1, p2, p3};
    int32_t converted[4][3];
    for (int32_t k = 0; k < 4; ++k) {
        const int32_t in[3] = {pixels[k][2], pixels[k][1], pixels[k][0]};
        RgbToYCbCrInt(in, converted[k]);
    }
    const int32_t w0 = LumaWeight(converted[0][0]);
    const int32_t w1 = LumaWeight(converted[1][0]);
    const int32_t w2 = LumaWeight(converted[2][0]);
    const int32_t w3 = LumaWeight(converted[3][0]);
    int32_t sum = w0 * converted[0][Component];
    sum += w1 * converted[1][Component];
    sum += w2 * converted[2][Component];
    sum += w3 * converted[3][Component];
    int32_t weights = w0 + w1;
    weights += w2;
    weights += w3;
    const int32_t divisor = weights != 0 ? weights : 1;
    *dst = ClampByte(RoundToMultiple(sum / divisor, d));
    return dst + 1;
}

} // namespace

// 0x885d30
int32_t __cdecl ClampRound(float x) {
    int32_t v = _mm_cvttss_si32(_mm_set_ss(x + kFloatHalf));
    if (v < 0) {
        v = 0;
    }
    if (v > kChannelMax) {
        v = kChannelMax;
    }
    return v;
}

// 0x8885a1
void __cdecl YuvToRgbFloat(float *out1, float *out2, float *out3, float y, float u, float v) {
    const float vCentred = v - kFloatChromaCentre;
    const float luma = (y - kFloatLumaBlack) * kFloatLumaScale;
    const float uCentred = u - kFloatChromaCentre;
    const float red = vCentred * kFloatRedFromV + luma;
    const float green = (luma - uCentred * kFloatGreenFromU) - vCentred * kFloatGreenFromV;
    const float blue = uCentred * kFloatBlueFromU + luma;
    *out1 = red;
    *out2 = green;
    *out3 = blue;
}

// 0x888f97
void __cdecl RgbToYCbCrFloat(float *out1, float *out2, float *out3, float red, float green, float blue) {
    const double r = red;
    const double g = green;
    const double b = blue;
    const double luma = ((g * kLumaFromGreen + r * kLumaFromRed) + b * kLumaFromBlue) + kLumaOffset;
    const double cb = ((r * kCbFromRed - g * kCbFromGreen) + b * kCbFromBlue) + kChromaOffset;
    const double cr = ((r * kCrFromRed - g * kCrFromGreen) - b * kCrFromBlue) + kChromaOffset;
    *out1 = static_cast<float>(luma);
    *out2 = static_cast<float>(cb);
    *out3 = static_cast<float>(cr);
}

// 0x888939
void __cdecl YCbCrToRgb(const int32_t *in, int32_t *out) {
    const double cb = static_cast<double>(in[1] - kChromaCentre);
    const double y = static_cast<double>((in[0] * kLumaScaleNumerator - kLumaBase) / kLumaRange);
    const double cr = static_cast<double>(in[2] - kChromaCentre);
    const double red = (cb * kZero + y) + cr * kRedFromCr;
    const double green = (y - cb * kGreenFromCb) - cr * kGreenFromCr;
    const double blue = (cb * kBlueFromCb + y) + cr * kZero;
    out[0] = ClampChannel(red);
    out[1] = ClampChannel(green);
    out[2] = ClampChannel(blue);
}

// 0x88d5e8
void __cdecl RgbToYCbCrInt(const int32_t *in, int32_t *out) {
    const double r = static_cast<double>(in[0]);
    const double g = static_cast<double>(in[1]);
    const double b = static_cast<double>(in[2]);
    const double luma = (g * kIntLumaFromGreen + r * kIntLumaFromRed) + b * kIntLumaFromBlue;
    const double cb = (r * kIntCbFromRed - g * kIntCbFromGreen) + b * kHalf;
    const double cr = (r * kHalf - g * kIntCrFromGreen) - b * kIntCrFromBlue;
    out[0] = Truncate(luma);
    out[1] = Truncate(cb) + kChromaCentre;
    out[2] = Truncate(cr) + kChromaCentre;
}

// 0x888a19
void __cdecl YuyvPairToBgra(uint8_t *dst, const uint8_t *src) {
    const float u = static_cast<float>(src[1]);
    const float y1 = static_cast<float>(src[2]);
    const float v = static_cast<float>(src[3]);
    const float y0 = static_cast<float>(src[0]);
    float red;
    float green;
    float blue;
    YuvToRgbFloat(&red, &green, &blue, y0, u, v);
    dst[3] = static_cast<uint8_t>(kChannelMax);
    dst[2] = static_cast<uint8_t>(ClampRound(red));
    dst[1] = static_cast<uint8_t>(ClampRound(green));
    dst[0] = static_cast<uint8_t>(ClampRound(blue));
    YuvToRgbFloat(&red, &green, &blue, y1, u, v);
    dst[7] = static_cast<uint8_t>(kChannelMax);
    dst[6] = static_cast<uint8_t>(ClampRound(red));
    dst[5] = static_cast<uint8_t>(ClampRound(green));
    dst[4] = static_cast<uint8_t>(ClampRound(blue));
}

// 0x88d362
int32_t __cdecl RoundToMultiple(int32_t v, int32_t d) {
    if (v < 0) {
        const int32_t half = (d - (d >> 31)) >> 1;
        const int32_t t = half - v;
        return t % d - t;
    }
    if (v > 0) {
        const int32_t half = (d - (d >> 31)) >> 1;
        const int32_t t = v + half;
        return t - t % d;
    }
    return v;
}

// 0x888b13
uint8_t *__cdecl AverageCb(uint8_t *dst, const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3, int32_t d) {
    return AverageChroma<1>(dst, p0, p1, p2, p3, d);
}

// 0x888c40
uint8_t *__cdecl AverageCr(uint8_t *dst, const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3, int32_t d) {
    return AverageChroma<2>(dst, p0, p1, p2, p3, d);
}

// 0x88d212
uint8_t *__cdecl StoreLuma(uint8_t *dst, const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3, int32_t d,
                           int32_t stride) {
    const uint8_t *const pixels[4] = {p0, p1, p2, p3};
    int32_t converted[4][3];
    for (int32_t k = 0; k < 4; ++k) {
        const int32_t in[3] = {pixels[k][2], pixels[k][1], pixels[k][0]};
        RgbToYCbCrInt(in, converted[k]);
    }
    dst[0] = ClampByte(RoundToMultiple(converted[0][0], d));
    dst[1] = ClampByte(RoundToMultiple(converted[1][0], d));
    dst[stride] = ClampByte(RoundToMultiple(converted[2][0], d));
    dst[stride + 1] = ClampByte(RoundToMultiple(converted[3][0], d));
    return dst + 2;
}

// 0x88d39e
void __cdecl PackYuyv(uint8_t *dst, const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3) {
    // ORIGINAL-BUG: the original converts all four pixels but keeps only the results of the middle two (the results of p0
    // and p3 land in slots the later calls overwrite or nothing reads), so p0 and p3 do not reach the output.
    (void)p0;
    (void)p3;
    float luma1;
    float cb1;
    float cr1;
    float luma2;
    float cb2;
    float cr2;
    RgbToYCbCrFloat(&luma1, &cb1, &cr1, static_cast<float>(p1[2]), static_cast<float>(p1[1]), static_cast<float>(p1[0]));
    RgbToYCbCrFloat(&luma2, &cb2, &cr2, static_cast<float>(p2[2]), static_cast<float>(p2[1]), static_cast<float>(p2[0]));
    dst[0] = static_cast<uint8_t>(ClampRound(luma1));
    dst[1] = static_cast<uint8_t>(ClampRound((cb2 + cb1) * kFloatHalf));
    dst[2] = static_cast<uint8_t>(ClampRound(luma2));
    dst[3] = static_cast<uint8_t>(ClampRound((cr2 + cr1) * kFloatHalf));
}

// 0x88ce4e
int32_t __cdecl RgbToYuyv(const Info *info, int32_t stride, const uint8_t *src, uint8_t *dst) {
    const int32_t width = info->width;
    const int32_t height = info->height;
    const int32_t rowBytes = ((width + 1) & ~1) * 2;
    const int32_t total = rowBytes * height;
    const bool odd = (width & 1) != 0;
    for (int32_t y = 0; y < height; ++y) {
        const uint8_t *pixel = src;
        uint8_t *out = dst;
        for (int32_t x = 0; x < width; x += 2) {
            const uint8_t *next = x < width - 1 ? pixel + 4 : pixel;
            const uint8_t *last = x < width - 2 ? pixel + 8 : next;
            const uint8_t *before = x != 0 ? pixel - 4 : pixel;
            PackYuyv(out, before, pixel, next, last);
            pixel += (odd && x == width - 1) ? 4 : 8;
            out += 4;
        }
        src += stride;
        dst += rowBytes;
    }
    return total;
}

// 0x88cc20
int32_t __cdecl RgbToYuv420(const Info *info, int32_t stride, const uint8_t *src, uint8_t *dst, int32_t lumaStep,
                            int32_t cbStep, int32_t crStep, uint32_t flags, int32_t mode, int32_t pixelFormat, int32_t unused,
                            int32_t alphaWidth, int32_t alphaHeight) {
    (void)unused;
    const int32_t width = info->width;
    const int32_t height = info->height;
    const bool alpha = (flags & kAlphaPlanes) != 0;
    const bool tiledAlpha = mode == kModeTiled && alpha;
    const float area = static_cast<float>(height) * static_cast<float>(width);
    int32_t result = Truncate(static_cast<double>(area) * 3.0 * kHalf);
    if (tiledAlpha) {
        const float alphaArea = static_cast<float>(alphaWidth) * static_cast<float>(alphaHeight);
        result = Truncate(static_cast<double>(alphaArea) * 3.0 * kHalf);
    }
    if (height - 1 > 0) {
        const uint32_t rowPairs = (static_cast<uint32_t>(height - 2) >> 1) + 1;
        const uint32_t columnPairs = (static_cast<uint32_t>(width - 1) >> 1) + 1;
        const uint8_t *top = src;
        uint8_t *out = dst;
        for (uint32_t row = 0; row < rowPairs; ++row) {
            const uint8_t *a = top;
            const uint8_t *b = top + stride;
            if (width > 0) {
                for (uint32_t column = 0; column < columnPairs; ++column) {
                    out = StoreLuma(out, a, a + 4, b, b + 4, lumaStep, width);
                    a += 8;
                    b += 8;
                }
            }
            out += tiledAlpha ? alphaWidth : width;
            top += stride * 2;
        }
        top = src;
        for (uint32_t row = 0; row < rowPairs; ++row) {
            const uint8_t *a = top;
            const uint8_t *b = top + stride;
            if (width > 0) {
                for (uint32_t column = 0; column < columnPairs; ++column) {
                    out = AverageCb(out, a, a + 4, b, b + 4, cbStep);
                    a += 8;
                    b += 8;
                }
            }
            top += stride * 2;
        }
        top = src;
        for (uint32_t row = 0; row < rowPairs; ++row) {
            const uint8_t *a = top;
            const uint8_t *b = top + stride;
            if (width > 0) {
                for (uint32_t column = 0; column < columnPairs; ++column) {
                    out = AverageCr(out, a, a + 4, b, b + 4, crStep);
                    a += 8;
                    b += 8;
                }
            }
            top += stride * 2;
        }
    }
    if (alpha) {
        if (mode == kModeTiled) {
            ReorderPlanes(dst, alphaWidth, alphaHeight, pixelFormat, kModeTiled);
        } else {
            ReorderPlanes(dst, width, height, pixelFormat, mode);
        }
    }
    return result;
}

// 0x88cf36
uint32_t __cdecl HighestBit(uint32_t v) {
    uint32_t index = 0;
    while ((v >>= 1) != 0) {
        ++index;
    }
    return index;
}

// 0x88cf96
uint8_t *__cdecl ReorderImage(const uint8_t *src, int32_t width, int32_t height, int32_t bits, int32_t format, int32_t mode,
                              int32_t stride) {
    const int32_t total = height * stride;
    uint8_t *const dest = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(total + kBufferPadding)));
    if (dest == nullptr) {
        return nullptr;
    }
    std::memset(dest, 0, static_cast<size_t>(total + kBufferPadding));
    if (mode == kModeSwizzled) {
        if (bits == 8) {
            Swizzle8(src, dest, width, height);
        } else if (bits == 4) {
            Swizzle4(src, dest, width, height);
        } else if (bits == 0x10 || bits == 0xf) {
            Swizzle16(src, dest, width, height);
        } else if (bits == 0x20) {
            std::memcpy(dest, src, static_cast<size_t>(total));
        } else if (bits == 0x18) {
            FreeMemory(dest);
            return nullptr;
        }
        return dest;
    }
    if (mode != kModeTiled && mode != kModeInterleaved && mode != kModeInterleavedB) {
        return dest;
    }
    int32_t bytes = 1;
    if (bits > 8) {
        if (bits <= 0x10) {
            bytes = 2;
        } else if (bits <= 0x18) {
            bytes = 3;
        } else if (bits <= 0x20) {
            bytes = 4;
        }
    }
    const bool nibble = IsNibbleFormat(format);
    int32_t rowBase = 0;
    for (int32_t y = 0; y < height; ++y) {
        int32_t sourceOffset = rowBase;
        int32_t destOffset = rowBase;
        for (int32_t x = 0; x < width; ++x) {
            if (mode == kModeTiled) {
                const int32_t tile = TileOffset(x, y, width, bits);
                destOffset = tile;
                if (format == kFormatTile4 || format == kFormatTile4B) {
                    // ORIGINAL-BUG: the last tile of a plane whose stride is its width ends 0x21 bytes past the image; the buffer has
                    // 0x20 spare (reproduced).
                    dest[tile] = src[sourceOffset];
                    dest[tile + 1] = src[sourceOffset + 1];
                    dest[tile + 0x20] = src[sourceOffset + 2];
                    dest[tile + 0x21] = src[sourceOffset + 3];
                } else if (nibble) {
                    const uint8_t keep = (x & 1) != 0 ? 0xf0 : 0x0f;
                    dest[tile] = static_cast<uint8_t>((src[sourceOffset] & keep) | dest[tile]);
                } else {
                    std::memcpy(dest + tile, src + sourceOffset, static_cast<size_t>(bytes));
                }
            } else {
                const int32_t index = InterleavedIndex(x, y, width, height);
                const int32_t quotient = index / width;
                const int32_t remainder = index % width;
                if (nibble) {
                    sourceOffset = remainder / 2 + quotient * stride;
                    const uint8_t keep = (x & 1) != 0 ? 0xf0 : 0x0f;
                    dest[destOffset] = static_cast<uint8_t>((src[sourceOffset] & keep) | dest[destOffset]);
                } else {
                    sourceOffset = remainder * bytes + quotient * stride;
                    std::memcpy(dest + destOffset, src + sourceOffset, static_cast<size_t>(bytes));
                }
            }
            if (bits != 4 || (x & 1) != 0) {
                sourceOffset += bytes;
                destOffset += bytes;
            }
        }
        rowBase += stride;
    }
    return dest;
}

// 0x888e00
void __cdecl ReorderPlanes(uint8_t *planes, int32_t width, int32_t height, int32_t format, int32_t mode) {
    uint8_t *const luma = ReorderImage(planes, width, height, 8, format, mode, width);
    if (luma == nullptr) {
        return;
    }
    const int32_t lumaSize = width * height;
    const int32_t halfWidth = width / 2;
    const int32_t halfHeight = height / 2;
    uint8_t *const second = planes + lumaSize;
    uint8_t *const cb = ReorderImage(second, halfWidth, halfHeight, 8, format, mode, halfWidth);
    if (cb == nullptr) {
        FreeMemory(luma);
        return;
    }
    const int32_t quarter = (lumaSize + ((lumaSize >> 31) & 3)) >> 2;
    uint8_t *const third = second + quarter;
    uint8_t *const cr = ReorderImage(third, halfWidth, halfHeight, 8, format, mode, halfWidth);
    if (cr != nullptr) {
        std::memcpy(planes, luma, static_cast<size_t>(lumaSize));
        std::memcpy(second, cb, static_cast<size_t>(quarter));
        std::memcpy(third, cr, static_cast<size_t>(quarter));
        FreeMemory(luma);
        FreeMemory(cb);
        FreeMemory(cr);
        return;
    }
    FreeMemory(luma);
    FreeMemory(cb);
}

// 0x88d5c9
int32_t __cdecl AlignedOffset(int32_t a, int32_t b, int32_t unused, int32_t align, int32_t count, int32_t f) {
    (void)unused;
    int32_t offset = b + f;
    if (a < count - 1) {
        offset = (offset + align) & ~align;
    }
    return offset;
}

// 0x88d52e
int32_t __cdecl MipChainSize(int32_t levels, int32_t count, uint32_t unused, uint32_t align, int32_t lastLevel,
                             int32_t *levelSize, int32_t width, int32_t height, int32_t bits) {
    (void)unused;
    uint32_t size = 0;
    for (int32_t level = 0; level < levels; ++level) {
        const int32_t rowBits = width * bits;
        const int32_t rowBytes = (rowBits + ((rowBits >> 31) & 7)) >> 3;
        size += static_cast<uint32_t>(rowBytes * height);
        if (level != lastLevel - 1) {
            size = (size + align) & ~align;
        }
        width >>= 1;
        height >>= 1;
    }
    const double perLevel = static_cast<double>(static_cast<float>(bits)) * kEighth * static_cast<double>(height / kSixthShare) *
                            static_cast<double>(width);
    const int32_t levelBytes = Truncate(perLevel);
    *levelSize = levelBytes;
    return static_cast<int32_t>(static_cast<uint32_t>(levelBytes * count) + size);
}

// 0x888d6d
void *__cdecl AllocateMipChain(int32_t width, int32_t height, int32_t bytes, int32_t *total, int32_t levels, uint32_t align) {
    int32_t size = 0;
    int32_t sixth = height / kSixthShare;
    if (levels > 0) {
        const double scale = static_cast<double>(static_cast<float>(bytes)) * kEighth;
        for (int32_t level = 0; level < levels; ++level) {
            const double aligned = static_cast<double>(static_cast<int32_t>((static_cast<uint32_t>(size) + align) & ~align));
            const double level0 = static_cast<double>(width) * scale * static_cast<double>(sixth);
            width >>= 1;
            sixth >>= 1;
            if (sixth < 1) {
                sixth = 1;
            }
            if (width < 1) {
                width = 1;
            }
            size = Truncate(level0 + aligned);
        }
    }
    const int32_t all = size * kSixthShare + kBufferPadding;
    *total = all;
    return Allocate(static_cast<uint32_t>(all));
}

// 0x888efc
void __cdecl DetectPixelFormat(const uint8_t *header, const void *data, int32_t force, int32_t *bits, int32_t *kind,
                               int32_t platform) {
    const uint32_t compressed = HasCodec(header, 3, platform);
    if (compressed == 0 && force == 0) {
        return;
    }
    const uint8_t *const body = header + kHeaderOffset;
    if (HasHeader1afb(body) != 0 || ReadWord(data, 1, platform) == kHeader1afbWord) {
        *bits = 8;
        *kind = 3;
        return;
    }
    if (HasHeader1cfb(body) != 0 || ReadWord(data, 1, platform) == kHeader1cfbWord) {
        *bits = 8;
        *kind = 5;
        return;
    }
    *bits = 4;
    *kind = 1;
}

} // namespace fsh

} // namespace gimex
