// cTSParticlesRenderable (lane WQ, rendering row E3). See the header.
#include "ts/cTSParticlesRenderable.h"

#include "compgeom/CompGeom.h"
#include "compgeom/Frustum.h"
#include "gz/cAbstractBehaviorTypeMT.h" // behavior_mt::DefaultAttribute
#include "game/game.h"
#include "game/vtable.h"
#include "misc/UtilitiesE.h" // GetCachedQueriedObject: the material manager
#include "rz/RZFrameworkHelpers.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"
#include "sg/DetailObject.h"
#include "sg/SGBehaviorCopies.h"      // the cell the game's copies share
#include "sg/cIDisplayList.h"
#include "sg/cMaterialDefinition.h"
#include "sg/cMaterialManager.h"
#include "sg/cNodeCloneContext.h"
#include "sg/cTransformNode.h"        // cTransformNode, node_family
#include "sg/cTextureManager.h"
#include "sg/cTransformNodeImpl.h"
#include "sg/cViewerRefNodeBaseImpl.h" // cViewerView
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"
#include "ts/cTSParticlesGeometry.h"

#include <bit>
#include <new>
#include <string>

namespace nGZSceneGraph {

namespace {

// The description's flags.
constexpr uint64_t kNamedMaterial = 0x200000;     // its name is a material's
constexpr uint64_t kShapeFromMaterial = 0x400000; // its name is a shape's
constexpr uint64_t kCentreDepth = 0x800000;       // the geometry's items take the centre's depth
constexpr uint32_t kOverrideTexture = 0x1000000;  // the effect's text, when it has one, replaces the texture
constexpr uint32_t kOverrideComposite = 0x2000000; // ... or is the composite texture
constexpr uint32_t kRawLayer = 0x80000000u;       // the layer is the number itself, not a slot of 8

cIMaterialManager *MaterialManager() { return static_cast<cIMaterialManager *>(GetCachedQueriedObject()); }

// Clears first, as the original's resets do: the object's release can reach the node.
void Drop(cTSParticlesRenderable::Held &held)
{
    if (cIGZUnknown *const object = held.mObject) {
        held.mObject = nullptr;
        object->Release();
    }
}

// The named material found or loaded (0xae8840), then the handle takes it (0xae88a0).
void AssignNamed(ShapeMaterialHandle &handle, const char *name)
{
    const ShapeMaterialHandle found(static_cast<int32_t>(MaterialManager()->FindOrLoad(name)));
    handle.Assign(found);
}

// The framework's slot 5, when there is a framework.
void QueryService(uint32_t id, uint32_t iid, RZAutoRef *out)
{
    void *const framework = reinterpret_cast<void *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
    if (framework) VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(framework, id, iid, &out->mObject);
}

// A texture the manager has goes on the definition as well.
void AddTextureIfKnown(cIMaterialDefinition *definition, const char *texture)
{
    if (!TextureManager()->Has(texture)) return;
    const cTextureHandle handle(texture);
    definition->AddTextureById(handle.mId);
}

// Slot 2 of the object when it goes.
template <class T> struct UnknownRefTo {
    T *mObject = nullptr;
    UnknownRefTo() = default;
    UnknownRefTo(const UnknownRefTo &) = delete;
    UnknownRefTo &operator=(const UnknownRefTo &) = delete;
    ~UnknownRefTo()
    {
        if (mObject) mObject->Release();
    }
};

cIDisplayList *SlotList() { return static_cast<cIDisplayList *>(GetDetailObject()); }

// Every word plus zero, which is what the original's reads do (-0 becomes +0, a signalling NaN goes quiet).
nGZCompGeom::BoundBox EffectBound(const cTSParticlesEffect &effect)
{
    nGZCompGeom::BoundBox box;
    box.min.x = fx::Add(effect.mBound[0], 0.0f);
    box.min.y = fx::Add(effect.mBound[1], 0.0f);
    box.min.z = fx::Add(effect.mBound[2], 0.0f);
    box.max.x = fx::Add(effect.mBound[3], 0.0f);
    box.max.y = fx::Add(effect.mBound[4], 0.0f);
    box.max.z = fx::Add(effect.mBound[5], 0.0f);
    return box;
}

// Slot 30 of a mesh (the object has no class here): its geometry, not counted.
cIGeometryRender *GeometryOf(void *mesh) { return VirtualCall<cIGeometryRender *(__thiscall *)(void *), 30>(mesh); }

} // namespace

nGZEvent::cIConnectionManager *&cTSParticlesRenderableBehaviorTraits::EventManagerCell() { return ts2_rva_10fc820; }

nGZEvent::cEventAttributeMap *cTSParticlesRenderableBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return cTransformNodeBehaviorTraits::RegistryFor(0, &defaults);
}

cTSParticlesRenderable::SlotRun::~SlotRun()
{
    if (mStart != -1) SlotList()->FreeSlots(mStart, 1);
}

cTSParticlesRenderable::SlotRuns::~SlotRuns()
{
    if (mCount != 0) SlotList()->FreeSlots(mStart, mCount);
}

// ORIGINAL-BUG: the copy is a plain cTransformNode that takes the source's flags, the bounded bit with them, so a disconnect
// (adding the copy to a cloned parent is one) reads slot 69 past the end of the plain node's vtable. The game's nodes are leaves.
void cTSParticlesRenderable::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cICompositionTreeNode *clone = nullptr;
    if (context) {
        context->FindClone(this, cITransformNode::kIid, &clone);
    } else {
        clone = new cTransformNode;
        clone->AddRef();
    }
    if (mode != 2) {
        auto *const cloneImpl = reinterpret_cast<cTransformNodeImpl *>(reinterpret_cast<uint8_t *>(clone) + 0xc);
        static_cast<cTransformNodeImpl &>(mImpl).CloneInto(this, clone, cloneImpl, mode, context);
    }
    clone->QueryInterface(iid, out);
    if (clone) clone->Release();
}

void *cTSParticlesRenderable::MakeIterator43(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator43(&mImpl, out, this, a, b);
}
void *cTSParticlesRenderable::MakeIterator44(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator44(&mImpl, out, this, a, b);
}
void *cTSParticlesRenderable::MakeIterator51(void *out, uint32_t a) { return node_family::BuildIterator51(&mImpl, out, a); }

cBoundBox cTSParticlesRenderable::GetBoundBoxIn(const cTransform34 *frame)
{
    nGZCompGeom::BoundBox box = EffectBound(*mEffect);
    box.Transform(reinterpret_cast<const AffineTransform3 *>(frame));
    return reinterpret_cast<const cBoundBox &>(box);
}

cBoundBox cTSParticlesRenderable::GetBoundBox()
{
    const nGZCompGeom::BoundBox box = EffectBound(*mEffect);
    return reinterpret_cast<const cBoundBox &>(box);
}

void cTSParticlesRenderable::Draw(void *viewer, uint32_t planeMask)
{
    if (mEffect->mCount <= 0) return;
    auto *const view = static_cast<cViewerView *>(viewer);
    const AffineTransform3 *const frame = reinterpret_cast<const AffineTransform3 *>(&view->mFrame);
    const nGZCompGeom::BoundBox box = EffectBound(*mEffect);
    // The test leaves its verdict in the mask's low byte.
    if (view->mFrustum.ClassifyBox(reinterpret_cast<uint8_t *>(&planeMask), &box, frame) <= 0)
        static_cast<cIDisplayList *>(view->mDrawer)->DrawNode(this, frame, &box, planeMask);
}

void cTSParticlesRenderable::Slot83(void *viewer, void *)
{
    auto *const list = static_cast<cIDisplayList *>(static_cast<cViewerView *>(viewer)->mDrawer);
    const uint32_t *subsetWords = nullptr;
    uint32_t word24 = 0;
    const auto drawSlot = [this] {
        if (mDrawSlot.mStart == -1) mDrawSlot.mStart = SlotList()->AllocSlots(1);
        return mDrawSlot.mStart;
    };

    if (mGeometry.mObject) {
        auto *const geometry = static_cast<cTSParticlesGeometry *>(mGeometry.mObject);
        const nGZCompGeom::BoundBox box = EffectBound(*mEffect);
        geometry->SetBounds(reinterpret_cast<const float *>(&box));
        const uint32_t flags = (mEffect->mDescription->mFlags & kCentreDepth) ? 0x41 : 0x40;
        const int32_t slot = drawSlot();
        list->AddGeometrySubsets(geometry, flags, &mMaterial.mIndex, &subsetWords, &word24, nullptr, nullptr, slot);
    }
    if (mMesh.mObject) {
        const int32_t slot = drawSlot();
        list->AddGeometrySubsets(GeometryOf(mMesh.mObject), 1, &mVisBound.mIndex, &subsetWords, &word24, nullptr, nullptr, slot);
    }
    if (mMeshes.mFirst == mMeshes.mLast) return;
    const int32_t count = static_cast<int32_t>(mMeshes.mLast - mMeshes.mFirst);
    uint32_t subsets = 0;
    for (int32_t i = 0; i < count; ++i) subsets += *GeometryOf(mMeshes.mFirst[i])->GetFlags();
    if (mMeshSlots.mCount < subsets) {
        mMeshSlots.mStart = SlotList()->ReallocSlots(mMeshSlots.mStart, mMeshSlots.mCount, subsets);
        mMeshSlots.mCount = subsets;
    }
    int32_t slot = mMeshSlots.mStart;
    for (int32_t i = 0; i < count; ++i) {
        cIGeometryRender *const geometry = GeometryOf(mMeshes.mFirst[i]);
        list->AddGeometrySubsets(geometry, 1, &mVisCollider.mIndex, &subsetWords, &word24, nullptr, nullptr, slot);
        slot += *geometry->GetFlags();
    }
}

void cTSParticlesRenderable::Setup(cTSParticlesDescription *description)
{
    Drop(mShape);
    for (uint32_t &param : mParams) param = 0xffffffffu;
    AssignNamed(mVisBound, "VisBound_Particles");
    AssignNamed(mVisCollider, "VisCollider");

    if (description->mFlags & kShapeFromMaterial) {
        Drop(mGeometry);
        LoadShape(description);
        return;
    }
    if (!mGeometry.mObject) {
        auto *const geometry = new cTSParticlesGeometry(mEffect);
        geometry->AddRef();
        mGeometry.mObject = geometry;
    }
    if (description->mFlags & kNamedMaterial) AssignNamed(mMaterial, description->mMaterial.c_str());
    else if (description->mMaterial.size() != 0) BuildMaterial(description);
}

// The material of the description's name holds a shape: the shape's first material gives the shader's parameters.
void cTSParticlesRenderable::LoadShape(cTSParticlesDescription *description)
{
    RZAutoRef materials{nullptr}; // (held through the load; the calls go through the cached pointer)
    QueryService(0xe5074b6b, 0x25074b4d, &materials);
    StringKeyParser::Result key{};
    StringKeyParser::ParseKey(&key, description->mMaterial.c_str(), 0xfc6eb1f7, nullptr);
    bool loaded;
    {
        RZAutoRef resources{nullptr};
        QueryService(0x56b906e, 0x656b8efc, &resources);
        Drop(mShape);
        loaded = VirtualCall<bool(__thiscall *)(void *, const StringKeyParser::Result *, uint32_t, cIGZUnknown **, uint32_t, uint32_t), 3>(
            resources.mObject, &key, 0x3c6eb1f0, &mShape.mObject, 0, 0);
        resources.ReleaseObject();
    }
    if (loaded) {
        const ShapeMaterialHandle none;
        mMaterial.Assign(none);
        ReadShapeParameters();
    }
    materials.ReleaseObject();
}

void cTSParticlesRenderable::ReadShapeParameters()
{
    // The shape's slot 54: three iterators over its materials (each counted for the caller), slot 4 "at this end", slot 5
    // the element.
    struct Iterators {
        cIGZUnknown *mCursor, *mBegin, *mEnd;
        ~Iterators()
        {
            if (mEnd) mEnd->Release();
            if (mBegin) mBegin->Release();
            if (mCursor) mCursor->Release();
        }
    };
    struct Element {
        uint32_t mWord;
        int32_t mMaterialIndex;
        uint32_t mMore[2];
    };
    // What the material's slot 12 answers for a parameter.
    struct Parameter {
        uint32_t mFound, mValue, mSize, mMore;
    };

    Iterators range{nullptr, nullptr, nullptr};
    VirtualCall<void(__thiscall *)(void *, Iterators *, uint32_t), 54>(mShape.mObject, &range, 0);
    if (VirtualCall<bool(__thiscall *)(void *, cIGZUnknown *), 4>(range.mCursor, range.mEnd)) return;
    cIMaterialManager *const materials = MaterialManager();
    Element element;
    VirtualCall<void(__thiscall *)(void *, Element *), 5>(range.mCursor, &element);
    const MaterialInfo info = materials->GetInfo(element.mMaterialIndex);
    if (info.nameHash == 0) {
        Drop(mShape);
        return;
    }
    void *const parsed = info.parsed;
    if (!parsed || VirtualCall<bool(__thiscall *)(void *), 6>(parsed)) return;
    const auto read = [parsed](const char *name, uint32_t type, uint32_t size, uint32_t &word) {
        Parameter parameter;
        parameter = *VirtualCall<Parameter *(__thiscall *)(void *, Parameter *, const char *, uint32_t), 12>(parsed, &parameter, name, type);
        if (parameter.mFound != 0 && parameter.mSize == size) word = parameter.mValue;
    };
    constexpr uint32_t kVector3 = 0x7b5b330c, kVector4 = 0x5b5b331a, kScalar = 0xbb22dd17;
    read("color", kVector3, 0xc, mParams[0]);
    read("alpha", kScalar, 4, mParams[1]);
    read("scale", kScalar, 4, mParams[2]);
    read("colorAlpha4", kVector4, 0x10, mParams[3]);
    read("scale4", kVector4, 0x10, mParams[4]);
    read("velocity4", kVector4, 0x10, mParams[5]);
    read("info4", kVector4, 0x10, mParams[6]);
}

void cTSParticlesRenderable::BuildMaterial(cTSParticlesDescription *description)
{
    cIMaterialManager *const materials = MaterialManager();
    const uint32_t flags = static_cast<uint32_t>(description->mFlags);
    const std::string &override = mEffect->TextureOverride();
    const char *texture = description->mMaterial.c_str();
    const char *composite = nullptr;
    if ((flags & kOverrideTexture) && override.size() != 0) {
        texture = override.c_str();
    } else if ((flags & kOverrideComposite) && override.size() != 0) {
        composite = override.c_str();
    }
    // The layer rounded as the original does: a float biased into [2^23, 2^24), its low bits.
    int32_t layer = static_cast<int32_t>(std::bit_cast<uint32_t>(fx::Add(description->mLayer, 12582912.0f)) + 0xb4c00000u);
    if (!(flags & kRawLayer)) layer = static_cast<int32_t>(static_cast<uint32_t>(layer) * 8 + 7);

    cRZString name;
    name.Construct();
    name.Sprintf(GameAddress<const char>(0xe8c96c), texture, composite ? composite : GameAddress<const char>(0xdad360), description->mDraw, layer);
    if (materials->IsLoaded(name.ToChar())) {
        AssignNamed(mMaterial, name.ToChar());
        name.Destruct();
        return;
    }

    UnknownRefTo<cIMaterialDefinition> definition;
    VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, cIMaterialDefinition **), 3>(
        GetDirectorFrameworkObject(), 0x49596978, cIMaterialDefinition::kIid, &definition.mObject);
    cIMaterialDefinition *const d = definition.mObject;
    d->SetMaterialName(name.ToChar());
    d->SetMaterialType("StandardParticlesMaterial");
    d->SetProperty("stdParticleTexture", texture);
    AddTextureIfKnown(d, texture);
    if (composite) {
        d->SetProperty("stdParticleCompositeTexture", composite);
        AddTextureIfKnown(d, composite);
    }
    switch (description->mDraw) {
    case 1: d->SetProperty("stdPMInvertDepth", "true"); break;
    case 3: d->SetProperty("stdPMDoDepthWrite", "true"); break;
    case 4: d->SetProperty("stdPMAdditive", "true"); break;
    case 5:
        d->SetProperty("stdPMAdditive", "true");
        d->SetProperty("stdPMInvertDepth", "true");
        break;
    case 6: d->SetProperty("stdPMAdditive", "true"); [[fallthrough]];
    case 2: d->SetProperty("stdPMDoDepthTest", "false"); break;
    case 7: d->SetProperty("stdPMModulate", "true"); break;
    default: break;
    }
    if (layer != 0) {
        cRZString text;
        text.Construct();
        text.Sprintf(GameAddress<const char>(0xd9b648), layer);
        d->SetProperty("stdPMLayer", text.ToChar());
        text.Destruct();
    }
    const ShapeMaterialHandle added(static_cast<int32_t>(materials->AddMaterial(d, 2)));
    mMaterial.Assign(added);
    name.Destruct();
}

// 0xbb6670.
void cTSParticlesRenderable::AddParticleChild()
{
    cICompositionTreeNode *child = nullptr;
    // receiver: cIGZCOM
    VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, cICompositionTreeNode **), 3>(GetDirectorFrameworkObject(), 0x65245517,
                                                                                            0xa516cd10, &child);
    VirtualCall<void(__thiscall *)(void *, cIGZUnknown *), 94>(child, mShape.mObject);
    using SetFlag = void(__thiscall *)(void *, uint32_t, uint32_t);
    VirtualCall<SetFlag, 29>(child, 0x15u, 1u);
    const int32_t layer = mEffect->mSettings->mRenderLayer;
    if (layer > 0) VirtualCall<SetFlag, 29>(child, static_cast<uint32_t>(layer), 1u);
    AddChild(child);
    if (child) child->Release();
}

// 0xbbcef0.
void cTSParticlesRenderable::RemoveParticleChild()
{
    if (HasChildren()) RemoveChild(GetChildren().mCurrent);
}

cTSParticlesRenderable *__fastcall cTSParticlesRenderableAdapters::Construct(Self *memory, void *, cTSParticlesEffect *effect)
{
    return ::new (memory) cTSParticlesRenderable(effect);
}

void __fastcall cTSParticlesRenderableAdapters::Destruct(Self *self, void *) { self->cTSParticlesRenderable::~cTSParticlesRenderable(); }

void __fastcall cTSParticlesRenderableAdapters::Setup(Self *self, void *, cTSParticlesDescription *description) { self->Setup(description); }

void __fastcall cTSParticlesRenderableAdapters::BuildMaterial(Self *self, void *, cTSParticlesDescription *description)
{
    self->BuildMaterial(description);
}

} // namespace nGZSceneGraph
