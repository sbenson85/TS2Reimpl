// nGZSceneGraph's generated textures (lane 261111g, scenegraph SU6): the cube face direction 0x18e0b0, the power 0xaebd00,
// the hue colour 0xc631e0 and the texture generators cTextureCommand vf4 0xad6f30 runs (0xaec1a0 density, 0xaec310
// mipTest, 0xaec4a0 normalizationCubeMap, 0xaec6b0 noise, 0xaec8e0 rampH, 0xaecba0 rampV, 0xaebd70 alphaFalloffCubeMap).
// Declarations and behaviour: sg/SGUtilities.h.
#include "sg/SGUtilities.h"

#include "compgeom/ConvexMath.h" // MulS / AddS / SubS: SSE operations in the original's operand order
#include "compgeom/Rigid.h"      // Dot87
#include "gz/cIGZCOM.h"
#include "numerics/PerlinNoise.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "sg/cImageData.h"
#include "sg/cTextureImage.h"
#include "sg/cTextureManager.h" // TextureManager (0xae8750, ours)
#include "gz/cIGZBuffer.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kImageClass = 0x1c4a276c;
constexpr uint32_t kTextureClass = 0x2a956dad;
constexpr uint32_t kCubeTextureClass = 0x4a956dd2;
constexpr uint32_t kNoiseClass = 0x9d332114;

// A counted pointer the generators hold, released (when not null) as their scopes end, the last made first.
template <class T> struct Held {
    T *p = nullptr;

    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held()
    {
        if (p) p->Release();
    }
    T *operator->() const { return p; }
};

// The COM director's object of class `classId` through interface `iid` into `held` (null when it has none).
template <class T> void Make(Held<T> &held, uint32_t classId, uint32_t iid)
{
    auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    void *made = nullptr;
    com->GetClassObject(classId, iid, &made); // receiver: cIGZCOM (ret 12)
    held.p = static_cast<T *>(made);
}

// Every generator's ending: a texture of class `classId` loads the image with `levels` levels and goes to *out, counted
// (the pointer *out held before is not released: ORIGINAL-BUG, a caller that generates twice into one pointer leaks the
// first texture).
void Finish(uint32_t classId, cIImageData *image, uint32_t levels, void **out)
{
    Held<cITextureImage> texture;
    Make(texture, classId, cITextureImage::kIid);
    texture->Load(image, levels);
    *out = texture.p;
    texture->AddRef();
}

void MakeImage(Held<cIImageData> &image) { Make(image, kImageClass, cIImageData::kIid); }

// A colour channel's byte: v + 49152 has the rounded value in its low mantissa bits; below 0 gives 0, above 255 gives
// 255 (through the bits, as the original: a NaN's payload decides).
uint8_t ChannelByte(float biased)
{
    const int32_t x = int32_t(std::bit_cast<uint32_t>(biased) + 0xb8c00000u);
    const int32_t over = int32_t(0xffu - uint32_t(x)) >> 31;
    return uint8_t((~(x >> 31) & x) | over);
}

// One ramp pixel's bytes (z, y, x order) for t of the way from `from` to `to`. Each operation keeps the original's operand
// order (a NaN pair gives the first operand's NaN, and its payload reaches the byte through ChannelByte).
void RampPixel(uint8_t *pixel, const float *from, const float *to, float t)
{
    using nGZCompGeom::convex::AddS;
    using nGZCompGeom::convex::MulS;
    const float u = nGZCompGeom::convex::SubS(1.0f, t);
    const float bias = 49152.0f;
    const uint8_t x = ChannelByte(AddS(AddS(MulS(to[0], t), MulS(from[0], u)), bias));
    const uint8_t y = ChannelByte(AddS(AddS(MulS(to[1], t), MulS(from[1], u)), bias));
    const uint8_t z = ChannelByte(AddS(AddS(MulS(to[2], t), MulS(from[2], u)), bias));
    pixel[0] = z;
    pixel[1] = y;
    pixel[2] = x;
}

// The nearest integer (ties to even) of v through the bits of v + 1.5 * 2^23.
int32_t NearInt(float v) { return int32_t(std::bit_cast<uint32_t>(v + 12582912.0f) + 0xb4c00000u); }

} // namespace

void __cdecl CubeDirection(float *out, int32_t face, int32_t x, int32_t y, int32_t size)
{
    const int32_t half = size >> 1;
    const float a = float(int32_t(uint32_t(x) - uint32_t(half)));
    const float b = float(int32_t(uint32_t(y) - uint32_t(half)));
    const float h = float(half);
    switch (uint32_t(face)) {
    case 0:
        out[0] = 1.0f;
        out[1] = b / h;
        out[2] = a / h;
        return;
    case 1:
        out[0] = -1.0f;
        out[1] = b / h;
        out[2] = a / h;
        return;
    case 2:
        out[1] = 1.0f;
        out[0] = a / h;
        out[2] = b / h;
        return;
    case 3:
        out[1] = -1.0f;
        out[0] = a / h;
        out[2] = b / h;
        return;
    case 4:
        out[2] = 1.0f;
        out[0] = a / h;
        out[1] = b / h;
        return;
    case 5:
        out[2] = -1.0f;
        out[0] = a / h;
        out[1] = b / h;
        return;
    default:
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        return;
    }
}

double __cdecl Pow(float x, int32_t y) { return std::pow(double(x), double(y)); }

float *__cdecl HueColour(float *out, float hue, float saturation, float value, float)
{
    float c[3];
    const float turns = float(std::floor(double(hue / 360.0f)));
    const float within = hue - turns * 360.0f;
    const double withinD = double(within);
    const float sector = float(std::floor(withinD / 120.0 + 0.5));
    int32_t i = NearInt(sector - 0.5f);
    if (float(i + 1) == sector) ++i;
    if (i == 3) i = 2;
    // ORIGINAL-BUG: a hue whose sector is outside 0..3 (NaN, infinite, past about 2^24) indexes outside c, as the original
    // writes outside its stack array.
    c[i] = value;
    const float d = float((withinD - double(uint32_t(i)) * 120.0) / 60.0);
    float next, last;
    if (d > 0.0f) {
        last = 1.0f - saturation;
        next = 1.0f - (1.0f - d) * saturation;
    } else {
        next = 1.0f - saturation;
        last = 1.0f - (d + 1.0f) * saturation;
    }
    c[(uint32_t(i) + 1) % 3] = next * value;
    c[(uint32_t(i) + 2) % 3] = last * value;
    out[0] = c[0];
    out[1] = c[1];
    out[2] = c[2];
    return out;
}

void __cdecl MakeDensityTexture(int32_t width, int32_t height, float hue, float step, void **out)
{
    Held<cIImageData> image;
    MakeImage(image);
    image->Init(width, height, 0x20, 2, 1.0f, 1);
    const uint32_t levels = image->LevelCount();
    for (uint32_t i = 0; i < levels; ++i) {
        float colour[3];
        image->FillOpaque(HueColour(colour, hue, 1.0f, 1.0f, 1.0f), int32_t(i));
        hue = hue + step;
    }
    Finish(kTextureClass, image.p, levels, out);
}

void __cdecl MakeMipTestTexture(int32_t width, int32_t height, void **out)
{
    Held<cIImageData> image;
    MakeImage(image);
    image->Init(width, height, 0x20, 2, 1.0f, 1);
    const uint32_t levels = image->LevelCount();
    uint32_t degrees = 0;
    for (uint32_t i = 0; i < levels; ++i) {
        float colour[3];
        const float hue = float(double(degrees / levels));
        image->FillOpaque(HueColour(colour, hue, 1.0f, 1.0f, 1.0f), int32_t(i));
        degrees += 360;
    }
    Finish(kTextureClass, image.p, levels, out);
}

void __cdecl MakeNormalCubeTexture(uint32_t size, void **out)
{
    static const float kFaces[6][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}, {1, 0, 1}};
    Held<cIImageData> image;
    MakeImage(image);
    image->Init(int32_t(size), int32_t(size), 1, 1, 1.0f, 6);
    for (uint32_t face = 0; face < 6; ++face) {
        image->SetCurrentEntry(face);
        float colour[3] = {kFaces[face][0], kFaces[face][1], kFaces[face][2]};
        image->FillOpaque(colour, -1);
    }
    Finish(kCubeTextureClass, image.p, 0, out);
}

void __cdecl MakeNoiseTexture(int32_t width, int32_t height, int32_t seed, float scaleX, float scaleY, void **out)
{
    Held<cIImageData> image;
    MakeImage(image);
    image->Init(width, height, 1, 3, 1.0f, 1);
    Held<nGZNumerics::cIPerlinNoise> noise;
    Make(noise, kNoiseClass, nGZNumerics::cIPerlinNoise::kIid);
    noise->Init(2, uint32_t(seed));
    for (uint32_t y = 0; y < uint32_t(height); ++y) {
        const float fy = float(int32_t(y)) / scaleY;
        for (uint32_t x = 0; x < uint32_t(width); ++x) {
            const float v = noise->Fractal2(float(int32_t(x)) / scaleX, fy, 1.0f, 0.5f, 1);
            const float value = float(double(float(double(v) * 0.5)) + 0.5);
            image->SetPixelChannel(int32_t(x), int32_t(uint32_t(height) - y - 1), value, 0);
        }
    }
    Finish(kTextureClass, image.p, 0, out);
}

void __cdecl MakeRampHTexture(int32_t width, int32_t height, const float *from, const float *to, void **out)
{
    Held<cIImageData> image;
    MakeImage(image);
    image->Init(width, height, 1, 2, 1.0f, 1);
    uint8_t *const data = image->LevelData(0);
    if (width != 0) {
        const float denom = float(double(uint32_t(width) - 1));
        uint8_t *pixel = data;
        for (uint32_t i = 0; i < uint32_t(width); ++i, pixel += 3)
            RampPixel(pixel, from, to, float(double(i)) / denom);
    }
    for (uint32_t row = 1; row < uint32_t(height); ++row) {
        const int32_t pitch = image->LevelPitch(0);
        std::memcpy(data + image->LevelPitch(0) * int32_t(row), data, size_t(pitch));
    }
    Finish(kTextureClass, image.p, 0, out);
}

void __cdecl MakeRampVTexture(int32_t width, int32_t height, const float *from, const float *to, void **out)
{
    Held<cIImageData> image;
    MakeImage(image);
    image->Init(width, height, 1, 2, 1.0f, 1);
    uint8_t *row = image->LevelData(0);
    if (height != 0) {
        const float denom = float(double(uint32_t(height) - 1));
        for (uint32_t i = 0; i < uint32_t(height); ++i) {
            if (width != 0) {
                RampPixel(row, from, to, float(double(i)) / denom);
                // The row's first pixel repeated: a forward byte copy over itself.
                const uint32_t count = (uint32_t(width) - 1) * 3;
                for (uint32_t k = 0; k < count; ++k) row[3 + k] = row[k];
            }
            row += image->LevelPitch(0);
        }
    }
    Finish(kTextureClass, image.p, 0, out);
}

void __cdecl MakeFalloffCubeTexture(uint32_t size, const float *range, const float *colour, int32_t power, void **out)
{
    using nGZCompGeom::convex::AddS;
    using nGZCompGeom::convex::MulS;
    using nGZCompGeom::convex::SubS;
    Held<cIImageData> image;
    MakeImage(image);
    image->Init(int32_t(size), int32_t(size), 1, 3, 1.0f, 6);
    // Per face: the texel's column and row from the outer (j) and inner (i) counters, {col j, col i, col +, row j, row i,
    // row +}.
    const int32_t last = int32_t(size - 1);
    const int32_t kMap[6][6] = {{-1, 0, last, 0, -1, last}, {-1, 0, last, 0, -1, last}, {1, 0, 0, 0, -1, last},
                                {1, 0, 0, 0, -1, last},     {1, 0, 0, 0, -1, last},     {-1, 0, last, 0, 1, 0}};
    for (int32_t face = 0; face < 6; ++face) {
        image->SetCurrentEntry(uint32_t(face));
        uint8_t *const data = image->LevelData(0);
        const int32_t *const m = kMap[face];
        for (uint32_t j = 0; j < size; ++j) {
            for (uint32_t i = 0; i < size; ++i) {
                float dir[3];
                CubeDirection(dir, face, int32_t(j), int32_t(i), int32_t(size));
                // The original's operand order throughout (a NaN pair gives the first operand's NaN).
                const float squares = AddS(AddS(MulS(dir[0], dir[0]), MulS(dir[1], dir[1])), MulS(dir[2], dir[2]));
                const float length = float(std::sqrt(double(squares)));
                const float inverse = 1.0f / length;
                dir[0] = MulS(inverse, dir[0]);
                dir[1] = MulS(inverse, dir[1]);
                dir[2] = MulS(inverse, dir[2]);
                const float cosine = float(nGZCompGeom::Dot87(colour, dir));
                const float raised = float(Pow(cosine, power));
                const float magnitude = std::bit_cast<float>(std::bit_cast<uint32_t>(raised) & 0x7fffffffu);
                const float level = AddS(range[0], MulS(SubS(range[1], range[0]), magnitude));
                const __m128 low = _mm_max_ss(_mm_setzero_ps(), _mm_set_ss(level));
                const __m128 clamped = _mm_min_ss(_mm_set_ss(255.0f), low);
                const int32_t col = m[0] * int32_t(j) + m[1] * int32_t(i) + m[2];
                const int32_t rowIndex = m[3] * int32_t(j) + m[4] * int32_t(i) + m[5];
                data[rowIndex * int32_t(size) + col] = uint8_t(_mm_cvttss_si32(clamped));
            }
        }
    }
    Finish(kCubeTextureClass, image.p, 0, out);
}

void __cdecl MakeTextureFromBuffer(cIGZBuffer *buffer, void **out, const char *name, uint8_t add)
{
    if (buffer == nullptr || out == nullptr) return;
    Held<cIImageData> image;
    MakeImage(image);
    if (image.p == nullptr) return;
    const uint32_t width = buffer->GetWidth();
    uint32_t rows = buffer->GetHeight();
    image->Init(int32_t(width), int32_t(rows), 1, 1, 1.0f, 1);
    if (name != nullptr) image->SetName(name);
    image->SetCurrentEntry(0);
    uint8_t *target = image->LevelData(0);
    const int32_t targetPitch = image->LevelPitch(0);
    if (buffer->Lock(0x40)) {
        const uint32_t sourcePitch = buffer->GetColorSurfaceStride();
        const uint8_t *source = static_cast<const uint8_t *>(buffer->GetColorSurfaceBits());
        const uint32_t rowBytes = buffer->GetBytesPerPixel() * width;
        for (; rows != 0; --rows) {
            std::memcpy(target, source, rowBytes);
            target += targetPitch;
            source += sourcePitch;
        }
        buffer->Unlock(0x40);
    }
    Held<cITextureImage> texture;
    Make(texture, kTextureClass, cITextureImage::kIid);
    texture->Load(image.p, 0);
    if (add != 0) TextureManager()->Add(texture.p, name, 1);
    *out = texture.p;
    texture->AddRef();
}

} // namespace nGZSceneGraph
