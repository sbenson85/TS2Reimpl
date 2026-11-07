#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

// cTSFamilyRelationship: one sim's family ties (the per-sim record of the FAMT resource that
// cTSFamilyRelationshipArray, class id 0x8c870743, keeps per sim id). A multimap from tie type to the other sim's id.
// RTTI bases: cITSFamilyRelationship (+0), cIGZPersistResource (+4), cIGZSerializable (+8), each a cIGZUnknown.
// 0x28 bytes; constructor 0x4f1fd0, factory 0x4f2680 (registered by the cTSSimCOMDirector constructor), no separate
// destructor: Release (slot 2) destroys and frees inline. Vtables 0xdce04c (+0, 15 slots), 0xdce08c (+4, 12 slots,
// all folded/thunks), 0xdce0c0 (+8, 6 slots). All functions are /O2 ("speed": any .cpp but *.size.cpp).
// Layout and signatures are recorded in the shared Ghidra program (lane CB): structs cTSFamilyRelationship,
// cTSFamilyRelationshipSerialFace, FamilyTieMap, FamilyTieNode, FamilyTie, SimIdVector, cGZPersistResourceKey.
// RVAs are 1.18.0.187.

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

// Tie types (the multimap's key; compared as signed int). 0/1 and 2 hold one sim each, 3 and 4 a list.
// cTSFamilyRelationshipArray vf9 (0x4f2280) shows the roles: a child gets its parent as 0, or 1 when 0 is taken;
// the parent gets the child as 4; 2 is mutual and exclusive; 3 is mutual. Mother/father: SimPE's FAMT names (tentative).
enum FamilyTieType : int32_t {
    kFamilyTieMother = 0,
    kFamilyTieFather = 1,
    kFamilyTieSpouse = 2,
    kFamilyTieSibling = 3,
    kFamilyTieChild = 4,
};

// MSVC 14.40 std::multimap, as in the game: node 0x18 bytes (left/parent/right, colour +0xc, nil +0xd, type +0x10,
// sim id +0x14). Sibling and child ties repeat their key; new nodes go after equal keys (upper-bound insert).
using FamilyTieMap = std::multimap<int32_t, uint16_t>;
static_assert(sizeof(FamilyTieMap) == 8);

// What GetChildren / GetSiblings append to: a vector in the allocator service's memory (its push_back reallocation is
// 0x256300, shared with cTSFamilyManager::GetFamilyIds' vector of the same type).
using SimIdVector = std::vector<uint16_t, GZServiceAllocator<uint16_t>>;
static_assert(sizeof(SimIdVector) == 12);

class cTSFamilyRelationship;

// The cIGZSerializable face at +8: its slots run with `this` at the face (Ghidra: cTSFamilyRelationshipSerialFace).
struct cTSFamilyRelationshipSerialFace {
    const void *mVTable;
    bool Write(ifaces::cIGZOStream *stream); // slot 3, 0x4f3dc0 (ret 4, EH frame)
    bool Read(ifaces::cIGZIStream *stream);  // slot 4, 0x4f2f80 (ret 4, EH frame)
    uint32_t GetGZCLSID();                   // slot 5, 0x4f2780: kClassId
    cTSFamilyRelationship *Owner();          // this - 8
};

class cTSFamilyRelationship {
public:
    static constexpr uint32_t kVTable = 0xdce04c;
    static constexpr uint32_t kPersistVTable = 0xdce08c;
    static constexpr uint32_t kSerialVTable = 0xdce0c0;
    static constexpr uint32_t kBasePersistVTable = 0xda66e4; // cIGZPersistResource's, stored first by the constructor
    static constexpr uint32_t kBaseSerialVTable = 0xda3f94;  // cIGZSerializable's
    static constexpr uint32_t kClassId = 0x8c870733;          // GetGZCLSID and the key's type
    static constexpr uint32_t kInterfaceId = 0x6c870723;      // cITSFamilyRelationship
    static constexpr uint32_t kPersistResourceId = 0x456b8f1d;
    static constexpr uint32_t kSerializableId = 0xe4fda3d4;
    static constexpr uint32_t kStreamReaderId = 0x051b11bd;   // ReadFrom asks its source for this
    static constexpr uint32_t kStreamWriterId = 0xc51b11cb;   // WriteTo
    static constexpr uint32_t kWriteVersion = 1;

    // The 16-byte resource key of the +4 face (Ghidra: cGZPersistResourceKey). Face slots 3/4 copy it, 5-7 read and
    // 8-10 write type/group/instance (all folded with other classes, not ours to hook).
    struct Key {
        uint32_t type;         // +0x0
        uint32_t group;        // +0x4
        uint32_t instance;     // +0x8
        uint32_t instanceHigh; // +0xc
    };

    cTSFamilyRelationship *Construct();       // 0x4f1fd0 (thiscall, no EH frame): returns this
    static cTSFamilyRelationship *Create();   // 0x4f2680 (cdecl, EH frame): new 0x28 + Construct, null on failure

    // cITSFamilyRelationship (vtable 0xdce04c)
    bool QueryInterface(uint32_t riid, void **out);   // slot 0, 0x4f2e90 (ret 8)
    //                                                    slot 1 AddRef: library-folded 0x15e520 (++mRefCount)
    uint32_t Release();                                // slot 2, 0x4f3720 (ret 0)
    bool GetTie(int32_t type, uint16_t *outSim);       // slot 3, 0x4f2d50 (ret 8): first tie of the type
    bool GetTieType(uint16_t sim, int32_t *outType);   // slot 4, 0x4f2d00 (ret 8): type of the first tie to sim
    bool GetChildren(SimIdVector *out);                // slot 5, 0x4f2c90 (ret 4)
    bool GetSiblings(SimIdVector *out);                // slot 6, 0x4f2e10 (ret 4)
    bool SetTie(int32_t type, uint16_t sim);           // slot 7, 0x4f3cd0 (ret 8)
    bool RemoveSim(uint16_t sim);                      // slot 8, 0x4f37e0 (ret 4): first tie to sim
    bool RemoveTie(int32_t type);                      // slot 9, 0x4f3840 (ret 4): first tie of the type
    //                                                    slot 10 GetRefCount: folded 0x145770 (mRefCount)
    bool ReadFrom(void *source);                       // slot 11, 0x4f3a90 (ret 4, EH frame)
    bool WriteTo(void *target);                        // slot 12, 0x4f3b90 (ret 4, EH frame)
    //                                                    slot 13 GetPersistResource: folded 0x1c04e0 (this ? +4 : 0)
    //                                                    slot 14 GetSerializable: folded 0x3a4f40 (this ? +8 : 0)

    // Trivial accessors (the folded slots' behaviour, for our own callers).
    uint32_t GetRefCount() const { return mRefCount; }
    cTSFamilyRelationshipSerialFace *Serializable() { return &mSerial; }

    const void *mVTable;                     // +0x00 cITSFamilyRelationship
    const void *mPersistVTable;              // +0x04 cIGZPersistResource face
    cTSFamilyRelationshipSerialFace mSerial; // +0x08 cIGZSerializable face
    uint32_t mRefCount;                      // +0x0c
    Key mKey;                                // +0x10 {kClassId, 0, 0, 0} after construction
    FamilyTieMap mTies;                      // +0x20 head, +0x24 size
};
static_assert(sizeof(cTSFamilyRelationship::Key) == 0x10, "resource key");
static_assert(sizeof(cTSFamilyRelationship) == 0x28, "cTSFamilyRelationship layout");
static_assert(offsetof(cTSFamilyRelationship, mPersistVTable) == 0x4, "persist face");
static_assert(offsetof(cTSFamilyRelationship, mSerial) == 0x8, "serializable face");
static_assert(offsetof(cTSFamilyRelationship, mRefCount) == 0xc, "reference count");
static_assert(offsetof(cTSFamilyRelationship, mKey) == 0x10, "resource key");
static_assert(offsetof(cTSFamilyRelationship, mTies) == 0x20, "tie map");
