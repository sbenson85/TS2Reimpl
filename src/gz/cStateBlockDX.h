#pragma once
// nGZGraphic4::cStateBlockDX (vtable 0xd9fb74, 12 slots): a recorded set of render state changes that the state cache
// (cDeviceStateDX) applies in one call (rendering.md, item 2b). The cache makes it from its delta (what the wanted state
// holds that the defaults do not, StateDelta) in CreateStateBlock and shares equal ones: the cache keeps a sorted list
// of the blocks it made (mList, ordered by record size, then record bytes), and a new block equal to a listed one is
// dropped for the listed one.
//
// Standalone shape (docs/standalone-shape.md): a real class on the resource interfaces, made by CreateStateBlock from
// the fixed pool at 0xfee35c (0x18-byte blocks). Layout:
//   +0 vtable, +4 reference count, +8 the owning cDeviceStateDX (null once released), +0xc / +0x10 / +0x14 the record
//   (a byte vector, see below).
//
// The record (StateDelta::Serialize writes it, cDeviceStateDX applies and releases it): an 8-byte header
//   +0 u16 length, +2 u8 the groups (1 and 2: which sections follow), +3 u8 the change count (low byte),
//   +4 u16 offset of section A (0xffff if none), +6 u16 offset of section B,
// then section A (group 1): the ChangeRecord of the block (0x54 bytes: the change bits the block sets, which the cache
// ORs in when the block is replaced), the textures (count, then index and texture), the vertex shader (flag, software
// flag, shader), the pixel shader (flag, shader), the render states (count, then index and value), the stage states
// (count of stages, each stage and its count of entries then index and value), the sampler states (the same), the
// texture transforms (count, then index and 16 words), and the material (flag, 0x44 bytes); section B (group 2): a word
// of change bits (streams, declaration, indices) and the declaration flag again, the declaration if flagged, the
// streams (count, then index, buffer, stride, offset), and the indices (flag, buffer). The Direct3D objects named are
// counted by the record.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gz/cSurfaceDX.h" // cIDeviceResource

#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"

// The classes' fixed-size pools (operator new / delete).
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x18>, fee35c);

namespace nGZGraphic4 {

class cDeviceStateDX;

// nGZGraphic4::cIStateBlock: the resource interface and slot 10.
class cIStateBlock : public cIDeviceResource {
public:
    virtual uint8_t GetGroups() = 0; // slot 10: the record's groups byte (1: section A, 2: section B)
    virtual ~cIStateBlock() = default; // slot 11: the deleting destructor (0x17caa0)
};

class cStateBlockDX : public cIStateBlock {
public:
    static constexpr uint32_t kVTable = 0xd9fb74;
    static constexpr uint32_t kPoolRva = 0xfee35c; // the 0x18-byte pool (its current page at +4)
    static rz::cRZFastFixedAllocator2<0x18> &Pool() { return ts2_rva_fee35c; }
    static constexpr uint32_t kIidStateBlock = 0xc9714531;

    cStateBlockDX();
    ~cStateBlockDX() override;
    cStateBlockDX(const cStateBlockDX &) = delete;
    cStateBlockDX &operator=(const cStateBlockDX &) = delete;
    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);

    // cIGZUnknown (QueryInterface 0x17dc80; the others folded 0x148fb0 / 0x153f70)
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    // cIDeviceResource
    void ReleaseResource() override;           // slot 3 (0x181660)
    cIDeviceDX *GetDevice() override;          // slot 4 (0x17da70: the owner's device)
    cIDeviceDX *GetDeviceConst() override;     // slot 5
    bool IsCreated() override;                 // slot 6 (0x17da80: there is an owner)
    uint32_t GetMemorySize() override;         // slot 7 (0x1816c0: the record and the object)
    bool SurvivesReset() override;             // slot 8 (shared 0x147480: false)
    bool SurvivesModeChange() override;        // slot 9
    uint8_t GetGroups() override;              // slot 10 (0x17cde0)

    // The record's order (size, then bytes) in the cache's list.
    static bool RecordLess(const cStateBlockDX *a, const cStateBlockDX *b);

    uint32_t mRefCount;                  // +4
    cDeviceStateDX *mOwner;              // +8
    std::vector<uint8_t> mRecord;        // +0xc

private:
    // The release shared by slot 3 and the destructor: off the owner's list and the device's resources, the record's
    // objects released, the record emptied (its storage kept).
    void Detach();
    void ReleaseRecord();                // 0x17dcd0
};
static_assert(sizeof(std::vector<uint8_t>) == 12, "the record vector is three pointers");
static_assert(sizeof(cStateBlockDX) == 0x18, "layout");

} // namespace nGZGraphic4
