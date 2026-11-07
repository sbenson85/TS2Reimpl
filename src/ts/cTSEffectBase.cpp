#include "ts/cTSEffectBase.h"

#include "compgeom/SeparatingAxes.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "sg/cIndexedMeshBuilder.h"

using fx::Add;
using fx::EffectsTransform;
using fx::Mul;
using fx::Vector3;

cITSComponentEffect::~cITSComponentEffect() = default;
cTSEffectBase::~cTSEffectBase() = default;

void cTSNodeEffectBase::Start(uint32_t)
{
    if (!mRunning) mRunning = true;
}

void cTSNodeEffectBase::Stop(uint32_t)
{
    if (!mRunning) return;
    mRunning = false;
    if (mNode->GetParent()) mNode->GetParent()->RemoveChild(mNode.Get());
}

void cTSNodeEffectBase::AddToScene(void *, void *)
{
    if (mNode->GetParent()) return;
    auto *parent = VirtualCall<nGZSceneGraph::cICompositionTreeNode *(__thiscall *)(void *), 42>(mOwner.Get());
    if (parent) parent->AddChild(mNode.Get());
}

void cTSNodeEffectBase::Update(const EffectsTransform *local, const EffectsTransform *parent)
{
    const float x = Mul(parent->scale, local->translation.x);
    const float y = Mul(parent->scale, local->translation.y);
    const float z = Mul(parent->scale, local->translation.z);
    const Vector3 world = {Add(fx::RowFromX(x, y, z, parent->m[0][0], parent->m[0][1], parent->m[0][2]), parent->translation.x),
                           Add(fx::RowFromX(x, y, z, parent->m[1][0], parent->m[1][1], parent->m[1][2]), parent->translation.y),
                           Add(fx::RowFromX(x, y, z, parent->m[2][0], parent->m[2][1], parent->m[2][2]), parent->translation.z)};
    float product[9];
    nGZCompGeom::MatrixProduct(product, &local->m[0][0], &parent->m[0][0]);
    float quaternion[4];
    nGZSceneGraph::indexedmesh::QuaternionFromMatrix(quaternion, product);
    mNode->SetRotation(quaternion);
    mNode->SetTranslation(&world.x);
}

void cTSNodeEffectBase::AttachNode(cIGZUnknown *owner, uint32_t classId, uint32_t iid)
{
    mOwner.Assign(owner);
    auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    mNode.Reset();
    com->GetClassObject(classId, iid, reinterpret_cast<void **>(mNode.Address()));
}
