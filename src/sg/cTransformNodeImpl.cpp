// nGZSceneGraph::cTransformNodeImpl (lane OB). See the header.
#include "sg/cTransformNodeImpl.h"

#include "compgeom/Frames.h"
#include "compgeom/Rigid.h"
#include "compgeom/SeparatingAxes.h"
#include "game/original.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/cNodeCloneContext.h"

#include <cstring>
#include <new>

namespace nGZSceneGraph {

namespace game_code {

cRigidTransform *ComposeRigid(cRigidTransform *self, const cRigidTransform *rhs)
{
    // 0x19c080, ours (src/compgeom/Rigid.h)
    reinterpret_cast<nGZCompGeom::RigidTransform3 *>(self)->Compose(reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(rhs));
    return self;
}

void ComposeFrameWithRigid(AffineTransform3 *frame, const cRigidTransform *rigid)
{
    // 0x19be70 (ours; no caller reads its result)
    static_cast<nGZCompGeom::FramePlacement *>(frame)->ComposeRigid(reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(rigid));
}

AffineTransform3 *SetFrameRotation(AffineTransform3 *frame, const float *quaternion)
{
    static_cast<nGZCompGeom::FrameMath *>(frame)->SetRotation(quaternion); // 0x19d0d0 (ours; no caller reads its result)
    return frame;
}

AffineTransform3 *ComposeFrames(AffineTransform3 *self, const AffineTransform3 *rhs)
{
    // 0x19be20 (ours)
    return static_cast<nGZCompGeom::FramePlacement *>(self)->Compose(static_cast<const nGZCompGeom::FramePlacement *>(rhs));
}

} // namespace game_code

namespace {
// The limit travels in a float slot's bytes of the attribute record.
uint32_t WordOf(float f)
{
    uint32_t word;
    std::memcpy(&word, &f, sizeof word);
    return word;
}
float FloatOf(uint32_t word)
{
    float f;
    std::memcpy(&f, &word, sizeof f);
    return f;
}
} // namespace

cTransformNodeImpl *cTransformNodeImpl::Construct() { return ::new (this) cTransformNodeImpl; }

cRigidTransform *cTransformNodeImpl::GetWorldTransform(cRigidTransform *out)
{
    if (mParent) {
        const cRigidTransform parentWorld = mParent->GetWorldTransform();
        cRigidTransform local = mTransform;
        *out = *game_code::ComposeRigid(&local, &parentWorld);
        return out;
    }
    *out = mTransform;
    return out;
}

void cTransformNodeImpl::FromParent34(cTransform34 *out, const cTransform34 *in)
{
    *out = *in;
    game_code::ComposeFrameWithRigid(reinterpret_cast<AffineTransform3 *>(out), &mTransform);
}

void cTransformNodeImpl::FromParent1c(cRigidTransform *out, const cRigidTransform *in)
{
    *out = *in;
    game_code::ComposeRigid(out, &mTransform);
}

void cTransformNodeImpl::ToParent34(cTransform34 *out, const cTransform34 *in)
{
    AffineTransform3 frame;
    frame.mUnknown0 = 1;
    game_code::SetFrameRotation(&frame, mTransform.Rotation());
    std::memcpy(frame.mOffset, mTransform.Translation(), sizeof frame.mOffset);
    const AffineTransform3 *result = game_code::ComposeFrames(&frame, reinterpret_cast<const AffineTransform3 *>(in));
    std::memcpy(out, result, sizeof *out);
}

void cTransformNodeImpl::ToParent1c(cRigidTransform *out, const cRigidTransform *in)
{
    cRigidTransform local = mTransform;
    *out = *game_code::ComposeRigid(&local, in);
}

void cTransformNodeImpl::GetAttribute(cAttributeValue *out, uint32_t id, uint32_t)
{
    switch (id) {
    case cITransformNode::kLimitAttribute: out->mValues[0] = FloatOf(mLimit); break;
    case cITransformNode::kTransformAttribute: std::memcpy(out->mValues, mTransform.mValues, sizeof mTransform.mValues); break;
    case cITransformNode::kRotationAttribute: std::memcpy(out->mValues, mTransform.Rotation(), 4 * sizeof(float)); break;
    case cITransformNode::kTranslationAttribute: std::memcpy(out->mValues, mTransform.Translation(), 3 * sizeof(float)); break;
    default: break;
    }
}

void cTransformNodeImpl::SetAttribute(cICompositionTreeNode *outer, uint32_t id, uint32_t, const cAttributeValue *value)
{
    cRigidTransform rigid;
    switch (id) {
    case cITransformNode::kLimitAttribute: SetLimit(outer, WordOf(value->mValues[0])); return;
    case cITransformNode::kTransformAttribute: std::memcpy(rigid.mValues, value->mValues, sizeof rigid.mValues); break;
    case cITransformNode::kRotationAttribute:
        std::memcpy(rigid.Translation(), mTransform.Translation(), 3 * sizeof(float));
        std::memcpy(rigid.Rotation(), value->mValues, 4 * sizeof(float));
        break;
    case cITransformNode::kTranslationAttribute:
        std::memcpy(rigid.Translation(), value->mValues, 3 * sizeof(float));
        std::memcpy(rigid.Rotation(), mTransform.Rotation(), 4 * sizeof(float));
        break;
    default: return;
    }
    SetTransform(outer, &rigid);
}

void cTransformNodeImpl::Read(cICompositionTreeNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cCompositionTreeNodeImpl::Read(outer, reader);
    float values[7];
    for (float &value : values) value = reader->GetFloat32();
    std::memcpy(mTransform.mValues, values, sizeof values);
    mLimit = reader->GetUint32();
    if (info.mName) info.mName->Release();
}

void cTransformNodeImpl::SetLimit(cICompositionTreeNode *outer, uint32_t limit)
{
    if (mLimit == limit) return;
    if ((mFlags >> kLimitCheckedFlag & 1) == 0) {
        NodeTransformChanged(outer);
        mLimit = limit;
        NodeTransformChanged(outer);
        return;
    }
    mLimit = limit;
}

void cTransformNodeImpl::SetTransform(cICompositionTreeNode *outer, const cRigidTransform *transform)
{
    mTransform = *transform;
    NodeTransformChanged(outer);
}

bool cTransformNodeImpl::IsKindOf(uint32_t iid) { return iid == kIid || cCompositionTreeNodeImpl::IsKindOf(iid); }

cPersistTypeInfo *cTransformNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cTransformNode");
    out->mWord4 = kTypeWord4;
    out->mVersion = kVersion;
    return out;
}

void cTransformNodeImpl::Write(cICompositionTreeNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cCompositionTreeNodeImpl::Write(outer, writer);
    for (float value : mTransform.mValues) writer->SetFloat32(value);
    writer->SetUint32(mLimit);
}

void cTransformNodeImpl::CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone,
                                   cTransformNodeImpl *cloneImpl, uint32_t mode, cINodeCloneContext *context)
{
    CloneCompositionInto(this, nullptr, source, clone, cloneImpl, mode, context);
    cloneImpl->mLimit = mLimit;
    cloneImpl->mTransform = mTransform;
}

} // namespace nGZSceneGraph
