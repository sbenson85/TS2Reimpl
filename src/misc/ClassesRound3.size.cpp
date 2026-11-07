#include "misc/ClassesRound3.h"

#include <new>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

// Size-built members of src/misc/ClassesRound3.h.

// ---- cCompletionObj ----

bool cCompletionObj::QueryInterface(uint32_t iid, void **out)
{
    if (iid == kIid) {
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    if (iid == 1) {
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    return false;
}

uint32_t cCompletionObj::Release()
{
    const uint32_t count = mRefCount - 1;
    mRefCount = count;
    if (count != 0) return count;
    mVTable = GameAddress<void>(kVTable); // the inline destructor
    ::operator delete(this, sizeof(cCompletionObj));
    return 0;
}

// ---- cRZNullIStream ----

bool cRZNullIStream::Skip(uint32_t)
{
    mFailed = true;
    return false;
}

bool cRZNullIStream::GetVoid(void *, uint32_t)
{
    mFailed = true;
    return false;
}

void cRZNullIStream::Destruct()
{
    mVTable = GameAddress<void>(kVTable);
    cRZIStream::Destruct();
}

cRZNullIStream *cRZNullIStream::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZNullIStream));
    return this;
}

// ---- nGZEvent::cConnectionChangedEvent ----

nGZEvent::cConnectionChangedEvent *nGZEvent::cConnectionChangedEvent::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kBaseVTable);
    if (flags & 1) ::operator delete(this, sizeof(cConnectionChangedEvent));
    return this;
}

nGZEvent::cConnectionChangedEvent *nGZEvent::cConnectionChangedEvent::Assign(const cConnectionChangedEvent &other)
{
    mData[0] = other.mData[0];
    mData[1] = other.mData[1];
    mData[2] = other.mData[2];
    mData[3] = other.mData[3];
    mData[4] = other.mData[4];
    return this;
}

uint32_t nGZEvent::cConnectionChangedEvent::GetEventType() const { return kEventType; }

void *nGZEvent::cConnectionChangedEvent::CloneInto(void *dest) const
{
    return static_cast<cConnectionChangedEvent *>(dest)->CopyConstruct(*this);
}

nGZEvent::cConnectionChangedEvent *nGZEvent::cConnectionChangedEvent::CopyConstruct(const cConnectionChangedEvent &other)
{
    mVTable = GameAddress<void>(kVTable);
    mData[0] = other.mData[0];
    mData[1] = other.mData[1];
    mData[2] = other.mData[2];
    mData[3] = other.mData[3];
    mData[4] = other.mData[4];
    return this;
}

// ---- inventory token filters ----

namespace {
using TokenFlags = uint16_t(__thiscall *)(void *);   // token slot 8
using TokenGuid = uint32_t(__thiscall *)(void *);    // token slot 6
using TokenCount = uint16_t(__thiscall *)(void *);   // token slot 15
using ObjectByGuid = void *(__thiscall *)(void *, uint32_t); // object manager slot 14
using ObjectData = void *(__thiscall *)(void *);     // object slot 4
using DataField = int32_t(__thiscall *)(void *, uint32_t); // data slot 3
} // namespace

bool cTSFridgeTokenFilter::Accept(void *token)
{
    if (token) {
        uint16_t flags = VirtualCall<TokenFlags, 8>(token);
        if (!(flags & 6) && (flags & 1)) {
            void *object = VirtualCall<ObjectByGuid, 14>(mObjectManager, VirtualCall<TokenGuid, 6>(token));
            if (object) {
                void *data = VirtualCall<ObjectData, 4>(object);
                return VirtualCall<DataField, 3>(data, 0x4b) == 0x7c;
            }
        }
    }
    return false;
}

bool cTSJuicerTokenFilter::Accept(void *token)
{
    if (token) {
        uint16_t flags = VirtualCall<TokenFlags, 8>(token);
        if (!(flags & 6) && (flags & 1)) {
            void *object = VirtualCall<ObjectByGuid, 14>(mObjectManager, VirtualCall<TokenGuid, 6>(token));
            if (object) {
                void *data = VirtualCall<ObjectData, 4>(object);
                if (VirtualCall<DataField, 3>(data, 0x4b) == 0x7c && VirtualCall<DataField, 3>(data, 0x4c) == 0) return true;
                return VirtualCall<DataField, 3>(data, 0x4b) == 0xaa;
            }
        }
    }
    return false;
}

// The original keeps a frame pointer here (its module is built without frame-pointer omission).
#pragma optimize("y", off)
bool cTSLeftoverTokenFilter::Accept(void *token)
{
    if (token) {
        uint16_t count = VirtualCall<TokenCount, 15>(token);
        uint16_t flags = VirtualCall<TokenFlags, 8>(token);
        if (count == 1 && !(flags & 6) && (flags & 1)) {
            void *object = VirtualCall<ObjectByGuid, 14>(mObjectManager, VirtualCall<TokenGuid, 6>(token));
            if (object) {
                void *data = VirtualCall<ObjectData, 4>(object);
                if (data) {
                    if (VirtualCall<DataField, 3>(data, 0x4b) == 0x25) VirtualCall<DataField, 3>(data, 0x4c);
                    return true;
                }
            }
        }
    }
    return false;
}
#pragma optimize("", on)

bool cTSFoodJudgingTokenFilter::Accept(int32_t id)
{
    if (id) {
        auto it = mPending.find(id);
        if (it != mPending.end()) {
            mPending.erase(it);
            return true;
        }
    }
    return false;
}

// ---- cHitSem ----

bool cHitSem::QueryInterface(uint32_t iid, void **out)
{
    switch (iid) {
    case kIid:
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    case 1:
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    return false;
}

bool cHitSem::GiveName(void *target)
{
    VirtualCall<uint32_t(__thiscall *)(void *, cRZString *), 17>(target, &mName);
    return true;
}

void cHitSem::Destruct()
{
    mVTable = GameAddress<void>(kVTable);
    mName.mVTable = GameAddress<void>(cRZString::kVTable);
    mName.mString.~basic_string();
}

cHitSem *cHitSem::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cHitSem));
    return this;
}

// ---- cRZFTPClientLowLevel ----

uint32_t cRZFTPClientLowLevel::Release()
{
    uint32_t count = mRefCount;
    if (count > 1) {
        mRefCount = --count;
        return count;
    }
    VirtualCall<void(__thiscall *)(void *), 4>(this); // Destroy
    return 0;
}

cRZFTPClientLowLevel *cRZFTPClientLowLevel::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cRZFTPClientLowLevel *), 0x994ad5>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cRZFTPClientLowLevel));
    return this;
}

void cRZFTPClientLowLevel::Destroy()
{
    OriginalFunction<void(__thiscall *)(cRZFTPClientLowLevel *), 0x998514>()(this);
    cRZFTPClientLowLevel *self = this;
    if (self) VirtualCall<void *(__thiscall *)(void *, uint32_t), 3>(self, 1); // delete this
}

// ---- cTSImgFileFilter ----

bool cTSImgFileFilter::QueryInterface(uint32_t iid, void **out)
{
    if (out && iid == kIid) {
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    return false;
}

bool cTSImgFileFilter::Accept(void *file)
{
    if (mInner && VirtualCall<bool(__thiscall *)(void *, void *), 10>(mInner, file)) return true;
    return false;
}

// ---- cRZFTPClient ----

bool cRZFTPClient::QueryInterface(uint32_t iid, void **out)
{
    if (iid == kIid) {
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    if (iid == 1) {
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    return false;
}

uint32_t cRZFTPClient::Release()
{
    uint32_t count = mRefCount;
    if (count > 1) {
        mRefCount = --count;
        return count;
    }
    VirtualCall<void(__thiscall *)(void *), 5>(this); // Destroy
    return 0;
}

cRZFTPClient *cRZFTPClient::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cRZFTPClient *), 0x994a69>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cRZFTPClient));
    return this;
}

void cRZFTPClient::Destroy()
{
    OriginalFunction<void(__thiscall *)(cRZFTPClient *), 0x9984f3>()(this);
    cRZFTPClient *self = this;
    if (self) VirtualCall<void *(__thiscall *)(void *, uint32_t), 4>(self, 1); // delete this
}
