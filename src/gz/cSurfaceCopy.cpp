// The surface copies of nGZGraphic4 (lane OS, rendering R3b step 4): src/gz/cSurfaceCopy.h.
#include "gz/cSurfaceCopy.h"

#include <cstring>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cSurfaceDX.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "rz/MiscHelpersA.h" // LogD3DError

namespace nGZGraphic4 {

namespace {

// The game's data these functions read.
// The device enumeration's kind for a surface's class (0-5) and the IID GetContainer is asked for (the surface's
// texture): ts2_rva_d9db08 and ts2_rva_d9c8a8 (cSurfaceDX.h).

// The LogD3DError lines of the copies (the surfaces' source file).
constexpr uint32_t kLineSourceDesc = 0x4b3, kLineTargetDesc = 0x4c4, kLineEndScene = 0x568, kLineBeginScene = 0x576;
constexpr uint32_t kLineLockedSourceDesc = 0x5af, kLineLockedTargetDesc = 0x5ba, kLineLockedUnlockSource = 0x5d1;
constexpr uint32_t kLineUnlockSource = 0x5ea, kLineUnlockTarget = 0x5eb, kLineTwinDesc = 0x5f2, kLineTwinDevice = 0x5f5;

constexpr DWORD kLockSource = D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK, kLockTarget = D3DLOCK_NOSYSLOCK;
constexpr HRESULT kFail = E_FAIL; // 0x80004005
constexpr uint32_t kFilterPointCaps = 0x1000100, kFilterLinearCaps = 0x2000200; // min and mag, point / linear
constexpr uint32_t kAccessLockable = 1, kAccessDepthStencil = 2, kAccessRenderTarget = 4, kAccessTextureLevel = 8;
constexpr UINT kUsageDepthStencil = D3DUSAGE_DEPTHSTENCIL;

static_assert(offsetof(D3DCAPS9, StretchRectFilterCaps) == 0xf4, "D3DCAPS9::StretchRectFilterCaps at 0xf4");

void Report(HRESULT hr, uint32_t line)
{
    LogD3DError(static_cast<uint32_t>(hr), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts2_rva_d9dab0)),
                line);
}
void Check(HRESULT hr, uint32_t line)
{
    if (FAILED(hr)) Report(hr, line);
}

// The pixel formats manager's format for a D3DFORMAT (0x18acd0 is the getter of the manager's static instance).
cIPixelFormat *FormatOf(uint32_t d3dFormat)
{
    return static_cast<cIPixelFormat *>(
        As<ifaces::cIPixelFormatsManager>(GetPixelFormatsManagerCached())->GetFormat(d3dFormat));
}

// The index into the kinds table: depth stencil 2, else render target 4, plus 1 for a texture level.
uint32_t KindIndex(uint32_t flags)
{
    uint32_t kind = 0;
    if (flags & kAccessDepthStencil)
        kind = 2;
    else if (flags & kAccessRenderTarget)
        kind = 4;
    return (flags & kAccessTextureLevel) ? kind | 1 : kind;
}

// The pieces of a system-memory twin (what 0x161b20 made), released cube, texture, surface.
struct Twin {
    IDirect3DSurface9 *surface = nullptr;
    IDirect3DTexture9 *texture = nullptr;
    IDirect3DCubeTexture9 *cube = nullptr;

    void Release()
    {
        if (cube) {
            cube->Release();
            cube = nullptr;
        }
        if (texture) {
            texture->Release();
            texture = nullptr;
        }
        if (surface) {
            surface->Release();
            surface = nullptr;
        }
    }
};

// A default-pool, non-dynamic surface that has a container (it is a texture's level) can't be locked.
bool InTextureContainer(IDirect3DSurface9 *surface, const D3DSURFACE_DESC &desc)
{
    if (desc.Pool != D3DPOOL_DEFAULT || (desc.Usage & D3DUSAGE_DYNAMIC)) return false;
    IUnknown *container = nullptr;
    const HRESULT hr = surface->GetContainer(ts2_rva_d9c8a8, reinterpret_cast<void **>(&container));
    if (container) container->Release();
    return SUCCEEDED(hr);
}

} // namespace

// 0x18ce70.
bool ClipCopyRects(RECT *sourceRect, const RECT *sourceBounds, RECT *targetRect, const RECT *targetBounds)
{
    const RECT source = *sourceRect;
    if (sourceBounds->right < source.left || sourceBounds->left > source.right || sourceBounds->bottom < source.top ||
        sourceBounds->top > source.bottom)
        return false;
    // The source cut to its bounds.
    int32_t left = source.left > sourceBounds->left ? source.left : sourceBounds->left;
    int32_t top = source.top > sourceBounds->top ? source.top : sourceBounds->top;
    int32_t right = source.right < sourceBounds->right ? source.right : sourceBounds->right;
    int32_t bottom = source.bottom < sourceBounds->bottom ? source.bottom : sourceBounds->bottom;

    // The same cut as fractions of the target rectangle.
    const float targetWidth = static_cast<float>(targetRect->right - targetRect->left);
    const int32_t targetHeightInt = targetRect->bottom - targetRect->top;
    const float sourceWidth = static_cast<float>(source.right - source.left);
    const int32_t sourceHeightInt = source.bottom - source.top;
    const float targetHeight = static_cast<float>(targetHeightInt);
    const float targetLeft = (static_cast<float>(left - source.left) * targetWidth) / sourceWidth +
                             static_cast<float>(targetRect->left);
    const float targetTop = (static_cast<float>(top - source.top) * targetHeight) / static_cast<float>(sourceHeightInt) +
                            static_cast<float>(targetRect->top);
    const float targetRight = static_cast<float>(targetRect->right) -
                              (static_cast<float>(source.right - right) * targetWidth) / sourceWidth;
    const float targetBottom = static_cast<float>(targetRect->bottom) -
                               (static_cast<float>(source.bottom - bottom) * targetHeight) /
                                   static_cast<float>(sourceHeightInt);
    const float boundsLeft = static_cast<float>(targetBounds->left), boundsTop = static_cast<float>(targetBounds->top);
    const float boundsRight = static_cast<float>(targetBounds->right);
    const float boundsBottom = static_cast<float>(targetBounds->bottom);
    // The target cut to its bounds (comiss + jae: a NaN does not fail these).
    if (targetLeft >= boundsRight || boundsLeft >= targetRight || targetTop >= boundsBottom || boundsTop >= targetBottom)
        return false;
    const float cutLeft = boundsLeft > targetLeft ? boundsLeft : targetLeft;
    const float cutTop = boundsTop > targetTop ? boundsTop : targetTop;
    const float cutRight = boundsRight < targetRight ? boundsRight : targetRight;
    const float cutBottom = boundsBottom < targetBottom ? boundsBottom : targetBottom;

    // And back into the source.
    const int32_t newLeft = static_cast<int32_t>(((cutLeft - targetLeft) * sourceWidth) / targetWidth) + left;
    const int32_t newRight = right - static_cast<int32_t>(((targetRight - cutRight) * sourceWidth) / targetWidth);
    const int32_t newTop = static_cast<int32_t>(((cutTop - targetTop) * static_cast<float>(sourceHeightInt)) /
                                                static_cast<float>(targetHeightInt)) +
                           top;
    const int32_t newBottom = bottom - static_cast<int32_t>(((targetBottom - cutBottom) * static_cast<float>(sourceHeightInt)) /
                                                            static_cast<float>(targetHeightInt));
    if (newRight - newLeft < 1 || newBottom - newTop < 1) return false;
    if (1.0f > cutRight - cutLeft || 1.0f > cutBottom - cutTop) return false;
    sourceRect->right = newRight;
    sourceRect->left = newLeft;
    sourceRect->top = newTop;
    sourceRect->bottom = newBottom;
    targetRect->left = static_cast<int32_t>(cutLeft);
    targetRect->top = static_cast<int32_t>(cutTop);
    targetRect->right = static_cast<int32_t>(cutRight);
    targetRect->bottom = static_cast<int32_t>(cutBottom);
    return true;
}

// 0x162d60.
bool CanStretchRectDX(cISurfaceDX *source, const D3DSURFACE_DESC *sourceDesc, const RECT *sourceRect,
                      cISurfaceDX *target, const D3DSURFACE_DESC *targetDesc, const RECT *targetRect, uint32_t *filter,
                      bool wantFilter)
{
    if (source == target || sourceDesc->Pool != D3DPOOL_DEFAULT || targetDesc->Pool != D3DPOOL_DEFAULT) return false;
    const uint32_t sourceFlags = source->GetSurface()->GetAccessFlags();
    const uint32_t targetFlags = target->GetSurface()->GetAccessFlags();
    // A surface that is both a depth stencil and a render target has no kind to stretch.
    if ((sourceFlags & 6) == 6 || (targetFlags & 6) == 6) return false;
    cIPixelFormat *sourceFormat = FormatOf(sourceDesc->Format);
    cIPixelFormat *targetFormat = FormatOf(targetDesc->Format);
    auto *devices = static_cast<cIDeviceEnum *>(source->GetDevice()->GetDeviceEnum45());
    const cSurfaceExtent sourceSize{sourceDesc->Width, sourceDesc->Height};
    const cSurfaceExtent targetSize{targetDesc->Width, targetDesc->Height};
    const uint32_t *const kinds = ts2_rva_d9db08;
    if (!devices->CanStretchRect(kinds[KindIndex(sourceFlags)], &sourceSize, sourceRect, sourceFormat,
                                 kinds[KindIndex(targetFlags)], &targetSize, targetRect, targetFormat))
        return false;
    *filter = 0;
    const D3DCAPS9 *caps = devices->GetCurrentCaps();
    if (!wantFilter) {
        if ((caps->StretchRectFilterCaps & kFilterPointCaps) == kFilterPointCaps) *filter = 1;
    } else if ((caps->StretchRectFilterCaps & kFilterLinearCaps) == kFilterLinearCaps) {
        *filter = 2;
    }
    return true;
}

// 0x160ef0.
bool CopySurfaceRectLocked(IDirect3DSurface9 *source, const RECT *sourceRect, IDirect3DSurface9 *target,
                           const RECT *targetRect)
{
    D3DSURFACE_DESC sourceDesc{};
    Check(source->GetDesc(&sourceDesc), kLineLockedSourceDesc);
    if (InTextureContainer(source, sourceDesc)) return false;
    D3DSURFACE_DESC targetDesc{};
    Check(target->GetDesc(&targetDesc), kLineLockedTargetDesc);
    if (InTextureContainer(target, targetDesc)) return false;

    D3DLOCKED_RECT sourceLock, targetLock;
    if (FAILED(source->LockRect(&sourceLock, sourceRect, kLockSource))) return false;
    HRESULT hr = target->LockRect(&targetLock, targetRect, kLockTarget);
    if (FAILED(hr)) {
        hr = source->UnlockRect();
        if (FAILED(hr)) Report(hr, kLineLockedUnlockSource);
        return false;
    }
    cIPixelFormat *format = FormatOf(sourceDesc.Format);
    int32_t rows = (sourceRect->bottom - sourceRect->top) / static_cast<int32_t>(format->GetBlockHeight());
    if (rows < 1) rows = 1;
    const uint32_t rowBytes =
        static_cast<uint32_t>(static_cast<uint32_t>(format->GetBitsPerPixel()) * static_cast<uint32_t>(sourceRect->right - sourceRect->left)) >> 3;
    auto *from = static_cast<uint8_t *>(sourceLock.pBits);
    auto *to = static_cast<uint8_t *>(targetLock.pBits);
    for (int32_t row = 0; row < rows; ++row) {
        std::memcpy(to, from, rowBytes);
        from += sourceLock.Pitch;
        to += targetLock.Pitch;
    }
    Check(source->UnlockRect(), kLineUnlockSource);
    Check(target->UnlockRect(), kLineUnlockTarget);
    return true;
}

// 0x161b20.
bool MakeSystemMemorySurfaceLike(IDirect3DSurface9 **surface, IDirect3DTexture9 **texture,
                                 IDirect3DCubeTexture9 **cube, IDirect3DSurface9 *source)
{
    D3DSURFACE_DESC desc{};
    Check(source->GetDesc(&desc), kLineTwinDesc);
    IDirect3DDevice9 *device = nullptr;
    Check(source->GetDevice(&device), kLineTwinDevice);
    HRESULT hr = kFail;
    IDirect3DResource9 *container = nullptr;
    if (source->GetContainer(ts2_rva_d9c8a8, reinterpret_cast<void **>(&container)) == S_OK) {
        switch (container->GetType()) {
        case D3DRTYPE_TEXTURE:
            hr = device->CreateTexture(desc.Width, desc.Height, 1, 0, desc.Format, D3DPOOL_SYSTEMMEM, texture, nullptr);
            if (SUCCEEDED(hr)) hr = (*texture)->GetSurfaceLevel(0, surface);
            break;
        case D3DRTYPE_CUBETEXTURE:
            hr = device->CreateCubeTexture(desc.Width, 1, 0, desc.Format, D3DPOOL_SYSTEMMEM, cube, nullptr);
            if (SUCCEEDED(hr)) hr = (*cube)->GetCubeMapSurface(D3DCUBEMAP_FACE_POSITIVE_X, 0, surface);
            break;
        default: break;
        }
    } else {
        hr = device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, surface, nullptr);
    }
    const bool made = SUCCEEDED(hr);
    if (container) container->Release();
    if (device) device->Release();
    return made;
}

// 0x1614b0.
bool CopySurfaceRectDX(cISurfaceDX *source, const RECT *sourceRect, cISurfaceDX *target, const POINT *targetPoint)
{
    cIDeviceDX *const device = source->GetDevice();
    IDirect3DDevice9 *const d3d = device->GetD3DDevice();
    IDirect3DSurface9 *sourceSurface = nullptr;
    source->QueryInterface(kIidD3DSurface, reinterpret_cast<void **>(&sourceSurface)); // not counted
    D3DSURFACE_DESC sourceDesc{};
    Check(sourceSurface->GetDesc(&sourceDesc), kLineSourceDesc);

    // The source rectangle cut to its surface (nothing to copy when it lies past either edge).
    const int32_t width = static_cast<int32_t>(sourceDesc.Width), height = static_cast<int32_t>(sourceDesc.Height);
    if (width < sourceRect->left) return true;
    if (sourceRect->right < 0) return true;
    if (height < sourceRect->top) return true;
    if (sourceRect->bottom < 0) return true;
    RECT from;
    from.left = sourceRect->left < 0 ? 0 : sourceRect->left;
    from.top = sourceRect->top < 0 ? 0 : sourceRect->top;
    from.right = width >= sourceRect->right ? sourceRect->right : width;
    from.bottom = height >= sourceRect->bottom ? sourceRect->bottom : height;
    // Where it lands: the point moved by what the cut took off.
    POINT at;
    at.x = targetPoint->x - sourceRect->left + from.left;
    at.y = targetPoint->y + (from.top - sourceRect->top);

    IDirect3DSurface9 *targetSurface = nullptr;
    target->QueryInterface(kIidD3DSurface, reinterpret_cast<void **>(&targetSurface)); // not counted
    D3DSURFACE_DESC targetDesc{};
    Check(targetSurface->GetDesc(&targetDesc), kLineTargetDesc);

    // ... and cut at the target's far edges (the point is not checked against the near ones).
    const int32_t targetWidth = static_cast<int32_t>(targetDesc.Width), targetHeight = static_cast<int32_t>(targetDesc.Height);
    if (from.right - from.left + at.x > targetWidth) from.right = targetWidth - at.x + from.left;
    if (from.bottom - from.top + at.y > targetHeight) from.bottom = targetHeight - at.y + from.top;
    if (from.right - from.left <= 0) return true;
    if (from.bottom - from.top <= 0) return true;
    const RECT to{at.x, at.y, from.right - from.left + at.x, at.y + (from.bottom - from.top)};
    uint32_t filter = 0;
    Twin twin;
    bool done;

    if (sourceDesc.Usage & D3DUSAGE_RENDERTARGET) {
        if (targetDesc.Pool == D3DPOOL_SYSTEMMEM) {
            // The whole render target into a system-memory surface of its size: the one GetRenderTargetData fits.
            if (sourceDesc.Width == targetDesc.Width && sourceDesc.Height == targetDesc.Height && from.left == 0 &&
                from.top == 0 && from.right == width && from.bottom == height && at.x == 0 && at.y == 0) {
                device->WaitForGpu();
                return SUCCEEDED(d3d->GetRenderTargetData(sourceSurface, targetSurface));
            }
        } else if (targetDesc.Pool == D3DPOOL_DEFAULT) {
            if (CanStretchRectDX(source, &sourceDesc, &from, target, &targetDesc, &to, &filter, false) &&
                SUCCEEDED(d3d->StretchRect(sourceSurface, &from, targetSurface, &to, static_cast<D3DTEXTUREFILTERTYPE>(filter))))
                return true;
        }
        if (targetDesc.MultiSampleType != 0 || targetDesc.MultiSampleQuality != 0) return false;
        if ((source->GetSurface()->GetAccessFlags() & kAccessLockable) &&
            CopySurfaceRectLocked(sourceSurface, &from, targetSurface, &to))
            return true;
        // Through a system-memory twin of the render target.
        if (MakeSystemMemorySurfaceLike(&twin.surface, &twin.texture, &twin.cube, sourceSurface) &&
            SUCCEEDED(d3d->GetRenderTargetData(sourceSurface, twin.surface)))
            done = CopySurfaceRectLocked(twin.surface, &from, targetSurface, &to);
        else
            done = false;
        twin.Release();
        return done;
    }

    // The pools decide: the source's in the high word, the target's in the low.
    const int32_t pools = static_cast<int32_t>((static_cast<uint32_t>(sourceDesc.Pool) << 16) | static_cast<uint32_t>(targetDesc.Pool));
    HRESULT hr = kFail;
    auto lockedCopy = [&] { return CopySurfaceRectLocked(sourceSurface, &from, targetSurface, &to); };
    auto inRange = [&](int32_t first) { return pools >= first && pools <= first + 2; };
    if (pools > 0x10000) {
        if (pools == 0x20000) {
            return SUCCEEDED(d3d->UpdateSurface(sourceSurface, &from, targetSurface, &at));
        }
        if (pools > 0x20000) {
            if (pools > 0x30000) return inRange(0x30001) ? lockedCopy() : false;
            if (pools != 0x30000) return inRange(0x20001) ? lockedCopy() : false;
        } else {
            return inRange(0x10001) ? lockedCopy() : false;
        }
    } else if (pools != 0x10000) {
        if (pools > 3 || pools < 0) return false; // (the table's range: unsigned compare)
        if (pools != 0) return lockedCopy();
        // Default to default: StretchRect, with the scene ended around it for a depth stencil source in a scene.
        bool sceneEnded = false;
        if (sourceDesc.Usage & kUsageDepthStencil) {
            if (device->InScene()) {
                Check(d3d->EndScene(), kLineEndScene);
                sceneEnded = true;
            }
        }
        if (CanStretchRectDX(source, &sourceDesc, &from, target, &targetDesc, &to, &filter, false))
            hr = d3d->StretchRect(sourceSurface, &from, targetSurface, &to, static_cast<D3DTEXTUREFILTERTYPE>(filter));
        if (sceneEnded) Check(d3d->BeginScene(), kLineBeginScene);
        return SUCCEEDED(hr);
    }

    // A managed or scratch source into a default-pool target: locked when the target can be, else through a
    // system-memory twin of the target and UpdateSurface.
    if ((target->GetSurfaceConst()->GetAccessFlags() & kAccessLockable) && lockedCopy()) return true;
    twin.Release();
    if (!MakeSystemMemorySurfaceLike(&twin.surface, &twin.texture, &twin.cube, targetSurface))
        done = false;
    else if (!CopySurfaceRectLocked(sourceSurface, &from, twin.surface, &to))
        done = false;
    else
        done = SUCCEEDED(d3d->UpdateSurface(twin.surface, &to, targetSurface, &at));
    twin.Release();
    return done;
}

} // namespace nGZGraphic4
