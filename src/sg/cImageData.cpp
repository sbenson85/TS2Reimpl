// nGZSceneGraph::cImageData as a whole class (lane SZ). Layout and the switch: src/sg/cImageData.h.
#include "sg/cImageData.h"

#include "game/game.h"
#include "game/owned.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h" // GetImageManipCached, GetPixelFormatsManagerCached
#include "gz/cGZAny.h"          // gz::AnyUint32, gz::AnyBoolean
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "gz/cImageManipDX.h"
#include "gz/cMemBlock.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cIGZPersistWriter.h"
#include "ifaces/cIGZPropertySetString.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StringKeyParser.h"
#include "sg/DrawMissingTextureName.h"
#include "sg/SGResourceHelpers.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName
#include "sg/cTextureManager.h"      // ResolveResourceName

#include <cstring>
#include <memory>
#include <string>

// The constructor's hook target (cImageData_Constructor, cImageData.h).
#pragma comment(linker, "/alternatename:_cImageData_Constructor=??0cImageData@nGZSceneGraph@@QAE@XZ")

// The records slots 18 and 19 answer (the game's texts "_txtr" and "6tx").
TS2_OWNED_STATIC(const char, db0818)[];
TS2_OWNED_STATIC(const char, da6450)[];

namespace nGZSceneGraph {

static_assert(sizeof(cImageData) == cImageData::kSize, "cImageData size (0x70)");
static_assert(offsetof(cImageData, mFormat) == 0x30, "format");
static_assert(offsetof(cImageData, mScale) == 0x34, "scale");
static_assert(offsetof(cImageData, mEntries) == 0x38, "entries");
static_assert(offsetof(cImageData, mCurrent) == 0x44, "current entry");
static_assert(offsetof(cImageData, mName) == 0x48, "name");
static_assert(offsetof(ImageEntry, mColour) == 0x14 && offsetof(ImageEntry, mScale) == 0x24, "entry layout");

namespace {

constexpr uint32_t kPropertySetService = 0x0ac1887e, kPropertySetIid = 0x4bb0d269;
constexpr uint32_t kResourceManagerService = 0x056b906e, kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kSmallImage = 0x400;  // at most this many pixels: a load keeps every level
constexpr float kColourBias = 49152.0f;  // the colour pack's float bias (bits 0x47400000)
constexpr int32_t kColourBiasBits = 0x47400000;

// A framework service, counted (released when the holder goes); null when there is no framework.
struct ServiceRef {
    void *p = nullptr;
    ServiceRef(uint32_t service, uint32_t iid)
    {
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(service, iid, &p);
    }
    ServiceRef(const ServiceRef &) = delete;
    ServiceRef &operator=(const ServiceRef &) = delete;
    ~ServiceRef()
    {
        if (p) static_cast<cIGZUnknown *>(p)->Release();
    }
};

// A cRZString on the stack (empty, refcount 0), destroyed at scope end.
struct LocalName {
    union {
        cRZString s;
    };
    LocalName() { s.Construct(); }
    LocalName(const LocalName &) = delete;
    LocalName &operator=(const LocalName &) = delete;
    ~LocalName() { s.Destruct(); }
};

// The reader's slot 23: skips n bytes of the stream.
void Skip(ifaces::cIGZPersistReader *reader, uint32_t n)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t), 23>(reader, n);
}

// The colour word a stream holds: r, g, b, a from the high byte down, each over 255.
void UnpackColour(uint32_t word, float *colour)
{
    colour[0] = static_cast<float>(word >> 24) / 255.0f;
    colour[1] = static_cast<float>((word >> 16) & 0xff) / 255.0f;
    colour[2] = static_cast<float>((word >> 8) & 0xff) / 255.0f;
    colour[3] = static_cast<float>(word & 0xff) / 255.0f;
}

// The write side's byte of a colour channel: the bits of (c + 49152) less those of 49152 (about c x 256, rounded),
// clamped to 0..255. The reader divides by 255, so a round trip is not exact.
uint32_t ColourByte(float c)
{
    const float biased = c + kColourBias;
    int32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    const int32_t x = bits - kColourBiasBits;
    if (x < 0) return 0;
    if (x > 0xff) return 0xff;
    return static_cast<uint32_t>(x);
}

bool IsPowerOfTwoOrZero(uint32_t x) { return (x & (x - 1)) == 0; }

// The vector's ends as the game reads them: the byte distance over 40, signed (no exact-division assumption).
int32_t EntriesBetween(const std::vector<ImageEntry> &entries)
{
    const uintptr_t first = reinterpret_cast<uintptr_t>(std::to_address(entries.begin()));
    const uintptr_t last = reinterpret_cast<uintptr_t>(std::to_address(entries.end()));
    return static_cast<int32_t>(static_cast<uint32_t>(last) - static_cast<uint32_t>(first)) /
           static_cast<int32_t>(sizeof(ImageEntry));
}

// A texture over memory for one level (its pixels, its size, the image's pixel format), counted once by its holder.
struct LevelTexture {
    nRZGraphic4::cMemBlockAsTexture block;
    LevelTexture(cImageData *image, cLevelInfo *level)
    {
        block.AddRef();
        block.SetTopLevel(level->mPixels.mData, image->PixelFormat(), static_cast<uint32_t>(level->mWidth),
                          static_cast<uint32_t>(level->mHeight), static_cast<uint32_t>(level->mPitch));
    }
};

} // namespace

// ---- the entry and its counted levels ----

LevelRef &LevelRef::operator=(const LevelRef &other)
{
    Reset(other.mLevel);
    return *this;
}

void LevelRef::Reset(cLevelInfo *level)
{
    if (level == mLevel) return;
    Hold(level);
    cLevelInfo *const old = mLevel;
    mLevel = level;
    Drop(old);
}

ImageEntry::ImageEntry() : mColour{1.0f, 1.0f, 1.0f, 1.0f}, mScale(kDefaultScale) {}

// ---- construction ----

cImageData::cImageData() { mName.Construct(); }

// The name, then the entries (their levels let go), then the cSGResource part.
cImageData::~cImageData() { mName.Destruct(); }

cIImageData *__cdecl cImageData::Create() { return new cImageData; }

void __cdecl cImageData::BuildLevels(ImageEntry *entry, int32_t width, int32_t height, uint32_t levels, int32_t format)
{
    entry->mLevels.clear();
    uint32_t made = 0;
    for (;;) {
        const LevelRef level(new cLevelInfo(width, height, format));
        entry->mLevels.push_back(level);
        cLevelInfo *const back = entry->mLevels.back().Get();
        back->mPixels.Resize(back->mByteCount);
        if (++made >= levels) return;
        if (!IsPowerOfTwoOrZero(static_cast<uint32_t>(width)) || !IsPowerOfTwoOrZero(static_cast<uint32_t>(height)))
            return;
        if (width < 2 && height < 2) return;
        width >>= 1;
        if (width == 0) width = 1;
        height >>= 1;
        if (height == 0) height = 1;
    }
}

uint32_t __cdecl cImageData::MipChainBytes(uint32_t levels, int32_t format, uint32_t *width, uint32_t *height)
{
    uint32_t sum = 0;
    if (levels == 0) return 0;
    for (uint32_t n = 0;;) {
        if (!IsPowerOfTwoOrZero(*width) || !IsPowerOfTwoOrZero(*height)) return sum;
        {
            const cLevelInfo level(static_cast<int32_t>(*width), static_cast<int32_t>(*height), format);
            sum += level.mByteCount;
        }
        if (*width < 2 && *height < 2) return sum;
        *width >>= 1;
        *height >>= 1;
        if (*width == 0) *width = 1;
        if (*height == 0) *height = 1;
        if (++n >= levels) return sum;
    }
}

bool cImageData::NoEntries() const { return mEntries.empty(); }

// ---- cIGZUnknown ----

bool cImageData::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cIImageData::kIid) {
        *out = static_cast<cIImageData *>(this);
        AddRef();
        return true;
    }
    return cSGResource::QueryInterface(iid, out);
}

uint32_t cImageData::AddRef() { return cSGResource::AddRef(); }
uint32_t cImageData::Release() { return cSGResource::Release(); }

// ---- cIImageData ----

// A new image with this one's format, scale, entries (their levels shared, counted), current entry and name; counted.
void cImageData::Clone(cIImageData **out)
{
    cImageData *const clone = new cImageData;
    clone->mFormat = mFormat;
    clone->mScale = mScale;
    clone->mEntries.assign(mEntries.begin(), mEntries.end());
    clone->mCurrent = mCurrent;
    clone->mName.mString = mName.mString;
    *out = clone;
    clone->AddRef();
}

cISGResource *cImageData::GetResourceConst() { return Face(); }
cISGResource *cImageData::GetResource() { return Face(); } // null for null (the conversion checks)

// The format and scale, entry 0 current, `entries` entries, each with its levels made again.
void cImageData::Init(int32_t width, int32_t height, uint32_t levels, int32_t format, float scale, uint32_t entries)
{
    mFormat = format;
    mScale = scale;
    mCurrent = 0;
    mEntries.resize(entries);
    for (ImageEntry &entry : mEntries) BuildLevels(&entry, width, height, levels, format);
}

void cImageData::SetCurrentEntry(uint32_t index) { mCurrent = index; }
uint8_t *cImageData::LevelDataConst(uint32_t level) { return CurrentLevel(level)->mPixels.mData; }
uint8_t *cImageData::LevelData(uint32_t level) { return CurrentLevel(level)->mPixels.mData; }
void cImageData::SetScale(float value) { mScale = value; }
void cImageData::SetEntryColour(const float *colour) { std::memcpy(Current().mColour, colour, sizeof(Current().mColour)); }
void cImageData::SetEntryScale(float value) { Current().mScale = value; }
void cImageData::SetName(const char *text) { mName.mString.assign(text, std::strlen(text)); }

// ORIGINAL-BUG: with n at least the level count, count - 1 levels go, so the last stays (and with no levels n becomes
// 0xffffffff: the loop walks an empty deque). Each step lets the top level go as the deque's pop_front does.
void cImageData::DropTopLevels(uint32_t n)
{
    std::deque<LevelRef> &levels = Current().mLevels;
    if (n != 0 && !(n < levels.size())) n = static_cast<uint32_t>(levels.size()) - 1;
    for (; n != 0; --n) levels.pop_front();
}

cISGResource *cImageData::LevelResource(uint32_t level) { return CurrentLevel(level)->GetResource(); }
void cImageData::SetLevelFlag(uint32_t level, uint8_t value) { CurrentLevel(level)->SetFlag15(value); }

uint8_t cImageData::HasPixels()
{
    if (NoEntries()) return 0;
    ImageEntry &entry = Current();
    if (entry.mLevels.size() == 0) return 0;
    cLevelInfo *const top = entry.mLevels[0].Get();
    if (!top) return 0;
    return top->mPixels.mSize != 0 ? 1 : 0;
}

int32_t cImageData::EntryCount() { return EntriesBetween(mEntries); }
int32_t cImageData::LevelWidth(uint32_t level) { return CurrentLevel(level)->mWidth; }
int32_t cImageData::LevelHeight(uint32_t level) { return CurrentLevel(level)->mHeight; }
int32_t cImageData::LevelPitch(uint32_t level) { return CurrentLevel(level)->mPitch; }

uint32_t cImageData::TotalBytes()
{
    if (NoEntries()) return 0;
    ImageEntry &entry = Current();
    const uint32_t count = static_cast<uint32_t>(entry.mLevels.size());
    uint32_t sum = 0;
    for (uint32_t k = 0; k < count; ++k) sum += entry.mLevels[k]->mPixels.mSize;
    return sum;
}

uint32_t cImageData::LevelBytes(uint32_t level)
{
    if (NoEntries()) return 0;
    ImageEntry &entry = Current();
    if (entry.mLevels.size() == 0) return 0;
    return entry.mLevels[level]->mPixels.mSize;
}

uint32_t cImageData::LevelCount()
{
    if (NoEntries()) return 0;
    return static_cast<uint32_t>(Current().mLevels.size());
}

int32_t cImageData::GetFormat() { return mFormat; }

// The pixel format object for the image's format word, from the graphics system's pixel formats manager.
void *cImageData::PixelFormat()
{
    void *const formats = nGZGraphic4::GetPixelFormatsManagerCached();
    using Masks = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using Luminance = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    using Dxt = void *(__thiscall *)(void *, uint32_t);
    switch (mFormat) {
    case cLevelInfo::kFormatRgba32: return VirtualCall<Masks, 6>(formats, 32u, 8u, 8u, 8u, 8u);
    case cLevelInfo::kFormatRgb24:
    case cLevelInfo::kFormatRgb24B: return VirtualCall<Masks, 6>(formats, 24u, 0u, 8u, 8u, 8u);
    case cLevelInfo::kFormatByte: return VirtualCall<Masks, 6>(formats, 8u, 8u, 0u, 0u, 0u);
    case cLevelInfo::kFormatDxt1: return VirtualCall<Dxt, 10>(formats, 1u);
    case cLevelInfo::kFormatDxt3: return VirtualCall<Dxt, 10>(formats, 3u);
    case cLevelInfo::kFormatByteB: return VirtualCall<Luminance, 7>(formats, 8u, 0u, 8u);
    case cLevelInfo::kFormatWord: return VirtualCall<Luminance, 7>(formats, 16u, 0u, 16u);
    case cLevelInfo::kFormatDxt5: return VirtualCall<Dxt, 10>(formats, 5u);
    default: return VirtualCall<void *(__thiscall *)(void *), 5>(formats);
    }
}

float cImageData::GetScale() { return mScale; }
float *cImageData::GetEntryColour(float *out)
{
    std::memcpy(out, Current().mColour, sizeof(Current().mColour));
    return out;
}
float cImageData::GetEntryScale() { return Current().mScale; }

const char *cImageData::GetNameText()
{
    if (mName.mString.size() == 0) return nullptr;
    return mName.mString.data();
}

// ---- the pixel slots ----
//
// A pixel is addressed by (x, y) in the level `level` of the current entry (no bounds checks, as the original). The
// formats: 1 is 32 bits (bytes b, g, r, a in memory), 2 is 24 bits (b, g, r), 3 is 8 bits; every other format is left
// alone (reads answer white, writes do nothing). The float colour is (a, r, g, b), its first member the alpha.

namespace {

// The byte a colour channel stores as: (c + 49152)'s bits less 0x47400000, clamped to 0..255 without a branch. The low
// side is tested by the sign, the high side by the sign of 255 - x, so the one value whose 255 - x overflows (INT_MIN,
// c = -98304) comes out 255, not 0.
uint8_t ChannelByte(float c)
{
    const float biased = c + kColourBias;
    uint32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    const uint32_t x = bits + 0xb8c00000u;
    const int32_t sign = static_cast<int32_t>(x) >> 31;
    const int32_t over = static_cast<int32_t>(0xffu - x) >> 31;
    return static_cast<uint8_t>((~static_cast<uint32_t>(sign) & x) | static_cast<uint32_t>(over));
}

// A level held for a scope (the face counted), the way every pixel slot takes it.
struct HeldLevel {
    LevelRef ref;
    explicit HeldLevel(const LevelRef &level) : ref(level) {}
    cLevelInfo *operator->() const { return ref.Get(); }
    // The byte address of pixel (x, y) with `bytes` per pixel.
    uint8_t *Pixel(int32_t x, int32_t y, int32_t bytes) const
    {
        return ref->mPixels.mData + ref->mPitch * y + x * bytes;
    }
};

// The word of a 32-bit pixel: the four channels from the high byte down.
uint32_t PackWord(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return (((static_cast<uint32_t>(a) << 8 | r) << 8 | g) << 8) | b;
}

float ByteToUnit(uint8_t b) { return static_cast<float>(b) / 255.0f; }

// Fills the level's bytes: format 1 a word at a time, format 2 three bytes at a time. The loops end on equality with the
// end address, as the original's do (a size that is not a whole number of pixels runs on).
void FillLevel32(cLevelInfo *level, uint32_t word)
{
    uint32_t *p = reinterpret_cast<uint32_t *>(level->mPixels.mData);
    uint32_t *const end = reinterpret_cast<uint32_t *>(level->mPixels.mData + level->mPixels.mSize);
    for (; p != end; ++p) *p = word;
}

void FillLevel24(cLevelInfo *level, const uint8_t bgr[3])
{
    uint8_t *p = level->mPixels.mData;
    uint8_t *const end = p + level->mPixels.mSize;
    if (p != end) {
        do {
            p[0] = bgr[0];
            p[1] = bgr[1];
            p[2] = bgr[2];
            p += 3;
        } while (p != end);
    }
}

} // namespace

void cImageData::SetPixelOpaque(int32_t x, int32_t y, const float *colour, int32_t level)
{
    if (mFormat == 1) {
        const uint8_t r = ChannelByte(colour[0]), g = ChannelByte(colour[1]), b = ChannelByte(colour[2]);
        const HeldLevel held(Current().mLevels[level]);
        *reinterpret_cast<uint32_t *>(held.Pixel(x, y, 4)) = PackWord(0xff, r, g, b);
    } else if (mFormat == 2) {
        const uint8_t r = ChannelByte(colour[0]), g = ChannelByte(colour[1]), b = ChannelByte(colour[2]);
        const HeldLevel held(Current().mLevels[level]);
        uint8_t *const pixel = held.Pixel(x, y, 3);
        pixel[0] = b;
        pixel[1] = g;
        pixel[2] = r;
    }
}

void cImageData::SetPixel(int32_t x, int32_t y, const float *colour, int32_t level)
{
    if (mFormat != 1) return;
    const uint8_t a = ChannelByte(colour[0]), r = ChannelByte(colour[1]), g = ChannelByte(colour[2]),
                  b = ChannelByte(colour[3]);
    const HeldLevel held(Current().mLevels[level]);
    *reinterpret_cast<uint32_t *>(held.Pixel(x, y, 4)) = PackWord(a, r, g, b);
}

void cImageData::SetPixelChannel(int32_t x, int32_t y, float value, int32_t level)
{
    if (mFormat == 1) {
        const uint8_t byte = ChannelByte(value);
        const HeldLevel held(Current().mLevels[level]);
        held.Pixel(x, y, 4)[3] = byte;
    } else if (mFormat == 3) {
        const uint8_t byte = ChannelByte(value);
        const HeldLevel held(Current().mLevels[level]);
        held.Pixel(x, y, 1)[0] = byte;
    }
}

float *cImageData::GetPixelRGB(float *out, int32_t x, int32_t y, int32_t level)
{
    if (mFormat == 1) {
        const HeldLevel held(Current().mLevels[level]);
        const uint32_t word = *reinterpret_cast<const uint32_t *>(held.Pixel(x, y, 4));
        out[0] = ByteToUnit(static_cast<uint8_t>(word >> 16));
        out[1] = ByteToUnit(static_cast<uint8_t>(word >> 8));
        out[2] = ByteToUnit(static_cast<uint8_t>(word));
    } else if (mFormat == 2) {
        const HeldLevel held(Current().mLevels[level]);
        const uint8_t *const pixel = held.Pixel(x, y, 3);
        out[0] = ByteToUnit(pixel[2]);
        out[1] = ByteToUnit(pixel[1]);
        out[2] = ByteToUnit(pixel[0]);
    } else {
        out[0] = out[1] = out[2] = 1.0f;
    }
    return out;
}

float *cImageData::GetPixel(float *out, int32_t x, int32_t y, int32_t level)
{
    if (mFormat == 1) {
        const HeldLevel held(Current().mLevels[level]);
        const uint8_t *const pixel = held.Pixel(x, y, 4);
        out[0] = ByteToUnit(pixel[3]);
        out[1] = ByteToUnit(pixel[2]);
        out[2] = ByteToUnit(pixel[1]);
        out[3] = ByteToUnit(pixel[0]);
    } else if (mFormat == 2) {
        const HeldLevel held(Current().mLevels[level]);
        const uint8_t *const pixel = held.Pixel(x, y, 3);
        out[0] = 1.0f;
        out[1] = ByteToUnit(pixel[2]);
        out[2] = ByteToUnit(pixel[1]);
        out[3] = ByteToUnit(pixel[0]);
    } else {
        out[0] = out[1] = out[2] = out[3] = 1.0f;
    }
    return out;
}

float cImageData::GetPixelChannel(int32_t x, int32_t y, int32_t level)
{
    if (mFormat == 1) {
        const HeldLevel held(Current().mLevels[level]);
        return static_cast<float>(held.Pixel(x, y, 4)[3]) * 0.003921569f;
    }
    if (mFormat == 3) {
        const HeldLevel held(Current().mLevels[level]);
        return static_cast<float>(held.Pixel(x, y, 1)[0]) * 0.003921569f;
    }
    return 1.0f;
}

// ---- fills ----

void cImageData::FillOpaque(const float *colour, int32_t level)
{
    if (mFormat == 1) {
        const uint32_t word = PackWord(0xff, ChannelByte(colour[0]), ChannelByte(colour[1]), ChannelByte(colour[2]));
        if (level == -1) {
            const uint32_t count = static_cast<uint32_t>(Current().mLevels.size());
            for (uint32_t i = 0; i < count; ++i) {
                const HeldLevel held(Current().mLevels[i]);
                FillLevel32(held.ref.Get(), word);
            }
        } else {
            const HeldLevel held(Current().mLevels[level]);
            FillLevel32(held.ref.Get(), word);
        }
    } else if (mFormat == 2) {
        const uint8_t bgr[3] = {ChannelByte(colour[2]), ChannelByte(colour[1]), ChannelByte(colour[0])};
        if (level == -1) {
            const uint32_t count = static_cast<uint32_t>(Current().mLevels.size());
            for (uint32_t i = 0; i < count; ++i) {
                const HeldLevel held(Current().mLevels[i]);
                FillLevel24(held.ref.Get(), bgr);
            }
        } else {
            const HeldLevel held(Current().mLevels[level]);
            FillLevel24(held.ref.Get(), bgr);
        }
    }
}

void cImageData::Fill(const float *colour, int32_t level)
{
    if (mFormat == 1) {
        const uint32_t word =
            PackWord(ChannelByte(colour[0]), ChannelByte(colour[1]), ChannelByte(colour[2]), ChannelByte(colour[3]));
        if (level == -1) {
            const uint32_t count = static_cast<uint32_t>(Current().mLevels.size());
            for (uint32_t i = 0; i < count; ++i) {
                const HeldLevel held(Current().mLevels[i]);
                FillLevel32(held.ref.Get(), word);
            }
        } else {
            const HeldLevel held(Current().mLevels[level]);
            FillLevel32(held.ref.Get(), word);
        }
    } else if (mFormat == 2) {
        // The alpha is not stored.
        const uint8_t bgr[3] = {ChannelByte(colour[3]), ChannelByte(colour[2]), ChannelByte(colour[1])};
        if (level == -1) {
            const uint32_t count = static_cast<uint32_t>(Current().mLevels.size());
            for (uint32_t i = 0; i < count; ++i) {
                const HeldLevel held(Current().mLevels[i]);
                FillLevel24(held.ref.Get(), bgr);
            }
        } else {
            const HeldLevel held(Current().mLevels[level]);
            FillLevel24(held.ref.Get(), bgr);
        }
    }
}

// ---- mips and format changes ----

namespace {

nGZGraphic4::cIImageManip *Manip() { return static_cast<nGZGraphic4::cIImageManip *>(nGZGraphic4::GetImageManipCached()); }

} // namespace

void cImageData::RegenerateMips()
{
    ImageEntry &entry = Current();
    {
        const cLevelInfo *const top = entry.mLevels[0].Get();
        BuildLevels(&entry, top->mWidth, top->mHeight, 1, mFormat);
    }
    {
        const cLevelInfo *const top = entry.mLevels[0].Get();
        BuildLevels(&entry, top->mWidth, top->mHeight, 0x20, mFormat);
    }
    nRZGraphic4::cMemBlockAsSurface source;
    {
        cLevelInfo *const top = entry.mLevels[0].Get();
        source.Init(top->mPixels.mData, PixelFormat(), static_cast<uint32_t>(top->mWidth),
                    static_cast<uint32_t>(top->mHeight), static_cast<uint32_t>(top->mPitch));
    }
    for (uint32_t i = 1; i < entry.mLevels.size(); ++i) {
        const HeldLevel held(entry.mLevels[i]);
        nRZGraphic4::cMemBlockAsSurface target;
        target.Init(held->mPixels.mData, PixelFormat(), static_cast<uint32_t>(held->mWidth),
                    static_cast<uint32_t>(held->mHeight), static_cast<uint32_t>(held->mPitch));
        Manip()->CopySurface(&source, &target);
    }
}

bool cImageData::SetFormat(int32_t format)
{
    const int32_t old = mFormat;
    if (old == format) return true;
    void *const oldFormat = PixelFormat();
    mFormat = format;
    void *const newFormat = PixelFormat();
    const bool fromColour = old == 1 || old == 2 || old == 3 || old == 4 || old == 5 || old == 8;
    if (fromColour && format >= 1 && format <= 8) {
        // Each entry's levels made again in the new format, the old ones converted into them.
        for (ImageEntry *entry = mEntries.data(), *end = entry + mEntries.size(); entry != end; ++entry) {
            const ImageEntry before(*entry);
            BuildLevels(entry, before.mLevels[0]->mWidth, before.mLevels[0]->mHeight,
                        static_cast<uint32_t>(entry->mLevels.size()), format);
            for (uint32_t i = 0; i < entry->mLevels.size(); ++i) {
                const HeldLevel from(before.mLevels[i]);
                const HeldLevel to(entry->mLevels[i]);
                nRZGraphic4::cMemBlockAsSurface source(from->mPixels.mData, oldFormat, static_cast<uint32_t>(from->mWidth),
                                                       static_cast<uint32_t>(from->mHeight),
                                                       static_cast<uint32_t>(from->mPitch));
                nRZGraphic4::cMemBlockAsSurface target(to->mPixels.mData, newFormat, static_cast<uint32_t>(to->mWidth),
                                                       static_cast<uint32_t>(to->mHeight), static_cast<uint32_t>(to->mPitch));
                Manip()->CopySurface(&source, &target);
            }
        }
        return true;
    }
    if ((old == 6 || old == 7) && format == 9) {
        // The two bump formats to the normal-map format: the levels made again, then one normal map from a texture of
        // the old levels to one of the new (the entry's scale its amplitude).
        for (ImageEntry *entry = mEntries.data(), *end = entry + mEntries.size(); entry != end; ++entry) {
            const ImageEntry before(*entry);
            BuildLevels(entry, before.mLevels[0]->mWidth, before.mLevels[0]->mHeight,
                        static_cast<uint32_t>(entry->mLevels.size()), 9);
            nRZGraphic4::cMemBlockAsTexture source, target;
            source.AddRef();
            target.AddRef();
            for (uint32_t i = 0; i < entry->mLevels.size(); ++i) {
                const HeldLevel from(before.mLevels[i]);
                const HeldLevel to(entry->mLevels[i]);
                if (i == 0) {
                    source.SetTopLevel(from->mPixels.mData, oldFormat, static_cast<uint32_t>(from->mWidth),
                                       static_cast<uint32_t>(from->mHeight), static_cast<uint32_t>(from->mPitch));
                    target.SetTopLevel(to->mPixels.mData, newFormat, static_cast<uint32_t>(to->mWidth),
                                       static_cast<uint32_t>(to->mHeight), static_cast<uint32_t>(to->mPitch));
                } else {
                    source.AddLevel(from->mPixels.mData, static_cast<uint32_t>(from->mWidth),
                                    static_cast<uint32_t>(from->mHeight), static_cast<uint32_t>(from->mPitch));
                    target.AddLevel(to->mPixels.mData, static_cast<uint32_t>(to->mWidth),
                                    static_cast<uint32_t>(to->mHeight), static_cast<uint32_t>(to->mPitch));
                }
            }
            Manip()->ComputeNormalMap(&source, &target, entry->mScale, false);
        }
        return true;
    }
    return false;
}

// ---- persistence ----

cPersistTypeInfo cImageData::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cImageData");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// The record, the resource part, the size words (width, height, format, level count), the scale, the entry count and the
// current entry; then the entries at the size the config's "imageDataSizeReductionOnLoad" leaves (unless
// "ignoreImageDataSizeReductionOnLoad"): fewer top levels skipped from a large chain, or (a reduction past the levels)
// one level read at its size and copied down into a smaller one. A failed entry leaves a 1x1-entry image showing the
// missing resource's name.
void cImageData::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cSGResource::Read(reader);
    uint32_t width = reader->GetUint32();
    uint32_t height = reader->GetUint32();
    const uint32_t pixels = width * height;
    const int32_t format = static_cast<int32_t>(reader->GetUint32());
    const uint32_t levelCount = reader->GetUint32();
    const float scale = reader->GetFloat32();
    const uint32_t entryCount = reader->GetUint32();
    mCurrent = reader->GetUint32();

    uint32_t reduction;
    {
        ServiceRef config(kPropertySetService, kPropertySetIid);
        auto *const properties = As<ifaces::cIGZPropertySetString>(config.p);
        gz::AnyUint32 value(0);
        {
            GZServiceString key("imageDataSizeReductionOnLoad");
            properties->GetPropertyValue(&key, static_cast<cIGZAny *>(&value));
        }
        gz::AnyBoolean ignore(0);
        {
            GZServiceString key("ignoreImageDataSizeReductionOnLoad");
            properties->GetPropertyValue(&key, static_cast<cIGZAny *>(&ignore));
        }
        if (ignore.mValue) value.mValue = 0;
        reduction = value.mValue;

        void *manager;
        {
            const ServiceRef managerRef(kResourceManagerService, kResourceManagerIid);
            manager = managerRef.p; // kept uncounted, as the original does
        }

        LocalName missing;
        bool failed = false;
        if (reduction < levelCount) {
            // Fewer levels: the top `skip` skipped (a small image keeps them all).
            const uint32_t skip = pixels > kSmallImage ? reduction : 0;
            const uint32_t skipBytes = MipChainBytes(skip, format, &width, &height);
            Init(static_cast<int32_t>(width), static_cast<int32_t>(height), levelCount - skip, format, scale, entryCount);
            if (info.mVersion > 6) reader->GetGZStr(mName.AsInterface());
            for (uint32_t i = 0; i < entryCount; ++i) {
                if (failed) break;
                if (!ReadLevels(reader, &info, skipBytes, manager, &mEntries[i], &missing.s)) failed = true;
                if (info.mVersion < 6) {
                    for (float &c : mEntries[i].mColour) c = 1.0f;
                } else {
                    UnpackColour(reader->GetUint32(), mEntries[i].mColour);
                }
                if (info.mVersion > 7) mEntries[i].mScale = reader->GetFloat32();
            }
        } else {
            // One level: all but the last skipped (a small image skips none), read, then copied into the reduced size.
            const uint32_t skip = pixels > kSmallImage && levelCount - 1 != 0 ? levelCount - 1 : 0;
            const uint32_t skipBytes = MipChainBytes(skip, format, &width, &height);
            std::vector<ImageEntry> read(entryCount);
            for (ImageEntry &entry : read) BuildLevels(&entry, static_cast<int32_t>(width), static_cast<int32_t>(height), 1, format);
            uint32_t finalWidth = width >> (reduction & 31);
            if (finalWidth == 0) finalWidth = 1;
            uint32_t finalHeight = height >> (reduction & 31);
            if (finalHeight == 0) finalHeight = 1;
            if (info.mVersion < 9 && skipBytes == 0) {
                finalWidth = width;
                finalHeight = height;
            }
            Init(static_cast<int32_t>(finalWidth), static_cast<int32_t>(finalHeight), 1, format, scale, entryCount);
            if (info.mVersion > 6) reader->GetGZStr(mName.AsInterface());
            for (uint32_t i = 0; i < entryCount; ++i) {
                if (failed) break;
                if (!ReadLevels(reader, &info, skipBytes, manager, &read[i], &missing.s)) failed = true;
                {
                    const LevelRef target(mEntries[i].mLevels[0]);
                    LevelTexture to(this, target.Get());
                    const LevelRef source(read[i].mLevels[0]);
                    LevelTexture from(this, source.Get());
                    static_cast<nGZGraphic4::cIImageManip *>(nGZGraphic4::GetImageManipCached())->CopyTexture(&from.block, &to.block);
                    if (info.mVersion < 6) {
                        for (float &c : mEntries[i].mColour) c = 1.0f;
                    } else {
                        UnpackColour(reader->GetUint32(), mEntries[i].mColour);
                    }
                    if (info.mVersion > 7) mEntries[i].mScale = reader->GetFloat32();
                }
            }
        }
        if (failed) {
            mEntries.clear();
            Init(static_cast<int32_t>(width), static_cast<int32_t>(height), 1, 1, 1.0f, 1);
            const LevelRef top(mEntries[0].mLevels[0]);
            LevelTexture texture(this, top.Get());
            nGZSceneGraph::DrawMissingTextureName(texture.block.LevelSurface(0), missing.s.AsInterface());
        }
    }
    if (info.mName) info.mName->Release();
}

bool cImageData::ReadLevels(ifaces::cIGZPersistReader *reader, const cPersistTypeInfo *info, uint32_t skipBytes,
                            void *manager, ImageEntry *entry, cRZString *missingName)
{
    if (info->mVersion < 9) {
        reader->GetUint32();
        // ORIGINAL-BUG: the block's size is the current entry's pixel bytes, whichever entry is read. On Read's one-level
        // path with levels skipped, the current entry is already the reduced image, smaller than the level read here, so
        // the copy below reads past the block.
        const uint32_t total = TotalBytes();
        if (skipBytes != 0) Skip(reader, skipBytes);
        std::vector<char> block(total);
        reader->GetVoid(block.data(), static_cast<uint32_t>(block.size()));
        uint32_t offset = 0;
        for (uint32_t k = 0; k < entry->mLevels.size(); ++k) {
            cLevelInfo *const level = entry->mLevels[k].Get();
            std::memcpy(level->mPixels.mData, block.data() + offset, level->mByteCount);
            offset += level->mByteCount;
        }
        return true;
    }
    const int32_t records = static_cast<int32_t>(reader->GetUint32());
    const int32_t levels = static_cast<int32_t>(entry->mLevels.size());
    for (int32_t i = 0; i < records; ++i) {
        if (!reader->GetBool()) {
            const uint32_t size = reader->GetUint32();
            if (i < levels) {
                // The records run from the smallest level up.
                const LevelRef level(entry->mLevels[static_cast<uint32_t>(levels - 1 - i)]);
                reader->GetVoid(level->mPixels.mData, level->mPixels.mSize);
            } else {
                Skip(reader, size);
            }
            continue;
        }
        LocalName name;
        reader->GetGZStr(name.s.AsInterface());
        if (i >= levels) continue;
        LocalName resolved;
        ResolveResourceName(resolved.s.AsInterface(), name.s.ToChar(), 0, nullptr);
        StringKeyParser::Result key = {};
        StringKeyParser::ParseKey(&key, resolved.s.ToChar(), 0, nullptr);
        cLevelInfo *found = nullptr;
        const bool ok = As<ifaces::cIGZPersistResourceManager>(manager)->GetResource(
                            &key, cLevelInfo::kIidSelf, reinterpret_cast<void **>(&found), 0, 0) != 0;
        if (!ok || !found) {
            if (missingName) missingName->mString = name.s.mString;
            LevelRef::Drop(found);
            return false;
        }
        entry->mLevels[static_cast<uint32_t>(levels - 1 - i)].Reset(found);
        LevelRef::Drop(found);
    }
    return true;
}

// The record, the resource part, the size words and the rest of the header as Read takes them, the name; then each
// entry: a level count, its levels from the smallest up (a flag; the pixels, or the resource's name with the resource
// handed to the resource manager), the colour word and the float.
void cImageData::Write(ifaces::cIGZPersistWriter *writer)
{
    {
        cPersistTypeInfo info = cImageData::GetTypeInfo();
        writer->WriteTypeInfo(&info);
        if (info.mName) info.mName->Release();
    }
    cSGResource::WriteResource(writer);
    writer->SetUint32(static_cast<uint32_t>(LevelWidth(0)));
    writer->SetUint32(static_cast<uint32_t>(LevelHeight(0)));
    writer->SetUint32(static_cast<uint32_t>(GetFormat()));
    writer->SetUint32(LevelCount());
    writer->SetFloat32(mScale);
    writer->SetUint32(static_cast<uint32_t>(EntriesBetween(mEntries)));
    writer->SetUint32(mCurrent);
    writer->WriteGZStr(mName.AsInterface());
    void *manager;
    {
        const ServiceRef managerRef(kResourceManagerService, kResourceManagerIid);
        manager = managerRef.p;
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(EntriesBetween(mEntries)); ++i) {
        // ORIGINAL-BUG: the count written is the current entry's, whichever entry follows.
        writer->SetUint32(LevelCount());
        for (int32_t j = static_cast<int32_t>(mEntries[i].mLevels.size()); --j >= 0;) {
            const LevelRef level(mEntries[i].mLevels[static_cast<uint32_t>(j)]);
            const uint8_t external = level->GetFlag16();
            writer->SetUint8(external);
            if (!external) {
                writer->SetUint32(level->mPixels.mSize);
                writer->SetVoid(level->mPixels.mData, level->mPixels.mSize);
            } else {
                cISGResource *const face = level->Face();
                writer->WriteRZCharStr(const_cast<char *>(face->GetName()));
                StringKeyParser::Result key = {};
                StringKeyParser::ParseKey(&key, face->GetName(), 0, nullptr);
                VirtualCall<uint8_t(__thiscall *)(void *, void *, void *, void *), 14>(manager, level.Get(), &key,
                                                                                       face->GetSegment());
            }
        }
        const float *const colour = mEntries[i].mColour;
        writer->SetUint32(ColourByte(colour[0]) << 24 | ColourByte(colour[1]) << 16 | ColourByte(colour[2]) << 8 |
                          ColourByte(colour[3]));
        writer->SetFloat32(mEntries[i].mScale);
    }
}

uint32_t cImageData::GetOwnerInfo() { return cSGResource::GetOwnerInfo(); }

const void *cImageData::GetRecord18() { return ts2_rva_db0818; }
const void *cImageData::GetRecord19() { return ts2_rva_da6450; }

// ---- adapters ----

void __fastcall cImageDataAdapters::Destruct(Self *self, void *) { [[msvc::forceinline_calls]] self->cImageData::~cImageData(); }
bool __fastcall cImageDataAdapters::QueryInterface(Self *self, void *, uint32_t iid, void **out)
{
    [[msvc::forceinline_calls]] return self->cImageData::QueryInterface(iid, out);
}
void __fastcall cImageDataAdapters::Clone(Self *self, void *, cIImageData **out) { [[msvc::forceinline_calls]] self->cImageData::Clone(out); }
cISGResource *__fastcall cImageDataAdapters::GetResource(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->cImageData::GetResource();
}
void __fastcall cImageDataAdapters::Init(Self *self, void *, int32_t width, int32_t height, uint32_t levels, int32_t format,
                                         float scale, uint32_t entries)
{
    [[msvc::forceinline_calls]] self->cImageData::Init(width, height, levels, format, scale, entries);
}
uint8_t *__fastcall cImageDataAdapters::LevelData(Self *self, void *, uint32_t level)
{
    [[msvc::forceinline_calls]] return self->cImageData::LevelData(level);
}
void __fastcall cImageDataAdapters::SetEntryColour(Self *self, void *, const float *colour)
{
    [[msvc::forceinline_calls]] self->cImageData::SetEntryColour(colour);
}
void __fastcall cImageDataAdapters::SetEntryScale(Self *self, void *, float value)
{
    [[msvc::forceinline_calls]] self->cImageData::SetEntryScale(value);
}
void __fastcall cImageDataAdapters::SetName(Self *self, void *, const char *text)
{
    [[msvc::forceinline_calls]] self->cImageData::SetName(text);
}
void __fastcall cImageDataAdapters::DropTopLevels(Self *self, void *, uint32_t n)
{
    [[msvc::forceinline_calls]] self->cImageData::DropTopLevels(n);
}
cISGResource *__fastcall cImageDataAdapters::LevelResource(Self *self, void *, uint32_t level)
{
    [[msvc::forceinline_calls]] return self->cImageData::LevelResource(level);
}
void __fastcall cImageDataAdapters::SetLevelFlag(Self *self, void *, uint32_t level, uint8_t value)
{
    [[msvc::forceinline_calls]] self->cImageData::SetLevelFlag(level, value);
}
uint8_t __fastcall cImageDataAdapters::HasPixels(Self *self, void *) { [[msvc::forceinline_calls]] return self->cImageData::HasPixels(); }
int32_t __fastcall cImageDataAdapters::EntryCount(Self *self, void *) { [[msvc::forceinline_calls]] return self->cImageData::EntryCount(); }
int32_t __fastcall cImageDataAdapters::LevelWidth(Self *self, void *, uint32_t level)
{
    [[msvc::forceinline_calls]] return self->cImageData::LevelWidth(level);
}
int32_t __fastcall cImageDataAdapters::LevelHeight(Self *self, void *, uint32_t level)
{
    [[msvc::forceinline_calls]] return self->cImageData::LevelHeight(level);
}
int32_t __fastcall cImageDataAdapters::LevelPitch(Self *self, void *, uint32_t level)
{
    [[msvc::forceinline_calls]] return self->cImageData::LevelPitch(level);
}
uint32_t __fastcall cImageDataAdapters::TotalBytes(Self *self, void *) { [[msvc::forceinline_calls]] return self->cImageData::TotalBytes(); }
uint32_t __fastcall cImageDataAdapters::LevelBytes(Self *self, void *, uint32_t level)
{
    [[msvc::forceinline_calls]] return self->cImageData::LevelBytes(level);
}
uint32_t __fastcall cImageDataAdapters::LevelCount(Self *self, void *) { [[msvc::forceinline_calls]] return self->cImageData::LevelCount(); }
void *__fastcall cImageDataAdapters::PixelFormat(Self *self, void *) { [[msvc::forceinline_calls]] return self->cImageData::PixelFormat(); }
float *__fastcall cImageDataAdapters::GetEntryColour(Self *self, void *, float *out)
{
    [[msvc::forceinline_calls]] return self->cImageData::GetEntryColour(out);
}
float __fastcall cImageDataAdapters::GetEntryScale(Self *self, void *) { [[msvc::forceinline_calls]] return self->cImageData::GetEntryScale(); }
void __fastcall cImageDataAdapters::RegenerateMips(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->cImageData::RegenerateMips();
}

bool __fastcall cImageDataAdapters::SetFormat(Self *self, void *, int32_t format)
{
    [[msvc::forceinline_calls]] return self->cImageData::SetFormat(format);
}

float *__fastcall cImageDataAdapters::GetPixelRGB(Self *self, void *, float *out, int32_t x, int32_t y, int32_t level)
{
    [[msvc::forceinline_calls]] return self->cImageData::GetPixelRGB(out, x, y, level);
}

float __fastcall cImageDataAdapters::GetPixelChannel(Self *self, void *, int32_t x, int32_t y, int32_t level)
{
    [[msvc::forceinline_calls]] return self->cImageData::GetPixelChannel(x, y, level);
}

const char *__fastcall cImageDataAdapters::GetNameText(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->cImageData::GetNameText();
}
void __fastcall cImageDataAdapters::Read(void *face, void *, ifaces::cIGZPersistReader *reader)
{
    [[msvc::forceinline_calls]] FromFace(face)->cImageData::Read(reader);
}
cPersistTypeInfo *__fastcall cImageDataAdapters::GetTypeInfo(void *face, void *, cPersistTypeInfo *out)
{
    [[msvc::forceinline_calls]] *out = FromFace(face)->cImageData::GetTypeInfo();
    return out;
}
void __fastcall cImageDataAdapters::Write(void *face, void *, ifaces::cIGZPersistWriter *writer)
{
    [[msvc::forceinline_calls]] FromFace(face)->cImageData::Write(writer);
}
bool __fastcall cImageDataAdapters::ReadLevels(Self *self, void *, ifaces::cIGZPersistReader *reader,
                                               const cPersistTypeInfo *info, uint32_t skipBytes, void *manager,
                                               ImageEntry *entry, cRZString *missingName)
{
    [[msvc::forceinline_calls]] return self->cImageData::ReadLevels(reader, info, skipBytes, manager, entry, missingName);
}

// ---- the virtual base's thunks (hook-shaped, kept) ----

void *cImageDataSubobject::VirtualBaseIfObject()
{
    // sub ecx,[ecx-4]; sub ecx,0x38; jmp 0xa3c750. The same adjustment that cAnimResource's thunks make, with 0x38.
    const uintptr_t address = reinterpret_cast<uintptr_t>(this);
    const uint32_t displacement = *reinterpret_cast<const uint32_t *>(address - 4);
    auto *secondary = reinterpret_cast<SGResourceHelpers::cSGSecondary *>(address - displacement - 0x38);
    return secondary->VirtualBaseIfObject();
}

void *cImageDataSubobject::ConstantSlot18()
{
    // sub ecx,[ecx-4] (the read faults on a null this, as the original's does); the body returns a constant.
    (void)*reinterpret_cast<volatile const uint32_t *>(reinterpret_cast<uintptr_t>(this) - 4);
    return const_cast<char *>(ts2_rva_db0818);
}

void *cImageDataSubobject::ConstantSlot19()
{
    (void)*reinterpret_cast<volatile const uint32_t *>(reinterpret_cast<uintptr_t>(this) - 4);
    return const_cast<char *>(ts2_rva_da6450);
}

} // namespace nGZSceneGraph
