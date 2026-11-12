// nGZSceneGraph's debug text billboards (lane 261111m): the free functions of src/sg/SGUtilities.h's "text billboards"
// block, over cTextureTextUpdateStrategy (src/sg/cTextureTextUpdateStrategy.h).
#include "sg/SGUtilities.h"

#include "gz/cIGZCOM.h"
#include "misc/UtilitiesE.h" // GetCachedQueriedObject: the material manager
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/cGeometryNode.h"
#include "sg/cMaterialDefinition.h"
#include "sg/cMaterialManager.h"
#include "sg/cProceduralTexture.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "sg/cTextureManager.h"
#include "sg/cTextureTextUpdateStrategy.h"

#include <cstring>
#include <new>
#include <string>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kShapeRefNodeClass = 0x65245517;
constexpr uint32_t kProceduralTextureClass = 0x6b81dfe9;
constexpr uint32_t kMaterialFlags = 2;  // the material manager's AddMaterial flags
constexpr uint32_t kTextureFlags = 1;   // the texture manager's Add flags
constexpr uint32_t kTextureLevels = 1;
constexpr int32_t kMinColourBits = 0x10;
constexpr int32_t kMinAlphaBits = 4;
constexpr char kNodeTagFormat[] = "@@TextOutput_%s";
constexpr char kTextureFormat[] = "@@TextOutput_%d_%s";
constexpr char kSubset[] = "layer";

// A cRZString on the stack as the original builds its temporaries (vtable, empty text, count 0), destroyed at the end
// of its scope.
struct LocalString {
    union {
        cRZString mText;
    };
    LocalString()
    {
        ::new (&mText.mString) std::string();
        mText.mVTable = cRZString::VTable();
        mText.mRefCount = 0;
    }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { mText.Destruct(); }
    const char *c_str() const { return mText.mString.c_str(); }
};

int32_t NodeWord(const void *node) { return int32_t(reinterpret_cast<uintptr_t>(node)); }

template <class T> void Create(uint32_t classId, uint32_t iid, T **cell)
{
    if (T *const held = *cell) {
        *cell = nullptr;
        held->Release();
    }
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(classId, iid, reinterpret_cast<void **>(cell));
}

// The first child of `node` (slot 42's range, slot 38 next) whose tag is `tag`, or null.
cICompositionTreeNode *FindTaggedChild(cICompositionTreeNode *node, const char *tag)
{
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        const char *const childTag = GetNodeTag(child);
        if (childTag && std::strcmp(childTag, tag) == 0) return child;
    }
    return nullptr;
}

// A material property set to a value formatted into a temporary string (its text through the string's slot 5).
template <class... Args> void SetFormattedProperty(cIMaterialDefinition *material, const char *name, const char *format,
                                                   Args... args)
{
    LocalString value;
    material->SetProperty(name, value.mText.Sprintf(format, args...)->AsInterface()->ToChar());
}

} // namespace

// 0xaf42e0
TextOutput *FindNodeTextOutput(TextOutput *out, cICompositionTreeNode *node, const char *name)
{
    LocalString texture;
    texture.mText.Sprintf(kTextureFormat, NodeWord(node), name);
    if (TextureManager()->Has(texture.c_str())) {
        const uint32_t id = TextureManager()->FindOrLoad(texture.c_str());
        cIProceduralTexture *procedural = nullptr;
        // ORIGINAL-BUG: the texture the id answers is not checked before it is asked.
        TextureManager()->GetTexture(id)->QueryInterface(cIProceduralTexture::kIid, reinterpret_cast<void **>(&procedural));
        if (procedural) {
            void *strategy = nullptr;
            procedural->GetStrategy()->QueryInterface(cTextureTextUpdateStrategy::kIid, &strategy);
            out->mTexture = procedural;
            out->mStrategy = static_cast<cTextureTextUpdateStrategy *>(strategy);
            if (strategy) static_cast<cIGZUnknown *>(strategy)->Release();
            procedural->Release();
            return out;
        }
    }
    out->mTexture = nullptr;
    out->mStrategy = nullptr;
    return out;
}

// 0xaf4250
void AddNodeTextLabel(cICompositionTreeNode *node, const char *name, const uint32_t *colour, const char *text)
{
    TextOutput output;
    FindNodeTextOutput(&output, node, name);
    if (!output.mTexture) return;
    // ORIGINAL-BUG: a texture whose strategy did not answer is pushed to through null.
    output.mStrategy->Push(*colour, text);
    output.mTexture->Redraw();
}

// 0xaf42a0
void ClearNodeTextLabels(cICompositionTreeNode *node, const char *name)
{
    TextOutput output;
    FindNodeTextOutput(&output, node, name);
    if (!output.mTexture) return;
    output.mStrategy->Clear(); // ORIGINAL-BUG: as AddNodeTextLabel, unchecked
    output.mTexture->Redraw();
}

// 0xaf4120
void RemoveBillboardTextOutput(cICompositionTreeNode *node, const char *name)
{
    LocalString tag;
    tag.mText.Sprintf(kNodeTagFormat, name);
    if (cICompositionTreeNode *const child = FindTaggedChild(node, tag.c_str())) node->RemoveChild(child);
}

// 0xaf3860
void MakeBillboardTextOutput(cICompositionTreeNode *node, const float *translation, const char *name, uint32_t width,
                             uint32_t height, const float *colour, uint32_t fontSize)
{
    LocalString tag;
    tag.mText.Sprintf(kNodeTagFormat, name);
    if (FindTaggedChild(node, tag.c_str())) return;

    LocalString materialName;
    materialName.mText.Sprintf(kTextureFormat, NodeWord(node), name);
    LocalString textureName;
    textureName.mText.Sprintf(kTextureFormat, NodeWord(node), name);
    const bool blend = !(colour[0] == 1.0f); // a NaN alpha blends too

    cIMaterialDefinition *material = nullptr;
    Create(cMaterialDefinition::kClassId, cIMaterialDefinition::kIid, &material);
    // ORIGINAL-BUG: none of the objects made here is checked before it is used.
    material->SetMaterialName(materialName.c_str());
    material->SetMaterialType("BillboardTextOutputMaterial");
    SetFormattedProperty(material, "width", "%d", width);
    SetFormattedProperty(material, "height", "%d", height);
    material->SetProperty("textureName", textureName.c_str());
    material->SetProperty("enableAlphaBlend", blend ? "true" : "false");
    SetFormattedProperty(material, "backgroundColor", "(%g,%g,%g,%g)", double(colour[1]), double(colour[2]),
                         double(colour[3]), double(colour[0]));
    {
        const uint32_t index =
            static_cast<cIMaterialManager *>(GetCachedQueriedObject())->AddMaterial(material, kMaterialFlags);
        const ShapeMaterialHandle handle{static_cast<int32_t>(index)};

        if (!TextureManager()->Has(textureName.c_str())) {
            cTextureTextUpdateStrategy *const strategy = new cTextureTextUpdateStrategy(height, fontSize);
            strategy->AddRef();
            cIProceduralTexture *texture = nullptr;
            Create(kProceduralTextureClass, cIProceduralTexture::kIid, &texture);
            texture->CreateClosest(strategy, width, height, kTextureLevels, kMinColourBits, kMinAlphaBits, 1.0f);
            {
                cTextureHandle id(TextureManager()->Add(texture, textureName.c_str(), kTextureFlags));
                material->AddTextureById(id.mId);
            }
            if (texture) texture->Release();
            strategy->Release();
        }

        cIShapeRefNode *holder = nullptr;
        Create(kShapeRefNodeClass, cIShapeRefNode::kIid, &holder);
        holder->SetTranslation(translation);
        SetNodeTag(holder, tag.c_str());
        cIShape *shape = nullptr;
        Create(cShape::kClassId, cIShape::kIid, &shape);
        holder->AddShape(shape);
        cIGeometryNode *mesh;
        MakeBillboardQuadMesh(&mesh, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f);
        shape->AddPart(reinterpret_cast<cIGZUnknown *>(mesh), 0);
        shape->BindMaterial(materialName.c_str(), kSubset);
        node->AddChild(holder);
        if (mesh) reinterpret_cast<cIGZUnknown *>(mesh)->Release();
        if (shape) shape->Release();
        if (holder) holder->Release();
    }
    if (material) material->Release();
}

} // namespace nGZSceneGraph
