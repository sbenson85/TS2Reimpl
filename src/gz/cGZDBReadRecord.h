#pragma once
#include "game/owned.h"
#include "gz/cGZPersistDBRecord.h"
#include "gz/cGZPersistDBSerialRecord.h"
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"

#include <cstddef>
#include <cstdint>

// The packed file's read records: cGZDBReadRecordPackedFile (the bytes stay in the .package file and are read on demand
// through a 0x1000-byte cache; 0x1060 bytes) and cGZDBReadRecordRAM (the bytes are a counted block in memory; 0x58 bytes).
// Both derive from cGZDBRecordPackedFile (the layout below) over cGZPersistDBSerialRecord. RVAs are 1.18.0.187.
//
// Vtables (four subobjects each, set by the constructors over the serial record's):
//   file  primary 0xe5f534 (67 slots)  +4 0xe5f644 (21)  +0x20 0xe5f69c (24)  +0x30 0xe5f700 (24)
//   RAM   primary 0xe5f764 (64 slots)  +4 0xe5f868 (21)  +0x20 0xe5f8c0 (24)  +0x30 0xe5f924 (24)
// The primary tables are the serial record's with the raw read (27) and raw write (51) filled in, and slots 52..56 and 62
// (the stream-like part: size, position, seek) and 63 (set error). The file variant adds 64..66 (the cache). The +4
// tables are the cGZPersistDBRecord face plus adjustors; the stream faces are cRZOStream's and cRZIStream's.
//
// Object (after the serial record's 0x44 bytes):
//   +0x44  kind: 1 file, 2 RAM (3 is the memory-backed write record, cGZDBWriteRecordPackedFile)
//   +0x48  error byte (primary slot 63 sets it; the stream faces' slot 18 read it)
//   +0x4c  data: the file offset of the record (file variant), or the counted block (RAM variant): a count dword whose
//          reference this record holds, and the bytes at +4
//   +0x50  position
//   +0x54  size
//   file variant only:
//   +0x58  cache start (the position of cache[0]; 0xffffffff: empty)
//   +0x5c  cache length
//   +0x60  cache[0x1000]
//
// Every operation first asks the +4 face's GetAccessFlags (slot 5, through the vtable, with this = object + 4): zero means
// the record is closed and the operation fails (the file variant's GetFieldVoid then also sets the error byte).
class cGZDBRecordPackedFile : public cGZPersistDBSerialRecord {
public:
    static constexpr uint32_t kKindFile = 1, kKindRAM = 2;
    static constexpr int kAccessFlagsSlot = 5;  // +4 face: GetAccessFlags
    static constexpr int kQueryChildSlot = 15;  // +4 face: QueryChild
    static constexpr int kSetChildSlot = 16;    // +4 face: SetChild
    static constexpr int kSetAccessFlagsSlot = 20; // +4 face
    static constexpr int kSetErrorSlot = 63;    // primary: SetError(byte)
    static constexpr int kReadSourceSlot = 64;  // primary (file variant)
    static constexpr int kReadBufferedSlot = 65;
    static constexpr int kCachedRemainingSlot = 66;
    static constexpr uint32_t kIdSegment = 0x656b8f0c; // the segment's interface id (ReadSource asks the child for it)
    static constexpr int kReadAtSlot = 56;      // the segment's primary slot: ReadAt(dst, offset, size)

    uint32_t mKind;       // +0x44
    uint8_t mError;       // +0x48
    uint8_t mPad49[3];
    uint32_t mData;       // +0x4c
    uint32_t mPosition;   // +0x50
    uint32_t mSize;       // +0x54

    bool Seek(uint32_t position); // slot 55, 0x9afa20 (ret 4): both variants run this one body
};

static_assert(offsetof(cGZDBRecordPackedFile, mKind) == 0x44, "cGZDBRecordPackedFile: kind at +0x44");
static_assert(offsetof(cGZDBRecordPackedFile, mError) == 0x48, "cGZDBRecordPackedFile: error at +0x48");
static_assert(offsetof(cGZDBRecordPackedFile, mData) == 0x4c, "cGZDBRecordPackedFile: data at +0x4c");
static_assert(offsetof(cGZDBRecordPackedFile, mPosition) == 0x50, "cGZDBRecordPackedFile: position at +0x50");
static_assert(offsetof(cGZDBRecordPackedFile, mSize) == 0x54, "cGZDBRecordPackedFile: size at +0x54");

// ---- The file-backed record (0x1060 bytes) ----------------------------------------------------------------------------

class cGZDBReadRecordPackedFile : public cGZDBRecordPackedFile {
public:
    static constexpr uint32_t kVTable = 0xe5f534;        // the game's tables; the VTable() functions are their owned symbols
    static constexpr uint32_t kRecordVTable = 0xe5f644;
    static constexpr uint32_t kOStreamVTable = 0xe5f69c;
    static constexpr uint32_t kIStreamVTable = 0xe5f700;
    static constexpr uint32_t kObjectSize = 0x1060;
    static constexpr uint32_t kCacheBytes = 0x1000;
    static const void *VTable();
    static const void *RecordVTable();
    static const void *OStreamVTable();
    static const void *IStreamVTable();
    static const void *OwnVTable();        // rzface::cGZDBReadRecordPackedFile's: the image-free VTable()
    static const void *OwnRecordVTable();  // rzface::cGZDBReadRecordPackedFileRecord's
    static const void *OwnOStreamVTable(); // rzface::cGZDBReadRecordPackedFileOStream's
    static const void *OwnIStreamVTable(); // rzface::cGZDBReadRecordPackedFileIStream's

    // 0x9a6de0 (ret 0x14): the constructor. The serial record's Construct gets the key, the segment's +4 face (file + 4,
    // or null) as the child and the flag dword; then kind 1, error 0, the data (file offset) and size, the four vtables,
    // position 0, cache start 0xffffffff, cache length 0, and the access byte (the low byte of the flags word at +0xa) = 1.
    cGZDBReadRecordPackedFile *Construct(uint32_t data, uint32_t size, const cGZPersistResourceKey *key, void *file, uint32_t flag);
    // 0x9a78e0 (ret 4), reached through the +4/+0x20/+0x30 slot 19/21 adjustors (0x9a76c6, 0x9a76ce, 0x9a76d6): the four
    // base vtables and the three member destructors (DestructMembers), then, when flags & 1: RZMemory::Free(this) unless
    // flags & 4 (a sized delete that does nothing). Returns this.
    cGZDBReadRecordPackedFile *DeletingDestructor(uint32_t flags);

    bool GetFieldVoid(void *dst, uint32_t n);   // slot 27, 0x9abc70 (ret 8)
    bool SetFieldVoid(const void *src, uint32_t n); // slot 51, 0x9afdf0 (ret 8): a file record is read-only
    bool SeekRelative(int32_t offset);          // slot 56, 0x9afb10 (ret 4)
    void Close();                               // slot 62, 0x9a9ff0
    bool ReadSource(void *dst, uint32_t offset, uint32_t n); // slot 64, 0x9af120 (ret 0xc)
    bool ReadBuffered(void *dst, uint32_t n);   // slot 65, 0x9ae500 (ret 8)
    uint32_t CachedRemaining();                 // slot 66, 0x9a8050

    uint32_t mCacheStart;  // +0x58
    uint32_t mCacheLength; // +0x5c
    uint8_t mCache[0x1000]; // +0x60
};

static_assert(offsetof(cGZDBReadRecordPackedFile, mCacheStart) == 0x58, "cGZDBReadRecordPackedFile: cache start at +0x58");
static_assert(offsetof(cGZDBReadRecordPackedFile, mCacheLength) == 0x5c, "cGZDBReadRecordPackedFile: cache length at +0x5c");
static_assert(offsetof(cGZDBReadRecordPackedFile, mCache) == 0x60, "cGZDBReadRecordPackedFile: cache at +0x60");
static_assert(sizeof(cGZDBReadRecordPackedFile) == cGZDBReadRecordPackedFile::kObjectSize, "cGZDBReadRecordPackedFile: 0x1060 bytes");

// ---- The memory-backed record (0x58 bytes) ----------------------------------------------------------------------------

class cGZDBReadRecordRAM : public cGZDBRecordPackedFile {
public:
    static constexpr uint32_t kVTable = 0xe5f764;
    static constexpr uint32_t kRecordVTable = 0xe5f868;
    static constexpr uint32_t kOStreamVTable = 0xe5f8c0;
    static constexpr uint32_t kIStreamVTable = 0xe5f924;
    static constexpr uint32_t kObjectSize = 0x58;
    static const void *VTable();
    static const void *RecordVTable();
    static const void *OStreamVTable();
    static const void *IStreamVTable();
    static const void *OwnVTable();        // rzface::cGZDBReadRecordRAM's
    static const void *OwnRecordVTable();  // rzface::cGZDBReadRecordRAMRecord's
    static const void *OwnOStreamVTable(); // rzface::cGZDBReadRecordRAMOStream's
    static const void *OwnIStreamVTable(); // rzface::cGZDBReadRecordRAMIStream's

    // 0x9a6e60 (ret 0x14): as the file constructor with kind 2 and the RAM tables; `block` is the counted block (its
    // count dword is incremented here: this record holds a reference), the bytes follow the count. No cache fields.
    cGZDBReadRecordRAM *Construct(void *block, uint32_t size, const cGZPersistResourceKey *key, void *file, uint32_t flag);
    // 0x9a7950 (ret 4): releases the block (count - 1; at zero it goes back to the global allocator), then as the file
    // variant's destructor (the sized delete is 0x58).
    cGZDBReadRecordRAM *DeletingDestructor(uint32_t flags);

    bool GetFieldVoid(void *dst, uint32_t n);   // slot 27, 0x9abd20 (ret 8): the bytes are at block + 4 + position
    bool SetFieldVoid(const void *src, uint32_t n); // slot 51, 0x9afe20 (ret 8): writes into the block, never past the size
    bool SeekRelative(int32_t offset);          // slot 56, 0x9afb50 (ret 4)
    void Close();                               // slot 62, 0x9aa020: also releases the block
};

static_assert(sizeof(cGZDBReadRecordRAM) == cGZDBReadRecordRAM::kObjectSize, "cGZDBReadRecordRAM: 0x58 bytes");

// The vtables as owned statics (owned.h, OwnedVTable).
TS2_OWNED_STATIC(const OwnedVTable, e5f534);
TS2_OWNED_STATIC(const OwnedVTable, e5f644);
TS2_OWNED_STATIC(const OwnedVTable, e5f69c);
TS2_OWNED_STATIC(const OwnedVTable, e5f700);
TS2_OWNED_STATIC(const OwnedVTable, e5f764);
TS2_OWNED_STATIC(const OwnedVTable, e5f868);
TS2_OWNED_STATIC(const OwnedVTable, e5f8c0);
TS2_OWNED_STATIC(const OwnedVTable, e5f924);

inline const void *cGZDBReadRecordPackedFile::VTable() { return &ts2_rva_e5f534; }
inline const void *cGZDBReadRecordPackedFile::RecordVTable() { return &ts2_rva_e5f644; }
inline const void *cGZDBReadRecordPackedFile::OStreamVTable() { return &ts2_rva_e5f69c; }
inline const void *cGZDBReadRecordPackedFile::IStreamVTable() { return &ts2_rva_e5f700; }
inline const void *cGZDBReadRecordRAM::VTable() { return &ts2_rva_e5f764; }
inline const void *cGZDBReadRecordRAM::RecordVTable() { return &ts2_rva_e5f868; }
inline const void *cGZDBReadRecordRAM::OStreamVTable() { return &ts2_rva_e5f8c0; }
inline const void *cGZDBReadRecordRAM::IStreamVTable() { return &ts2_rva_e5f924; }

// ---- Our vtables -------------------------------------------------------------------------------------------------------
//
// The image-free program's tables: one class per subobject of each record, in namespace rzface so that the RTTI name is the
// record's own (tools/matchcheck.py ties our table to the game's by bare RTTI name). The slots forward to the hook-shaped
// members above (and the serial record's, the cGZPersistDBRecord's and the streams'), reached through reinterpret casts:
// the primary face is the object, the +4/+0x20/+0x30 faces are the subobjects and the adjusting ones subtract their offset.
namespace rzface {

namespace readrecord {

inline uint8_t *Bytes(void *p) { return static_cast<uint8_t *>(p); }

// The primary face's slots 0..63 (the same for both variants; Rec supplies the record-specific ones).
template <class Rec> class Primary {
    Rec *Self() { return static_cast<Rec *>(reinterpret_cast<cGZDBRecordPackedFile *>(this)); }
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
    virtual uint32_t GetSize() { return Self()->mSize; }                                                                 // 52
    virtual bool Never53(uint32_t) { return false; }                                                                     // 53
    virtual uint32_t GetPosition() { return Self()->mPosition; }                                                         // 54
    virtual bool Seek(uint32_t position) { return Self()->Seek(position); }                                              // 55
    virtual bool SeekRelative(int32_t offset) { return Self()->SeekRelative(offset); }                                   // 56
    virtual bool GetFieldBlob(void *dst, uint32_t size, uint32_t count) { return Base()->GetFieldBlob(dst, size, count); }          // 57
    virtual bool SetFieldBlob(const void *src, uint32_t size, uint32_t count) { return Base()->SetFieldBlob(src, size, count); }    // 58
    virtual void *SetFieldStringThenOStream(const cIGZString *value) { return Base()->SetFieldStringThenOStream(value); }           // 59
    virtual void *GetOStream(uint32_t unused) { return Base()->GetOStream(unused); }                                                // 60
    virtual void *GetFieldStringThenIStream(cIGZString *dst) { return Base()->GetFieldStringThenIStream(dst); }                     // 61
    virtual void Close() { Self()->Close(); }                                                                                       // 62
    virtual void SetError(uint8_t error) { Self()->mError = error; }                                                                // 63
};

// The +4 face (the cGZPersistDBRecord subobject): `this` is the object + 4.
template <class Rec> class Record {
    Rec *Object() { return static_cast<Rec *>(reinterpret_cast<cGZDBRecordPackedFile *>(Bytes(this) - 4)); }
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
    virtual uint32_t GetSize() { return Object()->mSize; }                                                        // 10
    virtual bool Never11(uint32_t) { return false; }                                                              // 11
    virtual uint32_t GetPosition() { return Object()->mPosition; }                                                // 12
    virtual bool Seek(uint32_t position) { return Object()->Seek(position); }                                     // 13
    virtual bool SeekRelative(int32_t offset) { return Object()->SeekRelative(offset); }                          // 14
    virtual bool QueryChild(uint32_t riid, void **ppvObj) { return Base()->QueryChild(riid, ppvObj); }            // 15
    virtual void SetChild(void *child) { Base()->SetChild(child); }                                               // 16
    virtual bool TestHighFlags(uint8_t mask) { return Base()->TestHighFlags(mask); }                              // 17
    virtual uint32_t SetHighFlags(uint8_t value, uint8_t set) { return Base()->SetHighFlags(value, set); }        // 18
    virtual void *DeletingDestructor(uint32_t flags) { return Object()->DeletingDestructor(flags); }              // 19
    virtual uint32_t SetAccessFlags(uint8_t value) { return Base()->SetAccessFlags(value); }                      // 20
};

// The +0x20 face (the cRZOStream subobject): `this` is the object + 0x20.
template <class Rec> class OStream {
    Rec *Object() { return static_cast<Rec *>(reinterpret_cast<cGZDBRecordPackedFile *>(Bytes(this) - 0x20)); }
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
template <class Rec> class IStream {
    Rec *Object() { return static_cast<Rec *>(reinterpret_cast<cGZDBRecordPackedFile *>(Bytes(this) - 0x30)); }
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

} // namespace readrecord

// File variant. The primary face adds slots 64..66 (the cache).
class cGZDBReadRecordPackedFile final : public readrecord::Primary<::cGZDBReadRecordPackedFile> {
public:
    virtual bool ReadSource(void *dst, uint32_t offset, uint32_t n)
    {
        return reinterpret_cast<::cGZDBReadRecordPackedFile *>(this)->ReadSource(dst, offset, n);
    }                                                                                                 // 64
    virtual bool ReadBuffered(void *dst, uint32_t n) { return reinterpret_cast<::cGZDBReadRecordPackedFile *>(this)->ReadBuffered(dst, n); } // 65
    virtual uint32_t CachedRemaining() { return reinterpret_cast<::cGZDBReadRecordPackedFile *>(this)->CachedRemaining(); }                 // 66
};
class cGZDBReadRecordPackedFileRecord final : public readrecord::Record<::cGZDBReadRecordPackedFile> {};
class cGZDBReadRecordPackedFileOStream final : public readrecord::OStream<::cGZDBReadRecordPackedFile> {};
class cGZDBReadRecordPackedFileIStream final : public readrecord::IStream<::cGZDBReadRecordPackedFile> {};

// RAM variant.
class cGZDBReadRecordRAM final : public readrecord::Primary<::cGZDBReadRecordRAM> {};
class cGZDBReadRecordRAMRecord final : public readrecord::Record<::cGZDBReadRecordRAM> {};
class cGZDBReadRecordRAMOStream final : public readrecord::OStream<::cGZDBReadRecordRAM> {};
class cGZDBReadRecordRAMIStream final : public readrecord::IStream<::cGZDBReadRecordRAM> {};

} // namespace rzface

namespace readrecord_detail {
template <class Face> inline const void *VTableOf()
{
    Face face;
    return *reinterpret_cast<const void *const *>(&face);
}
} // namespace readrecord_detail

inline const void *cGZDBReadRecordPackedFile::OwnVTable() { return readrecord_detail::VTableOf<rzface::cGZDBReadRecordPackedFile>(); }
inline const void *cGZDBReadRecordPackedFile::OwnRecordVTable() { return readrecord_detail::VTableOf<rzface::cGZDBReadRecordPackedFileRecord>(); }
inline const void *cGZDBReadRecordPackedFile::OwnOStreamVTable() { return readrecord_detail::VTableOf<rzface::cGZDBReadRecordPackedFileOStream>(); }
inline const void *cGZDBReadRecordPackedFile::OwnIStreamVTable() { return readrecord_detail::VTableOf<rzface::cGZDBReadRecordPackedFileIStream>(); }
inline const void *cGZDBReadRecordRAM::OwnVTable() { return readrecord_detail::VTableOf<rzface::cGZDBReadRecordRAM>(); }
inline const void *cGZDBReadRecordRAM::OwnRecordVTable() { return readrecord_detail::VTableOf<rzface::cGZDBReadRecordRAMRecord>(); }
inline const void *cGZDBReadRecordRAM::OwnOStreamVTable() { return readrecord_detail::VTableOf<rzface::cGZDBReadRecordRAMOStream>(); }
inline const void *cGZDBReadRecordRAM::OwnIStreamVTable() { return readrecord_detail::VTableOf<rzface::cGZDBReadRecordRAMIStream>(); }
