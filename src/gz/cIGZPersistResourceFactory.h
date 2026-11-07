#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

namespace ifaces {
class cIGZPersistDBRecord;
} // namespace ifaces

// cIGZPersistResourceFactory (interface id 0xa56b8f17): creates resources of the record types it is registered for and
// reads / writes them from / to database records; for classes in the standalone shape (docs/standalone-shape.md) to derive
// from. The original's name; slots 0-6 as the facts give them (db/interface-slots.jsonl). Implementers add their own slots
// after slot 6 (a virtual deleting destructor at slot 7 in every factory seen so far, then class-specific slots), so the
// destructor is declared by each class, not here. Callers keep using the generated ifaces::cIGZPersistResourceFactory view.
// Implemented by cTSPersistResourceFactoryFamilyRelationshipArray.
class cIGZPersistResourceFactory : public cIGZUnknown {
public:
    // slot 3: a resource for the record's key type, asked for `riid`, then read from the record (ret 0x14; a4 / a5 unused)
    virtual bool CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4, uint32_t a5) = 0;
    // slot 4: a new resource of record type `type`, asked for `riid` (ret 0x14)
    virtual bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5) = 0;
    virtual bool Read(cIGZUnknown *resource, cIGZUnknown *record) = 0;  // slot 5 (ret 8)
    virtual bool Write(cIGZUnknown *resource, cIGZUnknown *record) = 0; // slot 6 (ret 8)
};
