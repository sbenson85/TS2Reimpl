#pragma once
#include "game/owned.h"

#include <cstddef>
#include <cstdint>

class cIGZString;

// Binary output stream base (cRZOStream, implementing cIGZOStream). The setters convert one value
// and pass it to the raw write, slot 17 (`call [eax+0x44]`), which subclasses implement (it is
// _purecall in this base). The raw write's return value is passed back unchanged, so the setters
// return the full 32-bit eax. The object is 0x10 bytes; subclasses add their fields after it.
// RVAs are 1.18.0.187.
class cRZOStream {
public:
    static constexpr uint32_t kVTable = 0xda29bc;     // the game's table; VTable() is its owned symbol
    static const void *VTable();                      // the word our constructors store
    static const void *OwnVTable();                   // rzface::cRZOStream's: the image-free program's VTable()
    static constexpr uint32_t kGZIID_cIGZUnknown = 0x00000001;
    // Second IID QueryInterface accepts. The interface it names is not identified yet.
    static constexpr uint32_t kGZIID_Unidentified = 0xc51b11cb;
    static constexpr int kRawWriteSlot = 17;
    static constexpr int kSetUint8Slot = 5;
    static constexpr int kAddRefSlot = 1;
    static constexpr int kReleaseSlot = 2;
    static constexpr int kDeletingDestructorSlot = 21;

    // Constructor 0x1b4f75 (ret 4): count 0, vtable, byte-order flag = low byte of `order`, owner null.
    // Returns this.
    cRZOStream *Construct(uint32_t order);
    // Destructor body 0x1b52fb: vtable reset to this class's, then the owner (if any) is released
    // through its slot 2. Run directly by the deleting destructor.
    void Destruct();
    // Slot 21, 0x1b580a (ret 4): Destruct, then (flags & 1) sized operator delete(this, 0x10). Returns this.
    cRZOStream *DeletingDestructor(uint32_t flags);

    bool QueryInterface(uint32_t riid, void **ppvObj);       // slot 0, 0x1b5f6c
    // Slot 2, 0x1b5fc4 (shared by 12 classes, not hooked): above one reference, decrement and
    // return the new count; otherwise run the deleting destructor (slot 21, argument 1) and return 0.
    uint32_t Release();
    uint32_t SetSint8(uint8_t value);                         // slots 4-5, 0x1b63e7
    uint32_t SetSint16(int16_t value);                        // slot 6, 0x1b621c
    uint32_t SetUint16(uint16_t value);                       // slot 7, 0x1b6459
    uint32_t SetSint32(int32_t value);                        // slots 8-9, 0x1b62b0
    uint32_t SetSint64(int64_t value);                        // slots 10-11, 0x1b634c (ret 8)
    uint32_t SetFloat32(float value);                         // slot 12, 0x1b5ffc (never byte-swapped)
    uint32_t SetFloat64(double value);                        // slot 13, 0x1b608b (never byte-swapped)
    // Slot 14, 0x1b61c9: length as a packed integer, then the characters (no terminator). 1
    // for an empty string; otherwise the low byte of the raw write's result (a bool in al).
    uint8_t SetRZCharStr(const char *text);
    // Slot 15, 0x1b6118: same encoding from a cIGZString (Strlen slot 7, ToChar slot 5).
    uint8_t SetGZStr(cIGZString *text);
    // Calls the serializable's slot 3 with this stream as its only argument. 0x1b60fc.
    uint32_t SetGZSerializable(void *serializable);           // slot 16
    // Slot 19, 0x1b6617 (shared, not hooked): stores `owner` in the +0xc slot (AddRef new, Release old);
    // returns the address of that slot.
    void *SetOwner(void *owner);
    // Slot 22, 0x1b5f3c (shared, not hooked): sets the byte-order flag; returns true.
    bool SetByteOrder(uint32_t order);

    // 0x1b58ee (ret 4): 7 bits per byte, low group first, continuation flag 0x80, written through
    // slot 5 one byte at a time (the argument to slot 5 keeps the upper 24 bits of `value` in its
    // last call). Returns the last slot 5 result.
    uint32_t SetPackedUint32(uint32_t value);

    // 0x1b6734 (__stdcall, ret 8): reverses the eight bytes of a 64-bit value.
    static uint64_t __stdcall ByteSwap64(uint64_t value);

    const void *mVTable;     // +0
    uint32_t mRefCount;      // +4
    // +8: nonzero writes integers as stored (host order, little-endian); zero byte-swaps them.
    // Only the low byte is tested. Floats ignore it.
    uint8_t mNativeOrder;
    void *mOwner;            // +0xc: cIGZUnknown* released by the destructor; set by SetOwner

private:
    uint32_t WriteRaw(const void *buf, uint32_t size);       // slot 17
};

// The vtable (owned.h, OwnedVTable): `offset ts2_rva_da29bc`, the same one-instruction store as GameAddress(kVTable).
TS2_OWNED_STATIC(const OwnedVTable, da29bc);
inline const void *cRZOStream::VTable() { return &ts2_rva_da29bc; }

static_assert(sizeof(cRZOStream) == 16,"cRZOStream layout: vtable, count, byte-order flag at +8, owner at +0xc");
static_assert(offsetof(cRZOStream, mNativeOrder) == 8, "cRZOStream: byte-order flag at +8");
static_assert(offsetof(cRZOStream, mOwner) == 12, "cRZOStream: owner at +0xc");
