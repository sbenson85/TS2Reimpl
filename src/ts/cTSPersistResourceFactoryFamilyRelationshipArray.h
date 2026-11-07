#pragma once
#include "gz/cIGZPersistResourceFactory.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>

namespace ifaces {
class cIGZPersistDBRecord;
} // namespace ifaces

// cTSPersistResourceFactoryFamilyRelationshipArray: the persist resource factory of the FAMT resource (record type 0x8c870743):
// creates cTSFamilyRelationshipArray objects (src/ts/cTSFamilyRelationshipArray.h) and reads / writes them through the array's
// ReadFrom / WriteTo slots. RTTI: cIGZPersistResourceFactory (+0, vtable 0xdce194, 9 slots). 8 bytes: vtable, reference count.
// Created by 0x4f2740 (cdecl, constructor inline; registered by address in the cTSSimCOMDirector constructor 0x415ae0). RVAs
// 1.18.0.187; all /O2.
//
// Standalone shape (docs/standalone-shape.md): a real C++ class; the compiler emits its vtable (slot 7 the scalar deleting
// destructor, slot 8 GetRefCount) and RTTI. The game creates the factory only through 0x4f2740 (the vtable is referenced by it
// and by the destructor 0x4f2170 alone), which is hooked to Create, so every factory carries our vtable. The slot hooks are
// adapters (below). Slots 0-3 and 8 are folded bodies shared with other factories (0x1ee250, 0x148fb0, 0x1ee310, 0x431e60,
// 0x147a30): implemented here, never hooked.
class cTSPersistResourceFactoryFamilyRelationshipArray : public cIGZPersistResourceFactory {
public:
    static constexpr uint32_t kOriginalVTable = 0xdce194;
    static constexpr uint32_t kFactoryId = 0xa56b8f17;  // cIGZPersistResourceFactory (QueryInterface answers it and 1)
    static constexpr uint32_t kResourceId = 0x456b8f1d; // cIGZPersistResource: what slot 3 asks the new resource for first

    cTSPersistResourceFactoryFamilyRelationshipArray() : mRefCount(0) {} // inline in Create (0x4f2740)
    virtual ~cTSPersistResourceFactoryFamilyRelationshipArray();          // slot 7: scalar deleting destructor 0x4f2170 (ret 4)
    cTSPersistResourceFactoryFamilyRelationshipArray(const cTSPersistResourceFactoryFamilyRelationshipArray &) = delete;
    cTSPersistResourceFactoryFamilyRelationshipArray &operator=(const cTSPersistResourceFactoryFamilyRelationshipArray &) = delete;
    static cTSPersistResourceFactoryFamilyRelationshipArray *Create(); // 0x4f2740 (cdecl): new 8, null on failure

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, folded 0x1ee250 (ret 8): 1 and kFactoryId give this
    uint32_t AddRef() override;                              // slot 1, folded 0x148fb0
    uint32_t Release() override;                             // slot 2, folded 0x1ee310: at 1 or below, delete this (slot 7)
    bool CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4,
                        uint32_t a5) override;               // slot 3, folded 0x431e60 (ret 0x14, EH frame)
    bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4,
                              uint32_t a5) override;         // slot 4, 0x4f25d0 (ret 0x14, EH frame)
    bool Read(cIGZUnknown *resource, cIGZUnknown *record) override;  // slot 5, 0x4f3280 (ret 8, EH frame)
    bool Write(cIGZUnknown *resource, cIGZUnknown *record) override; // slot 6, 0x4f3f70 (ret 8, EH frame)
    virtual uint32_t GetRefCount();                                  // slot 8, folded 0x147a30

    // +0x00 the vtable pointer
    uint32_t mRefCount; // +0x04
};
static_assert(sizeof(cTSPersistResourceFactoryFamilyRelationshipArray) == 8, "new 8 in 0x4f2740");
static_assert(offsetof(cTSPersistResourceFactoryFamilyRelationshipArray, mRefCount) == 4, "reference count");

// The RVA hooks' entry points (src/hook/tables/cTSPersistResourceFactoryFamilyRelationshipArray.cpp): reached only through an
// object carrying the game's vtable (none while the Create hook is on). Original calling convention (this in ecx), our
// functions called without virtual dispatch, bodies inlined.
struct cTSPersistResourceFactoryFamilyRelationshipArrayAdapters {
    using Self = cTSPersistResourceFactoryFamilyRelationshipArray;
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
};
