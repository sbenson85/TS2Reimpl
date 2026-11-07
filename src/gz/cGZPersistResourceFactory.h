#pragma once
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>

// cGZPersistResourceFactory: base class of the persist resource factories (cGZPersistBufferResourceFactory,
// cGZPersistBinaryResourceFactory, cSLTokenResourceFactory, ...). Implements cIGZPersistResourceFactory
// (vtable 0xe603e4) and cIGZUnknown at +0. Every public operation holds the lock at +8 (a cRZCriticalSection
// used through its vtable: slot 1 locks, slot 2 unlocks). RVAs are 1.18.0.187.
//
// Slots 9..11 are pure in this class (subclasses supply them):
//   slot 9  (0x24): bool Create(uint32_t typeId, uint32_t riid, void **ppv)
//   slot 10 (0x28): bool ReadRecord(void *resource, void *serialRecord)
//   slot 11 (0x2c): bool WriteRecord(void *resource, void *serialRecord)
// Slot 4 is the 5-argument CreateInstance, whose default body (CreateInstance2) forwards the first three
// arguments to slot 9. The first parameter of CreateInstance is a record whose slot 3 returns a pointer to
// its resource key (the type id is the first dword).
class cGZPersistResourceFactory {
public:
    static constexpr uint32_t kVTable = 0xe603e4;
    static constexpr int kCreateSlot = 9;
    static constexpr int kReadRecordSlot = 10;
    static constexpr int kWriteRecordSlot = 11;

    static constexpr uint32_t kIdFactory = 0xa56b8f17;       // cIGZPersistResourceFactory, compared directly
    static constexpr uint32_t kIdResourceIface = 0x456b8f1d; // asked of each created resource
    static constexpr uint32_t kIdSerialRecord = 0xc56b8f08;  // cIGZPersistDBSerialRecord

    cGZPersistResourceFactory *Construct();                        // 0x9b6010
    void Destruct();                                               // 0x9b6040 (base destructor body)
    cGZPersistResourceFactory *DeletingDestructor(uint32_t flags); // 0x9b6080, slot 7
    bool QueryInterface(uint32_t riid, void **ppvObj);             // 0x9b61d0, slot 0
    uint32_t AddRef();                                             // 0x9b60b0, slot 1
    uint32_t Release();                                            // 0x9b6290, slot 2
    bool CreateInstance(void *record, uint32_t riid, void **ppvObj, uint32_t arg4, uint32_t arg5);     // 0x9b60e0, slot 3
    bool CreateInstance2(uint32_t typeId, uint32_t riid, void **ppvObj, uint32_t arg4, uint32_t arg5); // 0x9b61b0, slot 4
    bool Read(void *resource, void *record);                       // 0x9b6200, slot 5
    bool Write(void *resource, void *record);                      // 0x9b62d0, slot 6

    const void *mVTable;      // +0
    uint32_t mRefCount;       // +4
    cRZCriticalSection mLock; // +8
};

static_assert(sizeof(cGZPersistResourceFactory) == 0x10, "cGZPersistResourceFactory layout");
static_assert(offsetof(cGZPersistResourceFactory, mLock) == 8, "cGZPersistResourceFactory layout");
