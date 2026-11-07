#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>

#include "gz/cGZPersistBinaryReader.h" // its counted reference type

// cGZPersistBinaryWriter: writes typed values to the persisted binary stream (cIGZPersistWriter and
// cIGZUnknown at +0). Vtable 0xe60fb0. RVAs are 1.18.0.187.
//
// Almost every typed write is forwarded to the current stream: the writer calls its own slot 29
// (GetCurrent, below), which returns the element at the back of the deque, and then calls the same
// slot on that element to get the stream object. The stream is then given the value through the
// matching cIGZOStream slot (4 SetSint8, 5 SetUint8, 6 SetSint16, 7 SetUint16, 8 SetSint32,
// 9 SetUint32, 12 SetFloat32, 13 SetFloat64, 14 SetRZCharStr, 15 SetGZStr, 17 SetVoid). Writer slots
// 4 and 6 are SetUint8, 14, 15 and 22 are SetVoid-style raw writes. The array forms loop over
// elements and repeat the GetCurrent call for each one.
//
// Deque (fields below): a ring of 4-element blocks. Element i is blocks[(i >> 2) & (count - 1)][i & 3],
// and the back is at index offset + size - 1 (32-bit wrap). The back element is delegated to: its
// own slot 29 is called on it and that result is returned.
class cGZPersistBinaryWriter {
public:
    static constexpr uint32_t kVTable = 0xe60fb0;
    static constexpr int kCurrentSlot = 29;  // vtable +0x74: GetCurrent
    static constexpr int kAddRefSlot = 1;

    // Interface ids accepted by QueryInterface (compared directly). Names not identified yet; the
    // first is cIGZUnknown.
    static constexpr uint32_t kGZIID_cIGZUnknown = 0x00000001;
    static constexpr uint32_t kGZIID_Unidentified1 = 0x1b0aab0e;
    static constexpr uint32_t kGZIID_Unidentified2 = 0xbb100367;

    bool QueryInterface(uint32_t riid, void **ppvObj);     // slot 0, 0x9bfce0 (AddRef through slot 1)
    void *GetCurrent();                                      // slot 29, 0x9bfc80: back element's slot 29

    // Typed writes. Values are passed to the stream as the caller's 32-bit stack word (the original
    // tail-jumps with it), so the narrow types keep their upper bits as passed.
    uint32_t SetUint8(uint32_t value);                       // slots 4, 6; 0x9c0930 (stream slot 5)
    uint32_t SetSint8(uint32_t value);                       // slot 5; 0x9c0c80 (stream slot 4)
    uint32_t SetSint16(uint32_t value);                      // slot 7; 0x9c0be0 (stream slot 6)
    uint32_t SetUint16(uint32_t value);                      // slot 8; 0x9c0cb0 (stream slot 7)
    uint32_t SetSint32(uint32_t value);                      // slot 9; 0x9c0c30 (stream slot 8)
    uint32_t SetUint32(uint32_t value);                      // slot 10; 0x9c0d00 (stream slot 9)
    uint32_t SetFloat32(float value);                        // slot 11; 0x9c0980 (stream slot 12)
    uint32_t SetFloat64(double value);                       // slot 12; 0x9c09f0 (stream slot 13)
    uint32_t SetVoid(const void *buf, uint32_t size);        // slots 14, 15, 22; 0x9c0bd0 (stream slot 17)
    uint32_t WriteRZCharStr(const char *str);                // slot 23; 0x9c0c90 (stream slot 14, one word)
    uint32_t WriteGZStr(const void *str);                    // slot 24; 0x9c0ca0 (stream slot 15, one word)

    // Arrays: one stream call per element, GetCurrent called before each. With count 0 they return
    // the value eax held on entry, as the original does. They are naked stubs that keep eax and hand
    // it to a C++ worker.
    uint32_t SetUint8Array(const uint8_t *buf, uint32_t count);    // slot 13; 0x9c0940
    uint32_t SetSint16Array(const uint16_t *buf, uint32_t count);  // slot 16; 0x9c0bf0
    uint32_t SetUint16Array(const uint16_t *buf, uint32_t count);  // slot 17; 0x9c0cc0
    uint32_t SetSint32Array(const uint32_t *buf, uint32_t count);  // slot 18; 0x9c0c40
    uint32_t SetUint32Array(const uint32_t *buf, uint32_t count);  // slot 19; 0x9c0d10 (stream slot 9)
    uint32_t SetFloat32Array(const float *buf, uint32_t count);    // slot 20; 0x9c09a0 (stream slot 12)
    uint32_t SetFloat64Array(const double *buf, uint32_t count);   // slot 21; 0x9c0a10 (stream slot 13)

    // Object and record writes (one stack word each).
    uint32_t WriteSerialized(void *obj);                     // slot 25; 0x9c0a60
    uint32_t WriteFlaggedObject(void *obj);                  // slot 27; 0x9c0ad0
    uint32_t WriteRecord(const uint32_t *rec);               // slot 28; 0x9c1230 (rec[0] to stream slot 15, rec[1..2] to slot 9)

    // Slot 3, 0x9c0d50 (ret 0xc): writes `root` to `stream` through a new write context (the framework's class
    // 0xbb194c78, iid 0x9b194c66) pushed on the deque for the duration; `context` may give a group face (its slot 5
    // goes to +0x1c). Details in the .cpp.
    void WriteRoot(void *root, void *stream, void *context);
    // Slot 31, 0x9bfc50 (ret 4): destroys the deque (releasing its elements; 0x9bfb50); frees this (0x20 bytes) when
    // flags & 1. Returns this.
    cGZPersistBinaryWriter *DeletingDestructor(uint32_t flags);

    using ElementRing = std::deque<cGZPersistBinaryReader::Ref>; // at +8: the game's std::deque of counted elements
    ElementRing &Ring() { return *reinterpret_cast<ElementRing *>(mUnknown4 + 4); }

    // Vtable and deque fields. +4 and +8 are not touched by the functions above.
    const void *mVTable;          // +0
    uint8_t mUnknown4[8];         // +4..+0x0b
    void ***mBlocks;              // +0x0c: blocks, each a pointer to four element pointers
    uint32_t mBlockCount;         // +0x10: number of blocks, a power of two
    uint32_t mOffset;             // +0x14: index of the front element
    uint32_t mSize;               // +0x18: number of elements
    uint32_t mGroup;              // +0x1c: the context's group (slot 5 of its 0xad095a01 face), set by WriteRoot

private:
    void *Stream();               // this->slot 29 (through the vtable): the stream the writes go to
    void *BackElement();          // blocks[(i >> 2) & (count - 1)][i & 3] with i = offset + size - 1
};

static_assert(offsetof(cGZPersistBinaryWriter, mVTable) == 0, "cGZPersistBinaryWriter: vtable at +0");
static_assert(offsetof(cGZPersistBinaryWriter, mBlocks) == 0x0c, "cGZPersistBinaryWriter: blocks at +0x0c");
static_assert(offsetof(cGZPersistBinaryWriter, mBlockCount) == 0x10, "cGZPersistBinaryWriter: block count at +0x10");
static_assert(offsetof(cGZPersistBinaryWriter, mOffset) == 0x14, "cGZPersistBinaryWriter: offset at +0x14");
static_assert(offsetof(cGZPersistBinaryWriter, mSize) == 0x18, "cGZPersistBinaryWriter: size at +0x18");
static_assert(sizeof(cGZPersistBinaryWriter) == 0x20, "cGZPersistBinaryWriter: 0x20 bytes (the deleting destructor's size)");
static_assert(sizeof(cGZPersistBinaryWriter::ElementRing) == 0x14, "cGZPersistBinaryWriter: deque = proxy, map, size, offset, count");
