#pragma once
#include "gz/cGZDBSegmentPackedFile.h"     // GZServiceAllocator
#include "gz/cGZPersistResourceManager.h"  // cGZPersistResourceKey
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// cTSPersistResKeyList: a saved list of persist resource keys (record class id 0xac506764; interface cITSPersistResKeyList,
// id 0x6cd04176). RTTI bases: cITSPersistResKeyList (+0, vtable 0xda6dd0, 9 slots), cIGZPersistResource (+4, 0xda6df8, 12 slots,
// mostly folded key accessors), cIGZSerializable (+8, 0xda6e2c, 6 slots), each a cIGZUnknown. 0x2c bytes. All functions are /O2
// ("speed": any .cpp but *.size.cpp). Layout and signatures are recorded in the shared Ghidra program (lane CE). RVAs 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md, lane DW): a real C++ class. The compiler emits its three vtables (one per
// interface, the original's slot order; slot 8 of the first is the scalar deleting destructor), the adjustor thunks of the +4 /
// +8 faces and the RTTI. The game creates the list only through the factory 0x1f4dc0 (operator new + the constructor inline;
// registered by address in the cTSPersistCOMDirector getter 0x1d3b70), which is hooked to Create: every list carries our vtables.
// The game's vtables are referenced only by that factory and the deleting destructor 0x1f4d20. The RVA hooks on the old slot
// functions are adapters (cTSPersistResKeyListAdapters). Nothing in the image reads RTTI (docs/standalone-shape.md).

// One persist resource key as the list stores it (16 bytes, as cGZPersistResourceKey): type, group, instance and the high word
// of the instance.
struct PersistResKey {
    uint32_t type;         // +0x0
    uint32_t group;        // +0x4
    uint32_t instance;     // +0x8
    uint32_t instanceHigh; // +0xc (not stored in streams older than version 2)
};
static_assert(sizeof(PersistResKey) == 0x10, "persist resource key");

// The keys, in the allocator service's memory (slot 3 allocates, slot 4 frees). The resize / reallocation instantiations
// 0x1f4b70 and 0x1f4ce0 are this vector's (the latter two also reached from 0x9b2fb0, another user of the same vector type).
using PersistResKeyVector = std::vector<PersistResKey, GZServiceAllocator<PersistResKey>>;
static_assert(sizeof(PersistResKeyVector) == 12);

// The list's own interface (id 0x6cd04176; the original's name, global as its RTTI). Slot 8 (the deleting destructor) is the
// class's own virtual destructor, declared there.
class cITSPersistResKeyList : public cIGZUnknown {
public:
    virtual void Clear() = 0;                              // slot 3 (the folded body leaves the vector's begin in eax)
    virtual bool Commit(void *segment) = 0;                // slot 4 (ret 4)
    virtual cIGZPersistResource *GetPersistResource() = 0; // slot 5
    virtual PersistResKeyVector *GetKeys() = 0;            // slot 6
    virtual PersistResKeyVector *GetKeys2() = 0;           // slot 7 (the same folded body as slot 6; a const overload?)
};

class cTSPersistResKeyList : public cITSPersistResKeyList, public cIGZPersistResource, public cIGZSerializable {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kOriginalVTable = 0xda6dd0;
    static constexpr uint32_t kOriginalPersistVTable = 0xda6df8;
    static constexpr uint32_t kOriginalSerialVTable = 0xda6e2c;
    static constexpr uint32_t kClassId = 0xac506764;          // GetGZCLSID
    static constexpr uint32_t kInterfaceId = 0x6cd04176;      // cITSPersistResKeyList
    static constexpr uint32_t kPersistResourceId = 0x456b8f1d;
    static constexpr uint32_t kSerializableId = 0xe4fda3d4;
    static constexpr uint32_t kRecordId = 0xe56b8f03;         // a stream that is a persist record gives its record face for this
    static constexpr uint32_t kKeyMapperId = 0x3b2271bb;      // the record face's slot 15 gives the segment's key mapper for this
    static constexpr uint32_t kSegmentInterfaceId = 0x656b8f0c; // Commit's FindSegment asks for the segment interface
    static constexpr uint32_t kPersistServiceId = 0x056b906e;   // the persist system, asked from the service object
    static constexpr uint32_t kPersistServiceInterfaceId = 0x9af5e924;
    static constexpr uint32_t kMarker = 0xdeadbeef;           // first word of a version 1+ stream (older ones start with the count)
    static constexpr uint32_t kWriteVersion = 2;
    static constexpr size_t kSize = 0x2c;

    cTSPersistResKeyList();          // inline in the factory 0x1f4dc0 (the base interfaces' vtables stored first, dead stores)
    virtual ~cTSPersistResKeyList(); // slot 8: the scalar deleting destructor 0x1f4d20 (ret 4, EH frame) is the compiler's
    cTSPersistResKeyList(const cTSPersistResKeyList &) = delete;
    cTSPersistResKeyList &operator=(const cTSPersistResKeyList &) = delete;
    static cTSPersistResKeyList *Create(); // 0x1f4dc0 (cdecl): new, null on failure

    // cIGZUnknown (every face; the +4 / +8 faces' slots 0-2 are the compiler's adjustor thunks, as in the original:
    // 0x1f4eb7 / 0x1f4daf / 0x1f5117 and 0x1f4ebf / 0x1f4db7 / 0x1f511f)
    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0x1f4e60 (ret 8)
    uint32_t AddRef() override;                              // slot 1, folded 0x15e520 (a library match: ++count)
    uint32_t Release() override;                             // slot 2, folded 0x1f5100: at 1 or below, delete this (slot 8)

    // cITSPersistResKeyList (+0)
    void Clear() override;                              // slot 3, folded 0x1f5280: end = begin
    bool Commit(void *segment) override;                // slot 4, 0x1f5130 (ret 4, EH frame): save the resource, then flush
    cIGZPersistResource *GetPersistResource() override; // slot 5, folded 0x1c04e0: this ? this + 4 : 0
    PersistResKeyVector *GetKeys() override;            // slot 6, folded 0x147780: &mKeys
    PersistResKeyVector *GetKeys2() override;           // slot 7, folded 0x147780

    // cIGZPersistResource (+4; these run with this = the face; all folded game functions)
    cGZPersistResourceKey GetKey() override;                // slot 3, folded 0x1f4e50
    bool SetKey(const cGZPersistResourceKey &key) override; // slot 4, folded 0x1f5260
    uint32_t GetType() override;                            // slot 5, folded 0x148ba0
    uint32_t GetGroup() override;                           // slot 6, folded 0x147d00
    uint32_t GetInstance() override;                        // slot 7, folded 0x15e550
    bool SetType(uint32_t type) override;                   // slot 8, folded 0x1f5270
    bool SetGroup(uint32_t group) override;                 // slot 9, folded 0x1f5240
    bool SetInstance(uint32_t instance) override;           // slot 10, folded 0x1f5250
    uint32_t GetRefCount() override;                        // slot 11, folded 0x46390 (a body of its own, no thunk)

    // cIGZSerializable (+8)
    bool Write(ifaces::cIGZOStream *stream) override; // slot 3, 0x1f5290 (ret 4, EH frame)
    bool Read(ifaces::cIGZIStream *stream) override;  // slot 4, 0x1f4ed0 (ret 4, EH frame)
    uint32_t GetGZCLSID() override;                   // slot 5, 0x1f4e40: kClassId

    // +0x00..+0x08: the three vtable pointers (the interfaces above, in that order)
    uint32_t mRefCount;         // +0x0c
    PersistResKeyVector mKeys;  // +0x10 begin, +0x14 end, +0x18 capacity end
    cGZPersistResourceKey mKey; // +0x1c the list's own resource key (as a persist resource): zero after construction
};
static_assert(sizeof(cTSPersistResKeyList) == cTSPersistResKeyList::kSize, "cTSPersistResKeyList layout");
static_assert(offsetof(cTSPersistResKeyList, mRefCount) == 0xc, "reference count");
static_assert(offsetof(cTSPersistResKeyList, mKeys) == 0x10, "key vector");
static_assert(offsetof(cTSPersistResKeyList, mKey) == 0x1c, "own key");

// Where each interface sits in the object (RTTI; base offsets are not constant expressions: the tests check them).
namespace reskeylist {
constexpr uint32_t kPersistFace = 0x4;
constexpr uint32_t kSerialFace = 0x8;
} // namespace reskeylist

// The RVA hooks' entry points (src/hook/tables/cTSPersistResKeyList.cpp). Every list the game creates carries our vtables, so
// these run only where the game calls an old entry point directly (GetGZCLSID, from the COM director) or holds a list built
// with its own vtables (none while the factory hook is on). Original calling convention (this in ecx = the face of the slot),
// our functions called without virtual dispatch, bodies inlined.
struct cTSPersistResKeyListAdapters {
    using Self = cTSPersistResKeyList;
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static uint32_t __fastcall GetGZCLSID(cIGZSerializable *face, void *);
};
