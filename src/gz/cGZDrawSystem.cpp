// cGZDrawSystem and its buffer engine factory (src/gz/cGZDrawSystem.h; docs/subsystems/rendering.md, R5).
#include "gz/cGZDrawSystem.h"

#include <algorithm>
#include <cmath>
#include <new>

#include "game/original.h"
#include "gz/GZConvertPixels.h"
#include "gz/cGZDrawContext.h"
#include "gz/cGZDrawContextAsBuffer.h"
#include "gz/cGZDrawContextRenderToBuffer.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cRZCursor.h" // GetGraphicSystem
#include "ifaces/cIGZGraphicSystem.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

namespace {

cIGZFrameWork *Framework() { return static_cast<cIGZFrameWork *>(GetServiceObject()); }

// The bytes a buffer's pixels take: bytes per pixel times its bounds' width and height (32-bit wrap, as the original).
uint32_t BufferBytes(cIGZBuffer *buffer)
{
    const cGZRect *const bounds = buffer->GetBoundsPtr();
    const uint32_t width = static_cast<uint32_t>(bounds->right) - static_cast<uint32_t>(bounds->left);
    const uint32_t height = static_cast<uint32_t>(bounds->bottom) - static_cast<uint32_t>(bounds->top);
    return buffer->GetBytesPerPixel() * height * width;
}

} // namespace

// ---- cGZDrawContextRenderToBufferFactory ----

bool cGZDrawContextRenderToBufferFactory::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kIid) return false;
    *out = static_cast<cIGZDrawContextEngineFactory *>(this);
    AddRef();
    return true;
}

uint32_t cGZDrawContextRenderToBufferFactory::AddRef() { return rz::cRZRefCount::AddRef(); }
uint32_t cGZDrawContextRenderToBufferFactory::Release() { return rz::cRZRefCount::Release(); }

bool cGZDrawContextRenderToBufferFactory::CreateEngine(cIGZBuffer *target, cIGZDrawContextEngine **engine)
{
    // Only targets that answer cIGZBuffer; the answer is held until the end and not used otherwise.
    cIGZUnknown *asBuffer = nullptr;
    bool made = false;
    if (target->QueryInterface(cIGZBuffer::kIid, reinterpret_cast<void **>(&asBuffer))) {
        // A new cGZDrawContextRenderToBuffer (0xe4 bytes; the original's constructor 0x8c7dc0, ours).
        // ORIGINAL-BUG: a failed allocation stores null and then calls its AddRef (a null read); ours throws instead.
        cIGZDrawContextEngine *const created = new cGZDrawContextRenderToBuffer;
        *engine = created;
        created->AddRef();
        made = true;
    }
    if (asBuffer) asBuffer->Release();
    return made;
}

// ---- cGZDrawSystem ----

cGZDrawSystem::cGZDrawSystem()
    : rz::cRZSystemService(kServiceId, kPriority), mTick(0), mUnused58(0), mBudget(kDefaultBudget), mFlag(true)
{
}

cGZDrawSystem::~cGZDrawSystem() = default;

cGZDrawSystem *cGZDrawSystem::Create() { return new cGZDrawSystem; }

bool cGZDrawSystem::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cIGZDrawSystem *>(this);
    AddRef();
    return true;
}

uint32_t cGZDrawSystem::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZDrawSystem::Release() { return rz::cRZSystemService::Release(); }

bool cGZDrawSystem::Init()
{
    const FactoryRef factory(new cGZDrawContextRenderToBufferFactory);
    mFactories.push_back(factory);
    Framework()->AddToTick(this);
    return true;
}

bool cGZDrawSystem::Shutdown()
{
    Framework()->RemoveFromTick(this);
    return true;
}

bool cGZDrawSystem::OnTick(uint32_t)
{
    if (mTick % kTrimInterval == 0) Trim(kTrimInterval);
    ++mTick;
    return true;
}

// The newest factory is asked first (pushed at the front; Init's own goes at the back).
void cGZDrawSystem::AddFactory(cIGZDrawContextEngineFactory *factory) { mFactories.push_front(FactoryRef(factory)); }

bool cGZDrawSystem::CreateEngine(cIGZBuffer *target, cIGZDrawContextEngine **engine)
{
    for (const FactoryRef &factory : mFactories)
        if (factory.p->CreateEngine(target, engine)) return true;
    return false;
}

bool cGZDrawSystem::CreateBuffer(cIGZBuffer **out, uint32_t width, uint32_t height, uint32_t format)
{
    // The graphics system's new buffer is held until the end, whatever happened.
    cIGZBuffer *buffer = nullptr;
    bool made = false;
    if (GetGraphicSystem()->CreateBuffer(static_cast<void *>(&buffer))) {
        uint32_t bits;
        switch (format) {
        case 2: bits = 8; break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 7: bits = 16; break;
        case 8:
        case 9: bits = 32; break;
        default: bits = 0; break;
        }
        if (buffer->Init(width, height, format, bits)) {
            // A drawable view of the buffer: a cGZDrawContextAsBuffer over a new context targeting it.
            cGZDrawContextAsBuffer *const view = new cGZDrawContextAsBuffer;
            static_cast<cIGZBuffer *>(view)->AddRef();
            cIGZDrawContext *const context = new cGZDrawContext;
            context->AddRef();
            context->SetTarget(buffer);
            static_cast<cIGZDrawContextAsBuffer *>(view)->Init(context, buffer);
            static_cast<cIGZBuffer *>(view)->AddRef();
            *out = view;
            context->Release();
            static_cast<cIGZBuffer *>(view)->Release();
            made = true;
        }
    }
    if (buffer) buffer->Release();
    return made;
}

bool cGZDrawSystem::ConvertPixels(uint8_t *to, uint32_t toStride, uint32_t toFormat, const uint8_t *from,
                                  uint32_t fromStride, uint32_t fromFormat, int32_t width, int32_t height,
                                  const uint32_t *key)
{
    if (width == 0 || height == 0) return true;
    // The pixel converter 0x8c6b10 (ours, src/gz/GZConvertPixels.cpp).
    return GZConvertPixels(to, static_cast<int32_t>(toStride), toFormat, from, static_cast<int32_t>(fromStride), fromFormat,
                           width, height, key);
}

void cGZDrawSystem::SetMemoryBudget(uint32_t bytes) { mBudget = bytes; }
uint32_t cGZDrawSystem::GetMemoryBudget() { return mBudget; }

void cGZDrawSystem::AddTransientBuffer(cIGZTransientBuffer *buffer)
{
    mLock.Lock();
    // The buffer's bounds are asked for and not used.
    buffer->GetBuffer()->GetBoundsPtr();
    mWatched.push_back(WatchedBuffer{buffer, buffer->GetLockTotal(), mTick, 0.0f});
    mLock.Unlock();
}

void cGZDrawSystem::RemoveTransientBuffer(cIGZTransientBuffer *buffer)
{
    mLock.Lock();
    // The first entry for it, replaced by the last.
    for (WatchedBuffer &entry : mWatched) {
        if (entry.buffer == buffer) {
            entry = mWatched.back();
            mWatched.pop_back();
            break;
        }
    }
    mLock.Unlock();
}

void cGZDrawSystem::AddHardwareCache(cIGZBufferHardwareCache *cache)
{
    mLock.Lock();
    mCaches.push_back(cache);
    mLock.Unlock();
}

void cGZDrawSystem::RemoveHardwareCache(cIGZBufferHardwareCache *cache)
{
    mLock.Lock();
    const auto found = std::find(mCaches.begin(), mCaches.end(), cache);
    if (found != mCaches.end()) {
        *found = mCaches.back();
        mCaches.pop_back();
    }
    mLock.Unlock();
}

void cGZDrawSystem::Trim(uint32_t minimumAge)
{
    mLock.Lock();
    // Caches drawn last `minimumAge` ticks ago or more let their textures go.
    for (cIGZBufferHardwareCache *cache : mCaches)
        if (cache->IsLoaded() && mTick - cache->GetLastUseTick() >= minimumAge) cache->Unload();

    // The ready transient buffers' bytes; those unchanged (the same lock total) for `minimumAge` ticks or more are the
    // candidates, weighted by the square root of their idle ticks times their bytes.
    uint32_t total = 0;
    for (WatchedBuffer &entry : mWatched) {
        cIGZBuffer *const buffer = entry.buffer->GetBuffer();
        const uint32_t bytes = BufferBytes(buffer);
        if (!buffer->IsReady()) continue;
        const uint32_t lockTotal = entry.buffer->GetLockTotal();
        total += bytes;
        if (entry.lockTotal != lockTotal) {
            entry.lockTotal = lockTotal;
            entry.changedTick = mTick;
            continue;
        }
        const uint32_t idle = mTick - entry.changedTick;
        if (idle < minimumAge) continue;
        // The unsigned counts reach float through double (each rounded once), as the original's conversions do.
        const float idleTicks = static_cast<float>(static_cast<double>(idle));
        const float root = static_cast<float>(std::sqrt(static_cast<double>(idleTicks)));
        entry.weight = root * static_cast<float>(static_cast<double>(bytes));
        mTrimList.push_back(&entry);
    }

    // Over the budget: the heaviest candidates committed (their memory given back) until under it.
    if (total > mBudget) {
        std::sort(mTrimList.begin(), mTrimList.end(),
                  [](const WatchedBuffer *a, const WatchedBuffer *b) { return a->weight > b->weight; });
        for (auto it = mTrimList.begin(); total > mBudget && it != mTrimList.end(); ++it) {
            WatchedBuffer *const entry = *it;
            const uint32_t bytes = BufferBytes(entry->buffer->GetBuffer());
            entry->buffer->Commit();
            total -= bytes;
        }
    }
    mTrimList.clear();
    mLock.Unlock();
}

void cGZDrawSystem::FlushAll()
{
    mLock.Lock();
    for (cIGZBufferHardwareCache *cache : mCaches) cache->Unload();
    for (WatchedBuffer &entry : mWatched) entry.buffer->Commit();
    mLock.Unlock();
}

uint32_t cGZDrawSystem::GetTick() { return mTick; }

void cGZDrawSystem::GetHardwareCacheUsage(uint32_t *count, uint32_t *bytes)
{
    mLock.Lock();
    uint32_t loaded = 0, total = 0;
    for (cIGZBufferHardwareCache *cache : mCaches) {
        if (!cache->IsLoaded()) continue;
        ++loaded;
        total += BufferBytes(cache->GetBuffer());
    }
    *count = loaded;
    *bytes = total;
    mLock.Unlock();
}

void cGZDrawSystem::GetTransientBufferUsage(uint32_t *count, uint32_t *ready, uint32_t *bytes)
{
    mLock.Lock();
    uint32_t all = 0, readyCount = 0, total = 0;
    for (WatchedBuffer &entry : mWatched) {
        cIGZBuffer *const buffer = entry.buffer->GetBuffer();
        const uint32_t size = BufferBytes(buffer);
        if (buffer->IsReady()) {
            // The lock total is asked for and not used.
            entry.buffer->GetLockTotal();
            total += size;
            ++readyCount;
        }
        ++all;
    }
    *count = all;
    *ready = readyCount;
    *bytes = total;
    mLock.Unlock();
}

bool cGZDrawSystem::vf18(uint32_t, uint32_t) { return false; }
void cGZDrawSystem::SetFlag(bool value) { mFlag = value; }
bool cGZDrawSystem::GetFlag() { return mFlag; }

cGZDrawSystem *__fastcall cGZDrawSystemAdapters::Construct(void *memory, void *) { return ::new (memory) cGZDrawSystem; }
