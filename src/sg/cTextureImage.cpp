// nGZSceneGraph::cTextureImage (lane SJ): see cTextureImage.h.
#include "sg/cTextureImage.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZPersistResourceManager.h"
#include "gz/cImageManipDX.h"
#include "gz/cMemBlock.h"
#include "gz/cTextureDX.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "sg/TextureDescription.h"
#include "sg/cSGResource.h"

namespace nGZSceneGraph {

namespace {

using nGZGraphic4::cIImageManip;
using nGZGraphic4::cITextureBase;
using nRZGraphic4::cMemBlockAsTexture;

constexpr uint32_t kTextureClass = 0xbb892198;     // the director's device texture classes (kind 4, kind 6)
constexpr uint32_t kCubeTextureClass = 0xe944343d;
constexpr uint32_t kResourceService = 0x056b906e;  // the framework's resource service
constexpr uint32_t kResourceIid = 0x656b8efc;
constexpr uint32_t kImageIid = 0xfc49f6ee;         // what the resource service is asked for (a cImageData)
constexpr uint32_t kEngineGroup = 0x1c0532fa;      // the group of the game's own resources
constexpr uint32_t kManagedPool = 2;               // Create's pool choice
constexpr uint32_t kKindNormalMap = 6, kKindNormalMapOcclusion = 7; // image kinds that upload as a normal map

// The image resource (nGZSceneGraph::cImageData through its 0xfc49f6ee face) as this class calls it. The slots act on
// the image's current entry (slot 7 selects it; a cube map has one per face), and per level of that entry.
struct ImageView {
    void *image;

    cISGResource *Resource() const { return VirtualCall<cISGResource *(__thiscall *)(void *), 5>(image); } // 5
    void SelectEntry(uint32_t entry) const { VirtualCall<void(__thiscall *)(void *, uint32_t), 7>(image, entry); } // 7
    void *Bits(uint32_t level) const { return VirtualCall<void *(__thiscall *)(void *, uint32_t), 9>(image, level); } // 9
    uint32_t EntryCount() const { return VirtualCall<uint32_t(__thiscall *)(void *), 25>(image); } // 25
    uint32_t Width(uint32_t level) const { return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 26>(image, level); }
    uint32_t Height(uint32_t level) const { return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 27>(image, level); }
    uint32_t Pitch(uint32_t level) const { return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 28>(image, level); }
    uint32_t LevelCount() const { return VirtualCall<uint32_t(__thiscall *)(void *), 31>(image); } // 31
    uint32_t Kind() const { return VirtualCall<uint32_t(__thiscall *)(void *), 32>(image); }       // 32
    void *Format() const { return VirtualCall<void *(__thiscall *)(void *), 33>(image); }          // 33 (cIPixelFormat)
    float Quality() const { return VirtualCall<float(__thiscall *)(void *), 34>(image); }          // 34
    float Amplitude() const { return VirtualCall<float(__thiscall *)(void *), 36>(image); }        // 36: the normal map's

    bool IsNormalMap() const { return Kind() == kKindNormalMap || Kind() == kKindNormalMapOcclusion; }
};

// The resource service, counted in `holder` (left null when there is no framework).
void GetResourceService(RZAutoRef &holder)
{
    holder.mObject = nullptr;
    if (auto *const framework = static_cast<cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kResourceService, kResourceIid, &holder.mObject);
}

cIGZPersistResourceManager *Service(const RZAutoRef &holder)
{
    return static_cast<cIGZPersistResourceManager *>(holder.mObject);
}

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }


// The current entry's levels into `block`: the top level, then levels 1 to the count. Each level's arguments are read
// pitch first, as the original evaluates them.
void FillBlock(cMemBlockAsTexture &block, const ImageView &image)
{
    {
        const uint32_t pitch = image.Pitch(0);
        const uint32_t height = image.Height(0);
        const uint32_t width = image.Width(0);
        void *const format = image.Format();
        void *const bits = image.Bits(0);
        block.SetTopLevel(bits, format, width, height, pitch);
    }
    const uint32_t levels = image.LevelCount();
    for (uint32_t level = 1; level < levels; ++level) {
        const uint32_t pitch = image.Pitch(level);
        const uint32_t height = image.Height(level);
        const uint32_t width = image.Width(level);
        void *const bits = image.Bits(level);
        block.AddLevel(bits, width, height, pitch);
    }
}

} // namespace

cTextureImage *__cdecl cTextureImage::NewTexture() { return new cTextureImage(kKindTexture); }

cTextureImage *__cdecl cTextureImage::NewCube() { return new cTextureImage(kKindCube); }

cTextureImage::cTextureImage(uint32_t kind) : mKind(kind)
{
    // With a device, the director's COM makes the device texture of the kind (its result unchecked).
    if (!nGZGraphic4::GetCurrentDeviceCached()) return;
    uint32_t classId;
    if (kind == kKindTexture) classId = kTextureClass;
    else if (kind == kKindCube) classId = kCubeTextureClass;
    else return;
    auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    com->GetClassObject(classId, nGZGraphic4::kIidTextureBase, reinterpret_cast<void **>(&mTexture));
}

cTextureImage::~cTextureImage()
{
    // ORIGINAL-BUG: with a device, the device texture's slot 3 is called without a null check (a texture made with a
    // device always has one, unless the director's COM made none).
    if (nGZGraphic4::GetCurrentDeviceCached()) mTexture->ReleaseResource();
    if (mTexture) mTexture->Release();
    if (mImage) mImage->Release();
}

bool cTextureImage::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIid && riid != kIidTexture) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTextureImage::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

void cTextureImage::Restore()
{
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (!device || !nGZGraphic4::GetCurrentDeviceCached()->IsDeviceReady()) return;

    // Not created yet: the device texture of the image's size and format, and the image's levels into it.
    if (!mTexture->IsCreated()) {
        if (cIGZUnknown *const image = GetImage()) {
            mTexture->ReleaseResource();
            const ImageView view{image};
            const bool normalMap = view.IsNormalMap();
            void *const format = view.Format();
            const uint32_t height = view.Height(0);
            const uint32_t width = view.Width(0);
            TextureDescription desc;
            if (ChooseTextureDescription(&desc, int32_t(width), int32_t(height), mLevels, static_cast<nGZGraphic4::cIPixelFormat *>(format),
                                         normalMap, mKind)) {
                CreateDeviceTexture(mTexture, reinterpret_cast<const uint32_t *>(&desc), view.Quality(), mKind);
                if (mTexture->IsCreated()) Upload();
            }
        }
    }

    // An image that is a keyed resource is let go: only its key is kept (GetImage loads it again), and the resource
    // service forgets it.
    if (!mImage) return;
    if (ImageView{mImage}.Resource()->GetFlag()) return;
    if (!ImageView{mImage}.Resource()->HasKey()) return;
    const cGZPersistResourceKey key = ImageView{mImage}.Resource()->GetPersistResource()->GetKey();
    mKey[0] = key.type;
    mKey[1] = key.group;
    mKey[2] = key.instance;
    mKey[3] = key.resource;
    if (cIGZUnknown *const old = mImage) {
        mImage = nullptr;
        old->Release();
    }
    RZAutoRef service;
    GetResourceService(service);
    // With no resource service the original calls through null here, as this does.
    Service(service)->vf10(Word(mKey));
    service.ReleaseObject();
}

void cTextureImage::ReleaseResource()
{
    if (!nGZGraphic4::GetCurrentDeviceCached()) return;
    if (!nGZGraphic4::GetCurrentDeviceCached()->IsDeviceReady()) return;
    mTexture->ReleaseResource();
}

// ORIGINAL-BUG: no null check (a texture made without a device has no device texture).
bool cTextureImage::IsCreated() { return mTexture->IsCreated(); }

void cTextureImage::AssignImage(cIGZUnknown *image)
{
    cIGZUnknown *const old = mImage;
    if (image == old) return;
    if (image) image->AddRef();
    mImage = image;
    if (old) old->Release();
}

void cTextureImage::Load(cIGZUnknown *image, uint32_t levels)
{
    mLevels = levels;
    AssignImage(image);
    for (uint32_t &word : mKey) word = 0;
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (!device || !nGZGraphic4::GetCurrentDeviceCached()->IsDeviceReady() || !mTexture) return;
    mTexture->ReleaseResource();
    Restore();
}

void cTextureImage::SetImage(cIGZUnknown *image)
{
    AssignImage(image);
    for (uint32_t &word : mKey) word = 0;
    // ORIGINAL-BUG: the device texture is asked without a null check.
    if (mTexture->IsCreated()) Upload();
}

cIGZUnknown *cTextureImage::GetImage()
{
    if (mImage || !mKey[0]) return mImage;
    // The image from its key: in the key's group, then (when that is not it already) in the game's.
    bool loaded;
    {
        RZAutoRef service;
        GetResourceService(service);
        if (cIGZUnknown *const old = mImage) {
            mImage = nullptr;
            old->Release();
        }
        // With no resource service the original calls through null here, as this does.
        loaded = static_cast<uint8_t>(
                     Service(service)->vf3(Word(mKey), kImageIid, Word(&mImage), 0, 0)) != 0;
        service.ReleaseObject();
    }
    if (!loaded && mKey[1] != kEngineGroup) {
        const uint32_t key[4] = {mKey[0], kEngineGroup, mKey[2], mKey[3]};
        RZAutoRef service;
        GetResourceService(service);
        if (cIGZUnknown *const old = mImage) {
            mImage = nullptr;
            old->Release();
        }
        Service(service)->vf3(Word(key), kImageIid, Word(&mImage), 0, 0);
        service.ReleaseObject();
    }
    return mImage;
}

void cTextureImage::Upload()
{
    const ImageView image{GetImage()};
    const bool normalMap = image.IsNormalMap();
    if (mKind == kKindTexture) {
        image.SelectEntry(0);
        cMemBlockAsTexture block;
        block.AddRef();
        FillBlock(block, image);
        nGZGraphic4::cITexture *target = nullptr;
        mTexture->QueryInterface(nGZGraphic4::kIidTexture, reinterpret_cast<void **>(&target));
        auto *const manip = static_cast<cIImageManip *>(nGZGraphic4::GetImageManipCached());
        if (normalMap) manip->ComputeNormalMap(&block, target, image.Amplitude(), false);
        else manip->CopyTexture(&block, target);
        if (target) target->Release();
    } else if (mKind == kKindCube) {
        cMemBlockAsTexture faces[6];
        for (uint32_t face = 0; face < 6; ++face) {
            // Faces past the image's entries repeat its last entry (an image with none uses the face's own index).
            const uint32_t last = image.EntryCount() - 1;
            image.SelectEntry(last < face ? last : face);
            faces[face].AddRef();
            FillBlock(faces[face], image);
        }
        nGZGraphic4::cICubeTexture *target = nullptr;
        mTexture->QueryInterface(nGZGraphic4::kIidCubeTexture, reinterpret_cast<void **>(&target));
        auto *const manip = static_cast<cIImageManip *>(nGZGraphic4::GetImageManipCached());
        manip->CopyCubeFaces(&faces[0], &faces[1], &faces[2], &faces[3], &faces[4], &faces[5], target);
        if (target) target->Release();
    }
}

void __cdecl cTextureImage::CreateDeviceTexture(cITextureBase *texture, const uint32_t *desc, float quality,
                                                uint32_t kind)
{
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (kind == kKindTexture) {
        nGZGraphic4::cITexture *target = nullptr;
        texture->QueryInterface(nGZGraphic4::kIidTexture, reinterpret_cast<void **>(&target));
        // The face is used unchecked, as in the original.
        target->Create(device, desc[0], desc[1], reinterpret_cast<void *>(uintptr_t(desc[3])), desc[2], quality,
                       kManagedPool);
        if (target) target->Release();
    } else if (kind == kKindCube) {
        nGZGraphic4::cICubeTexture *target = nullptr;
        texture->QueryInterface(nGZGraphic4::kIidCubeTexture, reinterpret_cast<void **>(&target));
        target->Create(device, desc[0], reinterpret_cast<void *>(uintptr_t(desc[3])), desc[2], quality, kManagedPool);
        if (target) target->Release();
    }
}

} // namespace nGZSceneGraph
