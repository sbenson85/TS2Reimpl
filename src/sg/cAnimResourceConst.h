#pragma once
#include <cstddef>
#include <cstdint>

// nGZSceneDBAnim::cAnimResourceConst is a cSGResource-derived object. Its nGZSceneDBAnim::cIAnimResourceConst
// interface is a secondary subobject at object +0x2c with a 66-slot vtable (0xe66470). The functions below are
// that subobject's own slots: their `this` is object + 0x2c, and the vtable points at the bodies directly (no
// thunks), so the bodies are hooked by RVA.
//
// Layout relative to the subobject (S = object + 0x2c), as far as these functions touch it:
//   S+0x08 (object +0x34): flag word; bits 0-1 are set by SetFlagBits
//   S+0x18 (object +0x44): pointer to the animation record (read-only data, see below)
//   S+0x1c (object +0x48): pointer to an array of 28-byte entries (EntryAt)
//
// The record (R = *(S+0x18)) fields read here:
//   R+0x02: 16-bit word           R+0x08: 32-bit flag/bit field word (bits 0, 1, 8-11, 12-23 read here)
//   R+0x14, R+0x18: 32-bit words  R+0x1c, +0x20, +0x24, +0x28, +0x2c, +0x30, +0x34, +0x38, +0x3c: floats
//
// Slot numbers are for the subobject vtable (slot N is at byte offset 4N). RVAs are 1.18.0.187.
namespace nGZSceneDBAnim {

class cAnimResourceConstSubobject {
public:
    static constexpr uint32_t kVTable = 0xe66470;       // the subobject's vtable (66 slots)
    static constexpr uint32_t kPrimaryVTable = 0xe66434; // the primary vtable, at object +0
    static constexpr uint32_t kSubobjectOffset = 0x2c;  // this = object + 0x2c

    uint8_t FlagBit0();                    // slot 58, 0x57e910: mov al,[S+8]; and al,1; ret. Byte in al only.
    void *RecordFieldC();                  // slot 16, 0xa111b0: record + 0x0c (a pointer, not a read)
    void *EntryAt(uint32_t index);         // slot 20, 0xa10da0: ret 4. entries + 28 * index (mod 2^32)
    bool HasFlagBits0To1();                // slot 57, 0xa15e90: setne al of (S+8) & 3
    uint8_t RecordFlagBit0();              // slot 53, 0xa15eb0: byte at record+8, bit 0. Byte in al only.
    uint8_t RecordFlagBit1();              // slot 54, 0xa16380: bit 1 of the dword at record+8. Byte in al only.
    uint32_t RecordField8Bits12To23();     // slot 38, 0xa16490: (dword record+8 >> 12) & 0xfff
    uint32_t RecordWord2();                // slot 51, 0xa16530: movzx eax, word [record+2]
    uint32_t RecordField8Bits8To11();      // slot 48, 0xa16590: (dword record+8 >> 8) & 0xf
    uint32_t RecordDword14();              // slot 33, 0xa17770: dword [record+0x14]
    uint32_t RecordDword18();              // slot 34, 0xa17780: dword [record+0x18]
    void SetFlagBits(uint8_t a, uint8_t b); // slot 31, 0xa175c0: ret 8. Clears bits 0-1; if a: sets 2 (b) or 1
    float RecordFloat1C();                 // slot 37, 0xa125f0: float at record+0x1c (x87 st0)
    float RecordFloat20();                 // slot 39, 0xa163a0: float at record+0x20
    float RecordFloat24();                 // slot 40, 0xa16390: float at record+0x24
    float RecordFloat28();                 // slot 41, 0xa16470: float at record+0x28 (x87 st0)
    float RecordFloat2C();                 // slot 42, 0xa16480: float at record+0x2c (x87 st0)
    float RecordFloat30();                 // slot 44, 0xa17870: float at record+0x30 (x87 st0)
    float RecordFloat34();                 // slot 45, 0xa17790: float at record+0x34 (x87 st0)
    float RecordFloat38();                 // slot 46, 0xa16580: float at record+0x38 (x87 st0)
    float RecordFloat3C();                 // slot 47, 0xa17100: float at record+0x3c (x87 st0)

    // Never constructed: `this` always points into an existing cAnimResourceConst (object + 0x2c).
};

// The second subobject of nGZSceneDBAnim::cAnimResourceConst, at object +0x58 (vtable 0xe6657c, 25 slots). Its
// slots are thunks (`sub ecx,[ecx-4]; sub ecx,0x28; jmp body`) into the cSGResource secondary-subobject bodies
// (src/gz/cSGResource.cpp, src/sg/SGResourceHelpers.cpp). The thunk computes S = this - [this - 4] - 0x28, which
// is the cSGResource secondary subobject (object + 0x30), and jumps to the body. The replacements here compute S
// the same way and call the verified bodies with the same arguments (`ret N` is the body's own N).
//
// Not done: slots 10, 12, 13 and 14 (SubmitKeyToService, AssignAndSubmit, ResetAndSubmit, OpenAssignAndSubmit),
// whose bodies need the service fakes of tests/sg_resource_helpers.cpp; slot 17 (Dispatch8Adjusted, a further
// this - 0x10 thunk); slots 18-21 (purecall and shared bodies).
class cAnimResourceConstSecondary {
public:
    static constexpr uint32_t kVTable = 0xe6657c; // secondary vtable at subobject +0

    void *VirtualBaseIfObject();          // slot 6, thunk 0xa170e0: body 0xa3c750
    void *GetOwner();                     // slot 7, thunk 0xa10e60: body 0xa3c3c0
    void *GetVirtualBase(void *arg);      // slot 9, thunk 0xa170d5: body 0xa3c720, ret 4
    int32_t CompareIdentity(void *other); // slot 11, thunk 0xa15e76: body 0xa3c3d0, ret 4
    bool SetResource(void *resource);     // slot 15, thunk 0xa175ff: body 0xa3cdd0, ret 4
    void SetFlag(uint32_t value);         // slot 16, thunk 0xa1759c: body 0xa3c870, ret 4 (low byte stored)
    void *CopyKeyTo(void *out);           // slot 22, thunk 0xa170eb: body 0xa3c770, ret 4
    uint8_t GetFlag();                    // slot 23, thunk 0xa15e98: body 0xa3c4a0 (al only)
    uint32_t ForwardKey();                // slot 24, thunk 0xa170af: body 0xa3c6d0
};

} // namespace nGZSceneDBAnim
