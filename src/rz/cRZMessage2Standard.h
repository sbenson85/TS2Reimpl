#pragma once
#include "game/owned.h"

#include <cstddef>
#include <cstdint>

// cRZMessage2Standard: the framework's standard message object (cIGZMessage2Standard, cIGZMessage2,
// cIGZUnknown). An id plus data fields; the message server keeps these in ordered containers, so
// the id comparisons are part of its contract. Original vtable: RVA 0xda4064 (38 slots). RVAs are
// 1.18.0.187. Object size 0x2c. Layout as the original uses it:
//   +0x00  vtable pointer
//   +0x04  uint32 reference count (AddRef is slot 1, shared code; Release is slot 2 here)
//   +0x08  uint8 flags: bit 0 = the id has been set (SetId); bits 1-4 = mValue[0..3] set
//          (SetValueN); bit 5 = mPtr28 set; bit 6 = mPtr20 set; bit 7 = mPtr24 set. The getters
//          read the bits; the setters only ever set them, and SetId clears bit 0 for a zero id.
//   +0x0C  uint32 id
//   +0x10  uint32 mValue[4] (+0x10, +0x14, +0x18, +0x1C)
//   +0x20  pointer mPtr20 (the string, cIGZMessage2Standard slots 28-30), +0x24 pointer mPtr24 (the
//          counted object, slots 31-33), +0x28 pointer mPtr28 (the extra object, slots 34-36): owned
//          references, set through RZUnknownSlot::Assign (0x142080) and released by the destructor.
// The class keeps the game's vtable (the hook shape): every slot is hooked at its RVA, and so are the
// constructor and destructor, which other code calls directly (the constructor from about 400 places).
// Slots 19 and 20 (0x148ba0, a 4-byte getter of +0x18 folded with other classes) are not hookable.
class cRZMessage2Standard {
public:
    static constexpr uint32_t kVTable = 0xda4064;
    static constexpr uint32_t kSize = 0x2c;

    static constexpr uint8_t kFlagId = 0x01;
    static constexpr uint8_t kFlagValue0 = 0x02;
    static constexpr uint8_t kFlagValue1 = 0x04;
    static constexpr uint8_t kFlagValue2 = 0x08;
    static constexpr uint8_t kFlagValue3 = 0x10;
    static constexpr uint8_t kFlagPtr28 = 0x20;
    static constexpr uint8_t kFlagPtr20 = 0x40;
    static constexpr uint8_t kFlagPtr24 = 0x80;

    // Slot 0 (cIGZUnknown::QueryInterface), 0x1c0660. Accepts IIDs 1, 0x65297976 and 0xa52294b4,
    // storing this in *out and taking a reference; other IIDs return false and leave *out alone.
    bool QueryInterface(uint32_t riid, void **out);
    // Slot 2 (cIGZUnknown::Release), 0x1c0804. Decrements the count; at one or less it calls the
    // deleting destructor (slot 37) with the delete flag and returns 0.
    uint32_t Release();
    // Slot 5, 0x1c0915: stores the id and sets or clears kFlagId (set when the id is nonzero).
    void SetId(uint32_t id);
    // Slot 6, 0x1c03c8: true when this id equals other's id (other's slot 4, its GetId).
    bool IdEquals(const void *other);
    // Slot 7, 0x1c03de: true when this id is below other's id (unsigned compare).
    bool IdLess(const void *other);
    // Slots 8, 13, 18, 23: whether mValue[0..3] has been set (flag bits 1-4).
    bool HasValue0() const;
    bool HasValue1() const;
    bool HasValue2() const;
    bool HasValue3() const;
    // Slots 11, 16, 21, 26: store the value and set the flag (never clear it).
    void SetValue0(uint32_t value);
    void SetValue1(uint32_t value);
    void SetValue2(uint32_t value);
    void SetValue3(uint32_t value);
    // Slots 28 and 31: whether mPtr20 (flag bit 6) and mPtr24 (flag bit 7) are set.
    bool HasPtr20() const;
    bool HasPtr24() const;

    // 0x1c033d: the game's vtable, every field zero (the flag byte only: bytes 9-11 are left alone);
    // returns this.
    cRZMessage2Standard *Construct();
    // 0x1c0388: the game's vtable again, then mPtr28, mPtr24 and mPtr20 released (slot 2), each
    // when set.
    void Destruct();
    // Slot 3, 0x1c04ea (Create): a new message from RZMemory::Malloc (0x2c bytes), constructed, asked
    // for `iid` (its slot 0) into *out. When it answers, it takes this message's type (this slot 4 into
    // its slot 5) and the result is true; otherwise it is deleted (its slot 37 with 1) and the result
    // is false. A failed allocation is not checked (the null object is called, as the original).
    bool Create(uint32_t iid, void **out);
    // Slot 29, 0x1c05f4: when this has a string (its slot 28), the string's QueryInterface(iid, out)
    // and its result; false otherwise.
    bool GetString(uint32_t iid, void **out);
    // Slot 30, 0x1c08f2: kFlagPtr20 set for a non-null string, cleared for null; the string counted
    // into mPtr20 (RZUnknownSlot::Assign). Returns the field (what Assign returns).
    void *SetString(void *string);
    // Slot 33, 0x1c08cf: the same for the counted object, kFlagPtr24 and mPtr24.
    void *SetObject(void *object);
    // Slot 34, 0x1c05c3: whether mPtr28 is set (flag bit 5).
    bool HasExtra() const;
    // Slot 35, 0x1c0582: slot 29 for the extra object (this slot 34, mPtr28).
    bool GetExtra(uint32_t iid, void **out);
    // Slot 36, 0x1c08ac: the same as slot 30 for the extra object, kFlagPtr28 and mPtr28.
    void *SetExtra(void *object);
    // Slot 37, 0x1c042a (deleting destructor): Destruct, then with flag 1 the block freed through
    // RZMemory::Free unless flag 4 is also set (then nothing: the original calls an empty function
    // with the size); returns this.
    cRZMessage2Standard *DeletingDestructor(uint32_t flags);

    // Slots of our table (rz/cRZMessage2StandardTable.cpp) doing what the image's folded getters do (each shared with
    // other classes, never hooked).
    uint32_t AddRef();            // slot 1, 0x148fb0: the count plus one, returned
    uint32_t GetId() const;       // slot 4, 0x145770: +0x0c
    uint32_t GetValue0() const;   // slots 9 and 10, 0x149cf0: +0x10
    uint32_t GetValue1() const;   // slots 14 and 15, 0x149d00: +0x14
    uint32_t GetValue2() const;   // slots 19 and 20, 0x148ba0: +0x18
    uint32_t GetValue3() const;   // slots 24 and 25, 0x147d00: +0x1c
    void *GetObjectField() const; // slot 32, 0x15e970: +0x24, as it is (no reference taken)

    const void *mVTable;     // +0x00
    uint32_t mRefCount;      // +0x04
    uint8_t mFlags;          // +0x08
    uint32_t mId;            // +0x0C
    uint32_t mValue[4];      // +0x10
    void *mPtr20;            // +0x20
    void *mPtr24;            // +0x24
    void *mPtr28;            // +0x28
};

static_assert(sizeof(cRZMessage2Standard) == cRZMessage2Standard::kSize, "cRZMessage2Standard layout");
static_assert(offsetof(cRZMessage2Standard, mFlags) == 0x08, "cRZMessage2Standard layout");
static_assert(offsetof(cRZMessage2Standard, mId) == 0x0C, "cRZMessage2Standard layout");
static_assert(offsetof(cRZMessage2Standard, mValue) == 0x10, "cRZMessage2Standard layout");
static_assert(offsetof(cRZMessage2Standard, mPtr20) == 0x20, "cRZMessage2Standard layout");
static_assert(offsetof(cRZMessage2Standard, mPtr24) == 0x24, "cRZMessage2Standard layout");
static_assert(offsetof(cRZMessage2Standard, mPtr28) == 0x28, "cRZMessage2Standard layout");

// The word our constructor and destructor store (lane RU): the image's table in a hooked build; an image-free
// program's is ours (rz/cRZMessage2StandardTable.cpp; standalone/owned_statics.cpp aliases it).
TS2_OWNED_STATIC(const OwnedVTable, da4064);
extern "C" const void *ts2_own_vt_da4064[38];
