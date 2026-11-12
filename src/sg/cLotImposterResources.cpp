// The lot imposter's resource helpers (lane 261108j): GetLotGroup 0x401790 and LoadImposterResources 0xb40560. Declared in
// src/sg/cLotImposter.h; their own TU so tests can stand in for them under Load.
#include "sg/cLotImposter.h"

#include "game/vtable.h"
#include "misc/UtilitiesE.h"      // GetCachedQueriedObject
#include "rz/ServiceGetters.h"    // CachedServiceGetter
#include "rz/Singleton_20ddfb.h"  // GetCachedSingleton
#include "rz/StaticGetters.h"     // GetDirectorFrameworkObject
#include "rz/StaticGetters2.h"    // GetCachedSlot23
#include "sg/cTextureManager.h"   // TextureManager, ResolveResourceName
#include "sg/cLotImposterDesignModes.h" // cMapString (nTSWorld::cMapString)

namespace {

constexpr uint32_t kMaterialType = 0x49596978;     // a material definition
constexpr uint32_t kMaterialIid = 0x49596972;
constexpr uint32_t kTextureType = 0x1c4a276c;      // a texture image
constexpr uint32_t kTextureIid = 0xfc49f6ee;
constexpr uint32_t kSceneType = 0xe519c933;        // a scene graph ("_cres")
constexpr uint32_t kResourceNodeIid = 0xe519c92c;
constexpr uint32_t kGlobalGroup = 0x1c0532fa;      // where a material or texture missing from its group is looked for
constexpr uint32_t kTextureIfaceIid = 0x2a956da8;  // the texture object's face that takes an image (slot 9)
constexpr uint32_t kTextureClass = 0x2a956dad;     // the texture class made for an image
constexpr uint32_t kCubeTextureClass = 0x4a956dd2; // ... for an image of kind 6

void *PersistService() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }

template <class T> void ReleaseAndNull(T *&p)
{
    if (T *old = p) {
        p = nullptr;
        old->Release();
    }
}

// The resource database's slot 3: the resource of `key` as `iid` into `*out`.
bool GetResource(void *database, const void *key, uint32_t iid, void **out)
{
    // cTSPersistSystem 0x1e0890 or cGZPersistResourceManager 0x9b4d70, both ret 20
    return RawSlot<3, bool>(database, key, iid, out, 0u, 0u); // receiver: cIGZPersistResourceManager
}

// The resource of `key` as `iid`; when it is not there, the same key in the global group (unless that is its group).
// `key` keeps the group it was found in.
bool GetResourceOrGlobal(void *database, nTSSG::ImposterKey *key, uint32_t iid, cIGZUnknown **out)
{
    if (GetResource(database, key, iid, reinterpret_cast<void **>(out))) return true;
    if (key->mGroup == kGlobalGroup) return false;
    key->mGroup = kGlobalGroup;
    ReleaseAndNull(*out);
    return GetResource(database, key, iid, reinterpret_cast<void **>(out));
}

} // namespace

namespace nTSSG {

bool __cdecl GetLotGroup(uint32_t lot, uint32_t *group)
{
    void *lotObject = nullptr;
    void *nhood = RawSlot<6, void *>(GetCachedSingleton()); // receiver: cITSGlobals (0x1457d0, ret 0)
    if (nhood)
        lotObject = RawSlot<57, void *>(nhood, lot); // receiver: cTSNeighborhood (0x475000, ret 4)
    else
        lotObject = RawSlot<9, void *>(GetCachedSlot23()); // receiver: cTSGameStateController (0x148ba0, ret 0)
    if (!lotObject) return false;
    void *name = RawSlot<8, void *>(lotObject); // receiver: cTSLot (0x158f90, ret 0)
    // cIGZString: slot 7 is 0x149d00 in every implementation; slot 6 is 0x1acd4f, or 0x149cf0 in cRZStringResource's
    // face at +32; all three ret 0
    const uint32_t length = RawSlot<7>(name); // receiver: cIGZString
    const char *data = RawSlot<6, const char *>(name); // receiver: cIGZString
    cMapString text;
    text.mString.assign(data, length);
    *group = RawSlot<11>(PersistService(), &text); // receiver: cITSPersistSystem (0x1dff90, ret 4)
    return true;
}

bool __cdecl LoadImposterResources(const ImposterKeyRange *keys, nGZSceneGraph::cIResourceNode **node)
{
    bool result = true;
    GetCachedQueriedObject();
    nGZSceneGraph::cITextureManager *textures = nGZSceneGraph::TextureManager();
    void *database = RawSlot<3, void *>(PersistService()); // receiver: cITSPersistSystem (0x1dae50, ret 0)
    for (uint32_t i = 0; i < static_cast<uint32_t>(keys->mEnd - keys->mBegin); ++i) {
        bool loaded = false;
        const uint32_t type = keys->mBegin[i].mType;
        if (type == kMaterialType) {
            ImposterKey key = keys->mBegin[i];
            // the definition's face at +44: slot 19 0xaae090 ret 0, slot 10 0xaaed10 ret 4
            cIGZUnknown *material = nullptr; // receiver: nGZSceneGraph::cIMaterialDefinition
            if (GetResourceOrGlobal(database, &key, kMaterialIid, &material)) {
                cMapString name;
                nGZSceneGraph::ResolveResourceName(reinterpret_cast<cIGZString *>(&name),
                                                   RawSlot<19, const char *>(material), key.mGroup, nullptr);
                RawSlot<10>(material, name.mString.c_str());
                loaded = true;
            }
            if (material) material->Release();
        } else if (type == kTextureType) {
            ImposterKey key = keys->mBegin[i];
            // cImageData, cTSTextureRecipeImageData: slot 37 0xab1870 in both, slot 25 0xab1c40 or 0x15e510; all ret 0
            cIGZUnknown *image = nullptr; // receiver: nGZSceneGraph::cIImageData
            if (GetResourceOrGlobal(database, &key, kTextureIid, &image)) {
                if (const char *name = RawSlot<37, const char *>(image)) {
                    // kTextureIfaceIid is cTextureImage's face (both class ids make one): slot 9 0xab5d20, ret 8
                    cIGZUnknown *texture = nullptr; // receiver: nGZSceneGraph::cITextureImage
                    if (!textures->Has(name)) {
                        const uint32_t clsid = RawSlot<25>(image) == 6 ? kCubeTextureClass : kTextureClass;
                        void *framework = GetDirectorFrameworkObject(); // receiver: cIGZCOM (0x145702, ret 12)
                        RawSlot<3>(framework, clsid, kTextureIfaceIid, reinterpret_cast<void **>(&texture));
                        RawSlot<9>(texture, image, 0u);
                        textures->Add(texture, name, 2);
                    } else {
                        cIGZUnknown *existing = textures->GetTexture(textures->FindOrLoad(name));
                        if (existing) existing->AddRef();
                        if (existing->QueryInterface(kTextureIfaceIid, reinterpret_cast<void **>(&texture)))
                            RawSlot<9>(texture, image, 0u);
                        if (existing) existing->Release();
                    }
                    if (texture) texture->Release();
                }
                loaded = true;
            }
            if (image) image->Release();
        } else if (type == kSceneType) {
            loaded = GetResource(database, &keys->mBegin[i], kResourceNodeIid, reinterpret_cast<void **>(node));
        } else {
            loaded = true;
        }
        result = result && loaded;
    }
    return result;
}

} // namespace nTSSG
