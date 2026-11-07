#pragma once
// nGZSceneGraph::cTransformNodeImpl (lane OB, scenegraph N0 part 3 step 1): the state of a transform node (the transform
// node template's member at +0xc) on top of the composition tree node's: a limit and a rigid transform (a translation
// and a rotation quaternion) relative to the parent. A plain class like its base cCompositionTreeNodeImpl; `outer` is
// the node that holds it. Hooked at its RVAs (the templates' forwarders, folded across the node family, reach it there).
// The destructor 0xa3a140 is a jump to the composition Impl's (0xa36bd0, ours), so it has no hook of its own.
//
// Also here, because every transform-level Impl calls them: the node family's game code that stays original for now,
// as small typed wrappers (OriginalFunction in one place): nGZCompGeom's rigid and affine transform arithmetic. (The tree
// code that announces a transform change and refreshes a node's cached world transform is ours: sg/SGTreeCode.h,
// sg/SGWorldTransformRefresh.h.)
#include "misc/UtilitiesC.h" // AffineTransform3: flags, 3x3 matrix, offset (13 words)
#include "sg/cCompositionTreeNodeImpl.h"
#include "sg/SGTreeCode.h"

#include <cstdint>

namespace nGZSceneGraph {

namespace game_code {
// nGZCompGeom (0x19c080 / 0x19be70 / 0x19d0d0 / 0x19be20; thiscall on the first operand, which they update and return).
// A rigid transform is a translation then a rotation quaternion; a frame is flags, nine matrix floats and an offset.
cRigidTransform *ComposeRigid(cRigidTransform *self, const cRigidTransform *rhs);       // self = self then rhs
void ComposeFrameWithRigid(AffineTransform3 *frame, const cRigidTransform *rigid);       // frame (13 words) with a rigid
AffineTransform3 *SetFrameRotation(AffineTransform3 *frame, const float *quaternion);   // flags and matrix from a quaternion
AffineTransform3 *ComposeFrames(AffineTransform3 *self, const AffineTransform3 *rhs);   // self = self then rhs
} // namespace game_code
// The tree code (NodeTransformChanged when a transform or limit changes) is ours: sg/SGTreeCode.h.

class cTransformNodeImpl : public cCompositionTreeNodeImpl {
public:
    static constexpr uint32_t kVersion = 7;           // what Write records
    static constexpr uint32_t kTypeWord4 = 0x65246462; // the record's second word
    static constexpr uint32_t kNoLimit = 0x7fffffff;
    static constexpr uint32_t kIid = 0xe51c10f5;       // cITransformNode
    enum : uint32_t { kLimitCheckedFlag = 7 };         // flag 7: SetLimit does not announce the change

    cTransformNodeImpl() = default;
    cTransformNodeImpl(const cTransformNodeImpl &) = delete;
    cTransformNodeImpl &operator=(const cTransformNodeImpl &) = delete;

    cTransformNodeImpl *Construct(); // 0xa3a020 (thiscall, returns this)

    // 0xa3a7d0: this node's transform after its parent's world transform (the local one without a parent); returns out.
    cRigidTransform *GetWorldTransform(cRigidTransform *out);
    void FromParent34(cTransform34 *out, const cTransform34 *in);   // 0xa3a860: out = in, then this transform
    void FromParent1c(cRigidTransform *out, const cRigidTransform *in); // 0xa3a8a0: out = in then this transform
    void ToParent34(cTransform34 *out, const cTransform34 *in);     // 0xa3a8d0: this transform's frame, then in
    void ToParent1c(cRigidTransform *out, const cRigidTransform *in);   // 0xa3a960: this transform then in
    // 0xa3a9b0 / 0xa3acb0: the node's attributes (cITransformNode): limit, transform, rotation, translation. `extra` is
    // a stack word the slot takes and ignores. An unknown id does nothing.
    void GetAttribute(cAttributeValue *out, uint32_t id, uint32_t extra);
    void SetAttribute(cICompositionTreeNode *outer, uint32_t id, uint32_t extra, const cAttributeValue *value);
    void Read(cICompositionTreeNode *outer, ifaces::cIGZPersistReader *reader);   // 0xa3aa60
    // 0xa3ab70: a changed limit is announced around the change (before and after) unless flag 7 is set.
    void SetLimit(cICompositionTreeNode *outer, uint32_t limit);
    void SetTransform(cICompositionTreeNode *outer, const cRigidTransform *transform); // 0xa3abb0: announced after
    bool IsKindOf(uint32_t iid);                                                  // 0xa3abe0
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);                        // 0xa3ac00 ("cTransformNode", 7)
    void Write(cICompositionTreeNode *outer, ifaces::cIGZPersistWriter *writer);  // 0xa3ad50
    // 0xa3ae50: the composition part copied by the composition Impl's CloneInto (CloneCompositionInto, sg/cNodeCloneContext.h), then limit and transform into
    // the clone's Impl (not the bounded levels' own state).
    void CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone, cTransformNodeImpl *cloneImpl,
                   uint32_t mode, cINodeCloneContext *context);

    uint32_t mLimit = kNoLimit;                          // +0x1c
    cRigidTransform mTransform = {{0, 0, 0, 0, 0, 0, 1}}; // +0x20 (translation +0x20, rotation +0x2c)
};
static_assert(sizeof(cTransformNodeImpl) == 0x3c, "cTransformNodeImpl size");

} // namespace nGZSceneGraph
