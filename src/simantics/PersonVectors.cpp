// The library vector workers of the person's ActionRecord vectors (see PersonVectors.h).
#include "simantics/PersonVectors.h"

#include <memory>

// ---------------------------------------------------------------------------------------------------------------------
// 0x50f560, std::vector<ActionRecord>::_Reallocate_exactly
// ---------------------------------------------------------------------------------------------------------------------

void __fastcall ActionRecordsReallocateExactly(ActionRecordVector *self, void *, const uint32_t *newCapacity)
{
    std::allocator<ActionRecord> allocator;
    const size_t capacity = *newCapacity;
    const size_t size = static_cast<size_t>(self->mLast - self->mFirst);
    ActionRecord *block = allocator.allocate(capacity); // bad_array_new_length beyond max_size, as the library's
    try {
        std::uninitialized_copy(self->mFirst, self->mLast, block);
    } catch (...) {
        allocator.deallocate(block, capacity);
        throw;
    }
    if (self->mFirst) {
        std::destroy(self->mFirst, self->mLast);
        allocator.deallocate(self->mFirst, static_cast<size_t>(self->mEnd - self->mFirst));
    }
    self->mFirst = block;
    self->mLast = block + size;
    self->mEnd = block + capacity;
}
