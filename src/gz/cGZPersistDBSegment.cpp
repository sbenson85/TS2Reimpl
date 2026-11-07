#include "gz/cGZPersistDBSegment.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZRefCount.h"

#include <new>

namespace {

using AddRefFn = uint32_t(__thiscall *)(void *);
using VoidFn = void(__thiscall *)(void *);
using VoidFlagFn = void(__thiscall *)(void *, uint32_t);
using BoolFn = bool(__thiscall *)(void *);
using U32ArgFn = uint32_t(__thiscall *)(void *, uint32_t);
using BoolArgFn = bool(__thiscall *)(void *, uint32_t);
using BoolArgPtrFn = bool(__thiscall *)(void *, void **);
using BoolArg2Fn = bool(__thiscall *)(void *, uint32_t, uint32_t *);
using VoidPtrFn = void(__thiscall *)(void *, void *);
using U32Arg2Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using U32Arg3Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using BoolArg2ValFn = bool(__thiscall *)(void *, uint32_t, uint32_t);
using BoolArg3Fn = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

// The lock is the cRZCriticalSection at +8: slot 1 Lock, slot 2 Unlock, with its own address as `this`.
void Enter(cGZPersistDBSegment *self)
{
    VirtualSlot<AddRefFn>(&self->mLock, 1)(&self->mLock);
}

void Leave(cGZPersistDBSegment *self)
{
    VirtualSlot<AddRefFn>(&self->mLock, 2)(&self->mLock);
}

} // namespace

cGZPersistDBSegment *cGZPersistDBSegment::Construct()
{
    mVTable = VTable();
    mRefCount = 0;
    mLock.Construct();
    mSegmentCache = nullptr;
    mNamespace = nullptr;
    return this;
}

void cGZPersistDBSegment::Destruct()
{
    mVTable = VTable();
    // Namespace first, then the cache; neither pointer is cleared.
    if (void *ns = mNamespace) VirtualCall<VoidFn, 2>(ns);
    if (void *cache = mSegmentCache) VirtualCall<VoidFn, 2>(cache);
    mLock.Destruct();
}

cGZPersistDBSegment *cGZPersistDBSegment::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

uint32_t cGZPersistDBSegment::AddRef()
{
    return static_cast<uint32_t>(RZAtomicAdd(&mRefCount, 1));
}

void cGZPersistDBSegment::SetNamespaceInner(void *ns)
{
    void *old = mNamespace;
    if (ns == old) return;
    if (ns) VirtualCall<AddRefFn, 1>(ns);
    mNamespace = ns;
    if (old) VirtualCall<VoidFn, 2>(old);
}

bool cGZPersistDBSegment::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != kIdSegment && riid != 1) return false;
    *ppvObj = this;
    VirtualCall<AddRefFn, 1>(this);
    return true;
}

uint32_t cGZPersistDBSegment::Release()
{
    int32_t count = RZAtomicAdd(&mRefCount, -1);
    if (count != 0) return static_cast<uint32_t>(count);
    // Last reference: the count is reset to 1 before the object deletes itself through slot 34.
    mRefCount = 1;
    VirtualCall<VoidFn, 34>(this);
    return 0;
}

bool cGZPersistDBSegment::Shutdown()
{
    if (void *ns = mNamespace) {
        mNamespace = nullptr;
        VirtualCall<VoidFn, 2>(ns);
    }
    if (void *cache = mSegmentCache) {
        mSegmentCache = nullptr;
        VirtualCall<VoidFn, 2>(cache);
    }
    return true;
}

bool cGZPersistDBSegment::Lock()
{
    Enter(this);
    return true;
}

bool cGZPersistDBSegment::Unlock()
{
    Leave(this);
    return true;
}

void cGZPersistDBSegment::SetSegmentCache(void *cache)
{
    void *old = mSegmentCache;
    if (cache == old) return;
    if (cache) VirtualCall<AddRefFn, 1>(cache);
    mSegmentCache = cache;
    if (old) VirtualCall<VoidFn, 2>(old);
}

uint32_t cGZPersistDBSegment::GetRecordCount(uint32_t a)
{
    Enter(this);
    uint32_t result = VirtualCall<U32ArgFn, 44>(this, a);
    Leave(this);
    return result;
}

uint32_t cGZPersistDBSegment::GetRecordSize(uint32_t a)
{
    Enter(this);
    uint32_t result = VirtualCall<U32ArgFn, 36>(this, a);
    Leave(this);
    return result;
}

uint32_t cGZPersistDBSegment::GetSegmentChangeID()
{
    Enter(this);
    uint32_t result = VirtualCall<AddRefFn, 46>(this);
    Leave(this);
    return result;
}

bool cGZPersistDBSegment::GetRecordChangeID(uint32_t key, uint32_t *changeId)
{
    Enter(this);
    bool result = VirtualCall<BoolArg2Fn, 47>(this, key, changeId);
    Leave(this);
    return result;
}

void cGZPersistDBSegment::SetNamespace(void *ns)
{
    Enter(this);
    VirtualCall<VoidPtrFn, 48>(this, ns);
    Leave(this);
}

bool cGZPersistDBSegment::GetNamespace(void **out)
{
    Enter(this);
    bool result = VirtualCall<BoolArgPtrFn, 49>(this, out);
    Leave(this);
    return result;
}

// Record operations: each is a locked forwarder to one of the pure slots (the arguments in order).

uint32_t cGZPersistDBSegment::GetResourceKeyList(uint32_t a, uint32_t b)
{
    Enter(this);
    uint32_t result = VirtualCall<U32Arg2Fn, 45>(this, a, b);
    Leave(this);
    return result;
}

bool cGZPersistDBSegment::TestForRecord(uint32_t a)
{
    Enter(this);
    bool result = VirtualCall<BoolArgFn, 35>(this, a);
    Leave(this);
    return result;
}

bool cGZPersistDBSegment::AbortRecord(uint32_t a)
{
    Enter(this);
    bool result = VirtualCall<BoolArgFn, 40>(this, a);
    Leave(this);
    return result;
}

bool cGZPersistDBSegment::CloseRecord(uint32_t a)
{
    Enter(this);
    bool result = VirtualCall<BoolArgFn, 39>(this, a);
    Leave(this);
    return result;
}

bool cGZPersistDBSegment::CreateNewRecord(uint32_t a, uint32_t b)
{
    Enter(this);
    bool result = VirtualCall<BoolArg2ValFn, 38>(this, a, b);
    Leave(this);
    return result;
}

bool cGZPersistDBSegment::DeleteRecord(uint32_t a)
{
    Enter(this);
    bool result = VirtualCall<BoolArgFn, 41>(this, a);
    Leave(this);
    return result;
}

bool cGZPersistDBSegment::OpenRecord(uint32_t a, uint32_t b, uint32_t c)
{
    Enter(this);
    bool result = VirtualCall<BoolArg3Fn, 37>(this, a, b, c);
    Leave(this);
    return result;
}

uint32_t cGZPersistDBSegment::ReadRecord(uint32_t a, uint32_t b, uint32_t c)
{
    Enter(this);
    uint32_t result = VirtualCall<U32Arg3Fn, 42>(this, a, b, c);
    Leave(this);
    return result;
}

bool cGZPersistDBSegment::WriteRecord(uint32_t a, uint32_t b, uint32_t c)
{
    Enter(this);
    bool result = VirtualCall<BoolArg3Fn, 43>(this, a, b, c);
    Leave(this);
    return result;
}

void cGZPersistDBSegment::vf34()
{
    if (this) VirtualCall<VoidFlagFn, 33>(this, 1);
}

bool cGZPersistDBSegment::vf47(uint32_t key, uint32_t *changeId)
{
    if (!VirtualCall<BoolArgFn, 35>(this, key)) return false;
    *changeId = 0;
    return true;
}

bool cGZPersistDBSegment::vf49(void **out)
{
    void *ns = mNamespace;
    if (!ns) return false;
    *out = ns;
    VirtualCall<AddRefFn, 1>(ns);
    return true;
}

// Our vtable's slots (rzface::cGZPersistDBSegment, the header): each runs the member of the same name.
extern "C" int __cdecl _purecall(void);

namespace rzface {

[[noreturn]] void PureSlot()
{
    _purecall();
    __assume(0);
}

namespace {
::cGZPersistDBSegment *Self(cGZPersistDBSegment *face) { return reinterpret_cast<::cGZPersistDBSegment *>(face); }
} // namespace

bool cGZPersistDBSegment::QueryInterface(uint32_t riid, void **ppvObj) { return Self(this)->QueryInterface(riid, ppvObj); }
uint32_t cGZPersistDBSegment::AddRef() { return Self(this)->AddRef(); }
uint32_t cGZPersistDBSegment::Release() { return Self(this)->Release(); }
bool cGZPersistDBSegment::Shutdown() { return Self(this)->Shutdown(); }
bool cGZPersistDBSegment::Lock() { return Self(this)->Lock(); }
bool cGZPersistDBSegment::Unlock() { return Self(this)->Unlock(); }
void cGZPersistDBSegment::SetSegmentCache(void *cache) { Self(this)->SetSegmentCache(cache); }
uint32_t cGZPersistDBSegment::GetRecordCount(uint32_t a) { return Self(this)->GetRecordCount(a); }
uint32_t cGZPersistDBSegment::GetResourceKeyList(uint32_t a, uint32_t b) { return Self(this)->GetResourceKeyList(a, b); }
bool cGZPersistDBSegment::TestForRecord(uint32_t a) { return Self(this)->TestForRecord(a); }
uint32_t cGZPersistDBSegment::GetRecordSize(uint32_t a) { return Self(this)->GetRecordSize(a); }
bool cGZPersistDBSegment::OpenRecord(uint32_t a, uint32_t b, uint32_t c) { return Self(this)->OpenRecord(a, b, c); }
bool cGZPersistDBSegment::CreateNewRecord(uint32_t a, uint32_t b) { return Self(this)->CreateNewRecord(a, b); }
bool cGZPersistDBSegment::CloseRecord(uint32_t a) { return Self(this)->CloseRecord(a); }
bool cGZPersistDBSegment::AbortRecord(uint32_t a) { return Self(this)->AbortRecord(a); }
bool cGZPersistDBSegment::DeleteRecord(uint32_t a) { return Self(this)->DeleteRecord(a); }
uint32_t cGZPersistDBSegment::ReadRecord(uint32_t a, uint32_t b, uint32_t c) { return Self(this)->ReadRecord(a, b, c); }
bool cGZPersistDBSegment::WriteRecord(uint32_t a, uint32_t b, uint32_t c) { return Self(this)->WriteRecord(a, b, c); }
uint32_t cGZPersistDBSegment::GetSegmentChangeID() { return Self(this)->GetSegmentChangeID(); }
bool cGZPersistDBSegment::GetRecordChangeID(uint32_t key, uint32_t *changeId) { return Self(this)->GetRecordChangeID(key, changeId); }
void cGZPersistDBSegment::SetNamespace(void *ns) { Self(this)->SetNamespace(ns); }
bool cGZPersistDBSegment::GetNamespace(void **out) { return Self(this)->GetNamespace(out); }
void *cGZPersistDBSegment::DeletingDestructor(uint32_t flags) { return Self(this)->DeletingDestructor(flags); }
void cGZPersistDBSegment::Delete() { Self(this)->vf34(); }
uint32_t cGZPersistDBSegment::Zero46() { return 0; }
bool cGZPersistDBSegment::GetRecordChangeIDInner(uint32_t key, uint32_t *changeId) { return Self(this)->vf47(key, changeId); }
void cGZPersistDBSegment::SetNamespaceInner(void *ns) { Self(this)->SetNamespaceInner(ns); }
bool cGZPersistDBSegment::GetNamespaceInner(void **out) { return Self(this)->vf49(out); }

} // namespace rzface
