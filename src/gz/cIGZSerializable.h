#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

// cIGZSerializable (interface id 0xe4fda3d4): a resource that writes / reads itself to a GZ stream, for classes in the
// standalone shape (docs/standalone-shape.md) to derive from. The original's name and slot order (global, as its RTTI).
// Implemented by cTSFamilyRelationshipArray (+0xc) and cTSPersistResKeyList (+8).
class cIGZSerializable : public cIGZUnknown {
public:
    virtual bool Write(ifaces::cIGZOStream *stream) = 0; // slot 3
    virtual bool Read(ifaces::cIGZIStream *stream) = 0;  // slot 4
    virtual uint32_t GetGZCLSID() = 0;                   // slot 5
};
