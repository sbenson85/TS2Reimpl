#pragma once
#include "simantics/cTSBehaviorTree.h"

#include <cstdint>

// A vector of reference-counted interface pointers whose storage comes from the game's allocator service (the memory
// functions RZMemory::Malloc / Free), such as the person's pending interactions (+0x840). Same layout as RefPtrVector (begin,
// end, capacity limit); the elements are cIGZUnknown-style objects (slot 1 AddRef, slot 2 Release, null allowed).
// Lane AH. RVAs are 1.18.0.187.
struct ServiceRefVector : RefPtrVector {
    // push_back of a counted pointer, as the game's callers write it out: with room left the pointer is stored and counted
    // in place, else InsertGrown makes the room.
    void PushBack(void *object);

    // 0x1d57e0 (__thiscall, ret 8): std::vector<counted pointer>::_Emplace_reallocate for the allocator service: puts a copy
    // of *value at `where` into a bigger block (1.5 times the capacity, at least one more element), moves the old elements
    // over by copying them (each is counted again) and releases the old block's elements and frees the block. Returns the
    // address of the new element. If counting throws while the new block is filled, what it holds is released and the block is
    // freed before the error goes on.
    void **InsertGrown(void **where, void *const *value);

    // 0x38f140 (__thiscall, noexcept): releases every element and frees the block (the allocator service), all pointers zero.
    // Nothing happens to a vector that has no block.
    void ReleaseAndFree() noexcept;

    // 0x41d6e0 (__thiscall, ret 8; lane FU, src/simantics/ServiceRefVectorAssign.cpp): std::vector<counted pointer>::
    // _Assign_counted_range, the vector becomes a copy of [first, first + count). See the .cpp.
    void AssignCounted(void *const *first, uint32_t count);
};

static_assert(sizeof(ServiceRefVector) == 12, "ServiceRefVector layout");

// A vector of plain values (no destructors) whose storage comes from the allocator service: three pointers (begin, end,
// capacity limit).
struct ServiceBlockVector {
    void *mFirst;
    void *mLast;
    void *mEnd;

    // 0x195720 (__thiscall, noexcept): frees the block and zeroes the three pointers; nothing happens when there is no block.
    void FreeBlock() noexcept;
};

static_assert(sizeof(ServiceBlockVector) == 12, "ServiceBlockVector layout");
