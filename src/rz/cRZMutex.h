#pragma once
#include "gz/cIGZString.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// A named Windows mutex behind a small interface. Original vtable: RVA 0xe9ebf0 (1.18.0.187), 7
// slots: Delete, Lock, Unlock, TryLock, IsValid, the scalar deleting destructor, GetHandle. The
// object is 8 bytes (vtable, pointer to its data); the 0x28-byte data block is allocated by the
// constructor. The application's constructor makes two of these ("Global\The Sims 2" and
// "Global\The Sims 2 Body Shop") for the single-instance check.

// The data block (0x28 bytes): the handle, whether creating it worked, and the name it was
// created with.
struct cRZMutexData {
    void *mHandle;     // +0x00 the HANDLE from CreateMutexA (null on failure)
    uint32_t mValid;   // +0x04 1 when the handle is not null, else 0
    cRZString mName;   // +0x08

    // 0x218f06 (ret 4): destroys the name; flags & 1 also frees the block (sized delete, 0x28).
    // Other classes' code (cGZWinFileBrowser and others) calls the same function.
    cRZMutexData *DeletingDestructor(uint32_t flags);
};

static_assert(sizeof(cRZMutexData) == 0x28, "cRZMutexData size");
static_assert(offsetof(cRZMutexData, mName) == 0x08, "cRZMutexData layout");

class cRZMutex {
public:
    static constexpr uint32_t kVTable = 0xe9ebf0;
    static constexpr uint32_t kSize = 8;
    static constexpr int kSlotDeletingDestructor = 5;
    // The cString calls the constructor makes through vtables.
    static constexpr int kNameSlotAssign = 16;   // cRZString::operator=(const cIGZString&) on the copy
    // The Windows calls (imports in the game): CreateMutexA 0xd9a3ec, WaitForSingleObject 0xd9a184,
    // ReleaseMutex 0xd9a3f0 and CloseHandle 0xd9a17c are the import-table slots they go through.

    cRZMutex *Construct(const cIGZString &name);        // 0xc6c85e: creates the named mutex
    void Destruct();                                    // 0xc6c8c3: closes the handle, frees the data
    cRZMutex *DeletingDestructor(uint32_t flags);       // slot 5, 0xc6c8e0

    uint32_t Delete();                                  // slot 0, 0x9ccb85 (shared with another class): deletes itself through slot 5
    uint32_t Lock();                                    // slot 1, 0xc6c906: wait forever; false only if the wait failed
    uint32_t Unlock();                                  // slot 2, 0xc6c93e: ReleaseMutex
    uint32_t TryLock();                                 // slot 3, 0xc6c91e: wait 0; true when acquired (also abandoned)
    uint32_t IsValid();                                 // slot 4, 0xc6c8ff: the data's valid flag
    uint32_t GetHandle(void **out);                     // slot 6, 0x9ccb69 (shared with another class): stores the handle, returns 1

    const void *mVTable;   // +0x00
    cRZMutexData *mData;   // +0x04
};

static_assert(sizeof(cRZMutex) == cRZMutex::kSize, "cRZMutex size");
