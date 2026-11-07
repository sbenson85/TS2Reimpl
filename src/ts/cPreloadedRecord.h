#pragma once
#include "gz/cGZPersistDBSerialRecord.h"

#include <cstddef>
#include <cstdint>

struct cGZPersistResourceKey;

// cPreloadedRecord (`.?AVcPreloadedRecord@?A0x0565dbc6@@`, anonymous namespace in the speculative loader's translation
// unit): a DB serial record over a block of bytes the loader's worker thread read raw from a packed segment (possibly
// compressed). cTSPersistSpeculativeLoaderPhysicalLayer::Process (0x1f9360) builds one per raw preload and
// LogicalLayer::Update (0x1f8d90) a second one for the decompressed bytes; both inline the constructor. A request's object
// is the +4 face; QueryInterface(kInterfaceId) answers the primary `this`, whose +0x48/+0x50/+0x55 Update reads directly.
// src/ts/cTSPersistSpeculativeLoader.cpp has a private stand-in (`PreloadedRecord`) with the same layout; switch it to
// this class when the class is implemented. Structure pass: lane CG (notes/feedback/CG.md).
//
// Object 0x58 = cGZPersistDBSerialRecord (0x44) + the fields below. Vtables 0xda6f2c (+0, 62 slots), 0xda7028 (+4, 21),
// 0xda7080 (+0x20, 24), 0xda70e4 (+0x30, 24). Build: /O2 ("speed"), and this translation unit HAS C++ EH (the destructor
// has a one-state frame), unlike the base class's.
//
// Slots. Own bodies: 0 QueryInterface, 27 GetFieldVoid (the raw read every typed getter goes through), 55 SeekAbsolute,
// 56 SeekRelative, and the deleting destructor (+4 slot 19). Folded bodies shared with other classes (shared_vf_*, not
// ours to hook): 51 SetFieldVoid 0x1e4d50 (error = 1, false; ret 8), 52 GetSize 0x169f60 (returns mSize), 53 SetSize
// 0x1e5350 (error = 1, false; ret 4), 54 GetPosition 0x1674d0 (returns mPosition); the +4 face's slots 9..12 are 8-byte
// thunks to those, and +0x20/+0x30 slot 18 (GetError) read the byte at +0x54 (0x1c05f0 / thunk 0x1dff0e). Every other
// slot is the base class's. The 8-byte `sub ecx, N; jmp` thunks are not hookable: +4/+0x20/+0x30 slot 0 (0x1f82dd,
// 0x1f82e5, 0x1f82ed), +4 slot 8 (0x1f7d8c), 13 (0x1f8c16), 14 (0x1f8c63), 19 (0x1f7560), +0x20/+0x30 slot 21 (0x1f7568,
// 0x1f7570).
class cPreloadedRecord : public cGZPersistDBSerialRecord {
public:
    static constexpr uint32_t kVTable = 0xda6f2c;
    static constexpr uint32_t kRecordVTable = 0xda7028;   // +4
    static constexpr uint32_t kOStreamVTable = 0xda7080;  // +0x20
    static constexpr uint32_t kIStreamVTable = 0xda70e4;  // +0x30
    static constexpr uint32_t kInterfaceId = 0xcdb88c9a;  // its own face: QueryInterface answers the primary this
    static constexpr uint32_t kSize = 0x58;

    // Not a function in the original: inlined in Process and Update. cGZPersistDBSerialRecord::Construct(&key, nullptr, 1)
    // (0x99b7c0, ret 0xc), then the four vtables (+0, +4, +0x20, +0x30), then mPosition = 0, mData = nullptr, mSize = 0
    // and the word at +0x54 = 0 (mError and mCompressed together). mUncompressedSize is left unset (the loader writes it).
    // So mSkipByteSwap is 1: values are taken as stored.
    void Construct(const cGZPersistResourceKey &key);

    // Slot 0; 0x1f82b0 (45 B, ret 8, no EH). riid == kInterfaceId: AddRef through slot 1 FIRST, then *out = this, true.
    // Otherwise the base cGZPersistDBSerialRecord::QueryInterface (0x99cfa0, verified ours) with the same arguments (a
    // direct call, then ret; not a tail jump).
    bool QueryInterface(uint32_t riid, void **out);
    // Slot 27; 0x1f7d50 (60 B, ret 8). mSize - mPosition < n: mError = 1, false (nothing copied). Otherwise
    // memcpy(dst, mData + mPosition, n) (0x8264a0, CRT), mPosition += n, true. No null checks.
    bool GetFieldVoid(void *dst, uint32_t n);
    // Slot 55; 0x1f8c00 (22 B, ret 4). mPosition = pos, then mPosition = mSize if pos > mSize; true.
    bool SeekAbsolute(uint32_t pos);
    // Slot 56; 0x1f8c20 (67 B, ret 4). delta < 0 && mPosition < -delta: mPosition = 0. Else if (uint32_t)delta >
    // mSize - mPosition (unsigned): mPosition = mSize. Else mPosition += delta. Always true.
    // ORIGINAL-BUG: a negative delta that stays in range compares as a huge unsigned value, so it moves to the END
    // (mPosition = mSize); a negative seek can only reach 0 or mSize. GetFieldString's SeekRelative(-4) hits this.
    bool SeekRelative(int32_t delta);
    // 0x1f74b0 (171 B, ret 4), via the +4 slot 19 thunk. EH: one state (0: the base destructor as the unwind action).
    // Stores this class's four vtables, RZFree(mData) (0x1be3a1, verified) when non-null, then the base destructor inline
    // (the same sequence as 0x1da060: base vtables, ~cRZIStream 0x1b52af, ~cRZOStream 0x1b52fb, cGZPersistDBRecord::
    // Destruct 0x99b8c0), then operator delete(this, 0x58) (0x824624) when flags & 1. Returns this.
    cPreloadedRecord *DeletingDestructor(uint32_t flags);
    // The body of that destructor (this class's vtables, the data block); the base destructor follows it. Declared so
    // the compiler produces the original's EH state (the base's destructor as the unwind action).
    ~cPreloadedRecord();

    uint32_t mPosition;         // +0x44: read cursor (GetPosition, the seeks, GetFieldVoid)
    uint8_t *mData;             // +0x48: the bytes (RZMemory::Malloc by the loader; RZFree by the destructor)
    uint32_t mSize;             // +0x4c: bytes in mData (GetSize)
    uint32_t mUncompressedSize; // +0x50: the packed segment's slot 21 answer; Update allocates this much to decompress
    uint8_t mError;             // +0x54: set by a failed read/write/resize; GetError (stream faces' slot 18) returns it
    uint8_t mCompressed;        // +0x55: the packed segment's slot 41 answer (nonzero: mData is compressed)
    uint8_t mPad56[2];          // +0x56
};

static_assert(offsetof(cPreloadedRecord, mPosition) == 0x44, "cPreloadedRecord: position at +0x44");
static_assert(offsetof(cPreloadedRecord, mData) == 0x48, "cPreloadedRecord: data at +0x48");
static_assert(offsetof(cPreloadedRecord, mSize) == 0x4c, "cPreloadedRecord: size at +0x4c");
static_assert(offsetof(cPreloadedRecord, mUncompressedSize) == 0x50, "cPreloadedRecord: uncompressed size at +0x50");
static_assert(offsetof(cPreloadedRecord, mError) == 0x54, "cPreloadedRecord: error flag at +0x54");
static_assert(offsetof(cPreloadedRecord, mCompressed) == 0x55, "cPreloadedRecord: compressed flag at +0x55");
static_assert(sizeof(cPreloadedRecord) == cPreloadedRecord::kSize, "cPreloadedRecord: 0x58 bytes");
