// nGZSceneGraph::cProceduralTexture (lane TH): see cProceduralTexture.h.
#include "sg/cProceduralTexture.h"

#include "game/vtable.h"
#include "gz/GraphicModeSearch.h" // FindClosestColorFormat
#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h" // cPixelFormatRange
#include "gz/cIGZCOM.h"
#include "gz/cImageManipDX.h"
#include "gz/cTextureDX.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "sg/TextureDescription.h"
#include "sg/cTextureUpdateStrategy.h"

namespace nGZSceneGraph {

namespace {

using nGZGraphic4::cIPixelFormat;
using nGZGraphic4::cPixelFormatRange;

constexpr uint32_t kTextureClass = 0xbb892198; // the director's device texture classes (kind 4, kind 6)
constexpr uint32_t kCubeTextureClass = 0xe944343d;
constexpr uint32_t kArgbIid = 0x9ac38991; // the formats manager's iterator filter: the ARGB formats
constexpr uint32_t kCubeFaces = 6;

// The closest ARGB format of at least the bits given: the formats manager's iterator (slot 3, CreateIterator) handed to
// FindClosestColorFormat, which releases it.
cIPixelFormat *ClosestArgbFormat(int32_t minBits, int32_t minAlphaBits)
{
    void *const formats = nGZGraphic4::GetPixelFormatsManagerCached();
    cPixelFormatRange range;
    VirtualCall<cPixelFormatRange *(__thiscall *)(void *, cPixelFormatRange *, uint32_t), 3>(formats, &range, kArgbIid);
    return nGZGraphic4::FindClosestColorFormat(range, minBits, minAlphaBits);
}

nGZGraphic4::cILockableSurface *Lockable(nGZGraphic4::cIImageSurface *surface)
{
    return reinterpret_cast<nGZGraphic4::cILockableSurface *>(surface);
}

} // namespace

cProceduralTexture *__cdecl cProceduralTexture::New() { return new cProceduralTexture(); }

cProceduralTexture::~cProceduralTexture()
{
    if (mTexture) mTexture->ReleaseResource();
    if (mTexture) mTexture->Release();
    if (mStrategy) mStrategy->Release();
}

bool cProceduralTexture::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIidTexture && riid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cProceduralTexture::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

void cProceduralTexture::Restore()
{
    // ORIGINAL-BUG: the device is asked without a null check, and so is the device texture.
    if (!nGZGraphic4::GetCurrentDeviceCached()->IsDeviceReady()) return;
    if (mTexture->IsCreated()) return;
    mStrategy->GetDescription(&mWidth, &mHeight, &mLevels, &mQuality); // its result unused
    TextureDescription desc;
    if (ChooseTextureDescription(&desc, int32_t(mWidth), int32_t(mHeight), mLevels, mFormat, false, mKind)) {
        cTextureImage::CreateDeviceTexture(mTexture, reinterpret_cast<const uint32_t *>(&desc), mQuality, mKind);
        if (mTexture->IsCreated()) DrawLevels(true);
    }
    mStrategy->Finish();
}

// ORIGINAL-BUG: no null check (a texture never set up has no device texture).
void cProceduralTexture::ReleaseResource() { mTexture->ReleaseResource(); }

// ORIGINAL-BUG: no null check, as ReleaseResource.
bool cProceduralTexture::IsCreated() { return mTexture->IsCreated(); }

void cProceduralTexture::SetStrategy(nRZSceneGraph::cITextureUpdateStrategy *strategy)
{
    nRZSceneGraph::cITextureUpdateStrategy *const old = mStrategy;
    if (strategy == old) return;
    if (strategy) strategy->AddRef();
    mStrategy = strategy;
    if (old) old->Release();
}

// The old device texture let go and a new one of `classId` made by the director's COM (its result unchecked). The COM
// is asked for before the old texture goes, as in the original.
void cProceduralTexture::NewDeviceTexture(uint32_t classId)
{
    auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (nGZGraphic4::cITextureBase *const old = mTexture) {
        mTexture = nullptr;
        old->Release();
    }
    com->GetClassObject(classId, nGZGraphic4::kIidTextureBase, reinterpret_cast<void **>(&mTexture));
}

void cProceduralTexture::Create(nRZSceneGraph::cITextureUpdateStrategy *strategy, uint32_t width, uint32_t height,
                                uint32_t levels, cIPixelFormat *format, float quality)
{
    SetStrategy(strategy);
    mWidth = width;
    mHeight = height;
    mLevels = levels;
    mKind = kKindTexture;
    mFormat = format;
    mQuality = quality;
    NewDeviceTexture(kTextureClass);
}

void cProceduralTexture::CreateClosest(nRZSceneGraph::cITextureUpdateStrategy *strategy, uint32_t width,
                                       uint32_t height, uint32_t levels, int32_t minBits, int32_t minAlphaBits,
                                       float quality)
{
    cIPixelFormat *const format = ClosestArgbFormat(minBits, minAlphaBits);
    Create(strategy, width, height, levels, format, quality); // through the vtable in the original: slot 9 is final
}

void cProceduralTexture::CreateCube(nRZSceneGraph::cITextureUpdateStrategy *strategy, uint32_t edge, uint32_t levels,
                                    int32_t minBits, int32_t minAlphaBits, float quality)
{
    SetStrategy(strategy);
    mWidth = edge;
    mHeight = edge;
    mKind = kKindCube;
    mLevels = levels;
    mFormat = ClosestArgbFormat(minBits, minAlphaBits);
    mQuality = quality;
    NewDeviceTexture(kCubeTextureClass);
}

void cProceduralTexture::Redraw()
{
    // ORIGINAL-BUG: the device texture is asked without a null check.
    if (mTexture->IsCreated()) DrawLevels(false);
}

void cProceduralTexture::DrawLevels(bool first)
{
    // ORIGINAL-BUG: the device texture's 2D / cube face is used without a null check.
    if (mKind == kKindTexture) {
        nGZGraphic4::cITexture *target = nullptr;
        mTexture->QueryInterface(nGZGraphic4::kIidTexture, reinterpret_cast<void **>(&target));
        mStrategy->Update(Lockable(target->GetTargetLockableSurface(0)), 0, first);
        if (target) target->Release();
    } else if (mKind == kKindCube) {
        nGZGraphic4::cICubeTexture *target = nullptr;
        mTexture->QueryInterface(nGZGraphic4::kIidCubeTexture, reinterpret_cast<void **>(&target));
        for (uint32_t face = 0; face < kCubeFaces; ++face)
            mStrategy->Update(Lockable(target->GetTargetLockableSurface(face, 0)), face, first);
        if (target) target->Release();
    }
    if (mTexture->GetLevelCount() != 1) {
        auto *const manip = static_cast<nGZGraphic4::cIImageManip *>(nGZGraphic4::GetImageManipCached());
        manip->GenerateMipmaps(mTexture);
    }
}

} // namespace nGZSceneGraph
