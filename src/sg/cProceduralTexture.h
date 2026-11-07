#pragma once
// nGZSceneGraph::cProceduralTexture (lane TH, milestone O row M4b; docs/subsystems/scenegraph.md): the scene graph's
// texture whose levels an update strategy draws (the texture manager's default white texture and its missing-texture
// stand-ins, the terrain paint, video and text textures), a 2D texture (kind 4) or a cube map (kind 6).
//
// The texture keeps its strategy (nRZSceneGraph::cITextureUpdateStrategy, counted), the description the strategy wants
// and the device texture the director's COM makes for it (class 0xbb892198 for kind 4, 0xe944343d for kind 6, counted).
// Restore (slot 3) asks the strategy for the description, creates the device texture from it (ChooseTextureDescription
// 0xab5f50, CreateDeviceTexture 0xab57d0), has the strategy fill level 0 (each face of a cube), generates the other
// levels with the image manipulator, and tells the strategy it is done.
//
// Layout (0x28 bytes, the game's): +0 vtable, +4 mRefs, +8 mStrategy (counted), +0xc mKind (0xe until set up),
// +0x10 mWidth, +0x14 mHeight, +0x18 mLevels, +0x1c mQuality (1.0 at first), +0x20 mFormat (a cIPixelFormat, not
// counted), +0x24 mTexture (the device texture's cITextureBase face, counted).
//
// Standalone shape (docs/standalone-shape.md): our class deriving from its interfaces, every slot an override. The
// factory the director's COM registers (0xab4d90, class 0x6b81dfe9; src/rz/COMDirectorData.cpp) is hooked and makes our
// object, so the constructor 0xab4230, the destructor 0xab44f0 and every slot (the game's vtable 0xe76750 is referenced
// only by them) are in effect through the factory. Slots 1, 2, 5, 6, 7, 8 and 13 are bodies the image folds with other
// classes (0x148fb0, shared_vf_151920, 0x145770, shared_vf_ab46a0, 0x15e510, shared_vf_ab4b50, 0x46390): reproduced
// here, never hooked.
#include "sg/cTextureImage.h" // cITexture

#include <cstddef>
#include <cstdint>

namespace nGZGraphic4 {
class cIPixelFormat;
}
namespace nRZSceneGraph {
class cITextureUpdateStrategy;
}

namespace nGZSceneGraph {

// nGZSceneGraph::cIProceduralTexture (RTTI base of cProceduralTexture): how the texture is set up.
class cIProceduralTexture : public cITexture {
public:
    static constexpr uint32_t kIid = 0x4b81dfe2;
    // 9: a 2D texture of the size and format given, drawn by `strategy` (counted); a new device texture.
    virtual void Create(nRZSceneGraph::cITextureUpdateStrategy *strategy, uint32_t width, uint32_t height,
                        uint32_t levels, nGZGraphic4::cIPixelFormat *format, float quality) = 0;
    // 10: slot 9 with the closest ARGB format of at least `minBits` and `minAlphaBits` (FindClosestColorFormat).
    virtual void CreateClosest(nRZSceneGraph::cITextureUpdateStrategy *strategy, uint32_t width, uint32_t height,
                               uint32_t levels, int32_t minBits, int32_t minAlphaBits, float quality) = 0;
    // 11: a cube map of `edge`, the format chosen as slot 10 does; a new device texture.
    virtual void CreateCube(nRZSceneGraph::cITextureUpdateStrategy *strategy, uint32_t edge, uint32_t levels,
                            int32_t minBits, int32_t minAlphaBits, float quality) = 0;
    virtual void Redraw() = 0; // 12: the strategy draws level 0 again (each face), when the device texture exists
    virtual nRZSceneGraph::cITextureUpdateStrategy *GetStrategy() = 0; // 13 (not counted)
    virtual ~cIProceduralTexture() = default;                          // 14
};

class cProceduralTexture final : public cIProceduralTexture {
public:
    static constexpr uint32_t kSize = 0x28;
    static constexpr uint32_t kKindTexture = 4, kKindCube = 6, kKindUnset = 0xe;
    static constexpr uint32_t kIidTexture = 0x4a95f211; // the cITexture id QueryInterface also answers

    static cProceduralTexture *__cdecl New(); // 0xab4d90: the director's factory (cdecl, no arguments; hooked)

    cProceduralTexture() = default;  // 0xab4230
    ~cProceduralTexture() override;  // 0xab44f0; the deleting destructor 0xab4640 (slot 14)
    cProceduralTexture(const cProceduralTexture &) = delete;
    cProceduralTexture &operator=(const cProceduralTexture &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0xab5a40
    uint32_t AddRef() override { return ++mRefs; }           // 1: 0x148fb0 (shared)
    uint32_t Release() override;                             // 2: shared_vf_151920
    void Restore() override;                                 // 3: 0xab5090
    void ReleaseResource() override;                         // 4: 0xab5dc0
    uint32_t GetKind() override { return mKind; }            // 5: 0x145770 (shared)
    bool IsCreated() override;                               // 6: shared_vf_ab46a0
    uint32_t GetDeviceTextureCount() override { return 1; }  // 7: 0x15e510 (shared)
    nGZGraphic4::cITextureBase *GetDeviceTexture(uint32_t) override { return mTexture; } // 8: shared_vf_ab4b50
    void Create(nRZSceneGraph::cITextureUpdateStrategy *strategy, uint32_t width, uint32_t height, uint32_t levels,
                nGZGraphic4::cIPixelFormat *format, float quality) override; // 9: 0xab4f50
    void CreateClosest(nRZSceneGraph::cITextureUpdateStrategy *strategy, uint32_t width, uint32_t height,
                       uint32_t levels, int32_t minBits, int32_t minAlphaBits, float quality) override; // 10: 0xab4ef0
    void CreateCube(nRZSceneGraph::cITextureUpdateStrategy *strategy, uint32_t edge, uint32_t levels, int32_t minBits,
                    int32_t minAlphaBits, float quality) override; // 11: 0xab4fe0
    void Redraw() override;                                       // 12: 0xab6320
    nRZSceneGraph::cITextureUpdateStrategy *GetStrategy() override { return mStrategy; } // 13: 0x46390 (shared)

    uint32_t mRefs = 0;                                      // +4
    nRZSceneGraph::cITextureUpdateStrategy *mStrategy = nullptr; // +8
    uint32_t mKind = kKindUnset;                             // +0xc
    uint32_t mWidth = 0;                                     // +0x10
    uint32_t mHeight = 0;                                    // +0x14
    uint32_t mLevels = 0;                                    // +0x18
    float mQuality = 1.0f;                                   // +0x1c
    nGZGraphic4::cIPixelFormat *mFormat = nullptr;           // +0x20
    nGZGraphic4::cITextureBase *mTexture = nullptr;          // +0x24

private:
    void SetStrategy(nRZSceneGraph::cITextureUpdateStrategy *strategy);
    void NewDeviceTexture(uint32_t classId);
    void DrawLevels(bool first); // level 0 (each face) through the strategy, then the mipmaps
};

static_assert(sizeof(cProceduralTexture) == cProceduralTexture::kSize, "cProceduralTexture: 0x28 bytes");
static_assert(offsetof(cProceduralTexture, mRefs) == 4 && offsetof(cProceduralTexture, mStrategy) == 8 &&
                  offsetof(cProceduralTexture, mKind) == 0xc && offsetof(cProceduralTexture, mWidth) == 0x10 &&
                  offsetof(cProceduralTexture, mQuality) == 0x1c && offsetof(cProceduralTexture, mFormat) == 0x20 &&
                  offsetof(cProceduralTexture, mTexture) == 0x24,
              "cProceduralTexture layout");

} // namespace nGZSceneGraph
