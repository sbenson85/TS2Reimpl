#pragma once
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZPersistResourceFactory.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>

namespace ifaces {
class cIGZPersistDBRecord;
} // namespace ifaces

// cTSEffectsResourceFactory: the persist resource factory of the effects resource (record type 0xea5118b0, lane IT). RTTI:
// cGZPersistResourceFactory, cIGZPersistResourceFactory, cIGZUnknown, all at +0 (vtable 0xe8e3e8, 12 slots). 0x10 bytes:
// vtable, reference count, lock (cGZPersistResourceFactory's layout). Created by Create 0xbd2ff0 (cdecl; registered by the
// cTSEffectsCOMDirector constructor 0xbb4710) and inline by cTSEffectsManager::Init (0xbc8fa0: new 0x10, then the
// constructor 0xbd2c50). RVAs 1.18.0.187; all /O2.
//
// Standalone shape (docs/standalone-shape.md): with Create and the constructor hooked, every factory carries our vtable
// (0xe8e3e8 is referenced by the constructor and destructor alone). Slots 0-6 are cGZPersistResourceFactory's (ours,
// src/gz/cGZPersistResourceFactory.h, old shape with this layout): our slots run those members on this object, whose virtual
// calls (slots 7, 9, 10, 11) come back to our vtable. Slot 8 is the shared body 0x1f62f0 (slot 3 with a4 = a5 = 0):
// implemented here, never hooked. The hooks of slots 7 and 9-11 are adapters.
class cTSEffectsResourceFactory : public cIGZPersistResourceFactory {
public:
    static constexpr uint32_t kOriginalVTable = 0xe8e3e8;
    static constexpr uint32_t kIStreamId = 0x051b11bd; // cIGZIStream: asked of the serial record by ReadRecord
    static constexpr uint32_t kOStreamId = 0xc51b11cb; // cIGZOStream: by WriteRecord

    cTSEffectsResourceFactory();          // 0xbd2c50: count 0, the lock built
    virtual ~cTSEffectsResourceFactory(); // 0xbd2e30: the lock destroyed; slot 7 the scalar deleting destructor 0xbd2e80
    cTSEffectsResourceFactory(const cTSEffectsResourceFactory &) = delete;
    cTSEffectsResourceFactory &operator=(const cTSEffectsResourceFactory &) = delete;
    static cTSEffectsResourceFactory *Create(); // 0xbd2ff0 (cdecl): new 0x10, null on failure

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0 (the base's 0x9b61d0)
    uint32_t AddRef() override;                              // slot 1 (0x9b60b0)
    uint32_t Release() override;                             // slot 2 (0x9b6290)
    bool CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4,
                        uint32_t a5) override;               // slot 3 (0x9b60e0)
    bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4,
                              uint32_t a5) override;         // slot 4 (0x9b61b0)
    bool Read(cIGZUnknown *resource, cIGZUnknown *record) override;  // slot 5 (0x9b6200)
    bool Write(cIGZUnknown *resource, cIGZUnknown *record) override; // slot 6 (0x9b62d0)
    virtual bool CreateInstance3(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out); // slot 8 (shared 0x1f62f0)
    virtual bool CreateResource(uint32_t type, uint32_t riid, void **out);         // slot 9, 0xbd2ed0 (ret 0xc)
    virtual bool ReadRecord(cIGZPersistResource *resource, cIGZUnknown *record);  // slot 10, 0xbd4030 (ret 8)
    virtual bool WriteRecord(cIGZPersistResource *resource, cIGZUnknown *record); // slot 11, 0xbd40e0 (ret 8)

    // +0x00 the vtable pointer
    uint32_t mRefCount;       // +0x04
    cRZCriticalSection mLock; // +0x08
};
static_assert(sizeof(cTSEffectsResourceFactory) == 0x10, "new 0x10 in 0xbd2ff0");
static_assert(offsetof(cTSEffectsResourceFactory, mRefCount) == 4 && offsetof(cTSEffectsResourceFactory, mLock) == 8,
              "cGZPersistResourceFactory's layout");

// The RVA hooks' entry points (src/hook/tables/cTSEffectsResourceFactory.cpp): reached only through an object carrying the
// game's vtable (none while the Create and constructor hooks are on). Original calling convention (this in ecx), our
// members called without virtual dispatch, bodies inlined.
struct cTSEffectsResourceFactoryAdapters {
    using Self = cTSEffectsResourceFactory;
    static Self *__fastcall Construct(void *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall CreateResource(Self *self, void *, uint32_t type, uint32_t riid, void **out);
    static bool __fastcall ReadRecord(Self *self, void *, cIGZPersistResource *resource, cIGZUnknown *record);
    static bool __fastcall WriteRecord(Self *self, void *, cIGZPersistResource *resource, cIGZUnknown *record);
};
