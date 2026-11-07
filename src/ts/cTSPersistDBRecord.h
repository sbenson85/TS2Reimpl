#pragma once
#include "rz/cRZSerializableStream.h"

#include <cstddef>
#include <cstdint>

// cTSPersistDBRecord: a persistence record (derives from cRZSerializableStream). RVAs are 1.18.0.187.
// Subobjects of the primary object (offsets from the primary object at +0):
//   +0x000  primary vtable 0xda6b20 (32 slots); its reimplemented slots are members of cTSPersistDBRecord.
//   +0x038  cITSPersistDBRecord, vtable 0xda6c44 (28 slots); members of cTSPersistDBRecordITS.
//   +0x03c  cIGZPersistDBRecord, vtable 0xda6cb8 (19 slots); members of cTSPersistDBRecordIGZ.
//
// Fields read or written by the functions reimplemented here (primary-object offsets):
//   +0x024  dword, read by vf56_15 (this - 0x14 with this = +0x38).
//   +0x02c  dword, written by vf56_16 and read by vf56_17 (this - 0xc with this = +0x38).
//   +0x04c  dword, written by vf30 (this + 0x4c).
//   +0x05c  pointer to a secondary object, vf60_15 (this + 0x20 with this = +0x3c); its slot 0 is called.
//   +0x060  flag byte, tested or changed by vf60_17 and vf60_18 (this + 0x24 with this = +0x3c).
//
// vf56_5 and vf56_6 compare `this` with 0x38: that is what a null primary object gives after the
// subobject offset is applied, and the original then returns null instead of the adjusted pointer.

class cTSPersistDBRecord {
public:
    // Slot 30 of the primary vtable, 0x1f0e50 (ret 4): stores the argument at +0x4c. Returns the
    // argument with its low byte replaced by 1 (the original loads eax, then sets al = 1).
    uint32_t vf30(uint32_t a);
};

// The cITSPersistDBRecord subobject (this = primary + 0x38). Slot numbers are the interface's.
class cTSPersistDBRecordITS {
public:
    // Slot 5, 0x1f08a0: returns this - 0x18, or null when this is 0x38.
    void *vf56_5();
    // Slot 6, 0x1f0890: returns this + 4, or null when this is 0x38.
    void *vf56_6();
    // Slot 15, 0x1f0c20: calls its own slot 12 (no arguments), then returns the dword at this - 0x14
    // minus that result. The field is read after the call.
    uint32_t vf56_15();
    // Slot 16, 0x1f0ed0 (ret 4): adds the argument to the result of its own slot 12, stores the sum at
    // this - 0xc and returns it.
    uint32_t vf56_16(uint32_t a);
    // Slot 17, 0x1f0ab0: returns the dword at this - 0xc minus the result of its own slot 12.
    uint32_t vf56_17();
};

// The cIGZPersistDBRecord subobject (this = primary + 0x3c). Slot numbers are the interface's.
class cTSPersistDBRecordIGZ {
public:
    // Slot 13, 0x1f0df0 (ret 4): calls the cITSPersistDBRecord subobject's slot 16 (this - 4) with the
    // argument. Returns al = 1; the rest of eax is the callee's result.
    uint32_t vf60_13(uint32_t a);
    // Slot 14, 0x1f0e10 (ret 4): calls its own slot 12 (no arguments), adds the argument to the result,
    // then calls its own slot 13 with the sum. Returns al = 1; the rest of eax is slot 13's result.
    uint32_t vf60_14(uint32_t a);
    // Slot 15, 0x1f0a70 (ret 8): if the second argument is null returns 0. Otherwise, if the object at
    // this + 0x20 is null, writes 0 to the dword the second argument points at and returns 0. Otherwise
    // tail-calls that object's slot 0 with both arguments and returns its result. The null-path returns
    // set only al in the original (upper eax bits are whatever the caller left there); this returns 0.
    uint32_t vf60_15(uint32_t a1, uint32_t a2);
    // Slot 17, 0x1f0bf0 (ret 4): al = ((byte at this + 0x24) & mask) == mask.
    bool vf60_17(uint8_t mask);
    // Slot 18, 0x1f0fa0 (ret 8): if `set` is nonzero, ORs `bits` into the byte at this + 0x24; otherwise
    // clears those bits.
    void vf60_18(uint8_t bits, uint8_t set);
};

// ---- second batch -------------------------------------------------------------------------------
// Fields (primary-object offsets) established by this batch, from the record's own members:
//   +0x024  size of the data (bytes), +0x028 capacity, +0x02c position, +0x034 data pointer (freed by
//           the base class's vf23), +0x030 error flag byte (slot 24 of the primary vtable sets it, the
//           GetError thunks read it).
//   +0x044  byte set by cITSPersistDBRecord slot 26.
//   +0x048..+0x057  16-byte key (GetKey / SetKey), whose dwords +0x48, +0x4c and +0x50 are also set by
//           primary slots 29, 30 and 31.
//   +0x058  cRZString* cache (cITSPersistDBRecord slots 22 and 23), reference counted.
//   +0x05c  secondary object (cIGZPersistDBRecord slot 16 and vf60_15), reference counted.

// The cRZIStream subobject (this = primary + 0x10).
class cTSPersistDBRecordIS {
public:
    // Slot 18, 0x1f0a33 (GetError thunk): al = the byte at primary + 0x30. Only al is set.
    uint8_t vf16_18();
};

// The cIGZSerializableStream subobject (this = primary + 0x20).
class cTSPersistDBRecordSS {
public:
    // Slot 7, 0x1f0860 (shared with cRZSerializableStream): this - 0x10, or null when this is 0x20.
    void *vf32_7();
    // Slot 10, 0x1f0a10: calls the cITSPersistDBRecord subobject's slot 12 (this + 0x18), then returns
    // the dword at this + 0x14 plus its result. The field is read after the call.
    uint32_t vf32_10();
    // Slot 11, 0x1f09e0: the same call, then returns the dword at this + 4 minus its result.
    uint32_t vf32_11();
    // Slot 13, 0x1f0a3b (GetError thunk): al = the byte at primary + 0x30.
    uint8_t vf32_13();
};

// Slots of the primary vtable not covered above.
class cTSPersistDBRecordSlots {
public:
    // Slot 29, 0x1f0f90 (ret 4, shared_vf, not hooked): stores the argument at +0x48; same return as vf30.
    uint32_t vf29(uint32_t a);
    // Slot 31, 0x1f0e60 (ret 4, shared_vf, not hooked): stores the argument at +0x50; same return as vf30.
    uint32_t vf31(uint32_t a);
};

// More members of the cITSPersistDBRecord subobject (this = primary + 0x38).
class cTSPersistDBRecordITS2 {
public:
    // Slot 1, 0x1f0805: the primary's reference count (+4) incremented; returns the new count.
    uint32_t AddRef();
    // Slot 3, 0x1f0884: the primary object (this - 0x38).
    void *vf56_3();
    // Slot 4, 0x1f086c: the cIGZSerializableStream subobject's slot 7 applied to this - 0x18.
    void *vf56_4();
    // Slot 9, 0x1f0a2b: tail-jumps to cIGZSerializableStream slot 10's function with this - 0x18.
    uint32_t vf56_9();
    // Slot 10, 0x1f09fb: the same for slot 11's function.
    uint32_t vf56_10();
    // Slot 11, 0x1f0a60: the dword at this - 4 (the data pointer; the string-length getter 0x149d00
    // applied to this - 0x18).
    uint32_t vf56_11();
    // Slot 20, 0x1f0cd0 (ret 4): reads a NUL-terminated string at data + position into a local
    // cRZString, advances the position past it (and its NUL), and hands the string to the argument's
    // slot 17. Returns false (after setting the primary's error flag through its slot 24) when the new
    // position passes the data size. Only al is meaningful in the original's return.
    bool vf56_20(void *outString);
    // Slot 22, 0x1f0ef0 (ret 4): replaces the cache at +0x58 with a new cRZString copied from the
    // argument (its slot 6 data, slot 7 length). Always returns true (al = 1).
    bool vf56_22(void *srcString);
    // Slot 23, 0x1f0ad0 (ret 4): fills the +0x58 cache from the record's data when it is empty, then
    // passes the cache to the argument's slot 17. Returns whether a cache exists afterwards.
    bool vf56_23(void *outString);
    // Slot 25, 0x1f0e70 (ret 4, shared_vf, not hooked): copies the 16 bytes at `key` to this + 0x10.
    void vf56_25(const void *key);
    // Slot 26, 0x1f0e90 (ret 4, shared_vf, not hooked): stores the low byte of the argument at this + 0xc.
    void vf56_26(uint32_t a);
};

// More members of the cIGZPersistDBRecord subobject (this = primary + 0x3c).
class cTSPersistDBRecordIGZ2 {
public:
    // Slot 1, 0x1f080d: the primary's reference count incremented; returns the new count.
    uint32_t AddRef();
    // Slot 3, 0x1f0a68: pointer to the 16-byte key (this + 0xc).
    void *GetKey();
    // Slot 4, 0x1f0e7e (ret 4): copies the 16 bytes at `key` to this + 0xc.
    void SetKey(const void *key);
    // Slots 6 and 7, 0x1f07e0: tail-jumps to the primary's slot 23 (no arguments) with this - 0x3c.
    uint32_t Close();
    // Slot 10, 0x1f0be0: the cIGZSerializableStream subobject's slot 11 (this - 0x1c), no arguments.
    uint32_t vf60_10();
    // Slot 12, 0x1f0aa0: the cITSPersistDBRecord subobject's slot 17 (this - 4), no arguments.
    uint32_t vf60_12();
    // Slot 16, 0x1f0ea0 (ret 4, shared_vf, not hooked): replaces the secondary object at this + 0x20
    // with the argument: unchanged when they are equal, else AddRef (slot 1) the new one if non-null,
    // store it, then Release (slot 2) the old one if non-null.
    void vf60_16(void *obj);
};

// ---- third batch: the object, constructor, destructor and the thunks into cRZSerializableStream ----------

// The whole record: 0x64 bytes, the stream (0x38 bytes) followed by the record's own fields.
//   +0x38  cITSPersistDBRecord vtable   +0x3c  cIGZPersistDBRecord vtable
//   +0x40  dword, cleared by the constructor (not touched elsewhere in the class)
//   +0x44  byte (cITSPersistDBRecord slot 26)
//   +0x48  16-byte key
//   +0x58  cRZString* cache (reference counted)   +0x5c  secondary object (reference counted)
//   +0x60  flag byte
// The record is allocated with operator new(0x64) (the factory 0x1f0900 and the deleting destructor).
struct cTSPersistDBRecordObject : cRZSerializableStream {
    static constexpr uint32_t kSize = 0x64;
    static constexpr uint32_t kVTableRecord = 0xda6b20;
    static constexpr uint32_t kVTableIS = 0xda6ba4;
    static constexpr uint32_t kVTableSS = 0xda6c08;
    static constexpr uint32_t kVTableITS = 0xda6c44;
    static constexpr uint32_t kVTableIGZ = 0xda6cb8;
    static constexpr uint32_t kIdITS = 0x3b2271c0; // cITSPersistDBRecord
    static constexpr uint32_t kIdIGZ = 0xe56b8f03; // cIGZPersistDBRecord

    const void *mVTableITS;  // +0x38
    const void *mVTableIGZ;  // +0x3c
    uint32_t m40;            // +0x40
    uint8_t m44;             // +0x44
    uint8_t mPad45[3];
    uint32_t mKey[4];        // +0x48
    void *mCache;            // +0x58
    void *mSecondary;        // +0x5c
    uint8_t mFlags;          // +0x60
    uint8_t mPad61[3];

    // 0x1f0690 (ret 4): the stream constructor with `order`, then the five vtables and the record
    // fields cleared. Returns this.
    cTSPersistDBRecordObject *Construct(uint32_t order);
    // Slot 21, 0x1f0740 (ret 4): installs the vtables, releases the secondary object (+0x5c) then the
    // cache (+0x58) through their slot 2, runs the stream's Destruct and, with bit 0 of flags, operator
    // delete (0x64 bytes). Returns this.
    cTSPersistDBRecordObject *DeletingDestructor(uint32_t flags);
    // 0x1f0900: operator new(0x64) and Construct(1); returns the cITSPersistDBRecord subobject
    // (object + 0x38), or null when the allocation fails.
    static void *Create();
    // Slot 0, 0x1f0c60 (ret 8): the ids of cITSPersistDBRecord and cIGZPersistDBRecord give the +0x38 and
    // +0x3c subobjects (null stays null) and add a reference through slot 1; anything else goes to the
    // stream's QueryInterface. Returns true (al = 1) for the two ids.
    bool QueryInterface(uint32_t riid, void **out);
    // Slot 1, 0x1f07f0 (shared_vf, not hooked): ++refcount, returns it.
    uint32_t AddRef();
    // Slots 2/17/22/23: jumps into the stream's members (0x1f0dc0, 0x1f0fd0, 0x1f0c40, 0x1f1010).
    uint32_t Release();
    bool SetVoid(const void *src, uint32_t n);
    bool SetByteOrder(uint32_t order);
    bool Reset();
};

static_assert(sizeof(cTSPersistDBRecordObject) == 0x64, "cTSPersistDBRecord is 0x64 bytes");
static_assert(offsetof(cTSPersistDBRecordObject, mVTableITS) == 0x38, "ITS vtable at +0x38");
static_assert(offsetof(cTSPersistDBRecordObject, mKey) == 0x48, "key at +0x48");
static_assert(offsetof(cTSPersistDBRecordObject, mCache) == 0x58, "cache at +0x58");
static_assert(offsetof(cTSPersistDBRecordObject, mSecondary) == 0x5c, "secondary at +0x5c");
static_assert(offsetof(cTSPersistDBRecordObject, mFlags) == 0x60, "flags at +0x60");

// The cRZIStream subobject (this = primary + 0x10): thunks that apply -0x10 and run the primary's member.
class cTSPersistDBRecordIS2 {
public:
    bool QueryInterface(uint32_t riid, void **out);   // 0x1f0c9d
    uint32_t AddRef();                                  // 0x1f07f5 (shared_vf, not hooked)
    uint32_t Release();                                 // 0x1f0dc5
    bool GetVoid(void *dst, uint32_t n);                // 0x1f0c10: the stream's GetVoid, this unchanged
    cTSPersistDBRecordObject *vf16_21(uint32_t flags);  // 0x1f0733
    bool vf16_22(uint32_t order);                       // 0x1f0c45
    bool vf16_23();                                     // 0x1f1015
};

// The cIGZSerializableStream subobject (this = primary + 0x20).
class cTSPersistDBRecordSS2 {
public:
    bool QueryInterface(uint32_t riid, void **out);   // 0x1f0ca5
    uint32_t AddRef();                                  // 0x1f07fd (shared_vf, not hooked)
    uint32_t Release();                                 // 0x1f0dcd
    void *vf32_6();                                     // 0x1f0880 (shared_vf, not hooked): this - 0x20
    bool Write(void *out);                              // 0x1f1030: the stream's Write, this unchanged
    bool Read(void *in);                                // 0x1f0cc0: the stream's Read, this unchanged
    bool vf32_8(uint32_t order);                        // 0x1f0c4d
    bool vf32_9();                                      // 0x1f101d
};

// More members of the cITSPersistDBRecord subobject (this = primary + 0x38).
class cTSPersistDBRecordITS3 {
public:
    bool QueryInterface(uint32_t riid, void **out);   // 0x1f0cad
    uint32_t Release();                                 // 0x1f0dd5
    bool vf56_7(uint32_t order);                        // 0x1f0c55
    bool vf56_8();                                      // 0x1f1025
    bool vf56_18(const void *src, uint32_t n);          // 0x1f0fd5
    // Slot 14, 0x1f0fe0 (ret 4): n = own slot 12 + the argument; when n is above the capacity (+0x28)
    // the stream's Reserve(n) runs; then the size (+0x24) is set to n. No meaningful return (the
    // original's eax is the slot-12 result or Reserve's).
    void vf56_14(uint32_t a);
    // Slot 19, 0x1f08b0 (ret 8): Grow(count), fills `count` bytes of the data at the size with the byte
    // value, adds count to the size, and returns !GetError() (primary slot 18).
    bool vf56_19(uint8_t value, uint32_t count);
    // Slot 21, 0x1f0820 (ret 4): Reserve(own slot 12 + the argument) on the primary; when that succeeds
    // the size (+0x24) is set to own slot 12 (called again) + the argument. Returns Reserve's result.
    bool vf56_21(uint32_t a);
};

// More members of the cIGZPersistDBRecord subobject (this = primary + 0x3c).
class cTSPersistDBRecordIGZ3 {
public:
    bool QueryInterface(uint32_t riid, void **out);   // 0x1f0cb5
    uint32_t Release();                                 // 0x1f0ddd
    bool vf60_8(void *dst, uint32_t n);                 // 0x1f0a50: the stream's GetVoid on this - 0x2c
    bool vf60_9(const void *src, uint32_t n);           // 0x1f0e40: SetVoid on this - 0x3c
};
