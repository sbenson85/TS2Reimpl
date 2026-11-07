#pragma once
#include "game/owned.h"

#include <cstddef>
#include <cstdint>

class cIGZString;

// cRZIStream: the game's binary input stream base (implements cIGZIStream and cIGZUnknown at +0;
// vtable 0xda2a20). Its raw read is slot 17 (cIGZIStream::GetVoid), which is pure virtual here and
// supplied by subclasses: bool GetVoid(void *dst, uint32_t n), __thiscall. The typed getters read
// through that slot and, unless mSkipByteSwap is nonzero, reverse the bytes of the value in place.
// The object is 0x10 bytes; subclasses add their fields after it. Slot 2 (Release, 0x1b5fc4) and
// slot 22 (set byte order, 0x1b5f3c) are the same code as the output base's: see cRZOStream.
// RVAs are 1.18.0.187.
class cRZIStream {
public:
    static constexpr uint32_t kVTable = 0xda2a20;     // the game's table; VTable() is its owned symbol
    static const void *VTable();                      // the word our constructors store
    static const void *OwnVTable();                   // rzface::cRZIStream's: the image-free program's VTable()
    static constexpr int kRawReadSlot = 17;
    static constexpr int kGetUint8Slot = 5;
    static constexpr int kSkipSlot = 3;       // cIGZIStream slot 3, pure in this base: Skip(count)
    static constexpr int kGetErrorSlot = 18;  // nonzero al: the stream has failed
    static constexpr int kReleaseSlot = 2;

    // The two interface ids the original accepts; both return this with a reference added.
    static constexpr uint32_t kIdFirst = 1;
    static constexpr uint32_t kIdSecond = 0x051b11bd; // the original subtracts 1 first, then compares with 0x051b11bc

    // Constructor 0x1b4d82 (ret 4): count 0, vtable, flag = low byte of `order`, owner null. Returns this.
    cRZIStream *Construct(uint32_t order);
    // Destructor body 0x1b52af: vtable reset to this class's, then the owner (if any) is released
    // through its slot 2.
    void Destruct();
    // Slot 21, 0x1b578e (ret 4): Destruct, then (flags & 1) sized operator delete(this, 0x10). Returns this.
    cRZIStream *DeletingDestructor(uint32_t flags);

    bool QueryInterface(uint32_t id, void **out);  // slot 0, 0x1b5f48
    bool GetSint8(void *dst);                      // slots 4/5, 0x1b5e0f: GetVoid(dst, 1)
    bool GetSint16(void *dst);                     // slots 6/7, 0x1b5c5b: GetVoid(dst, 2), swap unless skipped
    bool GetSint32(void *dst);                     // slots 8/9, 0x1b5cca: GetVoid(dst, 4), swap unless skipped
    bool GetSint64(void *dst);                     // slots 10/11, 0x1b5da9: GetVoid(dst, 8), swap unless skipped
    bool GetFloat32(void *dst);                    // slot 12, 0x1b5982: GetVoid(dst, 4), no swap
    bool GetFloat64(void *dst);                    // slot 13, 0x1b59c0: GetVoid(dst, 8), no swap
    // Slot 14, 0x1b5b86 (ret 8): reads a packed length, then min(length, size) characters into buf
    // and a terminator, then skips (slot 3) the rest. Returns the low byte: 1 when the stream has no error.
    // ORIGINAL-BUG: the terminator is written at buf[min(length, size)], one past the buffer when
    // length >= size (and at buf[0] for size 0).
    uint8_t GetRZCharStr(char *buf, uint32_t size);
    // Slot 15, 0x1b5a4d: empties the string (FromChar of the empty literal), reads a packed length and
    // appends the characters in chunks of at most 0xff. Returns the low byte: 1 when no error.
    uint8_t GetGZStr(cIGZString *out);
    // Slot 16, 0x1b59ce: calls the serializable's slot 4 with this stream as its only argument.
    uint32_t GetGZSerializable(void *serializable);

    // 0x1b588e (ret 8): reads a packed integer (7 bits per byte, low group first, 0x80 continues)
    // through slot 5 into *out; the second stack argument is unused. Always returns 0. A failed read
    // leaves its byte as it was; the original's byte buffer starts out holding the bytes of `this`
    // (twice), which is mirrored here.
    // ORIGINAL-BUG: the byte buffer is 8 bytes on the stack and the loop does not stop at its end,
    // so a stream of 8 or more continuation bytes smashes the stack.
    uint32_t GetPackedUint32(uint32_t *out, void *unused);

    const void *mVTable;     // +0
    uint32_t mRefCount;      // +4 (AddRef target for QueryInterface; the count's own methods are not here)
    uint8_t mSkipByteSwap;   // +8: nonzero: values are taken as stored; zero: byte-reversed after reading
    void *mOwner;            // +0xc: cIGZUnknown* released by the destructor
};

// The vtable (owned.h, OwnedVTable): `offset ts2_rva_da2a20`, the same one-instruction store as GameAddress(kVTable).
TS2_OWNED_STATIC(const OwnedVTable, da2a20);
inline const void *cRZIStream::VTable() { return &ts2_rva_da2a20; }

static_assert(sizeof(cRZIStream) == 16,"cRZIStream is 0x10 bytes");
static_assert(offsetof(cRZIStream, mVTable) == 0, "cRZIStream: vtable at +0");
static_assert(offsetof(cRZIStream, mRefCount) == 4, "cRZIStream: refcount at +4");
static_assert(offsetof(cRZIStream, mSkipByteSwap) == 8, "cRZIStream: byte-order flag at +8");
static_assert(offsetof(cRZIStream, mOwner) == 12, "cRZIStream: owner at +0xc");
