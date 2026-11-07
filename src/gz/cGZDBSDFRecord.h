#pragma once
#include <cstddef>
#include <cstdint>

// cGZDBSDFRecord: a record of a cGZDBSegmentDirectoryFiles segment, stored as one loose file. Primary vtable
// 0xe5ead4 (17 slots: cIGZPersistDBSDFRecord and cIGZUnknown at +0). Subobjects: cGZPersistDBSerialRecord at
// +4 (vtable 0xe5eb1c, 62 slots), cGZPersistDBRecord at +8 (vtable 0xe5ec18), cRZOStream at +36 and cRZIStream
// at +52. RVAs are 1.18.0.187.
//
// Field map, relative to the serial subobject at +4 (serial offset = primary offset - 4), as the code of the
// slots below reads it:
//   primary +0x00  vtable (17 slots)
//   primary +0x04  serial record subobject (cGZPersistDBSerialRecord, reference count uint16 at primary +0x0c)
//   primary +0x48  byte (serial +0x44): nonzero when the stream is open
//   primary +0x4c  embedded stream object (its vtable pointer is this word; the stream is cRZFile). GetPosition
//                  calls its slot 6, SeekAbsolute its slot 12, SeekRelative its slot 11, SetSize its slot 8
//   primary +0x184 dword (serial +0x180): seeking is allowed only when this is 1
// Not mapped yet: the rest of the object.
class cGZDBSDFRecord {
public:
    static constexpr uint32_t kVTable = 0xe5ead4;
    static constexpr uint32_t kSerialVTable = 0xe5eb1c;
    static constexpr uint32_t kIdSelf = 0x856c8d04;  // cIGZPersistDBSDFRecord: +0
    static constexpr int kStreamSlotPosition = 6;    // stream's vtable 0x18
    static constexpr int kStreamSlotSetLength = 8;   // stream's vtable 0x20
    static constexpr int kStreamSlotSeekRelative = 11; // stream's vtable 0x2c
    static constexpr int kStreamSlotSeekAbsolute = 12; // stream's vtable 0x30

    // Slot 0, 0x9a2610. The id 0x856c8d04 stores this in *ppvObj and calls AddRef through the vtable of this.
    // Any other id goes to the serial subobject's QueryInterface (cGZPersistDBSerialRecord, 0x99cfa0).
    bool QueryInterface(uint32_t riid, void **ppvObj);
    // Slot 1, 0x99f2e0: the serial subobject's AddRef (0x97b790).
    uint32_t AddRef();
    // Slot 2, 0x9a2780: the serial subobject's Release (0x99d0a0).
    uint32_t Release();

    // Primary slots 3 to 9, entered with ecx = this (the record). The stream is mStream (the embedded object at
    // +0x4c). Slot names follow the serial thunks that reach them (GetSize, GetFieldVoid, SetFieldVoid); the rest
    // are named by what they do, as the cIGZPersistDBSDFRecord interface names are not mapped yet.
    // Slot 3, 0x9a1c50: when open, the stream's slot 7 (Length) with no arguments; otherwise -1.
    int32_t GetSize();
    // Slot 4, 0x9a1460: two stack arguments. When open and the seek mode is 1, the stream's slot 14 (Read) with both
    // arguments; returns that call's result as a byte (al). Otherwise false.
    bool GetFieldVoid(void *data, uint32_t size);
    // Slot 5, 0x9a28d0: two stack arguments. When open and the seek mode is 2 or 3, the stream's slot 16 (Write) with
    // both arguments, as GetFieldVoid returns it. Otherwise false.
    bool SetFieldVoid(const void *data, uint32_t size);
    // Slot 6, 0x9a2740: two stack arguments. When open and the seek mode is 1, the stream's slot 15 (ReadWithCount)
    // with the buffer and the count held in a local, and returns the local after the call. Otherwise 0.
    uint32_t ReadCounted(void *data, uint32_t count);
    // Slot 7, 0x9a2bd0: as ReadCounted, but when open and the seek mode is not 1, with the stream's slot 17
    // (WriteWithCount).
    uint32_t WriteCounted(const void *data, uint32_t count);
    // Slot 8, 0x9a2ba0: one stack argument (an int32 out pointer). When open, stores the stream's slot 6 (Position)
    // result there and returns true. Otherwise stores 0 and returns false.
    bool GetPositionTo(int32_t *out);
    // Slot 9, 0x9a27e0: two stack arguments (position, origin). When open and position does not exceed the stream's
    // slot 7 (Length), calls the stream's slot 13 (Seek) with position and the dword at game RVA 0xe5e9f4 indexed by
    // origin, and returns whether it gave a position other than -1. Otherwise false.
    bool SeekTo(uint32_t position, uint32_t origin);

    const void *mVTable;            // +0x00
    const void *mSerialVTable;      // +0x04
    uint8_t mUnmapped08[0x48 - 0x08];
    uint8_t mStreamOpen;            // +0x48
    uint8_t mUnmapped49[0x4c - 0x49];
    const void *mStream;            // +0x4c: embedded stream object (vtable pointer)
    uint8_t mUnmapped50[0x184 - 0x50];
    uint32_t mSeekMode;             // +0x184
};

static_assert(offsetof(cGZDBSDFRecord, mVTable) == 0, "cGZDBSDFRecord: vtable at +0");
static_assert(offsetof(cGZDBSDFRecord, mSerialVTable) == 4, "cGZDBSDFRecord: serial subobject at +4");
static_assert(offsetof(cGZDBSDFRecord, mStreamOpen) == 0x48, "cGZDBSDFRecord: open byte at +0x48");
static_assert(offsetof(cGZDBSDFRecord, mStream) == 0x4c, "cGZDBSDFRecord: stream object at +0x4c");
static_assert(offsetof(cGZDBSDFRecord, mSeekMode) == 0x184, "cGZDBSDFRecord: seek mode at +0x184");

// The serial subobject at +4 of cGZDBSDFRecord, entered by the original with ecx = object + 4. `this` here is
// the subobject; offsets in the comments are relative to it. The class has no data members.
class cGZDBSDFRecordSerialView {
public:
    // Serial slot 3, 0x99f330: the record subobject at +8 of the primary (this + 4), or null when this is 4 (the
    // serial subobject of a null record).
    void *AsIGZPersistDBRecord();
    // Serial slot 54, 0x9a1b90: the stream's slot 6, or -1 when the stream is not open.
    int32_t GetPosition();
    // Serial slot 55, 0x9a2820 (one stack argument): when the stream is open and the seek mode is 1, the
    // stream's slot 12 with the argument; returns whether it gave a position other than -1. Otherwise false.
    bool SeekAbsolute(int32_t position);
    // Serial slot 56, 0x9a2860 (one stack argument): as SeekAbsolute, with the stream's slot 11.
    bool SeekRelative(int32_t offset);
    // Serial slot 53, 0x9a2a40 (one stack argument): when the stream is open, the stream's slot 8 with the
    // argument (its result is returned); otherwise false.
    bool SetSize(uint32_t size);
};
