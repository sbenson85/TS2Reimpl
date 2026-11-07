#pragma once
// nGZGraphic4::cImageManipDX: the engine's image operations over Direct3D surfaces and textures (docs/subsystems/
// rendering.md, R4). Copies between surfaces and from memory or files through D3DX (with clipping, colour keys and the
// graphic system's palette for P8 surfaces), copies textures and cube faces level by level (locked row copies when the
// formats match, D3DX conversions otherwise), generates mip chains and normal maps.
//
// One instance, handed out by cGraphicSystemDX's slot 20 (GetImageManip). In the game it is a function-local static
// (0xfecf98, constant-initialised; getter 0x16c0c0, its atexit stub 0xd90360). Standalone shape
// (docs/standalone-shape.md): the getter hook (GetImageManip at 0x16c0c0) is the switch: it hands out our instance,
// so the game's vtable 0xd9e844 is never reached and every slot is in effect through its callers.
//
// The object has no fields (4 bytes: the vtable). The original keeps the graphic system's palette in five
// function-local statics, one per method that needs it, each filled on that method's first use; ours keeps the same
// five caches (PaletteCache), filled at the same moments, so the service calls happen when the original's do.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>

#include <cstdint>

#include "gz/cIGZUnknown.h"

#include "game/owned.h"
TS2_OWNED_STATIC(const char, d9e880)[]; // the source file of the class's reports (kSourceFileRva)

namespace nGZGraphic4 {

// nGZGraphic4::cIImageManip (15 slots). Engine objects are passed as their engine interfaces (cISurface,
// cILockableSurface, cITexture, cICubeTexture, cIPixelFormat: build/generated/ifaces), here as void *.
class cIImageManip : public cIGZUnknown {
public:
    // slot 3 (0x16b2a0): D3DXLoadSurfaceFromSurface of the clipped rectangles (true when clipping leaves nothing).
    virtual bool CopySurfaceRect(void *source, const RECT *sourceRect, void *target, const RECT *targetRect,
                                 const uint32_t *colorKey) = 0;
    virtual bool CopySurface(void *source, void *target) = 0; // slot 4 (0x16b4c0): whole surfaces, no colour key
    // slot 5 (0x16b0d0): D3DXLoadSurfaceFromMemory of a width x height image at `bits` into the clipped target rect.
    virtual bool LoadSurfaceFromMemoryRect(const void *bits, void *format, uint32_t width, uint32_t height,
                                           uint32_t pitch, void *target, const RECT *targetRect,
                                           const uint32_t *colorKey) = 0;
    virtual bool LoadSurfaceFromMemory(const void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch,
                                       void *target) = 0; // slot 6 (0x16b060): the whole target, no colour key
    // slot 7 (0x16b550): six textures into the faces of a cube texture (+X, -X, +Y, -Y, +Z, -Z in D3D order).
    virtual bool CopyCubeFaces(void *face0, void *face1, void *face2, void *face3, void *face4, void *face5,
                               void *cube) = 0;
    virtual bool CopyTexture(void *source, void *target) = 0; // slot 8 (0x16ba90)
    // slot 9 (0x16af30): D3DXLoadSurfaceFromFileInMemory into the whole surface.
    virtual bool LoadSurfaceFromFileInMemory(const void *data, uint32_t size, void *target, const uint32_t *colorKey) = 0;
    // slot 10 (0x16ac20): D3DXSaveSurfaceToFileA. format 0 BMP, 1 JPG, 2 PNG.
    virtual bool SaveSurfaceRectToFile(const char *path, void *surface, const RECT *rect, uint32_t format) = 0;
    virtual bool SaveSurfaceToFile(const char *path, void *surface, uint32_t format) = 0; // slot 11 (0x16aed0)
    virtual bool GenerateMipmaps(void *texture) = 0;                                       // slot 12 (0x16bf80)
    // slot 13 (0x16a980): D3DXComputeNormalMap from a height texture (resampled to the target's size first).
    virtual bool ComputeNormalMap(void *source, void *target, float amplitude, bool occlusion) = 0;
    virtual ~cIImageManip() = default; // slot 14: the deleting destructor (0x16a950)
};

class cImageManipDX : public cIImageManip {
public:
    static constexpr uint32_t kIid = 0x1b954bf1;          // cIImageManip
    static constexpr uint32_t kSourceFileRva = 0xd9e880;  // LogD3DError's source file for every report in this class

    bool QueryInterface(uint32_t riid, void **ppvObj) override; // slot 0 (0x16c120)
    uint32_t AddRef() override;                                 // slot 1 (shared_vf_15e510: 1)
    uint32_t Release() override;                                // slot 2 (the same body)
    bool CopySurfaceRect(void *source, const RECT *sourceRect, void *target, const RECT *targetRect,
                         const uint32_t *colorKey) override;
    bool CopySurface(void *source, void *target) override;
    bool LoadSurfaceFromMemoryRect(const void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch,
                                   void *target, const RECT *targetRect, const uint32_t *colorKey) override;
    bool LoadSurfaceFromMemory(const void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch,
                               void *target) override;
    bool CopyCubeFaces(void *face0, void *face1, void *face2, void *face3, void *face4, void *face5,
                       void *cube) override;
    bool CopyTexture(void *source, void *target) override;
    bool LoadSurfaceFromFileInMemory(const void *data, uint32_t size, void *target, const uint32_t *colorKey) override;
    bool SaveSurfaceRectToFile(const char *path, void *surface, const RECT *rect, uint32_t format) override;
    bool SaveSurfaceToFile(const char *path, void *surface, uint32_t format) override;
    bool GenerateMipmaps(void *texture) override;
    bool ComputeNormalMap(void *source, void *target, float amplitude, bool occlusion) override;
    ~cImageManipDX() override = default;

    // The instance the game uses: the getter 0x16c0c0 (hooked; cdecl, no arguments).
    static cImageManipDX *Instance();

    // The graphic system's palette (cIGraphicSystemDX slot 28), fetched through the framework's graphic service on
    // first use. Not guarded against two threads filling it at once (the original's are thread-safe statics): both
    // would fetch the same pointer. The game calls the image operations from its render thread.
    struct PaletteCache {
        bool ready;
        const PALETTEENTRY *palette;
        const PALETTEENTRY *Get();
    };
    // One per method, as the original's (guard / value RVAs for tests).
    enum PaletteUser { kCopySurfaceRect, kLoadFromMemory, kLoadFromFile, kSaveToFile, kGenerateMipmaps, kPaletteUsers };
    static PaletteCache sPalettes[kPaletteUsers];
    static constexpr uint32_t kPaletteGuardRva[kPaletteUsers] = {0x10ca278, 0x10ca280, 0x10ca288, 0x10ca290, 0x10ca298};
    static constexpr uint32_t kPaletteValueRva[kPaletteUsers] = {0x10ca274, 0x10ca27c, 0x10ca284, 0x10ca28c, 0x10ca294};
};
static_assert(sizeof(cImageManipDX) == 4, "cImageManipDX: the vtable only");

} // namespace nGZGraphic4
