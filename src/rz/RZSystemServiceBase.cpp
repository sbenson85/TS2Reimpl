#include "rz/RZSystemServiceBase.h"

// cRZSystemService::QueryInterface, built for speed in the game (the rest of the class: RZSystemServiceBase.size.cpp).
namespace rz {

// 0x1b9650: the service interface is this (+0), counted through slot 1; anything else is cRZUnknownMT's (the +4 part,
// the folded 0x1b945c: id 1 only).
bool cRZSystemService::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cIGZSystemService::kIID) {
        // ORIGINAL-BUG: no null test on `out` (nor in cRZUnknownMT's id-1 case).
        *out = static_cast<cIGZSystemService *>(this);
        AddRef();
        return true;
    }
    // Never a byte match: the original tail-jumps to the folded 0x1b945c, ours to rz::cRZUnknownMT::QueryInterface.
    return cRZUnknownMT::QueryInterface(riid, out);
}

bool __fastcall cRZSystemServiceAdapters::QueryInterface(cRZSystemService *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->cRZSystemService::QueryInterface(riid, out);
}

} // namespace rz
