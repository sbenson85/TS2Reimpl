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

// cTSGameTipResourceFactory (lane KX): the persist resource factory of the game tips (record type 0xb1827a47): creates
// cTSGameTip objects (src/ts/cTSGameTip.h) and has them read / write themselves (cIGZSerializable) on a record's stream.
// RTTI: cGZPersistResourceFactory, cIGZPersistResourceFactory, cIGZUnknown, all at +0 (vtable 0xeb76ac, 12 slots). 0x10
// bytes: vtable, reference count, lock (cGZPersistResourceFactory's layout; the class adds nothing). RVAs 1.18.0.187; /O2.
//
// Standalone shape (docs/standalone-shape.md): the game makes the factory only in cTSGameTipService::Init (0xcb6170: new
// 0x10, the base constructor 0x9b6010 inline-called, then the vtable), ours once the service's factory is hooked; the
// vtable 0xeb76ac is referenced by nothing else. Slots 0-6 are cGZPersistResourceFactory's (ours, src/gz/
// cGZPersistResourceFactory.h, old shape with this layout): our slots run those members on this object, whose virtual
// calls (slots 7, 9, 10, 11) come back to our vtable. Slot 7 (the base's deleting destructor, folded 0x9b3010) and slot 8
// (folded 0x1f62f0) are shared bodies: implemented here, never hooked.
class cTSGameTipResourceFactory : public cIGZPersistResourceFactory {
public:
    static constexpr uint32_t kOriginalVTable = 0xeb76ac;
    static constexpr uint32_t kFactoryId = 0x318277de;  // what the service registers it under
    static constexpr uint32_t kIStreamId = 0x051b11bd;  // cIGZIStream: asked of the serial record by ReadRecord
    static constexpr uint32_t kOStreamId = 0xc51b11cb;  // cIGZOStream: by WriteRecord

    cTSGameTipResourceFactory();          // inline in 0xcb6170: count 0, the lock built
    virtual ~cTSGameTipResourceFactory(); // slot 7: the base's scalar deleting destructor (folded 0x9b3010; the lock)
    cTSGameTipResourceFactory(const cTSGameTipResourceFactory &) = delete;
    cTSGameTipResourceFactory &operator=(const cTSGameTipResourceFactory &) = delete;

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
    virtual bool CreateResource(uint32_t type, uint32_t riid, void **out);         // slot 9, 0xcb5bb0 (ret 0xc)
    virtual bool ReadRecord(cIGZPersistResource *resource, cIGZUnknown *record);  // slot 10, 0xcb6c20 (ret 8)
    virtual bool WriteRecord(cIGZPersistResource *resource, cIGZUnknown *record); // slot 11, 0xcb6cf0 (ret 8)

    // +0x00 the vtable pointer
    uint32_t mRefCount;       // +0x04
    cRZCriticalSection mLock; // +0x08
};
static_assert(sizeof(cTSGameTipResourceFactory) == 0x10, "new 0x10 in the service's Init");
static_assert(offsetof(cTSGameTipResourceFactory, mRefCount) == 4 && offsetof(cTSGameTipResourceFactory, mLock) == 8,
              "cGZPersistResourceFactory's layout");
