#pragma once
#include "simantics/FindBestAction.h"

#include <cstdint>

// The library vector code the original instantiates for the person's vectors of ActionRecords (+0x2f8, +0x43c, +0x84c):
// the workers its callers reach as separate functions.

// A std::vector<ActionRecord> (std::allocator) as its three pointers.
struct ActionRecordVector {
    ActionRecord *mFirst;
    ActionRecord *mLast;
    ActionRecord *mEnd;
};

// 0x50f560 (256 bytes, __thiscall ret 4): std::vector<ActionRecord>::_Reallocate_exactly (reserve's worker): copies the
// records into a block of exactly *newCapacity, destroys the old ones (releasing their interactions) and frees the old
// block. cTSPerson::vf198 0x51da50 reserves 512 in the vector at +0x2f8 with it.
void __fastcall ActionRecordsReallocateExactly(ActionRecordVector *self, void *, const uint32_t *newCapacity);
