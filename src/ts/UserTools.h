#pragma once
#include <cstdint>

// The build/buy-mode user tools: cTSUserToolBase, cTSUserToolBuyBuild and cTSUserToolGroundBased, at object +0 of
// each other (single inheritance, no virtual bases). RVAs are 1.18.0.187. Slot N of a vtable is at byte offset 4N.
//
// The classes have no data members here: the functions reach their fields by offset from `this`.
//
// cTSUserToolBase (primary vtable 0xdfde3c, 38 slots):
//   +0x04  delegate: pointer to an object whose vtable the forwarders call; null makes them do nothing
//   +0x14  dword copied from the first dword of *arg3 by StoreAndSelfSlot17
//   +0x18  dword copied from the second dword of *arg3
//
// cTSUserToolBuyBuild (no vtable of its own here; its functions are at +0 of the same object):
//   +0x4c  byte cleared by ClearByte4C
//
// cTSUserToolGroundBased (vtable 0xdfe0ec, 63 slots):
//   +0x50  byte flag: set by SetFlag50 (slot 59), cleared by ClearFlag50 (slot 60) and tested by EnsureFlag50
//   +0x54  sub-object whose address is passed to slot 57 by ClearByte4CAndNotify
//   +0x60  byte flag: cleared by ClearFlagsAfterSlot60, tested by ClearByte4CAndNotify
//   +0x61  byte stored by SetByte61
//
// Calls through an object's own vtable (`call [eax+N]`) go through the object's vtable at +0 and are made with
// VirtualSlot, in the same order as the original. Stack arguments follow the original's `ret N`: a member with N/4
// parameters pops them, and the forwarders' extra arguments are the ones the original pushes.
class cTSUserToolBase {
public:
    static constexpr uint32_t kVTable = 0xdfde3c; // 38 slots

    // Slot 22, 0x78d745: ret 0. Calls the delegate's slot 37 with no arguments; does nothing without a delegate.
    void ForwardSlot37();
    // Slot 23, 0x78d916: ret 4. Tail-jumps to the delegate's slot 17 with the same argument. Without a delegate the
    // original returns with eax untouched, so there is no defined result (the replacement returns 0).
    uint32_t ForwardSlot17(uint32_t a1);
    // Slot 29, 0x78d789: ret 0x14. Tail-jumps to the delegate's slot 23 with the five arguments; returns 0 without one.
    uint32_t ForwardSlot23(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
    // Slot 30, 0x78d64d: ret 0xc. Tail-jumps to the delegate's slot 24 with the three arguments; returns 0 without one.
    uint32_t ForwardSlot24(uint32_t a1, uint32_t a2, uint32_t a3);
    // Slot 31, 0x78d6c3: ret 0x10. Calls the delegate's slot 25 as (a1, a2, a3, 1, a4) and returns whether the low
    // byte of its result is nonzero (al only; the original leaves the other bits of eax alone).
    bool ForwardSlot25Flag(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
    // Slot 32, 0x78d6f0: ret 0x10. Calls the delegate's slot 26 as (a1, a2, a3, a4, 1) and returns the low byte != 0.
    bool ForwardSlot26Flag(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
    // Slot 9, 0x78d755: ret 0x10. Calls this's own slot 18 with (a1, a2, a3); the fourth argument is not used.
    uint32_t SelfSlot18(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t unused);
    // Slot 8, 0x78d769: ret 0x10. Copies the two dwords at a3 to +0x14 and +0x18, then calls this's own slot 17 with
    // (a1, a2, a3); the fourth argument is not used.
    uint32_t StoreAndSelfSlot17(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t unused);
};

class cTSUserToolBuyBuild : public cTSUserToolBase {
public:
    // Slot 44, 0x78e747: ret 4. Clears the byte at +0x4c; the argument is ignored.
    void ClearByte4C(uint32_t a1);
};

class cTSUserToolGroundBased : public cTSUserToolBuyBuild {
public:
    static constexpr uint32_t kVTable = 0xdfe0ec; // 63 slots

    // Slot 38, 0x78eb3c: ret 0. If the flag at +0x50 is clear, tail-jumps to this's slot 59 (sets the flag).
    void EnsureFlag50();
    // Slot 44, 0x78ec49: ret 4. Calls BuyBuild's slot 44 (ClearByte4C), then, if +0x60 is set, calls this's slot 57
    // with the address of +0x54.
    void ClearByte4CAndNotify(uint32_t a1);
    // Slot 49, 0x78eb4b: ret 0. If the flag at +0x50 is set, calls this's slot 60 (clears the flag); then clears +0x60.
    void ClearFlagsAfterSlot60();
    // Slot 59, 0x67d331: ret 0. Sets the byte at +0x50 to 1.
    void SetFlag50();
    // Slot 60, 0x67d336: ret 0. Clears the byte at +0x50.
    void ClearFlag50();
    // Slot 62, 0x78eca1: ret 4. Stores the low byte of the argument at +0x61.
    void SetByte61(uint32_t a1);
};
