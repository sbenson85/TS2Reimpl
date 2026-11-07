#pragma once
#include "simantics/SimAnticsParams.h"

#include <cstddef>
#include <cstdint>

// cTSBehaviorNode (1.18.0.187): one node of a SimAntics (BHAV) tree. Implements cITSBehaviorNode (vtable
// 0xdc8fb4, 26 slots). Blocks are 16 bytes from the pool cRZFastAllocator<cTSBehaviorNode> (global
// allocator 0x1012908); there is no stand-alone constructor (the tree's Read builds nodes inline).
//
//   +0x00 vtable
//   +0x04 u16 reference count (starts at 1)
//   +0x06 u16 opcode word: the low 15 bits are the primitive opcode, bit 15 is a flag (slots 18/19)
//   +0x08 u16 true target (jump index; the reader maps the old byte values 0xfd..0xff to 0xfffc..0xfffe)
//   +0x0a u16 false target
//   +0x0c operand object (an XPrimParam subclass built by XPrimParam::BuildForOpcode), owned
//
// Streams are objects with cIGZIStream-like numbering: slot 5 reads a byte through a pointer, 6 and 7
// read 16-bit values through a pointer (writes pass the value), 18 is an error flag (0 = fine).
class cTSBehaviorNode {
public:
    static constexpr uint32_t kVTable = 0xdc8fb4;
    static constexpr uint32_t kPoolAllocator = 0x1012908;
    static constexpr uint32_t kIidUnknown = 1;
    static constexpr uint32_t kIidBehaviorNode = 0x1b2497eb; // cITSBehaviorNode

    bool QueryInterface(uint32_t iid, void **out); // slot 0, 0x46d0b0: only cIGZUnknown and cITSBehaviorNode
    uint32_t Release();                            // slot 2, 0x46dbc0: at count <= 1 deletes (slot 25, flags 1), returns 0
    uint32_t GetOpcode();                          // slot 3, 0x46c100: opcode word without the flag bit
    uint16_t *GetOpcodeWord();                     // slot 4, 0x46c110: &opcode word (shared, 4 bytes; not hooked)
    void SetOpcode(uint32_t opcode);               // slot 5, 0x46e300: replaces the operand object, keeps the flag
    uint32_t GetTrueTarget();                      // slot 6, 0x432e30 (shared; not hooked)
    uint32_t GetFalseTarget();                     // slot 8, 0x4328e0 (shared; not hooked)
    uint16_t *GetFalseTargetPtr();                 // slot 9, 0x46bff0 (shared; not hooked)
    void SetTrueTarget(uint32_t v);                // slot 10, 0x4351b0 (shared; not hooked)
    void SetFalseTarget(uint32_t v);               // slot 11, 0x4350e0 (shared; not hooked)
    uint32_t GetOperand(int32_t index);            // slot 13, 0x46bc80: operand word `index` (0..7, bounded by slot 12), else 0
    void SetOperand(int32_t index, uint32_t v);    // slot 14, 0x46e180
    XPrimParam *CopyParamTo(XPrimParam *dest);     // slot 15, 0x46c330: dest copy-constructed from the operand object; returns dest
    void CopyParamFrom(const XPrimParam *src);     // slot 17, 0x46e3c0: operand bytes +4..+0x14 overwritten from src
    bool GetFlag();                                // slot 18, 0x46a270: bit 15 of the opcode word
    void SetFlag(uint32_t value);                  // slot 19, 0x46de60: only the low byte of value counts
    bool Write(void *stream);                      // slot 20, 0x46ef30
    bool Read(void *stream, int32_t version);      // slot 21, 0x46d2b0
    uint32_t GetClassId();                         // slot 24, 0x46a110: constant 0xeb54985f
    cTSBehaviorNode *DeletingDestructor(uint32_t flags); // slot 25, 0x469680: bit 0 frees to the pool unless bit 2 is set

    const void *mVTable;  // +0x00
    uint16_t mRefCount;   // +0x04
    uint16_t mOpcode;     // +0x06
    uint16_t mTrue;       // +0x08
    uint16_t mFalse;      // +0x0a
    XPrimParam *mParam;   // +0x0c
};

static_assert(sizeof(cTSBehaviorNode) == 0x10, "cTSBehaviorNode layout");
static_assert(offsetof(cTSBehaviorNode, mOpcode) == 6, "cTSBehaviorNode layout");
static_assert(offsetof(cTSBehaviorNode, mParam) == 0xc, "cTSBehaviorNode layout");
