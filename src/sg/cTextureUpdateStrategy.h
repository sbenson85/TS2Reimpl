#pragma once
// The texture update strategies the texture manager makes (lane SJ, milestone O row M4a part 4;
// docs/subsystems/scenegraph.md): nRZSceneGraph::cTextureUpdateStrategy (vtable 0xe7d084, 8 bytes), the plain one
// procedural textures fill white, and nGZSceneGraph::cMissingTextureUpdateStrategy (vtable 0xe7c818, 0x28 bytes), the
// one a texture that could not be found gets, which draws its name (upper case) on the surface.
//
// cProceduralTexture (ours since lane TH, sg/cProceduralTexture.h) holds a strategy: its Restore asks slot 3 for a
// description, fills level 0 (each face of a cube) through slot 4 and calls slot 5 at the end.
//
// Standalone shape (docs/standalone-shape.md): our classes deriving from their interface, every slot an override. The
// texture manager makes ours (src/sg/cTextureManager.cpp), so the missing strategy's constructor 0xadd030 and its slots
// 4 and 6 are in effect through it. Slots 0, 1, 2, 3 and 5 are bodies folded with other classes (shared_vf_178b20,
// 148fb0, 4b1d00, 147110, 140fe0): reproduced, never hooked. The plain strategy's Update 0xaed030 is hooked: the
// video strategy's Update (0x405130) calls it directly. The base constructor 0xaebd30 and destructor 0xaebd40 stay the
// game's for the game's own derived strategies (terrain paint, video, text).
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

namespace nGZGraphic4 {
class cILockableSurface;
}

namespace nRZSceneGraph {

// The strategy interface (slots 3-6) as cProceduralTexture calls it.
class cITextureUpdateStrategy : public cIGZUnknown {
public:
    // 3: the description the strategy wants (width, height, levels, quality); false: the texture's own.
    virtual bool GetDescription(uint32_t *width, uint32_t *height, uint32_t *levels, float *quality) = 0;
    // 4: fills `surface` (a level of the texture; `face` the cube face, 0 for 2D).
    virtual void Update(nGZGraphic4::cILockableSurface *surface, uint32_t face, bool first) = 0;
    virtual void Finish() = 0;                  // 5: after the last level
    virtual ~cITextureUpdateStrategy() = default; // 6
};

class cTextureUpdateStrategy : public cITextureUpdateStrategy {
public:
    cTextureUpdateStrategy() = default; // 0xaebd30 (its own objects only; the game's derived classes keep the game's)
    ~cTextureUpdateStrategy() override = default; // 0xaebd40; the deleting destructor 0xaebd50 (slot 6)
    cTextureUpdateStrategy(const cTextureUpdateStrategy &) = delete;
    cTextureUpdateStrategy &operator=(const cTextureUpdateStrategy &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0: shared_vf_178b20
    uint32_t AddRef() override { return ++mRefs; }           // 1: shared_vf_148fb0
    uint32_t Release() override;                             // 2: shared_vf_4b1d00
    bool GetDescription(uint32_t *, uint32_t *, uint32_t *, float *) override { return false; } // 3: shared_vf_147110
    void Update(nGZGraphic4::cILockableSurface *surface, uint32_t face, bool first) override;    // 4: 0xaed030
    void Finish() override {}                                                                    // 5: shared_vf_140fe0

    // 0xaed030 (thiscall, ret 0xc; hooked): locks the whole surface and sets every byte of its rows to 0xff.
    void FillWhite(nGZGraphic4::cILockableSurface *surface, uint32_t face, bool first);

    uint32_t mRefs = 0; // +4
};

} // namespace nRZSceneGraph

namespace nGZSceneGraph {

class cMissingTextureUpdateStrategy final : public nRZSceneGraph::cTextureUpdateStrategy {
public:
    static constexpr uint32_t kSize = 0x28;

    explicit cMissingTextureUpdateStrategy(const char *name); // 0xadd030 (thiscall, ret 4)
    ~cMissingTextureUpdateStrategy() override;                // with the deleting destructor 0xadd620 (slot 6)

    // 4: 0xadeb90: the missing-texture drawing 0xae8310 (ours, src/sg/DrawMissingTextureName.h) of the name.
    void Update(nGZGraphic4::cILockableSurface *surface, uint32_t face, bool first) override;

    cRZString *Name() { return reinterpret_cast<cRZString *>(mName); }

    // +8: a cRZString of the name ("(empty)" when empty, else upper case), built and destroyed by hand.
    alignas(4) unsigned char mName[sizeof(cRZString)];
};

static_assert(sizeof(nRZSceneGraph::cTextureUpdateStrategy) == 8, "cTextureUpdateStrategy: 8 bytes");
static_assert(sizeof(cMissingTextureUpdateStrategy) == cMissingTextureUpdateStrategy::kSize &&
                  offsetof(cMissingTextureUpdateStrategy, mName) == 8,
              "cMissingTextureUpdateStrategy layout");

} // namespace nGZSceneGraph
