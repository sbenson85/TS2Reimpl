#pragma once
#include <cstdint>

// The strings that allocate through the allocator service (std::basic_string with the game's service allocator:
// GZServiceString). RVAs 1.18.0.187. Lane LS.
//
// 0x177110: the buffer allocation of their growth paths (the MSVC string's "allocate for capacity" member,
// instantiated with the service allocator; 43 callers, all the string members of that instantiation). __cdecl; the
// first argument (the allocator) is unused. The capacity is incremented for the terminator, that many bytes are
// allocated through slot 3 of the allocator service (cached in 0x10c95d4, guard 0x10c95d8: GetAllocatorService on
// first use), and the capacity is decremented again. Returns the block. If the service throws, the capacity is left
// incremented (as the original: no handler restores it).
struct GZServiceString {
    static char *__cdecl AllocateForCapacity(void *allocator, uint32_t *capacity); // 0x177110
};
