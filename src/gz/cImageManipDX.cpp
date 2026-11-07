// nGZGraphic4::cImageManipDX (lane NG, rendering R4): src/gz/cImageManipDX.h.
#include "gz/cImageManipDX.h"
#include "gz/cSurfaceCopy.h"
#include "gz/cImageManipDXMemory.h"
#include "gz/cMemBlock.h"

#include <cstring>
#include <vector>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cICubeTexture.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGraphicSystemDX.h"
#include "ifaces/cILockableSurface.h"
#include "ifaces/cIPixelFormat.h"
#include "ifaces/cISurface.h"
#include "ifaces/cITexture.h"
#include "rz/MiscHelpersA.h"      // LogD3DError
#include "rz/RZAutoRef.h"
#include "rz/RZTextConvert.h"
#include "rz/Singleton_20ddfb.h"  // GetServiceObject
#include "rz/cRZString.h"

namespace nGZGraphic4 {

cImageManipDX::PaletteCache cImageManipDX::sPalettes[kPaletteUsers] = {};

namespace {

constexpr uint32_t kIidUnknown = 1;
constexpr uint32_t kD3DSurfaceIid = 0x4a5a7223;     // a surface's IDirect3DSurface9
constexpr uint32_t kD3DBaseTextureIid = 0x998dbd2;  // a texture's IDirect3DBaseTexture9
constexpr uint32_t kD3DTextureIid = 0x8b54da90;     // a texture's IDirect3DTexture9
constexpr uint32_t kMemoryTextureIid = 0x5b891e1e;  // nRZGraphic4::cMemBlockAsTexture's (a texture in memory)

// The framework's graphic service (cGraphicSystemDX's cIGraphicSystemDX face).
constexpr uint32_t kGraphicServiceId = 0x85522803, kGraphicServiceIid = 0xc55289ba;

constexpr DWORD kLoadFilter = 0x70004; // every load: D3DX_FILTER_TRIANGLE | D3DX_FILTER_MIRROR (U, V and W)
constexpr DWORD kMipFilter = 4;        // D3DX_FILTER_TRIANGLE, for GenerateMipmaps
constexpr DWORD kNormalMapChannel = 0x10;           // D3DX_CHANNEL_LUMINANCE
constexpr DWORD kNormalMapOcclusionFlag = 0x100000; // D3DX_NORMALMAP_COMPUTE_OCCLUSION
constexpr uint32_t kAccessLockable = 1;             // cISurface slot 19: the surface can be locked

// LogD3DError lines (the original's source lines).
constexpr uint32_t kLineCopySource = 0x6a, kLineCopyTarget = 0x6e, kLineLoadMemory = 0xa2, kLineLoadFile = 0x1b2,
                   kLineSave = 0x1f2;

// D3DX (statically linked, third-party code: called where it lies).
using LoadSurfaceFromSurfaceFn = HRESULT(__stdcall *)(IDirect3DSurface9 *, const PALETTEENTRY *, const RECT *,
                                                      IDirect3DSurface9 *, const PALETTEENTRY *, const RECT *, DWORD,
                                                      D3DCOLOR);
using LoadSurfaceFromMemoryFn = HRESULT(__stdcall *)(IDirect3DSurface9 *, const PALETTEENTRY *, const RECT *,
                                                     const void *, D3DFORMAT, UINT, const PALETTEENTRY *, const RECT *,
                                                     DWORD, D3DCOLOR);
using LoadSurfaceFromFileInMemoryFn = HRESULT(__stdcall *)(IDirect3DSurface9 *, const PALETTEENTRY *, const RECT *,
                                                           const void *, UINT, const RECT *, DWORD, D3DCOLOR, void *);
using SaveSurfaceToFileFn = HRESULT(__stdcall *)(const char *, uint32_t, IDirect3DSurface9 *, const PALETTEENTRY *,
                                                 const RECT *);
using FilterTextureFn = HRESULT(__stdcall *)(IDirect3DBaseTexture9 *, const PALETTEENTRY *, UINT, DWORD);
using ComputeNormalMapFn = HRESULT(__stdcall *)(IDirect3DTexture9 *, IDirect3DTexture9 *, const PALETTEENTRY *, DWORD,
                                                DWORD, float);

HRESULT D3DXLoadFromSurface(IDirect3DSurface9 *target, const PALETTEENTRY *targetPalette, const RECT *targetRect,
                               IDirect3DSurface9 *source, const PALETTEENTRY *sourcePalette, const RECT *sourceRect,
                               D3DCOLOR colorKey)
{
    return OriginalFunction<LoadSurfaceFromSurfaceFn, 0x1e310>()(target, targetPalette, targetRect, source,
                                                                  sourcePalette, sourceRect, kLoadFilter, colorKey);
}
HRESULT D3DXLoadFromMemory(IDirect3DSurface9 *target, const PALETTEENTRY *palette, const RECT *targetRect,
                              const void *bits, D3DFORMAT format, UINT pitch, const RECT *sourceRect, D3DCOLOR colorKey)
{
    return OriginalFunction<LoadSurfaceFromMemoryFn, 0x1d57c>()(target, palette, targetRect, bits, format, pitch,
                                                                 nullptr, sourceRect, kLoadFilter, colorKey);
}

// The memory-texture copy (0x184710) and normal map (0x183500): lane OW, src/gz/cImageManipDXMemory.h.
using memchain::ComputeMemoryNormalMap;
using memchain::CopyMemoryTextures;

// nRZGraphic4::cMemBlockAsTexture (ours, src/gz/cMemBlock.h) on the stack: a texture over caller-owned memory, one
// level per block, built and torn down through its hooked entry points (the tests stand in for them).
struct MemoryTexture {
    alignas(nRZGraphic4::cMemBlockAsTexture) unsigned char storage[sizeof(nRZGraphic4::cMemBlockAsTexture)];

    MemoryTexture() { nRZGraphic4::cMemBlockAsTexture::Construct(storage, nullptr); }
    ~MemoryTexture() { nRZGraphic4::cMemBlockAsTexture::Destruct(Get(), nullptr); }
    MemoryTexture(const MemoryTexture &) = delete;
    MemoryTexture &operator=(const MemoryTexture &) = delete;
    nRZGraphic4::cMemBlockAsTexture *Get() { return reinterpret_cast<nRZGraphic4::cMemBlockAsTexture *>(storage); }
};

// A locked surface (0x24 bytes, the engine's lock record): cILockableSurface::Lock fills it and keeps the surface
// counted in it until Unlock.
struct SurfaceLock {
    void *surface = nullptr; // +0x00 (counted)
    RECT rect{};             // +0x04
    uint8_t *bits = nullptr; // +0x14
    int32_t pitch = 0;       // +0x18
    uint32_t rowBytes = 0;   // +0x1c
    uint32_t rows = 0;       // +0x20

    SurfaceLock() = default;
    SurfaceLock(const SurfaceLock &) = delete;
    SurfaceLock &operator=(const SurfaceLock &) = delete;
    ~SurfaceLock()
    {
        Unlock();
        if (surface) static_cast<cIGZUnknown *>(surface)->Release();
    }
    void LockOn(void *lockable) { As<ifaces::cILockableSurface>(lockable)->Lock(this, 0); }
    // The surface's Unlock, then the lock lets the surface go (Unlock may already have).
    void Unlock()
    {
        if (!surface) return;
        As<ifaces::cILockableSurface>(surface)->Unlock(this);
        if (void *const held = surface) {
            surface = nullptr;
            static_cast<cIGZUnknown *>(held)->Release();
        }
        bits = nullptr;
        rowBytes = 0;
        pitch = 0;
        rows = 0;
    }
};
static_assert(sizeof(SurfaceLock) == 0x24, "lock record layout");

// Row by row, the target's row length (the formats are the same).
void CopyRows(const SurfaceLock &to, const SurfaceLock &from)
{
    for (uint32_t row = 0; row < to.rows; ++row)
        std::memcpy(to.bits + uint32_t(to.pitch) * row, from.bits + uint32_t(from.pitch) * row, to.rowBytes);
}

uint32_t SourceFile() { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts2_rva_d9e880)); }
void Report(HRESULT hr, uint32_t line) { LogD3DError(static_cast<uint32_t>(hr), SourceFile(), line); }

bool Succeeded(HRESULT hr) { return hr >= 0; }

// An engine object's Direct3D object (QueryInterface into `slot`, which keeps what it held when the answer is no).
template <class T> T *AskInto(void *object, uint32_t iid, void *&slot)
{
    static_cast<cIGZUnknown *>(object)->QueryInterface(iid, &slot);
    return static_cast<T *>(slot);
}

// The surface's description (32 bytes; reported when it fails), and the palette to pass for it: the cache's for P8.
const PALETTEENTRY *PaletteFor(IDirect3DSurface9 *surface, cImageManipDX::PaletteCache &cache, uint32_t line)
{
    D3DSURFACE_DESC desc;
    const HRESULT hr = surface->GetDesc(&desc);
    if (hr < 0) Report(hr, line);
    return desc.Format == D3DFMT_P8 ? cache.palette : nullptr;
}

bool Lockable(void *surface) { return (As<ifaces::cILockableSurface>(surface)->GetAccessFlags() & kAccessLockable) != 0; }

RECT Bounds(void *surface)
{
    const uint32_t height = As<ifaces::cISurface>(surface)->GetHeight();
    const uint32_t width = As<ifaces::cISurface>(surface)->GetWidth();
    return RECT{0, 0, LONG(width), LONG(height)};
}

// The first level of a texture that fits in width x height (both no larger), searched up to the last but one level
// (the last is taken whatever its size). Sizes compare signed.
template <class Texture> uint32_t FirstFittingLevel(Texture *texture, uint32_t levels, uint32_t width, uint32_t height)
{
    uint32_t level = 0;
    while (int32_t(level) < int32_t(levels - 1)) {
        if (int32_t(texture->GetWidth(level)) <= int32_t(width) && int32_t(texture->GetHeight(level)) <= int32_t(height))
            break;
        ++level;
    }
    return level;
}

cImageManipDX gInstance;

} // namespace

// ---- the palette caches ----

const PALETTEENTRY *cImageManipDX::PaletteCache::Get()
{
    if (!ready) {
        RZAutoRef graphics{nullptr};
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kGraphicServiceId, kGraphicServiceIid, &graphics.mObject);
        palette = static_cast<const PALETTEENTRY *>(As<ifaces::cIGraphicSystemDX>(graphics.mObject)->GetPalette());
        graphics.ReleaseObject();
        ready = true;
    }
    return palette;
}

cImageManipDX *cImageManipDX::Instance() { return &gInstance; }

// ---- slots ----

bool cImageManipDX::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (!ppvObj) return false;
    if (riid != kIidUnknown && riid != kIid) return false;
    *ppvObj = this;
    AddRef();
    return true;
}

uint32_t cImageManipDX::AddRef() { return 1; }
uint32_t cImageManipDX::Release() { return 1; }

bool cImageManipDX::CopySurfaceRect(void *source, const RECT *sourceRect, void *target, const RECT *targetRect,
                                    const uint32_t *colorKey)
{
    PaletteCache &cache = sPalettes[kCopySurfaceRect];
    cache.Get();
    const RECT sourceBounds = Bounds(source);
    const RECT targetBounds = Bounds(target);
    RECT from = *sourceRect, to = *targetRect;
    if (!ClipCopyRects(&from, &sourceBounds, &to, &targetBounds)) return true; // nothing to copy
    // The original asks into its argument slots (source's and sourceRect's): a "no" leaves the argument there.
    void *sourceSlot = source, *targetSlot = const_cast<RECT *>(sourceRect);
    IDirect3DSurface9 *const d3dSource = AskInto<IDirect3DSurface9>(source, kD3DSurfaceIid, sourceSlot);
    IDirect3DSurface9 *const d3dTarget = AskInto<IDirect3DSurface9>(target, kD3DSurfaceIid, targetSlot);
    const PALETTEENTRY *const sourcePalette = PaletteFor(d3dSource, cache, kLineCopySource);
    const PALETTEENTRY *const targetPalette = PaletteFor(d3dTarget, cache, kLineCopyTarget);
    return Succeeded(D3DXLoadFromSurface(d3dTarget, targetPalette, &to, d3dSource, sourcePalette, &from, *colorKey));
}

bool cImageManipDX::CopySurface(void *source, void *target)
{
    const RECT targetRect = Bounds(target);
    const RECT sourceRect = Bounds(source);
    const uint32_t noKey = 0;
    return CopySurfaceRect(source, &sourceRect, target, &targetRect, &noKey);
}

bool cImageManipDX::LoadSurfaceFromMemoryRect(const void *bits, void *format, uint32_t width, uint32_t height,
                                              uint32_t pitch, void *target, const RECT *targetRect,
                                              const uint32_t *colorKey)
{
    PaletteCache &cache = sPalettes[kLoadFromMemory];
    cache.Get();
    const RECT sourceBounds{0, 0, LONG(width), LONG(height)};
    RECT from = sourceBounds;
    const RECT targetBounds = Bounds(target);
    RECT to = *targetRect;
    if (!ClipCopyRects(&from, &sourceBounds, &to, &targetBounds)) return true;
    void *targetSlot = reinterpret_cast<void *>(uintptr_t(width)); // asked into `width`'s slot
    IDirect3DSurface9 *const d3dTarget = AskInto<IDirect3DSurface9>(target, kD3DSurfaceIid, targetSlot);
    const PALETTEENTRY *const palette = PaletteFor(d3dTarget, cache, kLineLoadMemory);
    const D3DFORMAT d3dFormat = D3DFORMAT(As<ifaces::cIPixelFormat>(format)->GetFormatId());
    return Succeeded(D3DXLoadFromMemory(d3dTarget, palette, &to, bits, d3dFormat, pitch, &from, *colorKey));
}

bool cImageManipDX::LoadSurfaceFromMemory(const void *bits, void *format, uint32_t width, uint32_t height,
                                          uint32_t pitch, void *target)
{
    const RECT whole = Bounds(target);
    const uint32_t noKey = 0;
    return LoadSurfaceFromMemoryRect(bits, format, width, height, pitch, target, &whole, &noKey);
}

bool cImageManipDX::CopyCubeFaces(void *face0, void *face1, void *face2, void *face3, void *face4, void *face5,
                                  void *cube)
{
    auto *const target = As<ifaces::cICubeTexture>(cube);
    if (!Lockable(target->GetTargetSurface(0, 0))) return false;
    void *const faces[6] = {face0, face1, face2, face3, face4, face5};
    bool needMips = false; // a face had fewer levels than the cube, or was resampled: the cube's mips are made after
    for (uint32_t face = 0; face < 6; ++face) {
        auto *const source = As<ifaces::cITexture>(faces[face]);
        if (!Lockable(source->GetSurface(0))) return false;
        void *const cubeFormat = target->GetPixelFormat();
        void *const faceFormat = source->GetPixelFormat();
        const uint32_t width = target->GetWidth(0);
        const uint32_t height = target->GetHeight(0);
        const uint32_t cubeLevels = target->GetLevelCount();
        const uint32_t faceLevels = source->GetLevelCount();
        const uint32_t first = FirstFittingLevel(source, faceLevels, width, height);
        const int32_t remaining = int32_t(faceLevels - first);
        const bool sameSize = source->GetWidth(first) == width && source->GetHeight(first) == height;
        if (remaining < int32_t(cubeLevels)) needMips = true;
        if (!sameSize) {
            void *const to = target->GetTargetSurface(face, 0);
            if (!CopySurface(source->GetSurface(0), to)) return false;
            needMips = true;
            continue;
        }
        if (faceFormat == cubeFormat) {
            for (uint32_t level = 0; level < cubeLevels; ++level) {
                SurfaceLock from, to;
                from.LockOn(source->GetLockableSurface(level + first));
                to.LockOn(target->GetTargetLockableSurface(face, level));
                CopyRows(to, from);
                to.Unlock();
                from.Unlock();
                if (needMips) break;
            }
        } else {
            for (uint32_t level = 0; level < cubeLevels; ++level) {
                void *const to = target->GetTargetSurface(face, level);
                if (!CopySurface(source->GetSurface(first + level), to)) return false;
                if (needMips) break;
            }
        }
    }
    return needMips ? GenerateMipmaps(cube) : true;
}

bool cImageManipDX::CopyTexture(void *source, void *target)
{
    auto *const from = As<ifaces::cITexture>(source);
    auto *const to = As<ifaces::cITexture>(target);
    if (!Lockable(from->GetSurface(0))) return false;
    if (!Lockable(to->GetTargetSurface(0))) return false;
    if (CopyMemoryTextures(source, target, true)) return true;
    void *const targetFormat = to->GetPixelFormat();
    void *const sourceFormat = from->GetPixelFormat();
    const uint32_t width = to->GetWidth(0);
    const uint32_t height = to->GetHeight(0);
    const uint32_t targetLevels = to->GetLevelCount();
    const uint32_t sourceLevels = from->GetLevelCount();
    const uint32_t first = FirstFittingLevel(from, sourceLevels, width, height);
    const int32_t remaining = int32_t(sourceLevels - first);
    if (from->GetWidth(first) != width || from->GetHeight(first) != height) {
        // No level of the right size: the top level resampled, then the target's mips made from it.
        void *const top = to->GetTargetSurface(0);
        return CopySurface(from->GetSurface(0), top) ? GenerateMipmaps(target) : false;
    }
    // The source runs out of levels before the target: only the top level is copied, the rest made from it.
    const bool needMips = remaining < int32_t(targetLevels);
    if (sourceFormat == targetFormat) {
        for (uint32_t level = 0; level < targetLevels; ++level) {
            SurfaceLock read, write;
            read.LockOn(from->GetLockableSurface(first + level));
            write.LockOn(to->GetTargetLockableSurface(level));
            CopyRows(write, read);
            write.Unlock();
            read.Unlock();
            if (needMips) break;
        }
    } else {
        for (uint32_t level = 0; level < targetLevels; ++level) {
            void *const surface = to->GetTargetSurface(level);
            if (!CopySurface(from->GetSurface(first + level), surface)) return false;
            if (needMips) break;
        }
    }
    return needMips ? GenerateMipmaps(target) : true;
}

bool cImageManipDX::LoadSurfaceFromFileInMemory(const void *data, uint32_t size, void *target, const uint32_t *colorKey)
{
    PaletteCache &cache = sPalettes[kLoadFromFile];
    cache.Get();
    void *targetSlot = nullptr; // the original's is an uninitialised local (a "no" would leave garbage)
    IDirect3DSurface9 *const d3dTarget = AskInto<IDirect3DSurface9>(target, kD3DSurfaceIid, targetSlot);
    const PALETTEENTRY *const palette = PaletteFor(d3dTarget, cache, kLineLoadFile);
    return Succeeded(OriginalFunction<LoadSurfaceFromFileInMemoryFn, 0x1e256>()(d3dTarget, palette, nullptr, data, size,
                                                                                 nullptr, kLoadFilter, *colorKey, nullptr));
}

bool cImageManipDX::SaveSurfaceRectToFile(const char *path, void *surface, const RECT *rect, uint32_t format)
{
    cRZString text;
    text.Construct(path);
    RZText::ConvertStringToString(*text.AsInterface(), *text.AsInterface(), RZText::kSystemCodePage);
    const char *const file = text.ToChar();
    // ORIGINAL-BUG: an unknown format passes the word in the original's `path` argument slot as the D3DX file format:
    // the path pointer, or, when this call filled the palette cache (which holds the graphic service there), the
    // service's pointer. Either way D3DX refuses it.
    uint32_t pathSlot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path));
    PaletteCache &cache = sPalettes[kSaveToFile];
    if (!cache.ready) {
        void *service = nullptr;
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kGraphicServiceId, kGraphicServiceIid, &service);
        cache.palette = static_cast<const PALETTEENTRY *>(As<ifaces::cIGraphicSystemDX>(service)->GetPalette());
        RZAutoRef{service}.ReleaseObject();
        cache.ready = true;
        pathSlot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(service));
    }
    uint32_t fileFormat;
    switch (format) {
    case 0: fileFormat = 0; break; // D3DXIFF_BMP
    case 1: fileFormat = 1; break; // D3DXIFF_JPG
    case 2: fileFormat = 3; break; // D3DXIFF_PNG
    default: fileFormat = pathSlot; break;
    }
    void *surfaceSlot = nullptr; // an uninitialised local in the original
    IDirect3DSurface9 *const d3dSurface = AskInto<IDirect3DSurface9>(surface, kD3DSurfaceIid, surfaceSlot);
    const PALETTEENTRY *const palette = PaletteFor(d3dSurface, cache, kLineSave);
    return Succeeded(OriginalFunction<SaveSurfaceToFileFn, 0x1d73e>()(file, fileFormat, d3dSurface, palette, rect));
}

bool cImageManipDX::SaveSurfaceToFile(const char *path, void *surface, uint32_t format)
{
    const RECT whole = Bounds(surface);
    return SaveSurfaceRectToFile(path, surface, &whole, format);
}

bool cImageManipDX::GenerateMipmaps(void *texture)
{
    void *memoryTexture = nullptr;
    bool result;
    if (static_cast<cIGZUnknown *>(texture)->QueryInterface(kMemoryTextureIid, &memoryTexture) &&
        CopyMemoryTextures(memoryTexture, memoryTexture, true)) {
        result = true;
    } else {
        const PALETTEENTRY *const palette = sPalettes[kGenerateMipmaps].Get();
        void *baseSlot = nullptr; // an uninitialised local in the original
        IDirect3DBaseTexture9 *const d3dTexture = AskInto<IDirect3DBaseTexture9>(texture, kD3DBaseTextureIid, baseSlot);
        result = Succeeded(OriginalFunction<FilterTextureFn, 0x1e5ac>()(d3dTexture, palette, 0, kMipFilter));
    }
    if (memoryTexture) static_cast<cIGZUnknown *>(memoryTexture)->Release();
    return result;
}

bool cImageManipDX::ComputeNormalMap(void *source, void *target, float amplitude, bool occlusion)
{
    auto *const from = As<ifaces::cITexture>(source);
    auto *const to = As<ifaces::cITexture>(target);
    const uint32_t sourceWidth = from->GetWidth(0);
    const uint32_t targetWidth = to->GetWidth(0);
    const uint32_t sourceHeight = from->GetHeight(0);
    const uint32_t targetHeight = to->GetHeight(0);
    const uint32_t sourceLevels = from->GetLevelCount();
    const uint32_t targetLevels = to->GetLevelCount();
    std::vector<uint8_t> pixels;
    MemoryTexture holder;
    nRZGraphic4::cMemBlockAsTexture *const resampled = holder.Get();
    ++resampled->mRefs; // the original's direct AddRef (folded 0x148fb0)
    void *heights = source;
    if (sourceWidth != targetWidth || sourceHeight != targetHeight || sourceLevels != targetLevels) {
        // The source resampled to the target's size and levels first, into one block of memory.
        uint32_t texels = 0;
        for (uint32_t level = 0; level < targetLevels; ++level) {
            const uint32_t height = to->GetHeight(level);
            texels += height * to->GetWidth(level);
        }
        const uint32_t bitsPerPixel = As<ifaces::cIPixelFormat>(from->GetPixelFormat())->GetBitsPerPixel();
        const uint32_t bytes = bitsPerPixel * texels >> 3;
        uint8_t *data = nullptr;
        if (bytes) {
            pixels.resize(bytes);
            data = pixels.data();
        }
        resampled->SetTopLevel(data, from->GetPixelFormat(), targetWidth, targetHeight, bitsPerPixel * targetWidth >> 3);
        uint32_t offset = bitsPerPixel * targetHeight * targetWidth >> 3;
        for (uint32_t level = 1; level < targetLevels; ++level) {
            const uint32_t width = to->GetWidth(level);
            const uint32_t height = to->GetHeight(level);
            resampled->AddLevel(data + offset, width, height, width * bitsPerPixel >> 3);
            offset += height * width * bitsPerPixel >> 3;
        }
        if (!CopyTexture(source, resampled)) return false;
        heights = resampled;
    }
    if (!occlusion && ComputeMemoryNormalMap(heights, target, amplitude)) return true;
    void *heightSlot = nullptr, *targetSlot = nullptr; // the second is an uninitialised local in the original
    IDirect3DTexture9 *const d3dHeights = AskInto<IDirect3DTexture9>(heights, kD3DTextureIid, heightSlot);
    IDirect3DTexture9 *const d3dTarget = AskInto<IDirect3DTexture9>(target, kD3DTextureIid, targetSlot);
    const DWORD flags = occlusion ? kNormalMapOcclusionFlag : 0;
    return Succeeded(OriginalFunction<ComputeNormalMapFn, 0x1d884>()(d3dTarget, d3dHeights, nullptr, flags,
                                                                      kNormalMapChannel, amplitude));
}

} // namespace nGZGraphic4
