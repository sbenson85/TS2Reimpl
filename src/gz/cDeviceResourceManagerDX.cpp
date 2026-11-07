// nGZGraphic4::cDeviceResourceManagerDX and its iterator (lane NT, rendering R1c): see the header.
#include "gz/cDeviceResourceManagerDX.h"

#include <d3d9.h>

#include <algorithm>
#include <new>

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceDX.h"
#include "gz/cGZAny.h"
#include "gz/cIGZPropertySet.h"
#include "gz/cTextureDX.h"
#include "rz/MiscHelpersA.h" // LogD3DError
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"

namespace nGZGraphic4 {

namespace {

constexpr uint32_t kLineEvictInManage = 0x1ec; // LogD3DError's line numbers
constexpr uint32_t kLineEvictInRefresh = 0x27c;
constexpr uint32_t kPoolManaged = 1;           // D3DPOOL_MANAGED: the pool ManageTextureMemory handles
constexpr float kPercent = 100.0f;             // 0xd9fac4

uint32_t SourceFile()
{
    return static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ts2_rva_d9fa60));
}

uint32_t Magnitude(int32_t value) { return value < 0 ? 0u - static_cast<uint32_t>(value) : static_cast<uint32_t>(value); }

// A counted reference released on the way out (the original's holders).
template <class T> struct Held {
    T *mObject = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held()
    {
        if (mObject) mObject->Release();
    }
    void **Out() { return reinterpret_cast<void **>(&mObject); }
};

} // namespace

// ---- cDeviceResourceIterator ----

cDeviceResourceIterator::cDeviceResourceIterator(cDeviceResourceSet::const_iterator position)
    : mRefCount(0), mPosition(position)
{
}

cDeviceResourceIterator::~cDeviceResourceIterator() = default;

void *cDeviceResourceIterator::operator new(size_t size)
{
    if (size != kSize) return ::operator new(size);
    return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc();
}

void cDeviceResourceIterator::operator delete(void *block, size_t size)
{
    if (size != kSize) {
        ::operator delete(block);
        return;
    }
    reinterpret_cast<RZFastFixed0xc *>(&Pool())->Free(block);
}

bool cDeviceResourceIterator::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (!ppvObj) return false;
    if (riid != 1) return false;
    *ppvObj = this;
    AddRef();
    return true;
}

uint32_t cDeviceResourceIterator::AddRef() { return ++mRefCount; }

uint32_t cDeviceResourceIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIDeviceResourceIterator *cDeviceResourceIterator::Clone() { return new cDeviceResourceIterator(mPosition); }

bool cDeviceResourceIterator::IsEqual(const cIDeviceResourceIterator *other)
{
    return mPosition == static_cast<const cDeviceResourceIterator *>(other)->mPosition;
}

cIDeviceResource *cDeviceResourceIterator::Get() { return *mPosition; }

void cDeviceResourceIterator::Next() { ++mPosition; }

void cDeviceResourceIterator::Prev() { --mPosition; }

// ---- cDeviceResourceManagerDX: construction and the unknown ----

cDeviceResourceManagerDX::cDeviceResourceManagerDX(cIDeviceDX *device)
    : mRefCount(0), mDevice(device), mMemoryDelta(0), mPending(0), mEnabled(1), mQuality(kDefaultQuality),
      mLimit(kDefaultLimit), mReserved(0), mAvailable(0)
{
}

cDeviceResourceManagerDX::~cDeviceResourceManagerDX() = default; // the set, then the vector (member order, reversed)

cDeviceResourceManagerDX *__fastcall cDeviceResourceManagerAdapters::Construct(void *memory, void *, cIDeviceDX *device)
{
    return ::new (memory) cDeviceResourceManagerDX(device);
}

bool cDeviceResourceManagerDX::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (!ppvObj) return false;
    if (riid != 1 && riid != kIidDeviceResourceManager) return false;
    *ppvObj = this;
    AddRef();
    return true;
}

uint32_t cDeviceResourceManagerDX::AddRef() { return ++mRefCount; }

uint32_t cDeviceResourceManagerDX::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIDeviceDX *cDeviceResourceManagerDX::GetDevice() { return mDevice; }

cIDeviceDX *cDeviceResourceManagerDX::GetDeviceConst() { return mDevice; }

// ---- the register ----

void cDeviceResourceManagerDX::Insert(cIDeviceResource *resource) { mResources.insert(resource); }

void cDeviceResourceManagerDX::Add(cIAllocDeviceResource *resource)
{
    mResources.insert(resource);
    if (mMemoryDelta != kUntracked) mMemoryDelta += resource->GetVideoMemorySize();
    if (resource->GetPool() == 0) mAvailable = 0;
}

uint32_t cDeviceResourceManagerDX::Erase(cIDeviceResource *resource)
{
    return static_cast<uint32_t>(mResources.erase(resource));
}

void cDeviceResourceManagerDX::Remove(cIAllocDeviceResource *resource)
{
    mResources.erase(resource);
    mDevice->WaitForGpu();
    if (mDevice->IsDeviceReady()) {
        if (mMemoryDelta != kUntracked) mMemoryDelta -= resource->GetVideoMemorySize();
        if (resource->GetPool() != 0) return;
    }
    mAvailable = 0;
}

cDeviceResourceRange cDeviceResourceManagerDX::GetResources()
{
    // The end first, then the beginning (the pool hands out blocks in that order), then a clone of the beginning as the
    // cursor; each is counted once for the range.
    cDeviceResourceIterator *const end = new cDeviceResourceIterator(mResources.end());
    cDeviceResourceIterator *const begin = new cDeviceResourceIterator(mResources.begin());
    cDeviceResourceRange range;
    range.mCurrent = begin->Clone();
    range.mCurrent->AddRef();
    range.mBegin = begin;
    begin->AddRef();
    range.mEnd = end;
    end->AddRef();
    return range;
}

// ---- the budget ----

void cDeviceResourceManagerDX::SetPolicy(bool enabled, float quality, uint32_t limit)
{
    mEnabled = enabled;
    mLimit = limit;
    mQuality = quality;
    ResetBudget();
}

void cDeviceResourceManagerDX::ResetBudget()
{
    mMemoryDelta = kUntracked;
    mAvailable = 0;
}

void cDeviceResourceManagerDX::AddPending(int32_t bytes)
{
    mPending = static_cast<int32_t>(static_cast<uint32_t>(mPending) + Magnitude(bytes));
}

cResourcePolicy cDeviceResourceManagerDX::GetPolicy()
{
    cResourcePolicy policy; // the padding after the byte is left as the caller had it
    policy.mEnabled = mEnabled;
    policy.mQuality = mQuality;
    policy.mLimit = mLimit;
    return policy;
}

float cDeviceResourceManagerDX::GetQuality() { return mQuality; }

void cDeviceResourceManagerDX::RefreshAvailableMemory()
{
    if (!mDevice->IsDeviceReady()) {
        mAvailable = 0;
        return;
    }
    mDevice->WaitForGpu();
    const HRESULT hr = mDevice->GetD3DDevice()->EvictManagedResources();
    if (hr < 0) LogD3DError(static_cast<uint32_t>(hr), SourceFile(), kLineEvictInRefresh);
    mAvailable = mDevice->GetD3DDevice()->GetAvailableTextureMem();
}

uint32_t cDeviceResourceManagerDX::GetFreeMemory()
{
    if (!mDevice->IsDeviceReady()) return 0;
    const uint32_t available = mDevice->GetD3DDevice()->GetAvailableTextureMem();
    return mReserved < available ? available - mReserved : 0;
}

// 0x17b080. The managed textures' levels are lowered, the texture that takes the most (its bytes over its quality)
// first, until what they take fits the budget (the available memory less the reserved, times the quality percentage):
// a texture keeps at least the levels whose size (times its factor, when modulating) is not under the enumeration's
// minimum dimension.
void cDeviceResourceManagerDX::ManageTextureMemory()
{
    if (Magnitude(mMemoryDelta) <= mLimit && Magnitude(mPending) <= mLimit) return;

    // Every managed texture, back at its full size, with the bytes it takes now.
    uint32_t total = 0;
    for (auto it = mResources.begin(); it != mResources.end(); ++it) {
        Held<cITextureBaseDX> texture;
        if ((*it)->QueryInterface(kIidTextureBaseDX, texture.Out()) && texture.mObject->GetPool() == kPoolManaged) {
            texture.mObject->GetD3DTexture()->SetLOD(0);
            const uint32_t size = texture.mObject->GetVideoMemorySize();
            total += size;
            const float factor = 1.0f / texture.mObject->GetQuality();
            mEntries.push_back({texture.mObject, 0, static_cast<int32_t>(size), factor});
        }
    }

    // The budget; the available memory is read again (after evicting what the driver keeps) when it is not known.
    IDirect3DDevice9 *const d3d = mDevice->GetD3DDevice();
    if (mAvailable == 0) {
        mDevice->WaitForGpu();
        const HRESULT hr = d3d->EvictManagedResources();
        if (hr < 0) LogD3DError(static_cast<uint32_t>(hr), SourceFile(), kLineEvictInManage);
        mAvailable = d3d->GetAvailableTextureMem();
    }
    const float scaled = static_cast<float>(static_cast<double>(static_cast<uint32_t>(mAvailable - mReserved))) *
                         (mQuality / kPercent);
    const uint32_t budget = static_cast<uint32_t>(static_cast<int64_t>(scaled));

    Held<cIDeviceEnum> devices;
    mDevice->GetDeviceEnumConst()->QueryInterface(kIidDeviceEnum, devices.Out());
    const uint32_t minimumDimension = devices.mObject->GetSetting(0);

    if (mEnabled) {
        cIGZPropertySetString *const properties = As<cIDeviceEnum>(mDevice->GetDeviceEnumConst())->GetProperties();
        gz::AnyBoolean modulate(1);
        const GZServiceString name("modulateClampedTextureDim");
        properties->GetPropertyValue(&name, &modulate);

        // The texture whose bytes weigh the most is lowered first: a heap on bytes times factor.
        const auto weighs = [](const cBudgetEntry &a, const cBudgetEntry &b) {
            return static_cast<float>(a.mSize) * a.mFactor < static_cast<float>(b.mSize) * b.mFactor;
        };
        std::make_heap(mEntries.begin(), mEntries.end(), weighs);
        const float minimum = static_cast<float>(static_cast<double>(minimumDimension));
        while (total > budget && !mEntries.empty()) {
            if (mEntries.size() >= 2) std::pop_heap(mEntries.begin(), mEntries.end(), weighs);
            cBudgetEntry &top = mEntries.back();
            const uint32_t next = top.mLevel + 1;
            const float scale = modulate.mValue ? top.mFactor : 1.0f;
            if (next < top.mTexture->GetLevelCount() &&
                // (a size that is not a number, from a quality of zero or less, is not big enough either: the branch
                // taken when "below" is taken for unordered operands too)
                static_cast<float>(static_cast<int32_t>(top.mTexture->GetWidth(next))) * scale >= minimum &&
                static_cast<float>(static_cast<int32_t>(top.mTexture->GetHeight(next))) * scale >= minimum) {
                // One level less: its bytes come off, and the texture takes its place in the heap again.
                const uint32_t bytes = top.mTexture->GetLevelBytes(top.mLevel);
                total = total >= bytes ? total - bytes : 0;
                top.mSize -= static_cast<int32_t>(bytes);
                top.mLevel = next;
                if (mEntries.size() >= 2) std::push_heap(mEntries.begin(), mEntries.end(), weighs);
            } else {
                // As small as it may go: its level is set and it leaves the list.
                // (the texture is asked for again to set it)
                if (top.mTexture->GetD3DTexture()->GetLOD() != top.mLevel) top.mTexture->GetD3DTexture()->SetLOD(top.mLevel);
                mEntries.pop_back();
            }
        }
    }

    // Every texture still listed keeps its chosen level.
    for (const cBudgetEntry &entry : mEntries) {
        IDirect3DBaseTexture9 *const base = entry.mTexture->GetD3DTexture();
        if (base->GetLOD() != entry.mLevel) base->SetLOD(entry.mLevel);
    }
    mEntries.clear();
    mMemoryDelta = 0;
    mPending = 0;
}

// ---- for the device ----

void cDeviceResourceManagerDX::ReleaseAll()
{
    while (!mResources.empty()) (*mResources.begin())->ReleaseResource();
}

void cDeviceResourceManagerDX::ReleaseNonSurvivors()
{
    ResetBudget();
    for (auto it = mResources.begin(); it != mResources.end();) {
        cIDeviceResource *const resource = *it;
        ++it; // before the release: it takes the resource out of the set
        if (!resource->SurvivesReset()) resource->ReleaseResource();
    }
}

void cDeviceResourceManagerDX::ReleaseOnModeChange()
{
    for (auto it = mResources.begin(); it != mResources.end();) {
        cIDeviceResource *const resource = *it;
        ++it;
        if (!resource->SurvivesModeChange()) resource->ReleaseResource();
    }
}

void cDeviceResourceManagerDX::TakeDevice(IDirect3DDevice9 *device)
{
    mAvailable = device->GetAvailableTextureMem();
    ResetBudget();
    mReserved = 0;
}

} // namespace nGZGraphic4
