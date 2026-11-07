#pragma once
// cRZLock (lane FD): the framework's scope lock. Eight bytes, the vtable word (0xd9b644, one slot: the deleting destructor
// 0x142248) and the lockable it holds; built on the stack around a critical section (cIGZCriticalSection: slot 1 locks,
// slot 2 unlocks). The constructor (0x1d8af0, 27 bytes, ret 4) stores the table and the lockable and locks it; the
// destructor puts the table back and unlocks. The one copy of the class: the persist system's (cTSPersistSystem.cpp),
// the speculative loader's (cTSPersistSpeculativeLoader.cpp) and the hook-shaped deleting destructor (formerly
// misc/SmallVirtuals.h) all use it.
//
// The vtable is owned (game/owned.h): our code stores &ts2_rva_d9b644 (the game's table in hooked builds; an image-free
// program's ts2_own_vt_d9b644, standalone/owned_statics.cpp's alias). The constructor and the deleting destructor are
// hooked (src/hook/tables/cRZLock.cpp); both bodies are size-built (src/rz/cRZLock.size.cpp).
#include "game/owned.h"
#include "game/vtable.h"

#include <cstdint>

TS2_OWNED_STATIC(const OwnedVTable, d9b644); // cRZLock
extern "C" const void *ts2_own_vt_d9b644[1];

class cRZLock {
public:
    static constexpr uint32_t kVTable = 0xd9b644;

    // Any lockable with the critical section's slot order (the game's objects or ours).
    explicit cRZLock(void *lockable) : mVTable(&ts2_rva_d9b644), mLockable(lockable)
    {
        VirtualCall<int32_t(__thiscall *)(void *), 1>(lockable); // lock
    }
    ~cRZLock()
    {
        mVTable = &ts2_rva_d9b644; // a destructor puts its class's table back
        VirtualCall<int32_t(__thiscall *)(void *), 2>(mLockable); // unlock
    }
    cRZLock(const cRZLock &) = delete;
    cRZLock &operator=(const cRZLock &) = delete;

    cRZLock *Construct(void *lockable);          // 0x1d8af0 (ret 4): the constructor on caller memory, this returned
    cRZLock *DeletingDestructor(uint32_t flags); // slot 0, 0x142248 (ret 4): frees 8 on flags & 1

    const OwnedVTable *mVTable; // +0
    void *mLockable;            // +4
};
static_assert(sizeof(cRZLock) == 8, "layout: the size 0x142248 frees");
