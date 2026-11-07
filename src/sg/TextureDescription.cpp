// nGZSceneGraph::ChooseTextureDescription (lane TH): see TextureDescription.h.
#include "sg/TextureDescription.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicModeSearch.h" // FindClosestColorFormat
#include "gz/GraphicServices.h"
#include "gz/PixelFormat.h"       // the interface ids
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cGZEventReflector.h" // ReleaseIteratorRange
#include "sg/cTextureManager.h"

namespace nGZSceneGraph {

namespace {

using nGZGraphic4::cIDeviceDX;
using nGZGraphic4::cIDeviceEnum;
using nGZGraphic4::cIPixelFormat;
using nGZGraphic4::cIPixelFormatIterator;
using nGZGraphic4::cPixelFormatRange;

constexpr uint32_t kKindTexture = 4, kKindCube = 6;
constexpr uint32_t kAllowTrueColor = 1; // the texture manager's filter flags (TextureFilterParams::a)
constexpr uint32_t kAllowDxt = 2;

// The pixel format's face `iid` (QueryInterface, then the reference let go again): borrowed, or null.
void *Face(void *format, uint32_t iid)
{
    void *face = nullptr;
    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(format, iid, &face);
    if (face) VirtualCall<uint32_t(__thiscall *)(void *), 2>(face);
    return face;
}

// The ARGB face's slots (gz/PixelFormat.h).
int32_t BitsPerPixel(void *argb) { return VirtualCall<int32_t(__thiscall *)(void *), 6>(argb); }
int32_t AlphaBits(void *argb) { return VirtualCall<int32_t(__thiscall *)(void *), 11>(argb); }
int32_t RedBits(void *argb) { return VirtualCall<int32_t(__thiscall *)(void *), 12>(argb); }
int32_t GreenBits(void *argb) { return VirtualCall<int32_t(__thiscall *)(void *), 13>(argb); }
int32_t BlueBits(void *argb) { return VirtualCall<int32_t(__thiscall *)(void *), 14>(argb); }
// The DXT face's slot 15: 1 for DXT1 (one bit of alpha).
uint32_t DxtVersion(void *dxt) { return VirtualCall<uint32_t(__thiscall *)(void *), 15>(dxt); }

// The formats manager's GetARGB (slot 6).
void *ArgbFormat(uint32_t bits, uint32_t alpha, uint32_t red, uint32_t green, uint32_t blue)
{
    return VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 6>(
        nGZGraphic4::GetPixelFormatsManagerCached(), bits, alpha, red, green, blue);
}

bool Supported(cIDeviceEnum *formats, void *format, uint32_t kind)
{
    return formats->IsColorFormatSupportedForDisplay(static_cast<cIPixelFormat *>(format), kind);
}

// The range's copy constructor (0x167180, thiscall; inline here in the original's first call): the cursor cloned and
// counted, then the ends counted.
cPixelFormatRange CopyRange(const cPixelFormatRange &range)
{
    cPixelFormatRange copy;
    copy.mCurrent = range.mCurrent->Clone();
    if (copy.mCurrent) copy.mCurrent->AddRef();
    copy.mBegin = range.mBegin;
    if (copy.mBegin) copy.mBegin->AddRef();
    copy.mEnd = range.mEnd;
    if (copy.mEnd) copy.mEnd->AddRef();
    return copy;
}

// The range's cursor put back at its begin (a counted clone of it) unless it is there already.
void Rewind(cPixelFormatRange &range)
{
    if (range.mCurrent->IsEqual(range.mBegin)) return;
    cIPixelFormatIterator *const begin = range.mBegin->Clone();
    cIPixelFormatIterator *const old = range.mCurrent;
    if (begin == old) return;
    if (begin) begin->AddRef();
    range.mCurrent = begin;
    if (old) old->Release();
}

// The device caps' texture flags (D3DCAPS9::TextureCaps) the fits read.
constexpr uint32_t kPow2 = 0x2;                // D3DPTEXTURECAPS_POW2
constexpr uint32_t kSquareOnly = 0x20;         // D3DPTEXTURECAPS_SQUAREONLY
constexpr uint32_t kNonPow2Conditional = 0x100; // D3DPTEXTURECAPS_NONPOW2CONDITIONAL
constexpr uint32_t kMipMap = 0x4000;           // D3DPTEXTURECAPS_MIPMAP
constexpr uint32_t kCubeMapPow2 = 0x20000;     // D3DPTEXTURECAPS_CUBEMAP_POW2
constexpr uint32_t kMipCubeMap = 0x10000;      // D3DPTEXTURECAPS_MIPCUBEMAP
constexpr uint32_t kIidDeviceEnumFace = 0x6c02c311;

// The bits below the top one set (the smear the power-of-two roundings use).
uint32_t Smear(uint32_t bits)
{
    bits |= bits >> 16;
    bits |= bits >> 8;
    bits |= bits >> 4;
    bits |= bits >> 2;
    return bits >> 1 | bits;
}

// The class id the format answers (its slot 5).
uint32_t FormatClass(cIPixelFormat *format) { return VirtualCall<uint32_t(__thiscall *)(void *), 5>(format); }

} // namespace

__declspec(noinline) void __cdecl AdjustTextureSize(cIDeviceDX *device, cIPixelFormat *format, int32_t *width,
                                                     int32_t *height, uint32_t *levels, bool allowNonPow2)
{
    void *enumeration = device->GetDeviceEnum();
    void *face = nullptr;
    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(enumeration, kIidDeviceEnumFace, &face);
    // ORIGINAL-BUG: a failed query leaves `face` null and the next call faults.
    const D3DCAPS9 *caps = static_cast<cIDeviceEnum *>(face)->GetCurrentCaps();
    const bool isDxt = FormatClass(format) == nGZGraphic4::kIidDXT;
    // Any size, when the caller allows it and the device takes non-power-of-two textures conditionally (not DXT).
    const bool anySize = allowNonPow2 && (caps->TextureCaps & (kPow2 | kNonPow2Conditional)) ==
                                             (kPow2 | kNonPow2Conditional) && !isDxt;

    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
    if (!anySize && (caps->TextureCaps & kPow2)) {
        if (!allowNonPow2 || *width <= 32 || *height <= 32) {
            // The next power of two at or above each.
            *width = static_cast<int32_t>(Smear(static_cast<uint32_t>(*width) - 1) + 1);
            *height = static_cast<int32_t>(Smear(static_cast<uint32_t>(*height) - 1) + 1);
        } else {
            // The power of two at or below each.
            *width = static_cast<int32_t>((Smear(static_cast<uint32_t>(*width)) + 1) >> 1);
            *height = static_cast<int32_t>((Smear(static_cast<uint32_t>(*height)) + 1) >> 1);
        }
    }
    if (isDxt) {
        *width = (*width + 3) & ~3;
        *height = (*height + 3) & ~3;
    }
    if (FormatClass(format) == nGZGraphic4::kIidYUV) *width = (*width + 1) & ~1;
    // Held to the caps' maximum width and height (signed comparisons).
    if (static_cast<int32_t>(caps->MaxTextureWidth) < *width) *width = static_cast<int32_t>(caps->MaxTextureWidth);
    if (static_cast<int32_t>(caps->MaxTextureHeight) < *height) *height = static_cast<int32_t>(caps->MaxTextureHeight);

    // The aspect ratio: square only, else the caps' maximum (none when 0). The shorter side is raised to the longer
    // over the ratio (an unsigned division, a signed comparison).
    const uint32_t ratio = (caps->TextureCaps & kSquareOnly) ? 1 : caps->MaxTextureAspectRatio;
    if (ratio != 0) {
        const int32_t w = *width, h = *height;
        if (h < w) {
            const int32_t least = static_cast<int32_t>(static_cast<uint32_t>(w) / ratio);
            *height = h < least ? least : h;
        } else {
            const int32_t least = static_cast<int32_t>(static_cast<uint32_t>(h) / ratio);
            *width = w < least ? least : w;
        }
    }

    if (anySize || !(caps->TextureCaps & kMipMap)) {
        *levels = 1;
    } else {
        int32_t wanted = static_cast<int32_t>(*levels);
        if (wanted < 0) wanted = 0;
        *levels = static_cast<uint32_t>(wanted);
        if (wanted != 0) {
            // One level per halving until both sides are 1, held to the levels asked for.
            int32_t w = *width, h = *height;
            int32_t count = 1;
            while (w != 1 || h != 1) {
                w >>= 1;
                if (w < 1) w = 1;
                h >>= 1;
                ++count;
                if (h < 1) h = 1;
            }
            if (wanted < count) count = wanted;
            *levels = static_cast<uint32_t>(count);
        }
    }
    if (face) VirtualCall<uint32_t(__thiscall *)(void *), 2>(face);
}

__declspec(noinline) void __cdecl AdjustCubeTextureSize(cIDeviceDX *device, cIPixelFormat *format, int32_t *edge,
                                                         uint32_t *levels)
{
    // The device's enumeration, asked for its enum face (a counted reference, let go at the end).
    void *enumeration = device->GetDeviceEnum();
    void *face = nullptr;
    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(enumeration, kIidDeviceEnumFace, &face);
    // ORIGINAL-BUG: a failed query leaves `face` null and the next call faults.
    const D3DCAPS9 *caps = static_cast<cIDeviceEnum *>(face)->GetCurrentCaps();

    int32_t size = *edge;
    if (size < 1) size = 1;
    *edge = size;
    if (caps->TextureCaps & kCubeMapPow2) {
        // The next power of two at or above the edge (the smear of the bits below the top one).
        uint32_t bits = static_cast<uint32_t>(size) - 1;
        bits |= bits >> 16;
        bits |= bits >> 8;
        bits |= bits >> 4;
        bits |= bits >> 2;
        *edge = static_cast<int32_t>((bits >> 1 | bits) + 1);
    }
    if (VirtualCall<uint32_t(__thiscall *)(void *), 5>(format) == nGZGraphic4::kIidDXT) *edge = (*edge + 3) & ~3;
    if (VirtualCall<uint32_t(__thiscall *)(void *), 5>(format) == nGZGraphic4::kIidYUV) *edge = (*edge + 1) & ~1;
    // The smaller of the caps' maximum width and height (an unsigned comparison), then the edge held to it (signed).
    uint32_t limit = caps->MaxTextureWidth;
    if (caps->MaxTextureHeight < caps->MaxTextureWidth) limit = caps->MaxTextureHeight;
    if (static_cast<int32_t>(limit) < *edge) *edge = static_cast<int32_t>(limit);

    if (!(caps->TextureCaps & kMipCubeMap)) {
        *levels = 1;
    } else {
        int32_t wanted = static_cast<int32_t>(*levels);
        if (wanted < 0) wanted = 0;
        *levels = static_cast<uint32_t>(wanted);
        if (wanted != 0) {
            // One level per halving of the edge down to 1, held to the levels asked for.
            int32_t value = *edge;
            int32_t count = 1;
            while (value != 1) {
                value >>= 1;
                if (value < 1) value = 1;
                ++count;
            }
            if (wanted < count) count = wanted;
            *levels = static_cast<uint32_t>(count);
        }
    }
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(face);
}

bool __cdecl ChooseTextureDescription(TextureDescription *desc, int32_t width, int32_t height, uint32_t levels,
                                      cIPixelFormat *format, bool normalMap, uint32_t kind)
{
    cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    auto *const formats = static_cast<cIDeviceEnum *>(static_cast<void *>(device->GetDeviceEnumConst()));
    *desc = TextureDescription{};
    const uint32_t flags = TextureManager()->GetFilterParams().a;

    void *const dxt = Face(format, nGZGraphic4::kIidDXT);
    void *argb = Face(format, nGZGraphic4::kIidARGB);
    Face(format, nGZGraphic4::kIidAlphaLuminance); // asked and let go, unused

    // The format as given (or X8R8G8B8 for a normal map), when the device takes it and the flags allow it.
    void *chosen = format;
    bool supported;
    if (normalMap) {
        chosen = argb = ArgbFormat(24, 0, 8, 8, 8);
        supported = Supported(formats, chosen, kind);
    } else {
        supported = Supported(formats, format, kind);
    }
    cIPixelFormat *result = nullptr;
    bool found = false;
    if (supported && chosen) {
        const bool allowTrueColor = (flags & kAllowTrueColor) != 0;
        const bool allowDxt = (flags & kAllowDxt) != 0;
        void *const chosenDxt = Face(chosen, nGZGraphic4::kIidDXT);
        void *const chosenArgb = Face(chosen, nGZGraphic4::kIidARGB);
        if (chosenDxt ? allowDxt : (BitsPerPixel(chosenArgb) <= 16 || allowTrueColor)) {
            result = static_cast<cIPixelFormat *>(chosen);
            found = true;
        }
    }

    if (!found) {
        // A DXT image's own format, or the closest colour format for its bits.
        int32_t colorBits, alphaBits;
        if (dxt) {
            if ((flags & kAllowDxt) && Supported(formats, dxt, kind)) result = static_cast<cIPixelFormat *>(dxt);
            alphaBits = DxtVersion(dxt) == 1 ? 1 : 8;
            colorBits = (flags & kAllowTrueColor) ? 24 : 12;
            found = result != nullptr;
        } else {
            alphaBits = AlphaBits(argb);
            const int32_t blue = BlueBits(argb);
            const int32_t green = GreenBits(argb);
            colorBits = blue + green + RedBits(argb);
            if (!(flags & kAllowTrueColor) && colorBits > 16) colorBits = 16;
        }
        if (!found) {
            cPixelFormatRange range = formats->GetColorFormatsForDisplay(kind, 0);
            result = nGZGraphic4::FindClosestColorFormat(CopyRange(range), colorBits, alphaBits);
            if (!result) {
                Rewind(range);
                result = nGZGraphic4::FindClosestColorFormat(CopyRange(range), colorBits / 2, alphaBits / 2);
                if (!result) {
                    nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
                    return false;
                }
            }
            nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
        }
    }

    if (kind == kKindTexture) {
        AdjustTextureSize(device, result, &width, &height, &levels, false);
    } else if (kind == kKindCube) {
        width = (width + height) / 2;
        AdjustCubeTextureSize(device, result, &width, &levels);
        height = width;
    }
    desc->format = result;
    desc->width = width;
    desc->height = height;
    desc->levels = levels;
    return true;
}

} // namespace nGZSceneGraph
