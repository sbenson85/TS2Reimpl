#pragma once
#include <cstddef>
#include <cstdint>

// cRZSerializableStream: an in-memory byte buffer that is both an output and an input stream and a
// serializable (base of cTSPersistDBRecord, which adds its own fields from +0x38). RVAs 1.18.0.187.
//
// Three subobjects, each with its own vtable (the game's, installed by the constructor for now):
//   +0x00  cRZOStream / cIGZOStream, vtable 0xe61a3c (25 slots)
//   +0x10  cRZIStream / cIGZIStream, vtable 0xe61aa4 (24 slots)
//   +0x20  cIGZSerializableStream / cIGZSerializable, vtable 0xe61b08 (14 slots)
// The members of the +0x10 and +0x20 subobjects are in cRZSerializableStreamIS and
// cRZSerializableStreamSS (their `this` is the subobject); most are thunks that subtract the offset
// and run the primary member.
//
// The buffer: data (+0x34) of capacity (+0x28) bytes, size (+0x24) bytes written, position (+0x2c) bytes
// read. Buffers are allocated with operator new[] (zero-filled) and freed with operator delete[].

class cRZSerializableStream {
public:
    static constexpr uint32_t kVTable = 0xe61a3c;
    static constexpr uint32_t kVTableIStream = 0xe61aa4;
    static constexpr uint32_t kVTableSerializable = 0xe61b08;
    static constexpr uint32_t kIdSerializableStream = 0x853d26de;
    static constexpr uint32_t kIdSerializable = 0xe4fda3d4;
    static constexpr int kReleaseDeleteSlot = 21; // deleting destructor
    static constexpr int kGetErrorSlot = 18;      // cIGZIStream-style GetError, in the primary vtable
    static constexpr int kSetErrorSlot = 24;
    static constexpr int kAddRefSlot = 1;

    // 0x9cbcf3 (ret 4): builds the base streams with the byte-order flag `order`, installs the vtables,
    // clears the buffer fields and applies vf22. Returns this.
    cRZSerializableStream *Construct(uint32_t order);
    // 0x9cbd43: installs the vtables, runs vf23 (frees the buffer) and destroys the two stream bases
    // (each releases the object at its +0xc, if any). The original's eax afterwards is whatever that
    // release returned; nothing uses it.
    void Destruct();
    // Slot 21, 0x9cbd79 (ret 4): Destruct, then operator delete of the 0x38-byte object when bit 0 of
    // flags is set. Returns this.
    cRZSerializableStream *DeletingDestructor(uint32_t flags);

    // Slot 0, 0x9cbe22 (ret 8): the serializable ids give the +0x20 subobject (null stays null) with a
    // reference added through slot 1; otherwise the cRZOStream then the cRZIStream implementation.
    bool QueryInterface(uint32_t riid, void **out);
    // Slot 2, 0x9cbed8: the shared stream release (the body of 0x1b5fc4): a count of 1 or less calls
    // the deleting destructor (slot 21) with 1 and returns 0; otherwise decrements and returns the count.
    uint32_t Release();
    // Slot 17, 0x9cbeed (ret 8): appends `n` bytes from `src` at data + size, growing the buffer through
    // Grow(n) first, then returns !GetError() (slot 18). Does nothing but the GetError call when src or n
    // is 0. Only al is meaningful.
    bool SetVoid(const void *src, uint32_t n);
    // Slot 22, 0x9cbdee (ret 4): while the buffer is unallocated, sets the byte-order flag (+8, and +0x18
    // of the input base) to the low byte of `order`. Always returns true (al = 1).
    bool SetByteOrder(uint32_t order);
    // Slot 23, 0x9cbf2f: frees the buffer and zeroes size, capacity and position. Returns true (al = 1).
    bool Reset();

    // 0x9cbff6: bytes between the position and the size (unsigned subtraction).
    uint32_t Remaining() const;
    // 0x9cbffd (ret 4): when the capacity is below n, replaces the buffer with a zeroed one of exactly n
    // bytes holding a copy of the first `size` bytes. False if the allocation fails.
    bool Reserve(uint32_t n);
    // 0x9cc050 (ret 4): when capacity - size is not above n, replaces the buffer with a zeroed one of
    // capacity + n + 0x80 bytes holding a copy of the first `size` bytes. False if the allocation fails.
    bool Grow(uint32_t n);

    const void *mVTable;      // +0x00
    uint32_t mRefCount;       // +0x04
    uint8_t mNativeOrder;     // +0x08: output base byte-order flag
    uint8_t mPad9[3];
    void *mOutOwner;          // +0x0c: released by the output base's destructor
    const void *mVTableIn;    // +0x10
    uint32_t mRefCountIn;     // +0x14
    uint8_t mSkipByteSwap;    // +0x18: input base byte-order flag
    uint8_t mPad19[3];
    void *mInOwner;           // +0x1c: released by the input base's destructor
    const void *mVTableSer;   // +0x20
    uint32_t mSize;           // +0x24
    uint32_t mCapacity;       // +0x28
    uint32_t mPosition;       // +0x2c
    uint8_t mError;           // +0x30
    uint8_t mPad31[3];
    uint8_t *mData;           // +0x34
};

static_assert(sizeof(cRZSerializableStream) == 0x38, "cRZSerializableStream is 0x38 bytes");
static_assert(offsetof(cRZSerializableStream, mOutOwner) == 0x0c, "output owner at +0xc");
static_assert(offsetof(cRZSerializableStream, mVTableIn) == 0x10, "input base at +0x10");
static_assert(offsetof(cRZSerializableStream, mInOwner) == 0x1c, "input owner at +0x1c");
static_assert(offsetof(cRZSerializableStream, mVTableSer) == 0x20, "serializable base at +0x20");
static_assert(offsetof(cRZSerializableStream, mSize) == 0x24, "size at +0x24");
static_assert(offsetof(cRZSerializableStream, mCapacity) == 0x28, "capacity at +0x28");
static_assert(offsetof(cRZSerializableStream, mPosition) == 0x2c, "position at +0x2c");
static_assert(offsetof(cRZSerializableStream, mError) == 0x30, "error flag at +0x30");
static_assert(offsetof(cRZSerializableStream, mData) == 0x34, "data at +0x34");

// The cRZIStream subobject (this = primary + 0x10). Slot numbers are the input vtable's.
class cRZSerializableStreamIS {
public:
    // Slot 0, 0x9cbe75: QueryInterface of the primary.
    bool QueryInterface(uint32_t riid, void **out);
    // Slot 2, 0x9cbedd: Release of the primary.
    uint32_t Release();
    // Slot 3, 0x9cbf71 (ret 4): skips n bytes: moves the position forward when that many remain, and
    // calls SetError(0) (slot 24) or SetError(1) when they don't. Returns !GetError() (slot 18).
    bool Skip(uint32_t n);
    // Slot 17, 0x9cbda0 (ret 8): copies n bytes from data + position to dst and advances the position
    // when that many remain; SetError(0) or SetError(1) as Skip. Returns !GetError().
    bool GetVoid(void *dst, uint32_t n);
    // Slots 21, 22, 23 (0x9cbd71, 0x9cbe12, 0x9cbf61): the primary's DeletingDestructor, SetByteOrder
    // and Reset.
    cRZSerializableStream *vf16_21(uint32_t flags);
    bool vf16_22(uint32_t order);
    bool vf16_23();
};

// The cIGZSerializableStream subobject (this = primary + 0x20).
class cRZSerializableStreamSS {
public:
    // Slot 0, 0x9cbe7d: QueryInterface of the primary.
    bool QueryInterface(uint32_t riid, void **out);
    // Slot 2, 0x9cbee5: Release of the primary.
    uint32_t Release();
    // Slot 3, 0x9cbfab (ret 4): writes this buffer to `out` (a cIGZOStream): size through its slot 9,
    // then the bytes through its slot 17; just a zero size when there is no data. Returns true.
    bool Write(void *out);
    // Slot 4, 0x9cbe85 (ret 4): reads a size from `in` (a cIGZIStream, slot 9), reserves that many bytes
    // and reads them through its slot 17; sets the error flag (slot 24, argument 1) when the
    // allocation fails. Returns true.
    bool Read(void *in);
    // Slots 8 and 9 (0x9cbe1a, 0x9cbf69): the primary's SetByteOrder and Reset.
    bool vf32_8(uint32_t order);
    bool vf32_9();
};
