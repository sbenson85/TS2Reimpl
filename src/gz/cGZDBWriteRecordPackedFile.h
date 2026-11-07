#pragma once
#include "game/owned.h"
#include "gz/cGZPersistDBRecord.h"
#include "gz/cGZPersistDBSerialRecord.h"
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// cGZDBWriteRecordPackedFile: the packed file's write record (0x64 bytes). It derives from the serial record
// (cGZPersistDBSerialRecord, 0x44 bytes) and collects the record's bytes in memory; when the record grows past about
// two megabytes (or is opened over a larger existing record) the bytes move to a temporary file, the sink, and the
// record passes its reads and writes on to that file. RVAs are 1.18.0.187.
//
// Vtables (four subobjects, set by the constructor over the serial record's):
//   primary 0xe5f988 (65 slots)  +4 0xe5fa90 (21)  +0x20 0xe5fae8 (24)  +0x30 0xe5fb4c (24)
// The primary table is the serial record's with the raw read (27) and raw write (51) filled in, and the stream-like part
// (52 size, 53 set size, 54 position, 55 seek, 56 seek relative), 62 (close), 63 (set error) and 64 (flush into the
// segment). The +4 table is the cGZPersistDBRecord face plus adjustors for slots 8..14 (raw read, raw write, size, set
// size, position, seek, seek relative) and 19 (the deleting destructor); the stream faces are cRZOStream's and
// cRZIStream's with the deleting destructor in slot 21.
//
// Object (after the serial record's 0x44 bytes):
//   +0x44  kind: 3 (1 is the file read record, 2 the RAM read record)
//   +0x48  error byte (primary slot 63 sets it; the stream faces' slot 18 read it)
//   +0x4c  the buffer: a std::vector<char> (first, last, end); the record's size is its size
//   +0x58  position
//   +0x5c  the sink: a cRZFile (the temporary file), or null while the bytes are in the buffer
//   +0x60  byte: the temporary file has been tried (set before the attempt, so one failure is final)
//
// Every operation first asks the +4 face's GetAccessFlags (slot 5, through the vtable, with this = object + 4): zero means
// the record is closed and the operation fails. Calls through other objects' vtables (the sink's, the segment's, the
// +4 face's) are made as the original makes them: through the vtable, with the same arguments in the same order.
class cGZDBWriteRecordPackedFile : public cGZPersistDBSerialRecord {
public:
    static constexpr uint32_t kVTable = 0xe5f988;       // the game's tables; the VTable() functions are their owned symbols
    static constexpr uint32_t kSubVTable = 0xe5fa90;
    static constexpr uint32_t kRecordVTable = 0xe5fa90;
    static constexpr uint32_t kOStreamVTable = 0xe5fae8;
    static constexpr uint32_t kIStreamVTable = 0xe5fb4c;
    static constexpr uint32_t kObjectSize = 0x64;
    static constexpr uint32_t kKind = 3;
    static constexpr uint32_t kInitialCapacity = 0x80;     // the constructor reserves this much
    static constexpr uint32_t kMemoryLimit = 2000000;      // 0x1e8480: from here on the bytes go to a temporary file
    static constexpr uint32_t kSmallWrite = 0x40;          // appends of fewer bytes are made byte by byte
    static constexpr uint32_t kCopyChunk = 0x4000;         // bytes moved at a time between the segment and the file
    static constexpr int kAccessFlagsSlot = 5;     // +4 face: GetAccessFlags
    static constexpr int kGetKeySlot = 3;          // +4 face: the key (this + 8)
    static constexpr int kQueryChildSlot = 15;     // +4 face: QueryChild
    static constexpr int kSetChildSlot = 16;       // +4 face: SetChild
    static constexpr int kSetAccessFlagsSlot = 20; // +4 face: SetAccessFlags
    static constexpr int kGetFieldVoidSlot = 27;   // primary slot 0x6c
    static constexpr int kSetFieldVoidSlot = 51;   // primary slot 0xcc
    static constexpr int kGetSizeSlot = 52;        // primary slot 0xd0
    static constexpr int kSetSizeSlot = 53;        // primary slot 0xd4
    static constexpr int kGetPositionSlot = 54;    // primary slot 0xd8
    static constexpr int kSeekAbsoluteSlot = 55;   // primary slot 0xdc
    static constexpr int kSeekRelativeSlot = 56;   // primary slot 0xe0
    static constexpr int kCloseSlot = 62;          // primary slot 0xf8
    static constexpr int kSetErrorSlot = 63;
    static constexpr int kFlushToSegmentSlot = 64; // primary slot 0x100
    static constexpr uint32_t kIdSegment = 0x656b8f0c; // the segment's interface id (asked of the child)
    static constexpr int kSegmentReadAtSlot = 56;      // the segment's primary slot: ReadAt(dst, offset, size)
    static constexpr int kSegmentWriteAtSlot = 58;     // the segment's primary slot: WriteAt(src, offset, size)
    // The sink's (cRZFile) slots.
    static constexpr int kSinkOpenSlot = 3;
    static constexpr int kSinkIsOpenSlot = 4;
    static constexpr int kSinkCloseSlot = 5;
    static constexpr int kSinkPositionSlot = 6;
    static constexpr int kSinkSizeSlot = 7;
    static constexpr int kSinkSetLengthSlot = 8;
    static constexpr int kSinkSeekSlot = 12;
    static constexpr int kSinkReadSlot = 14;
    static constexpr int kSinkWriteSlot = 16;
    static constexpr int kSinkGetPathSlot = 25;
    static constexpr int kSinkDeleteSlot = 33;

    static const void *VTable();
    static const void *RecordVTable();
    static const void *OStreamVTable();
    static const void *IStreamVTable();
    static const void *OwnVTable();        // rzface::cGZDBWriteRecordPackedFile's: the image-free VTable()
    static const void *OwnRecordVTable();  // rzface::cGZDBWriteRecordPackedFileRecord's
    static const void *OwnOStreamVTable(); // rzface::cGZDBWriteRecordPackedFileOStream's
    static const void *OwnIStreamVTable(); // rzface::cGZDBWriteRecordPackedFileIStream's

    // 0x9a70f0 (ret 0x14): the constructor. The serial record's Construct gets the key, the segment's +4 face (file + 4,
    // or null) as the child and the flag dword; then kind 3, an empty buffer, position 0, no sink, the four vtables, the
    // access byte (the low byte of the flags word at +0xa) = 3 and 0x80 bytes reserved. `offset` and `size` describe the
    // existing record in the segment, which is read into the record (an empty `size` means a new record): a size over
    // two million bytes goes to a temporary file (named "Record_<type>_...", made by the unique-name function 0x1c255e)
    // in 0x4000-byte chunks, with the position put back afterwards; if the file cannot be had, or any step fails, the
    // file is closed, deleted and dropped and the whole record is read into the buffer. Returns this.
    cGZDBWriteRecordPackedFile *Construct(uint32_t offset, uint32_t size, const cGZPersistResourceKey *key, void *file, uint32_t flag);
    // 0x9a7b80 (ret 4), reached through the +4/+0x20/+0x30 slot 19/21/21 adjustors (0x9a76fe, 0x9a7706, 0x9a770e): the four
    // write vtables, the sink dropped (a file that is still open is closed and deleted), the buffer freed, then the base
    // vtables and the three member destructors (DestructMembers) and, when flags & 1, RZMemory::Free(this) unless flags & 4.
    cGZDBWriteRecordPackedFile *DeletingDestructor(uint32_t flags);

    // Slot 27, 0x9abd70 (two stack arguments): false when the record is closed. With a sink, the sink's slot 14
    // gets (out, size) and its answer is returned. Otherwise, if the position plus size (unsigned 32-bit, wrapping)
    // exceeds the buffer size, returns false. Otherwise copies size bytes from start + position to out, advances the
    // position by size and returns true.
    bool GetFieldVoid(void *out, uint32_t size);
    // Slot 51, 0x9afe70 (ret 8): false when closed. With a sink, the sink's slot 16 writes the bytes (its answer is
    // returned). Otherwise, when the position plus the count (signed) reaches two million and no temporary file has been
    // tried, one is made and the buffer so far, then these bytes, are written to it (the buffer is emptied, the position
    // zeroed, the answer true); if that fails the file is dropped and the buffer path below runs. In the buffer: an
    // append at the end of fewer than 0x40 bytes is made byte by byte; otherwise the buffer grows (zero-filled) to the end
    // of the write and the bytes are copied at the position. The position moves on by the count; true.
    bool SetFieldVoid(const void *src, uint32_t n);
    // Slot 52, 0x9acb80: 0 when closed. With a sink, the sink's slot 7 answers. Otherwise end - start.
    uint32_t GetSize();
    // Slot 53, 0x9b0360 (ret 4), also +4 slot 11: false when closed. With a sink, the sink's slot 8 (set length). Otherwise
    // the buffer is resized (zero-filled when it grows); true.
    bool SetSize(uint32_t size);
    // Slot 54, 0x9ac910: 0 when closed. With a sink, the sink's slot 6 answers. Otherwise the position.
    uint32_t GetPosition();
    // Slot 55, 0x9afac0 (one stack argument): false when closed. With a sink, returns whether the sink's
    // slot 12 (position) returns the same position. Otherwise sets the position and returns true.
    bool SeekAbsolute(uint32_t position);
    // Slot 56, 0x9afb90 (one stack argument): the 64-bit sum of GetPosition() (slot 54, this's own vtable) and the
    // signed offset. Returns false when the sum is negative. Otherwise calls SeekAbsolute (slot 55, this's own
    // vtable) with the low 32 bits of the sum, and returns its answer.
    bool SeekRelative(int32_t offset);
    // Slot 62, 0x9aa070: closes the record: SetAccessFlags(0) and SetChild(0) on the +4 face, the sink dropped as in the
    // destructor, the buffer emptied (its memory kept), the temporary-file byte and the position cleared.
    void Close();
    // Slot 64, 0x9b2550 (ret 8): writes the record's bytes into the segment at file offset `offset` (at most `size` of them):
    // false when closed or when the child is not asked for the segment (QueryChild with the segment's id, +4 slot 15). With
    // a sink, the file is copied in 0x4000-byte chunks (read from the sink, WriteAt on the segment: slot 58) from its start,
    // and the sink's position is put back; otherwise the buffer's first min(size, buffer size) bytes are written in one
    // call. The segment reference is released. True if every write succeeded.
    bool FlushToSegment(uint32_t offset, uint32_t size);

    // The temporary file, dropped: a file that is still open is closed and deleted (0x1c2e25 on its path), then it is
    // deleted as an object (slot 33 with 1) and the sink cleared. Not a function of the original (inlined in four).
    void DropSink();

    uint32_t mKind;               // +0x44
    uint8_t mError;               // +0x48
    uint8_t mPad49[3];
    std::vector<char> mBuffer;    // +0x4c (the class is used on raw memory: Construct builds it, DeletingDestructor ends it)
    uint32_t mPosition;           // +0x58
    void *mSink;                  // +0x5c
    uint8_t mTriedFile;           // +0x60
    uint8_t mPad61[3];

    // Makes the temporary file for the record: a name "Record_%08x_" (the key's type) made unique by the function 0x1c255e,
    // a new cRZFile stored in mSink and opened (access 3, creation 3, sharing 1). True when it is open; the sink is kept
    // (for the caller to drop) once the name was made, null when no name could be had. Not a function of the original.
    bool CreateSink(uint32_t keyType);
};

static_assert(offsetof(cGZDBWriteRecordPackedFile, mKind) == 0x44, "cGZDBWriteRecordPackedFile: kind at +0x44");
static_assert(offsetof(cGZDBWriteRecordPackedFile, mError) == 0x48, "cGZDBWriteRecordPackedFile: error at +0x48");
static_assert(offsetof(cGZDBWriteRecordPackedFile, mBuffer) == 0x4c, "cGZDBWriteRecordPackedFile: buffer at +0x4c");
static_assert(offsetof(cGZDBWriteRecordPackedFile, mPosition) == 0x58, "cGZDBWriteRecordPackedFile: position at +0x58");
static_assert(offsetof(cGZDBWriteRecordPackedFile, mSink) == 0x5c, "cGZDBWriteRecordPackedFile: sink at +0x5c");
static_assert(offsetof(cGZDBWriteRecordPackedFile, mTriedFile) == 0x60, "cGZDBWriteRecordPackedFile: byte at +0x60");
static_assert(sizeof(cGZDBWriteRecordPackedFile) == cGZDBWriteRecordPackedFile::kObjectSize, "cGZDBWriteRecordPackedFile: 0x64 bytes");

// The vtables as owned statics (owned.h, OwnedVTable).
TS2_OWNED_STATIC(const OwnedVTable, e5f988);
TS2_OWNED_STATIC(const OwnedVTable, e5fa90);
TS2_OWNED_STATIC(const OwnedVTable, e5fae8);
TS2_OWNED_STATIC(const OwnedVTable, e5fb4c);

inline const void *cGZDBWriteRecordPackedFile::VTable() { return &ts2_rva_e5f988; }
inline const void *cGZDBWriteRecordPackedFile::RecordVTable() { return &ts2_rva_e5fa90; }
inline const void *cGZDBWriteRecordPackedFile::OStreamVTable() { return &ts2_rva_e5fae8; }
inline const void *cGZDBWriteRecordPackedFile::IStreamVTable() { return &ts2_rva_e5fb4c; }

// ---- Our vtables -------------------------------------------------------------------------------------------------------
//
// One class per subobject, in namespace rzface so that the RTTI name is the record's own (tools/matchcheck.py ties our table
// to the game's by bare RTTI name). The slots forward to the members above (and the serial record's, the
// cGZPersistDBRecord's and the streams'); the primary face is the object, the +4/+0x20/+0x30 faces are the subobjects and
// the adjusting ones subtract their offset.
namespace rzface {

namespace writerecord {

inline uint8_t *Bytes(void *p) { return static_cast<uint8_t *>(p); }

// The primary face: all 65 slots (the serial record's, with the write record's own in 27, 51..56, 62 and 64).
class Primary {
    ::cGZDBWriteRecordPackedFile *Self()
    {
        return static_cast<::cGZDBWriteRecordPackedFile *>(reinterpret_cast<cGZPersistDBSerialRecord *>(this));
    }
    cGZPersistDBSerialRecord *Base() { return reinterpret_cast<cGZPersistDBSerialRecord *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Base()->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Base()->AddRef(); }                                                      // 1
    virtual uint32_t Release() { return Base()->Release(); }                                                    // 2
    virtual void *GetRecordInterface() { return this != nullptr ? Bytes(this) + 4 : nullptr; }                  // 3
    virtual bool GetFieldUint8Counted4(void *dst, uint32_t count) { return Base()->GetFieldUint8(dst, count); }
    virtual bool GetFieldUint8Single5(void *dst) { return Base()->GetFieldUint8(dst); }
    virtual bool GetFieldUint8Counted6(void *dst, uint32_t count) { return Base()->GetFieldUint8(dst, count); }
    virtual bool GetFieldUint8Single7(void *dst) { return Base()->GetFieldUint8(dst); }
    virtual bool GetFieldUint16Counted8(void *dst, uint32_t count) { return Base()->GetFieldUint16(dst, count); }
    virtual bool GetFieldUint16Single9(void *dst) { return Base()->GetFieldUint16(dst); }
    virtual bool GetFieldUint16Counted10(void *dst, uint32_t count) { return Base()->GetFieldUint16(dst, count); }
    virtual bool GetFieldUint16Single11(void *dst) { return Base()->GetFieldUint16(dst); }
    virtual bool GetFieldUint32Counted12(void *dst, uint32_t count) { return Base()->GetFieldUint32(dst, count); }
    virtual bool GetFieldUint32Single13(void *dst) { return Base()->GetFieldUint32(dst); }
    virtual bool GetFieldUint32Counted14(void *dst, uint32_t count) { return Base()->GetFieldUint32(dst, count); }
    virtual bool GetFieldUint32Single15(void *dst) { return Base()->GetFieldUint32(dst); }
    virtual bool GetFieldUint64Counted16(void *dst, uint32_t count) { return Base()->GetFieldUint64(dst, count); }
    virtual bool GetFieldUint64Single17(void *dst) { return Base()->GetFieldUint64(dst); }
    virtual bool GetFieldUint64Counted18(void *dst, uint32_t count) { return Base()->GetFieldUint64(dst, count); }
    virtual bool GetFieldUint64Single19(void *dst) { return Base()->GetFieldUint64(dst); }
    virtual bool GetFieldFloat32Counted(void *dst, uint32_t count) { return Base()->GetFieldFloat32(dst, count); } // 20
    virtual bool GetFieldFloat32Single(void *dst) { return Base()->GetFieldFloat32(dst); }                         // 21
    virtual bool GetFieldFloat64Counted(void *dst, uint32_t count) { return Base()->GetFieldFloat64(dst, count); } // 22
    virtual bool GetFieldFloat64Single(void *dst) { return Base()->GetFieldFloat64(dst); }                         // 23
    virtual bool GetFieldString(cIGZString *dst) { return Base()->GetFieldString(dst); }                           // 24
    virtual bool GetFieldResKey(void *dst) { return Base()->GetFieldResKey(dst); }                                 // 25
    virtual bool GetFieldVariant(cRZVariant **out) { return Base()->GetFieldVariant(out); }                        // 26
    virtual bool GetFieldVoid(void *dst, uint32_t n) { return Self()->GetFieldVoid(dst, n); }                      // 27
    virtual bool SetFieldUint8Counted28(const void *src, uint32_t count) { return Base()->SetFieldUint8(src, count); }
    virtual bool SetFieldUint8Single29(uint8_t value) { return Base()->SetFieldUint8(value); }
    virtual bool SetFieldUint8Counted30(const void *src, uint32_t count) { return Base()->SetFieldUint8(src, count); }
    virtual bool SetFieldUint8Single31(uint8_t value) { return Base()->SetFieldUint8(value); }
    virtual bool SetFieldUint16Counted32(const void *src, uint32_t count) { return Base()->SetFieldUint16(src, count); }
    virtual bool SetFieldUint16Single33(uint16_t value) { return Base()->SetFieldUint16(value); }
    virtual bool SetFieldUint16Counted34(const void *src, uint32_t count) { return Base()->SetFieldUint16(src, count); }
    virtual bool SetFieldSint16Single35(int16_t value) { return Base()->SetFieldSint16(value); }
    virtual bool SetFieldUint32Counted36(const void *src, uint32_t count) { return Base()->SetFieldUint32(src, count); }
    virtual bool SetFieldUint32Single37(uint32_t value) { return Base()->SetFieldUint32(value); }
    virtual bool SetFieldUint32Counted38(const void *src, uint32_t count) { return Base()->SetFieldUint32(src, count); }
    virtual bool SetFieldUint32Single39(uint32_t value) { return Base()->SetFieldUint32(value); }
    virtual bool SetFieldUint64Counted40(const void *src, uint32_t count) { return Base()->SetFieldUint64(src, count); }
    virtual bool SetFieldUint64Single41(uint64_t value) { return Base()->SetFieldUint64(value); }
    virtual bool SetFieldUint64Counted42(const void *src, uint32_t count) { return Base()->SetFieldUint64(src, count); }
    virtual bool SetFieldSint64Single43(int64_t value) { return Base()->SetFieldSint64(value); }
    virtual bool SetFieldFloat32Counted(const void *src, uint32_t count) { return Base()->SetFieldFloat32(src, count); } // 44
    virtual bool SetFieldFloat32Single(float value) { return Base()->SetFieldFloat32(value); }                           // 45
    virtual bool SetFieldFloat64Counted(const void *src, uint32_t count) { return Base()->SetFieldFloat64(src, count); } // 46
    virtual bool SetFieldFloat64Single(double value) { return Base()->SetFieldFloat64(value); }                          // 47
    virtual bool SetFieldString(const cIGZString *value) { return Base()->SetFieldString(value); }                       // 48
    virtual bool SetFieldResKey(const void *key) { return Base()->SetFieldResKey(key); }                                 // 49
    virtual bool SetFieldVariant(const cRZVariant *value) { return Base()->SetFieldVariant(value); }                     // 50
    virtual bool SetFieldVoid(const void *src, uint32_t n) { return Self()->SetFieldVoid(src, n); }                      // 51
    virtual uint32_t GetSize() { return Self()->GetSize(); }                                                             // 52
    virtual bool SetSize(uint32_t size) { return Self()->SetSize(size); }                                                // 53
    virtual uint32_t GetPosition() { return Self()->GetPosition(); }                                                     // 54
    virtual bool Seek(uint32_t position) { return Self()->SeekAbsolute(position); }                                      // 55
    virtual bool SeekRelative(int32_t offset) { return Self()->SeekRelative(offset); }                                   // 56
    virtual bool GetFieldBlob(void *dst, uint32_t size, uint32_t count) { return Base()->GetFieldBlob(dst, size, count); }          // 57
    virtual bool SetFieldBlob(const void *src, uint32_t size, uint32_t count) { return Base()->SetFieldBlob(src, size, count); }    // 58
    virtual void *SetFieldStringThenOStream(const cIGZString *value) { return Base()->SetFieldStringThenOStream(value); }           // 59
    virtual void *GetOStream(uint32_t unused) { return Base()->GetOStream(unused); }                                                // 60
    virtual void *GetFieldStringThenIStream(cIGZString *dst) { return Base()->GetFieldStringThenIStream(dst); }                     // 61
    virtual void Close() { Self()->Close(); }                                                                                       // 62
    virtual void SetError(uint8_t error) { Self()->mError = error; }                                                                // 63
    virtual bool FlushToSegment(uint32_t offset, uint32_t size) { return Self()->FlushToSegment(offset, size); }                    // 64
};

// The +4 face (the cGZPersistDBRecord subobject): `this` is the object + 4.
class Record {
    ::cGZDBWriteRecordPackedFile *Object()
    {
        return static_cast<::cGZDBWriteRecordPackedFile *>(reinterpret_cast<cGZPersistDBSerialRecord *>(Bytes(this) - 4));
    }
    cGZPersistDBRecord *Base() { return reinterpret_cast<cGZPersistDBRecord *>(this); }
    cGZPersistDBSerialRecordThunks *Thunks() { return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Thunks()->QueryInterface4(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Thunks()->AddRef4(); }                                                     // 1
    virtual uint32_t Release() { return Thunks()->Release4(); }                                                   // 2
    virtual void *Slot3() { return Bytes(this) + 8; }                                                             // 3
    virtual void SetKey(const void *key) { Base()->SetKey(key); }                                                 // 4
    virtual uint32_t GetAccessFlags() { return Base()->GetAccessFlags(); }                                        // 5
    virtual bool Close() { return Base()->Close(); }                                                              // 6
    virtual bool Abort() { return Base()->Abort(); }                                                              // 7
    virtual bool GetFieldVoid(void *dst, uint32_t n) { return Object()->GetFieldVoid(dst, n); }                   // 8
    virtual bool SetFieldVoid(const void *src, uint32_t n) { return Object()->SetFieldVoid(src, n); }             // 9
    virtual uint32_t GetSize() { return Object()->GetSize(); }                                                    // 10
    virtual bool SetSize(uint32_t size) { return Object()->SetSize(size); }                                       // 11
    virtual uint32_t GetPosition() { return Object()->GetPosition(); }                                            // 12
    virtual bool Seek(uint32_t position) { return Object()->SeekAbsolute(position); }                             // 13
    virtual bool SeekRelative(int32_t offset) { return Object()->SeekRelative(offset); }                          // 14
    virtual bool QueryChild(uint32_t riid, void **ppvObj) { return Base()->QueryChild(riid, ppvObj); }            // 15
    virtual void SetChild(void *child) { Base()->SetChild(child); }                                               // 16
    virtual bool TestHighFlags(uint8_t mask) { return Base()->TestHighFlags(mask); }                              // 17
    virtual uint32_t SetHighFlags(uint8_t value, uint8_t set) { return Base()->SetHighFlags(value, set); }        // 18
    virtual void *DeletingDestructor(uint32_t flags) { return Object()->DeletingDestructor(flags); }              // 19
    virtual uint32_t SetAccessFlags(uint8_t value) { return Base()->SetAccessFlags(value); }                      // 20
};

// The +0x20 face (the cRZOStream subobject): `this` is the object + 0x20.
class OStream {
    ::cGZDBWriteRecordPackedFile *Object()
    {
        return static_cast<::cGZDBWriteRecordPackedFile *>(reinterpret_cast<cGZPersistDBSerialRecord *>(Bytes(this) - 0x20));
    }
    cRZOStream *Base() { return reinterpret_cast<cRZOStream *>(this); }
    cGZPersistDBSerialRecordThunks *Thunks() { return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Thunks()->QueryInterface32(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Thunks()->AddRef32(); }                                                     // 1
    virtual uint32_t Release() { return Thunks()->Release32(); }                                                   // 2
    virtual void Nop3() {}                                                                                         // 3
    virtual uint32_t SetSint8Slot4(uint8_t value) { return Base()->SetSint8(value); }
    virtual uint32_t SetSint8Slot5(uint8_t value) { return Base()->SetSint8(value); }
    virtual uint32_t SetSint16(int16_t value) { return Base()->SetSint16(value); }                                 // 6
    virtual uint32_t SetUint16(uint16_t value) { return Base()->SetUint16(value); }                                // 7
    virtual uint32_t SetSint32Slot8(int32_t value) { return Base()->SetSint32(value); }
    virtual uint32_t SetSint32Slot9(int32_t value) { return Base()->SetSint32(value); }
    virtual uint32_t SetSint64Slot10(int64_t value) { return Base()->SetSint64(value); }
    virtual uint32_t SetSint64Slot11(int64_t value) { return Base()->SetSint64(value); }
    virtual uint32_t SetFloat32(float value) { return Base()->SetFloat32(value); }                                 // 12
    virtual uint32_t SetFloat64(double value) { return Base()->SetFloat64(value); }                                // 13
    virtual uint8_t SetRZCharStr(const char *text) { return Thunks()->SetRZCharStr(text); }                        // 14
    virtual bool SetGZStr(const cIGZString *text) { return Thunks()->SetGZStr(text); }                             // 15
    virtual uint32_t SetGZSerializable(void *serializable) { return Base()->SetGZSerializable(serializable); }     // 16
    virtual bool SetVoid(const void *src, uint32_t n) { return Thunks()->SetVoid(src, n); }                       // 17
    virtual uint8_t GetError() { return Bytes(this)[0x28]; }                                                       // 18
    virtual void *SetOwner(void *owner) { return Base()->SetOwner(owner); }                                        // 19
    virtual void *GetOwner() { return *reinterpret_cast<void **>(Bytes(this) + 0xc); }                             // 20
    virtual void *DeletingDestructor(uint32_t flags) { return Object()->DeletingDestructor(flags); }               // 21
    virtual bool SetByteOrder(uint32_t order) { return Base()->SetByteOrder(order); }                              // 22
    virtual bool True23() { return true; }                                                                         // 23
};

// The +0x30 face (the cRZIStream subobject): `this` is the object + 0x30.
class IStream {
    ::cGZDBWriteRecordPackedFile *Object()
    {
        return static_cast<::cGZDBWriteRecordPackedFile *>(reinterpret_cast<cGZPersistDBSerialRecord *>(Bytes(this) - 0x30));
    }
    cRZIStream *Base() { return reinterpret_cast<cRZIStream *>(this); }
    cGZPersistDBSerialRecordThunks *Thunks() { return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Thunks()->QueryInterface48(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Thunks()->AddRef48(); }                                                     // 1
    virtual uint32_t Release() { return Thunks()->Release48(); }                                                   // 2
    virtual bool Skip(uint32_t n) { return Thunks()->Skip(n); }                                                    // 3
    virtual bool GetSint8Slot4(void *dst) { return Base()->GetSint8(dst); }
    virtual bool GetSint8Slot5(void *dst) { return Base()->GetSint8(dst); }
    virtual bool GetSint16Slot6(void *dst) { return Base()->GetSint16(dst); }
    virtual bool GetSint16Slot7(void *dst) { return Base()->GetSint16(dst); }
    virtual bool GetSint32Slot8(void *dst) { return Base()->GetSint32(dst); }
    virtual bool GetSint32Slot9(void *dst) { return Base()->GetSint32(dst); }
    virtual bool GetSint64Slot10(void *dst) { return Base()->GetSint64(dst); }
    virtual bool GetSint64Slot11(void *dst) { return Base()->GetSint64(dst); }
    virtual bool GetFloat32(void *dst) { return Base()->GetFloat32(dst); }                                         // 12
    virtual bool GetFloat64(void *dst) { return Base()->GetFloat64(dst); }                                         // 13
    virtual bool False14(char *, uint32_t) { return false; }                                                       // 14
    virtual bool GetGZStr(cIGZString *out) { return Thunks()->GetGZStr(out); }                                     // 15
    virtual uint32_t GetGZSerializable(void *serializable) { return Base()->GetGZSerializable(serializable); }     // 16
    virtual bool GetVoid(void *dst, uint32_t n) { return Thunks()->GetVoid(dst, n); }                              // 17
    virtual uint8_t GetError() { return Bytes(this)[0x18]; }                                                       // 18
    virtual void *SetOwner(void *owner) { return reinterpret_cast<cRZOStream *>(this)->SetOwner(owner); }         // 19
    virtual void *GetOwner() { return *reinterpret_cast<void **>(Bytes(this) + 0xc); }                             // 20
    virtual void *DeletingDestructor(uint32_t flags) { return Object()->DeletingDestructor(flags); }               // 21
    virtual bool SetByteOrder(uint32_t order) { return reinterpret_cast<cRZOStream *>(this)->SetByteOrder(order); } // 22
    virtual bool True23() { return true; }                                                                         // 23
};

} // namespace writerecord

class cGZDBWriteRecordPackedFile final : public writerecord::Primary {};
class cGZDBWriteRecordPackedFileRecord final : public writerecord::Record {};
class cGZDBWriteRecordPackedFileOStream final : public writerecord::OStream {};
class cGZDBWriteRecordPackedFileIStream final : public writerecord::IStream {};

} // namespace rzface

namespace writerecord_detail {
template <class Face> inline const void *VTableOf()
{
    Face face;
    return *reinterpret_cast<const void *const *>(&face);
}
} // namespace writerecord_detail

inline const void *cGZDBWriteRecordPackedFile::OwnVTable() { return writerecord_detail::VTableOf<rzface::cGZDBWriteRecordPackedFile>(); }
inline const void *cGZDBWriteRecordPackedFile::OwnRecordVTable() { return writerecord_detail::VTableOf<rzface::cGZDBWriteRecordPackedFileRecord>(); }
inline const void *cGZDBWriteRecordPackedFile::OwnOStreamVTable() { return writerecord_detail::VTableOf<rzface::cGZDBWriteRecordPackedFileOStream>(); }
inline const void *cGZDBWriteRecordPackedFile::OwnIStreamVTable() { return writerecord_detail::VTableOf<rzface::cGZDBWriteRecordPackedFileIStream>(); }
