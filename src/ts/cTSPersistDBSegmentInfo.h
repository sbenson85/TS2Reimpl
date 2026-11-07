#pragma once
#include "rz/cRZRefCount.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// cTSPersistDBSegmentInfo: describes one database segment (two strings, a 16-bit id and a flag word).
// Implements cITSPersistDBSegmentInfo (vtable 0xda6020, 22 slots, at +0) and, through the cRZUnknown at +4
// (vtable 0xda607c, 6 slots), cIGZUnknown. The reference count is the cRZUnknown's (+8), and deleting goes
// through that subobject's slot 3 (0x1da04b, which steps back to the primary and runs 0x1da210).
// The object is created only by 0x1df210 (new, 0x5c bytes, the constructor inlined). RVAs are 1.18.0.187.
//
// Slots whose bodies are named here but not reimplemented because they call unverified helpers of other
// groups: vf10 (0x1e0a60), vf12 (0x1e0bd0, calls 0x1e52f0), vf14 (0x1e4f40) and vf16 (0x1e5090).
class cTSPersistDBSegmentInfo {
public:
    static constexpr uint32_t kVTable = 0xda6020;
    static constexpr uint32_t kUnknownVTable = 0xda607c;
    static constexpr uint32_t kIdSegmentInfo = 0x51509de7; // cITSPersistDBSegmentInfo, compared directly
    static constexpr int kSetStringsSlot = 12;             // vf11 calls this slot: (a1, string1, string2, a4, a5)
    static constexpr int kSetPathSlot = 14;                // vf15 calls this slot: (const char *, a2)

    // The object is built by this function (the original's constructor is inlined into it): __cdecl,
    // no arguments, returns null only if allocation returned null.
    static cTSPersistDBSegmentInfo *Create();                             // 0x1df210
    cTSPersistDBSegmentInfo *DeletingDestructor(uint32_t flags);          // 0x1da210 (reached through the +4 slot 3 thunk)

    bool QueryInterface(uint32_t riid, void **ppvObj);                    // slot 0, 0x1e2e10
    uint32_t AddRef();                                                    // slot 1, 0x1dab20 (folded; not hooked)
    uint32_t Release();                                                   // slot 2, 0x1e3970 (folded; not hooked)
    cRZString *GetString1();                                              // slot 4, 0x147780 (folded; not hooked)
    cRZString *GetString2();                                              // slot 5, 0x1c6e40 (folded; not hooked)
    uint32_t GetField50();                                                // slot 6, 0x1e0950 (folded; not hooked)
    uint16_t GetField54();                                                // slot 7, 0x1e0540
    uint32_t GetFlags();                                                  // slot 8, 0x1dff70 (folded; not hooked)
    bool TestFlags(uint32_t mask);                                        // slot 9, 0x1dcc40: all bits of mask set
    // slot 11 (0x1e0c20): builds two strings from the C strings and calls slot 12 with them. The original's
    // eax on return is whatever its last helper left; nothing relies on it.
    void SetFromChars(uint32_t arg1, const char *text1, const char *text2, uint32_t arg4, uint32_t arg5);
    uint32_t SetString2FromChar(const char *text);                        // slot 17, 0x1e4dc0: tail call, slot 4 of string 2
    uint32_t AssignString2(const void *other);                            // slot 18, 0x1e4db0: tail call, slot 16 of string 2
    void SetField50(uint32_t value);                                      // slot 19, 0x1e5370 (folded; not hooked)
    void SetField54(uint16_t value);                                      // slot 20, 0x1e5250
    void ChangeFlags(uint32_t mask, bool set);                            // slot 21, 0x1e4d70: |= mask or &= ~mask
    uint32_t SetPathFromString(void *string, uint32_t arg2);                  // slot 15, 0x1e4f20: slot 14 with string->slot 5(); returns its result

    const void *mVTable;     // +0x00
    cRZUnknown mUnknown;     // +0x04: vtable 0xda607c, reference count at +0x08
    uint32_t mField0C;       // +0x0c: slot 3 getter (0x145770), slot 13 setter (0x149c60)
    cRZString mString1;      // +0x10
    cRZString mString2;      // +0x30
    uint32_t mField50;       // +0x50
    uint16_t mField54;       // +0x54: starts as 0xffff
    uint16_t mPad56;         // +0x56: never written
    uint32_t mFlags;         // +0x58: starts as 1
};

static_assert(sizeof(cTSPersistDBSegmentInfo) == 0x5c, "cTSPersistDBSegmentInfo layout");
static_assert(offsetof(cTSPersistDBSegmentInfo, mUnknown) == 4, "cTSPersistDBSegmentInfo layout");
static_assert(offsetof(cTSPersistDBSegmentInfo, mString1) == 0x10, "cTSPersistDBSegmentInfo layout");
static_assert(offsetof(cTSPersistDBSegmentInfo, mString2) == 0x30, "cTSPersistDBSegmentInfo layout");
static_assert(offsetof(cTSPersistDBSegmentInfo, mField54) == 0x54, "cTSPersistDBSegmentInfo layout");
static_assert(offsetof(cTSPersistDBSegmentInfo, mFlags) == 0x58, "cTSPersistDBSegmentInfo layout");

// Entries of the +4 vtable (cRZUnknown subobject, 0xda607c): `this` is the subobject, 4 bytes into the primary.
class cTSPersistDBSegmentInfoThunks {
public:
    bool QueryInterface4(uint32_t riid, void **ppvObj);     // slot 0, 0x1e2e48: primary QueryInterface
    uint32_t AddRef4();                                     // slot 1, 0x1dab28 (folded; not hooked)
    uint32_t Release4();                                    // slot 2, 0x1e3978 (folded; not hooked)
    void *DeletingDestructor4(uint32_t flags);              // slot 3, 0x1da04b: primary DeletingDestructor
};
