#pragma once
// nGZSceneGraph::cViewerRefNodeBaseImpl (lane PY, scenegraph N3 part 2): the state of a "viewer reference" node, the node
// that makes the scene graph draw its subtree through a view: a renderable node's state (cRenderableNodeImpl, 0x70 bytes)
// plus what the viewer adds up to 0xe8. Its parts, as far as the code shows them (names are ours, tentative):
//   * the render target: either the device's own back buffer and depth surface (the colour surface is null and the
//     accessors ask the current device), or two surfaces the node holds;
//   * the texture the view renders into: a mode word (+0x7c) says where it comes from: 0 the texture manager's texture
//     of the 4-byte handle at +0x98 (0xae8260..0xae8750: not ours), n >= 1 the n counted textures at +0x84.. held
//     directly (1 with none held = the device's targets);
//   * a bounded node whose bound the viewer reports as its own (+0xac: a weak pointer), and a plane (+0xb0, four
//     floats) with flags (+0xc0, bit 2 = the plane clips) the cull walk tests it against;
//   * a few persisted words and floats (+0xc4..+0xe4).
// A plain class like its bases; `outer` is the node that holds it. Hooked at its RVAs. Part 1 (lane PY): constructor,
// destructor, accessors, the setters, Read, Write, IsKindOf, GetTypeInfo. Part 2 (lane QC): the texture getters
// (0xae1000 / 0xae10f0 / 0xae1200 / 0xae1340), the cull walk 0xae09e0 and the event handler 0xae0c20. CloneInto 0xae1cb0 is
// lane TZ's (with the viewer node, sg/cViewerRefNode.h).
// The original dereferences null in two places the tests avoid (and these bodies do the same): 0xae10f0 / 0xae1200 in
// mode 0 with an invalid handle, and 0xae1000 in mode >= 2 with no first texture.
//
// Not ours yet (OriginalFunction): the texture manager's handle operations 0xae8260 / 0xae8280 / 0xae82b0 / 0xae82e0 /
// 0xae8710 / 0xae8730 and the manager 0xae8750, shared by many classes; the device's render-target size 0x18aea0. The
// transform's plane carrier 0x19ca50 is ours (nGZCompGeom::RigidTransform3::TransformPlane, lane VS).
#include "compgeom/Frustum.h"
#include "sg/cRenderableNodeImpl.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// What 0xae1000 answers: the size of the view's render target and its scale.
struct cViewerTargetSize {
    uint32_t mWidth;
    uint32_t mHeight;
    float mScale;
};
static_assert(sizeof(cViewerTargetSize) == 12, "cViewerTargetSize size");

// The viewer a reference node draws for, as far as the cull walk reads it (names ours, tentative): the object that draws
// a node (+0: its slot 27 draws `node` with a plane mask), the node being drawn (+0xc), the view frustum (+0x24), the
// eye position (+0x104) and the frame the bounds are taken in (+0x110, a 0x34-byte transform).
struct cViewerView {
    void *mDrawer; // +0
    uint8_t mPad4[0xc - 4];
    const void *mNode; // +0xc
    uint8_t mPad10[0x24 - 0x10];
    nGZCompGeom::Frustum mFrustum; // +0x24
    uint8_t mPad84[0x104 - 0x84];
    float mEye[3];       // +0x104
    cTransform34 mFrame; // +0x110
};
static_assert(offsetof(cViewerView, mNode) == 0xc, "cViewerView layout");
static_assert(offsetof(cViewerView, mFrustum) == 0x24, "cViewerView layout");
static_assert(offsetof(cViewerView, mEye) == 0x104, "cViewerView layout");
static_assert(offsetof(cViewerView, mFrame) == 0x110, "cViewerView layout");

class cViewerRefNodeBaseImpl : public cRenderableNodeImpl {
public:
    static constexpr uint32_t kVersion = 5;      // what Write records
    static constexpr uint32_t kIid = 0xc0c0c0b;  // cIViewerRefNodeBase (tentative)
    enum : uint32_t { kPlaneActiveFlag = 4 };    // +0xc0 bit 2 (version < 5 files had bit 0)

    cViewerRefNodeBaseImpl(); // 0xae06d0's work (Construct below is it as a hookable member)
    cViewerRefNodeBaseImpl(const cViewerRefNodeBaseImpl &) = delete;
    cViewerRefNodeBaseImpl &operator=(const cViewerRefNodeBaseImpl &) = delete;
    ~cViewerRefNodeBaseImpl(); // 0xae08a0: the references released (the order in the .cpp), then the bases'

    cViewerRefNodeBaseImpl *Construct(); // 0xae06d0 (thiscall, returns this)
    void Destruct();                     // 0xae08a0, the destructor as a hookable member

    // 0xae0b90 / 0xae0bd0: the depth surface / the colour surface: the node's own, or (none held) the current device's
    // (null when there is no device or it is not ready). 0xae0c10: whether the node holds no colour surface.
    cIGZUnknown *GetDepthSurface();
    cIGZUnknown *GetColorSurface();
    bool UsesDeviceTargets();

    // 0xae0ce0 / 0xae0d00 / 0xae0d20 / 0xae0d40 (cIBoundedNode slots 70 / 73 / 74 / 75 of the held bound node; false
    // without one) and 0xae0d60 / 0xae0db0 (slots 72 / 71: the held node's box, else the bounded node's own).
    bool BoundSlot70();
    bool BoundSlot73(uint32_t a, uint32_t b);
    bool IntersectsLinear(nGZCompGeom::cILinear3 *linear);
    bool FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear);
    cBoundBox *GetBoundBox(cBoundBox *out, cICompositionTreeNode *node);
    cBoundBox *GetBoundBoxIn(cBoundBox *out, cICompositionTreeNode *node, const cTransform34 *frame);

    // 0xae1870: the held bound node replaced (listeners on the old one removed, on the new one added: its event 0xa884a15
    // and, unless already connected, the bound-changed event); the flags cleared. 0xae19b0: that, then the flags and plane.
    void SetBound(cIBoundedNode *outer, cIBoundedNode *bound);
    void SetBoundAndPlane(cIBoundedNode *outer, cIBoundedNode *bound, const float *plane, uint32_t flags);

    // 0xae0e00: with the mode 0 (a texture-manager texture) the texture's slot 7 is called and answers; otherwise the
    // mode is returned unchanged (non-zero).
    uint32_t CallTextureSlot7();
    // 0xae1320: the mode is 1 and no texture is held (the device's targets are the view's).
    bool IsDeviceViewMode();

    // 0xae1470: the colour and depth surfaces (counted, assigned when they differ).
    void SetSurfaces(cIGZUnknown *color, cIGZUnknown *depth);
    void SetRefA8(cIGZUnknown *ref); // 0xae14d0
    void SetRefA4(cIGZUnknown *ref); // 0xae1510
    // 0xae1550: mode 1, the scale, the first texture and the +0x94 reference assigned, the other three textures and
    // the manager handle released.
    void SetExplicit(cIGZUnknown *texture, cIGZUnknown *ref94, float scale);
    // 0xae1600: mode `count`, the scale, the first `count` textures assigned from the array (not clamped to 4: as
    // the original), the +0x94 reference, the rest released and the handle released.
    void SetTextures(uint32_t count, cIGZUnknown *const *textures, cIGZUnknown *ref94, float scale);
    // 0xae16e0: a texture-manager texture by id (the handle counted into +0x98); an invalid one falls back to
    // SetExplicit(0, 0, 1.0), a valid one gives mode 0 and drops the four textures.
    void SetTexture(uint32_t id);
    // 0xae17a0: a null name = SetExplicit(0, 0, 1.0); otherwise the texture of the name (through the manager's slot 20
    // when `group` is non-zero, else by name: 0xae8280) then SetTexture of its id.
    void SetTextureByName(const char *name, uint32_t group);

    // 0xae1340: the texture object the manager's handle (+0x98) names, queried for iid 0xb96cc22 and tested through its
    // slot 6 (null when the handle is invalid, the query fails or the test does). Not counted for the caller.
    cIGZUnknown *GetHandleTexture();
    // 0xae1000: {1, 1, 1.0} unless a texture answers: the held texture's slots 20 / 21 and the scale (mode 1 with a
    // held texture, or mode >= 2), else (mode 1, none held) the device's render-target size (0x18aea0), else (mode 0)
    // the handle texture's slot 17(0) slots 20 / 21 and its slot 16 (a float). Returns `out`.
    cViewerTargetSize *GetTargetSize(cViewerTargetSize *out);
    // 0xae10f0: mode 1 with no held texture: the device's depth surface when it is ready (else null); mode 0: slot 18 of
    // the texture the handle object's query answers, when that object's own slot 6 passes (0xae1340 tests the answer's);
    // otherwise +0x94.
    cIGZUnknown *GetTextureRef94();
    // 0xae1200: the same for texture `index`: the device's colour surface 0 when ready, slot 17(index) of the handle
    // texture, or the held texture `index` (not range-checked).
    cIGZUnknown *GetTextureN(int32_t index);

    // 0xae09e0 (ret 0xc): the node `outer` drawn for `view` with the plane mask `mask`: nothing for the view's own node;
    // no bound: drawn with mask 0; the plane clipping (flag bit 2) and the eye behind the bound's plane: nothing; mask 0x3f
    // (every plane known inside): drawn as it is; otherwise the held bound's box in the view's frame against the frustum
    // (Frustum::ClassifyBoxAt), drawn with the mask the test leaves unless the box is wholly outside.
    void CullAndDraw(cIBoundedNode *outer, cViewerView *view, uint32_t mask);
    // 0xae0c20 (ret 0x14): the composition node's handling first; a removal of the held bound clears +0xac; the held
    // bound as `sender` then forwards its flags 0xd (NodeBoundFlagChanged) and 0xb (SetBoundFlag) as events 0xa884a15 /
    // 0xcc6c9992 announce.
    void OnEvent(cIBoundedNode *outer, uint32_t type, uint32_t arg1, void *data, uint32_t sender);

    // 0xae0e20 (Read: the renderable part, then the texture name (version >= 2), the bound with its plane (3), the
    // words and floats (4)), 0xae1ad0 (Write), 0xae19e0 (IsKindOf), 0xae1a00 (GetTypeInfo "cViewerRefNodeBase", 5).
    void Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader);
    bool IsKindOf(uint32_t iid);
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);
    void Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer);
    // 0xae1cb0 (ret 0x14; lane TZ): the renderable part (0xae0630), then the mode, the four textures, +0x94, the texture
    // handle and the two surfaces copied into the clone's Impl (counted); then the clone told the bound with its plane
    // and flags (slot 101: with a context, the context's clone of the bound when it has one, else this one's bound), the
    // byte (98), the +0xc4 word (97) and the +0x70 reference (103).
    void CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone, cViewerRefNodeBaseImpl *cloneImpl,
                   uint32_t mode, cINodeCloneContext *context);

    cIGZUnknown *mRef70;        // +0x70
    cIGZUnknown *mEvent74;      // +0x74 (the director framework's answer to slot 3(0xbcf4e33, 1))
    uint32_t mWord78;           // +0x78 (-1)
    uint32_t mMode;             // +0x7c (1)
    float mScale;               // +0x80 (1.0)
    cIGZUnknown *mTextures[4];  // +0x84: the held textures (the first is the one a mode 1 view draws into)
    cIGZUnknown *mRef94;        // +0x94
    uint32_t mTextureHandle;    // +0x98: a texture-manager handle (-1: none)
    cIGZUnknown *mColorSurface; // +0x9c
    cIGZUnknown *mDepthSurface; // +0xa0
    cIGZUnknown *mRefA4;        // +0xa4
    cIGZUnknown *mRefA8;        // +0xa8
    cIBoundedNode *mBound;      // +0xac: the held bound node (weak)
    float mPlane[4];            // +0xb0
    uint32_t mPlaneFlags;            // +0xc0
    uint32_t mWordC4;           // +0xc4 (persisted)
    uint8_t mLayerTagMatch;     // +0xc8 (persisted; 2): how a pass matches node tags to ours (Pass::Shows)
    uint32_t mWordCC;           // +0xcc (persisted; 3)
    float mVec[4];              // +0xd0 (persisted; the constructor copies the game's global at 0x10f8d1c)
    float mScaleE0;             // +0xe0 (persisted; 1.0)
    uint32_t mWordE4;           // +0xe4 (persisted)
};
static_assert(sizeof(cViewerRefNodeBaseImpl) == 0xe8, "cViewerRefNodeBaseImpl size");
static_assert(offsetof(cViewerRefNodeBaseImpl, mBound) == 0xac, "cViewerRefNodeBaseImpl layout");
static_assert(offsetof(cViewerRefNodeBaseImpl, mVec) == 0xd0, "cViewerRefNodeBaseImpl layout");

} // namespace nGZSceneGraph
