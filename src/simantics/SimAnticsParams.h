#pragma once
#include "rz/RZFastFixedAllocators.h"

#include <cstddef>
#include <cstdint>

// XPrimParam and its 68 derived *Param classes (61 built by the operand builder, 7 "Version0" layouts) (1.18.0.187): the operand block of one SimAntics (BHAV)
// node. Every class has the same 0x18-byte layout and four virtual slots; the classes differ only in the
// vtable (and so in the version they report and the conversions they run), because the operand bytes
// are plain bytes whose meaning belongs to the primitive that reads them (not modelled here).
//
//   +0x00 vtable
//   +0x04 operand bytes (0x10): read from / written to a stream as eight 16-bit values (+4, +6 ... +0x12)
//         in the newest format; older formats stored fewer operands, the rest read as zero
//   +0x14 version byte: how many conversion steps the operand bytes have been through (0 = none yet; the
//         builder presets the classes that have conversions to their current version)
//   +0x15 three bytes the constructor never writes (the pool block is 0x18 bytes)
// Blocks come from the pool cRZFastAllocator<XPrimParam> (global allocator 0x10fd4c4, vtable 0xe91974).
//
// The vtable slots are, for every class:
//   0 Write(stream) -> bool          0xc0b0b7, one body for all classes: writes slot 3's version byte then the
//                                    eight 16-bit operands, and reports whether the stream's slot 18 is 0
//   1 Read(stream, version) -> bool  0xc0ac22 (plain), or a class-specific wrapper that runs the plain read
//                                    and then upgrades the operand bytes from an older format
//   2 Convert()                      0x140fe0 (does nothing) or a class-specific conversion from the layout
//                                    of the class's Version0 variant (SetToNext, TurnTowards, ...)
//   3 GetVersion()                   0x6c6b0 / 0x15e510 / 0x155a00 / 0x18c4d0 / 0x15e570 return 0..4
// The stream is any object with the cIGZIStream/cIGZOStream slot numbering: slot 5 transfers one byte, slot 6
// one 16-bit value (reads get a pointer, writes a value), slot 18 is an error flag.
class XPrimParam {
public:
    static constexpr uint32_t kBaseVTable = 0xe9199c;
    static constexpr uint32_t kSize = 0x18;
    static constexpr uint32_t kPoolAllocator = 0x10fd4c4;
    static constexpr int kStreamByteSlot = 5;
    static constexpr int kStreamWordSlot = 6;
    static constexpr int kStreamErrorSlot = 18;
    static constexpr int kConvertSlot = 2;
    static constexpr int kVersionSlot = 3;

    // Memory management.
    static void *AllocateBlock(uint32_t size);   // 0xc0aa89, __cdecl: one block from the pool (size is ignored)
    static void FreeBlock(void *block) noexcept; // 0xc0aa93, __cdecl: null-safe release into the pool
    // 0xc0aacf is the pool's Free (a byte-for-byte twin of cRZFastFixedAllocator2<0x18>::Free): the hook
    // table points it at RZFastFixed0x18::Free.

    XPrimParam *Construct();                         // 0xc0aa6a: base vtable, bytes +4..+0x14 zero
    XPrimParam *CopyConstruct(const XPrimParam &o);  // 0xc0a9f5 (__thiscall, ret 4): base vtable, bytes +4..+0x14 copied

    // The node's operand builder (cTSBehaviorNode slots 5 and 21 call it): the object for a primitive opcode.
    // __stdcall; only the low 16 bits of the argument are used. See kXPrimOpcodeTable for the mapping.
    static XPrimParam *__stdcall BuildForOpcode(uint32_t opcode); // 0x46a860

    // Slot 0.
    bool Write(void *stream);                               // 0xc0b0b7
    // Slot 1.
    bool Read(void *stream, int32_t version);               // 0xc0ac22
    bool ReadRunPrivateTree(void *stream, int32_t version); // 0xc0abf3 (RunPrivateTreeParam)
    bool ReadBudget(void *stream, int32_t version);         // 0xc0ab29 (BudgetParam)
    bool ReadMakeActionString(void *stream, int32_t version); // 0xc0ab83
    bool ReadDialog(void *stream, int32_t version);         // 0xc0ab4c
    bool ReadThenConvert(void *stream, int32_t version);    // 0xc0ab08: Read, then this->slot 2 (Convert)
    // Slot 2.
    void ConvertSetToNext();         // 0xc0afd2
    void ConvertTurnTowards();       // 0xc0b072
    void ConvertNewRelationship();   // 0xc0ae6e
    void ConvertFindGoodLocation();  // 0xc0ad18
    void ConvertGotoRoutingSlot();   // 0xc0ad32
    void ConvertSnap();              // 0xc0b02b
    void ConvertAnimateOverlay();    // 0xc0accb
    void ConvertMaterialChange();    // 0xc0ae3f
    void ConvertLookAt();            // 0xc0ad87
    void ConvertManageInventory();   // 0xc0addb
    void ConvertSnapInto();          // 0xc0afe6

    const void *mVTable;  // +0x00
    uint8_t mData[0x10];  // +0x04: operand bytes
    uint8_t mVersion;     // +0x14
    uint8_t mSpare[3];    // +0x15: never written by the constructor

    uint8_t &Byte(uint32_t off) { return reinterpret_cast<uint8_t *>(this)[off]; }
    uint16_t &Word(uint32_t off) { return *reinterpret_cast<uint16_t *>(reinterpret_cast<uint8_t *>(this) + off); }
    uint32_t &Dword(uint32_t off) { return *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(this) + off); }
};

static_assert(sizeof(XPrimParam) == 0x18, "XPrimParam layout");
static_assert(offsetof(XPrimParam, mData) == 4, "XPrimParam layout");
static_assert(offsetof(XPrimParam, mVersion) == 0x14, "XPrimParam layout");

// One row of the builder's opcode table: opcode -> class (by vtable RVA) and the version byte the builder
// presets. Every other opcode (unlisted 0..0x7d, 0x7e..0xff, and anything negative as a 16-bit value) builds
// the plain XPrimParam; 0x100..0x7fff build RunPrivateTreeParam (0xdc923c), the call-a-private-tree primitive.
struct XPrimOpcodeEntry {
    uint16_t opcode;
    uint32_t vtable;
    uint8_t version;
};
extern const XPrimOpcodeEntry kXPrimOpcodeTable[60];
