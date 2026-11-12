// nTSSG::cLightingManager::BuildSunShadowTexture (lane 261110a, scenegraph LG4h). See src/sg/cLightingManager.h.
// Still the game's (original_calls): the scene graph system's getter 0x20defb.
#include "compgeom/ConvexMath.h"
#include "misc/UtilitiesE.h"
#include "rz/RZAutoRef.h"
#include "sg/cLightingManagerDetail.h"
#include "sg/cTextureManager.h"

#include <cstring>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::BitsOf;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;

constexpr uint32_t kImageClass = 0x1c4a276c;     // cImageData
constexpr uint32_t kImageIid = 0xfc49f6ee;       // cIImageData
constexpr uint32_t kTextureImageIid = 0x2a956da8; // cITextureImage
constexpr uint32_t kImageFill = 0xffd964;        // the game's colour the two images start from
constexpr float kRoundBias = 12582912.0f;        // 1.5 * 2^23: a float's low bits are then the integer nearest it
constexpr float kPixelBias = 49152.0f;           // 1.5 * 2^15: the low bits are then 256 times the value, rounded

// The original's inlined floor: (x - 0.5) + 1.5 * 2^23 leaves the integer at or below x in the low bits; one more when
// that integer plus one equals x. In 32-bit wrapping arithmetic, as there.
int32_t FloorToInt(float x)
{
    const uint32_t result = BitsOf(AddS(SubS(x, 0.5f), kRoundBias)) - 0x4b400000u, next = result + 1;
    return static_cast<float>(static_cast<int32_t>(next)) == x ? static_cast<int32_t>(next) : static_cast<int32_t>(result);
}

// And its ceiling: (x + 0.5) + 1.5 * 2^23, one less when that integer less one equals x.
int32_t CeilToInt(float x)
{
    const uint32_t result = BitsOf(AddS(AddS(x, 0.5f), kRoundBias)) - 0x4b400000u, previous = result - 1;
    return static_cast<float>(static_cast<int32_t>(previous)) == x ? static_cast<int32_t>(previous)
                                                                     : static_cast<int32_t>(result);
}

// The smallest power of two at or above n (n from 1; 0 for 0 and above 2^31 in 32-bit wrapping arithmetic).
int32_t PowerOfTwoAtLeast(uint32_t n)
{
    uint32_t v = n - 1;
    v |= v >> 16;
    v |= v >> 8;
    v |= v >> 4;
    v |= v >> 2;
    v |= v >> 1;
    return static_cast<int32_t>(v + 1);
}

// A cell index clamped to 0..last (negative to 0, beyond last, unsigned, to last).
uint32_t Clamped(int32_t v, uint32_t last)
{
    if (v < 0) return 0;
    return static_cast<uint32_t>(v) > last ? last : static_cast<uint32_t>(v);
}

// A colour channel as a byte: 256 times the value rounded (through the bias), clamped to 0..255.
uint32_t ChannelByte(float value)
{
    const int32_t x = static_cast<int32_t>(BitsOf(AddS(value, kPixelBias)) + 0xb8c00000u);
    return static_cast<uint32_t>((~(x >> 31) & x) | (static_cast<int32_t>(255u - static_cast<uint32_t>(x)) >> 31)) & 0xffu;
}

// (float)sqrt((double)x): the original's sqrtsd (x is above 0 here).
float Sqrt(float x) { return static_cast<float>(std::sqrt(static_cast<double>(x))); }

// The normal's x and y, bent towards the sun (`sun`: its direction with x and y negated) by how far the shadow
// (`shadow`) is above `base`, times the blend scale, and made unit length again when that length is above 0.
void Bent(const float *normal, const float *sun, float shadow, float base, float blend, float *uv)
{
    if (!(shadow > base)) { // comiss / jbe: equal or unordered keeps the normal
        uv[0] = normal[0];
        uv[1] = normal[1];
        return;
    }
    const float t = MulS(SubS(shadow, base), blend);
    float x = AddS(normal[0], MulS(sun[0], t));
    float y = AddS(normal[1], MulS(sun[1], t));
    const float z = AddS(MulS(sun[2], t), normal[2]);
    const float squared = AddS(AddS(MulS(x, x), MulS(y, y)), MulS(z, z));
    if (squared > 0.0f) {
        const float length = Sqrt(squared);
        x = DivS(x, length);
        y = DivS(y, length);
    }
    uv[0] = x;
    uv[1] = y;
}

struct AmbientMap {
    const void *mImage; // a cIImageData
    double mWidth, mHeight; // its level 0 size as unsigned, in doubles
    uint32_t mWidthMask, mHeightMask; // the size less one
};

// The map's texel at ((u + 1) / 2, (v + 1) / 2) of its size (wrapped by the masks), times the tint, as an opaque pixel.
uint32_t Pixel(const AmbientMap &map, const float *uv, const cLightVector &tint)
{
    const float u = static_cast<float>((static_cast<double>(uv[0]) + 1.0) * 0.5 * map.mWidth);
    const uint32_t x = static_cast<uint32_t>(FloorToInt(u));
    const float v = static_cast<float>((static_cast<double>(uv[1]) + 1.0) * 0.5 * map.mHeight);
    const uint32_t y = static_cast<uint32_t>(FloorToInt(v));
    float buffer[3];
    // receiver: the ambient map, cIImageData::GetPixelRGB (slot 38: out, x, y, level)
    const float *texel = Call<38, const float *>(map.mImage, buffer, x & map.mWidthMask, y & map.mHeightMask, 0u);
    float colour[3];
    std::memcpy(colour, texel, sizeof colour);
    return 0xff000000u | ChannelByte(MulS(tint.x, colour[0])) << 16 | ChannelByte(MulS(tint.y, colour[1])) << 8 |
           ChannelByte(MulS(tint.z, colour[2]));
}

// A new cImageData in `field` (the old one released first) of the size, one level, format 1, scale 1, one entry.
void MakeImage(void *&field, int32_t width, int32_t height)
{
    auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    RZAutoRef *const ref = reinterpret_cast<RZAutoRef *>(&field)->Reset();
    com->GetClassObject(kImageClass, kImageIid, &ref->mObject); // receiver: cIGZCOM (slot 3)
    Call<6, void>(field, width, height, 1u, 1, 1.0f, 1u); // receiver: cIImageData::Init (slot 6)
}

} // namespace

void cLightingManager::BuildSunShadowTexture()
{
    void *const system = OriginalFunction<void *(__cdecl *)(), 0x20defb>()();
    void *const terrain = Call<40, void *>(system); // receiver: nTSSG::cTSSGSystem (slot 40, the terrain)
    if (!terrain) return;
    const uint32_t current = mCurrentSet;
    if (current >= static_cast<uint32_t>(mAmbientMaps.size()) || !mAmbientMaps.data()[current]) return;
    Call<7, void>(terrain, &mGridWidth, &mGridHeight); // receiver: the terrain (slot 7, its grid size)
    mTerrainWidth = static_cast<int32_t>(mGridWidth - 1);
    mTerrainHeight = static_cast<int32_t>(mGridHeight - 1);
    mTerrainScale = 0.1f;
    const int32_t width = PowerOfTwoAtLeast(static_cast<uint32_t>(mTerrainWidth));
    const int32_t height = PowerOfTwoAtLeast(static_cast<uint32_t>(mTerrainHeight));
    if (width <= 0 || height <= 0) return;
    if (!mShadeImage) {
        MakeImage(mShadeImage, width, height);
        MakeImage(*const_cast<void **>(&mAmbientImage), width, height);
    }
    const float *const fill = GameAddress<const float>(kImageFill);
    Call<18, void>(mShadeImage, fill, -1);   // receiver: cIImageData::Fill (slot 18: colour, level -1)
    Call<18, void>(mAmbientImage, fill, -1); // receiver: cIImageData::Fill

    const cLightSet &set = CurrentSet();
    const cLightVector direction = set.mLights[0].mDirection;
    const float sun[3] = {Negated(direction.x), Negated(direction.y), direction.z};
    mSunHeights.Resize(static_cast<int32_t>(mGridWidth), static_cast<int32_t>(mGridHeight), 10.0f, -1000.0f);
    mSunHeights.mBlendScale = mShadowBlendScale;
    for (uint32_t row = 0; row < mGridHeight; ++row) {
        for (uint32_t column = 0; column < mGridWidth; ++column) {
            float point[3];
            Call<8, void>(terrain, column, row, point); // receiver: the terrain (slot 8, a grid point; [2] its height)
            mSunHeights.mGround.data()[mGridWidth * row + column] = point[2];
        }
    }
    mSunHeights.mValues = mSunHeights.mGround;
    for (uint32_t k = 0; k < static_cast<uint32_t>(mSamples.size()); ++k) {
        const cLightSample &sample = mSamples.data()[k];
        int32_t bounds[4];
        std::memcpy(bounds, sample.mValues, sizeof bounds);
        const int32_t x0 = FloorToInt(MulS(SubS(static_cast<float>(bounds[0]), mSampleMargin), mTerrainScale));
        const int32_t x1 = CeilToInt(MulS(AddS(static_cast<float>(bounds[2]), mSampleMargin), mTerrainScale));
        const int32_t y0 = FloorToInt(MulS(SubS(static_cast<float>(bounds[1]), mSampleMargin), mTerrainScale));
        const int32_t y1 = CeilToInt(MulS(AddS(static_cast<float>(bounds[3]), mSampleMargin), mTerrainScale));
        // ORIGINAL-BUG: the ends are exclusive but clamped to the last cell, so a rectangle never raises the grid's
        // last column or row.
        const uint32_t lastColumn = mGridWidth - 1;
        const int32_t left = static_cast<int32_t>(Clamped(x0, lastColumn));
        const int32_t right = static_cast<int32_t>(Clamped(x1, lastColumn));
        const uint32_t lastRow = mGridHeight - 1;
        const int32_t top = static_cast<int32_t>(Clamped(y0, lastRow));
        const int32_t bottom = static_cast<int32_t>(Clamped(y1, lastRow));
        for (int32_t y = top; y < bottom; ++y) {
            for (int32_t x = left; x < right; ++x) {
                const uint32_t index = mGridWidth * static_cast<uint32_t>(y) + static_cast<uint32_t>(x);
                const float raised = AddS(MulS(sample.mWeight, mSampleScale), mSunHeights.mGround.data()[index]);
                float &value = mSunHeights.mValues.data()[index];
                value = raised > value ? raised : value; // maxss: a NaN keeps the old value
            }
        }
    }
    mSunHeights.BuildShadow(&direction.x);

    uint8_t *shade = Call<9, uint8_t *>(mShadeImage, 0u);     // receiver: cIImageData::LevelData (slot 9)
    uint8_t *ambient = Call<9, uint8_t *>(mAmbientImage, 0u); // receiver: cIImageData::LevelData
    const uint32_t pitch = Call<28>(mShadeImage, 0u);         // receiver: cIImageData::LevelPitch (slot 28)
    const uint32_t stride = (pitch >> 2) * 4;
    AmbientMap map;
    map.mImage = mAmbientMaps.data()[mCurrentSet];
    const uint32_t mapWidth = Call<26>(map.mImage, 0u);  // receiver: the ambient map, cIImageData::LevelWidth
    const uint32_t mapHeight = Call<27>(map.mImage, 0u); // receiver: the ambient map, cIImageData::LevelHeight
    map.mWidth = static_cast<double>(mapWidth);
    map.mHeight = static_cast<double>(mapHeight);
    map.mWidthMask = mapWidth - 1;
    map.mHeightMask = mapHeight - 1;
    for (uint32_t row = 0; row < static_cast<uint32_t>(mTerrainHeight); ++row) {
        uint32_t *const shadeRow = reinterpret_cast<uint32_t *>(shade);
        uint32_t *const ambientRow = reinterpret_cast<uint32_t *>(ambient);
        for (uint32_t column = 0; column < static_cast<uint32_t>(mTerrainWidth); ++column) {
            float normal[3];
            Call<9, void>(terrain, column, row, normal); // receiver: the terrain (slot 9, a grid point's normal)
            const uint32_t index = static_cast<uint32_t>(mSunHeights.mWidth) * row + column;
            const float ground = mSunHeights.mGround.data()[index];
            const float value = mSunHeights.mValues.data()[index];
            const float shadow = mSunHeights.mHeights.data()[index];
            float uv[2];
            Bent(normal, sun, shadow, ground, mSunHeights.mBlendScale, uv);
            shadeRow[column] = Pixel(map, uv, set.mTint);
            Bent(normal, sun, shadow, value, mSunHeights.mBlendScale, uv);
            ambientRow[column] = Pixel(map, uv, set.mTint);
        }
        shade += stride;
        ambient += stride;
    }

    cIGZUnknown *const texture = nGZSceneGraph::cTextureHandle::At(&mShadeTexture)->Get();
    if (!texture) return;
    void *image = nullptr;
    // receiver: the texture, cIGZUnknown::QueryInterface (slot 0) for its cITextureImage
    if (Call<0, uint8_t>(texture, kTextureImageIid, &image)) {
        // receiver: cITextureImage::GetImage (slot 11), twice
        if (Call<11, void *>(image) && Call<11, void *>(image) == mShadeImage) {
            Call<10, void>(image, mShadeImage); // receiver: cITextureImage::SetImage (slot 10)
        } else {
            Call<9, void>(image, mShadeImage, 0u); // receiver: cITextureImage::Load (slot 9: image, levels)
            // receiver: the material manager (0xae8900), slot 24 (0, 0); cTSSGSystem's frame passes (0, 1)
            Call<24, void>(GetCachedQueriedObject(), 0u, 0u);
        }
    }
    if (image) Call<2, void>(image); // receiver: cITextureImage, cIGZUnknown::Release (slot 2)
}

} // namespace nTSSG
