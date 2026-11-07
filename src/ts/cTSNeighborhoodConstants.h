#pragma once
#include <cstdint>

// cTSConstantsClient (vtable 0xdcd934), the base of the tuning-constant clients: +4 its table, +8 its table id (16 bits,
// slot 1), +0xa a byte (slot 2). cTSSimSystem builds the two game subclasses (src/ts/cTSSimSystem.cpp).
class cTSConstantsClient {
public:
    static constexpr uint32_t kVTable = 0xdcd934;

    // 0x4df1b0 (7 bytes), the destructor body the subclasses' deleting destructors call: restores the base vtable.
    void Destruct();
    // Slot 0, 0x4df3d0 (19 bytes): the persist segment the table is read from: slot 24 of the cached singleton
    // (GetCachedSingleton), then slot 6 of what that answers (a tail call). Slot 2 (0x4df450, 4 bytes, not hookable) is
    // the byte at +0xa.
    void *GetSegment();
};

// cTSNeighborhoodConstants (vtable 0xdc2ef8, 7 slots, 0x28 bytes), the neighbourhood's tuning constants. Fields: +0xc
// loaded (byte; slot 3, 0x47a630, loads them from the float-constants table), +0x10..+0x18 three money/grade values
// (ids 0..2), +0x1c..+0x24 three float factors (ids 3..5: rental deposit factor, business rank multiplier, employee
// wage multiplier).
class cTSNeighborhoodConstants {
public:
    static constexpr uint32_t kVTable = 0xdc2ef8;
    static constexpr uint32_t kObjectSize = 0x28;

    // Slot 3, 0x47a630: builds a cTSFloatConstants on the stack, loads it from the segment slot 0 gives (instance: slot 1's
    // 16 bit id), reads 13 named constants (defaults when missing, added to the table): the +0x10..+0x18 values and the
    // +0x1c..+0x24 factors, then seven game globals (0x10daa30..0x10daa48: start hour, friend count, lot size weight, layout
    // fill, yard multiplier, dirty and broken upkeep penalties), hands three to the cached singleton C (slots 48, 50, 52),
    // saves the table back when it changed and the segment says (its slot 9) it can be written, and sets the loaded byte.
    void Load();
    // Slot 4, 0x417060 (ret 4): restores the vtable, runs the base destructor body and frees the object when
    // flags & 1. Returns this.
    cTSNeighborhoodConstants *DeletingDestructor(uint32_t flags);
    // Slot 5, 0x474100 (ret 8): loads the constants first when not loaded (slot 3); ids 3, 4, 5 copy the float at
    // +0x1c, +0x20, +0x24 into *out and return true (al); other ids return false.
    bool GetFloat(uint32_t id, float *out);
    // Slot 6, 0x4740a0 (ret 8): the same for ids 0, 1, 2 and the words at +0x10, +0x14, +0x18.
    bool GetValue(uint32_t id, uint32_t *out);
};
