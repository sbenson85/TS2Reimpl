// The graphic system's mode and format searches and the device switch (lane RJ): src/gz/GraphicModeSearch.h.
#include "gz/GraphicModeSearch.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cCanvasW32.h" // cICanvasW32Behavior, cICanvasMonitor
#include "gz/cDeviceDX.h"  // cIDeviceDX, cDeviceSettings
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGraphicSystemDX.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

namespace nGZGraphic4 {

namespace {

constexpr uint32_t kArgbFaceIid = 0x9ac38991;         // cIPixelFormatARGB
constexpr uint32_t kDepthStencilFaceIid = 0x5ac389b6; // cIPixelFormatDepthStencil
constexpr uint32_t kGraphicSystemId = 0x85522803, kGraphicSystemIid = 0xa55227bb;
constexpr uint32_t kWindowServiceId = 0xfb6372ee, kWindowBehaviorIid = 0x5b637209;
constexpr int32_t kCurrentDevice = -1;
constexpr uint32_t kUnset = 0x3fffffff; // the mode search's "nothing yet" width, height and unused bits

// A format's face for `iid`, released at once (the formats are static), or null.
void *FaceOf(cIPixelFormat *format, uint32_t iid)
{
    void *face = nullptr;
    format->QueryInterface(iid, &face);
    if (face) static_cast<cIGZUnknown *>(face)->Release();
    return face;
}

// The ARGB face's slots (gz/PixelFormat.h, cPixelFormatARGB).
int32_t BitsPerPixel(void *face) { return VirtualCall<int32_t(__thiscall *)(void *), 6>(face); }
int32_t AlphaBits(void *face) { return VirtualCall<int32_t(__thiscall *)(void *), 11>(face); }
int32_t UnusedBits(void *face) { return VirtualCall<int32_t(__thiscall *)(void *), 15>(face); }
// The depth-stencil face's slots (cPixelFormatDepthStencil).
int32_t DepthBits(void *face) { return VirtualCall<int32_t(__thiscall *)(void *), 11>(face); }
bool IsFloatDepth(void *face) { return VirtualCall<bool(__thiscall *)(void *), 12>(face); }
int32_t StencilBits(void *face) { return VirtualCall<int32_t(__thiscall *)(void *), 13>(face); }

template <class Iterator> void ReleaseRange(const cIteratorRange<Iterator> &range)
{
    if (range.mEnd) range.mEnd->Release();
    if (range.mBegin) range.mBegin->Release();
    if (range.mCurrent) range.mCurrent->Release();
}

// A framework service held for a scope (RZAutoRef, released through 0x1466e0's guarded release).
class HeldService {
public:
    HeldService(uint32_t id, uint32_t iid)
    {
        mRef.mObject = nullptr;
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &mRef.mObject);
    }
    ~HeldService() { mRef.ReleaseObject(); }
    HeldService(const HeldService &) = delete;
    HeldService &operator=(const HeldService &) = delete;
    void *Get() const { return mRef.mObject; }

private:
    RZAutoRef mRef;
};

bool SameMode(const cDisplayMode &a, const cDisplayMode &b)
{
    return a.mWidth == b.mWidth && a.mHeight == b.mHeight && a.mAspect == b.mAspect && a.mRefresh == b.mRefresh &&
           a.mFormat == b.mFormat;
}

} // namespace

bool __cdecl FindClosestDisplayMode(cDisplayMode *mode, cDisplayModeRange modes, uint32_t minWidth, uint32_t minHeight,
                                    int32_t minBits, int32_t maxBits, uint32_t minRefresh, uint32_t maxRefresh)
{
    cIDisplayModeIterator *const it = modes.mCurrent;
    cDisplayMode best{kUnset, kUnset, 0.0f, 0, nullptr};
    while (!it->IsEqual(modes.mEnd)) {
        void *const argb = FaceOf(it->Get().mFormat, kArgbFaceIid);
        if (it->Get().mWidth >= minWidth && it->Get().mHeight >= minHeight && BitsPerPixel(argb) >= minBits &&
            BitsPerPixel(argb) <= maxBits && it->Get().mRefresh >= minRefresh && it->Get().mRefresh <= maxRefresh) {
            const uint32_t width = it->Get().mWidth;
            const uint32_t area = it->Get().mHeight * width; // 32-bit products: the unset best is 0x80000001
            const uint32_t bestArea = best.mHeight * best.mWidth;
            uint32_t bestBits = 0, bestUnused = kUnset;
            if (best.mFormat) {
                bestBits = uint32_t(BitsPerPixel(FaceOf(best.mFormat, kArgbFaceIid)));
                bestUnused = uint32_t(UnusedBits(FaceOf(best.mFormat, kArgbFaceIid)));
            }
            bool better = area < bestArea;
            if (!better && area == bestArea) {
                better = it->Get().mWidth < best.mWidth ||
                         (it->Get().mWidth == best.mWidth && bestBits < uint32_t(BitsPerPixel(argb))) ||
                         (it->Get().mWidth == best.mWidth && bestBits == uint32_t(BitsPerPixel(argb)) &&
                          uint32_t(UnusedBits(argb)) < bestUnused) ||
                         (it->Get().mWidth == best.mWidth && bestBits == uint32_t(BitsPerPixel(argb)) &&
                          uint32_t(UnusedBits(argb)) == bestUnused && it->Get().mRefresh < best.mRefresh);
            }
            if (better) best = it->Get();
        }
        it->Next();
    }
    const bool found = best.mWidth != kUnset;
    if (found) *mode = best;
    ReleaseRange(modes);
    return found;
}

cIPixelFormat *__cdecl FindClosestColorFormat(cPixelFormatRange formats, int32_t minBits, int32_t minAlphaBits)
{
    cIPixelFormatIterator *const it = formats.mCurrent;
    void *best = nullptr;
    while (!it->IsEqual(formats.mEnd)) {
        void *const argb = FaceOf(it->Get(), kArgbFaceIid);
        if (argb && BitsPerPixel(argb) >= minBits && AlphaBits(argb) >= minAlphaBits) {
            bool better = !best || BitsPerPixel(argb) < BitsPerPixel(best);
            if (!better) {
                const int32_t bits = BitsPerPixel(argb), bestBits = BitsPerPixel(best);
                if (minAlphaBits != 0) {
                    better = (bits == bestBits && UnusedBits(argb) < UnusedBits(best)) ||
                             (BitsPerPixel(argb) == BitsPerPixel(best) && UnusedBits(argb) == UnusedBits(best) &&
                              AlphaBits(argb) < AlphaBits(best));
                } else {
                    better = bits == bestBits && AlphaBits(argb) < AlphaBits(best);
                }
            }
            if (better) best = argb;
        }
        it->Next();
    }
    ReleaseRange(formats);
    return static_cast<cIPixelFormat *>(best);
}

cIPixelFormat *__cdecl FindClosestDepthFormat(cPixelFormatRange formats, int32_t minDepthBits, int32_t minStencilBits,
                                              bool floatDepth)
{
    cIPixelFormatIterator *const it = formats.mCurrent;
    void *best = nullptr;
    while (!it->IsEqual(formats.mEnd)) {
        void *const depth = FaceOf(it->Get(), kDepthStencilFaceIid);
        if (depth && DepthBits(depth) >= minDepthBits && StencilBits(depth) >= minStencilBits &&
            (!floatDepth || IsFloatDepth(depth))) {
            const bool better = !best || (IsFloatDepth(depth) && !IsFloatDepth(best)) ||
                                DepthBits(depth) > DepthBits(best) ||
                                (DepthBits(depth) == DepthBits(best) && StencilBits(depth) < StencilBits(best));
            if (better) best = depth;
        }
        it->Next();
    }
    ReleaseRange(formats);
    return static_cast<cIPixelFormat *>(best);
}

cIPixelFormat *__cdecl FindFormatLowerBound(cPixelFormatRange formats, cIPixelFormat *wanted)
{
    cIPixelFormatIterator *const it = formats.mCurrent;
    cIPixelFormat *format = nullptr;
    while (!it->IsEqual(formats.mEnd)) {
        format = it->Get();
        if (wanted == format || wanted->Less(format, false)) break;
        it->Next();
    }
    ReleaseRange(formats);
    // ORIGINAL-BUG: with no format at or after `wanted`, the last one walked over is the answer, not null.
    return format;
}

bool __cdecl SetDeviceMode(int32_t device, const cDisplayMode *mode, cIPixelFormat *colorFormat, cIPixelFormat *depthFormat,
                           uint32_t backBufferCount, uint32_t multisampleType, uint32_t multisampleQuality, uint32_t flags)
{
    HeldService held(kGraphicSystemId, kGraphicSystemIid);
    // Used unchecked, as in the original.
    ifaces::cIGraphicSystemDX *const system = As<ifaces::cIGraphicSystemDX>(held.Get());
    const int32_t current = system->GetCurrentDevice();
    if (current < 0) return false;
    if (device == kCurrentDevice) device = current;
    if (uint32_t(device) >= system->GetDeviceCount()) return false;

    cIDeviceDX *const old = static_cast<cIDeviceDX *>(system->GetDevice(current));
    cIDeviceDX *const target = static_cast<cIDeviceDX *>(system->GetDevice(device));
    const cDeviceSettings saved = old->GetSettings();
    const cDeviceSettings settings{*mode,           colorFormat,      depthFormat,         backBufferCount,
                                   multisampleType, multisampleQuality, flags};
    const bool hadDevice = old->HasDeviceOrLost();
    if (device != current) old->ReleaseDevice();
    system->SetCurrentDevice(device);
    if (SameMode(*mode, ts2_rva_10ca868) && device != system->FindWindowDevice()) {
        // The window service (0x18ae10's holder), used unchecked.
        HeldService window(kWindowServiceId, kWindowBehaviorIid);
        static_cast<cICanvasW32Behavior *>(window.Get())
            ->PlaceOnMonitor(static_cast<cICanvasMonitor *>(target->GetDisplayMonitor()));
    }
    if (target->ApplySettings(&settings)) return true;
    target->ReleaseDevice();
    system->SetCurrentDevice(current);
    if (hadDevice && !old->ApplySettings(&saved)) old->OnDeviceLost();
    return false;
}

bool __cdecl SetDeviceDesktopMode(int32_t device, cIPixelFormat *colorFormat, cIPixelFormat *depthFormat,
                                  uint32_t backBufferCount, uint32_t multisampleType, uint32_t multisampleQuality,
                                  uint32_t flags)
{
    return SetDeviceMode(device, &ts2_rva_10ca868, colorFormat, depthFormat, backBufferCount, multisampleType,
                         multisampleQuality, flags);
}

} // namespace nGZGraphic4
