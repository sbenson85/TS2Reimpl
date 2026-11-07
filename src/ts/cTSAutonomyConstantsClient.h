#pragma once
#include "ts/cTSNeighborhoodConstants.h"

#include <cstdint>

// cTSAutonomyConstantsClient (vtable 0xdc2f18, 0xc bytes), the autonomy tuning-constant client built by cTSSimSystem
// (src/ts/cTSSimSystem.cpp). Only its deleting destructor is ours; slot 3 (0x5230f0, 2168 bytes) is still the original.
class cTSAutonomyConstantsClient {
public:
    static constexpr uint32_t kVTable = 0xdc2f18;
    static constexpr uint32_t kObjectSize = 0xc;

    // Slot 4, 0x417030 (ret 4): restores the vtable, runs the base destructor body and frees the object when flags & 1.
    // Returns this.
    cTSAutonomyConstantsClient *DeletingDestructor(uint32_t flags);
};
