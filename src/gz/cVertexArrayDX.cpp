// The vertex and index arrays (lane NJ, rendering R3b item 1): src/gz/cVertexArrayDX.h.
#include "gz/cVertexArrayDX.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceDX.h"
#include "ifaces/cIDeviceEnum.h"
#include "ifaces/cIDeviceResourceManager.h"
#include "rz/MiscHelpersA.h" // LogD3DError
#include "rz/RZFastFixedAllocators.h"

namespace nGZGraphic4 {

namespace array_detail {

void Check(HRESULT hr, uint32_t line)
{
    if (FAILED(hr))
        LogD3DError(static_cast<uint32_t>(hr),
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<const char>(kArraySourceFileRva))), line);
}

ifaces::cIDeviceResourceManager *Manager(cIDeviceDX *device)
{
    return As<ifaces::cIDeviceResourceManager>(device->GetResourceManager());
}

// The device enum's vertex processing modes: 0 hardware fixed function, 1 software fixed function, 2 hardware
// shaders, 3 software shaders.
bool HasVertexProcessing(cIDeviceDX *device, uint32_t mode)
{
    return As<ifaces::cIDeviceEnum>(device->GetDeviceEnumConst())->HasVertexProcessing(mode);
}

// The failures Create answers false for without a report (out of video memory, out of memory, the driver's).
bool Quiet(HRESULT hr)
{
    const uint32_t code = static_cast<uint32_t>(hr);
    return code == 0x8876017c || code == 0x8007000e || code == 0x88760827;
}

struct UsagePool {
    uint32_t usage, pool;
};

// 0x167020: the static arrays' usage and pool for `kind`: bit 0 (managed rather than system memory) only when the device
// has hardware vertex processing, bit 1 (software processing) only when it has software processing.
// The original indexes a four-entry table with the result; kinds above 3 read past it (callers pass 0-3).
UsagePool ChooseUsagePool(cIDeviceDX *device, uint32_t kind)
{
    static constexpr UsagePool kTable[4] = {
        {0, D3DPOOL_SYSTEMMEM},
        {0, D3DPOOL_MANAGED},
        {D3DUSAGE_SOFTWAREPROCESSING, D3DPOOL_SYSTEMMEM},
        {D3DUSAGE_SOFTWAREPROCESSING, D3DPOOL_MANAGED},
    };
    const bool hardware = HasVertexProcessing(device, 0) || HasVertexProcessing(device, 2);
    uint32_t index = hardware ? kind : kind & ~1u;
    const bool software = HasVertexProcessing(device, 1) || HasVertexProcessing(device, 3);
    index = software ? index : index & ~2u;
    return kTable[index & 3];
}

void *PoolAlloc(uint32_t rva) { return GameAddress<RZFixedAllocatorBase>(rva)->Alloc(); }

} // namespace array_detail

using namespace array_detail;

// ---- cVertexArrayDX<> ----

template <class P, class T> cVertexArrayDX<P, T>::~cVertexArrayDX()
{
    if (mBuffer) {
        mBuffer->Release();
        mBuffer = nullptr;
    }
}

template <class P, class T> bool cVertexArrayDX<P, T>::Answer(uint32_t riid, void **out, uint32_t own0, uint32_t own1)
{
    if (riid == T::kIidBuffer) {
        *out = mBuffer; // before the null check, not counted
        return true;
    }
    if (!out) return false;
    if (riid == own0 || riid == own1 || riid == 1 || riid == kIidDeviceResource || riid == kIidAllocDeviceResource ||
        riid == kIidVertexArray) {
        *out = static_cast<P *>(this);
        AddRef();
        return true;
    }
    if (riid == kIidD3DResource) {
        *out = mBuffer;
        return true;
    }
    if (riid == T::kIidFace) {
        *out = static_cast<cIVertexArrayDX *>(this);
        AddRef();
        return true;
    }
    return false;
}

template <class P, class T> uint32_t cVertexArrayDX<P, T>::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

template <class P, class T> void cVertexArrayDX<P, T>::ReleaseResource()
{
    if (!this->IsCreated()) return;
    Manager(mDevice)->Remove(static_cast<P *>(this));
    if (mBuffer) {
        mBuffer->Release();
        mBuffer = nullptr;
    }
    mKind = 0;
    mLockCount = 0;
    mDevice = nullptr;
}

template <class P, class T> typename T::Desc cVertexArrayDX<P, T>::Describe(uint32_t line)
{
    typename T::Desc desc; // read whatever GetDesc answered
    Check(mBuffer->GetDesc(&desc), line);
    return desc;
}

template <class P, class T> uint32_t cVertexArrayDX<P, T>::GetMemorySize()
{
    constexpr uint32_t kObject = 0x1c; // the static arrays' size, for the dynamic ones too
    if (!mBuffer) return kObject;
    const typename T::Desc desc = Describe(T::kLineMemory);
    return (desc.Pool != D3DPOOL_DEFAULT ? desc.Size : 0) + kObject;
}

template <class P, class T> bool cVertexArrayDX<P, T>::SurvivesReset()
{
    return Describe(T::kLineReset).Pool != D3DPOOL_DEFAULT;
}

template <class P, class T> uint32_t cVertexArrayDX<P, T>::GetPool() { return Describe(T::kLinePool).Pool; }

template <class P, class T> uint32_t cVertexArrayDX<P, T>::GetVideoMemorySize()
{
    const typename T::Desc desc = Describe(T::kLineVideo);
    if (desc.Pool == D3DPOOL_SYSTEMMEM || desc.Pool == D3DPOOL_SCRATCH) return 0;
    return desc.Size;
}

template <class P, class T> void cVertexArrayDX<P, T>::Unlock()
{
    --mLockCount;
    Check(mBuffer->Unlock(), T::kLineUnlock);
}

template <class P, class T> void cVertexArrayDX<P, T>::Take(cIDeviceDX *deviceDX, uint32_t kind, Buffer *buffer)
{
    mDevice = deviceDX;
    mKind = kind;
    mLockCount = 0;
    if (buffer) buffer->AddRef();
    if (mBuffer) mBuffer->Release();
    mBuffer = buffer;
    Manager(mDevice)->Add(static_cast<P *>(this));
}

// ---- the static arrays ----

template <class T> cStaticVertexArrayDX<T>::~cStaticVertexArrayDX()
{
    if (this->mBuffer) {
        Manager(this->mDevice)->Remove(static_cast<cIStaticVertexArray *>(this));
        this->mBuffer->Release();
        this->mBuffer = nullptr;
        this->mKind = 0;
        this->mLockCount = 0;
        this->mDevice = nullptr;
    }
}

template <class T> void *cStaticVertexArrayDX<T>::Lock(uint32_t write, uint32_t offset, uint32_t size)
{
    ++this->mLockCount;
    void *bytes; // read whatever Lock answered
    Check(this->mBuffer->Lock(offset, size, &bytes, write == 0 ? D3DLOCK_NOSYSLOCK | D3DLOCK_READONLY : D3DLOCK_NOSYSLOCK),
          T::kLineLock);
    return bytes;
}

template <class T> uint32_t cStaticVertexArrayDX<T>::GetSize() { return this->Describe(T::kLineSize).Size; }

template <class T> bool cStaticVertexArrayDX<T>::SurvivesModeChange()
{
    const UsagePool wanted = ChooseUsagePool(this->mDevice, this->mKind);
    const typename T::Desc desc = this->Describe(T::kLineModeChange);
    return desc.Usage == wanted.usage && static_cast<uint32_t>(desc.Pool) == wanted.pool;
}

template <class T> bool cStaticVertexArrayDX<T>::Create(cIDeviceDX *device, uint32_t size, uint32_t kind)
{
    if (!device->IsDeviceReady()) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    const UsagePool chosen = ChooseUsagePool(device, kind);
    if (chosen.pool == D3DPOOL_DEFAULT) Manager(device)->RefreshAvailableMemory();
    typename T::Buffer *buffer = nullptr;
    const HRESULT hr = T::Make(deviceDX->GetD3DDevice(), size, chosen.usage, chosen.pool, &buffer);
    bool done = false;
    if (!Quiet(hr)) {
        Check(hr, T::kLineCreate);
        this->Take(deviceDX, kind, buffer);
        done = true;
    }
    if (buffer) buffer->Release();
    if (deviceDX) deviceDX->Release();
    return done;
}

// ---- the dynamic arrays ----

template <class T> cDynamicVertexArrayDX<T>::~cDynamicVertexArrayDX()
{
    if (this->mBuffer) {
        Manager(this->mDevice)->Remove(static_cast<cIDynamicVertexArray *>(this));
        this->mBuffer->Release();
        this->mBuffer = nullptr;
        this->mKind = 0;
        this->mLockCount = 0;
        this->mDevice = nullptr;
        mOffset = 0;
        mSpanBytes = 0;
        mCapacity = 0;
    }
}

template <class T> void cDynamicVertexArrayDX<T>::ReleaseResource()
{
    if (!this->IsCreated()) return;
    Manager(this->mDevice)->Remove(static_cast<cIDynamicVertexArray *>(this));
    if (this->mBuffer) {
        this->mBuffer->Release();
        this->mBuffer = nullptr;
    }
    this->mKind = 0;
    this->mLockCount = 0;
    this->mDevice = nullptr;
    mOffset = 0;
    mSpanBytes = 0;
    mCapacity = 0;
}

template <class T> void *cDynamicVertexArrayDX<T>::Lock(uint32_t, uint32_t offset, uint32_t size)
{
    ++this->mLockCount;
    void *bytes; // read whatever Lock answered
    Check(this->mBuffer->Lock(mOffset + offset, size != 0 ? size : mSpanBytes, &bytes,
                              D3DLOCK_NOSYSLOCK | D3DLOCK_NOOVERWRITE),
          T::kLineDynamicLock);
    return bytes;
}

template <class T> void cDynamicVertexArrayDX<T>::Discard()
{
    mOffset = 0;
    void *bytes;
    Check(this->mBuffer->Lock(0, 0, &bytes, D3DLOCK_NOSYSLOCK | D3DLOCK_DISCARD), T::kLineDiscard);
    Check(this->mBuffer->Unlock(), T::kLineDiscardUnlock);
}

template <class T> bool cDynamicVertexArrayDX<T>::Create(cIDeviceDX *device, uint32_t size, uint32_t kind)
{
    if (!device->IsDeviceReady()) return false;
    cIDeviceDX *deviceDX = nullptr;
    device->QueryInterface(cDeviceDX::kIidDeviceDX, reinterpret_cast<void **>(&deviceDX));
    // Kind 1: video memory when the device has hardware processing; kind 2: system memory with software processing
    // when it has software processing; otherwise system memory.
    constexpr uint32_t kVideo = D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY;
    constexpr uint32_t kSoftware = kVideo | D3DUSAGE_SOFTWAREPROCESSING;
    bool video;
    if (kind == 1)
        video = HasVertexProcessing(device, 0) || HasVertexProcessing(device, 2);
    else if (kind == 2)
        video = !(HasVertexProcessing(device, 1) || HasVertexProcessing(device, 3));
    else
        video = false;
    uint32_t usage, pool;
    if (video) {
        usage = kVideo;
        pool = D3DPOOL_DEFAULT;
        Manager(device)->RefreshAvailableMemory();
    } else {
        usage = kind == 1 || kind == 2 ? kSoftware : kVideo;
        pool = D3DPOOL_SYSTEMMEM;
    }
    typename T::Buffer *buffer = nullptr;
    const HRESULT hr = T::Make(deviceDX->GetD3DDevice(), size, usage, pool, &buffer);
    bool done = false;
    if (!Quiet(hr)) {
        Check(hr, T::kLineDynamicCreate);
        mCapacity = size;
        this->Take(deviceDX, kind, buffer);
        done = true;
    }
    if (buffer) buffer->Release();
    if (deviceDX) deviceDX->Release();
    return done;
}

template class cVertexArrayDX<cIStaticVertexArray, ComponentTraits>;
template class cVertexArrayDX<cIDynamicVertexArray, ComponentTraits>;
template class cVertexArrayDX<cIStaticVertexArray, IndexTraits>;
template class cVertexArrayDX<cIDynamicVertexArray, IndexTraits>;
template class cStaticVertexArrayDX<ComponentTraits>;
template class cStaticVertexArrayDX<IndexTraits>;
template class cDynamicVertexArrayDX<ComponentTraits>;
template class cDynamicVertexArrayDX<IndexTraits>;

// ---- the leaves ----

void *cStaticVertexComponentArrayDX::operator new(size_t) { return PoolAlloc(kPoolRva); }
void cStaticVertexComponentArrayDX::operator delete(void *block, size_t)
{
    GameAddress<RZFastFixed0x1c>(kPoolRva)->Free(block);
}
cIStaticVertexArray *__cdecl cStaticVertexComponentArrayDX::New() { return new cStaticVertexComponentArrayDX; }
bool cStaticVertexComponentArrayDX::QueryInterface(uint32_t riid, void **out)
{
    return Answer(riid, out, kIidStaticVertexComponentArray, kIidStaticVertexComponentArray);
}

void *cDynamicVertexComponentArrayDX::operator new(size_t) { return PoolAlloc(kPoolRva); }
void cDynamicVertexComponentArrayDX::operator delete(void *block, size_t)
{
    GameAddress<RZFastFixed0x28>(kPoolRva)->Free(block);
}
cIDynamicVertexArray *__cdecl cDynamicVertexComponentArrayDX::New() { return new cDynamicVertexComponentArrayDX; }
bool cDynamicVertexComponentArrayDX::QueryInterface(uint32_t riid, void **out)
{
    return Answer(riid, out, kIidDynamicArray, kIidDynamicVertexComponentArray);
}
uint32_t cDynamicVertexComponentArrayDX::Reserve(uint32_t stride, uint32_t count)
{
    // The span starts at the first whole item past the current one.
    const uint32_t first = (mOffset + mSpanBytes + stride - 1) / stride;
    const uint32_t bytes = stride * count;
    mOffset = first * stride;
    uint32_t answer = first;
    if (mOffset + bytes > mCapacity) {
        Discard();
        answer = 0;
    }
    mSpanBytes = bytes;
    return answer;
}

void *cStaticVertexIndexArrayDX::operator new(size_t) { return PoolAlloc(kPoolRva); }
void cStaticVertexIndexArrayDX::operator delete(void *block, size_t) { GameAddress<RZFastFixed0x1c>(kPoolRva)->Free(block); }
cIStaticVertexArray *__cdecl cStaticVertexIndexArrayDX::New() { return new cStaticVertexIndexArrayDX; }
bool cStaticVertexIndexArrayDX::QueryInterface(uint32_t riid, void **out)
{
    return Answer(riid, out, kIidStaticVertexIndexArray, kIidStaticVertexIndexArray);
}

void *cDynamicVertexIndexArrayDX::operator new(size_t) { return PoolAlloc(kPoolRva); }
void cDynamicVertexIndexArrayDX::operator delete(void *block, size_t) { GameAddress<RZFastFixed0x28>(kPoolRva)->Free(block); }
cIDynamicVertexArray *__cdecl cDynamicVertexIndexArrayDX::New() { return new cDynamicVertexIndexArrayDX; }
bool cDynamicVertexIndexArrayDX::QueryInterface(uint32_t riid, void **out)
{
    return Answer(riid, out, kIidDynamicVertexIndexArray, kIidDynamicArray);
}
uint32_t cDynamicVertexIndexArrayDX::Reserve(uint32_t count, uint32_t stride)
{
    // Indices are 16 bits: the span follows the current one without alignment.
    const uint32_t bytes = count * stride;
    mOffset += mSpanBytes;
    if (mOffset + bytes > mCapacity) Discard();
    mSpanBytes = bytes;
    return mOffset >> 1;
}

} // namespace nGZGraphic4
