#pragma once
#include "gz/cGZPersistDBSerialRecord.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct cGZPersistResourceKey;

// cRAMShadowRecord (`.?AVcRAMShadowRecord@?A0x02237cae@@`, anonymous namespace in cTSPersistSystem's translation unit):
// a read-only DB serial record over a RAM copy of another record's bytes. cTSPersistSystem's primary slot 39 OpenRecord
// override (0x1e2850, its only creation site; the constructor is inlined there) copies a cached resource's record out of
// the store into one of these and hands that out instead. Lane FK.
//
// Object 0x58 = cGZPersistDBSerialRecord (0x44) + a read cursor, a std::vector<uint8_t> and an error byte. Vtables
// 0xda6470 (+0, 62 slots), 0xda656c (+4, 21), 0xda65c4 (+0x20, 24), 0xda6628 (+0x30, 24). Build: /O2 ("speed").
//
// Slots. Own bodies: 27 GetFieldVoid, 52 GetSize, 55 SeekAbsolute, 56 SeekRelative, and the deleting destructor 0x1da120
// (reached through the +4 slot 19 thunk 0x1da1bc and the +0x20/+0x30 slot 21 thunks 0x1da1c4 / 0x1da1cc). Slot 0 is the
// base's QueryInterface. Folded bodies shared with other classes (shared_vf_*, not ours to hook; the same ones
// cPreloadedRecord uses): 51 SetFieldVoid 0x1e4d50 (error = 1, false; ret 8), 53 SetSize 0x1e5350 (error = 1, false;
// ret 4), 54 GetPosition 0x1674d0 (returns mPosition), +0x20 slot 18 / +0x30 slot 18 GetError (byte +0x54: 0x1c05f0 /
// thunk 0x1dff0e), +0x30 slot 14 0x1474b0 (false). The 8-byte `sub ecx, N; jmp` thunks of the +4 face (slots 8, 10, 13,
// 14, 19) and the stream faces' slot 21 are not hooked. Every other slot is the base class's.
class cRAMShadowRecord : public cGZPersistDBSerialRecord {
public:
    static constexpr uint32_t kVTable = 0xda6470;
    static constexpr uint32_t kRecordVTable = 0xda656c;   // +4
    static constexpr uint32_t kOStreamVTable = 0xda65c4;  // +0x20
    static constexpr uint32_t kIStreamVTable = 0xda6628;  // +0x30
    static constexpr uint32_t kSize = 0x58;

    // Not a function in the original (inlined in 0x1e2850 after operator new(0x58)): cGZPersistDBSerialRecord::Construct
    // (key, nullptr, 1) (0x99b7c0), the four vtables (+0, +4, +0x20, +0x30), then +0x44..+0x50 = 0 and the byte +0x54 = 0.
    // So mSkipByteSwap is 1: values are taken as stored.
    explicit cRAMShadowRecord(const cGZPersistResourceKey *key);

    // Slot 27; 0x1dff20 (63 B, ret 8). size - mPosition < n: mError = 1, false (nothing copied). Otherwise
    // memcpy(dst, data + mPosition, n) (0x8264a0, CRT), mPosition += n, true. No null checks.
    bool GetFieldVoid(void *dst, uint32_t n);
    // Slot 52; 0x1e0940 (7 B): the vector's size.
    uint32_t GetSize();
    // Slot 55; 0x1e4a90 (25 B, ret 4). mPosition = pos, then mPosition = size if pos > size; true.
    bool SeekAbsolute(uint32_t pos);
    // Slot 56; 0x1e4ac0 (80 B, ret 4). delta < 0 && mPosition < -delta: mPosition = 0. Else if (uint32_t)delta >
    // size - mPosition (unsigned): mPosition = size. Else mPosition += delta. Always true.
    // ORIGINAL-BUG: as cPreloadedRecord's, a negative delta that stays in range moves to the END.
    bool SeekRelative(int32_t delta);
    // 0x1da120 (155 B, ret 4), no EH frame: the vector freed (std::vector's _Tidy: the big-block alignment check, then
    // operator delete 0x824624, pointers cleared), then the base destructor inline (the base's four vtables, ~cRZIStream
    // 0x1b52af, ~cRZOStream 0x1b52fb, cGZPersistDBRecord::Destruct 0x99b8c0), then operator delete(this, 0x58) when
    // flags & 1. Returns this. The derived vtables are not stored again (dead stores, removed).
    cRAMShadowRecord *DeletingDestructor(uint32_t flags);
    ~cRAMShadowRecord() = default;

    uint32_t mPosition;           // +0x44: read cursor (GetPosition, the seeks, GetFieldVoid)
    std::vector<uint8_t> mData;   // +0x48: begin, end, capacity; filled by OpenRecord's resize + raw read
    uint8_t mError;               // +0x54: set by a failed read/write/resize; GetError (stream faces' slot 18)
    uint8_t mPad55[3];            // +0x55
};

static_assert(offsetof(cRAMShadowRecord, mPosition) == 0x44, "cRAMShadowRecord: position at +0x44");
static_assert(offsetof(cRAMShadowRecord, mData) == 0x48, "cRAMShadowRecord: vector at +0x48");
static_assert(offsetof(cRAMShadowRecord, mError) == 0x54, "cRAMShadowRecord: error flag at +0x54");
static_assert(sizeof(cRAMShadowRecord) == cRAMShadowRecord::kSize, "cRAMShadowRecord: 0x58 bytes");
