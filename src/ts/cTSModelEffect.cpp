#include "ts/cTSModelEffect.h"

#include "compgeom/CompGeom.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "gz/cVector4fEvent.h"
#include "misc/UtilitiesE.h" // GetCachedQueriedObject
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "ts/cTSCompDescription.h"

#include <cstring>
#include <new>
#include <string>
#include <unordered_map>

using nGZSceneGraph::cICompositionTreeNode;
using nGZSceneGraph::cIRenderableNode;
using nGZSceneGraph::cIShape;
using nGZSceneGraph::cIShapeRefNode;

namespace {

constexpr uint32_t kShapeRefClass = 0x65245517;
constexpr uint32_t kShapeClass = 0xfc6eb1f7;
constexpr uint32_t kShapeIid = 0x3c6eb1f0;
constexpr uint32_t kColorAttribute = 0x8c798726;

const cTSModelDescription *Desc(const cTSEffectBase *e) { return static_cast<const cTSModelDescription *>(e->mDescription); }

// 0xc05b10: the layer and the colour attribute on every renderable node of the tree under `node`, the colour copied whole.
void SetColour(cICompositionTreeNode *node, uint32_t index, const float *colour)
{
    if (node->TestFlag(4)) {
        auto *renderable = static_cast<cIRenderableNode *>(node);
        renderable->AddLayer("ShapeColor");
        const nGZEvent::cVector4fEvent value(colour[0], colour[1], colour[2], colour[3]);
        renderable->SetAttribute(kColorAttribute, index, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&value)));
    }
    nGZSceneGraph::cChildRange range = node->GetChildren();
    for (cICompositionTreeNode *c = range.mCurrent; c != range.mEnd; c = c->GetNextSibling()) SetColour(c, index, colour);
}

// 0xc06630: the layer taken off again.
void ClearColour(cICompositionTreeNode *node)
{
    if (node->TestFlag(4)) static_cast<cIRenderableNode *>(node)->RemoveLayer("ShapeColor");
    nGZSceneGraph::cChildRange range = node->GetChildren();
    for (cICompositionTreeNode *c = range.mCurrent; c != range.mEnd; c = c->GetNextSibling()) ClearColour(c);
}

// 0xc066a0: flag 10 of every renderable node of the tree.
void SetFlagTree(cICompositionTreeNode *node, bool value)
{
    if (node->TestFlag(4)) node->SetFlag(10, value);
    nGZSceneGraph::cChildRange range = node->GetChildren();
    for (cICompositionTreeNode *c = range.mCurrent; c != range.mEnd; c = c->GetNextSibling()) SetFlagTree(c, value);
}

bool AllOne(const float *colour)
{
    for (int i = 0; i < 4; ++i)
        if (!nGZCompGeom::NearlyEqual(colour[i], 1.0f)) return false;
    return true;
}

// A new object of `classId` from the framework's class factory (the framework asked before the old object is let go, as the
// original does).
template <class T> void MakeObject(fx::CountedRef<T> &ref, uint32_t classId, uint32_t iid)
{
    auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    ref.Reset();
    com->GetClassObject(classId, iid, reinterpret_cast<void **>(ref.Address()));
}

// The primitive parts' bones of a mesh: the object of class 0x9bffc10d, a builder face with a virtual cIGZUnknown base.
namespace builder {
constexpr uint32_t kPartClass = 0x7ba3838c, kPartIid = 0x5ba38381, kClass = 0x9bffc10d, kIid = 0x9bffc101;
constexpr int kBegin = 0, kEnd = 1, kAddVertex = 9, kBeginSubset = 27, kBeginTriangle = 28, kEndTriangle = 29, kAddIndex = 35;
constexpr int kPartSetMesh = 32;
cIGZUnknown *AsUnknown(void *object)
{
    const int32_t *const bases = *reinterpret_cast<int32_t **>(static_cast<uint8_t *>(object) + 4);
    return reinterpret_cast<cIGZUnknown *>(static_cast<uint8_t *>(object) + bases[1] + 4);
}
} // namespace builder

} // namespace

// Three flat diamonds of size `size`, one along each axis, in the subsets the bone markers' materials bind to.
void **__cdecl cTSModelEffect::MakeAxesPart(void **out, float size)
{
    using namespace builder;
    static const float kVertices[12][3] = {
        {0, 0, 0.1f}, {1, 0, 0}, {0, 0.1f, 0}, {0, -0.1f, 0},
        {0, 0, 0.1f}, {0, 1, 0}, {0.1f, 0, 0}, {-0.1f, 0, 0},
        {0, 0.1f, 0}, {0, 0, 1}, {0.1f, 0, 0}, {-0.1f, 0, 0},
    };
    static const int kCorners[9] = {1, 0, 3, 1, 2, 0, 1, 3, 2};
    static const char *const kSubsets[3] = {"subsetX", "subsetY", "subsetZ"};

    *out = nullptr;
    auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    com->GetClassObject(kPartClass, kPartIid, out);
    void *mesh = nullptr;
    com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    com->GetClassObject(kClass, kIid, &mesh);
    VirtualCall<void(__thiscall *)(void *, void *), kPartSetMesh>(*out, mesh ? AsUnknown(mesh) : nullptr);
    VirtualCall<void(__thiscall *)(void *, int), kBegin>(mesh, 0);
    for (const float *vertex = kVertices[0]; vertex != kVertices[0] + 36; vertex += 3) {
        const float scaled[3] = {vertex[0] * size, vertex[1] * size, vertex[2] * size};
        VirtualCall<void(__thiscall *)(void *, const float *), kAddVertex>(mesh, scaled);
    }
    for (int subset = 0; subset < 3; ++subset) {
        VirtualCall<void(__thiscall *)(void *, const char *), kBeginSubset>(mesh, kSubsets[subset]);
        for (int corner = 0; corner < 9; corner += 3) {
            VirtualCall<void(__thiscall *)(void *), kBeginTriangle>(mesh);
            for (int i = 0; i < 3; ++i)
                VirtualCall<void(__thiscall *)(void *, int), kAddIndex>(mesh, kCorners[corner + i] + subset * 4);
            VirtualCall<void(__thiscall *)(void *), kEndTriangle>(mesh);
        }
    }
    VirtualCall<void(__thiscall *)(void *), kEnd>(mesh);
    if (mesh) AsUnknown(mesh)->Release();
    return out;
}

// The white blended material of the primitives: the material manager's, made there the first time.
uint32_t __cdecl cTSModelEffect::GetWhiteMaterial()
{
    constexpr int kHas = 31, kFind = 19, kAdd = 23;                       // the queried object
    constexpr int kSetName = 10, kSetDefinition = 12, kSetProperty = 13; // the material
    using SetFn = void(__thiscall *)(void *, const char *);
    using PropertyFn = void(__thiscall *)(void *, const char *, const char *);
    void *const materials = GetCachedQueriedObject();
    if (VirtualCall<bool(__thiscall *)(void *, const char *), kHas>(materials, "modelEffectWhite"))
        return VirtualCall<uint32_t(__thiscall *)(void *, const char *), kFind>(materials, "modelEffectWhite");
    void *material = nullptr;
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(0x49596978, 0x49596972, &material);
    VirtualCall<SetFn, kSetName>(material, "modelEffectWhite");
    VirtualCall<SetFn, kSetDefinition>(material, "StandardMaterial");
    VirtualCall<PropertyFn, kSetProperty>(material, "stdMatDiffCoef", "1, 1, 1");
    VirtualCall<PropertyFn, kSetProperty>(material, "stdMatShapeColorIndex", "0");
    VirtualCall<PropertyFn, kSetProperty>(material, "stdMatAlphaBlendMode", "blend");
    const uint32_t id = VirtualCall<uint32_t(__thiscall *)(void *, void *, int), kAdd>(materials, material, 2);
    if (material) static_cast<cIGZUnknown *>(material)->Release();
    return id;
}

namespace {

// Game 0x10fd3cc: the shapes made for primitives, by key.
using ShapeCache = std::unordered_map<std::string, fx::CountedRef<cIShape>>;
ShapeCache &Cache()
{
    static ShapeCache *cache = new ShapeCache; // never freed, as the original's lives to the end of the process
    return *cache;
}

struct Raw24 {
    uint32_t w[6];
};

using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);

} // namespace

cTSModelEffect::cTSModelEffect(const cTSModelDescription *description)
    : cTSNodeEffectBase(description), mColor{1.0f, 1.0f, 1.0f, 1.0f}
{
}

cTSModelEffect::~cTSModelEffect() = default; // the model, then (the base's members) the node and the owner

cITSComponentEffect *__cdecl cTSModelEffect::Create(const cTSModelDescription *description)
{
    return new (std::nothrow) cTSModelEffect(description);
}

void cTSModelEffect::Detach()
{
    mNode.Reset();
    mModel.Reset();
}

void cTSModelEffect::SetFlagged(bool value)
{
    if (mNode.Get()) SetFlagTree(mNode.Get(), value);
}

bool cTSModelEffect::HandleEvent(uint32_t kind, void *a, void *)
{
    const cTSModelDescription *d = Desc(this);
    const float *factors = static_cast<const float *>(a);
    if (kind == 3) {
        mColor[0] = d->mFloat30 * factors[0];
    } else if (kind == 4) {
        mColor[1] = d->mVector24.x * factors[0];
        mColor[2] = d->mVector24.y * factors[1];
        mColor[3] = d->mVector24.z * factors[2];
    } else {
        return false;
    }
    if (mNode.Get()) {
        if (AllOne(mColor)) ClearColour(mNode.Get());
        else SetColour(mNode.Get(), 0, mColor);
    }
    return true;
}

bool cTSModelEffect::SetModel(const char *text)
{
    mModel.Reset();
    StringKeyParser::Result key = {0, 0, 0, 0};
    StringKeyParser::ParseKey(&key, text, 0xe519c933, nullptr);
    cITSModelResources *manager = nullptr;
    if (void *service = GetServiceObject())
        VirtualCall<QueryFn, 5>(service, 0x056b906e, 0x656b8efc, reinterpret_cast<void **>(&manager));
    mModel.Reset();
    // ORIGINAL-BUG: a service that gave no manager is dereferenced all the same (the original crashes here too).
    const bool found = manager->Load(&key, 0xe519c92c, reinterpret_cast<void **>(mModel.Address()), 0, 0);
    if (manager) manager->Release();
    return found;
}

void cTSModelEffect::Attach(cIGZUnknown *owner)
{
    AttachNode(owner, kNodeClass, kNodeIid);
    const cTSModelDescription *d = Desc(this);
    if (d->mName.rfind("_cres") != std::string::npos) {
        SetModel(d->mName.c_str());
    } else {
        AddPrimitive(d);
    }

    if (mModel.Get()) {
        void *copy = nullptr;
        mModel->Clone(1, 0x4512dd13, &copy, nullptr);
        if (copy) {
            auto *tree = static_cast<cICompositionTreeNode *>(copy);
            mNode->AddChild(tree);
            d = Desc(this);
            mColor[1] = d->mVector24.x;
            mColor[2] = d->mVector24.y;
            mColor[3] = d->mVector24.z;
            mColor[0] = d->mFloat30;
            if (!AllOne(mColor)) SetColour(mNode.Get(), 0, mColor);
            tree->Release();
        }
    }
}

// Attach's primitive path (0xc05cf0, the part after the "_cres" test).
void cTSModelEffect::AddPrimitive(const cTSModelDescription *d)
{
    const float f = d->mFloat20;

    struct KeyString : cRZString {
        KeyString() { Construct(); }
        ~KeyString() { Destruct(); }
    } key;
    Raw24 raw;
    std::memcpy(&raw, &d->mName, sizeof(raw));
    // ORIGINAL-BUG: the name (a std::string) goes into the variable arguments by value: "%s" takes its first four bytes as a
    // pointer (the buffer pointer of a long name, the first characters of a short one) and "%f" the next eight as the double,
    // which the float then follows. The key is therefore not made from the name.
    key.Sprintf(GameAddress<const char>(0xe91434), raw, static_cast<double>(f));

    const std::string &name = Desc(this)->mName;
    // ORIGINAL-BUG: the lookup is by the name, the inserts below are by the key (made from the name's raw bytes), so the cache
    // answers only for a name that happens to equal a key already stored.
    auto hit = Cache().find(name);
    if (hit != Cache().end()) {
        fx::CountedRef<cIShapeRefNode> ref;
        MakeObject(ref, kShapeRefClass, cIShapeRefNode::kIid);
        ref->AddShape(hit->second.Get());
        mNode->AddChild(ref.Get());
        ref.Reset();
        return;
    }

    if (name == "axes") {
        fx::CountedRef<cIShapeRefNode> ref;
        MakeObject(ref, kShapeRefClass, cIShapeRefNode::kIid);
        fx::CountedRef<cIShape> shape;
        MakeObject(shape, kShapeClass, kShapeIid);
        void *part = nullptr;
        MakeAxesPart(&part, Desc(this)->mFloat20);
        shape->AddPart(static_cast<cIGZUnknown *>(part), 0);
        shape->BindMaterial("MV_BoneMarkerX", "subsetX");
        shape->BindMaterial("MV_BoneMarkerY", "subsetY");
        shape->BindMaterial("MV_BoneMarkerZ", "subsetZ");
        ref->AddShape(shape.Get());
        mNode->AddChild(ref.Get());
        Cache()[std::string(key.ToChar())].Assign(shape.Get());
        if (part) static_cast<cIGZUnknown *>(part)->Release();
        shape.Reset();
        ref.Reset();
        return;
    }

    void *made = nullptr;
    if (name == "cube") {
        const float low[3] = {-f, -f, -f};
        const float high[3] = {f, f, f};
        nGZSceneGraph::MakeBoxMesh(reinterpret_cast<nGZSceneGraph::cIGeometryNode **>(&made), low, high);
    } else if (name == "sphere") {
        nGZSceneGraph::MakeSphereMesh(reinterpret_cast<nGZSceneGraph::cIGeometryNode **>(&made), f, 0x10, "subset0", true);
    } else if (name == "cone") {
        nGZSceneGraph::MakeConeMesh(reinterpret_cast<nGZSceneGraph::cIGeometryNode **>(&made), f, f + f, true, 0x10);
    } else {
        return;
    }
    fx::CountedRef<cIGZUnknown> part;
    part.Assign(static_cast<cIGZUnknown *>(made));
    if (made) static_cast<cIGZUnknown *>(made)->Release();
    if (!part.Get()) return;

    fx::CountedRef<cIShapeRefNode> ref;
    MakeObject(ref, kShapeRefClass, cIShapeRefNode::kIid);
    fx::CountedRef<cIShape> shape;
    MakeObject(shape, kShapeClass, kShapeIid);
    shape->AddPart(part.Get(), 0);
    {
        // The white material held while the shape binds it (0xae8820 ... 0xae8870).
        const nGZSceneGraph::ShapeMaterialHandle handle(static_cast<int32_t>(GetWhiteMaterial()));
        shape->BindMaterial("modelEffectWhite", "subset0");
        ref->AddShape(shape.Get());
        mNode->AddChild(ref.Get());
        Cache()[std::string(key.ToChar())].Assign(shape.Get());
    }
    shape.Reset();
    ref.Reset();
}
