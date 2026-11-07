#include "gz/cGZPersistResourceFactory.h"

#include "game/game.h"
#include "game/vtable.h"

#include <new>

namespace {

using LockFn = int32_t(__thiscall *)(void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using ReleaseFn = uint32_t(__thiscall *)(void *);
using AddRefFn = uint32_t(__thiscall *)(void *);
using KeyFn = const uint32_t *(__thiscall *)(void *);
using SetKeyFn = void(__thiscall *)(void *, const void *);
using CreateFn = bool(__thiscall *)(void *, uint32_t, uint32_t, void **);
using Create5Fn = bool(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t);
using IoFn = bool(__thiscall *)(void *, void *, void *);

void Lock(cRZCriticalSection *lock) { VirtualCall<LockFn, 1>(lock); }
void Unlock(cRZCriticalSection *lock) { VirtualCall<LockFn, 2>(lock); }

} // namespace

cGZPersistResourceFactory *cGZPersistResourceFactory::Construct()
{
    mVTable = GameAddress(kVTable);
    mRefCount = 0;
    mLock.Construct();
    return this;
}

void cGZPersistResourceFactory::Destruct()
{
    mVTable = GameAddress(kVTable);
    mLock.Destruct();
}

cGZPersistResourceFactory *cGZPersistResourceFactory::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool cGZPersistResourceFactory::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid == kIdFactory || riid == 1) {
        *ppvObj = this;
        VirtualCall<AddRefFn, 1>(this);
        return true;
    }
    return false;
}

uint32_t cGZPersistResourceFactory::AddRef()
{
    Lock(&mLock);
    uint32_t count = mRefCount + 1;
    mRefCount = count;
    Unlock(&mLock);
    return count;
}

uint32_t cGZPersistResourceFactory::Release()
{
    Lock(&mLock);
    uint32_t count = mRefCount;
    if (count <= 1) {
        Unlock(&mLock);
        // Through the vtable, slot 7 (deleting destructor), flags 1.
        VirtualCall<void *(__thiscall *)(void *, uint32_t), 7>(this, 1);
        return 0;
    }
    --count;
    mRefCount = count;
    Unlock(&mLock);
    return count;
}

bool cGZPersistResourceFactory::CreateInstance(void *record, uint32_t riid, void **ppvObj, uint32_t arg4, uint32_t arg5)
{
    Lock(&mLock);
    void *created = this; // the original's local is an uninitialised stack slot that holds `this`
    bool ok;
    const uint32_t *key = VirtualCall<KeyFn, 3>(record);
    if (arg4 == 0 && arg5 == 0)
        ok = VirtualCall<CreateFn, kCreateSlot>(this, *key, kIdResourceIface, &created);
    else
        ok = VirtualCall<Create5Fn, 4>(this, *key, kIdResourceIface, &created, arg4, arg5);
    if (ok) {
        auto setKey = VirtualSlot<SetKeyFn>(created, 4); // read before the key getter runs, as the original does
        key = VirtualCall<KeyFn, 3>(record);
        setKey(created, key);
        if (VirtualCall<IoFn, 5>(this, created, record))
            ok = VirtualCall<QueryFn, 0>(created, riid, ppvObj);
        else
            ok = false;
        VirtualCall<ReleaseFn, 2>(created);
    }
    Unlock(&mLock);
    return ok;
}

bool cGZPersistResourceFactory::CreateInstance2(uint32_t typeId, uint32_t riid, void **ppvObj, uint32_t, uint32_t)
{
    return VirtualCall<CreateFn, kCreateSlot>(this, typeId, riid, ppvObj);
}

bool cGZPersistResourceFactory::Read(void *resource, void *record)
{
    Lock(&mLock);
    void *serial = this; // uninitialised stack slot in the original: holds `this`
    bool ok = false;
    if (VirtualCall<QueryFn, 0>(record, kIdSerialRecord, &serial)) {
        ok = VirtualCall<IoFn, kReadRecordSlot>(this, resource, serial);
        VirtualCall<ReleaseFn, 2>(serial);
    }
    Unlock(&mLock);
    return ok;
}

bool cGZPersistResourceFactory::Write(void *resource, void *record)
{
    Lock(&mLock);
    void *serial = this;
    bool ok = false;
    if (VirtualCall<QueryFn, 0>(record, kIdSerialRecord, &serial)) {
        ok = VirtualCall<IoFn, kWriteRecordSlot>(this, resource, serial);
        VirtualCall<ReleaseFn, 2>(serial);
    }
    Unlock(&mLock);
    return ok;
}
