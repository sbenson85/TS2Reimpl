#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cIGZSelfDescribing (interface id 0x51f85085): an object that can describe itself to a target, for classes in the standalone
// shape (docs/standalone-shape.md) to derive from. The original's name and slot order (global, as its RTTI). Implemented by
// cTSNeighborhoodInfo (+0xc); also a face of cTSLotInfo (+0x14) and cTSSimOutfitData (+8), still hook-shaped.
class cIGZSelfDescribing : public cIGZUnknown {
public:
    // slot 3 (ret 4): the implementers seen hand a text (the object's name) to the target's slot 16; true.
    virtual bool Describe(void *target) = 0;
    // slot 4 (ret 4): the folded `xor al, al; ret 4` (0x18a7a0) in the implementers seen: false. Purpose unknown.
    virtual bool DescribeMore(void *target) = 0;
};
