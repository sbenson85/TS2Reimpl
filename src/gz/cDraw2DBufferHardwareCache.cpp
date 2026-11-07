// nGZGraphic4::cDraw2DBufferHardwareCache and cDraw2DRecordRef (lane OL): src/gz/cDraw2DBufferHardwareCache.h.
#include "gz/cDraw2DBufferHardwareCache.h"

#include "gz/GraphicServices.h"

#include "game/game.h"
#include "game/original.h"
#include "gz/cIGZDrawContextEngine.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/ServiceGetters.h" // CachedServiceGetter

namespace nGZGraphic4 {

namespace {

// The draw system, cached at 0x10c95dc (uncounted, as the original's getters leave it).
cIGZDrawSystem *DrawSystem()
{
    return static_cast<cIGZDrawSystem *>(
        CachedServiceGetter<cDraw2D::kDrawSystemCell, cIGZDrawSystem::kServiceId, cIGZDrawSystem::kIid>());
}

// The draw system's view of a cache: the same vtable layout (slots 3 to 12 are named by what the draw system does with
// them there).
cIGZBufferHardwareCache *AsDrawSystemCache(cDraw2DBufferHardwareCache *cache)
{
    return reinterpret_cast<cIGZBufferHardwareCache *>(cache);
}

// The game's Draw2D (0x18c000: the service's slot 13, cached at 0x10ca8fc), the renderer a record handle counts on.
cIDraw2D *Draw2D() { return static_cast<cIDraw2D *>(GetDraw2DCached()); }

} // namespace

// ---- cDraw2DRecordRef -------------------------------------------------------------------------------------------

cDraw2DRecordRef *cDraw2DRecordRef::Set(int32_t index)
{
    mIndex = index;
    Draw2D()->AddRefRecord(mIndex);
    return this;
}

cDraw2DRecordRef *cDraw2DRecordRef::Assign(const cDraw2DRecordRef *other)
{
    Draw2D()->AddRefRecord(other->mIndex);
    Draw2D()->ReleaseRecord(mIndex);
    mIndex = other->mIndex;
    return this;
}

void cDraw2DRecordRef::Release()
{
    if (mIndex == -1) return;
    Draw2D()->ReleaseRecord(mIndex);
    mIndex = -1;
}

// ---- cDraw2DBufferHardwareCache ---------------------------------------------------------------------------------

cDraw2DBufferHardwareCache::cDraw2DBufferHardwareCache(cDraw2D *owner)
    : mRefCount(0), mOwner(owner), mBuffer(nullptr), mTick(0), mChangeCounter(0), mDirty(false), mPad1d{}
{
}

void *cDraw2DBufferHardwareCache::operator new(size_t) { return GameAddress<RZFixedAllocatorBase>(kPoolRva)->Alloc(); }
void cDraw2DBufferHardwareCache::operator delete(void *block) { GameAddress<RZFastFixed0x20>(kPoolRva)->Free(block); }

bool cDraw2DBufferHardwareCache::QueryInterface(uint32_t riid, void **out)
{
    if (!out || (riid != 1 && riid != kIidHardwareCache && riid != kIidOther)) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cDraw2DBufferHardwareCache::Release()
{
    const uint32_t count = --mRefCount;
    if (count == 0) {
        delete this; // slot 13
        return 0;
    }
    return count;
}

bool cDraw2DBufferHardwareCache::Attach(cIGZBuffer *buffer)
{
    mBuffer = buffer;
    DrawSystem()->AddHardwareCache(AsDrawSystemCache(this));
    return true;
}

void cDraw2DBufferHardwareCache::Detach()
{
    Unload();
    mOwner = nullptr;
    mBuffer = nullptr;
    DrawSystem()->RemoveHardwareCache(AsDrawSystemCache(this));
}

bool cDraw2DBufferHardwareCache::LoadForTick(uint32_t tick) { return Load(tick); }

void cDraw2DBufferHardwareCache::Unload()
{
    mRecord.Release();
    mDirty = true;
}

bool cDraw2DBufferHardwareCache::IsStale()
{
    if (mRecord.IsInvalid()) mDirty = true;
    if (!mDirty && mBuffer->GetChangeCounter() != mChangeCounter) mDirty = true;
    return mDirty;
}

bool cDraw2DBufferHardwareCache::Load(uint32_t tick)
{
    mTick = tick;
    if (mRecord.IsInvalid()) {
        // A record for the buffer: it borrows the buffer (uncounted), its colour class from the buffer's format.
        const int32_t index = mOwner->AllocateRecord();
        cDraw2D::Record &record = mOwner->mRecords[index];
        record.refCount = 0;
        record.width = mBuffer->GetWidth();
        record.height = mBuffer->GetHeight();
        record.bufferUncounted = 1;
        record.buffer = mBuffer;
        record.word1c = 0;
        switch (mBuffer->GetNativeColorType().format) {
        case 2:
            record.c = 8;
            record.d = 8;
            break;
        case 3:
        case 4:
        case 5:
            record.c = 0x10;
            record.d = 0;
            break;
        case 6:
            record.c = 0x10;
            record.d = 1;
            break;
        case 7:
            record.c = 0x10;
            record.d = 4;
            break;
        case 8:
            record.c = 0x18;
            record.d = 0;
            break;
        default:
            record.c = 0x20;
            record.d = 8;
            break;
        }
        if (!mOwner->PlaceRecord(record, false)) mOwner->RebuildAtlases();
        if (record.cells.empty()) {
            record.buffer = nullptr;
            mOwner->mFreeRecords.push_back(index);
            return false;
        }
        cDraw2DRecordRef taken;
        taken.Set(index);
        mRecord.Assign(&taken);
        mDirty = true;
    }
    if (IsStale()) {
        cDraw2D::Record &record = mOwner->mRecords[mRecord.mIndex];
        record.word1c = 0;
        uint32_t key;
        if (mBuffer->GetTransparentColor(&key)) {
            // ORIGINAL-BUG: the alpha byte starts as the top byte of the tick argument (a stack slot it shares), so a
            // buffer format whose conversion leaves alpha alone gets that byte; the other bytes are stack leftovers (0 here).
            uint8_t r = 0, g = 0, b = 0, a = static_cast<uint8_t>(tick >> 24);
            mBuffer->ConvertNativeValueToRGBA(key, &r, &g, &b, &a);
            record.word1c = static_cast<uint32_t>(a) << 24 | static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 | b;
        }
        mOwner->UploadRecord(record);
        mChangeCounter = mBuffer->GetChangeCounter();
        mDirty = false;
    }
    return true;
}

cDraw2DBufferHardwareCache::~cDraw2DBufferHardwareCache()
{
    mRecord.Release();
    mDirty = true;
    mOwner = nullptr;
    mBuffer = nullptr;
    DrawSystem()->RemoveHardwareCache(AsDrawSystemCache(this));
}

} // namespace nGZGraphic4
