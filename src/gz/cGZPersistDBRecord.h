#pragma once
#include "game/owned.h"

#include <cstddef>
#include <cstdint>

// cGZPersistDBRecord: the base record of the resource database. It is the +4 subobject of
// cGZPersistDBSerialRecord, and is also used on its own. Standalone vtable 0xe5e990 (21 slots;
// cIGZPersistDBRecord and cIGZUnknown at +0). The object is 0x1c bytes; its deleting destructor
// frees it with operator delete(p, 0x1c).
//
// Layout: +0 vtable; +4 uint16 reference count (as a serial record subobject, this is the serial
// record's count at +8); +6 uint16 flags, whose low byte is the access flags (GetAccessFlags,
// SetAccessFlags) and whose high byte is tested and changed by TestHighFlags and SetHighFlags; +8 a
// 16-byte key; +0x18 an optional child (an interface pointer whose reference the record holds).
// RVAs are 1.18.0.187.
class cGZPersistDBRecord {
public:
    static constexpr uint32_t kVTable = 0xe5e990;   // the game's table; VTable() is its owned symbol
    static const void *VTable();                    // the word our constructors and destructors store
    static const void *OwnVTable();                 // rzface::cGZPersistDBRecord's: the image-free program's VTable()
    static constexpr uint32_t kSize = 0x1c;
    static constexpr int kDeleteSlot = 19;      // deleting destructor (0x99b960)
    static constexpr int kCloseChildSlot = 24;  // child slot called by Close (0x60)
    static constexpr int kAbortChildSlot = 25;  // child slot called by Abort (0x64)

    // Interface ids QueryInterface accepts (compared directly, no subtraction).
    static constexpr uint32_t kIdUnknown = 1;         // cIGZUnknown
    static constexpr uint32_t kIdRecord = 0xe56b8f03; // cIGZPersistDBRecord

    bool QueryInterface(uint32_t riid, void **ppvObj); // slot 0; 0x99cf40: AddRef through slot 1 on success
    uint32_t AddRef();                                 // slot 1; 0x469a20 (folded; not hooked): 16-bit increment, returns the count
    uint32_t Release();                                // slot 2; 0x99d050: at count <= 1, deletes via slot 19 (flags 1)
    void SetKey(const void *key);                      // slot 4; 0x1e4dd0: copies 16 bytes to +8
    uint32_t GetAccessFlags();                         // slot 5; 0x1df630: low byte of the flags word
    bool Close();                                      // slot 6; 0x99ba60: child slot 24, then releases the child
    bool Abort();                                      // slot 7; 0x99b9e0: child slot 25, then releases the child
    bool QueryChild(uint32_t riid, void **ppvObj);     // slot 15; 0x1e03f0: child slot 0 (QueryInterface), or false
    void SetChild(void *child);                        // slot 16; 0x1e5060: AddRef the new child, release the old
    bool TestHighFlags(uint8_t mask);                  // slot 17; 0x1e0960: (high byte & mask) == mask
    uint32_t SetHighFlags(uint8_t value, uint8_t set); // slot 18; 0x1e5380: returns the new flags word
    void *DeletingDestructor(uint32_t flags);          // slot 19; 0x99b960: flags & 1 frees the object
    uint32_t SetAccessFlags(uint8_t value);            // slot 20; 0x1e4b20: replaces the low byte; returns the word

    // The non-deleting part of the destructor: writes this object's vtable and releases the child. The
    // child pointer itself is left as it is.
    void Destruct();

    const void *mVTable; // +0
    uint16_t mRefCount;  // +4
    uint16_t mFlags;     // +6
    uint8_t mKey[16];    // +8
    void *mChild;        // +0x18
};

// The vtable (owned.h, OwnedVTable): `offset ts2_rva_e5e990`, the same one-instruction store as GameAddress(kVTable).
TS2_OWNED_STATIC(const OwnedVTable, e5e990);
inline const void *cGZPersistDBRecord::VTable() { return &ts2_rva_e5e990; }

static_assert(offsetof(cGZPersistDBRecord, mVTable) == 0,"cGZPersistDBRecord: vtable at +0");
static_assert(offsetof(cGZPersistDBRecord, mRefCount) == 4, "cGZPersistDBRecord: count at +4");
static_assert(offsetof(cGZPersistDBRecord, mFlags) == 6, "cGZPersistDBRecord: flags at +6");
static_assert(offsetof(cGZPersistDBRecord, mKey) == 8, "cGZPersistDBRecord: key at +8");
static_assert(offsetof(cGZPersistDBRecord, mChild) == 0x18, "cGZPersistDBRecord: child at +0x18");
static_assert(sizeof(cGZPersistDBRecord) == cGZPersistDBRecord::kSize, "cGZPersistDBRecord: 0x1c bytes");
