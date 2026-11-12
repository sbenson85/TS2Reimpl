#pragma once
// nTSSG::cDisplayListCensorRenderStrategy (lane 261108c, scenegraph TS1): the censor boxes ("mosaic" blur) the dynamic
// render strategy draws over each pass. A cRZUnknown (vtable 0xdbbcc4, count +4) with the display-list strategy
// interface cITSDisplayListRenderStrategy (0x2ca4c07d, deriving from nGZSceneGraph::cIDisplayListRenderStrategy
// 0xbf077fd) at +8 (vtable 0xdbbce0, 11 slots). 0x38 bytes. The game builds it through the COM director's factory
// 0x377ee0 (class 0x0ca4c077) and inline in the dynamic strategy's constructor 0x36e400 (new 0x38, then 0x3775c0); the
// dynamic strategy keeps it at its +0xc and forwards its own interface slots 3, 6 and 7 to it.
//
// Each frame the game adds the world-space boxes to censor (slot 7, AddBox); each pass (slot 3, BeginPass) projects
// every box to the pass's pixels, clips it to the viewport and, where something is left, hands the display list one
// cDynamicGeometryRect per box (a grid of 8 x 8 cells over the clipped rectangle, drawn with the "CensorMaterial"
// material); slot 5 (Clear) forgets the boxes.
//
// Standalone shape (docs/standalone-shape.md): the constructor 0x3775c0 is hooked (the inline construction in 0x36e400
// then builds ours too) and so is the factory 0x377ee0; the slots are reached only through our vtables. The dirty-rect
// members the dynamic strategy calls directly (row TS1h) are hooked where the original drawing paths call them
// (CollectDirtyRects, ListBoxRects, RenderTexture), the rest in effect through those. The
// game's std::vector<CensorBox> instantiations (0x377190 growth, 0x377520 copy, 0x3776c0 destruction, 0x378560 clear)
// are our toolchain's vector of the same element.
#include "compgeom/CompGeom.h"
#include "rz/RZUnknownBase.h"
#include "sg/cIDisplayList.h"
#include "sg/cShape.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

namespace nGZSceneGraph {
class cDynamicGeometryRect;
class cIViewerRefNodeBase;
}

namespace nTSSG {

class cITSRenderSurface;
struct DynamicRectRecord;
struct DynamicStrategyState;
struct DynamicWords16;

// 0x378200 (__cdecl): the viewer's frame state (its +0xa8 reference, slot 116; when it has none, a new one from the COM
// director, class 0x6d548978, handed to the viewer by slot 99), counted once more into *out. The dynamic strategy's
// Render (0x374930) does the same inline.
void GetViewerFrameState(nGZSceneGraph::cIViewerRefNodeBase *viewer, DynamicStrategyState **out);

// The TS strategy interface (0x2ca4c07d): cIDisplayListRenderStrategy's BeginPass / Render, then six slots of its own.
// The names of 6, 8, 9 and 10 are ours: the censor strategy does nothing in them; the dynamic strategy's (src/sg/
// cDisplayListDynamicRenderStrategy.h) say what they are for.
class cITSDisplayListRenderStrategy : public nGZSceneGraph::cIDisplayListRenderStrategy {
public:
    static constexpr uint32_t kIid = 0x2ca4c07d;

    virtual void Clear() = 0;                                    // 5: the boxes forgotten
    virtual void Invalidate() = 0;                               // 6
    virtual void AddBox(const nGZCompGeom::BoundBox *box) = 0;   // 7: a world-space box to censor this frame
    virtual void Slot8() = 0;                                    // 8
    virtual void Register(void *item) = 0;                       // 9
    virtual void Unregister(void *item) = 0;                     // 10
};

// One box (0x30 bytes): the world box, its last pass's clipped pixel rectangle (min x, min y, max x, max y; zeros when
// nothing of it was left), its geometry (counted) and whether it was drawn. The game leaves the rectangle and the flag
// unset when a box is added (BeginPass writes both before anything reads them); ours zeroes them.
struct CensorBox {
    nGZCompGeom::BoundBox mBox;                     // +0
    int32_t mRect[4];                               // +0x18
    nGZSceneGraph::cDynamicGeometryRect *mGeometry; // +0x28 counted
    uint8_t mVisible;                               // +0x2c

    explicit CensorBox(const nGZCompGeom::BoundBox &box) : mBox(box), mRect{}, mGeometry(nullptr), mVisible(0) {}
    // 0x377520's element copy (and the in-place copies of push_back): every field, the geometry counted again.
    CensorBox(const CensorBox &other);
    CensorBox &operator=(const CensorBox &) = delete;
    // The geometry released.
    ~CensorBox();
};
static_assert(sizeof(CensorBox) == 0x30, "the game's censor box");
static_assert(offsetof(CensorBox, mRect) == 0x18);
static_assert(offsetof(CensorBox, mGeometry) == 0x28);
static_assert(offsetof(CensorBox, mVisible) == 0x2c);

class cDisplayListCensorRenderStrategy final : public rz::cRZUnknown, public cITSDisplayListRenderStrategy {
public:
    static constexpr uint32_t kSize = 0x38;
    static constexpr uint32_t kClassId = 0x0ca4c077; // the COM director's class (factory 0x377ee0)

    // 0x3775c0: no boxes, no material (-1); the slot count 0 (the slot start is left as it was).
    cDisplayListCensorRenderStrategy();
    // 0x377790 (deleting 0x377860, slot 3): the boxes cleared (Clear), the display-list slots freed when there are
    // any, then the members.
    ~cDisplayListCensorRenderStrategy() override;

    // 0x373cd0 (folded; also the dynamic strategy's): null `out` answers false; the two strategy interfaces answer the
    // +8 face, counted; anything else is cRZUnknown's answer.
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override { return rz::cRZUnknown::AddRef(); }
    uint32_t Release() override { return rz::cRZUnknown::Release(); }

    // 0x377920 (+8 slot 3): see the .cpp.
    void BeginPass(nGZSceneGraph::Pass *pass) override;
    void Render(nGZSceneGraph::Pass *, nGZSceneGraph::DrawItem *, uint32_t) override {} // 4 (folded 0x377e0)
    // 0x377ec0 (+8 slot 5, the thunk 0x3782b0): both lists emptied (0x378560 each), the boxes first.
    void Clear() override;
    void Invalidate() override {}                                   // 6 (folded 0x140fe0, ret 0)
    // 0x377880 (+8 slot 7): the box appended (0x377190 when the list grows).
    void AddBox(const nGZCompGeom::BoundBox *box) override;
    void Slot8() override {}                                        // 8 (folded 0x140fe0, ret 0)
    void Register(void *) override {}                               // 9 (folded 0x300a0)
    void Unregister(void *) override {}                             // 10 (folded 0x300a0)

    // The dynamic strategy's dirty-rect part (row TS1h; not virtual). See the .cpp.
    // 0x377f50 (ret 8): the drawn boxes' rectangles, then the frame state's last censored ones, appended.
    int32_t CollectDirtyRects(nGZSceneGraph::Pass *pass, std::vector<DynamicWords16> *out);
    // 0x378140 (ret 4): `out` made the drawn boxes' rectangles.
    int32_t CopyBoxRects(std::vector<DynamicWords16> *out);
    // 0x378020 (ret 4): `out` made the drawn boxes' rectangles (a list).
    int32_t ListBoxRects(std::list<DynamicWords16> *out);
    // 0x378370 (ret 0xc): with a texture, the censored items drawn into it, and the rectangles kept in the frame state.
    void RenderTexture(nGZSceneGraph::Pass *pass, const std::vector<nGZSceneGraph::DrawItem *> *items,
                       const std::vector<DynamicRectRecord *> *records);
    // 0x378400 (ret 4): the texture made the target, cleared.
    void SetUpTexture(nGZSceneGraph::Pass *pass);
    // 0x3782c0 (ret 0xc): the items whose records are drawn and marked (kind 4), drawn.
    void DrawItems(nGZSceneGraph::Pass *pass, const std::vector<nGZSceneGraph::DrawItem *> *items,
                   const std::vector<DynamicRectRecord *> *records);
    // 0x378350 (ret 8): one item drawn by its material variant, counted in the pass.
    void DrawOne(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *item);

    cDisplayListCensorRenderStrategy(const cDisplayListCensorRenderStrategy &) = delete;
    cDisplayListCensorRenderStrategy &operator=(const cDisplayListCensorRenderStrategy &) = delete;

    std::vector<CensorBox> mBoxes;                  // +0xc this frame's boxes
    std::vector<CensorBox> mVisible;                // +0x18 the last pass's drawn boxes (copies)
    nGZSceneGraph::ShapeMaterialHandle mMaterial;   // +0x24 "CensorMaterial", found by the first pass with boxes
    cITSRenderSurface *mTexture;                    // +0x28 the dynamic strategy's "CensorTexture" (not counted)
    nGZSceneGraph::ShapeMaterialHandle mUnusedHandle; // +0x2c -1, never set
    int32_t mSlotStart;                             // +0x30 the display-list slots (not set by the constructor)
    uint32_t mSlotCount;                            // +0x34
};
static_assert(sizeof(cDisplayListCensorRenderStrategy) == cDisplayListCensorRenderStrategy::kSize);
static_assert(offsetof(cDisplayListCensorRenderStrategy, mBoxes) == 0xc);
static_assert(offsetof(cDisplayListCensorRenderStrategy, mVisible) == 0x18);
static_assert(offsetof(cDisplayListCensorRenderStrategy, mMaterial) == 0x24);
static_assert(offsetof(cDisplayListCensorRenderStrategy, mTexture) == 0x28);
static_assert(offsetof(cDisplayListCensorRenderStrategy, mUnusedHandle) == 0x2c);
static_assert(offsetof(cDisplayListCensorRenderStrategy, mSlotStart) == 0x30);
static_assert(offsetof(cDisplayListCensorRenderStrategy, mSlotCount) == 0x34);

// The hooks' entry points (src/hook/tables/cDisplayListCensorRenderStrategy.cpp): original calling convention, our
// bodies.
struct cDisplayListCensorRenderStrategyAdapters {
    using Self = cDisplayListCensorRenderStrategy;
    // 0x3775c0 (__thiscall on the 0x38-byte block): our object built there.
    static Self *__fastcall Construct(Self *memory, void *);
    // 0x377ee0 (__cdecl): a new object, as its +8 interface (null when the allocation fails).
    static cITSDisplayListRenderStrategy *__cdecl Create();
    // 0x3776c0 (__thiscall on a vector): the game's vector destructor, reached only from the unwind funclets of the
    // constructor 0x3775c0 (0xd31e7e, 0xd31e89).
    static void __fastcall DestroyBoxes(std::vector<CensorBox> *boxes, void *);
};

} // namespace nTSSG
