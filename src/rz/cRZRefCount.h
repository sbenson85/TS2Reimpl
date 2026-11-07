#pragma once
#include <cstdint>

#include "game/owned.h"

// Reference-counting base classes: a vtable pointer and a count at +4. cRZUnknown implements
// cIGZUnknown (243 game classes derive from it); cRZRefCount is a lighter base without
// QueryInterface. The MT variants update the count atomically. Objects delete themselves through
// their own vtable when released. RVAs are 1.18.0.187.

// Atomically adds delta to *value and returns the new value. 0x1bdbad (__cdecl).
int32_t __cdecl RZAtomicAdd(volatile int32_t *value, int32_t delta);

class cRZUnknown {
public:
    static constexpr uint32_t kVTable = 0xda2fd0;
    uint32_t Release();                                 // slot 2, 0x1b94e9: deletes via slot 3
    void Destruct();                                    // 0x1b9096
    cRZUnknown *DeletingDestructor(uint32_t flags);     // slot 3, 0x1b90f0
    const void *mVTable;
    uint32_t mRefCount;
};

class cRZRefCount {
public:
    static constexpr uint32_t kVTable = 0xda2fa8;
    uint32_t Release();                                 // slot 1, 0x1b94af: deletes via slot 0
    void Destruct();                                    // 0x1b9088
    cRZRefCount *DeletingDestructor(uint32_t flags);    // slot 0, 0x1b90b2
    // The folded slots of the table (shared with other classes, never hooked; rz/cRZRefCountTable.cpp):
    uint32_t AddRef();                                  // slot 2, 0x148fb0: the count plus one, returned
    uint32_t RemoveRef();                               // slot 3, 0x1b9538: the count minus one unless it is 0, returned
    uint32_t RefCount() const;                          // slot 4, 0x147a30: the count
    const void *mVTable;
    uint32_t mRefCount;
};

class cRZUnknownMT {
public:
    static constexpr uint32_t kVTable = 0xda2fec;
    uint32_t AddRef();                                  // slot 1, 0x1b914d: atomic add of one, returns the new count
    uint32_t Release();                                 // slot 2, 0x1b9517: deletes via slot 3
    uint32_t RemoveRef();                             // slot 4, 0x1b9544: decrement without deleting
    void Destruct();                                    // 0x1b90ab
    cRZUnknownMT *DeletingDestructor(uint32_t flags);   // slot 3, 0x1b912e
    const void *mVTable;
    volatile int32_t mRefCount;
};

class cRZRefCountMT {
public:
    static constexpr uint32_t kVTable = 0xda2fc0;
    uint32_t AddRef();                                  // slot 1, 0x1b914d: atomic add of one, returns the new count
    uint32_t Release();                                 // slot 2, 0x1b94c5: deletes via slot 0
    void Destruct();                                    // 0x1b908f
    cRZRefCountMT *DeletingDestructor(uint32_t flags);  // slot 0, 0x1b90d1
    const void *mVTable;
    volatile int32_t mRefCount;
};

// The word cRZRefCount's destructor stores (lane RW): the image's table in a hooked build; an image-free program's is
// ours (rz/cRZRefCountTable.cpp; standalone/owned_statics.cpp aliases it).
TS2_OWNED_STATIC(const OwnedVTable, da2fa8);
extern "C" const void *ts2_own_vt_da2fa8[5];

static_assert(sizeof(cRZUnknown) == 8 && sizeof(cRZRefCount) == 8, "refcount base layout");
static_assert(sizeof(cRZUnknownMT) == 8 && sizeof(cRZRefCountMT) == 8, "refcount base layout");
