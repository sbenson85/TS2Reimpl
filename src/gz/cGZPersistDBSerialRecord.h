#pragma once
#include "game/game.h"
#include "game/owned.h"
#include "gz/cGZPersistDBRecord.h"
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"

#include <cstddef>
#include <cstdint>

class cIGZString;
class cRZVariant;
struct cGZPersistResourceKey;

// cGZPersistDBSerialRecord: a record read from or written to a database package file. Primary vtable
// 0xda6098 (cIGZPersistDBSerialRecord and cIGZUnknown at +0). Subobjects: cGZPersistDBRecord at +4
// (vtable 0xda6194), cRZOStream at +32, cRZIStream at +48. The reference count is a uint16 at +8.
// RVAs are 1.18.0.187.
//
// The typed field getters read through raw-read slot 27 of the primary vtable, which is pure in this
// class and supplied by subclasses: bool GetVoid(void *dst, uint32_t n), __thiscall. Multi-byte values
// are byte-reversed in place after the read unless mSkipByteSwap (+0x40) is nonzero. The reversal
// happens whether or not the read succeeded.
//
// The typed field setters are the write side: they write through raw-write slot 51 of the primary
// vtable (also pure here), bool WriteRaw(const void *src, uint32_t n), __thiscall. The single-value
// setters take the value by value on the stack; the counted ones take a source pointer. Multi-byte
// values are byte-reversed unless mSkipByteSwap is nonzero, as for the getters.
//
// Object: 0x44 bytes (base constructor 0x99b7c0, deleting destructor 0x1da060 frees 0x44). Subclasses: the speculative
// loader's cPreloadedRecord (src/ts/cPreloadedRecord.h), the packed file's write record (cGZDBWriteRecordPackedFile), the
// segment records. Structure pass: lane CG (notes/feedback/CG.md has the per-function handoff for the three variant/string
// bodies, the constructor and the destructor).
//
// Build: every function of this class is /O2 ("speed"). The original translation unit has NO C++ EH: GetFieldString
// (std::u16string local), GetFieldVariant (new cRZVariant, a cRZString local), SetFieldVariant (a cRZString local) and
// SetRZCharStr carry no EH frame although they hold objects with destructors across calls. Our build compiles with
// /EHsc, so a C++ object with a destructor there adds a frame: build such locals in raw storage (as SetRZCharStr does)
// and destroy them by hand. Even so the three bodies are difftest work, not byte matches.
//
// Field codes. The variant getter and setter store a uint32 code before the value. Codes are NOT cRZVariant's type tags:
// SetFieldVariant maps tag & 0x7fff through the table at kVariantTypeToFieldCodeRva (u32[20], tags 0..0x13; a larger tag
// reads past it) and ORs 0x8000 back for array tags.
namespace SerialRecordField {
constexpr uint32_t kEmpty = 0;
constexpr uint32_t kUint8 = 1, kUint16 = 2, kUint32 = 3, kUint64 = 4;
constexpr uint32_t kSint8 = 5, kSint16 = 6, kSint32 = 7, kSint64 = 8;
constexpr uint32_t kFloat32 = 9, kFloat64 = 0xa, kBool = 0xb, kRZChar = 0xc;
constexpr uint32_t kUnknown = 0xd, kTag11 = 0xe, kTag12 = 0xf; // tags 0x10..0x12 (unknown, ...): Set writes, then fails
constexpr uint32_t kRZUnicodeChar = 0x10, kChar = 0x11;        // Set only: GetFieldVariant has no scalar case for these
constexpr uint32_t kVoidPtr = 0x12, kTag13 = 0x13;             // Set writes the code, then fails
constexpr uint32_t kArray = 0x8000;                            // | element code; 0x8000 alone: a void (byte) buffer
constexpr uint32_t kVariantTypeToFieldCodeRva = 0xe5e788;      // {0,0xb,1,5,2,6,3,7,4,8,9,0xa,0x11,0x10,0xc,0x12,0xd,0xe,0xf,0x13}
constexpr uint32_t kMaxCountedStringUnits = 200000;            // GetFieldString: a larger length returns false
constexpr uint32_t kStringStackBytes = 0x400;                  // GetFieldString: up to this many bytes on the stack, else new[]
constexpr uint32_t kArrayStackElements = 0x80;                 // GetFieldVariant: up to this many elements on the stack
} // namespace SerialRecordField

// The variant-tag to field-code table as an owned static (owned.h): u32[20] at kVariantTypeToFieldCodeRva.
TS2_OWNED_STATIC(const uint32_t, e5e788)[20];

class cGZPersistDBSerialRecord {
public:
    static constexpr uint32_t kVTable = 0xda6098;
    static constexpr uint32_t kRecordVTable = 0xda6194;   // +4 (cGZPersistDBRecord face)
    static constexpr uint32_t kOStreamVTable = 0xda61ec;  // +0x20
    static constexpr uint32_t kIStreamVTable = 0xda6250;  // +0x30
    // Stored first by the base constructor (the interface's own vtable, before the final ones), then overwritten.
    static constexpr uint32_t kInterfaceVTable = 0xe5e7dc;   // cIGZPersistDBSerialRecord, at +0
    static constexpr uint32_t kBaseRecordVTable = 0xe5e990;  // cGZPersistDBRecord::kVTable, at +4
    static constexpr uint32_t kSize = 0x44;
    // The game's tables are owned statics (owned.h): these return their addresses, the words our constructors and
    // destructors store. Own*VTable() are the image-free program's (rzface:: faces whose slots run the members here).
    static const void *VTable();
    static const void *RecordVTable();
    static const void *OStreamVTable();
    static const void *IStreamVTable();
    static const void *InterfaceVTable();
    static const void *BaseRecordVTable();
    static const void *OwnVTable();
    static const void *OwnRecordVTable();
    static const void *OwnOStreamVTable();
    static const void *OwnIStreamVTable();
    static const void *OwnInterfaceVTable();
    static constexpr int kRawReadSlot = 27;     // primary vtable, 0x6c
    static constexpr int kGetUint32Slot = 13;    // primary vtable, 0x34: GetFieldUint32(void *), single
    static constexpr int kSetUint32Slot = 37;    // primary vtable, 0x94: SetFieldUint32(uint32_t), single
    static constexpr int kRawWriteSlot = 51;     // primary vtable, 0xcc
    static constexpr int kRecordDeleteSlot = 19; // cGZPersistDBRecord subobject's vtable, 0x4c

    // Interface ids the original accepts (no subtraction: compared directly).
    static constexpr uint32_t kIdFirst = 1;      // cIGZUnknown: cGZPersistDBRecord subobject (+4)
    static constexpr uint32_t kIdRecord = 0xe56b8f03;  // cIGZPersistDBRecord: +4
    static constexpr uint32_t kIdSerial = 0xc56b8f08;  // cIGZPersistDBSerialRecord: +0
    static constexpr uint32_t kIdOStream = 0xc51b11cb; // cIGZOStream: +32
    static constexpr uint32_t kIdIStream = 0x051b11bd; // cIGZIStream: +48

    bool QueryInterface(uint32_t riid, void **ppvObj); // slot 0, 0x99cfa0
    uint32_t AddRef();                                 // slot 1, 0x97b790
    uint32_t Release();                                // slot 2, 0x99d0a0: deletes via +4 slot 19 at 1

    // Slots 4/6 and 8/10 and so on: the counted form reads `count` elements into dst.
    // Slots 5/7, 9/11 and so on: the single-value form reads one element.
    bool GetFieldUint8(void *dst, uint32_t count);  // slots 4, 6; 0x99beb0 (tail call, no swap)
    bool GetFieldUint8(void *dst);                  // slots 5, 7; 0x99bea0 (1 byte, no swap)
    bool GetFieldUint16(void *dst, uint32_t count); // slots 8, 10; 0x99bcc0
    bool GetFieldUint16(void *dst);                 // slots 9, 11; 0x99bc80
    bool GetFieldUint32(void *dst, uint32_t count); // slots 12, 14; 0x99bd60
    bool GetFieldUint32(void *dst);                 // slots 13, 15; 0x99bd20
    bool GetFieldUint64(void *dst, uint32_t count); // slots 16, 18; 0x99be20
    bool GetFieldUint64(void *dst);                 // slots 17, 19; 0x99bdc0
    bool GetFieldFloat32(void *dst, uint32_t count); // slot 20; 0x99bbc0 (tail call, no swap)
    bool GetFieldFloat32(void *dst);                 // slot 21; 0x99bbb0 (no swap)
    bool GetFieldFloat64(void *dst, uint32_t count); // slot 22; 0x99bbf0 (tail call, no swap)
    bool GetFieldFloat64(void *dst);                 // slot 23; 0x99bbe0 (no swap)
    bool GetFieldResKey(void *dst);                  // slot 25; 0x99bc10: three uint32s at dst+0, +4, +8

    // Structure pass (lane CG), not implemented yet. All three: __thiscall, ret 4, no EH frame, /O2.
    // Slot 24; 0x99bec0 (948 B): reads a string in one of three encodings into dst (any cIGZString: slots 3 FromChar,
    // 18 Resize, 20 Append). Frame 0x428: header u32, a std::u16string, new[] pointer, a 0x400-byte stack buffer.
    bool GetFieldString(cIGZString *dst);
    // Slot 26; 0x99c280 (2661 B): code (slot 13), new cRZVariant (0x14, ctor 0x9c7c75) + AddRef, one switch over the
    // code (scalars 0..0xc, arrays 0x8000..0x800c, 0x8010, 0x8011); *out only on success, else Release. Frame 0x434.
    bool GetFieldVariant(cRZVariant **out);
    // Slot 50; 0x99d660 (1616 B): code from the tag table, SetFieldUint32(code), then one switch writing the value.
    bool SetFieldVariant(const cRZVariant *value);

    // 0x99b7c0, ret 0xc: the base constructor (subclass constructors call it; the speculative loader inlines its own
    // around it). Not a slot. Store order: [+8] = 0 (count and flags as one dword), +0 = kInterfaceVTable, +4 =
    // kBaseRecordVTable, key copied (movups), child stored and AddRef'd (slot 1) when non-null, cRZOStream::Construct
    // (0x1b4f75) and cRZIStream::Construct (0x1b4d82) each with the flag, +0x40 = flag, then the four final vtables in the
    // order +0, +4, +0x20, +0x30. Returns this. No EH frame.
    // The flag is a whole stack dword: the streams get all of it, +0x40 gets its low byte (so 0x100 means "swap").
    cGZPersistDBSerialRecord *Construct(const cGZPersistResourceKey *key, void *child, uint32_t skipByteSwap);
    // The shared part of the destructors (0x1da060 here, inlined in subclasses' destructors): stores this class's four
    // vtables (+0x30 first), then cRZIStream::Destruct on +0x30, cRZOStream::Destruct on +0x20 and cGZPersistDBRecord::
    // Destruct on +4. Not a function of the original.
    void DestructMembers();
    // Subclasses' destructors end with it (so the compiler gives them the original's unwind action). The class is
    // otherwise used on raw memory: nothing constructs or destroys it through C++.
    ~cGZPersistDBSerialRecord() { DestructMembers(); }
    // 0x1da060, ret 4: reached through the +4 slot 19 thunk 0x1da02b and the +32/+48 slot 21 thunks 0x1da033/0x1da03b
    // (8 bytes each, not hooked). Stores the four vtables (+0x30 first, then +0, +4, +0x20), cRZIStream::Destruct
    // (0x1b52af) on +0x30, cRZOStream::Destruct (0x1b52fb) on +0x20, cGZPersistDBRecord::Destruct (0x99b8c0, ours exists
    // unhooked) on +4, then operator delete(this, 0x44) when flags & 1. Returns this.
    cGZPersistDBSerialRecord *DeletingDestructor(uint32_t flags);

    // Write side. Counted: slots 28/30 (Uint8, tail call), 32/34 (Uint16), 36/38 (Uint32), 40/42 (Uint64),
    // 44 (Float32, tail call with count*4), 46 (Float64, tail call with count*8). Single: slots 29/31,
    // 33, 35 (Sint16), 37/39, 41, 45 (Float32), 47 (Float64).
    bool SetFieldUint8(const void *src, uint32_t count);  // slots 28, 30; 0x99d560 (tail call, no swap)
    bool SetFieldUint8(uint8_t value);                    // slots 29, 31; 0x99d540 (low byte, no swap)
    bool SetFieldUint16(const void *src, uint32_t count); // slots 32, 34; 0x99d1d0
    bool SetFieldUint16(uint16_t value);                  // slot 33; 0x99d5c0
    bool SetFieldSint16(int16_t value);                   // slot 35; 0x99d190 (same as the Uint16 single)
    bool SetFieldUint32(const void *src, uint32_t count); // slots 36, 38; 0x99d2f0
    bool SetFieldUint32(uint32_t value);                  // slots 37, 39; 0x99d2b0
    bool SetFieldUint64(const void *src, uint32_t count); // slots 40, 42; 0x99d3d0
    bool SetFieldUint64(uint64_t value);                  // slot 41; 0x99d600 (8 bytes, reversed unless flag)
    bool SetFieldFloat32(const void *src, uint32_t count); // slot 44; 0x99d0e0 (tail call, count*4)
    bool SetFieldFloat32(float value);                     // slot 45; 0x99d0c0 (no swap)
    bool SetFieldFloat64(const void *src, uint32_t count); // slot 46; 0x99d120 (tail call, count*8)
    bool SetFieldFloat64(double value);                    // slot 47; 0x99d100 (no swap)
    bool SetFieldResKey(const void *key);                  // slot 49; 0x99d140: three uint32s from key

    // Further slots. Sint64 (slot 43, 0x99d4d0) writes the same bytes as the Uint64 single.
    bool SetFieldSint64(int64_t value);                    // slot 43; 0x99d4d0
    bool SetFieldString(const cIGZString *value);          // slot 48; 0x99d570: length (slot 37), then
                                                           // data (slot 6 of the string), via slot 51
    // Blob forms (slots 57, 58): size * count bytes through slot 27 or slot 51, no swap.
    bool GetFieldBlob(void *dst, uint32_t size, uint32_t count);       // slot 57; 0x99cd60
    bool SetFieldBlob(const void *src, uint32_t size, uint32_t count); // slot 58; 0x99dd40
    // Slots 59 and 61 do their string call (ignoring its result) and return the stream subobject; slot
    // 60 returns the cRZOStream subobject without a call. The argument is ignored by slot 60.
    void *SetFieldStringThenOStream(const cIGZString *value); // slot 59; 0x99b930: this+0x20
    void *GetOStream(uint32_t unused);                        // slot 60; 0x99b950: this+0x20
    void *GetFieldStringThenIStream(cIGZString *dst);         // slot 61; 0x99b910: this+0x30

    const void *mVTable;        // +0
    // +4..+0x1f: the cGZPersistDBRecord subobject (src/gz/cGZPersistDBRecord.h, at its offsets + 4).
    const void *mRecordVTable;  // +4: the cGZPersistDBRecord subobject's vtable
    uint16_t mRefCount;         // +8
    uint16_t mRecordFlags;      // +0x0a: the record's flags word (low byte: access flags)
    uint32_t mKey[4];           // +0x0c: cGZPersistResourceKey (type, group, instance, instanceHigh)
    void *mChild;               // +0x1c: optional child (cIGZUnknown*), AddRef'd by the constructor
    cRZOStream mOStream;        // +0x20: vtable kOStreamVTable once constructed
    cRZIStream mIStream;        // +0x30: vtable kIStreamVTable once constructed
    uint8_t mSkipByteSwap;      // +0x40: nonzero: values are taken as stored; zero: byte-reversed
    uint8_t mPad41[3];          // +0x41: padding (the object is 0x44)

private:
    bool RawRead(void *dst, uint32_t n);
    bool RawWrite(const void *src, uint32_t n);
    void ReverseElements(void *dst, uint32_t count, uint32_t width);
    bool WriteSwapped(const void *src, uint32_t count, uint32_t width);
};

// The tables as owned statics (owned.h, OwnedVTable): `offset ts2_rva_<hex>`, the same one-instruction stores as GameAddress.
TS2_OWNED_STATIC(const OwnedVTable, da6098);
TS2_OWNED_STATIC(const OwnedVTable, da6194);
TS2_OWNED_STATIC(const OwnedVTable, da61ec);
TS2_OWNED_STATIC(const OwnedVTable, da6250);
TS2_OWNED_STATIC(const OwnedVTable, e5e7dc);
inline const void *cGZPersistDBSerialRecord::VTable() { return &ts2_rva_da6098; }
inline const void *cGZPersistDBSerialRecord::RecordVTable() { return &ts2_rva_da6194; }
inline const void *cGZPersistDBSerialRecord::OStreamVTable() { return &ts2_rva_da61ec; }
inline const void *cGZPersistDBSerialRecord::IStreamVTable() { return &ts2_rva_da6250; }
inline const void *cGZPersistDBSerialRecord::InterfaceVTable() { return &ts2_rva_e5e7dc; }
inline const void *cGZPersistDBSerialRecord::BaseRecordVTable() { return cGZPersistDBRecord::VTable(); }

// Inline so that subclasses' destructors (cPreloadedRecord's) contain it, as in the original.
inline void cGZPersistDBSerialRecord::DestructMembers()
{
    uint8_t *self = reinterpret_cast<uint8_t *>(this);
    *reinterpret_cast<const void **>(self + 0x30) = IStreamVTable();
    mVTable = VTable();
    mRecordVTable = RecordVTable();
    *reinterpret_cast<const void **>(self + 0x20) = OStreamVTable();
    mIStream.Destruct();
    mOStream.Destruct();
    reinterpret_cast<cGZPersistDBRecord *>(self + 4)->Destruct();
}

// Entries reached through the vtables of the +32 (cRZOStream) and +48 (cRZIStream) subobjects. `this` is
// the subobject, and the primary object is at this - 0x20 or this - 0x30. The original jumps (or calls and
// returns) into the primary's slots with the same arguments, so each one forwards to the slot named here.
// They are members of this helper class, not of cGZPersistDBSerialRecord, so `this` has the right type.
class cGZPersistDBSerialRecordThunks {
public:
    // The +4 subobject (cGZPersistDBRecord) entries of the primary's vtable 0xda6098 / 0xda6194: `this` is
    // the subobject, so the primary is at this - 4. Each tail-jumps to the primary's own function.
    bool QueryInterface4(uint32_t riid, void **ppvObj); // +4 slot 0; 0x1e2de3: primary QueryInterface
    uint32_t AddRef4();                                 // +4 slot 1; 0x1dab02: primary AddRef
    uint32_t Release4();                                // +4 slot 2; 0x1e3950: primary Release

    // The +32 (cRZOStream) and +48 (cRZIStream) subobjects' IUnknown slots: `this` is the subobject, and each
    // subtracts its offset and tail-jumps to the primary's function with the same arguments.
    bool QueryInterface32(uint32_t riid, void **ppvObj); // +32 slot 0; 0x1e2deb: primary QueryInterface
    uint32_t AddRef32();                                 // +32 slot 1; 0x1dab0a: primary AddRef
    uint32_t Release32();                                // +32 slot 2; 0x1e3958: primary Release
    bool QueryInterface48(uint32_t riid, void **ppvObj); // +48 slot 0; 0x1e2df3: primary QueryInterface
    uint32_t AddRef48();                                 // +48 slot 1; 0x1dab12: primary AddRef
    uint32_t Release48();                                // +48 slot 2; 0x1e3960: primary Release

    // +48 slot 18 (GetError). The original subtracts 0x10 and tail-jumps to a shared function that does
    // `xor al, al; ret`, so the result is always 0 in al and nothing else is touched. The original leaves the
    // upper bits of eax as they were on entry; this returns 0 in all of eax. Callers read al only.
    uint8_t GetError48();                                // +48 slot 18; 0x1dff06 (shared target 0x147480)

    // +32 slot 14: `this` is the cRZOStream subobject, the primary is at this - 0x20. Builds a cRZString from text
    // on the stack, calls the primary's slot 48 (SetFieldString) with it, destroys the string and returns the
    // call's result. Not a pure thunk: the string is built and destroyed here.
    bool SetRZCharStr(const char *text);                 // +32 slot 14; 0x99dda0

    bool GetVoid(void *dst, uint32_t n);      // +48 slot 17; 0x99cee0: primary slot 27 (raw read)
    bool SetVoid(const void *src, uint32_t n); // +32 slot 17; 0x99de60: primary slot 51 (raw write)
    bool Skip(uint32_t n);                    // +48 slot 3; 0x99dea0: primary slot 56 (a stub returning false)
    bool GetGZStr(cIGZString *dst);           // +48 slot 15; 0x99cd80: primary slot 24 (GetFieldString)
    bool SetGZStr(const cIGZString *src);     // +32 slot 15; 0x99dd60: primary slot 48 (SetFieldString)
};

static_assert(offsetof(cGZPersistDBSerialRecord, mVTable) == 0, "cGZPersistDBSerialRecord: vtable at +0");
static_assert(offsetof(cGZPersistDBSerialRecord, mRecordVTable) == 4, "cGZPersistDBSerialRecord: +4 subobject");
static_assert(offsetof(cGZPersistDBSerialRecord, mRefCount) == 8, "cGZPersistDBSerialRecord: refcount at +8");
static_assert(offsetof(cGZPersistDBSerialRecord, mSkipByteSwap) == 0x40, "cGZPersistDBSerialRecord: flag at +0x40");
static_assert(offsetof(cGZPersistDBSerialRecord, mRecordFlags) == 0x0a, "cGZPersistDBSerialRecord: record flags at +0xa");
static_assert(offsetof(cGZPersistDBSerialRecord, mKey) == 0x0c, "cGZPersistDBSerialRecord: key at +0xc");
static_assert(offsetof(cGZPersistDBSerialRecord, mChild) == 0x1c, "cGZPersistDBSerialRecord: child at +0x1c");
static_assert(offsetof(cGZPersistDBSerialRecord, mOStream) == 0x20, "cGZPersistDBSerialRecord: cRZOStream at +0x20");
static_assert(offsetof(cGZPersistDBSerialRecord, mIStream) == 0x30, "cGZPersistDBSerialRecord: cRZIStream at +0x30");
static_assert(sizeof(cGZPersistDBSerialRecord) == cGZPersistDBSerialRecord::kSize, "cGZPersistDBSerialRecord: 0x44 bytes");
