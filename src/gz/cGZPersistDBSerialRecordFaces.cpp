// The serial record's owned vtables (docs/standalone-shape.md, "Owned statics"): the faces rzface::X, one per table the
// game's constructor and destructor store, and the base tables under them.
//
//   cGZPersistDBSerialRecord      0xda6098  primary (62 slots)       the record itself
//   cGZPersistDBSerialRecordRecord 0xda6194 +4 (21 slots)            the cGZPersistDBRecord subobject
//   cGZPersistDBSerialRecordOStream 0xda61ec +0x20 (24 slots)        the cRZOStream subobject
//   cGZPersistDBSerialRecordIStream 0xda6250 +0x30 (24 slots)        the cRZIStream subobject
//   cIGZPersistDBSerialRecord     0xe5e7dc  the interface table the constructor stores first (57 pure slots)
//   cGZPersistDBRecord            0xe5e990  the base record's table the constructor stores at +4 first (21 slots)
//   cRZOStream, cRZIStream        0xda29bc, 0xda2a20  the stream bases the stream constructors store first (24 slots)
//
// Each slot runs the hook-shaped member of the same name; a sub-object face is entered with `this` = object + its offset,
// as the game does. The slots the game leaves pure (the raw read and write, the stream-like size and position of the
// primary table, the record table's 8..12) end in PureSlot. In the hooked game the objects keep the game's tables
// (VTable() is the image's symbol); the faces are what an image-free program stores. The derived records (the read
// records, the preloaded record) have their own faces over these.
#include "gz/cGZPersistDBSerialRecord.h"

#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"

namespace rzface {

[[noreturn]] void PureSlot(); // src/gz/cGZPersistDBSegment.cpp

namespace {
inline uint8_t *Bytes(void *p) { return static_cast<uint8_t *>(p); }
} // namespace

// ---- cGZPersistDBRecord (0xe5e990) ---------------------------------------------------------------------------------------

class cGZPersistDBRecord final {
    ::cGZPersistDBRecord *Self() { return reinterpret_cast<::cGZPersistDBRecord *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Self()->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Self()->AddRef(); }                                                     // 1
    virtual uint32_t Release() { return Self()->Release(); }                                                   // 2
    virtual void *Slot3() { return Bytes(this) + 8; }                                                          // 3
    virtual void SetKey(const void *key) { Self()->SetKey(key); }                                              // 4
    virtual uint32_t GetAccessFlags() { return Self()->GetAccessFlags(); }                                     // 5
    virtual bool Close() { return Self()->Close(); }                                                           // 6
    virtual bool Abort() { return Self()->Abort(); }                                                           // 7
    virtual void Pure8(void *, uint32_t) { PureSlot(); }                                                                       // 8
    virtual void Pure9(void *, uint32_t) { PureSlot(); }                                                                       // 9
    virtual void Pure10() { PureSlot(); }                                                                      // 10
    virtual void Pure11(void *) { PureSlot(); }                                                                      // 11
    virtual void Pure12() { PureSlot(); }                                                                      // 12
    virtual void Pure13(void *) { PureSlot(); }                                                                      // 13
    virtual void Pure14(void *) { PureSlot(); }                                                                      // 14
    virtual bool QueryChild(uint32_t riid, void **ppvObj) { return Self()->QueryChild(riid, ppvObj); }         // 15
    virtual void SetChild(void *child) { Self()->SetChild(child); }                                            // 16
    virtual bool TestHighFlags(uint8_t mask) { return Self()->TestHighFlags(mask); }                           // 17
    virtual uint32_t SetHighFlags(uint8_t value, uint8_t set) { return Self()->SetHighFlags(value, set); }     // 18
    virtual void *DeletingDestructor(uint32_t flags) { return Self()->DeletingDestructor(flags); }             // 19
    virtual uint32_t SetAccessFlags(uint8_t value) { return Self()->SetAccessFlags(value); }                   // 20
};

// ---- cRZOStream (0xda29bc) and cRZIStream (0xda2a20) ---------------------------------------------------------------------

class cRZOStream final {
    ::cRZOStream *Self() { return reinterpret_cast<::cRZOStream *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Self()->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return ++Self()->mRefCount; }                                                  // 1 (shared 0x148fb0)
    virtual uint32_t Release() { return Self()->Release(); }                                                   // 2 (shared 0x1b5fc4)
    virtual void Pure3() { PureSlot(); }                                                                       // 3
    virtual uint32_t SetSint8Slot4(uint8_t value) { return Self()->SetSint8(value); }
    virtual uint32_t SetSint8Slot5(uint8_t value) { return Self()->SetSint8(value); }
    virtual uint32_t SetSint16(int16_t value) { return Self()->SetSint16(value); }                             // 6
    virtual uint32_t SetUint16(uint16_t value) { return Self()->SetUint16(value); }                            // 7
    virtual uint32_t SetSint32Slot8(int32_t value) { return Self()->SetSint32(value); }
    virtual uint32_t SetSint32Slot9(int32_t value) { return Self()->SetSint32(value); }
    virtual uint32_t SetSint64Slot10(int64_t value) { return Self()->SetSint64(value); }
    virtual uint32_t SetSint64Slot11(int64_t value) { return Self()->SetSint64(value); }
    virtual uint32_t SetFloat32(float value) { return Self()->SetFloat32(value); }                             // 12
    virtual uint32_t SetFloat64(double value) { return Self()->SetFloat64(value); }                            // 13
    virtual uint8_t SetRZCharStr(const char *text) { return Self()->SetRZCharStr(text); }                      // 14
    virtual uint8_t SetGZStr(cIGZString *text) { return Self()->SetGZStr(text); }                              // 15
    virtual uint32_t SetGZSerializable(void *serializable) { return Self()->SetGZSerializable(serializable); } // 16
    virtual void Pure17(void *, uint32_t) { PureSlot(); }                                                                      // 17
    virtual void Pure18() { PureSlot(); }                                                                      // 18
    virtual void *SetOwner(void *owner) { return Self()->SetOwner(owner); }                                    // 19 (shared 0x1b6617)
    virtual void *GetOwner() { return Self()->mOwner; }                                                        // 20 (shared 0x145770)
    virtual void *DeletingDestructor(uint32_t flags) { return Self()->DeletingDestructor(flags); }             // 21
    virtual bool SetByteOrder(uint32_t order) { return Self()->SetByteOrder(order); }                          // 22 (shared 0x1b5f3c)
    virtual bool True23() { return true; }                                                                     // 23 (shared 0x144910)
};

class cRZIStream final {
    ::cRZIStream *Self() { return reinterpret_cast<::cRZIStream *>(this); }
    ::cRZOStream *Out() { return reinterpret_cast<::cRZOStream *>(this); } // the shared bodies are cRZOStream's, on the same layout

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Self()->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return ++Self()->mRefCount; }                                                  // 1 (shared 0x148fb0)
    virtual uint32_t Release() { return Out()->Release(); }                                                    // 2 (shared 0x1b5fc4)
    virtual void Pure3(void *) { PureSlot(); }                                                                       // 3
    virtual bool GetSint8Slot4(void *dst) { return Self()->GetSint8(dst); }
    virtual bool GetSint8Slot5(void *dst) { return Self()->GetSint8(dst); }
    virtual bool GetSint16Slot6(void *dst) { return Self()->GetSint16(dst); }
    virtual bool GetSint16Slot7(void *dst) { return Self()->GetSint16(dst); }
    virtual bool GetSint32Slot8(void *dst) { return Self()->GetSint32(dst); }
    virtual bool GetSint32Slot9(void *dst) { return Self()->GetSint32(dst); }
    virtual bool GetSint64Slot10(void *dst) { return Self()->GetSint64(dst); }
    virtual bool GetSint64Slot11(void *dst) { return Self()->GetSint64(dst); }
    virtual bool GetFloat32(void *dst) { return Self()->GetFloat32(dst); }                                     // 12
    virtual bool GetFloat64(void *dst) { return Self()->GetFloat64(dst); }                                     // 13
    virtual uint8_t GetRZCharStr(char *buf, uint32_t size) { return Self()->GetRZCharStr(buf, size); }         // 14
    virtual uint8_t GetGZStr(cIGZString *out) { return Self()->GetGZStr(out); }                                // 15
    virtual uint32_t GetGZSerializable(void *serializable) { return Self()->GetGZSerializable(serializable); } // 16
    virtual void Pure17(void *, uint32_t) { PureSlot(); }                                                                      // 17
    virtual void Pure18() { PureSlot(); }                                                                      // 18
    virtual void *SetOwner(void *owner) { return Out()->SetOwner(owner); }                                     // 19 (shared 0x1b6617)
    virtual void *GetOwner() { return Self()->mOwner; }                                                        // 20 (shared 0x145770)
    virtual void *DeletingDestructor(uint32_t flags) { return Self()->DeletingDestructor(flags); }             // 21
    virtual bool SetByteOrder(uint32_t order) { return Out()->SetByteOrder(order); }                           // 22 (shared 0x1b5f3c)
    virtual bool True23() { return true; }                                                                     // 23 (shared 0x144910)
};

// ---- The abstract interface table (0xe5e7dc): 57 pure slots --------------------------------------------------------------

class cIGZPersistDBSerialRecord final {
public:
#define SLOT(n) virtual void Pure##n() { PureSlot(); }
    SLOT(0) SLOT(1) SLOT(2) SLOT(3) SLOT(4) SLOT(5) SLOT(6) SLOT(7) SLOT(8) SLOT(9)
    SLOT(10) SLOT(11) SLOT(12) SLOT(13) SLOT(14) SLOT(15) SLOT(16) SLOT(17) SLOT(18) SLOT(19)
    SLOT(20) SLOT(21) SLOT(22) SLOT(23) SLOT(24) SLOT(25) SLOT(26) SLOT(27) SLOT(28) SLOT(29)
    SLOT(30) SLOT(31) SLOT(32) SLOT(33) SLOT(34) SLOT(35) SLOT(36) SLOT(37) SLOT(38) SLOT(39)
    SLOT(40) SLOT(41) SLOT(42) SLOT(43) SLOT(44) SLOT(45) SLOT(46) SLOT(47) SLOT(48) SLOT(49)
    SLOT(50) SLOT(51) SLOT(52) SLOT(53) SLOT(54) SLOT(55) SLOT(56)
#undef SLOT
};

// ---- The serial record's four tables -------------------------------------------------------------------------------------

// The primary table (0xda6098): `this` is the record.
class cGZPersistDBSerialRecord final {
    ::cGZPersistDBSerialRecord *Base() { return reinterpret_cast<::cGZPersistDBSerialRecord *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Base()->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Base()->AddRef(); }                                                      // 1
    virtual uint32_t Release() { return Base()->Release(); }                                                    // 2
    virtual void *GetRecordInterface() { return this != nullptr ? Bytes(this) + 4 : nullptr; }                  // 3 (shared 0x1c04e0)
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
    virtual void Pure27(void *, uint32_t) { PureSlot(); }                                                                          // 27 (raw read)
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
    virtual void Pure51(void *, uint32_t) { PureSlot(); }                                                                                // 51 (raw write)
    virtual void Pure52() { PureSlot(); }                                                                                // 52
    virtual void Pure53(void *) { PureSlot(); }                                                                                // 53
    virtual void Pure54() { PureSlot(); }                                                                                // 54
    virtual bool Seek(uint32_t) { return false; }                                                                        // 55 (shared 0x18a7a0)
    virtual bool SeekRelative(int32_t) { return false; }                                                                 // 56 (shared 0x18a7a0)
    virtual bool GetFieldBlob(void *dst, uint32_t size, uint32_t count) { return Base()->GetFieldBlob(dst, size, count); }       // 57
    virtual bool SetFieldBlob(const void *src, uint32_t size, uint32_t count) { return Base()->SetFieldBlob(src, size, count); } // 58
    virtual void *SetFieldStringThenOStream(const cIGZString *value) { return Base()->SetFieldStringThenOStream(value); }        // 59
    virtual void *GetOStream(uint32_t unused) { return Base()->GetOStream(unused); }                                             // 60
    virtual void *GetFieldStringThenIStream(cIGZString *dst) { return Base()->GetFieldStringThenIStream(dst); }                  // 61
};

// The +4 table (0xda6194): `this` is the record + 4.
class cGZPersistDBSerialRecordRecord final {
    ::cGZPersistDBSerialRecord *Object() { return reinterpret_cast<::cGZPersistDBSerialRecord *>(Bytes(this) - 4); }
    ::cGZPersistDBRecord *Base() { return reinterpret_cast<::cGZPersistDBRecord *>(this); }
    cGZPersistDBSerialRecordThunks *Thunks() { return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Thunks()->QueryInterface4(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Thunks()->AddRef4(); }                                                     // 1
    virtual uint32_t Release() { return Thunks()->Release4(); }                                                   // 2
    virtual void *Slot3() { return Bytes(this) + 8; }                                                             // 3 (shared 0x142840)
    virtual void SetKey(const void *key) { Base()->SetKey(key); }                                                 // 4
    virtual uint32_t GetAccessFlags() { return Base()->GetAccessFlags(); }                                        // 5
    virtual bool Close() { return Base()->Close(); }                                                              // 6
    virtual bool Abort() { return Base()->Abort(); }                                                              // 7
    virtual void Pure8() { PureSlot(); }                                                                          // 8
    virtual void Pure9() { PureSlot(); }                                                                          // 9
    virtual void Pure10() { PureSlot(); }                                                                         // 10
    virtual void Pure11() { PureSlot(); }                                                                         // 11
    virtual void Pure12() { PureSlot(); }                                                                         // 12
    virtual bool False13(uint32_t) { return false; }                                                              // 13 (0x1e4a82 -> 0x18a7a0)
    virtual bool False14(uint32_t) { return false; }                                                              // 14 (0x1e4a82 -> 0x18a7a0)
    virtual bool QueryChild(uint32_t riid, void **ppvObj) { return Base()->QueryChild(riid, ppvObj); }            // 15
    virtual void SetChild(void *child) { Base()->SetChild(child); }                                               // 16
    virtual bool TestHighFlags(uint8_t mask) { return Base()->TestHighFlags(mask); }                              // 17
    virtual uint32_t SetHighFlags(uint8_t value, uint8_t set) { return Base()->SetHighFlags(value, set); }        // 18
    virtual void *DeletingDestructor(uint32_t flags) { return Object()->DeletingDestructor(flags); }              // 19 (0x1da02b)
    virtual uint32_t SetAccessFlags(uint8_t value) { return Base()->SetAccessFlags(value); }                      // 20
};

// The +0x20 table (0xda61ec): `this` is the record + 0x20.
class cGZPersistDBSerialRecordOStream final {
    ::cGZPersistDBSerialRecord *Object() { return reinterpret_cast<::cGZPersistDBSerialRecord *>(Bytes(this) - 0x20); }
    ::cRZOStream *Base() { return reinterpret_cast<::cRZOStream *>(this); }
    cGZPersistDBSerialRecordThunks *Thunks() { return reinterpret_cast<cGZPersistDBSerialRecordThunks *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Thunks()->QueryInterface32(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Thunks()->AddRef32(); }                                                     // 1
    virtual uint32_t Release() { return Thunks()->Release32(); }                                                   // 2
    virtual void Nop3() {}                                                                                         // 3 (shared 0x140fe0)
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
    virtual uint8_t GetError() { return 0; }                                                                       // 18 (shared 0x147480)
    virtual void *SetOwner(void *owner) { return Base()->SetOwner(owner); }                                        // 19 (shared 0x1b6617)
    virtual void *GetOwner() { return Base()->mOwner; }                                                            // 20 (shared 0x145770)
    virtual void *DeletingDestructor(uint32_t flags) { return Object()->DeletingDestructor(flags); }              // 21 (0x1da033)
    virtual bool SetByteOrder(uint32_t order) { return Base()->SetByteOrder(order); }                              // 22 (shared 0x1b5f3c)
    virtual bool True23() { return true; }                                                                         // 23 (shared 0x144910)
};

// The +0x30 table (0xda6250): `this` is the record + 0x30.
class cGZPersistDBSerialRecordIStream final {
    ::cGZPersistDBSerialRecord *Object() { return reinterpret_cast<::cGZPersistDBSerialRecord *>(Bytes(this) - 0x30); }
    ::cRZIStream *Base() { return reinterpret_cast<::cRZIStream *>(this); }
    ::cRZOStream *Out() { return reinterpret_cast<::cRZOStream *>(this); } // the shared bodies are cRZOStream's, on the same layout
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
    virtual bool False14(char *, uint32_t) { return false; }                                                       // 14 (shared 0x1474b0)
    virtual bool GetGZStr(cIGZString *out) { return Thunks()->GetGZStr(out); }                                     // 15
    virtual uint32_t GetGZSerializable(void *serializable) { return Base()->GetGZSerializable(serializable); }     // 16
    virtual bool GetVoid(void *dst, uint32_t n) { return Thunks()->GetVoid(dst, n); }                              // 17
    virtual uint8_t GetError() { return Thunks()->GetError48(); }                                                  // 18
    virtual void *SetOwner(void *owner) { return Out()->SetOwner(owner); }                                         // 19 (shared 0x1b6617)
    virtual void *GetOwner() { return Base()->mOwner; }                                                            // 20 (shared 0x145770)
    virtual void *DeletingDestructor(uint32_t flags) { return Object()->DeletingDestructor(flags); }               // 21 (0x1da03b)
    virtual bool SetByteOrder(uint32_t order) { return Out()->SetByteOrder(order); }                               // 22 (shared 0x1b5f3c)
    virtual bool True23() { return true; }                                                                         // 23 (shared 0x144910)
};

} // namespace rzface

namespace {
template <class Face> const void *VTableOf()
{
    Face face;
    return *reinterpret_cast<const void *const *>(&face);
}
} // namespace

const void *cGZPersistDBRecord::OwnVTable() { return VTableOf<rzface::cGZPersistDBRecord>(); }
const void *cRZOStream::OwnVTable() { return VTableOf<rzface::cRZOStream>(); }
const void *cRZIStream::OwnVTable() { return VTableOf<rzface::cRZIStream>(); }
const void *cGZPersistDBSerialRecord::OwnVTable() { return VTableOf<rzface::cGZPersistDBSerialRecord>(); }
const void *cGZPersistDBSerialRecord::OwnRecordVTable() { return VTableOf<rzface::cGZPersistDBSerialRecordRecord>(); }
const void *cGZPersistDBSerialRecord::OwnOStreamVTable() { return VTableOf<rzface::cGZPersistDBSerialRecordOStream>(); }
const void *cGZPersistDBSerialRecord::OwnIStreamVTable() { return VTableOf<rzface::cGZPersistDBSerialRecordIStream>(); }
const void *cGZPersistDBSerialRecord::OwnInterfaceVTable() { return VTableOf<rzface::cIGZPersistDBSerialRecord>(); }
