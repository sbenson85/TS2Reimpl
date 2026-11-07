#pragma once
#include "gz/cGZPersistResourceManager.h"        // cGZPersistResourceKey
#include "ifaces/cITSFamilyRelationship.h"       // the map's value type
#include "ts/cTSFamilyDataImportExportContext.h" // ServiceWordVector (vector<uint32_t, GZServiceAllocator>)
#include "ts/cTSFamilyRelationship.h"            // SimIdVector, FamilyTieType, cTSFamilyRelationship

#include <cstddef>
#include <cstdint>
#include <map>

#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"
#include "ts/cITSDataContainer.h"

// cTSFamilyRelationshipArray: the family ties of every sim of a neighbourhood (the FAMT resource, record class id 0x8c870743):
// sim id -> counted cTSFamilyRelationship (ts/cTSFamilyRelationship.h, class id 0x8c870733). RTTI bases:
// cITSFamilyRelationshipArray (+0, 18 slots, vtable 0xdce0dc), cITSDataContainer (+4, 0xdce128), cIGZPersistResource (+8,
// 0xdce144, all folded), cIGZSerializable (+0xc, 0xdce178). 0x30 bytes. RVAs 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md): a real C++ class. The compiler emits its four vtables (one per interface,
// the original's slot order), the adjustor thunks of the secondary faces and the RTTI; the constructor installs them, so
// every object the game creates (Create 0x4f26e0, and the persist resource factory 0x4f25d0 through the constructor
// 0x4f2070, both hooked) carries our vtables and the game reaches our functions through them. The game's own vtables
// become unreachable; the RVA hooks on the old slot functions are adapters (cTSFamilyRelationshipArrayAdapters below).
// No game code reads RTTI (the image has no __RTDynamicCast / __RTtypeid, and no code refers to these type descriptors).

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
class cITSDataImportExportContext;
class cITSFamilyRelationship;
} // namespace ifaces

// ---- the interfaces this class implements (the original's names and slot order; global, as the original's RTTI) ----
// cIGZPersistResource and cIGZSerializable are shared (src/gz/), cITSDataContainer too (src/ts/cITSDataContainer.h);
// cITSFamilyRelationshipArray has no other implementer yet. Callers keep using the generated ifaces:: views (same ABI).

class cITSFamilyRelationshipArray : public cIGZUnknown {
public:
    virtual bool SetRelationship(uint16_t sim, ifaces::cITSFamilyRelationship *relation) = 0; // slot 3
    virtual bool RemoveRelationship(uint16_t sim) = 0;                                       // slot 4
    virtual bool IsDirty() = 0;                                                              // slot 5
    virtual void SetDirty() = 0;                                                             // slot 6
    virtual ifaces::cITSFamilyRelationship *GetRelationship(uint16_t sim) = 0;               // slot 7
    virtual void Unrelate(uint16_t sim, uint16_t other) = 0;                                 // slot 8
    virtual bool Relate(uint16_t sim, uint16_t other, int32_t role) = 0;                     // slot 9
    virtual uint32_t Unused10() = 0;                                                         // slot 10 (unnamed; 0 here)
    virtual bool Unused11(uint32_t, uint32_t, uint32_t) = 0;                                 // slot 11 (unnamed; false here)
    virtual uint32_t GetRefCount() = 0;                                                      // slot 12
    virtual bool ReadFrom(cIGZUnknown *source) = 0;                                          // slot 13
    virtual bool WriteTo(cIGZUnknown *target) = 0;                                           // slot 14
    virtual cIGZPersistResource *GetPersistResource() = 0;                                   // slot 15
    virtual bool Save() = 0;                                                                 // slot 16
    virtual bool Clear() = 0;                                                                // slot 17
};

// A counted interface pointer (the game's cRZAutoRefCount<T>, inlined everywhere): AddRef when a pointer is taken in,
// Release when it is dropped. T is a generated interface or ::cIGZUnknown; only slots 1 and 2 are used.
// (To be shared by the other classes; src/gz/cRZAutoRefCount.h once the coordinator hoists it.)
template <class T> class cRZAutoRefCount {
public:
    cRZAutoRefCount() = default;
    explicit cRZAutoRefCount(T *object) : mObject(object)
    {
        if (mObject) mObject->AddRef();
    }
    cRZAutoRefCount(const cRZAutoRefCount &other) : cRZAutoRefCount(other.mObject) {}
    cRZAutoRefCount &operator=(const cRZAutoRefCount &other)
    {
        Assign(other.mObject);
        return *this;
    }
    ~cRZAutoRefCount() noexcept(false) // noexcept(false): as NeighborDataRef (the EH frames of the callers)
    {
        if (mObject) mObject->Release();
    }

    // Counts the new object, stores it, then releases the old one; nothing for the same pointer (NeighborDataRef::Assign's
    // order, which the SetRelationship difftest checks).
    void Assign(T *object)
    {
        T *old = mObject;
        if (object == old) return;
        if (object) object->AddRef();
        mObject = object;
        if (old) old->Release();
    }
    // For an out-parameter (QueryInterface, GetClassObject, GetResource): releases what is held, the slot nulled first, and
    // hands out the slot. The original's `if (p) { t = p; p = 0; t->Release(); } f(..., &p)`.
    void **AsPPVoid()
    {
        if (mObject) {
            T *old = mObject;
            mObject = nullptr;
            old->Release();
        }
        return reinterpret_cast<void **>(&mObject);
    }
    T *Get() const { return mObject; }
    T *operator->() const { return mObject; }
    explicit operator bool() const { return mObject != nullptr; }

private:
    T *mObject = nullptr;
};
static_assert(sizeof(cRZAutoRefCount<ifaces::cITSFamilyRelationship>) == 4);

using FamilyRelationRef = cRZAutoRefCount<ifaces::cITSFamilyRelationship>;

// sim id -> relationship: MSVC 14.40 std::map as in the game (node 0x18: links, colour/nil bytes, sim at +0x10, the counted
// pointer at +0x14; erasing a node releases it: 0x1fc790 is this map's _Erase_tree).
using FamilyRelationMap = std::map<uint16_t, FamilyRelationRef>;
static_assert(sizeof(FamilyRelationMap) == 8);

// The destructor of a SimIdVector (0x4f2110, thiscall): a destructor has no address, so the hook entry points at this shim.
struct SimIdVectorTidy {
    void Destroy();
};

class cTSFamilyRelationshipArray : public cITSFamilyRelationshipArray,
                                   public cITSDataContainer,
                                   public cIGZPersistResource,
                                   public cIGZSerializable {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kOriginalVTable = 0xdce0dc;
    static constexpr uint32_t kOriginalContainerVTable = 0xdce128;
    static constexpr uint32_t kOriginalPersistVTable = 0xdce144;
    static constexpr uint32_t kOriginalSerialVTable = 0xdce178;
    static constexpr uint32_t kClassId = 0x8c870743;           // GetGZCLSID, the resource key's type
    static constexpr uint32_t kInterfaceId = 0x8c87073f;       // cITSFamilyRelationshipArray
    static constexpr uint32_t kContainerId = 0x8db5bf48;       // cITSDataContainer
    static constexpr uint32_t kPersistResourceId = 0x456b8f1d;
    static constexpr uint32_t kSerializableId = 0xe4fda3d4;
    static constexpr uint32_t kStreamReaderId = 0x051b11bd;    // ReadFrom asks its source for this
    static constexpr uint32_t kStreamWriterId = 0xc51b11cb;    // WriteTo
    static constexpr uint32_t kNeighborContextId = 0x0db5c0c2; // cITSNeighborDataImportExportContext, asked of a context
    static constexpr uint32_t kDBSegmentId = 0x656b8f0c;       // cIGZPersistDBSegment, asked of a context's held object
    static constexpr uint32_t kWriteVersion = 1;
    static constexpr uint32_t kExportInstance = 1;             // Export's key instance; Import tries 1, then 0

    cTSFamilyRelationshipArray();  // 0x4f2070 (no EH frame); hook entry: cTSFamilyRelationshipArrayAdapters::Construct
    ~cTSFamilyRelationshipArray(); // no game function: Release (0x4f3770) inlines it
    cTSFamilyRelationshipArray(const cTSFamilyRelationshipArray &) = delete;
    cTSFamilyRelationshipArray &operator=(const cTSFamilyRelationshipArray &) = delete;
    static cTSFamilyRelationshipArray *Create(); // 0x4f26e0 (cdecl, EH frame): new, null on failure

    // cIGZUnknown (every face; the secondary faces' slots 0-2 are the compiler's adjustor thunks, as in the original)
    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0x4f2f00 (ret 8)
    uint32_t AddRef() override;                              // slot 1, folded 0x146a40
    uint32_t Release() override;                             // slot 2, 0x4f3770

    // cITSFamilyRelationshipArray (+0)
    bool SetRelationship(uint16_t sim, ifaces::cITSFamilyRelationship *relation) override; // slot 3, 0x4f21a0 (ret 8): true if replaced
    bool RemoveRelationship(uint16_t sim) override;                  // slot 4, 0x4f38a0 (ret 4): true if removed
    bool IsDirty() override;                                         // slot 5, folded 0x16db30: the byte as it is
    void SetDirty() override;                                        // slot 6, 0x4f2e80
    ifaces::cITSFamilyRelationship *GetRelationship(uint16_t sim) override; // slot 7, 0x4f2db0 (ret 4): not counted
    void Unrelate(uint16_t sim, uint16_t other) override;            // slot 8, 0x4f3950 (ret 8)
    bool Relate(uint16_t sim, uint16_t other, int32_t role) override; // slot 9, 0x4f2280 (ret 0xc), role: FamilyTieType
    uint32_t Unused10() override;                                    // slot 10, folded 0x6c6b0: 0
    bool Unused11(uint32_t, uint32_t, uint32_t) override;            // slot 11, folded 0x1474a0: false
    uint32_t GetRefCount() override;                                 // slot 12, folded 0x149cf0 (+8 face slot 11: thunk 0x4f2dfc)
    bool ReadFrom(cIGZUnknown *source) override;                     // slot 13, 0x4f3b10 (ret 4)
    bool WriteTo(cIGZUnknown *target) override;                      // slot 14, 0x4f3c10 (ret 4)
    cIGZPersistResource *GetPersistResource() override;              // slot 15, folded 0x3a4f40: this ? this + 8 : 0
    bool Save() override;                                            // slot 16, 0x4f3c90: al only (callers use al)
    bool Clear() override;                                           // slot 17, 0x4f3d90

    // cITSDataContainer (+4; these run with this = the face, as in the original)
    bool CollectReferenced(ifaces::cITSDataImportExportContext *context) override; // slot 3, 0x4f27a0 (ret 4)
    bool Export(ifaces::cITSDataImportExportContext *context) override;            // slot 4, 0x4f3ff0 (ret 4)
    bool Import(ifaces::cITSDataImportExportContext *context) override;            // slot 5, 0x4f3300 (ret 4)

    // cIGZPersistResource (+8; all folded game functions shared with the other persist resources)
    cGZPersistResourceKey GetKey() override;                        // slot 3, folded 0x147790
    bool SetKey(const cGZPersistResourceKey &key) override;         // slot 4, folded 0x47d420
    uint32_t GetType() override;                                    // slot 5, folded 0x149cf0
    uint32_t GetGroup() override;                                   // slot 6, folded 0x149d00
    uint32_t GetInstance() override;                                // slot 7, folded 0x148ba0
    bool SetType(uint32_t type) override;                           // slot 8, folded 0x1eec40
    bool SetGroup(uint32_t group) override;                         // slot 9, folded 0x47d410
    bool SetInstance(uint32_t instance) override;                   // slot 10, folded 0x1f5270

    // cIGZSerializable (+0xc)
    bool Write(ifaces::cIGZOStream *stream) override; // slot 3, 0x4f3e70 (ret 4)
    bool Read(ifaces::cIGZIStream *stream) override;  // slot 4, 0x4f30b0 (ret 4)
    uint32_t GetGZCLSID() override;                   // slot 5, 0x4f2770

    // +0x00..+0x0c: the four vtable pointers (the interfaces above, in that order)
    uint32_t mRefCount;         // +0x10
    uint8_t mDirty;             // +0x14 a bool; kept a byte: the original stores/reads whole bytes and the difftests seed
    uint8_t mPad15[3];          //       it with other values
    cGZPersistResourceKey mKey; // +0x18 {kClassId, group, instance, 0} once saved; zero after construction
    FamilyRelationMap mRelations; // +0x28 sim -> relationship
};
static_assert(sizeof(cTSFamilyRelationshipArray) == 0x30, "cTSFamilyRelationshipArray layout");
static_assert(offsetof(cTSFamilyRelationshipArray, mRefCount) == 0x10, "reference count");
static_assert(offsetof(cTSFamilyRelationshipArray, mDirty) == 0x14, "dirty flag");
static_assert(offsetof(cTSFamilyRelationshipArray, mKey) == 0x18, "resource key");
static_assert(offsetof(cTSFamilyRelationshipArray, mRelations) == 0x28, "relation map");

// Where each interface sits in the object (the original's subobject offsets, RTTI): checked at run time by the tests
// (a base-class offset is not a constant expression), named here for the tests and the adapters.
namespace famrel {
constexpr uint32_t kContainerFace = 0x4;
constexpr uint32_t kPersistFace = 0x8;
constexpr uint32_t kSerialFace = 0xc;
} // namespace famrel

// The RVA hooks' entry points (src/hook/tables/cTSFamilyRelationshipArray.cpp). Every object the game creates carries our
// vtables, so these run only where the game calls an old entry point directly (the constructor, Create, GetGZCLSID) or
// holds an object built with its own vtables (none while the constructor hook is on). Each has the original's calling
// convention (this in ecx = the face the slot belongs to) and calls ours without virtual dispatch (the object may carry
// the game's vtables). Defined next to the functions they forward to, so the bodies are inlined into them.
// Slots whose originals are reached only through the game's vtables have no adapter (lane GJ): while the constructor hook
// is in, no object carries those vtables; their status entries say "in_effect": "callers" and the host traps the
// originals. RemoveRelationship, SetDirty and Clear keep theirs: their byte matches tie the RVA to the member through
// the adapter (tools/matchcheck.py).
struct cTSFamilyRelationshipArrayAdapters {
    using Self = cTSFamilyRelationshipArray;
    static Self *__fastcall Construct(void *memory, void *);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static bool __fastcall RemoveRelationship(Self *self, void *, uint16_t sim);
    static void __fastcall SetDirty(Self *self, void *);
    static bool __fastcall Clear(Self *self, void *);
    static uint32_t __fastcall GetGZCLSID(cIGZSerializable *face, void *);
};
