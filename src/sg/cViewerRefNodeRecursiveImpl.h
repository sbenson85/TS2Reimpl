#pragma once
// nGZSceneGraph::cViewerRefNodeRecursiveImpl (lane VF, scenegraph N3r): the state of cViewerRefNodeRecursive
// (sg/cViewerRefNodeRecursive.h), 0x100 bytes at node+0xc. Unlike the other node Impls it has a vtable (0xe71de4, one
// slot: FillViewport) and RTTI, so its viewer base (cViewerRefNodeBaseImpl) sits at Impl+4, node+0x10. Its own part: the
// materials the node's view is drawn with and their display-list slots.
//
// The recursive viewer draws the scene again from a mirrored eye (Cull, slot 83 of the node: the reflection about the
// plane its viewer base holds at +0xb0, carried by the base's bound node, with an oblique near plane), into the target
// its viewer base names.
#include "sg/cViewerRefNodeImpl.h" // cViewerMaterialHandle, the materials' records

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

class cViewerRefNodeRecursiveImpl : public cViewerRefNodeBaseImpl {
public:
    static constexpr uint32_t kIid = 0xc152b84;      // cIViewerRefNodeRecursive
    static constexpr uint32_t kTypeWord = 0xc152b8e; // the type record's word 4
    static constexpr uint32_t kVersion = 1;

    // 0xa69f70: the viewer base's, then no materials and no slots; the clear description reset (flags 0, colour
    // 1, 0, 0, 0, depth 1, stencil 0). The display-list slot word (+0xf8) is left as it was.
    cViewerRefNodeRecursiveImpl();
    // 0xa6a150: the materials' display-list slots freed when there are any, then the materials and the viewer base's.
    ~cViewerRefNodeRecursiveImpl();
    cViewerRefNodeRecursiveImpl(const cViewerRefNodeRecursiveImpl &) = delete;
    cViewerRefNodeRecursiveImpl &operator=(const cViewerRefNodeRecursiveImpl &) = delete;

    // 0xa6a650 (the vtable's only slot): the viewport matrix of the whole target (pixels from clip space, y down),
    // returns out (sixteen floats).
    virtual float *FillViewport(float *out);

    // 0xa6b980 (the node's slot 123): the display-list slots reallocated for `count` materials, then the list resized.
    void SetMaterialCount(uint32_t count);
    // 0xa6b9c0 (124): material `index` (the list grown to it first; the slots are not) set to the named one (found or
    // loaded), or none for a null name.
    void SetMaterial(uint32_t index, const char *name);

    // 0xa6aec0 (slot 83, thiscall (outer, view, mask); the mask is not read): the reflected view drawn, once a frame.
    // With bit 0 of the plane flags (+0xc0) the setting "reflectionWithExtraViewer" must be on. The eye: the node's
    // world frame, or (with a bound that isn't everywhere and bit 0) the view's frame reflected about the plane carried
    // by the bound's world transform, with an oblique near plane; with such a bound, bit 1 narrows the projection, viewport and rectangle to the bound's box on screen. Drawn when
    // the rectangle covers a pixel of the target each way, then the materials' pass.
    void Cull(cIBoundedNode *outer, cViewerView *view, uint32_t mask);

    bool IsKindOf(uint32_t iid);                          // 0xa6ba80
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out); // 0xa6baa0 {"cViewerRefNodeRecursive", 0xc152b8e, 1}
    // 0xa6b750: the type record, the viewer base's record, then the materials (ReadViewMaterials).
    void Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader);
    // 0xa6bb80: the type record, the viewer base's record, then the materials by name (WriteViewMaterials).
    void Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer);
    // 0xa6bde0: the viewer base's, then the materials copied (not their slots).
    void CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone, cViewerRefNodeRecursiveImpl *cloneImpl,
                   uint32_t mode, cINodeCloneContext *context);

    std::vector<cViewerMaterialHandle> mMaterials; // +0xec
    int32_t mDetailSlot;                           // +0xf8: the display list's slots for the materials
    uint32_t mDetailCount = 0;                     // +0xfc: how many slots it holds
};
static_assert(sizeof(cViewerRefNodeRecursiveImpl) == 0x100, "cViewerRefNodeRecursiveImpl size");
static_assert(offsetof(cViewerRefNodeRecursiveImpl, mMaterials) == 0xec &&
                  offsetof(cViewerRefNodeRecursiveImpl, mDetailCount) == 0xfc,
              "cViewerRefNodeRecursiveImpl layout");

// The mirrored view's geometry (lane VG; reached only from Cull, so in effect through it). Every product and sum keeps
// the original's operand order (a NaN pair gives the first operand's NaN, quieted). Matrices are row major.
namespace recursive_view {

// 0x19c980 (nGZNumerics::cAffineTransform3, __thiscall on the frame, ret 8): the plane `in` (normal, d) carried into the
// frame's space: normal' = transpose(inverse(matrix)) * normal (the inverse 0x19cb30), d' = d - normal' . offset. `out`
// is written as it goes (an `out` that is `in` sees the new values).
void PlaneToFrame(const AffineTransform3 *frame, float *out, const float *in);
// 0x1a6760 (__thiscall on a 3x3, ret 4; returns this): this -= other, element by element.
float *Subtract3(float *self, const float *other);
// 0x1a65e0 (__cdecl; returns out): the transpose of a 4x4, read in order (an `out` that is `m` gets the partly
// transposed result the original gives).
float *__cdecl Transpose4(float *out, const float *m);
// 0x1a6690 (__thiscall on a 4x4, ret 4; returns this): this = this x b (Matrix4Multiply into a temporary, copied back).
float *MultiplyBy4(float *self, const float *b);
// 0xa6c430 (__cdecl; returns out): `frame` followed by the reflection about `plane` (carried into the frame first), with
// its x axis turned back (the matrix's first row negated) so the mirrored eye keeps its handedness; flags 3.
AffineTransform3 *__cdecl Reflect(AffineTransform3 *out, const AffineTransform3 *frame, const float *plane);
// 0xa6bf10 (__cdecl): the projection's near plane moved onto `plane` (carried into `frame`): the third row replaced by
// the plane through the inverse transpose, sign-flipped to face the eye; nothing when its w is within 1.192093e-10 of 0.
void __cdecl ObliqueClip(float *projection, const AffineTransform3 *frame, const float *plane);

} // namespace recursive_view

} // namespace nGZSceneGraph
