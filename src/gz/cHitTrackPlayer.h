#pragma once
#include <cstddef>
#include <cstdint>

// Track player implementing cIHitTrackPlayer and the cIGZSnd family. The primary vtable (RVA
// 0xe82330, 1.18.0.187) and the unsigned count at +0x5c are modelled, plus a few small slots.
//
// Slots of a secondary vtable are called with `this` = the base subobject (object + base offset,
// e.g. +12 for cIGZSnd), so their member functions address fields relative to that pointer. They
// must not be called on a real cHitTrackPlayer except through that subobject.
class cHitTrackPlayer {
public:
    static constexpr uint32_t kVTable = 0xe82330;

    bool QueryInterface(uint32_t riid, void **ppvObj); // slot 0, 0xb366e2
    uint32_t AddRef();                                 // slot 1, 0xb337e7
    uint32_t Release();                                // slot 2, 0xb367c0: deletes via slot 20

    // Primary vtable slots (this = object).
    void *GetMultipleSourcesInterface();               // slot 5, 0xb2f12b: object+0x18, or null
    bool InvokeChainedSlot5();                         // slot 12, 0xb36857: (**(this+0xb4)) slot 5, returns true
    void SetFlagAtD4();                                // slot 16, 0xb368f4: byte +0xd4 = 1
    bool CallSndSlot7(uint32_t, uint32_t);             // slot 25, 0xb35aec: cIGZSnd slot 7, returns false

    // cIGZSnd subobject (this = object+12) slots.
    bool SndFlagAt40();                                // +12 slot 16, 0xb2f914: this+0x40 != 0
    bool SndFlagAt5C();                                // +12 slot 19, 0xb2f91c: this+0x5c != 0
    uint32_t SndQuery13();                             // +12 slot 31, 0xb2f44d: +4 slot 4 (0x13)
    uint32_t SndQuery15();                             // +12 slot 20, 0xb2f458: +4 slot 4 (0x15)
    uint32_t SndQuery16();                             // +12 slot 24, 0xb2f42f: +4 slot 4 (0x16)
    uint32_t SndQuery17();                             // +12 slot 26, 0xb2f419: +4 slot 4 (0x17)
    uint32_t SndQuery49();                             // +12 slot 28, 0xb2f424: +4 slot 4 (0x49)
    bool SndTestId2A();                                // +12 slot 23, 0xb2f904: +4 slot 4 (0x2a) != 0
    uint32_t SndForwardSlot5(uint32_t a, uint32_t, uint32_t); // +12 slot 6, 0xb3648b: slot 5 (a,0,0,0)
    bool SndNotifyEmbedded(uint32_t arg);              // +12 slot 33, 0xb3090e: embedded(+0x20) slot 17

    const void *mVTable;
    uint8_t mBases[0x5c - sizeof(void *)]; // subobject vtables and base data (not modelled)
    uint32_t mRefCount;
};

static_assert(offsetof(cHitTrackPlayer, mRefCount) == 0x5c, "cHitTrackPlayer reference count offset");
