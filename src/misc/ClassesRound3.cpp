#include "misc/ClassesRound3.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/Singleton_20ddfb.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

// Speed-built members of src/misc/ClassesRound3.h.

cTSTripleAdapters::Self *__fastcall cTSTripleAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

// ---- nTSWorld::tRoofObject ----

namespace {
// The original's float-to-int rounding: add 1.5 * 2^23 and take the low bits of the sum.
__forceinline int32_t RoundToInt(float value)
{
    float biased = value + 12582912.0f;
    int32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    return bits - 0x4b400000;
}
} // namespace

float nTSWorld::tRoofObject::GetMaxX() const
{
    float x = std::max(mX0, mX1);
    if (mHasEaves) {
        if (mType == 1 && !mFlag25 && mNoEavesX) return x;
        if (mType == 0x12 && !mFlag25 && mNoEavesX) return x;
        if (mType == 2 && mFlag25 && mNoEavesX) return x;
        x += mEavesX;
    }
    return x;
}

float nTSWorld::tRoofObject::GetMinX() const
{
    float x = std::min(mX0, mX1);
    if (mHasEaves) {
        if (mType == 1 && !mFlag25 && !mNoEavesX) return x;
        if (mType == 0x12 && !mFlag25 && !mNoEavesX) return x;
        if (mType == 2 && mFlag25 && !mNoEavesX) return x;
        x -= mEavesX;
    }
    return x;
}

float nTSWorld::tRoofObject::GetMaxY() const
{
    float y = std::max(mY0, mY1);
    if (mHasEaves) {
        if (mType == 1 && mFlag25 && mNoEavesY) return y;
        if (mType == 0x12 && mFlag25 && mNoEavesY) return y;
        if (mType == 2 && !mFlag25 && mNoEavesY) return y;
        y += mEavesY;
    }
    return y;
}

float nTSWorld::tRoofObject::GetMinY() const
{
    float y = std::min(mY0, mY1);
    if (mHasEaves) {
        if (mType == 1 && mFlag25 && !mNoEavesY) return y;
        if (mType == 0x12 && mFlag25 && !mNoEavesY) return y;
        if (mType == 2 && !mFlag25 && !mNoEavesY) return y;
        y -= mEavesY;
    }
    return y;
}

int32_t nTSWorld::tRoofObject::GetXVertexCount() const
{
    float extent = GetMaxX() - GetMinX();
    return RoundToInt(extent + extent) + 1;
}

int32_t nTSWorld::tRoofObject::GetYVertexCount() const
{
    float extent = GetMaxY() - GetMinY();
    return RoundToInt(extent + extent) + 1;
}

// ---- nTSSG::cQueryStrategyManager ----

bool nTSSG::cQueryStrategyManager::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    switch (iid) {
    case 1:
    case kIid:
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    return false;
}

uint32_t nTSSG::cQueryStrategyManager::Release()
{
    const uint32_t count = mRefCount - 1;
    mRefCount = count;
    if (count != 0) return count;
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 21>(this, 1); // deleting destructor
    return 0;
}

nTSSG::cQueryStrategyManager *nTSSG::cQueryStrategyManager::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cQueryStrategyManager *), 0x2c0a10>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cQueryStrategyManager));
    return this;
}

// ---- nTSSG::cImageReadback ----

uint32_t nTSSG::cImageReadback::AddRef() { return ++mRefCount; }

bool nTSSG::cImageReadback::QueryInterface(uint32_t iid, void **out)
{
    switch (iid) {
    case 1:
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    return false;
}

uint32_t nTSSG::cImageReadback::Release()
{
    if (mRefCount == 1) {
        OriginalFunction<void(__thiscall *)(cImageReadback *), 0x401fb0>()(this);
        ::operator delete(this, sizeof(cImageReadback));
        return 0;
    }
    return --mRefCount;
}

// ---- cHitSem (speed-built slot) ----

uint32_t cHitSem::Release()
{
    if (mRefCount == 1) {
        VirtualCall<void *(__thiscall *)(void *, uint32_t), 7>(this, 1); // deleting destructor
        return 0;
    }
    return --mRefCount;
}

// ---- nPathPlanner::cDistancePathHeuristic ----

float nPathPlanner::cDistancePathHeuristic::Estimate(float x, float y, uint32_t, uint32_t, const float *goal) const
{
    float dy = y - goal[1];
    float dx = x - goal[0];
    return static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
}

// ---- cTSPerson::cMotiveInc, cTSPerson::cObjectRecord ----

bool cTSPersonMotiveInc::Write(ifaces::cIGZOStream *stream) const
{
    stream->SetSint32(mMotive);
    stream->SetFloat32(mValue8);
    stream->SetFloat32(mValueC);
    return !stream->GetError();
}

bool cTSPersonMotiveInc::Read(ifaces::cIGZIStream *stream)
{
    int32_t motive;
    stream->GetSint32(&motive);
    mMotive = static_cast<int16_t>(motive);
    stream->GetFloat32(&mValue8);
    stream->GetFloat32(&mValueC);
    return !stream->GetError();
}

namespace {
// The cached singleton's slot 26 object: slot 61 reads an object reference from a stream, slot 62 writes one.
using ObjectRefService = void *(__thiscall *)(void *);                         // singleton slot 26
using ReadObjectRef = void(__thiscall *)(void *, ifaces::cIGZIStream *, void **); // slot 61
using WriteObjectRef = void(__thiscall *)(void *, ifaces::cIGZOStream *, void *); // slot 62
} // namespace

bool cTSPersonObjectRecord::Write(ifaces::cIGZOStream *stream) const
{
    void *service = VirtualCall<ObjectRefService, 26>(GetCachedSingleton());
    VirtualCall<WriteObjectRef, 62>(service, stream, mObject);
    stream->SetSint32(mValue8);
    stream->SetUint8(mFlagA);
    return !stream->GetError();
}

bool cTSPersonObjectRecord::Read(ifaces::cIGZIStream *stream)
{
    void *service = VirtualCall<ObjectRefService, 26>(GetCachedSingleton());
    void *object = mObject;
    VirtualCall<ReadObjectRef, 61>(service, stream, &object);
    void *fresh = object; // the counted assignment: new reference first, then the old one released
    void *old = mObject;
    if (fresh != old) {
        if (fresh) VirtualCall<uint32_t(__thiscall *)(void *), 1>(fresh); // AddRef
        mObject = fresh;
        if (old) VirtualCall<uint32_t(__thiscall *)(void *), 2>(old); // Release
    }
    int32_t value;
    stream->GetSint32(&value);
    uint8_t flag;
    stream->GetUint8(&flag);
    mValue8 = static_cast<int16_t>(value);
    mFlagA = flag != 0;
    return !stream->GetError();
}

// ---- nTSSG::cDisplayListDynamicRenderStrategyState ----

bool nTSSG::cDisplayListDynamicRenderStrategyState::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    switch (iid) {
    case 1:
    case kIid:
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    return false;
}

uint32_t nTSSG::cDisplayListDynamicRenderStrategyState::AddRef() { return ++mRefCount; }

uint32_t nTSSG::cDisplayListDynamicRenderStrategyState::Release()
{
    uint32_t count = mRefCount;
    if (count > 1) {
        mRefCount = --count;
        return count;
    }
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 3>(this, 1); // deleting destructor
    return 0;
}

nTSSG::cDisplayListDynamicRenderStrategyState *nTSSG::cDisplayListDynamicRenderStrategyState::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cDisplayListDynamicRenderStrategyState *), 0x36f5c0>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cDisplayListDynamicRenderStrategyState));
    return this;
}
