#pragma once
#include "game/owned.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>

// cGZPersistDBSegment: the base of the database segments (packed files, directories) that make up the
// game's resource database. Primary vtable 0xe5e8c4 (cIGZPersistDBSegment and cIGZUnknown at +0, 50
// slots). RVAs are 1.18.0.187.
//
// Object: 0x18 bytes. Constructor 0x99b790 writes the vtable, zeroes +4, +0x10 and +0x14, and constructs
// the lock at +8. Deleting destructor vf33 (0x99b990) releases +0x14 and +0x10 through their slot 2, then
// destroys the lock and frees 0x18 bytes when its flag has bit 0 set. Shutdown (below) does the releases
// without the destructor's trailing work.
//
// Field map:
//   +0x00  vptr (0xe5e8c4)
//   +0x04  int32 reference count, updated with RZAtomicAdd (0x1bdbad)
//   +0x08  cRZCriticalSection (8 bytes: vptr 0xda2708, Block*). Lock is its slot 1 and Unlock its slot 2,
//          called with the lock's address (this + 8) as `this`. Locked forwarders take it around the call.
//   +0x10  segment cache object (refcounted; type not identified yet). SetSegmentCache swaps it.
//   +0x14  namespace object (refcounted). vf49 hands it out with AddRef; the inner setter for slot 48
//          (shared with other classes, 0x4b8e40) swaps it in the same way as SetSegmentCache.
//
// Slots that forward to a pure virtual of this class (35..49, implemented by the subclasses) are called
// through this object's vtable with the same arguments and in the same order as the original.

class cGZPersistDBSegment {
public:
    static constexpr uint32_t kVTable = 0xe5e8c4; // the game's; VTable() is its owned symbol
    static const void *VTable();                  // the word our Construct and Destruct store
    static const void *OwnVTable();               // rzface::cGZPersistDBSegment's: the image-free program's VTable()
    static constexpr uint32_t kIdSegment = 0x656b8f0c; // accepted by QueryInterface, with 1 (cIGZUnknown)

    cGZPersistDBSegment *Construct();                   // constructor, 0x99b790 (returns this)
    void Destruct();                                    // destructor body, 0x99b8e0 (the derived classes' base call)
    cGZPersistDBSegment *DeletingDestructor(uint32_t flags); // slot 33, 0x99b990: Destruct, then frees 0x18 bytes if flags & 1
    uint32_t AddRef();                                  // slot 1, 0x99ba50 (folded with one other class): +4 count + 1
    void SetNamespaceInner(void *ns);                   // slot 48, 0x4b8e40 (folded, 10 classes): swaps +0x14
    bool QueryInterface(uint32_t riid, void **ppvObj); // slot 0, 0x99cf70: id 1 or kIdSegment
    uint32_t Release();                                 // slot 2, 0x99d070: at zero, deletes via slot 34
    bool Shutdown();                                    // slot 4, 0x99de70: releases +0x14, then +0x10
    bool Lock();                                        // slot 13, 0x99cef0
    bool Unlock();                                      // slot 14, 0x99def0
    void SetSegmentCache(void *cache);                  // slot 17, 0x1d4540: swaps +0x10 (AddRef new, Release old)
    uint32_t GetRecordCount(uint32_t a);                // slot 18, 0x99ce10: under lock, returns slot 44(a)
    uint32_t GetRecordSize(uint32_t a);                 // slot 21, 0x99ce40: under lock, returns slot 36(a)
    uint32_t GetSegmentChangeID();                      // slot 29, 0x99ceb0: under lock, returns slot 46()
    bool GetRecordChangeID(uint32_t key, uint32_t *changeId); // slot 30, 0x99cdd0: under lock, slot 47(key, changeId)
    void SetNamespace(void *ns);                        // slot 31, 0x99dd70: under lock, slot 48(ns)
    bool GetNamespace(void **out);                      // slot 32, 0x99cd90: under lock, slot 49(out)
    uint32_t GetResourceKeyList(uint32_t a, uint32_t b); // slot 19, 0x99ce70: under lock, returns slot 45(a, b)
    bool TestForRecord(uint32_t a);                     // slot 20, 0x99deb0: under lock, slot 35(a)
    bool AbortRecord(uint32_t a);                       // slot 25, 0x99ba10: under lock, slot 40(a)
    bool CloseRecord(uint32_t a);                       // slot 24, 0x99ba90: under lock, slot 39(a)
    bool CreateNewRecord(uint32_t a, uint32_t b);       // slot 23, 0x99bad0: under lock, slot 38(a, b)
    bool DeleteRecord(uint32_t a);                      // slot 26, 0x99bb10: under lock, slot 41(a)
    bool OpenRecord(uint32_t a, uint32_t b, uint32_t c); // slot 22, 0x99cf00: under lock, slot 37(a, b, c)
    uint32_t ReadRecord(uint32_t a, uint32_t b, uint32_t c); // slot 27, 0x99d010: under lock, returns slot 42(a, b, c)
    bool WriteRecord(uint32_t a, uint32_t b, uint32_t c); // slot 28, 0x99df00: under lock, slot 43(a, b, c)
    void vf34();                                        // slot 34, 0x99bba0: if non-null, this->slot33(1)
    bool vf47(uint32_t key, uint32_t *changeId);        // slot 47, 0x99bb70: slot 35(key), then *changeId = 0
    bool vf49(void **out);                              // slot 49, 0x99bb50: *out = +0x14 with AddRef

    const void *mVTable;               // +0x00
    int32_t mRefCount;                 // +0x04
    cRZCriticalSection mLock;          // +0x08
    void *mSegmentCache;               // +0x10
    void *mNamespace;                  // +0x14
};

static_assert(sizeof(cGZPersistDBSegment) == 0x18, "cGZPersistDBSegment: 24 bytes");
static_assert(offsetof(cGZPersistDBSegment, mRefCount) == 4, "cGZPersistDBSegment: refcount at +4");
static_assert(offsetof(cGZPersistDBSegment, mLock) == 8, "cGZPersistDBSegment: lock at +8");
static_assert(offsetof(cGZPersistDBSegment, mSegmentCache) == 0x10, "cGZPersistDBSegment: cache at +0x10");
static_assert(offsetof(cGZPersistDBSegment, mNamespace) == 0x14, "cGZPersistDBSegment: namespace at +0x14");

// The vtable (owned.h, OwnedVTable): `offset ts2_rva_e5e8c4`, the same one-instruction store as GameAddress(kVTable).
TS2_OWNED_STATIC(const OwnedVTable, e5e8c4);

namespace rzface {

// A slot that is pure in the game's table (the derived segments implement it). Our table gives it a body that ends the
// program the way the game's _purecall stub does; nothing calls it.
[[noreturn]] void PureSlot();

// cGZPersistDBSegment's own vtable (the image-free program's), over the hook-shaped class: `this` is the segment
// (cGZDBSegmentPackedFile + 4 in a packed file). Each implemented slot runs the member of the same name
// (src/gz/cGZPersistDBSegment.cpp); the pure ones (3, 5..12, 15, 16, 35..45) end in PureSlot. Namespaced so that its RTTI
// name is still cGZPersistDBSegment (tools/matchcheck.py ties our vtable to the game's 0xe5e8c4).
class cGZPersistDBSegment final {
public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj); // 0
    virtual uint32_t AddRef();                                 // 1
    virtual uint32_t Release();                                // 2
    virtual void Pure3() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual bool Shutdown(); // 4
    virtual void Pure5(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure6(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure7() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure8() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure9() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure10() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure11(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure12(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual bool Lock();   // 13
    virtual bool Unlock(); // 14
    virtual void Pure15() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure16(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void SetSegmentCache(void *cache);                       // 17
    virtual uint32_t GetRecordCount(uint32_t a);                     // 18
    virtual uint32_t GetResourceKeyList(uint32_t a, uint32_t b);     // 19
    virtual bool TestForRecord(uint32_t a);                          // 20
    virtual uint32_t GetRecordSize(uint32_t a);                      // 21
    virtual bool OpenRecord(uint32_t a, uint32_t b, uint32_t c);     // 22
    virtual bool CreateNewRecord(uint32_t a, uint32_t b);            // 23
    virtual bool CloseRecord(uint32_t a);                            // 24
    virtual bool AbortRecord(uint32_t a);                            // 25
    virtual bool DeleteRecord(uint32_t a);                           // 26
    virtual uint32_t ReadRecord(uint32_t a, uint32_t b, uint32_t c); // 27
    virtual bool WriteRecord(uint32_t a, uint32_t b, uint32_t c);    // 28
    virtual uint32_t GetSegmentChangeID();                           // 29
    virtual bool GetRecordChangeID(uint32_t key, uint32_t *changeId); // 30
    virtual void SetNamespace(void *ns);                             // 31
    virtual bool GetNamespace(void **out);                           // 32
    virtual void *DeletingDestructor(uint32_t flags);                // 33
    virtual void Delete();                                           // 34
    virtual void Pure35(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure36(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure37(uint32_t, uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure38(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure39(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure40(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure41(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure42(uint32_t, uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure43(uint32_t, uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure44(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure45(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual uint32_t Zero46();                                       // 46: returns 0
    virtual bool GetRecordChangeIDInner(uint32_t key, uint32_t *changeId); // 47
    virtual void SetNamespaceInner(void *ns);                        // 48
    virtual bool GetNamespaceInner(void **out);                      // 49
};

} // namespace rzface

inline const void *cGZPersistDBSegment::VTable() { return &ts2_rva_e5e8c4; }

// The address of rzface::cGZPersistDBSegment's vtable as a link-time constant (the compiler folds the local face's vptr;
// no object is built).
inline const void *cGZPersistDBSegment::OwnVTable()
{
    rzface::cGZPersistDBSegment face;
    return *reinterpret_cast<const void *const *>(&face);
}
