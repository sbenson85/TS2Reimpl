#pragma once
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cIGZPersistResource (interface id 0x456b8f1d): the GZ persistence key face of a resource, for classes in the standalone
// shape (docs/standalone-shape.md) to derive from. The original's name and slot order (global, as its RTTI). Callers of other
// objects keep using the generated ifaces::cIGZPersistResource view (same ABI for the slots it gets right).
// Implemented by cTSFamilyRelationshipArray (+8) and cTSPersistResKeyList (+4).
class cIGZPersistResource : public cIGZUnknown {
public:
    virtual cGZPersistResourceKey GetKey() = 0;                // slot 3 (hidden return pointer, ret 4, returns it in eax)
    virtual bool SetKey(const cGZPersistResourceKey &key) = 0; // slot 4
    virtual uint32_t GetType() = 0;                            // slot 5
    virtual uint32_t GetGroup() = 0;                           // slot 6
    virtual uint32_t GetInstance() = 0;                        // slot 7
    virtual bool SetType(uint32_t type) = 0;                   // slot 8
    virtual bool SetGroup(uint32_t group) = 0;                 // slot 9
    virtual bool SetInstance(uint32_t instance) = 0;           // slot 10
    virtual uint32_t GetRefCount() = 0;                        // slot 11
};
