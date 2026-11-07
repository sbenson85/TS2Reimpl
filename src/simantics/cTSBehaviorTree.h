#pragma once
#include "rz/cRZString.h"
#include "simantics/SimAnticsParams.h"
#include "simantics/cTSBehaviorNode.h"

#include <cstddef>
#include <cstdint>

// A vector of reference-counted interface pointers (MSVC std::vector layout: begin, end, capacity limit).
// Elements are cIGZUnknown-style objects: slot 1 is AddRef, slot 2 Release. The three helpers below are the
// shared functions the tree calls; they are used by many other classes, which is why they are hooked here
// only where they are byte-identical.
struct RefPtrVector {
    void **mFirst; // +0
    void **mLast;  // +4
    void **mEnd;   // +8

    uint32_t Size() const { return static_cast<uint32_t>(mLast - mFirst); }
    void Clear() noexcept;   // 0x176f00: Release every element (slot 2), then the end follows the begin; storage kept
    void Destroy() noexcept; // 0x14c060: Release every element, free the storage (sized delete; blocks of 0x1000 bytes
                             // and more carry the pre-aligned pointer in the dword before them), all three pointers zero
    // 0x14aac0 (__thiscall, ret 8; src/simantics/RefPtrVectorGrow.cpp): std::vector<counted pointer>::_Emplace_reallocate
    // over operator new, the push_back growth path: puts a copy of *value at `where` into a bigger block (1.5 times the
    // capacity, at least one more element; blocks of 0x1000 bytes and more are aligned as in Destroy), copies the old
    // elements over (each counted again), releases the old ones and frees the old block. Returns the new element's address.
    // When counting throws, what the new block holds is released and the block freed before the error goes on.
    void **EmplaceReallocate(void **where, void *const *value);
};

static_assert(sizeof(RefPtrVector) == 12, "RefPtrVector layout");

// cTSBehaviorTree (1.18.0.187): a SimAntics BHAV resource, a list of cTSBehaviorNode plus the header of the
// tree. Resource type 0x42484156 ("BHAV"). Two vtables: 0xdc9020 (38 slots, cITSBehaviorTree / cIGZSerializable
// at +0) and 0xdc90bc (cTSPersistResourceImpl at +4). The object is 0x60 bytes, allocated by operator new.
//
//   +0x00 vtable (0xdc9020)
//   +0x04 vtable (0xdc90bc; the constructor first sets the base cTSPersistResourceImpl vtable 0xdc4b0c)
//   +0x08 16-byte resource key (cTSPersistResourceImpl::GetKey/SetKey)
//   +0x18 reference count (dword; AddRef/Release are shared with other classes)
//   +0x1c i16 format word: the BHAV signature read from the file, 0x8001..0x8009 (as signed 16 bits: -32767..-32759).
//         A positive value is the old format, which had no signature and started with the node count; the word
//         is then left holding that count.
//   +0x1e u16 node count (the count stored in the file; AddNode/Clear keep it equal to the vector size)
//   +0x20 u8 tree type          (0 for the old format)
//   +0x21 u8 argument count     (4)
//   +0x22 u8 local count        (4)
//   +0x23 u8 flags: bit 0 is cleared on reading; vf34 recomputes bits 0..2 (1 = analysed, 2 and 4 = what
//         the nodes reach: see AnalyseFlags)
//   +0x24 u8 cache flags        (formats from 0x8009 on)
//   +0x25..0x27 padding the constructor never writes
//   +0x28 u32 tree version      (0xffff8009 from the constructor, 0 in the old format)
//   +0x2c vector of cTSBehaviorNode pointers (each held with a reference)
//   +0x38 owned interface pointer, released by the destructor (never written in the code of this class)
//   +0x3c owned interface pointer, released by the destructor; vf28 calls its slot 8 with a node
//   +0x40 cRZString (0x20 bytes): the tree's name (at most 0x40 bytes, a NUL-terminated field in the file)
//
// Streams (Read/Write): objects with cIGZIStream/cIGZOStream numbering: slot 5 transfers a byte, 6 a 16-bit value,
// 8 a 32-bit value (reads get a pointer, writes a value), 17 a block (pointer, length), 18 the error flag.
class cTSBehaviorTree {
public:
    static constexpr uint32_t kVTable = 0xdc9020;
    static constexpr uint32_t kVTable2 = 0xdc90bc;
    static constexpr uint32_t kBaseVTable = 0xdc4b0c; // cTSPersistResourceImpl, set first by the constructor
    static constexpr uint32_t kSize = 0x60;
    static constexpr uint32_t kClassId = 0x42484156;

    static constexpr uint32_t kIidTree = 0x5b267c7c;
    static constexpr uint32_t kIidSerializable = 0xe4fda3d4;
    static constexpr uint32_t kIidUnknown = 1;
    static constexpr uint32_t kIidPersistResource = 0x456b8f1d;

    // Slots called on this (the tree's own vtable).
    static constexpr int kGetNodeCountSlot = 20;
    static constexpr int kGetNodeSlot = 22;
    static constexpr int kAnalyseSlot = 34;
    static constexpr int kFlagsSlot = 16;

    static cTSBehaviorTree *Create();                          // 0x46a020: new + construct, __cdecl
    cTSBehaviorTree *DeletingDestructor(uint32_t flags);       // 0x469740 (sub-vtable slot 12 via 0x469551)

    bool QueryInterface(uint32_t iid, void **out);             // slot 0, 0x46d0e0
    bool Write(void *stream);                                  // slot 3, 0x46ef80
    bool Read(void *stream);                                   // slot 4, 0x46d530
    uint32_t GetClassId() const;                               // slot 5, 0x46a120: "BHAV"
    uint32_t GetArgCount() const;                              // slot 12, 0x46bd30: +0x21 limited by the format (see body)
    uint32_t GetLocalCount() const;                            // slot 14, 0x46a7d0
    void SetLocalCount(uint32_t v);                            // slot 15, 0x46e150
    void SetFlags(uint32_t v);                                 // slot 17, 0x46e0d0
    uint16_t GetNodeCount() const;                             // slot 20, 0x46a190
    void *GetNode(uint32_t index) const;                       // slot 22, 0x46a170
    bool AddNode(uint32_t opcode, uint32_t trueTarget, uint32_t falseTarget, const XPrimParam *param); // slot 24, 0x469910
    void ClearNodes();                                         // slot 26, 0x469c90
    bool IsJumpTarget(int32_t index);                          // slot 27, 0x46c3f0
    uint8_t CheckNode(int16_t index);                            // slot 28, 0x46c470
    void *ExportTo(void *sink);                                // slot 31, 0x469cb0 (ret 4, no result)
    void *SetName(void *name);                                 // slot 33, 0x46e170: cRZString slot 16 (operator=) on the name
    void AnalyseFlags(void *provider);                         // slot 34, 0x469a30

    const void *mVTable;     // +0x00
    const void *mVTable2;    // +0x04
    uint8_t mKey[16];        // +0x08
    uint32_t mRefCount;      // +0x18
    int16_t mVersion;        // +0x1c
    uint16_t mNodeCount;     // +0x1e
    uint8_t mType;           // +0x20
    uint8_t mArgCount;       // +0x21
    uint8_t mLocalCount;     // +0x22
    uint8_t mFlags;          // +0x23
    uint8_t mCacheFlags;     // +0x24
    uint8_t mPad[3];         // +0x25
    uint32_t mTreeVersion;   // +0x28
    RefPtrVector mNodes;     // +0x2c
    void *mOwned38;          // +0x38
    void *mOwned3C;          // +0x3c
    cRZString mName;         // +0x40
};

static_assert(sizeof(cTSBehaviorTree) == 0x60, "cTSBehaviorTree layout");
static_assert(offsetof(cTSBehaviorTree, mRefCount) == 0x18, "cTSBehaviorTree layout");
static_assert(offsetof(cTSBehaviorTree, mVersion) == 0x1c, "cTSBehaviorTree layout");
static_assert(offsetof(cTSBehaviorTree, mTreeVersion) == 0x28, "cTSBehaviorTree layout");
static_assert(offsetof(cTSBehaviorTree, mNodes) == 0x2c, "cTSBehaviorTree layout");
static_assert(offsetof(cTSBehaviorTree, mOwned3C) == 0x3c, "cTSBehaviorTree layout");
static_assert(offsetof(cTSBehaviorTree, mName) == 0x40, "cTSBehaviorTree layout");
