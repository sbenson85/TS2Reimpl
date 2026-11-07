#pragma once
// nGZSceneGraph::cTextureImage (lane SJ, milestone O row M4a part 4; docs/subsystems/scenegraph.md): the scene graph's
// texture of an image resource (the "_txtr" cImageData), a 2D texture (kind 4) or a cube map (kind 6).
//
// The texture keeps the image resource (counted) and the device texture the director's COM makes for it (class
// 0xbb892198 for kind 4, 0xe944343d for kind 6; counted). Restore (slot 3) creates the device texture from the image's
// size, levels and format (0xab5f50 chooses the description, 0xab57d0 creates it) and uploads the levels through
// cMemBlockAsTexture and the image manipulator (Upload, 0xab4780); then, when the image is a resource with a key, it
// lets the image go and keeps only its key (the resource service forgets it). GetImage (slot 11) loads the image back
// from the key when needed (from the key's group, then the game's 0x1c0532fa).
//
// Layout (0x28 bytes, the game's): +0 vtable, +4 mRefs, +8 mKind, +0xc mLevels (Load's level count, 0 at first),
// +0x10 mImage (counted), +0x14 mKey (the image's resource key once it is let go, else zero), +0x24 mTexture (the device
// texture's cITextureBase face, counted).
//
// Standalone shape (docs/standalone-shape.md): our class deriving from its interface, every slot an override. The two
// factories the director's COM registers (0xab4e50 kind 4, 0xab4d30 kind 6; src/rz/COMDirectorData.cpp) are hooked and
// make our object, so the constructor 0xab4400, the destructor 0xab45d0 and every slot (the game's vtable 0xe766c4 is
// referenced only by them) are in effect through the factories. Slots 2, 6 and 8 are bodies the image folds with other
// classes (shared_vf_244060, shared_vf_ab46a0, shared_vf_ab4b50): reproduced here, never hooked. 0xab57d0 is hooked
// (sg/cProceduralTexture.h's Restore calls it too); 0xab5f50 is ours (sg/TextureDescription.h).
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>

namespace nGZGraphic4 {
class cITextureBase;
}

namespace nGZSceneGraph {

// nGZSceneGraph::cITexture (RTTI base of cTextureImage and cProceduralTexture): what the texture manager and the
// materials call.
class cITexture : public cIGZUnknown {
public:
    virtual void Restore() = 0;          // 3: (re)creates the device texture when the device is ready
    virtual void ReleaseResource() = 0;  // 4: the device texture's slot 3, when the device is ready
    virtual uint32_t GetKind() = 0;      // 5: 4 a 2D texture, 6 a cube map
    virtual bool IsCreated() = 0;        // 6: the device texture's slot 6
    virtual uint32_t GetDeviceTextureCount() = 0; // 7: how many slot 8 answers (1; a render target its colour targets)
    // 8: the device texture (not counted) whatever face is asked; the manager hands it to the sampler appliers.
    virtual nGZGraphic4::cITextureBase *GetDeviceTexture(uint32_t face) = 0;
};

// nGZSceneGraph::cITextureImage (RTTI base of cTextureImage).
class cITextureImage : public cITexture {
public:
    static constexpr uint32_t kIid = 0x2a956da8;
    virtual void Load(cIGZUnknown *image, uint32_t levels) = 0; // 9: keeps the image (and levels), restores
    virtual void SetImage(cIGZUnknown *image) = 0;              // 10: keeps the image, uploads when created
    virtual cIGZUnknown *GetImage() = 0;                        // 11: the image (not counted), loaded from the key
    virtual ~cITextureImage() = default;                        // 12
};

class cTextureImage final : public cITextureImage {
public:
    static constexpr uint32_t kSize = 0x28;
    static constexpr uint32_t kKindTexture = 4, kKindCube = 6;
    static constexpr uint32_t kIidTexture = 0x4a95f211; // the cITexture id QueryInterface also answers

    // The director's factories (cdecl, no arguments; hooked).
    static cTextureImage *__cdecl NewTexture(); // 0xab4e50
    static cTextureImage *__cdecl NewCube();    // 0xab4d30

    explicit cTextureImage(uint32_t kind); // 0xab4400 (thiscall, ret 4)
    ~cTextureImage() override;             // 0xab45d0; the deleting destructor 0xab4680 (slot 12)
    cTextureImage(const cTextureImage &) = delete;
    cTextureImage &operator=(const cTextureImage &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0xab5aa0
    uint32_t AddRef() override { return ++mRefs; }           // 1: 0x148fb0 (shared)
    uint32_t Release() override;                             // 2: shared_vf_244060
    void Restore() override;                                 // 3: 0xab5610
    void ReleaseResource() override;                         // 4: 0xab5e00
    uint32_t GetKind() override { return mKind; }            // 5: 0x46390 (shared)
    bool IsCreated() override;                               // 6: shared_vf_ab46a0
    uint32_t GetDeviceTextureCount() override { return 1; }  // 7: 0x15e510 (shared)
    nGZGraphic4::cITextureBase *GetDeviceTexture(uint32_t) override { return mTexture; } // 8: shared_vf_ab4b50
    void Load(cIGZUnknown *image, uint32_t levels) override; // 9: 0xab5d20
    void SetImage(cIGZUnknown *image) override;              // 10: 0xab6430
    cIGZUnknown *GetImage() override;                        // 11: 0xab5e30

    // 0xab57d0 (cdecl, hooked): creates `texture`'s device texture of kind 4 or 6 from a description {width, height,
    // levels, format} on the current device, managed (pool 2), of the quality given. Other kinds: nothing.
    static void __cdecl CreateDeviceTexture(nGZGraphic4::cITextureBase *texture, const uint32_t *desc, float quality,
                                            uint32_t kind);

    uint32_t mRefs = 0;                              // +4
    uint32_t mKind;                                  // +8
    uint32_t mLevels = 0;                            // +0xc
    cIGZUnknown *mImage = nullptr;                   // +0x10
    uint32_t mKey[4] = {};                           // +0x14 {type, group, instance, resource}
    nGZGraphic4::cITextureBase *mTexture = nullptr;  // +0x24

private:
    void AssignImage(cIGZUnknown *image);
    void Upload(); // 0xab4780: the image's levels into the device texture
};

static_assert(sizeof(cTextureImage) == cTextureImage::kSize, "cTextureImage: 0x28 bytes");
static_assert(offsetof(cTextureImage, mRefs) == 4 && offsetof(cTextureImage, mKind) == 8 &&
                  offsetof(cTextureImage, mLevels) == 0xc && offsetof(cTextureImage, mImage) == 0x10 &&
                  offsetof(cTextureImage, mKey) == 0x14 && offsetof(cTextureImage, mTexture) == 0x24,
              "cTextureImage layout");

} // namespace nGZSceneGraph
